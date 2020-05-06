/*****************************************************************************/
// File: v_compress_local.h [scope = APPS/V_COMPRESSOR]
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
#ifndef V_COMPRESS_LOCAL_H
#define V_COMPRESS_LOCAL_H

#include "kdu_args.h"
#include "kdu_video_io.h"
#include "jpx.h"

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_cv_ompress" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace

// Create type definition for fast SIMD data transfer accelerators that
// we may import from the `kdu_stripe_compressor' implementation by including
// "stripe_compressor_local.h" directly into the kdu_v_compress project.  The
// actual inclusion of this header happens inside "kdu_v_compress.cpp".
// This is really a hack here, since we should instead be using the
// public `kdu_stripe_compressor' API's directly -- that is what we expect
// to do in future releases, but it is simpler for the moment just to
// borrow the fast data transfer functions defined within that API.
typedef void (*simd_transfer_func)(void **dst, void *src, int width,
                                   int src_precision, int tgt_precision,
                                   bool is_absolute, bool src_signed);

// Defined here
class kdv_cbr_stats;
class kdv_null_target;
class kdv_jpx_target;
class kdv_io_queue;
class kdv_compressor;

/*****************************************************************************/
/*                               kdv_cbr_stats                               */
/*****************************************************************************/

class kdv_cbr_stats {
  public: // Member functions
    kdv_cbr_stats() { memset(this,0,sizeof(*this)); }
    void get_stats(kdu_codestream cs)
      { 
        float bucket_max=0, bucket_mean=0, inter_flush=0;
        kdu_uint16 min_slope=0, max_slope=0;
        float mean_slope=0, mean_sq_slope=0;
        kdu_long fill_bytes=0;
        p_stats[8] = cs.get_cbr_flush_stats(bucket_max,bucket_mean,inter_flush,
                                            min_slope,max_slope,mean_slope,
                                            mean_sq_slope,fill_bytes);
        p_stats[0] = bucket_max;     p_stats[1] = bucket_mean;
        p_stats[2] = inter_flush;    p_stats[3] = min_slope;
        p_stats[4] = max_slope;      p_stats[5] = mean_slope;
        p_stats[6] = mean_sq_slope;  p_stats[7] = (double)fill_bytes;
        for (int n=0; n < 9; n++)
          acc_stats[n] += p_stats[n];
        acc_count += 1.0;
      }
    void get_averages()
      { 
        double inv_count = (acc_count > 0)?(1.0/acc_count):1.0;
        for (int n=0; n < 9; n++)
          p_stats[n] = acc_stats[n]*inv_count;
      }
    static void print_hdr(FILE *fp)
      { 
        fprintf(fp,"Bucket-max(B) Bucket-avg(B) Intr-flush(B) "
               "DL-lambda-min DL-lambda-max DL-lambda-avg "
               "DL-Lambda-rms Fill/frame(B) Flushes/frame\n"
               "------------- ------------- ------------- " 
               "------------- ------------- ------------- "
               "------------- ------------- -------------\n");
      }
    void print_stats(FILE *fp)
      { 
        fprintf(fp,"%13.1f %13.1f %13.1f "
                "%13.1f %13.1f %13.1f "
                "%13.1f %13.1f %13.1f\n",
                p_stats[0],p_stats[1],p_stats[2],
                p_stats[3],p_stats[4],p_stats[5],
                sqrt(p_stats[6]-p_stats[5]*p_stats[5]),
                p_stats[7],p_stats[8]);
      }
  private: // Data
    double p_stats[9];
    double acc_stats[9];
    double acc_count;
  };

/*****************************************************************************/
/*                              kdv_null_target                              */
/*****************************************************************************/

class kdv_null_target : public kdu_compressed_video_target {
  public: // Member functions
    virtual void open_image() { return; }
    virtual void close_image(kdu_codestream codestream) { return; }
    virtual bool write(const kdu_byte *buf, int num_bytes) { return true; }
  };
  /* Notes:
       This object exists simply to discard compressed output when the
     `kdv_compressor' object is configured to repeatedly compress each
     frame -- only the final such repetition actually writes to an output
     file. */

/*****************************************************************************/
/*                              kdv_jpx_labels                               */
/*****************************************************************************/

class kdv_jpx_labels {
  public: // Member functions
    kdv_jpx_labels(jpx_target *tgt, jpx_container_target container,
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
    ~kdv_jpx_labels()
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
/*                              kdv_jpx_target                               */
/*****************************************************************************/

class kdv_jpx_target : public kdu_compressed_video_target {
  public: // Member functions
    kdv_jpx_target(jpx_container_target cont, kdv_jpx_labels *labels=NULL)
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
    kdv_jpx_labels *label_writer;
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
/* EXTERN                  kdv_initialize_jpx_target                         */
/*****************************************************************************/

extern jpx_container_target
  kdv_initialize_jpx_target(jpx_target &tgt, jpx_source &prefix,
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
/*                               kdv_jpx_layer                               */
/*****************************************************************************/

struct kdv_jpx_layer {
    kdv_jpx_layer()
      { space=JP2_sLUM_SPACE; num_colours=1; components[0]=0; next=NULL; }
    jp2_colour_space space;
    int num_colours;
    int components[4]; // We don't have any colour spaces with more than 4
    kdv_jpx_layer *next;
  };
  /* Note: this structure is used internally to implement the
     `kdv_initialize_jpx_target'; convenient to declare it here. */

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
    void processor_joined(kdv_compressor *compressor, kdu_thread_env *caller);
      /* This function is called when the `io_queue' is first passed into a
         `kdv_compressor' object -- it schedules the first frame loading
         job for that compressor, so that the `wait_for_io' function
         will be able to succeed. */
    void frame_processed(kdv_compressor *compressor, kdu_thread_env *caller);
      /* This function is called by `kdv_compressor::process_frame' right
         before returning -- it causes new jobs to be scheduled to
         flush the codestream contents and also to load the next input
         frame into the object's buffer. */
    bool wait_for_io(kdv_compressor *compressor, kdu_thread_env *caller);
      /* This function is called from `kdv_compressor::load_frame' -- it
         waits until all required frame loading and flush processing jobs have
         completed, using `kdu_thread_entity::wait_for_condition' to donate
         the caller's thread to the job processing pool until the conditions
         are met.  This means that you can run with a multi-threaded
         environment that has only one thread.  The function returns false
         if frame data could not be loaded for any reason. */
  protected:
    void request_termination(kdu_thread_entity *caller);
      /* We do not actually need to override this function in the present
         application, because robust premature termination of background I/O
         processing is never required -- if an error occurs, the entire
         process will exit, and there is no interactive tool provided to
         stop things in mid-course.  However, we provide an implementation
         anyway, as an example of how it should be done. */
  private: // Helper functions
    void do_load(kdu_thread_env *caller);
    void do_flush(kdu_thread_env *caller);
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
    class kdv_flush_job : public kdu_thread_job {
      public: // Functions
        void init(kdv_io_queue *owner)
          { this->queue=owner; set_job_func((kdu_thread_job_func) do_flush); }
        static void do_flush(kdv_flush_job *job, kdu_thread_env *caller)
          { job->queue->do_flush(caller); }
      private: // Data
        kdv_io_queue *queue;
      };
  private: // Data
    kdu_mutex mutex; // Simple protection for the object's members
    bool termination_requested; // If `request_termination' has been called
    int max_frames_to_load; // Starts out >= 1
    int num_frames_loaded; // Num successful `kdv_compressor::load_frame' calls
    int num_frames_retrieved; // Num successful calls to `wait_for_io'
    int num_frames_flushed; // Num calls to `do_flush'
    kdv_compressor *active_loader;  // Objects for which load/flush has been
    kdv_compressor *active_flusher; // scheduled, but has not completed.
    kdv_compressor *ready_loader;   // Objects that can schedule a load/flush
    kdv_compressor *ready_flusher;  // as soon as the active one finishes.
    kdv_compressor *waiting_compressor; // Object passed to `wait_for_io'
    kdu_thread_entity_condition *waiting_cond;  // Waited for in `wait_for_io'
    kdu_uint16 last_min_slope; // From `kdv_compressor::finish_frame'
    kdv_load_job load_job; // Only one load can be active at any given time
    kdv_flush_job flush_job; // Only one flush can be active at any given time
  };

/*****************************************************************************/
/*                               kdv_compressor                              */
/*****************************************************************************/

class kdv_compressor {
  public: // Member functions
    kdv_compressor(kdu_compressed_video_target *video,
                   kdu_codestream codestream, int num_src_components,
                   FILE *source_fp, int initial_frame_idx, int frame_idx_inc,
                   int frame_repeat, int sample_bytes, bool native_order,
                   bool lsb_aligned, bool is_signed, int num_layer_specs,
                   int double_buffering_height, bool want_fastest,
                   bool force_precise, int flush_period,
                   kdu_thread_env *env, kdv_io_queue *io_queue,
                   kdv_cbr_stats *cbr_stats, FILE *cbr_trace_file);
      /* The compressor object's structure and parameters are derived from
         the supplied `codestream' object, except that `num_src_components'
         is the number of image components that will be recovered from
         the source file and passed to the compression machinery.  If there
         is a Part-2 multi-component transform, this value might be smaller
         than `Mcomponents' and perhaps larger than the `Scomponents' attribute
         with which `codestream' was created.
            After opening each tile, the `kdu_tile::set_components_of_interest'
         function is invoked with the `num_src_components' value to let
         `kdu_multi_analysis' know which image components to expect across its
         `exchange_line' interface.
            If non-NULL, the `env' argument specifies a multi-threading
         environment to be used for multi-threaded processing, in place of
         the default single-processing implementation.  This is useful for
         accelerating the throughput on machines with multiple physical
         processors.
            The `double_buffering_height' argument is useful only for
         multi-threaded environments.  If non-zero, this argument is passed
         to `kdu_multi_analysis::create' as the `processing_stripe_height'
         value, with `double_buffering' set to true.
            The `flush_period' argument should be zero unless you want the
         `kdu_codestream::auto_flush' feature to be used to flush content
         progressively while frame encoding is proceeding -- this is probably
         unnecessary and even undesirable, except in special circumstances,
         since multi-threaded deployments already typically use two compressors
         and an `io_queue' to overlap frame processing and flushing operations.
            If `io_queue' is non-NULL, `env' must be non-NULL and the
         `io_queue' is used to manage background load and flush operations.
         In this case, `io_queue->frame_processed' is called when
         `process_frame' completes and `io_queue->wait_for_io' is called
         when the main processing thread (the one associated with `env') calls
         `load_frame'.  Background flushing operations managed by queued I/O
         jobs normally do the entire rate control and flushing process, but
         if `flush_period' is non-zero, they simply serve to cap the flushing
         process off, waiting until it is all complete.
            The `initial_frame_idx' argument identifies the index of the
         first frame to be processed by this object, while `frame_idx_inc'
         is the amount by which the frame index is incremented for each
         successive frame processed by this object.  It is not important that
         these correspond to real frame indices (there might be sub-sampling),
         but it is important that `initial_frame_idx' lies in the range
         0 to `frame_idx_inc'-1 and that compressors process frames in the
         same order as their `initial_frame_idx' arguments would suggest,
         where there are multiple concurrent processors.
            The `frame_repeat' argument identifies the number of times each
         source frame is to be repeatedly compressed, using exactly the
         same parameters.  Normally this argument is 0 (no repeats).  If > 0,
         each source video frame is used to generate `frame_repeat'+1
         compressed output frames, only the first of which is actually
         written to the output file -- the others are passed to a
         `kdu_compressed_target' object that simply discards its data.
         Evidently, frame repeats only create more work to do, without
         producing any more output; however, this allows I/O bottlenecks
         to be factored out when measuring video processing throughput. */
    ~kdv_compressor()
      {
        if (buffer != NULL)
          delete[] buffer;
        if (components != NULL)
          delete[] components;
        if (layer_bytes != NULL)
          delete[] layer_bytes;
        if (layer_thresholds != NULL)
          delete[] layer_thresholds;
      }
    bool load_frame(kdu_thread_env *io_caller=NULL);
      /* Returns true if a new frame can be loaded.  Generates a warning
         message if only part of a frame remains in the file.
            You should call this function with a NULL `io_caller' argument.
         If a non-NULL `io_queue' reference was passed to the constructor,
         the function will be called from a background thread to perform
         the actual frame loading, in which case the `io_caller' argument
         will be non-NULL.  When called from the main thread, however, a
         NULL `io_queue' argument should be supplied and the function simply
         waits for the background thread to finish loading the object's
         frame buffer and flushing any previously processed frame's
         codestream data. */
    void process_frame(const kdu_long layer_bytes[],
                       const kdu_uint16 layer_thresholds[],
                       bool trim_to_rate, bool skip_codestream_comments,
                       bool predict_slope, double rate_tolerance);
      /* Call this function after `load_frame' returns successfully.  The
         `video' target object passed to the constructor may potentially
         still be in use by another `kdv_compressor' object that is flushing
         its contents.  For this reason, you should not invoke
         `kdu_compressed_video_target::open_image' or
         `kdu_compressed_video_target::close_image' explicitly yourself.
            The present function also takes care of invoking the
         `kdu_codestream::restart' function at appropriate points.
            It is important that this function be invoked from the main
         thread that invoked the constructor -- the one associated with any
         `env' reference passed into the constructor. */
  public: // Statistics; no harm in making these data members public
    kdu_long cumulative_total_bytes; // Includes all parameter marker segments
    kdu_long cumulative_compressed_bytes; // Only J2K packet bodies/headers
    kdu_long max_header_bytes; // Max. over all frames of the difference
      // between total codestream bytes and total J2K packet body/header bytes
  private: // Functions
    friend class kdv_io_queue;
    kdu_uint16 finish_frame(kdu_thread_env *caller, kdu_uint16 alt_min_slope);
      /* This is where any processed codestream's data gets flushed.  If
         the object was constructed with an `io_queue', this function gets
         called automatically from a background thread.  Otherwise, this
         function is called before `process_frame' returns.  The
         `alt_min_slope' value and the function's return value are used to
         share rate-distortion slope threshold prediction data between
         `kdv_compressor' objects.  If there is no `kdv_io_queue' object,
         `alt_min_slope' is 0 and the return value is ignored; otherwise,
         `alt_min_slope' is the value returned by the last call to this
         function within any `kdv_compressor' object. */
  private: // Declarations
    struct kdv_component {
      public: // Component-wide data
        kdu_dims comp_dims; // Total size of this component
        int precision; // Number of MSBs used from each VIX sample word
        bool is_absolute; // If `line' uses "reversible" integers
        bool uses_shorts; // If `line' uses a 16-bit representation
        kdu_byte *comp_buf; // Points to 1st sample of this component in buffer
      public: // Tile-component specific data managed here
        kdu_dims tile_dims; // Size of current tile-component
        kdu_byte *bp; // Points to next row in current tile
        kdu_line_buf *line; // Value retrieved last by `exchange_line'
      public: // SIMD data conversion and transfer accelerators
        simd_transfer_func simd_transfer; // Only planar (ILV1) accelerators
                                          // are relevant to this app.
      };
  private: // Codestream and external objects and parameters retained here
    kdu_codestream codestream;
    kdv_null_target null_target; // For compressed output to be discarded 
    kdu_compressed_video_target *video_target;
    kdu_compressed_target *tgt; // Either `video_target' or &`null_target'
    kdu_thread_env *main_env; // Used only for multi-threaded processing
    kdu_thread_queue *env_queue; // Created on demand using `env->add_queue'
    kdv_io_queue *io_queue;
    FILE *fp; // Used by `load_frame'
    kdv_cbr_stats *cbr_stats; // Used to record CBR stats after each flush
    FILE *cbr_trace_file; // Used to write CBR stats trace after each flush
    int frame_repetitions;
  private: // Fixed parameters
    int processing_stripe_height; // Derived from `double_buffering_height'
    bool double_buffering; // Derived from `double_buffering_height'
    bool want_fastest;
    bool force_precise;
    int sample_bytes; // 1, 2 or 4
    bool native_order; // If false, VIX sample words must be byte reversed
    bool lsb_aligned; // If VIX file's words hold sample bits in LSB positions
    bool is_signed; // If true, VIX samples have a signed representation
    int frame_bytes; // Total bytes in the frame, as stored in the VIX file
    kdu_dims tile_indices;
    int num_layer_specs;
    bool trim_to_rate;
    bool skip_codestream_comments;
    double rate_tolerance;
    int flush_flags; // Flags to pass to `kdu_codestream::flush'
  private: // Parameters associated with incremental flushing, if used
    int flush_period; // If non-zero, image opened early and auto-flush enabled
    int tc_trig_interval;   // Pre-computed parameters, passed to
    int incr_trig_interval; // `kdu_codestream::auto_flush'
  private: // Dynamic state information
    kdu_long frame_sequence_idx; // Spread thread queues for successive frames
    kdu_long frame_sequence_inc; // Increments sequence index between frames
    kdu_long *layer_bytes;         // These arrays each have `num_layer_specs'
    kdu_uint16 *layer_thresholds;  // entries
    kdu_long target_max_bytes; // Last byte target passed to `process_frame'
    kdu_uint16 target_min_slope; // Last threshold passed to `process_frame'
    kdu_uint16 last_min_slope; // Min slope value determined from last frame
    kdu_uint16 next_min_slope; // Value to be transferred to `last_min_slope'
                               // when a new frame is started.
  private: // Management of frame buffer memory and processing machinery
    int frame_reps_remaining; // Keeps track of repeated frames
    kdu_byte *buffer; // Single buffer for the entire frame
    int num_source_components;
    kdv_component *components; // Array with `num_source_components' entries
    kdu_tile open_tile; // These resources are instantiated by `process_frame'
    kdu_multi_analysis engine; // but not finally closed until `finish_frame'.
  };

#endif // V_COMPRESS_LOCAL_H
