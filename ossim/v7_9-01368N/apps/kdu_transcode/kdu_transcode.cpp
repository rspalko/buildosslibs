/*****************************************************************************/
// File: kdu_transcode.cpp [scope = APPS/TRANSCODE]
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
   Transcoding application, demonstrating many of the transcoding-oriented
capabilities of the Kakadu framework.  Supports most of the transcoding
operations which are appealing within the JPEG2000 framework, including
image rotation.  Rate control by the transcoder is usually superior to
that obtained by pure file truncation, even with layer-progressive
code-stream organizations.
******************************************************************************/

// System includes
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Want to use "sscanf".
#include <iostream>
#include <fstream>
#include <assert.h>
// Kakadu core includes
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_block_coding.h"
// Application includes
#include "kdu_args.h"
#include "kdu_file_io.h"
#include "jp2.h"
#include "jpx.h"

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
      { stream->flush(); }
  private: // Data
    std::ostream *stream;
  };

static kdu_stream_message cout_message(&std::cout);
static kdu_stream_message cerr_message(&std::cerr);
static kdu_message_formatter pretty_cout(&cout_message);
static kdu_message_formatter pretty_cerr(&cerr_message);


/* ========================================================================= */
/*                            Local Definitions                              */
/* ========================================================================= */

struct layer_spec {
    layer_spec() { num_colour_channels=0; next=NULL; }
    jp2_colour_space space;
    int num_colour_channels;
    int channels[4]; // None of the spaces use more colour channels
    layer_spec *next;
  };


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                        print_version                               */
/*****************************************************************************/

static void
  print_version()
{
  kdu_message_formatter out(&cout_message);
  out.start_message();
  out << "This is Kakadu's \"kdu_transcode\" application.\n";
  out << "\tCompiled against the Kakadu core system, version "
      << KDU_CORE_VERSION << "\n";
  out << "\tCurrent core system version is "
      << kdu_get_core_version() << "\n";
  out.flush(true);
  exit(0);
}

/*****************************************************************************/
/* STATIC                        print_about                                 */
/*****************************************************************************/

static void
  print_about(char *prog)
{
  kdu_message_formatter out(&cout_message);

  out << "Brief description of \"" << prog << "\"\n";
  out.set_master_indent(3);
  out << "  The standard inherently lends itself to a wide variety of "
         "transcoding operations.  This program offers the "
         "following capabilities:\n";
  out.set_master_indent(7);
  out << "-- changing the profile identifier;\n"
         "-- reducing the resolution (i.e., the number of DWT levels);\n"
         "-- reducing the number of image components;\n"
         "-- changing the number of quality layers;\n"
         "-- reducing the bit-rate;\n"
         "-- introducing or modifying multi-component transforms;\n"
         "-- re-sequencing of the packet progression in any tile;\n"
         "-- introducing/discarding interleaved tile-parts for any tile;\n"
         "-- introducing/discarding PLT marker segments;\n"
         "-- changing precinct dimensions to support better spatial "
         "progressions;\n"
         "-- introducing/discarding SOP (Start of Packet) markers;\n"
         "-- introducing/discarding EPH (End of Packet Header) markers;\n"
         "-- changing any of the six block coder mode switches; and\n"
         "-- rotation in the transformed domain.\n";
  out.set_master_indent(3);
  out << "In the future we plan to add compressed domain cropping to these "
         "features.\n";
  out << "\nThese capabilities are mostly realized by supplying the same "
         "type of attribute specification on the command-line, which you "
         "would use with the compressor.  Tile- and component-specific "
         "forms of the various arguments may be used.\n";
  out << "\nFor a list of arguments which may legitimately be supplied here, "
         "use the \"-u\" or \"-usage\" (long version) arguments to request "
         "a usage statement.\n";

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
  out << "-i <compressed file in>\n";
  if (comprehensive)
    out << "\tAccepts raw code-stream files, as well as JP2 and JPX files.  "
           "The file suffix is irrelevant -- the application figures out "
           "what type of file it is, based on its contents, assuming it is "
           "a raw codestream if it does not have a valid JP2 family "
           "signature box and file type box.\n";
  out << "-o <raw codestream or JPX file>\n";
  if (comprehensive)
    out << "\tTo learn how to use this utility, start by writing raw "
           "code-streams (a file with a suffix of \".j2c\" or \".j2k\"), "
           "even if the input was a JP2 or JPX file.  If the "
           "file suffix is one of \".jpx\" or \".jpf\", the application "
           "writes a JPX file (JP2 compatible if possible) containing "
           "the transcoded codestream.  However, you are required also "
           "to supply a `-jpx_layers' argument in this case.\n"
           "\t   Note: to avoid copying invalidated metadata from an input "
           "file, this utility reads only the codestream(s) embedded in "
           "any supplied JP2/JPX file, leaving it to you to add the "
           "colour space information explicitly.  Remember that this is "
           "just a demonstration -- if you need to add more metadata, it "
           "is easy to do so by augmenting the source code (it is not long) "
           "to meet your needs.\n";
  out << "-jpx_layers <space>,<channels>[+<space>,<channels>[+...]]\n";
  if (comprehensive)
    out << "\tThis argument is to be supplied if and only if the output "
           "(`-o') is a JPX file.  The argument takes a parameter string "
           "consisting of one or more JPX compositing layer specifications, "
           "separated by `+' characters.  If there are N transcoded "
           "codestreams, the first layer specification is reproduced N times, "
           "once for each codestream; the next layer specification, if any, "
           "is then used to generate another N compositing layers, and so "
           "forth.  It is up to you to ensure that the layer descriptions "
           "are compatible with all transcoded codestreams.\n"
           "\t   Each layer specification consists of a colour space "
           "identifier, followed by a comma-separated list of image "
           "component indices (zero-based) from the relevant codestream "
           "that are used to form the colour space's channels.  The <space> "
           "parameter may be any of the following strings:\n"
           "\t\t`bilevel1', `bilevel2', `YCbCr1', `YCbCr2', `YCbCr3', "
           "`PhotoYCC', `CMY', `CMYK', `YCCK', `CIELab', `CIEJab', "
           "`sLUM', `sRGB', `sYCC', `esRGB', `esYCC', `ROMMRGB', "
           "`YPbPr60',  `YPbPr50'\n"
           "\t   A description of each of these enumerated colour spaces may "
           "be found in IS15444-2 (very sketchy) or in the comments "
           "provided with the Kakadu function `jp2_colour::init' (more "
           "comprehensive).\n";
  out << "-reduce <discard levels>\n";
  if (comprehensive)
    out << "\tSet the number of highest resolution levels to be discarded.  "
           "The image resolution is effectively divided by 2 to the power of "
           "the number of discarded levels.\n";
  out << "-components <num image components>\n";
  if (comprehensive)
    out << "\tSet the maximum number of image components to be included "
           "in the output code-stream.\n";
  out << "-skip_components <num image components>\n";
  if (comprehensive)
    out << "\tSet the number of initial image components to be discarded.\n";
  out << "-rate <bits per pixel>\n";
  if (comprehensive)
    out << "\tMax output bit-rate, expressed in terms of the ratio between "
           "the total number of compressed bits (including headers) and the "
           "product of the largest horizontal and  vertical image component "
           "dimensions.  Note that all of the original layers will be "
           "included, many of which may be assigned empty packets, which "
           "still occupy at least one byte each.  Apart from this small cost, "
           "transcoding to a reduced rate usually produces superior results "
           "to simply truncating the original code-stream to that rate, even "
           "when it was layer progressive -- it also yields a fully compliant "
           "code-stream, complete with all packets and an EOC marker.\n";
  out << "-rotate <degrees>\n";
  if (comprehensive)
    out << "\tRotate by a multiple of 90 degrees.  JPEG2000 allows for "
           "image rotation by multiples of 90 degrees without the need to "
           "invert and redo the DWT (Discrete Wavelet Transform).  "
           "While this represents a considerable saving, individual "
           "code-blocks must still be transcoded.  While the distortion "
           "and size of most code-streams will not be invariant under "
           "rotation, once a compressed image has been rotated using "
           "this option, all subsequent rotations will preserve "
           "distortion and any set of rotations which sum to a multiple of "
           "360 degrees will preserve compressed length.\n"
        << "\tNote that rotating a reversibly compressed image using this "
           "approach will generally introduce artefacts into the least "
           "significant one or two bits of each subband's sample values.  "
           "This is often not visually significant, except where the original "
           "source content has very low bit-depth (e.g., 1 bit/sample); in "
           "the latter case, distortion may be very disturbing.  These "
           "artefacts are not produced if the image is directly compressed "
           "using \"kdu_compress\" with the `-rotate' option.\n"
        << "\tNote also that subject to rotation, any tile-specific parameter "
           "modifications supplied here refer to tiles in the output "
           "code-stream, as opposed to the input code-stream.\n";
  out << "-flush_tiles <tiles processed between calls to flush>\n";
  if (comprehensive)
    out << "\tBy default, the system waits until all tiles have been "
           "transcoded before any of the final codestream is actually "
           "written.  With this argument, you can request incremental "
           "flushing based on tiles.  The incremental flushing capabilities "
           "offered by Kakadu are much more interesting than this -- in "
           "particular, incremental flushing is most interesting when it "
           "is applied to precincts within tiles.  However, to take "
           "advantage of other bases for incremental flushing, the internal "
           "implementation would have to walk through the image elements "
           "(tiles, components, resolutions and code-blocks) in an order "
           "which agrees with that of the codestream to be written.  This "
           "would significantly complicate this simple demo application, so "
           "we demonstrate incremental flushing here only at the tile "
           "level.  The argument takes a single parameter which identifies "
           "the number of tiles to be processed between successive "
           "calls to `kdu_codestream::trans_out' -- call to this function "
           "parallel those to `kdu_codestream::flush' in the "
           "\"kdu_compress\" demo app.\n";

  siz_params siz;
  siz.describe_attribute(Sprofile,out,comprehensive);
  siz.describe_attribute(Sprecision,out,comprehensive);
  siz.describe_attribute(Ssigned,out,comprehensive);
  siz.describe_attribute(Mcomponents,out,comprehensive);
  siz.describe_attribute(Msigned,out,comprehensive);
  siz.describe_attribute(Mprecision,out,comprehensive);
  siz.describe_attribute(Ncomponents,out,comprehensive);
  siz.describe_attribute(Nsigned,out,comprehensive);
  siz.describe_attribute(Nprecision,out,comprehensive);
  
  cod_params cod;
  cod.describe_attribute(Corder,out,comprehensive);
  cod.describe_attribute(Clayers,out,comprehensive);
  cod.describe_attribute(Cprecincts,out,comprehensive);
  cod.describe_attribute(Cuse_precincts,out,comprehensive);
  cod.describe_attribute(Cuse_eph,out,comprehensive);
  cod.describe_attribute(Cuse_sop,out,comprehensive);
  cod.describe_attribute(Cmodes,out,comprehensive);
  poc_params poc;
  poc.describe_attribute(Porder,out,comprehensive);
  org_params org;
  org.describe_attribute(ORGgen_plt,out,comprehensive);
  org.describe_attribute(ORGtparts,out,comprehensive);
  mct_params mct; mct.describe_attributes(out,comprehensive);
  mcc_params mcc; mcc.describe_attributes(out,comprehensive);
  mco_params mco; mco.describe_attributes(out,comprehensive);  
  nlt_params nlt; nlt.describe_attributes(out,comprehensive);
  atk_params atk; atk.describe_attributes(out,comprehensive);

  out << "-fussy\n";
  if (comprehensive)
    out << "\tEncourage fussy input code-stream parsing, in which most "
           "code-stream compliance failures will terminate execution, with "
           "an appropriate error message.\n";
  out << "-resilient\n";
  if (comprehensive)
    out << "\tEncourage error resilient processing, in which an attempt is "
           "made to recover from errors in the input code-stream with minimal "
           "degradation in reconstructed image quality.  The current "
           "implementation should avoid execution failure so long as only "
           "a single tile-part was used and no errors are found in the main "
           "or tile header.  The implementation recognizes tile-part headers "
           "only if the first 4 bytes of the marker segment are correct, "
           "which makes it extremely unlikely that a code-stream with only "
           "one tile-part will be mistaken for anything else.  Multiple "
           "tiles or tile-parts can create numerous problems for an error "
           "resilient decompressor; complete failure may occur if a "
           "multi-tile-part code-stream is corrupted.\n";
  out << "-resilient_sop\n";
  if (comprehensive)
    out << "\tSame as \"-resilient\" except that the error resilient code-"
           "stream parsing algorithm is informed that it can expect SOP "
           "markers to appear in front of every single packet, whenever "
           "the relevant flag in the Scod style byte of the COD marker is "
           "set.  The JPEG2000 standard interprets this flag as meaning "
           "that SOP markers may appear; however, this does not give the "
           "decompressor any idea where it can expect SOP markers "
           "to appear.  In most cases, SOP markers, if used, will be placed "
           "in front of every packet and knowing this a priori can "
           "improve the performance of the error resilient parser.\n";
  out << "-mem -- Report memory usage.\n";
  out << "-s <switch file>\n";
  if (comprehensive)
    out << "\tSwitch to reading arguments from a file.  In the file, argument "
           "strings are separated by whitespace characters, including spaces, "
           "tabs and new-line characters.  Comments may be included by "
           "introducing a `#' or a `%' character, either of which causes "
           "the remainder of the line to be discarded.  Any number of "
           "\"-s\" argument switch commands may be included on the command "
           "line.\n";
  out << "-record <file>\n";
  if (comprehensive)
    out << "\tRecord output code-stream parameters in a file, using the same "
           "format which is accepted when specifying the parameters to the "
           "compressor.\n"; 
  out << "-quiet -- suppress informative messages.\n";
  out << "-about -- print a brief description of this program.\n";
  out << "-version -- print core system version I was compiled against.\n";
  out << "-v -- abbreviation of `-version'\n";
  out << "-usage -- print a comprehensive usage statement.\n";
  out << "-u -- print a brief usage statement.\"\n\n";
  out.flush();
  exit(0);
}

/*****************************************************************************/
/* STATIC                    check_parameter_args                            */
/*****************************************************************************/

static void
  check_parameter_args(kdu_args &args)
{
  char *string;
  for (string=args.get_first(); string != NULL; string=args.advance(false))
    {
      char *delim = string;
      while ((*delim != '\0') && (*delim != ':') && (*delim != '=') &&
             (*delim != ' '))
        delim++;
      if ((*delim != ':') && (*delim != '='))
        continue;
      char save = *delim; *delim = '\0';
      if (strcmp(string,Sprofile) &&
          strcmp(string,Ssigned) &&
          strcmp(string,Sprecision) &&
          strcmp(string,Mcomponents) &&
          strcmp(string,Mprecision) &&
          strcmp(string,Msigned) &&
          strcmp(string,Ncomponents) &&
          strcmp(string,Nprecision) &&
          strcmp(string,Nsigned) &&
          
          strcmp(string,Mmatrix_size) &&
          strcmp(string,Mmatrix_coeffs) &&
          strcmp(string,Mvector_size) &&
          strcmp(string,Mvector_coeffs) &&
          strcmp(string,Mtriang_size) &&
          strcmp(string,Mtriang_coeffs) &&
          strcmp(string,Mstage_inputs) &&
          strcmp(string,Mstage_outputs) &&
          strcmp(string,Mstage_collections) &&
          strcmp(string,Mstage_xforms) &&
          strcmp(string,Mnum_stages) &&
          strcmp(string,Mstages) &&
          
          strcmp(string,Kreversible) &&
          strcmp(string,Ksymmetric) &&
          strcmp(string,Kextension) &&
          strcmp(string,Ksteps) &&
          strcmp(string,Kcoeffs) &&
          
          strcmp(string,Corder) &&
          strcmp(string,Clayers) &&
          strcmp(string,Cprecincts) &&
          strcmp(string,Cuse_precincts) &&
          strcmp(string,Cuse_eph) &&
          strcmp(string,Cuse_sop) &&
          strcmp(string,Cmodes) &&
          strcmp(string,Porder) &&
          strcmp(string,ORGgen_plt) &&
          strcmp(string,ORGtparts))
        {  *delim = save;
          kdu_error e; e << "Attempting to modify a code-stream parameter "
          "for which transcoding is not defined.  Offending argument is \""
          << string << "\".  For more information see the usage statement."; }
      *delim = save;
    }
}

/*****************************************************************************/
/* STATIC                     parse_simple_args                              */
/*****************************************************************************/

static void
  parse_simple_args(kdu_args &args, char * &ifname, char * &ofname,
                    std::ostream * &record_stream, int &discard_levels,
                    int &skip_components, int &max_components,
                    float &max_bpp, int &flush_tiles, bool &transpose,
                    bool &vflip, bool &hflip, bool &mem, bool &quiet)
  /* Parses most simple arguments (those involving a dash).
     Note that `max_bpp' is returned as negative if the
     bit-rate is not explicitly set. */
{
  if ((args.get_first() == NULL) || (args.find("-about") != NULL))
    print_about(args.get_prog_name());
  if (args.find("-u") != NULL)
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();      

  ifname = NULL;
  ofname = NULL;
  record_stream = NULL;
  discard_levels = 0;
  skip_components = 0;
  max_components = 0;
  max_bpp = -1.0;
  flush_tiles = INT_MAX;
  int rotate = 0;
  transpose = hflip = vflip = false;
  mem = false;
  quiet = false;

  if (args.find("-i") != NULL)
    {
      if ((ifname = args.advance()) == NULL)
        { kdu_error e; e << "\"-i\" argument requires a file name!"; }
      args.advance();
    }

  if (args.find("-o") != NULL)
    {
      if ((ofname = args.advance()) == NULL)
        { kdu_error e; e << "\"-o\" argument requires a file name!"; }
      args.advance();
    }

  if (args.find("-reduce") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&discard_levels) != 1) ||
          (discard_levels < 0))
        { kdu_error e; e << "\"-reduce\" argument requires a non-negative "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-skip_components") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&skip_components) != 1) ||
          (skip_components < 0))
        { kdu_error e; e << "\"-skip_components\" argument requires a "
          "non-negative integer parameter!"; }
      args.advance();
    }

  if (args.find("-components") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&max_components) != 1) ||
          (max_components <= 0))
        { kdu_error e; e << "\"-components\" argument requires a positive "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-rate") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%f",&max_bpp) != 1) ||
          (max_bpp <= 0.0F))
        { kdu_error e; e << "\"-rate\" argument requires a positive "
          "numeric parameter!"; }
      args.advance();
    }

  if (args.find("-rotate") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&rotate) != 1) ||
          ((rotate % 90) != 0))
        { kdu_error e; e << "\"-rotate\" argument requires an integer "
          "multiple of 90 degrees!"; }
      args.advance();
      rotate /= 90;
    }

  if (args.find("-flush_tiles") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&flush_tiles) != 1) ||
          (flush_tiles <= 0))
        { kdu_error e; e << "\"-flush_tiles\" argument requires a positive "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-mem") != NULL)
    {
      mem = true;
      args.advance();
    }

  if (args.find("-quiet") != NULL)
    {
      quiet = true;
      args.advance();
    }

  if (args.find("-record") != NULL)
    {
      char *fname = args.advance();
      if (fname == NULL)
        { kdu_error e; e << "\"-record\" argument requires a file name!"; }
      record_stream = new std::ofstream(fname);
      if (record_stream->fail())
        { kdu_error e; 
          e << "Unable to open record file, \"" << fname << "\"."; }
      args.advance();
    }

  if ((ifname == NULL) || (ofname == NULL))
    { kdu_error e; e << "Must at least supply the names of an input and an "
      "output file."; }

  while (rotate >= 4)
    rotate -= 4;
  while (rotate < 0)
    rotate += 4;
  switch (rotate) { /* Note: we will be applying the geometric transformations
                       to the input code-stream. */
    case 0: transpose = false; vflip = false; hflip = false; break;
    case 1: transpose = true;  vflip = false; hflip = true;  break;
    case 2: transpose = false; vflip = true;  hflip = true;  break;
    case 3: transpose = true;  vflip = true;  hflip = false; break;
    }
}

/*****************************************************************************/
/* STATIC                       get_jpx_layers                               */
/*****************************************************************************/

static layer_spec *
  get_jpx_layers(kdu_args &args)
{
  layer_spec *layers=NULL, *tail=NULL;
  if (args.find("-jpx_layers") != NULL)
    { 
      char *cp, *next_string, *string = args.advance();
      for (; string != NULL; string=next_string)
        { 
          next_string = strchr(string,'+');
          if (next_string != NULL)
            { *next_string = '\0'; next_string++; }
          if (tail == NULL)
            layers = tail = new layer_spec;
          else
            tail = tail->next = new layer_spec;
          cp = strchr(string,',');
          if (cp == NULL)
            { kdu_error e; e << "Expected a colour space type, followed "
              "by a comma-separated list of channel component indices in "
              "`-jpx_layers' parameter."; }
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
              << string << "\", in `-jpx_layers' specification."; }
          string = cp+1;
          while ((tail->num_colour_channels < 4) && (*string != '\0'))
            { 
              if (*string == ',')
                { string++; continue; }
              int idx = (int)strtol(string,&cp,10);
              if ((cp == string) || (idx < 0))
                { kdu_error e; e << "`-jpx_layers' specification contains "
                  "an invalid channel codestream index."; }
              tail->channels[tail->num_colour_channels++] = idx;
              string = cp;
            }
          if (tail->num_colour_channels < 1)
            { kdu_error e; e << "`-jpx_layers' specification contains no "
              "channel codestream indices."; }
        }
      args.advance();
    }
  if (layers == NULL)
    { kdu_error e; e << "The `-jpx_layers' argument is required when "
      "generating a JPX output file."; }
  return layers;
}

/*****************************************************************************/
/* STATIC                       make_jpx_layer                               */
/*****************************************************************************/

static void
  make_jpx_layer(jpx_target &jpx_out, layer_spec *spec, int stream_idx)
{
  jpx_layer_target tgt = jpx_out.add_layer();
  tgt.add_colour().init(spec->space);
  jp2_channels channels = tgt.access_channels();
  channels.init(spec->num_colour_channels);
  for (int n=0; n < spec->num_colour_channels; n++)
    channels.set_colour_mapping(n,spec->channels[n],-1,stream_idx);
}

/*****************************************************************************/
/* STATIC                   configure_jpx_dimensions                         */
/*****************************************************************************/

static void
  configure_jpx_dimensions(kdu_compressed_source *input,
                           jpx_codestream_target &tgt,
                           int skip_components, int max_components,
                           bool transpose, kdu_args &args)
  /* Configures the `tgt' object by reading the header of the JPEG2000
     codestream via `input', performing the transcoding steps for just this
     header information, and using the resulting information to configure
     the `jp2_dimensions' associated with `tgt'. */
{
  jp2_dimensions dims = tgt.access_dimensions();
  
  // Create temporary input codestream
  kdu_codestream codestream_in; codestream_in.create(input);
  codestream_in.apply_input_restrictions(skip_components,max_components,
                                         0,0,NULL);
  codestream_in.change_appearance(transpose,false,false);
  siz_params *siz_in = codestream_in.access_siz();

  // Create temporary output codestream object
  siz_params siz;
  siz.copy_from(siz_in,-1,-1,-1,skip_components,0,transpose,false,false);
  char *string;
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz.parse_string(string));
  int components_in=0; siz.get(Scomponents,0,0,components_in);
  if (components_in != codestream_in.get_num_components())
    { // Reducing the set of codestream components
      siz.set(Scomponents,0,0,codestream_in.get_num_components());
      siz.set(Mcomponents,0,0,0); // Disable any multi-component transform
    }
  kdu_codestream codestream_out; codestream_out.create(&siz);
  siz_params *siz_out = codestream_out.access_siz();
  siz_out->copy_from(siz_in,-1,-1,-1,skip_components,0,transpose,false,false);
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz_out->parse_string(string,-1));
  codestream_out.access_siz()->finalize_all(-1);

  dims.init(codestream_out.access_siz());

  codestream_in.destroy();
  codestream_out.destroy();
}
  
/*****************************************************************************/
/* STATIC                      check_j2c_suffix                              */
/*****************************************************************************/

static bool
  check_j2c_suffix(char *fname)
  /* Returns true if the file-name has the suffix, ".j2c" or ".j2k", where the
     check is case insensitive. */
{
  const char *cp = strrchr(fname,'.');
  if (cp == NULL)
    return false;
  cp++;
  if ((*cp != 'j') && (*cp != 'J'))
    return false;
  cp++;
  if (*cp != '2')
    return false;
  cp++;
  if ((*cp != 'c') && (*cp != 'C') && (*cp != 'k') && (*cp != 'K'))
    return false;
  return true;
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
/* STATIC                    set_error_behaviour                             */
/*****************************************************************************/

static void
  set_error_behaviour(kdu_args &args, kdu_codestream codestream)
{
  bool fussy = false;
  bool resilient = false;
  bool ubiquitous_sops = false;
  if (args.find("-fussy") != NULL)
    { args.advance(); fussy = true; }
  if (args.find("-resilient") != NULL)
    { args.advance(); resilient = true; }
  if (args.find("-resilient_sop") != NULL)
    { args.advance(); resilient = true; ubiquitous_sops = true; }
  if (resilient)
    codestream.set_resilient(ubiquitous_sops);
  else if (fussy)
    codestream.set_fussy();
  else
    codestream.set_fast();
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
/* STATIC                        copy_block                                  */
/*****************************************************************************/
                    
static void
  copy_block(kdu_block *in, kdu_block *out)
{
  if (in->K_max_prime != out->K_max_prime)
    { kdu_error e; e << "Cannot copy blocks belonging to subbands with "
      "different quantization parameters."; }
  assert(!(out->transpose || out->vflip || out->hflip));
  kdu_coords size = in->size;
  if (in->transpose) size.transpose();
  if ((size.x != out->size.x) || (size.y != out->size.y))  
    { kdu_error e; e << "Cannot copy code-blocks with different dimensions."; }
  out->missing_msbs = in->missing_msbs;
  if (out->max_passes < (in->num_passes+2))      // Gives us enough to round up
    out->set_max_passes(in->num_passes+2,false); // to the next whole bit-plane
  out->num_passes = in->num_passes;
  int num_bytes = 0;
  for (int z=0; z < in->num_passes; z++)
    {
      num_bytes += (out->pass_lengths[z] = in->pass_lengths[z]);
      out->pass_slopes[z] = in->pass_slopes[z];
    }

  if ((in->modes != out->modes) || (in->orientation != out->orientation) ||
      in->transpose || in->vflip || in->hflip)
    { // Need to transcode the individual code-blocks.
      kdu_block_decoder decoder;
      decoder.decode(in);
      out->num_passes = in->num_passes; // Just in case we couldn't decode all
      if (out->num_passes == 0)
        return;
      int num_samples = in->size.x*in->size.y;
      if (num_samples > out->max_samples)
        out->set_max_samples((num_samples>4096)?num_samples:4096);
      if (in->transpose || in->vflip || in->hflip)
        { // Need geometric transformation and termination at whole bit-plane
          int row_gap_in = in->size.x;
          int row_gap_out = out->size.x;
          kdu_int32 *sp, *spp = in->sample_buffer;
          kdu_int32 *dp, *dpp = out->sample_buffer;
          int out_cinc = 1;
          if (in->vflip)
            { dpp += (size.y-1)*row_gap_out; row_gap_out = -row_gap_out; }
          if (in->hflip)
            { dpp += size.x-1; out_cinc = -out_cinc; }
          int r, c;
          if (!in->transpose)
            { // Non-transposed copy
              for (r=size.y; r > 0; r--, spp+=row_gap_in, dpp+=row_gap_out )
                { // Unroll loop a little for speed.
                  for (sp=spp, dp=dpp, c=size.x; c > 3; c-=4)
                    {
                      *dp = *(sp++); dp += out_cinc;
                      *dp = *(sp++); dp += out_cinc;
                      *dp = *(sp++); dp += out_cinc;
                      *dp = *(sp++); dp += out_cinc;
                    }
                  while (c--)
                    { *dp = *(sp++); dp += out_cinc; }
                }
            }
          else
            { // Transposed copy
              for (r=size.y; r > 0; r--, spp++, dpp+=row_gap_out)
                { // Unroll loop a little for speed.
                  for (sp=spp, dp=dpp, c=size.x; c > 3; c-=4)
                    {
                      *dp = *sp; sp += row_gap_in; dp += out_cinc;
                      *dp = *sp; sp += row_gap_in; dp += out_cinc;
                      *dp = *sp; sp += row_gap_in; dp += out_cinc;
                      *dp = *sp; sp += row_gap_in; dp += out_cinc;
                    }
                  while (c--)
                    { *dp = *sp; sp += row_gap_in; dp += out_cinc; }
                }
            }
          if ((out->num_passes % 3) != 1)
            { /* Round up to the next whole bit-plane to avoid any
                 degradation from further geometric transformations. */
              for (; (out->num_passes % 3) != 1; out->num_passes++)
                {
                  out->pass_slopes[out->num_passes] =
                    out->pass_slopes[out->num_passes-1];
                  out->pass_slopes[out->num_passes-1] = 0;
                }
              dp = out->sample_buffer; // Prepare to reset rounding bit
              int p = 30 - out->missing_msbs - (out->num_passes / 3);
              kdu_int32 mask = (((kdu_int32)(-1))<<p);
              for (c=num_samples; c > 3; c-=4, dp+=4)
                {
                  if ((dp[0] & mask) == 0) dp[0] &= (mask+mask);
                  if ((dp[1] & mask) == 0) dp[1] &= (mask+mask);
                  if ((dp[2] & mask) == 0) dp[2] &= (mask+mask);
                  if ((dp[3] & mask) == 0) dp[3] &= (mask+mask);
                }
              for (; c > 0; c--, dp++)
                if ((dp[0] & mask) == 0) dp[0] &= (mask+mask);
            }
        }
      else
        { // No change in the sample data itself.
          memcpy(out->sample_buffer,in->sample_buffer,
                 (size_t) num_samples * sizeof(kdu_int32));
        }
      kdu_block_encoder encoder;
      encoder.encode(out);
    }
  else
    { // Just copy compressed code-bytes.  No need for block transcoding.
      if (out->max_bytes < num_bytes)
        out->set_max_bytes(num_bytes,false);
      memcpy(out->byte_buffer,in->byte_buffer,(size_t) num_bytes);
    }
}

/*****************************************************************************/
/* STATIC                        copy_tile                                   */
/*****************************************************************************/

static void
  copy_tile(kdu_tile tile_in, kdu_tile tile_out, int tnum_in, int tnum_out,
            kdu_params *siz_in, kdu_params *siz_out, int skip_components,
            int &num_blocks)
  /* Although there could be more efficient ways of doing this (in terms of
     saving memory), we currently just walk through all code-blocks in the
     most obvious order, copying them from the input to the output tile.
     Note that the main tile-header coding parameters should have been
     copied already, but this function will copy POC parameters for
     non-initial tile-parts, wherever the information has not already
     been substituted for the purpose of rearranging the packet sequence
     during transcoding. */
{
  int num_components = tile_out.get_num_components();
  int new_tpart=0, next_tpart = 1;

  for (int c=0; c < num_components; c++)
    {
      kdu_tile_comp comp_in, comp_out;
      comp_in = tile_in.access_component(c);
      comp_out = tile_out.access_component(c);
      int num_resolutions = comp_out.get_num_resolutions();
      for (int r=0; r < num_resolutions; r++)
        {
          kdu_resolution res_in;  res_in = comp_in.access_resolution(r);
          kdu_resolution res_out; res_out = comp_out.access_resolution(r);
          int b, min_band;
          int num_bands = res_in.get_valid_band_indices(min_band);
          for (b=min_band; num_bands > 0; num_bands--, b++)
            {
              kdu_subband band_in;  band_in = res_in.access_subband(b);
              kdu_subband band_out; band_out = res_out.access_subband(b);
              kdu_dims blocks_in;  band_in.get_valid_blocks(blocks_in);
              kdu_dims blocks_out; band_out.get_valid_blocks(blocks_out);
              if ((blocks_in.size.x != blocks_out.size.x) ||
                  (blocks_in.size.y != blocks_out.size.y))
                { kdu_error e; e << "Transcoding operation cannot proceed: "
                  "Code-block partitions for the input and output "
                  "code-streams do not agree."; }
              kdu_coords idx;
              for (idx.y=0; idx.y < blocks_out.size.y; idx.y++)
                for (idx.x=0; idx.x < blocks_out.size.x; idx.x++)
                  {
                    kdu_block *in =
                      band_in.open_block(idx+blocks_in.pos,&new_tpart);
                    for (; next_tpart <= new_tpart; next_tpart++)
                      siz_out->copy_from(siz_in,tnum_in,tnum_out,next_tpart,
                                         skip_components);
                    kdu_block *out = band_out.open_block(idx+blocks_out.pos);
                    copy_block(in,out);
                    band_in.close_block(in);
                    band_out.close_block(out);
                    num_blocks++;
                  }
            }
        }
    }
}

/*****************************************************************************/
/* STATIC                     transcode_stream                               */
/*****************************************************************************/

static void
  transcode_stream(kdu_compressed_source *input, kdu_compressed_target *output,
                   int skip_components, int max_components, int discard_levels,
                   bool transpose, bool vflip, bool hflip, float max_bpp,
                   int flush_tiles, bool mem, bool quiet,
                   std::ostream *record_stream, kdu_args &args)
{
  // Create input codestream
  kdu_codestream codestream_in; codestream_in.create(input);
  set_error_behaviour(args,codestream_in);
  codestream_in.apply_input_restrictions(skip_components,max_components,
                                         discard_levels,0,NULL);
  codestream_in.change_appearance(transpose,vflip,hflip);
  siz_params *siz_in = codestream_in.access_siz();

  // Create output codestream object
  siz_params siz;
  siz.copy_from(siz_in,-1,-1,-1,skip_components,discard_levels,
                transpose,vflip,hflip);
  char *string;
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz.parse_string(string));
  int components_in=0; siz.get(Scomponents,0,0,components_in);
  if (components_in != codestream_in.get_num_components())
    { // Reducing the set of codestream components
      siz.set(Scomponents,0,0,codestream_in.get_num_components());
      siz.set(Mcomponents,0,0,0); // Disable any multi-component transform
    }

  kdu_codestream codestream_out; codestream_out.create(&siz,output);
  codestream_out.share_buffering(codestream_in);
  siz_params *siz_out = codestream_out.access_siz();
  siz_out->copy_from(siz_in,-1,-1,-1,skip_components,discard_levels,
                     transpose,vflip,hflip);
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz_out->parse_string(string,-1));
  codestream_out.access_siz()->finalize_all(-1);
  kdu_message_formatter *formatted_recorder = NULL;;
  kdu_stream_message recorder(record_stream);
  if (record_stream != NULL)
    { 
      formatted_recorder = new kdu_message_formatter(&recorder);
      codestream_out.set_textualization(formatted_recorder);
    }
  
  // Set up rate control variables
  kdu_long max_bytes = KDU_LONG_MAX;
  if (max_bpp > 0.0F)
    max_bytes = (kdu_long)(0.125*max_bpp*get_bpp_dims(siz_out));
  
  kdu_params *cod = siz_out->access_cluster(COD_params);
  int total_layers;  cod->get(Clayers,0,0,total_layers);
  kdu_long *layer_bytes = new kdu_long[total_layers];
  int nel, non_empty_layers = 0;

  // Now ready to perform the transfer of compressed data between streams
  int flush_counter = flush_tiles;
  kdu_dims tile_indices_in;  codestream_in.get_valid_tiles(tile_indices_in);
  kdu_dims tile_indices_out; codestream_out.get_valid_tiles(tile_indices_out);
  assert((tile_indices_in.size.x == tile_indices_out.size.x) &&
         (tile_indices_in.size.y == tile_indices_out.size.y));
  int num_blocks=0;

  kdu_coords idx;
  for (idx.y=0; idx.y < tile_indices_out.size.y; idx.y++)
    for (idx.x=0; idx.x < tile_indices_out.size.x; idx.x++)
      { 
        kdu_tile tile_in = codestream_in.open_tile(idx+tile_indices_in.pos);
        int tnum_in = tile_in.get_tnum();
        int tnum_out = idx.x + idx.y*tile_indices_out.size.x;
        siz_out->copy_from(siz_in,tnum_in,tnum_out,0,skip_components,
                           discard_levels,transpose,vflip,hflip);
        for (string=args.get_first(); string != NULL; )
          string = args.advance(siz_out->parse_string(string,tnum_out));
        siz_out->finalize_all(tnum_out);
        /* Note carefully: we must not open the output tile without
           first copying any tile-specific code-stream parameters, as
           above.  It is tempting to do this. */
        kdu_tile tile_out = codestream_out.open_tile(idx+tile_indices_out.pos);
        assert(tnum_out == tile_out.get_tnum());
        copy_tile(tile_in,tile_out,tnum_in,tnum_out,siz_in,siz_out,
                  skip_components,num_blocks);
        tile_in.close();
        tile_out.close();
        flush_counter--;
        if ((flush_counter <= 0) && codestream_out.ready_for_flush())
          { 
            flush_counter = flush_tiles;
            nel = codestream_out.trans_out(max_bytes,layer_bytes,total_layers);
            non_empty_layers = (nel > non_empty_layers)?nel:non_empty_layers;
          }
  
      }

  /* Leave argument warnings to this point, because we need to be able to
     parse arguments incrementally as tiles become available. */
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }
  
  // Generate the output code-stream
  if (codestream_out.ready_for_flush())
    { 
      nel = codestream_out.trans_out(max_bytes,layer_bytes,total_layers);
      non_empty_layers = (nel > non_empty_layers)?nel:non_empty_layers;
    }
  if (non_empty_layers > total_layers)
    non_empty_layers = total_layers; // Can happen if a tile has more layers
  
  // Cleanup
  
  if (mem)
    { 
      pretty_cout << "\tTotal compressed data memory = "
                  << codestream_out.get_compressed_data_memory()
                  << " bytes.\n";
      pretty_cout << "\tTotal state memory associated with compressed data = "
                  << (codestream_in.get_compressed_state_memory() +
                      codestream_out.get_compressed_state_memory())
                  << " bytes.\n";
    }
  
  if (!quiet)
    { 
      pretty_cout << "\tOutput contains " << total_layers
                  << " quality layers";
      if (non_empty_layers < total_layers)
        pretty_cout << " (" << total_layers-non_empty_layers
                    << " of these were assigned empty packets)\n";
      else
        pretty_cout << "\n";
    
      pretty_cout << "\tTransferred " << num_blocks << " code-blocks.\n\n";
    
      pretty_cout << "\tRead " << codestream_in.get_num_tparts()
                  << " tile-part(s) from a total of "
                  << (int) tile_indices_in.area() << " tile(s).\n";
      pretty_cout << "\tTotal bytes read = "
                  << codestream_in.get_total_bytes()
                  << " = "
                  << 8.0*codestream_in.get_total_bytes() /
                     get_bpp_dims(codestream_in.access_siz())
                  << " bits/pel.\n\n";
    
      pretty_cout << "\tWrote " << codestream_out.get_num_tparts()
                  << " tile-part(s) in a total of "
                  << (int) tile_indices_out.area() << " tile(s).\n";
      pretty_cout << "\tTotal bytes written = "
                  << codestream_out.get_total_bytes()
                  << " = "
                  << 8.0*codestream_out.get_total_bytes() /
                     get_bpp_dims(codestream_out.access_siz())
                  << " bits/pel.\n";
      pretty_cout << "\tAll header (marker segment) bytes written = "
                  << (codestream_out.get_total_bytes() -
                      codestream_out.get_packet_bytes()) << "\n";
    
      double bpp_dims = (double) get_bpp_dims(codestream_out.access_siz());
      int layer_idx;
      pretty_cout << "\tLayer bit-rates (possibly inexact if tiles are "
                     "divided across tile-parts):\n\t\t";
      for (layer_idx=0; layer_idx < total_layers; layer_idx++)
        { 
          if (layer_idx > 0)
            pretty_cout << ", ";
          pretty_cout << 8.0 * layer_bytes[layer_idx] / bpp_dims;
        }
      pretty_cout << "\n";
    }
  
  codestream_out.destroy();
  codestream_in.destroy();
  if (formatted_recorder != NULL)
    delete formatted_recorder;
  delete[] layer_bytes;  
}

/*****************************************************************************/
/* EXTERN                           main                                     */
/*****************************************************************************/

int
  main(int argc, char *argv[])
{
  kdu_customize_warnings(&pretty_cout);
  kdu_customize_errors(&pretty_cerr);
  kdu_args args(argc,argv,"-s");

  // Collect simple arguments.

  char *ifname, *ofname;
  std::ostream *record_stream;
  int discard_levels;
  int skip_components, max_components, flush_tiles;
  float max_bpp;
  bool transpose, vflip, hflip;
  bool mem, quiet;
  parse_simple_args(args,ifname,ofname,record_stream,
                    discard_levels,skip_components,max_components,max_bpp,
                    flush_tiles,transpose,vflip,hflip,mem,quiet);

  check_parameter_args(args);

  // Open input file
  kdu_compressed_source *input = NULL;
  kdu_simple_file_source file_in;
  jp2_family_src jp2_ultimate_src;
  jpx_source jpx_in;
  jpx_codestream_source jpx_stream_in;
  int num_streams=1, stream_idx=0;
  jp2_ultimate_src.open(ifname);
  if (jpx_in.open(&jp2_ultimate_src,true) < 0)
    { // Not compatible with JP2 or JPX.  Open as a raw codestream.
      jp2_ultimate_src.close();
      file_in.open(ifname);  
      input = &file_in;
    }
  else
    { 
      jpx_in.count_codestreams(num_streams);
      jpx_stream_in = jpx_in.access_codestream(0);
      input = jpx_stream_in.open_stream();
    }
  
  // Open output file
  kdu_compressed_target *output = NULL;
  kdu_simple_file_target file_out;
  jp2_family_tgt jp2_ultimate_tgt;
  jpx_target jpx_out;
  jpx_codestream_target *jpx_streams_out = NULL;
  layer_spec *jpx_layers=NULL;
  if (check_j2c_suffix(ofname))
    { // Write a raw codestream
      file_out.open(ofname);
      output = &file_out;
    }
  else if (check_jpx_suffix(ofname))
    { // Write a JPX file
      jp2_ultimate_tgt.open(ofname);
      jpx_out.open(&jp2_ultimate_tgt);
      jpx_layers = get_jpx_layers(args);
      while (jpx_layers != NULL)
        { 
          layer_spec *spec = jpx_layers;
          for (stream_idx=0; stream_idx < num_streams; stream_idx++)
            make_jpx_layer(jpx_out,spec,stream_idx);
          jpx_layers = spec->next;
          delete spec;
        }
      jpx_streams_out = new jpx_codestream_target[num_streams];
      stream_idx = 0;
      do { 
        jpx_streams_out[stream_idx] = jpx_out.add_codestream();
        kdu_args tmp_args(args);
        configure_jpx_dimensions(input,jpx_streams_out[stream_idx],
                                 skip_components,max_components,
                                 transpose,tmp_args);
        input->close();
        if ((++stream_idx) == num_streams)
          stream_idx = 0;
        if (jpx_in.exists())
          { 
            jpx_stream_in = jpx_in.access_codestream(stream_idx);
            input = jpx_stream_in.open_stream();
          }
        else
          { 
            file_in.open(ifname);  
            input = &file_in;
          }
      } while (stream_idx != 0);
      jpx_out.write_headers();
      jpx_streams_out[0].open_stream();
      output = jpx_streams_out[0].access_stream();
    }
  else
    { kdu_error e; e << "Output file must have one of the suffices "
      "\".j2c\", \".j2k\", \".jpx\" or \".jpf\"."; }
  
  // Do transcoding
  while ((input != NULL) && (output != NULL))
    { 
      stream_idx++;
      if (mem || !quiet)
        pretty_cout << "\nCodestream #" << stream_idx << "/" << num_streams
                    << "\n---------------\n";
      if ((record_stream != NULL) && jpx_in.exists() && jpx_out.exists())
        { 
          kdu_stream_message recorder(record_stream);
          recorder << "Codestream #" << stream_idx << "/" << num_streams
                   << "\n-------------\n";
        }
      kdu_args tmp_args(args);
      transcode_stream(input,output,skip_components,max_components,
                       discard_levels,transpose,vflip,hflip,max_bpp,
                       flush_tiles,mem,quiet,record_stream,tmp_args);
      input->close();  input = NULL;
      output->close(); output = NULL;
      if (jpx_in.exists() && jpx_out.exists() && (stream_idx < num_streams))
        { 
          jpx_stream_in = jpx_in.access_codestream(stream_idx);
          input = jpx_stream_in.open_stream();
          output = jpx_streams_out[stream_idx].open_stream();
        }
    }

  if (jpx_streams_out != NULL)
    delete[] jpx_streams_out;
  jpx_in.close(); // Does nothing if not open
  jpx_out.close(); // Does nothing if not open
  jp2_ultimate_src.close(); // Does nothing if not open
  jp2_ultimate_tgt.close(); // Does nothing if not open
  if (record_stream != NULL)
    delete record_stream;
  return 0;
}
