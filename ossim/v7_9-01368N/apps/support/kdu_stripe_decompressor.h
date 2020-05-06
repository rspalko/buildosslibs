/*****************************************************************************/
// File: kdu_stripe_decompressor.h [scope = APPS/SUPPORT]
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
   Defines the `kdu_stripe_decompressor' object, a high level, versatile
facility for decompressing images in memory by stripes.  The application
provides stripe buffers, of any desired size and passes them to the object to
be filled with decompressed image component samples.  The object takes care of
all the other details to optimally sequence the decompression tasks.  This
allows the image to be decompressed in one hit, into a memory buffer, or to be
recovered incrementally into application-defined stripe buffers.  Provides
an easy way to use Kakadu without having to know much about the JPEG2000, but
advanced developers may still wish to use the lower level mechanisms to avoid
memory copying, or for customed sequencing of the decompression machinery.
******************************************************************************/

#ifndef KDU_STRIPE_DECOMPRESSOR_H
#define KDU_STRIPE_DECOMPRESSOR_H

#include "kdu_compressed.h"
#include "kdu_sample_processing.h"

// Objects declared here:
namespace kdu_supp {
  class kdu_stripe_decompressor;
}

// Declared elsewhere:
namespace kd_supp_local {
  struct kdsd_tile;
  struct kdsd_component_state;
  struct kdsd_queue;
}

namespace kdu_supp {
  using namespace kdu_core;
  
#define KDU_STRIPE_PAD_BEFORE ((int) 0x001)
#define KDU_STRIPE_PAD_AFTER  ((int) 0x002)
#define KDU_STRIPE_PAD_LOW    ((int) 0x100)
#define KDU_STRIPE_PAD_HIGH   ((int) 0x200)

#define KDU_STRIPE_STORE_PREF_STREAMING ((int) 1)
  /* Note: the above flag should not be redefined to anything different, since
     it may interfere with the implementation of SIMD vectorized data
     transfer functions that are not able to directly import this header due
     to different compilation requirements. */

/*****************************************************************************/
/*                         kdu_stripe_decompressor                           */
/*****************************************************************************/

class kdu_stripe_decompressor {
  /* [BIND: reference]
     [SYNOPSIS]
       This object provides a high level interface to the Kakadu decompression
       machinery, which is capable of satisfying the needs of most developers
       while providing essentially a one-function-call solution for simple
       applications.  Most new developers will probably wish to base their
       decompression applications either upon this object, or the
       `kdu_region_decompressor' object.
       [//]
       It should be noted, however, that some performance benefits can be
       obtained by directly interfacing with the `kdu_multi_synthesis' object
       or, at an even lower level, directly creating your own `kdu_synthesis'
       and/or `kdu_decoder' objects, from which to pull individual image
       lines -- these approaches can often avoid unnecessary copying and
       level shifting of image samples.  Nevertheless, there has been a lot
       of demand for a dead-simple, yet also powerful interface to Kakadu,
       and this object is intended to fill that requirement.  In fact, the
       various objects found in the "support" directory
       (`kdu_stripe_compressor', `kdu_stripe_decompressor' and
       `kdu_region_decompressor') are all aimed at meeting the needs of 90% of
       the applications using Kakadu.  That is not to say that these objects
       are all that is required.  You still need to open streams of one
       form or another and create a `kdu_codestream' interface.
       [//]
       In a typical decompression application based on this object, you will
       need to do the following:
       [>>] Create a `kdu_codestream' object.
       [>>] Optionally use one of the
            `kdu_codestream::apply_input_restrictions' functions to adjust the
            portion of the original compressed image that you want to
            recover -- you can also use these functions to configure the
            set of image components you want decompressed and whether or
            not you want any multi-component transforms to be inverted.
       [>>] Initialize the `kdu_stripe_decompressor' object, by calling
            `kdu_stripe_decompressor::start'.
       [>>] Pull image stripes from `kdu_stripe_decompressor::pull_stripe'
            until the image is fully decompressed (you can do it all in one
            go, into a memory buffer of your choice, if you like).
       [>>] Call `kdu_stripe_decompressor::finish' (not strictly necessary).
       [>>] Call `kdu_codestream::destroy'.
       [//]
       For a tuturial example of how to use the present object in a typical
       application, consult the Kakadu demo application,
       "kdu_buffered_decompress".
       [//]
       It is worth noting that this object is built directly on top of the
       services offered by `kdu_multi_synthesis', so for a thorough
       understanding of how things work, you might like to consult the
       documentation for that object as well.
       [//]
       Most notably, the image components manipulated by this object are
       those which are described by the `kdu_codestream' machinery as
       output image components, as opposed to codestream image components.
       For a discussion of the differences between codestream and output
       image components, see the second form of the
       `kdu_codestream::apply_input_restrictions' function.  For our
       purposes here, however, it is sufficient to note that output
       components are derived from codestream components by applying any
       multi-component (or decorrelating colour) transforms.  Output
       components are the image components which the content creator
       intends to be rendered.  Note, however, that if the component access
       mode is set to `KDU_WANT_CODESTREAM_COMPONENTS'
       instead of `KDU_WANT_OUTPUT_COMPONENTS' (see
       `kdu_codestream::apply_input_restrictions'), the codestream image
       components will appear to be the output components, so no loss of
       flexibility is incurred by insisting that this object processes
       output components.
       [//]
       To take advantage of multi-threading, you need to create a
       `kdu_thread_env' object, add a suitable number of working threads to
       it (see comments appearing with the definition of `kdu_thread_env') and
       pass it into the `start' function.  You can re-use this `kdu_thread_env'
       object as often as you like -- that is, you need not tear down and
       recreate the collaborating multi-threaded environment between calls to
       `finish' and `start'.  Multi-threading could not be much simpler.  The
       only thing you do need to remember is that all calls to `start',
       `pull_stripe' and `finish' should be executed from the same thread --
       the one identified by the `kdu_thread_env' reference passed to `start'.
       This constraint represents a slight loss of flexibility with respect
       to the core processing objects such as `kdu_multi_synthesis', which
       allow calls from any thread.  In exchange, however, you get simplicity.
       In particular, you only need to pass the `kdu_thread_env' object into
       the `start' function, after which the object remembers the thread
       reference for you.
       [//]
       From Kakadu version 7.5, the implementation of this object has been
       provided with two different cleanup methods, embodied by the
       `finish' and `reset' functions.  Previously, `finish' cleaned up all
       resources and was implicitly invoked by the destructor; however, this
       was dangerous since it may have led to the use of a `kdu_thread_env'
       reference supplied with `start' that became invalid before the object
       was destroyed.  The destructor now implicitly invokes `reset', but
       that function may be called explicitly to re-use the object after
       a failure or premature termination condition -- be sure to read the
       documentation for `reset' very carefully, since it requires that you
       first wait for any multi-threaded processing to terminate.
       [//]
       Connected with this change, it is worth noting that the `finish'
       function no longer de-allocates all physical memory resources that the
       object may have allocated.  This is useful, since it allows the memory
       to be re-used when `start' is called again, without the overhead of
       re-allocation and potentially moving the memory to a disadvantageous
       location in a NUMA environment.  In most applications where instances
       of this object experience multiple `create'/`finish' cycles, the
       new behaviour can speed things up without any changes required at the
       application level.  However, if you were somehow relying upon
       `finish' deleting all physical memory, keeping many instances of the
       object around without invoking their destructor, you may have to
       modify your application to explicitly invoke `reset' after `finish'.
  */
  public: // Member functions
    KDU_AUX_EXPORT kdu_stripe_decompressor();
      /* [SYNOPSIS]
           All the real initialization is done within `start'.  You may
           use a single `kdu_stripe_decompressor' object to process multiple
           images, bracketing each use by calls to `start' and `finish'.
      */
    ~kdu_stripe_decompressor() { reset(); }
      /* [SYNOPSIS]
           Calls `reset' and `finish' do similar things, but `finish' does
           not clean up all physical memory.  This destructor implicitly
           invokes the `reset' function to ensure that all memory has been
           deallocated.
           [//]
           The `reset' function (and hence this destructor) will work
           correctly if the object was used with a multi-threaded environment
           (i.e., non-NULL `env' argument was passed to `start') and the
           processing was aborted, so long as you have been careful to either
           destroy the multi-threaded environment or invoke `terminate' or
           `join' on a non-NULL `env_queue' that was passed to `start'.  The
           `reset' call (and hence this destructor) is also fine if `finish'
           has already been invoked since the last call to `start'.
           [//]
           If a call to `start' might not have been bracketed by a call to
           `finish' or `reset' already, you must be sure not to destroy the
           `kdu_codestream' object before this destructor is invoked, since
           the `reset' function that is implicitly called here attempts to
           close open tile interfaces that may still exist into the
           codestream.
      */
    KDU_AUX_EXPORT void
      start(kdu_codestream codestream, bool force_precise=false,
            bool want_fastest=false, kdu_thread_env *env=NULL,
            kdu_thread_queue *env_queue=NULL, int env_dbuf_height=-1,
            int env_tile_concurrency=-1,
            const kdu_push_pull_params *multi_xform_extra_params=NULL);
      /* [SYNOPSIS]
           Call this function to initialize the object for decompression.  Each
           call to `start' must be matched by a call to `finish', but you may
           re-use the object to process subsequent images, if you like.
           If you are using the object in a multi-threaded processing
           environment, be sure to read the notes accompanying `reset' and
           `finish' to understand which you should use.  When reading these
           notes, bear in mind also that from Kakadu version 7.5 on, the
           current object's destructor invokes `reset', rather than `finish',
           since the latter was not safe for a destructor.
         [ARG: codestream]
           Interface to a `kdu_codestream' object whose `create' function has
           already been called.  Before passing the code-stream to this
           function, you might like to alter the geometry by calling
           `kdu_codestream::change_appearance', or you might like to restrict
           the spatial region, image components or number of layers which
           will appear to be present during decompression, by calling
           one of the `kdu_codestream::apply_input_restrictions' functions.
         [ARG: force_precise]
           If true, 32-bit internal representations are used by the
           decompression engines created by this object, regardless of the
           precision of the image samples reported by
           `kdu_codestream::get_bit_depth'.
         [ARG: want_fastest]
           If this argument is true and `force_precise' is false, the function
           selects a 16-bit internal representation (usually leads to the
           fastest processing) even if this will result in reduced image
           quality, at least for irreversible processing.  For image
           components which require reversible compression, the 32-bit
           representation must be selected if the image sample precision
           is too high, or else numerical overflow might occur.
         [ARG: env]
           This argument is used to establish multi-threaded processing.  For
           a discussion of the multi-threaded processing features offered
           by the present object, see the introductory comments to
           `kdu_stripe_decompressor'.  We remind you here, however, that
           all calls to `start', `pull_stripe' and `finish' must be executed
           from the same thread, which is identified only in this function.
           [//]
           If you re-use the object to process a subsequent image, you may
           change threads between the two uses, passing the appropriate
           `kdu_thread_env' reference in each call to `start'.
           [//]
           If the `env' argument is NULL, all processing is single threaded.
           Different threads can potentially invoke the `start', `pull_stripe'
           and `finish' functions but they must be serialized by the
           application so that it is not possible to have any more than one
           thread working on any of the compression tasks at once.   
         [ARG: env_queue]
           This argument is ignored unless `env' is non-NULL, in which case
           a non-NULL `env_queue' means that all multi-threaded processing
           queues created inside the present object, by calls to
           `pull_stripe', should be created as sub-queues of the identified
           `env_queue'.
           [//]
           One application for a non-NULL `env_queue' might be one which
           processes two frames of a video sequence in parallel.  There
           can be some benefit to doing this, since it can avoid the small
           amount of thread idle time which often appears at the end of
           the last call to the `pull_stripe' function prior to `finish'.  In
           this case, each concurrent frame would have its own `env_queue',
           and its own `kdu_stripe_decompressor' object.  Moreover, the
           `env_queue' associated with a given `kdu_stripe_decompressor'
           object can be used to run a job which invokes the `start',
           `pull_stripe' and `finish' member functions.  In this case,
           however, it is particularly important that the `start',
           `pull_stripe' and `finish' functions all be called from within
           the execution of a single job, since otherwise there is no
           guarantee that they would all be executed from the same thread,
           whose importance has already been stated above.
           [//]
           Note that `env_queue' is not detached from the multi-threaded
           environment (identified by `env') when the current object is
           destroyed, or by `finish'.  It is, therefore, possible to have
           other `kdu_stripe_decompressor' objects (or indeed any other
           processing machinery) share this `env_queue'.
         [ARG: env_dbuf_height]
           This argument may be used to introduce and control parallelism
           in the DWT processing steps, allowing you to distribute the
           load associated with multiple tile-components across multiple
           threads. In the simplest case, this argument is 0, and parallel
           processing applies only to the block decoding processes.  For
           a small number of processors, this is usually sufficient to keep
           all CPU's active.  If this argument is non-zero, however, the
           `kdu_multi_synthesis' objects on which all processing is based,
           are created with `double_buffering' equal to true and a
           `processing_stripe_height' equal to the value supplied for this
           argument.  See `kdu_multi_synthesis::create' for a more
           comprehensive discussion of double buffering principles and
           guidelines.
           [//]
           Note that the special value -1 is particularly useful, as
           it causes `kdu_multi_synthesis::create' to select a good double
           buffering stripe height automatically.  In the case where the
           codestream contains multiple horizontally adjacent tiles and the
           stripes retrieved via the `pull_stripe' function correspond to
           whole tile rows (preferable and likely to occur if you use the
           `get_recommended_stripe_heights' function to determine good
           stripe heights) the best policy is usually to use an
           `env_dbuf_height' value that is at least half the tile height.
           Otherwise, the best value is usually closer to 30 or 40.  Given
           these complexities, it is usually best to pass -1 for this
           argument (the default), so that the internal machinery is
           free to make these sort of decisions itself.
         [ARG: env_tile_concurrency]
           This argument is of interest when decompressing from codestreams
           with many small tiles, in a multi-threaded (`env' != NULL)
           processing environment.  It is especially interesting whre the
           stripe height used for processing is equal to (or a multiple of)
           the tile height, so that each call to `pull_stripe' results in
           the opening and closing of tiles one by one.  The internal
           machinery prefers to open tiles and start their tile processing
           engines in advance, keeping a list of up to `env_tile_concurrency'-1
           future tile processing engines that have already been started,
           while data is being pulled out of a current tile processing
           engine.  These future tile processing engines can contribute
           processing jobs to the multi-threaded processing machinery, but
           those jobs have progressively lower priority the further they
           are from the current tile of interest.  This ensures that the
           extra jobs are used only to keep threads from going idle, not to
           delay processing of the current active tile.  Larger values of
           `env_tile_concurrency' reduce the risk that any thread needs to
           go idle, increasing overall processing throughput, at the expense
           of larger memory concumption.  If the value is overly large, there
           may also be some negative impact on the efficiency with which jobs
           are dequeued by the underlying multi-threaded processing machinery.
           [//]
           If the value passed for this argument is less than or equal to 0,
           the internal machinery automatically selects a reasonable tile
           concurrency level.  The algorithm used to do this may be very
           simple, but may also evolve over time, so it is always worth
           testing the performance of your application with a variety of
           different values for this arguement.
           [//]
           Whatever value is passed for this argument, the actual value used
           internally is limited to at most 1 more than the number of tiles
           spanned by the image width, so that the maximum number of future
           tile processing engines that will be started is at most equal to
           the number of tiles across the image.
           [//]
           If the stripes retrieved via `pull_stripes' are not high enough
           to span an entire row of tiles, the impact of this argument is
           slightly different.  In this case, the internal machinery always
           needs to keep an entire row of open tiles with active tile
           processing engines.  If the `env_tile_concurrency' argument is
           not equal to 1, the function also starts tile processing engines
           for the next row of tiles -- i.e., one whose future row of tile
           processing engines.  Otherwise, the current row of tile processing
           engines is started only just in time -- no concurrency across
           rows of tiles.
           [//]
           For maximum multi-threaded processing efficiency when working with
           small tiles, you should pull stripes whose height is exactly one
           tile height, setting `env_dbuf_height' equal to half the stripe
           height (or a little more) and setting `env_tile_concurrency' to
           a modest number (e.g., 4).  The `env_dbuf_height' strategy is
           implemented automatically if you pass -1 for that argument, which
           is usually best and simplest.  The `env_tile_concurrentcy' strategy
           is also likely to be implemented automatically if you pass 0 or a
           a negative value for this argument.  The reason for selecting
           such a large DWT double buffering size for images with lots of
           small tiles is that it allows each tile processing engine to buffer
           all samples in its tile so that future tile processing engines
           can run to completion, if required, while data is being pulled
           from a current tile.  This allows all `env_tile_concurrency'
           concurrently active tile processing engines to contribute the
           maximum possible number of processing jobs to the multi-threaded
           job pool.  The internal machinery ensures that the tile processing
           engines are prioritised by assigning increasing sequence indices to
           each engine's `kdu_thread_queue', so that tiles almost certainly
           complete in order, minimising the risk that the thread which calls
           `pull_stripe' is unnecessarily suspended, since that might
           eventually starve other parts of the system of sufficient work.
         [ARG: multi_xform_extra_params]
           This optional argument is passed along internally to the
           `kdu_multi_synthesis::create' function when it is called to
           set up each tile processing engine, which may give you extra
           control over the internal operation of the compression machinery.
      */
    KDU_AUX_EXPORT bool finish();
      /* [SYNOPSIS]
           Each call to `start' must be bracketed by a call to either
           `finish' or `reset', unless you intend to use the object only
           once, in which case the destructor implicitly calls `reset'.  It
           is important that you know the difference between `finish' and
           `reset', especially in multi-threaded applications.  The `finish'
           function does the following things:
           [>>] Waits for any multi-threaded processing initiated by the
                object to complete, requesting premature completion first.
           [>>] Invokes `kdu_thread_env::cs_terminate' on any non-NULL
                `env' object that was passed to `start'.
           [>>] Destroys all `kdu_multi_synthesis' tile-processing engines. 
           [>>] Closes any open tile interfaces on the `codestream' that
                was passed to `start', being careful to also close any
                tiles that may have been the subject of a background tile
                opening request (multi-threaded processing case only).
           [//]
           This means that any non-NULL `env' argument that was passed to
           `start' must still refer to a valid `kdu_thread_env' object
           that has not been destroyed, by the time this function is called.
           [//]
           Note also that this function does not actually deallocate the
           primary memory surfaces that were allocated for internal tile
           processing.  These are deliberately retained internally so that
           they can be re-used if the `start' function is called again.  If
           you wish to deallocate these immediately, you can either invoke
           `reset' or the object's destructor.
           [//]
           You can always call `finish' or `reset' again after this function
           returns, without doing any harm.  In fact, a subsequent call to
           `reset' is exactly what you need to free all allocated memory,
           noting that this also happens in the destructor.
         [RETURNS]
           True only if all available image data was recovered using the
           `pull_stripe' function.  Regardless of the return value, however,
           all processing (including background multi-threaded processing)
           is terminated by this call.
      */
    KDU_AUX_EXPORT void reset(bool free_memory=true);
      /* [SYNOPSIS]
           Each call to `start' must be bracketed by a call to either
           `finish' or `reset', although the destructor itself invokes `reset'.
           Like `finish', this function does nothing if the object has
           already been finished or reset.  The main differences between this
           function and `reset' are:
           [>>] This function completely ignores any `kdu_thread_env' reference
                that may have been passed to `start', assuming that the
                multi-threaded environment has either been destroyed, or
                at least all multi-threaded work related to this object has
                and the `codestream' passed to `start' has been terminated.
           [>>] The above property means that you must call this function
                instead of `finish' if a non-NULL `kdu_thread_env' reference
                was passed to `start' but the multi-threaded environment has
                since been destroyed (e.g., during exception handling).
           [>>] Unlike `finish', this function de-allocates all memory
                resources, unless you pass false in the `free_memory'
                argument.
           [//]
           If you did pass a non-NULL `env' argument to `start' and you call
           this function in place of `finish', you need to keep the following
           in mind:
           [>>] You must be sure that there is no multi-threaded processing
                going on when this call arrives.  One way to ensure this is
                to destroy the multi-threaded processing environment.  Another
                way is to invoke `kdu_thread_queue::terminate' or
                `kdu_thread_queue::join' (not so interesting for abortive
                processing) on a non-NULL `env_queue' that was passed to
                `start'.
           [>>] If the multi-threaded processing environment is not destroyed,
                you should also note that the `kdu_thread_env::cs_terminate'
                function needs to be explicitly called first, before invoking
                this function!!!
           [//]
           You should be sure to call this function or `finish' before
           destroying the `kdu_codestream' interface that was passed to
           `start'.
           [//]
           In summary, if you have a live multi-threaded environment still
           running, you must do things in the following order:
           [>>] Terminate or join on `env_queue' passed to `start'.
           [>>] `kdu_thread_env::cs_terminate' the codestream passed to `start'
           [>>] Call this `reset' function
           [>>] `kdu_codestream::destroy' -- can happen any time.
         [ARG: free_memory]
           Normally, calls to `reset' should deallocate all internal memory
           resources; however, if you wish to retain the memory, so that it
           can be used again after a subsequent call to `start', this can be
           arranged by passing false for the `free_memory' argument.  This
           might be appropriate if an exception occurred, causing you to
           destroy a multi-threaded processing environment and invoke `reset',
           but you wish to reconstruct the multi-threaded processing
           environment and re-enter the `start' function without having to
           re-allocate all resources.
      */
    KDU_AUX_EXPORT bool
      get_recommended_stripe_heights(int preferred_min_height,
                                     int absolute_max_height,
                                     int stripe_heights[],
                                     int *max_stripe_heights);
      /* [SYNOPSIS]
           Convenience function, provides recommended stripe heights for the
           most efficient use of the `pull_stripe' function, subject to
           some guidelines provided by the application.
           [//]
           If the image is vertically tiled, the function recommends stripe
           heights which will advance each component to the next vertical tile
           boundary.  If any of these exceed `absolute_max_height', the
           function scales back the recommendation.  In either event, the
           function returns true, meaning that this is a well-informed
           recommendation and doing anything else may result in less
           efficient processing.
           [//]
           If the image is not tiled (no vertical tile boundaries), the
           function returns small stripe heights which will result in
           processing the image components in a manner which is roughly
           proportional to their dimensions.  In this case, the function
           returns false, since there are no serious efficiency implications
           to selecting quite different stripe heights.  The stripe height
           recommendations in this case are usually governed by the
           `preferred_min_height' argument.
           [//]
           In either case, the recommendations may change from time to time,
           depending on how much data has already been retrieved for each
           image component by previous calls to `pull_stripe'.  However,
           the function will never recommend the use of stripe heights larger
           than those returned via the `max_stripe_heights' array.  These
           maximum recommendations are determined the first time the function
           is called after `start'.  New values will only be computed if
           the object is re-used by calling `start' again, after `finish'.
         [RETURNS]
           True if the recommendation should be taken particularly seriously,
           meaning there will be efficiency implications to selecting different
           stripe heights.
         [ARG: preferred_min_height]
           Preferred minimum value for the recommended stripe height of the
           image component which has the largest stripe height.  This value is
           principally of interest where the image is not vertically tiled.
         [ARG: absolute_max_height]
           Maximum value which will be recommended for the stripe height of
           any image component.
         [ARG: stripe_heights]
           Array with one entry for each image component, which receives the
           recommended stripe height for that component.  Note that the number
           of image components is the value returned by
           `kdu_codestream::get_num_components' with its optional
           `want_output_comps' argument set to true.
         [ARG: max_stripe_heights]
           If non-NULL, this argument points to an array with one entry for
           each image component, which receives an upper bound on the stripe
           height which this function will ever recommend for that component.
           There is no upper bound on the stripe height you can actually use
           in a call to `pull_stripe', only an upper bound on the
           recommendations which this function will produce as it is called
           from time to time.  Thus, if you intend to use this function to
           guide your stripe selection, the `max_stripe_heights' information
           might prove useful in pre-allocating storage for stripe buffers
           provided by your application.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_byte *stripe_bufs[], int stripe_heights[],
                  int *sample_gaps=NULL, int *row_gaps=NULL,
                  int *precisions=NULL, int *pad_flags=NULL,
                  int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Decompresses new vertical stripes of samples from each image
           component.  The number of entries in each of the arrays here is
           equal to the number of image components, as returned by
           `kdu_codestream::get_num_components' with its optional
           `want_output_comps' argument set to true -- note that this value
           is affected by calls to `kdu_codestream::apply_input_restrictions'
           which may have been made prior to supplying the `kdu_codestream'
           object to `start'.   Each stripe spans the entire width of its
           image component, which must be no larger than the ratio between the
           corresponding entries in the `row_gaps' and `sample_gaps' arrays.
           [//]
           Each successive call to this function advances the vertical position
           within each image component by the number of lines identified within
           the `stripe_heights' array.  Although components nominally advance
           from the top to the bottom, if `kdu_codestream::change_appearance'
           was used to flip the appearance of the vertical dimension, the
           function actually advances the true underlying image
           components from the bottom up to the top.  This is exactly what one
           should expect from the description of
           `kdu_codestream::change_appearance' and requires no special
           processing in the implemenation of the present object.
           [//]
           Although considerable flexibility is offered with regard to stripe
           heights, there are a number of constraints.  As a general rule, you
           should endeavour to advance the various image components in a
           proportional way, when processing incrementally (as opposed to
           decompressing the entire image into a single buffer, with a single
           call to this function).  What this means is that the stripe height
           for each component should, ideally, be inversely proportional to its
           vertical sub-sampling factor.  If you do not intend to decompress
           the components in a proportional fashion, the following notes should
           be taken into account:
           [>>] If the image happens to be tiled, then you must follow
                the proportional processing guideline at least to the extent
                that no component should fall sufficiently far behind the rest
                that the object would need to maintain multiple open tile rows
                simultaneously.
           [>>] If a code-stream colour transform (ICT or RCT) is being used,
                you must follow the proportional processing guideline at least
                to the extent that the same stripe height must be used for the
                first three components (otherwise, internal application of the
                colour transform would not be possible).
           [//]
           In addition to the constraints and guidelines mentioned above
           regarding the selection of suitable stripe heights, it is worth
           noting that the efficiency (computational and memory efficiency)
           with which image data is decompressed depends upon how your
           stripe heights interact with image tiling.  If the image is
           untiled, you are generally best off working with small stripes,
           unless your application naturally provides larger stripe buffers.
           If, however, the image is tiled, then the implementation is most
           efficient if your stripes happen to be aligned on vertical tile
           boundaries.  To simplify the determination of suitable stripe
           heights (all other things being equal), the present object
           provides a convenient utility, `get_recommended_stripe_heights',
           which you can call at any time.
           [//]
           To understand the interpretation of the sample byte values retrieved
           by this function, consult the comments appearing with the
           `precisions' argument below.  Other forms of the overloaded
           `pull_stripe' function are provided to allow for the accurate
           representation of higher precision image samples.
           [//]
           Certain internal paths involve heavily optimized data transfer
           routines that may exploit the availability of SIMD instructions.
           Currently, SSSE3 and AVX2 based optimizations exist for the
           following conditions, most of which also have ARM-NEON
           optimizations also:
           [>>] Conversion from all but the 32-bit absolute integer
                representation (high precision reversible processing) to
                buffer organizations with a sample-gap of 1 (i.e., separate
                memory blocks for each component).
           [>>] Conversion from all but the 32-bit absolute integer
                representation (high precision reversible processing) to
                sample interleaved buffers with a sample-gap of 3 (e.g.,
                RGB interleaved).
           [>>] Conversion from all but the 32-bit absolute integer
                representation (high precision reversible processing) to
                sample interleaved buffers with a sample-gap of 4 (e.g.,
                RGBA interleaved).  Note that in this specific case, the
                fourth component (e.g., alpha) can be synthesized on the
                fly in a particularly efficient way based on the `pad_flags',
                for the case where only 3 actual image components are
                present (e.g, RGB decompressed, but RGBA written with a dummy
                A value).
           [//]
           If you are not exciting one of the above optimization paths, you
           could find that the transfer of decompressed imagery to your
           stripe buffers is actually the bottleneck in the overall processing
           pipeline, because this part runs in a single thread, while
           everything else can potentially be heavily multi-threaded.  By and
           large, the above optimization paths cover almost everything that
           is useful, except that the `pad_flags' may be required to allow
           the internal machinery to recognize when it can synthesize an
           extra channel to write 4-way interleaved RGB organizations
           efficiently.
           [//]
           In the event that an error is generated for some reason through
           `kdu_error', this function may throw an exception -- assuming
           the error handler passed to `kdu_customize_errors' throws
           exceptions.  These exceptions should be of type `kdu_exception' or
           possibly of type `std::bad_alloc'.  In any case, you should be
           prepared to catch such errors in a robust application.  In
           multi-threaded applications (where a non-NULL `env' argument was
           passed to `start'), you should pass any caught exception to
           the `env' object's `kdu_thread_entity::handle_exception' function.
           After doing this, you should still invoke `finish', either directly
           or indirectly by invoking the current object's destructor.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_bufs]
           Array with one entry for each image component, containing a pointer
           to a buffer which holds the stripe samples for that component.
           The pointers may all point into a single common buffer managed by
           the application, or they might point to separate buffers.  This,
           together with the information contained in the `sample_gaps' and
           `row_gaps' arrays allows the application to implement a wide
           variety of different stripe buffering strategies.  The entries
           (pointers) in the `stripe_bufs' array are not modified by this
           function.
         [ARG: stripe_heights]
           Array with one entry for each image component, identifying the
           number of lines to be decompressed for that component in the present
           call.  All entries must be non-negative.  See the discussion above,
           on the various constraints and guidelines which may exist regarding
           stripe heights and their interaction with tiling and sub-sampling.
         [ARG: sample_gaps]
           Array containing one entry for each image component, identifying the
           separation between horizontally adjacent samples within the
           corresponding stripe buffer found in the `stripe_bufs' array.  If
           this argument is NULL, all component stripe buffers are assumed to
           have a sample gap of 1.
         [ARG: row_gaps]
           Array containing one entry for each image component, identifying
           the separation between vertically adjacent samples within the
           corresponding stripe buffer found in the `stripe_bufs' array.  If
           this argument is NULL, all component stripe buffers are assumed to
           hold contiguous lines from their respective components.
         [ARG: precisions]
           If NULL, all component precisions are deemed to be 8; otherwise, the
           argument points to an array with a single precision value for each
           component.  The precision identifies the number of significant bits
           used to represent each sample.  If this value is less than 8, the
           remaining most significant bits in each byte will be set to 0.
           [//]
           There is no implied connection between the precision values, P, and
           the bit-depth, B, of each image component, as found in the
           code-stream's SIZ marker segment, and returned via
           `kdu_codestream::get_bit_depth'.  The original image sample
           bit-depth, B, may be larger or smaller than the value of P supplied
           via the `precisions' argument.  In any event, the most significant
           bit of the P-bit integer represented by each sample byte is aligned
           with the most significant bit of the B-bit integers associated
           with the original compressed image components.
           [//]
           These conventions, provide the application with tremendous
           flexibility in how it chooses to represent image sample values.
           Suppose, for example, that the original image sample precision for
           some component is only B=1 bit, as recorded in the code-stream
           main header.  If the value of P provided by the `precisions' array
           is set to 1, the bi-level image information is written into the
           least significant bit of each byte supplied to this function.  On
           the other hand, if the value of P is 8, the bi-level image
           information is written into the most significant bit of each byte.
           [//]
           The sample values recovered using this function are always unsigned,
           regardless of whether or not the original image samples had a
           signed or unsigned representation (as recorded in the SIZ marker
           segment, and returned via `kdu_codestream::get_bit_depth').  If
           the original samples were signed, or the application requires a
           signed representation for other reasons, the application is
           responsible for level adjusting the data returned here, subtracting
           2^{P-1} from the unsigned values.
         [ARG: pad_flags]
           If non-NULL, this argument points to an array with one entry per
           component, which may be used to specify additional "padding"
           channels that are of particular interest for the case where
           R, G and B components are interleaved with a non-existent A (alpha)
           component -- a common configuration.  Although useful primarily in
           this case, the definition is generic, as folows.
           [>>] If the entry for component c is 0, nothing special happens.
           [>>] If the entry for compnent c includes the flag
                `KDU_STRIPE_PAD_BEFORE' the interleaved component
                group to which component c belongs is augmented with a dummy
                component, whose first sample occurs at location
                `stripe_bufs[c]'-1.  The dummy component has the same
                dimensions, precision, sample gap and row gap as component c.
           [>>] If the entry for compnent c includes the flag
                `KDU_STRIPE_PAD_AFTER', the interleaved component
                group to which component c belongs is augmented with a dummy
                component, whose first sample occurs at location
                `stripe_bufs[c]'+1.  The dummy component has the same
                dimensions, precision, sample gap and row gap as component c.
           [>>] If the entry for component c includes both
                `KDU_STRIPE_PAD_BEFORE' and `KDU_STRIPE_PAD_AFTER', the
                latter is ignored -- you should not do this, though.
           [>>] If the entry for component c includes the flag
                `KDU_STRIPE_PAD_HIGH', any inserted dummy component is padded
                with the maximum value that can be written to component c.
           [>>] If the entry for component c includes the flag
                `KDU_STRIPE_PAD_LOW', any inserted dummy component is padded
                with the minimum value (0 in this function, but may be
                different in other versions of the `pull_stripe' function)
                that can be written to component c.
           [>>] If both `KDU_STRIPE_PAD_HIGH' and `KDU_STRIPE_PAD_LOW' are
                present, the latter is ignored -- you should not do this,
                though.  If neither flag is present, the padding value is
                the natural mid-point between the two extremes.
           [//]
           The main application for the `pad_flags' argument is for
           efficiently writing to buffers that are interleaved with a sample
           gap of 4, where only 3 of the interleaved slots are occupied by
           actual decompressed image values.  For example, suppose that
           components 0, 1 and 2 represents Red, Green and Blue colour samples,
           all with identical dimensions, and we wish to write the data to a
           buffer with 32-bit pixels.  This can be particularly efficient if
           we are prepared to insert a padding value for the missing channel
           (say Alpha).  If Alpha goes at the end of the interleaved group
           and assumes the default maximum value (typically 255, or opaque),
           we would supply a 3-element `pad_flags' array containing the values
              [0, 0 and (KDU_STRIPE_PAD_AFTER | KDU_STRIPE_PAD_HIGH)]
           If Alpha goes at the start of the interleaved group (before Red),
           we would supply
              [(KDU_STRIPE_PAD_BEFORE | KDU_STRIPE_PAD_HIGH), 0, 0]       
           If Alpha goes between Red and Green (a bit weird), we could supply
              [0, (KDU_STRIPE_PAD_BEFORE | KDU_STRIPE_PAD_HIGH), 0]
           or
              [(KDU_STRIPE_PAD_AFTER | KDU_STRIPE_PAD_HIGH), 0, 0]
         [ARG: vectorized_store_prefs]
           This argument may be used to pass flags that provide additional
           information to the vectorized transfer rountines about how the
           data should be most efficiently stored in the output stripe
           buffers.  Currently only one such flag is defined, as follows:
           [>>] `KDU_STRIPE_STORE_PREF_STREAMING' -- if present, this
                flag requests the use of non-temporal vector stores when
                writing to the `stripe_bufs'.  Non-temporal stores are
                intended to write around the processor caches, which may
                help reduce cache pollution.  This option is most appropriate
                when the caller intends to pull the entire decompressed image
                out via one or more calls to this function, before accessing
                the retrieved data again.  For large images this may result in
                more efficient cache utilization, but you should be aware that
                the internal machinery may or may not actually offer a
                streaming store implementation.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_byte *buffer, int stripe_heights[],
                  int *sample_offsets=NULL, int *sample_gaps=NULL,
                  int *row_gaps=NULL, int *precisions=NULL,
                  int *pad_flags=NULL, int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `pull_stripe' function,
           except in the following respect:
           [>>] The stripe samples for all image components are located
                within a single array, given by the `buffer' argument.  The
                location of the first sample of each component stripe within
                this single array is given by the corresponding entry in the
                `sample_offsets' array.
           [//]
           This form of the function is no more useful (in fact less general)
           than the first form, but is more suitable for the automatic
           construction of Java language bindings by the "kdu_hyperdoc"
           utility.  It can also be more convenient to use when the
           application uses an interleaved buffer.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the lines of each component stripe buffer are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the first form of the `pull_stripe' function.
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_int16 *stripe_bufs[], int stripe_heights[],
                  int *sample_gaps=NULL, int *row_gaps=NULL,
                  int *precisions=NULL, bool *is_signed=NULL,
                  int *pad_flags=NULL, int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `pull_stripe' function,
           except in the following respects:
           [>>] The stripe samples for each image component are written with
                a 16-bit representation; as with other forms of the
                `pull_stripe' function, the actual number of bits of this
                representation which are used is given by the `precisions'
                argument, but all 16 bits may be used (this is the default).
           [>>] The default representation for each recovered sample value is
                signed, but the application may explicitly identify whether
                or not each component is to have a signed or unsigned
                representation.  Note that there is no required connection
                between the `Ssigned' attribute managed by `siz_params' and
                the application's decision to request signed or unsigned data
                from the present function.  If the original data for component
                c was unsigned, the application may choose to request signed
                sample values here, or vice-versa.
           [//]
           Certain internal paths involve heavily optimized data transfer
           routines that may exploit the availability of SIMD instructions.
           Currently, SSSE3 and AVX2 based optimizations exist for the
           following conditions, most of which also have ARM-NEON
           optimizations also:
           [>>] Conversion from all the internal representations defined
                by `kdu_sample16' and `kdu_sample32' to buffer
                organizations with a sample-gap of 1 (i.e., separate
                memory blocks for each component).
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: precisions]
           See description of the first form of the `pull_stripe' function,
           but note these two changes: the precision for any component may be
           as large as 16 (this is the default, if `precisions' is NULL);
           and the samples all have a nominally signed representation (not the
           unsigned representation assumed by the first form of the function),
           unless otherwise indicated by a non-NULL `is_signed' argument.
         [ARG: is_signed]
           If NULL, the samples recovered for each component, c, will
           have a signed representation in the range -2^{`precisions'[c]-1} to
           2^{`precisions'[c]-1}-1.  Otherwise, this argument points to
           an array with one element for each component.  If `is_signed'[c]
           is true, the default signed representation is used for that
           component; if false, the component samples have an
           unsigned representation in the range 0 to 2^{`precisions'[c]}-1.
           What this means is that the function adds 2^{`precisions[c]'-1}
           to the samples of any component for which `is_signed'[c] is false.
           It is allowable to have `precisions'[c]=16 even if `is_signed'[c] is
           false, although this means that the `kdu_int16' sample values
           are really being used to store unsigned short integers
           (`kdu_uint16').
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_int16 *buffer, int stripe_heights[],
                  int *sample_offsets=NULL, int *sample_gaps=NULL,
                  int *row_gaps=NULL, int *precisions=NULL,
                  bool *is_signed=NULL, int *pad_flags=NULL,
                  int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the third form of the overloaded `pull_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  Specifically, sample values have a
           16-bit signed (but possibly unsigned, depending upon the
           `is_signed' argument) representation, rather than an 8-bit unsigned
           representation.  As with the second form of the function, this
           fourth form is provided primarily to facilitate automatic
           construction of Java language bindings by the "kdu_hyperdoc"
           utility.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the third form of the `pull_stripe' function.
         [ARG: is_signed]
           See description of the third form of the `pull_stripe' function.
       [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_int32 *stripe_bufs[], int stripe_heights[],
                  int *sample_gaps=NULL, int *row_gaps=NULL,
                  int *precisions=NULL, bool *is_signed=NULL,
                  int *pad_flags=NULL, int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `pull_stripe' function,
           except that stripe samples for each image component are provided
           with a 32-bit representation; as with other forms of the function,
           the actual number of bits of this representation which are
           used is given by the `precisions' argument, but all 32 bits may
           be used (this is the default).
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: precisions]
           See description of the first form of the `pull_stripe' function,
           but note these two changes: the precision for any component may be
           as large as 32 (this is the default, if `precisions' is NULL);
           and the samples all have a nominally signed representation (not the
           unsigned representation assumed by the first form of the function),
           unless otherwise indicated by a non-NULL `is_signed' argument.
         [ARG: is_signed]
           If NULL, the samples recovered for each component, c, will
           have a signed representation in the range -2^{`precisions'[c]-1} to
           2^{`precisions'[c]-1}-1.  Otherwise, this argument points to
           an array with one element for each component.  If `is_signed'[c]
           is true, the default signed representation is used for that
           component; if false, the component samples have an
           unsigned representation in the range 0 to 2^{`precisions'[c]}-1.
           What this means is that the function adds 2^{`precisions[c]'-1}
           to the samples of any component for which `is_signed'[c] is false.
           It is allowable to have `precisions'[c]=32 even if `is_signed'[c] is
           false, although this means that the `kdu_int32' sample values
           are really being used to store unsigned integers (`kdu_uint32').
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(kdu_int32 *buffer, int stripe_heights[],
                  int *sample_offsets=NULL, int *sample_gaps=NULL,
                  int *row_gaps=NULL, int *precisions=NULL,
                  bool *is_signed=NULL, int *pad_flags=NULL,
                  int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the fifth form of the overloaded `pull_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  As with the second form of the function,
           this sixth form is provided primarily to facilitate automatic
           construction of Java language bindings by the "kdu_hyperdoc"
           utility.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the fifth form of the `pull_stripe' function.
         [ARG: is_signed]
           See description of the fifth form of the `pull_stripe' function.
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(float *stripe_bufs[], int stripe_heights[],
                  int *sample_gaps=NULL, int *row_gaps=NULL,
                  int *precisions=NULL, bool *is_signed=NULL,
                  int *pad_flags=NULL, int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `pull_stripe' function,
           except that stripe samples for each image component are provided
           with a floating point representation.  In this case, the
           interpretation of the `precisions' member is slightly different,
           as explained below.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
         [ARG: precisions]
           If NULL, all component samples are deemed to have a nominal range
           of 1.0; that is, signed values lie in the range -0.5 to +0.5,
           while unsigned values lie in the range 0.0 to 1.0; equivalently,
           the precision is taken to be P=0.  Otherwise, the argument points
           to an array with one precision value for each component.  The
           precision value, P, identifies the nominal range of the samples
           which are produced, such that signed values range from
           -2^{P-1} to +2^{P-1}, while unsigned values range from 0 to 2^P.
           [//]
           The value of P, provided by the `precisions' argument may be
           the same, larger or smaller than the actual bit-depth, B, of
           the corresponding image component, as provided by the
           `Sprecision' attribute (or the `Mprecision' attribute) managed
           by the `siz_params' object passed to `kdu_codestream::create'.  The
           relationship between samples represented at bit-depth B and the
           floating point quantities generated by this function is that the
           latter are understood to have been scaled by the value 2^{P-B}.
           [//]
           While this scaling factor seems quite natural, you should pay
           particular attention to its implications for small values of B.
           For example, when P=1 and B=1, the nominal range of unsigned
           floating point quantities is from 0 to 2, while the actual
           range of 1-bit sample values is obviously from 0 to 1.  Thus,
           the maximum "white" value actually occurs when the floating point
           quantity equals 1.0 (half its nominal maximum value).  For signed
           floating point representations, the implications are even less
           intuitive, with the maximum integer value achieved when the
           floating point sample value is 0.0.  More generally, although the
           nominal range of the floating point component sample values is of
           size 2^P, a small upper fraction -- 2^{-B} -- of this nominal range
           lies beyond the range which can be represented by B-bit samples.
           [//]
           There is no guarantee that returned component samples will lie
           entirely within the range dictated by the corresponding B-bit
           integers, or even within the nominal range.  This is because
           the function does not perform any clipping of out-of-range
           values, and the impact of quantization effects in the subband
           domain is hard to quantify precisely in the image domain.
           [//]
           It is worth noting that this function, unlike its predecessors,
           allows P to take both negative and positive values.  For
           implementation reasons, though, we restrict precisions to take
           values in the range -64 to +64.
         [ARG: is_signed]
           If NULL, the samples returned for each component, c, will have a
           signed representation, with a nominal range from
           -2^{`precisions'[c]-1} to +2^{`precisions'[c]-1}.  Otherwise, this
           argument points to an array with one element for each component.  If
           `is_signed'[c] is true, the default signed representation is adopted
           for that component; if false, the component samples are assigned
           an unsigned representation, with a nominal range from 0.0 to
           2^{`precisions'[c]}.  What this means is that the function adds
           2^{`precisions[c]'-1} to the samples of any component for which
           `is_signed'[c] is false, before returning them -- if `precisions'
           is NULL, 0.5 is added.
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
    KDU_AUX_EXPORT bool
      pull_stripe(float *buffer, int stripe_heights[],
                  int *sample_offsets=NULL, int *sample_gaps=NULL,
                  int *row_gaps=NULL, int *precisions=NULL,
                  bool *is_signed=NULL, int *pad_flags=NULL,
                  int vectorized_store_prefs=0);
      /* [SYNOPSIS]
           Same as the seventh form of the overloaded `pull_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  As with the second, fourth and sixth forms of
           the function, this eighth form is provided primarily to facilitate
           automatic construction of Java language bindings by the
           "kdu_hyperdoc" utility.
         [RETURNS]
           True until all samples of all image components have been
           decompressed and returned, at which point the function returns
           false.
         [ARG: stripe_heights]
           See description of the first form of the `pull_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `pull_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the seventh form of the `pull_stripe' function.
         [ARG: is_signed]
           See description of the seventh form of the `pull_stripe' function.
         [ARG: pad_flags]
           See description of the first form of the `pull_stripe' function.
         [ARG: vectorized_store_prefs]
           See description of the first form of the `pull_stripe' function.
      */
  private: // Helper functions
    kd_supp_local::kdsd_tile *get_new_tile();
      /* This function first tries to take a tile from the `inactive_tiles'
         list, invoking its `cleanup' function and moving it onto the
         `free_list'.  Regardless of whether or not this succeeds, the
         function then tries to recover a tile from the free list.  If the
         free list is empty, a new tile is created.  This sequence encourages
         the re-use of the tile that was least recently entered onto the
         `inactive_tiles' list, which usually results in de-allocation and
         subsequent re-allocation of exactly the same amount of memory,
         keeping the memory footprint of the application roughly constant
         and thus avoiding costly operating system calls. */
    void note_inactive_tile(kd_supp_local::kdsd_tile *tile);
      /* Moves `tile' onto the list of `inactive_tiles' from which tiles.
         Tiles are pulled from this list in-order and cleaned up before
         being recycled to the free list.  This encourages the re-use of
         recently allocated memory so that there should be few, if any, calls
         to sbrk -- can be a problem when heavily tiled images are processed
         incrementally with an entire row of active tile processing engines. */
    kd_supp_local::kdsd_queue *get_new_queue();
      /* Uses the free list, if possible; returns with the `thread_queue'
         member instantiated, but without any tiles to use it yet. */
    void release_queue(kd_supp_local::kdsd_queue *queue);
      /* Joins upon the queue, then moves all of its tiles to the
         `inactive_tiles' list.  We do not immediately destroy the tile
         processing engines or close the `kdu_tile' interfaces by calling
         `kdsd_tile::cleanup', since this may create large fluctuations in
         memory usage, encouraging expensive OS calls.  Instead, each time
         we need to allocate a new tile, we first cleanup and free one of
         the inactive ones. */
    bool augment_started_queues();
      /* This function is only called in multi-threaded mode (`env' != NULL).
         It aims to create a new `kdsd_queue' object, fill it with the
         appropriate number of tile processing engines and start them all
         running.  The function returns false if all tiles in the codestream
         have already been started so that no new tile queue can be created.
         The function uses the `next_start_idx' member to identify (and update)
         the absolute index of the next tile to be started.  This function
         ensures that all tiles that it creates are hooked up via their
         `next' links, with `partial_tiles' pointing to the head of the list
         of all tiles created.
            This function is also responsible for scheduling future tile
         opening operations to the codestream management machinery's background
         processing jobs via calls to `kdu_codestream::open_tiles'.  This is
         done in a careful manner so that background tile opening operations
         should not greatly affect the reading of compressed data for
         currently active tiles, but should also leave the tiles already
         open by the time their `kdsd_tile::create' function eventually
         gets called. */
    bool pull_common(int vectorized_store_prefs);
      // Common part of `pull_stripe' funcs
  private: // Data
    kdu_codestream codestream;
    kdu_push_pull_params pp_params; // Copy of params passed to `start'
    bool force_precise;
    bool want_fastest;
    bool all_done; // When all samples have been processed
    int num_components;
    kd_supp_local::kdsd_component_state *comp_states;
    kdu_coords left_tile_idx; // Indices of left-most tile in current row
    kdu_coords num_tiles; // Tiles wide and remaining tiles vertically.
    kd_supp_local::kdsd_tile *partial_tiles;
    kd_supp_local::kdsd_tile *inactive_tiles; // List of tiles that are no
    kd_supp_local::kdsd_tile *last_inactive_tile; //  longer in use, but whose
                                          // `engine' is yet to be destroyed.
    kd_supp_local::kdsd_tile *free_tiles;
  private: // Members used for multi-threading
    kdu_thread_env *env; // NULL, if multi-threaded environment not used
    kdu_thread_queue local_env_queue; // Used only with `env'
    int env_dbuf_height; // Used only with `env'
    kd_supp_local::kdsd_queue *active_queue; // Head of list of started queues
    kd_supp_local::kdsd_queue *last_started_queue; // Tail of the list of
                                                   // started queues
    kd_supp_local::kdsd_queue *free_queues; // List of recycled tile queues
    kdu_coords next_start_idx; // Index of next tile to be started with a queue
    int unstarted_tile_rows; // Num tile rows with at least one tile to start
    kdu_long next_queue_idx; // Sequence index for the next tile queue
    int num_future_tiles; // # tiles belonging to non-initial started queues
    int max_future_tiles;
    kdu_dims tiles_to_open; // Range of tiles not yet scheduled for opening
    kdu_coords last_tile_accessed; // Index of the latest (in raster order)
        // tile used to fill out a `kdu_tile' interface; if no tiles have yet
        // been accessed, this member actually references the top-left tile
        // which causes no harm in practice because we only use this member to
        // determine which tiles may have been scheduled for opening but not
        // actually accessed by the time `finish' is called.
  };
  
} // namespace kdu_supp

#endif // KDU_STRIPE_DECOMPRESSOR_H
