/*****************************************************************************/
// File: kdu_vcom_fast.cpp [scope = APPS/VCOM_FAST]
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
   High Performance video compressor.  This demo app is essentially the
dual of "kdu_vex_fast"; it does pretty much the same thing as
"kdu_v_compress" but allows multiple independent frame compression
engines to be instantiated, each with its own set of processing threads.
******************************************************************************/

#include <string.h>
#include <ctype.h>
#include <stdio.h> // so we can use `sscanf' for arg parsing.
#include <math.h>
#include <assert.h>
#include <time.h>
// Kakadu core includes
#include "kdu_arch.h"
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
// Application includes
#include "kdu_args.h"
#include "kdu_video_io.h"
#include "mj2.h"
#include "jpx.h"
#include "kdu_vcom.h"

using namespace kdu_supp_vcom; // Includes namespaces `kdu_supp' and `kdu_core'

/* ========================================================================= */
/*                         Set up messaging services                         */
/* ========================================================================= */

class kdu_stream_message : public kdu_thread_safe_message {
  public: // Member classes
    kdu_stream_message(FILE *stream, kdu_exception exception_code)
      { // Service throws exception on end of message if
        // `exception_code' != KDU_NULL_EXCEPTION.
        this->stream = stream;  this->exception_code = exception_code;
      }
    void put_text(const char *string)
      { if (stream != NULL) fputs(string,stream); }
    void flush(bool end_of_message=false)
      {
        fflush(stream);
        kdu_thread_safe_message::flush(end_of_message);
        if (end_of_message && (exception_code != KDU_NULL_EXCEPTION))
          throw exception_code;
      }
  private: // Data
    FILE *stream;
    int exception_code;
  };

static kdu_stream_message cout_message(stdout, KDU_NULL_EXCEPTION);
static kdu_stream_message cerr_message(stderr, KDU_ERROR_EXCEPTION);
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
  out << "This is Kakadu's \"kdu_vcom_fast\" application.\n";
  out << "\tCompiled against the Kakadu core system, version "
      << KDU_CORE_VERSION << "\n";
  out << "\tCurrent core system version is "
      << kdu_get_core_version() << "\n";
  out << "This application demonstrates a flexible and extremely powerful "
         "approach to video compression, in which one or more independent "
         "compression engines can be instantiated, each heavily "
         "multi-threaded, so as to explore the most effective means to "
         "exploit the full computational power of platforms with massive "
         "numbers of CPUs.\n";
  out << "   Subject to good parameter selection, it should be possible to "
         "arrange for all threads of execution to remain active virtually "
         "100% of the time, and this should be achievable with only a small "
         "number of processing engines (often only 1), minimizing latency "
         "and memory consumption.\n";
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
    out << "\tTo avoid over complicating this demonstration "
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
  out << "-frate <ticks per composite frame>,<ticks per second>\n";
  if (comprehensive)
    out << "\tBy default, frame rate information is derived from the source "
           "file, if possible.  However, this argument allows you to "
           "override such information and provide very a high precision "
           "specification of the frame rate, as a rational number.  "
           "The argument takes a comma-separated pair of positive integer "
           "parameters, neither of which may exceed 65535, such that the "
           "frame rate has the precise value: "
           "<ticks per second>/<ticks per frame>.\n"
           "\t   It is worth noting that the precise frame rate for "
           "NTSC video should be given as \"-frate 1001,30000\".\n";
  out << "-frames <max frames to process>\n";
  if (comprehensive)
    out << "\tBy default, all available input frames are processed.  This "
           "argument may be used to limit the number of frames which are "
           "actually processed.  This argument is especially intereting in "
           "conjunction with \"-loop\", which causes the input file's "
           "video frames to be read over and over again in cyclic fashion "
           "until the total number of frames that have been read and "
           "processed reaches the value supplied here.  Otherwise, the "
           "number of frames that are processed will not exceed the number "
           "that are found in the input file, regardless of the value "
           "supplied with this argument.\n";
  out << "-loop -- loop through source frames to respect `-frames' request\n";
  if (comprehensive)
    out << "\tIgnored unless \"-frames\" is also specified, in which case "
           "providing this argument effectively expands the set of source "
           "video frames to achieve the number requested via \"-frames\".  "
           "This is achieved by repeatedly cycling back to the start of the "
           "video source file until all required frames have been read.\n";
  out << "-frame_reps <total number of times to compress each frame>\n";
  if (comprehensive)
    out << "\tThis argument is useful only when investigating the "
           "processing throughput achievable by the video compression "
           "implementation here.  Specifically, the argument passed to "
           "this function represents a number N > 0, such that each frame "
           "read from the video source is compressed N times.  Each "
           "iteration of the compression process is identical, including "
           "rate control properties and flushing of compressed content, "
           "except that the output from all but the first iteration of "
           "each frame is discarded after it has been generated.  In this "
           "way, each source frame appears only once in the target file, "
           "but the throughput and processed frame count statistics "
           "reported by the application are based on the total number of "
           "frame compression iterations performed.  This "
           "means that you can factor out the impact of any I/O bottlenecks "
           "when estimating througput performance, simply by specifying a "
           "moderate to large value for N here.  This is reasonable, because "
           "in many applications the source frames and compressed output "
           "are not actually derived from or transferred to disk.\n";
  out << "-o <MJ2 or JPX compressed output file>\n";
  if (comprehensive)
    out << "\tIt is allowable to omit this argument, in which case all "
           "compression operations will be performed, but the result will "
           "not be written anywhere.  This can be useful for timing tests, "
           "since I/O is often the bottlneck on modern platforms.\n"
           "\t   Two types of compressed video files may be generated.  If "
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
           "`-jpx_layers' and/or `-jpx_labels' arguments.\n";
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
  out << "-engine_threads <#thrds>[:<cpus>][+<#thrds>[:<cpus>][...]] ...\n";
  if (comprehensive)
    out << "\tThis application provides two mechanisms to exploit "
           "multiple CPU's: 1) by processing frames in parallel; and 2) by "
           "using Kakadu's multi-threaded environment to speed up the "
           "processing of each frame.  These can be blended in whatever "
           "way you like by separately selecting the number of frame "
           "processing engines and the number of threads to use within each "
           "engine.  This argument takes one parameter (an engine descriptor) "
           "for each frame processing engine you would like to create.  "
           "In its simplest form an engine descriptor is a single integer "
           "identifying the number of threads to assign to the frame "
           "processing engine.  This single integer may, optionally, be "
           "followed by a CPU affinity descriptor, delimited by a colon, "
           "whose purpose is to identify the logical CPUs on which the "
           "threads should be scheduled.  In its most advanced form, the "
           "engine descriptor consists of a sequence of simple descriptors "
           "separated by `+' characters, identifying multiple collections "
           "of threads, each with their own CPU affinity, that collectively "
           "implement the frame processing engine in question.  The main "
           "reason for providing such sequences is that individual CPU "
           "affinity descriptors cannot describe more than 64 logical CPUs "
           "so it may not be possible to assign all the CPU resources of a "
           "very powerful platform to a single frame processing engine "
           "without specifying multiple thread collections with different "
           "affinity sets.\n"
           "\t   CPU affinity desriptors consist of a comma-separated list "
           "of CPU identifiers, enclosed in parentheses, and optionally "
           "prepended by an affinity context value that adds meaning to the "
           "CPU identifiers, as explained below.  The CPU identifiers found "
           "in the parenthetically enclosed list be integers "
           "in the range 0 to 63, or else the wildcard character `*' that "
           "expands to all values from 0 to 63.\n"
           "\t   On Windows systems, the affinity context is the processor "
           "group index (typically 0 for the first processor die, 1 for "
           "the second, etc., depending on how the system administrator "
           "has configured processor groups) and the parenthetically enclosed "
           "list identifies logical CPUs relative to that group.\n"
           "\t   On Linux systems, the affinity context is an integer offset "
           "to be added to the values in the parenthetically enclosed list to "
           "obtain absolute logical CPU numbers; typically you would set "
           "the affinity context on Linux systems to the first absolute "
           "logical CPU number of a processor die -- you may have to "
           "experiment.\n"
           "\t   OSX implementations use the affinity descriptor (context "
           "plus parenthetically enclosed list) to generate (hopefully) "
           "unique identifiers for threads that share the same affinity, "
           "but the operating system decides which CPUs to actually use, "
           "endeavouring to run threads with the same identifier on "
           "physically close CPUs.  This may produce the same benefits as "
           "direct assignment of logical CPUs, but you will have to "
           "experiment.\n"
           "\t   Example 1: \"-engine_threads 4:(0,1,2,3) 4:(4,5,6,7)\" "
           "creates two frame processing engines, each with 4 threads, bound "
           "to logical CPUs 0-3 and 4-7, respectively.\n"
           "\t   Example 2: \"-engine_threads 36:0(*)+36:1(*)\" creates one "
           "frame processing engine with 72 threads, the first 36 of which "
           "are bound to the CPUs belonging to processor group 0 on a "
           "Windows platform, while the last 32 are bound to the CPUs in "
           "processor group 1 on the same platform.  The Linux equivalent "
           "of this (assuming a platform with two dies, each with 36 logical "
           "CPUs) would be \"-engine_threads 36:0(*)+36:36(*)\".\n"
           "\t   If you do not provide an \"-engine_threads\" argument, "
           "the default policy is to assign roughly 4 threads to each "
           "frame processing engine, such that the total number of such "
           "threads equals the number of physical/virtual CPUs available.  "
           "Overall, the default policy provides a reasonable balance between "
           "throughput and latency, whose performance is often close to "
           "optimal.  However, it is often possible to deploy a much larger "
           "number of threads to each processing engine, without any "
           "significant throughput penalty, leading to fewer engines and "
           "hence a shorter pipeline with lower rendering latency.  The "
           "following things are worth considering when constructing "
           "different processing environments via this argument:\n"
           "\t  1) A separate management thread always consumes some "
           "resources to pre-load imagery for the frame processing "
           "engines and to save the compressed codestreams.  On a system "
           "with a large number of CPUs, it might possibly be best to "
           "create less frame processing threads than the number of "
           "CPU's so as to ensure timely operation of the management thread.  "
           "However, we have not observed this to be a significant issue "
           "so far.\n"
           "\t  2) As more threads are added to each processing engine, "
           "some inefficiencies are incurred due to occasional blocking "
           "on shared resources; however, these tend to be very small and may "
           "be compensated by the fact that fewer processing engines means "
           "less working memory.\n"
           "\t  3) Although the single threaded processing environment (i.e., "
           "one thread per engine) has minimal overhead, multi-threaded "
           "engines have the potential to better exploit the sharing of L2/L3 "
           "cache memory between close CPUs.  This is especially likely if "
           "CPU affinity is selected carefully.\n";
  out << "-read_ahead <num frames read ahead by the management thread>\n";
  if (comprehensive)
    out << "\tBy default, the number of frames which can be active at any "
           "given time is set to twice the number of processing engines.  "
           "By \"active\", we mean frames whose image samples have "
           "been read, but whose compressed output has not yet been "
           "fully generated.  This argument allows you to specify the "
           "number of active frames as E + A, where E is the number of "
           "frame processing engines and A is the read-ahead "
           "value supplied as the argument's parameter.\n";
  out << "-double_buffering <stripe height>\n";
  if (comprehensive)
    out << "\tThis option is intended to be used in conjunction with "
           "`-engine_threads'.  Double buffering is activated by "
           "default when the number of threads per frame processing "
           "engine exceeds 4, but you can exercise more precise "
           "control over when and how it is used via this argument.  "
           "Supplying 0 causes the feature to be disabled.\n"
           "\t   Without double buffering, DWT operations will all be "
           "performed by the single thread which \"owns\" the multi-threaded "
           "processing group associated with each frame processing engine.  "
           "For small processing thread groups, this may be acceptable or "
           "even optimal, since the DWT is generally quite a bit less CPU "
           "intensive than block encoding (which is always spread across "
           "multiple threads) and synchronous single-threaded DWT operations "
           "may improve memory access locality.  However, even for a small "
           "number of threads, the amount of thread idle time can be reduced "
           "by using the double buffered DWT feature.  In this case, a "
           "certain number of image rows in each image component are actually "
           "double buffered, so that one set can be processed by colour "
           "transformation and data format conversion operations, while the "
           "other set is processed by the DWT analysis engines, which "
           "feed the processing of block encoding jobs.  The number of "
           "rows in each component which are to be double buffered "
           "is known as the \"stripe height\", supplied as a "
           "parameter to this argument.  The stripe height can be as small "
           "as 1, but this may add a lot of thread context switching "
           "overhead.  For this reason, a stripe height in the range 8 to 64 "
           "is recommended.\n"
           "\t   The default policy, selects 0 for frame processing engines "
           "with 4 or less processing threads; otherwise it passes the "
           "special value -1 to the `kdu_multi_analysis' engine, which "
           "causes a suitable value to be selected automatically.\n";
#ifdef KDU_SPEEDPACK
  out << "-bc_jobs <min job samples>,<tgt jobs/stripe>,<tgt stripes/band>\n";
  if (comprehensive)
    out << "\tThis option is unique to the speed-pack, which allows you to "
           "modify the default internal policy for partitioning code-blocks "
           "into multi-threaded processing jobs and determining the "
           "trade-off between memory consumption and available parallelism.  "
           "The argument takes three integer parameters.\n"
           "\t   The first parameter specifies an approximate lower bound "
           "on the number of samples that will be found in any given block "
           "encoding job within any subband.  This determines the minimum "
           "number of code-blocks that will be processed together, subject "
           "to other constraints that may exist.  A typical value for this "
           "parameter would be 4096 (one 64x64 block, or four 32x32 blocks).\n"
           "\t   The second parameter specifies the minimum number of "
           "block encoding jobs you would like to be available across a "
           "row of code-blocks (or stripe) within any given subband.  Of "
           "course, this may not be achievable, especially for smaller "
           "tile-components or lower resolutions, and the lower bound "
           "on the job size provided by the first parameter takes "
           "precedence.  As a starting point, you might set this parameter "
           "to the number of threads in the multi-threaded processing engine, "
           "but smaller values may be more appropriate, especially if you "
           "have multiple image components or multiple tiles.  Smaller values "
           "encourage the selection of larger job sizes, which can improve "
           "cache utilization, while larger values favour more parallelism.\n"
           "\t   The third parameter allows you to control the number of "
           "consecutive rows of code-blocks that can be processed "
           "concurrently within any given subband.  This is another way to "
           "increase parallelism, but comes at the expense of memory "
           "consumption and perhaps poorer cache utilization.  Meaningful "
           "values for this parameter lie in the range 1 to 4 -- other "
           "values are truncted to this range.  Default values for "
           "this parameter are usually two (double buffered block "
           "encoding), but other values are occasionally selected if you "
           "have a very large number of processing threads and you may "
           "either want to either prevent this or encourage the use of even "
           "more buffering.\n";
#endif // KDU_SPEEDPACK
  out << "-fastest -- use of 16-bit data processing as often as possible.\n";
  if (comprehensive)
    out << "\tThis argument causes image samples to be coerced into a "
           "16-bit fixed-point representation even if the "
           "numerical approximation errors associated with this "
           "representation would normally be considered excessive -- makes "
           "no difference unless the source samples have a bit-depth of "
           "around 13 bits or more (depends upon other coding conditions).\n";
  out << "-precise -- force float/32-bit processing\n";
  if (comprehensive)
    out << "\tUse this option to force the internal machinery to use the "
           "full 32-bit (float/int) processing path, even if the sample "
           "precision involved suggests that the lower precision 16-bit "
           "processing path should be OK.  The current application "
           "naturally prefers to take the fastest reasonable processing "
           "path, but this option allows you to explore the impact of "
           "maximising accuracy instead.\n";
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
  out << "-stats -- report compression statistics.\n";
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
/* STATIC                    parse_simple_arguments                          */
/*****************************************************************************/

static kdu_thread_entity_affinity *
  parse_simple_arguments(kdu_args &args, char * &ifname, char * &ofname,
                         int &max_frames, bool &loop_frames, int &frame_repeat,
                         int &double_buffering_height,
                         double &rate_tolerance, bool &trim_to_rate,
                         bool &no_slope_predict, bool &want_fastest,
                         bool &want_precise, kdu_push_pull_params &pp_params,
                         int &num_engines, int &read_ahead_frames,
                         bool &no_info, bool &no_weights, 
                         bool &stats, bool &quiet)
  /* Returns an array with `num_engines' engine descriptors.  The
     `kdu_thread_entity_affinity::get_total_threads' function returns the
     total number of threads in each thread engine. */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();

  num_engines = 0;
  ifname = ofname = NULL;
  max_frames = INT_MAX;
  loop_frames = false;
  frame_repeat = 0;
  double_buffering_height = -1; // i.e., automatically select suitable value
  rate_tolerance = 0.02;
  trim_to_rate = false;
  no_slope_predict = false;
  want_fastest = false;
  want_precise = false;
  no_info = true;
  no_weights = false;
  stats = false;
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
  
  if (args.find("-loop") != NULL)
    { 
      if (max_frames == INT_MAX)
        { kdu_error e; e << "The \"-loop\" argument can only be used in "
          "conjunction with \"-frames\"."; }
      loop_frames = true;
      args.advance();
    }
  
  if (args.find("-frame_reps") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&frame_repeat) != 1) ||
          (frame_repeat < 1))
        { kdu_error e; e << "The `-frame_reps' argument requires a positive "
          "integer parameter, indicating the number of times each frame is "
          "to be compressed for throughput measurement purposes.\n"; }
      frame_repeat--; // Because `frame_repeat' is the number of repeats
      args.advance();
    }

  if (args.find("-double_buffering") != NULL)
    { 
      char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d",&double_buffering_height) != 1) ||
          (double_buffering_height < 0))
        { kdu_error e; e << "\"-double_buffering\" argument requires a "
          "positive integer, specifying the number of rows from each "
          "component which are to be double buffered, or else 0 (see "
          "`-usage' statement)."; }
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

  if (args.find("-fastest") != NULL)
    { 
      want_fastest = true;
      args.advance();
    }

  if (args.find("-precise") != NULL)
    { 
      want_precise = true;
      args.advance();
    }
  
#ifdef KDU_SPEEDPACK
  if (args.find("-bc_jobs") != NULL)
    { 
      int bc_min_job_samples = 0;
      int bc_min_jobs_across = 0;
      int bc_hires_stripes = 0;
      char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d,%d,%d",&bc_min_job_samples,
                  &bc_min_jobs_across,&bc_hires_stripes) != 3) ||
          (bc_min_job_samples < 1) || (bc_min_jobs_across < 1) ||
          (bc_hires_stripes < 1))
        { kdu_error e; e << "\"-bc_jobs\" argument requires three "
          "positive integer parameters -- \"-usage\" statement for "
          "a detailed explanation."; }
      args.advance();
      if (bc_min_job_samples > 0)
        { 
          int log2_min_job_samples=10, typical_val = 1500;
          while (typical_val < bc_min_job_samples)
            { typical_val*=2; log2_min_job_samples++; }
          int log2_ideal_job_samples = log2_min_job_samples + 2;
          pp_params.set_preferred_job_samples(log2_min_job_samples,
                                               log2_ideal_job_samples);
        }
      if (bc_hires_stripes > 0)
        { 
          if (bc_hires_stripes > 4)
            bc_hires_stripes = 4;
          pp_params.set_max_block_stripes(bc_hires_stripes,0);
        }
      if (bc_min_jobs_across > 0)
        pp_params.set_min_jobs_across(bc_min_jobs_across);
    }
#endif // KDU_SPEEDPACK
  
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

  if (args.find("-stats") != NULL)
    { 
      stats = true;
      args.advance();
    }

  if (args.find("-quiet") != NULL)
    { 
      quiet = true;
      args.advance();
    }

  kdu_thread_entity_affinity *engine_specs = NULL;
  if (args.find("-engine_threads") != NULL)
    { 
      // Start by counting the number of frame engines
      const char *cp, *string;
      int nthrds=0;
      while (((string=args.advance(false)) != NULL) &&
             (sscanf(string,"%d",&nthrds) == 1))
        num_engines++;
      if (num_engines == 0)
        { kdu_error e; e << "\"-engine_threads\" requires one or more "
          "parameter strings."; }
      engine_specs = new kdu_thread_entity_affinity[num_engines];
      
      // Now go back and start parsing the engine descriptors
      string = args.find("-engine_threads");
      assert(string != NULL);
      for (int e=0; e < num_engines; e++)
        { 
          string = args.advance();
          do { 
            nthrds = 0;
            if ((sscanf(string,"%d",&nthrds) != 1) || (nthrds < 1))
              { kdu_error e; e << "Error parsing \"-engine_threads\" "
                "parameter string.  Expected positive number of threads at:\n"
                "\t\t\"" << string << "\"."; }
            for (cp=string; (*cp >= '0') && (*cp <= '9'); cp++);
            kdu_int64 mask=0;
            kdu_int32 ctxt=0;
            if (*cp == ':')
              { 
                cp++;
                if ((sscanf(cp,"%d",&ctxt) == 1) && (ctxt >= 0))
                  for (; (*cp >= '0') && (*cp <= '9'); cp++);
                if (*cp != '(')
                  { kdu_error e; e << "Error parsing \"-engine_threads\" "
                    "parameter string.  Expected opening parenthesis `(' "
                    "at:\n\t\t\"" << cp << "\"."; }
                cp++;
                while ((*cp != ')') && (*cp != '\0'))
                  { 
                    int idx = 0;
                    if (*cp == '*')
                      { 
                        mask = -1;
                        cp++;
                      }
                    else if ((sscanf(cp,"%d",&idx) == 1) &&
                             (idx >= 0) && (idx < 64))
                      { 
                        mask |= (((kdu_int64) 1) << idx);
                        for (; (*cp >= '0') && (*cp <= '9'); cp++);
                      }
                    else
                      { kdu_error e; e << "Error parsing \"-engine_threads\" "
                        "parameter string.  Expected (relative) CPU "
                        "identifier in the range 0 to 63 (or else `*') at:\n"
                        "\t\t\"" << cp << "\".\n\t"
                        "If you want access to more than 64 logical CPUs you "
                        "must make use of affinity contexts, as explained in "
                        "the usage statement."; }
                    if (*cp == ',')
                      cp++;
                  }
                if (*cp != ')')
                  { kdu_error e; e << "Error parsing \"-engine_threads\" "
                    "parameter string.  Expected closing parenthesis `)' "
                    "at:\n\t\t\"" << cp << "\"."; }
                cp++;
              }
            if ((*cp != '+') && (*cp != '\0'))
              { kdu_error e; e << "Error parsing \"-engine_threads\" "
                "parameter string.  Expected `+' or string termination "
                "at:\n\t\t\"" << cp << "\".\n\t"
                "Note that the \"-engine_threads\" syntax changed "
                "significantly between Kakadu versions 7.4 and 7.5."; }
            string = cp+1;
            engine_specs[e].add_thread_bundle(nthrds,mask,ctxt);
          } while (*cp != '\0');
        }
      args.advance();
    }
  else
    { // Create a default set of engines.
      int num_cpus = kdu_get_num_processors();
      if (num_cpus > 64)
        { kdu_warning w; w << "Your system appears to have more than 64 "
          "logical CPUs.  To gain full access to all these CPUs you may "
          "need to provide an \"-engine_threads\" argument with explicit "
          "CPU affinity descriptors -- see the \"-usage\" statement for "
          "more information on this."; }
      int threads_per_engine = 4;
      if (num_cpus <= threads_per_engine)
        { 
          threads_per_engine = num_cpus;
          num_engines = 1;
        }
      else if (num_cpus <= (2*threads_per_engine))
        { 
          threads_per_engine = (num_cpus+1)/2;
          num_engines = 2;
        }
      else
        num_engines = 1 + ((num_cpus-1) / threads_per_engine);
      engine_specs = new kdu_thread_entity_affinity[num_engines];
      for (int e=0; e < num_engines; e++)
        engine_specs[e].add_thread_bundle(threads_per_engine,0,0);
    }
  
  read_ahead_frames = num_engines;
  if (args.find("-read_ahead") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&read_ahead_frames) != 1) ||
          (read_ahead_frames < 0))
        { kdu_error e; e << "\"-read_ahead\" argument requires a "
          "non-negative integer parameter!"; }
      args.advance();
    }

  return engine_specs;
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
/* STATIC                    reverse_source_bytes                            */
/*****************************************************************************/

static void
  reverse_source_bytes(vcom_frame_buffer *buffer)
{
  if (buffer->sample_bytes == 2)
    { 
      size_t n = buffer->sample_bytes>>1;
      kdu_uint16 val, *sp = (kdu_uint16 *)(buffer->comp_buffers[0]);
      for (; n > 3; n-=4, sp+=4)
        { // Slightly unrolled loop 
          val = sp[0];  sp[0] = (val >> 8) + (val << 8);
          val = sp[1];  sp[1] = (val >> 8) + (val << 8);
          val = sp[2];  sp[2] = (val >> 8) + (val << 8);
          val = sp[3];  sp[3] = (val >> 8) + (val << 8);
        }
      for (; n > 0; n--, sp++)
        { val = sp[0];  sp[0] = (val >> 8) + (val << 8); }
    }
  else if (buffer->sample_bytes == 4)
    { 
      size_t n = buffer->sample_bytes>>2;
      kdu_uint32 val, *sp = (kdu_uint32 *)(buffer->comp_buffers[0]);
      for (; n > 3; n-=4, sp+=4)
        { // Slightly unrolled loop 
          val = sp[0];
          sp[0] = ((val >> 24) + ((val >> 8) & 0xFF00) +
                   ((val & 0xFF00) << 8) + (val << 24));
          val = sp[1];
          sp[1] = ((val >> 24) + ((val >> 8) & 0xFF00) +
                   ((val & 0xFF00) << 8) + (val << 24));
          val = sp[2];
          sp[2] = ((val >> 24) + ((val >> 8) & 0xFF00) +
                   ((val & 0xFF00) << 8) + (val << 24));
          val = sp[3];
          sp[3] = ((val >> 24) + ((val >> 8) & 0xFF00) +
                   ((val & 0xFF00) << 8) + (val << 24));
        }
      for (; n > 0; n--, sp++)
        { 
          val = sp[0];
          sp[0] = ((val >> 24) + ((val >> 8) & 0xFF00) +
                   ((val & 0xFF00) << 8) + (val << 24));
        }
    }
  else
    { kdu_error e; e << "Source samples with 3, 5 or more bytes per sample "
      "are not supported by this demo application."; }
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
          siz->set(Ssigned,c,0,false);
          siz->set(Sprecision,c,0,8);
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
                siz->set(Ssigned,c,0,is_signed);
                siz->set(Sprecision,c,0,bits_used);
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

  char *ifname=NULL, *ofname=NULL;
  kdu_thread_entity_affinity *engine_specs = NULL;  
  FILE *vix_file = NULL;
  kdu_coords *comp_sizes = NULL;

  kdu_codestream cs_template;
  vcom_engine *engines = NULL;
  vcom_frame_queue *queue = NULL;
  kdu_long *layer_bytes = NULL;
  kdu_uint16 *layer_thresholds = NULL;

  kdu_compressed_video_target *video_tgt=NULL;
  jp2_family_tgt family_tgt;
  mj2_target movie;
  mj2_video_target *mj2_video = NULL;
  jpx_target composit_target;
  jpx_container_target jpx_container;
  vcom_jpx_target *jpx_video = NULL;
  vcom_jpx_labels *jpx_labels = NULL;
  
  int num_written_pictures = 0;
  int return_code = 0;
  try {
    kdu_args args(argc,argv,"-s");
    int max_pictures=INT_MAX, picture_repeat=0;
    bool loop_pictures=false;
    int double_buffering_stripe_height;
    double rate_tolerance;
    bool trim_to_rate, no_slope_predict, want_fastest, want_precise;
    kdu_push_pull_params pp_params;
    int num_engines, read_ahead_frames;
    bool no_info, no_weights, stats, quiet;
    engine_specs =
      parse_simple_arguments(args,ifname,ofname,max_pictures,loop_pictures,
                             picture_repeat,double_buffering_stripe_height,
                             rate_tolerance,trim_to_rate,no_slope_predict,
                             want_fastest,want_precise,pp_params,
                             num_engines,read_ahead_frames,
                             no_info,no_weights,stats,quiet);

    // Collect any parameters relevant to the SIZ marker segment
    siz_params siz;
    const char *string;
    for (string=args.get_first(); string != NULL; )
      string = args.advance(siz.parse_string(string));

    // Open input file and collect dimensional information
    int sample_bytes, bits_used, num_pictures;
    bool is_signed, lsb_aligned, is_ycc, native_order;
    kdu_uint32 timescale, frame_period;
    siz_params vix_siz; // Captures information derived from the vix/yuv file
    vix_file =
      open_vix_file(ifname,&vix_siz,num_pictures,timescale,frame_period,
                    sample_bytes,bits_used,lsb_aligned,is_signed,
                    native_order,is_ycc,quiet);
    int num_source_components = merge_siz_info(&siz,&vix_siz);
      // Note: `num_source_components' is the number of components we will be
      // supplying from the uncompressed source file.  This may be different
      // to the number of multi-component transform output components (produced
      // during decompression) that are available for binding to colour
      // channels.  These types of complexities arise only when generating
      // JPX files, since they are able to embed Part-2 codestreams.
    kdu_long total_samples=0, total_pixels=0;
    comp_sizes = new kdu_coords[num_source_components];
    for (int c=0; c < num_source_components; c++)
      { 
        vix_siz.get(Sdims,c,0,comp_sizes[c].y);
        vix_siz.get(Sdims,c,1,comp_sizes[c].x);
        kdu_long samples =
          ((kdu_long) comp_sizes[c].x) * ((kdu_long) comp_sizes[c].y);
        total_samples += samples;
        if (samples > total_pixels)
          total_pixels = samples;
      }
    if (args.find("-frate") != NULL)
      { // Frame-rate override
        int v1=0, v2=0;
        char *string = args.advance();
        if ((string == NULL) || (sscanf(string,"%d,%d",&v1,&v2) != 2) ||
            (v1 < 1) || (v2 < 1) || (v1 >= (1<<16)) || (v2 >= (1<<16)))
          { kdu_error e;
            e << "The `-frate' argument requires a comma-separated "
            "pair of positive integer parameters, no greater than 65535."; }
        frame_period = (kdu_uint32) v1; timescale = (kdu_uint32) v2;
        args.advance();
      }

    // Create the compressed data target.
    if (ofname != NULL)
      { 
        if (check_mj2_suffix(ofname))
          { 
            family_tgt.open(ofname);
            movie.open(&family_tgt);
            mj2_video = movie.add_video_track();
            mj2_video->set_timescale(timescale);
            mj2_video->set_frame_period(frame_period);
            mj2_video->set_field_order(KDU_FIELDS_NONE);
            video_tgt = mj2_video;
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
              vcom_initialize_jpx_target(composit_target,prefix_source,
                                         num_output_components,is_ycc,
                                         KDU_FIELDS_NONE,args);
            prefix_source.close();
            prefix_family_src.close();
            if (args.find("-jpx_labels") != NULL)
              { 
                const char *label_prefix = args.advance();
                if (label_prefix == NULL)
                  { kdu_error e; e << "The `-jpx_labels' argument requires a "
                    "string parameter."; }
                jpx_labels = new vcom_jpx_labels(&composit_target,
                                                 jpx_container,label_prefix);
                args.advance();
              }
            jpx_video = new vcom_jpx_target(jpx_container,jpx_labels);
            video_tgt = jpx_video;
          }
        else
          { kdu_error e; e << "Output file must have one of the suffices "
            "\".mj2\", \".jpx\" or \".jpf\".  See usage statement for more "
            "information."; }
      }
    
    // Construct template codestream from which all active codestream
    // interfaces will be copied.
    vcom_null_target null_target;
    cs_template.create(&siz,&null_target);
    for (string=args.get_first(); string != NULL; )
      string = args.advance(cs_template.access_siz()->parse_string(string));
    int num_layers = 0;
    layer_bytes = assign_layer_bytes(args,cs_template.access_siz(),num_layers);
    layer_thresholds = assign_layer_thresholds(args,num_layers);
    if (cs_template.cbr_flushing())
      { 
        if (layer_bytes[num_layers-1] <= 0)
          { kdu_error e; e << "With the `Scbr' option, you must specify a "
            "specific overall target bit-rate via `-rate'!"; }
      }
    else if ((num_layers < 2) && !quiet)
      pretty_cout << "Note:\n\tIf you want quality scalability, you should "
      "generate multiple layers with `-rate' or by using "
      "the \"Clayers\" option.\n";
    if ((cs_template.get_num_components() >= 3) && (!no_weights))
      set_default_colour_weights(cs_template.access_siz(),is_ycc,quiet);
    if (mj2_video != NULL)
      set_mj2_video_attributes(mj2_video,cs_template.access_siz(),is_ycc);
    else if (jpx_video != NULL)
      { 
        set_jpx_video_attributes(jpx_container,cs_template.access_siz(),
                                 timescale,frame_period);
        composit_target.write_headers();
      }
    cs_template.access_siz()->finalize_all();
    if (args.show_unrecognized(pretty_cout) != 0)
      { kdu_error e; e << "There were unrecognized command line arguments!"; }
    
    // Construct frame queue and hand the `cs_template' interface over
    kdu_long loop_pos = kdu_ftell(vix_file); // In case we have to loop around
    if ((num_pictures > 0) && (num_pictures < max_pictures) && !loop_pictures)
      max_pictures = num_pictures;
    int max_active_frames = num_engines + read_ahead_frames;
    queue = new vcom_frame_queue;
    queue->init(max_pictures,max_active_frames,max_active_frames+num_engines,
                num_source_components,comp_sizes,sample_bytes,bits_used,
                lsb_aligned,is_signed);
  
    // Construct compression machinery
    int t, total_engine_threads = 0;
    for (t=0; t < num_engines; t++)
      total_engine_threads += engine_specs[t].get_total_threads();
    int thread_concurrency = kdu_get_num_processors();
    if (thread_concurrency < total_engine_threads)
      thread_concurrency = total_engine_threads;
    engines = new vcom_engine[num_engines];
    for (t=0; t < num_engines; t++)
      { 
        vcom_frame *frame;
        vcom_stream *stream=NULL;
        frame = queue->get_frame_and_stream(stream);
        if (frame == NULL)
          { 
            assert(t > 0);
            num_engines = t;
            break;
          }
        kdu_codestream engine_codestream;
        engine_codestream.create(&siz,stream);
        engine_codestream.access_siz()->copy_all(cs_template.access_siz());
        engine_codestream.access_siz()->finalize_all();
        engines[t].startup(engine_codestream,queue,frame,stream,
                           t,engine_specs[t],num_layers,layer_bytes,
                           layer_thresholds,trim_to_rate,no_info,
                           !no_slope_predict,rate_tolerance,thread_concurrency,
                           double_buffering_stripe_height,
                           want_fastest,want_precise,&pp_params,
                           picture_repeat);
      }

    if (num_engines > 1)
      { // Set the management thread to have a larger priority than the
        // engine threads so as to make extra sure that we have data
        // available for the engines whenever the processing resources
        // are available to use it.
        int min_priority, max_priority;
        kdu_thread thread;
        thread.set_to_self();
        thread.get_priority(min_priority,max_priority);
        thread.set_priority(max_priority);
      }
    
    // Compress all the frames
    kdu_clock timer;
    double cpu_seconds = 0.0;
    double last_report_time = 0.0;
    bool source_exhausted=false;
    int num_generated_streams=0;
    kdu_long total_codestream_bytes=0;
    kdu_long total_compressed_bytes=0;
    kdu_long accumulated_min_slopes=0;
    kdu_long max_header_bytes=0;
    vcom_frame *frame=NULL;
    vcom_stream *stream=NULL;
    while (queue->service_queue(frame,stream,true,source_exhausted))
      { 
        if (frame != NULL)
          { 
            assert(!source_exhausted);
            size_t num_bytes_read =
              fread(frame->buffer->comp_buffers[0],1,
                    frame->buffer->frame_bytes,vix_file);
            if (num_bytes_read < frame->buffer->frame_bytes)
              { 
                if (num_bytes_read != 0)
                  { kdu_warning w; w << "Source file appears to have been "
                    "truncated part way through a frame!"; }
                if (!loop_pictures)
                  { 
                    source_exhausted = true;
                    frame = NULL; // So we don't pass anything back to queue
                  }
                else
                  { // We can keep reading around the loop
                    kdu_fseek(vix_file,loop_pos);
                    num_bytes_read =
                      fread(frame->buffer->comp_buffers[0],1,
                            frame->buffer->frame_bytes,vix_file);
                    if (num_bytes_read != frame->buffer->frame_bytes)
                      { kdu_warning w; w << "Problem trying to loop back to "
                        "start of the source file.  Perhaps the file does not "
                        "support seeking??";
                        source_exhausted = true;
                        frame = NULL;
                      }
                  }
              }
            if ((frame != NULL) && (!native_order) &&
                (frame->buffer->sample_bytes > 1))
              reverse_source_bytes(frame->buffer);
          }
        if (stream == NULL)
          continue;
        accumulated_min_slopes += (kdu_long) stream->min_slope_threshold;
        total_codestream_bytes += (kdu_long) stream->codestream_bytes;
        total_compressed_bytes += (kdu_long) stream->compressed_bytes;
        kdu_long header_bytes =
        stream->codestream_bytes - stream->compressed_bytes;
        if (header_bytes > max_header_bytes)
          max_header_bytes = header_bytes;
        num_generated_streams++;
        if (video_tgt != NULL)
          { 
            if (stream->check_failed())
              { kdu_error e; e << "It appears that one or more compressed "
                "data streams was incompletely written because the frame "
                "queue manager could not allocate enough memory to hold "
                "the contents.  You may like to try again with a smaller "
                "number of frame processing engines, a smaller "
                "\"read-ahead\" threshold, or a tighter bound on the "
                "number of buffered streams that can be maintained "
                "concurrently."; }
            video_tgt->open_image();
            stream->write_contents(video_tgt);
            video_tgt->close_image(cs_template);
            num_written_pictures++;
          }
        if (!quiet)
          { 
            cpu_seconds += timer.get_ellapsed_seconds();
            if (cpu_seconds >= (last_report_time+0.5))
              { 
                last_report_time = cpu_seconds;
                pretty_cout << num_generated_streams
                            << " frames compressed -- avg rate = "
                            << (num_generated_streams/cpu_seconds) << " fps";
                if (picture_repeat)
                  pretty_cout << " ("
                              << (num_generated_streams*(picture_repeat+1) /
                                  cpu_seconds) << " fps with repeats)";
                pretty_cout << "\n";
              }
          }
      }
    cpu_seconds += timer.get_ellapsed_seconds();
    pretty_cout << num_generated_streams
                << " frames compressed -- avg rate = "
                << (num_generated_streams/cpu_seconds) << " fps";
    if (picture_repeat)
      pretty_cout << " (" << (num_generated_streams*(picture_repeat+1) /
                              cpu_seconds) << " fps with repeats)";
    pretty_cout << "\n";
    
    // Collect final statistics
    pretty_cout << "Processed using\n\t"
                << num_engines << " frame processing engines, with\n\t"
                << total_engine_threads << " frame processing threads, in "
                << cpu_seconds << " seconds\n";
    pretty_cout << "\tThroughput = "
                << ((((double) total_samples) * 0.000001 *
                     num_generated_streams*(picture_repeat+1))/cpu_seconds)
                << " Msamples/s.\n";
    if ((num_generated_streams > 0) && stats)
      { // Print compressed data statistics here as well
        pretty_cout << "Avg codestream bytes per frame = "
                    << ((double)total_codestream_bytes) / num_generated_streams
                    << " = "
                    << (8.0 * ((double) total_codestream_bytes) /
                        (((double) total_pixels) * num_generated_streams))
                    << " bpp\n";
        pretty_cout << "Avg disortion-length slope threshold = "
                    << (int)(0.5 + (((double)accumulated_min_slopes) /
                                    num_generated_streams)) << "\n"; 
        pretty_cout << "Avg J2K packet bytes (headers+bodies) "
                       "per frame = "
                    << ((double)total_compressed_bytes) / num_generated_streams
                    << " = "
                    << (8.0 * ((double) total_compressed_bytes) /
                        (((double) total_pixels) * num_generated_streams))
                    << " bpp\n";
        pretty_cout << "Max codestream header (non-packet) bytes = "
                    << ((int) max_header_bytes) << "\n";
      }
  }
  catch (kdu_exception) {
    if (queue != NULL)
      queue->terminate();
    return_code = 1;
  }
  catch (std::bad_alloc &) {
    pretty_cerr << "Memory allocation failure detected!\n";
    if (queue != NULL)
      queue->terminate();
    return_code = 2;
  }

  // Cleanup
  if (ifname != NULL)
    delete[] ifname;
  if (ofname != NULL)
    delete[] ofname;
  if (engine_specs != NULL)
    delete[] engine_specs;
  if (vix_file != NULL)
    fclose(vix_file);
  if (comp_sizes != NULL)
    delete[] comp_sizes;
  
  if (engines != NULL)
    delete[] engines;  // Must delete engines before the queue  
  if (queue != NULL)
    delete queue;
  if (cs_template.exists())
    cs_template.destroy(); // Error was caught before transfering to `queue'
  if (layer_bytes != NULL)
    delete[] layer_bytes;
  if (layer_thresholds != NULL)
    delete[] layer_thresholds;
  
  if (video_tgt != NULL)
    video_tgt->close();
  if (jpx_video != NULL)
    delete jpx_video;
  if (jpx_labels != NULL)
    delete jpx_labels;
  if (num_written_pictures > 0)
    movie.close(); // Does nothing if not an MJ2 file.
  else
    movie.destroy(); // Also does nothing if not an MJ2 file
  composit_target.close(); // Does nothing if not a JPX file.
  family_tgt.close();
  return return_code;
}
