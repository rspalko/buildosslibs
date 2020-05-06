/*****************************************************************************/
// File: kdu_stripe_compressor.cpp [scope = APPS/BUFFERED_COMPRESS]
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
`kdu_stripe_compressor' object.
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
#include "kdu_stripe_compressor.h"
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
/* INLINE                    eat_white_and_comments                          */
/*****************************************************************************/

static inline void
  eat_white_and_comments(FILE *in)
{
  int ch;
  bool in_comment;

  in_comment = false;
  while ((ch = getc(in)) != EOF)
    if (ch == '#')
      in_comment = true;
    else if (ch == '\n')
      in_comment = false;
    else if ((!in_comment) && (ch != ' ') && (ch != '\t') && (ch != '\r'))
      {
        ungetc(ch,in);
        return;
      }
}

/*****************************************************************************/
/* STATIC                      from_little_endian                            */
/*****************************************************************************/

static void
  from_little_endian(kdu_int32 * words, int num_words)
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
/*                             kdc_null_target                               */
/* ========================================================================= */

class kdc_null_target : public kdu_compressed_target {
public:
  ~kdc_null_target() { return; } // Destructor must be virtual
  int get_capabilities() { return KDU_TARGET_CAP_CACHED; }
  bool write(const kdu_byte *buf, int num_bytes) { return true; }
  };
  /* Note: this object does nothing other than advertise that it supports
     structured writing (`KDU_TARGET_CAP_CACHED'), but silently discarding
     all data.  This is useful for testing the throughput of the
     compression process, unburdened by any I/O delays. */


/* ========================================================================= */
/*                                kd_source_file                             */
/* ========================================================================= */

struct kd_source_file {
  public: // Lifecycle functions
    kd_source_file()
      { fname=NULL; fp=NULL; first_comp_idx=lim_comp_idx=0;
        samples_per_pel=1; bytes_per_sample=1; precision=8;
        is_signed=is_raw=is_bmp=swap_bytes=false; start_pos=0;
        buf_row_gap=0; buf16=NULL; buf8=NULL;
        alt_buf16=NULL; alt_buf8=NULL; buffered_lines=0; next=NULL; }
    ~kd_source_file()
      { 
        if (fname != NULL) delete[] fname;
        if (fp != NULL) fclose(fp);
      }
    void swap_buffers()
      { // Swaps `buf8' with `alt_buf8' and `buf16' with `alt_buf16' for
        // double-buffered file reading with `compress_multi_threaded_io'.
        assert(alt_buf16 || alt_buf8);
        kdu_byte *tmp8=buf8; buf8=alt_buf8; alt_buf8=tmp8;
        kdu_int16 *tmp16=buf16; buf16=alt_buf16; alt_buf16=tmp16;
      }
    void read_pnm_header();
      /* Reads a PGM or PPM header, setting the dimensions and
       `samples_per_pel' members.  It is the caller's responsibility to use
       `samples_per_pel' to configure `lim_comp_idx'.*/
    void read_bmp_header();
      /* Reads a BMP header, setting the dimensions and `samples_per_pel'
         members.  It is the caller's responsibility to use `samples_per_pel'
         to configure `lim_comp_idx'. */
    kdu_long read_stripe(int height);
      /* Reads to the internal `buf8' or `buf16' array, as appropriate.
            Note: this function does not computation outside of the kernel
         so long as all files have the same number of bytes per sample (1 or 2)
         and data with multi-byte samples is already in native word order so
         that `swap_bytes' is false.  Otherwise, this function has to do
         some conversions which it does in a simplistic sample-by-sample
         fashion that could become a bottleneck for overall throughput
         on systems with a large number of CPU's.
            If you are developing your own application, based on this demo,
         the lesson is that you should keep your data in the most natural
         format (i.e., as small as possible) and do as few (if any)
         transformations of the data yourself, letting the
         `kdu_stripe_compressor::push_stripe' function handle all
         required transformations.
            The function returns the total number of bytes read.
      */
  public: // Data
    char *fname;
    FILE *fp;
    int first_comp_idx; // First component index supplied by this file
    int lim_comp_idx; // Last component index supplied, plus 1
    int samples_per_pel; // always lim_comp-first_comp in this application
    int bytes_per_sample;
    int precision; // Num bits
    bool is_signed;
    bool is_raw;
    bool is_bmp;
    bool swap_bytes; // If raw file word order differs from machine word order
    kdu_long start_pos; // Start of data region within the file
    kdu_coords size; // Width, and remaining rows
    kdu_coords original_size; // `size' before anything is read
    int buf_row_gap; // Measured in samples
    kdu_int16 *buf16; // Non-NULL if any files require 16-bit precision
    kdu_byte *buf8; // Non-NULL if all files have 8-bit precision
    kdu_int16 *alt_buf16; // These are NULL unless we are doing double-buffered
    kdu_byte *alt_buf8;   // stripe reading (see `compress_multi_threaded_io')
    int buffered_lines; // Number of valid lines buffered by last `read_stripe'
    kd_source_file *next;
  };
  /* Note: `buf16' and `buf8' point to resources that are not owned by
     this object; they must be deallocated externally.  The same is true
     for `alt_buf16' and `alt_buf8', if non-NULL. */

/*****************************************************************************/
/*                      kd_source_file::read_pnm_header                      */
/*****************************************************************************/

void
  kd_source_file::read_pnm_header()
{
  int ch;
  char magic[3] = {'\0','\0','\0'};
  size_t nb = fread(magic,1,2,fp);
  bool is_pgm=false, is_ppm=false;
  if ((nb != 2) ||
      !((is_pgm = (strcmp(magic,"P5") == 0)) ||
        (is_ppm = (strcmp(magic,"P6") == 0))))
    { kdu_error e; e << "PGM/PPM image file must start with the magic string, "
      "\"P5\" or \"P6\"!"; }
  samples_per_pel = ((is_ppm)?3:1);
  bool failed = false;
  eat_white_and_comments(fp);
  if (fscanf(fp,"%d",&size.x) != 1)
    failed = true;
  eat_white_and_comments(fp);
  if (fscanf(fp,"%d",&size.y) != 1)
    failed = true;
  eat_white_and_comments(fp);
  if (fscanf(fp,"%d",&ch) != 1)
    failed = true;
  if (failed || (size.x < 1) || (size.y < 1))
    {kdu_error e; e << "Image file \"" << fname << "\" does not appear to "
     "have a valid PGM header."; }
  while ((ch = fgetc(fp)) != EOF)
    if ((ch == '\n') || (ch == ' '))
      break;
  start_pos = kdu_ftell(fp);
}

/*****************************************************************************/
/*                      kd_source_file::read_bmp_header                      */
/*****************************************************************************/

void
  kd_source_file::read_bmp_header()
{
  kdu_byte magic[14];
  bmp_header header;
  size_t nb = fread(magic,1,14,fp);
  if ((nb != 14) ||
      (magic[0] != 'B') || (magic[1] != 'M') || (fread(&header,1,40,fp) != 40))
    { kdu_error e; e << "BMP image file must start with the magic string, "
      "\"BM\", and continue with a header whose total size is at least 54 "
      "bytes."; }
  from_little_endian((kdu_int32 *) &header,10);
  if (header.compression != 0)
    { kdu_error e; e << "BMP image file contains a compressed "
      "representation.  Processing of BMP compression types is certainly "
      "not within the scope of this JPEG2000-centric demonstration "
      "application.  Try loading your file into an image editing application "
      "and saving it again in an uncompressed format."; }
  size.x = header.width;
  size.y = header.height;
  int bit_count = (header.planes_bits>>16);
  if (bit_count == 32)
    samples_per_pel = 4;
  else if (bit_count == 24)
    samples_per_pel = 3;
  else if (bit_count == 8)
    samples_per_pel = 1;
  else
    { kdu_error e;
      e << "This app supports only 8-, 24- and 32-bit BMP files."; }
  int palette_entries_used = header.num_colours_used;
  if (samples_per_pel != 1)
    palette_entries_used = 0;
  else if (header.num_colours_used == 0)
    palette_entries_used = 256;
  int header_size = 54 + 4*palette_entries_used;

  int offset = magic[13];
  offset <<= 8; offset += magic[12];
  offset <<= 8; offset += magic[11];
  offset <<= 8; offset += magic[10];
  if (offset < header_size)
    { kdu_error e; e << "Invalid sample data offset field specified in BMP "
      "file header!"; }
  if (samples_per_pel == 1)
    { 
      kdu_byte map[1024];
      assert((palette_entries_used >= 0) && (palette_entries_used <= 256));
      size_t nb = fread(map,1,(size_t)(4*palette_entries_used),fp);
      if (nb != (size_t)(4*palette_entries_used))
        { kdu_error e; e << "Could not read declared palette map from "
          "BMP file header!"; }
      int n;
      for (n=0; n < palette_entries_used; n++)
        if ((map[4*n] != n) || (map[4*n+1] != n) || (map[4*n+2] != n))
          break;
      if (n < palette_entries_used)
        { kdu_error e; e << "BMP file uses a non-trivial colour palette -- "
          "i.e., not just used to encode an 8-bit greyscale image.  This "
          "application does not support palette lookup.  Try using "
          "\"kdu_compress\" instead."; }
    }

  if (offset > header_size)
    fseek(fp,offset-header_size,SEEK_CUR);
}

/*****************************************************************************/
/*                         kd_source_file::read_stripe                       */
/*****************************************************************************/

kdu_long
  kd_source_file::read_stripe(int height)
{
  int line_bytes = buf_row_gap * bytes_per_sample;
  int num_samples = buf_row_gap * height;
  kdu_byte *buf = buf8;
  if (buf == NULL)
    buf = (kdu_byte *) buf16;
  if (original_size.y == 0)
    original_size = size; // Keep track of original size
  else if ((size.y == 0) && (height == original_size.y) &&
           (buffered_lines == height))
    return 0; // The buffer already holds a complete copy of the file's
              // contents.
  
  kdu_long total_read_bytes = 0;
  buffered_lines = height;
  while (height > 0)
    { 
      if (size.y == 0)
        { 
          size = original_size;
          kdu_fseek(fp,start_pos);
        }
      int xfer_lines = size.y; // Number of lines left in file
      xfer_lines = (xfer_lines > height)?height:xfer_lines;
      int xfer_bytes = xfer_lines * line_bytes;
      int read_bytes = (int) fread(buf,1,xfer_bytes,fp);
      if (read_bytes != xfer_bytes)
        { kdu_error e;
          e << "File, \"" << fname << "\" terminated unexpectedly."; }
      height -= xfer_lines;
      size.y -= xfer_lines;
      buf += read_bytes;
      total_read_bytes += read_bytes;
    }
  if ((buf16 != NULL) && (bytes_per_sample == 1))
    { // Expand to 16-bit representation
      kdu_int16 *dp=buf16;
      kdu_byte *sp=(kdu_byte *) dp;
      sp += num_samples-1; dp += num_samples-1;
      while (num_samples--)
        *(dp--) = (kdu_int16) *(sp--);
    }
  else if ((buf16 != NULL) && swap_bytes)
    { // Swap byte order
      kdu_int16 *dp = buf16;
      while (num_samples--)
        { kdu_int16 val=*dp; val=(val<<8)+((val>>8)&0x00FF); *(dp++)=val; }
    }
  return total_read_bytes;
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
  out << "This is Kakadu's \"kdu_buffered_compress\" demo application.\n";
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
  out << "-i <PGM/PPM/BMP/raw file 1>[,<PGM/PPM/BMP/raw file 2>[,...]]\n";
  if (comprehensive)
    out << "\tOne or more input image files.  To understand how the number "
           "and dimensions of the input files interact with the dimensions "
           "and bit-depths recorded in the codestream header, along with any "
           "defined multi-component transform, see the discussion which "
           "appears at the end of this usage statement.\n"
           "\t   To simplify this demo application, while still allowing the "
           "`kdu_stripe_compressor' interface to be adequately tested "
           "and demonstrated, only the following input file formats are "
           "currently supported: PGM (1 component, 8bits); PPM (3 components, "
           "8bits); BMP (1, 3 or 4 components, 8bits); RAW (1 component, "
           "up to 16bits/sample).  Multiple files may be supplied, but "
           "BMP files may not be mixed with the other types, because BMP "
           "files require the compression process to proceed in "
           "bottom-up, rather than top-down fashion.\n"
           "\t   As in the \"kdu_compress\" application, raw files "
           "rely upon the dimensions, precision and signed/unsigned "
           "characteristics being configured using `Sdims', `Nprecision' "
           "and `Nsigned' command-line arguments -- see multi-component "
           "transforms discussion below.  The sample bits in a raw file "
           "are obtained from the least significant bit positions of an 8 "
           "or 16 bit word, depending on the bit-depth; any unused MSB's are "
           "ignored, regardless of whether the sample representation is "
           "identified as signed or unsigned.  The default word organization "
           "is big-endian, regardless of your machine architecture, but this "
           "application allows you to explicitly nominate a different byte "
           "order, via the `-little_endian' argument.  Moreover, the process "
           "is most efficient if the word organization corresponds to the "
           "machine's native order.  Unlike the \"kdu_compress\" demo app, "
           "this one does not take note of the suffix used for raw files, but "
           "we recommend a suffix of \".raw\" for big-endian and \".rawl\" "
           "for little-endian formats.\n";
  out << "-little_endian -- use little-endian byte order with raw files\n";
  out << "-vrep <vertical replicas>\n";
  if (comprehensive)
    out << "\tThis option allows you to test the performance of Kakadu "
           "when compressing much larger images than the source files you "
           "supply via the \"-i\" argument.  It effectively creates a "
           "new image that contains <vertical replicas> replicas of the "
           "original, for each supplied input file.  When using this "
           "option to a vertical replication factor that is greater than 1, "
           "the input files are up front into a single large buffer, from "
           "which stripes are passed into the `kdu_stripe_compressor::push' "
           "function.  This allows you to test the performance in a manner "
           "that is independent of the I/O bandwidth available for "
           "reading the input files.\n"
           "\t   If raw input files are used, the size of the entire "
           "vertical dimension specified via `Sdims' is divided by the "
           "<vertical replicas> in order to determine the number of lines "
           "in the raw image file -- it must be exactly divisible.  "
           "If PGM/PPM files are used, the dimensions of the compressed "
           "image are determined by multiplying the number of image lines "
           "in the input file by the <vertical replicas> parameter.\n";
  out << "-o <compressed file -- raw code-stream unless suffix is \".jp2\">\n";
  if (comprehensive)
    out << "\tName of file to receive the compressed code-stream.  If the "
           "file name has a \".jp2\" suffix (not case sensitive), the "
           "code-stream will be wrapped up inside the JP2 file format.  In "
           "this case, the first 3 source image components will be treated "
           "as sRGB colour channels (red, green then blue) and the remainder "
           "will be identified as auxiliary undefined components in the JP2 "
           "file.  For other options in writing JP2 files, refer to the "
           "more sophisticated \"kdu_compress\" application.\n"
           "\t  From KDU-7.2.1, this argument may be omitted, in which case "
           "the internal codestream generation machinery sees a special "
           "\"structured cache\" compressed data target that allows the "
           "data to be written out of order -- can be very helpful in "
           "conjunction with incremental flushing (see `-flush_period').  "
           "This special compressed data target actually just discards all "
           "generated content -- i.e., it currently exists only for "
           "experimental purposes -- but you may derive your own "
           "structured cache targets that write the content to a "
           "structured database, for example, as opposed to a linear file.\n";
  out << "-slope <distortion-length slope threshold>\n";
  if (comprehensive)
    out << "\tSame interpretation as in \"kdu_compress\" -- this argument "
           "may be used to control compressed image quality through the "
           "distortion-length slope threshold.  The compressed size may vary, "
           "but quality is generally more consistent with slope than "
           "\"-rate\".  You may not use both methods at once.  Generally "
           "\"-slope\" is significantly faster that \"-rate\".\n";
  out << "-rate -|<max bits/pel>[,<min bits/pel>]\n";
  if (comprehensive)
    out << "\tUse this argument to control the maximum bit-rate and/or the "
           "minimum bit-rate associated with the layered code-stream.  The "
           "number of layers is given by the `Clayers' attribute, which you "
           "must set separately, if you want more than one quality layer.  "
           "If the `-' character is substituted for a maximum bit-rate, or "
           "if no `-rate' argument is supplied, the highest quality layer "
           "includes all generated bits.  If the minimum bit-rate is not "
           "supplied, it will be determined by an internal heuristic.  Layer "
           "bit-rates are spaced approximately logarithmically between the "
           "minimum and maximum bit-rates.\n"
           "\t   Note that from KDU7.2, the algorithm used to generate "
           "intermediate quality layers (as well as the lower bound, if not "
           "specified) has changed.  The new algorithm introduces a constant "
           "separation between the logarithmically expressed "
           "distortion-length slope thresholds for the layers.  This is "
           "every bit as useful but much more efficient than the algorithm "
           "employed by previous versions of Kakadu.\n"
           "\t   Note also that the default `-tolerance' value is 2%, "
           "meaning that the actual bit-rate(s) may be as much as 2% smaller "
           "than the specified target(s).  Specify `-tolerance 0' if you "
           "want the most precise rate control.\n";
  out << "-tolerance <percent tolerance on layer sizes given using `-rate'>\n";
  if (comprehensive)
    out << "\tThis argument affects the behaviour of the `-rate' argument "
           "slightly, providing a tolerance specification on the achievement "
           "of the cumulative layer bit-rates given by that argument.  The "
           "rate allocation algorithm will attempt to find distortion-length "
           "slopes such that the relevant bit-rate(s) lie between the "
           "specified limit(s) and (1-tolerance/100) times the specified "
           "limit(s).  Note that the tolerance is given as a "
           "percentage, that it affects only the lower bound, not the upper "
           "bound on the bit-rate, and that the default tolerance is 2%.  For "
           "the most precise rate control, you should provide an explicit "
           "`-tolerance' value of 0.  The lower bound associated with the "
           "rate tolerance might not be achieved if there is insufficient "
           "coded data (after quantization) available for rate control -- in "
           "that case, you may need to reduce the quantization step sizes "
           "employed, which is most easily done using the `Qstep' "
           "attribute.\n";
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
           "granularity with which the input files are read -- larger "
           "values may therefore lead to I/O performance improvements, "
           "especially in multi-threaded environments, where file reading is "
           "performed in a background thread unless \"-foreground_io\" "
           "is specified.\n";
  out << "-max_height <maximum stripe height>\n";
  if (comprehensive)
    out << "\tRegardless of the desire to process in stripes whose height is "
           "equal to the tile height, wherever the image is vertically "
           "tiled, this argument provides an upper bound on the maximum "
           "stripe height.  If the tile height exceeds this value, "
           "an entire row of tiles will be kept open for processing.  This "
           "can avoid excessive memory consumption but tends to be less "
           "efficient when procesing with a large number of threads.  This "
           "argument allows you to control the trade-off between stripe "
           "buffering and tile compression engine memory.  The default "
           "limit is 1024.  Both this default and any value you explicitly "
           "supply here may be increased as required to ensure that they "
           "are no smaller than the \"-min_height\" values.\n";
  out << "-flush_period <incremental flush period, measured in image lines>\n";
  if (comprehensive)
    out << "\tBy default, the system waits until all compressed data has "
           "been generated, by applying colour transforms, wavelet transforms "
           "and block encoding processes to the entire image, before any of "
           "this compressed data is actually written to the output file.  "
           "The present argument may be used to request incremental flushing, "
           "where the compressed data is periodically flushed to the output "
           "file, thereby avoiding the need for internal buffering of the "
           "entire compressed image.  The agument takes a single parameter, "
           "identifying the minimum number of image lines which should be "
           "processed before each attempt to flush new code-stream data.  The "
           "actual period may be larger, if insufficient data has "
           "been generated to progress the code-stream.\n"
           "\t   You should be careful to keep the flushing period large "
           "enough to give the rate control algorithm a decent amount of "
           "compressed data to perform effective rate control.  Generally "
           "a period of at least 1000 or 2000 image lines should be used "
           "for rate driven flushing.\n"
           "\t   You should be aware of the fact that incremental flushing "
           "is possible only on tile boundaries or when the packet "
           "progression sequence is spatially progressive (PCRL), with "
           "sufficiently small precincts.  The vertical dimension of "
           "precincts in the lowest resolution levels must be especially "
           "tightly controlled, particularly if you have a large number of "
           "DWT levels.  As an example, with `Clevels=6', the following "
           "precinct dimensions would be a good choice for use with 32x32 "
           "code-blocks: `Cprecincts={256,256},{128,128},{64,64},{32,64},"
           "{16,64},{8,64},{4,64}'.\n";
  siz_params siz; siz.describe_attributes(out,comprehensive);
  cod_params cod; cod.describe_attributes(out,comprehensive);
  qcd_params qcd; qcd.describe_attributes(out,comprehensive);
  rgn_params rgn; rgn.describe_attributes(out,comprehensive);
  poc_params poc; poc.describe_attributes(out,comprehensive);
  crg_params crg; crg.describe_attributes(out,comprehensive);
  org_params org; org.describe_attributes(out,comprehensive);
  atk_params atk; atk.describe_attributes(out,comprehensive);
  dfs_params dfs; dfs.describe_attributes(out,comprehensive);
  ads_params ads; ads.describe_attributes(out,comprehensive);
  mct_params mct; mct.describe_attributes(out,comprehensive);
  mcc_params mcc; mcc.describe_attributes(out,comprehensive);
  mco_params mco; mco.describe_attributes(out,comprehensive);
  nlt_params nlt; nlt.describe_attributes(out,comprehensive);

  out << "-s <switch file>\n";
  if (comprehensive)
    out << "\tSwitch to reading arguments from a file.  In the file, argument "
           "strings are separated by whitespace characters, including spaces, "
           "tabs and new-line characters.  Comments may be included by "
           "introducing a `#' or a `%' character, either of which causes "
           "the remainder of the line to be discarded.  Any number of "
           "\"-s\" argument switch commands may be included on the command "
           "line.\n";
  out << "-no_weights -- target MSE minimization for colour images.\n";
  if (comprehensive)
    out << "\tBy default, visual weights will be automatically used for "
           "colour imagery (anything with 3 compatible components).  Turn "
           "this off if you want direct minimization of the MSE over all "
           "reconstructed colour components.\n";
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
           "unless the bit-depths of the input images are around 13 bits or "
           "more (depending on other coding conditions) or if \"-precise\" "
           "is specified.\n";
  out << "-double_buffering <num double buffering rows, D>\n";
  if (comprehensive)
    out << "\tThis option is intended to be used in conjunction with "
           "`-num_threads'.  From Kakadu version 7, double buffering "
           "is activated by default in multi-threaded processing "
           "environments, but you can disable it by supplying 0 "
           "to this argument.\n"
           "\t   Without double buffering, DWT operations are all "
           "performed by the single thread which \"owns\" the multi-threaded "
           "processing group.  For a small number of processors, this may "
           "be acceptable, or even optimal, since the DWT is generally quite "
           "a bit less CPU intensive than block encoding (which is always "
           "spread across multiple threads,  if available) and synchronous "
           "single-threaded DWT operations may improve memory access "
           "locality.  However, even for a small number of threads, the "
           "amount of thread idle time can be reduced by activating the "
           "`-double_buffering' option.  In this case, a certain number "
           "of image rows in each image component are actually double "
           "buffered, so that one set can be processed by colour "
           "transformation and sample reading operations, while the other "
           "set is processed by the DWT analysis engines, which themselves "
           "drive the block coding engines.  The number of rows in "
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
           "stripes pushed into `kdu_stripe_compressor' are large enough to "
           "allow tiles to be processed one by one) where it is best for "
           "2D to be equal to the tile height.  In most cases, you are best "
           "off omitting this argument so that the internal machinery "
           "can make its own decisions; in fact, for tiled imagery, the "
           "value of D might be set differently for each tile if you "
           "defer to the default policy.\n";
  out << "-tile_concurrency <num tiles>\n";
  if (comprehensive)
    out << "\tThis option is of interest when generating codestreams with "
           "many small tiles.  In this case, the system will attempt to "
           "read imagery in stripes whose height equals one tile height, "
           "passing these stripes to `kdu_stripe_compressor::push_stripe', "
           "unless the \"-max_height\" value is too small to allow this.  "
           "This allows the `kdu_stripe_compressor' object to open tiles "
           "one by one, pushing the relevant data in and moving on.  With "
           "multi-threaded processing, it can be difficult to keep all "
           "threads concurrently active if the tiles are small and each "
           "tile must be fully processed before the next can be opened, so "
           "instead the system keeps multiple tiles open at once.  The "
           "number of concurrently open tile processing engines can be "
           "customised using this argument, but will not exceed 1 + the "
           "number of tiles that span the image width.\n"
           "\t   This argument does not have any effect if you are not "
           "using a multi-threaded processing environment (see "
           "\"-num_threads\").  If \"-max_height\" is too small to "
           "allow whole tile rows of image samples to be buffered and pushed "
           "into the underlying `kdu_stripe_compressor' API, the argument "
           "has no effect unless you set the tile concurrency value to 1.  In "
           "general the value 1 means \"one tile at a time\" but in the "
           "case where the stripe height is too small, the internal machinery "
           "has no choice but to keep an entire row of tile processing "
           "engines active concurrently, so the value 1 is interpreted as "
           "\"one row of tiles at a time\".  In this case, each row of tile "
           "processing engines will be cleaned up immediately after all rows "
           "have been pushed rather than waiting for the next row of tiles "
           "to be finished before cleaning up the previous row of tile "
           "engines (the default).\n"
           "\t   For maximum processing throughput on systems with "
           "a large number of CPU cores or hardware threads, it is best "
           "if the \"-max_height\" argument is large enough to accommodate "
           "the pushing of stripes that correspond to whole tile rows and "
           "then the \"-double_buffering\" value should ideally be large "
           "enough to allow the multi-threaded DWT buffers associated with "
           "each tile to store the entire tile's data; this means that "
           "\"-double_buffering\" should be at least half the tile height.  "
           "Of course, these parameters would not be appropriate if tiles "
           "are very large or the image has only one tile.  While it is "
           "possible to manually select all these parameters, you are "
           "usually best off omitting both \"-max_height\" and "
           "\"-double_buffering\" arguments, letting the internal machinery "
           "select suitable default values, possibly in a dynamic manner.\n"
           "\t   The default value for \"-tile_concurrency\" is 0, which "
           "lets the `kdu_stripe_compressor' object select good levels of "
           "tile concurrency automatically.  This may well produce the best "
           "throughput, but you should be prepared to play around to find "
           "the best parameter for a particular platform and compression "
           "configuration.\n";
  out << "-foreground_io -- read image files in primary thread\n";
  if (comprehensive)
    out << "\tBy default, in multi-threaded applications, except where the "
           "entire image is read in ahead of time for vertical replication "
           "(see \"-vrep\"), the image files are read in a background thread "
           "so that the primary thread can devote all its effort to pushing "
           "imagery into the `kdu_stripe_compressor::push_stripe' function.  "
           "Use this argument to revert to reading and pushing stripes "
           "sequentially rather than concurrently.  The only reason you might "
           "want to do this would be to reduce memory usage, since background "
           "reading of imagery requires two sets of stripe buffers.\n";
  out << "-cpu -- report processing CPU time\n";
  out << "-stats -- report compressed size, buffering and R-D slope stats\n";
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

static kd_source_file *
  parse_simple_args(kdu_args &args, char * &ofname, int &vertical_replicas,
                    float &max_rate, float &min_rate, double &rate_tolerance,
                    kdu_uint16 &min_slope, int &preferred_min_stripe_height,
                    int &absolute_max_stripe_height, int &flush_period,
                    bool &force_precise, bool &want_fastest, bool &no_weights,
                    int &num_threads, int &double_buffering_height,
                    int &tile_concurrency, bool &multi_threaded_io,
                    bool &cpu, bool &stats, bool &quiet)
  /* Parses all command line arguments whose names include a dash.  Returns
     a list of open input files. */
{
  if ((args.get_first() == NULL) || (args.find("-u") != NULL))
    print_usage(args.get_prog_name());
  if (args.find("-usage") != NULL)
    print_usage(args.get_prog_name(),true);
  if ((args.find("-version") != NULL) || (args.find("-v") != NULL))
    print_version();      

  ofname = NULL;
  vertical_replicas = 1;
  max_rate = min_rate = -1.0F;
  rate_tolerance = 0.02;
  min_slope = 0;
  preferred_min_stripe_height = 16;
  absolute_max_stripe_height = 1024;
  flush_period = 0;
  force_precise = want_fastest = false;
  no_weights = false;
  num_threads = 0; // This is not actually the default -- see below.
  double_buffering_height = -1; // Select automatic policy
  tile_concurrency = -1; // Select automatic policy
  multi_threaded_io = true;
  cpu = false;
  stats = false;
  quiet = false;
  bool little_endian = false;
  kd_source_file *fhead=NULL, *ftail=NULL;

  if (args.find("-o") != NULL)
    {
      const char *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-o\" argument requires a file name!"; }
      ofname = new char[strlen(string)+1];
      strcpy(ofname,string);
      args.advance();
    }
  
  if (args.find("-vrep") != NULL)
    { 
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&vertical_replicas) != 1) ||
          (vertical_replicas < 1))
        { kdu_error e; e << "\"-vrep\" argument requires a positive "
          "integer parameter."; }
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
        { kdu_warning w; w << "\"-double_buffering\" argument ignored in "
          "single-threaded processing mode (use \"-num_threads\" to "
          "force the use of a multi-threaded processing argument)."; }
      else
        { 
          char *string = args.advance();
          if ((string == NULL) ||
              (sscanf(string,"%d",&double_buffering_height) != 1) ||
              (double_buffering_height < 0))
            { kdu_error e; e << "\"-double_buffering\" argument requires a "
              "positive integer, specifying the number of rows from each "
              "component which are to be double buffered, or else 0 (see "
              "`-usage' statement)."; }
        }
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
          "`-min_height' argument (defaults to 8)."; }
      args.advance();
    }
  if (preferred_min_stripe_height > absolute_max_stripe_height)
    absolute_max_stripe_height = preferred_min_stripe_height;

  if (args.find("-rate") != NULL)
    {
      const char *string = args.advance();
      if (string == NULL)
        { kdu_error e;
          e << "\"-rate\" argument requires a parameter string!"; }
      bool valid = false;
      if ((*string == '-') && (string[1] == ',') &&
          (sscanf(string+2,"%f",&min_rate) == 1) && (min_rate > 0.0F))
        valid = true;
      else if ((*string == '-') && (string[1] == '\0'))
        valid = true;
      else if ((sscanf(string,"%f,%f",&max_rate,&min_rate) == 2) &&
               (min_rate > 0.0F) && (max_rate > min_rate))
        valid = true;
      else if (sscanf(string,"%f",&max_rate) == 1)
        valid = true;
      if (!valid)
        { kdu_error e; e << "\"-rate\" argument has an invalid parameter "
          "string; you must specify either one or two rate tokens, "
          "corresponding to maximum and minimum bit-rates (in order), over "
          "which to allocate the quality layers.  The maximum rate spec may "
          "be replaced by a '-' character, meaning use all available bits.  "
          "The minimum rate spec, if missing, will be automatically created.  "
          "Both parameters must be strictly positive if supplied."; }
      args.advance();
    }
  if (args.find("-slope") != NULL)
    { 
      int slope_val=0;
      const char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&slope_val) != 1) ||
          (slope_val < 1) || (slope_val > 65535))
        { kdu_error e; e << "\"-slope\" argument requires an integer "
          "parameter in the range 1 to 65535."; }
      min_slope = (kdu_uint16) slope_val;
      if ((max_rate > 0.0) || (min_rate > 0.0))
        { kdu_error e; e << "You may not supply both \"-rate\" and \"-slope\" "
          "arguments."; }
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
      args.advance();
    }

  if (args.find("-flush_period") != NULL)
    {
      char *string = args.advance();
      if ((string == NULL) || (sscanf(string,"%d",&flush_period) != 1) ||
          (flush_period < 128))
        { kdu_error e; e << "\"-flush_period\" argument requires a positive "
          "integer, no smaller than 128.  Typical values will generally be "
          "in the thousands; incremental flushing has no real benefits, "
          "except when the image is large."; }
      args.advance();
    }
  
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
  if (args.find("-no_weights") != NULL)
    { 
      no_weights = true;
      args.advance();
    }

  if (args.find("-i") != NULL)
    { 
      const char *delim, *string = args.advance();
      if (string == NULL)
        { kdu_error e; e << "\"-i\" argument requires a parameter string."; }
      for (; *string != '\0'; string=delim)
        {
          while (*string == ',') string++;
          for (delim=string; (*delim != '\0') && (*delim != ','); delim++);
          kd_source_file *file = new kd_source_file();
          if (ftail == NULL)
            fhead = ftail = file;
          else
            ftail = ftail->next = file;
          file->fname = new char[(delim-string)+1];
          strncpy(file->fname,string,delim-string);
          file->fname[delim-string] = '\0';
          if ((file->fp = fopen(file->fname,"rb")) == NULL)
            { kdu_error e;
              e << "Unable to open input file, \"" << file->fname << "\"."; }
        }
      args.advance();
    }

  if (fhead == NULL)
    { kdu_error e; e << "You must supply at least one input file"; }

  // Go through file list, setting `is_raw', `swap_bytes', component
  // indices, and reading PNM headers
  int num_comps=0;
  bool have_bmp=false, have_non_bmp=false;
  for (ftail=fhead; ftail != NULL;
       num_comps=ftail->lim_comp_idx, ftail=ftail->next)
    { 
      ftail->first_comp_idx = num_comps;
      ftail->samples_per_pel = 1; // Until proven otherwise
      ftail->is_bmp = ftail->swap_bytes = false;
      const char *delim = strrchr(ftail->fname,'.');
      ftail->is_raw = true; // Until proven otherwise
      if ((delim != NULL) && (toupper(delim[1]) == (int) 'B') &&
          (toupper(delim[2]) == (int) 'M') && (toupper(delim[3]) == (int) 'P'))
        { 
          ftail->is_raw = false;
          ftail->is_bmp = true;
          have_bmp = true;
          ftail->read_bmp_header();
        }
      else if ((delim != NULL) && (toupper(delim[1]) == (int) 'P') &&
               ((toupper(delim[2]) == (int) 'G') ||
                (toupper(delim[2]) == (int) 'P')) &&
               (toupper(delim[3]) == (int) 'M'))
        { 
          ftail->is_raw = false;
          have_non_bmp = true;
          ftail->read_pnm_header();
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
      ftail->lim_comp_idx = num_comps + ftail->samples_per_pel;
    }
  if (have_bmp && have_non_bmp)
    { kdu_error e; e << "Either all of the input files supplied to \"-i\" "
      "must be BMP files (suffix ending in \".bmp\") or else none of them "
      "may be BMP files."; }
  return fhead;
}

/*****************************************************************************/
/* STATIC                  set_default_colour_weights                        */
/*****************************************************************************/

static void
  set_default_colour_weights(kdu_params *siz, bool quiet)
{
  kdu_params *cod = siz->access_cluster(COD_params);
  assert(cod != NULL);
  
  float weight;
  if (cod->get(Clev_weights,0,0,weight) ||
      cod->get(Cband_weights,0,0,weight))
    return; // Weights already specified explicitly.
  bool can_use_ycc = true;
  bool rev0=false;
  int depth0=0, sub_x0=1, sub_y0=1;
  for (int c=0; c < 3; c++)
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
  if (!can_use_ycc)
    return;
  
  bool use_ycc;
  if (!cod->get(Cycc,0,0,use_ycc))
    cod->set(Cycc,0,0,use_ycc=true);
  if (!use_ycc)
    return;
  
  /* These example weights are adapted from numbers generated by Marcus Nadenau
   at EPFL, for a viewing distance of 15 cm and a display resolution of
   300 DPI. */
  
  cod->parse_string("Cband_weights:C0="
                    "{0.0901},{0.2758},{0.2758},"
                    "{0.7018},{0.8378},{0.8378},{1}");
  cod->parse_string("Cband_weights:C1="
                    "{0.0263},{0.0863},{0.0863},"
                    "{0.1362},{0.2564},{0.2564},"
                    "{0.3346},{0.4691},{0.4691},"
                    "{0.5444},{0.6523},{0.6523},"
                    "{0.7078},{0.7797},{0.7797},{1}");
  cod->parse_string("Cband_weights:C2="
                    "{0.0773},{0.1835},{0.1835},"
                    "{0.2598},{0.4130},{0.4130},"
                    "{0.5040},{0.6464},{0.6464},"
                    "{0.7220},{0.8254},{0.8254},"
                    "{0.8769},{0.9424},{0.9424},{1}");
  if (!quiet)
    pretty_cout << "Note:\n\tThe default rate control policy for colour "
                   "images employs visual (CSF) weighting factors.  To "
                   "minimize MSE instead, specify `-no_weights'.\n";
}

/*****************************************************************************/
/* STATIC                      check_jp2_suffix                              */
/*****************************************************************************/

static bool
  check_jp2_suffix(const char *fname)
  /* Returns true if the file-name has the suffix, ".jp2", where the
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
  if (*cp != '2')
    return false;
  return true;
}


/* ========================================================================= */
/*                    Compression Workflow Demonstrations                    */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                  compress_single_threaded                          */
/*****************************************************************************/

static void
  compress_single_threaded(kdu_stripe_compressor &compressor,
                           int preferred_min_stripe_height,
                           int absolute_max_stripe_height,
                           int stripe_heights[], int max_stripe_heights[],
                           int sample_gaps[], int row_gaps[],
                           int precisions[], bool is_signed[],
                           kdu_byte *stripe_bufs8[],
                           kdu_int16 *stripe_bufs16[],
                           kdu_byte *image_bufs8[],
                           kdu_int16 *image_bufs16[],
                           kd_source_file *in_files, kdu_long total_samples,
                           int flush_period, bool cpu, bool quiet)
  /* There are three versions of the main compression process, split out to
     make it easier for you to see what is going on.  This one is intended for
     use with single-threaded processing.  The `compress_multi_threaded'
     version is similar, except that it makes no sense to separately measure
     and subtract the amount of CPU time spent reading in imagery from files
     because such file I/O is likely to be overlapped by background processing
     in other threads.  The final version is `compress_multi_threaded_io',
     which explicitly moves the image file reading logic to a separate thread,
     with double buffering of the stripe buffers, so as to minimize the risk
     that processing is held up by disk I/O. */
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0, reading_time=0.0;
  
  // Load up the input files up front if vertical replication was requested
  kd_source_file *in;
  if (image_bufs8 || image_bufs16)
    { // Pre-read the entire image.  In this application, we do this only
      // when generating vertically replicated compressed images -- this
      // allows us to completely factor out the cost of reading the image and
      // focus solely on the amount of CPU time required to do the actual
      // compression of a (potentially) very large image.  In many applications
      // you will already have the entire image sitting inside a memory
      // buffer.
      if (cpu && !quiet)
        pretty_cout << "Pre-buffering all input files ...\n";
      kdu_long initial_load_bytes = 0;
      for (in=in_files; in != NULL; in = in->next)
        { 
          initial_load_bytes += in->read_stripe(in->size.y);
          assert(in->size.y == 0);
          in->size.y = in->buffered_lines; // To walk the stripe buffer
             // repeatedly the image buffer we will use `buffered_lines' as
             // a position variable and `size.y' as the original height.
        }
      if (cpu)
        { 
          reading_time = timer.get_ellapsed_seconds();
          if (!quiet)
            pretty_cout << "   Initial file read time (" << initial_load_bytes
                        << " bytes) = " << reading_time << " s\n";
          timer.reset();
        }
    }
  
  // Do the incremental stripe processing
  do { 
    compressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                              absolute_max_stripe_height,
                                              stripe_heights,NULL);
    if (image_bufs8 || image_bufs16)
      { // Configure stripe bufs to point into the relevant image bufs
        for (in=in_files; in != NULL; in=in->next)
          { 
            int max_h = in->size.y - in->buffered_lines;
            if (max_h == 0)
              { // Go back to the start of the replicated buffer
                in->buffered_lines = 0;
                max_h = in->size.y;
              }
            int stripe_offset = in->buf_row_gap * in->buffered_lines;
            int n = in->first_comp_idx;
            if (max_h > stripe_heights[n])
              max_h = stripe_heights[n];
            in->buffered_lines += max_h;
            for (; n < in->lim_comp_idx; n++)
              { 
                stripe_heights[n] = max_h;
                if (stripe_bufs8 != NULL)
                  stripe_bufs8[n] = image_bufs8[n] + stripe_offset;
                else
                  stripe_bufs16[n] = image_bufs16[n] + stripe_offset;
              }
          }
      }
    else
      { // Load the next stripe of imagery from the input files.  This is
        // a typical application for stripe-oriented compression, allowing
        // minimal buffering of the input files in stripes, with interleaved
        // reading and processing of the stripe data.
        if (cpu)
          processing_time += timer.get_ellapsed_seconds();
        kdu_long load_bytes = 0;
        for (in=in_files; in != NULL; in=in->next)
          { 
            int n = in->first_comp_idx;
            assert(stripe_heights[n] <= max_stripe_heights[n]);
            load_bytes += in->read_stripe(stripe_heights[n]);
          }
        if (cpu)
          reading_time += timer.get_ellapsed_seconds();
      }
  } while (((stripe_bufs8 != NULL) &&
            compressor.push_stripe(stripe_bufs8,stripe_heights,
                                   sample_gaps,row_gaps,precisions,
                                   flush_period)) ||
           ((stripe_bufs16 != NULL) &&
            compressor.push_stripe(stripe_bufs16,stripe_heights,
                                   sample_gaps,row_gaps,precisions,
                                   is_signed,flush_period)));
            
  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = total_samples / processing_time;
      pretty_cout << "Processing time = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second << " Msamples/s\n";
      pretty_cout << "End-to-end time (including file reading) = "
                  << processing_time + reading_time << " s.\n";
    }
}

/*****************************************************************************/
/* STATIC                   compress_multi_threaded                          */
/*****************************************************************************/

static void
  compress_multi_threaded(kdu_stripe_compressor &compressor,
                          int preferred_min_stripe_height,
                          int absolute_max_stripe_height,
                          int stripe_heights[], int max_stripe_heights[],
                          int sample_gaps[], int row_gaps[],
                          int precisions[], bool is_signed[],
                          kdu_byte *stripe_bufs8[],
                          kdu_int16 *stripe_bufs16[],
                          kdu_byte *image_bufs8[],
                          kdu_int16 *image_bufs16[],
                          kd_source_file *in_files, kdu_long total_samples,
                          int flush_period, bool cpu, bool quiet,
                          int num_threads)
  /* There are three versions of the main compression process in this
     demo app, split out to help you understand what is going on.  This one
     is almost identical to `compress_single_threaded', except that we do
     not bother separately measuring the time taken for incremental file
     reading operations since this cannot be legitimately subtracted from
     the overall processing time, given that there are background worker
     threads that have the potential to keep processing while the file
     reading operation is going on.  Only the very initial file reading
     operation can legitimately be removed from the overall compression
     processing time.
        Nevertheless, while the main thread is waiting to read an incremental
     stripe of input data from the image files, it may happen that background
     working threads begin to run out of things to do.  This is more
     likely when generating tiled codestreams, if the `tile_concurrency'
     argument passed to `kdu_stripe_compressor::start' was smaller than
     the number of tiles across the image.  To enable maximum thread
     concurrency with small values of `tile_concurrency' (better for cache
     localized processing), we recommend reading the image stripes on a
     separate thread and periodically exchanging them with the main thread
     that pushes them into the `kdu_stripe_compressor'.  This is exactly what
     the third processing demonstration does, embodied by the
     `compress_multi_threaded_io' function. */
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0, reading_time=0.0;

  // Load up the input files up front if vertical replication was requested
  kd_source_file *in;
  kdu_long initial_load_bytes = 0;
  if (image_bufs8 || image_bufs16)
    { // Pre-read the entire image.  In this application, we do this only
      // when generating vertically replicated compressed images -- this
      // allows us to completely factor out the cost of reading the image and
      // focus solely on the amount of CPU time required to do the actual
      // compression of a (potentially) very large image.  In many applications
      // you will already have the entire image sitting inside a memory
      // buffer.
      if (cpu && !quiet)
        pretty_cout << "Pre-buffering all input files ...\n";
      for (in=in_files; in != NULL; in = in->next)
        { 
          initial_load_bytes += in->read_stripe(in->size.y);
          assert(in->size.y == 0);
          in->size.y = in->buffered_lines; // To walk the stripe buffer
             // repeatedly the image buffer we will use `buffered_lines' as
             // a position variable and `size.y' as the original height.
        }
      if (cpu)
        { 
          reading_time = timer.get_ellapsed_seconds();
          if (!quiet)
            pretty_cout << "   Initial file read time (" << initial_load_bytes
                        << " bytes) = " << reading_time << " s\n";
          timer.reset();
        }
    }

  // Do the incremental stripe processing
  do { 
    compressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                              absolute_max_stripe_height,
                                              stripe_heights,NULL);
    if (image_bufs8 || image_bufs16)
      { // Configure stripe bufs to point into the relevant image bufs
        for (in=in_files; in != NULL; in=in->next)
          { 
            int max_h = in->size.y - in->buffered_lines;
            if (max_h == 0)
              { // Go back to the start of the replicated buffer
                in->buffered_lines = 0;
                max_h = in->size.y;
              }
            int stripe_offset = in->buf_row_gap * in->buffered_lines;
            int n = in->first_comp_idx;
            if (max_h > stripe_heights[n])
              max_h = stripe_heights[n];
            in->buffered_lines += max_h;
            for (; n < in->lim_comp_idx; n++)
              { 
                stripe_heights[n] = max_h;
                if (stripe_bufs8 != NULL)
                  stripe_bufs8[n] = image_bufs8[n] + stripe_offset;
                else
                  stripe_bufs16[n] = image_bufs16[n] + stripe_offset;
              }
          }
      }
    else
      { // Load the next stripe of imagery from the input files.  This is
        // a typical application for stripe-oriented compression, allowing
        // minimal buffering of the input files in stripes, with interleaved
        // reading and processing of the stripe data.
        if (cpu && (initial_load_bytes == 0) && !quiet)
          pretty_cout << "Pre-buffering initial stripe from input files ...\n";
        kdu_long load_bytes = 0;
        for (in=in_files; in != NULL; in=in->next)
          { 
            int n = in->first_comp_idx;
            assert(stripe_heights[n] <= max_stripe_heights[n]);
            load_bytes += in->read_stripe(stripe_heights[n]);
          }
        if (initial_load_bytes == 0)
          { 
            initial_load_bytes = load_bytes;
            if (cpu)
              { 
                reading_time = timer.get_ellapsed_seconds();
                if (!quiet)
                  pretty_cout << "Initial file read time (pre-buffered "
                              << initial_load_bytes << " bytes) = "
                              << reading_time << " s\n";
                timer.reset();
              }
          }
      }
  } while (((stripe_bufs8 != NULL) &&
            compressor.push_stripe(stripe_bufs8,stripe_heights,
                                   sample_gaps,row_gaps,precisions,
                                   flush_period)) ||
           ((stripe_bufs16 != NULL) &&
            compressor.push_stripe(stripe_bufs16,stripe_heights,
                                   sample_gaps,row_gaps,precisions,
                                   is_signed,flush_period)));

  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = total_samples / processing_time;
      pretty_cout << "End-to-end time (includes non-initial file reads) = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second << " Msamples/s\n";
      pretty_cout << "Processed using the multi-threaded environment, with\n"
                  << "    "  << num_threads
                  << " parallel threads of execution (see `-num_threads')\n";
    }
}

/*****************************************************************************/
/* STATIC                 compress_multi_threaded_io                         */
/*****************************************************************************/

    struct reader_thread_state {
        kdu_thread thread;
        kdu_semaphore free_sets; // Num stripe sets available to file reader
        kdu_semaphore full_sets; // Num stripe sets available for push
        kd_source_file *in_files;
        int *stripe_heights[2]; // Entry becomes NULL if corresponding stripe
                                // set lies beyond the input image
        void close()
         { thread.destroy(); free_sets.destroy(); full_sets.destroy(); }
      };

    static kdu_thread_startproc_result
      KDU_THREAD_STARTPROC_CALL_CONVENTION reader_thread_entry(void *param)
      { 
        reader_thread_state *state = (reader_thread_state *) param;
        int priority, min_priority, max_priority;
        priority = state->thread.get_priority(min_priority,max_priority);
        if (priority < max_priority)
          state->thread.set_priority(priority+1);
        int read_set = 1; // We always start out reading into stripe set 1
        while (state->free_sets.wait() &&
               (state->stripe_heights[read_set] != NULL))
          { 
            int *heights = state->stripe_heights[read_set];
            for (kd_source_file *in=state->in_files; in != NULL; in=in->next)
              { 
                in->swap_buffers();
                in->read_stripe(heights[in->first_comp_idx]);
              }
            state->full_sets.signal();
            read_set = 1 - read_set;
          }
        return KDU_THREAD_STARTPROC_ZERO_RESULT;
      }

static void
  compress_multi_threaded_io(kdu_stripe_compressor &compressor,
                             int preferred_min_stripe_height,
                             int absolute_max_stripe_height,
                             int stripe_heights[],
                             int alt_stripe_heights[],
                             int max_stripe_heights[],
                             int sample_gaps[], int row_gaps[],
                             int precisions[], bool is_signed[],
                             kdu_byte *stripe_bufs8[],
                             kdu_byte *alt_stripe_bufs8[],
                             kdu_int16 *stripe_bufs16[],
                             kdu_int16 *alt_stripe_bufs16[],
                             kd_source_file *in_files, kdu_long total_samples,
                             int flush_period, bool cpu, bool quiet,
                             int num_threads)
  /* There are three versions of the compression processing workflow, split
     out to make it easier for you to see what is going on and adopt patterns
     that are relevant to your application.  This one is similar to 
     `compress_multi_threaded', except that it creates a separate thread
     for incrementally reading image stripes (except for the first one) from
     disk so that the main thread can push the previously read stripe into
     `kdu_stripe_compressor', devoting all of its effort to keeping all
     processing activities alive.  If the throughput is not held up by
     disk I/O, the separate disk reading thread will be blocked most of
     the time so it is not consuming a physical hardware thread.  In fact,
     we expect this disk reading thread to be blocked almost always, either
     waiting for the I/O itself to complete or else waiting for a new stripe
     to become available for reading.
        The main drawback of the approach here is that there need to be
     two copies of each stripe buffer, one that is being processed by the
     main thread, while another is concurrently available to the disk
     reading thread.
        This function does not bother handling the case in which the full
     image is pre-buffered (used for the vertical replication case) since
     that would be identical to the `compress_multi_threaded' version of
     the processing workflow.
        The `stripe_heights', `stripe_bufs8' and `stripe_bufs16' arrays
     are supplemented by `alt_stripe_heights', `alt_stripe_bufs8' and
     `alt_stripe_bufs16' that together describe two sets of stripe buffers,
     so that the file reading thread can be writing to one set while the
     main thread is pushing the other set to `kdu_stripe_compressor::push'. */
{
  // Start the timer
  kdu_clock timer;
  double processing_time=0.0, reading_time=0.0;

  int cur_set=0; // Working set of stripe parameters alternates between 0 and 1
  
  // Load up the first stripe up front
  compressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                            absolute_max_stripe_height,
                                            stripe_heights,NULL);
  if (cpu && !quiet)
    pretty_cout << "Pre-buffering initial stripe from input files ...\n";
  kdu_long load_bytes = 0;
  for (kd_source_file *in=in_files; in != NULL; in=in->next)
    load_bytes +=
      in->read_stripe(stripe_heights[in->first_comp_idx]);
  if (cpu)
    { 
      reading_time = timer.get_ellapsed_seconds();
      if (!quiet)
        pretty_cout << "Initial file read time (pre-buffered "
                    << load_bytes << " bytes) = " << reading_time << " s\n";
      timer.reset();
    }

  // Create and launch the file reading machinery
  reader_thread_state reader;
  reader.free_sets.create(1);
  reader.full_sets.create(0);
  reader.in_files = in_files;
  reader.stripe_heights[0] = stripe_heights;
  reader.stripe_heights[1] = alt_stripe_heights;
  kdu_byte **bufs8[2]={stripe_bufs8,alt_stripe_bufs8};
  kdu_int16 **bufs16[2]={stripe_bufs16,alt_stripe_bufs16};
  int *heights[2]={stripe_heights,alt_stripe_heights};
  if (compressor.get_next_stripe_heights(preferred_min_stripe_height,
                                         absolute_max_stripe_height,
                                         stripe_heights,alt_stripe_heights))
    { // Launch file reader thread
      if (!reader.thread.create(reader_thread_entry,&reader))
        { kdu_error e; e << "Unable to start background file reader thread!"; }
    }
  while (((stripe_bufs8 != NULL) &&
          compressor.push_stripe(bufs8[cur_set],heights[cur_set],sample_gaps,
                                 row_gaps,precisions,flush_period)) ||
         ((stripe_bufs16 != NULL) &&
          compressor.push_stripe(bufs16[cur_set],heights[cur_set],sample_gaps,
                                 row_gaps,precisions,is_signed,flush_period)))
    { 
      if (!compressor.get_next_stripe_heights(preferred_min_stripe_height,
                                              absolute_max_stripe_height,
                                              heights[1-cur_set],
                                              heights[cur_set]))
        reader.stripe_heights[cur_set] = NULL; // Causes reader thread to exit
      cur_set = 1 - cur_set; // Swap sets
      reader.free_sets.signal();
      reader.full_sets.wait();
    }
  
  if (cpu)
    { // Report processing time
      processing_time += timer.get_ellapsed_seconds();
      double samples_per_second = total_samples / processing_time;
      pretty_cout << "End-to-end time (includes non-initial file reads) = "
                  << processing_time << " s;\n   i.e., "
                  << 0.000001*samples_per_second << " Msamples/s\n";
      pretty_cout << "Processed using the multi-threaded environment, with\n"
                  << "    "  << num_threads
                  << " parallel threads of execution (see `-num_threads')\n";
    }
  
  reader.close();
}


/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/*                                   main                                    */
/*****************************************************************************/

int main(int argc, char *argv[])
{
  pretty_cout.set_master_indent(4);
  kdu_customize_warnings(&pretty_cout);
  kdu_customize_errors(&pretty_cerr);
  kdu_args args(argc,argv,"-s");

  // Parse simple arguments from command line
  char *ofname;
  int vertical_replicas; // Guaranteed to be >= 1
  bool multi_threaded_io;
  float min_rate, max_rate;
  kdu_uint16 target_slope;
  double rate_tolerance;
  int preferred_min_stripe_height, absolute_max_stripe_height;
  int num_threads, env_dbuf_height, tile_concurrency, flush_period;
  bool force_precise, want_fastest, no_weights, cpu, stats, quiet;
  kd_source_file *in_files =
    parse_simple_args(args,ofname,vertical_replicas,
                      max_rate,min_rate,rate_tolerance,target_slope,
                      preferred_min_stripe_height,
                      absolute_max_stripe_height,flush_period,
                      force_precise,want_fastest,no_weights,
                      num_threads,env_dbuf_height,tile_concurrency,
                      multi_threaded_io,cpu,stats,quiet);

  // Create appropriate output file
  kdu_compressed_target *output = NULL;
  kdc_null_target null_out;
  kdu_simple_file_target file_out;
  jp2_family_tgt jp2_ultimate_tgt;
  jp2_target jp2_out;
  if (ofname == NULL)
    output = &null_out;
  else if (check_jp2_suffix(ofname))
    {
      output = &jp2_out;
      jp2_ultimate_tgt.open(ofname);
      jp2_out.open(&jp2_ultimate_tgt);
    }
  else
    {
      output = &file_out;
      file_out.open(ofname);
    }
  if (ofname != NULL)
    { delete[] ofname; ofname = NULL; }
  
  // Collect any dimensioning/tiling parameters supplied on the command line;
  // need dimensions for raw files, if any.
  siz_params siz;
  const char *string;
  for (string=args.get_first(); string != NULL; )
    string = args.advance(siz.parse_string(string));

  // Set input file dimensions (either by reading header, or using `siz')
  // This looks a little complex, only because we want to allow for
  // multi-component transforms, as defined in JPEG2000 Part 2.  A
  // multi-component transform is being used if the `Mcomponents' attribute
  // is defined and greater than 0.  In this case, `Mcomponents' identifies
  // the set of image components that will be decoded after applying the
  // multi-component transform to the `Scomponents' codestream components.
  //    During compression, we supply `num_components' source components to
  // the `kdu_stripe_compressor' object, where `num_components' is allowed to
  // be less than `Mcomponents' if we believe that the multi-component
  // transform network can be inverted (this is done automatically by
  // `kdu_multi_analysis' on top of which `kdu_stripe_compressor' is built)
  // to produce the `Scomponents' codestream components from the
  // `num_components' supplied source components.  These source components
  // correspond to the initial `num_components' components reconstructed by
  // the decompressor, out of the total `Mcomponents'.  This is why the code
  // below involves three different component counts (`m_components',
  // `c_components' and `num_components').
  //    For Part-1 codestreams, `Mcomponents' is 0 and `num_components' and
  // `c_components' are identical.  In this case, `Scomponents' can be derived
  // simply by counting files.
  //    For Part-2 codestreams, `Mcomponents' is greater than 0 and
  // `Scomponents' must be explicitly set by the application (or by parsing the
  // command line).  If you have `Mcomponents' > 0 and no defined value for
  // `Scomponents', the default `Scomponents' value is set to
  // `num_components' (i.e., to the number of source files).
  //    For all cases, we follow the recommendation documented with the
  // `siz_params' object, according to which precision and signed/unsigned
  // properties of each original image component should be specified via
  // `Nprecision' and `Nsigned' attributes, leaving `Mprecision'/`Msigned'
  // and/or `Sprecision'/`Ssigned' to be determined automatically or explicitly
  // set to accommodate specific attributes of a Part-2 multi-component
  // transform or non-linear point transform.
  int m_components=0;  siz.get(Mcomponents,0,0,m_components);
  kdu_long total_samples=0, total_pixels=0;
  int n, num_components=0;
  bool read_bytes=true; // Until we find a file that requires more precision
  bool flip_vertically=false; // Until we encounter a BMP file
  kd_source_file *in;
  for (in=in_files; in != NULL; num_components++)
    { 
      kdu_coords comp_size;
      if (in->is_raw)
        { 
          if ((!(siz.get(Sdims,num_components,0,comp_size.y) &&
                 siz.get(Sdims,num_components,1,comp_size.x))) ||
              (!(siz.get(Nsigned,num_components,0,in->is_signed) &&
                 siz.get(Nprecision,num_components,0,in->precision))) ||
              (in->precision > 16))
            { kdu_error e; e << "Raw file, \"" << in->fname << "\" supplied "
              "on command line has no matching dimension parameters; these "
              "must be provided via the `Sdims', `Nprecision' and `Nsigned' "
              "arguments -- you must specify all three attributes."; }
          in->bytes_per_sample = (in->precision > 8)?2:1;
          if (in->bytes_per_sample > 1)
            read_bytes = false;
          
          if ((comp_size.y % vertical_replicas) != 0)
            { kdu_error e; e << "You are using one or more raw files "
              "together with the \"-vrep\" (vertical replication) option.  "
              "In this case, the vertical dimension provided via the `Sdims' "
              "attribute must be divisible by the supplied replication "
              "factor."; }
          in->size = comp_size;
          in->size.y /= vertical_replicas;
        }
      else
        { // In this case, the image dimensions are known from the header
          comp_size = in->size;
          comp_size.y *= vertical_replicas;
          siz.set(Sdims,num_components,0,comp_size.y);
          siz.set(Sdims,num_components,1,comp_size.x);
          siz.set(Nsigned,num_components,0,in->is_signed=false);
          siz.set(Nprecision,num_components,0,in->precision=8);
          in->bytes_per_sample = 1;
        }
      kdu_long samples = comp_size.x; samples *= comp_size.y;
      total_samples += samples;
      total_pixels = (samples > total_pixels)?samples:total_pixels;
      if (in->bytes_per_sample > 1)
        read_bytes = false;
      if (in->is_bmp)
        flip_vertically = true;
      if ((num_components+1) == in->lim_comp_idx)
        in = in->next;
    }
  int c_components=0;
  if (!siz.get(Scomponents,0,0,c_components))
    siz.set(Scomponents,0,0,c_components=num_components);
  siz.finalize_all();

  // Construct the `kdu_codestream' object and parse all remaining args
  kdu_codestream codestream;
  codestream.create(&siz,output);
  for (string=args.get_first(); string != NULL; )
    string = args.advance(codestream.access_siz()->parse_string(string));
  if (args.show_unrecognized(pretty_cout) != 0)
    { kdu_error e; e << "There were unrecognized command line arguments!"; }
  if ((c_components >= 3) && (m_components == 0) && (!no_weights))
    set_default_colour_weights(codestream.access_siz(),quiet);
  codestream.access_siz()->finalize_all();

  // Write the JP2 header, if necessary
  if (jp2_ultimate_tgt.exists())
    { // Do minimal JP2 file initialization, for demonstration purposes
      jp2_dimensions dimensions = jp2_out.access_dimensions();
      dimensions.init(codestream.access_siz());
      dimensions.finalize_compatibility(codestream.access_siz());
           // There is no need to actually call the above function here,
           // since the `init' function was invoked using the already
           // finalized parameter sub-system.  However, if your application
           // needs to initialize the `jp2_dimensions' object using only the
           // siz information (as in "kdu_compress") you really should later
           // call `jp2_dimensions::finalize_compatibility' once you have
           // created the codestream and finalized the parameter sub-system.
      jp2_colour colour = jp2_out.access_colour();
      colour.init((num_components>=3)?JP2_sRGB_SPACE:JP2_sLUM_SPACE);
      jp2_out.write_header();
           // If you want to write additional JP2 boxes, this is the place to
           // do it.  For an example, refer to the `write_extra_jp2_boxes'
           // function in the "kdu_compress" demo application.
      jp2_out.open_codestream(true);
    }
  
  // Flip the compression direction if BMP files are in use
  if (flip_vertically)
    codestream.change_appearance(false,true,false);

  // Determine the desired cumulative layer sizes
  int num_layer_specs;
  kdu_params *cod = codestream.access_siz()->access_cluster(COD_params);
  if (!(cod->get(Clayers,0,0,num_layer_specs) && (num_layer_specs > 0)))
    cod->set(Clayers,0,0,num_layer_specs=1);
  kdu_long *layer_sizes = new kdu_long[num_layer_specs];
  kdu_uint16 *layer_slopes = new kdu_uint16[num_layer_specs];
  memset(layer_sizes,0,sizeof(kdu_long)*num_layer_specs);
  memset(layer_slopes,0,sizeof(kdu_uint16)*num_layer_specs);
  if ((min_rate > 0.0F) && (num_layer_specs < 2))
    { kdu_error e; e << "You have specified two bit-rates using the `-rate' "
      "argument, but only one quality layer.  Use `Clayers' to specify more "
      "layers -- they will be spaced logarithmically between the min and max "
      "bit-rates."; }
  if (target_slope > 0)
    layer_slopes[num_layer_specs-1] = target_slope;
  else
    { 
      if (min_rate > 0.0F)
        layer_sizes[0] = (kdu_long)(total_pixels*min_rate*0.125F);
      if (max_rate > 0.0F)
        layer_sizes[num_layer_specs-1] =
          (kdu_long)(total_pixels*max_rate*0.125);
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
  // `env.handle_exception(exc)', where `exc' is the exception code which you
  // catch, of type `kdu_exception'.  Even this is not necessary if you are
  // happy for the `kdu_thread_env' object to be destroyed when an
  // error/exception occurs.
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
  kdu_byte **buf_handles = new kdu_byte *[num_components]; // For dealloc
  for (n=0; n < num_components; n++)
    buf_handles[n] = NULL;
  kdu_int16 **stripe_bufs16=NULL;
  kdu_byte **stripe_bufs8=NULL;
  if (read_bytes)
    stripe_bufs8 = new kdu_byte *[num_components];
  else
    stripe_bufs16 = new kdu_int16 *[num_components];  
  
  // If vertical replication is being used, create whole image buffers into
  // which the image files will be imported a priori.
  kdu_int16 **image_bufs16=NULL;
  kdu_byte **image_bufs8=NULL;
  if (vertical_replicas > 1)
    { 
      multi_threaded_io = false;
      if (read_bytes)
        image_bufs8 = new kdu_byte *[num_components];
      else
        image_bufs16 = new kdu_int16 *[num_components];
    }
  
  // If double-buffered I/O is being used, create alternate stripe buffers
  // so that a background thread can be reading into one set of stripes
  // while the foreground thread processes another.
  int *alt_stripe_heights=NULL;
  kdu_int16 **alt_stripe_bufs16=NULL;
  kdu_byte **alt_stripe_bufs8=NULL;
  if (multi_threaded_io)
    { 
      alt_stripe_heights = new int[num_components];
      if (read_bytes)
        alt_stripe_bufs8 = new kdu_byte *[num_components];
      else
        alt_stripe_bufs16 = new kdu_int16 *[num_components];  
    }
  
  // Construct the stripe-compressor object (this does all the work), use it
  // to find recommended stripe heights, then allocate the buffers.
  // Note that nothing stops you from passing in stripes of an image you have
  // in memory, produced by your application in any desired manner, but the
  // present demonstration uses files to recover stripes, so it is generally
  // better to limit the amount of memory used to temporarily buffer file
  // data between reading and compression.   The present application uses
  // `kdu_stripe_compressor::get_recommended_stripe_heights' to find suitable
  // stripe heights for processing, but if your application has its own idea of
  // what constitutes a good set of stripe heights, you may generally use those
  // values instead (could be up to the entire image in one stripe).
  //    Note, however, that whenever the codestream uses tiles, stripe heights
  // can have an impact on the efficiency with which the image is compressed
  // (a fundamental issue, not a Kakadu implementation issue).  We recommend
  // that codestreams with small tiles (e.g., 1024 x 1024) read and push
  // data in stripes whose height corresponds exactly to that of the tiles.
  // Moreover, in such cases, in a multi-threaded setting with multiple
  // horizontally adjacent tiles, we recommend that you set the DWT double
  // buffering height (`env_dbuf_height' here) to half the tile height.  For
  // larger tiles, or untiled codestreams, you are usually best off working
  // with much smaller double buffering stripe heights and pushing in modest
  // stripes.  Fortunately, these recommendations are all implemented
  // automatically for you if you use the `get_recommende_stripe_heights' and
  // pass the default value of -1 for the `env_dbuf_height' argument to
  // `kdu_stripe_compressor::start'.
  kdu_stripe_compressor compressor;
  compressor.start(codestream,num_layer_specs,
                   (target_slope==0)?layer_sizes:NULL,
                   (target_slope==0)?NULL:layer_slopes,
                   0,false,force_precise,true,rate_tolerance,num_components,
                   want_fastest,env_ref,NULL,env_dbuf_height,
                   tile_concurrency);
  compressor.get_recommended_stripe_heights(preferred_min_stripe_height,
                                            absolute_max_stripe_height,
                                            stripe_heights,max_stripe_heights);
  
  for (in=in_files; in != NULL; in=in->next)
    { 
      int stride = in->size.x * in->samples_per_pel;
      if (in->is_bmp)
        stride += (-stride) & 3; // Round out to multiple of 4 bytes for BMP
      in->buf_row_gap = stride;
      
      for (n=in->first_comp_idx; n < in->lim_comp_idx; n++)
        { 
          assert(stripe_heights[n] == stripe_heights[in->first_comp_idx]);
          precisions[n] = in->precision;
          is_signed[n] = in->is_signed;
          sample_gaps[n] = in->samples_per_pel;
          row_gaps[n] = in->buf_row_gap;
        }

      if (vertical_replicas > 1)
        { // Create whole image buffers instead of separate stripe buffers
          int num_samples = stride * in->size.y;
          int num_bytes = (read_bytes)?num_samples:(2*num_samples);
          kdu_byte *addr = new (std::nothrow) kdu_byte[num_bytes+31];
          if (addr == NULL)
            { kdu_error e;
              e << "Insufficient memory to allocate image buffers; try "
              "removing the \"-vrep\" argument, since vertical replication "
              "is implemented by reading the input files all into memory "
              "in one hit at the start.";
            }
          buf_handles[in->first_comp_idx] = addr; // Save deallocation handle
          addr += (-_addr_to_kdu_int32(addr)) & 0x1F; // 32-byte alignment may
                                              // make transfers more efficient
          if (read_bytes)
            in->buf8 = addr;
          else
            in->buf16 = (kdu_int16 *) addr;            
          for (n=in->first_comp_idx; n < in->lim_comp_idx; n++)
            { 
              int comp_offset = n-in->first_comp_idx;
              if (in->is_bmp && (sample_gaps[n] >= 3) && (comp_offset < 3))
                comp_offset = 2-comp_offset; // Reverse RGB to BGR
              if (read_bytes)
                { image_bufs8[n] = in->buf8 + comp_offset;
                  stripe_bufs8[n] = NULL; }
              else
                { image_bufs16[n] = in->buf16 + comp_offset;
                  stripe_bufs16[n] = NULL; }
            }
        }
      else
        { // Create stripe buffers instead of whole image buffers
          kdu_byte *addr = NULL;
          int num_samples = stride*max_stripe_heights[in->first_comp_idx];
          int num_bytes = (read_bytes)?num_samples:(2*num_samples);
          if (multi_threaded_io)
            { 
              num_samples += (-num_samples) & 31; // Round up to multiple of 32
              num_bytes = (read_bytes)?num_samples:(2*num_samples);
              addr = new (std::nothrow) kdu_byte[2*num_bytes + 31];
            }
          else
            addr = new (std::nothrow) kdu_byte[num_bytes+31];
          if (addr == NULL)
            { kdu_error e;
              e << "Insufficient memory to allocate stripe buffers; try "
              "changing the value passed to the \"-min_height\" or "
              "\"-max_height\" command-line argument.";
            }
          buf_handles[in->first_comp_idx] = addr; // Save deallocation handle
          addr += (-_addr_to_kdu_int32(addr)) & 0x1F; // 32-byte alignment may
                                              // make transfers more efficient
          if (read_bytes)
            { 
              in->buf8 = addr;
              if (multi_threaded_io)
                in->alt_buf8 = in->buf8 + num_samples;
            }
          else
            { 
              in->buf16 = (kdu_int16 *) addr;        
              if (multi_threaded_io)
                in->alt_buf16 = in->buf16 + num_samples;
            }
          for (n=in->first_comp_idx; n < in->lim_comp_idx; n++)
            { 
              int comp_offset = n-in->first_comp_idx;
              if (in->is_bmp && (sample_gaps[n] >= 3) && (comp_offset < 3))
                comp_offset = 2-comp_offset; // Reverse RGB to BGR
              if (read_bytes)
                { 
                  stripe_bufs8[n] = in->buf8 + comp_offset;
                  if (multi_threaded_io)
                    alt_stripe_bufs8[n] = stripe_bufs8[n] + num_samples;
                }
              else
                { 
                  stripe_bufs16[n] = in->buf16 + comp_offset;
                  if (multi_threaded_io)
                    alt_stripe_bufs16[n] = stripe_bufs16[n] + num_samples;
                }
            }
        }
    }

  // At last we come to the actual processing, having configured all the
  // parameters in a reasonable manner.  This is where all the action
  // actually is.  Everything else can be simplified, hard-wired, etc.,
  // depending on your particular application.
  if (num_threads <= 1)
    compress_single_threaded(compressor,preferred_min_stripe_height,
                             absolute_max_stripe_height,
                             stripe_heights,max_stripe_heights,
                             sample_gaps,row_gaps,precisions,is_signed,
                             stripe_bufs8,stripe_bufs16,
                             image_bufs8,image_bufs16,
                             in_files,total_samples,
                             flush_period,cpu,quiet);
  else if (!multi_threaded_io)
    compress_multi_threaded(compressor,preferred_min_stripe_height,
                            absolute_max_stripe_height,
                            stripe_heights,max_stripe_heights,
                            sample_gaps,row_gaps,precisions,is_signed,
                            stripe_bufs8,stripe_bufs16,
                            image_bufs8,image_bufs16,
                            in_files,total_samples,
                            flush_period,cpu,quiet,num_threads);
  else
    compress_multi_threaded_io(compressor,preferred_min_stripe_height,
                               absolute_max_stripe_height,
                               stripe_heights,alt_stripe_heights,
                               max_stripe_heights,
                               sample_gaps,row_gaps,precisions,is_signed,
                               stripe_bufs8,alt_stripe_bufs8,
                               stripe_bufs16,alt_stripe_bufs16,
                               in_files,total_samples,
                               flush_period,cpu,quiet,num_threads);
  
  // Finish up and print any required statistics
  compressor.finish(num_layer_specs,layer_sizes,layer_slopes);
  if (env.exists())
    env.destroy(); // Note: there is no need to call `env.cs_terminate' here,
                   // because: a) it has already been called inside
                   // `compressor.finish'; and b) we are calling `env.destroy'
                   // first.

  if (stats)
    { 
      pretty_cout << "Codestream bytes (excluding file format) = "
                  << codestream.get_total_bytes()
                  << " = "
                  << 8.0*codestream.get_total_bytes() / (double) total_pixels
                  << " bits/pel\n";
      pretty_cout << "Layer thresholds: ";
      for (int layer_idx=0; layer_idx < num_layer_specs; layer_idx++)
        { 
          if (layer_idx > 0)
            pretty_cout << ", ";
          pretty_cout << (int)(layer_slopes[layer_idx]);
        }
      pretty_cout << "\n";
      pretty_cout << "Compressed data memory = "
                  << codestream.get_compressed_data_memory()
                  << " bytes\n";
      pretty_cout << "State memory associated with compressed data = "
                  << codestream.get_compressed_state_memory()
                  << " bytes\n";
    }
  
  // Clean up resources
  codestream.destroy();
  output->close();
  if (jp2_ultimate_tgt.exists())
    jp2_ultimate_tgt.close();
  
  for (n=0; n < num_components; n++)
    if (buf_handles[n] != NULL)
      delete[] buf_handles[n];
  delete[] buf_handles;
  if (stripe_bufs8 != NULL) delete[] stripe_bufs8;
  if (stripe_bufs16 != NULL) delete[] stripe_bufs16;
  if (image_bufs8 != NULL) delete[] image_bufs8;
  if (image_bufs16 != NULL) delete[] image_bufs16;
  if (alt_stripe_bufs8 != NULL) delete[] alt_stripe_bufs8;
  if (alt_stripe_bufs16 != NULL) delete[] alt_stripe_bufs16;
  delete[] precisions;
  delete[] is_signed;
  delete[] stripe_heights;
  if (alt_stripe_heights != NULL) delete[] alt_stripe_heights;
  delete[] sample_gaps;
  delete[] row_gaps;
  delete[] max_stripe_heights;
  delete[] layer_sizes;
  delete[] layer_slopes;
  while ((in=in_files) != NULL)
    { in_files=in->next; delete in; }
  return 0;
}
