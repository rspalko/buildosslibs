/*****************************************************************************/
// File: encoder.cpp [scope = CORESYS/CODING]
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
   Implements the functionality offered by the "kdu_encoder" object defined
in "kdu_sample_processing.h".  Includes quantization, subband sample buffering
and geometric appearance transformations.
******************************************************************************/

#include <math.h>
#include <string.h>
#include <assert.h>
#include "kdu_arch.h"
#include "kdu_threads.h"
#include "kdu_messaging.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_block_coding.h"
#include "kdu_kernels.h"
#include "kdu_roi_processing.h"
#include "encoding_local.h"

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
*/
#ifdef KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
   kdu_error _name("E(encoder.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(encoder.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) kdu_error _name("Kakadu Core Error:\n");
#  define KDU_WARNING(_name,_id) kdu_warning _name("Kakadu Core Warning:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers

using namespace kd_core_local;

#if defined KDU_X86_INTRINSICS
#  include "x86_encoder_local.h" // x86-based SIMD accelerators
#elif defined KDU_NEON_INTRINSICS
#  include "neon_encoder_local.h" // ARM-NEON based SIMD accelerators
#endif

#ifdef KDU_SIMD_OPTIMIZATIONS
using namespace kd_core_simd; // So macro selectors can access SIMD externals
#endif


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    get_first_unscheduled_job_in_stripe                    */
/*****************************************************************************/

static inline int
  get_first_unscheduled_job(kdu_int32 sched, int which, int num_stripes,
                            int jobs_per_stripe, int jobs_per_quantum)
  /* The `sched' argument here is a snapshot of `kd_encoder_sync_state::sched'
     from which the function deduces the index of the first job within the
     stripe identified by `which' that has not yet been scheduled.  If all
     jobs in the stripe have been scheduled, the function returns
     'jobs_per_stripe', which corresponds to `kd_encoder::num_jobs_per_stripe'.
     The total number of job processing stripes available in the
     `kd_encoder' object is supplied as `num_stripes', while the number of
     jobs in all but the last quantum of a stripe is given by
     `jobs_per_quantum'; these correspond to the values of
     `kd_encoder::num_stripes' and `kd_decoder::jobs_per_quantum',
     respectively.  This function is deliberately not made a
     member of the `kd_encoder' object, because there are subtle conditions
     under which it may need to be called after the `kd_encoder' object
     has been cleaned up. */
{
  int P_rel = ((sched & KD_ENC_SYNC_SCHED_P_MASK) >> KD_ENC_SYNC_SCHED_P_POS);
  int R_rel = P_rel >> KD_ENC_QUANTUM_BITS;
  int active = (sched >> KD_ENC_SYNC_SCHED_A_POS) & 3;
  int status = (sched >> (KD_ENC_SYNC_SCHED_U_POS + 2*which)) & 3;
  if (status < 2)
    return 0; // Stripe not available for encoding
  int w_rel = which - active;
  R_rel -= (w_rel < 0)?(num_stripes+w_rel):w_rel;
  if (R_rel < 0)
    return 0; // Nothing ready to be scheduled
  int quanta = (1 << KD_ENC_QUANTUM_BITS);
  if (R_rel == 0)
    quanta = P_rel&(quanta-1); // `KD_ENC_QUANTUM_BITS' LSB's of P_rel
  if (status == 2)
    { // Partially schedulable stripe
      int max_quanta = ((sched & KD_ENC_SYNC_SCHED_Q_MASK) >>
                        KD_ENC_SYNC_SCHED_Q_POS);
      quanta = (max_quanta < quanta)?max_quanta:quanta;
    }
  int J = quanta * jobs_per_quantum;
  return (J >= jobs_per_stripe)?jobs_per_stripe:J;
}


/* ========================================================================= */
/*                                kdu_encoder                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdu_encoder::kdu_encoder                         */
/*****************************************************************************/

kdu_encoder::kdu_encoder(kdu_subband band, kdu_sample_allocator *allocator,
                         bool use_shorts, float normalization,
                         kdu_roi_node *roi, kdu_thread_env *env,
                         kdu_thread_queue *env_queue, int flags)
  // In the future, we may create separate, optimized objects for each kernel.
{
  float visibility_floor=0.0f, masking_exponent=0.5f, visual_scale=1.0f;
  if (band.get_masking_params(visibility_floor,masking_exponent,visual_scale))
    { 
      assert((masking_exponent > 0.49f) && (masking_exponent < 0.51f) &&
             (visibility_floor > 0.0f) && (visual_scale > 0.0f));
      kd_mask_encoder *enc = new kd_mask_encoder;
      state = enc;
      enc->init(band,allocator,use_shorts,normalization,roi,env,env_queue,
                flags,visibility_floor,visual_scale);
    }
  else
    { 
      kd_encoder *enc = new kd_encoder;
      state = enc;
      enc->init(band,allocator,use_shorts,normalization,roi,env,env_queue,
                flags);
    }
}


/* ========================================================================= */
/*                             kd_encoder_job                                */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC               kd_encoder_job::encode_blocks                        */
/*****************************************************************************/

void
  kd_encoder_job::encode_blocks(kd_encoder_job *job, kdu_thread_env *env)
{
  float block_cell_weights[256];
  float *cells = job->cell_ptr; // Non-NULL only if visual masking models used
  int src_cells_across=0; // Amount to increment `cells' between code-blocks
  
  bool using_shorts = job->using_shorts;
  bool reversible = job->reversible;
  int K_max = job->K_max;
  int K_max_prime = job->K_max_prime;
  float delta = job->delta;
  kdu_block_encoder *block_encoder = job->block_encoder;
  int offset = job->grp_offset; // Horizontal offset into line buffers
  int blocks_remaining = job->grp_blocks;
  kdu_coords idx = job->first_block_idx; // Index of first block to process
  job->first_block_idx.y += job->num_stripes; // For the next time we come here

  // Scan through the blocks to process
  kdu_coords xfer_size;
  kdu_block *block;
  kdu_uint16 estimated_slope_threshold =
    job->band.get_conservative_slope_threshold();
  for (bool scan_start=true; blocks_remaining > 0; blocks_remaining--,
       idx.x++, offset+=xfer_size.x, cells+=src_cells_across,
       scan_start=false)
    { 
      // Open the block.
      block = job->band.open_block(idx,NULL,env,blocks_remaining,scan_start);
      int num_stripes = (block->size.y+3) >> 2;
      xfer_size = block->size;
      assert((xfer_size.x == block->region.size.x) &&
             (xfer_size.y == block->region.size.y) &&
             (0 == block->region.pos.x) && (0 == block->region.pos.y));
      if (block->transpose)
        xfer_size.transpose();

      int block_first_cell_rows=0, block_first_cell_cols=0;
      if (cells != NULL)
        { // Generate cell weights in `block_cell_weights' array
          src_cells_across = (xfer_size.x + 3)>>2;
          int src_cells_down = (xfer_size.y + 3)>>2;
          memset(block_cell_weights,0,
                 sizeof(float)*(size_t)(src_cells_down*src_cells_across));
          int cell_h_inc, cell_v_inc;
          if (!block->transpose)
            { 
              cell_h_inc = 1;
              cell_v_inc = src_cells_across; // This is dst_cells_across also
              block_first_cell_rows = (block->vflip)?4:(4-((-xfer_size.y)&3));
              block_first_cell_cols = (block->hflip)?4:(4-((-xfer_size.x)&3)); 
            }
          else
            { 
              cell_h_inc = src_cells_down; // This is dst_cells_across
              cell_v_inc = 1;
              block_first_cell_cols = (block->vflip)?4:(4-((-xfer_size.y)&3));
              block_first_cell_rows = (block->hflip)?4:(4-((-xfer_size.x)&3));
            }
          int i, j, src_cell_row_gap=job->cell_row_gap;
          float *cell_spp=cells, *cell_dpp=block_cell_weights;
          if (block->vflip)
            { 
              cell_dpp += cell_v_inc*(src_cells_down-1);
              cell_v_inc = -cell_v_inc;
            }
          if (block->hflip)
            { 
              cell_dpp += cell_h_inc*(src_cells_across-1);
              cell_h_inc = -cell_h_inc;
            }
          float mask_offset = job->mask_offset;
          float mask_scale = job->mask_scale;
          for (i=0; i < src_cells_down; i++,
               cell_spp+=src_cell_row_gap, cell_dpp+=cell_v_inc)
            { 
              float *cell_dp = cell_dpp;
              for (j=0; j < src_cells_across; j++, cell_dp+=cell_h_inc)
                { 
                  float val = cell_spp[j]; // This is the cell activity value
                  *cell_dp = 1.0f / (mask_offset + mask_scale * val * val);
                }
            }
        }
      
      //  Make sure we have enough sample buffer storage.
      int num_samples = (num_stripes<<2) * block->size.x;
      assert(num_samples > 0);
      if (block->max_samples < num_samples)
        block->set_max_samples((num_samples>4096)?num_samples:4096);
      
      /* Now quantize and transfer samples to the block, observing any
         required geometric transformations. */
      int row_gap = block->size.x;
      kdu_int32 *dp, *dpp = block->sample_buffer;
      kdu_int32 or_val = 0; // Logical OR of the sample values in this block
      int m, n, m_start = 0, m_inc=1, n_start=offset, n_inc=1;
      if (block->vflip)
        { m_start += xfer_size.y-1; m_inc = -m_inc; }
      if (block->hflip)
        { n_start += xfer_size.x-1; n_inc = -1; }
#ifdef KDU_SIMD_OPTIMIZATIONS
      if (job->simd_block_quant32 != NULL)
        { 
          or_val =
            job->simd_block_quant32(dpp,job->untyped_lines,offset,xfer_size.x,
                                    row_gap,xfer_size.y,K_max,delta);
        }
      else
#endif // KDU_SIMD_OPTIMIZATIONS
      { // Need sample-by-sample general purpose data transfer/quantization
        // First transfer the sample data
        if (using_shorts)
          { // Working with 16-bit source data.
            kdu_sample16 *sp, **spp=job->lines16+m_start;
            if (reversible)
              { // Source data is 16-bit absolute integers.
                kdu_int32 val;
                kdu_int32 upshift = 31-K_max;
                assert(upshift>=0); // Otherwise should be using 16 bits
                if (!block->transpose)
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp+=row_gap)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp++, sp+=n_inc)
                      { 
                        val = sp->ival;
                        if (val < 0)
                          *dp = ((-val)<<upshift) | KDU_INT32_MIN;
                        else
                          *dp = val<<upshift;
                        or_val |= *dp;
                      }
                else
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp++)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp+=row_gap, sp+=n_inc)
                      {
                        val = sp->ival;
                        if (val < 0)
                          *dp = ((-val)<<upshift) | KDU_INT32_MIN;
                        else
                          *dp = val<<upshift;
                        or_val |= *dp;
                      }
              }
            else
              { // Source data is 16-bit fixed point integers.
                float fscale = 1.0F / (delta * (float)(1<<KDU_FIX_POINT));
                if (K_max <= 31)
                  fscale *= (float)(1<<(31-K_max));
                else
                  fscale /= (float)(1<<(K_max-31));
                kdu_int32 val, scale = (kdu_int32)(fscale+0.5F);
                if (!block->transpose)
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp+=row_gap)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp++, sp+=n_inc)
                      {
                        val = sp->ival; val *= scale;
                        if (val < 0.0F)
                          val = (-val) | KDU_INT32_MIN;
                        *dp = val;
                        or_val |= val;
                      }
                else
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp++)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp+=row_gap, sp+=n_inc)
                      {
                        val = sp->ival; val *= scale;
                        if (val < 0.0F)
                          val = (-val) | KDU_INT32_MIN;
                        *dp = val;
                        or_val |= val;
                      }
              }
          }
        else
          { // Working with 32-bit data types.
            kdu_sample32 *sp, **spp = job->lines32+m_start;
            if (reversible)
              { // Source data is 32-bit absolute integers.
                kdu_int32 val;
                kdu_int32 upshift = 31-K_max;
                if (upshift < 0)
                  { KDU_ERROR(e,1); e <<
                    KDU_TXT("Insufficient implementation "
                    "precision available for true reversible compression!");
                  }
                if (!block->transpose)
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp+=row_gap)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp++, sp+=n_inc)
                      {
                        val = sp->ival;
                        if (val < 0)
                          *dp = ((-val)<<upshift) | KDU_INT32_MIN;
                        else
                          *dp = val<<upshift;
                        or_val |= *dp;
                      }
                else
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp++)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp+=row_gap, sp+=n_inc)
                      {
                        val = sp->ival;
                        if (val < 0)
                          *dp = ((-val)<<upshift) | KDU_INT32_MIN;
                        else
                          *dp = val<<upshift;
                        or_val |= *dp;
                      }
              }
            else
              { // Source data is true floating point values.
                float val;
                float scale = (1.0F / delta);
                if (K_max <= 31)
                  scale *= (float)(1<<(31-K_max));
                else
                  scale /= (float)(1<<(K_max-31));// Can't encode all planes
                if (!block->transpose)
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp+=row_gap)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp++, sp+=n_inc)
                      {
                        val = scale * sp->fval;
                        if (val < 0.0F)
                          *dp = ((kdu_int32)(-val)) | KDU_INT32_MIN;
                        else
                          *dp = (kdu_int32) val;
                        or_val |= *dp;
                      }
                else
                  for (m=xfer_size.y; m--; spp+=m_inc, dpp++)
                    for (sp=spp[0]+n_start, dp=dpp,
                         n=xfer_size.x; n--; dp+=row_gap, sp+=n_inc)
                      {
                        val = scale * sp->fval;
                        if (val < 0.0F)
                          *dp = ((kdu_int32)(-val)) | KDU_INT32_MIN;
                        else
                          *dp = (kdu_int32) val;
                        or_val |= *dp;
                      }
              }
          }
        } // End of sample-by-sample general purpose transfer/quantization

      // Now check to see if any ROI up-shift has been specified.  If so,
      // we need to zero out sufficient LSB's to ensure that the foreground
      // and background regions do not get confused.
      if (K_max_prime > K_max)
        {
          dpp = block->sample_buffer;
          kdu_int32 mask = ((kdu_int32)(-1)) << (31-K_max);
          if ((K_max_prime - K_max) < K_max)
            { KDU_ERROR(e,2); e <<
                KDU_TXT("You have selected too small a value for "
                "the ROI up-shift parameter.  The up-shift should be "
                "at least as large as the largest number of magnitude "
                "bit-planes in any subband; otherwise, the foreground and "
                "background regions might not be properly distinguished by "
                "the decompressor.");
            }
          for (m=block->size.y; m--; dpp+=row_gap)
            for (dp=dpp, n=block->size.x; n--; dp++)
              *dp &= mask;
        }

      // Now transfer any ROI information which may be available.
      bool have_background = false; // If no background, code less bit-planes.
      bool scale_wmse = false; // If true, scale WMSE to account for shifting.
      if ((job->roi8 != NULL) && (K_max_prime != K_max))
        {
          // Adjust `m_start' and `m_inc' for transfers from the `roi8' buffer
          m_start = 0; m_inc=job->roi_row_gap;
          if (block->vflip)
            { m_start += (xfer_size.y-1)*job->roi_row_gap; m_inc = -m_inc; }
          scale_wmse = true; // Background will be shifted down at least
          dpp = block->sample_buffer;
          kdu_byte *sp, *spp=job->roi8+m_start+n_start-job->grp_offset;
          kdu_int32 val;
          kdu_int32 downshift = K_max_prime - K_max;
          assert(downshift >= K_max);
          bool have_foreground = false; // If no foreground, downshift `or_val'
          if (!block->transpose)
            {
              for (m=xfer_size.y; m--; spp+=m_inc, dpp+=row_gap)
                for (sp=spp, dp=dpp, n=xfer_size.x; n--; dp++, sp+=n_inc)
                  if (*sp == 0)
                    { // Adjust background samples down.
                      have_background = true;
                      val = *dp;
                      *dp = (val & KDU_INT32_MIN)
                          | ((val & KDU_INT32_MAX) >> downshift);
                    }
                  else
                    have_foreground = true;
            }
          else
            {
              for (m=xfer_size.y; m--; spp+=m_inc, dpp++)
                for (sp=spp, dp=dpp,
                     n=xfer_size.x; n--; dp+=row_gap, sp+=n_inc)
                  if (*sp == 0)
                    { // Adjust background samples down.
                      have_background = true;
                      val = *dp;
                      *dp = (val & KDU_INT32_MIN)
                          | ((val & KDU_INT32_MAX) >> downshift);
                    }
                  else
                    have_foreground = true;
            }
          if (!have_foreground)
            or_val = (or_val & KDU_INT32_MAX) >> downshift;
        }
      else if (job->roi8 != NULL)
        { 
          m_inc=job->roi_row_gap;
          //kdu_byte *sp, *spp=job->roi8+m_start+n_start-job->grp_offset;
          kdu_byte *sp, *spp=job->roi8+offset-job->grp_offset;
          for (m=xfer_size.y; m > 0; m--, spp+=m_inc)
            for (sp=spp, n=xfer_size.x; n--; sp++)
              if (*sp != 0)
                { // Treat whole block as foreground if it intersects with ROI
                  scale_wmse = true; m=0; break;
                }
        }
      else
        scale_wmse = true; // Everything belongs to foreground

      // Finally, we can encode the block.
      int K = (have_background)?K_max_prime:K_max;
      if (K > 30)
        {
          if (reversible && (K_max_prime > K_max) &&
              !block->insufficient_precision_detected)
            {
              block->insufficient_precision_detected = true;
              KDU_WARNING(w,0); w <<
                KDU_TXT("The ROI shift (`Rshift' attribute) which you "
                "are using is too large to ensure truly lossless recovery of "
                "both the foreground and the background regions, at least by "
                "Kakadu -- other compliant implementations may give up much "
                "earlier.  You might like to consider using the `Rweight' "
                "attribute instead of `Rshift' -- a 32x32 code-block size "
                "(not the default) is recommended in this case and `Rweight' "
                "should be set to around 2 to the power of the `Rshift' value "
                "you would have used.");
            }
        }
      K = (K>31)?31:K;
      or_val &= KDU_INT32_MAX;
      if (or_val == 0)
        block->missing_msbs = 31;
      else
        for (block->missing_msbs=0, or_val<<=1; or_val >= 0; or_val<<=1)
          block->missing_msbs++;
      if (block->missing_msbs >= K)
        {
          block->missing_msbs = K;
          block->num_passes = 0;
        }
      else
        {
          K -= block->missing_msbs;
          block->num_passes = 3*K-2;
        }
      double block_msb_wmse =
        (scale_wmse)?(job->msb_wmse*job->roi_weight):job->msb_wmse;
      if (cells == NULL)
        block_encoder->encode(block,reversible,block_msb_wmse,
                              estimated_slope_threshold);
      else
        block_encoder->cellular_encode(block,reversible,block_msb_wmse,
                                       block_cell_weights,
                                       block_first_cell_cols,
                                       block_first_cell_rows,
                                       estimated_slope_threshold);
      job->band.close_block(block,env);
    }

  if (env != NULL)
    { 
      kdu_int32 old_count = job->pending_stripe_jobs->exchange_add(-1);
      assert(old_count > 0);
      if (old_count == 1)
        job->owner->stripe_encoded(job->which_stripe,env);
    }
}


/* ========================================================================= */
/*                               kd_encoder                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_encoder::stripe_encoded                         */
/*****************************************************************************/

bool
  kd_encoder::stripe_encoded(int which, kdu_thread_env *env)
{
  kdu_int32 new_sched=0, old_sched=0;
  if (num_stripes == 1)
    { 
      kdu_int32 delta_sched = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + KD_ENC_SYNC_SCHED_S0_BIT // S++
        - (3 << KD_ENC_SYNC_SCHED_U_POS);  // Sets U0 = 0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        new_sched = old_sched + delta_sched;
        if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) & KD_ENC_SYNC_SCHED_P_MASK)
          new_sched -= (KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
        new_sched &= ~(KD_ENC_SYNC_SCHED_W_BIT);
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & (3*KD_ENC_SYNC_SCHED_U0_BIT)) ==
             (3*KD_ENC_SYNC_SCHED_U0_BIT));
      assert(((new_sched >> KD_ENC_SYNC_SCHED_R_POS) -
              (old_sched >> KD_ENC_SYNC_SCHED_R_POS)) == 1);
        // Above statement checks for underflow in the P-field
    }
  else if (num_stripes == 2)
    { 
      kdu_int32 A_test = which << KD_ENC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three, A_inc;
      if (which == 0)
        { 
          U0_one      = KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one   =  4*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc =       KD_ENC_SYNC_SCHED_A0_BIT;
        }
      else if (which == 1)
        { 
          U1_one      = KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one   =  4*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc =      -KD_ENC_SYNC_SCHED_A0_BIT;
        }
      else
        abort();
      kdu_int32 delta_sched_1 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + KD_ENC_SYNC_SCHED_S0_BIT // S++
        - U0_three // Sets U0 = 0
        + A_inc; // Increments A (modulo 2)
      kdu_int32 delta_sched_2 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_ENC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one; // Sets U0=U1=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_ENC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance 2 stripes
                new_sched = old_sched + delta_sched_2;
                if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                    KD_ENC_SYNC_SCHED_P_MASK)
                  new_sched-=2*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
              }
            else
              { // Advance by 1 stripe only
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                    KD_ENC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
              }
            new_sched &= ~KD_ENC_SYNC_SCHED_W_BIT;
            assert(((new_sched >> KD_ENC_SYNC_SCHED_R_POS) -
                    (old_sched >> KD_ENC_SYNC_SCHED_R_POS)) == 1);
              // Above statement checks for underflow in the P-field
          }
        else
          new_sched = old_sched - 2*U0_one; // Just change U0 from 3 to 1
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }
  else if (num_stripes == 3)
    { 
      kdu_int32 A_test = which << KD_ENC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three, U2_one, U2_three;
      kdu_int32 A_inc_1, A_inc_2;
      switch (which) {
        case 0:
          U0_one      = KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        case 1:
          U2_one      = KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =    -KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        case 2:
          U1_one      = KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three  = 3*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =  -2*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =    -KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        default: abort();
      }
      kdu_int32 delta_sched_1 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + KD_ENC_SYNC_SCHED_S0_BIT // S++
        - U0_three // Sets U0=0
        + A_inc_1; // Increments A (modulo 3)
      kdu_int32 delta_sched_2 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_ENC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one // Sets U0=U1=0
        + A_inc_2; // Increments A by 2 (modulo 3)
      kdu_int32 delta_sched_3 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (3*KD_ENC_SYNC_SCHED_S0_BIT) // S += 3
        - U0_three - U1_one - U2_one; // Sets U0=U1=U2=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_ENC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance by at least 2 stripes
                if ((old_sched & U2_three)==U2_one)
                  { // Can advance 3 stripes
                    new_sched = old_sched + delta_sched_3;
                    if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                        KD_ENC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        3*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
                  }
                else
                  { // Advance by 2 stripes
                    new_sched = old_sched + delta_sched_2;
                    if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                        KD_ENC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        2*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
                  }
              }
            else
              { // Advance by one stripe
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                    KD_ENC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
              }
            new_sched &= ~KD_ENC_SYNC_SCHED_W_BIT;
            assert(((new_sched >> KD_ENC_SYNC_SCHED_R_POS) -
                    (old_sched >> KD_ENC_SYNC_SCHED_R_POS)) == 1);
              // Above statement checks for underflow in the P-field
          }
        else
          new_sched = old_sched - 2*U0_one; // Just change U0 from 3 to 1
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }
  else if (num_stripes == 4)
    { 
      kdu_int32 A_test = which << KD_ENC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three;
      kdu_int32 U2_one, U2_three, U3_one, U3_three;
      kdu_int32 A_inc_1, A_inc_2, A_inc_3;
      switch (which) {
        case 0:
          U0_one =      KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_one =   64*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_three =192*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_3 =   3*KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        case 1:
          U3_one =      KD_ENC_SYNC_SCHED_U0_BIT;
          U3_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_one =   64*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three =192*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        case 2:
          U2_one =      KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_one =   64*KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three =192*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =  -2*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        case 3:
          U1_one =      KD_ENC_SYNC_SCHED_U0_BIT;
          U1_three =  3*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_one =    4*KD_ENC_SYNC_SCHED_U0_BIT;
          U2_three = 12*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_one =   16*KD_ENC_SYNC_SCHED_U0_BIT;
          U3_three = 48*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_one =   64*KD_ENC_SYNC_SCHED_U0_BIT;
          U0_three =192*KD_ENC_SYNC_SCHED_U0_BIT;
          A_inc_1 =  -3*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_2 =  -2*KD_ENC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_ENC_SYNC_SCHED_A0_BIT;
          break;
        default: abort();
      }
      kdu_int32 delta_sched_1 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + KD_ENC_SYNC_SCHED_S0_BIT // S++
        - U0_three // U0=0
        + A_inc_1; // Increments A (modulo 4)
      kdu_int32 delta_sched_2 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_ENC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one // U0=U1=0
        + A_inc_2; // Increments A by 2 (modulo 4)
      kdu_int32 delta_sched_3 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (3*KD_ENC_SYNC_SCHED_S0_BIT) // S += 3
        - U0_three - U1_one - U2_one // U0=U1=U2=0
        + A_inc_3; // Increments A by 3 (modulo 4)
      kdu_int32 delta_sched_4 = KD_ENC_SYNC_SCHED_R_BIT0 // R++
        + (4*KD_ENC_SYNC_SCHED_S0_BIT) // S += 4
        - U0_three-U1_one-U2_one-U3_one; // U0=U1=U2=U3=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_ENC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance by at least 2 stripes
                if ((old_sched & U2_three)==U2_one)
                  { // Can advance by at least 3 stripes
                    if ((old_sched & U3_three)==U3_one)
                      { // Can advance by 4
                        new_sched = old_sched + delta_sched_4;
                        if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                            KD_ENC_SYNC_SCHED_P_MASK)
                          new_sched -=
                            4*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
                      }
                    else
                      { // Advance by 3 
                        new_sched = old_sched + delta_sched_3;
                        if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                            KD_ENC_SYNC_SCHED_P_MASK)
                          new_sched -=
                            3*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
                      }
                  }
                else
                  { // Advance by 2
                    new_sched = old_sched + delta_sched_2;
                    if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                        KD_ENC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        2*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
                  }
              }
            else
              { // Advance by 1 stripe
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_ENC_SYNC_SCHED_P0_BIT) &
                    KD_ENC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
              }
            new_sched &= ~KD_ENC_SYNC_SCHED_W_BIT;
            assert(((new_sched >> KD_ENC_SYNC_SCHED_R_POS) -
                    (old_sched >> KD_ENC_SYNC_SCHED_R_POS)) == 1);
              // Above statement checks for underflow in the P-field
          }
        else
          new_sched = old_sched - 2*U0_one; // Just change U0 from 3 to 1
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }

  if (((old_sched ^ new_sched) & KD_ENC_SYNC_SCHED_S_MASK) == 0)
    { // S has not changed, so we have not incremented the R field.  We should
      // return immediately.  We do not call `band.block_row_generated' here
      // because the rows are being generated out of order.  When S advances,
      // we will call `band.block_row_generated' once for each change in S.
      return false; // S has not changed
    }
  
  assert(new_sched & KD_ENC_SYNC_SCHED_R_MASK);

  if (old_sched & KD_ENC_SYNC_SCHED_W_BIT)
    { // `pull' has requested a wakeup
      assert((old_sched & KD_ENC_SYNC_SCHED_S_MASK) == 0);
      env->signal_condition(sync_state->wakeup); // Does nothing if NULL
    }
  
  kdu_int32 old_S, new_S, min_S, delta_S;
  old_S = (old_sched & KD_ENC_SYNC_SCHED_S_MASK)>>KD_ENC_SYNC_SCHED_S_POS;
  new_S = (new_sched & KD_ENC_SYNC_SCHED_S_MASK)>>KD_ENC_SYNC_SCHED_S_POS;
  min_S = (new_sched & KD_ENC_SYNC_SCHED_MS_MASK)>>KD_ENC_SYNC_SCHED_MS_POS;
  delta_S = new_S - old_S;
  assert(delta_S > 0);
  if (!(old_sched & KD_ENC_SYNC_SCHED_T_BIT))
    { // Generate calls to `band.block_row_generated' and
      // `propagate_dependencies', as appropriate.
      bool subband_finished =
        ((min_S==0) && !(new_sched & KD_ENC_SYNC_SCHED_U_MASK));
      int s = delta_S;
      int height = nominal_block_height;
      if ((nominal_block_height != first_block_height) &&
          (sync_state->block_row_counter.exchange_add(1) == 0))
        height = first_block_height;
      for (; s > 0; s--, height=nominal_block_height)
        band.block_row_generated(height,(subband_finished && (s==1)),env);
  
      if ((old_S < min_S) && (new_S >= min_S))
        { // Need to call `propagate_dependencies' with a
          // `delta_max_dependencies' argument of -1 -- calls to `push' can
          // never block in the future and this is the first time that this
          // state has occurred.
          if (old_S == 0)
            propagate_dependencies(-1,-1,env); // Was potentially blocking
          else
            propagate_dependencies(0,-1,env); // Was not blocking
        }
      else if ((old_S == 0) && (min_S > 0))
        { // This object previously represented a potential blocking condition
          assert(new_S < min_S);
          propagate_dependencies(-1,0,env);
        }
    }

  // Finally, we need to decrement the R bit, relinquishing our right to
  // continue accessing the object.  However, we have to be very careful to
  // avoid decrementing R below 1 if the `all_done' function needs to be
  // called, or if we are responsible for arranging for it to be called.
  bool need_all_done;
  do { // Enter compare-and-set loop
    old_sched = sync_state->sched.get();
    new_sched = old_sched - KD_ENC_SYNC_SCHED_R_BIT0;
    assert(old_sched & KD_ENC_SYNC_SCHED_R_MASK);
    need_all_done = (((old_sched & KD_ENC_SYNC_SCHED_T_BIT) ||
                      !(old_sched & KD_ENC_SYNC_SCHED_MS_MASK)) &&
                     !(new_sched & (KD_ENC_SYNC_SCHED_R_MASK |
                                    KD_ENC_SYNC_SCHED_U_MASK)));
  } while (!(need_all_done ||
             sync_state->sched.compare_and_set(old_sched,new_sched)));
  if (!need_all_done)
    return false;
  
  if (((old_sched & KD_ENC_SYNC_SCHED_P_MASK) == KD_ENC_SYNC_SCHED_P_MASK) ||
      band.detach_block_notifier(this,env))
    all_done(env);
  // Notes:
  // 1. If the "dependencies closed" condition is found (first conditional),
  //    the `update_dependencies' function will not be called again and if
  //    such a call is in progress still, it will not schedule any jobs or
  //    touch any member functions because there are no jobs that are
  //    schedulable, so it is safe to call `all_done' here.
  // 2. Otherwise, if the `detach_block_notifier' function returned true, there
  //    was never any dependency notifier installed, so `update_dependencies'
  //    never gets called and it is again safe to invoke `all_done'
  //    immediately.
  // 3. Otherwise, `detach_block_notifier' returned false, so a final call
  //    to `update_dependencies' with the otherwise impossible `p_delta'=0
  //    value has been scheduled and that call will invoke `all_done'. This
  //    will happen even if a prior call to `update_dependencies' with the
  //    `closure'=true condition was in progress while we were making the
  //    above tests.  In this case, we need to return immediately.
  return true;
}

/*****************************************************************************/
/*                    kd_encoder::request_termination                        */
/*****************************************************************************/

void
  kd_encoder::request_termination(kdu_thread_entity *caller)
{
  // Start by setting the T bit and making sure that no new jobs get
  // scheduled by asynchronous calls to `update_dependencies'.
  kdu_int32 rel_Rp4 = 4*(KD_ENC_SYNC_SCHED_P0_BIT<<KD_ENC_QUANTUM_BITS);
  kdu_int32 old_sched, new_sched;
  do { // Enter compare-and-set loop
    old_sched = sync_state->sched.get();
    new_sched = old_sched | KD_ENC_SYNC_SCHED_T_BIT;
    if (!(old_sched & rel_Rp4))
      { // Set rel_Rp = 4 so as to ensure that there appear to be enough
        // resourced code-blocks for all jobs in all stripes to have been
        // scheduled, being careful to avoid the situation in which all
        // bits of the rel_P field could become non-zero, since this is
        // interpreted as the "dependencies closed" condition.  Note also
        // that the rel_P field will not be incremented further within
        // `update_dependencies', since the T flag is being set.
        new_sched = (new_sched & ~KD_ENC_SYNC_SCHED_P_MASK) + rel_Rp4;
      }
    new_sched |= (new_sched & (0xAA << KD_ENC_SYNC_SCHED_U_POS)) >> 1;
      // The above line ensures that any PARTIALLY SCHEDULABLE stripe
      // appears to now be FULLY SCHEDULABLE.
  } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
  
  // It is completely safe for us to continue accessing the object's member
  // variables here, because even if another thread invokes `all_done', the
  // object's resources cannot be cleaned up until the lock that is held
  // while this function is in-flight is released.
  
  // Next figure out which, if any, encoding jobs never got scheduled and
  // pretend to have scheduled and performed them already.
  for (int n=0; n < num_stripes; n++)
    { 
      int old_status = (old_sched >> (KD_ENC_SYNC_SCHED_U_POS+2*n)) & 3;
      int new_status = (old_sched >> (KD_ENC_SYNC_SCHED_U_POS+2*n)) & 3;
      if (old_status < 2)
        { // Stripe unused or fully encoded 
          assert(new_status < 2);
          continue;
        }
      if (new_status != 3)
        assert(0); // Because we converted partials to fulls above
      int first_idx =
        get_first_unscheduled_job(old_sched,n,num_stripes,
                                  jobs_per_stripe,jobs_per_quantum);
      int lim_idx =
        get_first_unscheduled_job(new_sched,n,num_stripes,
                                  jobs_per_stripe,jobs_per_quantum);
      int extra_jobs = lim_idx - first_idx;
      if (extra_jobs > 0)
        { 
          kdu_interlocked_int32 *cnt=(jobs[n])[0]->pending_stripe_jobs;
          int old_jobs = cnt->exchange_add(-extra_jobs);
          assert(old_jobs >= extra_jobs);
          if (old_jobs == extra_jobs)
            {               
              if (stripe_encoded(n,(kdu_thread_env *)caller))
                return; // Above func called `all_done' or arranged for it to
                        // be called.
            }
        }
    }
  
  // Finally, determine whether or not there are any jobs still in flight
  new_sched = sync_state->sched.get(); // Above calls to `stripe_encoded'
                                       // may have changed this value.
  if (!(new_sched & KD_ENC_SYNC_SCHED_INFLIGHT_MASK))
    { // No jobs are in flight, so it is not possible that any job thread
      // is accessing the object.
      if (((new_sched&KD_ENC_SYNC_SCHED_P_MASK) == KD_ENC_SYNC_SCHED_P_MASK) ||
          (!band.exists()) ||
          band.detach_block_notifier(this,(kdu_thread_env *)caller))
        all_done(caller);
      // Notes:
      // 1. If the "dependencies closed" condition is found (1st conditional),
      //    the `update_dependencies' function will not be called again and
      //    if such a call is in progress still, it will not schedule any jobs
      //    or touch any member functions because there are no no jobs in
      //    flight and the T bit is now set.  In this case, it is certainly
      //    safe to call `all_done'.
      // 2. Otherwise, if the `detach_block_notifier' function returned true,
      //    there was never any dependency notifier installed, so
      //    `update_dependencies' never gets called and it is again safe to
      //    invoke `all_done' immediately.
      // 3. Otherwise, `detach_block_notifier' returned false, so a final call
      //    to `update_dependencies' with the otherwise impossible `p_delta'=0
      //    value has been scheduled and that call will invoke `all_done'. This
      //    will happen even if a prior call to `update_dependencies' with the
      //    `closure'=true condition was in progress while we were making the
      //    above tests.  In this case, we need to return immediately.
    }
}

/*****************************************************************************/
/*                     kd_encoder::update_dependencies                       */
/*****************************************************************************/

bool
  kd_encoder::update_dependencies(kdu_int32 p_delta, kdu_int32 closure,
                                  kdu_thread_entity *caller)
{
  if (p_delta == 0)
    { 
      if (closure != 0)
        { // Special call to close out a previously pending detaching of this
          // queue from the block notification machinery.
          if (!(sync_state->sched.get() & KD_ENC_SYNC_SCHED_T_BIT))
            { assert(0); abort(); }  
             // This call must have been induced via an attempt to prematurely
             // invoke `kdu_subband::detach_block_notifier', which can only
             // happen if the `T' bit is set as a result of a prior call to
             // `request_termination'.  If this test fails, it is not exactly
             // a disaster, but it means that we did not actually receive the
             // `closure'=1 value when the final update notification was
             // received, during normal operation.  This suggests either that
             // the background resourcing machinery has produced erroneous
             // notification messages (not a disaster, but inefficient) or
             // that we have not interpreted them correctly and have gone
             // ahead and decoded blocks that might not have been pre-parsed
             // (also not a disaster, but inefficient).  To trace the
             // origin of an assert failure here more carefully, insert a
             // test for the condition (sync_state->dependencies_closed ||
             // (sync_state->sched & KD_ENC_SYNC_SCHED_T_BIT)) right before
             // the conditional call to `all_done' at the end of
             // the `stripe_encoded' function.
          assert(!(sync_state->sched.get() & KD_ENC_SYNC_SCHED_U_MASK));
          all_done(caller);
        }
    }
  else
    { 
      // Take local copies of the member variables that we will need to
      // pass to `schedule_new_jobs', since if `closure' is true, we will be
      // setting the the rel_P field within `sync_state->sched' to the
      // special value `KD_ENC_SYNC_SCHED_P_MASK', after which it is possible
      // that the present object gets cleaned up (but only if it turns out
      // that there are no jobs to schedule, so that the call to
      // `schedule_new_jobs' will not touch any member variables whatsoever).
      int local_num_stripes = this->num_stripes;
      int local_jobs_per_stripe = this->jobs_per_stripe;
      int local_jobs_per_quantum = this->jobs_per_quantum;
      
      assert(p_delta > 0);
      p_delta <<= KD_ENC_SYNC_SCHED_P_POS;
      kdu_int32 old_sched, new_sched;
      kdu_int32 closure_mask = (closure)?KD_ENC_SYNC_SCHED_P_MASK:0;
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        new_sched = (old_sched + p_delta) | closure_mask;
        if (old_sched & KD_ENC_SYNC_SCHED_T_BIT)
          return true; // Do nothing here; termination is in progress
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & KD_ENC_SYNC_SCHED_P_MASK) !=
             KD_ENC_SYNC_SCHED_P_MASK); // Else `closure' happened already!!
      assert(!((new_sched ^ old_sched) & ~(KD_ENC_SYNC_SCHED_P_MASK)));
        // Otherwise wrap-around has occurred within the `rel_P' field,
        // which suggests that `kdu_subband::advance_block_rows_needed' has
        // been called in such a way as to allow the background resourcing
        // machinery to get too far ahead of the first active stripe.
      
      // Finally, schedule any jobs that could not be scheduled based on
      // `old_sched', but are now to be scheduled based on `new_sched' -- of
      // course the actual scheduling may happen out of order, but this is of
      // no concern.  Each value of the `sync_state->sched' interlocked
      // variable has an implied set of scheduled jobs associated with it,
      // regardless of any of the present object's dynamic state variables.
      schedule_new_jobs(old_sched,new_sched,caller,
                        local_num_stripes,local_jobs_per_stripe,
                        local_jobs_per_quantum);
    }
  return true;
}

/*****************************************************************************/
/*                      kd_encoder::schedule_new_jobs                        */
/*****************************************************************************/

void
  kd_encoder::schedule_new_jobs(kdu_int32 old_sched, kdu_int32 new_sched,
                                kdu_thread_entity *caller,
                                int local_num_stripes,
                                int local_jobs_per_stripe,
                                int local_quantum)
{
  // Note: It is important that this function is non-virtual and does not
  // touch any member variables of the `kd_encoder' object unless it is
  // actually scheduling jobs.  This is because the function may be called
  // from `update_dependencies', after setting up a condition that enables
  // another thread to asynchronously invoke `all_done' if there are no
  // in-flight jobs (schedulable jobs are in-flight).  With this in mind,
  // the function takes, as arguments, the stripe and job dimensioning
  // parameters that are required to deduce whether or not jobs need to
  // be scheduled, rather than obtaining threse parameters from member
  // variables.

  // Begin by laying out the jobs that we can schedule on the stack so
  // that we can determine ahead of time whether a call to `schedule_jobs'
  // can be informed that it is scheduling the last job for the queue.
  int num_batches=0;
  kdu_thread_job **batch_jobs[4]={NULL,NULL,NULL,NULL};
  int batch_num_jobs[4]={0,0,0,0};
  //bool more_scheduling_required =
  //  ((new_sched & KD_ENC_SYNC_SCHED_MS_MASK) != 0);
  int n, s=(new_sched >> KD_ENC_SYNC_SCHED_A_POS) & 3; // First active stripe
  for (n=0; n < local_num_stripes; n++)
    { 
      kdu_int32 new_status = (new_sched >> (KD_ENC_SYNC_SCHED_U_POS+2*s)) & 3;
      if (new_status == 0) 
        break; // No more active stripes
      if (new_status >= 2)
        { // Otherwise stripe has been fully encoded already
          int j_lim =
            get_first_unscheduled_job(new_sched,s,local_num_stripes,
                                      local_jobs_per_stripe,local_quantum);
          int j_start =
            get_first_unscheduled_job(old_sched,s,local_num_stripes,
                                      local_jobs_per_stripe,local_quantum);
          if (j_lim > j_start)
            { // There are jobs to schedule, so it is OK to access the
              // object's member variables.
              batch_jobs[num_batches] = (kdu_thread_job **)(jobs[s]+j_start);
              batch_num_jobs[num_batches] = j_lim-j_start;
              num_batches++;
            }
          //if (j_lim < local_jobs_per_stripe)
          //  more_scheduling_required = true;
        }
      if ((++s) == local_num_stripes)
        s = 0;
    }
  for (n=0; n < num_batches; n++)
    { 
      /* We used to evaluate the `all_scheduled' condition here and pass it
       along, as shown below.  However, this turns out to be dangerous,
       since there is a slim chance that an earlier scheduling thread
       got held up within the `schedule_jobs' calls.   It is not required
       to determine the `all_scheduled' condition, but there may be some
       very minor inefficiencies introduced by not recognizing the
       all-scheduled condition until the `all_done' call is finally
       issued.
       bool all_scheduled = ((n+1)==num_batches)&&!more_scheduling_required;
       schedule_jobs(batch_jobs[n],batch_num_jobs[n],caller,all_scheduled);
      */
      schedule_jobs(batch_jobs[n],batch_num_jobs[n],caller);
    }
}

/*****************************************************************************/
/*                              kd_encoder::push                             */
/*****************************************************************************/

void
  kd_encoder::push(kdu_line_buf &line, kdu_thread_env *env)
{
  if (line.get_width() == 0)
    return;
  if (!initialized)
    start(env);
  assert(subband_rows > 0);
  assert((env == NULL) ||
         !(sync_state->sched.get() & KD_ENC_SYNC_SCHED_T_BIT));
     // `push' calls should not arrive after `request_termination'!
  if (push_state->active_lines_left == 0)
    { // Need to obtain a new empty `active_push_stripe'
      assert(push_state->subband_lines_left > 0);
      if (env != NULL)
        { // Block encoding is performed asynchronously; we might need to block
          kdu_int32 sched_S;
          while ((sched_S =
                  (sync_state->sched.get() & KD_ENC_SYNC_SCHED_S_MASK)) == 0)
            { // No empty stripes are available at present; we
              // should never have to go through this loop multiple times,
              // but it doesn't hurt to make the check for S > 0 again.
              sync_state->wakeup = env->get_condition();
              kdu_int32 old_sched, new_sched;
              do { // Enter compare-and-set loop
                old_sched = sync_state->sched.get();
                new_sched = old_sched | KD_ENC_SYNC_SCHED_W_BIT;
              } while (((old_sched & KD_ENC_SYNC_SCHED_S_MASK) == 0) &&
                       !sync_state->sched.compare_and_set(old_sched,
                                                          new_sched));
              if ((old_sched & KD_ENC_SYNC_SCHED_S_MASK) == 0)
                env->wait_for_condition("push line"); // Safe to wait now
              sync_state->wakeup = NULL;
            }
          
          // Advance number of block rows that will be required in the
          // future from the background codestream processing machinery.
          // Should not advance by more than one at a time here or else
          // it is possible that jobs will be scheduled in a non-ideal
          // fashion.
          if (push_state->last_stripes_requested <
              push_state->num_stripes_in_subband)
            { 
              int test_max_Rp = push_state->last_stripes_requested+1;
              if ((test_max_Rp - push_state->num_stripes_released_to_encoder +
                   num_stripes - sched_S) <= KD_ENC_MAX_REL_P)
                { // Safe to advance block rows needed without risking overflow
                  // in the `sync_state->sched' variable's P field.
                  push_state->last_stripes_requested++;
                  band.advance_block_rows_needed(this,1,KD_ENC_QUANTUM_BITS,
                                                 (kdu_uint32)
                                                 (jobs_per_quantum <<
                                                  log2_job_blocks),env);
                }
            }
        }
      push_state->active_lines_left = push_state->next_stripe_height;
      push_state->subband_lines_left -= push_state->active_lines_left;
      push_state->next_stripe_height = this->nominal_block_height;
      if (push_state->next_stripe_height > push_state->subband_lines_left)
        push_state->next_stripe_height = push_state->subband_lines_left;
      assert(push_state->active_push_line == 0);
    }
  
  // Transfer data
  int line_idx = (push_state->active_push_stripe*push_state->stripe_height +
                  push_state->active_push_line);
  int buf_offset = push_state->buffer_offset;
  assert(line.get_width() == subband_cols);
  if (using_shorts)
    { 
      if ((buf_offset != 0) ||
          !line.raw_exchange(push_state->lines16[line_idx],raw_line_width))
        memcpy(push_state->lines16[line_idx]+buf_offset,
               line.get_buf16(),(size_t)(subband_cols<<1));
    }
  else
    { 
      if ((buf_offset != 0) ||
          !line.raw_exchange(push_state->lines32[line_idx],raw_line_width))
        memcpy(push_state->lines32[line_idx]+buf_offset,
               line.get_buf32(),(size_t)(subband_cols<<2));
    }
  if (roi_node != NULL)
    { 
      if (push_state->active_push_line == 0)
        push_state->active_roi_line = roi_buf[push_state->active_push_stripe];
      if (env != NULL)
        roi_context->acquire_lock(KD_THREADLOCK_ROI,env);
      roi_node->pull(push_state->active_roi_line,subband_cols);
      if (env != NULL)
        roi_context->release_lock(KD_THREADLOCK_ROI,env);
      push_state->active_roi_line += roi_row_gap;
    }
  
  // Update push status
  push_state->active_push_line++;
  push_state->active_lines_left--;
  assert(push_state->active_lines_left >= 0);
  
  // Determine what changes we need to make to `sync_state->shed' (or what
  // can be directly encoded).
  kdu_int32 sched_inc=0; // Account for scheduling and changes in S
  if (push_state->active_lines_left == 0)
    { // The active stripe has been completely filled by `push'
      
      // Start by copying the line buffers in the stripe just filled back
      // to the relevant `kd_encoder_job::lines16/32' array -- we need to
      // do this because the line buffers might have been changed by
      // exchange operations.
      int n, num_lines=push_state->active_push_line;
      if (full_block_stripes)
        num_lines = (num_lines + 3) & ~3; // Round to multiple of 4 lines
      kdu_sample16 **dst_lines16 =
        (jobs[push_state->active_push_stripe])[0]->lines16;
      kdu_sample16 **src_lines16 = push_state->lines16 +
        push_state->active_push_stripe*push_state->stripe_height;
      for (n=0; n < num_lines; n++)
        dst_lines16[n] = src_lines16[n];
      
      push_state->active_push_line = 0;
      if (env == NULL)
        { // Perform block encoding immediately
          assert(num_stripes == 1);
          for (int g=0; g < jobs_per_stripe; g++)
            { 
              kd_encoder_job *job = (jobs[0])[g];
              job->do_job(NULL);
            }
          return;
        }
      
      int stripe_idx = push_state->active_push_stripe;
      if ((++push_state->active_push_stripe) == num_stripes)
        push_state->active_push_stripe = 0;
      push_state->active_sched_stripe = stripe_idx;
      assert(push_state->partial_quanta_remaining == 0);
        // Partial scheduling should be completed well before end of a stripe
      
      sched_inc -= KD_ENC_SYNC_SCHED_S0_BIT; // Decrement S count
      (jobs[stripe_idx])[0]->pending_stripe_jobs->set(jobs_per_stripe);
      push_state->num_stripes_released_to_encoder++;
      int stripes_left = (push_state->num_stripes_in_subband -
                          push_state->num_stripes_released_to_encoder);
      if (stripes_left == num_stripes)
        { // Initialize Min_S value to `num_stripes' -- it was equal to 7
          sched_inc += (num_stripes-7) << KD_ENC_SYNC_SCHED_MS_POS;
        }
      else if (stripes_left < num_stripes)
        sched_inc -= KD_ENC_SYNC_SCHED_MS_BIT0; // Decrement Min_S along with S
      /* 
       // First part of test below might be detrimental -- tends to schedule
       // too many low resolution subband jobs near the start, which are
       // expensive and may prevent the high resolution subband stripes from
       // advancing as fast as they can.  This creates a less uniform
       // processing rate for the DWT engines (due to temporary blocking
       // conditions), which may cause hold-ups when a large number of
       // threads is involved.
       if ((push_state->num_stripes_released_to_encoder >= num_stripes) &&
           (lines_per_scheduled_quantum > 0) && (stripes_left > 0))
       */
      if ((lines_per_scheduled_quantum > 0) && (stripes_left > 0))
        { 
          assert(push_state->num_stripes_released_to_encoder <
                 push_state->num_stripes_in_subband);
          assert(push_state->next_stripe_height > 0);
          push_state->partial_quanta_remaining =
            (push_state->next_stripe_height-quantum_scheduling_offset) /
            lines_per_scheduled_quantum;
        }      
      if (push_state->partial_quanta_remaining <= 0)
        { // Make the new stripe FULLY SCHEDULABLE immediately
          push_state->partial_quanta_remaining = 0;
          sched_inc += 3 << (KD_ENC_SYNC_SCHED_U_POS+2*stripe_idx);
        }
      else
        { // Make the new stripe PARTIALLY SCHEDULABLE
          int q = quanta_per_stripe - push_state->partial_quanta_remaining;
          q = (q < 0)?0:q;
          assert(q < (1<<KD_ENC_QUANTUM_BITS));
          sched_inc += 2 << (KD_ENC_SYNC_SCHED_U_POS+2*stripe_idx);
          sched_inc += q << KD_ENC_SYNC_SCHED_Q_POS;
        }
    }
  else
    { // We may need to schedule further job quanta for a partially
      // scheduled stripe
      int stripe_idx = push_state->active_sched_stripe;
      int old_q = push_state->partial_quanta_remaining;
      if (old_q == 0)
        return;
      int new_q = 0;
      if (lines_per_scheduled_quantum > 0)
        { 
          new_q = (push_state->active_lines_left-quantum_scheduling_offset) /
            lines_per_scheduled_quantum;
          if (old_q == new_q)
            return;
        }
      push_state->partial_quanta_remaining = new_q;
      old_q = quanta_per_stripe - old_q;  old_q = (old_q < 0)?0:old_q;
      new_q = quanta_per_stripe - new_q;  new_q = (new_q < 0)?0:new_q;
      // The above two lines make `old_q' and `new_q' equal to the number
      // of initial quanta from the stripe that could previously and now
      // can be marked as schedulable.
      if (new_q >= quanta_per_stripe)
        { // Make the PARTIALLY SCHEDULED stripe FULLY SCHEDULED
          push_state->partial_quanta_remaining = new_q = 0;
          sched_inc += 1 << (KD_ENC_SYNC_SCHED_U_POS+2*stripe_idx);
        }
      sched_inc += (new_q-old_q) << KD_ENC_SYNC_SCHED_Q_POS;
    }
  
  assert(env != NULL);
  if (sched_inc == 0)
    return; // Nothing more to do
  
  kdu_int32 old_sched = sync_state->sched.exchange_add(sched_inc);
  kdu_int32 new_sched = old_sched + sched_inc;
#ifdef _DEBUG
  assert(!(old_sched & KD_ENC_SYNC_SCHED_T_BIT)); // Just to be sure
  int q_val = push_state->partial_quanta_remaining;
  if (q_val != 0)
    { q_val = quanta_per_stripe - q_val; q_val = (q_val<0)?0:q_val; }
  assert(((new_sched & KD_ENC_SYNC_SCHED_Q_MASK) >>
          KD_ENC_SYNC_SCHED_Q_POS) == q_val);
  int s = push_state->active_sched_stripe;
  assert(((q_val==0) && (((new_sched>>(KD_ENC_SYNC_SCHED_U_POS+2*s))&3)==3)) ||
         ((q_val>0) && (((new_sched>>(KD_ENC_SYNC_SCHED_U_POS+2*s))&3)==2)));
  int stripes_left = (push_state->num_stripes_in_subband -
                      push_state->num_stripes_released_to_encoder);
  assert(stripes_left >= 0);
  if (stripes_left > num_stripes)
    assert(((new_sched & KD_ENC_SYNC_SCHED_MS_MASK) >>
            KD_ENC_SYNC_SCHED_MS_POS) == 7);
  else
    assert(((new_sched & KD_ENC_SYNC_SCHED_MS_MASK) >>
            KD_ENC_SYNC_SCHED_MS_POS) == stripes_left);
#endif // _DEBUG
  schedule_new_jobs(old_sched,new_sched,env,num_stripes,
                    jobs_per_stripe,jobs_per_quantum);
  if ((!(new_sched & KD_ENC_SYNC_SCHED_S_MASK)) &&
      (new_sched & KD_ENC_SYNC_SCHED_MS_MASK))
    propagate_dependencies(1,0,env); // Next `push' call might block
}

/*****************************************************************************/
/*                            kd_encoder::init                               */
/*****************************************************************************/

void kd_encoder::init(kdu_subband band, kdu_sample_allocator *allocator,
                      bool use_shorts, float normalization,
                      kdu_roi_node *roi, kdu_thread_env *env,
                      kdu_thread_queue *env_queue,
                      int flags)
{
  assert(this->allocator == NULL);
  this->band = band;
  this->roi_node = roi;
  this->K_max = (kdu_int16) band.get_K_max();
  this->K_max_prime = (kdu_int16) band.get_K_max_prime();
  this->reversible = band.get_reversible();
  this->using_shorts = use_shorts;
  this->initialized = false;
  this->full_block_stripes = false; // Until proven otherwise
  this->delta = band.get_delta() * normalization;
  this->msb_wmse = band.get_msb_wmse();
  this->roi_weight = 1.0F;
  bool have_roi_weight = band.get_roi_weight(roi_weight);
  
  kdu_dims dims;
  band.get_dims(dims);
  kdu_coords nominal_block_size, first_block_size;
  band.get_block_size(nominal_block_size,first_block_size);
  this->subband_cols = dims.size.x;
  this->subband_rows = dims.size.y;
  this->first_block_width = (kdu_int16) first_block_size.x;
  this->first_block_height = (kdu_int16) first_block_size.y;
  this->nominal_block_width = (kdu_int16) nominal_block_size.x;
  this->nominal_block_height = (kdu_int16) nominal_block_size.y;
  band.get_valid_blocks(this->block_indices);
  
  if ((subband_rows <= 0) || (subband_cols <= 0))
    { 
      this->num_stripes = this->jobs_per_stripe = 0;
      return;
    }
  
  // Start by figuring out how to partition each stripe into jobs and quanta
  this->log2_job_blocks = 0;
  int blocks_per_job=1, blocks_across=block_indices.size.x;
  int job_width = nominal_block_size.x;
  int job_samples = job_width;
  if (first_block_size.y == subband_rows)
    job_samples *= first_block_size.y;
  else
    job_samples *= nominal_block_size.y;
  int num_threads=1;
  if (env != NULL)
    num_threads = env->get_num_threads();
  int log2_min_samples=12, log2_ideal_samples=14;
  int min_jobs_across = num_threads;
  while ((blocks_per_job < blocks_across) &&
         ((job_width < 64) ||
          ((job_samples+(job_samples>>1)) < (1<<log2_min_samples))))
    { // Increase to min size
      job_samples *= 2;
      job_width *= 2;
      blocks_per_job *= 2;
      log2_job_blocks++;
    }
  while ((blocks_per_job < blocks_across) &&
         ((job_samples+(job_samples>>1)) < (1<<log2_ideal_samples)))
    { // Increase to ideal size, unless insufficient jobs across
      if (((job_samples+(job_samples>>1))*min_jobs_across) > blocks_across)
        break;
      job_samples *= 2;
      job_width *= 2;
      blocks_per_job *= 2;
      log2_job_blocks++;
    }
  if (blocks_per_job >= (blocks_across-(blocks_per_job>>1)))
    { // Avoid having 2 highly unequal jobs
      job_samples *= 2;
      job_width *= 2;
      blocks_per_job *= 2;
      log2_job_blocks++;
    }      
  this->jobs_per_stripe = 1 + ((blocks_across-1) >> log2_job_blocks);
  this->jobs_per_quantum = 1 + ((jobs_per_stripe-1) >> KD_ENC_QUANTUM_BITS);
  this->quanta_per_stripe = (kdu_int16)
    (1 + ((jobs_per_stripe-1) / jobs_per_quantum));
  assert(quanta_per_stripe <= (1<<KD_ENC_QUANTUM_BITS));
  assert(((quanta_per_stripe*jobs_per_quantum)<<log2_job_blocks) >=
         blocks_across);

  this->lines_per_scheduled_quantum = 0; // We may change this below
  this->quantum_scheduling_offset = 1; // This will do for now
  
  // Now figure out how many stripes there are
  this->num_stripes = 1;
  if (env != NULL)
    { 
      bool is_top = band.is_top_level_band();
      int ideal_stripes = 0; // Until we know better
      if (is_top)
        { // Default policy for high resolution subbands
          ideal_stripes = 2;
          if ((jobs_per_stripe < num_threads) && (num_threads > 8))
            ideal_stripes = 3; // Just a rough heuristic; we could do better
              // incorporating knowledge of the number of parallel DWT engines
              // available, which also depends on the number of tile
              // processing engines we might be running in parallel.
        }
      else
        { // Default policy for lower resolution subbands
          ideal_stripes = 2;
          if (num_threads > 4)
            ideal_stripes = 3; // With larger numbers of threads, we
              // need to worry about keeping all threads alive.  This
              // means especially that we should try to avoid the case in
              // which the DWT's progress is held up by block decoding
              // jobs in the lower resolutions, which can take a lot longer
              // to perform.
          if ((num_threads > 8) && ((2*jobs_per_stripe) < min_jobs_across))
            ideal_stripes = 4;
        }
      
      int cum_stripe_height = first_block_height;
      while ((num_stripes < ideal_stripes) &&
             (cum_stripe_height < subband_rows))
        { 
          num_stripes++;
          cum_stripe_height += nominal_block_height;
        }
      assert(num_stripes <= block_indices.size.y);
      
      if ((quanta_per_stripe > 1) && (num_stripes > 2) && !is_top)
        this->lines_per_scheduled_quantum = (kdu_int16)
          (1 + ((nominal_block_height-1) / quanta_per_stripe));
      if (!env->attach_queue(this,env_queue,KDU_CODING_THREAD_DOMAIN))
        { 
          KDU_ERROR_DEV(e,0x22081103); e <<
          KDU_TXT("Failed to create thread queue when constructing "
                  "`kdu_encoder' object.  One possible cause is that "
                  "the thread group might not have been created first using "
                  "`kdu_thread_env::create', before passing its reference to "
                  "`kdu_encoder', or an exception might have occurred.  "
                  "Another possible (highly unlikely) cause is that too many "
                  "thread working domains are in use.");
        }
      band.attach_block_notifier(this,env);
      if (num_stripes < block_indices.size.y)
        propagate_dependencies(0,1,env);
    }
  
  // Next figure out stripe heights and the memory required by all jobs.
  // Note that the current implementation requires all stripes to have the
  // same height, except possibly the last one, even though this might be
  // wasteful of memory.
  size_t encoder_job_mem = 0;
  int s, sum_stripe_heights=0, stripe_heights[4]={0,0,0,0};
  this->full_block_stripes = (subband_rows >= 4);
  for (s=0; s < num_stripes; s++)
    { 
      int max_height = nominal_block_height;
      if (s == (num_stripes-1))
        { // See if the last stripe can be assigned less memory
          max_height = subband_rows;
          if (s > 0)
            max_height -= (first_block_height + (s-1)*nominal_block_height);
          if (max_height > (int) nominal_block_height)
            max_height = nominal_block_height;
        }
      if (full_block_stripes)
        max_height = (max_height+3) & ~3; // Round to whole code-block stripes
      
      stripe_heights[s] = max_height;
      sum_stripe_heights += max_height;
      encoder_job_mem +=
        kd_encoder_job::calculate_size(max_height,jobs_per_stripe);
    }
  
  // Figure out resources required for ROI processing, if any
  size_t roi_stripe_mem[4]={0,0,0,0};
  this->roi_context = NULL;
  this->roi_row_gap = 0;
  if (roi_node != NULL)
    { 
      if ((K_max_prime == K_max) && !have_roi_weight)
        { 
          roi_node->release();
          roi_node = NULL;
        }
      else
        { 
          this->roi_context = band.get_thread_context(env);
          this->roi_row_gap = (subband_cols+15) & ~15; // 16-byte row alignment
          for (s=0; s < num_stripes; s++)
            { 
              roi_stripe_mem[s] = (size_t)(roi_row_gap*stripe_heights[s]);
              roi_stripe_mem[s]+= KDU_MAX_L2_CACHE_LINE-1; // Round up to whole
              roi_stripe_mem[s]&= ~(KDU_MAX_L2_CACHE_LINE-1); // L2 lines
            }
        }
    }
  
  // Now figure out the line buffer memory
  int alignment = (using_shorts)?KDU_ALIGN_SAMPLES16:KDU_ALIGN_SAMPLES32;
  int buffer_offset = 0;
  if (blocks_across > 1)
    buffer_offset = (- (int) first_block_width) & (alignment-1);
  raw_line_width = subband_cols;
  if ((buffer_offset == 0) && (flags & KDU_LINE_WILL_BE_EXTENDED))
    raw_line_width++;
  int alloc_line_samples =
    (raw_line_width+buffer_offset+alignment-1) & ~(alignment-1);
  size_t line_buf_mem = ((size_t) alloc_line_samples) << ((using_shorts)?1:2);
  size_t optional_align = (-(int)line_buf_mem) & (KDU_MAX_L2_CACHE_LINE-1);
  if (line_buf_mem > (optional_align*8))
    line_buf_mem += optional_align;
  line_buf_mem *= sum_stripe_heights;
  
  // Pre-allocate the memory required to complete initialization
  // within `start'
  size_t job_ptr_mem = ((size_t) jobs_per_stripe) * sizeof(void *);
  this->allocator_bytes = encoder_job_mem + line_buf_mem +
    kd_encoder_push_state::calculate_size(num_stripes,stripe_heights,
                                          job_ptr_mem);
  for (s=0; s < 4; s++)
    allocator_bytes += roi_stripe_mem[s];
  if (env != NULL)
    { // Allocate extra space for synchronization variables
      allocator_bytes += kd_encoder_sync_state::calculate_size() +
      (size_t)num_stripes * KDU_MAX_L2_CACHE_LINE;
      // Last part allocates space for the `pending_stripe_jobs' counters
    }
  this->allocator = allocator;
  allocator->pre_align(KDU_MAX_L2_CACHE_LINE);
  this->allocator_offset = allocator->pre_alloc_block(allocator_bytes);
  allocator->pre_align(KDU_MAX_L2_CACHE_LINE);
  
  // Finally, set up function pointers for any fast SIMD block transfer
  // options that might be available.
#ifdef KDU_SIMD_OPTIMIZATIONS
  simd_block_quant32 = NULL;
  bool tr, vf, hf;
  band.get_block_geometry(tr,vf,hf);
  int nominal_width = nominal_block_width;
  if (blocks_across == 1)
    nominal_width = 2*first_block_width-1;
    // NB: the macros below use the `nominal_width' to determine whether a
    // vector accelerated quantization function is appropriate.  A function
    // is deemed to be appropriate if the number of samples associated with
    // an output vector is <= `nominal_width'.  If there are multiple blocks
    // across the line, the `nominal_width' is certain to be a power of 2, and
    // so this means that the nominal block width is spanned by a whole number
    // of output vectors.  In the case where there is only one block across
    // the subband, we have set `nominal_width' to twice the actual width minus
    // 1, so that a vector processing function is selected so long as more
    // than 50% of the vector width is ºactually used -- otherwise, the most
    // efficient implementation is likely to be one that involves smaller
    // vectors.  In any event, the macros should only compare their output
    // vector size with `nominal_width' rather than testing for divisibility.
  
  if (use_shorts)
    { // 16-bit sample processing
      if (reversible)
        { 
          KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV16(simd_block_quant32,
                                               tr,vf,hf,K_max,nominal_width);
        }
      else
        { 
          KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV16(simd_block_quant32,
                                                 tr,vf,hf,K_max,nominal_width);
        }
    }
  else
    { // 32-bit sample processing
      if (reversible)
        { 
          KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV32(simd_block_quant32,
                                               tr,vf,hf,nominal_width);
        }
      else
        { 
          KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV32(simd_block_quant32,
                                                 tr,vf,hf,nominal_width);
        }
    }
#endif // KDU_SIMD_OPTIMIZATIONS
}

/*****************************************************************************/
/*                             kd_encoder::start                             */
/*****************************************************************************/

void
  kd_encoder::start(kdu_thread_env *env)
{
  if (initialized || (subband_cols==0) || (subband_rows==0))
    { 
      initialized = true;
      return;
    }
  initialized = true;
  
  // Start by recalculating important dimensional parameters
  int alignment = (using_shorts)?KDU_ALIGN_SAMPLES16:KDU_ALIGN_SAMPLES32;
  int buffer_offset = 0;
  if (block_indices.size.x > 1)
    buffer_offset = (- (int) first_block_width) & (alignment-1);
  int s, stripe_heights[4]={0,0,0,0};
  for (s=0; s < num_stripes; s++)
    { 
      int max_height = nominal_block_height;
      if (s == (num_stripes-1))
        { // See if the last stripe can be assigned less memory
          max_height = subband_rows;
          if (s > 0)
            max_height -= (first_block_height+(s-1)*nominal_block_height);
          if (max_height > (int) nominal_block_height)
            max_height = nominal_block_height;
        }      
      if (full_block_stripes)
        max_height = (max_height+3) & ~3; // Round to whole code-block stripes
      stripe_heights[s] = max_height;
    }
  
  // Now assign memory and initialize associated objects
  kdu_byte *alloc_block = (kdu_byte *)
    allocator->alloc_block(allocator_offset,allocator_bytes);
  kdu_byte *alloc_lim=alloc_block+allocator_bytes;
  push_state = (kd_encoder_push_state *) alloc_block;
  size_t job_ptr_mem = ((size_t) jobs_per_stripe) * sizeof(void *);
  alloc_block +=
    kd_encoder_push_state::calculate_size(num_stripes,stripe_heights,
                                          job_ptr_mem);
  assert(alloc_block <= alloc_lim); // Avoid possibility of mem overwrite
  push_state->init(num_stripes,stripe_heights,first_block_height,
                   subband_rows,block_indices.size.y,buffer_offset);
  jobs[0] = (kd_encoder_job **)
    (alloc_block - job_ptr_mem * (size_t) num_stripes);
  for (s=1; s < num_stripes; s++)
    jobs[s] = jobs[s-1] + jobs_per_stripe;
  assert((jobs[num_stripes-1] + jobs_per_stripe) ==
         (kd_encoder_job **) alloc_block);
  kdu_interlocked_int32 *pending_stripe_jobs[4]={NULL,NULL,NULL,NULL};
  if (env != NULL)
    { 
      sync_state = (kd_encoder_sync_state *) alloc_block;
      alloc_block += kd_encoder_sync_state::calculate_size();
      assert(alloc_block <= alloc_lim); // Avoid possibility of mem overwrite
      sync_state->init();
      for (s=0; s < num_stripes; s++)
        { 
          pending_stripe_jobs[s] = (kdu_interlocked_int32 *) alloc_block;
          alloc_block += KDU_MAX_L2_CACHE_LINE;
          assert(alloc_block<=alloc_lim); // Avoid possibility of mem overwrite
          pending_stripe_jobs[s]->set(0);
        }
    }
  
  for (s=0; s < num_stripes; s++)
    { 
      int width, remaining_cols = subband_cols;
      int blocks, remaining_blocks = block_indices.size.x;
      int grp_offset = buffer_offset;
      kdu_coords first_block_idx = block_indices.pos;
      first_block_idx.y += s;
      kd_encoder_job *job, *prev_stripe_job=NULL;
      for (int j=0; j < jobs_per_stripe; j++, prev_stripe_job=job,
           remaining_cols-=width, remaining_blocks-=blocks,
           first_block_idx.x+=blocks, grp_offset+=width)
        { 
          width = nominal_block_width << log2_job_blocks;
          blocks = 1<<log2_job_blocks;
          
          if (j == 0)
            width += first_block_width-nominal_block_width;
          if (width > remaining_cols)
            width = remaining_cols;
          if (blocks > remaining_blocks)
            blocks = remaining_blocks;
          assert((width > 0) && (blocks > 0));
          job = (jobs[s])[j] = (kd_encoder_job *) alloc_block;
          alloc_block += job->init(stripe_heights[s],prev_stripe_job);
          assert(alloc_block <= alloc_lim);
          job->band = this->band;
          job->owner = this;
          job->block_encoder = &(this->block_encoder);
#ifdef KDU_SIMD_OPTIMIZATIONS
          job->simd_block_quant32 = this->simd_block_quant32;
#endif
          job->K_max = this->K_max;
          job->K_max_prime = this->K_max_prime;
          job->reversible = this->reversible;
          job->using_shorts = this->using_shorts;
          job->full_block_stripes = this->full_block_stripes;
          job->delta = this->delta;
          job->msb_wmse = this->msb_wmse;
          job->num_stripes = this->num_stripes;
          job->which_stripe = s;
          job->grp_offset = grp_offset;
          job->grp_width = width;
          job->grp_blocks = blocks;
          job->first_block_idx = first_block_idx;
          job->pending_stripe_jobs = pending_stripe_jobs[s];
          job->roi_weight = roi_weight;
          assert(job->lines16 != NULL);
        }
    }
  
  if (roi_node != NULL)
    for (s=0; s < num_stripes; s++)
      { 
        size_t roi_stripe_mem = (size_t)(roi_row_gap*stripe_heights[s]);
        roi_stripe_mem += KDU_MAX_L2_CACHE_LINE-1;    // Round up to whole
        roi_stripe_mem &= ~(KDU_MAX_L2_CACHE_LINE-1); // L2 lines
        this->roi_buf[s] = (kdu_byte *) alloc_block;
        alloc_block += roi_stripe_mem;
        assert(alloc_block <= alloc_lim);
        kdu_byte *roi8 = roi_buf[s];
        for (int j=0; j < jobs_per_stripe; j++)
          { 
            kd_encoder_job *job = (jobs[s])[j];
            job->roi8 = roi8;
            job->roi_row_gap = roi_row_gap;
            roi8 += job->grp_width;
          }
      }
  
  int alloc_line_samples =
    (raw_line_width+buffer_offset+alignment-1) & ~(alignment-1);
  size_t line_buf_mem = ((size_t) alloc_line_samples) << ((using_shorts)?1:2);
  size_t optional_align = (-(int)line_buf_mem) & (KDU_MAX_L2_CACHE_LINE-1);
  if (line_buf_mem > (optional_align*8))
    line_buf_mem += optional_align;
  
  for (s=0; s < num_stripes; s++)
    { 
      kd_encoder_job *job = (jobs[s])[0];
      kdu_sample16 **lines16 = push_state->lines16 + s*stripe_heights[0];
      for (int m=0; m < stripe_heights[s]; m++)
        { 
          lines16[m] = job->lines16[m] = (kdu_sample16 *) alloc_block;
          alloc_block += line_buf_mem;
        }
    }
  if (alloc_block != alloc_lim)
    { assert(0);
      KDU_ERROR_DEV(e,0x13011201); e <<
      KDU_TXT("Memory allocation/assignment error in `kd_encoder::start'; "
              "pre-allocated memory block has different size to actual "
              "required memory block!  Compile and run in debug mode to "
              "catch this error.");
    }
  
  if (env != NULL)
    { 
      bind_jobs((kdu_thread_job **)(jobs[0]),jobs_per_stripe*num_stripes);
      kdu_int32 S_val = num_stripes;
      kdu_int32 min_S = 7; // If Min_S is too large to be set yet
      assert(S_val <= push_state->num_stripes_in_subband);
      if (S_val >= push_state->num_stripes_in_subband)
        min_S = push_state->num_stripes_in_subband;
      sync_state->sched.set((S_val << KD_ENC_SYNC_SCHED_S_POS) +
                            (min_S << KD_ENC_SYNC_SCHED_MS_POS));
      
      // During encoding, it's fine to request availability for all the
      // code-block rows that we need in advance, without having to iterate
      // through the subbands, asking for them one at a time.  This is because
      // no encoding will happen for quite a while -- still need to do DWT
      // analysis, etc., so nothing can be scheduled yet.
      int max_Rp=(num_stripes > KD_ENC_MAX_REL_P)?KD_ENC_MAX_REL_P:num_stripes;
      push_state->last_stripes_requested = max_Rp;
      band.advance_block_rows_needed(this,(kdu_uint32) max_Rp,
                                     KD_ENC_QUANTUM_BITS,
                                     (kdu_uint32)(jobs_per_quantum <<
                                                  log2_job_blocks),env);
    }
}


/* ========================================================================= */
/*                       kd_encoder_masking_push_state                       */
/* ========================================================================= */

/*****************************************************************************/
/*                    kd_encoder_masking_push_state::init                    */
/*****************************************************************************/

size_t
  kd_encoder_masking_push_state::init(int first_block_width, int subband_cols,
                                      int subband_rows, int nstripes,
                                      int first_block_height,
                                      int nom_block_height)
{
  this->cur_cell_height = 4 - ((-first_block_height) & 3);
  cur_cell_lines_pushed = nxt_cell_lines_pushed = 0;
  this->subband_lines_left = subband_rows;
  cur_acc_row = nxt_acc_row = NULL;
  
  this->samples_across = subband_cols;
  this->cells_across = (first_block_width+3) >> 2;
  this->cells_across += (subband_cols-first_block_width+3)>>2;
  this->first_cell_width = 4 - ((-first_block_width) & 3);
  this->last_cell_width = 4 - ((-(subband_cols-first_block_width)) & 3);
  
  assert(nstripes <= 4);
  this->num_stripes = nstripes;
  assert(!(nom_block_height & 3));
  this->nominal_stripe_cell_rows = nom_block_height >> 2;
  
  active_stripe = 0;
  active_stripe_cell_rows_left = (first_block_height+3)>>2;
  next_stripe_cell_rows = (subband_rows - first_block_height + 3) >> 2;
  if (next_stripe_cell_rows > nominal_stripe_cell_rows)
    next_stripe_cell_rows = nominal_stripe_cell_rows;
  active_cell_row = NULL;
  stripe_cell_activity[0] = stripe_cell_activity[1] = NULL;
  stripe_cell_activity[2] = stripe_cell_activity[3] = NULL;
  
  float *ptr = cell_storage + 1; // Allow access to 2 samples before `ptr'
  int align_off = _addr_to_kdu_int32(ptr) >> 2;
  align_off = (-align_off) & (KDU_ALIGN_SAMPLES32-1);
  ptr += align_off; // align on natural float-vector boundary
  int acc_row_width = subband_cols + 4; // allows left/right extension
  acc_row_width += ((-acc_row_width) & (KDU_ALIGN_SAMPLES16-1));
  assert(acc_row_width >= (subband_cols+4));
  memset(ptr,0,sizeof(float)*(size_t)(2*acc_row_width));
  cur_acc_row = ptr;  ptr += acc_row_width;
  nxt_acc_row = ptr;  ptr += acc_row_width;
  
  align_off = ( - _addr_to_kdu_int32(ptr)) & (KDU_MAX_L2_CACHE_LINE-1);
  align_off >>= 2; // Multiple of floats
  ptr += align_off;
  assert((_addr_to_kdu_int32(ptr) & (KDU_MAX_L2_CACHE_LINE-1)) == 0);
  
  int num_stripe_cells = nominal_stripe_cell_rows * cells_across;
  num_stripe_cells += (-num_stripe_cells)&((KDU_MAX_L2_CACHE_LINE/4)-1);
  for (int s=0; s < num_stripes; s++, ptr+=num_stripe_cells)
    stripe_cell_activity[s] = ptr;
  active_cell_row = stripe_cell_activity[0];
  
  size_t len = ((kdu_byte *) ptr) - ((kdu_byte *) this);
  return len;
}

/*****************************************************************************/
/*            kd_encoder_masking_push_state::process_line (non-LL)           */
/*****************************************************************************/

void
  kd_encoder_masking_push_state::process_line(kdu_line_buf &line)
{
  if (cur_cell_lines_pushed == cur_cell_height)
    { // In this case, we are 4 lines away from finishing the current cell
      // row so we should reset the next one prior to accumulation.  We could
      // always do this resetting right after we flush `cur_acc_row' and
      // swap it with `nxt_acc_row', but that would not work when we
      // are just getting started.  Doing it here is just as easy and also
      // works on startup.
      memset(nxt_acc_row,0,sizeof(float)*(size_t)samples_across);
      nxt_cell_lines_pushed = 0;
    }
  float *dp1=cur_acc_row;
  float *dp2=nxt_acc_row;
  kdu_int16 *sp16 = (kdu_int16 *) line.get_buf16();
  if (sp16 != NULL)
    {       
      for (int n=0; n < samples_across; n++)
        { 
          float val = sqrtf(fabsf((float) sp16[n]));
          dp1[n] += val;  dp2[n] += val;
        }
    }
  else if (line.is_absolute())
    { 
      kdu_int32 *sp32 = (kdu_int32 *) line.get_buf32();
      for (int n=0; n < samples_across; n++)
        { 
          float val = sqrtf(fabsf((float) sp32[n]));
          dp1[n] += val;  dp2[n] += val;
        }      
    }
  else
    { 
      float *sp32 = (float *) line.get_buf32();
      for (int n=0; n < samples_across; n++)
        { 
          float val = sqrtf(fabsf(sp32[n]));
          dp1[n] += val;  dp2[n] += val;
        }
    }
  
  cur_cell_lines_pushed++;
  nxt_cell_lines_pushed++;
  if (cur_cell_lines_pushed == (cur_cell_height+4))
    { 
      generate_cells();
      dp1 = cur_acc_row;  cur_acc_row = nxt_acc_row;  nxt_acc_row = dp1;
      cur_cell_lines_pushed = nxt_cell_lines_pushed;
      nxt_cell_lines_pushed = 0;
      cur_cell_height = (subband_lines_left < 4)?subband_lines_left:4;
    }
}

/*****************************************************************************/
/*              kd_encoder_masking_push_state::process_line (LL)             */
/*****************************************************************************/

void
  kd_encoder_masking_push_state::process_line(kdu_line_buf &prev_line,
                                              kdu_line_buf &line,
                                              kdu_line_buf &next_line)
{
  if (cur_cell_lines_pushed == cur_cell_height)
    { 
      memset(nxt_acc_row,0,sizeof(float)*(size_t)samples_across);
      nxt_cell_lines_pushed = 0;
    }
  float *dp1=cur_acc_row;
  float *dp2=nxt_acc_row;
  kdu_int16 *sp16 = (kdu_int16 *) line.get_buf16();
  if (sp16 != NULL)
    { 
      kdu_int16 *pp16 = (kdu_int16 *) prev_line.get_buf16();
      kdu_int16 *np16 = (kdu_int16 *) next_line.get_buf16();
      sp16[samples_across]=sp16[samples_across-1]; // zero order hold on right
      kdu_int16 left_val=sp16[0]; // zero order hold on left
      for (int n=0; n < samples_across; n++)
        { 
          kdu_int32 v=left_val;  v += sp16[n+1];  v += pp16[n];  v += np16[n];
          left_val = sp16[n]; // will be left val in next iteration of the loop
          v = ((v+2)>>2) - left_val;
          float val = sqrtf(fabsf((float) v));
          dp1[n] += val;  dp2[n] += val;
        }
    }
  else if (line.is_absolute())
    { 
      kdu_int32 *sp32 = (kdu_int32 *) line.get_buf32();
      kdu_int32 *pp32 = (kdu_int32 *) prev_line.get_buf32();
      kdu_int32 *np32 = (kdu_int32 *) next_line.get_buf32();
      sp32[samples_across]=sp32[samples_across-1]; // zero order hold on right
      kdu_int32 left_val=sp32[0]; // zero order hold on left
      for (int n=0; n < samples_across; n++)
        { 
          kdu_int32 v=left_val;  v += sp32[n+1];  v += pp32[n];  v += np32[n];
          left_val = sp32[n]; // will be left val in next iteration of the loop
          v = ((v+2)>>2) - left_val;
          float val = sqrtf(fabsf((float) v));
          dp1[n] += val;  dp2[n] += val;
        }
    }
  else
    { 
      float *sp32 = (float *) line.get_buf32();
      float *pp32 = (float *) prev_line.get_buf32();
      float *np32 = (float *) next_line.get_buf32();      
      sp32[samples_across]=sp32[samples_across-1]; // zero order hold on right
      float left_val=sp32[0]; // zero order hold on left
      for (int n=0; n < samples_across; n++)
        { 
          float v=left_val;  v += sp32[n+1];  v += pp32[n];  v += np32[n];
          left_val = sp32[n]; // will be left val in next iteration of the loop
          v = 0.25f*v - left_val;
          float val = sqrtf(fabsf(v));
          dp1[n] += val;  dp2[n] += val;
        }
    }
  
  cur_cell_lines_pushed++;
  nxt_cell_lines_pushed++;
  if (cur_cell_lines_pushed == (cur_cell_height+4))
    { 
      generate_cells();
      dp1 = cur_acc_row;  cur_acc_row = nxt_acc_row;  nxt_acc_row = dp1;
      cur_cell_lines_pushed = nxt_cell_lines_pushed;
      nxt_cell_lines_pushed = 0;
      cur_cell_height = (subband_lines_left < 4)?subband_lines_left:4;
    }
}

/*****************************************************************************/
/*                  kd_encoder_masking_push_state::generate                  */
/*****************************************************************************/

void kd_encoder_masking_push_state::generate_cells()
{
  assert(cur_cell_lines_pushed == (cur_cell_height + 4));
  float *dp = active_cell_row;
  float sum, *sp = cur_acc_row;
  
  // Extend `sp' on the left and right so as to simplify the horizontal
  // accumulation process.
  sp[-2] = sp[-1] = sp[0];
  sp[samples_across+1] = sp[samples_across] = sp[samples_across-1];
  sp -= 2; // Align `sp' with start of overlap region for the first cell
  
  // Do first cell specially
  int n, cell_span = first_cell_width + 4;
  for (sum=0.0f, n=cell_span; n > 0; n--)
    sum += *(sp++);
  *(dp++) = sum / (cell_span*cur_cell_lines_pushed);
  
  // Do all but the last cell now
  sp -= 4; // Align `sp' with start of overlap region for the second cell
  float factor = 1.0f / (cur_cell_lines_pushed*8);
  for (n=cells_across-2; n > 0; n--, sp+=4)
    { 
      sum = sp[0]+sp[1]+sp[2]+sp[3]+sp[4]+sp[5]+sp[6]+sp[7];
      *(dp++) = sum * factor;
    }
  
  // Do last cell specially
  cell_span = last_cell_width + 4;
  for (sum=0.0f, n=cell_span; n > 0; n--)
    sum += *(sp++);
  *(dp++) = sum / (cell_span*cur_cell_lines_pushed);
  
  // Advance to the next row of cells for next time we come here
  assert(subband_lines_left >= cur_cell_height);
  subband_lines_left -= cur_cell_height;
  active_stripe_cell_rows_left--;
  active_cell_row += cells_across;
  if (active_stripe_cell_rows_left == 0)
    { // Move on to the next stripe
      active_stripe_cell_rows_left = next_stripe_cell_rows;
      next_stripe_cell_rows = (subband_lines_left + 3) >> 2;
      if (next_stripe_cell_rows > nominal_stripe_cell_rows)
        next_stripe_cell_rows = nominal_stripe_cell_rows;
      active_stripe++;
      if (active_stripe == num_stripes)
        active_stripe = 0;
      active_cell_row = stripe_cell_activity[active_stripe];
    }
}


/* ========================================================================= */
/*                              kd_mask_encoder                              */
/* ========================================================================= */

/*****************************************************************************/
/*                          kd_mask_encoder::init                            */
/*****************************************************************************/

void kd_mask_encoder::init(kdu_subband band, kdu_sample_allocator *allocator,
                           bool use_shorts, float normalization,
                           kdu_roi_node *roi, kdu_thread_env *env,
                           kdu_thread_queue *env_queue, int flags,
                           float visibility_floor, float visual_scale)
{
  assert((visibility_floor > 0.0) && (visual_scale > 0.0f));
  ll_band = (band.get_band_idx() == 0);
  is_absolute = band.get_reversible();
  this->mask_offset = visibility_floor;
  this->mask_scale = visual_scale;
  if (!is_absolute)
    { 
      mask_scale /= normalization;
      if (use_shorts)
        mask_scale /= (float)(1<<KDU_FIX_POINT);
    }
  kd_encoder::init(band,allocator,use_shorts,normalization,
                   roi,env,env_queue,flags);
  aux_allocator_bytes =
    kd_encoder_masking_push_state::calculate_size(first_block_width,
                                                  subband_cols,num_stripes,
                                                  nominal_block_height);
  allocator->pre_align(KDU_MAX_L2_CACHE_LINE);
  aux_allocator_offset = allocator->pre_alloc_block(aux_allocator_bytes);
  
  num_delay_lines = (ll_band)?3:2;
  int left_extend=0, right_extent=(ll_band)?1:0;
  for (int d=0; d < num_delay_lines; d++)
    delay_lines[d].pre_create(allocator,subband_cols,is_absolute,
                              use_shorts,left_extend,right_extent);
  allocator->pre_align(KDU_MAX_L2_CACHE_LINE);
}

/*****************************************************************************/
/*                          kd_mask_encoder::start                           */
/*****************************************************************************/

void
  kd_mask_encoder::start(kdu_thread_env *env)
{
  if (initialized || (subband_cols == 0) || (subband_rows == 0))
    return;
  kd_encoder::start(env);
  
  // Allocate extra resources
  kdu_byte *alloc_block = (kdu_byte *)
    allocator->alloc_block(aux_allocator_offset,aux_allocator_bytes);
  kdu_byte *alloc_lim=alloc_block+aux_allocator_bytes;
  masking_push_state = (kd_encoder_masking_push_state *) alloc_block;
  size_t reserved_bytes =
    kd_encoder_masking_push_state::calculate_size(first_block_width,
                                                  subband_cols,num_stripes,
                                                  nominal_block_height);
  alloc_block += reserved_bytes;
  if (alloc_block != alloc_lim)
    assert(0);
  size_t used_bytes =
    masking_push_state->init(first_block_width,subband_cols,subband_rows,
                             num_stripes,first_block_height,
                             nominal_block_height);
  if (used_bytes > reserved_bytes)
    assert(0);
  for (int d=0; d < num_delay_lines; d++)
    delay_lines[d].create();
  
  // Configure the block coding jobs to use cellular masking weights
  kd_encoder_masking_push_state *mps = masking_push_state;
  for (int s=0; s < num_stripes; s++)
    { 
      float *cell_ptr=mps->stripe_cell_activity[s];
      for (int j=0; j < jobs_per_stripe; j++)
        { 
          kd_encoder_job *job = (jobs[s])[j];
          int job_cells_wide =
            job->init_mask_encoding(cell_ptr,mps->cells_across,
                                    mask_offset,mask_scale);
          cell_ptr += job_cells_wide;
        }      
    }
}

/*****************************************************************************/
/*                           kd_mask_encoder::push                           */
/*****************************************************************************/

void
  kd_mask_encoder::push(kdu_line_buf &line, kdu_thread_env *env)
{
  kd_encoder_masking_push_state *mps = masking_push_state;
  size_t line_bytes = (size_t)subband_cols;
  line_bytes = (using_shorts)?(line_bytes<<1):(line_bytes<<2);
  assert(subband_lines_received < subband_rows);
  if (subband_lines_received == 0)
    { // Just getting started; fill the delay queue with copies of `line',
      // generating masking data for two extrapolated lines along the way.
      if (num_delay_lines == 2)
        { 
          assert(!ll_band);
          memcpy(delay_lines[0].get_buf(),line.get_buf(),line_bytes);
          memcpy(delay_lines[1].get_buf(),line.get_buf(),line_bytes);
          mps->process_line(delay_lines[0]);
          mps->process_line(delay_lines[1]);
        }
      else if (num_delay_lines == 3)
        { 
          assert(ll_band);
          memcpy(delay_lines[0].get_buf(),line.get_buf(),line_bytes);
          memcpy(delay_lines[1].get_buf(),line.get_buf(),line_bytes);
          memcpy(delay_lines[2].get_buf(),line.get_buf(),line_bytes);
          mps->process_line(delay_lines[0],delay_lines[1],delay_lines[2]);
          mps->process_line(delay_lines[1],delay_lines[2],line);
        }
      else
        assert(0);
    }
  
  // Generate masking data from the new `line'
  if (num_delay_lines == 2)
    { 
      assert(!ll_band);
      mps->process_line(line);
    }
  else if (num_delay_lines == 3)
    { 
      assert(ll_band);
      mps->process_line(delay_lines[1],delay_lines[2],line);
    }
  
  // Push the head of the delay queue down to the base object, so long as
  // it contains valid data.
  if (subband_lines_received >= num_delay_lines)
    kd_encoder::push(delay_lines[0],env);
  
  // Rotate the delay queue, copying the new `line' into the released buffer
  // which moves to the tail of the queue.
  int d;
  kdu_line_buf head = delay_lines[0];
  for (d=1; d < num_delay_lines; d++)
    delay_lines[d-1] = delay_lines[d];
  if (!(head.exchange(line)))
    memcpy(head.get_buf(),line.get_buf(),line_bytes);
  delay_lines[num_delay_lines-1] = head;
  
  // See if we have reached the end of the subband
  subband_lines_received++;
  if (subband_lines_received == subband_rows)
    { // Replicate the last row `num_delay_lines' times, advancing the queue
      // and performing the relevant processing steps as we go.
      for (; subband_lines_received < (subband_rows+num_delay_lines);
           subband_lines_received++)
        { 
          if (num_delay_lines == 2)
            mps->process_line(delay_lines[1]);
          else
            mps->process_line(delay_lines[1],delay_lines[2],delay_lines[2]);
          if (subband_lines_received >= num_delay_lines)
            { // Need this test, just in case the subband only has 1 or 2 lines
              kd_encoder::push(delay_lines[0],env);
            }
          head = delay_lines[0];
          for (d=1; d < num_delay_lines; d++)
            delay_lines[d-1] = delay_lines[d];
          memcpy(head.get_buf(),delay_lines[d-2].get_buf(),line_bytes);
          delay_lines[num_delay_lines-1] = head;
        }
    }
}
