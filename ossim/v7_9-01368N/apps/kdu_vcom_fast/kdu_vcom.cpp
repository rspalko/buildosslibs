/*****************************************************************************/
// File: kdu_vcom.cpp [scope = APPS/VCOM_FAST]
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
   Implements the objects defined in "kdu_vcom.h".
******************************************************************************/

#include <string.h>
#include <assert.h>
#include "kdu_vcom.h"

using namespace kdu_supp_vcom; // Includes namespaces `kdu_supp' and `kdu_core'

/* ========================================================================= */
/*                            INTERNAL FUNCTIONS                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                      kd_set_threadname                             */
/*****************************************************************************/

#if (defined _WIN32 && defined _DEBUG && defined _MSC_VER && (_MSC_VER>=1300))
static void
kd_set_threadname(LPCSTR thread_name)
{
  struct {
    DWORD dwType; // must be 0x1000
    LPCSTR szName; // pointer to name (in user addr space)
    DWORD dwThreadID; // thread ID (-1=caller thread)
    DWORD dwFlags; // reserved for future use, must be zero
  } info;
  info.dwType = 0x1000;
  info.szName = thread_name;
  info.dwThreadID = -1;
  info.dwFlags = 0;
  __try {
    RaiseException(0x406D1388,0,sizeof(info)/sizeof(DWORD),
                   (ULONG_PTR *) &info );
  }
  __except(EXCEPTION_CONTINUE_EXECUTION) { }
}
#endif // .NET debug compilation


/* ========================================================================= */
/*                              vcom_frame_queue                             */
/* ========================================================================= */

/*****************************************************************************/
/*                     vcom_frame_queue::vcom_frame_queue                    */
/*****************************************************************************/

vcom_frame_queue::vcom_frame_queue()
{
  max_source_frames = 0;
  max_buffered_frames = max_allocated_streams = 0;
  num_comps = 0;
  sample_bytes = 0;
  comp_heights = NULL;
  comp_precisions = NULL;
  comp_signed = NULL;
  comp_bytes = NULL;
  frame_bytes = 0;
  
  head_frame = tail_frame = NULL;
  first_unaccessed_frame = NULL;
  first_unfilled_frame = NULL;
  first_unbuffered_frame = NULL;
  num_buffered_frames = 0;
  next_access_frame_idx = 0;
  next_tail_frame_idx = 0;
  next_fill_frame_idx = 0;
  
  first_active_stream = last_active_stream = NULL;
  unconsumed_streams = NULL;
  free_streams = NULL;
  next_consume_frame_idx = 0;
  num_allocated_streams = 0;
  for (int s=0; s < VCOM_SLOPE_PREDICT_HISTORY; s++)
    recent_min_slope_thresholds[s] = 0;
  
  terminated = exception_raised = false;
  last_exception_code = KDU_NULL_EXCEPTION;
  
  mutex.create();
  service_waiting = false;
  engines_waiting = 0;
  service_wakeup.create(true);
  engine_wakeup.create(true);
}

/*****************************************************************************/
/*                    vcom_frame_queue::~vcom_frame_queue                    */
/*****************************************************************************/

vcom_frame_queue::~vcom_frame_queue()
{
  mutex.destroy();
  service_wakeup.destroy();
  engine_wakeup.destroy();
  if (comp_heights != NULL)
    delete[] comp_heights;
  if (comp_precisions != NULL)
    delete[] comp_precisions;
  if (comp_signed != NULL)
    delete[] comp_signed;
  if (comp_bytes != NULL)
    delete[] comp_bytes;
  vcom_frame *frame;
  while ((frame = head_frame) != NULL)
    { head_frame = frame->next; delete frame; }
  vcom_stream *stream;
  while ((stream=first_active_stream) != NULL)
    { first_active_stream=stream->next; delete stream; }
  while ((stream=free_streams) != NULL)
    { free_streams=stream->next; delete stream; }
}

/*****************************************************************************/
/*                          vcom_frame_queue::init                           */
/*****************************************************************************/

void
  vcom_frame_queue::init(int max_frames_to_read, int max_frames_to_buffer,
                         int max_streams_to_allocate, int num_source_comps,
                         const kdu_coords source_comp_sizes[],
                         int num_sample_bytes, int bits_used,
                         bool lsb_aligned, bool is_signed)
{
  if (this->comp_heights != NULL)
    { kdu_error e; e << "Attempting to call `vcom_frame_queue::init' on an "
      "object which is already initialized."; }
  if (max_frames_to_buffer < 1)
    max_frames_to_buffer = 1;
  if (max_streams_to_allocate < max_frames_to_buffer)
    max_streams_to_allocate = max_frames_to_buffer;
  this->max_source_frames = max_frames_to_read;
  this->max_buffered_frames = max_frames_to_buffer;
  this->max_allocated_streams = max_streams_to_allocate;
  this->num_comps = num_source_comps;
  this->sample_bytes = num_sample_bytes;
  this->comp_heights = new int[num_comps];
  this->comp_precisions = new int[num_comps];
  this->comp_signed = new bool[num_comps];
  this->comp_bytes = new size_t[num_comps];
  this->frame_bytes = 0;
  for (int c=0; c < num_comps; c++)
    { 
      comp_heights[c] = source_comp_sizes[c].y;
      comp_signed[c] = is_signed;
      if (lsb_aligned)
        comp_precisions[c] = bits_used;
      else
        comp_precisions[c] = sample_bytes<<3;
      comp_bytes[c] = (((size_t) source_comp_sizes[c].x) *
                       ((size_t) source_comp_sizes[c].y) *
                       ((size_t) sample_bytes));
      frame_bytes += comp_bytes[c];
    }
}

/*****************************************************************************/
/*                vcom_frame_queue::get_frame_and_stream                     */
/*****************************************************************************/

vcom_frame *
  vcom_frame_queue::get_frame_and_stream(vcom_stream * &stream)
{
  stream = NULL; // In case we return NULL
  if (terminated)
    return NULL;
  
  mutex.lock();
  
  // First collect a `stream' object -- may have to wait
  while ((!terminated) && (next_access_frame_idx < max_source_frames) &&
         ((stream = free_streams) == NULL))
    { 
      if (num_allocated_streams < max_allocated_streams)
        { 
          stream = free_streams = new vcom_stream;
          num_allocated_streams++;
          break;
        }
      engines_waiting++;
      engine_wakeup.reset();
      engine_wakeup.wait(mutex);
      engines_waiting--;
    }
  if (stream == NULL)
    { 
      mutex.unlock();
      return NULL;
    }
  
  // Now we are committed to returning a result; make `stream' active
  assert(stream == free_streams);
  free_streams = stream->next;
  stream->next = NULL;
  stream->prev = last_active_stream;
  if (last_active_stream == NULL)
    first_active_stream = last_active_stream = stream;
  else
    last_active_stream = last_active_stream->next = stream;
  
  // Fill in slope prediction information to help the compression engine
  kdu_uint16 min_slope=0;
  for (int s=0; s < VCOM_SLOPE_PREDICT_HISTORY; s++)
    { 
      kdu_uint16 slope = recent_min_slope_thresholds[s];
      if ((min_slope == 0) || ((slope != 0) && (slope < min_slope)))
        min_slope = slope;
    }
  stream->restart(min_slope);
  
  // Prepare the `vcom_frame' that will be returned
  vcom_frame *frame;
  if ((frame = first_unaccessed_frame) == NULL)
    { // Allocate a new one and assign a frame index to it
      frame = new vcom_frame;
      frame->prev = tail_frame;
      if (tail_frame == NULL)
        head_frame = tail_frame = frame;
      else
        tail_frame = tail_frame->next = frame;
      if (first_unfilled_frame == NULL)
        first_unfilled_frame = frame;
      if (first_unbuffered_frame == NULL)
        first_unbuffered_frame = frame; // No buffer assigned yet
      first_unaccessed_frame = frame;
      frame->frame_idx = next_tail_frame_idx++;
    }
  assert(frame->frame_idx == next_access_frame_idx);
  next_access_frame_idx++;
  first_unaccessed_frame = frame->next; // May become NULL
  stream->frame_idx = frame->frame_idx;

  mutex.unlock();
  return frame;
}

/*****************************************************************************/
/*                vcom_frame_queue::return_processed_frame                   */
/*****************************************************************************/

void
  vcom_frame_queue::return_processed_frame(vcom_frame *frame)
{
  mutex.lock();
  
  // Unlink frame from the list
  if (frame->prev == NULL)
    { 
      assert(frame == head_frame);
      head_frame = frame->next;
    }
  else
    frame->prev->next = frame->next;
  if (frame->next == NULL)
    { 
      assert(frame == tail_frame);
      tail_frame = frame->prev;
    }
  else
    frame->next->prev = frame->prev;
  assert(frame != first_unaccessed_frame);
  if (frame == first_unfilled_frame)
    { // Could happen if application encountered the end of the video source
      // unexectedly.
      assert(terminated || (frame->frame_idx >= max_source_frames));
      first_unfilled_frame = frame->next;
    }
  if (frame == first_unbuffered_frame)
    first_unbuffered_frame = frame->next;
  
  // Extract buffer and make sure frame state is reset
  vcom_frame_buffer *buffer = frame->buffer;
  frame->buffer = NULL;
  frame->engine = NULL; // Just for good measure
  frame->state.set(0); // Also just for good measure
  
  // Next, tack `frame' onto the end of the frame list
  frame->frame_idx = next_tail_frame_idx++;
  frame->prev = tail_frame;
  frame->next = NULL;
  if (tail_frame == NULL)
    tail_frame = head_frame = frame;
  else
    tail_frame = tail_frame->next = frame;
  if (first_unaccessed_frame == NULL)
    first_unaccessed_frame = frame;
  if (first_unfilled_frame == NULL)
    first_unfilled_frame = frame;
  if (first_unbuffered_frame == NULL)
    first_unbuffered_frame = frame;
  
  // Use buffer to advance `first_unbuffered_frame' -- note that this may
  // just put the buffer back into the same `frame' object, but that is fine.
  first_unbuffered_frame->buffer = buffer;
  first_unbuffered_frame = first_unbuffered_frame->next;
  if (service_waiting && (first_unfilled_frame->buffer != NULL) &&
      (first_unfilled_frame->frame_idx < max_source_frames))
    service_wakeup.protected_set(); // Wake thread blocked in `service_queue'

  mutex.unlock();
}

/*****************************************************************************/
/*                vcom_frame_queue::return_generated_stream                  */
/*****************************************************************************/

void
  vcom_frame_queue::return_generated_stream(vcom_stream *stream)
{
  mutex.lock();

  // Unlink from the active streams list (doubly-linked)
  if (stream->prev == NULL)
    { 
      assert(stream == first_active_stream);
      first_active_stream = stream->next;
    }
  else
    stream->prev->next = stream->next;
  if (stream->next == NULL)
    { 
      assert(stream == last_active_stream);
      last_active_stream = stream->prev;
    }
  else
    stream->next->prev = stream->prev;
  
  // Update slope history list and other stats
  int s;
  for (s=1; s < VCOM_SLOPE_PREDICT_HISTORY; s++)
    recent_min_slope_thresholds[s-1] = recent_min_slope_thresholds[s];
  recent_min_slope_thresholds[s-1] = stream->min_slope_threshold;

  // Insert into the unconsumed streams list (singly-linked)
  vcom_stream *scan, *prev=NULL;
  for (scan=unconsumed_streams; scan != NULL; prev=scan, scan=scan->next)
    if (scan->frame_idx > stream->frame_idx)
      break; // Insert between `prev' and `scan'
  stream->next = scan;
  if (prev != NULL)
    prev->next = stream;
  else
    { 
      unconsumed_streams = stream;
      if ((stream->frame_idx == next_consume_frame_idx) && service_waiting)
        service_wakeup.protected_set(); // Wake up thread blocked in
                    // `service_queue' to consume the newly generated stream.
    }
  
  mutex.unlock();
}

/*****************************************************************************/
/*                 vcom_frame_queue::return_unused_stream                   */
/*****************************************************************************/

void
  vcom_frame_queue::return_unused_stream(vcom_stream *stream)
{
  mutex.lock();
  
  // Unlink from the active streams list (doubly-linked)
  if (stream->prev == NULL)
    { 
      assert(stream == first_active_stream);
      first_active_stream = stream->next;
    }
  else
    stream->prev->next = stream->next;
  if (stream->next == NULL)
    { 
      assert(stream == last_active_stream);
      last_active_stream = stream->prev;
    }
  else
    stream->next->prev = stream->prev;
  
  // Move immediately onto the free list
  stream->next = free_streams;
  free_streams = stream;
  
  mutex.unlock();
}

/*****************************************************************************/
/*                 vcom_frame_queue::return_aborted_stream                   */
/*****************************************************************************/

void
  vcom_frame_queue::return_aborted_stream(vcom_stream *stream,
                                          kdu_exception exception_code)
{
  mutex.lock();
  
  // Unlink from the active streams list (doubly-linked)
  if (stream->prev == NULL)
    { 
      assert(stream == first_active_stream);
      first_active_stream = stream->next;
    }
  else
    stream->prev->next = stream->next;
  if (stream->next == NULL)
    { 
      assert(stream == last_active_stream);
      last_active_stream = stream->prev;
    }
  else
    stream->next->prev = stream->prev;

  // Move immediately onto the free list
  stream->next = free_streams;
  free_streams = stream;
  
  // Record the exception and wake the queue management thread, if necessary,
  // to discover the condition.
  exception_raised = true;
  if ((exception_code != KDU_NULL_EXCEPTION) ||
      (last_exception_code == KDU_NULL_EXCEPTION))
    last_exception_code = exception_code;
  service_wakeup.protected_set();
  mutex.unlock();
}

/*****************************************************************************/
/*                     vcom_frame_queue::service_queue                       */
/*****************************************************************************/

bool
  vcom_frame_queue::service_queue(vcom_frame * &frame_in_out,
                                  vcom_stream * &stream_in_out,
                                  bool blocking, bool no_more_frames)
{
  mutex.lock();

  // Start by processing any frame and/or stream being returned to us here
  if (frame_in_out != NULL)
    { // Frame reading was successful
      vcom_frame *frame = frame_in_out;
      frame_in_out = NULL;
      assert(frame == first_unfilled_frame);
      first_unfilled_frame = frame->next;
      next_fill_frame_idx++;
      kdu_int32 old_state, new_state;
      do { // Atomically manipulate `frame->state'
        old_state = frame->state.get();
        new_state = old_state | VCOM_FRAME_STATE_READY;
        new_state &= ~VCOM_FRAME_STATE_WAKEUP;
      } while (!frame->state.compare_and_set(old_state,new_state));
      if ((old_state & VCOM_FRAME_STATE_WAKEUP) &&
          (frame->engine != NULL))
        frame->engine->frame_wakeup(frame);
    }
  if (stream_in_out != NULL)
    { 
      vcom_stream *stream = stream_in_out;
      stream_in_out = NULL;
      assert(stream == unconsumed_streams);
      unconsumed_streams = stream->next;
      next_consume_frame_idx++;
      stream->next = free_streams;
      free_streams = stream;
      if (engines_waiting > 0)
        engine_wakeup.protected_set(); // Newly recycled stream allows at least
                             // one call to `get_frame_and_stream' to proceed.
    }
  
  // See if the video source has terminated (perhaps prematurely), in which
  // case we need to adjust `max_source_frames' and mark all unfilled frames
  // as "ended", awaking any waiting engines as we go.
  if (no_more_frames)
    { 
      max_source_frames = next_fill_frame_idx;
      for (vcom_frame *frame=first_unfilled_frame;
           frame != NULL; frame=frame->next)
        { 
          kdu_int32 old_state, new_state;
          do { // Atomically manipulate `frame->state'
            old_state = frame->state.get();
            new_state = old_state | VCOM_FRAME_STATE_END;
            new_state &= ~VCOM_FRAME_STATE_WAKEUP;
          } while (!frame->state.compare_and_set(old_state,new_state));
          if ((old_state & VCOM_FRAME_STATE_WAKEUP) &&
              (frame->engine != NULL))
            frame->engine->frame_wakeup(frame);
        }
      if (engines_waiting > 0)
        engine_wakeup.protected_set();
    }
  
  // Now look for new frame/stream objects to return
  while ((frame_in_out == NULL) && (stream_in_out == NULL) &&
         (next_consume_frame_idx < max_source_frames) &&
         !(terminated || exception_raised))
    { 
      if ((unconsumed_streams != NULL) &&
          (unconsumed_streams->frame_idx == next_consume_frame_idx))
        stream_in_out = unconsumed_streams;
      if ((next_fill_frame_idx < max_source_frames) &&
          !(terminated || exception_raised))
        { 
          vcom_frame *frame = first_unfilled_frame;
          if ((frame == NULL) && (num_buffered_frames < max_buffered_frames))
            { // May be able to allocate a new frame
              frame = new vcom_frame;
              frame->prev = tail_frame;
              if (tail_frame == NULL)
                tail_frame = head_frame = frame;
              else
                tail_frame = tail_frame->next = frame;
              frame->frame_idx = next_tail_frame_idx++;
              first_unfilled_frame = frame;
              if (first_unaccessed_frame == NULL)
                first_unaccessed_frame = frame;
              if (first_unbuffered_frame == NULL)
                first_unbuffered_frame = frame;            
            }
          if ((frame != NULL) && (frame->buffer == NULL))
            { 
              if (num_buffered_frames >= max_buffered_frames)
                frame = NULL; // Cannot allocate a frame right now
              else
                { 
                  vcom_frame_buffer *buffer = new vcom_frame_buffer;
                  buffer->num_comps = this->num_comps;
                  buffer->sample_bytes = this->sample_bytes;
                  buffer->comp_heights = this->comp_heights;
                  buffer->comp_precisions = this->comp_precisions;
                  buffer->comp_signed = this->comp_signed;
                  buffer->comp_buffers = new kdu_byte *[num_comps];
                  memset(buffer->comp_buffers,0,
                         sizeof(kdu_byte *)*(size_t)num_comps);
                  buffer->frame_bytes = this->frame_bytes;
                  buffer->buffer_handle =
                  new(std::nothrow) kdu_byte[frame_bytes+31];
                  if (buffer->buffer_handle == NULL)
                    { // Not enough memory to continue
                      delete buffer;
                      kdu_error e; e <<
                      "Allocated only " << num_buffered_frames <<
                      " frame buffers before running out of memory.  Each "
                      "frame buffer requires " << (kdu_long) frame_bytes <<
                      " bytes.  You may wish to try again with a smaller "
                      "number of frame processing engines or a smaller "
                      "\"read-ahead\" threshold.";
                    }
                  int align_off =
                  (32 - _addr_to_kdu_int32(buffer->buffer_handle)) & 31;
                  kdu_byte *buf = buffer->buffer_handle + align_off;
                  for (int c=0; c < num_comps; c++)
                    { 
                      buffer->comp_buffers[c] = buf;
                      buf += this->comp_bytes[c];
                    }
                  frame->buffer = buffer;
                  first_unbuffered_frame = frame->next;
                  num_buffered_frames++;
                }
            }
          frame_in_out = frame;
        }

      // See if we need to block
      if (!blocking)
        break;
      if ((frame_in_out == NULL) && (stream_in_out == NULL))
        { 
          service_waiting = true;
          service_wakeup.reset();
          service_wakeup.wait(mutex);
        }
    }

  mutex.unlock();
  if (exception_raised)
    throw last_exception_code;
  return ((next_consume_frame_idx < max_source_frames) && !terminated);
}

/*****************************************************************************/
/*                       vcom_frame_queue::terminate                         */
/*****************************************************************************/

void
  vcom_frame_queue::terminate()
{
  mutex.lock();
  terminated = true;
  max_source_frames = next_fill_frame_idx; // There can be no more reading
  for (vcom_frame *frame=first_unfilled_frame;
       frame != NULL; frame=frame->next)
    { // Make sure no engine remains blocked waiting for the frame to be read
      kdu_int32 old_state, new_state;
      do { // Atomically manipulate `frame->state'
        old_state = frame->state.get();
        new_state = old_state | VCOM_FRAME_STATE_END;
        new_state &= ~VCOM_FRAME_STATE_WAKEUP;
      } while (!frame->state.compare_and_set(old_state,new_state));
      if ((old_state & VCOM_FRAME_STATE_WAKEUP) &&
          (frame->engine != NULL))
        frame->engine->frame_wakeup(frame);
    }
  engine_wakeup.protected_set();
  mutex.unlock();
}


/* ========================================================================= */
/*                               vcom_processor                              */
/* ========================================================================= */

/*****************************************************************************/
/*                       vcom_processor::vcom_processor                      */
/*****************************************************************************/

vcom_processor::vcom_processor()
{
  num_frames_started = num_frames_ended = num_layer_specs = 0;
  last_layer_sizes = NULL;
  last_layer_slopes = NULL;
  flush_flags = 0;
  end_frame_stream = NULL;
  end_frame_queue = NULL;
  end_frame_job.init(this);
  ready_state.set(0);
  ready_waiter = NULL;
}

/*****************************************************************************/
/*                           vcom_processor::reset                           */
/*****************************************************************************/

void
  vcom_processor::reset()
{
  num_frames_started = num_frames_ended = num_layer_specs = 0;
  if (last_layer_sizes != NULL)
    { delete[] last_layer_sizes; last_layer_sizes = NULL; }
  if (last_layer_slopes != NULL)
    { delete[] last_layer_slopes; last_layer_slopes = NULL; }
  flush_flags = 0;
  compressor.reset();
  if (codestream.exists())
    codestream.destroy(); // Must be called after `compressor.reset'.
  end_frame_stream = NULL;
  end_frame_queue = NULL;
  ready_state.set(0);
  ready_waiter = NULL;
}

/*****************************************************************************/
/*                   vcom_processor::init (from codestream)                  */
/*****************************************************************************/

void
  vcom_processor::init(kdu_codestream cs, int num_layers)
{
  reset(); // Just in case
  this->codestream = cs;
  this->num_layer_specs = num_layers;
  this->last_layer_sizes = new kdu_long[num_layers];
  this->last_layer_slopes = new kdu_uint16[num_layers];
  memset(last_layer_sizes,0,sizeof(kdu_long)*(size_t)num_layers);
  memset(last_layer_slopes,0,sizeof(kdu_uint16)*(size_t)num_layers);
}

/*****************************************************************************/
/*                   vcom_processor::init (from processor)                   */
/*****************************************************************************/

void
  vcom_processor::init(vcom_processor &src, vcom_stream *stream)
{
  reset(); // Just in case
  siz_params *src_params = src.codestream.access_siz();
  codestream.create(src_params,stream);
  codestream.access_siz()->copy_all(src_params);
  codestream.access_siz()->finalize_all();
  this->num_layer_specs = src.num_layer_specs;
  this->last_layer_sizes = new kdu_long[num_layer_specs];
  this->last_layer_slopes = new kdu_uint16[num_layer_specs];
  memset(last_layer_sizes,0,sizeof(kdu_long)*(size_t)num_layer_specs);
  memset(last_layer_slopes,0,sizeof(kdu_uint16)*(size_t)num_layer_specs);
}

/*****************************************************************************/
/*                        vcom_processor::start_frame                        */
/*****************************************************************************/

void
  vcom_processor::start_frame(kdu_long *layer_sizes_in,
                              kdu_uint16 *layer_slopes_in, bool trim_to_rate,
                              bool predict_slope, bool force_precise,
                              bool want_fastest, bool skip_codestream_comments,
                              double rate_tolerance, int env_dbuf_height,
                              const kdu_push_pull_params *pp_params,
                              vcom_frame *frame, vcom_stream *stream,
                              kdu_thread_env *env)
{
  assert(num_frames_started == num_frames_ended);
  
  // Prepare quality layer drivers
  kdu_long *flush_sizes=layer_sizes_in;
  kdu_uint16 *flush_slopes=layer_slopes_in;
  flush_flags = 0;
  if ((flush_slopes == NULL) || (flush_slopes[0] == 0))
    { // Use previous slope thresholds as hints to the rate control machinery
      flush_slopes = last_layer_slopes;
      if (num_frames_started > 0)
        flush_flags = KDU_FLUSH_THRESHOLDS_ARE_HINTS;
    }
  else if (flush_sizes != NULL)
    { // We will be using slope-based rate control, but check to see if we
      // will also be using `layer_sizes_in' to lower bound the layer output
      // sizes.
      int n;
      for (n=0; n < num_layer_specs; n++)
        if (flush_sizes[n] != 0)
          break;
      if (n < num_layer_specs)
        flush_flags = KDU_FLUSH_USES_THRESHOLDS_AND_SIZES;
      else
        flush_sizes = NULL; // No point in passing these to `compressor.start'
    }
  
  // Prepare the `codestream'
  if (num_frames_started == 0)
    codestream.enable_restart();
  else
    codestream.restart(stream,env);
  kdu_uint16 min_slope_threshold = 0;
  if (num_layer_specs > 0)
    { // Give the `codestream' some idea what to expect so that the block
      // encoder has a chance to skip some coding passes up front.
      if ((layer_slopes_in != NULL) &&
          (layer_slopes_in[num_layer_specs-1] != 0))
        { // We know exactly what the smallest slope threshold will be
          predict_slope = true;
          min_slope_threshold = layer_slopes_in[num_layer_specs-1];
        }
      else if ((flush_sizes != NULL) && (flush_sizes[num_layer_specs-1] > 0))
        { // Guess the smallest slope threshold using the value supplied
          // via `stream->min_slope_threshold' by the `vcom_frame_queue'.  We
          // don't have to get this prediction right, since calls to
          // `kdu_codestream::set_min_slope_threshold' do not hard limit the
          // set of code-block coding passes based on the threshold -- they
          // just provide a somewhat conservative means for the block coder
          // to avoid generating unnecessary coding passes.
          min_slope_threshold = stream->min_slope_threshold;
        }
    }
  
  // Start the stripe compressor
  compressor.start(codestream,num_layer_specs,flush_sizes,flush_slopes,
                   min_slope_threshold,!predict_slope,force_precise,
                   !skip_codestream_comments,rate_tolerance,
                   frame->buffer->num_comps,want_fastest,env,this,
                   env_dbuf_height,-1,trim_to_rate,flush_flags,pp_params);
  num_frames_started++;
}

/*****************************************************************************/
/*                        vcom_processor::push_samples                       */
/*****************************************************************************/

kdu_long
  vcom_processor::push_samples(vcom_frame *frame,
                               kdu_long next_queue_sequence_idx)
{
  assert(num_frames_started == (num_frames_ended+1));
  next_queue_sequence_idx =
    compressor.get_set_next_queue_sequence(next_queue_sequence_idx);
  vcom_frame_buffer *buf = frame->buffer;
  if (buf->sample_bytes == 1)
    { 
      if (buf->comp_signed[0])
        { kdu_error e; e << "This demo-app does not support source formats "
          "that involve signed 2's complement input samples that are stored "
          "in bytes -- higher precision signed representations are "
          "acceptable, but there are hardly any use cases for low precision "
          "2's complement input formats."; }
      compressor.push_stripe(buf->comp_buffers,buf->comp_heights,
                             NULL,NULL,buf->comp_precisions);
    }
  else if (buf->sample_bytes == 2)
    compressor.push_stripe((kdu_int16 **)(buf->comp_buffers),buf->comp_heights,
                           NULL,NULL,buf->comp_precisions,buf->comp_signed);
  else if (buf->sample_bytes == 4)
    compressor.push_stripe((kdu_int32 **)(buf->comp_buffers),buf->comp_heights,
                           NULL,NULL,buf->comp_precisions,buf->comp_signed);
  else
    { kdu_error e; e << "This demo-app supports source formats that involve "
      "1, 2 and 4 byte integer representations (signed or unsigned) for "
      "each sample, but it does not support " << buf->sample_bytes <<
      " byte sample values."; }
  next_queue_sequence_idx =
    compressor.get_set_next_queue_sequence(next_queue_sequence_idx);
  return next_queue_sequence_idx;
}

/*****************************************************************************/
/*                          vcom_processor::end_frame                        */
/*****************************************************************************/

void
  vcom_processor::end_frame(vcom_stream *stream, vcom_frame_queue *queue,
                            kdu_thread_env *env)
{
  assert(num_frames_started == (num_frames_ended+1));
  if (env == NULL)
    { // Synchronous case
      bool stream_is_valid =
        compressor.finish(num_layer_specs,last_layer_sizes,last_layer_slopes);
      if (stream != NULL)
        { 
          if (!stream_is_valid)
            { kdu_error e; e << "Failed to completely finish compressing "
              "frame " << stream->get_frame_idx() << ".  "
              "Looks like there must have been some inconsistency between "
              "dimensions of the source frame data and those used to set "
              "coding parameters -- i.e., an error in the use of the "
              "API's defined by the \"kdu_vcom_fast\" demo-app.";
            }
          stream->min_slope_threshold = last_layer_slopes[num_layer_specs-1];
          stream->codestream_bytes = codestream.get_total_bytes();
          stream->compressed_bytes = codestream.get_packet_bytes();
          queue->return_generated_stream(stream);
        }
      num_frames_ended++;
    }
  else
    { 
      end_frame_stream = stream;
      end_frame_queue = queue;
      ready_state.set(1); // Flush job in progress; no waiter
      ready_waiter = NULL; // Only calling thread can wait for flush complete
      schedule_job(&end_frame_job,env);
    }
}

/*****************************************************************************/
/*                       vcom_processor::do_end_frame                        */
/*****************************************************************************/

void
  vcom_processor::do_end_frame(kdu_thread_env *caller)
{
  // See if we should terminate early
  if (ready_state.get() & 8) // "termination" flag detected
    { // No harm in terminating early.  Leave the "job-in-progress" flag
      // behind so nobody else calls `all_done'.
      this->all_done(caller);
      return;
    }
  
  // Retrieve parameters that were passed to `end_frame'
  vcom_stream *stream = end_frame_stream;
  end_frame_stream = NULL;
  vcom_frame_queue *queue = end_frame_queue;
  end_frame_queue = NULL;
  
  // Now do the work that `end_frame' otherwise does synchronously
  bool stream_is_valid =
    compressor.finish(num_layer_specs,last_layer_sizes,last_layer_slopes,
                      caller);
  if (stream != NULL)
    { 
      if (!stream_is_valid)
        { kdu_error e; e << "Failed to completely finish compressing "
          "frame " << stream->get_frame_idx() << ".  "
          "Looks like there must have been some inconsistency between "
          "dimensions of the source frame data and those used to set "
          "coding parameters -- i.e., an error in the use of the "
          "API's defined by the \"kdu_vcom_fast\" demo-app.";
        }
      stream->min_slope_threshold = last_layer_slopes[num_layer_specs-1];
      stream->codestream_bytes = codestream.get_total_bytes();
      stream->compressed_bytes = codestream.get_packet_bytes();
      queue->return_generated_stream(stream);
    }
  num_frames_ended++;
  
  // Update `ready_state' and wake any thread that is in `wait_until_ready'
  kdu_int32 old_state, new_state;
  do { // Enter compare-and-set loop
    old_state = new_state = ready_state.get();
    new_state &= ~3; // Clear "job-in-progress" and "need-wakeup" flags, but
                     // leave behind the "no-more-jobs" flag to make it clear
                     // that `all_done' has been called already.
  } while (!ready_state.compare_and_set(old_state,new_state));
  if (old_state & 2)
    caller->signal_condition(ready_waiter);
  if (old_state & 12)
    this->all_done(caller);
}

/*****************************************************************************/
/*                     vcom_processor::wait_until_ready                      */
/*****************************************************************************/

bool
  vcom_processor::wait_until_ready(kdu_thread_env *caller)
{
  if (!codestream)
    return false; // We still need a call to `init'
  
  if (ready_state.get() == 0)
    return true;
  
  ready_waiter = caller->get_condition();
  kdu_int32 old_state, new_state;
  do { // Enter compare-and-swap loop
    old_state = new_state = ready_state.get();
    if (old_state & 1)
      new_state |= 2; // Append "need-wakeup" flag
    } while (!ready_state.compare_and_set(old_state,new_state));
  if (old_state & 1)
    caller->wait_for_condition();

  assert(ready_state.get() == 0);
  ready_waiter = NULL;
  return true;
}

/*****************************************************************************/
/*                       vcom_processor::no_more_jobs                        */
/*****************************************************************************/

void
  vcom_processor::no_more_jobs(kdu_thread_env *caller)
{
  kdu_int32 old_state, new_state;
  do { // Enter compare-and-swap loop
    old_state = ready_state.get();
    new_state = old_state | 4; // Append "no-more-jobs" flag
  } while (!ready_state.compare_and_set(old_state,new_state));
  if (!(old_state & 1))
    { // There is no "job-in-progress" so we must call `all_done' ourself here
      this->all_done(caller);
    }
}

/*****************************************************************************/
/*                        vcom_processor::terminate                          */
/*****************************************************************************/

void
  vcom_processor::terminate(kdu_thread_env *caller, kdu_exception exc_code)
{
  // First make sure that the base `kdu_thread_queue' will not block a
  // call to `caller->terminate'.
  kdu_int32 old_state, new_state;
  do { // Enter compare-and-swap loop
    old_state = ready_state.get();
    new_state = old_state | 8; // Append "termination" flag
  } while (!ready_state.compare_and_set(old_state,new_state));
  if (!(old_state & 5))
    { // Detected the "job-in-progress" or "no-more-jobs" flag.  In the
      // former case a scheduled job will invoke (or has invoked) `all_done'.
      // In the latter case, the call to `all_done' may have been issued
      // from within `no_more_jobs' or it may have been issued by the
      // end-frame job after clearing the "job-in-progress" flag.  In all
      // cases `all_done' has been or will be called.  If neither condition
      // is found, we must call `all_done' ourselves here.
      this->all_done(caller);
    }
    
  // Now wait for processing to finish and clean things up
  caller->terminate(this);
  caller->cs_terminate(codestream);
  vcom_stream *stream = end_frame_stream;
  vcom_frame_queue *queue = end_frame_queue;
  end_frame_stream = NULL;
  end_frame_queue = NULL;
  if ((stream != NULL) && (queue != NULL))
    queue->return_aborted_stream(stream,exc_code);
  
  reset();
}


/* ========================================================================= */
/*                                vcom_engine                                */
/* ========================================================================= */

/*****************************************************************************/
/*                             engine_startproc                              */
/*****************************************************************************/
namespace kdu_supp_vcom {
  
kdu_thread_startproc_result
  KDU_THREAD_STARTPROC_CALL_CONVENTION engine_startproc(void *param)
{
  vcom_engine *self = (vcom_engine *) param;
#if (defined _WIN32 && defined _DEBUG && defined _MSC_VER && (_MSC_VER>=1300))
  kd_set_threadname(self->master_thread_name);
#endif // .NET debug compilation
  if (self->num_threads <= 1)
    self->run_single_threaded();
  else
    self->run_multi_threaded();
  return KDU_THREAD_STARTPROC_ZERO_RESULT;
}
  
} // namespace kdu_supp_vcom

/*****************************************************************************/
/*                          vcom_engine::vcom_engine                         */
/*****************************************************************************/

vcom_engine::vcom_engine()
{
  engine_idx = -1;
  num_threads = 0;
  thread_concurrency = 0;
  double_buffering_height = 0;
  want_fastest = force_precise = false;
  predict_slope = false;
  trim_to_rate = false;
  skip_codestream_comments = false;
  rate_tolerance = 0.0;
  num_layer_specs = 0;
  layer_sizes = NULL;
  layer_slopes = NULL;
  active_processor = 0;
  queue = NULL;
  active_frame = NULL;
  active_stream = NULL;
  thread_env = NULL;
  wait_condition = NULL;
  waiting_for_frame = false;
  graceful_shutdown_requested = false;
  immediate_shutdown_requested = false;
  master_thread_name[0] = '\0';
}

/*****************************************************************************/
/*                            vcom_engine::startup                           */
/*****************************************************************************/

void
  vcom_engine::startup(kdu_codestream codestream,
                       vcom_frame_queue *queue, vcom_frame *frame,
                       vcom_stream *stream, int engine_idx,
                       const kdu_thread_entity_affinity &engine_specs,
                       int num_layer_specs, const kdu_long *layer_bytes,
                       const kdu_uint16 *layer_thresholds,
                       bool trim_to_rate, bool skip_codestream_comments,
                       bool predict_slope, double rate_tolerance,
                       int thread_concurrency, int double_buffering_height,
                       bool want_fastest, bool want_precise,
                       const kdu_push_pull_params *params, int extra_reps)
{
  if (this->layer_sizes != NULL)
    { // May be left over after a previous engine shutdown.
      delete[] this->layer_sizes; this->layer_sizes = NULL;
    }
  if (this->layer_slopes != NULL)
    { // May be left over after a previous engine shutdown.
      delete[] this->layer_slopes; this->layer_slopes = NULL;
    }
  
  this->engine_idx = engine_idx;
  this->thread_concurrency = thread_concurrency;
  this->num_threads = engine_specs.get_total_threads();
  if (num_threads < 1)
    { kdu_error e; e <<
      "Engine " << engine_idx << " is not assigned any threads -- looks "
      "like an implementation error.";
    }
  this->cpu_affinity.copy_from(engine_specs);
  this->double_buffering_height = double_buffering_height;
  this->want_fastest = want_fastest;
  this->force_precise = want_precise;
  this->predict_slope = predict_slope;
  this->trim_to_rate = trim_to_rate;
  this->skip_codestream_comments = skip_codestream_comments;
  this->rate_tolerance = rate_tolerance;
  if (params != NULL)
    this->pp_params = *params;
  this->extra_compression_reps = (extra_reps < 0)?0:extra_reps;
  this->num_layer_specs = num_layer_specs;
  this->layer_sizes = new kdu_long[num_layer_specs];
  this->layer_slopes = new kdu_uint16[num_layer_specs];
  for (int n=0; n < num_layer_specs; n++)
    { 
      this->layer_sizes[n] = (layer_bytes==NULL)?0:(layer_bytes[n]);
      this->layer_slopes[n] = (layer_thresholds==NULL)?0:(layer_thresholds[n]);
    }
  this->active_processor = 0;
  this->queue = queue;
  this->active_frame = frame;
  this->active_stream = stream;
  this->thread_env = NULL;
  waiting_for_frame = false;
  graceful_shutdown_requested = false;
  immediate_shutdown_requested = false;
  
  processors[0].init(codestream,num_layer_specs);
  
  sprintf(master_thread_name,"Master thread for engine %d",engine_idx);
  if (!master_thread.create(engine_startproc,(void *) this))
    { shutdown(false);
      kdu_error e; e << 
      "Unable to start master thread for engine " << engine_idx << ".";
    }
}

/*****************************************************************************/
/*                            vcom_engine::shutdown                          */
/*****************************************************************************/

void
  vcom_engine::shutdown(bool graceful)
{
  if (queue == NULL)
    return;
  if (graceful)
    graceful_shutdown_requested = true;
  else
    immediate_shutdown_requested = true;
  if (waiting_for_frame)
    queue->terminate();
  if (master_thread.exists())
    master_thread.destroy(); // Blocks the caller until the thread terminates
  if (wait_semaphore.exists())
    wait_semaphore.destroy();
  wait_condition = NULL;
  
  for (int p=0; p < 2; p++)
    processors[p].reset();
  active_processor = 0;
  queue = NULL;
  active_frame = NULL;
  active_stream = NULL;
  thread_env = NULL;
  waiting_for_frame = false;
  graceful_shutdown_requested = immediate_shutdown_requested = false;
  num_threads = 0;
  master_thread_name[0] = '\0';
}

/*****************************************************************************/
/*                        vcom_engine::frame_wakeup                          */
/*****************************************************************************/

void
  vcom_engine::frame_wakeup(vcom_frame *frame)
{
  assert(frame->engine == this);
  assert(this->active_frame == frame);
  assert(waiting_for_frame);
  if (thread_env == NULL)
    wait_semaphore.signal();
  else if (wait_condition != NULL)
    thread_env->signal_condition(wait_condition,true); // The caller is not
      // the thread associated with `thread_env' so `foreign_caller'=true here
  else
    assert(0); // One of the above two wakeup methods must have been in use
}

/*****************************************************************************/
/*                    vcom_engine::wait_for_active_frame                     */
/*****************************************************************************/

bool
  vcom_engine::wait_for_active_frame()
{
  waiting_for_frame = true;
  if (active_frame == NULL)
    { 
      assert(active_stream == NULL);
      active_frame = queue->get_frame_and_stream(active_stream);
      if (active_frame == NULL)
        { // End of video or else an error occurred somewhere else
          assert(active_stream == NULL);
          waiting_for_frame = false;
          return false;
        }
    }

  active_frame->engine = this;
  kdu_int32 old_state=0, new_state=0;
  do {
    if (thread_env != NULL)
      wait_condition = thread_env->get_condition();
    do { // Enter compare-and-set loop
      old_state = new_state = active_frame->state.get();
      if (!(old_state & (VCOM_FRAME_STATE_READY | VCOM_FRAME_STATE_END)))
        new_state |= VCOM_FRAME_STATE_WAKEUP;
    } while ((old_state != new_state) &&
             !active_frame->state.compare_and_set(old_state,new_state));
    if (new_state & VCOM_FRAME_STATE_WAKEUP)
      { 
        if (thread_env == NULL)
          wait_semaphore.wait();
        else
          thread_env->wait_for_condition();
      }
  } while (!(old_state & (VCOM_FRAME_STATE_READY | VCOM_FRAME_STATE_END)));
  active_frame->engine = NULL;
  waiting_for_frame = false;
  if (old_state & VCOM_FRAME_STATE_END)
    { // No frames left
      queue->return_processed_frame(active_frame);
      active_frame = NULL;
      queue->return_unused_stream(active_stream);
      active_stream = NULL;
      return false;
    }
  return true;    
}

/*****************************************************************************/
/*                     vcom_engine::return_active_frame                      */
/*****************************************************************************/

void
  vcom_engine::return_active_frame()
{
  if ((active_frame != NULL) && (queue != NULL))
    queue->return_processed_frame(active_frame);
  active_frame = NULL;
}

/*****************************************************************************/
/*                     vcom_engine::run_single_threaded                      */
/*****************************************************************************/

void
  vcom_engine::run_single_threaded()
{
  kdu_exception exc_code = KDU_NULL_EXCEPTION;
  wait_semaphore.create(0);
  bool exception_caught = false;
  try {
    while ((!(graceful_shutdown_requested || immediate_shutdown_requested)) &&
           wait_for_active_frame())
      { 
        kdu_uint16 min_slope_on_entry = active_stream->min_slope_threshold;
        for (int rep=0; rep <= extra_compression_reps; rep++)
          { 
            vcom_stream *stream;
            if ((stream=active_stream) == NULL)
              stream = dummy_streams[0].restart(min_slope_on_entry);
            processors[0].start_frame(layer_sizes,layer_slopes,trim_to_rate,
                                      predict_slope,force_precise,want_fastest,
                                      skip_codestream_comments,rate_tolerance,
                                      0,&pp_params,active_frame,stream,NULL);
            processors[0].push_samples(active_frame);
            if (immediate_shutdown_requested)
              break;
            if (rep == extra_compression_reps)
              return_active_frame(); // Last compression iteration
            processors[0].end_frame(active_stream,queue,NULL);
            active_stream = NULL;            
          }
      }
  } catch (kdu_exception exc) {
    exc_code = exc;
    exception_caught = true;
  } catch (std::bad_alloc) {
    exc_code = KDU_MEMORY_EXCEPTION;
    exception_caught = true;
  } catch (...) {
    exc_code = KDU_CONVERTED_EXCEPTION;
    exception_caught = true;
  }
  
  return_active_frame(); // In case loop ended prematurely for some reason
  if (exception_caught && (active_stream != NULL))
    queue->return_aborted_stream(active_stream,exc_code);
  else if (active_stream != NULL)
    queue->return_unused_stream(active_stream);
  active_stream = NULL;

  wait_semaphore.destroy();
}

/*****************************************************************************/
/*                      vcom_engine::run_multi_threaded                      */
/*****************************************************************************/

void
  vcom_engine::run_multi_threaded()
{
  int p;
  kdu_exception exc_code = KDU_NULL_EXCEPTION;
  kdu_thread_env multi_thread_env;
  multi_thread_env.create();
  thread_env = &multi_thread_env;
  
  bool exception_caught = false;
  try {  
    multi_thread_env.set_cpu_affinity(cpu_affinity);
    multi_thread_env.set_min_thread_concurrency(thread_concurrency);
    for (int nt=1; nt < num_threads; nt++)
      if (!multi_thread_env.add_thread())
        num_threads = nt;
    for (p=0; p < 2; p++)
      { // Attach each `processor' as a queue to which jobs can be scheduled,
        // so that the `vcom_processor::end_frame' functionality can be
        // implemented asynchronously.
        thread_env->attach_queue(processors+p,NULL,"Flush Domain",0,
                                 KDU_THREAD_QUEUE_SAFE_CONTEXT);
      }
    int env_dbuf_height = this->double_buffering_height;
    if ((env_dbuf_height < 0) && (num_threads <= 4))
      env_dbuf_height = 0;

    kdu_long next_queue_sequence_idx=0;
    while ((!(graceful_shutdown_requested || immediate_shutdown_requested)) &&
           wait_for_active_frame())
      { 
        kdu_uint16 min_slope_on_entry = active_stream->min_slope_threshold;
        for (int rep=0; rep <= extra_compression_reps; rep++)
          { 
            int proc = this->active_processor;
            bool need_init = !processors[proc].wait_until_ready(thread_env);
            vcom_stream *stream;
            if ((stream=active_stream) == NULL)
              { // Set up dummy stream to write to -- note that we cannot
                // reliably do this until after the above call to
                // `wait_until_ready'.  Otherwise the dummy stream may
                // actually still be in use by a background `end_frame' job.
                stream = dummy_streams[proc].restart(min_slope_on_entry);
              }
            if (need_init)
              { // Need to initialize second processor for the first time
                assert(proc == 1);
                processors[1].init(processors[0],stream);
              }
            processors[proc].start_frame(layer_sizes,layer_slopes,trim_to_rate,
                                         predict_slope,force_precise,
                                         want_fastest,skip_codestream_comments,
                                         rate_tolerance,env_dbuf_height,
                                         &pp_params,active_frame,stream,
                                         thread_env);
            next_queue_sequence_idx =
              processors[proc].push_samples(active_frame,
                                            next_queue_sequence_idx);
            if (immediate_shutdown_requested)
              break;
            if (rep == extra_compression_reps)
              return_active_frame(); // Last compression iteration
            processors[proc].end_frame(active_stream,queue,thread_env);
            active_stream = NULL;
            active_processor = 1-active_processor;
          }
      }

    // To finish up, we need to inform the processors that they will be
    // receiving no more calls to `end_frame' so they can issue their
    // `kdu_thread_queue::all_done' call as soon as there are no outstanding
    // scheduled jobs.  We can then join upon completion of the system.
    for (p=0; p < 2; p++)
      processors[p].no_more_jobs(thread_env);
    thread_env->join(NULL);
  } catch (kdu_exception exc) {
    exc_code = exc;
    exception_caught = true;
  } catch (std::bad_alloc) {
    exc_code = KDU_MEMORY_EXCEPTION;
    exception_caught = true;
  } catch (...) {
    exc_code = KDU_CONVERTED_EXCEPTION;
    exception_caught = true;
  }
  
  for (p=0; p < 2; p++)
    processors[p].terminate(thread_env,exc_code);
  multi_thread_env.destroy();
  thread_env = NULL;
  
  return_active_frame(); // In case loop ended prematurely for some reason
  if (exception_caught && (active_stream != NULL))
    queue->return_aborted_stream(active_stream,exc_code);
  else if (active_stream != NULL)
    queue->return_unused_stream(active_stream);
  active_stream = NULL;
}
