/*****************************************************************************/
// File: kdu_merge.cpp [scope = APPS/MERGE]
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
   Utility for merging multiple JP2/JPX files into a single JPX file.
******************************************************************************/
#include <string.h>
#include <stdio.h> // so we can use `sscanf' for arg parsing.
#include <math.h>
#include <assert.h>
#include <iostream>
// Kakadu core includes
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_compressed.h"
// Application includes
#include "kdu_args.h"
#include "kdu_file_io.h"
#include "jpx.h"
#include "kdu_merge.h"

using namespace kdu_supp; // Also includes the `kdu_core' namespace

/* ========================================================================= */
/*                         Set up messaging services                         */
/* ========================================================================= */

class kdu_stream_message : public kdu_message {
  public: // Member classes
    kdu_stream_message(std::ostream *stream)
      { this->stream = stream; }
    void put_text(const char *string)
      { (*stream) << string; }
    void flush(bool end_of_message=false)
      {
        stream->flush();
      }
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
  out << "This is Kakadu's \"kdu_merge\" application.\n";
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
  out << "-i <file 1>[+<extend>][:<track>][:<from>-<to>][,...]\n";
  if (comprehensive)
    out << "\tA comma-separated list of one or more input sources, each of "
           "which may be a JP2, JPX or MJ2 compatible file, an MJC file with "
           "the CBR attribute, or a raw codestream file.  By default, the "
           "output file is constructed by concatenating the compositing "
           "layers from each JP2/JPX input, with the fields (if interlaced) "
           "or frames from each MJ2 input, the frames from each MJC "
           "input, and the raw codestreams.  Raw codestream files are "
           "promoted to single-layer JPX sources using the policy described "
           "in connection with the \"-raw_proto\" argument.  Beyond just "
           "concatenating all the image sources, more sophisticated behaviour "
           "may be  achieved using the `-jpx_layers' or `-mj2_tracks' "
           "argument.\n"
           "\t   MJC files, generated using \"kdu_v_compress\" for example, "
           "are acceptable only if they are marked with the CBR header flag, "
           "which is written if the `Scbr' attribute was present during "
           "compression; this enables frame counting, frame seeking and "
           "related features, similar to those offered by MJ2 files.\n"
           "\t   The `+<extend>' suffix may be used to compactly specify "
           "a large number of source files whose filenames are identical "
           "apart from an integer suffix (base-10).  In this case, the "
           "`+<extend>' suffix automatically replicates the source file "
           "specification <extend> times, incrementing the index portion "
           "of the file name by 1 each time.\n"
           "\t   MJ2 file names can be followed by up to two optional "
           "qualifiers, which may be used to identify a particular video "
           "track within the file and/or a particular range of frames "
           "within the track.  The track qualifier, T, if provided, is not "
           "the actual track number within the MJ2 source, since "
           "MJ2 track numbers are arbitrary (not necessarily contiguous) "
           "natural numbers.  Instead, track qualifier values are strictly "
           "sequential.  A track qualifier of T=0 translates to the "
           "video track which has the smallest MJ2 track number; a track "
           "qualifier of T=1 translates to the video track which has "
           "the second smallest MJ2 track number; and so forth.  If no "
           "track qualifier is provided, the effect is the same as "
           "specifying T=0.  The second type of qualifier which may be "
           "provided is the range of frame indices which are to be "
           "extracted from the track in question.  The range consists of "
           "a <from> index and a <to> index.  The first frame has an index "
           "of 0 and the range is inclusive.  Note that the range "
           "refers to frames, not fields; each frame may contain two "
           "field codestreams, if the track happens to be interlaced.\n";
  out << "-o <JPX or MJ2 file>\n";
  if (comprehensive)
    out << "\tName of the JPX or MJ2 file to which the merged result is to be "
           "written.  If the file suffix is anything other than \".mj2\" or "
           "\".mjp2\" (not case sensitive), a JPX file is written.  Although "
           "not enforced here, a JPX file should really be assigned a suffix "
           "of \".jpf\" or \".jpx\", where the former is the official "
           "registered suffix -- \".jpx\" was already in use by some other "
           "registered application when the JPEG2000 project came along.\n";
  out << "-raw_proto <JP2/JPX prototype file>\n";
  if (comprehensive)
    out << "\tThis argument is required if any of the supplied input "
           "files is a raw codestream.  Each raw input file is treated as "
           "though it were a JP2/JPX file, containing one image, with colour "
           "description, channel bindings and all other relevant attributes "
           "derived from those of the `-raw_proto' prototype file's first "
           "compositing layer, except that the properties of the JP2 Image "
           "Header Box (image size, number of components and bit-depth) are "
           "derived from the input codestreams themselves.  Thus, the "
           "`-raw_proto' file may have different dimensions, number of "
           "components and bit-depth, but its colour space and associated "
           "channel bindings must be compatible with the raw input "
           "codestream(s).  It is up to you to ensure that these properties "
           "are compatible.\n";
    out << "-palette <precision> <A1>,<B1>,... <A2>,<B2>,... ...\n";
  if (comprehensive)
    out << "\tYou can use this argument to define a set of palette lookup "
           "tables that will be used to write a palette box for each "
           "output codestream, replacing any palette box that would "
           "otherwise be copied from the relevant input codestream.  The "
           "same palette is written to all output codestreams; moreover, "
           "all LUT's in the palette box must have the same bit-depth "
           "(Kakadu has no such restritions).  If `precision' is -ve, the "
           "palette LUT's are all assigned to hold signed 2's complement "
           "values with bit-depth -`precision'; otherwise, the palette LUT's "
           "hold unsigned integers with `precision' bits per entry.  Each "
           "of the argument's non-initial parameter strings holds the values "
           "belonging to a single entry in each of the palette LUT's.  So "
           "the number of entries in each palette LUT is the number of "
           "non-initial parameter strings, while the number of LUT's is the "
           "number of integers found in the comma-separated list that "
           "forms each of these parameter strings.\n";
  out << "-no_interleave -- don't interleave JPX headers with codestreams\n";
  if (comprehensive)
    out << "\tBy default, JPX codestream header boxes and compositing layer "
           "header boxes are interleaved with the codestreams themselves so "
           "as to create a representation which can be read as efficiently "
           "as possible, by applications which wish to render the image "
           "elements in sequence.  This is especially useful, if you are "
           "including a video clip in a JPX file.  If the present argument "
           "is specified, all headers will be written up front, followed by "
           "all the codestreams.  The argument is ignored if the output "
           "is an MJ2 file, rather than a JPX file.\n";
  out << "-links -- record links rather than actual codestream data\n";
  if (comprehensive)
    out << "\tBy default, codestream data from the input files is directly "
           "copied into the merged file.  If this argument is given, "
           "however, the merged file only contains links to the "
           "source codestreams.  This capability is currently supported "
           "only when writing a JPX file, in which case the links are "
           "represented using JPX fragment table (ftbl) boxes.  There "
           "is no fundamental reason, however, why it could not be "
           "supported in the future for MJ2 outputs as well.\n";
  out << "-jpx_layers (<input>:<elt>)|(<space>,[alpha,]<channels>) [...]\n";
  if (comprehensive)
    out << "\tThis argument is to be used only when generating JPX files.  "
           "By default, the original compositing layers from each JP2/JPX "
           "input source, and the codestreams belonging to each MJ2 input "
           "source are simply concatenated to form the output JPX file.  "
           "This argument, however, allows you to reorganize layers, "
           "construct new layers, define layers which share code-stream data, "
           "and even define layers which draw from codestreams in different "
           "original files.  For example, it is possible, to define two "
           "layers which render colour images from different sets of "
           "components in a single code-stream, or to define one layer "
           "which draws its colour and alpha data from different "
           "code-streams.\n"
           "\t   The argument takes one or more separate parameters, each "
           "of which defines a single compositing layer in the final JPX "
           "file.  Each argument takes one of the following two forms:\n"
           "\t\t   a) An input source index (starting from 1) separated by a "
           "colon from the index (starting from 0) of the element from "
           "that source which is to be used for the new compositing layer.  "
           "A JP2 source has one element corresponding to its single "
           "codestream.  The elements of JPX sources are their compositing "
           "layers.  The elements of MJ2 input sources are their codestreams, "
           "subject to the track and range qualifiers which may have "
           "been specified in the \"-i\" argument.  For example, \"1:3\" "
           "includes the fourth layer (or MJ2 codestream) from the first "
           "input source specified via the \"-i\" argument.\n"
           "\t\t   b) A colour space identifier, followed by a "
           "comma-separated list of colour channel specifiers.  Each channel "
           "specifier has the form <input>:<stream>/<component>[+<lut>], "
           "where <input> is the index of the input source (starting from 1), "
           "<stream> is the index of the code-stream within that input source "
           "(starting from 0), <component> is the index of the image "
           "component within that file (starting from 0), and <lut> is an "
           "optional index of a palette lookup table (0 for the first palette "
           "LUT associated with the code-stream) to be used in deriving the "
           "channel samples from the image component.  The <space> parameter "
           "may be any of the following strings:\n"
           "\t\t\t`bilevel1', `bilevel2', `YCbCr1', `YCbCr2', `YCbCr3', "
           "`PhotoYCC', `CMY', `CMYK', `YCCK', `CIELab', `CIEJab', "
           "`sLUM', `sRGB', `sYCC', `esRGB', `esYCC', `ROMMRGB', "
           "`YPbPr60',  `YPbPr50'\n"
           "\t\tA description of each of these enumerated colour spaces may "
           "be found in IS15444-2 (very sketchy) or in the comments "
           "provided with the Kakadu function `jp2_colour::init' (more "
           "comprehensive).\n"
           "\t\t  If the keyword \"alpha\" appears immediately after the "
           "colour space name (including a comma separator), the last "
           "channel specifier identifies an alpha channel.  For example, "
           "\"sLUM,alpha,1:0/0+0,2:0/0\" defines a luminance layer based on "
           "component 0 of code-stream 0 in file 1, mapped through its "
           "palette index 0, with an additional alpha channel based on "
           "component 0 or code-stream 0 in the second input file.\n";
  out << "-containers <B0>-<B1>*<repetitions> [...]\n";
  if (comprehensive)
    out << "\tThis argument may be used to pack composting layers into "
           "JPX containers.  There are several advantages to this.  First, "
           "all the layers in the container are described compactly through "
           "the container's base compositing layers.  Second, each "
           "container may define one or more presentation tracks, each of "
           "which has its own compositing instructions.  Presentation "
           "tracks are defined using the separate `-jpx_track' argument.\n"
           "\t   Each parameter string defines a single container, in terms "
           "of its base compositing layers and a number of repetitions.  "
           "Specifically, <B0> and <B1> identify the first and last base "
           "compositing layer as zero-based indices into the complete "
           "collection of compositing layers defined for the JPX file -- "
           "either implicitly or through the `-jpx_layers' argument.  The "
           "container implicitly defines the ensuing (R-1)*(B1+1-B0) "
           "compositing layers also, where R is the <repetitions> value.  "
           "It is essential that for each n in the range B0 to B1, "
           "compositing layer n has the same description as compositing "
           "layer n+r*(B1+1-B0), where r=1 through to R, except that they "
           "may use different codestreams.  Internally, the application "
           "figures out how to represent the codestreams used by these "
           "compositing layers within the container -- in some unusual "
           "cases, this might not be possible, in which case an error will "
           "be generated.  In particular, the codestreams referenced by "
           "compositing layers <B0> to <B1> must either be drawn from those "
           "represented by top-level compositing layers (not in containers) "
           "or else they must be referenced only by layers <B0> to <B1> and "
           "the layers referenced by each subsequent iteration must follow "
           "a simple pattern.\n"
           "\t   In using this argument, bear in mind that one or more "
           "initial compositing layers must not be referenced by containers, "
           "after which all other compositing layers must belong to "
           "containers.  It is also necessary to define a global "
           "JPX compositing box using the `-composit' argument, if there "
           "are any containers.  Finally, note that the last container in "
           "the file can have 0 for its <repetitions> value, meaning that "
           "it is repeated as often as required to accommodate all remaining "
           "available compositing layers -- this can be convenient.\n";
  out << "-composit [[<iterations>]@<fps>*]<layer-descriptor>[,...] [...]\n";
  if (comprehensive)
    out << "\tThis argument may be used only when generating a JPX file.  "
           "You may use it to create a top-level JPX composition box, "
           "describing either a single composited frame or an "
           "animated collection of frames, based on the compositing layers "
           "which are being written to the file.  If there are any "
           "containers (see the `-containers' argument), you must use this "
           "argument to create a top-level JPX composition box; moreover, "
           "this top-level composition box can only build its composited "
           "frames from the top-level compositing layers -- those that are "
           "not managed by containers.  The compositing layers that are "
           "managed by containers may be the subject of additional "
           "composition instructions that are described via the "
           "`-jpx_track' argument.\n"
           "\t   Animations are constructed from a sequence of frame "
           "descriptors, where each frame descriptor consists of a "
           "separate command line token.  Each frame descriptor commences "
           "with an integer number of iterations (possibly missing), followed "
           "by the `@' character and then a real-valued number of "
           "frames-per-second at which to play the frame iterations.  This "
           "is followed by a `*' character and then a comma-separated list "
           "of layer descriptors, corresponding to the succession of layers "
           "which are to be composed within each frame.  For a single "
           "composited frame, the iteration count and frame rate (everything "
           "up to and including the `*' character) may be skipped.  If the "
           "iteration count alone is skipped, the frame is repeated "
           "indefinitely.  If <iterations> = 0, the frame is made persistent, "
           "meaning that it will serve as a background for all successive "
           "frames.  If <fps> <= 0 the frame is treated as a \"PAUSE\" frame "
           "that in the animation.\n"
           "\t   Each layer descriptor has the following form:\n"
           "\t<layer>[+<inc>][:(<xc>,<yc>,<wc>,<hc>)][R<rotation>][F]"
           "[@(<x0>,<y0>,<scale>)]\n"
           "\twhere <layer> is the index (starting from 0) of the compositing "
           "layer being written into the file which is to be used; <inc> is "
           "an optional increment to be applied to the initial layer index "
           "after each successive iteration; <xc> and <yc> are real-valued "
           "relative cropping offsets to be applied to the layer, expressed "
           "in the range 0 to 1; <ws> and <hs> are real valued cropped "
           "dimensions, expressed relative to the original width and height "
           "of the layer as fractions in the range 0 to 1; <rotation> is the "
           "number of degrees through which the cropped layer is to be "
           "rotated clockwise (must be multiple of 90 degrees); the `F' is "
           "present if the layer is to be flipped horizontally after any "
           "rotation; <x0> and <y0> are horizontal and vertical offsets of "
           "the composited layer from the upper left hand corner of the "
           "compositing surface, expressed as fractions of the (scaled) layer "
           "width and height, in the range 0 to 1; and <scale> is a "
           "real-valued positive scale factor, indicating the amount by which "
           "the layer dimensions are to be scaled up when placing it on the "
           "compositing surface. You should note that the scale factor is "
           "used to create integer valued dimensions for the scaled layer, "
           "so some rounding is generally involved, and scale factors are "
           "prevented from producing scaled dimensions smaller than 1x1.\n"
           "\t   It is OK to provide compositing/animation instructions "
           "for more compositing layers than the number which are actually "
           "written to the file.  In this case, a compliant reader should "
           "process all frames/layers up until the point where there are "
           "no further layers left.\n";
  out << "-jpx_track <container>:<num-base-layers> composited-frames\n";
  if (comprehensive)
    out << "\tThis argument may be used to describe  presentation tracks "
           "(also known as \"presentation threads\") for the compositing "
           "layers that are managed by JPX containers (as created by the "
           "`-containers' argument).  The argument may appear multiple "
           "times, with each appearance describing just one presentation "
           "track within one container.  The argument's first parameter "
           "string identifies the relevant container as well as the range "
           "of compositing layers within that container that are to be "
           "used by the track's compositing instructions.  All remaining "
           "parameters correspond exactly, in syntax and interpretation, "
           "with those of the `-composit' argument, except that the "
           "compositing layers that are referenced by the compositing "
           "instructions are interpreted relative to the collection of "
           "compositing layers that belong to the track.  This collection "
           "of layers consists of <num-base-layers> base layers from the "
           "container, and all of their repetitions (see `-containers' for "
           "an explanation of base compositing layers and repetitions).  "
           "The first base layer used by this track is the one that "
           "immediately follows the last base layer used by the last track "
           "which specified the same container, if any.  In this way, "
           "the <num-base-layers> values supplied for all of the tracks "
           "associated with any given container must not exceed the number "
           "of base layers defined for that container -- however, the "
           "container may have additional base layers that are not "
           "associated with any presentation track.  The <container> "
           "value identifies the ordinal position of the container, "
           "starting from 1.\n";
  out << "-jpx_meta_swap <input>[,<input>[,...]]\n";
  if (comprehensive)
    out << "\tBy default, when writing JPX files, this demo application tries "
           "to preserve as much metadata as possible from the original JPX "
           "sources, rewriting \"numlist\" boxes from each original source "
           "file so that the embedded codestream and compositing layer "
           "indices are mapped to the new indices used in the output file.  "
           "While this is mostly the right thing to do, it can be useful to "
           "import the metadata structure from one JPX file into another "
           "one, so that the numlist entities in the imported metadata "
           "should correspond to the codestreams and compositing layers "
           "taken from a separate JPX file.  This argument allows you to do "
           "this.  The argument takes a comma-separated list of input file "
           "sources (starting from 1 for the first file supplied to \"-i\").  "
           "The metadata from the first specified <input> is treated as "
           "though it were the original metadata for the first actual "
           "source file.  Similarly, the metadata from a second specified "
           "<input> source is treated as though it were the original "
           "metadata for the second actual source file; and so forth.  If "
           "the identified <input> source does not exist, or is not a JPX "
           "files, or if no <input> source is provided for some actual "
           "source file, no metadata is available for the actual source "
           "file.  You can use this mechanism to erase metadata for some "
           "source files, or even to import JPX metadata for use with MJ2 "
           "source frames.\n";
  out << "-album [<seconds-per-frame>]\n";
  if (comprehensive)
    out << "\tThe use of this command causes a JPX composition box "
           "to be generated, the initial frames of which contain an "
           "arrangement of the complete set of compositing layers album "
           "pages.  The number of photos on each album page is internally "
           "set to 9.  Album pages are all marked as \"pause\" frames.\n"
           "\t   These are followed by frames for each individual "
           "compositing layer, scaled to just fit within the frame size.  "
           "The optional seconds-per-frame argument indicates the "
           "number of seconds between frames when the album is played as an "
           "animation at native frame rate -- the default value is "
           "2 seconds.  A typical player will skip over the initial "
           "album pages (marked as \"pause\" frames) when instructed to "
           "\"play\".\n"
           "\t   An important additional feature of the `-album' option is "
           "that metadata tags are automatically inserted for each photo,  "
           "along with index terms for each album page.  The intent is that "
           "you will open the album page using \"kdu_show\" and edit these "
           "tentative labels to add your own descriptive metadata.\n"
           "\t   You may still add additional frames "
           "to the composition by using the `-composit' argument, but this "
           "would be relatively unusual.\n";
  out << "-album2 [<seconds-per-frame]\n";
  if (comprehensive)
    out << "\tThis command is similar to \"-album\", but it just assigns "
           "one frame per compositing layer (i.e., photo).  There are no "
           "multi-photo album pages at the front, which is a simplification "
           "that is actually preferable for mobile devices, where the "
           "fixed window size makes construction of contact sheets a more "
           "natural task to defer until rendering, rather than explicitly "
           "specifying their organization.  The individual photos are also "
           "centred within their frames, whereas \"-album\" puts them in "
           "the top left corner of their frames.  A final difference is "
           "that this option generates initial metadata for each photo "
           "based on the source file-name.\n";
  out << "-mj2_tracks (P|I1|I2):<from>-[<to>][@<fps>][,<from>-...] [...]\n";
  if (comprehensive)
    out << "\tThis argument must be used when generating MJ2 files.  It "
           "takes one or more separate parameter strings, each of which "
           "represents a separate output track to be generated.  Each track "
           "must be designated as either progressive ('P') or interlaced "
           "('I1' or `I2').  The `I1' designation means that the first "
           "field provides the first row of the frame, while `I2' means "
           "that the first frame row is provided by the second field.\n"
           "\t   The components of each track are described by a "
           "comma-separated list of input element ranges and associated "
           "playback speeds.  The <from> and <to> values are zero-based "
           "indices into the collection of frames (not fields) represented by "
           "the set of all input sources supplied to the \"-i\" argument.  "
           "If you omit the <to> parameter, or specify a value which is "
           "larger than the amount of source data which is available, "
           "the generated video track continues until the end of the "
           "available data.  The <fps> value identifies play speed in "
           "frames per second; the reciprocal of this value is assigned "
           "as the duration of each corresponding output frame.  If "
           "you omit this value, timing is based on the source files, "
           "in which case the first input frame at least must be drawn "
           "from an MJ2 file.\n"
           "\t   When building an interlaced output track, all MJ2 source "
           "frames which are used must also be interlaced.  Also, each "
           "frame in the <from>-<to> range which refers to a JP2/JPX "
           "compositing layer, actually refers to two such layers, the "
           "dimensions of which must be compatible with interlacing "
           "(the heights of the two fields may differ by 1 if the interlaced "
           "frame is to have an odd height).  When building a progressive "
           "output track, all MJ2 source frames which are used must also be "
           "progressive.  Apart from these conditions, the dimensions of "
           "all frame components of a track must be compatible and "
           "JPX compositing layers which are used must be representable in "
           "the MJ2 format -- e.g., no multi-codestream layers.  Any "
           "violation of these conditions will result in a terminal error.\n";
  out << "-s <switch file>\n";
  if (comprehensive)
    out << "\tSwitch to reading arguments from a file.  In the file, argument "
           "strings are separated by whitespace characters, including spaces, "
           "tabs and new-line characters.  Comments may be included by "
           "introducing a `#' or a `%' character, either of which causes "
           "the remainder of the line to be discarded.  Any number of "
           "\"-s\" argument switch commands may be included on the command "
           "line.\n";
  out << "-quiet -- suppress informative messages.\n";
  out << "-version -- print core system version I was compiled against.\n";
  out << "-v -- abbreviation of `-version'\n";
  out << "-usage -- print a comprehensive usage statement.\n";
  out << "-u -- print a brief usage statement.\"\n\n";

  out.flush();
  exit(0);
}

/*****************************************************************************/
/* STATIC                       has_mj2_suffix                               */
/*****************************************************************************/

static bool
  has_mj2_suffix(char *fname)
  /* Returns true if the file-name has the suffix, ".mj2" or ".mjp2", where the
     check is case insensitive. */
{
  char *cp = strrchr(fname,'.');
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
  return true;
}

/*****************************************************************************/
/* STATIC                 increment_filename_index                           */
/*****************************************************************************/

static void
  increment_filename_index(char *filename, int instance)
 /* Examines the integer suffix of `filename' and augments it by
    `instance', generating an error if there are insufficient digits to
    accommodate the extension. */
{
  char *cp, *start=NULL, *suffix=strrchr(filename,'.');
  if (suffix != NULL)
    *suffix = '\0';
  int max_val = 0;
  int num_digits = 0;
  for (cp=filename; *cp != '\0'; cp++)
    {
      if ((cp[0] < '0') || (cp[0] > '9'))
        {
          start = NULL;
        }
      else if (start == NULL)
        {
          num_digits = 1;
          max_val = 9;
          start = cp;
        }
      else
        {
          num_digits++;
          max_val = max_val*10+9;
        }
    }
  if (start == NULL)
    { kdu_error e;
      e << "You have used the `+<extend>' suffix during an "
      "input source file specification, yet the file name does not appear "
      "to contain an integer suffix.  It is possible that this demo app "
      "is confused by a single filename which happens to include a `+'; "
      "you should rename the file if that is the case.";
    }
  int val=0;
  sscanf(start,"%d",&val);
  val += instance;
  if (val > max_val)
    { kdu_error e;
      e << "You have used the `+<extend>' suffix during an input source "
      "file specification, but the file name does not appear to contain "
      "sufficient digits in its integer suffix to accommodate the "
      "requested number of extensions.";
    }
  char pattern[6];
  sprintf(pattern,"%%0%dd",num_digits);
  sprintf(start,pattern,val);
  if (suffix != NULL)
    *suffix = '.';
}

/*****************************************************************************/
/* STATIC                     parse_common_args                              */
/*****************************************************************************/

static mg_source_spec *
  parse_common_args(kdu_args &args, char * &ofname,
                    mg_source_spec *&raw_proto,
                    mg_palette_spec *&user_palette,
                    bool &quiet, int &num_inputs,
                    bool &interleave_headers,
                    bool &link_to_codestreams)
  /* Returns a linked list of output layer specifications. */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();

  mg_source_spec *inputs = NULL;
  ofname = NULL;
  raw_proto = NULL;
  user_palette = NULL;
  quiet = false;
  interleave_headers = true;
  link_to_codestreams = false;
  num_inputs = 0;

  if (args.find("-raw_proto") != NULL)
    {
      const char *string = args.advance();
      raw_proto = new mg_source_spec;
      if (string == NULL)
        { kdu_error e; e << "`-raw_proto' argument requires a JP2/JPX file "
          "name as its parameter."; }
      raw_proto->filename = new char[strlen(string)+1];
      strcpy(raw_proto->filename,string);
      raw_proto->family_src.open(raw_proto->filename);
      if (raw_proto->jpx_src.open(&(raw_proto->family_src),true) <= 0)
        { kdu_error e; e << "File supplied with the `-raw_proto' argument "
          "does not appear to be a JP2/JPX compatible file."; }
      raw_proto->num_codestreams = 1;
      raw_proto->num_layers = 1;
      raw_proto->num_frames = 1;
      raw_proto->field_order = KDU_FIELDS_NONE;
      args.advance();
    }
  
  if (args.find("-i") != NULL)
    {
      mg_source_spec *tail=NULL;
      char *string, *cp;
      int extend_count=0, extend_instance=0;
      for (string=args.advance(); string != NULL; string=cp)
        { 
          if (tail == NULL)
            inputs = tail = new mg_source_spec;
          else
            tail = tail->next = new mg_source_spec;
          size_t filename_len = strlen(string);
          if ((cp=strchr(string,',')) != NULL)
            {
              filename_len = (size_t)(cp-string);
              cp++;
            }
          tail->filename = new char[filename_len+1];
          memcpy(tail->filename,string,filename_len);
          tail->filename[filename_len] = '\0';
          char *sep = strchr(tail->filename+2,':');
             // Skip first 2 chars to accommodate absolute paths in Windows
          if (sep != NULL)
            *sep = '\0';
          char *extend_sep = strchr(tail->filename,'+');
          if (extend_count > 0)
            {
              assert(extend_sep != NULL);
              *extend_sep = '\0';
              extend_instance++;
              extend_count--;
              increment_filename_index(tail->filename,extend_instance);
            }
          else if (extend_sep != NULL)
            {
              *extend_sep = '\0';
              if ((sscanf(extend_sep+1,"%d",&extend_count) == 0) ||
                  (extend_count < 1) || (extend_count > 1000000))
                { kdu_error e; e << "The `+<extend>' suffix found with "
                  "an input file does not appear to have a valid <extend> "
                  "count -- or the <extend> count exceeds 1000000, an "
                  "arbitrary limit imposed to prevent excessive resource "
                  "consumption.  You can always merge smaller collections "
                  "of source images and then merge the merged results in an "
                  "hierarchical fashion.  Problem encountered in the "
                  "following source file specification:\n\t\t" <<
                  tail->filename;
                }
              extend_instance = 0;
            }
          tail->family_src.open(tail->filename);
          if (tail->mj2_src.open(&(tail->family_src),true) > 0)
            {
              int rel_track = 0;
              int from = 0;
              int to = INT_MAX;
              // Look for optional track or frame-range qualifiers
              if (sep != NULL)
                {
                  *sep = ':';
                  if (sscanf(sep+1,"%d-%d",&from,&to) != 2)
                    {
                      if (sscanf(sep+1,"%d",&rel_track) != 1)
                        { kdu_error e; e << "Unrecognizable qualifier "
                          "found with MJ2 input specification, \""
                          << tail->filename << "\"."; }
                      char *sep2 = strchr(sep+1,':');
                      if ((sep2 != NULL) &&
                          (sscanf(sep2+1,"%d-%d",&from,&to) != 2))
                        { kdu_error e; e << "Unrecognizable second qualifier "
                          "found with MJ2 input specification, \""
                          << tail->filename << "\"."; }
                    }
                }
              if ((to < from) || (from < 0) || (rel_track < 0))
                { kdu_error e; e << "Invalid track or frame range qualifiers "
                  "found with MJ2 input specification, \""
                  << tail->filename << "\"."; }
              if (sep != NULL)
                *sep = '\0';

              kdu_uint32 track_idx = 0;
              do { // Look for video track
                  track_idx = tail->mj2_src.get_next_track(track_idx);
                  tail->video_source =
                    tail->mj2_src.access_video_track(track_idx);
                  if ((tail->video_source != NULL) && (rel_track > 0))
                    { rel_track--;  tail->video_source = NULL; }
                } while ((track_idx != 0) && (tail->video_source == NULL));
              if (tail->video_source == NULL)
                { kdu_error e; e << "MJ2 video source, \"" << tail->filename
                  << "\", contains no video track matching the requested "
                  "specifications."; }

              tail->num_frames = tail->video_source->get_num_frames();
              if ((tail->num_frames-1) > to)
                tail->num_frames = to+1;
              tail->first_frame_idx = from;
              tail->num_frames -= from;
              if (tail->num_frames < 1)
                { kdu_error e; e << "MJ2 video source, \"" << tail->filename
                  << "\", contains no frames matching the requested "
                  "specifications."; }
              tail->field_order = tail->video_source->get_field_order();
              tail->num_codestreams = tail->num_fields =
                tail->num_frames  * ((tail->field_order==KDU_FIELDS_NONE)?1:2);
              tail->num_layers = tail->num_codestreams;              
            }
          else if (tail->jpx_src.open(&(tail->family_src),true) > 0)
            {
              tail->jpx_src.count_codestreams(tail->num_codestreams);
              tail->jpx_src.count_compositing_layers(tail->num_layers);
              tail->num_frames = tail->num_fields = tail->num_layers;
              tail->field_order = KDU_FIELDS_NONE;
            }
          else
            { 
              tail->family_src.close();
              if (tail->mjc_src.open(tail->filename,tail->mjc_flags,true))
                { 
                  if (!(tail->mjc_flags & KDU_SIMPLE_VIDEO_CBR))
                    { kdu_error e; e << "MJC files can be imported by this "
                      "application only if they have the CBR flag, which "
                      "endows them with frame seeking, counting and related "
                      "capabilities.";
                    }
                  tail->mjc_src.open_image();
                  kdu_codestream cs;
                  cs.create(&(tail->mjc_src)); // Generates error if necessary
                  kdu_params *siz_params = cs.access_siz();
                  tail->mjc_num_components = cs.get_num_components();
                  siz_params->get(Ssize,0,0,tail->mjc_codestream_size.y);
                  siz_params->get(Ssize,0,1,tail->mjc_codestream_size.x);
                  cs.destroy();
                  tail->mjc_src.close_image();
                  tail->mjc_src.seek_to_frame(0);
                  tail->num_codestreams = tail->mjc_src.get_num_frames();
                  tail->num_frames = tail->num_codestreams;
                  tail->num_layers = tail->num_frames;
                  tail->num_fields = tail->num_frames;
                  tail->field_order = KDU_FIELDS_NONE;
                }
              else
                { 
                  if (raw_proto == NULL)
                    { kdu_error e; e << "You must supply a prototype file via "
                      "`-raw_proto', since one or more of the input files "
                      "is a raw codestreams.  Problem at: " <<
                      tail->filename;
                    }
                  tail->raw_src.open(tail->filename);
                  kdu_codestream cs;
                  cs.create(&(tail->raw_src)); // Generates error if necessary
                  kdu_params *siz_params = cs.access_siz();
                  siz_params->get(Ssize,0,0,tail->raw_codestream_size.y);
                  siz_params->get(Ssize,0,1,tail->raw_codestream_size.x);
                  cs.destroy();
                  tail->raw_src.close();
                  tail->num_layers = tail->num_codestreams = 1;
                  tail->num_frames = tail->num_fields = 1;
                  tail->field_order = KDU_FIELDS_NONE;
                }
            }
          tail->codestream_specs =
            new mg_codestream_spec *[tail->num_codestreams];
          memset(tail->codestream_specs,0,
                 sizeof(mg_codestream_spec *)*(size_t)(tail->num_codestreams));
          num_inputs++;
          if (extend_count > 0)
            cp = string; // Reuse the file name
        }
      args.advance();
    }
  if (num_inputs < 1)
    { kdu_error e;
      e << "You must supply at least one input file using \"-i\"."; }

  if (args.find("-o") != NULL)
    {
      const char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-o\" argument requires a file name!"; }
      ofname = new char[strlen(string)+1];
      strcpy(ofname,string);
      args.advance();
    }
  else
    { kdu_error e; e << "You must supply an output file using \"-o\"."; }
  
  if (args.find("-jpx_meta_swap") != NULL)
    {
      char *cp, *string = args.advance();
      mg_source_spec *src, *dst;
      for (dst=inputs; dst != NULL; dst=dst->next)
        dst->metadata_source = NULL; // Erase all metadata sources
      dst = inputs;
      int file_idx = 0;
      while (((file_idx=(int)strtol(string,&cp,10)) > 0) && (cp > string))
        {
          if (dst == NULL)
            { kdu_error e; e << "You have supplied more input sources than "
              "there are original actual files for the \"-jpx_meta_swap\" "
              "argument!"; }
          for (src=inputs; src != NULL; src=src->next)
            if ((--file_idx) == 0)
              break;
          dst->metadata_source = src;
          string = (*cp == ',')?(cp+1):cp;
          dst = dst->next;
        }
      args.advance();
    }
  
  if (args.find("-palette") != NULL)
    { 
      char *cp, *string = args.advance();
      int precision = 0;
      if ((string == NULL) || (sscanf(string,"%d",&precision) != 1) ||
          (precision == 0) || (precision < -32) || (precision > 32))
        { kdu_error e; e << "The \"-palette\" argument requires an "
          "integer valued first parameter (the precision) that is non-zero "
          "and no larger than 32 or smaller than -32."; }
      string = args.advance();
      int val;
      if ((string == NULL) || (sscanf(string,"%d",&val) != 1))
        { kdu_error e; e << "The \"-palette\" argument appears to have no "
          "entries -- you must supply at least two parameters beyond the "
          "<precision> parameter, each of which consists of a comma-separated "
          "list of LUT entries."; }
      int n, num_luts = 1, num_entries=0;
      for (cp=string+1; (cp = strchr(cp,',')) != NULL; cp++)
        num_luts++;
      user_palette = new mg_palette_spec(num_luts,precision);
      int *entry_buf = new int[num_luts];
      try { 
        do {
          for (cp=string, n=0; n < num_luts; n++, cp=strchr(cp,','))
            { 
              if (cp == NULL)
                { kdu_error e; e << "Too few entries in comma-separated "
                  "list for \"-palette\" argument.  Problem encountered at:\n"
                  "\t\t\"" << string << "\""; }
              if (*cp == ',')
                cp++;
              if ((sscanf(cp,"%d",&val) != 1) ||
                  ((precision > 0) && (val < 0)) ||
                  ((precision > 0) && ((val >> precision) != 0)) ||
                  ((precision < 0) && (val>0) && ((val >> -precision) != 0)) ||
                  ((precision < 0) && (val<0) && ((val >> -precision) != -1)))
                { kdu_error e; e << "Insufficient or invalid entries in the "
                  "comma-separated list for \"-palette\" argument.  Problem "
                  "encountered at:\n\t\t\"" << string << "\""; }
              entry_buf[n] = val;
            }
          user_palette->add_entry(entry_buf);
          num_entries++;
        } while (((string = args.advance()) != NULL) &&
                 (sscanf(string,"%d",&val) == 1));
        if (num_entries < 2)
          { kdu_error e; e << "The \"-palette\" argument requires at least 2 "
            "parameter string beyond the <precision> parameter, supplying at "
            "least two entries for each LUT in the palette."; }
      } catch (...) { delete[] entry_buf; throw; }
      delete[] entry_buf;
    }

  if (args.find("-no_interleave") != NULL)
    {
      interleave_headers = false;
      args.advance();
    }
  
  if (args.find("-links") != NULL)
    {
      link_to_codestreams = true;
      args.advance();
    }

  if (args.find("-quiet") != NULL)
    {
      quiet = true;
      args.advance();
    }

  return inputs;
}

/*****************************************************************************/
/* STATIC                      parse_track_specs                             */
/*****************************************************************************/

static mg_track_spec *
  parse_track_specs(kdu_args &args, int &num_tracks,
                    mg_source_spec *inputs, int num_inputs)
{
  num_tracks = 0;
  mg_track_spec *tracks=NULL, *tail=NULL;

  if (args.find("-mj2_tracks") == NULL)
    { kdu_error e; e << "You must supply an \"-mj2_tracks\" argument to "
      "generate an MJ2 output file."; }

  char *string = args.advance();
  for (; (string != NULL) && (*string != '-'); string=args.advance())
    {
      if (tail == NULL)
        tracks = tail = new mg_track_spec;
      else
        tail = tail->next = new mg_track_spec;
      num_tracks++;

      if ((string[0] == 'P') && (string[1] == ':'))
        { tail->field_order = KDU_FIELDS_NONE; string += 2; }
      else if ((string[0] == 'I') && (string[1] == '1') && (string[2] == ':'))
        { tail->field_order = KDU_FIELDS_TOP_FIRST; string += 3; }
      else if ((string[0] == 'I') && (string[1] == '2') && (string[2] == ':'))
        { tail->field_order = KDU_FIELDS_TOP_SECOND; string += 3; }
      else
        { kdu_error e; e << "Malformed track specifier found in "
          "\"-mj2_tracks\" argument at \"" << string << "\".  Expected "
          "progressive/interlaced identifier."; }

      tail->segs = NULL;
      mg_track_seg *seg = NULL;
      do {
          if (seg != NULL)
            string++;
          char *cp;
          int from, to=-1;
          double fps = -1.0;
          if (((from = (int)strtol(string,&cp,10)) < 0) ||
              (cp == string) || (*cp != '-'))
            { kdu_error e; e << "Malformed track  specifier found in "
              "\"-mj2_tracks\" argument at \"" << string << "\".  Expected "
              "range/fps segment with at least a <from> value followed by "
              "a dash."; }
          cp++;
          if ((*cp != '\0') && (*cp != ',') && (*cp != '@'))
            {
              if (((to = (int)strtol(string=cp,&cp,10)) < from) || (cp == string))
                { kdu_error e; e << "Malformed track  specifier found in "
                  "\"-mj2_tracks\" argument at \"" << string << "\".  "
                  "Expected <to> value in range/fps segment."; }
            }
          if ((*cp != '\0') && (*cp != ','))
            {
              if ((*cp != '@') || ((fps = strtod(string=cp+1,&cp)) <= 0.0))
                { kdu_error e; e << "Malformed track  specifier found in "
                  "\"-mj2_tracks\" argument at \"" << string << "\".  "
                  "Expected <fps> value in range/fps segment."; }
            }
          string = cp;
          if (seg == NULL)
            tail->segs = seg = new mg_track_seg;
          else
            seg = seg->next = new mg_track_seg;
          seg->from = from;
          seg->to = to;
          seg->fps = (float) fps;
        } while (*string == ',');

      if (*string != '\0')
        { kdu_error e; e << "Malformed track  specifier found in "
          "\"-mj2_tracks\" argument at \"" << string << "\"."; }
    }

  if (tracks == NULL)
    { kdu_error e; e << "You must supply at least one track specifier "
      "with the \"-mj2_tracks\" argument."; }

  return tracks;
}

/*****************************************************************************/
/* STATIC                     create_layer_specs                             */
/*****************************************************************************/

static mg_layer_spec *
  create_layer_specs(kdu_args &args, int &num_layers,
                     mg_source_spec *inputs, int num_inputs)
{
  mg_layer_spec *layers=NULL;
  num_layers = 0;

  if (args.find("-jpx_layers") != NULL)
    {
      int file_idx;
      char *cp, *string = args.advance();
      mg_layer_spec *tail=NULL;
      for (; (string != NULL) && (*string != '-'); string=args.advance())
        {
          if (tail == NULL)
            layers = tail = new mg_layer_spec(num_layers);
          else
            tail = tail->next = new mg_layer_spec(num_layers);
          num_layers++;
          if (((file_idx=(int)strtol(string,&cp,10)) > 0) && (cp > string) &&
              (*cp == ':'))
            { // Using an existing layer
              if (file_idx > num_inputs)
                { kdu_error e; e << "Layer specification " << num_layers
                  << " supplied with the \"-jpx_layers\" argument refers to "
                  "a non-existent input file!"; }
              for (tail->file=inputs; file_idx > 1; file_idx--)
                tail->file = tail->file->next;
              string = cp+1;
              tail->source_layer_idx = (int)strtol(string,&cp,10);
              if ((cp == string) || (*cp != '\0') ||
                  (tail->source_layer_idx < 0) ||
                  (tail->source_layer_idx >= tail->file->num_layers))
                { kdu_error e; e << "Layer specification " << num_layers
                  << " supplied with the \"-jpx_layers\" argument refers to "
                  "a non-existent compositing layer in a source file."; }
            }
          else
            { // Building a new layer from scratch
              cp = strchr(string,',');
              if (cp == NULL)
                { kdu_error e; e << "Expected a colour space type, followed "
                  "by a comma-separated list of channel specifiers in "
                  "layer specification " << num_layers
                  << " of the `-jpx_space' argument."; }
              *cp = '\0';
              if (strcmp(string,"bilevel1") == 0)
                tail->space = JP2_bilevel1_SPACE;
              else if (strcmp(string,"bilevel2") == 0)
                tail->space = JP2_bilevel2_SPACE;
              else if (strcmp(string,"YCbCr1") == 0)
                tail->space = JP2_YCbCr1_SPACE;
              else if (strcmp(string,"YCbCr2") == 0)
                tail->space = JP2_YCbCr2_SPACE;
              else if (strcmp(string,"YCbCr3") == 0)
                tail->space = JP2_YCbCr3_SPACE;
              else if (strcmp(string,"PhotoYCC") == 0)
                tail->space = JP2_PhotoYCC_SPACE;
              else if (strcmp(string,"CMY") == 0)
                tail->space = JP2_CMY_SPACE;
              else if (strcmp(string,"CMYK") == 0)
                tail->space = JP2_CMYK_SPACE;
              else if (strcmp(string,"YCCK") == 0)
                tail->space = JP2_YCCK_SPACE;
              else if (strcmp(string,"CIELab") == 0)
                tail->space = JP2_CIELab_SPACE;
              else if (strcmp(string,"CIEJab") == 0)
                tail->space = JP2_CIEJab_SPACE;
              else if (strcmp(string,"sLUM") == 0)
                tail->space = JP2_sLUM_SPACE;
              else if (strcmp(string,"sRGB") == 0)
                tail->space = JP2_sRGB_SPACE;
              else if (strcmp(string,"sYCC") == 0)
                tail->space = JP2_sYCC_SPACE;
              else if (strcmp(string,"esRGB") == 0)
                tail->space = JP2_esRGB_SPACE;
              else if (strcmp(string,"esYCC") == 0)
                tail->space = JP2_esYCC_SPACE;
              else if (strcmp(string,"ROMMRGB") == 0)
                tail->space = JP2_ROMMRGB_SPACE;
              else if (strcmp(string,"YPbPr60_SPACE") == 0)
                tail->space = JP2_YPbPr60_SPACE;
              else if (strcmp(string,"YPbPr50_SPACE") == 0)
                tail->space = JP2_YPbPr50_SPACE;
              else
                { kdu_error e; e << "Unrecognized colour space type, \""
                  << string << "\", in layer specication " << num_layers
                  << " of the `-jpx_layers' argument."; }
              string = cp+1;
              if (strncmp(string,"alpha,",strlen("alpha,")) == 0)
                {
                  tail->num_alpha_channels++; tail->num_colour_channels--;
                  string += strlen("alpha,");
                }
              mg_channel_spec *last_channel=NULL;
              while (*string != '\0')
                {
                  if (*string == ',')
                    { string++; continue; }
                  tail->num_colour_channels++;
                  if (last_channel == NULL)
                    last_channel = tail->channels = new mg_channel_spec;
                  else
                    last_channel = last_channel->next = new mg_channel_spec;
                  file_idx = (int)strtol(string,&cp,10);
                  if ((file_idx < 1) || (file_idx > num_inputs) ||
                      (cp == string) || (*cp != ':'))
                    { kdu_error e; e << "Invalid file identifier supplied "
                      "in channel specifier associated with layer "
                      "specification " << num_layers << " of the `-jpx_space' "
                      "argument.  Problem occurred at:\n\t\""
                      << string << "\"."; }
                  for (last_channel->file=inputs; file_idx > 1; file_idx--)
                    last_channel->file = last_channel->file->next;
                  string = cp+1;
                  last_channel->codestream_idx = (int)strtol(string,&cp,10);
                  if ((cp == string) || (*cp != '/') ||
                      (last_channel->codestream_idx < 0) ||
                      (last_channel->codestream_idx >=
                       last_channel->file->num_codestreams))
                    { kdu_error e; e << "Layer specification " << num_layers
                      << " supplied with the \"-jpx_layers\" argument "
                      "contains a channel specification with an invalid "
                      "code-stream identifier.  Problem encountered at:\n\t\""
                      << string << "\"."; }
                  string = cp+1;
                  last_channel->component_idx = (int)strtol(string,&cp,10);
                  if ((cp == string) || (last_channel->component_idx < 0))
                    { kdu_error e; e << "Layer specification " << num_layers
                      << " supplied with the \"-jpx_layers\" argument "
                      "contains a channel specification with an invalid "
                      "component identifier.  Problem encountered at:\n\t\""
                      << string << "\"."; }
                  string = cp;
                  if (*string == '+')
                    {
                      string++;
                      last_channel->lut_idx = (int)strtol(string,&cp,10);
                      if ((cp == string) || (last_channel->lut_idx < 0))
                        { kdu_error e; e << "Layer specification "
                          << num_layers << " supplied with the "
                          "\"-jpx_layers\" argument contains a channel "
                          "specification with an invalid palette LUT index.  "
                          "Problem encountered at:\n\t\"" << string << "\"."; }
                      string = cp;
                    }
                  if ((*string != '\0') && (*string != ','))
                    { kdu_error e; e << "Layer specification " << num_layers
                      << " supplied with the \"-jpx_layers\" argument "
                      "contains a channel specification which cannot be "
                      "completely parsed.  Problem encountered at:\n\t\""
                      << string << "\"."; }
                }
              if (tail->num_colour_channels < 1)
                { kdu_error e; e << "Insufficient channel specifiers found "
                  "in layer specification " << num_layers
                  << " of the `-jpx_space' argument."; }
            }
        }
      if (layers == NULL)
        { kdu_error e; e << "The \"-jpx_layers\" argument must be followed "
          "by at least one layer specification string."; }
    }
  else
    { // Create the default set of compositing layers
      mg_source_spec *in;
      mg_layer_spec *tail=NULL;
      for (in=inputs; in != NULL; in=in->next)
        {
          for (int l=0; l < in->num_layers; l++, num_layers++)
            {
              if (tail == NULL)
                layers = tail = new mg_layer_spec(num_layers);
              else
                tail = tail->next = new mg_layer_spec(num_layers);
              tail->file = in;
              tail->source_layer_idx = l;
            }
        }
    }

  return layers;
}

/*****************************************************************************/
/* STATIC                   create_codestream_specs                          */
/*****************************************************************************/

static mg_codestream_spec *
  create_codestream_specs(mg_layer_spec *layer_specs, int &num_codestreams)
  /* Builds the list of codestreams that are used by all the layers.
     Along the way, we also create each layer's `used_codestreams' array,
     and fill in each file's `codestream_specs' array. */
{
  mg_codestream_spec *stream=NULL, *head=NULL, *tail=NULL;
  num_codestreams = 0;
  int lyr_idx;
  mg_layer_spec *lyr;
  for (lyr_idx=0, lyr=layer_specs; lyr != NULL; lyr=lyr->next, lyr_idx++)
    { 
      int max_used_codestreams=0;
      int single_source_codestream_idx=-1;
      int num_channel_src_colours=0; // Only used with `channel_src'
      jp2_channels channel_src; // If reading channels for source JPX layer
      mg_source_spec *file=NULL; // No yet known
      if (lyr->channels != NULL)
        max_used_codestreams=lyr->num_colour_channels+lyr->num_alpha_channels;
      else
        { 
          file = lyr->file;
          if (file->video_source != NULL)
            { 
              single_source_codestream_idx = lyr->source_layer_idx;
              max_used_codestreams = 1;
            }
          else if (!file->jpx_src)
            { // Raw codestream
              single_source_codestream_idx = 0;
              max_used_codestreams = 1;
            }
          else if (file->num_codestreams == 1)
            { 
              single_source_codestream_idx = 0;
              max_used_codestreams = 1;
            }
          else
            { 
              jpx_layer_source jpx_src =
                file->jpx_src.access_layer(lyr->source_layer_idx);
              if (!jpx_src)
                { kdu_error e; e << "Cannot access layer " <<
                  lyr->source_layer_idx << " within file \"" <<
                  file->filename << "\".";
                }
              channel_src = jpx_src.access_channels();
              num_channel_src_colours = channel_src.get_num_colours();
              max_used_codestreams = num_channel_src_colours*3;
            }
        }
      
      lyr->used_codestreams = new int[max_used_codestreams];
      if (single_source_codestream_idx >= 0)
        { // Simple case; no need to scan through source chanels
          assert(file->num_codestreams > single_source_codestream_idx);
          stream = file->codestream_specs[single_source_codestream_idx];
          if (stream == NULL)
            { 
              stream = new mg_codestream_spec;
              if (tail == NULL)
                head = tail = stream;
              else
                tail = tail->next = stream;
              stream->out_codestream_idx = num_codestreams++;
              stream->source_codestream_idx = single_source_codestream_idx;
              stream->source = file;
              file->codestream_specs[single_source_codestream_idx] = stream;
            }
          lyr->num_used_codestreams = 1;
          lyr->used_codestreams[0] = stream->out_codestream_idx;
        }
      else
        { // Need to scan through all the channels, since each one might
          // use a different codestream.
          lyr->num_used_codestreams = 0;
          int chnl_idx=0;
          mg_channel_spec *chnl=lyr->channels;
          for (chnl_idx=0; chnl_idx < max_used_codestreams;
               chnl_idx++, chnl=(chnl==NULL)?NULL:chnl->next)
            { 
              int stream_idx = -1;
              if (chnl != NULL)
                { 
                  file = chnl->file;
                  stream_idx = chnl->codestream_idx;
                }
              else if (channel_src.exists())
                { 
                  int comp_idx, lut_idx, fmt;
                  if (chnl_idx < num_channel_src_colours)
                    channel_src.get_colour_mapping(chnl_idx,comp_idx,lut_idx,
                                                   stream_idx,fmt);
                  else if (chnl_idx < (2*num_channel_src_colours))
                    channel_src.get_opacity_mapping(chnl_idx -
                                                    num_channel_src_colours,
                                                    comp_idx,lut_idx,
                                                    stream_idx,fmt);
                  else
                    channel_src.get_premult_mapping(chnl_idx -
                                                    2*num_channel_src_colours,
                                                    comp_idx,lut_idx,
                                                    stream_idx,fmt);
                }
              if (stream_idx < 0)
                continue;
              if (stream_idx >= file->num_codestreams)
                { kdu_error e; e << "Metadata in file \"" <<
                  file->filename << "\" appears to be corrupt.  Referenced "
                  "source file codestream does not exist.";
                }
              stream = file->codestream_specs[stream_idx];
              if (stream == NULL)
                { 
                  stream = new mg_codestream_spec;
                  if (tail == NULL)
                    head = tail = stream;
                  else
                    tail = tail->next = stream;
                  stream->out_codestream_idx = num_codestreams++;
                  stream->source_codestream_idx = stream_idx;
                  stream->source = file;
                  file->codestream_specs[stream_idx] = stream;
                }
              lyr->used_codestreams[lyr->num_used_codestreams++] =
                stream->out_codestream_idx;
            }
          
          // Finish by removing replica used codestream indices
          for (int n=0; n < lyr->num_used_codestreams; n++)
            { 
              int idx = lyr->used_codestreams[n];
              int c = n+1;
              while (c < lyr->num_used_codestreams)
                { 
                  if (lyr->used_codestreams[c] == idx)
                    { 
                      lyr->num_used_codestreams--;
                      for (int t=c; t < lyr->num_used_codestreams; t++)
                        lyr->used_codestreams[t] = lyr->used_codestreams[t+1];
                    }
                  else
                    c++;
                }
            }
        }
    }
  return head;
}

/*****************************************************************************/
/* STATIC                   create_container_specs                           */
/*****************************************************************************/

static mg_container_spec *
  create_container_specs(kdu_args &args, int num_layers, int num_codestreams,
                         mg_layer_spec *layer_specs,
                         int &num_top_layers, int &num_top_codestreams)
{
  int num_containers=0, layers_used=0, codestreams_used=0;
  mg_container_spec *container, *head=NULL, *tail=NULL;
  num_top_layers = num_layers; // Until proven otherwise
  num_top_codestreams = num_codestreams; // Until proven otherwise
  if (args.find("-containers") != NULL)
    { 
      char *string = args.advance();
      for (; (string != NULL) && (*string != '-'); string=args.advance())
        { 
          container = new mg_container_spec;
          if (tail == NULL)
            head = tail = container;
          else
            tail = tail->next = container;
          num_containers++;
          int b0, b1, reps;
          if (sscanf(string,"%d-%d*%d",&b0,&b1,&reps) != 3)
            { kdu_error e; e << "Container specification " << num_containers
              << " supplied with the \"-containers\" argument is "
              "incorrectly formatted.";
            }
          if ((b0 < 1) || (b1 < b0) || (b1 >= num_layers) || (reps < 0))
            { kdu_error e; e << "Container specification " << num_containers
              << " has invalid parameters.  Must have 0 < <B0> <= <B1>, "
              "<repetitions> >= 0, and <B1> strictly less than the "
              "number of defined output compositing layers, " <<
              num_layers << ", in this case.";
            }
          int num_base_layers = b1+1-b0;
          container->num_repetitions = reps;
          container->num_base_layers = num_base_layers;
          if (num_containers == 1)
            { 
              layers_used = 0;
              num_top_layers = b0;
              num_top_codestreams = codestreams_used = 0;
              for (; layers_used < b0; layers_used++)
                { 
                  for (int c=0; c < layer_specs->num_used_codestreams; c++)
                    { 
                      int cidx = layer_specs->used_codestreams[c];
                      if (cidx >= num_top_codestreams)
                        num_top_codestreams = codestreams_used = cidx+1;
                    }
                  layer_specs = layer_specs->next;
                }
            }
          else if (b0 != layers_used)
            { kdu_error e; e << "First layer in container specification " <<
              num_containers << " must be the first layer not covered "
              "by the previous container, " << layers_used <<
              " in this case.";
            }
          container->base_layers = new mg_layer_spec *[num_base_layers];
          
          int lidx;
          for (lidx=0; lidx < num_base_layers; lidx++)
            { 
              assert(layer_specs != NULL); // We already checked b1
              container->base_layers[lidx] = layer_specs;
              layer_specs = layer_specs->next;
              layers_used++;
            }
          // We won't actually check that the repeated compositing layers are
          // in fact identical, but we do need to find and verify the
          // patern of codestreams that repeated layers use.
          int cs_gap = 0; // Codestream interval between repetitions, if any
          int acc_cs_gap = 0; // Accumulate across repetitions
          int min_rep_cidx=-1; // First repeated codestream for container
          for (reps--; reps != 0; reps--, acc_cs_gap+=cs_gap)
            { 
              if ((layer_specs == NULL) && (reps < 0))
                break; // indefinite repetition has used all the layers
              for (lidx=0; lidx < num_base_layers; lidx++)
                { 
                  if (layer_specs == NULL)
                    { kdu_error e; e << "Insufficient compositing layers "
                      "to accommodate all repetitions defined for "
                      "container number " << num_containers << " defined "
                      "by the `-container' argument.  You may get this "
                      "error even if you specified an indefinite repetition "
                      "factor, if each repetition of the container involves "
                      "more L > 1 compositing layers and the number of "
                      "layers available to the container is not divisible "
                      "by L -- a common error.";
                    }
                  mg_layer_spec *lyr1 = container->base_layers[lidx];
                  mg_layer_spec *lyr2 = layer_specs;
                  layer_specs = layer_specs->next;
                  layers_used++;
                  if (lyr1->num_used_codestreams != lyr2->num_used_codestreams)
                    { kdu_error e; e << "Not all repetitions of base "
                      "compositing layers defined in container number "
                      << num_containers << " use the same number of "
                      "codestreams.";
                    }
                  int c, cgap, cidx;
                  for (c=0; c < lyr1->num_used_codestreams; c++)
                    { 
                      cidx = lyr1->used_codestreams[c];
                      cgap = lyr2->used_codestreams[c] - cidx;
                      if (cgap == 0)
                        { // Needs to be a top-level codestream
                          if ((codestreams_used == num_top_codestreams) &&
                              (cidx >= num_top_codestreams))
                            num_top_codestreams = codestreams_used = cidx+1;
                          if (cidx >= num_top_codestreams)
                            { kdu_error e; e << "Repeated compositing layer "
                              "in container number " << num_containers <<
                              " requires non-repeated codestream that is "
                              "unable to be one of the top-level "
                              "codestreams.  This problem might be avoidable "
                              "by reordering codestreams, but this is beyond "
                              "the scope of the current demo app.  Read "
                              "usage statement for `-containers' carefully.";
                            }
                        }
                      else
                        { // Must be a repeated codestream
                          if (min_rep_cidx < 0)
                            { 
                              min_rep_cidx = cidx;
                              cs_gap = cgap;
                            }
                          else if (cgap != (cs_gap+acc_cs_gap))
                            { kdu_error e; e << "Repeated compositing layers "
                              "within container number " << num_containers <<
                              " must have a constant gap between repeated "
                              "codestreams that they use.";
                            }
                          else if (cidx < min_rep_cidx)
                            min_rep_cidx = cidx;
                        }
                    }
                }
              if ((min_rep_cidx >= 0) &&
                  (min_rep_cidx != codestreams_used))
                { kdu_error e; e << "Codestreams that need to be repeated as "
                  "part of a JPX container defined by the `-containers' "
                  "argument must comprise all codestream beyond those "
                  "that can be placed at the top-level of the file. This "
                  "problem might be avoidable by reordering codestreams, "
                  "but this is behond the scope of the current demo app.  "
                  "Read usage statement for `-containers' carefully.";
                }
            }
          
          container->num_base_codestreams = cs_gap;
          container->first_base_codestream_idx = min_rep_cidx;
          if (cs_gap > 0)
            codestreams_used += cs_gap * container->num_repetitions;
        }
    }
  if ((num_containers > 0) && (layers_used != num_layers))
    { kdu_error e; e << "The `-containers' argument must provide containers "
      "that cover all non top-level compositing layers.  The simplest way "
      "to do this is to arrange for the last container to have an "
      "indefinite repetition factor -- i.e., <repetitions>=0.";
    }
  return head;
}

/*****************************************************************************/
/* STATIC                    generate_album_pages                            */
/*****************************************************************************/

static int
  generate_album_pages(jpx_composition composition, mg_layer_spec *layer_specs,
                       float seconds_per_frame)
  /* This function lays out a reasonable number of compositing layers from
     the list headed by `layer_specs' on each album index pages, until all
     layers have been layed out.  The function then adds additional frames
     to the animation -- one for each compositing layer, to be played at a
     rate of `seconds_per_frame'.
        Returns the number of album index pages which were created -- not
     including the single-layer frames which follow the index pages.
        The function also records the frame index of the album index page
     which contains each compositing layer, within its `mg_layer_spec'
     record in the `layer_specs' list. */
{
  if (layer_specs == NULL)
    return 0;

  // Start by determining the album dimensions
  mg_layer_spec *lp;
  int num_layers=0;
  kdu_coords max_size, cover_size;
  for (lp=layer_specs; lp != NULL; lp=lp->next)
    {
      num_layers++;
      if (lp->size.x > max_size.x)
        max_size.x = lp->size.x;
      if (lp->size.y > max_size.y)
        max_size.y = lp->size.y;
    }
  int surround = ((max_size.x > max_size.y)?max_size.x:max_size.y) >> 4;
  cover_size.x = max_size.x*3 + surround*2;
  cover_size.y = max_size.y*3 + surround*2;
  
  // Now create the index frames and their compositing instructions
  int layer_idx = 0;
  int album_index_page = 0;
  jx_frame *frame = composition.add_frame(0,0,false);
  kdu_dims target_dims, source_dims;
  int row_idx, col_idx;
  for (row_idx=col_idx=0, lp=layer_specs;
       lp != NULL;
       lp=lp->next, col_idx++, layer_idx++)
    {
      target_dims.size = source_dims.size = lp->size;
      source_dims.pos = kdu_coords(0,0);
      if (col_idx == 3)
        { // Start a new row
          col_idx = 0;
          row_idx++;
          if (row_idx == 3)
            { // Start a new index page
              row_idx = 0;
              album_index_page++;
              frame =
                composition.add_frame(0,0,false);
            }
        }
      lp->album_page_idx = album_index_page;
      target_dims.pos.x = col_idx*(max_size.x+surround);      
      target_dims.pos.y = row_idx*(max_size.y+surround);
      double sc_x = ((double) max_size.x) / target_dims.size.x;
      double sc_y = ((double) max_size.y) / target_dims.size.y;
      if (sc_x < sc_y)
        {
          target_dims.size.x = max_size.x;
          target_dims.size.y = (int) (target_dims.size.y * sc_x + 0.5);
        }
      else
        {
          target_dims.size.y = max_size.y;
          target_dims.size.x = (int) (target_dims.size.x * sc_y + 0.5);
        }
      composition.add_instruction(frame,layer_idx,0,source_dims,target_dims);
    }

  // Finally, create frames for each individual layer
  for (layer_idx=0, lp=layer_specs; lp != NULL; lp=lp->next, layer_idx++)
    {
      target_dims.size = source_dims.size = lp->size;
      source_dims.pos = target_dims.pos = kdu_coords(0,0);
      double sc_x = ((double) cover_size.x) / target_dims.size.x;
      double sc_y = ((double) cover_size.y) / target_dims.size.y;
      double scale = (sc_x < sc_y)?sc_x:sc_y;
      target_dims.size.x = (int) (target_dims.size.x * scale);
      target_dims.size.y = (int) (target_dims.size.y * scale);

      frame = composition.add_frame((int)(seconds_per_frame*1000),0,false);
      composition.add_instruction(frame,layer_idx,0,source_dims,target_dims);
    }
  return album_index_page+1;
}

/*****************************************************************************/
/* STATIC                   generate_album2_pages                            */
/*****************************************************************************/

static void
  generate_album2_pages(jpx_composition composition,
                        mg_layer_spec *layer_specs,
                        float seconds_per_frame)
  /* This function lays out a each layer from the `layer_specs' list within
     its own frame, centering the layer within the frame and choosing a good
     common frame size.  The frames are nominally to be played at a
     rate of `seconds_per_frame'. */
{
  if (layer_specs == NULL)
    return;
  
  // Start by determining the album dimensions
  mg_layer_spec *lp;
  int num_layers=0;
  kdu_coords cover_size;
  for (lp=layer_specs; lp != NULL; lp=lp->next)
    { 
      num_layers++;
      if (lp->size.x > cover_size.x)
        cover_size.x = lp->size.x;
      if (lp->size.y > cover_size.y)
        cover_size.y = lp->size.y;
    }
 
  // Now create frames for each individual layer
  int layer_idx = 0;
  kdu_dims target_dims, source_dims;
  for (layer_idx=0, lp=layer_specs; lp != NULL; lp=lp->next, layer_idx++)
    { 
      source_dims.size = lp->size;
      source_dims.pos = kdu_coords(0,0);
      double sc_x = ((double) cover_size.x) / source_dims.size.x;
      double sc_y = ((double) cover_size.y) / source_dims.size.y;
      double scale = (sc_x < sc_y)?sc_x:sc_y;
      target_dims.size.x = (int) (source_dims.size.x * scale);
      target_dims.size.y = (int) (source_dims.size.y * scale);
      if (target_dims.size.x >= (cover_size.x-1))
        target_dims.size.x = cover_size.x;
      if (target_dims.size.y >= (cover_size.y-1))
        target_dims.size.y = cover_size.y;
      target_dims.size.x -= (cover_size.x^target_dims.size.x)&1; // Allows even
      target_dims.size.y -= (cover_size.y^target_dims.size.y)&1; // borders
      target_dims.pos.x = (cover_size.x-target_dims.size.x)>>1;
      target_dims.pos.y = (cover_size.y-target_dims.size.y)>>1;
    
      jx_frame *frame =
        composition.add_frame((int)(seconds_per_frame*1000),0,false);
      composition.add_instruction(frame,layer_idx,0,source_dims,target_dims);
    }
}

/*****************************************************************************/
/* STATIC               parse_and_write_composition_info                     */
/*****************************************************************************/

static void
  parse_and_write_composition_info(kdu_args &args, jpx_composition composition,
                                   mg_layer_spec *layer_specs,
                                   int num_top_layers,
                                   mg_container_spec *container)
  /* This function is called after the `-composit' argument has already been
     recovered from `args', or after the `-jpx_track' argument has been
     recovered and its first token parsed.  When `composition' identifies a
     presentation track, `container' points to its container.  In this
     case, `container' is used to map specified compositing layer indices
     to the relevant `mg_layer_spec' specifications. */
{
  char *cp, *delim, *string;
  while (((string = args.advance()) != NULL) && (*string != '-'))
    {
      int duration=INT_MAX, repeat_count=0;
      bool persistent = false;
      if ((delim = strchr(string,'*')) != NULL)
        { // Have repeat/frame rate spec
          float fps;
          if (sscanf(string,"@%f*",&fps) == 1)
            repeat_count = -1;
          else if ((sscanf(string,"%d@%f*",&repeat_count,&fps) != 2) ||
                   (repeat_count < 0))
            { kdu_error e; e << "Malformed iterations/fps fields in a "
              "frame specification supplied with the `-composit' argument.  "
              "Frame specification is\n\t   \"" << string << "\"."; }
          if (repeat_count == 0)
            persistent = true;
          else
            repeat_count--;
          if (fps <= 0.00001F)
            duration = 0;
          else if ((duration = (int)(0.5F + 1000.0F / fps)) < 1)
            duration = 1;
          string = delim+1;
        }
      jx_frame *frame =
        composition.add_frame(duration,repeat_count,persistent);
      while (*string != '\0')
        { 
          int layer_idx = (int)strtol(string,&cp,10);
          if ((cp == string) || (layer_idx < 0))
            { kdu_error e; e << "Illegal or missing layer index found "
              "while attempting to parse layer specification at\n\t  \""
              << string << "\"."; }
          string = cp;
          int increment = 0;
          if (*string == '+')
            {
              string++;
              increment = (int)strtol(string,&cp,10);
              if (string == cp)
                { kdu_error e; e << "Expected integer-valued increment "
                  "when parsing layer specification at\n\t  \""
                  << string << "\"."; }
              string = cp;
            }

          kdu_dims source_dims, target_dims;
          jpx_composited_orientation orientation;
          mg_layer_spec *lp=NULL;
          if (container != NULL)
            { 
              int num_base_layers = container->num_base_layers;
              assert(num_base_layers > 0);
              int rel_idx = composition.map_rel_layer_idx(layer_idx);
              rel_idx -= container->base_layers[0]->out_layer_idx;
              if ((container->num_repetitions > 0) &&
                  (rel_idx >= (container->num_repetitions*num_base_layers)))
                rel_idx = -1; // Illegal layer reference
              if (rel_idx >= 0)
                lp = container->base_layers[rel_idx % num_base_layers];
            }
          else if (layer_idx < num_top_layers)
            { 
              int n;
              for (lp=layer_specs, n=layer_idx; (n > 0) && (lp != NULL); n--)
                lp = lp->next;
            }
          if (lp == NULL)
            { kdu_error e; e << "Non-existent or non-accessible compositing "
              "layer " << layer_idx << " referenced by `-composit' or "
              "`-jpx_track' argument.  Note that the `-composit' argument "
              "may not reference compositing layers that are found within "
              "a JPX container and the `-jpx_track' argument defines "
              "composited frames using layer indices that are expressed "
              "relative to those available to the track within its "
              "container.";
            }

          source_dims.size = lp->size;
          if (*string == ':')
            {
              float xc, yc, wc, hc;
              if ((sscanf(string+1,"(%f,%f,%f,%f)",&xc,&yc,&wc,&hc) != 4) ||
                  (xc < 0.0F) || (yc < 0.0F) || (wc <= 0.0F) || (hc <= 0.0F) ||
                  (xc >= 1.0F) || (yc >= 1.0F) ||
                  ((xc+wc) > 1.01F) || ((yc+hc) > 1.01F))
                { kdu_error e; e << "Malformed cropping spec found "
                  "in layer specification -- dimensions must be positive "
                  "fractions of the full image dimensions (in the range 0 to "
                  "1.0).  Problem encountered at\n\t  \""
                  << string << "\"."; }
              string = strchr(string,')');
              assert(string != NULL);
              string++;
              source_dims.pos.x = (int) (xc*source_dims.size.x+0.5);
              source_dims.pos.y = (int) (yc*source_dims.size.y+0.5);
              if (source_dims.pos.x >= source_dims.size.x)
                source_dims.pos.x = source_dims.size.x-1;
              if (source_dims.pos.y >= source_dims.size.y)
                source_dims.pos.y = source_dims.size.y-1;
              kdu_coords max_size = source_dims.size - source_dims.pos;
              source_dims.size.x = (int) (wc*source_dims.size.x+0.5);
              source_dims.size.y = (int) (hc*source_dims.size.y+0.5);
              if (max_size.x < source_dims.size.x)
                source_dims.size.x = max_size.x;
              if (max_size.y < source_dims.size.y)
                source_dims.size.y = max_size.y;
              target_dims.size = source_dims.size;
            }
          
          if (*string == 'R')
            { 
              int r_val;
              if ((sscanf(string+1,"%d",&r_val) != 1) || ((r_val % 90) != 0))
                { kdu_error e; e << "Malformed target scaling/offset found "
                  "in layer specification.  Problem encountered at\n\t  \""
                  << string << "\"."; }
              r_val = r_val / 90; // Convert to multiple of 90 degrees
              orientation.init(r_val,false);
              string++;
              if (*string == '-')
                string++;
              while (isdigit(*string))
                string++;
            }
          
          if (*string == 'F')
            { 
              orientation.hflip = !orientation.hflip;
              string++;
            }

          target_dims.size = source_dims.size;
          if (orientation.transpose)
            target_dims.size.transpose();
          if (*string == '@')
            {
              float x0, y0, scale, val;
              if ((sscanf(string+1,"(%f,%f,%f)",&x0,&y0,&scale) != 3) ||
                  (x0 < 0.0F) || (y0 < 0.0F) || (scale <= 0.0F))
                { kdu_error e; e << "Malformed target scaling/offset found "
                  "in layer specification.  Problem encountered at\n\t  \""
                  << string << "\"."; }
              string = strchr(string,')');
              assert(string != NULL);
              string++;
              bool overflow_risk = false;
              val = target_dims.size.x * scale + 0.5F;
              if (val < 1.0F)
                target_dims.size.x = 1;
              else
                {
                  target_dims.size.x = (int) val;
                  if (val > (float) INT_MAX)
                    overflow_risk = true;
                }
              val = target_dims.size.y * scale + 0.5F;
              if (val < 1.0F)
                target_dims.size.y = 1;
              else
                {
                  target_dims.size.y = (int) val;
                  if (val > (float) INT_MAX)
                    overflow_risk = true;
                }
              val = x0 * target_dims.size.x + 0.5F;
              if ((val > (float) INT_MAX) || (val < (float) INT_MIN))
                overflow_risk = true;
              target_dims.pos.x = (int) val;
              val = y0 * target_dims.size.y + 0.5F;
              if ((val > (float) INT_MAX) || (val < (float) INT_MIN))
                overflow_risk = true;
              target_dims.pos.y = (int) val;
              if ((target_dims.pos.x+target_dims.size.x) < target_dims.size.x)
                overflow_risk = true;
              if ((target_dims.pos.y+target_dims.size.y) < target_dims.size.y)
                overflow_risk = true;
              if (overflow_risk)
                { kdu_error e; e << "Scaling factor or (x0,y0) offsets "
                  "supplied with the \"-composit\" argument appear to be too "
                  "large to avoid the risk of overflow during 32-bit integer "
                  "coordinate computations within Kakadu."; }
            }

          composition.add_instruction(frame,layer_idx,increment,
                                      source_dims,target_dims,orientation);
          if (*string == ',')
            string++;
          else if (*string != '\0')
            { kdu_error e; e << "Malformed text encountered in layer "
              "specification.  Problem encountered at\n\t  \""
              << string << "\"."; }
        }
    }
}

/*****************************************************************************/
/* STATIC                 initialize_codestream_header                       */
/*****************************************************************************/

static void
  initialize_codestream_header(mg_codestream_spec *cstream,
                               mg_source_spec *raw_proto,
                               mg_palette_spec *user_palette)
{
  mg_source_spec *in = cstream->source;
  int in_codestream_idx = cstream->source_codestream_idx;
  if (in->jpx_src.exists())
    { 
      jpx_codestream_source src =
        in->jpx_src.access_codestream(in_codestream_idx);
      jp2_palette plt = cstream->tgt.access_palette();
      if (user_palette == NULL)
        plt.copy(src.access_palette());
      else
        user_palette->write(plt);
      cstream->tgt.access_dimensions().copy(src.access_dimensions(true));
    }
  else if (in->mjc_src.exists())
    { 
      jp2_palette plt = cstream->tgt.access_palette();
      if (user_palette != NULL)
        user_palette->write(plt);
      jp2_dimensions tgt_dims = cstream->tgt.access_dimensions();
      in->mjc_src.seek_to_frame(in_codestream_idx);
      kdu_codestream cs; cs.create(&(in->mjc_src));
      tgt_dims.init(cs.access_siz());
      cs.destroy();
    }
  else if (in->video_source == NULL)
    { // Get any palette from `raw_proto'
      assert(raw_proto != NULL);
      jpx_layer_source l_src = raw_proto->jpx_src.access_layer(0);
      jpx_codestream_source src =
        raw_proto->jpx_src.access_codestream(l_src.get_codestream_id(0));
      jp2_palette plt = cstream->tgt.access_palette();
      if (user_palette == NULL)
        plt.copy(src.access_palette());
      else
        user_palette->write(plt);
      jp2_dimensions tgt_dims = cstream->tgt.access_dimensions();
      in->raw_src.open(in->filename);
      kdu_codestream cs; cs.create(&(in->raw_src));
      tgt_dims.init(cs.access_siz());
      cs.destroy();
      in->raw_src.close();
    }
  else
    { 
      jp2_dimensions in_dims = in->video_source->access_dimensions();
      jp2_palette plt = cstream->tgt.access_palette();
      if (user_palette == NULL)
        plt.copy(in->video_source->access_palette());
      else
        user_palette->write(plt);
      if (in->field_order == KDU_FIELDS_NONE)
        cstream->tgt.access_dimensions().copy(in_dims);
      else
        { // Need to convert frame dimensions to field dimensions
          kdu_coords size = in_dims.get_size();
          if (in->field_order == KDU_FIELDS_TOP_FIRST)
            size.y = (size.y + 1 - (in_codestream_idx & 1)) >> 1;
          else
            size.y = (size.y + (in_codestream_idx & 1)) >> 1;
          jp2_dimensions out_dims = cstream->tgt.access_dimensions();
          int n, num_components = in_dims.get_num_components();
          out_dims.init(size,num_components,
                        in_dims.colour_space_known(),
                        in_dims.get_compression_type());
          out_dims.finalize_compatibility(in_dims);
          for (n=0; n < num_components; n++)
            out_dims.set_precision(n,in_dims.get_bit_depth(n),
                                   in_dims.get_signed(n));
        }
    }
}

/*****************************************************************************/
/* STATIC                      copy_source_layer                             */
/*****************************************************************************/

static kdu_coords
  copy_source_layer(jpx_layer_target tgt, mg_source_spec *file,
                    int source_layer_idx, mg_source_spec *raw_proto)
  /* Copies the indicated layer from the supplied `file' to the `tgt'
     compositing layer, mapping source codestream indices to output
     codestream indices along the way.  Returns the size of the layer. */
{
  jpx_layer_source jpx_src;
  if ((file->video_source == NULL) && !file->mjc_src.exists())
    {
      if (file->jpx_src.exists())
        jpx_src = file->jpx_src.access_layer(source_layer_idx);
      else
        jpx_src = raw_proto->jpx_src.access_layer(0);
    }
  
  jp2_colour colour_tgt;
  if (jpx_src.exists())
    { 
      jp2_colour colour_src;
      for (int n=0; (colour_src = jpx_src.access_colour(n)).exists(); n++)
        { 
          colour_tgt = tgt.add_colour(colour_src.get_precedence(),
                                      colour_src.get_approximation_level());
          colour_tgt.copy(colour_src);
        }
    }
  else if (file->video_source != NULL)
    { 
      jp2_colour colour_src = file->video_source->access_colour();
      colour_tgt = tgt.add_colour(colour_src.get_precedence(),
                                  colour_src.get_approximation_level());
      colour_tgt.copy(colour_src);
    }
  else
    { 
      assert(file->mjc_src.exists());
      colour_tgt = tgt.add_colour();
      if ((file->mjc_flags & KDU_SIMPLE_VIDEO_YCC) &&
          (file->mjc_num_components >= 3))
        colour_tgt.init(JP2_sYCC_SPACE);
      else if ((file->mjc_flags & KDU_SIMPLE_VIDEO_RGB) &&
               (file->mjc_num_components >= 3))
        colour_tgt.init(JP2_sRGB_SPACE);
      else
        colour_tgt.init(JP2_sLUM_SPACE);
    }

  if (jpx_src.exists())
    tgt.access_resolution().copy(jpx_src.access_resolution());
  else
    tgt.access_resolution().copy(file->video_source->access_resolution());

  jp2_channels channels_src;
  int stream_offset = 0;
  if (jpx_src.exists())
    channels_src = jpx_src.access_channels();
  else if (file->video_source != NULL)
    { 
      channels_src = file->video_source->access_channels();
      stream_offset = source_layer_idx;
    }
  if (channels_src.exists())
    { 
      jp2_channels channels_tgt=tgt.access_channels();
      int num_colours = channels_src.get_num_colours();
      channels_tgt.init(num_colours);
      for (int n=0; n < num_colours; n++)
        { 
          int comp_idx, lut_idx, stream_idx, format, fmt_params[3]={0,0,0};
          kdu_int32 key_val;
          if (channels_src.get_colour_mapping(n,comp_idx,lut_idx,stream_idx,
                                              format,fmt_params))
            { 
              stream_idx += stream_offset;
              if (stream_idx >= file->num_codestreams)
                stream_idx = file->num_codestreams-1;
              stream_idx =
                file->codestream_specs[stream_idx]->out_codestream_idx;
              channels_tgt.set_colour_mapping(n,comp_idx,lut_idx,stream_idx,
                                              format,fmt_params);
            }
          if (channels_src.get_opacity_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format,fmt_params))
            { 
              stream_idx += stream_offset;
              if (stream_idx >= file->num_codestreams)
                stream_idx = file->num_codestreams-1;
              stream_idx =
                file->codestream_specs[stream_idx]->out_codestream_idx;
              channels_tgt.set_opacity_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format,fmt_params);
            }
          if (channels_src.get_premult_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format,fmt_params))
            { 
              stream_idx += stream_offset;
              if (stream_idx >= file->num_codestreams)
                stream_idx = file->num_codestreams-1;
              stream_idx =
                file->codestream_specs[stream_idx]->out_codestream_idx;
              channels_tgt.set_premult_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format,fmt_params);
            }
          if (channels_src.get_chroma_key(n,key_val))
            channels_tgt.set_chroma_key(n,key_val);
        }
    }

  if (jpx_src.exists())
    {
      int num_streams = jpx_src.get_num_codestreams();
      kdu_coords alignment, sampling, denominator;
      for (int n=0; n < num_streams; n++)
        { 
          int stream_idx =
            jpx_src.get_codestream_registration(n,alignment,sampling,
                                                denominator);
          stream_idx += stream_offset;
          if (stream_idx >= file->num_codestreams)
            stream_idx = file->num_codestreams-1;
          stream_idx = file->codestream_specs[stream_idx]->out_codestream_idx;
          tgt.set_codestream_registration(stream_idx,alignment,sampling,
                                          denominator);
        }
      if (file->jpx_src.exists())
        return jpx_src.get_layer_size();
      else
        { // Raw file; calculate the layer size explicitly
          kdu_coords codestream_size = file->raw_codestream_size;
          kdu_coords layer_size;
          layer_size.x = (int)
            ((((kdu_long) codestream_size.x) * sampling.x) / denominator.x);
          layer_size.y = (int)
            ((((kdu_long) codestream_size.y) * sampling.y) / denominator.y);
          return layer_size;
        }
    }
  else if (file->video_source != NULL)
    {
      kdu_coords layer_size =
        file->video_source->access_dimensions().get_size();
      if (file->field_order != KDU_FIELDS_NONE)
        { // Convert frame dimensions into field dimensions, and then
          // double the height to produce a compositing layer which will
          // be rendered at the correct size.
          if (file->field_order == KDU_FIELDS_TOP_FIRST)
            layer_size.y = (layer_size.y + 1 - (source_layer_idx & 1)) >> 1;
          else
            layer_size.y = (layer_size.y + (source_layer_idx & 1)) >> 1;
          kdu_coords field_scaling;
          field_scaling.y = 2;  field_scaling.x = 1;
          int stream_idx = stream_offset;
          assert(stream_idx < file->num_codestreams);
          stream_idx = file->codestream_specs[stream_idx]->out_codestream_idx;
          tgt.set_codestream_registration(stream_idx,kdu_coords(0,0),
                                          field_scaling,kdu_coords(1,1));
          layer_size.y *= field_scaling.y;
        }
      return layer_size;
    }
  else
    { // MJC file; layer size is just `file->mjc_codestream_size'.
      assert(file->mjc_src.exists());
      return file->mjc_codestream_size;      
    }
}

/*****************************************************************************/
/* STATIC                       create_new_layer                             */
/*****************************************************************************/

static kdu_coords
  create_new_layer(jpx_layer_target tgt, mg_layer_spec *spec)
{
  tgt.add_colour().init(spec->space);

  int n;
  jp2_channels channels = tgt.access_channels();
  channels.init(spec->num_colour_channels);
  mg_channel_spec *cp = spec->channels;
  for (n=0; n < spec->num_colour_channels; n++, cp=cp->next)
    { 
      int stream_idx = cp->codestream_idx;
      assert((stream_idx >= 0) && (stream_idx < cp->file->num_codestreams));
      stream_idx = cp->file->codestream_specs[stream_idx]->out_codestream_idx;
      channels.set_colour_mapping(n,cp->component_idx,cp->lut_idx,
                                  stream_idx);
    }
  if (spec->num_alpha_channels)
    { 
      assert(cp != NULL);
      int stream_idx = cp->codestream_idx;
      assert((stream_idx >= 0) && (stream_idx < cp->file->num_codestreams));
      stream_idx = cp->file->codestream_specs[stream_idx]->out_codestream_idx;
      for (n=0; n < spec->num_colour_channels; n++)
        channels.set_opacity_mapping(n,cp->component_idx,cp->lut_idx,
                                     stream_idx);
    }
  int cnl_idx, num_channels=spec->num_colour_channels+spec->num_alpha_channels;
  double *scale_x = new double[num_channels];
  double *scale_y = new double[num_channels];
  kdu_coords ref_size;
  for (cnl_idx=0, cp=spec->channels; cp != NULL; cp=cp->next, cnl_idx++)
    {
      kdu_coords codestream_size;
      if (cp->file->video_source != NULL)
        {
          codestream_size =
            cp->file->video_source->access_dimensions().get_size();
          if (cp->file->field_order == KDU_FIELDS_TOP_FIRST)
            codestream_size.y =
              (codestream_size.y + 1 - (cp->codestream_idx & 1)) >> 1;
          else if (cp->file->field_order == KDU_FIELDS_TOP_SECOND)
            codestream_size.y =
              (codestream_size.y + (cp->codestream_idx & 1)) >> 1;
          if (cp == spec->channels)
            {
              ref_size = codestream_size;
              if (cp->file->field_order != KDU_FIELDS_NONE)
                ref_size.y *= 2;
            }
        }
      else
        { 
          if (cp->file->jpx_src.exists())
            {
              jpx_codestream_source codestream =
                cp->file->jpx_src.access_codestream(cp->codestream_idx);
              codestream_size = codestream.access_dimensions().get_size();
            }
          else if (cp->file->mjc_src.exists())
            codestream_size = cp->file->mjc_codestream_size;
          else 
            codestream_size = cp->file->raw_codestream_size;          
          if (cp == spec->channels)
            ref_size = codestream_size;
        }

      scale_x[cnl_idx] = ((double) ref_size.x) / codestream_size.x;
      scale_y[cnl_idx] = ((double) ref_size.y) / codestream_size.y;
    }
  
  // Now figure out how we can approximate the `scale_x' and `scale_y' values
  // with a common denominator in the range 1 to 65535 and numerators in the
  // range 1 to 255.  Since we know that the first codestream is guaranteed
  // to have scale values of (1.0,1.0), we only need to consider denominators
  // in the range 1 to 255.  Can't be that bad to do a brute force search.
  kdu_coords best_denominator;
  double min_max_err_x=1.0, min_max_err_y=1.0;
  int num, den;
  for (den=1; den <= 255; den++)
    {
      double err, max_err_x=0.0, max_err_y=0.0;
      bool acceptable_x=true, acceptable_y=true;
      for (cnl_idx=0; cnl_idx < num_channels; cnl_idx++)
        {
          num = (int)(scale_x[cnl_idx]*den + 0.5);
          err = (((double) num) / (scale_x[cnl_idx]*den)) - 1.0;
          err = (err < 0.0)?-err:err;
          max_err_x = (max_err_x > err)?max_err_x:err;
          if ((num < 1) || (num > 255))
            acceptable_x = false;
          num = (int)(scale_y[cnl_idx]*den + 0.5);
          err = (((double) num) / (scale_y[cnl_idx]*den)) - 1.0;
          err = (err < 0.0)?-err:err;
          max_err_y = (max_err_y > err)?max_err_y:err;
          if ((num < 1) || (num > 255))
            acceptable_y = false;
        }
      if (acceptable_x &&
          ((max_err_x < min_max_err_x) || (best_denominator.x == 0)))
        { best_denominator.x = den; min_max_err_x = max_err_x; }
      if (acceptable_y &&
          ((max_err_y < min_max_err_y) || (best_denominator.y == 0)))
        { best_denominator.y = den; min_max_err_y = max_err_y; }
      if ((min_max_err_x < 0.000001) && (min_max_err_y < 0.000001))
        break;
    }
  
  if ((best_denominator.x == 0) || (best_denominator.y == 0))
    { kdu_error e; e << "Unable to create compositing layer based upon "
      "supplied description in `-jpx_layers' argument.  You are attempting "
      "to constitute a single compositing layer from multiple codestreams "
      "with different dimensions.  This is OK, so long as the dimensions "
      "can be related through rational sampling factors involving numerators "
      "in the range 1 to 255, but the codestreams you are combining have "
      "dimensions which are too different for this constraint (imposed "
      "by the JPEG2000 standard itself) to be satisfied.";
    }
          
  if ((min_max_err_x > 0.001) || (min_max_err_y > 0.001))
    { kdu_warning w; w << "You have supplied a `-jpx_layers' description "
      "which will constitute a compositing layer from multiple codestreams "
      "which happen to have different dimensions.  This is OK, but you should "
      "be aware that the rational scaling factors which can be used within "
      "the JPX standard to register these different codestreams are rather "
      "limited.  In particular, the worst case relative error in the "
      "rational scaling factor approximation is " << min_max_err_x <<
      " in the horizontal direction and " << min_max_err_y << " in the "
      "vertical direction.  The common rational denominator is "
      "(" << best_denominator.x << "," << best_denominator.y << ").";
    }
     
  kdu_coords layer_size, numerator, denominator=best_denominator;
  for (cnl_idx=0, cp=spec->channels; cp != NULL; cp=cp->next, cnl_idx++)
    { 
      kdu_coords codestream_size, scaled_size;
      numerator.x = (int)(scale_x[cnl_idx]*denominator.x + 0.5);
      codestream_size.x = (int)(0.5 + ref_size.x / scale_x[cnl_idx]);
      numerator.y = (int)(scale_y[cnl_idx]*denominator.y + 0.5);
      codestream_size.y = (int)(0.5 + ref_size.y / scale_y[cnl_idx]);
      scaled_size.x = (int)
        ((((kdu_long) codestream_size.x) * numerator.x) / denominator.x);
      scaled_size.y = (int)
        ((((kdu_long) codestream_size.y) * numerator.y) / denominator.y);
      if (cnl_idx == 0)
        layer_size = scaled_size;
      else
        {
          if (scaled_size.x < layer_size.x)
            layer_size.x = scaled_size.x;
          if (scaled_size.y < layer_size.y)
            layer_size.y = scaled_size.y;
        }
      int stream_idx = cp->codestream_idx;
      assert((stream_idx >= 0) && (stream_idx < cp->file->num_codestreams));
      stream_idx = cp->file->codestream_specs[stream_idx]->out_codestream_idx;
      tgt.set_codestream_registration(stream_idx,kdu_coords(0,0),
                                      numerator,denominator);
    }
  return layer_size;
}

/*****************************************************************************/
/* STATIC             write_jpx_headers_and_codestreams                      */
/*****************************************************************************/

static void
  write_jpx_headers_and_codestreams(jpx_target &out,
                                    mg_codestream_spec *stream_specs,
                                    bool interleave_headers,
                                    bool link_to_codestreams)
{
  if (!interleave_headers)
    out.write_headers();
  kdu_byte *buf = new kdu_byte[1<<16];
  mg_codestream_spec *stream;
  for (stream=stream_specs; stream != NULL; stream=stream->next)
    { 
      int num_bytes;
      assert(stream->tgt.exists());
      int base_id = stream->tgt.get_codestream_id();
      if ((base_id == stream->out_codestream_idx) && interleave_headers)
        out.write_headers(NULL,NULL,stream->out_codestream_idx);
      mg_source_spec *file = stream->source;
      jp2_input_box local_box_in, *box_in=NULL;
      jp2_output_box *box_out=NULL;
      if (!link_to_codestreams)
        box_out = stream->tgt.open_stream();
      jpx_fragment_list frags_in;
      jp2_data_references data_refs_in;
      if (file->video_source != NULL)
        { 
          int frame_idx=file->first_frame_idx, field_idx=0;
          if (file->field_order == KDU_FIELDS_NONE)
            frame_idx += stream->source_codestream_idx;
          else
            { 
              frame_idx += stream->source_codestream_idx >> 1;
              field_idx = stream->source_codestream_idx & 1;
            }
          if ((!file->video_source->seek_to_frame(frame_idx)) ||
              (file->video_source->open_stream(field_idx,&local_box_in) < 0))
            { kdu_error e; e << "Unable to open all video frames "
              "whose existence is indicated within MJ2 input file \""
              << file->filename << "\".";
            }
          box_in = &local_box_in;
        }
      else if (file->jpx_src.exists())
        { 
          jpx_codestream_source src =
            file->jpx_src.access_codestream(stream->source_codestream_idx);
          if (link_to_codestreams)
            { 
              frags_in = src.access_fragment_list();
              data_refs_in = file->jpx_src.access_data_references();
            }
          if (!(frags_in.exists() && data_refs_in.exists()))
            box_in = src.open_stream();
        }
      else if (file->mjc_src.exists())
        {  
          file->mjc_src.seek_to_frame(stream->source_codestream_idx);
          file->mjc_src.open_image();
        }
      else
        file->raw_src.open(file->filename);

      if (link_to_codestreams)
        { // Write link
          kdu_long offset, length;
          if (frags_in.exists() && data_refs_in.exists())
            { 
              int frag_idx, num_frags = frags_in.get_num_fragments();
              for (frag_idx=0; frag_idx < num_frags; frag_idx++)
                { 
                  int url_idx;
                  const char *url_path;
                  if (frags_in.get_fragment(frag_idx,url_idx,
                                            offset,length) &&
                      ((url_path=data_refs_in.get_url(url_idx)) != NULL))
                    stream->tgt.add_fragment(url_path,offset,length);
                }
            }
          else if (box_in != NULL)
            { 
              jp2_locator loc = box_in->get_locator();
              offset=loc.get_file_pos() + box_in->get_box_header_length();
              length = box_in->get_remaining_bytes();
              if (length < 0)
                for (length=0; (num_bytes=box_in->read(buf,1<<16)) > 0; )
                  length += num_bytes;
              stream->tgt.add_fragment(file->filename,offset,length,true);
            }
          else if (file->mjc_src.exists())
            { 
              length = file->mjc_src.get_remaining_bytes();
              kdu_long offset = file->mjc_src.get_image_file_pos();
              stream->tgt.add_fragment(file->filename,offset,length,true);
            }
          else
            { // Must be raw codestream source
              length = 0;
              while ((num_bytes = file->raw_src.read(buf,1<<16)) > 0)
                length += num_bytes;                  
              stream->tgt.add_fragment(file->filename,0,length,true);
            }
          stream->tgt.write_fragment_table();
        }
      else
        { 
          if (file->mjc_src.exists())
            box_out->set_target_size(file->mjc_src.get_remaining_bytes());
          else if ((box_in == NULL) || (box_in->get_remaining_bytes() < 0))
            box_out->write_header_last();
          else
            box_out->set_target_size(box_in->get_remaining_bytes());
        }

      if (box_out != NULL)
        { 
          if (box_in != NULL)
            while ((num_bytes = box_in->read(buf,1<<16)) > 0)
              box_out->write(buf,num_bytes);
          else if (file->mjc_src.exists())
            while ((num_bytes = file->mjc_src.read(buf,1<<16)) > 0)
              box_out->write(buf,num_bytes);
          else
            while ((num_bytes = file->raw_src.read(buf,1<<16)) > 0)
              box_out->write(buf,num_bytes);
          box_out->close();
        }
      if (box_in != NULL)
        box_in->close();
      else if (file->mjc_src.exists())
        file->mjc_src.close_image();
      else
        file->raw_src.close();
    }
  delete[] buf;

  if (interleave_headers)
    out.write_headers(); // Write any outstanding headers
}

/*****************************************************************************/
/* STATIC                      copy_descendants                              */
/*****************************************************************************/

static void
  copy_descendants(jpx_metanode src, jpx_metanode dst,
                   int num_src_streams, int *src_stream_map, int *stream_temp,
                   int num_dst_layers, int *dst_layer_map, int *layer_temp,
                   jpx_meta_manager dst_manager, bool inside_numlist)
  /* This recursive function copies all descendants of the `src' node to
     become descendants of the `dst' node.  Wherever a number list node is
     encountered, its entries are transcribed using the stream and layer
     mappings provided via the 3'rd, 4'th, 6'th and 7'th arguments.
     Specifically, a codestream index n in the input number list node is
     transcribed to an output codestream index of `src_stream_map[n]',
     unless it is negative, meaning that the source codestream is not used.
     Layer indices are treated somewhat differently, since a single source
     layer may potentially be used by multiple destination layers.  A
     compositing layer index of n in the input number list node is
     transcribed to the union of all indices, k, such that
     `dst_layer_map[k]'=n.
        If an input number list is associated with the composited result
     rather than a specific codestream or compositing layer, this association
     will not be passed through to the output number list, since its
     interpretation is unclear.
        If the output number list would be empty, neither that
     node, nor any of its descendants are copied.
        The `stream_temp' and `layer_temp' arrays are provided for working
     storage, allowing the function to create lists of translated codestream
     and compositing layer indices for number lists.
        The `dst_manager' object's `jpx_meta_manager::insert_node' function
     is used to create number lists so that common number lists can be
     shared.
        The `inside_numlist' argument indicates whether or not a number list
     has been seen in the ancestry of the node being copied.  If not, a
     special number list is automatically created to contain metadata which
     was global to the source.  This special number list references all
     layers and codestreams from the original source which are used by the
     output JPX file. */
{
  bool rendered_result;
  int n, d, num_descendants, num_streams, num_layers;
  src.count_descendants(num_descendants);
  if (num_descendants == 0)
    return;
  for (d=0; d < num_descendants; d++)
    { 
      jpx_metanode nd, ns = src.get_descendant(d);
      if (ns.get_numlist_info(num_streams,num_layers,rendered_result))
        { 
          int k, num_xlt_streams=0, num_xlt_layers=0;
          const int *orig_streams = ns.get_numlist_codestreams();
          const int *orig_layers = ns.get_numlist_layers();
          for (n=0; n < num_streams; n++)
            {
              k = orig_streams[n];
              if ((k >= 0) && (k < num_src_streams) &&
                  (src_stream_map[k] >= 0))
                stream_temp[num_xlt_streams++] = src_stream_map[k];
            }
          for (n=0; n < num_dst_layers; n++)
            {
              for (k=0; k < num_layers; k++)
                if (orig_layers[k] == dst_layer_map[n])
                  {
                    layer_temp[num_xlt_layers++] = n;
                    break;
                  }
            }
          nd = dst_manager.insert_node(num_xlt_streams,stream_temp,
                                       num_xlt_layers,layer_temp,false,
                                       0,NULL);
          copy_descendants(ns,nd,num_src_streams,src_stream_map,stream_temp,
                           num_dst_layers,dst_layer_map,layer_temp,
                           dst_manager,true);
        }
      else if ((ns.get_box_type() == jp2_free_4cc) ||
               (ns.get_box_type() == jp2_mdat_4cc))
        { // These need not be copied; only their descendants
          copy_descendants(ns,dst,num_src_streams,src_stream_map,stream_temp,
                           num_dst_layers,dst_layer_map,layer_temp,
                           dst_manager,inside_numlist);
        }
      else
        { // All other node types get copied, but first see if we need to
          // place them inside a special number list
          jpx_metanode container = dst;
          if (!inside_numlist)
            {
              for (n=num_streams=0; n < num_src_streams; n++)
                if (src_stream_map[n] >= 0)
                  stream_temp[num_streams++] = src_stream_map[n];
              for (n=num_layers=0; n < num_dst_layers; n++)
                if (dst_layer_map[n] >= 0)
                  layer_temp[num_layers++] = n;
              container = dst_manager.insert_node(num_streams,stream_temp,
                                                  num_layers,layer_temp,
                                                  false,0,NULL);
            }
          nd = container.add_copy(ns,false);
          if (nd.exists())
            copy_descendants(ns,nd,num_src_streams,src_stream_map,stream_temp,
                             num_dst_layers,dst_layer_map,layer_temp,
                             dst_manager,true);
        }
    }
}

/*****************************************************************************/
/* STATIC                        copy_metadata                               */
/*****************************************************************************/

static void
  copy_metadata(jpx_meta_manager meta_in, jpx_meta_manager meta_out,
                mg_source_spec *in, mg_layer_spec *layer_specs)
{
  mg_layer_spec *lp;
  int n, num_dst_layers, num_src_streams;
  int *dst_layer_map, *src_stream_map, *layer_temp, *stream_temp;

  num_src_streams = in->num_codestreams;
  src_stream_map = new int[num_src_streams];
  stream_temp = new int[num_src_streams];
  for (n=0; n < num_src_streams; n++)
    { 
      if (in->codestream_specs[n] == NULL)
        src_stream_map[n] = -1;
      else
        src_stream_map[n] = in->codestream_specs[n]->out_codestream_idx;
    }
  num_dst_layers = 0;
  for (lp=layer_specs; lp != NULL; lp=lp->next)
    num_dst_layers++;
  dst_layer_map = new int[num_dst_layers];
  layer_temp = new int[num_dst_layers];
  for (n=0, lp=layer_specs; lp != NULL; lp=lp->next, n++)
    if (lp->file == in)
      dst_layer_map[n] = lp->source_layer_idx;
    else
      dst_layer_map[n] = -1; // This destination layer does not use this source
  copy_descendants(meta_in.access_root(),meta_out.access_root(),
                   num_src_streams,src_stream_map,stream_temp,
                   num_dst_layers,dst_layer_map,layer_temp,meta_out,false);
  delete[] dst_layer_map;
  delete[] layer_temp;
  delete[] src_stream_map;
  delete[] stream_temp;
}

/*****************************************************************************/
/* STATIC                  generate_album_metadata                           */
/*****************************************************************************/

static void
 generate_album_metadata(jpx_meta_manager meta_manager,
                         mg_layer_spec *layer_specs,
                         int num_index_pages)
{
  // Start by discovering some information
  int max_photos_per_index_page = 0;
  mg_layer_spec *scan;
  int photo_idx=0; // Counts photos on an album index page
  int photos_on_page=0; // Counts photos within current album index page
  int page_idx=0; // Counts album index pages
  for (scan=layer_specs; scan != NULL; scan=scan->next)
    {
      if (scan->album_page_idx < 0)
        break;
      if ((scan->album_page_idx > page_idx) ||
          (scan->next == NULL) || (scan->next->album_page_idx < 0))
        {
          if (photos_on_page > max_photos_per_index_page)
            max_photos_per_index_page = photos_on_page;
          page_idx++;
          photos_on_page = 0;
        }
      photo_idx++;
      photos_on_page++;
    }
  int num_photos = photo_idx;
  
  // Now generate top-level index nodes
  jpx_metanode node;
  jpx_metanode indices = meta_manager.access_root().add_label("Index Pages");
  jpx_metanode photos = meta_manager.access_root().add_label("Photos");
  jpx_metanode contents = meta_manager.access_root().add_label("Contents");
  jpx_metanode places = contents.add_label("Places");
  jpx_metanode place_example = places.add_label("Example place of interest");
  jpx_metanode people = contents.add_label("People");
  people.add_label("Add names, link as grouping link from regions in photos");
  
  // Now allocate arrays to remember the quantities we need to interlink
  jpx_metanode *index_nodes = new jpx_metanode[num_index_pages];
  jpx_metanode *photo_nodes = new jpx_metanode[num_photos];
  int *layer_indices = new int[max_photos_per_index_page];
  
  // Next, generate initial labels for each frame
  page_idx = 0;
  photo_idx = 0;
  photos_on_page = 0;
  for (scan=layer_specs; scan != NULL; scan=scan->next)
    { 
      if (scan->album_page_idx < 0)
        break;
      const char *photo_format = "Photo %d";
      if (num_photos >= 10)
        photo_format = "Photo %02d";
      if (num_photos >= 100)
        photo_format = "Photo %03d";
      if (num_photos >= 1000)
        photo_format = "Photo %04d";
      char photo_label[30];
      sprintf(photo_label,photo_format,photo_idx+1);
      node = meta_manager.insert_node(0,NULL,1,&photo_idx,false,0,NULL,photos);
      photo_nodes[photo_idx] = node.add_label(photo_label);
      char description[30];
      sprintf(description,"Description of photo %d",photo_idx+1);
      photo_nodes[photo_idx].add_label(description);
      layer_indices[photos_on_page] = photo_idx; 
      photo_idx++;
      photos_on_page++;
      if ((scan->next == NULL) || (scan->next->album_page_idx < 0) ||
          (scan->next->album_page_idx > page_idx))
        {
          const char *index_format = "Index Page %d";
          if (num_index_pages >= 10)
            index_format = "Index Page %02d";
          if (num_index_pages >= 100)
            index_format = "Index Page %03d";
          char page_label[30];
          sprintf(page_label,index_format,page_idx+1);
          node = meta_manager.insert_node(0,NULL,photos_on_page,layer_indices,
                                          false,0,NULL,indices);
          index_nodes[page_idx] = node.add_label(page_label);
          page_idx++;
          photos_on_page = 0;
         }
    }
  
  // Now add links from index to photo pages and other example metadata
  for (photo_idx=0, scan=layer_specs; scan != NULL; scan=scan->next)
    {
      if (scan->album_page_idx < 0)
        break;
      page_idx = scan->album_page_idx;
      index_nodes[page_idx].add_link(photo_nodes[photo_idx],
                                     JPX_ALTERNATE_CHILD_LINK,false);
      if (photo_idx == 0)
        { 
          photo_nodes[photo_idx].add_link(place_example,
                                          JPX_GROUPING_LINK,false);
          place_example.add_link(photo_nodes[photo_idx],
                                 JPX_ALTERNATE_CHILD_LINK,false);
        }
      photo_idx++;
    }
  
  delete[] layer_indices;
  delete[] photo_nodes;
  delete[] index_nodes;
}

/*****************************************************************************/
/* STATIC                 generate_album2_metadata                           */
/*****************************************************************************/

static void
  generate_album2_metadata(jpx_meta_manager meta_manager,
                           mg_layer_spec *layer_specs)
{
  // Start by counting photos
  mg_layer_spec *scan;
  int num_photos = 0;
  for (scan=layer_specs; scan != NULL; scan=scan->next)
    num_photos++;
  
  // Generate top-level index nodes
  jpx_metanode node;
  jpx_metanode photos = meta_manager.access_root().add_label("Photos");
  jpx_metanode contents = meta_manager.access_root().add_label("Contents");
  jpx_metanode places = contents.add_label("Places");
  places.add_label("Example place of interest");
  jpx_metanode people = contents.add_label("People");
  people.add_label("Add names, link as grouping link from regions in photos");
  
  // Next, generate initial labels for each frame
  int photo_idx = 0;
  for (scan=layer_specs; scan != NULL; scan=scan->next)
    { 
      const char *fname = scan->file->filename;
      for (const char *cp=fname; *cp != '\0'; cp++)
        if (((*cp == '/') || (*cp == '\\')) && (cp[1] != '\0'))
          fname = cp+1;
      while (*fname == '.')
        fname++; // Skip leading "." characters
      
      node = meta_manager.insert_node(0,NULL,1,&photo_idx,false,0,NULL,photos);
      char *photo_name = new char[strlen(fname)+20];
      strcpy(photo_name,fname);      

      // Replace underscores with spaces and remove suffices
      char *dp;
      for (dp=photo_name; *dp != '\0'; dp++)
        if (*dp == '_')
          *dp = ' ';
        else if (*dp == '.')
          *dp = '\0';
      
      // See if the entire string is now empty
      if (photo_name[0] == '\0')
        sprintf(photo_name,"Photo %d",photo_idx);
      else if (scan->source_layer_idx > 0)
        sprintf(photo_name+strlen(photo_name)," (%d)",
                scan->source_layer_idx+1);        

      node = node.add_label(photo_name);
      delete [] photo_name;
      photo_idx++;
    }
}

/*****************************************************************************/
/* STATIC                       generate_jpx                                 */
/*****************************************************************************/

static void
  generate_jpx(jp2_family_tgt *family_tgt, kdu_args &args,
               mg_source_spec *inputs, mg_source_spec *raw_proto,
               mg_palette_spec *user_palette,
               int num_inputs, bool interleave_headers,
               bool link_to_codestreams, bool quiet)
{
  // Start by creating a full set of compositing layers
  int num_layers=0;
  mg_layer_spec *layer_specs =
    create_layer_specs(args,num_layers,inputs,num_inputs);

  // Now create codestream specs, based on codestreams that are used by
  // the compositing layers.
  int num_codestreams=0;
  mg_codestream_spec *codestreams =
    create_codestream_specs(layer_specs,num_codestreams);
  
  // Next create containers, if required
  int num_top_layers=num_layers, num_top_codestreams=num_codestreams;
  mg_container_spec *container_specs =
    create_container_specs(args,num_layers,num_codestreams,
                           layer_specs,num_top_layers,num_top_codestreams);
  
  jpx_target out;
  out.open(family_tgt);
  
  
  // Generate top-level composition layers
  int lyr_idx;
  mg_layer_spec *layer;
  for (lyr_idx=0, layer=layer_specs;
       lyr_idx < num_top_layers;
       lyr_idx++, layer=layer->next)
    {
      jpx_layer_target tgt = out.add_layer();
      if (layer->file == NULL)
        layer->size = create_new_layer(tgt,layer);
      else
        layer->size = copy_source_layer(tgt,layer->file,
                                        layer->source_layer_idx,raw_proto);
    }

  // Create composition instructions, if any
  int num_album_index_pages = 0;
  bool want_album2_metadata = false;
  if (args.find("-album") != NULL)
    {
      const char *string = args.advance();
      float secs;
      if ((string == NULL) || (sscanf(string,"%f",&secs) == 0) || (secs < 0.0))
        secs = 2.0F;
      if (string != NULL)
        args.advance();
      num_album_index_pages =
        generate_album_pages(out.access_composition(),layer_specs,secs);
    }
  else if (args.find("-album2") != NULL)
    { 
      const char *string = args.advance();
      float secs;
      if ((string == NULL) || (sscanf(string,"%f",&secs) == 0) || (secs < 0.0))
        secs = 2.0F;
      if (string != NULL)
        args.advance();
      generate_album2_pages(out.access_composition(),layer_specs,secs);
      want_album2_metadata = true;
    }
  if (args.find("-composit") != NULL)
    parse_and_write_composition_info(args,out.access_composition(),
                                     layer_specs,num_top_layers,NULL);

  // Generate top-level codestream headers
  int stream_idx;
  mg_codestream_spec *stream;
  for (stream_idx=0, stream=codestreams;
       stream_idx < num_top_codestreams;
       stream_idx++, stream=stream->next)
    { 
      stream->tgt = out.add_codestream();
      initialize_codestream_header(stream,raw_proto,user_palette);
    }
  
  // Generate JPX containers
  mg_container_spec *container;
  for (container=container_specs; container != NULL; container=container->next)
    { 
      container->tgt =
        out.add_container(container->num_base_codestreams,
                          container->num_base_layers,
                          container->num_repetitions);
      mg_codestream_spec *first_base_stream = stream;
      for (stream_idx=0;
           stream_idx < container->num_base_codestreams;
           stream_idx++, stream=stream->next)
        { 
          assert((stream != NULL) &&
                 ((stream->out_codestream_idx-stream_idx) ==
                  container->first_base_codestream_idx));
          stream->tgt = container->tgt.access_codestream(stream_idx);
          initialize_codestream_header(stream,raw_proto,user_palette);
        }
      for (lyr_idx=0; lyr_idx < container->num_base_layers; lyr_idx++)
        { 
          jpx_layer_target tgt = container->tgt.access_layer(lyr_idx);
          layer = container->base_layers[lyr_idx];
          if (layer->file == NULL)
            layer->size = create_new_layer(tgt,layer);
          else
            layer->size = copy_source_layer(tgt,layer->file,
                                            layer->source_layer_idx,raw_proto);
        }
      int reps_left = container->num_repetitions-1;
      for (; (reps_left != 0) && (stream != NULL); reps_left--)
        { 
          mg_codestream_spec *base_stream = first_base_stream;
          for (stream_idx=0;
               stream_idx < container->num_base_codestreams; stream_idx++,
               stream=stream->next, base_stream=base_stream->next)
            stream->tgt = base_stream->tgt; // Write codestreams for all
                                // repeated instances to the same object.
        }
    }
  
  // Finally generate container tracks, generating some initial
  // metadata labels to help reveal the structure and capabiities of
  // containers and presentation tracks.
  jpx_meta_manager meta_manager = out.access_meta_manager();
  while (args.find("-jpx_track") != NULL)
    { 
      char *string = args.advance();
      int cont_idx, num_track_layers;
      if ((string == NULL) ||
          (sscanf(string,"%d:%d",&cont_idx,&num_track_layers) != 2) ||
          (cont_idx < 1) || (num_track_layers < 1))
        { kdu_error e; e << "Invalid first token encountered in "
          "`-jpx_track' argument.  Container index and track layer count "
          "must both be strictly positive.";
        }
      for (container=container_specs;
           (container != NULL) && (cont_idx > 1); cont_idx--)
        container = container->next;
      if (container == NULL)
        { kdu_error e; e << "Invalid container index passed in first "
          "token to `-jpx_track' argument.  Index exceeds number of "
          "containers defined.";
        }
      jpx_composition track_comp =
        container->tgt.add_presentation_track(num_track_layers);
      parse_and_write_composition_info(args,track_comp,layer_specs,
                                       num_top_layers,container);
      container->num_tracks++;
      
      // Add some association metadata
      jpx_metanode container_root =
        meta_manager.insert_node(0,NULL,0,NULL,false,0,NULL,jpx_metanode(),
                                 container->tgt.get_container_id());
      char track_label[80];
      sprintf(track_label,"Presentation track %d",track_comp.get_track_idx());
      jpx_metanode label = container_root.add_label(track_label);
      int first_abs_layer_idx = track_comp.map_rel_layer_idx(0);
      int *track_layer_indices = new int[num_track_layers];
      for (int i=0; i < num_track_layers; i++)
        track_layer_indices[i] = first_abs_layer_idx+i;
      jpx_metanode numlist =
        label.add_numlist(0,NULL,num_track_layers,track_layer_indices,false);
      delete[] track_layer_indices;
      numlist.add_label("frames");
    }
  
  // Copy metadata, translating associated stream/layer indices
  mg_source_spec *in;
  for (in=inputs; in != NULL; in=in->next)
    if ((in->metadata_source != NULL) &&
        in->metadata_source->jpx_src.exists())
      copy_metadata(in->metadata_source->jpx_src.access_meta_manager(),
                    meta_manager,in,layer_specs);
  
  // Generate album metadata
  if (num_album_index_pages > 0)
    generate_album_metadata(meta_manager,layer_specs,num_album_index_pages);
  else if (want_album2_metadata)
    generate_album2_metadata(meta_manager,layer_specs);

  // Write JPX headers and output all the code-stream (or frag table) boxes
  write_jpx_headers_and_codestreams(out,codestreams,interleave_headers,
                                    link_to_codestreams);

  out.write_metadata();
  out.close();

  // Cleanup
  while ((layer=layer_specs) != NULL)
    {
      layer_specs = layer->next;
      delete layer;
    }
  while ((stream=codestreams) != NULL)
    { 
      codestreams = stream->next;
      delete stream;
    }
  while ((container=container_specs) != NULL)
    { 
      container_specs = container->next;
      delete container;
    }

  // Statistics
  if (!quiet)
    {
      pretty_cout << "\nMerged " << num_codestreams
                  << " code-streams from some or all of "
                  << num_inputs << " input files\n"
                  << "Wrote " << num_layers << " compositing layers into the "
                     "output file.\n";
    }
}

/*****************************************************************************/
/* STATIC                  configure_track_params                            */
/*****************************************************************************/

static void
  configure_track_params(mj2_video_target *tgt, mg_source_spec *in,
                         int source_layer_idx, mg_source_spec *raw_proto,
                         mg_palette_spec *user_palette)
  /* Initializes the palette, resolution, colour and other specifications
     associated with the `tgt' object, based upon the `in' object.  If
     `in' represents a JP2/JPX source, `source_layer_idx' identifies the
     particular compositing layer to be used. */
{
  if (in->video_source != NULL)
    { 
      kdu_int16 op_red, op_green, op_blue, mode =
        in->video_source->get_graphics_mode(op_red,op_green,op_blue);
      tgt->set_graphics_mode(mode,op_red,op_green,op_blue);
      tgt->access_colour().copy(in->video_source->access_colour());
      jp2_palette plt = tgt->access_palette();
      if (user_palette == NULL)
        plt.copy(in->video_source->access_palette());
      else
        user_palette->write(plt);
      tgt->access_channels().copy(in->video_source->access_channels());
      tgt->access_resolution().copy(in->video_source->access_resolution());
    }
  else if (in->mjc_src.exists())
    { 
      jp2_colour colour_tgt = tgt->access_colour();
      if ((in->mjc_flags & KDU_SIMPLE_VIDEO_YCC) &&
          (in->mjc_num_components >= 3))
        colour_tgt.init(JP2_sYCC_SPACE);
      else if ((in->mjc_flags & KDU_SIMPLE_VIDEO_RGB) &&
               (in->mjc_num_components >= 3))
        colour_tgt.init(JP2_sRGB_SPACE);
      else
        colour_tgt.init(JP2_sLUM_SPACE);
    }
  else
    { 
      jpx_layer_source jpx_src;
      jpx_codestream_source cs_src;
      if (in->jpx_src.exists())
        {
          jpx_src = in->jpx_src.access_layer(source_layer_idx);
          if (jpx_src.get_num_codestreams() != 1)
            { kdu_error e; e << "Trying to include a complex JPX compositing "
              "layer (one with multiple codestreams) as a frame in the "
              "target MJ2 file.  Problem encountered in layer " <<
              source_layer_idx << " (starting from 1) of \""
            << in->filename << "\"."; }
          cs_src = in->jpx_src.access_codestream(jpx_src.get_codestream_id(0));
        }
      else
        {
          assert(raw_proto != NULL);
          jpx_src = raw_proto->jpx_src.access_layer(0);
          cs_src =
            raw_proto->jpx_src.access_codestream(jpx_src.get_codestream_id(0));
        }
      
      jp2_palette plt = tgt->access_palette();
      if (user_palette == NULL)
        plt.copy(cs_src.access_palette());
      else
        user_palette->write(plt);
      tgt->access_colour().copy(jpx_src.access_colour(0));
      tgt->access_resolution().copy(jpx_src.access_resolution());
      jp2_channels channels_src = jpx_src.access_channels();
      jp2_channels channels_tgt = tgt->access_channels();
      int n, num_colours = channels_src.get_num_colours();
      int alpha_comp = -1, premult_alpha_comp = -1;
      channels_tgt.init(num_colours);
      bool non_default_format=false;
      for (n=0; n < num_colours; n++)
        { 
          int comp_idx, lut_idx, stream_idx, format;
          if (channels_src.get_colour_mapping(n,comp_idx,lut_idx,stream_idx,
                                              format))
            { 
              if (format != JP2_CHANNEL_FORMAT_DEFAULT)
                non_default_format = true;
              channels_tgt.set_colour_mapping(n,comp_idx,lut_idx,0);
            }
          if (channels_src.get_opacity_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format))
            { 
              if (format != JP2_CHANNEL_FORMAT_DEFAULT)
                non_default_format = true;
              channels_tgt.set_opacity_mapping(n,comp_idx,lut_idx,0);
              if (n == 0)
                alpha_comp = comp_idx;
              else if (alpha_comp != comp_idx)
                alpha_comp = -1;
            }
          if (channels_src.get_premult_mapping(n,comp_idx,lut_idx,stream_idx,
                                               format))
            { 
              if (format != JP2_CHANNEL_FORMAT_DEFAULT)
                non_default_format = true;
              channels_tgt.set_premult_mapping(n,comp_idx,lut_idx,0);
              alpha_comp = -1;
              if (n == 0)
                premult_alpha_comp = comp_idx;
              else if (premult_alpha_comp != comp_idx)
                premult_alpha_comp = -1;
            }
        }
      if (alpha_comp >= 0)
        tgt->set_graphics_mode(MJ2_GRAPHICS_ALPHA);
      else if (premult_alpha_comp >= 0)
        tgt->set_graphics_mode(MJ2_GRAPHICS_PREMULT_ALPHA);
      if (non_default_format)
        { kdu_warning w; w << "Non-default pixel format encountered in "
          "source file(s) cannot be legally embedded in target MJ2 file.  "
          "Pixel values are not likely to render correctly.";
        }
    }
}

/*****************************************************************************/
/* STATIC                     write_mj2_image                                */
/*****************************************************************************/

static void
  write_mj2_image(mj2_video_target *tgt, mg_source_spec *in,
                  int source_layer_idx)
  /* Uses `tgt::open_image' and `tgt::close_image' to write a single
     image (single field for interlaced tracks), copying its contents
     from the codestream belonging to the `source_layer_idx' compositing
     layer of a JPX source or to the `source_layer_idx'th codestream of
     an MJ2 source. */
{
  kdu_codestream codestream; // We need this to execute `tgt->close_image'.
  jpx_input_box box_in;
  if (in->video_source != NULL)
    {
      int frame_idx, field_idx;
      if (in->field_order == KDU_FIELDS_NONE)
        { frame_idx = source_layer_idx;  field_idx = 0; }
      else
        { frame_idx = source_layer_idx>>1; field_idx = source_layer_idx&1; }
      frame_idx += in->first_frame_idx;
      in->video_source->seek_to_frame(frame_idx);
      in->video_source->open_stream(field_idx,&box_in);
      codestream.create(&box_in);
      box_in.close();
      in->video_source->open_stream(field_idx,&box_in);
    }
  else if (in->jpx_src.exists())
    {
      jpx_layer_source jpx_src = in->jpx_src.access_layer(source_layer_idx);
      jpx_codestream_source cs_src =
        in->jpx_src.access_codestream(jpx_src.get_codestream_id(0));
      cs_src.open_stream(&box_in);
      codestream.create(&box_in);
      box_in.close();
      cs_src.open_stream(&box_in);
    }
  else if (in->mjc_src.exists())
    { 
      in->mjc_src.seek_to_frame(source_layer_idx);
      in->mjc_src.open_image();
      codestream.create(&(in->mjc_src));
      in->mjc_src.seek(0);
    }
  else
    { 
      in->raw_src.open(in->filename);
      codestream.create(&(in->raw_src));
      in->raw_src.seek(0);
    }

  int num_bytes;
  kdu_byte *buf = new kdu_byte[1<<16];
  tgt->open_image();
  if (box_in.exists())
    while ((num_bytes = box_in.read(buf,1<<16)) > 0)
      tgt->write(buf,num_bytes);
  else if (in->mjc_src.exists())
    while ((num_bytes = in->mjc_src.read(buf,1<<16)) > 0)
      tgt->write(buf,num_bytes);
  else
    while ((num_bytes = in->raw_src.read(buf,1<<16)) > 0)
      tgt->write(buf,num_bytes);
  tgt->close_image(codestream);
  codestream.destroy();
  delete[] buf;
  if (box_in.exists())
    box_in.close();
  else if (in->mjc_src.exists())
    in->mjc_src.close_image();
  else
    in->raw_src.close();
}

/*****************************************************************************/
/* STATIC                       generate_mj2                                 */
/*****************************************************************************/

static void
  generate_mj2(jp2_family_tgt *family_tgt, kdu_args &args,
               mg_source_spec *inputs, mg_source_spec *raw_proto,
               mg_palette_spec *user_palette, int num_inputs, bool quiet)
{
  int num_tracks=0, num_frames=0;
  mg_track_spec *track, *track_specs =
    parse_track_specs(args,num_tracks,inputs,num_inputs);

  mj2_target out;
  out.open(family_tgt);

  for (track=track_specs; track != NULL; track=track->next)
    {
      mg_track_seg *seg;
      mj2_video_target *tgt = out.add_video_track();
      tgt->set_field_order(track->field_order);
      kdu_uint32 timescale = 0;
      bool need_init = true;
      double exact_period = -1.0;
      for (seg=track->segs; seg != NULL; seg=seg->next)
        {
          // Navigate to the first frame
          int fields_per_frame = (track->field_order==KDU_FIELDS_NONE)?1:2;
          int n=seg->from*fields_per_frame;
          mg_source_spec *in=inputs;
          while ((in != NULL) && (n > 0))
            { // Skip through the sources until we get to the `from' element
              if (in->num_fields > n)
                break;
              n -= in->num_fields;
              in = in->next;
            }
          int src_field = n;

          // Scan through the frames
          for (n=seg->from; (seg->to < seg->from) || (n <= seg->to); n++)
            { 
              if (in == NULL)
                break;

              if (seg->fps > 0.0F)
                exact_period = 1.0 / seg->fps;
              else if (in->video_source != NULL)
                {
                  in->video_source->seek_to_frame(src_field/fields_per_frame);
                  exact_period =
                    ((double) in->video_source->get_frame_period()) /
                    ((double) in->video_source->get_timescale());
                }
              else if (in->mjc_src.exists())
                { 
                  kdu_long period = in->mjc_src.get_frame_period();
                  kdu_long timescale = in->mjc_src.get_timescale();
                  if ((period > 0) && (timescale > 0))
                  exact_period = ((double) period) / ((double)timescale);
                }
              if (exact_period <= 0.0)
                { kdu_error e; e << "No timing information available in "
                  "source data used to generate video track.  You must "
                  "supply an explicit frame rate via the \"-mj2_tracks\" "
                  "argument."; }
              kdu_uint32 period;
              if (need_init)
                { // Writing the first frame for this track; set the timescale
                  if (in->video_source != NULL)
                    timescale = in->video_source->get_timescale();
                  else
                    { // Find a good timescale to match the exact period
                      kdu_uint32 ticks_per_sec, best_ticks=1000;
                      double error, best_error=1000.0; // Ridiculous value
                      for (ticks_per_sec=1000; ticks_per_sec < 2000;
                           ticks_per_sec++)
                        {
                          period=(kdu_uint32)(exact_period*ticks_per_sec+0.5);
                          error = fabs(exact_period -
                            ((double) period) / ((double) ticks_per_sec));
                          if (error < best_error)
                            { best_error=error;  best_ticks=ticks_per_sec; }
                        }
                      timescale = best_ticks;
                    }
                  tgt->set_timescale(timescale);
                }

              period = (kdu_uint32)(exact_period*timescale + 0.5);
              tgt->set_frame_period(period);

              for (int p=0; p < fields_per_frame; p++, need_init=false)
                {
                  if (in == NULL)
                    break;
                  if ((in->video_source != NULL) &&
                      (in->field_order != track->field_order))
                    { kdu_error e; e << "Trying to construct an MJ2 video "
                      "track from source MJ2 frames which do not have the "
                      "same interlacing convention as the output track."; }
                  if ((in->field_order != KDU_FIELDS_NONE) && (p & 1))
                    { kdu_error e; e << "Trying to construct an "
                      "interlaced video track from segments of MJ2 "
                      "interlaced source material and JP2/JPX source material "
                      "where the latter does not provide an even number of "
                      "compositing layers."; }
                  if (need_init)
                    configure_track_params(tgt,in,src_field,raw_proto,
                                           user_palette);
                  write_mj2_image(tgt,in,src_field);

                  // Advance to next source field
                  src_field++;
                  if (in->num_fields == src_field)
                    {
                      in = in->next;
                      src_field = 0;
                    }
                }
              num_frames++;
            }
        }
    }

  out.close();
  while ((track=track_specs) != NULL)
    {
      track_specs = track->next;
      delete track;
    }

  // Statistics
  if (!quiet)
    {
      pretty_cout << "\nWrote " << num_tracks << " video tracks, "
                     "containing a total of " << num_frames
                  << " frames to the output file.\n";
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
  kdu_args args(argc,argv,"-s");

  // Collect simple arguments.
  char *ofname=NULL;
  bool quiet, interleave_headers, link_to_codestreams;
  int num_inputs;
  mg_palette_spec *user_palette;
  mg_source_spec *in, *raw_proto, *inputs =
    parse_common_args(args,ofname,raw_proto,user_palette,quiet,num_inputs,
                      interleave_headers,link_to_codestreams);

  // Create output file
  jp2_family_tgt family_tgt;
  family_tgt.open(ofname);

  if (!has_mj2_suffix(ofname))
    generate_jpx(&family_tgt,args,inputs,raw_proto,user_palette,
                 num_inputs,interleave_headers,link_to_codestreams,quiet);
  else
    {
      if (link_to_codestreams)
        { kdu_error e; e << "the `-links' argument cannot currently "
          "be used when generating an MJ2 output file."; }
      generate_mj2(&family_tgt,args,inputs,raw_proto,user_palette,
                   num_inputs,quiet);
    }

  // Cleanup
  family_tgt.close();
  while ((in=inputs) != NULL)
    { inputs = in->next; delete in; }
  if (raw_proto != NULL)
    delete raw_proto;
  if (user_palette != NULL)
    delete user_palette;
  delete[] ofname;

  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }

  return 0;
}
