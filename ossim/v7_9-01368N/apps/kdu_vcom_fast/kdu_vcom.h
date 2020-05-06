/*****************************************************************************/
// File: kdu_vcom.h [scope = APPS/VCOM_FAST]
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
   Class declarations for the platform independent multi-threaded video
decompression pipeline used by the "kdu_vcom_fast" demo app.
******************************************************************************/

#ifndef KDU_VCOM_H
#define KDU_VCOM_H

#include "kdu_threads.h"
#include "kdu_elementary.h"
#include "kdu_arch.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_args.h"
#include "mj2.h"
#include "jpx.h"
#include "kdu_stripe_compressor.h"

// Declared here:
namespace kdu_supp_vcom { // Worth putting these in own reusable namespace
  class vcom_jpx_labels; // Identical to `kdv_jpx_labels' from "kdu_v_compress"
  class vcom_jpx_target; // Identical to `kdv_jpx_target' from "kdu_v_compress"
  struct vcom_jpx_layer; // Identical to `kdv_jpx_layer' from "kdu_v_compress"
  class vcom_null_target;
  struct vcom_frame_buffer;
  struct vcom_frame;
  class vcom_stream;
  class vcom_frame_queue;
  class vcom_processor;
  class vcom_engine;
}

namespace kdu_supp_vcom {
  using namespace kdu_supp;
  
/*****************************************************************************/
/*                             vcom_jpx_labels                               */
/*****************************************************************************/

class vcom_jpx_labels {
  public: // Member functions
    vcom_jpx_labels(jpx_target *tgt, jpx_container_target container,
                    const char *prefix_string)
      { /* Prepares the object for incrementally writing labels that refer to
           successive repetitions of the `container'.  The `advance' function
           should be called each time a repetition is completed. */
        this->target=tgt;  root = (tgt->access_meta_manager()).access_root();
        link_target = root.add_label("Labels");
        link_target.preserve_for_links();
        prefix_chars = (int) strlen(prefix_string);
        label_string = new char[prefix_chars+10];
        strcpy(label_string,prefix_string);    frame_idx = 0;
        int first_layer_idx = container.get_base_layers(num_layer_indices);
        layer_indices = new int[num_layer_indices];
        for (int n=0; n < num_layer_indices; n++)
          layer_indices[n] = first_layer_idx+n;
      }
    ~vcom_jpx_labels()
      { 
        if (label_string != NULL) delete[] label_string;
        if (layer_indices != NULL) delete[] layer_indices;
      }
    void advance()
      { // Called at the end of each frame; generates and writes the metadata
        frame_idx++;   sprintf(label_string+prefix_chars,"-%d",frame_idx);
        jpx_metanode node = root.add_numlist(0,NULL,num_layer_indices,
                                             layer_indices,false);
        (node.add_link(link_target,JPX_GROUPING_LINK)).add_label(label_string);
        target->write_metadata();
        for (int n=0; n < num_layer_indices; n++)
          layer_indices[n] += num_layer_indices;
      }
  private: // Data
    jpx_target *target; // Need this to generate `write_metadata' calls
    jpx_metanode root; // Root of the metadata hierarchy
    jpx_metanode link_target;
    char *label_string;
    int prefix_chars; // Initial characters of `label_string' with label prefix
    int frame_idx; // 0 for first frame then incremented by `advance'
    int num_layer_indices; // Number of base layers in the JPX container
    int *layer_indices; // These start out identifying all base layer indices
  };

/*****************************************************************************/
/*                             vcom_jpx_target                               */
/*****************************************************************************/

class vcom_jpx_target : public kdu_compressed_video_target {
  public: // Member functions
    vcom_jpx_target(jpx_container_target cont, vcom_jpx_labels *labels=NULL)
      { /* If `labels' != NULL, the object also generates the label metadata
           on appropriate calls to `close_image'. */
        this->container = cont;  this->label_writer = labels;
        out_box=NULL;  base_codestream_idx=0;
        cont.get_base_codestreams(num_base_codestreams);
      }
    virtual void open_image()
      { 
        jpx_codestream_target tgt =
          container.access_codestream(base_codestream_idx);
        if (tgt.exists()) out_box = tgt.open_stream();
      }
    virtual void close_image(kdu_codestream codestream)
      { 
        if (out_box == NULL) return;
        out_box->close(); out_box = NULL;
        base_codestream_idx++;
        if (base_codestream_idx >= num_base_codestreams)
          { 
            base_codestream_idx = 0;
            if (label_writer != NULL) label_writer->advance();
          }
      }
    virtual bool write(const kdu_byte *buf, int num_bytes)
      { 
        if (out_box == NULL) return false;
        return out_box->write(buf,num_bytes);
      }
  private: // Data
    jpx_container_target container;
    vcom_jpx_labels *label_writer;
    int num_base_codestreams; // We cycle around between the base codestreams
    jp2_output_box *out_box; // Non-NULL between `open_stream' & `close_stream'
    int base_codestream_idx; // Next one to be opened if `out_box' is NULL
  };
  /* Notes:
       This object allows a JPX file to be used as the actual compressed
     data target, with an indefinitely repeated JPX container.  Each call
     to `open_image' is translated to a `jpx_codestream_target::open_stream'
     call.  Each call to `close_image' closes the open codestream box and
     advances the internal notion of the next `jpx_codestream_target' object
     to be written.  In this way, the JPX target can be made to look like
     all the other compressed video targets. */

/*****************************************************************************/
/* EXTERN                  vcom_initialize_jpx_target                        */
/*****************************************************************************/

extern jpx_container_target
  vcom_initialize_jpx_target(jpx_target &tgt, jpx_source &prefix,
                             int num_output_components, bool is_ycc,
                             kdu_field_order field_order, kdu_args &args);
  /* This function is defined external so we can implement it in a separate
     source file -- this is only to avoid clutter in the main
     "kdu_v_compress.cpp" source file.  The function sets things up so
     that video can be written to a JPX target.  To do this, the function
     does the following things:
     1. Copies all metadata and imagery from `prefix' to `tgt', being sure
        to copy any JPX containers as having a known number of repetitions.
     2. Verifies that the `prefix' file did actually have a top-level
        Composition box, since this is required.
     3. Creates a single indefinitely repeated JPX container (the associated
        interface is the function's return value) to represent the video
        content.  The JPX container has one base codestream, unless the
        video is interlaced, in which case it has 2.  For each base
        codestream, the container has N base compositing layers, where
        N is the number of separate descriptions provided by the
        `-jpx_layers' argument (N is 1 if there is no `-jpx_layers' argument).
        A container embedded metadata label is added to each base
        compositing layer for explanatory purposes.
     4. Configures the colour space for each added compositing layer, along
        with the codestream registration information.  By default, the
        colour space information is derived from `num_output_components' and
        `is_ycc', unless there is a `-jpx_layers' argument.
  */

/*****************************************************************************/
/*                              vcom_jpx_layer                               */
/*****************************************************************************/

struct vcom_jpx_layer {
    vcom_jpx_layer()
      { space=JP2_sLUM_SPACE; num_colours=1; components[0]=0; next=NULL; }
    jp2_colour_space space;
    int num_colours;
    int components[4]; // We don't have any colour spaces with more than 4
    vcom_jpx_layer *next;
  };
  /* Note: this structure is used internally to implement the
     `vcom_initialize_jpx_target'; convenient to declare it here. */

/*****************************************************************************/
/*                             vcom_null_target                              */
/*****************************************************************************/

class vcom_null_target : public kdu_compressed_video_target {
  public: // Member functions
    virtual void open_image() { return; }
    virtual void close_image(kdu_codestream codestream) { return; }
    virtual bool write(const kdu_byte *buf, int num_bytes) { return true; }
  };
  /* Notes:
     This object simply discards any output that might be sent its way. */

/*****************************************************************************/
/*                              vcom_frame_buffer                            */
/*****************************************************************************/

struct vcom_frame_buffer {
  public: // Member functions
    vcom_frame_buffer()
      { 
        num_comps=0; comp_heights=comp_precisions=NULL; comp_signed=NULL;
        comp_buffers = NULL; buffer_handle = NULL;
      }
    ~vcom_frame_buffer()
      { 
        if (comp_buffers != NULL) delete[] comp_buffers;
        if (buffer_handle != NULL) delete[] buffer_handle;
      }
  public: // Data
    int num_comps; // Same as `vcom_frame_queue::num_comps'
    int sample_bytes; // Same as `vcom_frame_queue::sample_bytes'
    const int *comp_heights; // Ptr to `vcom_frame_queue::comp_heights'
    const int *comp_precisions; // Ptr to `vcom_frame_queue::comp_precisions'
    const bool *comp_signed; // Ptr to `vcom_frame_queue::comp_signed'
    kdu_byte **comp_buffers; // Ptr to start of each component's buffer
    size_t frame_bytes; // Total number of bytes in all component buffers
    kdu_byte *buffer_handle; // Passed to delete[] on cleanup
  };
  /* Note:
        All component buffers referenced by `comp_buffers' are actually
     contiguous in memory, so that the entire frame is represented by a
     single buffer that starts at `comp_buffers'[0] and runs for
     `frame_bytes' bytes in total. */

/*****************************************************************************/
/*                                 vcom_frame                                */
/*****************************************************************************/

#define VCOM_FRAME_STATE_READY    ((kdu_int32) 1)
#define VCOM_FRAME_STATE_WAKEUP   ((kdu_int32) 2)
#define VCOM_FRAME_STATE_END      ((kdu_int32) 4)

struct vcom_frame {
  public: // Member functions
    vcom_frame()
      { buffer=NULL; state.set(0); engine=NULL; next=prev=NULL; }
    ~vcom_frame()
      { if (buffer != NULL) delete buffer; }
  int get_frame_idx() const { return frame_idx; }
    /* Returns the index of the frame. */
  public: // Data members
    vcom_frame_buffer *buffer; // May be NULL if `state' is not ready
    kdu_interlocked_int32 state;
    vcom_engine *engine;
  private: // Data
    friend class vcom_frame_queue;
    int frame_idx;
    vcom_frame *next; // For building a doubly-linked list within the
    vcom_frame *prev; // `vcom_frame_queue' object.
  };
  /* Notes:
        This object is used to exchange frame sample data between the
     `vcom_frame_queue' and individual `vcom_engine' objects, and
     also between the `vcom_frame_queue' and the application (via
     `vcom_frame_queue::service_queue').
        A frame processing engine's main thread retrives `vcom_frame' objects
     from the `vcom_frame_queue' whenever it is in a position to start
     processing a new frame.  These objects are instantiated on demand and
     can be retrieved immediately without blocking the engine's main thread.
     However, the frame data itself might not be available immediately.
        To wait for frame data to become available, an engine's main thread
     manipulates the `state' member and then waits for the
     `engine->frame_wakeup' function to be called.  Depending on whether the
     frame processing engine is single-threaded or multi-threaded, that
     function may either signal a semaphore on which the engine's
     single thread is waiting or pass a `kdu_thread_entity_condition'
     reference to the `kdu_thread_entity::signal_condition' function to
     wake the engine's main thread from a working wait state.  The `engine'
     argument is non-NULL so long as the frame remains in the possession of a
     frame processing engine. 
        The following flags are defined for the `state' member:
     * `VCOM_FRAME_STATE_READY' -- set if the `buffer' contains valid samples
     * `VCOM_FRAME_STATE_END' -- set if the video source has been exhausted,
        so that this object's frame data will never become ready.
     * `VCOM_FRAME_STATE_WAKEUP' -- set by a frame processing engine while
          waiting for frame samples to become available; when the
          `VCOM_FRAME_STATE_READY' or `VCOM_FRAME_STATE_END' flag is asserted,
          if this flag was set, it is atomically cleared and the
          `engine->frame_wakeup' function is invoked.
     The `buffer' member might be NULL until such time as the
     `VCOM_FRAME_STATE_READY' flag is asserted.  This is because
     the `vcom_frame_queue' generally creates more `vcom_frame' objects than
     buffers so that calls to `vcom_frame_queue::get_frame_and_stream' usually
     succeeds immediately, even when the number of frame buffers is
     insufficient to actually start filling the returned frame with sample
     values.
  */

/*****************************************************************************/
/*                                 vcom_stream                               */
/*****************************************************************************/

class vcom_stream : public kdu_compressed_target {
  public: // Member functions
    vcom_stream()
      { min_slope_threshold=0; codestream_bytes = compressed_bytes = 0;
        failed=false; buffer=NULL;  buf_pos=buf_size=0;  restore_pos=0;
        frame_idx=-1; next=prev=NULL; }
    ~vcom_stream()
      { if (buffer != NULL) delete[] buffer; }
    int get_frame_idx() const { return frame_idx; }
      /* Returns the index of the frame to which this stream belongs. */
    bool check_failed() const { return failed; }
      /* Returns true if attempts to allocate sufficient memory to hold
         the codestream failed -- this is not a fatal condition, but of
         course it means that the data was lost. */
    vcom_stream *restart(kdu_uint16 min_slope)
      { 
        buf_pos=0; restore_pos=0; this->min_slope_threshold=min_slope;
        codestream_bytes = compressed_bytes = 0;
        return this;
      } 
      /* Prepares to start collecting compressed data again from scratch;
         it also resets the codestream statitics to zero and sets the
         `min_slope_threshold' member to `min_slope' so that the relevant
         compression engine can use this information to help generate
         compressed data efficiently.  The function is normally invoked only
         by `vcom_frame_queue' right before returning a stream to the calling
         compression engine from its `get_frame_and_stream' function.  However,
         if a compression engine has been asked to repeatedly compress a frame
         (for timing purposes), the function is invoked before each repetition,
         resetting `min_slope' to the `min_slope_threshold' value that was
         present when the repeated compression process was initiated. */
    bool write_contents(kdu_compressed_target *tgt)
      { // Convenience function to write everything to a video target. */
        size_t pos, xfer_bytes=0;
        for (pos=0; pos < buf_pos; pos+=xfer_bytes)
          { 
            xfer_bytes = buf_pos - pos;
            if (xfer_bytes > (size_t)(1<<28)) // Reasonable chunk size
              xfer_bytes = (size_t)(1<<28);
            if (!tgt->write(buffer+pos,(int)xfer_bytes))
              return false;
          }
        return true;
      }
  public: // Function overrides
    bool write(const kdu_byte *src_buf, int num_bytes)
      { 
        if (failed)
          return true; // Let the write appear to succeed, but we will catch
                       // the failure when we come to write to the target file
        size_t new_buf_pos = buf_pos + num_bytes;
        if (new_buf_pos > buf_size)
          { // Allocate more buffer space -- this might fail
            size_t new_size = new_buf_pos + buf_size; // Grow quickly
            kdu_byte *new_buf = new(std::nothrow) kdu_byte[new_size];
            if (new_buf == NULL)
              { 
                failed = true;
                return true; // As above, let the write appear to succeed here
              }
            if (buf_pos > 0)
              memcpy(new_buf,buffer,buf_pos);
            if (buffer != NULL)
              delete[] buffer;
            buffer = new_buf;
            buf_size = new_size;
          }
        memcpy(buffer+buf_pos,src_buf,(new_buf_pos-buf_pos));
        buf_pos = new_buf_pos;
        return true;
      }
    bool start_rewrite(kdu_long backtrack)
      { // Note: we don't allow backtracking to the very first byte here
        if ((restore_pos > 0) || (backtrack < 0) ||
            (((size_t)backtrack) >= buf_pos))
          return false;
        restore_pos = buf_pos;
        buf_pos -= (size_t)backtrack;
        return true;
      }
    bool end_rewrite()
      { 
        if (restore_pos == 0)
          return false;
        buf_pos = restore_pos;
        restore_pos = 0;
        return true;
      }
  public: // Data exchanged between the frame queue and compression engines
    kdu_uint16 min_slope_threshold; // Passed both ways -- see below
    kdu_long codestream_bytes; // From `kdu_codestream::get_total_bytes'
    kdu_long compressed_bytes; // From `kdu_codestream::get_packet_bytes'
  private: // Data
    friend class vcom_frame_queue;
    bool failed; // If we were unable to allocate enough storage
    kdu_byte *buffer; // This is where we store data
    size_t buf_pos; // Current write ptr, relative to start of `buffer'
    size_t restore_pos; // For implementing the rewrite capability
    size_t buf_size; // Max bytes that `buffer' can hold
    int frame_idx;
    vcom_stream *next; // For building a doubly-linked list within the
    vcom_stream *prev; // `vcom_frame_queue' object.
  };
  /* Notes:
        This object provides a memory-buffered compressed data target, whose
     purpose is to capture the contents of a compressed codestream produced
     by a frame processor.  The `vcom_frame_queue' object serves up these
     objects and receives them back again once the codestream has been
     generated; the frame queue orders the compressed codestreams and writes
     them to the output file.
        The object can also exchange compression statistics between the
     `vcom_frame_queue', the `vcom_engines' which use it, and the application.
     In particular `min_slope_threshold' plays an important role.  When a
     stream is passed to `vcom_frame_queue::return_generated_stream', the
     `min_slope_threshold' member should hold the slope threshold that was
     used to generate the final quality layer.  The frame queue samples these
     slope thresholds from multiple frames/engines in order to come up with
     values that might be expected for future frames and passes these back
     to the compression engines via the `vcom_stream' objects it passes along
     in successful calls to `vcom_frame_queue::get_frame_and_stream'.  If a
     compression engine finds a non-zero value for this member in the stream
     to which it is about to compress data, it can pass the value along to
     `kdu_codestream::set_min_slope_threshold'.  In practice, this is done
     by passing the value to `kdu_stripe_compressor::start'.
 */

/*****************************************************************************/
/*                             vcom_frame_queue                              */
/*****************************************************************************/

#define VCOM_SLOPE_PREDICT_HISTORY 2
  /* Number of most recently received compressed streams from which to
     estimate minimum slope threshold for future frames -- we just take the
     minimum of the non-zero values as the predictor right now, so larger
     values for this parameter will tend to make the prediction algorithm
     very conservative. */

class vcom_frame_queue {
  public: // Lifecycle functions
    vcom_frame_queue();
    ~vcom_frame_queue();
    void init(int max_frames_to_read, int max_buffered_frames,
              int max_buffered_streams, int num_source_comps,
              const kdu_coords source_comp_sizes[],
              int sample_bytes, int bits_used, bool lsb_aligned,
              bool is_signed);
      /* With loss of only a little generality, all source components are
         required to have the same numerical representation and byte order,
         as given by `sample_bytes', `bits_used', `lsb_aligned'
         and `is_signed'.  It is expected that the `kdu_codestream' objects
         used to compress the frame data are configured to assign all image
         components values of `Sprecision'=`bits_used' and
         `Signed'=`is_signed'.  However, image components can have different
         dimensions, as given by the `source_comp_sizes' array.  If
         `lsb_aligned' is true, the `bits_used' bits of each
         `sample_bytes'-sized input sample value run from bit 0 to bit
         `bits_used'-1.  Otherwise, the `bits_used' run from the MSB of
         each `sample_bytes'-sized input word down.
            The `max_buffered_frames' argument indicates the maximum number
         of frame buffers the object is prepared to maintain.  The
         `service_queue' function can provide frame samples to the object at
         a faster rate than they are consumed by the compression engines,
         until all such frame buffers are in use, after which it must wait
         for compression engines to return used frame buffers.
            The `max_buffered_streams' argument is similar in that it
         represents the maximum number of buffers that can be maintained for
         holding compressed codestreams.  This value should be at least as
         large as `max_buffered_frames', but a somewhat larger value will
         help to avoid stalls in the event that compression engines process
         their frames at very different speeds, since compressed streams
         must be returned in-order via the `service_queue' function.
      */
  public: // Functions invoked by or on behalf of compression engines.
    vcom_frame *get_frame_and_stream(vcom_stream * &stream);
      /* Invoked by a frame compression engine when it is getting ready to
         process a new frame.  The returned frame might not yet have been
         filled, as explained in the notes following the `vcom_frame' object.
            This function returns NULL if it can be determined that there are
         no further frames available to process, which might occur if the
         `max_frames_to_read' limit passed to `init' is reached, or if the
         `terminate' function has been called.  However, it may well happen
         that a frame is successfully retrieved by this function and later
         found to lie beyond the end of the video source, as indicated by the
         `vcom_frame::state' member's `VCOM_FRAME_STATE_END' flag.
            A successful call to this function sets `stream' to refer to the
         `vcom_stream' object to which the engine should write its compressed
         data.  The frame itself should be returned to `return_processed_frame'
         once all sample data has been used, after which the `stream' object
         should be returned via `return_generated_stream' -- this may happen
         in a different thread.
            This function will not normally block, since `vcom_frame' objects
         are constructed to meet the demand, even if they cannot yet be
         assigned buffers.  However, the function may block the caller if
         there are no more `vcom_stream' objects available -- can happen
         if one compression engine is much slower than the rest so that
         a hole in the completed stream list prevents the writing of other
         streams to disk.  This can be rendered unlikely by ensuring that
         the maximum number of streams is significantly larger than the
         maximum number of buffered frames. */
    void return_processed_frame(vcom_frame *frame);
      /* Each call to `get_frame_and_stream' must be followed by a call to this
         function, once the frame is no longer required by the compression
         engine that retrieved it. */
    void return_generated_stream(vcom_stream *stream);
      /* Each call to `get_frame_and_stream' must be followed by a call to this
         function, once the compressed codestream has been written to
         `stream'.  If something has gone wrong, however, you should call
         `return_aborted_stream' instead.  If the end of the video source
         was encountered while waiting for the accompanying frame to be
         filled with valid samples, `stream' should be passed to
         `return_unused_stream', since there was no error, but nothing was
         generated either. */
    void return_unused_stream(vcom_stream *stream);
      /* Same as above, but the `stream' is moved immediately to the internal
         free list. */
    void return_aborted_stream(vcom_stream *stream,
                               kdu_exception exception_code);
      /* This function is called from a processing engine if an exception
         condition occurred while processing the frame -- the condition will
         most likely already have generated a call to `kdu_error', although
         memory allocation problems caught as `std::bad_alloc' are converted
         to `KDU_MEMORY_EXCEPTION' so that their nature is not lost.  The
         function causes an exception to be thrown in the queue management
         thread at the earliest convenience, using the supplied
         `exception_code'.  In practice, this exception is thrown on the
         next call to `service_queue', if one is not already in
         process.
      */
  public: // Functions invoked by the management thread
    bool service_queue(vcom_frame * &frame, vcom_stream * &stream,
                       bool blocking, bool no_more_frames);
      /* [SYNOPSIS]
           This function is called by the queue management thread to collect
           objects that need to be served.  The function returns a pointer to
           at most one frame and one compressed stream via the supplied
           arguments.  If a frame is available to be filled by the
           application, `frame' will be non-NULL upon return; similarly,
           if a compressed stream is available to be consumed by the
           application, `stream' will be non-NULL upon return.  On entry, the
           `frame' and `stream' arguments should either be NULL or point to
           objects that were returned previously and have subsequently been
           filled with valid sample values (`frame') or consumed in any desired
           manner (`stream').
           [//]
           It is not strictly necessary for you to fill frames or consume
           streams immediately, but if you receive a non-NULL `frame' value
           upon calling this function and then call it again with a NULL
           `frame' argument, you will get back the same `frame'.  Similarly,
           if you get back a non-NULL `stream' value from one call and then
           issue a subsequent call with a NULL `stream' argument, you will
           get back the same `stream'.
           [//]
           You can obtain the frame index associated with any non-NULL
           `frame' or `stream' returned by this function by calling
           `vcom_frame::get_frame_idx' or `vcom_stream::get_frame_idx',
           as appropriate, but these frame indices are guaranteed to be
           ordered sequentially.
         [RETURNS]
           True unless the function is not returning any `frame' or `stream'
           and there are no more frames or streams that can possibly
           be returned in the future.
         [ARG: blocking]
           If true, the function blocks the caller until it can return with
           at least one of `frame' or `stream' non-NULL, or it can be
           determined that there will be no more frames or streams.  Otherwise,
           the function just polls the state of the queue.
         [ARG: no_more_frames'
           If true, the caller is informing the object that there will be no
           more frame samples.  If `frame' is non-NULL on entry when the
           function is called with `no_more_frames'=true, the `frame' being
           returned to the function is the last frame.  Otherwise, the last
           `frame' passed back to the queue via this function was the last
           one in the video source.
      */
    void terminate();
      /* [SYNOPSIS]
         This function may be called directly from the top level application,
         or indirectly via `vcom_engine::shutdown'.  The latter occurs if a
         call to `vcom_engine::shutdown' with `graceful'=false found the
         engine to be blocked.  In either case, this function ultimately
         unblocks any threads which are blocked on `get_frame_and_stream',
         or while waiting for frame samples to be filled, and ensures that
         these and all future functions return NULL.  If an exception occurred
         within the queue management thread, it should invoke this function
         before attempting to clear things up.
      */
  private: // Configuration parameters
    int max_source_frames;
    int max_buffered_frames;
    int max_allocated_streams;
    int num_comps; // Number of source components
    int sample_bytes; // Bytes in each source sample (all components)
    int *comp_heights; // Num rows in each source component
    int *comp_precisions; // As passed to `kdu_stripe_compressor::push_stripe'
    bool *comp_signed; // As passed to `kdu_stripe_compressor::push_stripe'
    size_t *comp_bytes; // Num bytes buffered for each component
    size_t frame_bytes; // Total bytes in each input frame
  private: // List of `vcom_frame' objects and associated info
    vcom_frame *head_frame; // Oldest still in use by some compression engine
    vcom_frame *first_unaccessed_frame; // Next one for `get_frame_and_stream'
    vcom_frame *first_unfilled_frame; // Oldest frame whose sample need filling
    vcom_frame *first_unbuffered_frame; // Oldest frame with no buffer yet
    vcom_frame *tail_frame; // Most recent frame, filled or otherwise.
    int num_buffered_frames; // See below
    int next_tail_frame_idx;
    int next_access_frame_idx;
    int next_fill_frame_idx; // Next frame to be filled via `service_queue'
  private: // List of `vcom_stream' objects and associated info
    vcom_stream *first_active_stream;
    vcom_stream *last_active_stream;
    vcom_stream *unconsumed_streams;
    vcom_stream *free_streams; // List of recycled (spare) streams
    int next_consume_frame_idx;
    int num_allocated_streams;
    kdu_uint16 recent_min_slope_thresholds[VCOM_SLOPE_PREDICT_HISTORY];
  private: // Progress indicators
    bool terminated; // True if queue should appear empty to engines
    bool exception_raised; // Set only by `return_aborted_stream'
    kdu_exception last_exception_code; // From `return_aborted_stream' call
  private: // Synchronization members
    kdu_mutex mutex; // Protects members accessed by engine and service threads
    bool service_waiting; // True if the `service_queue' function is blocked
    kdu_event service_wakeup; // `service_queue' calls may wait on this
    int engines_waiting; // Num engines blocked calling `get_frame_and_stream'
    kdu_event engine_wakeup; // `get_frame_and_stream' callers may wait on this
  };
  /* Notes:
        All `vcom_frame' objects are kept in a single ordered doubly-linked
     list that runs from `head_frame' to `tail_frame'.  When a new frame is
     appended to the tail, it takes the `next_tail_frame_idx' index, which is
     then incremented.  The `num_buffered_frames' value indicates the number of
     initial elements of the list headed by `head' that have non-NULL
     `buffer' members; these are frames that have been filled with new sample
     values and are actively being used, or frames that have been or are about
     to be filled in advance of the point at which engines access them.  This
     value may not exceed `max_buffered_frames', which will never be smaller
     than the number of compression engines.  At the end of the list may be
     one or more unbuffered frames, starting from the one identified by the
     `first_unbuffered_frame' member.  When a compression engine returns one
     of its frames, that frame is appended to the tail of the list and its
     buffer is moved across to the `first_unbuffered_frame', advancing that
     pointer and potentially allowing another frame to be filled.
        The `vcom_stream' objects are kept on one of three lists:
     1) The doubly-linked list delimited by `first_active_stream' and
     `last_active_stream' contains streams that are currently in use by
     compression engines, but have not yet been completed and passed back to
     the `return_generated_stream' function.
     2) The singly-linked list headed by `unconsumed_streams' holds an ordered
     collection of streams that have been fully generated but have not yet
     been consumed via the `service_queue' function.  This list is ordered,
     but may contain holes.  The index of the frame whose stream should be
     consumed next is given by the `next_consume_frame_idx' member, which
     might be less than `unconsumed_streams->frame_idx' if a hole has been
     encountered.
     3) The singly-linked list headed by `free_streams' contains an unordered
     collection of streams that have been consumed and are not currently
     active, waiting to be recycled.
        The `num_allocated_streams' member keeps track of the total number
     of `vcom_stream' objects that have been allocated so far.  This value
     may not exceed the `max_allocated_streams' parameter that is usually
     larger than `max_buffered_frames'.  If the `max_allocated_streams' limit
     is reached, calls to `get_frame_and_stream' must block until a stream
     becomes free, which requires it to be returned by any compression engine
     that is holding up the works and written to disk (in order).
  */

/*****************************************************************************/
/*                              vcom_processor                               */
/*****************************************************************************/

class vcom_processor : public kdu_thread_queue {
  public: // Member functions
    vcom_processor();
    ~vcom_processor() { reset(); }
    void init(kdu_codestream codestream, int num_layer_specs);
      /* Donates the `codestream' to this object and prepares it for
         operation, allocating space to record compressed data statistics
         for `num_layer_specs' quality layers. */
    void init(vcom_processor &src, vcom_stream *stream);
      /* Same as above, but initializes a second processor to use the same
         configuration as `src'.  The `src' processor must already have been
         initialized.  This function creates a new `codestream' within the
         current object and copies its coding parameters from those found in
         `src.codestream', setting `stream' as the initial compressed data
         target for the newly created `codestream'. */
    void reset();
      /* Restores the object to its initial state so it can be reused.  This
         function destroys any `codestream' that is still active and also
         invokes `kdu_stripe_compressor::reset', which ensures that internal
         resources are deleted regardless of whether the
         `kdu_stripe_compressor::finish' function was called or not.  It
         is imperative, however, that this function is not called while
         multi-threaded processing might be continuing within the
         `compressor', so in multi-threaded applications, you should be
         sure to call `terminate' before this function is invoked. */
    void start_frame(kdu_long *layer_sizes_in, kdu_uint16 *layer_slopes_in,
                     bool trim_to_rate, bool predict_slope, bool force_precise,
                     bool want_fastest, bool skip_codestream_comments,
                     double rate_tolerance, int double_buffering_height,
                     const kdu_push_pull_params *pp_params,
                     vcom_frame *frame, vcom_stream *stream,
                     kdu_thread_env *env);
      /* If this is the first frame being started, `codestream.enable_restart'
         is called, otherwise, the `codestream' is restarted with `stream' as
         its compressed data target.  The other parameters are used to
         call the `kdu_stripe_compressor::start' function. */
    kdu_long push_samples(vcom_frame *frame,
                          kdu_long next_queue_sequence_idx=0);
      /* Pushes all of the frame samples found in `frame' into the
         `kdu_stripe_compressor::push_stripe' function, using the
         specifications found in the `frame' object itself to determine
         how this should be done.
            The function executes two calls to the
         `kdu_stripe_compressor::get_set_next_queue_sequence' function:
         one before pushing any sample values in, to adjust the thread-queue
         sequence index that the stripe-compressor will use next to
         the value of `next_queue_sequence_idx', andone at the end to
         determine the thread-queue sequence index it would use next if
         it needed to create more thread-queues in the future -- this
         becomes the function's return value.  The caller passes
         the return value from this function as the second argument when
         the function is called again, whether on the same `vcom_processor'
         or a different one, so that successive frame processors use
         thread-queues with monotonically advancing sequence indices, no
         matter how many such thread queues each compressor must instantiate
         internally.
            In single-threaded operation, the `next_queue_sequence_idx'
         argument should be zero, the function will return zero, and
         thread-queue sequence numbers are not used in any event. */
    void end_frame(vcom_stream *stream, vcom_frame_queue *queue,
                   kdu_thread_env *env);
      /* Each call to `start_frame' must be matched by one to `end_frame'.
         The function may be invoked synchronously (`env'=NULL) or
         asynchronously (`env' != NULL).  The primary role of the
         function is to invoke `kdu_stripe_compressor::finish'.
         [//]
         Additionally, if `stream' is non-NULL, the function records the final
         quality layer's slope threshold in the `stream->min_slope_threshold'
         member and records other statistics that become available after the
         final codestream flushing operation in `stream->codestream_bytes' and
         `stream->compressed_bytes', before passing `stream' to the
         `queue->return_generated_stream' function.
         [//]
         If the `stream' is non-NULL and `kdu_stripe_compressor::finish' call
         returns false, the function generates a suitable error message through
         `kdu_error' to report the fact that the compression of the frame was
         incomplete.
         [//]
         In the asynchronous case (`env' != NULL), the function schedules a
         single "flush processing" job to be run in the next available thread
         and that job is the one that completes the actions described above.
         In this case, the object should not be used again without first
         calling the `wait_until_ready' function. */
  public: // Functions used only by multi-threaded engines
    int get_max_jobs() { return 1; }
      /* Overrides `kdu_thread_queue::get_max_jobs' to indicate that this
         is a thread queue to which at most 1 job can be scheduled but not
         yet launched, at any given time.  This is necessary because the
         object invokes its `schedule_job' function to schedule the final
         call to `kdu_stripe_compressor::finish' that flushes codestream
         content -- we want this to be able to fully overlap the processing
         of data in another processor that shares the same multi-threaded
         processing environment. */
    bool wait_until_ready(kdu_thread_env *caller);
      /* Called by the owning engine's main thread (`caller') to wait for
         any asynchronous codestream flushing work to complete (see
         `end_frame') before the `codestream' and `compressor' can
         be reused.  The function returns false only if the `init' function
         has not yet been called, which happens when a second processor is
         about to be used for the first time within
         `vcom_engine::run_multi_threaded'. */
    void no_more_jobs(kdu_thread_env *caller);
      /* This function is invoked once we can be sure that there will be no
         further jobs scheduled by calls to `end_frame' that supply a
         non-NULL `env' argument.  We need this, because the
         `kdu_thread_queue::all_done' function must be called before the
         engine's main thread invokes `kdu_thread_env::join' to wait upon
         orderly completion of all work.
            If the function finds that there are no outstanding
         scheduled end-frame processing jobs, it directly invokes the
         `all_done' function itself.  Otherwise, it uses the `ready_state'
         member to deposit a flag that will be picked up by the final
         `end_frame_job' when it runs, from which `all_done' will be
         invoked. */
    void terminate(kdu_thread_env *caller, kdu_exception exc_code);
      /* This function should be invoked before destroying the multi-threaded
         environment that a processor might be using.  If everything has
         already been closed down, the function does nothing.  Otherwise,
         this function terminates any processing which is going on as soon
         as possible and then cleans up all resources.  The function first
         invokes `kdu_thread_queue::terminate' on the base thread-queue, from
         which any active stripe-compressor is also descended.  Thereafter,
         the function can safely clean up the stripe-compressor via
         `kdu_stripe_compressor::reset', invoke `cs_terminate', destroy
         any `codestream' that is still alive, and return any
         `end_frame_stream' that may still be sitting around to the
         `end_frame_queue', passing `exc_code' to the
         `vcom_frame_queue::return_aborted_stream' function. */
  private: // Helper functions
    void do_end_frame(kdu_thread_env *caller);
  private: // Declarations
    class vcom_end_frame_job : public kdu_thread_job {
      public: // Functions
        void init(vcom_processor *owner)
          { this->processor = owner;
            set_job_func((kdu_thread_job_func) do_end_frame); }
        static void do_end_frame(vcom_end_frame_job *job,
                                 kdu_thread_env *caller)
          { job->processor->do_end_frame(caller); }
      private: // Data
        vcom_processor *processor;
     };
  private: // Data
    kdu_codestream codestream;
    kdu_stripe_compressor compressor;
    int num_frames_started;
    int num_frames_ended;
    int num_layer_specs;
    kdu_long *last_layer_sizes; // Lengths passed back by `compressor.finish'
    kdu_uint16 *last_layer_slopes; // Slopes passed back by `compressor.finish'
    int flush_flags; // Based on layer specs passed to `start_frame'
  private: // Data members used to implement background flushing
    vcom_stream *end_frame_stream; // Saved by `end_frame' for `end_frame_job'
    vcom_frame_queue *end_frame_queue; // Also saved for `end_frame_job' to use
    vcom_end_frame_job end_frame_job;
    kdu_interlocked_int32 ready_state;
    kdu_thread_entity_condition *ready_waiter;
  };
  /* Notes:
       The `ready_state' member is used to manage synchronisation with the
     background `end_frame_job' that does the work of `end_frame' in a
     thread that usually differs from the main compression engine thread.
     The main thread waits upon completion of these activities inside he
     `wait_until_ready' function.  The `ready_state' member may be understood
     as a set of four flag bits, as follows:
        Bit-0 is set if there is an `end_frame_job' in progress.
        Bit-1 is set if there is a valid `ready_waiter' reference that should
              be used to wake up the waiter when the `end_frame_job' finishes.
        Bit-2 is set if `no_more_jobs' has been called so that there will
              be no further `end_frame_job' functions scheduled; if an
              `end_frame_job' function finds this condition when it goes to
              clear bit-0, it should invoke `all_done'.
        Bit-3 is set if `terminated' has been called; if `do_end_frame'
              detects this condition on entry, it can terminate early, calling
              `all_done' and leaving the `end_frame_stream' behind to be
              cleaned up by the in-progress call to `terminate'.
  */

/*****************************************************************************/
/*                                vcom_engine                                */
/*****************************************************************************/

class vcom_engine {
  public: // Member functions
    vcom_engine();
      /* [SYNOPSIS]
           Remember to call `startup' to start the engine runing.
      */
    ~vcom_engine() { shutdown(false); }
      /* [SYNOPSIS]
         Calls `shutdown' (non-gracefully), in case it has not already been
         called and then destroys the object's resources.
      */
    void startup(kdu_codestream codestream,
                 vcom_frame_queue *queue, vcom_frame *frame,
                 vcom_stream *stream, int engine_idx,
                 const kdu_thread_entity_affinity &engine_specs,
                 int num_layer_specs, const kdu_long *layer_bytes,
                 const kdu_uint16 *layer_thresholds,
                 bool trim_to_rate, bool skip_codestream_comments,
                 bool predict_slope, double rate_tolerance,
                 int thread_concurrency, int double_buffering_height,
                 bool want_fastest, bool want_precise,
                 const kdu_push_pull_params *params=NULL,
                 int extra_compression_reps=0);
      /* [SYNOPSIS]
         Use this function to start the engine's master thread and any
         additional threads which it should control.
         [//]
         The `codestream' interface supplied here is used by the engine's
         first processor; if another processor is required for sequenced
         multi-threaded processing, a copy of the `codestream' is created.
         In any event the supplied `codestream' is owned by the engine
         henceforth and must be destroyed when the engine is deleted.
         [//]
         The `frame' and `stream' arguments refer to objects already
         retrieved from the `queue' by calling `queue->get_frame_and_stream';
         these are the frame and stream associated with the first frame to be
         processed by this engine and they should be returned via the
         `queue->return_processed_frame' and `queue->return_generated_stream'
         functions once ready.  Subsequent frame/stream pairs are obtained by
         the engine's master thread, which calls `queue->get_frame_and_stream'
         itself henceforth.
         [//]
         The `layer_bytes' and `layer_thresholds' arrays each contain
         `num_layer_specs' specifications, if non-NULL, that are copied
         internally.
         [//]
         The `engine_specs' object defines the number of threads to assign
         to the engine, along with any CPU affinity information that needs
         to be passed to `kdu_thread_entity::set_cpu_affinity'.  If the
         value returned by `engine_specs.get_total_threads' is 1, Kakadu's
         single threaded processing environment will be used, rather than the
         multi-threaded environment with only one thread.  This is OK, since
         disjoint processing engines running on different threads have
         different codestreams and do not directly exercise any shared I/O
         operations.
         [//]
         The `thread_concurrency' argument is relevant only for operating
         systems which do not automatically allocate all CPUs to each
         running process -- Solaris is one example of this.  The value of
         the `thread_concurrency' argument should be the same for all
         constructed engines; it should be set to the number of CPUs that
         you would like to be accessible to the process.  In most cases,
         this should simply be equal to the total number of frame processing
         threads, accumulated over all frame processing engines, or the
         number of known system CPUs, whichever is larger -- since knowledge
         of CPUs can be unreliable.
         [//]
         If `double_buffering_height' is -ve, a value is selected automatically
         (may be 0 -- no single-threaded DWT), depending on the number of
         threads associated with the frame processing engine; otherwise, the
         supplied value is used to configure double/single buffered DWT
         processing for the frame processing engine.
         [//]
         The object referenced by `params' carries any parameters that might
         have been supplied by the "-bc_jobs" command-line argument, if
         available, through to the block encoding machinery.  This information
         is relevant only for the Kakadu speed-pack and only in multi-threaded
         mode.
         [//]
         If `extra_compression_reps' > 0, each frame is actually compressed
         multiple times into the relevant `vcom_stream' object, resetting
         the stream between calls; only the first generated codestream's
         data is actually passed back to the frame queue on calls to
         `queue->return_generated_stream'.
      */
    void shutdown(bool graceful);
      /* [SYNOPSIS]
         This function should be called from the queue management thread -- the
         same one which called `startup'.  It requests an orderly termination
         of the engine's master thread, along with any additional threads it
         manages, at the earliest convenience.
         [//]
         If `graceful' is true, the engine will shut down only after
         completing the processing of any outstanding frames and passing the
         `vcom_frame' and `vcom_stream' objects it still has back to the
         `vcom_frame_queue' object's `frame_used' and `stream_generated'
         functions.  If there was an internal error, the function returns
         its `vcom_stream' objects to
         `vcom_frame_queue::return_aborted_stream'.
         [//]
         If `graceful' is false, the engine will stop processing as soon as
         possible and it will not return any partially processed frames
         to the frame queue -- there is no need for this, since the frame
         queue keeps a record of all frames it has ever created, for
         destruction purposes.  If the engine is found to be blocked in
         a call to `vcom_frame_queue::get_frame_and_stream' or while waiting
         for frame data to be filled, the `vcom_frame_queue::terminate'
         function is called, which ensures that this engine (and all others)
         will be immediately unblocked and exit their processing loops.  It
         follows that non-graceful shutdown is appropriate only when we wish
         to terminate all engines -- typically due to the occurrence of an
         error.
         [//]
         Once an engine has been shutdown, it can be started again using the
         `startup' function, although it is unclear how useful this
         capability actually is.
      */
    void frame_wakeup(vcom_frame *frame);
      /* Called by the `vcom_frame_queue' object if the engine's main thread
         was found to have been waiting for the `frame' to enter the
         `VCOM_FRAME_STATE_READY' state when it became ready (filled with
         valid sample values) or an error occurred -- in the latter case,
         the `VCOM_FRAME_STATE_END' flag will be found in `frame->state'. */
  private: // Member functions
    friend kdu_thread_startproc_result
           KDU_THREAD_STARTPROC_CALL_CONVENTION engine_startproc(void *);
    void run_single_threaded();
    void run_multi_threaded();
      /* The engine's master thread runs in one of these two functions. */
    bool wait_for_active_frame();
      /* Waits until there is a fully-read `active_frame' or else we can
         be sure that there will be no more frames.  If `active_frame' is
         already non-NULL, we only wait for it to become ready.  Otherwise,
         we collect a new `active_frame' and its accompanying `active_stream'
         from the `queue'.  The function returns false if a non-NULL
         `active_stream' cannot be obtained or it cannot become ready
         (i.e., if `VCOM_FRAME_STATE_END' is foundin `active_frame->state'.
            If the end of the source is encountered, but `active_stream' is
         non-NULL, this function returns `active_frame' and `active_stream' to
         the `queue' itself, leaving both members NULL.  This prevents the
         `active_stream' from being sent to `queue->return_aborted_stream'. */
    void return_active_frame();
      /* This function can safely be called even if `active_frame' is NULL. */
  private: // Fixed parameters
    int engine_idx; // Identifies us
    int num_threads; // Num threads controlled by this engine
    int thread_concurrency; // See `startup'
    kdu_thread_entity_affinity cpu_affinity;
    int double_buffering_height; // -1 auto-selects single/double buffered DWT
    bool want_fastest;
    bool force_precise;
    bool predict_slope;
    bool trim_to_rate;
    bool skip_codestream_comments;
    double rate_tolerance;
    int num_layer_specs;
    kdu_long *layer_sizes;
    kdu_uint16 *layer_slopes;
    kdu_push_pull_params pp_params; // Copied from last argument to `startup'
    int extra_compression_reps;
  private: // Objects and state information
    vcom_processor processors[2]; // Only one used in single-threaded mode
    vcom_stream dummy_streams[2]; // Used only with extra compression reps
    int active_processor; // 0 or 1; frame samples are pushed to this one
    vcom_frame_queue *queue;
    vcom_frame *active_frame; // Non-NULL until frame pushed to active proc
    vcom_stream *active_stream; // Stream we obtained along with `active_frame'
    kdu_thread_env *thread_env; // Main thread's env-ref if multi-threaded
    kdu_semaphore wait_semaphore; // Main thread waits here if single-threaded
    kdu_thread_entity_condition *wait_condition; // Used in multi-threaded case
    bool waiting_for_frame; // If main thread may be blocked
    bool graceful_shutdown_requested;
    bool immediate_shutdown_requested;
    char master_thread_name[81]; // Makes debugging a bit easier
    kdu_thread master_thread;
  };
  
} // namespace kdu_supp_vcom

#endif // KDU_VCOM_H
