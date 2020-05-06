/*****************************************************************************/
// File: kdu_v_expand.cpp [scope = APPS/V_DECOMPRESSOR]
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
   File-based Motion JPEG2000 decompressor application.
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
#include "v_expand_local.h"

// The following header is included here only as a temporary hack to import
// the fast data transfer functions used in the internal implementation of
// the portable `kdu_stripe_decompressor' API.  We will use the public
// interface itself in the future, but that requires stripping the tile
// processing logic out of this demo app, so for the moment we just use the
// hack.
#include "../support/stripe_decompressor_local.h"

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
  out << "This is Kakadu's \"kdu_v_expand\" application.\n";
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
  out << "-i <MJ2, JPX, JPB or MJC (almost raw) compressed input file>\n";
  if (comprehensive)
    out << "\tFour types of compressed video files may be supplied.  If the "
           "file has the suffix \".mj2\" (or \".mjp2\"), it is taken to be "
           "a Motion JPEG2000 file (as defined by IS15444-3).\n"
           "\t   If the file has the suffix \".jpx\" (or \".jpf\"), it is "
           "taken to be a JPX file -- note that in this case, decompression "
           "starts from the first frame defined by the first JPX container, "
           "unless there are no JPX containers -- the intent is to recover "
           "the frames produced by \"kdu_v_compress\", assuming that the "
           "`-jpx_prefix' image supplied to that application contained "
           "only top-level imagery.\n"
           "\t   If the file has the suffix \".jpb\" (or \".jpb2\"), "
           "it is taken to be an elementary broadcast stream (as defined by "
           "IS15444-1/AMD3), consisting of a sequence of elementary broadcast "
           "stream marker super-boxes (`ELSM' boxes).\n"
           "\t   Otherwise, the suffix must be \".mjc\" and the input file "
           "is taken to consist of a 16-byte header, followed by the video "
           "image code-streams, each preceded by a four byte big-endian "
           "length field (length of the code-stream, not including the "
           "length field itself).  For more information on the 16-byte "
           "header, consult the usage statements produced by the "
           "\"kdu_v_compress\" application.\n";
  out << "-o <vix file>\n";
  if (comprehensive)
    out << "\tTo avoid over complicating this simple demonstration "
           "application, decompressed video is written as a VIX file.  VIX "
           "is a trivial non-standard video file format, consisting of a "
           "plain ASCI text header, followed by raw binary data.  A "
           "description of the VIX format is embedded in the usage "
           "statements printed by the \"kdu_v_compress\" application.  It "
           "is legal to leave this argument out, the principle purpose of "
           "which would be to time the decompression processing (using "
           "`-cpu'), without cluttering the statistics with file I/O "
           "issues.\n";
  out << "-fields (first|second|both)\n";
  if (comprehensive)
    out << "\tBy default, only the first field of each interlaced frame "
           "is actually decompressed and written to any output VIX file.  "
           "This argument allows you to specify whether you would like the "
           "second field of each frame or both fields of each frame to be "
           "decompressed.  Note carefully that when both fields are to be "
           "decompressed, they are treated as separate frames for the "
           "purpose of interpreting the other arguments presented by this "
           "application.  So, for example, the `-frames' argument in this "
           "case would be interpreted as an upper bound on the number of "
           "distinct fields to be processed, not the number of "
           "interlaced frames.  It is also worth noting that the simple "
           "VIX file format does not provide any mechanism of distinguishing "
           "between fields.  If the content is not interlaced, this "
           "argument may still be used, but has no effect.\n";
  out << "-frames <max frames to process>\n";
  if (comprehensive)
    out << "\tBy default, all available frames are decompressed.  This "
           "argument may be used to limit the number of frames which are "
           "actually written to the output VIX file.  As noted in "
           "connection with the `-fields' argument, if the content is "
           "interlaced, this argument actually serves to limit the "
           "number of fields that are processed, not the number of "
           "interlaced frames.\n";
  out << "-layers <max layers to decode>\n";
  if (comprehensive)
    out << "\tSet an upper bound on the number of quality layers to actually "
           "decode.\n";
  out << "-components <max image components to decode>\n";
  if (comprehensive)
    out << "\tSet the maximum number of image components to decode from each "
           "frame.  This would typically be used to decode luminance only "
           "from a colour video.\n";
  out << "-reduce <discard levels>\n";
  if (comprehensive)
    out << "\tSet the number of highest resolution levels to be discarded.  "
           "The frame resolution is effectively divided by 2 to the power of "
           "the number of discarded levels.\n";
  out << "-int_region {<top>,<left>},{<height>,<width>}\n";
  if (comprehensive)
    out << "\tEstablish a region of interest within the original compressed "
           "frames.  Only the region of interest will be decompressed and the "
           "output frame dimensions will be modified accordingly.  The "
           "coordinates of the top-left corner of the region are given first, "
           "separated by a comma and enclosed in curly braces, after which "
           "the dimensions of the region are given in similar fashion.  The "
           "two coordinate pairs must be separated by a comma, with no "
           "intervening spaces.  All coordinates and dimensions are expressed "
           "as integer numbers of pixels for the first image component to "
           "be decompressed, taking into account any resolution adjustments "
           "associated with the `-reduce' argument.  The location of the "
           "region is expressed relative to the upper left hand corner of the "
           "relevant image component, at the relevant resolution.  If any "
           "part of the specified region does not intersect with the image, "
           "the decompressed region will be reduced accordingly.  Note that "
           "the `-region' argument offered by the \"kdu_expand\" application "
           "is similar, except that it accepts normalized region coordinates, "
           "in the range 0 to 1.\n";
  out << "-fprec <forced precision to use for generated VIX file>\n";
  if (comprehensive)
    out << "\tThis argument provides a means to change the precision with "
           "which video samples are written to the VIX file.  If the "
           "supplied value is less than the actual precision of compressed "
           "data, only the MSB's will be kept.  This option is mainly "
           "provided as a convenience, for creating 8-bit/sample video "
           "files from higher precision content, so as to expand the range "
           "of content readily available for testing.  The integer parameter "
           "should be in the range 1 to 16, since the application does not "
           "currently support writing VIX files with more than 16 bits "
           "per sample.\n";
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
           "one actual thread of execution.  This incurs some unnecessary "
           "overhead.  Note also that the multi-threading behaviour can "
           "be further customized using the `-double_buffering' and "
           "`-overlapped_frames' arguments.\n";
  out << "-fastest -- use of 16-bit data processing as often as possible.\n";
  if (comprehensive)
    out << "\tThis argument causes image sample processing to use a "
           "16-bit fixed-point representation if possible, even if the "
           "numerical approximation errors associated with this "
           "representation would normally be considered excessive -- makes "
           "no difference unless the original bit-depths recorded in the "
           "compressed codestream headers are around 13 bits or more "
           "(depends upon other coding conditions).\n";
  out << "-precise -- force the use of 32-bit data processing.\n";
  if (comprehensive)
    out << "\tThis option can be useful for extremely high quality "
           "applications, forcing the use of the most accurate "
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
           "performed by the single thread which \"owns\" the "
           "multi-threaded processing group.  For a small number of "
           "processors, this may be acceptable, or even optimal, since "
           "the DWT is generally quite a bit less CPU intensive than "
           "block decoding (which is always spread across "
           "multiple threads, if available) and performing the DWT "
           "synchronously in a single thread may improve memory access "
           "locality.  However, even for a small number of threads, the "
           "amount of thread idle time can be reduced by specifying the "
           "`-double_buffering' option.  In this case, a certain number "
           "of image rows in each image component are actually double "
           "buffered, so that one set can be processed by colour "
           "transformation and sample writing operations, while the other "
           "set is generated by the DWT synthesis engines, which themselves "
           "feed off the block decoding engines.  The number of rows in "
           "each component which are to be double buffered is known as the "
           "\"stripe height\", supplied as a parameter to this argument.  The "
           "stripe height can be as small as 1, but this may add quite a bit "
           "of thread context switching overhead.  For this reason, a stripe "
           "height in the range 8 to 64 is recommended.\n"
           "\t   The default policy for multi-threaded environments is to "
           "pass the special value of -1 to `kdu_multi_synthesis' so that "
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
           "this cannot be realized unless image file writing and compressed "
           "data reading operations for one frame are able to proceed while "
           "another is being processed.  The overlapped frame option achieves "
           "this by instantiating two sets of frame processing resources, "
           "using one to perform decompression, while compressed video data "
           "is being loaded into and previously generated frame sample "
           "data is saved out of the other.  Moreover, once processing for "
           "one frame nears completion, the next frame is able to start "
           "scheduling jobs.  Together, these features usually keep "
           "all threads active as often as possible.\n"
           "\t   With the `-overlapped_frames' option, the maximum throughput "
           "might be achieved with a `-num_threads' value set somewhere "
           "between the number of physical/virtual processors and this "
           "value plus 2 -- because up to two threads can be simultaneously "
           "blocked on disk I/O conditions (one while writing frame data and "
           "one while reading compressed data).  For this reason, you "
           "may find it useful to play around with the `-num_threads' "
           "argument to obtain the best overall throughput on your "
           "platform.\n"
           "\t   You should also note that background reading of compressed "
           "data for a whole frame while the other is being decompressed "
           "occurs only when the `-in_memory' option is in force -- this "
           "happens by default, unless you explicitly specify "
           "`-not_in_memory'.\n";
  out << "-disjoint_frames -- explicitly disable overlapped frames\n";
  if (comprehensive)
    out << "\tDisables the `-overlapped_frames' option, which is "
           "selected by default on platforms with multiple CPU resources, "
           "unless `-num_threads 0' is specified.  One reason you might "
           "want to do this is to obtain separate timing information for "
           "frame loading and frame compression processing via the "
           "`-cpu' option.\n";
  out << "-in_memory [decompression count]\n";
  if (comprehensive)
    out << "\tThis option is meaningful only with the `-overlapped_frames' "
           "option.  It directs a background thread to load the next frame's "
           "compressed bit-stream entirely into memory while a current "
           "frame is being decompressed.  This usually achieves the "
           "maximum possible throughput, but it may cause an excessive "
           "amount of compressed data to be read, in the event that you "
           "are only decompressing a limited region of interest (see "
           "`-int_region'), a reduced resolution (see `-reduce') or a"
           "limited set of image components (see `-components').\n"
           "\t   The `-in_memory' option is selected for you automatically "
           "if whole frames are being decompressed and overlapped frame "
           "processing is in force, but it can be disabled using the "
           "`-not_in_memory' argument.\n"
           "\t   The argument takes an optional positive integer parameter "
           "that indicates the total number of times each compressed source "
           "frame should be fully decompressed and rendered to the internal "
           "frame buffer after it has been loaded to memory.  The default "
           "value for this parameter is 1, but larger values allow you to "
           "get an idea of the fundamental throughput of the video "
           "decompression and rendering processes without having to worry "
           "about I/O bottlenecks that might exist.  When a video frame is "
           "rendered multiple times, only the last one is written to any "
           "output video file, but all renderings are counted in the "
           "summary statistics printed by the application.\n";
  out << "-not_in_memory -- prevent automatic selection of `-in_memory'\n";
  if (comprehensive)
    out << "\tThis argument allows you to explicitly prevent the `-in_memory' "
           "option from being selected for you -- see above for an "
           "explanation of the conditions under which this happens.  You "
           "would only use this argument to explore the impact of memory "
           "buffering of the compressed source data on overall throughput.\n";
  out << "-cpu -- collect CPU time statistics.\n";
  if (comprehensive)
    out << "\tThis option is always enabled unless `-quiet' is specified -- "
           "only then do you need to mention it on the command line to get "
           "reporting of processing time.  Exactly what CPU times can be "
           "reported depends upon whether or not the `-overlapped_frames' "
           "option has been selected (or not explicitly deselected via "
           "`-disjoint_frames').  If it is selected, only the overall "
           "processing time/throughput can be reported.  On the other hand, "
           "if frame processing is not overlapped, the time taken to store "
           "decompressed video frames from memory to disk is reported "
           "separately from the time taken to decompress the frame data "
           "to a memory buffer.\n";
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
/* STATIC                     parse_simple_args                              */
/*****************************************************************************/

static void
  parse_simple_arguments(kdu_args &args, char * &ifname, char * &ofname,
                         int &field_mode, int &max_frames, int &max_layers,
                         int &max_components, int &discard_levels,
                         kdu_dims &region, int &force_prec, int &num_threads,
                         int &double_buffering_height, bool &overlapped_frames,
                         bool &want_fastest, bool &force_precise,
                         int &in_memory_count, bool &cpu, bool &quiet)
  /* Parses most simple arguments (those involving a dash). */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();

  field_mode = 0; // Process only first field of each interlaced frame
  max_frames = INT_MAX;
  num_threads = 0; // Not actually the default value -- see below
  ofname = NULL;
  max_layers = 0;
  max_components = 0;
  discard_levels = 0;
  region.size = region.pos = kdu_coords(0,0);
  force_prec = 0;
  double_buffering_height = 0; // i.e., no double buffering
  overlapped_frames = false;
  in_memory_count = 1;
  want_fastest = false;
  force_precise = false;
  cpu = false;
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

  if (args.find("-fields") != NULL)
    { 
      char *string = args.advance();
      if ((string != NULL) && (strcmp(string,"first") == 0))
        field_mode = 0;
      else if ((string != NULL) && (strcmp(string,"second") == 0))
        field_mode = 1;
      else if ((string != NULL) && (strcmp(string,"both") == 0))
        field_mode = 2;
      else
        { kdu_error e; e << "The `-fields' argument requires a single "
          "string parameter, equal to one of \"first\", \"second\" or "
          "\"both\"."; }
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

  if (args.find("-layers") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&max_layers) != 1) ||
          (max_layers < 1))
        { kdu_error e; e << "\"-layers\" argument requires a positive "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-components") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&max_components) != 1) ||
          (max_components < 1))
        { kdu_error e; e << "\"-components\" argument requires a positive "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-reduce") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&discard_levels) != 1) ||
          (discard_levels < 0))
        { kdu_error e; e << "\"-reduce\" argument requires a non-negative "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-int_region") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"{%d,%d},{%d,%d}",
                  &region.pos.y,&region.pos.x,&region.size.y,&region.size.x)
           != 4) ||
          (region.pos.x < 0) || (region.pos.y < 0) ||
          (region.size.x <= 0) || (region.size.y <= 0))
        { kdu_error e; e << "\"-int_region\" argument requires a set of four "
          "coordinates of the form, \"{<top>,<left>},{<height>,<width>}\", "
          "where `top' and `left' must be non-negative integers, and "
          "`height' and `width' must be positive integers."; }
      args.advance();
    }

  if (args.find("-fprec") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&force_prec) == 0) ||
          (force_prec < 1) || (force_prec > 16))
        { kdu_error e; e << "\"-fprec\" requires an integer parameter in "
          "the range 1 to 16."; }
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
  else if (num_threads > 1)
    double_buffering_height = -1;

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

  if (args.find("-in_memory") != NULL)
    {
      if (!overlapped_frames)
        { kdu_error e; e << "\"-in_memory\" argument may be used only in "
          "conjunction with \"-overlapped_frames\"."; }
      in_memory_count = 1;
      const char *string = args.advance();
      if ((string != NULL) && isdigit(string[0]))
        {
          if ((sscanf(string,"%d",&in_memory_count) == 0) ||
              (in_memory_count < 1))
            { kdu_error e; e << "The optional parameter supplied with "
              "`-in_memory' must be a positive integer, indicating the "
              "total number of times to process each source video frame."; }
          args.advance();
        }
    }
  else if (args.find("-not_in_memory") != NULL)
    { 
      in_memory_count = 0;
      args.advance();
    }
  else if ((discard_levels == 0) && region.is_empty() && overlapped_frames)
    in_memory_count = 1;
  else
    in_memory_count = 0;

  if (args.find("-cpu") != NULL)
    { 
      cpu = true;
      args.advance();
    }

  if (args.find("-quiet") != NULL)
    {
      quiet = true;
      args.advance();
    }
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
/* STATIC                        open_vix_file                               */
/*****************************************************************************/

static FILE *
  open_vix_file(char *fname, kdu_codestream codestream, int discard_levels,
                int num_frames, kdu_uint32 timescale, int frame_period,
                bool is_ycc, int force_precision,
                int &precision, bool &is_signed)
{
  FILE *file;
  if ((file = fopen(fname,"wb")) == NULL)
    { kdu_error e; e << "Unable to open VIX file, \"" << fname << "\", for "
      "writing.  File may be write-protected."; }
  fprintf(file,"vix\n");
  if (timescale == 0)
    timescale = frame_period = 1;
  else if (frame_period == 0)
    frame_period = timescale;
  fprintf(file,">VIDEO<\n%f %d\n",
          ((double) timescale) / ((double) frame_period),
          num_frames);
  fprintf(file,">COLOUR<\n%s\n",(is_ycc)?"YCbCr":"RGB");

  is_signed = codestream.get_signed(0);
  precision = codestream.get_bit_depth(0);
  if (force_precision != 0)
    precision = force_precision;
  const char *container_string = "char";
  if (precision > 8)
    container_string = "word";
  if (precision > 16)
    container_string = "dword";
  int is_big = 1; ((char *)(&is_big))[0] = 0;
  const char *endian_string = (is_big)?"big-endian":"little-endian";
  int components = codestream.get_num_components();
  
  kdu_dims dims; codestream.get_dims(-1,dims);
  kdu_coords min=dims.pos, lim=dims.pos+dims.size;
  min.x = (min.x+1)>>discard_levels;  min.y = (min.y+1)>>discard_levels;
  lim.x = (lim.x+1)>>discard_levels;  lim.y = (lim.y+1)>>discard_levels;
  dims.pos = min; dims.size = lim - min;

  fprintf(file,">IMAGE<\n%s %s %d %s\n",
          ((is_signed)?"signed":"unsigned"),
          container_string,precision,endian_string);
  fprintf(file,"%d %d %d\n",dims.size.x,dims.size.y,components);
  for (int c=0; c < components; c++)
    {
      kdu_coords subs; codestream.get_subsampling(c,subs);
      subs.x >>= discard_levels;  subs.y >>= discard_levels;
      fprintf(file,"%d %d\n",subs.x,subs.y);
    }
  return file;
}

/*****************************************************************************/
/* STATIC                    transfer_line_to_bytes                          */
/*****************************************************************************/

static void
  transfer_line_to_bytes(kdu_line_buf &line, void *dst, int width,
                         int precision, bool is_signed)
{
  kdu_byte *dp = (kdu_byte *) dst;
  kdu_int32 val;
  if (line.get_buf16() != NULL)
    {
      kdu_sample16 *sp = line.get_buf16();
      if (line.is_absolute() && (precision <= 8))
        {
          int upshift = 8-precision;
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = ((kdu_int32) sp->ival) << upshift;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte)(val-128);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = ((kdu_int32) sp->ival) << upshift;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte) val;
              }
        }
      else if (line.is_absolute())
        {
          int downshift = precision-8;
          kdu_int32 offset = (1<<downshift) >> 1;
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = (((kdu_int32) sp->ival) + offset) >> downshift;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte)(val-128);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = (((kdu_int32) sp->ival) + offset) >> downshift;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte) val;
              }
        }
      else
        {
          kdu_int32 offset = 1 << (KDU_FIX_POINT-1);
          offset += (1 << (KDU_FIX_POINT-8)) >> 1;
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = (((kdu_int32) sp->ival) + offset) >> (KDU_FIX_POINT-8);
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte)(val-128);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = (((kdu_int32) sp->ival) + offset) >> (KDU_FIX_POINT-8);
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte) val;
              }
        }
    }
  else
    {
      kdu_sample32 *sp = line.get_buf32();
      if (line.is_absolute())
        {
          assert(precision >= 8);
          int downshift = precision-8;
          kdu_int32 offset = (1<<downshift)-1;
          offset += 128 << downshift;
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = (sp->ival + offset) >> downshift;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte)(val-128);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = (sp->ival + offset) >> downshift;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte) val;
              }
        }
      else
        {
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = (kdu_int32)(sp->fval * (float)(1<<24));
                val = (val + (1<<15)) >> 16;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte)(val-128);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = (kdu_int32)(sp->fval * (float)(1<<24));
                val = (val + (1<<15)) >> 16;
                val += 128;
                if (val & 0xFFFFFF00)
                  val = (val < 0)?0:255;
                *dp = (kdu_byte) val;
              }
        }
    }
}

/*****************************************************************************/
/* STATIC                    transfer_line_to_words                          */
/*****************************************************************************/

static void
  transfer_line_to_words(kdu_line_buf &line, void *dst, int width,
                         int precision, bool is_signed)
{
  kdu_int16 *dp = (kdu_int16 *) dst;
  kdu_int32 val;
  if (line.get_buf16() != NULL)
    {
      kdu_sample16 *sp = line.get_buf16();
      if (line.is_absolute())
        {
          int upshift = 16-precision;
          if (is_signed)
            for (; width--; sp++, dp++)
              {
                val = ((kdu_int32) sp->ival) << upshift;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
          else
            for (; width--; sp++, dp++)
              {
                val = ((kdu_int32) sp->ival) << upshift;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16) val;
              }
        }
      else
        {
          if (is_signed)
            for (; width--; sp++, dp++)
              {
                val = ((kdu_int32) sp->ival) << (16-KDU_FIX_POINT);
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
          else
            for (; width--; sp++, dp++)
              {
                val = ((kdu_int32) sp->ival) << (16-KDU_FIX_POINT);
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16) val;
              }
        }
    }
  else
    {
      kdu_sample32 *sp = line.get_buf32();
      if (line.is_absolute() && (precision > 16))
        {
          int downshift = precision - 16;
          kdu_int32 offset = (1<<downshift) - 1;
          offset += 0x8000 << downshift;
          if (is_signed)
            for (; width--; sp++, dp++)
              {
                val = (sp->ival + offset) >> downshift;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
          else
            for (; width--; sp++, dp++)
              {
                val = (sp->ival + offset) >> downshift;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
        }
      else if (line.is_absolute())
        {
          int upshift = 16-precision;
          if (is_signed)
            for (; width--; sp++, dp++)
              {
                val = sp->ival << upshift;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
          else
            for (; width--; sp++, dp++)
              {
                val = sp->ival << upshift;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16) val;
              }
        }
      else
        {
          if (is_signed)
            for (; width--; dp++, sp++)
              {
                val = (kdu_int32)(sp->fval * (float)(1<<24));
                val = (val + (1<<7)) >> 8;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16)(val - 0x8000);
              }
          else
            for (; width--; dp++, sp++)
              {
                val = (kdu_int32)(sp->fval * (float)(1<<24));
                val = (val + (1<<7)) >> 8;
                val += 0x8000;
                if (val & 0xFFFF0000)
                  val = (val < 0)?0:0x0000FFFF;
                *dp = (kdu_int16) val;
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
  num_frames_loaded = num_frames_retrieved = num_frames_stored = 0;
  fpnum = 0;
  active_loader = active_storer = NULL;
  ready_loader = ready_storer = NULL;
  ready_starter = NULL;
  next_start_sequence_idx = 0;
  waiting_decompressor = NULL;
  waiting_cond = NULL;
  load_job.init(this);
  store_job.init(this);
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

void kdv_io_queue::processor_joined(kdv_decompressor *obj, kdu_thread_env *env)
{
  mutex.lock();
  assert((ready_loader == NULL) && (ready_storer == NULL));
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

void kdv_io_queue::frame_processed(kdv_decompressor *obj, kdu_thread_env *env)
{
  mutex.lock();
  assert((ready_loader == NULL) && (ready_storer == NULL));
  if (active_loader != NULL)
    ready_loader = obj;
  else if (num_frames_loaded < max_frames_to_load)
    { 
      active_loader = obj;
      this->schedule_job(&load_job,env);
      assert(ready_starter == NULL);
    }
  if (active_storer != NULL)
    ready_storer = obj;
  else
    { 
      active_storer = obj;
      this->schedule_job(&store_job,env);
    } 
  mutex.unlock();
}

/*****************************************************************************/
/*                         kdv_io_queue::wait_for_io                         */
/*****************************************************************************/

bool kdv_io_queue::wait_for_io(kdv_decompressor *obj, kdu_thread_env *env)
{
  bool result = false;
  mutex.lock();
  assert((obj != ready_loader) && (obj != ready_storer));
  if ((obj == active_loader) || (obj == active_storer))
    { // Need to wait until load, flush or both complete
      assert(waiting_decompressor == NULL);
      waiting_decompressor = obj;
      waiting_cond = env->get_condition();
      mutex.unlock();
      env->wait_for_condition();
      mutex.lock();
      assert((obj != active_loader) && (obj != active_storer) &&
             (obj != waiting_decompressor));
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
/*                    kdv_io_queue::last_tile_started                        */
/*****************************************************************************/

void kdv_io_queue::last_tile_started(kdv_decompressor *obj,
                                     kdu_long next_seq_idx,
                                     kdu_thread_env *env)
{
  assert(next_start_sequence_idx < 0); // We have been waiting for this call
  mutex.lock();
  next_start_sequence_idx = next_seq_idx;
  kdv_decompressor *start_obj = ready_starter;
  if (start_obj != NULL)
    { 
      ready_starter = NULL;
      next_start_sequence_idx = -1;
    }
  mutex.unlock();
  if (start_obj != NULL)
    start_obj->start_frame(next_seq_idx,env);
}

/*****************************************************************************/
/*                          kdv_io_queue::do_load                            */
/*****************************************************************************/

void kdv_io_queue::do_load(kdu_thread_env *env)
{
  assert((active_loader != NULL) && (ready_starter == NULL));
  bool success = active_loader->load_frame(env,true,&fpnum);
  mutex.lock();
  if (success && (num_frames_loaded < max_frames_to_load))
    {
      ready_starter = active_loader;
      if (next_start_sequence_idx >= 0)
        { 
          kdu_long idx = next_start_sequence_idx;
          next_start_sequence_idx = -1;
          ready_starter = NULL;
          mutex.unlock();
          active_loader->start_frame(idx,env);
          mutex.lock();
        }
    }
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
                        (active_storer == NULL) && (active_loader == NULL));
  if ((waiting_decompressor != NULL) &&
      (waiting_decompressor != active_loader) &&
      (waiting_decompressor != active_storer))
    { // Waiting compressor can wake up
      env->signal_condition(waiting_cond);
      waiting_cond = NULL;
      waiting_decompressor = NULL;
    }
  mutex.unlock();
  if (need_all_done)
    all_done(env); // This should be the last thing we do
}

/*****************************************************************************/
/*                          kdv_io_queue::do_store                           */
/*****************************************************************************/

void kdv_io_queue::do_store(kdu_thread_env *env)
{
  assert(active_storer != NULL);
  active_storer->store_frame();
  mutex.lock();
  num_frames_stored++;
  if ((active_storer = ready_storer) != NULL)
    { 
      ready_storer = NULL;
      schedule_job(&store_job,env);
    }
  bool need_all_done =
    ((active_storer == NULL) && (active_loader == NULL) &&
     (termination_requested || (num_frames_stored >= max_frames_to_load)));
  if ((waiting_decompressor != NULL) &&
      (waiting_decompressor != active_loader) &&
      (waiting_decompressor != active_storer))
    { // Waiting compressor can wake up
      env->signal_condition(waiting_cond);
      waiting_cond = NULL;
      waiting_decompressor = NULL;
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
  ready_storer = ready_loader = NULL;
  max_frames_to_load = num_frames_loaded;
  bool need_all_done = (active_storer == NULL) && (active_loader == NULL);
  mutex.unlock();
  if (need_all_done)
    all_done(env); // Best not to invoke this with the mutex locked
}


/* ========================================================================= */
/*                              kdv_decompressor                             */
/* ========================================================================= */

/*****************************************************************************/
/*                     kdv_decompressor::kdv_decompressor                    */
/*****************************************************************************/

kdv_decompressor::kdv_decompressor(kdu_compressed_video_source *video,
                                   int which_fields,
                                   FILE *store_fp, int frame_repeat,
                                   kdu_codestream initial_codestream,
                                   int discard_levels, int max_layers,
                                   kdu_dims *region_of_interest,
                                   bool load_compressed_data_to_memory,
                                   int precision, bool is_signed,
                                   int double_buffering_height,
                                   bool want_fastest, bool force_precise,
                                   kdu_thread_env *env,
                                   kdv_io_queue *bkgnd_io_queue)
{
  int c;

  this->src = video;
  this->field_mode = which_fields;
  this->io_queue = bkgnd_io_queue;
  this->frame_repetitions = (frame_repeat <= 0)?0:frame_repeat;
  this->double_buffering = (double_buffering_height != 0);
  this->processing_stripe_height=(double_buffering)?double_buffering_height:1;
  this->want_fastest = want_fastest;
  this->force_precise = force_precise;
  
  this->fp = store_fp;
  this->is_signed = is_signed;
  sample_bytes = 1;
  if (precision > 8)
    sample_bytes = 2;
  if (precision > 16)
    sample_bytes = 4;
  frame_bytes = 0; // We will increment this as we go.

  frame_reps_remaining = 0;
  buf = buf_handle = NULL;
  num_components = initial_codestream.get_num_components();
  comp_info = new kdv_comp_info[num_components];
  for (c=0; c < num_components; c++)
    {
      kdv_comp_info *comp = comp_info + c;
      initial_codestream.get_dims(c,comp->comp_dims);
      kdu_coords  subs; initial_codestream.get_subsampling(c,subs);
      comp->count_val = subs.y;
      frame_bytes += sample_bytes * (int) comp->comp_dims.area();
    }
  buf_handle = buf = new kdu_byte[frame_bytes+64];
  buf += ((-_addr_to_kdu_int32(buf))&31); // Align on 32-byte boundary; this
           // maximizes the chance that SIMD data transfer operations will be
           // able to use aligned writes.
  kdu_byte *comp_buf = buf;
  for (c=0; c < num_components; c++)
    {
      kdv_comp_info *comp = comp_info + c;
      comp->comp_buf = comp_buf;
      comp_buf += sample_bytes * (int) comp->comp_dims.area();
    }

  sequence_idx = 0;
  current_tile = next_tile = NULL;

  // Configure the codestream parameters
  this->load_in_memory = load_compressed_data_to_memory;
  this->discard_levels = discard_levels;
  this->max_layers = max_layers;
  this->region_ref = NULL;
  if (region_of_interest != NULL)
    { 
      this->region = *region_of_interest;
      this->region_ref = &region;
    }
  if (io_queue == NULL)
    { // All ready to go; no need to load initial frame
      this->image_open = true;
      this->codestream = initial_codestream;
      codestream.enable_restart();
    }
  else
    { // Need to create our own internal codestream and open our own
      // `jp2_input_box' from `mj2_src'.  This is done automatically by
      // the `io_queue' once we register.
      this->image_open = false;
      io_queue->processor_joined(this,env);
    }

#ifdef KDU_SIMD_OPTIMIZATIONS
  kdu_check_sample_alignment(); // Best to verify that there is no
          // mismatch between the sample alignment definitions currently
          // found in "kdu_arch.h" and those present at the time when the
          // core system was compiled, since our line buffers are allocated
          // inside the core system's `kdu_multi_analysis' object.
#endif // KDU_SIMD_OPTIMIZATIONS
}

/*****************************************************************************/
/*                      kdv_decompressor::load_frame                         */
/*****************************************************************************/

bool
  kdv_decompressor::load_frame(kdu_thread_env *env, bool in_io_job,
                               int *fpnum)
{
  if (io_queue == NULL)
    { // Simple case in which `src->image_open' and `src->image_close' are
      // used to open and close frames consecutively.  In this case
      // we don't need `fpnum'.
      assert(!in_io_job);
      if (frame_repetitions > 0)
        { kdu_error e; e << "Frame repetition is not supported with "
          "serial data sources or non-overlapped processing modes -- use an "
          "MJ2 data source and overlapped processing."; }
      if (!image_open)
        { // For the very first frame, the image is already open
          if (src->open_image() < 0)
            return false;
          image_open = true;
          codestream.restart(src,env);
          codestream.apply_input_restrictions(0,num_components,discard_levels,
                                              max_layers,region_ref,
                                              KDU_WANT_OUTPUT_COMPONENTS);
        }
      start_frame(sequence_idx,env);
    }
  else
    { // Overlapped frame processing
      if (!in_io_job)
        return io_queue->wait_for_io(this,env);
      assert(!image_open);
      assert(fpnum != NULL);
      if (frame_reps_remaining > 0)
        { 
          src_box.seek(0);
          codestream.restart(&src_box,env);
          frame_reps_remaining--;
        }
      else
        { 
          assert(!src_box.exists());
          int frame_idx = (*fpnum) >> 1;
          int field_idx = (*fpnum) & 1;
          if (!src->seek_to_frame(frame_idx))
            return false;
          bool interlaced = (src->get_field_order() != KDU_FIELDS_NONE);
          if (interlaced && (field_mode == 1))
            field_idx = 1; // In case it is currently 0
          if (src->open_stream(field_idx,&src_box) != frame_idx)
            return false;
          if ((!interlaced) || (field_mode != 2))
            *fpnum = 2*(frame_idx+1); // Adjust to first field of next frame
          else
            *fpnum = 2*frame_idx + field_idx + 1;
          if (!codestream.exists())
            { 
              codestream.create(&src_box,env);
              codestream.enable_restart();
            }
          else
            codestream.restart(&src_box,env);
          if (load_in_memory)
            src_box.load_in_memory(100000000);
          frame_reps_remaining = frame_repetitions;
        }
      codestream.apply_input_restrictions(0,num_components,discard_levels,
                                          max_layers,region_ref,
                                          KDU_WANT_OUTPUT_COMPONENTS);
    }
  return true;
}

/*****************************************************************************/
/*                      kdv_decompressor::start_frame                        */
/*****************************************************************************/

void kdv_decompressor::start_frame(kdu_long initial_sequence_idx,
                                   kdu_thread_env *env)
{
  assert((current_tile == NULL) && (next_tile == NULL));
  this->sequence_idx = initial_sequence_idx;
  current_tile = tiles;
  if (env != NULL)
    {
      next_tile = tiles+1;
      current_tile->next = next_tile;
      next_tile->next = current_tile;
      init_tile(next_tile,env);
    }
}

/*****************************************************************************/
/*                       kdv_decompressor::process_frame                     */
/*****************************************************************************/

void kdv_decompressor::process_frame(kdu_thread_env *env)
{
  int c;
  bool frame_complete = false;
  while (!frame_complete)
    {
      if (env != NULL)
        { // Set up the current and next tile engine
          assert((current_tile != NULL) && (next_tile != NULL));
          next_tile = (current_tile = next_tile)->next;
          assert(current_tile->is_active());
          frame_complete = !init_tile(next_tile,current_tile,env);
          if (frame_complete && (io_queue != NULL))
            io_queue->last_tile_started(this,sequence_idx,env);
        }
      else
        { // Set up the current tile engine only
          assert(next_tile == NULL);
          if (!current_tile->is_active())
            init_tile(current_tile,NULL); // Must be first tile
        }

      // Now process the tile
      bool done = false;
      while (!done)
        {
          done = true;

          // Decompress a line for each component whose counter is 0
          for (c=0; c < num_components; c++)
            { 
              kdv_tcomp *comp = current_tile->components + c;
              if (comp->tile_rows_left == 0)
                continue;
              done = false;
              comp->counter--;
              if (comp->counter >= 0)
                continue;
              comp->counter += comp_info[c].count_val;
              comp->tile_rows_left--;
              kdu_line_buf *line = current_tile->engine.get_line(c,env);
              if (comp->simd_transfer != NULL)
                { 
                  void *dst=comp->bp;
                  void *src=line->get_buf(); // Good for all data types
                  int tgt_prec=8*sample_bytes; // VIX samples MSB-aligned
                  int src_prec=comp->precision;
                  comp->simd_transfer(dst,&src,comp->tile_size.x,tgt_prec,
                                      src_prec,comp->is_absolute,is_signed,
                                      KDU_STRIPE_STORE_PREF_STREAMING);
                  comp->bp += comp->comp_size.x*sample_bytes;
                }
              else if (sample_bytes == 1)
                { 
                  transfer_line_to_bytes(*line,comp->bp,comp->tile_size.x,
                                         comp->precision,is_signed);
                  comp->bp += comp->comp_size.x;
                }
              else if (sample_bytes == 2)
                {
                  transfer_line_to_words(*line,comp->bp,comp->tile_size.x,
                                         comp->precision,is_signed);
                  comp->bp += (comp->comp_size.x << 1);
                }
              else
                { kdu_error e; e << "The VIX writer provided here "
                    "currently supports only \"byte\" and \"word\" data "
                    "types as sample data containers.   The \"dword\" "
                    "data type could easily be added if necessary."; }
            }
        }

      // All processing done for current tile
      close_tile(current_tile,env);
      if (env == NULL)
        frame_complete = !init_tile(current_tile,current_tile,NULL);
    }

  if (env != NULL)
    env->cs_terminate(codestream);
  current_tile = next_tile = NULL;
  if (src_box.exists() && (frame_reps_remaining == 0))
    src_box.close();
  if (image_open)
    { 
      assert(io_queue == NULL);
      src->close_image();
      image_open = false;
    }
  else
    { 
      assert(io_queue != NULL);
      io_queue->frame_processed(this,env);
    }
}

/*****************************************************************************/
/*                       kdv_decompressor::store_frame                       */
/*****************************************************************************/

void kdv_decompressor::store_frame()
{
  if ((fp == NULL) || (frame_reps_remaining > 0))
    return;
  if (fwrite(buf,1,(size_t) frame_bytes,fp) != (size_t) frame_bytes)
    { kdu_error e; e << "Unable to write to output VIX file.  Device may "
      "be full."; }
}

/*****************************************************************************/
/*                       kdv_decompressor::init_tile                         */
/*****************************************************************************/

bool
  kdv_decompressor::init_tile(kdv_tile *tile, kdu_thread_env *env)
{
  int c;
  assert(!(tile->engine.exists() || tile->ifc.exists()));
  codestream.get_valid_tiles(tile->valid_indices);
  tile->idx = kdu_coords();
  tile->ifc = codestream.open_tile(tile->valid_indices.pos,env);
  if (tile->components == NULL)
    tile->components = new kdv_tcomp[num_components];
  if (env != NULL)
    tile->env_queue = env->add_queue(NULL,NULL,NULL,sequence_idx++);
  tile->engine.create(codestream,tile->ifc,force_precise,false,want_fastest,
                      processing_stripe_height,env,tile->env_queue,
                      double_buffering);
  for (c=0; c < num_components; c++)
    { 
      kdv_tcomp *comp = tile->components + c;
      comp->comp_size = comp_info[c].comp_dims.size;
      comp->tile_size = tile->engine.get_size(c);
      comp->counter = 0;
      comp->precision = codestream.get_bit_depth(c,true);
      comp->is_absolute = tile->engine.is_line_absolute(c);
      comp->uses_shorts = !tile->engine.is_line_precise(c);
      comp->tile_rows_left = comp->tile_size.y;
      comp->bp = comp->next_tile_bp = comp_info[c].comp_buf;
      if (tile->idx.x < (tile->valid_indices.size.x-1))
        comp->next_tile_bp += sample_bytes * comp->tile_size.x;
      else
        comp->next_tile_bp += sample_bytes *
          (comp->tile_size.x + (comp->tile_size.y-1)*comp->comp_size.x);
      if ((comp->next_tile_bp-comp_info[c].comp_buf) >
          (sample_bytes*comp->comp_size.x*comp->comp_size.y))
        { kdu_error e; e << "All frames in the compressed video sequence "
          "should have the same dimensions for each image component.  "
          "Encountered a frame for which one image component "
          "is larger than the corresponding component in the first frame."; }
      comp->simd_transfer = NULL;
#ifdef KDU_SIMD_OPTIMIZATIONS
      int out_buf_type=0;
      if (sample_bytes == 1)
        out_buf_type = KDSD_BUF8;
      else if (sample_bytes == 2)
        out_buf_type = KDSD_BUF16;
      else if (sample_bytes == 4)
        out_buf_type = KDSD_BUF32;
      else
        assert(0);
      using namespace kd_supp_local; // Just for the macro below
      KDSD_FIND_SIMD_TRANSFER_FUNC(comp->simd_transfer,out_buf_type,
                                   comp->uses_shorts,1,comp->tile_size.x,
                                   (8*sample_bytes),comp->precision,
                                   comp->is_absolute);
#endif
    }
  return true;
}

/*****************************************************************************/
/*                 kdv_decompressor::init_tile (ref_tile)                    */
/*****************************************************************************/

bool
  kdv_decompressor::init_tile(kdv_tile *tile, kdv_tile *ref_tile,
                              kdu_thread_env *env)
{
  int c;
  assert(!(tile->engine.exists() || tile->ifc.exists()));
  tile->valid_indices = ref_tile->valid_indices;
  tile->idx = ref_tile->idx;
  tile->idx.x++;
  if (tile->idx.x >= tile->valid_indices.size.x)
    {
      tile->idx.x = 0;
      tile->idx.y++;
      if (tile->idx.y >= tile->valid_indices.size.y)
        return false;
    }
  tile->ifc = codestream.open_tile(tile->valid_indices.pos+tile->idx,env);
  if (tile->components == NULL)
    tile->components = new kdv_tcomp[num_components];
  if (env != NULL)
    tile->env_queue = env->add_queue(NULL,NULL,NULL,sequence_idx++);
  tile->engine.create(codestream,tile->ifc,force_precise,false,want_fastest,
                      processing_stripe_height,env,tile->env_queue,
                      double_buffering);
  for (c=0; c < num_components; c++)
    {
      kdv_tcomp *comp = tile->components + c;
      kdv_tcomp *ref_comp = ref_tile->components + c;
      comp->comp_size = ref_comp->comp_size;
      comp->tile_size = tile->engine.get_size(c);
      comp->counter = 0;
      comp->precision = ref_comp->precision;
      comp->tile_rows_left = comp->tile_size.y;
      comp->bp = comp->next_tile_bp = ref_comp->next_tile_bp;
      if (tile->idx.x < (tile->valid_indices.size.x-1))
        comp->next_tile_bp += sample_bytes * comp->tile_size.x;
      else
        comp->next_tile_bp += sample_bytes *
          (comp->tile_size.x + (comp->tile_size.y-1)*comp->comp_size.x);
      if ((comp->next_tile_bp-comp_info[c].comp_buf) >
          (sample_bytes*comp->comp_size.x*comp->comp_size.y))
        { kdu_error e; e << "All frames in the compressed video sequence "
          "should have the same dimensions for each image component.  "
          "Encountered a frame for which one image component "
          "is larger than the corresponding component in the first frame."; }
    }
  return true;
}

/*****************************************************************************/
/*                       kdv_decompressor::close_tile                        */
/*****************************************************************************/

void
  kdv_decompressor::close_tile(kdv_tile *tile, kdu_thread_env *env)
{
  if (!tile->ifc)
    return;
  if (env != NULL)
    env->terminate(tile->env_queue,false);
  tile->env_queue = NULL; // Should have been destroyed by `env'
  tile->engine.destroy();
  tile->ifc.close(env);
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

  // Collect arguments.
  char *ifname = NULL;
  char *ofname = NULL;
  int field_mode=0, max_pictures=INT_MAX;
  int max_layers, max_components, discard_levels, force_precision;
  int num_threads, double_buffering_height;
  kdu_dims region;
  int in_memory_count;
  bool overlapped_pictures, want_fastest, force_precise, cpu, quiet;
  parse_simple_arguments(args,ifname,ofname,field_mode,max_pictures,max_layers,
                         max_components,discard_levels,region,force_precision,
                         num_threads,double_buffering_height,
                         overlapped_pictures,want_fastest,force_precise,
                         in_memory_count,cpu,quiet);
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }
  bool in_memory = (in_memory_count > 0);
  int picture_repeat = (in_memory)?(in_memory_count-1):0;

  // Open input file and first codestream
  jp2_threadsafe_family_src family_src;
  mj2_source movie;
  
  jpb_source broadcast_source;
  kdv_simple_video_source mjc_video;
  kdu_compressed_video_source *video=NULL;
  jpx_source composit_source;
  kdv_jpx_source *jpx_video = NULL;
  bool is_ycc=false;
  if (check_mj2_suffix(ifname))
    { 
      family_src.open(ifname);
      movie.open(&family_src);
      mj2_video_source *mj2_video = movie.access_video_track(1);
      if (mj2_video == NULL)
        { kdu_error e; e << "Motion JPEG2000 data source contains no video "
          "tracks."; }
      is_ycc = (mj2_video->access_colour().is_opponent_space());
      video = mj2_video;
      video->set_field_mode(field_mode);
    }
  else if (check_jpx_suffix(ifname))
    { 
      family_src.open(ifname);
      composit_source.open(&family_src,false);
      jpx_composition composition = composit_source.access_composition();
      jpx_container_source container = composit_source.access_container(0);
      if (container.exists())
        composition = container.access_presentation_track(1);
      jx_frame *frm = NULL;
      if ((!composition) || ((frm = composition.get_next_frame(NULL)) == NULL))
        { kdu_error e; e << "Supplied JPX input file does not appear to have "
          "a suitable animation frame from which to start decompressing.  "
          "This application expects to start from the first frame defined by "
          "the first JPX container, or the first top-level animation frame "
          "if there are no JPX containers."; }
      jpx_frame frame = composition.get_interface_for_frame(frm,0,false);
      int layer_idx=0; kdu_dims src_dims, tgt_dims;
      jpx_composited_orientation orient;
      frame.get_instruction(0,layer_idx,src_dims,tgt_dims,orient);
      jpx_layer_source layer = composit_source.access_layer(layer_idx);
      if (!layer.exists())
        { kdu_error e; e << "Unable to access first compositing layer "
          "used by the first suitable animation frame in the source JPX "
          "file."; }
      is_ycc = layer.access_colour(0).is_opponent_space();
      jpx_video = new kdv_jpx_source(&composit_source,1,frame);
      video = jpx_video;
      field_mode = 0; // Interlaced video not supported here anyway
    }
  else if (check_broadcast_suffix(ifname))
    { 
      family_src.open(ifname);
      broadcast_source.open(&family_src);
      kdu_byte colour = broadcast_source.get_frame_space();
      is_ycc = (colour == JPB_601_SPACE) || (colour == JPB_709_SPACE) ||
      (colour == JPB_LUV_SPACE);
      video = &broadcast_source;
      video->set_field_mode(field_mode);
    }
  else if (check_mjc_suffix(ifname))
    { 
      kdu_uint32 flags;
      mjc_video.open(ifname,flags);
      is_ycc = ((flags & KDU_SIMPLE_VIDEO_YCC) != 0);
      video = &mjc_video;
      field_mode = 0; // Interlaced video not supported here anyway
      if (overlapped_pictures && !in_memory)
        { kdu_error e; e << "The `-overlapped_frames' and `-not_in_memory' "
          "arguments cannot both be used with MJC input files -- these are a "
          "simple, non-standard concatenation of codestreams, designed for "
          "simple demonstrations, without the machinery for concurrent access "
          "to compressed content from multiple threads, so it is only safe "
          "to concurrently access multiple frames from such sources if they "
          "are loaded into memory.  Use an MJ2 or JPB source instead, "
          "or else use one of the `-disjoint_frames' or `-in_memory' options.";
        }
    }
  else
    { kdu_error e; e << "Input file must have one of the suffices "
      "\".mj2\", \".mjp2\", \".jpb\", \".jpb2\" or \".mjc\".  See "
      "usage statement for more information."; }
  int pictures_per_frame = (video->get_field_order()==KDU_FIELDS_NONE)?1:2;
  if (field_mode < 2)
    pictures_per_frame = 1; // Only actually decoding 1 picture/frame
  
  if ((max_components > 0) && (max_components < 3))
    is_ycc = false;

  if (video->open_image() < 0)
    { kdu_error e; e << "Compressed video source contains no images!"; }

  kdu_codestream codestream;
  codestream.create(video);

  // Restrict quality, resolution and/or spatial region as required
  codestream.apply_input_restrictions(0,max_components,discard_levels,
                                      max_layers,NULL,
                                      KDU_WANT_OUTPUT_COMPONENTS);
  kdu_dims *reg_ptr = NULL;
  if (region.area() > 0)
    {
      kdu_dims dims; codestream.get_dims(0,dims);
      dims &= region;
      if (!dims)
        { kdu_error e; e << "Region supplied via `-int_region' argument "
          "has no intersection with the first image component to be "
          "decompressed, at the resolution selected."; }
      codestream.map_region(0,dims,region);
      reg_ptr = &region;
      codestream.apply_input_restrictions(0,max_components,discard_levels,
                                          max_layers,reg_ptr,
                                          KDU_WANT_OUTPUT_COMPONENTS);
    }

        // If you wish to have geometric transformations folded into the
        // decompression process automatically, this is the place to call
        // `kdu_codestream::change_appearance'.

  // Create VIX file
  int num_pictures = video->get_num_frames() * pictures_per_frame;
  if (num_pictures > max_pictures)
    num_pictures = max_pictures;
  kdu_uint32 timescale = video->get_timescale();
  int picture_period = (int) video->get_frame_period();
  timescale *= (kdu_uint32) pictures_per_frame;
  
  int precision=0; // Value set by `open_vix_file' call below
  bool is_signed=false; // Value set by `open_vix_file' call below
  FILE *vix_file = NULL;
  if (ofname != NULL)
    vix_file = open_vix_file(ofname,codestream,discard_levels,num_pictures,
                             timescale,picture_period,is_ycc,force_precision,
                             precision,is_signed);
  
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

  // Create decompression machinery
  int max_processed_pictures = max_pictures;
  if ((num_pictures > 0) && (num_pictures < max_processed_pictures))
    max_processed_pictures = num_pictures;
  if ((in_memory_count > 1) &&
      (max_processed_pictures < (INT_MAX/in_memory_count)))
    max_processed_pictures *= in_memory_count;

  int c, num_processors = 1;
  if (overlapped_pictures)
    { 
      if (env_ref == NULL)
        { kdu_error e; e << "The `-overlapped_frames' processing option "
          "cannot be used in single-threaded mode -- see `-num_threads'\n"; }
      num_processors = 2;
    }
  
  kdv_decompressor *picture_decompressors[2]={NULL,NULL};
  kdv_io_queue *io_queue = NULL;
  if (overlapped_pictures)
    {
      io_queue = new kdv_io_queue(max_processed_pictures);
      env_ref->attach_queue(io_queue,NULL,"I/O Domain",0,
                            KDU_THREAD_QUEUE_BACKGROUND);
      video->close_image();
    }
  for (c=0; c < num_processors; c++)
    picture_decompressors[c] =
      new kdv_decompressor(video,field_mode,vix_file,picture_repeat,codestream,
                           discard_levels,max_layers,reg_ptr,in_memory,
                           precision,is_signed,double_buffering_height,
                           want_fastest,force_precise,env_ref,io_queue);
  if (overlapped_pictures)
    codestream.destroy(); // We won't be needing this temporary object again

  // Decompress the pictures
  double total_time = 0.0;
  double file_time = 0.0;
  double last_report_time = 0.0;
  int picture_count = 0;
  kdu_clock timer;
  for (; picture_count < max_processed_pictures; picture_count++)
    { 
      kdv_decompressor *decompressor =
        picture_decompressors[(num_processors==1)?0:(picture_count & 1)];

      if (!decompressor->load_frame(env_ref,false))
        break; // Cannot read any more compressed frames

      decompressor->process_frame(env_ref);

      if ((vix_file != NULL) && !overlapped_pictures)
        { 
          total_time += timer.get_ellapsed_seconds();
          decompressor->store_frame();
          double store_time = timer.get_ellapsed_seconds();
          file_time += store_time;
          total_time += store_time;
        }
      else
        total_time += timer.get_ellapsed_seconds();

      if ((!quiet) && (total_time >= (last_report_time+0.5)))
        { 
          last_report_time = total_time;
          pretty_cout << picture_count+1
                      << " pictures processed -- avg rate is "
                      << (picture_count+1)/total_time << " pictures/s\n";
        }
    }

  // Print summary statistics
  if (cpu || !quiet)
    pretty_cout << picture_count
                << " pictures processed -- avg rate is "
                << picture_count/total_time << " pictures/s\n\n";

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
          << ((in_memory)?", with\n    source pictures pre-loaded to memory"
                         :"")
          << "\n";
    }

  if (cpu || !quiet)
    { 
      pretty_cout << "Total time = " << total_time << "s\n";
      if (picture_count && (vix_file != NULL) && !overlapped_pictures)
        { // Only used one decompressor; can separate reading/processing
          pretty_cout << "\tActual decompression processing time = "
                      << (total_time-file_time)/picture_count
                      << " seconds per picture\n";
          pretty_cout << "\tFile writing time = "
                      << file_time/picture_count
                      << " seconds per picture\n";
        }
    }
  
  // Cleanup
  if (codestream.exists())
    codestream.destroy();
  if (io_queue != NULL)
    {
      env_ref->join(io_queue); // Wait for background processing to finish
      delete io_queue;
    }
  if (env.exists())
    env.terminate(NULL,true);
  delete[] ifname;
  if (ofname != NULL)
    delete[] ofname;
  if (vix_file != NULL)
    fclose(vix_file);
  for (c=0; c < 2; c++)
    if (picture_decompressors[c] != NULL)
      delete picture_decompressors[c];
  video->close();
  if (jpx_video != NULL)
    delete jpx_video;
  movie.close(); // Does nothing if not an MJ2 file.
  composit_source.close(); // Does nothing if not a JPX file.
  family_src.close();
  if (env.exists())
    env.destroy(); // Destroy multi-threading environment, if one was used
  return 0;
}
