/*****************************************************************************/
// File: v_expand_local.h [scope = APPS/V_DECOMPRESSOR]
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
   Local class declarations for "kdu_v_compress.cpp".
******************************************************************************/

#ifndef V_EXPAND_LOCAL_H
#define V_EXPAND_LOCAL_H

#include "kdu_video_io.h"
#include "jpx.h"

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_v_expand" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace

// Create type definition for fast SIMD data transfer accelerators that
// we may import from the `kdu_stripe_decompressor' implementation by including
// "stripe_decompressor_local.h" directly into the kdu_v_expand project.  The
// actual inclusion of this header happens inside "kdu_v_expand.cpp".
// This is really a hack here, since we should instead be using the
// public `kdu_stripe_decompressor' API's directly -- that is what we expect
// to do in future releases, but it is simpler for the moment just to
// borrow the fast data transfer functions defined within that API.
typedef void (*simd_transfer_func)(void *dst, void **src, int width,
                                   int precision, int orig_precision,
                                   bool is_absolute, bool dst_signed,
                                   int store_preferences);

// Defined here
class kdv_simple_video_source;
class kdv_io_queue;
class kdv_jpx_source;
class kdv_decompressor;

/*****************************************************************************/
/*                          kdv_simple_video_source                          */
/*****************************************************************************/

class kdv_simple_video_source : public kdu_simple_video_source {
  public: // Member functions
    int open_stream(int field_idx, jp2_input_box *input_box)
      { /* Overrides the base virtural function
           `kdu_compressed_video_source::open_stream'. */
        if (field_idx != 0) return -1;
        int result = open_image();
        kdu_long num_bytes = get_remaining_bytes();
        if ((result >= 0) && (num_bytes < INT_MAX))
          { 
            tmp_family_src.open(this);
            input_box->open_as(jp2_codestream_4cc,&tmp_family_src,
                               jp2_locator(),jp2_locator(),num_bytes);
            if (!input_box->load_in_memory((int)num_bytes))
              result = -1;
            tmp_family_src.close();
          }
        close_image();
        return result;
      }
  private: // Data
    jp2_family_src tmp_family_src;
  };
  /* Notes:
        This class builds upon `kdu_simple_video_source' by implementing the
     `open_stream' virtual function from the base class, thereby providing
     a mechanism for overlapped reading from the simple video source that is
     completely compatible with the method used for reading from JPX and MJ2
     files.  The `open_stream' function cannot be implemented in the
     "kdu_video_io.h" file where `kdu_simple_video_source' and its base
     `kdu_compressed_video_source' classes are defined, because the
     `jp2_input_box' type is not available at that point.
        The approach taken is very simple, although it relies upon having
     sufficient memory to load a compressed video frame into memory.
     Essentially, `open_image' is called, then a `jp2_family_src' is
     opened temporarily to use the `kdu_simple_video_source' as its input,
     then `jp2_input_box::open_as' is used to provide access to the
     `jp2_family_src' object, and then all data is loaded via
     `jp2_input_box::load_in_memory', after which the `jp2_family_src'
     object is immediately closed and `close_image' is invoked, leaving
     the `jp2_input_box' object with a memory resident copy of all relevant
     content.  This process could be made more efficient if it turns out
     to be important.
  */

/*****************************************************************************/
/*                               kdv_io_queue                                */
/*****************************************************************************/

class kdv_io_queue : public kdu_thread_queue {
  public:
    kdv_io_queue(int max_frames);
    ~kdv_io_queue();
    int get_max_jobs() { return 2; }
      /* Overrides `kdu_thread_queue::get_max_jobs' to indicate the max
         number of jobs that can be scheduled but not yet executed, at any
         given time -- this is necessary because the object invokes
         its `schedule_job' function. */
    void processor_joined(kdv_decompressor *decompressor,
                          kdu_thread_env *caller);
      /* This function is called when the `io_queue' is first passed into a
         `kdv_decompressor' object -- it schedules the first frame loading
         job for that compressor, so that the `wait_for_io' function
         will be able to succeed. */
    void frame_processed(kdv_decompressor *decompressor,
                         kdu_thread_env *caller);
      /* This function is called by `kdv_decompressor::process_frame' right
         before returning -- it causes new jobs to be scheduled to
         store the decompressed contents and also to load the
         next compressed input frame. */
    bool wait_for_io(kdv_decompressor *decompressor, kdu_thread_env *caller);
      /* This function is called from `kdv_decompressor::load_frame' -- it
         waits until all required compressed data loading and decompressed
         frame store processing jobs have completed, using
         `kdu_thread_entity::wait_for_condition' to donate
         the caller's thread to the job processing pool until the conditions
         are met.  This means that you can run with a multi-threaded
         environment that has only one thread.  The function returns false
         if frame data could not be loaded for any reason. */
    void last_tile_started(kdv_decompressor *decompressor,
                           kdu_long next_sequence_idx, kdu_thread_env *caller);
      /* This function is called from within `decompressor->process_frame'
         or `decompressor->start_frame' at the point when the last tile
         processing engine for the frame is created.  Calls to this function
         are inherently sequential, because the object whose last tile
         processing engine has just been created cannot possibly be a
         current candidate for a call to `decompressor->load_frame'; as a
         result, there cannot be another thread that is executing within
         `decompressor->process_frame' or `this->do_load' that might cause
         this function to be called asynchronously.  Once a decompressor
         object invokes this function, the other decompressor object's
         `start_frame' function may be called, so long as its `load_frame'
         function has already returned successfully.   It is possible for
         a call to `start_frame' to recursively call back into this function
         to notify the object that the started frame has also started its
         last tile processing engine. */
  protected:
    void request_termination(kdu_thread_entity *caller);
      /* We do not actually need to override this function in the present
         application, because robust premature termination of background I/O
         processing is never required -- if an error occurs, the entire
         process will exist, and there is no interactive tool provided to
         stop things in mid-course.  However, we provide an implementation
         anyway, as an example of how it should be done. */
  private: // Helper functions
    void do_load(kdu_thread_env *caller);
    void do_store(kdu_thread_env *caller);
  private: // Declarations
    class kdv_load_job : public kdu_thread_job {
      public: // Functions
        void init(kdv_io_queue *owner)
          { this->queue=owner; set_job_func((kdu_thread_job_func) do_load); }
        static void do_load(kdv_load_job *job, kdu_thread_env *caller)
          { job->queue->do_load(caller); }
      private: // Data
        kdv_io_queue *queue;
      };
    class kdv_store_job : public kdu_thread_job {
      public: // Functions
        void init(kdv_io_queue *owner)
          { this->queue=owner; set_job_func((kdu_thread_job_func) do_store); }
        static void do_store(kdv_store_job *job, kdu_thread_env *caller)
          { job->queue->do_store(caller); }
      private: // Data
        kdv_io_queue *queue;
      };
  private: // Data
    kdu_mutex mutex; // Simple protection for the object's members
    bool termination_requested; // If `request_termination' has been called
    int max_frames_to_load; // Starts out >= 1
    int num_frames_loaded; // Num successful `kdv_compressor::load_frame' calls
    int num_frames_retrieved; // Num successful calls to `wait_for_io'
    int num_frames_stored; // Num calls to `do_store'
    int fpnum; // Frame/field state value managed on behalf of both
               // decompressors and passed to their `load_frame' functions.
    kdv_decompressor *active_loader; // Objects for which load/store has been
    kdv_decompressor *active_storer; // scheduled, but has not completed.
    kdv_decompressor *ready_loader;  // Objects that can schedule a load/store
    kdv_decompressor *ready_storer;  // as soon as the active one finishes.
    kdv_decompressor *ready_starter; // Object waiting for `start_frame' call
    kdu_long next_start_sequence_idx; // See below
    kdv_decompressor *waiting_decompressor; // Object passed to `wait_for_io'
    kdu_thread_entity_condition *waiting_cond;  // Waited for in `wait_for_io'
    kdv_load_job load_job; // Only one load can be active at any given time
    kdv_store_job store_job; // Only one store can be active at any given time
  };
  /* Notes:
       The `next_start_sequence_idx' member holds either -1 or else a valid
     sequence index to be passed to `kdv_decompressor::start_frame'.  The
     value starts out as 0 when the object is constructed.  The value
     becomes negative each time we issue a call to
     `kdv_decompressor::start_frame', and only becomes non-negative again
     once we receive a call to `last_tile_started'.
  */

/*****************************************************************************/
/*                               kdv_jpx_source                              */
/*****************************************************************************/

class kdv_jpx_source : public kdu_compressed_video_source {
  public: // Member functions
    kdv_jpx_source(jpx_source *src, kdu_uint32 track_idx,
                   jpx_frame first_frame)
      { /* Configures the object to offer frames sequentially from
           the indicated presentation track; the supplied `first_frame'
           becomes the one known outside of this object as frame 0, regardless
           of what its original frame index is.  The `first_frame' object
           should have been created without the `follow_persistents' attribute
           because we don't want to go repeatedly decompressing a persistent
           background image in this application.  Note that this object only
           presents the first codestream associated with the compositing layer
           associated with the first compositing instruction of each JPX
           frame. */
        this->source = src;  this->comp = src->access_composition();
        this->track_idx = track_idx;  this->frame = first_frame;
        this->first_idx = first_frame.get_frame_idx();
        this->lim_idx = 0; comp.count_track_frames(track_idx,lim_idx);
        frame_instant = frame_duration = first_instant = lim_instant = 0;
        comp.count_track_time(track_idx,lim_instant);
        frame.get_info(frame_instant,frame_duration);
        first_instant = frame_instant;  box = NULL;
      }
    virtual kdu_uint32 get_timescale() { return comp.get_timescale(); }
    virtual int get_num_frames() { return lim_idx-first_idx; }
    virtual bool seek_to_frame(int frame_idx)
      { 
        assert(box == NULL);
        if (frame.exists() && (frame_idx == (frame.get_frame_idx()+1)))
          frame = frame.access_next(track_idx,true);
        else
          frame = comp.access_frame(track_idx,first_idx+frame_idx,true,false);
        if (frame.exists())
          { frame.get_info(frame_instant,frame_duration); return true; }
        else
          { frame_instant=lim_instant; frame_duration=0; return false; }
      }
    virtual kdu_long get_duration() { return lim_instant-first_instant; }
    virtual int time_to_frame(kdu_long time_instant)
      { 
        int count=0;  time_instant += first_instant;
        comp.count_track_frames_before_time(track_idx,time_instant,count);
        count -= first_idx;
        return (count < 0)?0:count;
      }
    virtual kdu_long get_frame_period() { return frame_duration; }
    virtual int open_image()
      { 
        if (!frame.exists()) return -1;  assert(box == NULL);
        int stream_idx, layer_idx=0; kdu_dims src_dims, tgt_dims;
        jpx_composited_orientation orient;
        frame.get_instruction(0,layer_idx,src_dims,tgt_dims,orient);
        stream_idx = source->get_layer_codestream_id(layer_idx,0);
        jpx_codestream_source stream = source->access_codestream(stream_idx);
        box = stream.open_stream(NULL);
        return frame.get_frame_idx() - first_idx;
      }
    virtual int open_stream(int field_idx, jp2_input_box *input_box)
      { 
        jpx_input_box *jpx_box = input_box->get_jpx_box();
        if ((jpx_box==NULL) || (field_idx != 0) || !frame.exists()) return -1;
        int stream_idx, layer_idx=0; kdu_dims src_dims, tgt_dims;
        jpx_composited_orientation orient;
        frame.get_instruction(0,layer_idx,src_dims,tgt_dims,orient);
        stream_idx = source->get_layer_codestream_id(layer_idx,0);
        jpx_codestream_source stream = source->access_codestream(stream_idx);
        stream.open_stream(jpx_box);
        return frame.get_frame_idx() - first_idx;
      }
    virtual void close_image()
      { 
        if (box != NULL) box->close();
        box = NULL;  frame_instant = lim_instant;  frame_duration = 0;
        frame = frame.access_next(track_idx,true);
        if (frame.exists())
          frame.get_info(frame_instant,frame_duration);
      }
    virtual int get_capabilities()
      { return KDU_SOURCE_CAP_SEQUENTIAL | KDU_SOURCE_CAP_SEEKABLE; }
    virtual bool seek(kdu_long offset)
      { return (box == NULL)?false:box->seek(offset); }
    virtual kdu_long get_pos()
      { return (box == NULL)?-1:box->get_pos(); } 
    virtual int read(kdu_byte *buf, int num_bytes)
      { return (box == NULL)?0:box->read(buf,num_bytes); }
  private: // Data
    jpx_source *source;
    jpx_composition comp; // Top-level JPX composition interface
    kdu_uint32 track_idx;
    int first_idx; // True index of first accessible frame
    int lim_idx; // True index of last accessible frame + 1
    kdu_long first_instant; // True start time of first accessible frame
    kdu_long lim_instant; // True end time of last accessible frame
    jpx_frame frame; // Currently open frame interface
    kdu_long frame_instant; // True starting time of `frame'
    kdu_long frame_duration; // Duration of `frame'
    jpx_input_box *box; // Non-NULL between `open_image' and `close_image'
  };
  /* Notes:
       This class allows any presentation track from a `jpx_source' object
     to be accessed as an abstract video stream using the API defined by
     `kdu_compressed_video_source'.  This ensures allows the `kdv_decompressor'
     class to work with MJ2, JPX, JPB and MJC (raw) compressed video source
     files, without any internal modifications whatsoever. */

/*****************************************************************************/
/*                              kdv_decompressor                             */
/*****************************************************************************/

class kdv_decompressor {
  public: // Member functions
    kdv_decompressor(kdu_compressed_video_source *video, int field_mode,
                     FILE *store_fp, int frame_repeat,
                     kdu_codestream initial_codestream,
                     int discard_levels, int max_layers, kdu_dims *region,
                     bool load_compressed_data_to_memory,
                     int precision, bool is_signed, 
                     int double_buffering_height,
                     bool want_fastest, bool force_precise,
                     kdu_thread_env *env, kdv_io_queue *io_queue);
      /* The decompressor object's structure and parameters are derived from
         the supplied `initial_codestream' object; however, the sample
         precision and signed/unsigned properties of the data to be written to
         the VIX file are supplied separately, so as to ensure that they will
         be consistent with the VIX file header which has already been
         written.
            Compressed video frames are sourced from `video', using either
         the sequential reading interface functions `video->open_image' and
         `video->close_image', or the concurrent reading features offered by
         `video->open_stream' in conjunction with `video->seek_to_frame'.
         The sequential approach is adopted if `io_queue' is NULL (see below).
         In this case `initial_codestream' is actually used as the first
         codestream (the caller should not destroy it) and there can be
         only one `kdv_decompressor' object in use.  If `io_queue' is non-NULL,
         the concurrent reading approach is adopted and `initial_codestream'
         is used only to initialize the internal parameters -- it should be
         destroyed by the caller, once all decompressors have been
         constructed.  In any event, destruction of the `initial_codestream'
         object is always the caller's responsibility to perform at some
         point.
            The `field_mode' argument has the same interpretation as that
         documented for the `kdu_compressed_video_source::set_field_mode'
         function.  In fact, if the content is interlaced, that function
         should already have been invoked with the same `field_mode'.  The
         field mode is explicitly needed only for concurrent reading
         operations, where `video->open_image' and `video->close_image'
         are not employed.  In particular if `field_mode' is 0, only the
         first field of each interlaced frame is processed, while if
         `field_mode' is 1, only the second field is processed, and if
         `field_mode' is 2, both fields are processed.  The `field_mode' is
         ignored for frames that are not interlaced.  It is worth noting
         that for elementary broadcast stream sources, it is possible for
         some frames to be interlaced and others not to be interlaced.  This
         object is designed to work correctly in such circumstances -- i.e.,
         each frame's interlaced/progressive attributes are tested separately.
         It is also worth noting that where there are two concurrent
         `kdv_decompressor' objects deployed to process alternate pictures
         from the source, they must both share the same `field_mode' value.
         The `load_frame' function's `fpnum' argument is used to resolve
         which particular field is to be loaded by each `kdv_decompressor'
         object -- i.e., this is not done by assigning one decompressor to
         work on the even fields and another to work on the odd fields, which
         would not work in the general case where some frames are interlaced
         and others progressive.
            The `discard_levels', `max_layers' and `region' arguments are
         used with `kdu_codestream::apply_input_restrictions', each time
         a codestream is created or restarted at the start of a new frame.
         These same parameters should have been applied to the
         `initial_codestream'.  The `region' argument should be NULL if no
         restriction is to be applied to the decompressed region -- in any
         case any referenced `kdu_dims' object is copied internally for
         safety.
            The `load_compressed_data_to_memory' option is meaningful only
         when used with a non-NULL `io_queue' and when supported by the
         `mj2_video' source.  In this case, the entire compressed data for
         a video frame is loaded into memory by the `load_frame' function.
         This can lead to the highest possible throughput, but if you are
         only decompressing a tiny part of each video frame (e.g.,
         `discard_levels' might be quite large), it may be better to allow
         the codestream machinery to read compressed data on demand from the
         source, seeking to just those relevant locations -- `kdu_codestream'
         has many internal features to support reading the minimal amount of
         compressed codestream data.
            Frame data is decompressed into an internal frame buffer first
         (to emulate what would happen in a real application) and then
         written to the file identified by `store_fp'.  If `store_fp' is
         NULL, the file writing itself does not occur.
            If non-NULL, the `env' argument specifies a multi-threading
         environment to be used for multi-threaded processing, in place of
         the default single-processing implementation.  This is useful for
         accelerating the throughput on machines with multiple physical
         processors.
            The `double_buffering_height' argument is useful only for
         multi-threaded environments.  If non-zero, this argument is passed
         to `kdu_multi_synthesis::create' as the `processing_stripe_height'
         value, with `double_buffering' set to true.
            If `io_queue' is non-NULL, `env' must be non-NULL and the `video'
         object must implement the `kdu_compressed_video_source::seek_to_frame'
         and `kdu_compressed_video_source::open_stream' functions.  This is
         true for `jpb_source' and `mj2_video_source' derivations of the
         base `kdu_compressed_video_source' class, but it is not true for
         the `kdu_simple_video_source' derivation.
            The `io_queue' object is used to manage background load and
         flush operations on behalf of two concurrent `kdv_decompressor'
         objects. `io_queue->frame_processed' is called when
         `process_frame' completes and `io_queue->wait_for_io' is called
         when the main processing thread (the one associated with `env') calls
         `load_frame'.  Since source I/O on the `video' object involves
         concurrent access from multiple threads, it is important that the
         `jpb_source' or `mj2_movie' objects used to obtain the `video'
         interface were themselves opened using a `jp2_family_source'
         object that implements the `jp2_family_source::acquire_lock' and
         `jp2_family_source::release_lock' functions -- in practice, this
         is most easily accomplished by using `jp2_threadsafe_family_source'.
            The `frame_repeat' argument identifies the number of times each
         source frame is to be repeated.  Normally this argument is 0 (no
         repeats).  If > 0, each source video frame is used to generate
         `frame_repeat'+1 output frames, only the last of which is actually
         stored (if appropriate) to the output file.  Of course this only
         creates more work to do, without producing any more output; however,
         this allows I/O bottlenecks to be factored out when measuring
         video processing throughput, because frame repetition does not
         involve repeated loading of the compressed data for the frame or
         repeated storing of the decompressed results.
      */
    ~kdv_decompressor()
      {
        delete[] buf_handle;
        if (comp_info != NULL) delete[] comp_info;
        if (codestream.exists() && (io_queue != NULL))
          codestream.destroy();
      }
    bool load_frame(kdu_thread_env *caller, bool in_io_job, int *fpnum=NULL);
      /* Returns true if a new compressed frame can be loaded.  Generates a
         warning message if only part of a frame remains in the file.
            In multi-threaded applications, the `caller' should always be
         non-NULL.
            If a non-NULL `io_queue' reference was passed to the
         constructor, the function will be called from a background thread
         to perform the actual frame loading, in which case the `caller'
         argument will identify the background thread and `in_io_job' will
         be true.  When called from the main processing thread (see
         `process_frame' for an explanation of what the main processing
         thread is), the `in_io_job' argument should be false, in which
         case the function simply waits for the background thread to finish
         loading the object's compressed frame data and storing any
         previously processed frame's image data -- these two activities
         should normally have already happened.
            As documented with the constructor, compressed frames are opened
         either sequentially, using `kdu_compressed_video_source::open_image'
         and `kdu_compressed_video_source::close_image', or concurrently,
         using `kdu_compressed_video_source::open_stream', together with
         `kdu_compressed_video_source::seek_to_frame'.  In the latter case,
         it is necessary for this function to know which frame should be
         opened.  In particular, the function has to know the index of the
         frame and, for interlaced content, the index of the field that
         should be opened.  This information is collectively managed by
         the variable referenced by the `fpnum' (read as "frame and parity
         number") argument, which must not be NULL if `io_queue' is non-NULL.
         Specifically, *`fpnum' holds P + 2*F, where P is the field parity
         (0 for the first field, 1 for the second) and F is the frame index,
         for the next image to be loaded.  Based on the prevailing
         `field_mode', *`fpnum' is adjusted if necessary to avoid loading
         a field that we are not interested in.  Before returning,
         the function adjusts the value of *`fpnum' to reference the next
         field of an interlaced frame (if `field_mode'=2) or to reference the
         first field of the next frame.  The caller keeps track of the
         state variable referenced by `fpnum', since this variable needs
         to be shared between multiple `kdv_decompressor' objects when an
         `io_queue' is being used.
      */
    void process_frame(kdu_thread_env *env);
      /* To process a frame, you should first invoke `load_frame' and check
         the return value.  If `load_frame' returned false, there are no more
         frames to process.  If `load_frame' returned ture, you then invoke
         `process_frame'.
            The compressed video source objects passed to the constructor may
         potentially still be in use by another `kdv_decompressor' object
         that is flushing its contents.  For this reason, you should not
         invoke `kdu_compressed_video_source::open_image' or
         `kdu_compressed_video_source::close_image' explicitly yourself.  The
         present function also takes care of invoking the
         `kdu_codestream::restart' function at appropriate points.
            We now discuss two usage scenarios for this function:
         1. If there is no multi-threaded processing environment, you
            pass NULL for `env'.  After this, you are responsible for calling
            the `store_frame' yourself.
         2. If there is a multi-threaded processing environment, you should
            generally invoke the function from the thread group owner,
            identified by the `env' reference.  In this case, the `store_frame'
            function is invoked automatically from a background thread, so
            long as a non-NULL `io_queue' argument was supplied to the
            constructor.
      */
    void store_frame();
      /* This is where any decompressed frame buffer gets written to the
         `store_fp' file pointer supplied to the constructor, unless it is
         NULL.  You could modify this application to do something different
         with the decompressed frame samples -- e.g., write them to a
         display or a video analysis algorithm.
            If the object was constructed with an `io_queue', this function
         gets called automatically from a background thread.  Otherwise, this
         function is called directly by the application, once `process_frame'
         returns -- this allows the application to time the call, for
         example. */      
  private: // Functions
    friend class kdv_io_queue;
    void start_frame(kdu_long initial_sequence_idx, kdu_thread_env *caller);
      /* This is where the first tile processing engine is first created.
            If the object was constructed without an `io_queue' object, there
         is only one `kdv_decompressor' object, so there is no processing
         to overlap.  In this case, the function is simply called at the
         start of the `process_frame' function.
            Otherwise, this function is called from within
         `io_queue::do_load' or `io_queue::last_tile_started', once the
         compressed frame data for this object has been loaded and the
         previous `kdv_decompressor' object has created its last tile
         processing engine.  This is the earliest point at which the
         current object should be allowed to schedule decompression work
         to be performed.
            The `initial_sequence_idx' argument identifies the first
         tile processing sequence index to be used (this is ignored unless
         the function is being called from `io_queue'). */
  private: // Declarations
    struct kdv_comp_info { // Stores fixed component-wide information
        kdu_dims comp_dims; // Dimensions and location of component
        int count_val; // Vertical sub-sampling factor for component
        kdu_byte *comp_buf; // Points into master buffer
      };
    struct kdv_tcomp {
      public: // State information
        kdu_coords comp_size; // Obtained from `kdv_comp_info::comp_dims'
        kdu_coords tile_size; // Size of current tile-component
        kdu_byte *bp; // Points to next row in component buffer for the tile
        kdu_byte *next_tile_bp; // Points to start of next tile in codestream
        int precision; // # bits from tile-component stored in VIX sample MSB's
        bool is_absolute; // If we are using "reversible" integer processing
        bool uses_shorts; // If we are using 16-bit sample values
        int counter;
        int tile_rows_left; // Rows which have not yet been processed
      public: // SIMD data conversion and transfer accelerators
        simd_transfer_func simd_transfer; // Only planar (ILV1) accelerators
                                          // are relevant to this app.
      };
    struct kdv_tile {
      public: // Members
        kdv_tile() { components=NULL; env_queue=NULL; next=NULL; }
        ~kdv_tile()
          { if (components != NULL) delete[] components;
            if (ifc.exists()) ifc.close();
            if (engine.exists()) engine.destroy(); }
        bool is_active() { return engine.exists(); }
      public: // Data
        kdu_dims valid_indices; // For the current codestream
        kdu_coords idx; // Relative index, within `valid_indices' range
        kdu_tile ifc;
        kdu_multi_synthesis engine;
        kdv_tcomp *components; // One for each component produced by `engine'
        kdu_thread_queue *env_queue;
        kdv_tile *next; // Points to the next tile in a circular buffer
      };
  private: // Helper functions
    bool init_tile(kdv_tile *tile, kdu_thread_env *caller);
      /* Initializes the `tile' object to reference the first valid tile
         of the `codestream', updating or even creating all internal
         members as appropriate.  The function even creates the internal
         `comp_info' array if it does not currently exist.  The function
         returns false if it cannot open a valid tile for some reason -- e.g.,
         if `codestream' does not exist.  The `tile' object must not be
         active when this function is called, meaning that
         `kdv_tile::is_active' must return false. */
    bool init_tile(kdv_tile *tile, kdv_tile *ref_tile,
                   kdu_thread_env *caller);
      /* This second form of the `init_tile' function initializes the object
         referenced by `tile' to use the next tile beyond that associated
         with `ref_tile'.  If there is no such tile, the function
         returns false, leaving the object in the inactive state (see
         `kdv_tile::is_active').  Note that the `ref_tile' object can be
         either active or inactive, so long as it has a valid tile `idx'.
         In fact, if `ref_tile' is inactive, it can refer to the same object
         as `tile'. */
    void close_tile(kdv_tile *tile, kdu_thread_env *caller);
      /* This function does nothing unless `tile' is currently active
         (see `kdv_tile::is_active').  In that case, the function destroys
         the internal `kdv_tile::engine' object and closes any open codestream
         tile.  It leaves the `kdv_tile::codestream' and `kdv_tile::idx'
         members alone, however, so that they can be used to find and open
         the next tile in the same codestream, if one exists.
            This function is not invoked from the present object's destructor,
         since it executes the `kdu_thread::terminate' function in
         multi-threaded environments.  This is safe only if the application
         has not itself invoked `kdu_thread::terminate'. */
  private: // External data objects and parameters retained here
    kdu_compressed_video_source *src;
    kdv_io_queue *io_queue;
    int field_mode; // Same interpretation as `src->set_field_mode'
    int frame_repetitions;
  private: // Codestream machinery and configuration parameters
    kdu_codestream codestream; // Belongs to us only if `io_queue' is non-NULL
    jpx_input_box src_box; // Used only with non-NULL `io_queue'
    bool load_in_memory; // If `src_box.load_in_memory' is to be called
    int discard_levels;
    int max_layers;
    kdu_dims region;
    kdu_dims *region_ref; // NULL, or pointer to `region'
  private: // Fixed parameters
    int processing_stripe_height; // Derived from `double_buffering_height'
    bool double_buffering; // Derived from `double_buffering_height'
    bool want_fastest;
    bool force_precise;
    FILE *fp; // Used by `store_frame'
    int sample_bytes; // Bytes per VIX sample
    bool is_signed; // True if VIX file is to have signed samples
    int frame_bytes; // Total bytes in VIX frame
  private: // State information that may change between frames
    bool image_open; // When `src->open_image' needs matched `src->close_image'
    kdu_long sequence_idx; // Only for multi-threaded processing (see below)
  private: // Management of frame buffer memory and processing machinery
    int frame_reps_remaining; // Keeps track of repeated frames
    kdu_byte *buf; // Complete frame buffer, organized by components
    kdu_byte *buf_handle; // Used to deallocate the buffer
    int num_components;
    kdv_comp_info *comp_info; // Fixed information for each component
    kdv_tile tiles[2];
    kdv_tile *current_tile; // Points to 1 of the objects in the `tiles' array
    kdv_tile *next_tile; // Used only for multi-threaded processing (see below)
  };
  /* For multi-threaded applications (i.e., when `env' is non-NULL), the
     implementation manages up to two `kdv_tile' objects.  The `current_tile'
     pointer refers to the one from which data is currently being decompressed
     into the frame buffer.  The `next_tile' pointer refers to the next
     tile in the current frame, if there is one; its `engine' has been
     started, which means that there are code-block decoding
     tasks which can be performed in the background by Kakadu's thread
     scheduler.  However, these jobs will not be performed until the system
     becomes under-utilized, in the sense described by the
     `kdu_thread_entity::attach_queue' function.  This is managed through the
     `sequence_idx' counter, which is initialized by `start_frame' (unless
     `io_queue' is NULL) and increments each time a new tile queue 
     is created. */

#endif // V_EXPAND_LOCAL_H
