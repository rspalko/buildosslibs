/*****************************************************************************/
// File: kdu_stripe_expand.cpp [scope = APPS/BUFFERED_EXPAND]
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
   A Kakadu demo application, demonstrating use of the powerful
`kdu_stripe_decompressor' object.
******************************************************************************/

#include <stdio.h>
#include <iostream>
#include <assert.h>
// Kakadu core includes
#include "kdu_arch.h"
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_stripe_decompressor.h"
// Application includes
#include "kdu_args.h"
#include "kdu_file_io.h"
#include "jp2.h"

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

/*****************************************************************************/
/* STATIC                       to_little_endian                             */
/*****************************************************************************/

static void
  to_little_endian(kdu_int32 * words, int num_words)
  /* Used to convert the BMP header structure to a little-endian word
     organization for platforms which use the big-endian convention. */
{
  kdu_int32 test = 1;
  kdu_byte *first_byte = (kdu_byte *) &test;
  if (*first_byte)
    return; // Machine uses little-endian architecture already.
  kdu_int32 tmp;
  for (; num_words--; words++)
    { 
      tmp = *words;
      *words = ((tmp >> 24) & 0x000000FF) +
      ((tmp >> 8)  & 0x0000FF00) +
      ((tmp << 8)  & 0x00FF0000) +
      ((tmp << 24) & 0xFF000000);
    }
}

/*****************************************************************************/
/*                               bmp_header                                  */
/*****************************************************************************/

struct bmp_header {
  kdu_uint32 size; // Size of this structure: must be 40
  kdu_int32 width; // Image width
  kdu_int32 height; // Image height; -ve means top to bottom.
  kdu_uint32 planes_bits; // Planes in 16 LSB's (must be 1); bits in 16 MSB's
  kdu_uint32 compression; // Only accept 0 here (uncompressed RGB data)
  kdu_uint32 image_size; // Can be 0
  kdu_int32 xpels_per_metre; // We ignore these
  kdu_int32 ypels_per_metre; // We ignore these
  kdu_uint32 num_colours_used; // Entries in colour table; 0 = use default
  kdu_uint32 num_colours_important; // 0 means all colours are important.
  };
  /* Notes:
        This header structure must be preceded by a 14 byte field, whose
     first 2 bytes contain the string, "BM", and whose next 4 bytes contain
     the length of the entire file.  The next 4 bytes must be 0. The final
     4 bytes provides an offset from the start of the file to the first byte
     of image sample data.
        If the bit_count is 1, 4 or 8, the structure must be followed by
     a colour lookup table, with 4 bytes per entry, the first 3 of which
     identify the blue, green and red intensities, respectively. */


/* ========================================================================= */
/*                               kd_output_file                              */
/* ========================================================================= */

struct kd_output_file {
  public: // Lifecycle functions
    kd_output_file()
      { fname=NULL; fp=NULL; first_comp_idx=lim_comp_idx=0;
        samples_per_pel=1; bytes_per_sample=1; precision=8;
        is_signed=is_raw=is_bmp=swap_bytes=false;
        buf_row_gap=0; buf16=NULL; buf8=NULL;
        alt_buf16=NULL; alt_buf8=NULL; next=NULL; }
    ~kd_output_file()
      { 
        if (fname != NULL) delete[] fname;
        if (fp != NULL) fclose(fp);
      }
    void swap_buffers()
      { // Swaps `buf8' with `alt_buf8' and `buf16' with `alt_buf16' for
        // double-buffered file writing with `expand_multi_threaded_io'.
        assert(alt_buf16 || alt_buf8);
        kdu_byte *tmp8=buf8; buf8=alt_buf8; alt_buf8=tmp8;
        kdu_int16 *tmp16=buf16; buf16=alt_buf16; alt_buf16=tmp16;
      }
    void write_header();
      /* Writes the PGM, PPM or BMP header, as appropriate. */
    void write_stripe(int height);
      /* Writes from the internal `buf8' or `buf16' array, as appropriate.
            Note: this function does not computation outside of the kernel
         so long as all files have the same number of bytes per sample (1 or 2)
         and data with multi-byte samples is written in native word order so
         that `swap_bytes' is false.  Otherwise, this function has to do
         some conversions which it does in a simplistic sample-by-sample
         fashion that could become a bottleneck for overall throughput
         on systems with a large number of CPU's.
            If you are developing your own application, based on this demo,
         the lesson is that you should buffer your data in the most natural
         format (i.e. as small as possible) and do as few (if any)
         transformations of the data yourself, letting the
         `kdu_stripe_decompressor::pull_stripe' function handle all
         required transformations. */
  public: // Data
    char *fname;
    FILE *fp;
    int first_comp_idx; // First component index consumed by this file
    int lim_comp_idx; // Last component index consumed, plus 1
    int samples_per_pel; // lim_comp-first_comp, or 1 more if padding to 32bpp
    int bytes_per_sample;
    int precision; // Num bits
    bool is_signed;
    bool is_raw;
    bool is_bmp;
    bool swap_bytes; // If raw file word order differs from machine word order
    kdu_coords size; // Width, and remaining rows
    int buf_row_gap; // Measured in samples
    kdu_int16 *buf16; // Non-NULL if any files require 16-bit precision
    kdu_byte *buf8; // Non-NULL if all files have 8-bit precision
    kdu_int16 *alt_buf16; // These are NULL unless we are doing double-buffered
    kdu_byte *alt_buf8;  // stripe reading (see `expand_multi_threaded_io').
    kd_output_file *next;
  };
  /* Note: `buf16' and `buf8' point to resources that are not owned by
     this object; they must be deallocated externally.  The same is true
     for `alt_buf16' and `alt_buf8', if non-NULL. */

/*****************************************************************************/
/*                         kd_output_file::write_header                      */
/*****************************************************************************/

void
  kd_output_file::write_header()
{
  if (is_bmp)
    { 
      kdu_byte magic[14];
      bmp_header header;
      int header_bytes = 14+sizeof(header);
      assert(header_bytes == 54);
      if (samples_per_pel == 1)
        header_bytes += 1024; // Need colour LUT
      else
        assert((samples_per_pel == 3) || (samples_per_pel == 4));
      int file_bytes = buf_row_gap*size.y + header_bytes;
      magic[0] = 'B'; magic[1] = 'M';
      magic[2] = (kdu_byte) file_bytes;
      magic[3] = (kdu_byte)(file_bytes>>8);
      magic[4] = (kdu_byte)(file_bytes>>16);
      magic[5] = (kdu_byte)(file_bytes>>24);
      magic[6] = magic[7] = magic[8] = magic[9] = 0;
      magic[10] = (kdu_byte) header_bytes;
      magic[11] = (kdu_byte)(header_bytes>>8);
      magic[12] = (kdu_byte)(header_bytes>>16);
      magic[13] = (kdu_byte)(header_bytes>>24);
      header.size = 40;
      header.width = size.x;
      header.height = size.y;
      header.planes_bits = 1; // Set `planes'=1 (mandatory)
      header.planes_bits |= ((samples_per_pel*8) << 16);
      header.compression = 0;
      header.image_size = 0;
      header.xpels_per_metre = header.ypels_per_metre = 0;
      header.num_colours_used = header.num_colours_important = 0;
      to_little_endian((kdu_int32 *) &header,10);
      fwrite(magic,1,14,fp);
      fwrite(&header,1,40,fp);
      if (samples_per_pel == 1)
        { // Write colour LUT
          for (int n=0; n < 256; n++)
            { fputc(n,fp); fputc(n,fp); fputc(n,fp); fputc(0,fp); }
        }
    }
  else if (!is_raw)
    { 
      if (samples_per_pel == 1)
        fprintf(fp,"P5\n%d %d\n255\n",size.x,size.y);
      else if (samples_per_pel == 3)
        fprintf(fp,"P6\n%d %d\n255\n",size.x,size.y);
      else
        assert(0);
    }
}

/*****************************************************************************/
/*                         kd_output_file::write_stripe                      */
/*****************************************************************************/

void
  kd_output_file::write_stripe(int height)
{
  int n, num_samples = height*buf_row_gap;
  int num_bytes = num_samples * bytes_per_sample;
  if (num_samples <= 0) return;
  void *buf = buf8;
  if (buf == NULL)
    buf = buf16;
  if ((buf16 != NULL) && (bytes_per_sample == 1))
    { // Reduce to an 8-bit representation
      kdu_int16 *sp = buf16;
      kdu_byte *dp=(kdu_byte *) buf;
      for (n=num_samples; n > 0; n--)
        *(dp++) = (kdu_byte) *(sp++);
    }
  else if ((buf16 != NULL) && swap_bytes)
    { // Swap byte order
      kdu_int16 *sp = buf16;
      for (n=num_samples; n > 0; n--)
        { kdu_int16 val=*sp; val=(val<<8)+((val>>8)&0x00FF); *(sp++)=val; }
    }
  int written_bytes = (int) fwrite(buf,1,num_bytes,fp);
  if (written_bytes != num_bytes)
    { kdu_error e; e << "Unable to write to file \"" << fname << "\"."; }
  size.y -= height;
  assert(size.y >= 0);
}


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
  out << "This is Kakadu's \"kdu_buffered_expand\" demo application.\n";
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
  out << "-i <compressed file>\n";
  if (comprehensive)
    out << "\tCurrently accepts raw code-stream files and code-streams "
           "wrapped in any JP2 compatible file format.  The file signature "
           "is used to check whether or not the file is a raw codestream, "
           "rather than relying upon a file name suffix.\n";
  out << "-o <PGM/PPM/BMP/raw file 1>[,<PGM/PPM/BMP/raw file 2>[,...]]\n";
  if (comprehensive)
    out << "\tIf you omit this argument, all image components will be fully "
           "decompressed into memory stripe buffers internally, but only "
           "the final file writing phase will be omitted.  This is very "
           "useful if you want to evaluate the decompression speed (use "
           "`-cpu') without having the results confounded by file writing -- "
           "for large images, disk I/O accounts for most of the processing "
           "time on modern CPU's.\n"
           "\t   The argument takes one or more output image files.  To "
           "simplify this demo application, while still allowing the "
           "`kdu_stripe_decompressor' interface to be adequately tested "
           "and demonstrated, only the following output file formats are "
           "currently supported: PGM (1 component, 8bits); PPM (3 components, "
           "8bits); BMP (1, 3 or 4 components, 8bits); RAW (1 component, "
           "up to 16bits/sample).  Multiple files may be supplied, to handle "
           "extra image components, but BMP files may not be mixed with the "
           "other types, because BMP files require the decompression process "
           "to proceed in bottom-up, rather than top-down fashion.  Where "
           "BMP files are used, each file soaks up as many image components "
           "as possible (1, 3 or 4), but this policy may be overridden via "
           "the `-bmp' argument.\n"
           "\t   As in the \"kdu_expand\" application, the sample bits in "
           "a raw file are written to the least significant bit positions "
           "of an 8 or 16 bit word, depending on the bit-depth.  For signed "
           "data, the word is sign extended.  The default word organization "
           "is big-endian, regardless of your machine architecture, but this "
           "application allows you to explicitly nominate a different "
           "byte order, via the `-little_endian' argument.  Moreover, the "
           "process is most efficient if the word organization corresponds "
           "to the machine's native order.  Unlike the \"kdu_expand\" "
           "demo app, this one does not take note of the suffix used for "
           "raw files, but we recommend a suffix of \".raw\" for "
           "big-endian and \".rawl\" for little-endian formats.\n";
  out << "-bmp <num components>\n";
  if (comprehensive)
    out << "\tThis argument may be used to adjust the number of image "
           "components that we prefer to record in each BMP file (if there "
           "are any).  The argument accepts one integer parameter that must "
           "be equal to 1, 3 or 4.  The number of image components stored "
           "in a final BMP file may be adjusted downwards from this value "
           "to 1 or 3, if necessary.  Also, if the preferred value is 4, "
           "and there are only 3 image components available to be written to "
           "a final BMP file, the missing (alpha) channel will be set to "
           "255.\n";
  out << "-rate <bits per pixel>\n";
  if (comprehensive)
    out << "\tMaximum bit-rate, expressed in terms of the ratio between the "
           "total number of compressed bits (including headers) and the "
           "product of the largest horizontal and  vertical image component "
           "dimensions. Note that we use the original dimensions of the "
           "compressed image, regardless or resolution scaling and regions "
           "of interest.  Note CAREFULLY that the file is simply truncated "
           "to the indicated limit, so that the effect of the limit will "
           "depend strongly upon the packet sequencing order used by the "
           "code-stream.  The effect of the byte limit may be modified by "
           "supplying the `-simulate_parsing' flag, described below.\n";
  out << "-simulate_parsing\n";
  if (comprehensive)
    out << "\tIf this flag is supplied, discarded resolutions, image "
           "components or quality layers (see `-reduce' and `-layers') will "
           "not be counted when applying any rate limit supplied via "
           "`-rate' and when reporting overall bit-rates.  The effect is "
           "intended to be the same as if the code-stream were first "
           "parsed to remove the resolutions, components or quality layers "
           "which are not being used.\n";
  out << "-skip_components <num initial image components to skip>\n";
  if (comprehensive)
    out << "\tSkips over one or more initial image components, reconstructing "
           "as many remaining image components as can be stored in the "
           "output image file(s) specified with \"-o\" (or all remaining "
           "components, if no \"-o\" argument is supplied).\n";
  out << "-layers <max layers to decode>\n";
  if (comprehensive)
    out << "\tSet an upper bound on the number of quality layers to actually "
           "decode.\n";
  out << "-reduce <discard levels>\n";
  if (comprehensive)
    out << "\tSet the number of highest resolution levels to be discarded.  "
           "The image resolution is effectively divided by 2 to the power of "
           "the number of discarded levels.\n";
  out << "-int_region {<top>,<left>},{<height>,<width>}\n";
  if (comprehensive)
    out << "\tEstablish a region of interest within the original compressed "
           "image.  Only the region of interest will be decompressed and the "
           "output image dimensions will be modified accordingly.  The "
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
  out << "-min_height <preferred minimum stripe height>\n";
  if (comprehensive)
    out << "\tAllows you to control the processing stripe height which is "
           "preferred in the event that the image is not tiled.  If the image "
           "is tiled, the preferred stripe height is the height of a tile, so "
           "that partially processed tiles need not be buffered.  Otherwise, "
           "the stripes used for incremental processing of the image data "
           "may be as small as 1 line, but it is usually preferable to use "
           "a larger value, as specified here, so as to avoid switching back "
           "and forth between file reading and compression too frequently.  "
           "The default value, for this parameter is 16.  Play around with "
           "this parameter, together with \"-foreground_io\", if you want "
           "to get the best processing performance.\n"
           "\t   Note that the processing stripe height also determines the "
           "granularity with which any output files are written -- larger "
           "values may therefore lead to I/O performance improvements, "
           "especially in multi-threaded environments, where file writing is "
           "performed in a background thread unless \"-foreground_io\" "
           "is specified.\n";
  out << "-max_height <maximum stripe height>\n";
  if (comprehensive)
    out << "\tRegardless of the desire to process in stripes whose height is "
           "equal to the tile height, wherever the image is horizontally "
           "tiled, this argument provides an upper bound on the maximum "
           "stripe height.  If the tile height exceeds this value, "
           "an entire row of tiles will be kept open for processing.  This "
           "avoids excessive memory consumption.  This argument allows you "
           "to control the trade-off between stripe buffering and "
           "tile decompression engine memory.  The default limit is 1024.\n";
  out << "-s <switch file>\n";
  if (comprehensive)
    out << "\tSwitch to reading arguments from a file.  In the file, argument "
           "strings are separated by whitespace characters, including spaces, "
           "tabs and new-line characters.  Comments may be included by "
           "introducing a `#' or a `%' character, either of which causes "
           "the remainder of the line to be discarded.  Any number of "
           "\"-s\" argument switch commands may be included on the command "
           "line.\n";
  out << "-little_endian -- use little-endian byte order with raw files\n";
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
           "\t   If the `-num_threads' argument is not supplied explicitly, "
           "the default behaviour is to create a threading environment only "
           "if the system offers multiple CPU's (or virtual CPU's), with "
           "one thread per CPU.  However, this default behaviour depends "
           "upon knowledge of the number of CPU's which are available -- "
           "something which cannot always be accurately determined through "
           "system calls.  The default value might also not yield the "
           "best possible throughput.\n";
  out << "-precise -- forces the use of 32-bit representations.\n";
  if (comprehensive)
    out << "\tBy default, 16-bit data representations will be employed for "
           "internal sample data processing operations whenever the image "
           "component bit-depths are sufficiently small.  This option "
           "forces the use of 32-bit representations, which is of greatest "
           "interest for irreversible processing (`Creversible' is not true), "
           "in which case the added precision afforded by floating point "
           "calculations can reduce numerical errors significantly when the "
           "compressed bit-rate is high and there are a large number of DWT "
           "(resolution) levels (`Clevels').\n";
  out << "-fastest -- use of 16-bit data processing as often as possible.\n";
  if (comprehensive)
    out << "\tThis argument causes sample processing to use a 16-bit "
           "fixed-point representation if possible, even if the numerical "
           "approximation errors associated with this representation "
           "would normally be considered excessive -- makes no difference "
           "unless the original bit-depths recorded in the compressed "
           "codestream headers are around 13 bits or more (depending "
           "on other coding conditions) or if \"-precise\" is specified.\n";
  out << "-double_buffering <num double buffered rows, D>\n";
  if (comprehensive)
    out << "\tThis option may be used only in conjunction with a non-zero "
           "`-num_threads' value.  From Kakadu version 7, double buffering "
           "is activated by default in multi-threaded processing "
           "environments, but you can disable it by supplying 0 "
           "to this argument.\n"
           "\t   Without double buffering, DWT operations will all be "
           "performed by the single thread which \"owns\" the multi-threaded "
           "processing group.  For a small number of processors, this may be "
           "acceptable, or even optimal, since the DWT is generally quite a "
           "bit less CPU intensive than block decoding (which is always "
           "spread across multiple threads, if available) and synchronous "
           "single threaded DWT operations can improve memory access "
           "locality.  However, even for a small number of threads, the "
           "amount of thread idle time can be reduced by specifying the "
           "`-double_buffering' option.  In this case, a certain number "
           "of image rows in each image component are actually double "
           "buffered, so that one set can be processed by colour "
           "transformation and sample writing operations, while the other "
           "set is generated by the DWT synthesis engines, which themselves "
           "feed off the block decoding engines.  The number of rows in "
           "each component which are to be double buffered is the value D "
           "supplied here, so the total number of rows in the double "
           "buffering system is actually 2D.  Internally, these 2D rows "
           "may actually get partitioned differently, for triple or even "
           "quadruple buffering, but no more than 2D row buffers will be "
           "used.\n"
           "\t   The value of D can be as small as 1, but this may add a lot "
           "of thread context switching overhead.  A reasonable value is "
           "typically in the range 20 to 50, but there are some cases "
           "(notably where the codestream contains many small tiles and the "
           "stripes retrieved via `kdu_stripe_decompressor' are large enough "
           "to allow tiles to be processed one by one) where it is best for "
           "2D to be equal to the tile height.  In most cases, you are best "
           "off omitting this argument so that the internal machinery "
           "can make its own decisions; in fact, for tiled imagery, the "
           "value of D might be set differently for each tile if you "
           "defer to the default policy.\n";
  out << "-tile_concurrency <num tiles>\n";
  if (comprehensive)
    out << "\tThis option is of interest when decompressing codestreams with "
           "many small tiles.  In this case, the system will attempt to "
           "generate imagery in stripes whose height equals one tile height, "
           "passing these stripes to `kdu_stripe_decompressor::pull_stripe', "
           "unless the \"-max_height\" value is too small to allow this.  "
           "This allows the `kdu_stripe_decompressor' object to open tiles "
           "one by one, pulling out the relevant data and moving on.  With "
           "multi-threaded processing, it can be difficult to keep all "
           "threads concurrently active if the tiles are small and each "
           "tile must be fully processed before the next can be opened, so "
           "instead the system creates a number of multi-threaded tile "
           "processing engines in advance.  The number of concurrently open "
           "tile processing engines can be customised using this argument, "
           "but will not exceed 1 + the number of tiles that span "
           "the image width.\n"
           "\t   This argument does not have any effect if you are not "
           "using a multi-threaded processing environment (see "
           "\"-num_threads\").  If \"-max_height\" is too small to "
           "allow a whole tile rows of image samples to be retrieved through "
           "each call to `kdu_stripe_decompressor::pull_stripe', all that "
           "matters is whether or not this argument is equal to 1.  In "
           "general the value 1 means \"one tile at a time\" but in the "
           "case where the stripe height is too small, the internal machinery "
           "has no choice but to keep an entire row of tile processing "
           "engines active concurrently, so the value 1 is interpreted as "
           "\"one row of tiles at a time\".  In this case, each row of tile "
           "processing engines will be created only at the point when it is "
           "needed to fill an image stripe buffer.  Otherwise, the internal "
           "machinery creates a future row of tile processing engines also, "
           "so that there should usually be sufficient available jobs for the "
           "multi-threaded processing system to keep all threads active all "
           "time -- this is the default behaviour.\n"
           "\t   For maximum processing throughput on systems with "
           "a large number of CPU cores or hardware threads, it is best "
           "if the \"-max_height\" argument is large enough to accommodate "
           "the use of stripes that correspond to whole tile rows and "
           "then the \"-double_buffering\" value should ideally be large "
           "enough to allow the multi-threaded DWT buffers associated with "
           "each tile to hold the entire tile's data; this means that "
           "\"-double_buffering\" should be at least half the tile height.  "
           "Of course, these parameters would not be appropriate if tiles "
           "are very large or the image has only one tile.  While it is "
           "possible to manually select all these parameters, you are "
           "usually best off omitting both \"-max_height\" and "
           "\"-double_buffering\" arguments, letting the internal machinery "
           "select suitable default values, possibly in a dynamic manner.\n"
           "\t   The default value for \"-tile_concurrency\" is 0, which "
           "lets the `kdu_stripe_decompressor' object select good levels of "
           "tile concurrency automatically.  This may well produce the best "
           "throughput, but you should be prepared to play around to find "
           "the best parameter for a particular platform and compression "
           "configuration.\n";
  out << "-foreground_io -- write image files in primary thread\n";
  if (comprehensive)
    out << "\tBy default, in multi-threaded applications, whenever the \"-o\" "
           "argument is used to specify output files, these files are written "
           "in a separate background thread so that the primary thread can "
           "devote all its effort to pulling imagery from the "
           "`kdu_stripe_decompressor::pull_stripe' function.  Use "
           "this argument to revert to pulling and writing image stripes "
           "sequentially rather than concurrently.  The only reason you might "
           "want to do this would be to reduce memory usage, since background "
           "writing of imagery requires two sets of stripe buffers.\n";
  out << "-cpu -- report processing CPU time\n";
  if (comprehensive)
    out << "\tFor results which more closely reflect the actual decompression "
           "processing time, do not specify any output files via the `-o' "
           "option.  The image is still fully decompressed into memory "
           "buffers, but the final phase of writing the contents of these "
           "buffers to disk files is skipped.  This can have a huge impact "
           "on timing, depending on your platform, and many applications "
           "do not need to write the results to disk.\n";
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

static kd_output_file *
  parse_simple_args(kdu_args &args, char * &ifname, bool &prefer_bmp4,
                    float &max_bpp, bool &simulate_parsing,
                    int &skip_components, int &max_layers, int &discard_levels,
                    kdu_dims &region, int &preferred_min_stripe_height,
                    int &absolute_max_stripe_height, bool &force_precise,
                    bool &want_fastest, int &num_threads,
                    int &double_buffering_height, int &tile_concurrency,
                    bool &multi_threaded_io, bool &cpu)
  /* Parses all command line arguments whose names include a dash.  Returns
     a list of open output files.  `prefer_bmp4' is set only if the `-bmp'
     argument appears with a value of 4 and if the output file list contains
     BMP files.
        Note that `num_threads' is set to 0 if no multi-threaded processing
     group is to be created, as distinct from a value of 1, which means
     that a multi-threaded processing group is to be used, but this group
     will involve only one thread. */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();      

  ifname = NULL;
  prefer_bmp4 = false;
  max_bpp = -1.0F;
  simulate_parsing = false;
  skip_components = 0;
  max_layers = 0;
  discard_levels = 0;
  region.size = region.pos = kdu_coords(0,0);
  preferred_min_stripe_height = 16;
  absolute_max_stripe_height = 1024;
  force_precise = want_fastest = false;
  num_threads = 0; // This is not actually the default -- see below.
  double_buffering_height = -1; // Select autoamtic policy
  tile_concurrency = -1; // Select automatic policy
  multi_threaded_io = true;
  cpu = false;
  bool little_endian = false;
  kd_output_file *fhead=NULL, *ftail=NULL;

  if (args.find("-i") != NULL)
    {
      const char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-i\" argument requires a file name!"; }
      ifname = new char[strlen(string)+1];
      strcpy(ifname,string);
      args.advance();
    }
  else
    { kdu_error e; e << "You must supply an input file name."; }

  if (args.find("-rate") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%f",&max_bpp) != 1) ||
          (max_bpp <= 0.0F))
        { kdu_error e; e << "\"-rate\" argument requires a positive "
          "real-valued parameter."; }
      args.advance();
    }
  if (args.find("-simulate_parsing") != NULL)
    {
      simulate_parsing = true;
      args.advance();
    }
  if (args.find("-skip_components") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&skip_components) != 1) ||
          (skip_components < 0))
        { kdu_error e; e << "\"-skip_components\" argument requires a "
          "non-negative integer parameter!"; }
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
  if (args.find("-little_endian") != NULL)
    {
      little_endian = true;
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
      if (num_threads == 0)
        { kdu_error e; e << "\"-double_buffering\" may only be used with "
          "a non-zero `-num_threads' value."; }
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
  
  if (args.find("-tile_concurrency") != NULL)
    { 
      if (num_threads == 0)
        { kdu_warning w; w << "\"-tile_concurrency\" argument is irrelevant "
          "in single-threaded processing mode (use \"-num_threads\" to "
          "force the use of a multi-threaded processing argument)."; }
      char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d",&tile_concurrency) != 1) ||
          (tile_concurrency < 0))
        { kdu_error e; e << "\"-tile_concurrency\" argument requires a "
          "non-negative integer parameter."; }
      args.advance();
    }
  
  if (args.find("-foreground_io") != NULL)
    { 
      multi_threaded_io = false;
      args.advance();
    }

  if (args.find("-cpu") != NULL)
    {
      cpu = true;
      args.advance();
    }
  
  if (args.find("-min_height") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d",&preferred_min_stripe_height) != 1) ||
          (preferred_min_stripe_height < 1))
        { kdu_error e; e << "\"-min_height\" argument requires a positive "
          "integer parameter."; }
      args.advance();
    }
  
  if (args.find("-max_height") != NULL)
    {
      const char *string = args.advance();
      if ((string == NULL) ||
          (sscanf(string,"%d",&absolute_max_stripe_height) != 1) ||
          (absolute_max_stripe_height < preferred_min_stripe_height))
        { kdu_error e; e << "\"-max_height\" argument requires a positive "
          "integer parameter, no smaller than the value associated with the "
          "`-min_height' argument."; }
      args.advance();
    }
  if (preferred_min_stripe_height > absolute_max_stripe_height)
    absolute_max_stripe_height = preferred_min_stripe_height;
  
  if (args.find("-fastest") != NULL)
    { 
      args.advance();
      want_fastest = true;
    }
  
  if (args.find("-precise") != NULL)
    { 
      args.advance();
      force_precise = true;
    }

  int bmp_comps = 4; // Max value
  if (args.find("-bmp") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&bmp_comps) != 1) ||
          (bmp_comps < 1) || (bmp_comps > 4) || (bmp_comps == 2))
        { kdu_error e; e << "\"-bmp\" argument requires an integer "
          "number of components to write to each BMP file, possible values "
          "for which are 1, 3 and 4 only."; }
      if (bmp_comps == 4)
        prefer_bmp4 = true;
      args.advance();
    }
  if (args.find("-o") != NULL)
    { 
      const char *delim, *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-o\" argument requires a parameter string."; }
      for (; *string != '\0'; string=delim)
        {
          while (*string == ',') string++;
          for (delim=string; (*delim != '\0') && (*delim != ','); delim++);
          kd_output_file *file = new kd_output_file;
          if (ftail == NULL)
            fhead = ftail = file;
          else
            ftail = ftail->next = file;
          file->fname = new char[(delim-string)+1];
          strncpy(file->fname,string,delim-string);
          file->fname[delim-string] = '\0';
          if ((file->fp = fopen(file->fname,"wb")) == NULL)
            { kdu_error e;
              e << "Unable to open output file, \"" << file->fname << "\"."; }
        }
      args.advance();
    }

  // Go through file list, setting `is_raw', `is_bmp', `swap_bytes' and
  // component indices.
  int num_comps=0;
  bool have_bmp=false, have_non_bmp=false;
  for (ftail=fhead; ftail != NULL; ftail=ftail->next)
    { 
      ftail->first_comp_idx = num_comps;
      ftail->lim_comp_idx = num_comps+1; // Until proven otherwise
      ftail->is_bmp = ftail->swap_bytes = false;
      const char *delim = strrchr(ftail->fname,'.');
      ftail->is_raw = true; // Until proven otherwise
      if ((delim != NULL) && (toupper(delim[1]) == (int) 'B') &&
          (toupper(delim[2]) == (int) 'M') && (toupper(delim[3]) == (int) 'P'))
        { 
          ftail->is_raw = false;
          ftail->is_bmp = true;
          ftail->lim_comp_idx = num_comps+bmp_comps;
          have_bmp = true;
        }
      else if ((delim != NULL) && (toupper(delim[1]) == (int) 'P'))
        { 
          have_non_bmp = true;
          if ((toupper(delim[2]) == (int) 'G') &&
              (toupper(delim[3]) == (int) 'M'))
            ftail->is_raw = false;
          if ((toupper(delim[2]) == (int) 'P') &&
              (toupper(delim[3]) == (int) 'M'))
            { 
              ftail->is_raw = false;
              ftail->lim_comp_idx = num_comps+3;
            }
        }
      if (ftail->is_raw)
        { 
          have_non_bmp = true;
          int is_bigendian=1;
          ((kdu_byte *) &is_bigendian)[0] = 0;
          if (is_bigendian)
            ftail->swap_bytes = little_endian;
          else
            ftail->swap_bytes = !little_endian;
        }
      ftail->samples_per_pel = ftail->lim_comp_idx - ftail->first_comp_idx;
      num_comps = ftail->lim_comp_idx;
    }
  if (!have_bmp)
    prefer_bmp4 = false;
  if (have_bmp && have_non_bmp)
    { kdu_error e; e << "Either all of the output files supplied to \"-o\" "
      "must be BMP files (suffix ending in \".bmp\") or else none of them "
      "may be BMP files."; }
  return fhead;
}

/*****************************************************************************/
/* STATIC                     check_jp2_family_file                          */
/*****************************************************************************/

static bool
  check_jp2_family_file(const char *fname)
  /* This function opens the file and checks its first box, to see if it
     contains the JP2-family signature.  It then closes the file and returns
     the result.  This should avoid some confusion associated with files
     whose suffix has been unreliably names. */
{
  jp2_family_src src;
  jp2_input_box box;
  src.open(fname);
  bool result = box.open(&src) && (box.get_box_type() == jp2_signature_4cc);
  src.close();
  return result;
}

/*****************************************************************************/
/* STATIC                        get_bpp_dims                                */
/*****************************************************************************/

static kdu_long
  get_bpp_dims(siz_params *siz)
  /* Figures out the number of "pixels" in the image, for the purpose of
     converting byte counts into bits per pixel, or vice-versa. */
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


/* ========================================================================= */
/*                   Decompression Workflow Demonstrations                   */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                   expand_single_threaded                           */
/*****************************************************************************/

static void
  expand_single_threaded(kdu_stripe_decompressor &decompressor,
                         int preferred_min_stripe_height,
                         int absolute_max_stripe_height,
                         int stripe_heights[], int max_stripe_heights[],
                         int sample_gaps[], int row_gaps[],
                         int precisions[], bool is_signed[], int pad_flags[],
                         kdu_byte *stripe_bufs8[], kdu_int16 *stripe_bufs16[],
                         kd_output_file *out_files, kdu_long total_samples,
                         bool cpu)
 /* There are three versions of the main decompression process, split out to
    make it easier for you to see what is going on.  This one is intended for
    use with single-threaded processing.  The `expand_multi_threaded'
    version is similar, except that it makes no sense to separately measure
    and subtract the amount of CPU time spent writing imagery to files
    because such file I/O is likely to be overlapped by background processing
    in other threads.  The final version is `expand_multi_threaded_io',
    which explicitly moves the image file writing logic to a separate thread,
    with double buffering of the stripe buffers, so as to minimize the risk
    that processing is held up by disk I/O. */
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0, writing_time=0.0;
  bool continues=true;
  while (continues)
    { 
      decompressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                                  absolute_max_stripe_height,
                                                  stripe_heights,NULL);
      if (stripe_bufs8 != NULL)
        continues = decompressor.pull_stripe(stripe_bufs8,stripe_heights,
                                             sample_gaps,row_gaps,precisions,
                                             pad_flags);
      else
        continues = decompressor.pull_stripe(stripe_bufs16,stripe_heights,
                                             sample_gaps,row_gaps,precisions,
                                             is_signed);
      if (out_files != NULL)
        { // Attempt to discount file writing time
          if (cpu)
            processing_time += timer.get_ellapsed_seconds();
          for (kd_output_file *out=out_files; out != NULL; out=out->next)
            out->write_stripe(stripe_heights[out->first_comp_idx]);
          if (cpu)
            writing_time += timer.get_ellapsed_seconds();
        }
    }
  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = ((double)total_samples) / processing_time;
      pretty_cout << "Processing time = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second << " Msamples/s\n";
      pretty_cout << "End-to-end time (including file writing) = "
                  << processing_time + writing_time << " s.\n";
    }
}

/*****************************************************************************/
/* STATIC                    expand_multi_threaded                           */
/*****************************************************************************/

static void
  expand_multi_threaded(kdu_stripe_decompressor &decompressor,
                        int preferred_min_stripe_height,
                        int absolute_max_stripe_height,
                        int stripe_heights[], int max_stripe_heights[],
                        int sample_gaps[], int row_gaps[],
                        int precisions[], bool is_signed[], int pad_flags[],
                        kdu_byte *stripe_bufs8[], kdu_int16 *stripe_bufs16[],
                        kd_output_file *out_files, kdu_long total_samples,
                        bool cpu, int num_threads)
 /* There are three versions of the main decompression process in this
    demo app, split out to help you understand what is going on.  This one
    is almost identical to `expand_single_threaded', except that we do
    not bother separately measuring the time taken for incremental file
    writing operations since this cannot be legitimately subtracted from
    the overall processing time, given that there are background worker
    threads that have the potential to keep processing while the file
    writing operation is going on.
       Nevertheless, while the main thread is waiting to write an incremental
    stripe of output data to any image files, it may happen that background
    working threads begin to run out of things to do.  This is more
    likely when processing tiled codestreams, if the `tile_concurrency'
    argument passed to `kdu_stripe_decompressor::start' was smaller than
    the number of tiles across the image.  To enable maximum thread
    concurrency with small values of `tile_concurrency' (better for cache
    localized processing), we recommend writing the image stripes in a
    separate, as done bhy the third processing demonstration that is
    embodied b the `expand_multi_threaded_io' function. */
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0;
  
  bool continues=true;
  while (continues)
    { 
      decompressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                                  absolute_max_stripe_height,
                                                  stripe_heights,NULL);
      if (stripe_bufs8 != NULL)
        continues = decompressor.pull_stripe(stripe_bufs8,stripe_heights,
                                             sample_gaps,row_gaps,precisions,
                                             pad_flags);
        else
          continues = decompressor.pull_stripe(stripe_bufs16,stripe_heights,
                                               sample_gaps,row_gaps,precisions,
                                               is_signed);
      for (kd_output_file *out=out_files; out != NULL; out=out->next)
        out->write_stripe(stripe_heights[out->first_comp_idx]);
    }

  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = ((double)total_samples) / processing_time;
      pretty_cout << "End-to-end time (including file writing) = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second
                  << " Msamples/s\n";
      pretty_cout << "Processed using the multi-threaded environment, with\n"
                  << "    "
                  << num_threads << " parallel threads of execution "
                  << "(see `-num_threads')\n";
    }
}

/*****************************************************************************/
/* STATIC                  expand_multi_threaded_io                          */
/*****************************************************************************/
      
    struct writer_thread_state {
      kdu_thread thread;
      kdu_semaphore full_sets; // Num stripe sets available to file writer
      kdu_semaphore free_sets; // Num stripe sets available for pull
      kd_output_file *out_files;
      int *stripe_heights[2]; // Entry becomes NULL if corresponding stripe
                              // set lies beyond the output image
      void close()
        { thread.destroy(); free_sets.destroy(); full_sets.destroy(); }
    };
    
    static kdu_thread_startproc_result
      KDU_THREAD_STARTPROC_CALL_CONVENTION writer_thread_entry(void *param)
    { 
      writer_thread_state *state = (writer_thread_state *) param;
      int priority, min_priority, max_priority;
      priority = state->thread.get_priority(min_priority,max_priority);
      if (priority < max_priority)
        state->thread.set_priority(priority+1);
      int write_set = 0; // We always start out writing from stripe set 0
      while (state->full_sets.wait() &&
             (state->stripe_heights[write_set] != NULL))
        { 
          int *heights = state->stripe_heights[write_set];
          for (kd_output_file *out=state->out_files;
               out != NULL; out=out->next)
            { 
              out->write_stripe(heights[out->first_comp_idx]);
              out->swap_buffers();
            }
          state->free_sets.signal();
          write_set = 1 - write_set;
        }
      return KDU_THREAD_STARTPROC_ZERO_RESULT;
    }
      
static void
  expand_multi_threaded_io(kdu_stripe_decompressor &decompressor,
                           int preferred_min_stripe_height,
                           int absolute_max_stripe_height,
                           int stripe_heights[], int alt_stripe_heights[],
                           int max_stripe_heights[],
                           int sample_gaps[], int row_gaps[],
                           int precisions[], bool is_signed[], int pad_flags[],
                           kdu_byte *stripe_bufs8[],
                           kdu_byte *alt_stripe_bufs8[],
                           kdu_int16 *stripe_bufs16[],
                           kdu_int16 *alt_stripe_bufs16[],
                           kd_output_file *out_files, kdu_long total_samples,
                           bool cpu, int num_threads)
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0;
  
  // Set up writing machinery (but don't start it yet)
  writer_thread_state writer;
  writer.free_sets.create(1);
  writer.full_sets.create(1);
  writer.out_files = out_files;
  writer.stripe_heights[0] = stripe_heights;
  writer.stripe_heights[1] = alt_stripe_heights;
  kdu_byte **bufs8[2]={stripe_bufs8,alt_stripe_bufs8};
  kdu_int16 **bufs16[2]={stripe_bufs16,alt_stripe_bufs16};
  int *heights[2]={stripe_heights,alt_stripe_heights};
    
  // Expand the first stripe before launching the writing thread
  bool continues;
  decompressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                              absolute_max_stripe_height,
                                              stripe_heights,NULL);
  if (stripe_bufs8 != NULL)
    continues = decompressor.pull_stripe(stripe_bufs8,stripe_heights,
                                         sample_gaps,row_gaps,precisions,
                                         pad_flags);
  else
    continues = decompressor.pull_stripe(stripe_bufs16,stripe_heights,
                                         sample_gaps,row_gaps,precisions,
                                         is_signed);

  // Launch the file writer thread
  int cur_set=1; // Working set of stripe parameters for next pull (0, 1)
  if (!writer.thread.create(writer_thread_entry,&writer))
    { kdu_error e; e << "Unable to start background file writer thread!"; }
  
  // Process the remaining stripes
  while (continues)
    { 
      decompressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                                  absolute_max_stripe_height,
                                                  heights[cur_set],NULL);
      writer.free_sets.wait();
      if (stripe_bufs8 != NULL)
        continues = decompressor.pull_stripe(bufs8[cur_set],heights[cur_set],
                                             sample_gaps,row_gaps,precisions,
                                             pad_flags);
      else
        continues = decompressor.pull_stripe(bufs16[cur_set],heights[cur_set],
                                             sample_gaps,row_gaps,precisions,
                                             is_signed);
      writer.full_sets.signal();
      cur_set = 1 - cur_set;
    }

  // Terminate the writer thread
  writer.free_sets.wait(); // Make sure writer is not using `cur_set'
  writer.stripe_heights[cur_set] = NULL; // Writer exits when it sees this
  writer.full_sets.signal(); // Encourage writer to see the above NULL pointer
  writer.close();
  
  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = ((double)total_samples) / processing_time;
      pretty_cout << "End-to-end time (including file writing) = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second << " Msamples/s\n";
      pretty_cout << "Processed using the multi-threaded environment, with\n"
                  << "    "  << num_threads
                  << " parallel threads of execution (see `-num_threads')\n";
    }
}


/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/*                                   main                                    */
/*****************************************************************************/

int main(int argc, char *argv[])
{
  kdu_customize_warnings(&pretty_cout);
  kdu_customize_errors(&pretty_cerr);
  kdu_args args(argc,argv,"-s");

  // Parse simple arguments from command line
  char *ifname;
  bool prefer_bmp4;
  float max_bpp;
  int skip_components, max_layers, discard_levels;
  int preferred_min_stripe_height, absolute_max_stripe_height;
  kdu_dims region;
  int num_threads, env_dbuf_height, tile_concurrency;
  bool multi_threaded_io;
  bool force_precise, want_fastest, simulate_parsing, cpu;
  kd_output_file *out_files =
    parse_simple_args(args,ifname,prefer_bmp4,max_bpp,simulate_parsing,
                      skip_components,max_layers,discard_levels,
                      region,preferred_min_stripe_height,
                      absolute_max_stripe_height,force_precise,want_fastest,
                      num_threads,env_dbuf_height,tile_concurrency,
                      multi_threaded_io,cpu);
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }

  // Create appropriate output file
  kdu_compressed_source *input = NULL;
  kdu_simple_file_source file_in;
  jp2_family_src jp2_ultimate_src;
  jp2_source jp2_in;
  if (check_jp2_family_file(ifname))
    {
      input = &jp2_in;
      jp2_ultimate_src.open(ifname);
      if (!jp2_in.open(&jp2_ultimate_src))
        { kdu_error e; e << "Supplied input file, \"" << ifname << "\", does "
          "not appear to contain any boxes."; }
      jp2_in.read_header();
    }
  else
    {
      input = &file_in;
      file_in.open(ifname);
    }
  delete[] ifname;

  // Create the code-stream, and apply any restrictions/transformations
  kdu_codestream codestream;
  codestream.create(input);
  if ((max_bpp > 0.0F) || simulate_parsing)
    {
      kdu_long max_bytes = KDU_LONG_MAX;
      if (max_bpp > 0.0F)
        max_bytes = (kdu_long)
          (0.125 * max_bpp * get_bpp_dims(codestream.access_siz()));
      codestream.set_max_bytes(max_bytes,simulate_parsing);
    }
  codestream.apply_input_restrictions(skip_components,0,
                                      discard_levels,max_layers,NULL,
                                      KDU_WANT_OUTPUT_COMPONENTS);
  codestream.set_fast();

  kdu_dims *reg_ptr = NULL;
  if (region.area() > 0)
    {
      kdu_dims dims; codestream.get_dims(0,dims,true);
      dims &= region;
      if (!dims)
        { kdu_error e; e << "Region supplied via `-int_region' argument "
          "has no intersection with the first image component to be "
          "decompressed, at the resolution selected."; }
      codestream.map_region(0,dims,region,true);
      reg_ptr = &region;
      codestream.apply_input_restrictions(skip_components,0,
                                          discard_levels,max_layers,reg_ptr,
                                          KDU_WANT_OUTPUT_COMPONENTS);
    }

        // If you wish to have rotation/transposition folded into the
        // decompression process automatically, this is the place to call
        // `kdu_codestream::change_appearance'.

  // Find the dimensions of each image component we will be decompressing
  int n, num_components = codestream.get_num_components(true);
  kdu_dims *comp_dims = new kdu_dims[num_components];
  for (n=0; n < num_components; n++)
    codestream.get_dims(n,comp_dims[n],true);

  // Next, prepare the output files
  bool add_padding_channel=false;
  bool write_bytes=true; // Until we find a file that requires more precision
  kd_output_file *out;
  if (out_files == NULL)
    multi_threaded_io = false;
  else
    { 
      bool flip_vertically=false;
      for (n=0, out=out_files; out != NULL; out=out->next)
        { 
          n = out->first_comp_idx;
          if (out->is_bmp && (out->next == NULL))
            { // BMP files can have a variable number of components
              if ((out->lim_comp_idx > num_components) && prefer_bmp4 &&
                  (num_components == (out->first_comp_idx+3)))
                { // Special case where we want to write a 32bpp BMP file to
                  // hold 3 colour channels plus a padding channel (alpha)
                  add_padding_channel = true;
                  out->lim_comp_idx = num_components;
                  assert(out->samples_per_pel == 4);
                }
              if (out->lim_comp_idx > num_components)
                { 
                  out->lim_comp_idx = out->first_comp_idx + 3;
                  out->samples_per_pel = 3;
                }
              if (out->lim_comp_idx > num_components)
                { 
                  out->lim_comp_idx = out->first_comp_idx + 1;
                  out->samples_per_pel = 1;
                }
            }
          if (out->lim_comp_idx > num_components)
            { kdu_error e; e << "The supplied output files represent more "
              "image components than are available to decompress!"; }
          out->size = comp_dims[n].size;
          if (out->is_raw)
            { // Try to preserve all original precision and signed/unsigned
              // properties for raw files.
              out->precision = codestream.get_bit_depth(n,true);
              out->is_signed = codestream.get_signed(n,true);
              assert(out->precision > 0);
              if (out->precision > 16)
                out->precision = 16; // Can't store more than 16 bits/sample
              out->bytes_per_sample = (out->precision > 8)?2:1;
              if (out->bytes_per_sample > 1)
                write_bytes = false;
            }
          else if (out->is_bmp)
            { // BMP files are bottom-up
              flip_vertically = true;
              out->precision = 8; out->is_signed = false;
              out->bytes_per_sample = 1;
              out->write_header();
            }
          else
            { // PGM/PPM files always have an 8-bit unsigned representation
              out->precision = 8; out->is_signed = false;
              out->bytes_per_sample = 1;
              out->write_header();
            }
          for (; n < out->lim_comp_idx; n++)
            if (out->size != comp_dims[n].size)
              { kdu_error e; e << "Trying to write image components with "
                "different sizes to a single PPM file."; }
        }
      if (n < num_components)
        codestream.apply_input_restrictions(skip_components,num_components=n,
                                            discard_levels,max_layers,reg_ptr,
                                            KDU_WANT_OUTPUT_COMPONENTS);
      if (flip_vertically)
        codestream.change_appearance(false,true,false);
    }

  // Construct multi-threaded processing environment, if requested.  Note that
  // all we have to do to leverage the presence of multiple physical processors
  // is to create the multi-threaded environment with at least one thread for
  // each processor, pass a reference (`env_ref') to this environment into
  // `kdu_stripe_decompressor::start', and destroy the environment once we are
  // all done.
  //    If you are going to run the processing within a try/catch
  // environment, with an error handler which throws exceptions rather than
  // exiting the process, the only extra thing you need to do to realize
  // robust multi-threaded processing, is to arrange for your `catch' clause
  // to invoke `kdu_thread_entity::handle_exception' -- i.e., call
  // `env.handle_exception(exc)', where `exc' is the exception code you catch,
  // of type `kdu_exception'.  Even this is not necessary if you are happy for
  // the `kdu_thread_env' object to be destroyed when an error/exception
  // occurs.
  kdu_thread_env env, *env_ref=NULL;
  if (num_threads > 0)
    {
      env.create();
      for (int nt=1; nt < num_threads; nt++)
        if (!env.add_thread())
          num_threads = nt; // Unable to create all the threads requested
      env_ref = &env;
    }
  if (num_threads < 2)
    multi_threaded_io = false;

  // Create arrays to store per-component specifications and stripe buffers
  int *precisions = new int[num_components];
  bool *is_signed = new bool[num_components];
  int *stripe_heights = new int[num_components];
  int *sample_gaps = new int[num_components];
  int *row_gaps = new int[num_components];
  int *max_stripe_heights = new int[num_components];
  int *pad_flags = NULL;
  if (add_padding_channel)
    { 
      pad_flags = new int[num_components];
      for (n=0; n < num_components; n++)
        pad_flags[n] = 0;
      pad_flags[0] = KDU_STRIPE_PAD_AFTER | KDU_STRIPE_PAD_HIGH;
        // Note: component 0 (Red) is in the third slot of the four slots
        // for each pixel.
    }
  kdu_byte **buf_handles = new kdu_byte *[num_components]; // For dealloc
  for (n=0; n < num_components; n++)
    buf_handles[n] = NULL;
  kdu_int16 **stripe_bufs16=NULL;
  kdu_byte **stripe_bufs8=NULL;
  if (write_bytes)
    stripe_bufs8 = new kdu_byte *[num_components];
  else
    stripe_bufs16 = new kdu_int16 *[num_components];
  
  // If double-buffered I/O is being used, create alternate stripe buffers
  // so that a background thread can be writing to one set of stripes while
  // the foreground thread pulls data from the stripe-decompressor.
  int *alt_stripe_heights=NULL;
  kdu_int16 **alt_stripe_bufs16=NULL;
  kdu_byte **alt_stripe_bufs8=NULL;
  if (multi_threaded_io)
    { 
      alt_stripe_heights = new int[num_components];
      if (write_bytes)
        alt_stripe_bufs8 = new kdu_byte *[num_components];
      else
        alt_stripe_bufs16 = new kdu_int16 *[num_components];  
    }
  
  // Construct the stripe-decompressor object (this does all the work), use it
  // to find recommended stripe heights, then allocate the buffers.
  // Note that nothing stops you from pulling stripes of any desired size to
  // an image buffer of your choice -- you can even pull the entire image
  // into a suitably large buffer in one hit.  This demonstration writes
  // the image incrementally to any output files that may have been supplied
  // (skipped if there are no output files of course).  In this case, it is
  // generally better to limit the amount of memory used to temporarily buffer
  // file data between decompression and writing.   The present application
  // uses `kdu_stripe_decompressor::get_recommended_stripe_heights' to find
  // suitable stripe heights for processing -- not surprisingly, we do
  // recommend this.
  //    Note that whenever the codestream uses tiles, stripe heights
  // can have an impact on the efficiency with which the image is decompressed
  // (a fundamental issue, not a Kakadu implementation issue).  We recommend
  // that codestreams with small tiles (e.g., 1024 x 1024) pull data in
  // stripes whose height corresponds exactly to that of the tiles.
  // Moreover, in such cases, in a multi-threaded setting with multiple
  // horizontally adjacent tiles, we recommend that you set the DWT double
  // buffering height (`env_dbuf_height' here) to half the tile height.  For
  // larger tiles, or untiled codestreams, you are usually best off working
  // with much smaller double buffering stripe heights and pulling modest
  // stripes.  Fortunately, these recommendations are all implemented
  // automatically for you if you use the `get_recommende_stripe_heights' and
  // pass the default value of -1 for the `env_dbuf_height' argument to
  // `kdu_stripe_decompressor::start'.
  kdu_stripe_decompressor decompressor;
  decompressor.start(codestream,force_precise,want_fastest,
                     env_ref,NULL,env_dbuf_height,tile_concurrency);
  decompressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                              absolute_max_stripe_height,
                                              stripe_heights,
                                              max_stripe_heights);
  if (out_files == NULL)
    { // Allocate the buffers, independently of any files
      assert(!multi_threaded_io);
      for (n=0; n < num_components; n++)
        { 
          precisions[n] = (write_bytes)?8:16;
          is_signed[n] = false;
          sample_gaps[n] = 1;
          row_gaps[n] = comp_dims[n].size.x;
          int num_samples = row_gaps[n] * max_stripe_heights[n];
          int num_bytes = (write_bytes)?num_samples:(2*num_samples);
          kdu_byte *addr = new (std::nothrow) kdu_byte[num_bytes+31];
          if (addr == NULL)
            { kdu_error e;
              e << "Insufficient memory to allocate stripe buffers."; }
          buf_handles[n] = addr;
          addr += (-_addr_to_kdu_int32(addr))&0x1F; // 32-byte aligned
          if (write_bytes)
            stripe_bufs8[n] = addr;
          else
            stripe_bufs16[n] = (kdu_int16 *) addr;
        }
    }
  for (out=out_files; out != NULL; out=out->next)
    { 
      n = out->first_comp_idx;
      
      kdu_byte *addr = NULL;
      int stride = out->size.x * out->samples_per_pel;
      if (out->is_bmp)
        stride += (-stride) & 3; // Round out to multiple of 4 bytes for BMP
      out->buf_row_gap = stride;
      int num_samples = stride*max_stripe_heights[n];
      int num_bytes = (write_bytes)?num_samples:(2*num_samples);      
      if (multi_threaded_io)
        { 
          num_samples += (-num_samples) & 31; // Round up to multiple of 32
          num_bytes = (write_bytes)?num_samples:(2*num_samples);
          addr = new (std::nothrow) kdu_byte[2*num_bytes + 31];
        }
      else
        addr = new (std::nothrow) kdu_byte[num_bytes+31];
      if (addr == NULL)
        { kdu_error e;
          e << "Insufficient memory to allocate stripe buffers; try "
          "changing the value passed to the \"-min_height\" or "
          "\"-max_height\" command-line argument."; }
      buf_handles[n] = addr; // Save handle so we can deallocate the buffer
      addr += (-_addr_to_kdu_int32(addr)) & 0x1F; // 32-byte alignment may
                                            // make transfers more efficient
      
      // Assign aligned buffer to the appropriate pointer
      if (write_bytes)
        { 
          out->buf8 = addr;
          if (multi_threaded_io)
            out->alt_buf8 = out->buf8 + num_samples;
        }
      else
        { 
          out->buf16 = (kdu_int16 *) addr;
          if (multi_threaded_io)
            out->alt_buf16 = out->buf16 + num_samples;
        }
      for (; n < out->lim_comp_idx; n++)
        { 
          assert(stripe_heights[n] == stripe_heights[out->first_comp_idx]);
          precisions[n] = out->precision;
          is_signed[n] = out->is_signed;
          sample_gaps[n] = out->samples_per_pel;
          row_gaps[n] = out->buf_row_gap;
          int comp_offset = n - out->first_comp_idx;
          if (out->is_bmp && (sample_gaps[n] >= 3) && (comp_offset < 3))
            comp_offset = 2-comp_offset; // Reverse RGB to BGR
          if (write_bytes)
            { 
              stripe_bufs8[n] = out->buf8 + comp_offset;
              if (multi_threaded_io)
                alt_stripe_bufs8[n] = stripe_bufs8[n] + num_samples;
            }
          else
            { 
              stripe_bufs16[n] = out->buf16 + comp_offset;
              if (multi_threaded_io)
                alt_stripe_bufs16[n] = stripe_bufs16[n] + num_samples;
            }
        }
    }
  
  // At last we come to the actual processing, having configured all the
  // parameters in a reasonable manner.  This is where all the action
  // actually is.  Everything else can be simplified, hard-wired, etc.,
  // depending on your particular application.
  kdu_long total_samples = 0; // Just for throughput reporting
  for (n=0; n < num_components; n++)
    total_samples += comp_dims[n].area();
  if (num_threads <= 1)
    expand_single_threaded(decompressor,preferred_min_stripe_height,
                           absolute_max_stripe_height,
                           stripe_heights,max_stripe_heights,
                           sample_gaps,row_gaps,precisions,is_signed,
                           pad_flags,stripe_bufs8,stripe_bufs16,
                           out_files,total_samples,cpu);
  else if (!multi_threaded_io)
    expand_multi_threaded(decompressor,preferred_min_stripe_height,
                          absolute_max_stripe_height,
                          stripe_heights,max_stripe_heights,
                          sample_gaps,row_gaps,precisions,is_signed,
                          pad_flags,stripe_bufs8,stripe_bufs16,
                          out_files,total_samples,cpu,num_threads);
  else
    expand_multi_threaded_io(decompressor,preferred_min_stripe_height,
                             absolute_max_stripe_height,
                             stripe_heights,alt_stripe_heights,
                             max_stripe_heights,
                             sample_gaps,row_gaps,precisions,is_signed,
                             pad_flags,stripe_bufs8,alt_stripe_bufs8,
                             stripe_bufs16,alt_stripe_bufs16,
                             out_files,total_samples,cpu,num_threads);

  // Clean up
  decompressor.finish();
  if (env.exists())
    env.destroy(); // Note: there is no need to call `env.cs_terminate' here,
                   // because: a) it has already been called inside
                   // `decompressor.finish'; and b) we are calling
                   // `env.destroy' first.
  codestream.destroy();
  input->close();
  if (jp2_ultimate_src.exists())
    jp2_ultimate_src.close();
  for (n=0; n < num_components; n++)
    if (buf_handles[n] != NULL)
      delete[] buf_handles[n];
  delete[] buf_handles;
  if (stripe_bufs8 != NULL) delete[] stripe_bufs8;
  if (stripe_bufs16 != NULL) delete[] stripe_bufs16;
  if (pad_flags != NULL) delete[] pad_flags;
  if (alt_stripe_bufs8 != NULL) delete[] alt_stripe_bufs8;
  if (alt_stripe_bufs16 != NULL) delete[] alt_stripe_bufs16;
  delete[] precisions;
  delete[] is_signed;
  delete[] stripe_heights;
  if (alt_stripe_heights != NULL) delete[] alt_stripe_heights;
  delete[] sample_gaps;
  delete[] row_gaps;
  delete[] max_stripe_heights;
  delete[] comp_dims;
  while ((out=out_files) != NULL)
    { out_files=out->next; delete out; }
  return 0;
}
