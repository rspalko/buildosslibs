/*****************************************************************************/
// File: kdu_video_io.h [scope = APPS/COMPRESSED-IO]
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
   Defines classes derived from "kdu_compressed_source" and
"kdu_compressed_target" which may be used by video processing applications.
A pair of abstract base classes provide generic video management tools,
building on those of "kdu_compressed_source" and "kdu_compressed_target",
which may be implemented in a variety of ways.  A simple implementation
of these base classes is provided here for use with sequential, video
sequences, while a much more sophisticated implementation is provided
in "mj2.h" to support the Motion JPEG2000 file format.
******************************************************************************/

#ifndef KDU_VIDEO_IO_H
#define KDU_VIDEO_IO_H

#include <stdio.h> // Use C I/O functions for speed; can make a big difference
#include <string.h>
#include "kdu_file_io.h"

// Classes defined here:
namespace kdu_supp {
  class kdu_compressed_video_source;
  class kdu_compressed_video_target;
  class kdu_simple_video_source;
  class kdu_simple_video_target;
}

// Prototypes for classes which might be defined elsewhere if needed
namespace kdu_supp {
  class jp2_input_box;
}

namespace kdu_supp {
  using namespace kdu_core;

/* Note Carefully:
      If you want to be able to use the "kdu_text_extractor" tool to
   extract text from calls to `kdu_error' and `kdu_warning' so that it
   can be separately registered (possibly in a variety of different
   languages), you should carefully preserve the form of the definitions
   below, starting from #ifdef KDU_CUSTOM_TEXT and extending to the
   definitions of KDU_WARNING_DEV and KDU_ERROR_DEV.  All of these
   definitions are expected by the current, reasonably inflexible
   implementation of "kdu_text_extractor".
      The only things you should change when these definitions are ported to
   different source files are the strings found inside the `kdu_error'
   and `kdu_warning' constructors.  These strings may be arbitrarily
   defined, as far as "kdu_text_extractor" is concerned, except that they
   must not occupy more than one line of text.
      When defining these macros in header files, be sure to undefine
   them at the end of the header.
*/
#ifdef KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("E(kdu_video_io.h)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_video_io.h)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu File Format Support:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu File Format Support:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


/*****************************************************************************/
/*                              kdu_field_order                              */
/*****************************************************************************/

enum kdu_field_order {
    KDU_FIELDS_NONE,
    KDU_FIELDS_TOP_FIRST,
    KDU_FIELDS_TOP_SECOND,
    KDU_FIELDS_UNKNOWN
  };

/* ========================================================================= */
/*                         Abstract Base Classes                             */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdu_compressed_video_source                          */
/*****************************************************************************/

class kdu_compressed_video_source :
  public kdu_compressed_source {
  /* [BIND: reference]
     [SYNOPSIS]
     This abstract base class defines core services of interest to
     applications working with compressed video content.  Itself
     derived from `kdu_compressed_source', implementations of this class
     may be passed to `kdu_codestream::create', for the purpose of
     parsing and/or decompressing individual images from a compressed video
     source.
     [//]
     Kakadu's implementation of the Motion JPEG2000 file format offers
     an appropriately derived class (`mj2_video_source'), which implements the
     interfaces declared here.  For a much simpler implementation, or
     inspiration for implementing your own video source classes, you might
     consider the `kdu_simple_video_source' class.
  */
  public: // Member functions
    virtual kdu_uint32 get_timescale() { return 0; }
      /* [SYNOPSIS]
           If the video source provides no timing information, this function
           may return 0.  Otherwise, it returns the number of ticks per
           second, which defines the time scale used to describe frame
           periods.  See `get_frame_period'.
      */
    virtual kdu_field_order get_field_order() { return KDU_FIELDS_NONE; }
      /* [SYNOPSIS]
           Returns `KDU_FIELDS_NONE' if the video track contains progressive
           scan frames.  Some video sources may not be able to support anything
           other than progressive scan frames; however, it is convenient to
           provide support for interlaced formats directly from the abstract
           base class.
           [//]
           For interlaced video, the function returns one of the following
           values:
           [>>] `KDU_FIELDS_TOP_FIRST' -- means that the frames are interlaced
                with the first field of a frame holding the frame's top line.
           [>>] `KDU_FIELDS_TOP_SECOND' -- means that the frames are interlaced
                with the second field of a frame holding the frame's top line.
           [>>] `KDU_FIELDS_UNKNOWN' -- means that the frames are interlaced
                but the order of the fields within a frame is not known; this
                value should rarely if ever occur, but is required at least
                to properly support broadcast profiles.
      */
    virtual void set_field_mode(int which) { return; }
      /* [SYNOPSIS]
           This function may be called at any time, to specify which fields
           will be accessed by subsequent calls to `open_image'.  If the
           video is progressive (see `get_field_order'), this function has
           no effect.  Note that some video sources might not support anything
           other than progressive video, in which case the function will also
           do nothing.
         [ARG: which]
           Must be one of 0, 1 or 2.  If 0, calls to `open_image' open
           the first field of the next frame in sequence.  If 1, calls to
           `open_image' open the second field of the next frame in sequence.
           If 2, `open_image' opens each field of the frame in sequence.
      */
    virtual int get_num_frames() { return 0; }
      /* [SYNOPSIS]
           Returns the total number of frames which are available, or 0 if
           the value is not known.  Some video sources might not provide
           an indication of the total number of frames available in a global
           header, in which case they are at liberty to return 0 here.
      */
    virtual bool seek_to_frame(int frame_idx) { return false; }
      /* [SYNOPSIS]
           Call this function to set the index (starts from 0) of the frame
           to be opened by the next call to `open_image'.
         [RETURNS]
           False if the indicated frame does not exist, or frame seeking is
           not supported by the implementation.
      */
    virtual kdu_long get_duration() { return 0; }
      /* [SYNOPSIS]
           If the video source provides no timing information, or the full
           extent of the video is not readily deduced a priori, this function
           may return 0.  Otherwise, it returns the total duration of the
           video track, measured in the time scale (ticks per second)
           identified by the `get_timescale' function.
      */
    virtual int time_to_frame(kdu_long time_instant) { return -1; }
      /* [SYNOPSIS]
           If the video source provides no time indexing capabilities,
           this function may return -1.  Otherwise, it should return the
           index of the frame whose period includes the supplied
           `time_instant', measured in the time scale (ticks per second)
           identified by the `get_timescale' function.
           [//]
           If time indexing is available, but `time_instant' exceeds the
           duration of the video track, the function returns the index of
           the last available frame.  Similarly, if `time_instant' refers
           to a time prior to the start of the video sequence, the function
           should return 0 (the index of the first frame).
      */
    virtual kdu_long get_frame_instant() { return 0; }
      /* [SYNOPSIS]
           If the video source provides no timing information, this function
           may return 0.  Otherwise, it should return the starting time
           of the frame to which the currently open image belongs, measured
           in the time scale (ticks per second) identified by the
           `get_timescale' function.  If no image is currently open,
           the function returns the starting time of the next frame which
           will be opened by `open_image', or the duration of the video track
           if no new frames are available for opening.
           [//]
           Note that the return value should be unaffected by the field mode
           established by `set_field_mode'.  That is, the function returns
           frame starting times, not field starting times, when the video is
           interlaced.
      */
    virtual kdu_long get_frame_period() { return 0; }
      /* [SYNOPSIS]
           If the compressed video source provides no timing information,
           this function returns 0.  Otherwise, it returns the number of
           ticks associated with the frame to which the currently open image
           belongs.  If no image is currently open, the function returns the
           frame period associated with the frame to which the next open
           image would belong if `open_image' were called.  The number of
           ticks per second is identified by the `get_timescale' function.
           If the video is interlaced, there are two images (fields) in each
           frame period.
      */
    virtual int open_image() = 0;
      /* [SYNOPSIS]
           Call this function to open the next video image in sequence,
           providing access to its underlying JPEG2000 code-stream.  The
           sequence of images opened by this function depends upon whether
           the video is interlaced or progressive, and also on any previous
           calls to `set_field_mode'.  For progressive video, the function
           opens each frame in sequence.  If the field mode was set to 0 or
           1, the function also opens each frame of an interlaced video in
           sequence, supplying only the first or second field, respectively,
           of each frame.  If the video is interlaced and the field mode was
           set to 2, the function opens each field of each frame in turn, so
           that the frame index advances only on every second call to this
           function.
           [//]
           After calling this function, the present object may be passed into
           `kdu_codestream::create' for parsing and, optionally, decompression
           of the image's code-stream.  Once the `kdu_codestream' object is
           done (destroyed or re-created), the `close_image' function may be
           called to prepare the object for opening a subsequent image.
         [RETURNS]
           The frame index associated with the open image, or -1 if no further
           images can be opened.  Note that the frame index advances only
           once every two calls to this function, if the video is interlaced
           and the field mode (see `set_field_mode') is 2.  Note also, that
           `seek_to_frame' might be able to re-position the frame pointer
           before opening an image.
      */
    virtual int open_stream(int field_idx, jp2_input_box *input_box)
      { return -1; }
      /* [SYNOPSIS]
           This function is provided as a prototype for derived objects
           that are able to support multiple simultaneously open images
           via the `jp2_family_src' and `jp2_input_box' machinery.
           Derived objects that implement this function include
           `mj2_video_source' and `jpb_source'.
           [//]
           If this function is implemented and the underlying data source
           is a `jp2_family_src' object that is seekable and implements the
           `jp2_family_src::acquire_lock' and `jp2_family_src::release_lock'
           functions (typically, a `jp2_threadsafe_family_src' object) then
           it is generally safe to interact with any number of open images
           simultaneously.
           [//]
           If thread safety is not provided by the underlying data source,
           it can still be possible to safely interact with multiple images
           at once by using the `jp2_input_box::load_in_memory' function.
           [//]
           The `jp2_input_box' class definition is not actually loaded by
           this header file, nor does it need to be available or implemented
           unless a derived object intends to provide a meaningful
           implementation of this function.
         [RETURNS]
           The frame index associated with the opened image stream, or -1
           if the requested field does not exist, or if the frame which
           would be accessed by the next call to `open_image' does not
           exist, or if the functionality is not implemented.
         [ARG: field_idx]
           0 for the first field in the frame; 1 for the second field in
           the frame, if there is one.
         [ARG: input_box]
           Pointer to a box which is not currently open.  The box is open
           upon return unless the function's return valueis negative.  The
           box-type will usually be `jp2_codestream_4cc'.
      */
    virtual void close_image() = 0;
      /* [SYNOPSIS]
           Each successful call to `open_image' must be bracketed by a call to
           `close_image'.  Does nothing if no image is currently open.
      */
  };

/*****************************************************************************/
/*                      kdu_compressed_video_target                          */
/*****************************************************************************/

class kdu_compressed_video_target :
  public kdu_compressed_target {
  /* [BIND: reference]
     [SYNOPSIS]
     This abstract base class defines core services of interest to
     applications which generate compressed video content.  Itself
     derived from `kdu_compressed_target', implementations of this class
     may be passed to `kdu_codestream::create', for the purpose of generating
     or transcoding individual images in a compressed video sequence.
     [//]
     Kakadu's implementation of the Motion JPEG2000 file format offers
     an appropriately derived class (`mj2_video_target'), which implements the
     interfaces declared here.  For a much simpler implementation, or
     inspiration for implementing your own video target classes, you might
     consider the `kdu_simple_video_target' class.
  */
  public: // Member functions
    virtual void open_image() = 0;
      /* [SYNOPSIS]
           Call this function to initiate the generation of a new image for
           the video sequence.  At the most basic level, video is considered
           to be a sequence of images.  In the case of interlaced video, a
           frame/field structure may be imposed where each frame consists of
           two fields and each field is considered a separate image.  However,
           some compressed video targets might not support interlaced video.
           [//]
           After calling this function, the present object may be passed into
           `kdu_codestream::create' to generate the JPEG2000 code-stream
           representing the open video image.  Once the code-stream has been
           fully generated (usually performed by `kdu_codestream::flush'),
           the image must be closed using `close_image'.  A new video image
           can then be opened.
      */
    virtual void close_image(kdu_codestream codestream) = 0;
      /* [SYNOPSIS]
           Each call to `open_image' must be bracketed by a call to
           `close_image'.  The caller must supply a non-empty `codestream'
           interface, which was used to generate the compressed data for
           the image just being closed.  Its member functions may be used to
           determine dimensional parameters for internal initialization
           and consistency checking.
      */
  };


/* ========================================================================= */
/*                          Simple Video Format                              */
/* ========================================================================= */

#define KDU_SIMPLE_VIDEO_MAGIC ((((kdu_uint32) 'M')<<24) |  \
                                (((kdu_uint32) 'J')<<16) |  \
                                (((kdu_uint32) 'C')<< 8) |  \
                                (((kdu_uint32) '2')<< 0))
#define KDU_SIMPLE_VIDEO_YCC ((kdu_uint32) 1)
#define KDU_SIMPLE_VIDEO_RGB ((kdu_uint32) 2)
#define KDU_SIMPLE_VIDEO_CBR ((kdu_uint32) 4)

/*****************************************************************************/
/*                         kdu_simple_video_source                           */
/*****************************************************************************/

class kdu_simple_video_source :
  public kdu_compressed_video_source {
  /* [BIND: reference]
       This object has expanded somewhat from its extremely simple
       beginnings, although it is still very simple.  The main
       enhancement in recent times is that it supports MJC files whose
       simple header includes the `KDU_SIMPLE_VIDEO_CBR' flag, in which
       case there is just one codestream length field, right at the start
       of the file, and all codestreams are assigned exactly this same
       size -- unused bytes after the codestream EOC marker are generally
       filled with 0's.  If this CBR flag is found, the source offers
       frame counting and frame seeking capabilities, and it is a simple
       matter to extend the class into one that supports asynchronous
       reading of codestreams by implementing the base virtual function
       `kdu_compressed_video_source::open_stream'.
  */
  public: // Member functions
    kdu_simple_video_source() { file = NULL; }
    kdu_simple_video_source(const char *fname, kdu_uint32 &flags)
      { file = NULL; open(fname,flags); }
      /* [SYNOPSIS] Convenience constructor, which also calls `open'. */
    ~kdu_simple_video_source() { close(); }
      /* [SYNOPSIS] Automatically calls `close'. */
    bool exists() { return (file != NULL); }
      /* [SYNOPSIS]
           Returns true if there is an open file associated with the object.
      */
    bool operator!() { return (file == NULL); }
      /* [SYNOPSIS]
           Opposite of `exists', returning false if there is an open file
           associated with the object.
      */
    bool open(const char *fname, kdu_uint32 &flags,
              bool return_if_incompatible=false)
      { 
      /* [SYNOPSIS]
           Closes any currently open file and attempts to open a new one,
           generating an appropriate error (through `kdu_error') if the
           indicated file cannot be opened, unless `return_if_incompatible'
           is true.  If successful, the function returns true, setting `flags'
           to the value of the flags word recovered from the file, as
           explained below.
         [ARG: fname]
           Relative path name of file to be opened.
         [ARG: flags]
           Currently, only three flags are defined, as follows:
           [>>] `KDU_SIMPLE_VIDEO_YCC'
           [>>] `KDU_SIMPLE_VIDEO_RGB'
           [>>] `KDU_SIMPLE_VIDEO_CBR'
           [//]
           The first two flags are mutually exclusive; if neither is present,
           the first component of each video image can be taken to represent
           a monochrome image, and that is all that can be assumed.
           [//]
           If the `KDU_SIMPLE_VIDEO_CBR' flag is present, it is possible to
           seek to a frame of interest, compute the number of frames in the
           file and also to get the duration of the video, using the
           `seek_to_frame', `get_num_frames' and `get_duration' member
           functions that are overridden in this class.  Otherwise, none
           of these capabilities will be available.
         [ARG: return_if_incompatible]
           If true, and the file header is not compatible with the MJC file
           format, the function returns false, leaving the object in the
           closed state (`exists' returns false) rather than generating an
           error through `kdu_error'.
      */
        close();
        file = fopen(fname,"rb");
        if (file == NULL)
          { KDU_ERROR(e,0); e <<
            KDU_TXT("Unable to open compressed data file")
            << ", \"" << fname << "\"!";
          }
        kdu_uint32 magic = 0;
        if (!(read_dword(magic) && read_dword(timescale) &&
              read_dword(frame_period) && read_dword(flags) &&
              (magic == KDU_SIMPLE_VIDEO_MAGIC)))
          { 
            if (return_if_incompatible && (magic != KDU_SIMPLE_VIDEO_MAGIC))
              { close(); return false; }
            KDU_ERROR(e,1); e <<
            KDU_TXT("Input file")
            << ", \"" << fname << "\", " <<
            KDU_TXT("does not appear to have a valid format.");
          }
        header_len = 16;
        start_pos = file_pos = 16;
        if (flags & KDU_SIMPLE_VIDEO_CBR)
          { 
            if (!(read_dword(fixed_len) && (fixed_len > 0)))
              { KDU_ERROR(e,0x01091601); e <<
                KDU_TXT("Input file")
                << ", \"" << fname << "\", " <<
                KDU_TXT("advertises a fixed compressed frame size, but "
                        "does not include a non-zero size value!");
              }
            header_len += 4;  start_pos += 4;
            kdu_fseek(file,0,SEEK_END);
            file_pos = kdu_ftell(file);
            kdu_long frame_span = file_pos - start_pos;
            num_frames = 1 + (int)((frame_span-1)/fixed_len); // Round up
            kdu_fseek(file,file_pos=start_pos);
          }
        image_open = false;
        return true;
      }
    bool close()
      { /* [SYNOPSIS]
             It is safe to call this function, even if no file has been opened.
             This particular implementation of the `close' function always
             returns true.
        */
        if (file != NULL)
          fclose(file);
        file = NULL;
        frame_period = timescale = fixed_len = header_len = 0;
        frame_idx = num_frames = 0; frame_instant = 0; image_open = false;
        file_pos = start_pos = lim_pos = 0;
        return true;
      }
    kdu_uint32 get_timescale() { return timescale; }
      /* [SYNOPSIS]
           See `kdu_compressed_video_source::get_timescale'.
      */
    int get_num_frames() { return num_frames; }
      /* [SYNOPSIS]
           See `kdu_compressed_video_source::get_num_frames'.    
           Returns 0 if the `KDU_SIMPLE_VIDEO_CBR' flag was not found.
           See `open' for more on the significance of the CBR flag.
      */
    bool seek_to_frame(int frm_idx)
      { /* [SYNOPSIS]
             See `kdu_compressed_video_source::seek_to_frame'.
             Returns false if `frm_idx' > 0 and the `KDU_SIMPLE_VIDEO_CBR'
             flag was not found; also returns false if the indicated frame
             is known not to exist.  See `open' for more on the significance
             of the CBR flags.
        */
        assert(!image_open);
        if ((frm_idx >= num_frames) && (fixed_len || frm_idx))
          return false;
        kdu_long pos=(((kdu_long)fixed_len)*(kdu_long)frm_idx) + header_len;
        file_pos = start_pos = lim_pos = pos;
        image_open = false; // Just in case
        kdu_fseek(file,pos);
        this->frame_idx = frm_idx;
        return true;
      }
    kdu_long get_duration() { return ((kdu_long)num_frames)*frame_period; }
      /* [SYNOPSIS]
           See `kdu_compressed_video_source::get_duration'.
           Returns 0 if the `KDU_SIMPLE_VIDEO_CBR' flag was not found.
           See `open' for more on the significance of the CBR flag.
      */
    int time_to_frame(kdu_long time_instant)
      { /* [SYNOPSIS]
              See `kdu_compressed_video_source::time_to_frame'.
        */
        kdu_long result = time_instant / frame_period;
        if (fixed_len != 0)
          { if (result > (kdu_long) num_frames) result = num_frames; }
        else if (result > (kdu_long)KDU_INT32_MAX)
          result = KDU_INT32_MAX;
        return (int) result;
      }
    kdu_long get_frame_instant() { return frame_instant; }
      /* [SYNOPSIS]
           See `kdu_compressed_video_source::get_frame_instant'.
      */
    kdu_long get_frame_period() { return frame_period; }
      /* [SYNOPSIS]
           See `kdu_compressed_video_source::get_frame_instant'.
      */
    int open_image()
      { /* [SYNOPSIS]
             See `kdu_compressed_video_source::open_image'.
        */
        assert(!image_open);
        if (fixed_len)
          { 
            if (frame_idx >= num_frames)
              return -1;
            start_pos = file_pos;
            lim_pos = file_pos + fixed_len;
          }
        else
          { 
            kdu_uint32 image_length;
            if (!read_dword(image_length))
              return -1;
            file_pos += 4; start_pos = file_pos;
            lim_pos = start_pos + image_length;
          }
        image_open = true;
        return frame_idx;
      }
    kdu_long get_remaining_bytes() const { return lim_pos-file_pos; }
      /* [SYNOPSIS]
           Returns the number of bytes that have not yet been read from
           an open image.  If there is no current open image, the function
           returns 0.
      */
    kdu_long get_image_file_pos() const { return start_pos; }
      /* [SYNOPSIS]
           Returns the absolute file position of the first byte in the
           currently open image's codestream.  The behaviour of this
           function is undefined if `open_image' has not been called, or the
           last such call has already been matched by a call to `close_image'.
      */
    void close_image()
      { /* [SYNOPSIS]
             See `kdu_compressed_video_source::close_image' for an explanation.
        */
        assert(image_open); image_open = false;
        if (file_pos != lim_pos)
          { kdu_fseek(file,file_pos=lim_pos); }
        frame_idx++; frame_instant += frame_period;
        start_pos = lim_pos; // Because the frame index has increased
      }
    int get_capabilities()
      { return KDU_SOURCE_CAP_SEQUENTIAL | KDU_SOURCE_CAP_SEEKABLE; }
      /* [SYNOPSIS]
           The returned capabilities word always includes the flag,
           `KDU_SOURCE_CAP_SEQUENTIAL' and `KDU_SOURCE_CAP_SEEKABLE'.
           See `kdu_compressed_source::get_capabilities'
           for an explanation of capabilities.
      */
    bool seek(kdu_long offset)
      { /* [SYNOPSIS] See `kdu_compressed_source::seek' for an explanation. */
        assert((file != NULL) && image_open);
        file_pos = start_pos + offset;
        if (file_pos >= lim_pos)
          file_pos = lim_pos-1;
        if (file_pos < start_pos)
          file_pos = start_pos;
        kdu_fseek(file,file_pos);
        return true;
      }
    kdu_long get_pos()
      { /* [SYNOPSIS]
           See `kdu_compressed_source::get_pos' for an explanation. */
        return (file == NULL)?-1:(file_pos-start_pos);
      }
    int read(kdu_byte *buf, int num_bytes)
      { /* [SYNOPSIS] See `kdu_compressed_source::read' for an explanation. */
        assert((file != NULL) && image_open);
        int max_bytes = (int)(lim_pos-file_pos);
        if (num_bytes > max_bytes)
          num_bytes = max_bytes;
        num_bytes = (int) fread(buf,1,(size_t) num_bytes,file);
        file_pos += num_bytes;
        return num_bytes;
      }
  private: // Helper functions
    bool read_dword(kdu_uint32 &val)
      { int byte;
        val = (kdu_uint32) getc(file); val <<= 8;
        val += (kdu_uint32) getc(file); val <<= 8;
        val += (kdu_uint32) getc(file); val <<= 8;
        val += (kdu_uint32)(byte = getc(file));
        return (byte != EOF);
      }
  private: // Data
    FILE *file;
    kdu_uint32 frame_period, timescale;
    kdu_uint32 fixed_len; // 0 if frames have a variable length
    kdu_uint32 header_len; // Fixed header bytes at start of file
    int num_frames; // 0 if not known -- deduced by `open' if `fixed_len' > 0
    int frame_idx; // Index of frame to which `file_pos' points (0-based)
    kdu_long frame_instant; // Start time associated with `frame_idx'
    bool image_open; // True if  an image is currently open
    kdu_long file_pos; // Current position in file
    kdu_long start_pos; // Location in file of currently open image
    kdu_long lim_pos; // Location beyond end of currently open image
  };

/*****************************************************************************/
/*                         kdu_simple_video_target                           */
/*****************************************************************************/

class kdu_simple_video_target :
  public kdu_compressed_video_target {
  /* [BIND: reference]
        This object has expanded somewhat from its extremely simple
        beginnings, although it is still very simple.  The main
        enhancement in recent times is that it can write MJC files whose
        simple header includes the `KDU_SIMPLE_VIDEO_CBR' flag, in which
        case there is just one codestream length field, right at the start
        of the file, and all codestreams are assigned exactly this same
        size -- unused bytes after the codestream EOC marker are generally
        filled with 0's.  To use this capabiity, the `set_fixed_length'
        function must be called after `open'.
        [//]
        Instances of this class can be either a master video target,
        representing the open file, or a view into the target.  Views are
        useful if you need to be able to write to multiple images at the
        same time.  The `open_image' and `close_image' functions manage
        a single frame resource that cannot easily be shared; each call to
        `open_image' must be followed by a call to `close_image' before
        the next `open_image' call.  With multiple views, however, each
        one has its own `open_image', `close_image' and `write' calls that
        can be used independently, except that each view's `close_image'
        calls must be serialized, since these actually write to the file.
  */
  public: // Member functions
    kdu_simple_video_target()
      { master=NULL; file=NULL; num_views=0;
        head=tail=NULL; fixed_len=0; fixed_buf=NULL; }
    kdu_simple_video_target(const char *fname, kdu_uint32 timescale,
                            kdu_uint32 frame_period, kdu_uint32 flags)
      { master=NULL; file=NULL; num_views=0;
        open(fname,timescale,frame_period,flags); }
      /* [SYNOPSIS] Convenience constructor, which also calls `open'. */
    ~kdu_simple_video_target()
      { close();
        while ((tail=head) != NULL)
          { head=tail->next; delete tail; }
        if (fixed_buf != NULL)
          delete[] fixed_buf;
      }
    bool exists() { return (master != NULL); }
      /* [SYNOPSIS]
           Returns true if there is an open file associated with the object,
           either as the original (master) target or a view into another
           target.
      */
    bool operator!() { return (master == NULL); }
      /* [SYNOPSIS]
           Opposite of `exists', returning false if there is an open file
           associated with the object.
      */
    void open(const char *fname, kdu_uint32 timescale, kdu_uint32 frame_period,
              kdu_uint32 flags)
      { 
      /* [SYNOPSIS]
           Opens the indicated file for writing, generating an error message
           through `kdu_error', if this is not possible.  Writes a 16-byte
           header consisting of 4 integers, in big-endian byte order.  The
           first holds the magic string, "MJC2"; the second holds the time
           scale (clock ticks per second); the third holds a frame period
           (number of clock ticks between frame); and the fourth holds a
           flags word, which is explained below.
         [ARG: flags]
           Currently, only three flags are defined, as follows:
           [>>] `KDU_SIMPLE_VIDEO_YCC'
           [>>] `KDU_SIMPLE_VIDEO_RGB'
           [>>] `KDU_SIMPLE_VIDEO_CBR'
           [//]
           The first two flags are mutually exclusive; if neither is present,
           the first component of each video image will be taken to represent
           a monochrome image, and that is all that can be assumed.
           [//]
           You do not need to explicitly include the `KDU_SIMPLE_VIDEO_CBR'
           flag.  It will be included automatically if `set_fixed_length' is
           called before the first `open_image' call, as explained in the
           notes accompanying the `set_fixed_length' function.
     */
        close();
        file = fopen(fname,"wb");
        if (file == NULL)
          { KDU_ERROR(e,2); e <<
            KDU_TXT("Unable to open compressed data file")
            << ", \"" << fname << "\"!";
          }
        master = this;
        fixed_len = 0;
        assert((fixed_buf == NULL) && !hdr_written);
        write_dword(KDU_SIMPLE_VIDEO_MAGIC);
        write_dword(timescale);
        write_dword(frame_period);
        this->hdr_flags = flags;
        image_open=false;
      }
    void set_fixed_length(kdu_uint32 fixed_length)
      { 
        if ((master == NULL) || master->hdr_written) return;
        master->fixed_len = this->fixed_len = fixed_length;
      }
      /* [SYNOPSIS]
           This function is provided to allow for the explicit identification
           of streams in which every compressed frame occupies exactly the
           same number of bytes.  You can call this function as often as you
           like between calls to `open' and the first `open_image' call,
           from the master object or any of its views.  The first call to
           `open' that arises within any view first makes sure the master
           header is written and then imports the header information (including
           the `fixed_length' value) into the view.  Calls to this function
           that arrive from any view will update the master header information
           if it has not been written.  Calls to this function from any view
           or the master are ignored if the header has already been written.
         [ARG: fixed_length]
           If `fixed_length' is non-zero, the `KDU_SIMPLE_VIDEO_CBR' flag will
           be included in the flags recorded in the file's header, and there
           will be only one 4-byte big-endian length field (rather than one
           for every codestream), recording the value of `fixed_length'.  Every
           codestream generated between calls to `open_image' and `close_image'
           must then fit within the `fixed_length' available bytes, but need
           not occupy all of them.  Any unused bytes following the codestream's
           EOC marker will be filled with 0's.
           [//]
           The main advantage of writing fixed-length streams is that any
           arbitrary frame can readily be located within the stream without
           the need for any index table.  Fixed length streams are important
           to low latency applications with constant bit-rate channels, as
           highlighted by the JPEG-XS standardization activitity, which this
           format is intended to address.
        */
    bool close()
      { /* [SYNOPSIS]
             It is safe to call this function, even if no file has yet been
             opened.  This particular implementation of the `close' function
             always returns true.
        */
        assert(num_views == 0);
        if (file != NULL)
          fclose(file);
        else if (master != NULL)
          { 
            assert(master->num_views > 0);
            master->num_views--;
          }
        file = NULL;
        master = NULL;
        if (fixed_buf != NULL)
          delete[] fixed_buf;
        tail = NULL;  fixed_buf = NULL;  fixed_len = 0;
        hdr_flags = 0;  hdr_written = false;  image_open = false;
        return true;
      }
    bool attach_as_view(kdu_simple_video_target *master_target)
      { /* [SYNOPSIS]
             Make this object a view into a separate master video target.  If
             the `master_target' cannot be the master (e.g., it is not open),
             the function returns false.  See summary comments for this class
             for an explanation of views and masters.
        */
        close(); // Just in case
        if ((master_target == NULL) ||
            (master_target != master_target->master))
          return false;
        this->master = master_target;
        master->num_views++;
        this->hdr_written = master->hdr_written;
        this->hdr_flags = master->hdr_flags;
        this->fixed_len = master->fixed_len;
        return true;
      }
    void open_image()
      { /* [SYNOPSIS]
             See description of `kdu_compressed_video_target::open_image'.
        */
        assert(!image_open);
        if (!hdr_written)
          { 
            master->write_header_if_necessary();
            hdr_written = true;
            hdr_flags = master->hdr_flags;
            fixed_len = master->fixed_len;
          }
        if (fixed_len && (fixed_buf == NULL))
          { 
            fixed_buf = new(std::nothrow) kdu_byte[fixed_len];
            if (fixed_buf == NULL)
              { KDU_ERROR(e,0x31081601); e <<
                KDU_TXT("Unable to allocate sufficient memory to hold a "
                        "compressed frame.  Perhaps you should not be using "
                        "the simple MJC file format!");
              }
          }
        image_open = true; image_len = 0; tail = NULL;
      }
    bool write(const kdu_byte *buf, int num_bytes)
      { /* [SYNOPSIS]
             See `kdu_compressed_video_target::write' for an explanation.
        */
        assert(image_open);
        image_len += num_bytes;
        if (fixed_len)
          { 
            if (((kdu_uint32)image_len) > fixed_len)
              { KDU_ERROR_DEV(e,0x31081602); e <<
                KDU_TXT("Fixed compressed frame size declared when opening "
                        "MJC output file is violated during codestream "
                        "generation!");
              }
            memcpy(fixed_buf+(image_len-num_bytes),buf,(size_t)num_bytes);
          }
        else
          { 
            while (num_bytes > 0)
              { 
                if (head == NULL)
                  head = new kd_stream_store;
                if (tail == NULL)
                  { tail = head; tail->remaining += tail->len; tail->len = 0; }
                if (tail->remaining == 0)
                  { 
                    if (tail->next == NULL)
                      tail->next = new kd_stream_store;
                    tail = tail->next;
                    tail->remaining += tail->len;
                    tail->len = 0;
                  }
                int xfer =
                  (num_bytes<tail->remaining)?num_bytes:(tail->remaining);
                memcpy(tail->buf+tail->len,buf,(size_t) xfer);
                tail->remaining -= xfer; tail->len += xfer;
                num_bytes -= xfer; buf += xfer;
              }
          }
        return true;
      }
    void close_image(kdu_codestream codestream)
      { /* [SYNOPSIS]
             See `kdu_compressed_video_target::close_image' for an explanation.
        */
        assert(image_open);
        if (fixed_len)
          { 
            if (fixed_len > (kdu_uint32)image_len)
              memset(fixed_buf+image_len,0,(size_t)(fixed_len-image_len));
            fwrite(fixed_buf,1,(size_t)fixed_len,master->file);
          }
        else
          { 
            write_dword((kdu_uint32) image_len);
            for (tail=head; image_len > 0; image_len -= tail->len,
                 tail=tail->next)
              fwrite(tail->buf,1,(size_t)(tail->len),master->file);
          }
        image_open = false;
      }
  private: // Helper functions
    void write_dword(kdu_uint32 val)
      { 
        FILE *fp = master->file;
        putc((kdu_byte)(val>>24),fp);   putc((kdu_byte)(val>>16),fp);
        putc((kdu_byte)(val>> 8),fp); putc((kdu_byte)(val>> 0),fp);
      }
    void write_header_if_necessary()
      { // Writing of the main file header is deferred
        if (hdr_written) return;
        assert(master == this); // Views always have `hdr_written' set
        if (fixed_len)
          hdr_flags |= KDU_SIMPLE_VIDEO_CBR;
        else if (hdr_flags & KDU_SIMPLE_VIDEO_CBR)
          { KDU_ERROR(e,0x01091602); e <<
            KDU_TXT("If `kdu_simple_video_target::open' is called with "
                    "the `KDU_SIMPLE_VIDEO_CBR' flag, a non-zero fixed "
                    "frame length must be specified via a call to "
                    "`kdu_simple_video_target::set_fixed_length'.");
          }
        write_dword(hdr_flags);
        if (fixed_len)
          write_dword(fixed_len);
        hdr_written = true;
      }
  // --------------------------------------------------------------------------
  private: // Declarations
      struct kd_stream_store {
          kd_stream_store()
            { len = 0; remaining = 8192; next = NULL; }
          int len, remaining;
          kdu_byte buf[8192];
          kd_stream_store *next;
        };
  // --------------------------------------------------------------------------
  private: // Members used differently by master and view targets
    kdu_simple_video_target *master; // Points to self if we are not a view
    FILE *file; // Non-NULL only for the master target (when open)
    int num_views; // Master keeps track of the views attached to it
    kdu_uint32 hdr_flags; // Flags passed to `open'; used to write file header
    bool hdr_written; // False until the file header has been written
  private: // Members that appear the same in master and view targets
    kdu_uint32 fixed_len; // 0 for variable-length streams; copied to all views
    bool image_open; // Each view manages its own open/close cycle
    int image_len; // Bytes written to currently open image
    kd_stream_store *head, *tail; // Used only for variable-length targets
    kdu_byte *fixed_buf; // Used only for fixed-length targets
  };

#undef KDU_ERROR
#undef KDU_ERROR_DEV
#undef KDU_WARNING
#undef KDU_WARNING_DEV
#undef KDU_TXT

} // namespace kdu_supp

#endif // KDU_VIDEO_IO_H