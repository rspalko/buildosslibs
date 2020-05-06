/*****************************************************************************/
// File: kdu_v_compress.cpp [scope = APPS/V_COMPRESSOR]
// Version: Kakadu, V7.9
// Author: David Taubman
// Last Revised: 8 January, 2017
/*****************************************************************************/
// Copyright 2001, David Taubman, The University of New South Wales (UNSW)
// The copyright owner is Unisearch Ltd, Australia (commercial arm of UNSW)
// Neither this copyright statement, nor the licensing details below
// may be removed from this file or dissociated from its contents.
/*****************************************************************************/
// Licensee: Open Systems Integration; Inc
// License number: 01368
// The licensee has been granted a NON-COMMERCIAL license to the contents of
// this source file.  A brief summary of this license appears below.  This
// summary is not to be relied upon in preference to the full text of the
// license agreement, accepted at purchase of the license.
// 1. The Licensee has the right to install and use the Kakadu software and
//    to develop Applications for the Licensee's own use.
// 2. The Licensee has the right to Deploy Applications built using the
//    Kakadu software to Third Parties, so long as such Deployment does not
//    result in any direct or indirect financial return to the Licensee or
//    any other Third Party, which further supplies or otherwise uses such
//    Applications.
// 3. The Licensee has the right to distribute Reusable Code (including
//    source code and dynamically or statically linked libraries) to a Third
//    Party, provided the Third Party possesses a license to use the Kakadu
//    software, and provided such distribution does not result in any direct
//    or indirect financial return to the Licensee.
/******************************************************************************
Description:
   File-based Motion JPEG2000 compressor application.
******************************************************************************/

#include <string.h>
#include <stdio.h> // so we can use `sscanf' for arg parsing.
#include <math.h>
#include <assert.h>
#include <iostream>
#include <fstream>
// Kakadu core includes
#include "kdu_arch.h"
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
// Application includes
#include "kdu_args.h"
#include "kdu_video_io.h" // App can write video to a raw video stream
#include "jpx.h"          // App can write video to a JPX file
#include "mj2.h"          // App can write video to an MJ2 file
#include "jpb.h"          // App can write video to a JPB file
#include "v_compress_local.h"

// The following header is included here only as a temporary hack to import
// the fast data transfer functions used in the internal implementation of
// the portable `kdu_stripe_compressor' API.  We will use the public
// interface itself in the future, but that requires stripping the tile
// processing logic out of this demo app, so for the moment we just use the
// hack.
#include "../support/stripe_compressor_local.h"

using namespace kdu_supp; // Also includes the `kdu_core' namespace


/* ========================================================================= */
/*                         Set up messaging services                         */
/* ========================================================================= */

class kdu_stream_message : public kdu_thread_safe_message {
  public: // Member classes
    kdu_stream_message(std::ostream *stream)
      { this->stream = stream; }
    void put_text(const char *string)
      { (*stream) << string; }
    void flush(bool end_of_message=false)
      { stream->flush();
        kdu_thread_safe_message::flush(end_of_message); }
  private: // Data
    std::ostream *stream;
  };

static kdu_stream_message cout_message(&std::cout);
static kdu_stream_message cerr_message(&std::cerr);
static kdu_message_formatter pretty_cout(&cout_message);
static kdu_message_formatter pretty_cerr(&cerr_message);


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                        print_version                               */
/*****************************************************************************/

static void
  print_version()
{
  kdu_message_formatter out(&cout_message);
  out.start_message();
  out << "This is Kakadu's \"kdu_v_compress\" application.\n";
  out << "\tCompiled against the Kakadu core system, version "
      << KDU_CORE_VERSION << "\n";
  out << "\tCurrent core system version is "
      << kdu_get_core_version() << "\n";
  out.flush(true);
  exit(0);
}

/*****************************************************************************/
/* STATIC                        print_usage                                 */
/*****************************************************************************/

static void
  print_usage(char *prog, bool comprehensive=false)
{
  kdu_message_formatter out(&cout_message);

  out << "Usage:\n  \"" << prog << " ...\n";
  out.set_master_indent(3);
  out << "-i <vix or yuv file>\n";
  if (comprehensive)
    out << "\tTo avoid over complicating this simple demonstration "
           "application, input video must be supplied as a VIX file or a "
           "raw YUV file.  Part-2 multi-component transforms can be used, "
           "but in this case you should read the discussion and examples "
           "which appear at the end of this usage statement for more "
           "information on the interaction between `Ssigned' and "
           "`Sprecision' values that you must supply and the values "
           "recovered from the source files.\n"
           "\t   If a raw YUV file is used, the dimensions, "
           "frame rate and format must be found in the filename itself, "
           "as a string of the form: \"<width>x<height>x<fps>x<format>\", "
           "where <width> and <height> are integers, <fps> is real-valued "
           "and <format> is one of \"422\", \"420\" or \"444\".  Any file "
           "with a \".yuv\" suffix, which does not contain a string of this "
           "form in its name, will be rejected.  VIX is a trivial "
           "non-standard video file format, consisting of a plain ASCI text "
           "header, followed by raw binary data.\n"
           "\t   VIX files commence with a text header, beginning with the "
           "3 character magic string, \"vix\", followed by a new-line "
           "character.  The rest of the header consists of a sequence of "
           "unframed tags.  Each tag commences with a tag identifier, inside "
           "angle quotes.  The final quoted tag must be \">IMAGE<\".  Besides "
           "the \"IMAGE\" tag, \"VIDEO\" and \"COLOUR\" tags are recognized.  "
           "Text inside tag bodies is free format, with regard to white "
           "space, but the \">\" character which introduces a new tag must "
           "be the first character on its line.\n"
           "\t   The \"VIDEO\" tag is followed by text strings containing "
           "the numeric value of the nominal frame rate (real-valued) and "
           "the number of frames (integer) -- the latter may be 0 if the "
           "number of frames is unknown.\n"
           "\t   The \"COLOUR\" tag must be followed by one of the two "
           "strings, \"RGB\" or \"YCbCr\".  If no \"Colour\" tag is present, "
           "an RGB colour space will be assumed, unless there are fewer "
           "than 3 image components.  For images with more than 3 components "
           "you will probably want to write a JPX file, providing custom "
           "colour space definitions and channel mappings via the "
           "`-jpx_layers' argument.\n"
           "\t   The \"IMAGE\" tag must be followed by a 4 token description "
           "of the numerical sample representation: 1) \"signed\" or "
           "\"unsigned\"; 2) \"char\", \"word\" or \"dword\"; 3) the number "
           "of bits (bit-depth) from each sample's byte, word or dword which "
           "are actually used; and 4) \"little-endian\" or \"big-endian\".  "
           "If the bit-depth token (item 3 above) is prefixed with an `L' "
           "the bits used are drawn from the LSB's of each sample's byte, "
           "word or dword -- this option is never used by other Kakadu "
           "utilities when writing VIX files.  Otherwise, the bits used "
           "are drawn from the MSB's of each sample's byte, word or dword.  "
           "The four tokens described above are followed by a 3 token "
           "description of the dimensions of each video frame: "
           "1) canvas width; 2) canvas height; and 3) the number of image "
           "components.  Finally, horizontal and vertical sub-sampling "
           "factors (relative to the canvas dimensions) are provided for "
           "each successive component; these must be integers in the range "
           "1 to 255.\n"
           "\t   The actual image data follows the new-line character "
           "which concludes the \"IMAGE\" tag's last sub-sampling factor.  "
           "Each video frame appears component by component, without any "
           "framing or padding or any type.\n";
  out << "-fields (normal|reversed)\n";
  if (comprehensive)
    out << "\tBy default, the source video content is considered to be "
           "progressive.  This argument allows you to identify the content "
           "as interlaced, with either \"normal\" field ordering "
           "(i.e., the field with the top scan line appears first in each "
           "frame) or \"reversed\" field ordering.  This information "
           "affects the generation of MJ2 files, JPX files and elementary "
           "broadcast streams (JPB files).  In the case of JPX files, "
           "interlaced video is written as two presentation tracks, "
           "each labeled with an appropriate text label.\n";
  out << "-frate <ticks per composite frame>,<ticks per second>\n";
  if (comprehensive)
    out << "\tBy default, frame rate information is derived from the source "
           "file, if possible.  However, this argument allows you to "
           "override such information and provide very a high precision "
           "specification of the frame rate, as a rational number.  "
           "The argument takes a comma-separated pair of positive integer "
           "parameters, neither of which may exceed 65535, such that the "
           "frame rate has the precise value: "
           "<ticks per second>/<ticks per composite frame>.  Note that "
           "for interlaced video, this value is a true frame rate (as "
           "opposed to a field rate).  All other references to \"frame\" "
           "in the command-line arguments and output statements "
           "generated by this application actually refer to fields.  Also, "
           "the `-subsample' argument has no impact over this overriding "
           "frame rate, whereas it does modify the frame rate reported by "
           "the source file.\n"
           "\t   It is worth noting that the precise frame rate for "
           "NTSC video should be given as \"-frate 1001,30000\".\n";
  out << "-jpb_data <colour>,<max bit-rate in Mbits/s>[,NDF|DF1|DF4][,FF2]\n";
  if (comprehensive)
    out << "\tThis argument is both valid and required if and only if "
           "an elementary broadcast stream is to be generated -- i.e., if "
           "the \"-o\" argument is used with a file name whose suffix is "
           "\".jpb\" or \".jpb2\".  The argument provides two pieces of "
           "mandatory frame metadata that are required for that output "
           "format.  These two pieces of metadata are provided via a "
           "comma-separated pair of integer parameters.  The first "
           "parameter is a broadcast colour space identifier (0 to 5), "
           "while the second is a maximum bit-rate, expressed in Mbits/s, "
           "which will be combined with the frame/field rate (see `-frate') "
           "to obtain a limit on the size of each compressed codestream.\n"
           "\t   Valid colour indices (first parameter) are: 0 (unknown); "
           "1 (SRGB); 2 (Rec. ITU-R BT.601-6); 3 (Rec. ITU-R BT.709-5); "
           "4 (a colour space defined in Annex M of IS15444-1); or "
           "5 (XYZ as used in Digital Cinema).\n"
           "\t   The maximum bit-rate parameter should agree with the "
           "codestream profiles that are set up via the `Sprofile' "
           "and `Sbroadcast' parameter attributes.  Upper bounds for this "
           "parameter are as follows: Digital Cinema profiles -> 244; "
           "Broadcast profile levels 1,2,3 -> 200; Broadcast profile "
           "level 4 -> 400; Broadcast profile level 5 -> 800; Broadcast "
           "profile level 6 -> 1600; and Broadcast profile level 7 -> "
           "no limit.  Note that profiles affect other attributes besides "
           "the maximum bit-rate; note also that if no profile is specified, "
           "the profile is automatically set to broadcast, level 1.\n"
           "\t   The maximum bit-rate (second parameter) establishes an "
           "upper bound for the size of each picture's compressed "
           "codestream.  If the bound is exceeded (depends on "
           "your choice of -rate or -slope arguments), an informative "
           "error message will be generated.  To ensure that violation "
           "does not occur, you can (and should) generally use the "
           "`Creslength' parameter attribute to good effect.  Note, "
           "however, that `Creslength' controls the total size of "
           "the actual compressed data (JPEG2000 packets and packet headers), "
           "but does not account for the impact of codestream headers and "
           "marker segments.  You can figure out the size of these headers "
           "(generally fixed, for a given frame size and given coding "
           "and organizational parameter attributes) by looking at the "
           "summary information printed when this application finishes.\n"
           "\t   These mandatory integer parameters may be followed by "
           "one or two optional comma-separated fields that control timecode "
           "generation.  These fields must match one of the following "
           "text strings: \"NDF\" means avoid drop-frame timecode "
           "generation even for non-integer frame-rates, in which case the "
           "timecode clock may run more slowly than the true frame clock; "
           "\"DF1\" and \"DF4\" mean drop one (resp. 4) frame-counts at a "
           "time rather than the default 2 frame counts, when drop-frame "
           "timecode generation is called for (this may be appropriate for "
           "60Hz progressive video with an NTSC timebase); and \"FF2\" means "
           "consecutive pairs of frames should share the same time code -- "
           "i.e., the FF field increments only on every second frame, which "
           "may be appropriate for 50Hz or 60Hz progressive video.\n";
  out << "-frames <max frames to process>\n";
  if (comprehensive)
    out << "\tBy default, all available input frames are processed.  This "
           "argument may be used to limit the number of frames which are "
           "actually processed.  If the `-fields' argument was used to "
           "specify that the content is interlaced, the value supplied "
           "to this argument actually refers to fields, rather than "
           "frames.  Moreover, in this case, you are advised to ensure "
           "that the number of frames (actually fields) is a multiple "
           "of 2.\n";
  out << "-subsample <temporal subsampling factor>\n";
  if (comprehensive)
    out << "\tYou may use this argument to subsample the input frame "
           "sequence, keeping only frames with index n*K, where K is "
           "the subsampling factor and frame indices start from 0.  Note "
           "that the \"frames\" here are input pictures, which might "
           "be individual fields if your source content is interlaced.\n";
  out << "-frame_reps <total number of times to compress each frame>\n";
  if (comprehensive)
    out << "\tThis argument is useful only when investigating the "
           "processing throughput achievable by the video compression "
           "implementation here.  Specifically, the argument passed to "
           "this function represents a number N > 0, such that each frame "
           "read from the video source is compressed N times.  Each "
           "iteration of the compression process is identical, including "
           "rate control properties and flushing of compressed content, "
           "except that the output is sent to a null compressed data "
           "target (discards everything) on all but the first iteration of "
           "each frame.  In this way, each compressed frame appears only "
           "once in the target file, but the throughput and processed frame "
           "count statistics reported by the application are based on the "
           "total number of frame compression iterations performed.  This "
           "means that you can factor out the impact of any I/O bottlenecks "
           "when estimating througput performance, simply by specifying a "
           "moderate to large value for N here.  This is reasonable, because "
           "in many applications the source frames and compressed output "
           "are not actually derived from or transferred to disk.\n"
           "\t   Note that this argument cannot be used together with "
           "`-subsample'.\n";
  out << "-o <MJ2, JPX, JPB or MJC (almost raw) compressed output file>\n";
  if (comprehensive)
    out << "\tIt is allowable to omit this argument, in which case all "
           "compression operations will be performed, but the result will "
           "not be written anywhere.  This can be useful for timing tests, "
           "since reading from and writing to the same disk may introduce "
           "large latencies.\n"
           "\t   Four types of compressed video files may be generated.  If "
           "the file has the suffix, \".mj2\" (or \".mjp2\"), the compressed "
           "video will be wrapped in the Motion JPEG2000 file format, which "
           "embeds all relevant timing and indexing information, as well as "
           "rendering hints to allow faithful reproduction and navigation by "
           "suitably equipped readers.\n"
           "\t  If the file has the suffix, \".jpx\" (or \".jpf\"), the "
           "compressed video will be written to the end of a JPX file that "
           "is formed by copying the JPX file supplied via the `-jpx_prefix' "
           "argument.  The prefix file must have a composition box.  "
           "Typically, the prefix file will define one composited frame "
           "that serves as a \"front cover image\", to be followed by the "
           "video content generated here.  You may be interested in further "
           "customizing the generated JPX file using the optional "
           "`-jpx_layers' and/or `-jpx_labels' arguments.\n"
           "\t  If the file has the suffix, \".jpb\" (or \".jpb2\"), the "
           "compressed video will be wrapped in an elementary broadcast "
           "stream, as described in Annex-M of IS15444-2 (after amendments).  "
           "Elementary broadcast streams consist of a sequence of frames, "
           "each wrapped in an Elementary Stream Marker super-box, each "
           "containing its own independent copy of the frame metadata "
           "(frame rate, colour space, field encoding, etc.), along with "
           "contiguous codestream boxes for each of the frame's fields.  To "
           "use this output file format, you must be sure to select a "
           "Digital Cinema or Broadcast profile for the codestreams "
           "themselves -- see `Sprofile' and `Sbroadcast' attributes.  "
           "Moreover, you will have to supply a \"-jpb_data\" argument.\n"
           "\t  Otherwise, the suffix must be \".mjc\" and the "
           "compressed video will be packed into a simple output file, "
           "having a 16 byte header, followed by the video "
           "image code-streams, each preceded by a four byte big-endian "
           "word, which holds the length of the ensuing code-stream, "
           "not including the length word itself.  The 16 byte header "
           "consists of a 4-character magic string, \"MJC2\", followed "
           "by three big-endian integers: a time scale (number of ticks "
           "per second); the frame period (number of time scale ticks); "
           "and a flags word, which is currently used to distinguish "
           "between YCC and RGB type colour spaces.\n";
  out << "-jpx_prefix <JPX prefix file>\n";
  if (comprehensive)
    out << "\tThis argument is required if the `-o' argument specifies a "
           "JPX target file.  The file identified here must be a JPX file "
           "that provides a Composition box and at least one composited "
           "frame.  The new file is written by appending an indefinitely "
           "repeated JPX container (Compositing Layer Extensions box) to "
           "a copy of the prefix file, after which the generated codestreams "
           "are written in an efficient way.\n";
  out << "-jpx_layers <space>,<components> [...]\n";
  if (comprehensive)
    out << "\tThis argument is recognized only when writing to a JPX file.  "
           "It allows you to override the default assignment of codestream "
           "image components to colour channels and the default colour "
           "space selection.  Even more interesting, the argument allows "
           "you to create multiple compositing layers for each compressed "
           "codestream, corresponding to different ways of viewing the "
           "image components -- these might be built from the output "
           "channels of a multi-component transform, for example.  Each "
           "such compositing layer that you define is assigned its own "
           "presentation track so a user can conveniently select the "
           "desired format.  Later, you can use \"kdu_show\" if you like "
           "to add metadata labels, links and so forth to make navigation "
           "between presentation tracks even more convenient.\n"
           "\t   Each source codestream (video frame) is assigned one "
           "compositing layer (and hence one presentation track) for each "
           "parameter string token supplied to this argument; tokens are "
           "separated by spaces.  Each token commences with a colour space "
           "identifier, which is followed by a comma-separated list of "
           "image components from the codestream that are to be used for "
           "the colour channels.  Image component numbers start from 0; the "
           "number of listed image components must match the number of "
           "colours for the colour space.  The <space> parameter may be "
           "any of the following strings:"
           "\t\t`bilevel1', `bilevel2', `YCbCr1', `YCbCr2', `YCbCr3', "
           "`PhotoYCC', `CMY', `CMYK', `YCCK', `CIELab', `CIEJab', "
           "`sLUM', `sRGB', `sYCC', `esRGB', `esYCC', `ROMMRGB', "
           "`YPbPr60',  `YPbPr50'\n";
  out << "-jpx_labels <label prefix string>\n";
  if (comprehensive)
    out << "\tThis argument is provided mostly to enable testing and "
           "demonstration of Kakadu's ability to write auxiliary "
           "metadata on-the-fly while pushing compressed video to a JPX "
           "file.  In practice, Kakadu supports very rich metadata structures "
           "with links (cross-references), imagery and region of interest "
           "associations and much more, all of which can be written "
           "on-the-fly, meaning that as each frame becomes available from "
           "a live data source, the content can be compressed and auxiliary "
           "metadata can also be generated and written.  Moreover, this "
           "is done in such a way as to avoid polluting the top level (or "
           "any other level) of the file hierarchy with large flat lists of "
           "metadata boxes, since those can interfere with efficient random "
           "access to a remotely located file via JPIP.  The way Kakadu does "
           "this is to reserve space within the file for assembling "
           "hierarchical grouping boxes to contain the metadata.  There is "
           "no need to provide any hints to the system on how to reserve this "
           "space, because it learns as it goes.\n"
           "\t   The present argument generates a simple set of label strings "
           "(one for each compressed frame), associating them with the "
           "imagery.  Each label is formed by adding a numerical suffix to "
           "the supplied prefix string.  You can always edit the labels "
           "later using \"kdu_show\", but in a real application the "
           "labels might be replaced by timestamps, environmental data or "
           "even tracking regions of interest..\n";
  out << "-rate -|<bits/pel>,<bits/pel>,...\n";
  if (comprehensive)
    out << "\tOne or more bit-rates, expressed in terms of the ratio between "
           "the total number of compressed bits (including headers) per video "
           "frame, and the product of the largest horizontal and vertical "
           "image component dimensions.  A dash, \"-\", may be used in place "
           "of the first bit-rate in the list to indicate that the final "
           "quality layer should include all compressed bits.  Specifying a "
           "very large rate target is fundamentally different to using the "
           "dash, \"-\", because the former approach may cause the "
           "incremental rate allocator to discard terminal coding passes "
           "which do not lie on the rate-distortion convex hull.  This means "
           "that reversible compression might not yield a truly lossless "
           "representation if you specify `-rate' without a dash for the "
           "first rate target, no matter how large the largest rate target "
           "is.\n"
           "\t   If \"Clayers\" is not used, the number of layers is "
           "set to the number of rates specified here. If \"Clayers\" is used "
           "to specify an actual number of quality layers, one of the "
           "following must be true: 1) the number of rates specified here is "
           "identical to the specified number of layers; or 2) one, two or no "
           "rates are specified using this argument.  When two rates are "
           "specified, the number of layers must be 2 or more and intervening "
           "layers will be assigned roughly logarithmically spaced bit-rates. "
           "When only one rate is specified, an internal heuristic determines "
           "a lower bound and logarithmically spaces the layer rates over the "
           "range.\n"
           "\t   Note that from KDU7.2, the algorithm used to generate "
           "intermediate quality layers (as well as the lower bound, if not "
           "specified) has changed.  The new algoirthm introduces a constant "
           "separation between logarithmically expressed distortion-length "
           "slope thresholds for the layers.  This is every bit as useful "
           "but much more efficient than the algorithm employed by previous "
           "versions of Kakadu.\n"
           "\t   Note also that if `-accurate' is not specified, the default "
           "`-tolerance' value is 2%, meaning that the actual bit-rate(s) "
           "may be as much as 2% smaller than the specified target(s).  In "
           "most cases, specifying `-tolerance 0' is the best way to achieve "
           "more precise rate control; however, `-accurate' might also be "
           "required if the video content has large changes in "
           "compressibility between frames.\n"
           "\t   Note carefully that all bit-rates refer only to the "
           "code-stream data itself, including all code-stream headers, "
           "excepting only the headers produced by certain `ORG...' "
           "parameter attributes -- these introduce optional extra headers "
           "to realize special organization attributes.  The size of "
           "auxiliary information from the wrapping file format is not "
           "taken into account in the `-rate' limit.\n"
           "\t   If this argument is used together with `-slope', and any "
           "value supplied to `-slope' is non-zero (i.e., slope would "
           "also limit the amount of compressed data generated), the "
           "interpretation of the layer bit-rates supplied via this argument "
           "is altered such that they represent preferred lower bounds on "
           "the quality layer bit-rates that will be taken into account "
           "in the event that the distortion-length slopes specified directly "
           "via the `-slopes' argument lead to the generation of too little "
           "content for any given frame (i.e., if the frame turns out to be "
           "unexpectedly compressible).  Note, however, that the ability "
           "of the system to respect such lower bounds is limited by the "
           "number of bits generated by block encoding, which may depend "
           "upon quantization parameters, as well as the use of slope "
           "thresholds during block encoding.\n";
  out << "-slope <layer slope>,<layer slope>,...\n";
  if (comprehensive)
    out << "\tIf present, this argument provides rate control information "
           "directly in terms of distortion-length slope values.  In most "
           "cases, you would not also supply the `-rates' argument; however, "
           "if you choose to do so, the values supplied via the `-rates' "
           "argument will be re-interpreted as lower bounds (as opposed "
           "to upper bounds) on the quality layer bit-rates, to be "
           "considered if the distortion-length slopes supplied here lead "
           "to unexpectedly small amounts of compressed data.  See the "
           "description of `-rate' for a more comprehensive explanation of "
           "the interaction between `-rate' and `-slope'; the remainder "
           "of this description, however, assumes that `-slope' is "
           "supplied all by itself.\n"
           "\t   If the number of quality layers is  not "
           "specified via a `Qlayers' argument, it will be deduced from the "
           "number of slope values.  Slopes are inversely related to "
           "bit-rate, so the slopes should decrease from layer to layer.  The "
           "program automatically sorts slopes into decreasing order so you "
           "need not worry about getting the order right.  For reference "
           "we note that a slope value of 0 means that all compressed bits "
           "will be included by the end of the relevant layer, while a "
           "slope value of 65535 means that no compressed bits will be "
           "included in the  layer.\n";
  out << "-tolerance <percent tolerance on layer sizes given using `-rate'>\n";
  if (comprehensive)
    out << "\tThis argument affects the behaviour of the `-rate' argument "
           "slightly, providing a tolerance specification on the achievement "
           "of the cumulative layer bit-rates given by that argument.  It "
           "has no effect if layer construction is controlled using the "
           "`-slope' argument.  The rate allocation algorithm "
           "will attempt to find a distortion-length slope such that the "
           "bit-rate, R_L, associated with layer L is in the range "
           "T_L*(1-tolerance/100) <= R_L <= T_L, where T_L is the target "
           "bit-rate, which is the difference between the cumulative bit-rate "
           "at layer L and the cumulative bit-rate at layer L-1, as specified "
           "in the `-rate' list.  Note that the tolerance is given as a "
           "percentage, that it affects only the lower bound, not the upper "
           "bound on the bit-rate, and that the default tolerance is 2%, "
           "except where `-accurate' is specified, in which case the "
           "default tolerance is 0.  The lower bound associated with the "
           "rate tolerance might not be achieved if there is insufficient "
           "coded data (after quantization) available for rate control -- in "
           "that case, you may need to reduce the quantization step sizes "
           "employed, which is most easily done using the `Qstep' "
           "attribute.\n";
  out << "-trim_to_rate -- use rate budget as fully as possible\n";
  if (comprehensive)
    out << "\tThis argument is relevant only when `-rate' is used for rate "
           "control, in place of `-slope', and only when `-accurate' is not "
           "specified and `-tolerance' is not set to 0.  Under these "
           "circumstances, the default behaviour is to find distortion-length "
           "slope thresholds that achieve the `-rate' objectives (to within "
           "the specified `-tolerance') and to truncate encoded block "
           "bit-streams based on these thresholds.  If this argument is "
           "specified, however, one additional coding pass may be included "
           "from some code-blocks in the final quality layer, so as to use "
           "up as much of the available `-rate' budget as possible, for each "
           "individual frame.  If `-accurate' is specified, or if "
           "`-tolerance' is set to 0, the default behaviour is modified so "
           "that trimming occurs automatically.\n";
  out << "-flush_period <image lines between flushes>\n";
  if (comprehensive)
    out << "\tThis argument allows you to arrange for the coded data "
           "in each frame to be progressively flushed to the output while "
           "the compression is proceeding in that frame.  Normally, this is "
           "not required, because video frames are not enormous and the "
           "flushing of codestream data is already overlapped with generation "
           "of the next frame.  The option requires either tiling, or else "
           "careful choice of precinct dimensions, together with a suitable "
           "packet progression sequence such as `Corder=PCRL'.  These "
           "conditions are outlined more carefully in the \"kdu_compress\" "
           "demo-app, which offers the same capability for single images.  "
           "The option is only supported if the output file has been "
           "configured to allow overlapped writing to multiple frames, which "
           "is the case for the MJC files, or if the `-disjoint_frames' "
           "option or a single-threaded deployment is used.  You may find "
           "this option interesting when used in conjunction with the "
           "`-proc_limit' argument, since it allows flush statistics "
           "collected in one frame to be made available to the next frame, "
           "even when the frame generation processes are overlapped, which "
           "is the most efficient (and default) arrangement with multiple "
           "threads.  Note that the flush period is just a guideline that "
           "governs the frequency with which background flushing jobs are "
           "scheduled.  If the `Scbr' option has been used to introduce "
           "constant-bit-rate flushing processes, the actual flushing "
           "operations fully respect the CBR guidelines regardless of how or "
           "when they are actually invoked.\n";
  out << "-accurate -- slower, slightly more reliable rate control\n";
  if (comprehensive)
    out << "\tThis argument is relevant only when `-rate' is used for rate "
           "control, in place of `-slope'.  By default, distortion-length "
           "slopes derived during rate control for the previous frame, are "
           "used to inform the block encoder of a lower bound on the "
           "distortion-length slopes associated with coding passes it "
           "produces.  This allows the block coder to stop before processing "
           "all coding passes, saving time.  The present argument may be "
           "used to disable this feature, which will slow the compressor "
           "down (except during lossless compression), but may improve the "
           "reliability of the rate control slightly.  Specifying `-accurate' "
           "also causes the rate control `-tolerance' to default to 0 and "
           "forces the `-trim_to_rate' feature to be used.\n";
  out << "-proc_limit <sugg_passes>,<start_plane>,<extra_start_passes>\n";
  if (comprehensive)
    out << "\tThis argment is most useful (perhaps only useful) when used "
           "in conjunction with the FAST block coding algorithm (Ctyp=FAST).  "
           "By default, all coding passes will be generated, up to a dynamic "
           "limit based on the distortion-length slope threshold selected in "
           "previous frames, which may or may not be reliable.  The argument "
           "takes a comma-separated list of 3 integer parameters.  When these "
           "are supplied, the set of coding passes that are generated by the "
           "block encoding units is based on statistics derived from the "
           "encoding and rate constrained flushing of previous frames, "
           "interpreted relative to the supplied set of processing "
           "complexity constraints.\n"
           "\t   The block encoders attempt to generate only the number of "
           "coding passes suggested via the <sugg_passes> parameter, although "
           "the suggestion is likely to adjusted to a value no smaller than "
           "3, and 4 coding passes may be required to achieve good "
           "performance.  The actual coding passes that are generated are "
           "based, as mentioned, on the evidence collected from previous "
           "frames.\n"
           "\t   Since this is not possible for the first frame in a "
           "video sequence, for that frame the block encoders are encouraged "
           "to anchor the set of generated coding passes around the CLEANUP "
           "pass that corresponds to the second parameter, <start_plane>.  "
           "If <start_plane> is 0, this anchor point corresponds to the "
           "highest quality admitted by the quantized subband samples; if "
           "<start_plane> is 1, the anchor point corresponds to the next "
           "coarser bit-plane, representing an effective doubling in the "
           "quantization step sizes; and so forth.\n"
           "\t   You are strongly recommended to set the `Qweights' attribute "
           "to \"yes\" in order to make the complexity constraint mechanism "
           "effective when processing the first frame of a video sequence, or "
           "perhaps only processing a single image.  You should also choose "
           "`Qstep' carefully, keeping in mind the finest quantization you "
           "are likely to want.  For example, you might use a `Qstep' value "
           "that is smaller than the effective quantization you expect to be "
           "used most of the time, by a factor of say 4, supplying "
           "<start_plane>=2 in order to encourage the block encoders to "
           "spread their first-frame processing effort around this typical "
           "operating point.\n"
           "\t   The last parameter, <extra_start_passes>, may be used to "
           "relax the encoding complexity constraints for the very first "
           "frame, where no statistics are available from previously coded "
           "frames; the suggested number of generated coding passes is "
           "augmented by this value.\n";
  out << "-fastest -- use of 16-bit data processing as often as possible.\n";
  if (comprehensive)
    out << "\tThis argument causes image samples to be coerced into a "
           "16-bit fixed-point representation even if the "
           "numerical approximation errors associated with this "
           "representation would normally be considered excessive -- makes "
           "no difference unless the source samples have a bit-depth of "
           "around 13 bits or more (depends upon other coding conditions).\n";
  out << "-precise -- force the use of 32-bit data processing.\n";
  if (comprehensive)
    out << "\tThis option can be useful for extremely high quality, but not "
           "reversible compression, forcing the use of the most accurate "
           "available numerical processing operations for wavelet and "
           "multi-component transforms, even where there use might not "
           "appear warranted based on the precision of the original "
           "imagery.\n";
  out << "-double_buffering <stripe height>\n";
  if (comprehensive)
    out << "\tThis option is intended to be used in conjunction with "
           "`-num_threads'.  From Kakadu version 7, double buffering "
           "is activated by default in multi-threaded processing "
           "environments, but you can disable it by supplying 0 "
           "to this argument.\n"
           "\t   Without double buffering, DWT operations will all be "
           "performed by the single thread which \"owns\" the multi-threaded "
           "processing group.  For a small number of processors, this may "
           "be acceptable, or even optimal, since the DWT is generally quite "
           "a bit less CPU intensive than block encoding (which is always "
           "spread across multiple threads, if available) and synchronous "
           "single-threaded DWT operations may improve memory access "
           "locality.  However, even for a small number of threads, the "
           "amount of thread idle time can be reduced by specifying the "
           "`-double_buffering' option.  In this case, a certain number "
           "of image rows in each image component are actually double "
           "buffered, so that one set can be processed by colour "
           "transformation and data format conversion operations, while the "
           "other set is processed by the DWT analysis engines, which "
           "themselves drive the block encoding engines.  The number of rows "
           "in each component which are to be double buffered is known as the "
           "\"stripe height\", supplied as a parameter to this argument.  The "
           "stripe height can be as small as 1, but this may add a lot "
           "of thread context switching overhead.  For this reason, a stripe "
           "height in the range 8 to 64 is recommended.\n"
           "\t   The default policy for multi-threaded environments is to "
           "pass the special value of -1 to `kdu_multi_analysis' so that "
           "a good value will be selected automatically.\n";
  out << "-overlapped_frames -- allow overlapped processing of frames\n";
  if (comprehensive)
    out << "\tThis option can be used only in conjunction with "
           "multi-threaded processing.  In fact, it is selected by default "
           "(can be disabled using `-disjoint_frames') unless "
           "`-num_threads 0' is specified, or the platform offers only "
           "one physical/virtual CPU.\n"
           "\t   In multi-processor environments, with at least "
           "one thread per processor, you would hope to get a speedup in "
           "proportion to the number of available processors.  However, "
           "this cannot be realized unless image data reading and compressed "
           "data flushing operations for one frame are able to proceed while "
           "another is being processed.  The overlapped frame option achieves "
           "this by instantiating two sets of frame processing resources, "
           "using one to push in image data for compression, while "
           "video frame samples are being loaded for and previously "
           "compressed data are being written out from the other.  This "
           "usually keeps all threads active as often as possible.\n"
           "\t   With the `-overlapped_frames' option, the maximum throughput "
           "might be achieved with a `-num_threads' value set somewhere "
           "between the number of physical/virtual processors and this "
           "value plus 2 -- because up to two threads can be simultaneously "
           "blocked on disk I/O conditions (one while reading frame data and "
           "one while writing codestream contents).  For this reason, you "
           "may find it useful to play around with the `-num_threads' "
           "argument to obtain the best overall throughput on your "
           "platform.\n";
  out << "-disjoint_frames -- explicitly disable overlapped frames\n";
  if (comprehensive)
    out << "\tDisables the `-overlapped_frames' option, which is "
           "selected by default on platforms with multiple CPU resources, "
           "unless `-num_threads 0' is specified.  One reason you might "
           "want to do this is to obtain separate timing information for "
           "frame loading and frame compression processing via the "
           "`-cpu' option.\n";
  out << "-add_info -- causes the inclusion of layer info in COM segments.\n";
  if (comprehensive)
    out << "\tIf you specify this flag, a code-stream COM (comment) marker "
           "segment will be included in the main header of every "
           "codestream, to record the distortion-length slope and the "
           "size of each quality layer which is generated.  Since this "
           "is done for each codestream and there is one codestream "
           "for each frame, you may find that the size overhead of this "
           "feature is unwarranted.  The information can be of use for "
           "R-D optimized delivery of compressed content using Kakadu's "
           "JPIP server, but this feature is mostly of interest when "
           "accessing small regions of large images (frames).  Most "
           "video applications, however, involve smaller frame sizes.  For "
           "this reason, this feature is disabled by default in this "
           "application, while it is enabled by default in the "
           "\"kdu_compress\" still image compression application.\n";
  out << "-no_weights -- target MSE minimization for colour images.\n";
  if (comprehensive)
    out << "\tBy default, visual weights will be automatically used for "
           "colour imagery (anything with 3 compatible components).  Turn "
           "this off if you want direct minimization of the MSE over all "
           "reconstructed colour components.\n";
  siz_params siz; siz.describe_attributes(out,comprehensive);
  cod_params cod; cod.describe_attributes(out,comprehensive);
  qcd_params qcd; qcd.describe_attributes(out,comprehensive);
  rgn_params rgn; rgn.describe_attributes(out,comprehensive);
  poc_params poc; poc.describe_attributes(out,comprehensive);
  crg_params crg; crg.describe_attributes(out,comprehensive);
  org_params org; org.describe_attributes(out,comprehensive);
  mct_params mct; mct.describe_attributes(out,comprehensive);
  mcc_params mcc; mcc.describe_attributes(out,comprehensive);
  mco_params mco; mco.describe_attributes(out,comprehensive);
  nlt_params nlt; nlt.describe_attributes(out,comprehensive);
  atk_params atk; atk.describe_attributes(out,comprehensive);
  dfs_params dfs; dfs.describe_attributes(out,comprehensive);
  ads_params ads; ads.describe_attributes(out,comprehensive);
  out << "-s <switch file>\n";
  if (comprehensive)
    out << "\tSwitch to reading arguments from a file.  In the file, argument "
           "strings are separated by whitespace characters, including spaces, "
           "tabs and new-line characters.  Comments may be included by "
           "introducing a `#' or a `%' character, either of which causes "
           "the remainder of the line to be discarded.  Any number of "
           "\"-s\" argument switch commands may be included on the command "
           "line.\n";
  out << "-num_threads <0, or number of parallel threads to use>\n";
  if (comprehensive)
    out << "\tUse this argument to gain explicit control over "
           "multi-threaded or single-threaded processing configurations.  "
           "The special value of 0 may be used to specify that you want "
           "to use the conventional single-threaded processing "
           "machinery -- i.e., you don't want to create or use a "
           "threading environment.  Otherwise, you must supply a "
           "positive integer here, and the object will attempt to create "
           "a threading environment with that number of concurrent "
           "processing threads.  The actual number of created threads "
           "may be smaller than the number requested, if your "
           "request exceeds internal resource limits.  It is worth "
           "noting that \"-num_threads 1\" and \"-num_threads 0\" "
           "both result in single-threaded processing, although the "
           "former creates an explicit threading environment and uses "
           "it to schedule the processing steps, even if there is only "
           "one actual thread of execution.\n"
           "\t   As noted in connection with the `-overlapped_frames' "
           "option, you may obtain the highest overall throughput with "
           "overlapped frames and perhaps 1 or 2 more threads than the "
           "number of physical/virtual processors available on your "
           "platform, assuming there is no other significant work going "
           "on.\n";
  out << "-cpu -- collect CPU time statistics.\n";
  if (comprehensive)
    out << "\tThis option is always enabled unless `-quiet' is specified -- "
           "only then do you need to mention it on the command line to get "
           "reporting of processing time.  Exactly what CPU times can be "
           "reported depends upon whether or not the `-overlapped_frames' "
           "option has been selected (or not explicitly deselected via "
           "`-disjoint_frames').  If it is selected, only the overall "
           "processing time/throughput can be reported.  On the other hand, "
           "if frame processing is not overlapped, the time taken to load "
           "video frames into memory is reported separately from the time "
           "taken to process the in-memory frame buffers.\n";
  out << "-cbr_stats -- print summary CBR flushing stats, if appropriate\n";
  if (comprehensive)
    out << "\tIf the `Scbr' attribute has been specified with non-zero "
           "parameters, this argument causes CBR flushing statistics to be "
           "logged and summary information to be printed at the end.\n";
  out << "-cbr_trace <trace file>\n";
  if (comprehensive)
    out << "\tSimilar to `-cbr_stats', this function causes CBR flushing "
           "stats to be logged if the `Scbr' attribute has been specified "
           "with non-zero parameters, but this argument also causes a "
           "frame-by-frame trace to be written to the identified file.\n";
  out << "-quiet -- suppress informative messages.\n";
  out << "-version -- print core system version I was compiled against.\n";
  out << "-v -- abbreviation of `-version'\n";
  out << "-usage -- print a comprehensive usage statement.\n";
  out << "-u -- print a brief usage statement.\"\n\n";

  if (!comprehensive)
    {
      out.flush();
      exit(0);
    }

  out.set_master_indent(0);
  out << "Notes:\n";
  out.set_master_indent(3);
  out << "    Arguments which commence with an upper case letter (rather than "
         "a dash) are used to set up code-stream parameter attributes. "
         "These arguments have the general form:"
         "  <arg name>={fld1,fld2,...},{fld1,fld2,...},..., "
         "where curly braces enclose records and each record is composed of "
         "fields.  The type and acceptable values for the fields are "
         "identified in the usage statements, along with whether or not "
         "multiple records are allowed.  In the special case where only one "
         "field is defined per record, the curly braces may be omitted. "
         "In no event may any spaces appear inside an attribute argument.\n";
  out << "    Most of the code-stream parameter attributes take an optional "
         "tile-component modifier, consisting of a colon, followed by a "
         "tile specifier, a component specifier, or both.  The tile specifier "
         "consists of the letter `T', followed immediately be the tile index "
         "(tiles are numbered in raster order, starting from 0).  Similarly, "
         "the component specifier consists of the letter `C', followed "
         "immediately by the component index (starting from 0). These "
         "modifiers may be used to specify parameter changes in specific "
         "tiles, components, or tile-components.\n";
  out << "    If you do not remember the exact form or description of one of "
         "the code-stream attribute arguments, simply give the attribute name "
         "on the command-line and the program will exit with a detailed "
         "description of the attribute.\n";
  out << "    If SIZ parameters are to be supplied explicitly on the "
         "command line, be aware that these may be affected by simultaneous "
         "specification of geometric transformations.  If uncertain of the "
         "behaviour, use `-record' to determine the final compressed "
         "code-stream parameters which were used.\n";
  out << "    If you are compressing a 3 component image using the "
         "reversible or irreversible colour transform (this is the default), "
         "or where the image sample values are already known to be in "
         "a YCbCr colour space, the program will automatically introduce "
         "a reasonable set of visual weighting factors, unless you use "
         "the \"Clev_weights\" or \"Cband_weights\" options yourself.  "
         "This does not happen automatically in the case of single component "
         "images, which are optimized purely for MSE by default.  To see "
         "whether weighting factors were used, you may like to use the "
         "`-record' option.\n\n";
  
  out.set_master_indent(0);
  out << "Understanding Multi-Component Transforms:\n";
  out.set_master_indent(3);
  out << "   Kakadu supports JPEG2000 Part 2 multi-component "
    "transforms.  These features are used if you define the `Mcomponents' "
    "attribute to be anything other than 0.  In this case, `Mcomponents' "
    "denotes the number of multi-component transformed output components "
    "produced during decompression, with `Mprecision' and `Msigned' "
    "identifying the precision and signed/unsigned attributes of these "
    "components.  These parameters will be derived from the source files "
    "(non-raw files), or else they will be used to figure out the source "
    "file format (raw files).  When working with multi-component transforms, "
    "the term \"codestream components\" refers to the set of components "
    "which are subjected to spatial wavelet transformation, quantization "
    "and coding.  These are the components which are supplied to the input "
    "of the multi-component transform during decompression.  The number of "
    "codestream components is given by the `Scomponents' attribute, while "
    "their precision and signed/unsigned properties are given by `Sprecision' "
    "and `Ssigned'.  You should set these parameter attributes "
    "to suitable values yourself.  If you do not explicitly supply a value "
    "for the `Scomponents' attribute, it will default to the number of "
    "source components (image planes) found in the set of supplied input "
    "files.  The value of `Mcomponents' may also be larger than the number "
    "of source components found in the supplied input files.  In this case, "
    "the source files provide the initial set of image components which will "
    "be recovered during decompression.  This subset must be large enough to "
    "allow the internal machinery to invert the multi-component transform "
    "network, so as to recover a full set of codestream image components.  If "
    "not, you will receive a descriptive error message explaining what is "
    "lacking.\n";
  out << "   As an example, suppose the codestream image components "
    "correspond to the first N <= M principle components of an original "
    "set of M image components -- obtained by applying the KLT to, say, "
    "a hyperspectral data set.  To compress the image, you would "
    "probably want to supply all M original image planes.  However, you "
    "could supply as few as the first N original image planes.  Here, "
    "M is the value of `Mcomponents' and N is the value of `Scomponents'.\n";
  out << "   If there is no multi-component transform, `Scomponents' is the "
    "number of output and codestream components; it will be set to the "
    "number of source components found in the set of supplied input files.  "
    "`Sprecision' and `Ssigned' hold the bit-depth and signed/unsigned "
    "attributes of the image components.\n";
  out << "   From KDU-7.8, the `Ncomponents', `Nprecision' and `Nsigned' "
    "attributes provide means for defining the number, precision and "
    "signed/unsigned properties of the output image components (equivalently, "
    "the original input components to the compressor), in a manner that "
    "does not depend on whether or not there is a multi-component transform.  "
    "This mechanism also allows for the possibility that non-linear point "
    "transforms might appear between the original image samples and the "
    "multi-component output components or codestream components, changing "
    "the precision and/or signed/unsigned attributes yet again.  Where "
    "raw input files are used, without any precision information of their "
    "own, you should explicitly supply `Nprecision' and `Nsigned' values, "
    "allowing `Sprecision' and `Signed' and perhaps `Mprecision' and "
    "`Msigned' values to be derived automatically, unless you need to "
    "override them.  For non-raw input image formats, allow the internal "
    "machinery to set `Nprecision' and `Nsigned' attributes for you and "
    "override `Sprecision'/`Ssigned' or `Mprecision'/`Msigned' only if "
    "required by a non-linear point transform or multi-component transform "
    "you are interested in.\n";
  out << "   It is worth noting that the dimensions of the N=`Scomponents' "
    "codestream image components are assumed to be identical to those of the "
    "N source image components contained in the set of supplied input files.  "
    "This assumption is imposed for simplicity in this demonstration "
    "application; it is not required by the Kakadu core system.\n\n";
  
  out.flush();
  exit(0);
}

/*****************************************************************************/
/* STATIC                     parse_simple_args                              */
/*****************************************************************************/

static void
  parse_simple_arguments(kdu_args &args, char * &ifname, char * &ofname,
                         int &max_frames, int &frame_repeat,
                         int &subsampling_factor, kdu_field_order &fields,
                         int &double_buffering_stripe_height,
                         double &rate_tolerance, bool &trim_to_rate,
                         int &proc_limit_passes, int &proc_limit_start_plane,
                         int &proc_limit_extra_start_passes,
                         bool &no_slope_predict, bool &want_fastest,
                         bool &force_precise, bool &overlapped_frames,
                         int &flush_period, bool &no_info, bool &no_weights,
                         int &num_threads, bool &cpu, bool &cbr_stats,
                         char * &cbr_fname, bool &quiet)
  /* Parses most simple arguments (those involving a dash).  Note that
     `cbr_stats' is set to true if `-cbr_stats' or `-cbr_trace' is present, but
     `cbr_fname' is assigned a non-NULL file name only in the latter case. */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();      

  ifname = ofname = NULL;
  max_frames = INT_MAX;
  frame_repeat = 0;
  subsampling_factor = 1;
  fields = KDU_FIELDS_NONE;
  double_buffering_stripe_height = 0;
  rate_tolerance = 0.02;
  trim_to_rate = false;
  proc_limit_passes = 0;
  proc_limit_start_plane = 0;
  proc_limit_extra_start_passes = 0;
  no_slope_predict = false;
  want_fastest = false;
  force_precise = false;
  overlapped_frames = false;
  flush_period = 0;
  no_info = true;
  no_weights = false;
  num_threads = 0; // Not actually the default value -- see below
  cpu = false;
  cbr_stats = false;
  cbr_fname = NULL;
  quiet = false;

  if (args.find("-i") != NULL)
    {
      char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-i\" argument requires a file name!"; }
      ifname = new char[strlen(string)+1];
      strcpy(ifname,string);
      args.advance();
    }
  else
    { kdu_error e; e << "You must supply an input file name."; }

  if (args.find("-o") != NULL)
    {
      char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-o\" argument requires a file name!"; }
      ofname = new char[strlen(string)+1];
      strcpy(ofname,string);
      args.advance();
    }

  if (args.find("-frames") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&max_frames) != 1) ||
          (max_frames <= 0))
        { kdu_error e; e << "The `-frames' argument requires a positive "
          "integer parameter."; }
      args.advance();
    }

  if (args.find("-subsample") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&subsampling_factor) != 1) ||
          (subsampling_factor <= 0))
        { kdu_error e; e << "The `-subsample' argument requires a positive "
          "integer parameter."; }
      args.advance();
    }

  if (args.find("-frame_reps") != NULL)
    {
      if (subsampling_factor != 1)
        { kdu_error e; e << "The `-frame_reps' and `-subsample' options "
          "may not be used together."; }
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&frame_repeat) != 1) ||
          (frame_repeat < 1))
        { kdu_error e; e << "The `-frame_reps' argument requires a positive "
          "integer parameter, indicating the number of times each frame is "
          "to be compressed for throughput measurement purposes.\n"; }
      frame_repeat--; // Because `frame_repeat' is the number of repeats
      args.advance();
    }

  if (args.find("-fields") != NULL)
    { 
      char *string = args.advance();
      if ((string != NULL) && (strcmp(string,"normal") == 0))
        fields = KDU_FIELDS_TOP_FIRST;
      else if ((string != NULL) && (strcmp(string,"reversed") == 0))
        fields = KDU_FIELDS_TOP_SECOND;
      else
        { kdu_error e; e << "The `-fields' argument requires a string "
          "parameter, equal to one of \"normal\" or \"reversed\"."; }
      args.advance();
    }

  if (args.find("-accurate") != NULL)
    { 
      no_slope_predict = true;
      trim_to_rate = true;
      rate_tolerance = 0.0;
      args.advance();
    }
  
  if (args.find("-tolerance") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%lf",&rate_tolerance) != 1) ||
          (rate_tolerance < 0.0) || (rate_tolerance > 50.0))
        { kdu_error e; e << "\"-tolerance\" argument requires a real-valued "
          "parameter (percentage) in the range 0 to 50."; }
      rate_tolerance *= 0.01; // Convert from percentage to a fraction
      if (rate_tolerance == 0.0)
        trim_to_rate = true;
      args.advance();
    }
  
  if (args.find("-trim_to_rate") != NULL)
    { 
      trim_to_rate = true;
      args.advance();
    }
  
  if (args.find("-proc_limit") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d,%d,%d",&proc_limit_passes,
                  &proc_limit_start_plane,
                  &proc_limit_extra_start_passes) != 3) ||
          (proc_limit_passes <= 0) || (proc_limit_start_plane < 0) ||
          (proc_limit_extra_start_passes < 0))
        { kdu_error e; e << "\"-proc_limit\" argument requires a comma-"
          "separated list with three non-negative integer parameters, the "
          "first of which <sugg_passes> may not be 0.";
        }
      args.advance();
    }
  
  if (args.find("-fastest") != NULL)
    { 
      want_fastest = true;
      args.advance();
    }

  if (args.find("-precise") != NULL)
    { 
      force_precise = true;
      args.advance();
    }

  if (args.find("-add_info") != NULL)
    {
      no_info = false;
      args.advance();
    }

  if (args.find("-no_weights") != NULL)
    {
      no_weights = true;
      args.advance();
    }

  if (args.find("-num_threads") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&num_threads) != 1) ||
          (num_threads < 0))
        { kdu_error e; e << "\"-num_threads\" argument requires a "
          "non-negative integer."; }
      args.advance();
    }
  else if ((num_threads = kdu_get_num_processors()) < 2)
    num_threads = 0;

  if (args.find("-double_buffering") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d",&double_buffering_stripe_height) != 1) ||
          (double_buffering_stripe_height < 0))
        { kdu_error e; e << "\"-double_buffering\" argument requires a "
          "positive integer, specifying the number of rows from each "
          "component which are to be double buffered, or else 0 (see "
          "`-usage' statement)."; }
      args.advance();
    }
  else if (num_threads > 1)
    double_buffering_stripe_height = -1;
  
  if (args.find("-flush_period") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&flush_period) != 1) ||
          (flush_period < 1))
        { kdu_error e; e << "\"-flush_period\" argument requires a "
          "positive integer parameter."; }
      args.advance();
    }

  if (args.find("-overlapped_frames") != NULL)
    { 
      overlapped_frames = true;
      args.advance();
    }
  else if (num_threads > 1)
    overlapped_frames = true;
  
  if (args.find("-disjoint_frames") != NULL)
    { 
      overlapped_frames = false;
      args.advance();
    }

  if (args.find("-cpu") != NULL)
    { 
      cpu = true;
      args.advance();
    }
  
  if (args.find("-cbr_stats") != NULL)
    { 
      cbr_stats = true;
      args.advance();
    }
  
  if (args.find("-cbr_trace") != NULL)
    { 
      cbr_stats = true;
      char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-cbr_trace\" argument requires a file name!"; }
      cbr_fname = new char[strlen(string)+1];
      strcpy(cbr_fname,string);
      args.advance();
    }

  if (args.find("-quiet") != NULL)
    {
      quiet = true;
      args.advance();
    }
}

/*****************************************************************************/
/* STATIC                       parse_jpb_data                               */
/*****************************************************************************/

static int
  parse_jpb_data(kdu_args &args, int &frame_space, int &max_bitrate)
{
  int timeflags = JPB_TIMEFLAG_DF2;
  if (args.find("-jpb_data") != NULL)
    { // Frame metadata
      char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d,%d",&frame_space,&max_bitrate) != 2) ||
          (frame_space < 0) || (frame_space > 5) ||
          (max_bitrate < 1) || (max_bitrate > 4000))
        { kdu_error e; e << "The `-jpb_data' argument requires "
          "two comma-separated parameters, the first of "
          "which must be a colour space ID in the range 0 to 5, "
          "while the second must be an integer maximum bit-rate, in "
          "the range 1 to 4000 (measured in Mbits/s).";
        }
      string = strchr(1+strchr(string,','),',');
      while (string != NULL)
        { 
          string++;
          if ((toupper(string[0]) == (int) 'N') &&
              (toupper(string[1]) == (int) 'D') &&
              (toupper(string[2]) == (int) 'F') &&
              ((string[3] == '\0') || (string[3] == '\0')))
            { 
              timeflags &= ~(JPB_TIMEFLAG_DF2 | JPB_TIMEFLAG_DF4);
              timeflags |= JPB_TIMEFLAG_NDF;
            }
          else if ((toupper(string[0]) == (int) 'D') &&
                   (toupper(string[1]) == (int) 'F') &&
                   (string[2] == '2') &&
                   ((string[3] == '\0') || (string[3] == '\0')))
            { 
              timeflags &= ~(JPB_TIMEFLAG_NDF | JPB_TIMEFLAG_DF4);
              timeflags |= JPB_TIMEFLAG_DF2;
            }
          else if ((toupper(string[0]) == (int) 'D') &&
                   (toupper(string[1]) == (int) 'F') &&
                   (string[2] == '4') &&
                   ((string[3] == '\0') || (string[3] == '\0')))
            { 
              timeflags &= ~(JPB_TIMEFLAG_NDF | JPB_TIMEFLAG_DF2);
              timeflags |= JPB_TIMEFLAG_DF4;
            }
          else if ((toupper(string[0]) == (int) 'F') &&
                   (toupper(string[1]) == (int) 'F') &&
                   (string[2] == '2') &&
                   ((string[3] == '\0') || (string[3] == '\0')))
            timeflags |= JPB_TIMEFLAG_FRAME_PAIRS;
          else if ((toupper(string[0]) == (int) 'D') &&
                   (toupper(string[1]) == (int) 'F') &&
                   (string[2] == '2') &&
                   ((string[3] == '\0') || (string[3] == '\0')))
            timeflags &= ~(JPB_TIMEFLAG_NDF);
          else
            { kdu_error e; e << "Malformed parameter string supplied "
              "for `-jpb_data' argument.  Problem encountered at:\n\t\""
              << string << "\"\n";
            }
          string = strchr(string,',');
        }
      args.advance();
    }
  else
    { kdu_error e; e << "The `-jpb_data' argument is required "
      "because you have provided an output file with a \".jpb\" "
      "or \".jpb2\" suffix -- these are interpreted as "
      "files containing elementary broadcast streams, as "
      "defined in Annex M of IS15444-1 (with amendments).";
    }
  return timeflags;
}

/*****************************************************************************/
/* STATIC                      check_yuv_suffix                              */
/*****************************************************************************/

static bool
  check_yuv_suffix(const char *fname)
/* Returns true if the file-name has the suffix ".yuv", where the
   check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'y') || (*cp == 'Y'))
    return false;
  cp++;
  if ((*cp != 'u') && (*cp != 'U'))
    return false;
  cp++;
  if ((*cp != 'v') && (*cp != 'V'))
    return false;
  cp++;
  return (*cp == '\0');
}

/*****************************************************************************/
/* STATIC                      check_mj2_suffix                              */
/*****************************************************************************/

static bool
  check_mj2_suffix(const char *fname)
  /* Returns true if the file-name has the suffix, ".mj2" or ".mjp2", where the
     check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'm') && (*cp != 'M'))
    return false;
  cp++;
  if ((*cp != 'j') && (*cp != 'J'))
    return false;
  cp++;
  if (*cp == '2')
    return true;
  if ((*cp != 'p') && (*cp != 'P'))
    return false;
  cp++;
  if (*cp != '2')
    return false;
  cp++;
  return (*cp == '\0');
}

/*****************************************************************************/
/* STATIC                      check_jpx_suffix                              */
/*****************************************************************************/

static bool
  check_jpx_suffix(char *fname)
  /* Returns true if the file-name has the suffix, ".jpx" or ".jpf", where the
     check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'j') && (*cp != 'J'))
    return false;
  cp++;
  if ((*cp != 'p') && (*cp != 'P'))
    return false;
  cp++;
  if ((*cp != 'x') && (*cp != 'X') && (*cp != 'f') && (*cp != 'F'))
    return false;
  return true;
}

/*****************************************************************************/
/* STATIC                   check_broadcast_suffix                           */
/*****************************************************************************/

static bool
  check_broadcast_suffix(const char *fname)
/* Returns true if the file-name has the suffix, ".jpb" or ".jpb2", where the
   check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'j') && (*cp != 'J'))
    return false;
  cp++;
  if ((*cp != 'p') && (*cp != 'P'))
    return false;
  cp++;
  if ((*cp != 'b') && (*cp != 'B'))
    return false;
  cp++;
  if (*cp == '2')
    cp++;
  return (*cp == '\0');
}

/*****************************************************************************/
/* STATIC                      check_mjc_suffix                              */
/*****************************************************************************/

static bool
  check_mjc_suffix(const char *fname)
  /* Returns true if the file-name has the suffix, ".mjc", where the
     check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'm') && (*cp != 'M'))
    return false;
  cp++;
  if ((*cp != 'j') && (*cp != 'J'))
    return false;
  cp++;
  if ((*cp != 'c') && (*cp != 'C'))
    return false;
  cp++;
  return (*cp == '\0');
}

/*****************************************************************************/
/* STATIC                       parse_yuv_format                             */
/*****************************************************************************/

static const char *
  parse_yuv_format(const char *fname, int &height, int &width,
                   double &frame_rate)
  /* Returns NULL, or one of the strings "444", "420" or "422", setting the
     various arguments to their values. */
{
  const char *format = "x444";
  const char *end = strstr(fname,format);
  if (end == NULL)
    { format = "x420"; end = strstr(fname,format); }
  if (end == NULL)
    { format = "x422"; end = strstr(fname,format); }
  if (end == NULL)
    return NULL;
  const char *start = end-1;
  for (; (start > fname) && (*start != 'x'); start--);
  if ((start == fname) || (sscanf(start+1,"%lf",&frame_rate) != 1) ||
      (frame_rate <= 0.0))
    return NULL;
  for (start--; (start > fname) && isdigit(*start); start--);
  if ((start == fname) || (*start != 'x') ||
      (sscanf(start+1,"%d",&height) != 1) || (height < 1))
    return NULL;
  for (start--; (start >= fname) && isdigit(*start); start--);  
  if ((sscanf(start+1,"%d",&width) != 1) || (width < 1))
    return NULL;
  return format+1;
}

/*****************************************************************************/
/* INLINE                    eat_white_and_comments                          */
/*****************************************************************************/

static inline void
  eat_white_and_comments(FILE *fp)
{
  int ch;
  bool in_comment;

  in_comment = false;
  while ((ch = fgetc(fp)) != EOF)
    if ((ch == '#') || (ch == '%'))
      in_comment = true;
    else if (ch == '\n')
      in_comment = false;
    else if ((!in_comment) && (ch != ' ') && (ch != '\t') && (ch != '\r'))
      {
        ungetc(ch,fp);
        return;
      }
}

/*****************************************************************************/
/* STATIC                         read_token                                 */
/*****************************************************************************/

static bool
  read_token(FILE *fp, char *buffer, int buffer_len)
{
  int ch;
  char *bp = buffer;

  while (bp == buffer)
    {
      eat_white_and_comments(fp);
      while ((ch = fgetc(fp)) != EOF)
        {
          if ((ch == '\n') || (ch == ' ') || (ch == '\t') || (ch == '\r') ||
              (ch == '#') || (ch == '%'))
            {
              ungetc(ch,fp);
              break;
            }
          *(bp++) = (char) ch;
          if ((bp-buffer) == buffer_len)
            { kdu_error e; e << "Input VIX file contains an unexpectedly long "
              "token in its text header.  Header is almost certainly "
              "corrupt or malformed."; }
        }
      if (ch == EOF)
        break;
    }
  if (bp > buffer)
    {
      *bp = '\0';
      return true;
    }
  else
    return false;
}

/*****************************************************************************/
/* INLINE                        read_to_tag                                 */
/*****************************************************************************/

static inline bool
  read_to_tag(FILE *fp, char *buffer, int buffer_len)
{
  while (read_token(fp,buffer,buffer_len))
    if ((buffer[0] == '>') && (buffer[strlen(buffer)-1] == '<'))
      return true;
  return false;
}

/*****************************************************************************/
/* STATIC                       open_vix_file                                */
/*****************************************************************************/

static FILE *
  open_vix_file(char *ifname, kdu_params *siz, int &num_frames,
                kdu_uint32 &timescale, kdu_uint32 &frame_period,
                int &sample_bytes, int &bits_used, bool &lsb_aligned,
                bool &is_signed, bool &native_order, bool &is_ycc, bool quiet)
  /* Opens the VIX (or YUV) input file, reading its header and returning a
     file pointer from which the sample values can be read. */
{
  double frame_rate = 1.0; // Default frame rate
  num_frames = 0; // Default number of frames
  sample_bytes = 0; // In case we forget to set it
  bits_used = 0; // In case we forget to set it
  is_ycc = false;
  lsb_aligned = false;
  is_signed = false;
  native_order = true;
  FILE *fp = fopen(ifname,"rb");
  if (fp == NULL)
    { kdu_error e; e << "Unable to open input file, \"" << ifname << "\"."; }
  if (check_yuv_suffix(ifname))
    { 
      int height=0, width=0;
      const char *format = parse_yuv_format(ifname,height,width,frame_rate);
      if (format == NULL)
        { kdu_error e; e << "YUV input filename must contain format and "
          "dimensions -- see `-i' in the usage statement.";
          fclose(fp);
        }
      sample_bytes = 1;
      bits_used = 8;
      is_ycc = true;
      native_order = true;
      is_signed = false;
      siz->set(Ssize,0,0,height); siz->set(Ssize,0,1,width);
      siz->set(Scomponents,0,0,3);
      for (int c=0; c < 3; c++)
        { 
          siz->set(Nsigned,c,0,false);
          siz->set(Nprecision,c,0,8);
          int sub_y=1, sub_x=1;
          if (c > 0)
            { 
              if (strcmp(format,"420") == 0)
                sub_x = sub_y = 2;
              else if (strcmp(format,"422") == 0)
                sub_x = 2;
            }
          siz->set(Ssampling,c,0,sub_y); siz->set(Ssampling,c,1,sub_x);
        }
      siz->finalize();
    }
  else
    { 
      int height=0, width=0, components=0;
      int native_order_is_big = 1; ((kdu_byte *)(&native_order_is_big))[0] = 0;
      char buffer[64];
      if ((fread(buffer,1,3,fp) != 3) ||
          (buffer[0] != 'v') || (buffer[1] != 'i') || (buffer[2] != 'x'))
        { kdu_error e; e << "The input file, \"" << ifname << "\", does not "
          "commence with the magic string, \"vix\"."; }
      while (read_to_tag(fp,buffer,64))
        if (strcmp(buffer,">VIDEO<") == 0)
          { 
            if ((!read_token(fp,buffer,64)) ||
                (sscanf(buffer,"%lf",&frame_rate) != 1) ||
                (!read_token(fp,buffer,64)) ||
                (sscanf(buffer,"%d",&num_frames) != 1) ||
                (frame_rate <= 0.0) || (num_frames < 0))
              { kdu_error e; e << "Malformed \">VIDEO<\" tag found in "
                "VIX input file.  Tag requires two numeric fields: a real-"
                "valued positive frame rate; and a non-negative number of "
                "frames."; }
          }
        else if (strcmp(buffer,">COLOUR<") == 0)
          { 
            if ((!read_token(fp,buffer,64)) ||
                (strcmp(buffer,"RGB") && strcmp(buffer,"YCbCr")))
              { kdu_error e; e << "Malformed \">COLOUR<\" tag found in "
                "VIX input file.  Tag requires a single token, with one of "
                "the strings, \"RGB\" or \"YCbCr\"."; }
            if (strcmp(buffer,"YCbCr") == 0)
              is_ycc = true;
          }
        else if (strcmp(buffer,">IMAGE<") == 0)
          { 
            if ((!read_token(fp,buffer,64)) ||
                (strcmp(buffer,"unsigned") && strcmp(buffer,"signed")))
              { kdu_error e; e << "Malformed \">IMAGE<\" tag found in VIX "
                "input file.  First token in tag must be one of the strings, "
                "\"signed\" or \"unsigned\"."; }
            is_signed = (strcmp(buffer,"signed") == 0);

            if ((!read_token(fp,buffer,64)) ||
                (strcmp(buffer,"char") && strcmp(buffer,"word") &&
                 strcmp(buffer,"dword")))
              { kdu_error e; e << "Malformed \">IMAGE<\" tag found in VIX "
                "input file.  Second token in tag must be one of the strings, "
                "\"char\", \"word\" or \"dword\"."; }
            if (strcmp(buffer,"char") == 0)
              sample_bytes = 1;
            else if (strcmp(buffer,"word") == 0)
              sample_bytes = 2;
            else
              sample_bytes = 4;

            if ((!read_token(fp,buffer,64)) ||
                ((lsb_aligned = (buffer[0]=='L')) &&
                 (sscanf(buffer+1,"%d",&bits_used) != 1)) ||
                ((!lsb_aligned) &&
                 (sscanf(buffer,"%d",&bits_used) != 1)) ||
                (bits_used < 1) || (bits_used > (8*sample_bytes)))
              { kdu_error e; e << "Malformed  \">IMAGE<\" tag found in VIX "
                "input file.  Third token in tag must hold the number of "
                "MSB's used in each sample word, a quantity in the range 1 "
                "through to the number of bits in the sample word, or "
                "else the number of LSB's used in each sample word, "
                "prefixed by `L'."; }
            if ((!read_token(fp,buffer,64)) ||
                (strcmp(buffer,"little-endian") &&
                 strcmp(buffer,"big-endian")))
              { kdu_error e; e << "Malformed \">IMAGE<\" tag found in VIX "
                "input file.  Fourth token in tag must hold one of the "
                "strings \"little-endian\" or \"big-endian\"."; }
            if (strcmp(buffer,"little-endian") == 0)
              native_order = (native_order_is_big)?false:true;
            else
              native_order = (native_order_is_big)?true:false;

            if ((!read_token(fp,buffer,64)) ||
                (sscanf(buffer,"%d",&width) != 1) ||
                (!read_token(fp,buffer,64)) ||
                (sscanf(buffer,"%d",&height) != 1) ||
                (!read_token(fp,buffer,64)) ||
                (sscanf(buffer,"%d",&components) != 1) ||
                (width <= 0) || (height <= 0) || (components <= 0))
              { kdu_error e; e << "Malformed \">IMAGE<\" tag found in VIX "
                "input file.  Fifth through seventh tags must hold positive "
                "values for the width, height and number of components in "
                "each frame, respectively."; }

            siz->set(Ssize,0,0,height); siz->set(Ssize,0,1,width);
            siz->set(Scomponents,0,0,components);
            for (int c=0; c < components; c++)
              { 
                siz->set(Nsigned,c,0,is_signed);
                siz->set(Nprecision,c,0,bits_used);
                int sub_y=0, sub_x=0;
                if ((!read_token(fp,buffer,64)) ||
                    (sscanf(buffer,"%d",&sub_x) != 1) ||
                    (!read_token(fp,buffer,64)) ||
                    (sscanf(buffer,"%d",&sub_y) != 1) ||
                    (sub_x < 1) || (sub_x > 255) ||
                    (sub_y < 1) || (sub_y > 255))
                  { kdu_error e; e << "Malformed \">IMAGE<\" tag found in VIX "
                    "input file.  Horizontal and vertical sub-sampling "
                    "factors in the range 1 to 255 must appear for each "
                    "image component."; }
                siz->set(Ssampling,c,0,sub_y); siz->set(Ssampling,c,1,sub_x);
              }
            siz->finalize();
            break;
          }
        else if (!quiet)
          { kdu_warning w; w << "Unrecognized tag, \"" << buffer << "\", "
            "found in VIX input file."; }
      if (sample_bytes == 0)
        { kdu_error e; e << "Input VIX file does not contain the mandatory "
          "\">IMAGE<\" tag."; }
      if (components < 3)
        is_ycc = false;

      // Read past new-line character which separates header from data
      int ch;
      while (((ch = fgetc(fp)) != EOF) && (ch != '\n'));
    }
  
  // Convert frame rate to a suitable timescale/frame period combination
  kdu_uint32 period, ticks_per_second, best_ticks=1000;
  double exact_period = 1.0 / frame_rate;
  double error, best_error=1000.0; // Ridiculous value
  for (ticks_per_second=10; ticks_per_second < (1<<16); ticks_per_second+=10)
    { 
      period = (kdu_uint32)(exact_period*ticks_per_second + 0.5);
      if (period >= (1<<16))
        break;
      error = fabs(exact_period-((double) period)/((double) ticks_per_second));
      if (error < best_error)
        { 
          best_error = error;
          best_ticks = ticks_per_second;
        }
    }
  
  timescale = best_ticks;
  frame_period = (kdu_uint32)(exact_period*best_ticks + 0.5);

  return fp;
}

/*****************************************************************************/
/* STATIC                       merge_siz_info                               */
/*****************************************************************************/

static int
  merge_siz_info(kdu_params *siz, kdu_params *vix_siz)
  /* This function is needed only because this demo application supports
     Part-2 codestreams that may use Multi-Component transforms -- these
     can be written to JPX files or MJC (raw) files only.
        To understand this function, we begin by noting that `vix_siz'
     holds the dimensional and sample attributes recovered by reading the
     header of the uncompressed input file (or by parsing the filename of
     a YUV file).  Let C be the `Scomponents' attribute derived from
     `vix_siz'.  This value represents the number of source image
     components supplied to the compression machinery and is also the
     function's return value.
        Regardless of whether multi-component transforms are to be used
     or not, the function transfers `Sdims' attributes from `vix_siz' to
     `siz'.  This means that any multi-component transform must be
     structured so that the first C output components (after decompression
     and inversion of the multi-component transform) must have the same
     dimensions as the first C codestream image components.  This is not a
     requirement for all applications that might be based on Kakadu, but
     simplifies the setting of `Sdims' values.
        If multi-component transforms are not used (`Mcomponents' is not set
     in `siz'), the function simply transfers `Nsigned' and `Nprecision'
     attributes to `siz' and then finalizes it, returning the `Scomponents'
     value C.
        If multi-component transforms are used, the user is required to
     have supplied `Ssigned' and `Sprecision' values that have been parsed
     into `siz'; the user may also have supplied explicit `Msigned' and
     `Mprecision' valiues.  The `Nsigned' and `Nprecision' values found in
     `vix_siz', however, are transferred to `siz'.  The value of `Mcomponents'
     found in `siz' must be no smaller than C.  If `siz' has an `Scomponents'
     attribute already (supplied by the user), it must be no larger than C --
     otherwise, `kdu_multi_analysis' will not be able to invert the
     multi-component transform to produce codestream image components from the
     source components that are interpreted as the first C multi-component
     transform output components (from the perspective of a decompressor).
     If `siz' has no `Scomponents' attribute already, it is set equal to C. */
{
  int c, c_components=0, m_components=0;
  siz->get(Mcomponents,0,0,m_components);
  vix_siz->get(Scomponents,0,0,c_components);
  int rows=-1, cols=-1, prec_val=-1, sign_val=-1;
  for (c=0; c < c_components; c++)
    { 
      vix_siz->get(Sdims,c,0,rows);  vix_siz->get(Sdims,c,1,cols);
      siz->set(Sdims,c,0,rows);      siz->set(Sdims,c,1,cols);
      vix_siz->get(Nsigned,c,0,sign_val);
      siz->set(Nsigned,c,0,sign_val);
      vix_siz->get(Nprecision,c,0,prec_val);
      siz->set(Nprecision,c,0,prec_val);
    }
  if (m_components == 0)
    { // No multi-component transform
      siz->set(Scomponents,0,0,c_components);
    }
  else
    { 
      int s_comps=0;  siz->get(Scomponents,0,0,s_comps);
      if (s_comps == 0)
        siz->set(Scomponents,0,0,s_comps=c_components);
    }
  
  return c_components;
}

/*****************************************************************************/
/* STATIC                        get_bpp_dims                                */
/*****************************************************************************/

static kdu_long
  get_bpp_dims(siz_params *siz)
{
  int comps, max_width, max_height, n;

  siz->get(Scomponents,0,0,comps);
  max_width = max_height = 0;
  for (n=0; n < comps; n++)
    {
      int width, height;
      siz->get(Sdims,n,0,height);
      siz->get(Sdims,n,1,width);
      if (width > max_width)
        max_width = width;
      if (height > max_height)
        max_height = height;
    }
  return ((kdu_long) max_height) * ((kdu_long) max_width);
}

/*****************************************************************************/
/* STATIC                      assign_layer_bytes                            */
/*****************************************************************************/

static kdu_long *
  assign_layer_bytes(kdu_args &args, siz_params *siz, int &num_specs)
  /* Returns a pointer to an array of `num_specs' quality layer byte
     targets.  The value of `num_specs' is determined in this function, based
     on the number of rates (or slopes) specified on the command line,
     together with any knowledge about the number of desired quality layers.
     Before calling this function, you must parse all parameter attribute
     strings into the code-stream parameter lists rooted at `siz'.  Note that
     the returned array will contain 0's whenever a quality layer's
     bit-rate is unspecified.  This allows the compressor's rate allocator to
     assign the target size for those quality layers on the fly. */
{
  char *cp;
  char *string = NULL;
  int arg_specs = 0;
  int slope_specs = 0;
  int cod_specs = 0;

  if (args.find("-slope") != NULL)
    {
      string = args.advance(false); // Need to process this arg again later.
      if (string != NULL)
        {
          while (string != NULL)
            {
              slope_specs++;
              string = strchr(string+1,',');
            }
        }
    }

  // Determine how many rates are specified on the command-line
  if (args.find("-rate") != NULL)
    {
      string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-rate\" argument must be followed by a "
          "string identifying one or more bit-rates, separated by commas."; }
      cp = string;
      while (cp != NULL)
        {
          arg_specs++;
          cp = strchr(cp,',');
          if (cp != NULL)
            cp++;
        }
    }

  // Find the number of layers specified by the main COD marker

  kdu_params *cod = siz->access_cluster(COD_params);
  assert(cod != NULL);
  cod->get(Clayers,0,0,cod_specs,false,false,false);
  if (!cod_specs)
    cod_specs = (arg_specs>slope_specs)?arg_specs:slope_specs;
  num_specs = cod_specs;
  if (num_specs == 0)
    num_specs = 1;
  if ((arg_specs != num_specs) &&
      ((arg_specs > 2) || ((arg_specs == 2) && (num_specs == 1))))
    { kdu_error e; e << "The relationship between the number of bit-rates "
      "specified by the \"-rate\" argument and the number of quality layers "
      "explicitly specified via \"Clayers\" does not conform to the rules "
      "supplied in the description of the \"-rate\" argument.  Use \"-u\" "
      "to print the usage statement."; }
  cod->set(Clayers,0,0,num_specs);
  int n;
  kdu_long *result = new kdu_long[num_specs];
  for (n=0; n < num_specs; n++)
    result[n] = 0;

  kdu_long total_pels = get_bpp_dims(siz);
  bool have_dash = false;
  for (n=0; n < arg_specs; n++)
    {
      cp = strchr(string,',');
      if (cp != NULL)
        *cp = '\0'; // Temporarily terminate string.
      if (strcmp(string,"-") == 0)
        { have_dash = true; result[n] = KDU_LONG_MAX; }
      else
        {
          double bpp;
          if ((!sscanf(string,"%lf",&bpp)) || (bpp <= 0.0))
            { kdu_error e; e << "Illegal sub-string encoutered in parameter "
              "string supplied to the \"-rate\" argument.  Rate parameters "
              "must be strictly positive real numbers, with multiple "
              "parameters separated by commas only.  Problem encountered at "
              "sub-string: \"" << string << "\"."; }
          result[n] = (kdu_long) floor(bpp * 0.125 * total_pels);
        }
      if (cp != NULL)
        { *cp = ','; string = cp+1; }
    }

  if (arg_specs)
    { // Bubble sort the supplied specs.
      bool done = false;
      while (!done)
        { // Use trivial bubble sort.
          done = true;
          for (int n=1; n < arg_specs; n++)
            if (result[n-1] > result[n])
              { // Swap misordered pair.
                kdu_long tmp=result[n];
                result[n]=result[n-1];
                result[n-1]=tmp;
                done = false;
              }
        }
    }

  if (arg_specs && (arg_specs != num_specs))
    { // Arrange for specified rates to identify max and/or min layer rates
      assert((arg_specs < num_specs) && (arg_specs <= 2));
      result[num_specs-1] = result[arg_specs-1];
      result[arg_specs-1] = 0;
    }

  if (have_dash)
    { // Convert final rate target of KDU_LONG_MAX into 0 (forces rate
      // allocator to assign all remaining compressed bits to that layer.)
      assert(result[num_specs-1] == KDU_LONG_MAX);
      result[num_specs-1] = 0;
    }

  if (string != NULL)
    args.advance();
  return result;
}

/*****************************************************************************/
/* STATIC                      assign_layer_thresholds                       */
/*****************************************************************************/

static kdu_uint16 *
  assign_layer_thresholds(kdu_args &args, int num_specs)
  /* Returns a pointer to an array of `num_specs' slope threshold values,
     all of which are set to 0 unless the command-line arguments contain
     an explicit request for particular distortion-length slope thresholds. */
{
  int n;
  kdu_uint16 *result = new kdu_uint16[num_specs];

  for (n=0; n < num_specs; n++)
    result[n] = 0;
  if (args.find("-slope") == NULL)
    return result;
  char *string = args.advance();
  if (string == NULL)
    { kdu_error  e; e << "The `-slope' argument must be followed by a "
      "comma-separated list of slope values."; }
  for (n=0; (n < num_specs) && (string != NULL); n++)
    {
      char *delim = strchr(string,',');
      if (delim != NULL)
        { *delim = '\0'; delim++; }
      int val;
      if ((sscanf(string,"%d",&val) != 1) || (val < 0) || (val > 65535))
        { kdu_error e; e << "The `-slope' argument must be followed by a "
          "comma-separated  list of integer distortion-length slope values, "
          "each of which must be in the range 0 to 65535, inclusive."; }
      result[n] = (kdu_uint16) val;
      string = delim;
    }

  // Now sort the entries into decreasing order.
  int k;
  if (n > 1)
    {
      bool done = false;
      while (!done)
        { // Use trivial bubble sort.
          done = true;
          for (k=1; k < n; k++)
            if (result[k-1] < result[k])
              { // Swap misordered pair.
                kdu_uint16 tmp=result[k];
                result[k]=result[k-1];
                result[k-1]=tmp;
                done = false;
              }
        }
    }
  
  // Fill in any remaining missing values.
  for (k=n; k < num_specs; k++)
    result[k] = result[n-1];
  args.advance();
  return result;
}

/*****************************************************************************/
/* STATIC                  set_default_colour_weights                        */
/*****************************************************************************/

static void
  set_default_colour_weights(kdu_params *siz, bool is_ycc, bool quiet)
  /* If the data to be compressed already has a YCbCr representation,
     (`is_ycc' is true) or the code-stream colour transform is to be used,
     this function sets appropriate weights for the luminance and
     chrominance components.  The weights are taken from the Motion JPEG2000
     standard (ISO/IEC 15444-3).  Otherwise, no weights are used. */
{
  kdu_params *cod = siz->access_cluster(COD_params);
  assert(cod != NULL);

  float weight;
  if (cod->get(Clev_weights,0,0,weight) ||
      cod->get(Cband_weights,0,0,weight))
    return; // Weights already specified explicitly.
  bool can_use_ycc = !is_ycc;
  bool rev0=false;
  int c, depth0=0, sub_x0=1, sub_y0=1;
  for (c=0; c < 3; c++)
    { 
      int depth=0;
      if (!siz->get(Sprecision,c,0,depth))
        siz->get(Nprecision,c,0,depth);
      int sub_y=1; siz->get(Ssampling,c,0,sub_y);
      int sub_x=1; siz->get(Ssampling,c,1,sub_x);
      kdu_params *coc = cod->access_relation(-1,c,0,true);
      if (coc->get(Clev_weights,0,0,weight) ||
          coc->get(Cband_weights,0,0,weight))
        return;
      bool rev=false; coc->get(Creversible,0,0,rev);
      if (c == 0)
        { rev0=rev; depth0=depth; sub_x0=sub_x; sub_y0=sub_y; }
      else if ((rev != rev0) || (depth != depth0) ||
               (sub_x != sub_x0) || (sub_y != sub_y0))
        can_use_ycc = false;
    }
  bool use_ycc = can_use_ycc;
  if (!cod->get(Cycc,0,0,use_ycc))
    cod->set(Cycc,0,0,use_ycc=can_use_ycc);
  if (!(use_ycc || is_ycc))
    return;

  for (c=0; c < 3; c++)
    { 
      kdu_params *coc = cod->access_relation(-1,c,0,false);
      int sub_y=1; siz->get(Ssampling,c,0,sub_y);
      int sub_x=1; siz->get(Ssampling,c,1,sub_x);
      
      double weight;
      int b_src=0, b=0;
      while ((sub_y > 1) && (sub_x > 1))
        { sub_y >>= 1; sub_x >>= 1; b_src += 3; }
      if (c == 0)
        for (; b_src < 9; b++, b_src++)
          { 
            switch (b_src) {
               case 0:         weight = 0.090078; break;
               case 1: case 2: weight = 0.275783; break;
               case 3:         weight = 0.701837; break;
               case 4: case 5: weight = 0.837755; break;
               case 6:         weight = 0.999988; break;
               case 7: case 8: weight = 0.999994; break;
              }
            coc->set(Cband_weights,b,0,weight);
          }
      else if (c == 1)
        for (; b_src < 15; b++, b_src++)
          { 
            switch (b_src) {
                case 0:           weight = 0.027441; break;
                case 1:  case 2:  weight = 0.089950; break;
                case 3:           weight = 0.141965; break;
                case 4:  case 5:  weight = 0.267216; break;
                case 6:           weight = 0.348719; break;
                case 7:  case 8:  weight = 0.488887; break;
                case 9:           weight = 0.567414; break;
                case 10: case 11: weight = 0.679829; break;
                case 12:          weight = 0.737656; break;
                case 13: case 14: weight = 0.812612; break;
              }
            coc->set(Cband_weights,b,0,weight);
          }
      else
        for (; b_src < 15; b++, b_src++)
          { 
            switch (b_src) {
                case 0:           weight = 0.070185; break;
                case 1:  case 2:  weight = 0.166647; break;
                case 3:           weight = 0.236030; break;
                case 4:  case 5:  weight = 0.375136; break;
                case 6:           weight = 0.457826; break;
                case 7:  case 8:  weight = 0.587213; break;
                case 9:           weight = 0.655884; break;
                case 10: case 11: weight = 0.749805; break;
                case 12:          weight = 0.796593; break;
                case 13: case 14: weight = 0.856065; break;
              }
            coc->set(Cband_weights,b,0,weight);
          }
    }

  if (!quiet)
    pretty_cout << "Note:\n\tThe default rate control policy for colour "
                   "video employs visual (CSF) weighting factors.  To "
                   "minimize MSE, instead of visually weighted MSE, "
                   "specify `-no_weights'.\n";
}

/*****************************************************************************/
/* STATIC                  set_mj2_video_attributes                          */
/*****************************************************************************/

static void
  set_mj2_video_attributes(mj2_video_target *video, kdu_params *siz,
                           bool is_ycc)
{
  // Set colour attributes
  jp2_colour colour = video->access_colour();
  int num_components; siz->get(Scomponents,0,0,num_components);
  if (num_components >= 3)
    colour.init((is_ycc)?JP2_sYCC_SPACE:JP2_sRGB_SPACE);
  else
    colour.init(JP2_sLUM_SPACE);
}

/*****************************************************************************/
/* STATIC                  set_jpx_video_attributes                          */
/*****************************************************************************/

static void
  set_jpx_video_attributes(jpx_container_target container, siz_params *siz,
                           kdu_uint32 timescale, kdu_uint32 frame_period)
  /* When writing content to a JPX file, this function must be called once
     the first `kdu_codestream' interface has been created, passing in
     the root of the parameter sub-system, retrieved via
     `kdu_codestream::access_siz', as the `siz' argument.  The function
     performs the following critical tasks, before which no codestream data
     can be written to the target:
     1. Each of the `container's `jpx_codestream_target' interfaces is
        accessed and its `jp2_dimensions' are configured based on `siz'.
     2. The function adds one presentation track to `container' for each
        set of S base compositing layers, where S is the number of base
        codestreams in the container.  Each such presentation track is
        configured with a single indefinitely repeated frame that uses
        relative compositing layer 0.  The associated compositing
        instruction uses source and target dimensions that are derived from
        the size of the codestream targets, configured in step 1.
     After this, the caller should invoke `jpx_target::write_headers'
     to ensure that all headers have been written -- only then can
     codestreams be written.
  */
{
  int num_base_streams=0, num_base_layers=0;
  container.get_base_codestreams(num_base_streams);
  container.get_base_layers(num_base_layers);
  int num_tracks = num_base_layers / num_base_streams;
  assert((num_tracks*num_base_streams) == num_base_layers);

  kdu_dims compositing_dims;
  for (int c=0; c < num_base_streams; c++)
    { 
      jpx_codestream_target cs = container.access_codestream(c);
      jp2_dimensions dimensions = cs.access_dimensions();
      dimensions.init(siz);
      compositing_dims.size = dimensions.get_size();
    }
  
  int frame_duration = (int)
    (0.5 + 1000.0* ((double) frame_period) / ((double) timescale));
  for (int t=0; t < num_tracks; t++)
    { 
      jpx_composition comp=container.add_presentation_track(num_base_streams);
      jx_frame *frm = comp.add_frame(frame_duration,-1,false);
      comp.add_instruction(frm,0,1,compositing_dims,compositing_dims);
    }
}

/*****************************************************************************/
/* STATIC                   transfer_bytes_to_line                           */
/*****************************************************************************/

static void
  transfer_bytes_to_line(void *src, kdu_line_buf line, int width,
                         int bits_used, bool lsb_aligned, bool is_signed)
{
  kdu_byte *sp = (kdu_byte *) src;
  if (line.get_buf16() != NULL)
    {
      kdu_sample16 *dp = line.get_buf16();
      if (line.is_absolute())
        { 
          if (lsb_aligned || (bits_used == 8))
            {
              if (is_signed)
                { 
                  int shift = 16-bits_used;
                  for (; width--; dp++, sp++)
                    dp->ival = ((((kdu_int16) *sp) << shift) >> shift);
                }
              else
                { 
                  kdu_int16 offset = (kdu_int16)(1 << (bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->ival = ((kdu_int16) *sp) - offset;
                }
            }
          else if (is_signed)
            { 
              int shift = 16-bits_used;              
              for (; width--; dp++, sp++)
                dp->ival = (((kdu_int16) *sp) << 8) >> shift;
            }
          else
            { 
              int shift = 8-bits_used;
              for (; width--; dp++, sp++)
                dp->ival = (((kdu_int16) *sp) - 128) >> shift;
            }
        }
      else
        { 
          if (lsb_aligned)
            {
              if (is_signed)
                { 
                  int shift = 16-bits_used;
                  for (; width--; dp++, sp++)
                    dp->ival = (((kdu_int16) *sp)<<shift)>>(16-KDU_FIX_POINT);
                }
              else
                for (; width--; dp++, sp++)
                  dp->ival = (((kdu_int16) *sp) - 128) << (KDU_FIX_POINT-8);
            }
          else
            {
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int16) *sp) << (KDU_FIX_POINT-8);
              else
                for (; width--; dp++, sp++)
                  dp->ival = (((kdu_int16) *sp) - 128) << (KDU_FIX_POINT-8);
            }
        }
    }
  else
    {
      kdu_sample32 *dp = line.get_buf32();
      if (line.is_absolute())
        {
          if (lsb_aligned)
            {
              if (is_signed)
                { 
                  int shift = 32-bits_used;
                  for (; width--; dp++, sp++)
                    dp->ival = ((((kdu_int32) *sp) << shift) >> shift);
                }
              else
                { 
                  kdu_int32 offset = (kdu_int32)(1 << (bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->ival = ((kdu_int32) *sp) - offset;
                }
            }
          else
            {
              int shift = 8-bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int32) *sp) >> shift;
              else
                for (; width--; dp++, sp++)
                  dp->ival = (((kdu_int32) *sp) - 128) >> shift;
            }
        }
      else
        {
          if (lsb_aligned)
            { 
              int shift = 16-bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->fval = ((float)(((kdu_int16) *sp) << shift))
                           * (1.0F / 65536.0F);
              else
                { 
                  kdu_int16 offset = (kdu_int16)(1<<(bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->fval = ((float)((((kdu_int16) *sp)-offset) << shift))
                             * (1.0F / 65536.0F);
                }
            }
          else
            { 
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->fval = ((float) *sp) * (1.0F/256.0F);
              else
                for (; width--; dp++, sp++)
                  dp->fval = ((float) *sp) * (1.0F/256.0F) - 0.5F;
            }
        }
    }
}

/*****************************************************************************/
/* STATIC                   transfer_words_to_line                           */
/*****************************************************************************/

static void
  transfer_words_to_line(void *src, kdu_line_buf line, int width,
                         int bits_used, bool lsb_aligned, bool is_signed)
{
  kdu_int16 *sp = (kdu_int16 *) src;
  if (line.get_buf16() != NULL)
    {
      kdu_sample16 *dp = line.get_buf16();
      if (line.is_absolute())
        {
          if (lsb_aligned || (bits_used == 16))
            {
              if (is_signed)
                { 
                  int shift = 16-bits_used;
                  for (; width--; dp++, sp++)
                    dp->ival = ((*sp << shift) >> shift);
                }
              else
                { 
                  kdu_int16 offset = (kdu_int16)(1 << (bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->ival = *sp - offset;
                }
            }
          else
            {
              int downshift = 16-bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = (*sp) >> downshift;
              else
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int16)(*sp - 0x8000)) >> downshift;
            }
        }
      else
        {
          if (lsb_aligned)
            { 
              int shift = 16 - bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = (*sp<<shift) >> (16-KDU_FIX_POINT);
              else
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int16)((*sp<<shift)-0x8000))
                           >> (16-KDU_FIX_POINT);
            }
          else
            {
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = (*sp) >> (16-KDU_FIX_POINT);
              else
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int16)(*sp - 0x8000)) >> (16-KDU_FIX_POINT);
            }
        }
    }
  else
    {
      kdu_sample32 *dp = line.get_buf32();
      if (line.is_absolute())
        { 
          if (lsb_aligned)
            {
              if (is_signed)
                { 
                  int shift = 32-bits_used;
                  for (; width--; dp++, sp++)
                    dp->ival = ((((kdu_int32) *sp) << shift) >> shift);
                }
              else
                { 
                  kdu_int32 offset = (kdu_int32)(1 << (bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->ival = ((kdu_int32) *sp) - offset;
                }
            }
          else
            {
              int shift = 16-bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->ival = ((kdu_int32) *sp) >> shift;
              else
                for (; width--; dp++, sp++)
                  dp->ival = (kdu_int32)(((kdu_int16)(*sp - 0x8000)) >> shift);
            }
        }
      else
        { 
          if (lsb_aligned)
            { 
              int shift = 16-bits_used;
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->fval = ((float)(*sp << shift)) * (1.0F / 65536.0F);
              else
                { 
                  kdu_int16 offset = (kdu_int16)(1<<(bits_used-1));
                  for (; width--; dp++, sp++)
                    dp->fval = ((float)((*sp-offset)<<shift))*(1.0F/65536.0F);
                }
            }
          else
            { 
              if (is_signed)
                for (; width--; dp++, sp++)
                  dp->fval = ((float) *sp) * (1.0F / (float)(1<<16));
              else
                for (; width--; dp++, sp++)
                  dp->fval = (*((kdu_uint16 *) sp)) * (1.0F/(1<<16)) - 0.5F;
            }
        }
    }
}


/* ========================================================================= */
/*                                kdv_io_queue                               */
/* ========================================================================= */

/*****************************************************************************/
/*                         kdv_io_queue::kdv_io_queue                        */
/*****************************************************************************/

kdv_io_queue::kdv_io_queue(int max_frames)
{
  max_frames_to_load = (max_frames <= 0)?INT_MAX:max_frames;
  num_frames_loaded = num_frames_retrieved = num_frames_flushed = 0;
  active_loader = active_flusher = NULL;
  ready_loader = ready_flusher = NULL;
  waiting_compressor = NULL;
  waiting_cond = NULL;
  last_min_slope = 0;
  load_job.init(this);
  flush_job.init(this);
  termination_requested = false;
  mutex.create();
}

/*****************************************************************************/
/*                        kdv_io_queue::~kdv_io_queue                        */
/*****************************************************************************/

kdv_io_queue::~kdv_io_queue()
{
  mutex.destroy();
}

/*****************************************************************************/
/*                      kdv_io_queue::processor_joined                       */
/*****************************************************************************/

void kdv_io_queue::processor_joined(kdv_compressor *obj, kdu_thread_env *env)
{
  mutex.lock();
  assert((ready_loader == NULL) && (ready_flusher == NULL));
  if (active_loader != NULL)
    ready_loader = obj;
  else if (num_frames_loaded < max_frames_to_load)
    { 
      active_loader = obj;
      this->schedule_job(&load_job,env);
    }
  mutex.unlock();
}

/*****************************************************************************/
/*                       kdv_io_queue::frame_processed                       */
/*****************************************************************************/

void kdv_io_queue::frame_processed(kdv_compressor *obj, kdu_thread_env *env)
{
  mutex.lock();
  assert((ready_loader == NULL) && (ready_flusher == NULL));
  if (active_loader != NULL)
    ready_loader = obj;
  else if (num_frames_loaded < max_frames_to_load)
    { 
      active_loader = obj;
      this->schedule_job(&load_job,env);
    }
  if (active_flusher != NULL)
    ready_flusher = obj;
  else
    { 
      active_flusher = obj;
      this->schedule_job(&flush_job,env);
    } 
  mutex.unlock();
}

/*****************************************************************************/
/*                         kdv_io_queue::wait_for_io                         */
/*****************************************************************************/

bool kdv_io_queue::wait_for_io(kdv_compressor *obj, kdu_thread_env *env)
{
  bool result = false;
  mutex.lock();
  assert((obj != ready_loader) && (obj != ready_flusher));
  if ((obj == active_loader) || (obj == active_flusher))
    { // Need to wait until load, flush or both complete
      assert(waiting_compressor == NULL);
      waiting_compressor = obj;
      waiting_cond = env->get_condition();
      mutex.unlock();
      env->wait_for_condition();
      mutex.lock();
      assert((obj != active_loader) && (obj != active_flusher) &&
             (obj != waiting_compressor));
    }
  if (num_frames_retrieved < max_frames_to_load)
    { 
      result = true;
      num_frames_retrieved++;
    }
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                          kdv_io_queue::do_load                            */
/*****************************************************************************/

void kdv_io_queue::do_load(kdu_thread_env *env)
{
  assert(active_loader != NULL);
  bool success = active_loader->load_frame(env);
  mutex.lock();
  if ((!success) || ((++num_frames_loaded) >= max_frames_to_load))
    {
      max_frames_to_load = num_frames_loaded;
      active_loader = ready_loader = NULL; // In case someone is waiting
    }
  else if ((active_loader = ready_loader) != NULL)
    { 
      ready_loader = NULL;
      schedule_job(&load_job,env);
    }
  bool need_all_done = (termination_requested &&
                        (active_flusher == NULL) && (active_loader == NULL));
  if ((waiting_compressor != NULL) &&
      (waiting_compressor != active_loader) &&
      (waiting_compressor != active_flusher))
    { // Waiting compressor can wake up
      env->signal_condition(waiting_cond);
      waiting_cond = NULL;
      waiting_compressor = NULL;
    }
  mutex.unlock();
  if (need_all_done)
    all_done(env); // This should always be the last thing we do
}

/*****************************************************************************/
/*                          kdv_io_queue::do_flush                           */
/*****************************************************************************/

void kdv_io_queue::do_flush(kdu_thread_env *env)
{
  assert(active_flusher != NULL);
  last_min_slope = active_flusher->finish_frame(env,last_min_slope);
  mutex.lock();
  num_frames_flushed++;
  if ((active_flusher = ready_flusher) != NULL)
    { 
      ready_flusher = NULL;
      schedule_job(&flush_job,env);
    }
  bool need_all_done =
    ((active_flusher == NULL) && (active_loader == NULL) &&
     (termination_requested || (num_frames_flushed >= max_frames_to_load)));
  if ((waiting_compressor != NULL) &&
      (waiting_compressor != active_loader) &&
      (waiting_compressor != active_flusher))
    { // Waiting compressor can wake up
      env->signal_condition(waiting_cond);
      waiting_cond = NULL;
      waiting_compressor = NULL;
    }
  mutex.unlock();
  if (need_all_done)
    all_done(env); // This should be the last thing we do
}

/*****************************************************************************/
/*                    kdv_io_queue::request_termination                      */
/*****************************************************************************/

void kdv_io_queue::request_termination(kdu_thread_entity *env)
{
  mutex.lock();
  termination_requested = true;
  ready_flusher = ready_loader = NULL;
  max_frames_to_load = num_frames_loaded;
  bool need_all_done = (active_flusher == NULL) && (active_loader == NULL);
  mutex.unlock();
  if (need_all_done)
    all_done(env); // Best not to invoke this with the mutex locked
}


/* ========================================================================= */
/*                               kdv_compressor                              */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdv_compressor::kdv_compressor                     */
/*****************************************************************************/

kdv_compressor::kdv_compressor(kdu_compressed_video_target *video,
                               kdu_codestream codestream, int num_src_comps,
                               FILE *source_fp, int initial_frame_idx,
                               int frame_idx_inc, int frame_repeat,
                               int sample_bytes, bool native_order,
                               bool lsb_aligned, bool is_signed,
                               int num_layer_specs,
                               int double_buffering_height,
                               bool want_fastest, bool force_precise,
                               int preferred_flush_period, kdu_thread_env *env,
                               kdv_io_queue *bkgnd_io_queue,
                               kdv_cbr_stats *cbr_stats_obj,
                               FILE *cbr_trace_fp)
{
  int c;

  this->codestream = codestream;
  this->video_target = video;
  this->num_source_components = num_src_comps;
  this->tgt = NULL;
  this->main_env = env;
  this->env_queue = NULL;
  this->io_queue = bkgnd_io_queue;
  this->cbr_stats = cbr_stats_obj;
  this->cbr_trace_file = cbr_trace_fp;
  this->frame_sequence_idx = initial_frame_idx;
  this->frame_sequence_inc = frame_idx_inc;
  this->frame_repetitions = (frame_repeat <= 0)?0:frame_repeat;

  this->double_buffering = (double_buffering_height != 0);
  this->processing_stripe_height=(double_buffering)?double_buffering_height:1;
  this->want_fastest = want_fastest;
  this->force_precise = force_precise;
  this->flush_period = preferred_flush_period;
  this->fp = source_fp;
  this->sample_bytes = sample_bytes;
  this->native_order = native_order;
  this->lsb_aligned = lsb_aligned;
  this->is_signed = is_signed;
  codestream.get_valid_tiles(this->tile_indices);
  this->trim_to_rate = this->skip_codestream_comments = false;
  this->rate_tolerance = 0.0;
  this->flush_flags = 0;
  this->target_max_bytes = 0;
  this->target_min_slope = 0;
  this->last_min_slope = this->next_min_slope = 0;
  frame_bytes = 0; // We will grow this in a little while

  frame_reps_remaining = 0;
  buffer = NULL; // We will allocate this after determining `frame_bytes'
  components = new kdv_component[num_source_components];
  for (c=0; c < num_source_components; c++)
    { 
      kdv_component *comp = components + c;
      codestream.get_dims(c,comp->comp_dims);
      comp->precision = codestream.get_bit_depth(c,true);
      comp->line = NULL; // Just to be on the safe side
      frame_bytes += sample_bytes * (int)  comp->comp_dims.area();
    }

  // Allocate buffer and fill in component pointers
  buffer = new kdu_byte[frame_bytes];
  kdu_byte *comp_buf = buffer;
  for (c=0; c < num_source_components; c++)
    {
      kdv_component *comp = components + c;
      comp->comp_buf = comp_buf;
      comp_buf += sample_bytes * (int) comp->comp_dims.area();
    }

  // Allocate storage for the per-frame layer bytes and slope thresholds
  this->num_layer_specs = num_layer_specs;
  layer_bytes = new kdu_long[num_layer_specs];
  layer_thresholds = new kdu_uint16[num_layer_specs];
  for (c=0; c < num_layer_specs; c++)
    { 
      layer_bytes[c] = 0;
      layer_thresholds[c] = 0;
    }
  
  // Initialize statistics
  cumulative_total_bytes = 0;
  cumulative_compressed_bytes = 0;
  max_header_bytes = 0;  

  // Launch background frame loading, if appropriate
  if (io_queue != NULL)
    io_queue->processor_joined(this,main_env);
  
#ifdef KDU_SIMD_OPTIMIZATIONS
  kdu_check_sample_alignment(); // Best to verify that there is no
          // mismatch between the sample alignment definitions currently
          // found in "kdu_arch.h" and those present at the time when the
          // core system was compiled, since our line buffers are allocated
          // inside the core system's `kdu_multi_synthesis' object.
#endif // KDU_SIMD_OPTIMIZATIONS
  
  // Determine suitable parameters to pass to `kdu_codestream::auto_flush'
  // if `flush_period' > 0.
  if ((flush_period <= 0) || (flush_period == INT_MAX))
    { 
      this->flush_period = 0;
      this->tc_trig_interval = 0;
      this->incr_trig_interval = 0;
    }
  else
    { // The logic here is reproduced from the "kdu_compress" demo-app.
      int min_sub_y=0; // Will be min component vertical sub-sampling
      for (c=0; c < num_source_components; c++)
        { 
          kdu_coords subs; codestream.get_subsampling(c,subs,true);
          if ((min_sub_y == 0) || (min_sub_y > subs.y))
            min_sub_y = subs.y;
        }
      kdu_dims t_dims; codestream.get_tile_partition(t_dims);
      int max_tile_lines = 1 + ((t_dims.size.y-1) / min_sub_y);

      kdu_long tc_trigger_interval=1; 
      if (flush_period > max_tile_lines)
        tc_trigger_interval = flush_period / max_tile_lines;
      tc_trigger_interval *= num_source_components;
      tc_trigger_interval *= tile_indices.size.x;
      if (tc_trigger_interval > (1<<30))
        tc_trigger_interval = 1<<30; // Just in case
      
      kdu_long incr_trigger_interval = 0;
      if ((flush_period+(flush_period>>1)) < max_tile_lines)
        { // Otherwise, don't bother with incremental flush within tile
          incr_trigger_interval = flush_period * min_sub_y;
          incr_trigger_interval *= num_source_components;
          incr_trigger_interval *= tile_indices.size.x;
          if (incr_trigger_interval > (1<<30))
            incr_trigger_interval = 1<<30; // Just in case
        }
      
      this->tc_trig_interval = (int)tc_trigger_interval;
      this->incr_trig_interval = (int)incr_trigger_interval;
    }
}

/*****************************************************************************/
/*                          kdv_compressor::load_frame                       */
/*****************************************************************************/

bool
  kdv_compressor::load_frame(kdu_thread_env *io_caller)
{
  if ((io_queue != NULL) && (io_caller == NULL))
    return io_queue->wait_for_io(this,main_env);

  if (frame_reps_remaining == 0)
    { // Otherwise, reuse the frame that has already been loaded
      int num_bytes = (int) fread(buffer,1,(size_t) frame_bytes,fp);
      if (num_bytes == frame_bytes)
        { 
          if ((!native_order) && (sample_bytes > 1))
            { // Reorder bytes in each sample word
              int n = frame_bytes / sample_bytes;
              kdu_byte tmp, *bp = buffer;
              if (sample_bytes == 2)
                for (; n--; bp += 2)
                  {
                    tmp=bp[0]; bp[0]=bp[1]; bp[1]=tmp;
                  }
              else if (sample_bytes == 4)
                for (; n--; bp += 4)
                  {
                    tmp=bp[0]; bp[0]=bp[3]; bp[3]=tmp;
                    tmp=bp[1]; bp[1]=bp[2]; bp[2]=tmp;
                  }
              else
                assert(0);
            }
          frame_reps_remaining = frame_repetitions;
          return true;
        }
      else if (num_bytes > 0)
        { kdu_error w;
          w << "The input VIX file does not contain a whole number of frames, "
            "suggesting that the file has been incorrectly formatted."; }
    }
  else
    {
      frame_reps_remaining--;
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                        kdv_compressor::process_frame                      */
/*****************************************************************************/

void
  kdv_compressor::process_frame(const kdu_long lyr_bytes[],
                                const kdu_uint16 lyr_thresholds[],
                                bool trim_to_rate, bool no_info,
                                bool predict_slope, double rate_tolerance)
{
  int n, c;

  // Copy quality layer specifications and other parameters and determine
  // how they should be used.
  if (lyr_thresholds[0] == 0)
    { // Rate control based on target sizes
      target_min_slope = 0;
      this->flush_flags = KDU_FLUSH_THRESHOLDS_ARE_HINTS;
      for (n=0; n < num_layer_specs; n++)
        this->layer_bytes[n] = lyr_bytes[n];
    }
  else
    { 
      target_min_slope = lyr_thresholds[num_layer_specs-1];
      this->flush_flags = 0;
      for (n=0; n < num_layer_specs; n++)
        { 
          this->layer_bytes[n] = lyr_bytes[n];
          this->layer_thresholds[n] = lyr_thresholds[n];
          if (lyr_bytes[n] != 0)
            this->flush_flags = KDU_FLUSH_USES_THRESHOLDS_AND_SIZES;
        }
    }
  target_max_bytes = lyr_bytes[num_layer_specs-1];
  this->trim_to_rate = trim_to_rate;
  this->skip_codestream_comments = no_info;
  this->rate_tolerance = rate_tolerance;

  // Start up the codestream processing machinery
  tgt = video_target;
  if (frame_reps_remaining == frame_repetitions)
    { // First frame in new repetition (or we are not repeating)
      last_min_slope = next_min_slope; // We may be using `last_min_slope' to
        // initialize the codestream and we want to make sure we do the same
        // thing for each repetition
    }
  else
    tgt = &null_target;
  if (frame_sequence_idx >= frame_sequence_inc)
    codestream.restart(tgt,main_env);
  if ((num_layer_specs > 0) && (target_min_slope > 0))
    codestream.set_min_slope_threshold(target_min_slope);
  else if ((num_layer_specs > 0) && (target_max_bytes > 0))
    { 
      if (predict_slope && (last_min_slope != 0))
        codestream.set_min_slope_threshold(last_min_slope);
      else
        codestream.set_max_bytes(target_max_bytes,false,false);
    }
  
  if (flush_period > 0)
    { // Open the image and configure auto-flush; opening the image ahead of
      // time here works with overlapped frame processing only if there are
      // multiple output interfaces, one for each frame processing engine; this
      // is the case for MJC files right now.  The `main' function should not
      // allow the `flush_period' feature to be passed in under any other
      // circumstances.
      if (tgt == video_target)
        video_target->open_image();
      codestream.auto_flush(tc_trig_interval,tc_trig_interval,
                            incr_trig_interval,incr_trig_interval,
                            layer_bytes,num_layer_specs,layer_thresholds,
                            trim_to_rate,!skip_codestream_comments,
                            rate_tolerance,main_env,flush_flags);
    }

  assert(env_queue == NULL);
  if (main_env != NULL)
    env_queue = main_env->add_queue(NULL,NULL,NULL,frame_sequence_idx);
  frame_sequence_idx += frame_sequence_inc;

  // Process tiles one by one -- we could do better if there are multiple
  // tiles by working in tile banks, but it is a bit weird to use tiles for
  // video, so there is no point in complicating this example.
  kdu_coords idx;
  for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < tile_indices.size.x; idx.x++)
      { 
        if (open_tile.exists())
          { // Wait until all processing completes for the last tile before
            // starting a new one; this is where we could do better if we
            // wanted to maximize throughput on multi-tiled video -- but this
            // is not a sufficiently likely case to bother with for this demo.
            if (main_env != NULL)
              main_env->join(env_queue,true); // Joins with descendants only
            engine.destroy();
            open_tile.close();
          }

        // Configure the tile processing engine
        open_tile = codestream.open_tile(tile_indices.pos+idx,main_env);
        open_tile.set_components_of_interest(num_source_components);
        engine.create(codestream,open_tile,force_precise,NULL,want_fastest,
                      processing_stripe_height,main_env,env_queue,
                      double_buffering);

        // Configure tile-component state
        kdv_component *comp;
        for (comp=components, c=0; c < num_source_components; c++, comp++)
          { 
            kdu_tile_comp tc = open_tile.access_component(c);
            kdu_resolution res = tc.access_resolution();
            res.get_dims(comp->tile_dims);
            comp->line = NULL;
            assert((comp->tile_dims & comp->comp_dims) == comp->tile_dims);
            comp->bp = comp->comp_buf + sample_bytes *
              ((comp->tile_dims.pos.x - comp->comp_dims.pos.x) +
               (comp->tile_dims.pos.y - comp->comp_dims.pos.y) *
               comp->comp_dims.size.x);
            comp->is_absolute = engine.is_line_absolute(c);
            comp->uses_shorts = !engine.is_line_precise(c);
            comp->simd_transfer = NULL;
#ifdef KDU_SIMD_OPTIMIZATIONS
            int in_buf_type=0;
            if (sample_bytes == 1)
              in_buf_type = KDSC_BUF8;
            else if (sample_bytes == 2)
              in_buf_type = KDSC_BUF16;
            else if (sample_bytes == 4)
              in_buf_type = KDSC_BUF32;
            else
              assert(0);
            using namespace kd_supp_local; // Just for the macro below
            KDSC_FIND_SIMD_TRANSFER_FUNC(comp->simd_transfer,in_buf_type,
                               comp->uses_shorts,1,
                               comp->tile_dims.size.x,
                               ((lsb_aligned)?comp->precision:8*sample_bytes),
                               comp->precision,comp->is_absolute);
#endif // KDU_SIMD_OPTIMIZATIONS
          }

        // Now for the processing
        bool done = false;
        while (!done)
          { 
            done = true; // Until proven otherwise
            for (comp=components, c=0; c < num_source_components; c++, comp++)
              { 
                if (comp->tile_dims.size.y <= 0)
                  continue;
                if ((comp->line == NULL) &&
                    ((comp->line=engine.exchange_line(c,NULL,main_env))==NULL))
                  continue; // This component is not yet ready for writing
                done = false;
                comp->tile_dims.size.y--;
                assert(comp->tile_dims.size.y >= 0);
                int width = comp->tile_dims.size.x;
                if (comp->simd_transfer != NULL)
                  { 
                    void *dst=comp->line->get_buf(); // Good for all data types
                    void *src=comp->bp;
                    int src_prec=(lsb_aligned)?comp->precision:8*sample_bytes;
                    int tgt_prec=comp->precision;
                    comp->simd_transfer(&dst,src,width,src_prec,tgt_prec,
                                        comp->is_absolute,this->is_signed);
                    comp->bp += (comp->comp_dims.size.x * sample_bytes);
                  }
                else if (sample_bytes == 1)
                  {
                    transfer_bytes_to_line(comp->bp,*(comp->line),width,
                                           comp->precision,lsb_aligned,
                                           is_signed);
                    comp->bp += comp->comp_dims.size.x;
                  }
                else if (sample_bytes == 2)
                  {
                    transfer_words_to_line(comp->bp,*(comp->line),width,
                                           comp->precision,lsb_aligned,
                                           is_signed);
                    comp->bp += (comp->comp_dims.size.x << 1);
                  }
                else
                  { kdu_error e; e << "The VIX reader provided here "
                    "currently supports only \"byte\" and \"word\" data "
                    "types as sample data containers.   The \"dword\" "
                    "data type could easily be added if necessary."; }
                comp->line = engine.exchange_line(c,comp->line,main_env);
              }
          }
      }

  if (io_queue == NULL)
    finish_frame(main_env,0);
  else
    io_queue->frame_processed(this,main_env);
}

/*****************************************************************************/
/*                        kdv_compressor::finish_frame                       */
/*****************************************************************************/

kdu_uint16
  kdv_compressor::finish_frame(kdu_thread_env *caller,
                               kdu_uint16 alt_min_slope)
{
  if (caller != NULL)
    { 
      assert(env_queue != NULL);
      caller->join(env_queue); // Also deletes the `env_queue'
      env_queue = NULL;
      caller->cs_terminate(codestream);
    }
  engine.destroy();
  open_tile.close();

  kdu_uint16 result=0;
  if (tgt == video_target)
    { // In this case, we are actually generating output
      if (flush_period <= 0)
        video_target->open_image(); // else already done in `process_frame'
      codestream.flush(this->layer_bytes,num_layer_specs,
                       this->layer_thresholds,this->trim_to_rate,
                       !this->skip_codestream_comments,rate_tolerance,NULL,
                       flush_flags);
      video_target->close_image(codestream);
      kdu_uint16 min_slope = this->layer_thresholds[num_layer_specs-1];
      if (target_max_bytes > 0)
        { // See how close to the target size we came.  If we are a long way
          // off, adjust `min_slope' to help ensure that we do not excessively
          // truncate the code-block generation process in subsequent frames.
          // This is only a heuristic.
          kdu_long target = target_max_bytes;
          kdu_long actual = this->layer_bytes[num_layer_specs-1];
          kdu_long gap = target - actual;
          if (gap > actual)
            min_slope = 0;
          else
            while (gap > (target>>4))
              {
                gap >>= 1;
                if (min_slope < 64)
                  { min_slope = 0; break; }
                min_slope -= 64;
              }
        }
      this->next_min_slope = min_slope;
      if ((alt_min_slope > 0) && (alt_min_slope < min_slope))
        this->next_min_slope = alt_min_slope;

      result = min_slope;
    }
  else
    { // In this case, we are flushing the compressed result to `null_target'
      assert(tgt == &null_target);
      codestream.flush(this->layer_bytes,num_layer_specs,
                       this->layer_thresholds,this->trim_to_rate,
                       !this->skip_codestream_comments,rate_tolerance,NULL,
                       flush_flags);
      result = alt_min_slope; // Prevents changes in the min slope threshold
                              // between frames of a repetition.
    }
  
  // Update statistics before returning
  kdu_long total_bytes = codestream.get_total_bytes();
  kdu_long compressed_bytes = codestream.get_packet_bytes();
  kdu_long header_bytes = total_bytes - compressed_bytes;
  this->cumulative_total_bytes += total_bytes;
  this->cumulative_compressed_bytes += compressed_bytes;
  if (header_bytes > this->max_header_bytes)
    this->max_header_bytes = header_bytes;
  if (cbr_stats != NULL)
    { 
      cbr_stats->get_stats(codestream);
      if (cbr_trace_file != NULL)
        cbr_stats->print_stats(cbr_trace_file);
    }
  
  return result;  
}


/* ========================================================================= */
/*                             External Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/*                                   main                                    */
/*****************************************************************************/

int main(int argc, char *argv[])
{
  kdu_customize_warnings(&pretty_cout);
  kdu_customize_errors(&pretty_cerr);
  kdu_args args(argc,argv,"-s");

  // Collect simple arguments.
  char *ifname=NULL, *ofname=NULL, *cbr_fname=NULL;
  kdu_field_order field_order;
  int max_pictures=INT_MAX, picture_repeat=0, subsampling_factor=1;
  int num_threads, double_buffering_stripe_height, flush_period;
  double rate_tolerance;
  int proc_limit_passes, proc_limit_start_plane, proc_limit_extra_start_passes;
  bool trim_to_rate, no_slope_predict, want_fastest, force_precise;
  bool overlapped_pictures;
  bool no_info, no_weights, cpu, cbr_stats, quiet;
  parse_simple_arguments(args,ifname,ofname,max_pictures,picture_repeat,
                         subsampling_factor,field_order,
                         double_buffering_stripe_height,
                         rate_tolerance,trim_to_rate,proc_limit_passes,
                         proc_limit_start_plane,proc_limit_extra_start_passes,
                         no_slope_predict,want_fastest,force_precise,
                         overlapped_pictures,flush_period,
                         no_info,no_weights,num_threads,cpu,
                         cbr_stats,cbr_fname,quiet);
  
  // Collect any parameters relevant to the SIZ marker segment
  siz_params siz;
  const char *string;
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz.parse_string(string));
  if ((ofname != NULL) && check_broadcast_suffix(ofname))
    { // Automatically set broadcast profile if no profile has been explicitly
      // set on the command-line -- this is just a convenience for the user.
      // It must be done here, because `open_vix_file' invokes `siz.finalize'.
      int profile;
      if (!siz.get(Sprofile,0,0,profile))
        siz.set(Sprofile,0,0,Sprofile_BROADCAST);
    }

  // Open input file and collect dimensional information
  int sample_bytes, msbs_used, num_pictures;
  bool is_signed, lsb_aligned, is_ycc, native_order;
  kdu_uint32 timescale, frame_period;
  siz_params vix_siz; // Captures information derived from the vix/yuv file
  FILE *vix_file =
    open_vix_file(ifname,&vix_siz,num_pictures,timescale,frame_period,
                  sample_bytes,msbs_used,lsb_aligned,is_signed,
                  native_order,is_ycc,quiet);
  int num_source_components = merge_siz_info(&siz,&vix_siz);
    // Note: `num_source_components' is the number of components we will be
    // supplying from the uncompressed source file.  This may be different
    // to the number of multi-component transform output components (produced
    // during decompression) that are available for binding to colour
    // channels.  These types of complexities arise only when generating
    // JPX files, since they are able to embed Part-2 codestreams.
  
  frame_period *= (kdu_uint32) subsampling_factor;
  num_pictures /= subsampling_factor;
  if (field_order != KDU_FIELDS_NONE)
    { 
      num_pictures &= ~1; // Need to process an even number of pictures
      max_pictures &= ~1;
      frame_period *= 2;
    }
  if (args.find("-frate") != NULL)
    { // Frame-rate override
      int v1=0, v2=0;
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d,%d",&v1,&v2) != 2) ||
          (v1 < 1) || (v2 < 1) || (v1 >= (1<<16)) || (v2 >= (1<<16)))
        { kdu_error e; e << "The `-frate' argument requires a comma-separated "
          "pair of positive integer parameters, no greater than 65535."; }
      frame_period = (kdu_uint32) v1; timescale = (kdu_uint32) v2;
      args.advance();
    }

  // Create the compressed data target.
  kdv_null_target null_video_target; // Discards all data sent to it
  kdu_compressed_video_target *video[2] = // Allows up to 2 separate interfaces
    {&null_video_target,&null_video_target};
  jp2_family_tgt family_tgt;
  mj2_target movie;
  mj2_video_target *mj2_video = NULL;
  jpx_target composit_target;
  jpx_container_target jpx_container;
  kdv_jpx_target *jpx_video = NULL;
  kdv_jpx_labels *jpx_labels = NULL;
  jpb_target broadcast_target;
  kdu_simple_video_target mjc_video, mjc_views[2];
  
  if (ofname != NULL)
    { 
      if (check_mj2_suffix(ofname))
        { 
          family_tgt.open(ofname);
          movie.open(&family_tgt);
          mj2_video = movie.add_video_track();
          mj2_video->set_timescale(timescale);
          mj2_video->set_frame_period(frame_period);
          mj2_video->set_field_order(field_order);
          video[0] = video[1] = mj2_video;
        }
      else if (check_jpx_suffix(ofname))
        { 
          const char *prefix_fname=NULL;
          if ((args.find("-jpx_prefix") == NULL) ||
              ((prefix_fname = args.advance()) == NULL))
            { kdu_error e; e << "To generate a JPX file, you need to supply "
              "an initial JPX file via the `-jpx_prefix'; the new content "
              "will be appended to a copy of this prefix file.";
            }
          jp2_family_src prefix_family_src;
          prefix_family_src.open(prefix_fname);
          jpx_source prefix_source;
          prefix_source.open(&prefix_family_src,false);
          args.advance(); // No longer need `prefix_fname'
          family_tgt.open(ofname);
          composit_target.open(&family_tgt);
          int num_output_components=0;
          if (!siz.get(Mcomponents,0,0,num_output_components))
            siz.get(Scomponents,0,0,num_output_components);
          jpx_container =
            kdv_initialize_jpx_target(composit_target,prefix_source,
                                      num_output_components,is_ycc,
                                      field_order,args);
          prefix_source.close();
          prefix_family_src.close();
          if (args.find("-jpx_labels") != NULL)
            { 
              const char *label_prefix = args.advance();
              if (label_prefix == NULL)
                { kdu_error e; e << "The `-jpx_labels' argument requires a "
                  "string parameter."; }
              jpx_labels = new kdv_jpx_labels(&composit_target,jpx_container,
                                              label_prefix);
              args.advance();
            }
          jpx_video = new kdv_jpx_target(jpx_container,jpx_labels);
          video[0] = video[1] = jpx_video;
        }
      else if (check_broadcast_suffix(ofname))
        { 
          int frame_space=0;
          int max_bitrate=0;
          int timeflags = parse_jpb_data(args,frame_space,max_bitrate);
          family_tgt.open(ofname);
          broadcast_target.open(&family_tgt,(kdu_uint16)timescale,
                                (kdu_uint16)frame_period,field_order,
                                (kdu_byte)frame_space,
                                ((kdu_uint32)max_bitrate) * 1000000,
                                0,timeflags);
          video[0] = video[1] = &broadcast_target;
        }
      else if (check_mjc_suffix(ofname))
        { 
          if (field_order != KDU_FIELDS_NONE)
            { kdu_warning w; w << "The simple MJC file format does not "
              "support interlaced video -- the `-fields' argument will "
              "not have any effect for this output file format.  Try "
              "using MJ2 or JPB (broadcast stream)."; }
          mjc_video.open(ofname,timescale,frame_period,
                         (is_ycc)?KDU_SIMPLE_VIDEO_YCC:KDU_SIMPLE_VIDEO_RGB);
          mjc_views[0].attach_as_view(&mjc_video); // No harm in creating extra
          mjc_views[1].attach_as_view(&mjc_video); // view, even if not used.
          video[0] = &mjc_views[0];
          video[1] = &mjc_views[1];
        }
      else
        { kdu_error e; e << "Output file must have one of the suffices "
          "\".mj2\", \".jpx\", \".jpf\", \".jpb\", \".jpb2\" or \".mjc\".  "
          "See usage statement for more information."; }
    }

  // Create the multi-threading environment if required
  kdu_thread_env env, *env_ref=NULL;
  if (num_threads > 0)
    { 
      env.create();
      for (int nt=1; nt < num_threads; nt++)
        if (!env.add_thread())
          num_threads = nt; // Unable to create all the threads requested
      env_ref = &env;
    }

  // Create code-stream management object(s)
  int num_codestreams = 1;
  if (overlapped_pictures)
    { 
      if (subsampling_factor > 1)
        { kdu_error e; e << "The `-overlapped_frames' and `-subsample' "
          "arguments cannot be used together."; }
      if (env_ref == NULL)
        { kdu_error e; e << "The `-overlapped_frames' processing option "
          "cannot be used in single-threaded mode -- see `-num_threads'\n"; }
      if (num_threads < 2)
        { kdu_error e; e << "The `-overlapped_frames' processing option "
          "cannot be used if there is only one thread in the "
          "multi-threaded processing environment, because the special "
          "queue created for background processing should only be run from "
          "the safe context offered by a worker thread that is not waiting "
          "for another condition to occur."; }
      if ((flush_period > 0) && !mjc_video.exists())
        { kdu_error e; e << "The `-flush_period' option is not compatible "
          "with overlapped multi-threaded frame processing, unless the "
          "compressed video target supports this -- currently only MJC "
          "output files are configured to support this.  You may need to "
          "use `-disjoint_frames' if you really need progressive "
          "flushing within each codestream."; }
      num_codestreams = 2;
    }
  kdu_codestream codestreams[2];  
  codestreams[0].create(&siz,video[0]);
  for (string=args.get_first(); string != NULL; )
    string = args.advance(codestreams[0].access_siz()->parse_string(string));
  int num_layers = 0;
  kdu_long *layer_bytes =
    assign_layer_bytes(args,codestreams[0].access_siz(),num_layers);
  kdu_uint16 *layer_thresholds =
    assign_layer_thresholds(args,num_layers);
  if (mjc_video.exists())
    { 
      kdu_long tgt_bytes = layer_bytes[num_layers-1];
      if (tgt_bytes != (kdu_long)((kdu_uint32)(tgt_bytes)))
        { kdu_error e; e << "The target bit-rate supplied via `-rate' is too "
          "large to record the codestream length within a 32-bit field, as "
          "required by the selected \"MJC\" output file format."; }
    }
  FILE *cbr_file = NULL;
  if (codestreams[0].cbr_flushing())
    { 
      kdu_long tgt_bytes = layer_bytes[num_layers-1];
      if (tgt_bytes == 0)
        { kdu_error e; e << "With the `Scbr' option, you must specify a "
          "specific overall target bit-rate via `-rate'!"; }
      if (mjc_video.exists())
        { 
          mjc_video.set_fixed_length((kdu_uint32)tgt_bytes);
          if (!quiet)
            pretty_cout << "\nCBR flushing: each frame will be exactly "
                        << tgt_bytes << " bytes in length\n\n";
        }
      if (cbr_fname != NULL)
        { 
          if ((cbr_file = fopen(cbr_fname,"w")) == NULL)
            { kdu_error e; e << "Unable to open CBR trace file specified via "
              "`-cbr_trace' for writing.";
            }
          kdv_cbr_stats::print_hdr(cbr_file);
        }
    }  
  else
    { 
      if ((num_layers < 2) && !quiet)
        pretty_cout << "Note:\n\tIf you want quality scalability, you should "
                       "generate multiple layers with `-rate' or by using "
                       "the \"Clayers\" option.\n";
      if (cbr_stats)
        { kdu_warning w; w << "`-cbr_stats'/`-cbr_trace' argument being "
          "ignored, because CBR flushing has not been specified via `Scbr'.";
        }
    }
  if ((codestreams[0].get_num_components() >= 3) && (!no_weights))
    set_default_colour_weights(codestreams[0].access_siz(),is_ycc,quiet);
  if (mj2_video != NULL)
    set_mj2_video_attributes(mj2_video,codestreams[0].access_siz(),is_ycc);
  else if (jpx_video != NULL)
    { 
      set_jpx_video_attributes(jpx_container,codestreams[0].access_siz(),
                               timescale,frame_period);
      composit_target.write_headers();
    }
  codestreams[0].access_siz()->finalize_all();
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }
  codestreams[0].enable_restart();
  if (num_codestreams == 2)
    { // Create the second codestream
      codestreams[1].create(&siz,video[1]);
      codestreams[1].access_siz()->copy_all(codestreams[0].access_siz());
      codestreams[1].access_siz()->finalize_all();
      codestreams[1].enable_restart();
    }

  // Configure flush-stats to be used with processing constraints
  kdu_flush_stats flush_stats[2];
  if (proc_limit_passes > 0)
    { 
      flush_stats[0] =
        codestreams[0].add_flush_stats(0);
      flush_stats[0].auto_advance(num_codestreams);
      if (num_codestreams == 2)
        { 
          flush_stats[1] =
            codestreams[1].add_flush_stats(1);
          flush_stats[1].auto_advance(num_codestreams);
          codestreams[0].attach_flush_stats(flush_stats[1]);
          codestreams[1].attach_flush_stats(flush_stats[0]);
        }
    }

  // Create picture compressors and associated processing machinery
  if (picture_repeat > 0)
    { 
      num_pictures *= (1+picture_repeat);
      if (max_pictures < (INT_MAX/(+picture_repeat)))
        max_pictures *= (1+picture_repeat);
    }
  if ((num_pictures > 0) && (num_pictures < max_pictures))
    max_pictures = num_pictures;
  kdv_compressor *picture_compressors[2]={NULL,NULL};
  kdv_io_queue *io_queue = NULL;
  if (overlapped_pictures)
    { 
      io_queue = new kdv_io_queue(max_pictures);
      env_ref->attach_queue(io_queue,NULL,"I/O Domain",0,
                            KDU_THREAD_QUEUE_SAFE_CONTEXT);
        // Note: The `KDU_THREAD_QUEUE_SAFE_CONTEXT' flag is required here to
        // avoid the possibility of deadlock.  This is because the `io_queue'
        // is used to schedule codestream flushing jobs and these jobs invoke
        // the `kdu_thread_entity::join' function.  See notes on the
        // `kdu_thread_entity::attach_queue' function for an explanation.
    }
  
  kdv_cbr_stats cbr_stats_recorder; // Keep track of global CBR stats here
  int c;
  for (c=0; c < num_codestreams; c++)
    picture_compressors[c] =
      new kdv_compressor(video[c],codestreams[c],num_source_components,
                         vix_file,c,num_codestreams,picture_repeat,
                         sample_bytes,native_order,lsb_aligned,
                         is_signed,num_layers,double_buffering_stripe_height,
                         want_fastest,force_precise,flush_period,
                         env_ref,io_queue,
                         (cbr_stats)?&cbr_stats_recorder:NULL,cbr_file);

  // Compress the pictures
  double total_time = 0.0001;
  double file_time = 0.0001;
  double last_report_time = 0.0;
  int skip_pictures=0; // Initial number of pictures to skip is 0
  int picture_count = 0;
  kdu_clock timer;
  for (; picture_count < max_pictures; picture_count++)
    { 
      kdv_compressor *compressor =
        picture_compressors[(num_codestreams==1)?0:(picture_count & 1)];
      for (; skip_pictures > 0; skip_pictures--)
        { 
          assert(io_queue == NULL);
          compressor->load_frame();
        }
      skip_pictures = subsampling_factor-1; // For next time
      if (!compressor->load_frame())
        break; // Cannot read any more frame data
      if (!overlapped_pictures)
        { 
          double delta = timer.get_ellapsed_seconds();
          file_time += delta;
          total_time += delta;
        }
      compressor->process_frame(layer_bytes,layer_thresholds,
                                trim_to_rate,no_info,!no_slope_predict,
                                rate_tolerance);
      total_time += timer.get_ellapsed_seconds();
      if ((!quiet) && (total_time >= (last_report_time+0.5)))
        { 
          last_report_time = total_time;
          pretty_cout << picture_count+1
                      << " pictures processed -- avg rate is "
                      << (picture_count+1)/total_time << " pictures/s\n";
        }
    }
  if (io_queue != NULL)
    { 
      env_ref->join(io_queue); // Wait for background processing to finish
      delete io_queue;
    }

  // Print summary statistics
  if (cbr_stats)
    { 
      cbr_stats_recorder.print_hdr(stdout);
      cbr_stats_recorder.get_averages();
      cbr_stats_recorder.print_stats(stdout);
    }
  
  if (cpu || !quiet)
    pretty_cout << picture_count
                << " pictures processed -- avg rate is "
                << picture_count/total_time << " pictures/s\n\n";

  if ((num_pictures > 0) && (picture_count < num_pictures) &&
      (picture_count < max_pictures))
    { kdu_warning w; w << "Managed to read only " << picture_count
      << " of the " << num_pictures << " pictures advertised by the input "
      "VIX file's text header.  It is likely that a formatting error exists.";
    }

  if (!quiet)
    {
      if (num_threads == 0)
        pretty_cout << "Processed using the single-threaded environment "
          "(see `-num_threads').\n";
      else
        pretty_cout << "Processed using the multi-threaded environment, with\n"
          << "    " << num_threads << " parallel threads of execution "
          "(see `-num_threads') and\n    "
          << ((overlapped_pictures)?"two overlapped processing engines":
                                    "disjoint picture processing")
          << "\n";
    }
  
  if (cpu || !quiet)
    {
      pretty_cout << "Total time = " << total_time << "s\n";
      if (picture_count && !overlapped_pictures)
        { // Only used one picture compressor; can separate reading/processing
          pretty_cout << "\tActual decompression processing time = "
                      << (total_time-file_time)/picture_count
                      << " seconds per picture\n";
          pretty_cout << "\tFile reading time = "
                      << file_time/picture_count
                      << " seconds per picture\n";
        }
    }
  
  if ((picture_count > 0) && !quiet)
    { 
      kdu_long codestream_bytes=0, compressed_bytes=0, max_header_bytes=0;
      for (c=0; c < num_codestreams; c++)
        { 
          codestream_bytes +=
            picture_compressors[c]->cumulative_total_bytes;
          compressed_bytes +=
            picture_compressors[c]->cumulative_compressed_bytes;
          if (max_header_bytes < picture_compressors[c]->max_header_bytes)
            max_header_bytes = picture_compressors[c]->max_header_bytes;
        }
      pretty_cout << "Avg codestream bytes = "
                  << ((double) codestream_bytes) / picture_count << "\n";
      pretty_cout << "Avg J2K packet bytes (headers+bodies) per codestream = "
                  << ((double) compressed_bytes) / picture_count << "\n";
      pretty_cout << "Max codestream header (non-packet) bytes = "
                  << ((int) max_header_bytes) << "\n";
      if (broadcast_target.exists())
        { 
          pretty_cout <<
            "\tNB: This is the component of the overall codestream "
            "size that cannot be hard limited by the `Creslengths' "
            "parameter attribute; when using `Creslengths' to "
            "impose hard limits on the size of codestreams "
            "that are generated to satisfy a broadcast profile, "
            "you should subtract this value from the profile's maximum "
            "codestream size in order to determine a suitable limit for "
            "`Creslengths'.\n";
          kdu_uint32 tc = broadcast_target.get_last_timecode();
          char tc_string[20];
          sprintf(tc_string,"%02x:%02x:%02x:%02x",
                  (int)((tc>>24) & 255), (int)((tc>>16) & 255),
                  (int)((tc>>8 ) & 255), (int)(tc & 255));
          pretty_cout << "Last frame time-code is: " << tc_string << "\n";
        }
    }

  // Cleanup
  if (env.exists())
    env.terminate(NULL,true);
  delete[] ifname;
  delete[] ofname;
  delete[] layer_bytes;
  delete[] layer_thresholds;
  fclose(vix_file);
  for (c=0; c < 2; c++)
    if (picture_compressors[c] != NULL)
      { 
        delete picture_compressors[c];
        codestreams[c].destroy();
      }
  video[0]->close();
  video[1]->close(); // No harm in an extra close operation, if identical
  if (mjc_video.exists())
    mjc_video.close(); // Above operations only closed the two views
  if (jpx_video != NULL)
    delete jpx_video;
  if (jpx_labels != NULL)
    delete jpx_labels;
  movie.close(); // Does nothing if not an MJ2 file.
  composit_target.close(); // Does nothing if not a JPX file.
  family_tgt.close();
  if (env.exists())
    env.destroy(); // Destroy multi-threading environment, if one was used.
  if (cbr_file != NULL)
    fclose(cbr_file);
  return 0;
}
