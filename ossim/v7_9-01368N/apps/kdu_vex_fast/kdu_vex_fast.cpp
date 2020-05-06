/*****************************************************************************/
// File: kdu_vex_fast.cpp [scope = APPS/VEX_FAST]
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
   High Performance Motion JPEG2000 decompressor -- this demo can form the
foundation for a real-time software-based digital cinema solution.
******************************************************************************/

#include <string.h>
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
#include "kdu_vex.h"
#include "vex_display.h"

using namespace kdu_supp_vex; // Includes namespaces `kdu_supp' and `kdu_core'


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
  out << "This is Kakadu's \"kdu_vex_fast\" application.\n";
  out << "\tCompiled against the Kakadu core system, version "
      << KDU_CORE_VERSION << "\n";
  out << "\tCurrent core system version is "
      << kdu_get_core_version() << "\n";
  out << "This demo application could form the basis for a real-time "
         "software-only digital cinema playback solution, provided a "
         "sufficiently powerful computational platform.\n";
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
  out << "-i <MJ2 or JPX input file>\n";
  if (comprehensive)
    out << "\tEither an MJ2 or a JPX file may be supplied -- the "
           "application figures out the type based on the file "
           "contents, rather than a file suffix.  If the case of JPX "
           "files, decompression starts from the first frame defined by "
           "first JPX container, unless there are no JPX containers; this "
           "is the same policy as that used by \"kdu_v_expand\" -- in "
           "both applications, the intent is to recover the frames "
           "produced by \"kdu_v_compress\", assuming that the \"-jpx_prefix\" "
           "image supplied to that application contained only top-level "
           "imagery.  As with \"kdu_v_expand\", this application does "
           "not perform any of the higher level composition, scaling, "
           "rotation or colour conversion tasks that may be involved with "
           "a complete rendering of arbitrary JPX animation frames (those "
           "activities are performed by the \"kdu_show\" demo apps).  "
           "Instead, the first first codestream used by each animation "
           "frame is decompressed as if it were the entire video frame, "
           "ignoring any other composited codestreams.\n";
  out << "-o <vix file>\n";
  if (comprehensive)
    out << "\tTo avoid over complicating this simple demonstration "
           "application, decompressed video is written as a VIX file.  VIX "
           "is a trivial non-standard video file format, consisting of a "
           "plain ASCI text header, followed by raw binary data.  A "
           "description of the VIX format is embedded in the usage "
           "statements printed by the \"kdu_v_compress\" application.  "
           "If neither this argument nor \"-display\" is supplied, the "
           "program writes rendered data to a buffer, as if it were about "
           "to write to disk, but without incurring the actual I/O "
           "overhead -- the principle purpose of this would be to time "
           "the decompression processing alone.  However, you should "
           "consider supplying the `-display' argument, as an alternative.\n";
  out << "-display [F<fps>|W<fps>]\n";
  if (comprehensive)
    out << "\tThis argument provides an alternative way to consume the "
           "decompressed results.  You may supply either \"-o\" or "
           "\"-display\", but not both.  If this option is selected, the "
           "decompressed imagery will be written to an interleaved ARGB "
           "buffer, with 8 bits per sample, regardless of the original "
           "image precision, or the original number of image components.  "
           "This option is allowed only for the case in which there is "
           "only 1 image component (greyscale), 3 identically "
           "sized image components (colour) or 4 identically sized "
           "image components (colour + alpha).  In the greyscale case, the "
           "RGB channels are all set to the same value.  For more generic "
           "rendering of arbitrary imagery, the \"kdu_show\" application "
           "provides a much more comprehensive solution, but less optimized "
           "for speed.\n"
           "\t   The purpose of the \"-display\" argument here is to show "
           "how the decompressed content can be most efficiently prepared for "
           "display and then blasted to a graphics card, in high performance "
           "applications.  To enable this latter phase, a parameter must "
           "be supplied with the argument, indicating the frame rate <fps> "
           "and whether full-screen (\"F<fps>\") or a windowed (\"W<fps>\") "
           "presentation should be attempted.  Both of these options will "
           "fail if suitable DirectX support is not available.  Note that "
           "frames transferred to the graphics card can only be flipped "
           "into the foreground on the next available frame blanking period, "
           "which limits the maximum frame rate to the display's refresh "
           "rate; moreover, the maximum portion of the decompressed frame "
           "which is transferred to the graphics card is limited by the "
           "available display dimensions, even though the entire frame is "
           "decompressed into an off-screen memory buffer.  When this "
           "happens, you can move the display around the video frame region "
           "with the arrow keys.\n"
           "\t   If you are just interested in measuring the maximum "
           "throughput of the application, you can supply this argument "
           "without the \"F<fps>\" or \"W<fps>\" suffix.\n";
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
           "threads equals the number of physical/virtual CPUs available to "
           "the current process.  Overall, the default policy provides a "
           "reasonable balance between throughput and latency, whose "
           "performance is often close to optimal.  However, it is often "
           "possible to deploy a much larger number of threads to each "
           "processing engine, without any significant throughput penalty, "
           "leading to fewer engines and hence a shorter pipeline with lower "
           "rendering latency.  Also, the default policy cannot access "
           "logical CPUs found in more than one processor group on Windows "
           "platforms.  The following things are worth considering when "
           "constructing different processing environments via this "
           "argument:\n"
           "\t  1) A separate management thread always consumes some "
           "resources to pre-load compressed data for the frame processing "
           "engines and to save the decompressed frame data.  If the "
           "`-display' option is used with an auxiliary parameter, at "
           "least one extra thread is created to manage the display update "
           "process -- in practice, however, DirectX creates some threads of "
           "its own.  On a system with a large number of CPUs, it might "
           "possibly be best to create less frame processing threads than "
           "the number of CPU's so as to ensure timely operation of these "
           "other management and display oriented threads.  However, we "
           "have not observed this to be a significant issue so far.\n"
           "\t  2) As more threads are added to each processing engine, "
           "some inefficiencies are incurred due to occasional blocking "
           "on shared resources; however, these tend to be very small and may "
           "be compensated by the fact that fewer processing engines means "
           "less working memory.\n"
           "\t  3) Although the single threaded processing environment (i.e., "
           "one thread per engine) has minimal overhead, multi-threaded "
           "engines have the potential to better exploit the sharing of L2/L3 "
           "cache memory between close CPUs.  This is expecially likely if "
           "CPU affinity is selected carefully.\n";
  out << "-read_ahead <num frames read ahead by the management thread>\n";
  if (comprehensive)
    out << "\tBy default, the number of frames which can be active at any "
           "given time is set to twice the number of processing engines.  "
           "By \"active\", we mean frames whose compressed contents have "
           "been read, but whose decompressed output has not yet been "
           "consumed by the management thread.  This argument allows you "
           "to specify the number of active frames as E + A, where E is "
           "the number of frame processing engines and A is the read-ahead "
           "value supplied as the argument's parameter.\n";
  out << "-yield_freq <jobs between voluntary worker thread yields>\n";
  if (comprehensive)
    out << "\tThis argument allows you to play around the Kakadu core "
           "multi-threading engine's yielding behaviour.  Worker threads "
           "consider the option of yielding their execution to other "
           "OS threads/tasks periodically, so that these tasks can be done "
           "at convenient points, rather than when the thread is in the "
           "midst of a task on which other threads may depend.  This "
           "argument specifies the yield frequency in terms of the "
           "number of jobs performed between yields.  The significance of "
           "a job is not well defined, but the `kdu_thread_entity' API "
           "exposes methods that an application can use to measure the "
           "rate at which threads are doing jobs and hence derive good "
           "yield frequencies for a given purpose.  The exposure of this "
           "argument here is intended to provide you with an externally "
           "visible way of playing around with this feature to determine "
           "the sensitivity of the overall application to yield patterns.  "
           "The default yielding policy is specified by the "
           "`kdu_thread_entity' API, but a typical value might be 100.  In "
           "some cases, much smaller values may be beneficial.  You can "
           "completely disable voluntary yielding by supplying 0 for this "
           "argument.\n";
  out << "-double_buffering <stripe height>\n";
  if (comprehensive)
    out << "\tThis option is intended to be used in conjunction with "
           "`-engine_threads'.  From Kakadu version 7, double buffering "
           "is activated by default when the number of threads per frame "
           "processing engine exceeds 4, but you can exercise more precise "
           "control over when and how it is used via this argument.  "
           "Supplying 0 causes the feature to be disabled.\n"
           "\t   Without double buffering, DWT operations will all be "
           "performed by the single thread which \"owns\" the multi-threaded "
           "processing group associated with each frame processing engine.  "
           "For small processing thread groups, this may be acceptable or "
           "even optimal, since the DWT is generally quite a bit less CPU "
           "intensive than block decoding (which is always spread across "
           "multiple threads) and synchronous single-threaded DWT operations "
           "may improve memory access locality.  However, even for a small "
           "number of threads, the amount of thread idle time can be reduced "
           "by using the double buffered DWT feature.  In this case, a "
           "certain number of image rows in each image component are actually "
           "double buffered, so that one set can be processed by colour "
           "transformation and data format conversion operations, while the "
           "other set is processed by the DWT synthesis engines, which "
           "themselves depend upon the processing of block decoding jobs.  "
           "The number of rows in each component which are to be double "
           "buffered is known as the \"stripe height\", supplied as a "
           "parameter to this argument.  The stripe height can be as small "
           "as 1, but this may add a lot of thread context switching "
           "overhead.  For this reason, a stripe height in the range 8 to 64 "
           "is recommended.\n"
           "\t   The default policy, selects 0 for frame processing engines "
           "with 4 or less processing threads; otherwise it passes the "
           "special value -1 to the `kdu_multi_synthesis' engine, which "
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
           "decoding job within any subband.  This determines the minimum "
           "number of code-blocks that will be processed together, subject "
           "to other constraints that may exist.  A typical value for this "
           "parameter would be 4096 (one 64x64 block, or four 32x32 blocks).\n"
           "\t   The second parameter specifies the minimum number of "
           "block decoding jobs you would like to be available across a "
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
           "decoding), but other values are occasionally selected if you "
           "have a very large number of processing threads and you may "
           "either want to either prevent this or encourage the use of even "
           "more buffering.\n";
#endif // KDU_SPEEDPACK
  out << "-trunc <block truncation factor>\n";
  if (comprehensive)
    out << "\tYou may use this option to experiment with the framework's "
           "dynamic block truncation feature.  The real-valued parameter is "
           "multiplied by 256 before ultimately passing it to the "
           "`kdu_codestream::set_block_truncation' function, so that "
           "the supplied real-valued parameter can be roughly interpreted "
           "as the number of coding passes to discard.  Fractional values "
           "may cause coding passes to be discarded only from some "
           "code-blocks.  Ultimately, this features allows you to trade "
           "computation time for quality, even when the compressed "
           "source contains only one quality layer.  The internal objects "
           "allow the truncation factor to be changed dynamically, so you "
           "could implement a feedback loop to maintain a target frame "
           "rate for computation-limited applications.  The present "
           "demonstration application does not implement such a feedback "
           "loop, since it would obscure true processing performance.\n";
  out << "-precise -- force float/32-bit processing\n";
  if (comprehensive)
    out << "\tUse this option to force the internal machinery to use the "
           "full 32-bit (float/int) processing path, even if the sample "
           "precision involved suggests that the lower precision 16-bit "
           "processing path should be OK.  The current application "
           "naturally prefers to take the fastest reasonable processing "
           "path, but this option allows you to explore the impact of "
           "maximising accuracy instead.\n";
  out << "-repeat <number of times to cycle through the entire video>\n";
  if (comprehensive)
    out << "\tUse this option to simulate larger video sequences for more "
           "accurate timing information, by looping over the supplied "
           "video source the indicated number of times.\n";
  out << "-reduce <discard levels>\n";
  if (comprehensive)
    out << "\tSet the number of highest resolution levels to be discarded.  "
           "The frame resolution is effectively divided by 2 to the power of "
           "the number of discarded levels.\n";
  out << "-components <max image components to decompress>\n";
  if (comprehensive)
    out << "\tYou can use this argument to limit the number of (leading) "
           "image components which are decompressed.\n";
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
/* STATIC                       parse_arguments                              */
/*****************************************************************************/

static kdu_thread_entity_affinity *
  parse_arguments(kdu_args &args, char * &ifname, char * &ofname,
                  int &discard_levels, int &max_components,
                  int &double_buffering_height,
                  kdu_push_pull_params &pp_params, int &truncation_factor,
                  int &num_engines, int &repeat_factor, int &read_ahead_frames,
                  int &yield_freq, bool &want_display, bool &want_full_screen,
                  float &want_fps, bool &want_precise, bool &quiet)
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
  ofname = NULL;
  want_display = false;
  want_full_screen = false;
  want_fps = -1.0F; // Anything <= 0.0 means no physical display
  discard_levels = 0;
  max_components = 0; // Means no limit
  double_buffering_height = -1; // i.e., automatically select suitable value
  truncation_factor = 0; // No truncation
  repeat_factor = 1;
  yield_freq = -1; // Use the default policy
  want_precise = false;
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

  if (args.find("-display") != NULL)
    {
      want_display = true;
      const char *string = args.advance();
      if ((string != NULL) && (*string == 'F'))
        {
          want_full_screen = true;
          if ((sscanf(string+1,"%f",&want_fps) != 1) || (want_fps <= 0.0F))
            { kdu_error e; e << "The optional parameter to \"-display\" "
              "must contain a positive real-valued frame rate (frames/second) "
              "after the `F' or `W' prefix."; }
          args.advance();
        }
      else if ((string != NULL) && (*string == 'W'))
        {
          if ((sscanf(string+1,"%f",&want_fps) != 1) || (want_fps <= 0.0F))
            { kdu_error e; e << "The optional parameter to \"-display\" "
              "must contain a positive real-valued frame rate (frames/second) "
              "after the `F' or `W' prefix."; }
          args.advance();
        }
    }

  if (want_display && (ofname != NULL))
    { kdu_error e; e << "The \"-o\" and \"-display\" arguments are "
      "mutually exclusive."; }

  if (args.find("-reduce") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&discard_levels) != 1) ||
          (discard_levels < 0))
        { kdu_error e; e << "\"-reduce\" argument requires a non-negative "
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

  if (args.find("-trunc") != NULL)
    { 
      const char *string = args.advance();
      float factor=0.0F;
      if ((string == NULL) || (sscanf(string,"%f",&factor) != 1) ||
          (factor < 0.0F) || (factor > 255.0F))
        { kdu_error e; e << "\"-trunc\" argument requires a non-negative "
          "real-valued parameter, no larger than 255.0!"; }
      args.advance();
      truncation_factor = (int) floor(factor*256.0+0.5);
    }

  if (args.find("-repeat") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&repeat_factor) != 1) ||
          (repeat_factor < 1))
        { kdu_error e; e << "\"-repeat\" argument requires a positive "
          "integer parameter!"; }
      args.advance();
    }

  if (args.find("-precise") != NULL)
    { 
      want_precise = true;
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

  if (args.find("-yield_freq") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&yield_freq) != 1) ||
          (yield_freq < 0))
        { kdu_error e; e << "\"-yield_freq\" argument requires a "
          "non-negative integer parameter!"; }
      args.advance();
    }

  return engine_specs;
}

/*****************************************************************************/
/* STATIC                        open_vix_file                               */
/*****************************************************************************/

static FILE *
  open_vix_file(char *fname, vex_frame_queue *queue,
                kdu_uint32 timescale, int frame_period, bool is_ycc)
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
  fprintf(file,">VIDEO<\n%f 0\n",
          ((double) timescale) / ((double) frame_period));
  fprintf(file,">COLOUR<\n%s\n",(is_ycc)?"YCbCr":"RGB");
  const char *container_string = "char";
  int precision = queue->get_precision();
  if (precision > 8)
    container_string = "word";
  if (precision > 16)
    container_string = "dword";
  int is_big = 1; ((char *)(&is_big))[0] = 0;
  const char *endian_string = (is_big)?"big-endian":"little-endian";
  int components = queue->get_num_components();
  kdu_dims dims = queue->get_frame_dims();
  bool is_signed = queue->get_signed();
  fprintf(file,">IMAGE<\n%s %s %d %s\n",
          ((is_signed)?"signed":"unsigned"),
          container_string,precision,endian_string);
  fprintf(file,"%d %d %d\n",dims.size.x,dims.size.y,components);
  for (int c=0; c < components; c++)
    {
      kdu_coords subs = queue->get_component_subsampling(c);
      fprintf(file,"%d %d\n",subs.x,subs.y);
    }
  return file;
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
  int discard_levels, max_components, truncation_factor;
  int double_buffering_height=-1; // Means auto-select
  int num_engines, repeat_factor, read_ahead_frames, yield_freq=-1;
  kdu_push_pull_params pp_params;
  bool want_display, want_full_screen, want_precise, quiet;
  float want_fps=-1.0F; // <= 0.0 means no physical display
  kdu_thread_entity_affinity *engine_specs = NULL;
  jp2_family_src ultimate_src; // No need for `jp2_threadsafe_family_src' here
                               // because all reading is done in one thread
  mj2_source movie;
  mj2_video_source *mj2_video = NULL;
  jpx_source composit_source;
  vex_jpx_source *jpx_video = NULL;
  kdu_compressed_video_source *video=NULL; // Base for both video source types
  
  vex_frame_queue *queue = NULL;
  vex_engine *engines=NULL;
  FILE *vix_file = NULL;

  vex_display display; // Impotent if `KDU_DX9' is not defined

  int return_code = 0;
  try { 
      kdu_args args(argc,argv,"-s");
      engine_specs =
        parse_arguments(args,ifname,ofname,discard_levels,max_components,
                        double_buffering_height,pp_params,truncation_factor,
                        num_engines,repeat_factor,read_ahead_frames,yield_freq,
                        want_display,want_full_screen,want_fps,want_precise,
                        quiet);
    if (args.show_unrecognized(pretty_cout) != 0)
        { kdu_error e;
          e << "There were unrecognized command line arguments!"; }
      int t, total_engine_threads = 0;
      for (t=0; t < num_engines; t++)
        total_engine_threads += engine_specs[t].get_total_threads();
      
      bool is_ycc = false;
      ultimate_src.open(ifname);
      if (movie.open(&ultimate_src,true) > 0)
        { 
          mj2_video = movie.access_video_track(1);
          if (mj2_video == NULL)
            { kdu_error e; e << "Motion JPEG2000 data source contains "
              "no video tracks."; }
          is_ycc = mj2_video->access_colour().is_opponent_space();
          video = mj2_video;
        }
      else if (composit_source.open(&ultimate_src,true) > 0)
        { 
          jpx_composition composition = composit_source.access_composition();
          jpx_container_source container = composit_source.access_container(0);
          if (container.exists())
            composition = container.access_presentation_track(1);
          jx_frame *frm = NULL;
          if ((!composition) ||
              ((frm = composition.get_next_frame(NULL)) == NULL))
            { kdu_error e; e << "Supplied JPX input file does not appear to "
              "have a suitable animation frame from which to start "
              "decompressing.  This application expects to start from the "
              "first frame defined by the first JPX container, or the first "
              "top-level animation frame if there are no JPX containers."; }
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
          jpx_video = new vex_jpx_source(&composit_source,1,frame);
          video = jpx_video;
        }
      else
        { kdu_error e; e << "Input file does not appear to be compatible "
          "with the MJ2 or JPX file type specifications."; }

      queue = new vex_frame_queue;
      int max_active_frames = num_engines + read_ahead_frames;
      if (!queue->init(video,discard_levels,max_components,
                       repeat_factor,max_active_frames,want_display))
        { kdu_error e; e << "Video track contains no frames!"; }

      queue->set_truncation_factor(truncation_factor); // You can call this
                  // at any time in applications requiring dynamic tradeoff
                  // between computation speed and quality.

      kdu_uint32 timescale = video->get_timescale();
      int frame_period = (int) video->get_frame_period();

      vex_frame_memory_allocator *frame_memory_allocator = queue;
      if (ofname != NULL)
        vix_file = open_vix_file(ofname,queue,timescale,frame_period,is_ycc);
      else if (want_display && (want_fps > 0.0F))
        {
          int total_frame_buffers = max_active_frames + 4;
          const char *result = display.init(queue->get_component_dims(0).size,
                                            want_full_screen,want_fps,
                                            total_frame_buffers);
          if (result == NULL)
            {
              pretty_cout << "   Use arrows to pan; hit any other key to "
                             "terminate ...\n";
              pretty_cout.flush(false);
              frame_memory_allocator = &display;
            }
          else
            { kdu_error e; e << result; }
        }

      int thread_concurrency = kdu_get_num_processors();
      if (thread_concurrency < total_engine_threads)
        thread_concurrency = total_engine_threads;
      engines = new vex_engine[num_engines];
      for (t=0; t < num_engines; t++)
        engines[t].startup(queue,t,engine_specs[t],thread_concurrency,
                           yield_freq,double_buffering_height,want_precise,
                           &pp_params);

      kdu_clock timer;
      vex_frame *frame;
      int num_processed_frames = 0;
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
      while ((frame =
              queue->get_processed_frame(frame_memory_allocator)) != NULL)
        {
          num_processed_frames++;
          if (vix_file != NULL)
            {
              if (fwrite(frame->buffer,1,(size_t)(frame->frame_bytes),
                         vix_file) != (size_t)(frame->frame_bytes))
                { kdu_error e; e << "Unable to write to output VIX file.  "
                  "device may be full."; }
            }
          if (display.exists() && !display.push_frame(frame))
            break;
          queue->recycle_processed_frame(frame);
          if (!quiet)
            pretty_cout << "Number of frames processed = "
                        << num_processed_frames << "\n";
        }

      double cpu_seconds = timer.get_ellapsed_seconds();
      pretty_cout << "Processed a total of\n\t"
             << num_processed_frames << " frames, using\n\t"
             << num_engines << " frame processing engines, with\n\t"
             << total_engine_threads << " frame processing threads, in\n\t"
             << cpu_seconds << " seconds = "
             << num_processed_frames/cpu_seconds << " frames/second.\n";
    }
  catch (kdu_exception) {
      return_code = 1;
    }
  catch (std::bad_alloc &) {
      pretty_cerr << "Memory allocation failure detected!\n";
      return_code = 2;
    }

  // Cleanup
  if (display.exists() && want_full_screen && (engines != NULL))
    { // Be extra careful to avoid the risk of deadlocks during
      // premature (user-kill) termination, by gracefully shutting
      // down the engines.
      for (int e=0; e < num_engines; e++)
        engines[e].shutdown(true);
    }
  if (ifname != NULL)
    delete[] ifname;
  if (ofname != NULL)
    delete[] ofname;
  if (engine_specs != NULL)
    delete[] engine_specs;
  if (vix_file != NULL)
    fclose(vix_file);
  if (engines != NULL)
    delete[] engines;  // Must delete engines before the queue
  if (queue != NULL)
    delete queue;
  if (video != NULL)
    video->close();
  if (jpx_video != NULL)
    delete jpx_video;
  movie.close();  // Does nothing if not MJ2
  composit_source.close(); // Does nothing if not JPX
  ultimate_src.close();
  return return_code;
}
