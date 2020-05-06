/*****************************************************************************/
// File: kdu_stripe_compressor.h [scope = APPS/SUPPORT]
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
   Defines the `kdu_stripe_compressor' object, a high level, versatile facility
for compressing images in memory by stripes.  The app fills stripe buffers, of
any desired size and passes them to the object, which takes care of all the
other details to optimally sequence the compression tasks.  This allows
the image to be processed in one hit, from a memory buffer, or to be
processed progressively from application-defined stripe buffers.  Provides
an easy way to use Kakadu without having to know much about the JPEG2000, but
advanced developers may still wish to use the lower level mechanisms to avoid
memory copying, or for customed sequencing of the processing machinery.
******************************************************************************/

#ifndef KDU_STRIPE_COMPRESSOR_H
#define KDU_STRIPE_COMPRESSOR_H

#include "kdu_compressed.h"
#include "kdu_sample_processing.h"

// Objects declared here:
namespace kdu_supp {
  class kdu_stripe_compressor;
}

// Declared elsewhere:
namespace kd_supp_local {
  struct kdsc_tile;
  struct kdsc_component_state;
  struct kdsc_queue;
}

namespace kdu_supp {
  using namespace kdu_core;
  
/*****************************************************************************/
/*                          kdu_stripe_compressor                            */
/*****************************************************************************/

class kdu_stripe_compressor {
  /* [BIND: reference]
     [SYNOPSIS]
       This object provides a high level interface to the Kakadu compression
       machinery, which is capable of satisfying the needs of most developers
       while providing essentially a one-function-call solution for simple
       applications.  Most new developers will probably wish to base their
       compression applications upon this object.
       [//]
       It should be noted, however, that some performance benefits can be
       obtained by creating compression engines yourself and directly passing
       them `kdu_line_buf' lines, since this can often avoid unnecessary
       copying and level shifting of image samples.  Nevertheless, there
       has been a lot of demand for a dead-simple, yet also powerful interface
       to Kakadu, and this object is intended to fill that requirement.  In
       fact, the various objects found in the "support" directory
       (`kdu_stripe_compressor', `kdu_stripe_decompressor' and
       `kdu_region_decompressor') are aimed at meeting the needs of 90% of
       the applications using Kakadu.  That is not to say that these objects
       are all that is required.  You still need to open streams of one
       form or another and create a `kdu_codestream' interface.
       [//]
       In a typical compression application based on this object, you will
       need to do the following:
       [>>] Create a `kdu_codestream' object;
       [>>] Use the interface recovered using `kdu_codestream::access_siz' to
            install any custom compression parameters you have in mind, unless
            you are happy with all the defaults;
       [>>] Initialize the `kdu_stripe_compressor' object, by calling
            `kdu_stripe_compressor::start'.
       [>>] Push image stripes into `kdu_stripe_compressor::push_stripe' until
            the image is fully compressed (you can do it all in one go, from
            a memory buffer of your choice, if you like);
       [>>] Call `kdu_stripe_compressor::finish'.
       [>>] Call `kdu_codestream::destroy'.
       [//]
       For a tuturial example of how to use the present object in a typical
       application, consult the Kakadu demo application,
       "kdu_buffered_compress".
       [//]
       It is worth noting that this object is built directly on top of the
       services offered by `kdu_multi_analysis', so for a thorough
       understanding of how things work, you might like to consult the
       documentation for that object as well.
       [//]
       Most notably, the image components which are supplied to the
       `push_stripe' function are those which are known (during
       decompression) as multi-component output components (or just
       output components).  This means that the present object inverts
       any Part 2 multi-component transformation network, which may be
       involved.
       [//]
       To take advantage of multi-threading, you need to create a
       `kdu_thread_env' object, add a suitable number of working threads to
       it (see comments appearing with the definition of `kdu_thread_env') and
       pass it into the `start' function.  You can re-use this `kdu_thread_env'
       object as often as you like -- that is, you need not tear down and
       recreate the collaborating multi-threaded environment between calls to
       `finish' and `start'.  Multi-threading could not be much simpler.  The
       only thing you do need to remember is that all calls to `start',
       `push_stripe' and `finish' should be executed from the same thread --
       the one identified by the `kdu_thread_env' reference passed to `start'.
       This constraint represents a slight loss of flexibility with respect
       to the core processing objects such as `kdu_multi_analysis', which
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
    KDU_AUX_EXPORT kdu_stripe_compressor();
      /* [SYNOPSIS]
           All the real initialization is done within `start'.  You may
           use a single `kdu_stripe_compressor' object to compress multiple
           images, bracketing each use by calls to `start' and `finish'.
      */
    ~kdu_stripe_compressor() { reset(); }
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
           The `finish' function attempts to actually finish all
           processing and codestream flushing, which will prove disasterous
           if a multi-threaded environment was used but has since been
           destroyed.  Thus, it is not generally appropriate to invoke
           `finish' within an exception handling routine.
           [//]
           If a call to `start' might not have been bracketed by a call to
           `finish' or `reset' already, you must be sure not to destroy the
           `kdu_codestream' object before this destructor is invoked, since
           the `reset' function that is implicitly called here attempts to
           close open tile interfaces that may still exist into the
           codestream.
      */
    KDU_AUX_EXPORT void
      start(kdu_codestream codestream, int num_layer_specs=0,
            const kdu_long *layer_sizes=NULL,
            const kdu_uint16 *layer_slopes=NULL,
            kdu_uint16 min_slope_threshold=0,
            bool no_prediction=false, bool force_precise=false,
            bool record_layer_info_in_comment=true,
            double size_tolerance=0.0, int num_components=0,
            bool want_fastest=false, kdu_thread_env *env=NULL,
            kdu_thread_queue *env_queue=NULL, int env_dbuf_height=-1,
            int env_tile_concurrency=-1,
            bool trim_to_rate=true, int flush_flags=0,
            const kdu_push_pull_params *multi_xform_extra_params=NULL);
      /* [SYNOPSIS]
           Call this function to initialize the object for compression.  Each
           call to `start' must be matched by a call to either `finish'
           or `reset', after which you may re-use the object to compress
           subsequent images, if you like.  If you are using the object in
           a multi-threaded processing environment, be sure to read the notes
           accompanying `reset' and `finish' to understand which you should
           use.  When reading these notes, bear in mind also that from
           Kakadu version 7.5 on, the current object's destructor invokes
           `reset', rather than `finish', since the latter was not safe for
           a destructor.
         [ARG: codestream]
           Interface to a `kdu_codestream' object whose `create' function has
           already been called.  The `kdu_params::finalize_all' function should
           not be called by the application; it will be invoked from within
           the present function, possibly after making some final adjustments
           to coding parameter attributes which have not been configured by
           the application.
         [ARG: num_layer_specs]
           If this argument is equal to 0, the number of quality layers to
           build into the code-stream is recovered from the `Clayers' coding
           parameter attribute, which the application may have been configured
           prior to calling this function.  If the `Clayers' attribute was not
           set, it will default to 1 when the `kdu_params::finalize_all'
           function is called from within this function.  If the present
           argument is greater than 0, and the `Clayers' attribute has not
           already been set, it will be set equal to the value of
           `num_layer_specs'.  Regardless of the final number of code-stream
           quality layers which are used, the `num_layer_specs' argument
           provides the number of entries in the `layer_sizes' and
           `layer_slopes' arrays, if non-NULL.  These arrays, if provided,
           allow the application to specify the properties of the quality
           layers. If no layer sizes or slopes are specified, a logarithmically
           spaced set of quality layers will be constructed, following the
           conventions described with the `kdu_codestream::flush' function.
         [ARG: layer_sizes]
           If non-NULL, this argument points to an array with `num_layer_specs'
           entries, containing the cumulative number of bytes from the
           start of the code-stream to the end of each quality layer, if the
           code-stream were to be arranged in layer progressive order.  The
           code-stream may be arranged in a very different order, of course,
           but that has no impact on the sizes of the layers, as controlled
           by this argument.  If the actual number of quality layers,
           as specified by the `Clayers' attribute, is smaller than
           `num_layer_specs', not all of the entries in this array will be
           used.  If the actual number of quality layers is larger than
           `num_layer_specs', the additional quality layers will be empty.
           This argument is ignored if `num_layer_specs' is 0.
           [//]
           Note that the interpretation of this argument is modified
           substantially if all of the following conditions hold:
           [>>] the `flush_flags' argument includes the
                `KDU_FLUSH_USES_THRESHOLDS_AND_SIZES' flag;
           [>>] the `flush_flag' argument does not include the
                `KDU_FLUSH_THRESHOLDS_ARE_HINTS' flag; and
           [>>] the `layer_slopes' argument is non-NULL with a first entry
                that is non-zero.
           [//]
           When all of the above conditions hold, the quality layers are
           driven primarily by the distortion-length slope thresholds
           provided via `layer_slopes', while the `layer_sizes' values are
           interpreted as lower bounds on the size of the quality layers
           that come into effect only if the number of bytes produced by
           using the `layer_slopes' values is too small.  This is explained
           more carefully in the comments accompanying `kdu_codestream::flush'.
         [ARG: layer_slopes]
           If non-NULL, this argument points to an array with `num_layer_specs'
           entries, containing distortion-length slope thresholds to use when
           generating each quality layer.  This argument is ignored if
           `num_layer_specs' is 0.
           [//]
           The argument is also ignored if `layer_sizes' is non-NULL, except
           in the event that `flush_flags' includes the
           `KDU_FLUSH_THRESHOLDS_ARE_HINTS' flag or the
           `KDU_FLUSH_USES_THRESHOLDS_AND_SIZES'.  In the former case,
           quality layer generation is driven by the target sizes supplied via
           the `layer_sizes' array, but the `layer_slopes' values are treated
           as a good starting point for the rate control algorithm, which may
           reduce computational effort in converging to a suitable operating
           point.  The `KDU_FLUSH_USES_THRESHOLDS_AND_SIZES' flag has a very
           special interpretation if the `KDU_FLUSH_THRESHOLDS_ARE_HINTS' flag
           is missing and `layer_slopes'[0] is non-zero.  In this case, the
           quality layer generation process is driven by the distortion-length
           slope thresholds provided here, and any additional information
           supplied via `layer_sizes' is used only to lower bound the
           generated quality layer sizes in the event that the slope
           thresholds produce unexpectedly small quality layers.
           [//]
           For a detailed explanation of the logarithmic representation used
           for distortion length slope thresholds in Kakadu, see the API
           documentation that accompanies the `kdu_codestream::flush' function.
         [ARG: min_slope_threshold]
           If this argument is non-zero, the
           `kdu_codestream::set_min_slope_threshold' function will be used
           to apply this slope threshold prior to compression.  As explained
           in connection with that function, this can help to speed up the
           compression process significantly.  Although the application could
           invoke `kdu_codestream::set_min_slope_threshold' itself, providing
           a non-zero argument here will prevent the present function from
           calling `kdu_codestream::set_max_bytes' if the `layer_sizes' array
           is non-NULL.  More precisely, the function follows the following
           set of rules in determining what speedup features to apply:
           [>>] If `no_prediction' is true, no speedup features are applied;
           [>>] else, if `min_slope_threshold' is non-zero, the value
                will be supplied to `kdu_codestream::set_min_slope_threshold';
           [>>] else, if `layer_sizes' is non-NULL, the last entry in the
                `layer_sizes' array will be passed to
                `kdu_codestream::set_max_bytes';
           [>>] else, if `layer_slopes' is non-NULL, the last entry in the
                `layer_slopes' array will be passed to
                `kdu_codestream::set_min_slope_threshold'.
         [ARG: no_prediction]
           If true, neither the `kdu_codestream::set_max_bytes' function, nor
           the `kdu_codestream::set_min_slope_threshold' function will be
           invoked.  Applications should set this argument to true only if
           they want to adopt a very conservative stance in relation to
           maximizing image quality at the expense of compression speed.  For
           typical images, Kakadu's code-block truncation prediction mechanisms
           have no impact on image quality at all, while saving processing
           time.
         [ARG: force_precise]
           If true, 32-bit internal representations are used by the
           compression engines created by this object, regardless of the
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
         [ARG: record_layer_info_in_comment]
           If true, the rate-distortion slope and the target number of bytes
           associated with each quality layer will be recorded in a COM
           (comment) marker segment in the main code-stream header.  This
           can be very useful for applications which wish to process the
           code-stream later in a manner which depends upon the interpretation
           of the quality layers.  For this reason, you should generally
           set this argument to true, unless you want to get the smallest
           possible file size when compressing small images.  For more
           information on this option, consult the comments appearing with
           its namesake in `kdu_codestream::flush'.
         [ARG: size_tolerance]
           This argument is ignored unless layering is controlled by
           cumulative layer sizes supplied via a `layer_sizes' array.  In
           this case, it may be used to trade accuracy for speed when
           determining the distortion-length slopes which achieve the target
           layer sizes as closely as possible.  In particular, the algorithm
           will finish once it has found a distortion-length slope which
           yields a size in the range target*(1-tolerance) <= size <= target,
           where target is the target size for the relevant layer.  If no
           such slope can be found, the layer is assigned a slope such that
           the size is as close as possible to the target, without exceeding
           it.
         [ARG: num_components]
           If zero, the number of image components to be supplied to the
           `push_stripe' function is identical to the value returned by
           `kdu_codestream::get_num_components', with its optional
           `want_output_comps' argument set to true.  However, you may
           supply a smaller number of image components during compression,
           if you think that these provide sufficient information to
           generate all codestream image components.  This can happen
           where a Part 2 multi-component transformation defines more
           MCT output components than there are codestream image components.
           Then, during compression, it may be possible to invert the
           defined multi-component transform network by supplying only a
           subset of the MCT output components as source components (the
           components supplied to `push_stripe').  If a non-zero
           `num_components' argument is supplied, this is the number of
           components you will push to `push_stripe' -- if there are not
           sufficient components, the machinery will generate an appropriate
           error message through `kdu_error'.  For more on this, consult the
           description of the `kdu_multi_analysis' object on which this
           is built.
         [ARG: env]
           This argument is used to establish multi-threaded processing.  For
           a discussion of the multi-threaded processing features offered
           by the present object, see the introductory comments to
           `kdu_stripe_compressor'.  We remind you here, however, that
           all calls to `start', `push_stripe' and `finish' must be executed
           from the same thread, which is identified only in this function,
           except where those functions explicitly provide a non-NULL
           `alt_env' argument -- see `finish' for a discussion of this.
           [//]
           If you re-use the object to process a subsequent image, you may
           change threads between the two uses, passing the appropriate
           `kdu_thread_env' reference in each call to `start'.
           [//]
           If the `env' argument is NULL, all processing is single threaded.
           Different threads can potentially invoke the `start', `push_stripe'
           and `finish' functions but they must be serialized by the
           application so that it is not possible to have any more than one
           thread working on any of the compression tasks at once.
         [ARG: env_queue]
           This argument is ignored unless `env' is non-NULL, in which case
           a non-NULL `env_queue' means that all multi-threaded processing
           queues created inside the present object, by calls to
           `push_stripe', should be created as sub-queues of the identified
           `env_queue'.
           [//]
           Note that `env_queue' is not detached from the multi-threaded
           environment (identified by `env') when the current object is
           destroyed, or by `finish'.  It is, therefore, possible to have
           other `kdu_stripe_compressor' objects (or indeed any other
           processing machinery) share this `env_queue'.       
         [ARG: env_dbuf_height]
           This argument may be used to introduce and control parallelism
           in the DWT processing steps, allowing you to distribute the
           load associated with multiple tile-components across multiple
           threads. In the simplest case, this argument is 0, and parallel
           processing applies only to the block encoding processes.  For
           a small number of processors, this is usually sufficient to keep
           all CPU's active.  If this argument is non-zero, however, the
           `kdu_multi_analysis' objects on which all processing is based,
           are created with `double_buffering' equal to true and a
           `processing_stripe_height' equal to the value supplied for this
           argument.  See `kdu_multi_analysis::create' for a more
           comprehensive discussion of double buffering principles and
           guidelines.
           [//]
           Note that the special value -1 is particularly useful, as
           it allows the internal machinery to select a good double buffering
           stripe height automatically.  In the case where the codestream
           contains multiple horizontally adjacent tiles and the stripes
           pushed into the `push' function correspond to whole tile rows
           (preferable and likely to occur if you use the
           `get_recommended_stripe_heights' function to determine good
           stripe heights) the best policy is usually to use an
           `env_dbuf_height' value that is at least half the tile height.
           Otherwise, the best value is usually closer to 30 or 40.
           Given these complexities, it is usually best to pass -1 for
           this argument (the default) so that the internal machinery is
           free to make these sort of decisions itself.
         [ARG: env_tile_concurrency]
           This argument is of interest when compressing codestreams with
           many small tiles, with multi-threaded processing (`env' != NULL).
           It is especially interesting where the stripe height used for
           processing is equal to (or a multiple of) the tile height, so that
           each call to `push_stripe' results in the opening and closing of
           tiles one by one.  Rather than closing a tile as soon as all samples
           have been pushed into its `kdu_multi_analysis' processing engine,
           the internal machinery keeps a list of up to
           `env_tile_concurrency'-1 such "finished" tiles around.  This means
           that in cases where tiles are opened, processed and closed one by
           one, the total number of concurrently active tiles is given by
           `env_tile_concurrency'.  Larger values of `env_tile_concurrency'
           increase the likelihood that when the call to `push' must join
           upon completion of the least recently finished tile, the join
           succeeds immediately.  On the other hand, increasing the tile
           concurrency obviously also increases working memory; it may also
           slightly reduce the distributed job scheduling efficiency in the
           internal system.
           [//]
           If the value passed for this argument is less than or equal to 0,
           the internal machinery automatically selects a reasonable tile
           concurrency level.  The algorithm used to do this may be very
           simple, but may also evolve over time, so it is always worth
           testing the performance of your application with a variety of
           different values for this argument.
           [//]
           Whatever value is passed for this argument, the actual value used
           internally is limited to at most 1 more than the number of tiles
           spanned by the image width, so that the maximum number of finished
           tiles that are kept around is no more than the number of tiles
           across the image.
           [//]
           If the pushed stripes are not high enough to span an entire row of
           tiles, the impact of this argument is slightly different.  In this
           case, the internal machinery always needs to keep an entire row of
           open tiles with active tile processing engines.  If the
           `env_tile_concurrency' argument is not equal to 1, the function also
           keeps the previous row of tile processing engines open as well, so
           that their internal processing jobs can complete while the current
           row of tiles is being started.
           [//]
           For maximum multi-threaded processing efficiency when working with
           small tiles, you should push stripes whose height is exactly one
           tile height, setting `env_dbuf_height' equal to half the stripe
           height (or a little more) and setting `env_tile_concurrency' to
           a modest number (e.g., 4).  The `env_dbuf_height' strategy is
           implemented automatically if you pass -1 for that argument, which
           is usually best and simplest.  The `env_tile_concurrentcy' strategy
           is also likely to be implemented automatically if you pass 0 or a
           a negative value for this argument.  The reason for selecting
           such a large DWT double buffering size for images with lots of
           small tiles is that it allows each tile processing engine to buffer
           all samples in its tile so that the main thread is not held up
           waiting for the tile to be processed.  This allows all
           `env_tile_concurrency' concurrently open tiles to offer processing
           jobs for the worker threads to complete.  The internal machinery
           ensures that the tile processing engines are prioritised by
           assigning increasing sequence indices to each engine's
           `kdu_thread_queue', so that tiles almost certainly complete
           in order.
         [ARG: trim_to_rate]
           This argument is passed through to `kdu_codestream::flush' and
           `kdu_codestream::auto_flush', when they are used internally,
           indicating whether or not the final codestream flushing operation
           should make a final rate control pass through all available
           code-blocks to determine whether any of them can add an extra
           coding pass to the final quality layer in such a way as to use
           up any byte limit communicated via the last entry in `layer_sizes'.
           This argument is irrelevant unless the `layer_sizes', `layer_slopes'
           and `flush_flags' parameters indicate that rate control will be
           based on byte limits.  It is also irrelevant if the final entry
           in `layer_sizes' is 0 (unlimited) or there is no such entry
           (e.g., `num_layer_specs'=0 or `layer_sizes'=NULL).
         [ARG: flush_flags]
           This argument affects the interpretation of the `layer_sizes'
           and `layer_slopes' arguments in cases where both are non-NULL.
           The argument is passed to `kdu_codestream::flush' and related
           functions, such as `kdu_codestream::auto_flush', where the
           interpretation of the flags is discussed in detail.  The key
           flags are `KDU_FLUSH_THRESHOLDS_ARE_HINTS' and
           `KDU_FLUSH_USES_THRESHOLDS_AND_SIZES', but others may be defined
           in the future.  See the descriptions of `layer_sizes' and
           `layer_slopes' above for more on how these flags affect their
           interaction.
         [ARG: multi_xform_extra_params]
           This optional argument is passed along internally to the
           `kdu_multi_analysis::create' function when it is called to
           set up each tile processing engine, which may give you extra
           control over the internal operation of the compression machinery.
      */
    KDU_AUX_EXPORT kdu_long
      get_set_next_queue_sequence(kdu_long min_val);
      /* [SYNOPSIS]
           This function is provided to support advanced multi-threading
           functionality, where multiple `kdu_stripe_compressor' objects may
           be sharing a `kdu_thread_env'.  Internally, multi-threaded
           processing instantiates and attaches `kdu_thread_queue' objects
           to the thread-group identified to `start' via the `env' argument,
           assigning them sequence indices that are monotonically increasing
           (see `kdu_thread_entity::attach' for a detailed explanation of
           thread-queue sequence indices).  For single tile images there
           will only ever be one queue instantiation and attachment, while
           for multi-tile images there may be many (the number depends in
           part upon `env_tile_concurrency').  Each such queue is assigned
           a higher thread-queue sequence index than the last one so that
           threads will not process the jobs associated with later queues
           unless there are no jobs left to run from earlier queues.  This
           helps to keep processing running sequentially while avoiding any
           thread idle time.
           [//]
           While the strategy described above works wonderfully for a single
           stripe-compressor, in some cases (especially video) you may have
           a second stripe-compressors whose internal queues you would like
           to continue the sequence numbering used by a first
           stripe-compressor; this ensures that threads will move seamlessly
           across to do work scheduled by the second stripe-compressor, as
           available jobs drie up within the first.  This is exactly what
           one needs for video compression and related applications to run
           with absolute maximum efficiency.
           [//]
           To achieve this objective, one would push all stripes into the
           first stripe-compressor, then invoke this function on the first
           stripe-compressor to determine the next queue sequence index that
           it would have used (the return value), passing that value into
           the second stripe-compressor's `get_set_next_queue_sequence'
           function as its `min_val' argument.  Typically, one would schedule
           a background processing job to invoke the first stripe-compressor's
           `finish' function asynchronously while pushing stripe data into
           the second stripe-compressor, and so forth.  To see a good example
           of this, refer to the "kdu_vcom_fast" demo app.
         [RETURNS]
           The thread-queue sequence index that the current object would use
           when it next creates and attaches an internal thread queue to
           manage tile processing, ignoring any change that might be induced
           by the `min_val' argument.  Thread queues are created and attached
           only from within calls to `push_stripe', so there will be no
           further queues created if all sample data has already been pushed
           in.
         [ARG: min_val]
           Minimum value that the current object will use when it next needs
           to create and attach an internal thread queue to manage tile
           processing -- this happens inside calls to `push_stripe', unless
           all stripes have already been pushed in, in which case `min_val'
           does not really matter.
           [//]
           If the object's next thread-queue sequence index is already larger
           than `min_val', no change is made -- the caller can, of course,
           detect this by comparing `min_val' with the function's return
           value.
           [//]
           We consider the object's next thread-queue sequence index `val' to
           be smaller than `min_val' if (`val'-`min_val') is a negative
           integer of type `kdu_long'.  That is, a change is made so long as
           addition of a positive integer to the object's next sequence index
           can carry it to `min_val', taking numerical overflow into account.
           This definition ensures that things work correctly even if sequence
           indices wrap-around within the precision afforded by the `kdu_long'
           data type.
      */
    KDU_AUX_EXPORT bool
      finish(int num_layer_specs=0,
             kdu_long *layer_sizes=NULL, kdu_uint16 *layer_slopes=NULL,
             kdu_thread_env *alt_env=NULL);
      /* [SYNOPSIS]
           Each call to `start' must be bracketed by a call to `finish' for
           the compression cycle to be completed.
           [//]
           If you did not push all required image data into the `push_stripe'
           function before calling `finish', the function returns false,
           without flushing additional data to the codestream's compressed
           data target.
           [//]
           Regardless of the value returned by this function, if a non-NULL
           `env' argument was passed to `start', the function calls
           `kdu_thread_env::cs_terminate' before it returns (actually before
           a final call to `kdu_codestream::flush').  This means that you do
           not yourself need to call `kdu_thread_env::cs_terminate' prior to
           `kdu_codestream::destroy', although there is no harm in doing so.
           [//]
           If your objective is to shut down and clean up the object's
           processing machinery as quickly as possible, the `reset' function
           is a better choice, but before that function is called, you must
           be certain that there is no multi-threaded processing going on.
           One way to do so is to destroy any `kdu_thread_env' environment
           that may have been used with `start'.  Alternatively, you can
           invoke `kdu_thread_queue::terminate' on a non-NULL `env_queue'
           that was passed to `start', but then you should remember that it
           will be your responsibility to invoke `kdu_thread_env::cs_terminate'
           if you are calling `reset'.
           [//]
           If you passed a non-NULL `env' argument to `start' and the
           thread environment has already been destroyed, you must use
           `reset' instead of `finish' to clean up this object.  For this
           reason, the object's destructor actually invokes `reset'.
           [//]
           Note that from Kakadu version 7.5, this function does not actually
           deallocate the primary memory resources that were allocated by
           `start' or during calls to `push_stripe'.  Instead, this memory is
           retained so that it can be re-used in a subsequent call to `start'
           if that is appropriate.  To deallocate all memory resources, you
           may use the `reset' function, which is also invoked implicitly
           by the object's destructor.
         [RETURNS]
           True only if the compressed image is complete.  Otherwise,
           insufficent image data was pushed in via `push_stripe', but the
           internal machinery is cleaned up anyway.
         [ARG: num_layer_specs]
           Identifies the number of entries in the `layer_sizes' and/or
           `layer_slopes' arrays, if non-NULL.
         [ARG: layer_sizes]
           If non-NULL, the final cumulative sizes of each code-stream
           quality layer will be written into this array.  At most
           `num_layer_specs' cumulative layer sizes will be written.  If the
           actual number of quality layers whose sizes are known is less
           than `num_layer_specs', the additional entries are set to 0.
         [ARG: layer_slopes]
           Same as `layer_sizes' but used to receive final values of the
           distortion length slope thresholds associated with each layer.
           Again, if `num_layer_specs' exceeds the number of layers for
           which slope information is available, the additional entries
           will be set to 0.
         [ARG: alt_env]
           If this argument is non-NULL AND a non-NULL `env' argument was
           also passed to `start', this function uses the `alt_env' reference
           to perform multi-threaded cleanup and final flushing operations.
           This allows the `finish' function to be called by a different
           thread to the one that invoked `start', so long as both threads
           belong to the same thread group (at least one must be a worker
           thread created by `kdu_thread_entity::add_thread', if the threads
           are different).  Of course, it is imperative that whatever thread
           calls this function, `alt_env' is its own unique `kdu_thread_env'
           reference, as opposed to one belonging to some other thread.
           [//]
           If the `start' function was called with a NULL `env' argument,
           the processing machinery is single-threaded, so any `alt_env'
           argument is ignored and it is assumed that whatever thread calls
           this function has been serialized by the application to avoid
           conflict with the activities of any other threads in the system.
      */
    KDU_AUX_EXPORT void reset(bool free_memory=true);
      /* [SYNOPSIS]
           Each call to `start' must be bracketed by a call to either
           `finish' or `reset'.  Like `finish', this function does nothing
           if the object has already been finished or reset.  Calls to this
           function are appropriate if you need to abort processing at some
           point or if a `kdu_thread_env' reference was passed to `start'
           and the multi-threaded environment has since been destroyed.
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
           resources; however, if you wish to retain the memory after some
           premature termination of the compression process, so that it can
           be used again after a subsequent call to `start', this can be
           arranged by passing false for the `free_memory' argument.
      */
    KDU_AUX_EXPORT bool
      get_recommended_stripe_heights(int preferred_min_height,
                                     int absolute_max_height,
                                     int stripe_heights[],
                                     int *max_stripe_heights);
      /* [SYNOPSIS]
           Convenience function, provides recommended stripe heights for the
           most efficient use of the `push_stripe' function, subject to
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
           depending on how much data has already been supplied for each
           image component by previous calls to `push_stripe'.  However,
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
           recommended stripe height for that component.
         [ARG: max_stripe_heights]
           If non-NULL, this argument points to an array with one entry for
           each image component, which receives an upper bound on the stripe
           height which this function will ever recommend for that component.
           Note that the number of image components is the value returned by
           `kdu_codestream::get_num_components' with its optional
           `want_output_comps' argument set to true.
           There is no upper bound on the stripe height you can actually use
           in a call to `push_stripe', only an upper bound on the
           recommendations which this function will produce as it is called
           from time to time.  Thus, if you intend to use this function to
           guide your stripe selection, the `max_stripe_heights' information
           might prove useful in pre-allocating storage for stripe buffers
           provided by your application.
      */
    KDU_AUX_EXPORT bool
      get_next_stripe_heights(int preferred_min_height,
                              int absolute_max_height,
                              int cur_stripe_heights[],
                              int next_stripe_heights[]);
      /* [SYNOPSIS]
           Similar to `get_recommended_stripe_heights', except that this
           function works out the heights that would be recommended for the
           next stripe, assuming that stripes with the heights found in the
           `cur_stripe_heights' are first pushed into the `push' function.
           [//]
           This function is provided to facilitate double-buffered I/O where
           two sets of stripe buffers are maintained, one of which is used
           to by an image reading thread while the other is passed to `push'.
           Before calling `push', the present function can be used to figure
           out the stripe heights that should be used by the file reading
           thread while we are pushing the current stripe.
         [RETURNS]
           Unlike `get_recommended_stripe_heights', this function returns true
           if and only if there will be a next stripe -- i.e., if and only if
           at least one entry of `next_stripe_heights' array gets set to a
           non-zero value.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_byte *stripe_bufs[], const int stripe_heights[],
                  const int *sample_gaps=NULL, const int *row_gaps=NULL,
                  const int *precisions=NULL, int flush_period=0);
      /* [SYNOPSIS]
           Supplies vertical stripes of samples for each image component.  The
           number of entries in each of the arrays here is equal to the
           number of image components, as returned by
           `kdu_codestream::get_num_components' with its optional
           `want_output_comps' argument set to true.  Each stripe spans the
           entire width of its image component, which must be no larger than
           the ratio between the corresponding entries in the `row_gaps' and
           `sample_gaps' arrays.
           [//]
           Each successive call to this function advances the vertical position
           within each image component by the number of lines identified within
           the `stripe_heights' array.  To properly compress the image, you
           must eventually advance all components to the bottom.  At this
           point, the present function returns false (no more lines needed
           in any component) and a subsequent call to `finish' will return
           true.
           [//]
           Note that although components nominally advance from the top to
           the bottom, if `kdu_codestream::change_appearance' was used to
           flip the appearance of the vertical dimension, the supplied data
           actually advances the true underlying image components from the
           bottom up to the top.  This is exactly what one should expect from
           the description of `kdu_codestream::change_appearance' and requires
           no special processing in the implemenation of the present object.
           [//]
           Although considerable flexibility is offered with regard to stripe
           heights, there are a number of constraints.  As a general rule,
           you should attempt to advance the various image components in a
           proportional way, when processing incrementally (as opposed to
           supplying the entire image in a single call to this function).  What
           this means is that the stripe height for each component should,
           ideally, be inversely proportional to its vertical sub-sampling
           factor.  If you do not intend to do this for any reason, the
           following notes should be taken into account:
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
           [>>] Similar to colour transforms, if a Part-2 multi-component
                transform is being used, you must follow the proportional
                processing guidelines at least to the extent that the same
                stripe height must be used for components which are combined
                by the multi-component transform.
           [>>] Regardless of the above constraints, the selection of
                proportional stripe heights improves the reliability of the
                block truncation prediction algorithm associated with calls to
                `kdu_codestream::set_max_bytes'.  To understand the conditions
                under which that function will be called, consult the comments
                appearing with the `min_slope_threshold' argument in the
                `start' function. If prediction is disabled or
                `kdu_codestream::set_min_slope_threshold' was called from
                within `start' (e.g., because you supplied a non-zero
                `min_slope_threshold' argument), there is no need to worry
                about pushing stripe heights which are proportional to the
                corresponding image component heights.
           [//]
           In addition to the constraints and guidelines mentioned above
           regarding the selection of suitable stripe heights, it is worth
           noting that the efficiency (computational and memory efficiency)
           with which image data is compressed depends upon how your
           stripe heights interact with image tiling.  If the image is
           untiled, you are generally best off passing small stripes, unless
           your application naturally provides larger stripe buffers.  If,
           however, the image is tiled, then the implementation is most
           efficient if your stripes happen to be aligned on vertical tile
           boundaries.  To simplify the determination of suitable stripe
           heights (all other things being equal), the present object
           provides a convenient utility, `get_recommended_stripe_heights',
           which you can call at any time.  Alternatively, just push in
           whatever stripes your application produces naturally.
           [//]
           To understand the interpretation of the sample bytes passed to
           this function, consult the comments appearing with the `precisions'
           argument below.  Other forms of the overloaded `push_stripe'
           function are provided to allow for compression of higher precision
           image samples.
           [//]
           Certain internal paths involve heavily optimized data transfer
           routines that may exploit the availability of SIMD instructions.
           Currently, SSSE3 and AVX2 based optimizations exist for the
           following conditions, most of which also have ARM-NEON
           optimizations also:
           [>>] Conversion to all but the 32-bit absolute integer
                representation (high precision reversible processing) from
                buffer organizations with a sample-gap of 1 (i.e., separate
                memory blocks for each component).
           [>>] Conversion to all but the 32-bit absolute integer
                representation (high precision reversible processing) from
                sample interleaved buffers with a sample-gap of 3 (e.g.,
                RGB interleaved).
           [>>] Conversion to all but the 32-bit absolute integer
                representation (high precision reversible processing) from
                sample interleaved buffers with a sample-gap of 4 (e.g.,
                RGBA interleaved).
           [//]
           If you are not exciting one of the above optimization paths, you
           could find that the transfer of imagery from your stripe buffers
           for compression actually becomes the bottleneck in the overall
           processing pipeline, because this part runs in a single thread,
           while everything else can potentially be heavily multi-threaded.
           By and large, the above optimization paths cover almost everything
           that is useful.
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
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
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
           number of lines being supplied for that component in the present
           call.  All entries must be non-negative.  See the extensive
           discussion above, on the various constraints and guidelines which
           may exist regarding stripe heights and their interaction with
           tiling and sub-sampling.
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
           component.  The precision identifies the number of least
           significant bits which are actually used in each sample.
           If this value is less than 8, one or more most significant bits
           of each byte will be ignored.
           [//]
           There is no implied connection between the precision values, P, and
           the bit-depth of each image component, as provided by the
           `Sprecision' attribute managed by the `siz_params' object passed to
           `kdu_codestream::create'.  The original image sample
           bit-depth may be larger or smaller than the value of P, supplied via
           the `precisions' argument.  In any event, the most significant bit
           of the P-bit integer represented by each sample byte is aligned with
           the most significant bit of image sample words which we actually
           compress internally.  Zero padding and discarding of excess least
           significant bits is applied as required.
           [//]
           These conventions, provide the application with tremendous
           flexibility in how it chooses to represent image sample values.
           Suppose, for example, that the original image sample precision for
           some component is only 1 bit, as represented by the `Sprecision'
           attribute managed by the `siz_params' object (this is the
           bit-depth value actually recorded in the code-stream).  If
           the value of P provided by the `precisions' array is set to 1, the
           bi-level image information is embedded in the least significant bit
           of each byte supplied to this function.  On the other hand, if the
           value of P is 8, the bi-level image information is embedded
           in the most significant bit of each byte.
           [//]
           The sample values supplied to this function are always unsigned,
           regardless of whether or not the `Ssigned' attribute managed
           by `siz_params' identifies an image component as having an
           originally signed representation.  In this, relatively unlikely,
           event, the application is responsible for level adjusting the
           original sample values, by adding 2^{P-1} to each originally signed
           value.
         [ARG: flush_period]
           This argument may be used to control Kakadu's incremental
           code-stream flushing machinery, where applicable.  If 0, no
           incremental flushing is attempted.  Otherwise, the function checks
           to see if at least this number of lines have been pushed in for
           the first image component, since the last successful call to
           `kdu_codestream::flush' (or since the call to `start').  If so,
           an attempt is made to flush the code-stream again, after first
           calling `kdu_codestream::ready_for_flush' to see whether a new
           call to `kdu_codestream::flush' will succeed.  Incremental flushing
           is possible only under a very narrow set of conditions on the
           packet progression order and precinct dimensions, or where there
           are multiple tiles; however, the conditions for successful
           incremental flushing are much more relaxed if the compressed
           data target is a structured cache as opposed to a linear codestream
           (see `kdu_compressed_target::get_capabilities').
           For a detailed discussion of the incremental flushing conditions,
           you should carefully review the comments provided with
           `kdu_codestream::flush' before trying to use this feature.
           [//]
           In a multi-threaded processing environment (i.e., when the `env'
           argument passed to `start' was non-NULL), this argument is ignored
           on all but the first call to this function.  During this first
           call, the argument is used to configure a call to
           `kdu_codestream::auto_flush' so that incremental flushing can
           proceed in the background, at the most appropriate points.
           [//]
           It is possible for you do organize incremental flushing all by
           yourself, passing 0 for the `flush_period' argument and invoking
           `flush' or `auto_flush' yourself, with appropriate parameters.
           [//]
           In any event, it is recommended that you do not attempt to
           change the `flush_period' argument between successive calls to this
           function, even if you think you have some clever way of doing so.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_byte *buffer, const int stripe_heights[],
                  const int *sample_offsets=NULL, const int *sample_gaps=NULL,
                  const int *row_gaps=NULL, const int *precisions=NULL,
                  int flush_period=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `push_stripe' function,
           except in the following respect:
           [>>] The stripe samples for all image components must be located
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
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the lines of each component stripe buffer are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the first form of the `push_stripe' function.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_int16 *stripe_bufs[], const int stripe_heights[],
                  const int *sample_gaps=NULL, const int *row_gaps=NULL,
                  const int *precisions=NULL, const bool *is_signed=NULL,
                  int flush_period=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `push_stripe' function,
           except in the following respects:
           [>>] The stripe samples for each image component are provided with
                a 16-bit representation; as with other forms of the
                `push_stripe' function, the actual number of bits of this
                representation which are used is given by the `precisions'
                argument, but all 16 bits may be used (this is the default).
           [>>] The default representation for each supplied sample value is
                signed, but the application may explicitly identify whether
                or not each component has a signed or unsigned representation.
                Note that there is no required connection between the `Ssigned'
                attribute managed by `siz_params' and the application's
                decision to supply signed or unsigned data to the present
                function.  If the original data for component c was unsigned,
                the application may choose to supply signed sample values here,
                in which case it is responsible for first subtracting
                2^{`precisions'[c]-1} from each sample of image component c.
                If the application supplies unsigned data (setting
                `is_signed'[c] to true), the present function will subtract
                2^{`precisions'[c]-1} from the corresponding sample values.
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: precisions]
           See description of the first form of the `push_stripe' function,
           but note these two changes: the precision for any component may be
           as large as 16 (this is the default, if `precisions' is NULL);
           and the samples all have a nominally signed representation (not the
           unsigned representation assumed by the first form of the function),
           unless otherwise indicated by a non-NULL `is_signed' argument.
         [ARG: is_signed]
           If NULL, the supplied samples for each component, c, are assumed to
           have a signed representation in the range -2^{`precisions'[c]-1} to
           2^{`precisions'[c]-1}-1.  Otherwise, this argument points to
           an array with one element for each component.  If `is_signed'[c]
           is true, the default signed representation is assumed for that
           component; if false, the component samples are assumed to have an
           unsigned representation in the range 0 to 2^{`precisions'[c]}-1.
           What this means is that the function subtracts 2^{`precisions[c]'-1}
           from the samples of any component for which `is_signed'[c] is false.
           It is allowable to have `precisions'[c]=16 even if `is_signed'[c] is
           false, meaning that the input words are treated as though they were
           16-bit unsigned words.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_int16 *buffer, const int stripe_heights[],
                  const int *sample_offsets=NULL, const int *sample_gaps=NULL,
                  const int *row_gaps=NULL, const int *precisions=NULL,
                  const bool *is_signed=NULL, int flush_period=0);
      /* [SYNOPSIS]
           Same as the third form of the overloaded `push_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  Specifically, sample values have a
           16-bit signed (but possibly unsigned, depending on the `is_signed'
           argument) representation, rather than an 8-bit unsigned
           representation.  As with the second form of the function, this
           fourth form is provided primarily to facilitate automatic
           construction of Java language bindings by the "kdu_hyperdoc"
           utility.
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the third form of the `push_stripe' function.
         [ARG: is_signed]
           See description of the third form of the `push_stripe' function.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_int32 *stripe_bufs[], const int stripe_heights[],
                  const int *sample_gaps=NULL, const int *row_gaps=NULL,
                  const int *precisions=NULL, const bool *is_signed=NULL,
                  int flush_period=0);
      /* [SYNOPSIS]
           Same as the third form of the overloaded `push_stripe' function,
           except that stripe samples for each image component are provided
           with a 32-bit representation; as with other forms of the function,
           the actual number of bits of this representation which are used is
           given by the `precisions' argument, but all 32 bits may be used
           (this is the default).
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: precisions]
           See description of the first form of the `push_stripe' function,
           but note these two changes: the precision for any component may be
           as large as 32 (this is the default, if `precisions' is NULL);
           and the samples all have a nominally signed representation (not the
           unsigned representation assumed by the first form of the function),
           unless otherwise indicated by a non-NULL `is_signed' argument.
         [ARG: is_signed]
           If NULL, the supplied samples for each component, c, are assumed to
           have a signed representation in the range -2^{`precisions'[c]-1} to
           2^{`precisions'[c]-1}-1.  Otherwise, this argument points to
           an array with one element for each component.  If `is_signed'[c]
           is true, the default signed representation is assumed for that
           component; if false, the component samples are assumed to have an
           unsigned representation in the range 0 to 2^{`precisions'[c]}-1.
           What this means is that the function subtracts 2^{`precisions[c]'-1}
           from the samples of any component for which `is_signed'[c] is false.
           It is allowable to have `precisions'[c]=32 even if `is_signed'[c] is
           false, meaning that the input words are treated as though they were
           32-bit unsigned words.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(kdu_int32 *buffer, const int stripe_heights[],
                  const int *sample_offsets=NULL, const int *sample_gaps=NULL,
                  const int *row_gaps=NULL, const int *precisions=NULL,
                  const bool *is_signed=NULL, int flush_period=0);
      /* [SYNOPSIS]
           Same as the fifth form of the overloaded `push_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  As with the second and fourth forms of the
           function, this sixth form is provided primarily to facilitate
           automatic construction of Java language bindings by the
           "kdu_hyperdoc" utility.
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the fifth form of the `push_stripe' function.
         [ARG: is_signed]
           See description of the fifth form of the `push_stripe' function.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(float *stripe_bufs[], const int stripe_heights[],
                  const int *sample_gaps=NULL, const int *row_gaps=NULL,
                  const int *precisions=NULL, const bool *is_signed=NULL,
                  int flush_period=0);
      /* [SYNOPSIS]
           Same as the third form of the overloaded `push_stripe' function,
           except that stripe samples for each image component are provided
           with a floating point representation.  In this case, the
           interpretation of the `precisions' member is slightly different,
           as explained below.
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
         [ARG: precisions]
           If NULL, all component samples are deemed to have a nominal range
           of 1.0; that is, signed values lie in the range -0.5 to +0.5,
           while unsigned values lie in the range 0.0 to 1.0; equivalently,
           the precision is taken to be P=0.  Otherwise, the argument points
           to an array with one precision value for each component.  The
           precision value, P, identifies the nominal range of the input
           samples, such that signed values range from -2^{P-1} to +2^{P-1},
           while unsigned values range from 0 to 2^P.
           [//]
           The value of P, provided by the `precisions' argument may be
           the same, larger or smaller than the actual bit-depth, B, of
           the corresponding image component, as provided by the
           `Sprecision' attribute (or the `Mprecision' attribute) managed
           by the `siz_params' object passed to `kdu_codestream::create'.  The
           relationship between samples represented at bit-depth B and the
           floating point quantities supplied to this function is that the
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
           You can use this "invalid" portion of the nominal range if you
           like, but values may be clipped during decompression.  To minimize
           the impact of this small "invalid" fraction of the nominal range,
           you might choose to set the image bit-depth, B, to a large value
           when compressing data which you really think of as floating
           point data.  This will also help to minimize the effective
           quantization error introduced by reversible compression, if
           used, although irreversible compression makes a lot more sense
           if you are working with floating point samples.
           [//]
           It is worth noting that this function, unlike its predecessors,
           allows P to take both negative and positive values.  For
           implementation reasons, though, we restrict precisions to take
           values in the range -64 to +64.  Also unlike its predecessors,
           this function does not limit the range of input samples.  To a
           certain extent, therefore, you can get away with exceeding the
           nominal dynamic range, without causing overflow.  This extent is
           determined by the number of guard bits.  However, overflow problems
           might be encountered in some decompressor implementations if you
           take too many liberties here.
         [ARG: is_signed]
           If NULL, the supplied samples for each component, c, are assumed to
           have a signed representation, with a nominal range from
           -2^{`precisions'[c]-1} to +2^{`precisions'[c]-1}.  Otherwise, this
           argument points to an array with one element for each component.  If
           `is_signed'[c] is true, the default signed representation is assumed
           for that component; if false, the component samples are assumed to
           have an unsigned representation, with a nominal range from 0.0 to
           2^{`precisions'[c]}.  What this means is that the function subtracts
           2^{`precisions[c]'-1} from the samples of any component for which
           `is_signed'[c] is false -- if `precisions' is NULL, 0.5 is
           subtracted.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
    KDU_AUX_EXPORT bool
      push_stripe(float *buffer, const int stripe_heights[],
                  const int *sample_offsets=NULL, const int *sample_gaps=NULL,
                  const int *row_gaps=NULL, const int *precisions=NULL,
                  const bool *is_signed=NULL, int flush_period=0);
      /* [SYNOPSIS]
           Same as the seventh form of the overloaded `push_stripe' function,
           except that all component buffers are found within the single
           supplied `buffer'.  As with the second, fourth and sixth forms
           of the function, this eighth form is provided primarily to
           facilitate automatic construction of Java language bindings by the
           "kdu_hyperdoc" utility.
         [RETURNS]
           True until all samples for all image components have been pushed in,
           at which point the function returns false.
         [ARG: stripe_heights]
           See description of the first form of the `push_stripe' function.
         [ARG: sample_offsets]
           Array with one entry for each image component, identifying the
           position of the first sample of that component within the `buffer'
           array.  If this argument is NULL, the implied sample offsets are
           `sample_offsets'[c] = c -- i.e., samples are tightly interleaved.
           In this case, the interpretation of a NULL `sample_gaps' array is
           modified to match the tight interleaving assumption.
         [ARG: sample_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the sample gaps for all image components are taken to be
           1, which means that the organization of `buffer' must be
           either line- or component- interleaved.  The only exception to this
           is if `sample_offsets' is also NULL, in which case, the sample
           gaps all default to the number of image components, corresponding
           to a sample-interleaved organization.
         [ARG: row_gaps]
           See description of the first form of the `push_stripe' function.
           If NULL, the lines of each component stripe buffers are assumed to
           be contiguous, meaning that the organization of `buffer' must
           be either component- or sample-interleaved.
         [ARG: precisions]
           See description of the seventh form of the `push_stripe' function.
         [ARG: is_signed]
           See description of the seventh form of the `push_stripe' function.
         [ARG: flush_period]
           See description of the first form of the `push_stripe' function.
      */
  private: // Helper functions
    kd_supp_local::kdsc_tile *get_new_tile();
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
    void note_inactive_tile(kd_supp_local::kdsc_tile *tile,
                            kdu_thread_env *caller, bool all_pushed);
      /* This function closes the `kdu_tile' interface inside `tile', then
         moves the `tile' object to the tail of the `inactive_tiles' list.
         The function does not destroy the `tile->engine' object, since this
         may deallocate a large amount of memory.  We prefer to do this
         immediately before a new tile needs to be created -- inside
         `get_new_tile'.  If `all_pushed' is true, all data has been pushed
         into all tiles in the codestream, so the `kdu_tile::close' function
         is called in a manner that causes immediate closure to occur (requires
         locking a mutex inside the core codestream machinery, but there is
         no longer any harm in having the caller blocked if other background
         operations are proceeding).  Otherwise, if `env' is non-NULL, the
         tile closure operation is scheduled to occur in the background. */
    kd_supp_local::kdsc_queue *get_new_queue();
      /* Uses the free list, if possible; returns with the `thread_queue'
         member instantiated, but without any tiles to use it yet. */
    void release_queue(kd_supp_local::kdsc_queue *queue, bool all_pushed,
                       kdu_thread_env *caller);
      /* Joins upon the queue, then moves all of its tiles to the
         `inactive_tiles' list via `note_inactive_tile'.  The
         meaning of the `all_pushed' argument is the same as it is in
         `note_inactive_tile'.  The reason for providing a `caller' argument
         here is that this function might be called from within `finish',
         by a thread that is different from that which invoked `start'. */
    void cleanup_queue(kd_supp_local::kdsc_queue *queue);
      /* Executed in place of `release_queue' if `reset' is called in place
         of `finish'.  This function does close any open tiles, but otherwise
         just recycles resources. */
    bool push_common(int flush_period); // Common part of `push_stripe' funcs
    void configure_auto_flush(int flush_period);
  private: // Data
    kdu_codestream codestream;
    kdu_push_pull_params pp_params; // Copy of params passed to `start'
    int flush_layer_specs; // Num specs provided in `flush' calls
    kdu_long *flush_sizes; // Layer sizes provided in `flush' calls
    kdu_uint16 *flush_slopes; // Layer slopes array provided in `flush' calls
    int flush_flags; // Augments the interpretation the `flush_...' arrays
    double size_tolerance; // Value supplied by `start'
    bool trim_to_rate; // Value supplied by `start'
    bool record_layer_info_in_comment; // Value supplied by `start'
    bool force_precise;
    bool want_fastest;
    bool all_done; // When all samples have been processed
    int num_components;
    kd_supp_local::kdsc_component_state *comp_states;
    kdu_coords left_tile_idx; // Index of left-most tile in current row
    kdu_coords num_tiles; // Tiles wide and remaining tiles vertically.
    kd_supp_local::kdsc_tile *partial_tiles; // Need to push more sample
                                             // values into these
    kd_supp_local::kdsc_tile *inactive_tiles; // List of tiles that are no
    kd_supp_local::kdsc_tile *last_inactive_tile; // no longer in use, but
                                  // whose `engine' is yet to be destroyed.
    kd_supp_local::kdsc_tile *free_tiles; // These have been recycled for
                                          // re-use
    int lines_since_flush; // Lines of component 0 pushed in since last flush
    bool flush_on_tile_boundary; // Want flush at start of next tile row
  private: // Members used for multi-threading
    bool auto_flush_started; // For multi-threaded processing
    kdu_thread_env *env; // NULL, if multi-threaded environment not used
    kdu_thread_queue local_env_queue; // Used only with `env'
    int env_dbuf_height; // Used only with `env'
    kd_supp_local::kdsc_queue *active_queue; // There is only one active
                                             // tile queue
    kd_supp_local::kdsc_queue *finished_queues; // There may be zero or
                                                // more of these
    kd_supp_local::kdsc_queue *last_finished_queue; // Tail of above list
    kd_supp_local::kdsc_queue *free_queues; // List of recycled tile queues
    kdu_long next_queue_idx; // Sequence index for the next tile queue
    int num_finished_tiles; // Num tiles within `finished_queues' list
    int max_finished_tiles;
    kdu_dims tiles_to_open; // Range of tiles not yet scheduled for opening
    kdu_coords last_tile_accessed; // Index of the latest (in raster order)
        // tile used to fill out a `kdu_tile' interface; if no tiles have yet
        // been accessed, this member actually references the top-left tile
        // which causes no harm in practice because we only use this member to
        // determine which tiles may have been scheduled for opening but not
        // actually accessed by the time `finish' is called.
};

} // namespace kdu_supp

#endif // KDU_STRIPE_COMPRESSOR_H
