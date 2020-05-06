/*****************************************************************************/
// File: decoder.cpp [scope = CORESYS/CODING]
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
   Implements the functionality offered by the "kdu_decoder" object defined
in "kdu_sample_processing.h".  Includes ROI adjustments, dequantization,
subband sample buffering and geometric appearance transformations.
******************************************************************************/

#include <string.h>
#include <assert.h>
#include "kdu_arch.h" // Include architecture-specific info for speed-ups.
#include "kdu_messaging.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "kdu_block_coding.h"
#include "kdu_kernels.h"
#include "decoding_local.h"

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
   kdu_error _name("E(decoder.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(decoder.cpp)",_id);
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

// Import architecture-specific optimizations; note that KDU_SIMD_OPTIMIZATIONS
// should have been defined already (inside "decoding_local.h")
#if defined KDU_X86_INTRINSICS
#  include "x86_decoder_local.h" // x86-based SIMD accelerators
#elif defined KDU_NEON_INTRINSICS
#  include "neon_decoder_local.h" // ARM-NEON based SIMD accelerators
#endif

#ifdef KDU_SIMD_OPTIMIZATIONS
using namespace kd_core_simd; // So macro selectors can access SIMD externals
#endif

using namespace kd_core_local;


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    get_first_unscheduled_job_in_stripe                    */
/*****************************************************************************/

static inline int
  get_first_unscheduled_job(kdu_int32 sched, int which, int num_stripes,
                            int jobs_per_stripe, int jobs_per_quantum)
  /* The `sched' argument here is a snapshot of `kd_decoder_sync_state::sched'
     from which the function deduces the index of the first job within the
     stripe identified by `which' that has not yet been scheduled.  If all
     jobs in the stripe have been scheduled, the function returns
     'jobs_per_stripe', which corresponds to `kd_decoder::num_jobs_per_stripe'.
     The total number of job processing stripes available in the
     `kd_decoder' object is supplied as `num_stripes', while the number of
     jobs in all but the last quantum of a stripe is given by
     `jobs_per_quantum'; these correspond to the values of
     `kd_decoder::num_stripes' and `kd_decoder::jobs_per_quantum',
     respectively.  This function is deliberately not made a
     member of the `kd_decoder' object, because there are subtle conditions
     under which it may need to be called after the `kd_decoder' object
     has been cleaned up. */
{
  int P_rel = ((sched & KD_DEC_SYNC_SCHED_P_MASK) >> KD_DEC_SYNC_SCHED_P_POS);
  int R_rel = P_rel >> KD_DEC_QUANTUM_BITS;
  int active = (sched >> KD_DEC_SYNC_SCHED_A_POS) & 3;
  int status = (sched >> (KD_DEC_SYNC_SCHED_U_POS + 2*which)) & 3;
  if (status < 2)
    return 0; // Stripe not available for decoding
  int w_rel = which - active;
  R_rel -= (w_rel < 0)?(num_stripes+w_rel):w_rel;
  if (R_rel < 0)
    return 0; // Nothing ready to be scheduled
  int quanta = (1 << KD_DEC_QUANTUM_BITS);
  if (R_rel == 0)
    quanta = P_rel&(quanta-1); // `KD_DEC_QUANTUM_BITS' LSB's of P_rel 
  if (status == 2)
    { // Partially schedulable stripe
      int max_quanta = ((sched & KD_DEC_SYNC_SCHED_Q_MASK) >>
                        KD_DEC_SYNC_SCHED_Q_POS);
      quanta = (max_quanta < quanta)?max_quanta:quanta;
    }
  int J = quanta * jobs_per_quantum;
  return (J >= jobs_per_stripe)?jobs_per_stripe:J;
}


/* ========================================================================= */
/*                                kdu_decoder                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdu_decoder::kdu_decoder                         */
/*****************************************************************************/

kdu_decoder::kdu_decoder(kdu_subband band, kdu_sample_allocator *allocator,
                         bool use_shorts, float normalization,
                         int pull_offset, kdu_thread_env *env,
                         kdu_thread_queue *env_queue, int flags)
  // In the future, we may create separate, optimized objects for each kernel.
{
  kd_decoder *dec = new kd_decoder;
  state = dec;
  dec->init(band,allocator,use_shorts,normalization,pull_offset,
            env,env_queue,flags);
}


/* ========================================================================= */
/*                               kd_decoder_job                              */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC               kd_decoder_job::decode_blocks                        */
/*****************************************************************************/

void
  kd_decoder_job::decode_blocks(kd_decoder_job *job, kdu_thread_env *env)
{
  bool using_shorts = job->using_shorts;
  bool reversible = job->reversible;
  int K_max = job->K_max;
  int K_max_prime = job->K_max_prime;
  float delta = job->delta;
  kdu_block_decoder *block_decoder = job->block_decoder;
  int offset = job->grp_offset; // Horizontal offset into line buffers
  int blocks_remaining = job->grp_blocks;
  kdu_coords idx = job->first_block_idx; // Index of first block to process
  job->first_block_idx.y += job->num_stripes; // For the next time we come here

  // Now scan through the blocks to process
  kdu_coords xfer_size;
  kdu_block *block;
  for (bool scan_start=true; blocks_remaining > 0; blocks_remaining--,
       idx.x++, offset+=xfer_size.x, scan_start=false)
    { 
      block = job->band.open_block(idx,NULL,env,blocks_remaining,scan_start);
      xfer_size = block->region.size;
      if (block->transpose)
        xfer_size.transpose();
      if (block->num_passes > 0)
        block_decoder->decode(block);
      if ((K_max_prime > K_max) && (block->num_passes != 0))
        job->adjust_roi_background(block);
      int row_gap = block->size.x;

      if (block->num_passes == 0)
        { // Fill block region with zeros
#ifdef KDU_SIMD_OPTIMIZATIONS
          if (job->simd_block_zero)
            job->simd_block_zero(job->untyped_lines,offset,
                                 xfer_size.x,xfer_size.y);
          else
#endif // KDU_SIMD_OPTIMIZATIONS
            { // Need sample-by-sample general purpose zeroing function
              int m, n;
              if (using_shorts)
                { // 16-bit samples
                  kdu_sample16 *dp, **dpp=job->lines16;
                  for (m=0; m < xfer_size.y; m++)
                    { 
                      for (dp=dpp[m]+offset, n=xfer_size.x; n>3; n-=4, dp+=4)
                        { dp[0].ival=0;  dp[1].ival=0;
                          dp[2].ival=0;  dp[3].ival=0; }
                      for (; n > 0; n--, dp++)
                        dp->ival = 0;
                    }
                }
              else
                { // 32-bit samples
                  kdu_sample32 *dp, **dpp=job->lines32;
                  for (m=0; m < xfer_size.y; m++)
                    if (reversible)
                      { 
                        for (dp=dpp[m]+offset, n=xfer_size.x; n>3; n-=4, dp+=4)
                          { dp[0].ival=0;  dp[1].ival=0;
                            dp[2].ival=0;  dp[3].ival=0; }
                        for (; n > 0; n--, dp++)
                          dp->ival = 0;
                      }
                    else
                      { 
                        for (dp=dpp[m]+offset, n=xfer_size.x; n>3; n-=4, dp+=4)
                          { dp[0].fval = 0.0F;  dp[1].fval = 0.0F;
                            dp[2].fval = 0.0F;  dp[3].fval = 0.0F; }
                        for (; n > 0; n--, dp++)
                          dp->fval = 0.0;
                      }
                }
            }
        }
      else
        { // Need to dequantize and/or convert quantization indices
          kdu_int32 *spp = block->sample_buffer + block->region.pos.y*row_gap;
          spp += block->region.pos.x;
#ifdef KDU_SIMD_OPTIMIZATIONS
          if (job->simd_block_xfer)
            job->simd_block_xfer(spp,job->untyped_lines,offset,                                 
                                 xfer_size.x,row_gap,xfer_size.y,K_max,delta);
          else
#endif // KDU_SIMD_OPTIMIZATIONS
            { // Need sample-by-sample general purpose sample dequant/transfer
              kdu_int32 *sp;
              int m, n, m_start = 0, m_inc=1, n_start=offset, n_inc=1;
              if (block->vflip)
                { m_start += xfer_size.y-1; m_inc = -1; }
              if (block->hflip)
                { n_start += xfer_size.x-1; n_inc = -1; }
              if (using_shorts)
                { // Need to produce 16-bit dequantized values.
                  kdu_sample16 *dp, **dpp = job->lines16+m_start;
                  if (reversible)
                    { // Output is 16-bit absolute integers.
                      kdu_int32 val;
                      kdu_int32 downshift = 31-K_max;
                      assert(downshift>=0); //Otherwise should be using 32 bits
                      if (!block->transpose)
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp+=row_gap)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp++, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                dp->ival = (kdu_int16)
                                  -((val & KDU_INT32_MAX) >> downshift);
                              else
                                dp->ival = (kdu_int16)(val >> downshift);
                            }
                      else
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp++)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp+=row_gap, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                dp->ival = (kdu_int16)
                                  -((val & KDU_INT32_MAX) >> downshift);
                              else
                                dp->ival = (kdu_int16)(val >> downshift);
                            }
                    }
                  else
                    { // Output is 16-bit fixed point values.
                      float fscale = delta * (float)(1<<KDU_FIX_POINT);
                      if (K_max <= 31)
                        fscale /= (float)(1<<(31-K_max));
                      else
                        fscale *= (float)(1<<(K_max-31));
                      fscale *= (float)(1<<16) * (float)(1<<16);
                      kdu_int32 val, scale = ((kdu_int32)(fscale+0.5F));
                      if (!block->transpose)
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp+=row_gap)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp++, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                val = -(val & KDU_INT32_MAX);
                              val = (val+(1<<15))>>16; val *= scale;
                              dp->ival = (kdu_int16)((val+(1<<15))>>16);
                            }
                      else
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp++)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp+=row_gap, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                val = -(val & KDU_INT32_MAX);
                              val = (val+(1<<15))>>16; val *= scale;
                              dp->ival = (kdu_int16)((val+(1<<15))>>16);
                            }
                    }
                }
              else
                { // Need to generate 32-bit dequantized values.
                  kdu_sample32 *dp, **dpp = job->lines32+m_start;        
                  if (reversible)
                    { // Output is 32-bit absolute integers.
                      kdu_int32 val;
                      kdu_int32 downshift = 31-K_max;
                      if (downshift < 0)
                        { KDU_ERROR(e,0); e <<
                          KDU_TXT("Insufficient implementation precision "
                                  "available for true reversible processing!");
                        }
                      if (!block->transpose)
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp+=row_gap)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp++, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                dp->ival = -((val & KDU_INT32_MAX)>>downshift);
                              else
                                dp->ival = val >> downshift;
                            }
                      else
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp++)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp+=row_gap, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                dp->ival = -((val & KDU_INT32_MAX)>>downshift);
                              else
                                dp->ival = val >> downshift;
                            }
                    }
                  else
                    { // Output is true floating point values.
                      kdu_int32 val;
                      float scale = delta;
                      if (K_max <= 31)
                        scale /= (float)(1<<(31-K_max));
                      else  // Can't decode all planes
                        scale*=(float)(1<<(K_max-31));
                      if (!block->transpose)
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp+=row_gap)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp++, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                val = -(val & KDU_INT32_MAX);
                              dp->fval = scale * val;
                            }
                      else
                        for (m=xfer_size.y; m--; dpp+=m_inc, spp++)
                          for (dp=dpp[0]+n_start, sp=spp,
                               n=xfer_size.x; n--; sp+=row_gap, dp+=n_inc)
                            { 
                              val = *sp;
                              if (val < 0)
                                val = -(val & KDU_INT32_MAX);
                              dp->fval = scale * val;
                            }
                    }
                }
            }
        }

      job->band.close_block(block,env);
    }

    if (env != NULL)
      {
        kdu_int32 old_count = job->pending_stripe_jobs->exchange_add(-1);
        assert(old_count > 0);
        if (old_count == 1)
          job->owner->stripe_decoded(job->which_stripe,env);
      }
}

/*****************************************************************************/
/*                  kd_decoder_job::adjust_roi_background                    */
/*****************************************************************************/

void
  kd_decoder_job::adjust_roi_background(kdu_block *block)
{
  kdu_int32 upshift = K_max_prime - K_max;
  kdu_int32 mask = (-1)<<(31-K_max); mask &= KDU_INT32_MAX;
  kdu_int32 *sp = block->sample_buffer;
  kdu_int32 val;
  int num_samples = ((block->size.y+3)>>2) * (block->size.x<<2);

  for (int n=num_samples; n--; sp++)
    if ((((val=*sp) & mask) == 0) && (val != 0))
      {
        if (val < 0)
          *sp = (val << upshift) | KDU_INT32_MIN;
        else
          *sp <<= upshift;
      }
}


/* ========================================================================= */
/*                               kd_decoder                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_decoder::stripe_decoded                         */
/*****************************************************************************/

bool
  kd_decoder::stripe_decoded(int which, kdu_thread_env *env)
{
  kdu_int32 new_sched=0, old_sched=0;
  if (num_stripes == 1)
    { 
      kdu_int32 delta_sched = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + KD_DEC_SYNC_SCHED_S0_BIT // S++
        - (3 << KD_DEC_SYNC_SCHED_U_POS);  // U0 = 0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        new_sched = old_sched + delta_sched;
        if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) & KD_DEC_SYNC_SCHED_P_MASK)
          new_sched -= (KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
        new_sched &= ~KD_DEC_SYNC_SCHED_W_BIT;
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & (3*KD_DEC_SYNC_SCHED_U0_BIT)) ==
             (3*KD_DEC_SYNC_SCHED_U0_BIT));
    }
  else if (num_stripes == 2)
    { 
      kdu_int32 A_test = which << KD_DEC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three, A_inc;
      if (which == 0)
        { 
          U0_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one   =  4*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc =       KD_DEC_SYNC_SCHED_A0_BIT;
        }
      else
        { 
          U1_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one   =  4*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc =      -KD_DEC_SYNC_SCHED_A0_BIT;
        }
      kdu_int32 delta_sched_1 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + KD_DEC_SYNC_SCHED_S0_BIT // S++
        - U0_three // Sets U0 = 0
        + A_inc; // Increments A (modulo 2)
      kdu_int32 delta_sched_2 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_DEC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one; // Sets U0=U1=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_DEC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance by 2 stripes
                new_sched = old_sched + delta_sched_2;
                if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                    KD_DEC_SYNC_SCHED_P_MASK)
                  new_sched-=2*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
              }
            else
              { // Advance by 1 stripe only
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                    KD_DEC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
              }                
            new_sched &= ~KD_DEC_SYNC_SCHED_W_BIT;
          }
        else
          new_sched = old_sched - 2*U0_one; // Just change U0 from 3 to 1
      } while(!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }
  else if (num_stripes == 3)
    { 
      kdu_int32 A_test = which << KD_DEC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three, U2_one, U2_three;
      kdu_int32 A_inc_1, A_inc_2;
      switch (which) {
        case 0:
          U0_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        case 1:
          U2_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =    -KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        case 2:
          U1_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three  = 3*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =  -2*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =    -KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        default: abort();
      }
      kdu_int32 delta_sched_1 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + KD_DEC_SYNC_SCHED_S0_BIT // S++
        - U0_three // Sets U0=0
        + A_inc_1; // Increments A (modulo 3)
      kdu_int32 delta_sched_2 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_DEC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one // Sets U0=U1=0
        + A_inc_2; // Increments A by 2 (modulo 3)
      kdu_int32 delta_sched_3 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (3*KD_DEC_SYNC_SCHED_S0_BIT) // S += 3
        - U0_three - U1_one - U2_one; // Sets U0=U1=U2=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_DEC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance by at least 2 stripes
                if ((old_sched & U2_three)==U2_one)
                  { // Can advance 3 stripes
                    new_sched = old_sched + delta_sched_3;
                    if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                        KD_DEC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        3*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
                  }
                else
                  { // Advance only 2 stripes
                    new_sched = old_sched + delta_sched_2;
                    if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                        KD_DEC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        2*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);                    
                  }
              }
            else
              { // Advance by 1 stripe only
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                    KD_DEC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
              }
            new_sched &= ~KD_DEC_SYNC_SCHED_W_BIT;
          }
        else
          new_sched = old_sched - 2*U0_one; // Just change U0 from 3 to 1
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }
  else if (num_stripes == 4)
    { 
      kdu_int32 A_test = which << KD_DEC_SYNC_SCHED_A_POS;
      kdu_int32 U0_one, U0_three, U1_one, U1_three;
      kdu_int32 U2_one, U2_three, U3_one, U3_three;
      kdu_int32 A_inc_1, A_inc_2, A_inc_3;
      switch (which) {
        case 0:
          U0_one      = KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_one =   64*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_three =192*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_3 =   3*KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        case 1:
          U3_one =      KD_DEC_SYNC_SCHED_U0_BIT;
          U3_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_one =   64*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three =192*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =   2*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        case 2:
          U2_one =      KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_one =   64*KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three =192*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =     KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =  -2*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        case 3:
          U1_one =      KD_DEC_SYNC_SCHED_U0_BIT;
          U1_three =  3*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_one =    4*KD_DEC_SYNC_SCHED_U0_BIT;
          U2_three = 12*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_one =   16*KD_DEC_SYNC_SCHED_U0_BIT;
          U3_three = 48*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_one =   64*KD_DEC_SYNC_SCHED_U0_BIT;
          U0_three =192*KD_DEC_SYNC_SCHED_U0_BIT;
          A_inc_1 =  -3*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_2 =  -2*KD_DEC_SYNC_SCHED_A0_BIT;
          A_inc_3 =    -KD_DEC_SYNC_SCHED_A0_BIT;
          break;
        default: abort();
      }
      kdu_int32 delta_sched_1 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + KD_DEC_SYNC_SCHED_S0_BIT // S++
        - U0_three // U0=0
        + A_inc_1; // Increments A (modulo 4)
      kdu_int32 delta_sched_2 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (2*KD_DEC_SYNC_SCHED_S0_BIT) // S += 2
        - U0_three - U1_one // U0=U1=0
        + A_inc_2; // Increments A by 2 (modulo 4)
      kdu_int32 delta_sched_3 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (3*KD_DEC_SYNC_SCHED_S0_BIT) // S += 3
        - U0_three - U1_one - U2_one // U0=U1=U2=0
        + A_inc_3; // Increments A by 3 (modulo 4)
      kdu_int32 delta_sched_4 = KD_DEC_SYNC_SCHED_R_BIT0 // R++
        + (4*KD_DEC_SYNC_SCHED_S0_BIT) // S += 4
        - U0_three-U1_one-U2_one-U3_one; // U0=U1=U2=U3=0
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        if ((old_sched & KD_DEC_SYNC_SCHED_A_MASK) == A_test)
          { // `which' is the first active stripe
            if ((old_sched & U1_three) == U1_one)
              { // Can advance by at least 2 stripes
                if ((old_sched & U2_three)==U2_one)
                  { // Can advance by at least 3 stripes
                    if ((old_sched & U3_three)==U3_one) // Can advance by 4
                      { // Advance by 4 stripes
                        new_sched = old_sched + delta_sched_4;
                        if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                            KD_DEC_SYNC_SCHED_P_MASK)
                          new_sched -=
                            4*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
                      }
                    else
                      { // Advance by 3 stripes
                        new_sched = old_sched + delta_sched_3;
                        if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                            KD_DEC_SYNC_SCHED_P_MASK)
                          new_sched -=
                            3*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
                      }
                  }
                else
                  { // Advance by 2 stripes
                    new_sched = old_sched + delta_sched_2;
                    if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                        KD_DEC_SYNC_SCHED_P_MASK)
                      new_sched -=
                        2*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
                  }
              }
            else
              { // Advance by 1 stripe only
                new_sched = old_sched + delta_sched_1;
                if ((old_sched + KD_DEC_SYNC_SCHED_P0_BIT) &
                    KD_DEC_SYNC_SCHED_P_MASK)
                  new_sched-=(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
              }
            new_sched &= ~KD_DEC_SYNC_SCHED_W_BIT;
          }
        else
          new_sched = old_sched - 2*U0_one; // Change U0 from 3 to 1
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & U0_three) == U0_three);
    }

  if (((old_sched ^ new_sched) & KD_DEC_SYNC_SCHED_S_MASK) == 0)
    { // S has not changed, so we have not incremented the R field.  We should
      // return immediately.
      return false; // S has not changed
    }

  assert(new_sched & KD_DEC_SYNC_SCHED_R_MASK);
  assert(new_sched & KD_DEC_SYNC_SCHED_S_MASK); // S now >= 1
  
  if (old_sched & KD_DEC_SYNC_SCHED_W_BIT)
    { // `pull' has requested a wakeup
      assert((old_sched & KD_DEC_SYNC_SCHED_S_MASK) == 0);
      env->signal_condition(sync_state->wakeup); // Does nothing if NULL
    }
  
  if (!(new_sched & KD_DEC_SYNC_SCHED_T_BIT))
    { 
      if ((old_sched & KD_DEC_SYNC_SCHED_L_BIT) &&
          !(new_sched & KD_DEC_SYNC_SCHED_U_MASK))
        { // Need to call `propagate_dependencies' with a
          // `delta_max_dependencies' argument of -1 -- calls to `pull' can
          // never block in the future and this is the first time that this
          // state has occurred.
          if (!(old_sched & KD_DEC_SYNC_SCHED_S_MASK))
            propagate_dependencies(-1,-1,env);
          else
            propagate_dependencies(0,-1,env);
        }
      else if (!(old_sched & KD_DEC_SYNC_SCHED_S_MASK))
        { // This queue previously presented a potential blocking condition
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
    new_sched = old_sched - KD_DEC_SYNC_SCHED_R_BIT0;
    assert(old_sched & KD_DEC_SYNC_SCHED_R_MASK);
    need_all_done = ((old_sched & (KD_DEC_SYNC_SCHED_L_BIT |
                                   KD_DEC_SYNC_SCHED_T_BIT)) &&
                     !(new_sched & (KD_DEC_SYNC_SCHED_R_MASK |
                                    KD_DEC_SYNC_SCHED_U_MASK)));
  } while (!(need_all_done ||
             sync_state->sched.compare_and_set(old_sched,new_sched)));
  if (!need_all_done)
    return false;
  
  if (((old_sched & KD_DEC_SYNC_SCHED_P_MASK) == KD_DEC_SYNC_SCHED_P_MASK) ||
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
/*                    kd_decoder::request_termination                        */
/*****************************************************************************/

void
  kd_decoder::request_termination(kdu_thread_entity *caller)
{
  // Start by setting the T bit and making sure that no new jobs get
  // scheduled by asynchronous calls to `update_dependencies'.
  kdu_int32 rel_Rp4 = 4*(KD_DEC_SYNC_SCHED_P0_BIT<<KD_DEC_QUANTUM_BITS);
  kdu_int32 old_sched, new_sched;
  do { // Enter compare-and-set loop
    old_sched = sync_state->sched.get();
    new_sched = old_sched | KD_DEC_SYNC_SCHED_T_BIT;
    if (!(old_sched & rel_Rp4))
      { // Set rel_Rp = 4 so as to ensure that there appear to be enough
        // resourced code-blocks for all jobs in all stripes to have been
        // scheduled, being careful to avoid the situation in which all
        // bits of the rel_P field could become non-zero, since this is
        // interpreted as the "dependencies closed" condition.  Note also
        // that the rel_P field will not be incremented further within
        // `update_dependencies', since the T flag is being set.
        new_sched = (new_sched & ~KD_DEC_SYNC_SCHED_P_MASK) + rel_Rp4;
      }
    new_sched |= (new_sched & (0xAA << KD_DEC_SYNC_SCHED_U_POS)) >> 1;
      // The above line ensures that any PARTIALLY SCHEDULABLE stripe
      // appears to now be FULLY SCHEDULABLE.
  } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
  
  // It is completely safe for us to continue accessing the object's member
  // variables here, because even if another thread invokes `all_done', the
  // object's resources cannot be cleaned up until the lock that is held
  // while this function is in-flight is released.
  
  // Next, figure out which decoding jobs never got scheduled and pretend
  // to have scheduled and performed them already.
  for (int n=0; n < num_stripes; n++)
    { 
      kdu_int32 new_status = (new_sched >> (KD_DEC_SYNC_SCHED_U_POS+2*n)) & 3;
      if (new_status < 2)
        continue;
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
              if (stripe_decoded(n,(kdu_thread_env *)caller))
                return; // Above func called `all_done' or arranged for it to
                        // be called.
            }
        }
    }
  
  // Finally, determine whether or not there are any jobs still in flight
  new_sched = sync_state->sched.get(); // Above calls to `stripe_encoded'
                                       // may have changed this value.
  if (!(new_sched & KD_DEC_SYNC_SCHED_INFLIGHT_MASK))
    { // No jobs are in flight, so it is not possible that any job thread
      // is accessing the object.
      if (((new_sched&KD_DEC_SYNC_SCHED_P_MASK) == KD_DEC_SYNC_SCHED_P_MASK) ||
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
/*                     kd_decoder::update_dependencies                       */
/*****************************************************************************/

bool
  kd_decoder::update_dependencies(kdu_int32 p_delta, kdu_int32 closure,
                                  kdu_thread_entity *caller)
{
  if (p_delta == 0)
    { 
      if (closure != 0)
        { // Special call to close out a previously pending detachment of this
          // queue from the block notification machinery.
          assert(sync_state->sched.get() & KD_DEC_SYNC_SCHED_T_BIT);
             // This call must have been induced via an attempt to prematurely
             // invoke `kdu_subband::detach_block_notifier', which can only
             // happen if the `T' bit is set as a result of a prior call to
             // `request_termination'.  If this test fails, it is not exactly
             // a disaster, but it means that we did not actually receive the
             // `closure'=1 value when the final update notification was
             // received, during normal operation.  This suggests either that
             // the background parsing machinery has produced erroneous
             // notification messages (not a disaster, but inefficient) or
             // that we have not interpreted them correctly and have gone
             // ahead and decoded blocks that might not have been pre-parsed
             // (also not a disaster, but inefficient).  To trace the
             // origin of an assert failure here more carefully, insert a
             // test for the condition (sync_state->dependencies_closed ||
             // (sync_state->sched & KD_DEC_SYNC_SCHED_T_BIT)) right before
             // the conditional call to `all_done' at the end of
             // the `stripe_decoded' function.
          assert(!(sync_state->sched.get() & KD_DEC_SYNC_SCHED_U_MASK));
          all_done(caller);
        }
    }
  else
    { 
      // Take local copies of the member variables that we will need to
      // pass to `schedule_new_jobs', since if `closure' is true, we will be
      // setting the the rel_P field within `sync_state->sched' to the
      // special value `KD_DEC_SYNC_SCHED_P_MASK', after which it is possible
      // that the present object gets cleaned up (but only if it turns out
      // that there are no jobs to schedule, so that the call to
      // `schedule_new_jobs' will not touch any member variables whatsoever).
      int local_num_stripes = this->num_stripes;
      int local_jobs_per_stripe = this->jobs_per_stripe;
      int local_jobs_per_quantum = this->jobs_per_quantum;
      
      // Now update `sync_state->sched'
      assert(p_delta > 0);
      p_delta <<= KD_DEC_SYNC_SCHED_P_POS;
      kdu_int32 old_sched, new_sched;
      kdu_int32 closure_mask = (closure)?KD_DEC_SYNC_SCHED_P_MASK:0;
      do { // Enter compare-and-set loop
        old_sched = sync_state->sched.get();
        new_sched = (old_sched + p_delta) | closure_mask;
        if (old_sched & KD_DEC_SYNC_SCHED_T_BIT)
          return true; // Do nothing here; termination is in progress
      } while (!sync_state->sched.compare_and_set(old_sched,new_sched));
      assert((old_sched & KD_DEC_SYNC_SCHED_P_MASK) !=
             KD_DEC_SYNC_SCHED_P_MASK); // Else `closure' happened already!!
      assert(!((new_sched ^ old_sched) & ~(KD_DEC_SYNC_SCHED_P_MASK)));
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
/*                      kd_decoder::schedule_new_jobs                        */
/*****************************************************************************/

void
  kd_decoder::schedule_new_jobs(kdu_int32 old_sched, kdu_int32 new_sched,
                                kdu_thread_entity *caller,
                                int local_num_stripes,
                                int local_jobs_per_stripe,
                                int local_quantum)
{
  // Note: It is important that this function is non-virtual and does not
  // touch any member variables of the `kd_decoder' object unless it is
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
  
  //bool more_scheduling_required = !(new_sched & KD_DEC_SYNC_SCHED_L_BIT);
  int n, s=(new_sched >> KD_DEC_SYNC_SCHED_A_POS) & 3; // First active stripe
  for (n=0; n < local_num_stripes; n++)
    { 
      kdu_int32 new_status = (new_sched >> (KD_DEC_SYNC_SCHED_U_POS+2*s)) & 3;
      if (new_status == 0) 
        break; // No more active stripes
      if (new_status >= 2)
        { // Otherwise stripe has been fully decoded already
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
/*                              kd_decoder::pull                             */
/*****************************************************************************/

void
  kd_decoder::pull(kdu_line_buf &line, kdu_thread_env *env)
{
  if (line.get_width() <= pull_offset)
    return;
  
  while (!fully_started)
    start(env);
  assert((env == NULL) ||
         !(sync_state->sched.get() & KD_DEC_SYNC_SCHED_T_BIT));
     // `pull' calls should not arrive after `request_termination'!
  if (pull_state->active_lines_left == 0)
    { // Need to populate the `active_pull_stripe'
      assert(pull_state->subband_lines_left > 0);
      if (env == NULL)
        { // Single-threaded
          assert(num_stripes == 1);
          for (int g=0; g < jobs_per_stripe; g++)
            { 
              kd_decoder_job *job = (jobs[0])[g];
              job->do_job(NULL);
            }
        }
      else
        { // Block decoding is performed asynchronously; we might need to block
          while ((sync_state->sched.get() & KD_DEC_SYNC_SCHED_S_MASK) == 0)
            { // No fully decoded stripes are available at present; we
              // should never have to go through this loop multiple times,
              // but it doesn't hurt to make the check for S > 0 again.
              sync_state->wakeup = env->get_condition();
              kdu_int32 old_sched, new_sched;
              do { // Enter compare-and-set loop
                old_sched = sync_state->sched.get();
                new_sched = old_sched | KD_DEC_SYNC_SCHED_W_BIT;
              } while (((old_sched & KD_DEC_SYNC_SCHED_S_MASK) == 0) &&
                       !sync_state->sched.compare_and_set(old_sched,
                                                          new_sched));
              if ((old_sched & KD_DEC_SYNC_SCHED_S_MASK) == 0)
                env->wait_for_condition("pull line");// Safe to wait now
              sync_state->wakeup = NULL;
            }
          
          // Advance number of block rows that will be required in the
          // future from the background codestream processing machinery.
          // Should not advance by more than one at a time here or else
          // it is possible that jobs will be scheduled in a non-ideal
          // fashion.
          if (pull_state->last_stripes_requested <
              pull_state->num_stripes_in_subband)
            { 
              pull_state->last_stripes_requested++;
              band.advance_block_rows_needed(this,1,KD_DEC_QUANTUM_BITS,
                                             (kdu_uint32)(jobs_per_quantum <<
                                                          log2_job_blocks),
                                             env);
            }
        }
      pull_state->active_lines_left = pull_state->next_stripe_height;
      pull_state->subband_lines_left -= pull_state->active_lines_left;
      pull_state->next_stripe_height = this->nominal_block_height;
      if (pull_state->next_stripe_height > pull_state->subband_lines_left)
        pull_state->next_stripe_height = pull_state->subband_lines_left;
      assert(pull_state->active_pull_line == 0);
    }
  
  // Transfer data
  int line_idx = (pull_state->active_pull_stripe*pull_state->stripe_height +
                  pull_state->active_pull_line);
  int buf_offset = pull_state->buffer_offset;
  assert(line.get_width() == (subband_cols+pull_offset));
  if (using_shorts)
    { 
      if (((pull_offset | buf_offset) != 0) ||
          !line.raw_exchange(pull_state->lines16[line_idx],raw_line_width))
        memcpy(line.get_buf16()+pull_offset,
               pull_state->lines16[line_idx]+buf_offset,
               (size_t)(subband_cols<<1));
    }
  else
    { 
      if (((pull_offset | buf_offset) != 0) ||
          !line.raw_exchange(pull_state->lines32[line_idx],raw_line_width))
        memcpy(line.get_buf32()+pull_offset,
               pull_state->lines32[line_idx]+buf_offset,
               (size_t)(subband_cols<<2));
    }
  
  // Update pull status
  pull_state->active_pull_line++;
  pull_state->active_lines_left--;
  assert(pull_state->active_lines_left >= 0);
  
  // Determine what changes we need to make to `sync_state->sched'
  kdu_int32 sched_inc=0; // Account for schedulability and changes in S
  if (pull_state->active_lines_left == 0)
    { // Whole stripe just completed
      
      // Start by copying the line buffers in the stripe just emptied back
      // to the relevant `kd_decoder_job::lines16/32' array -- we need to
      // do this because the line buffers might have been changed by
      // exchange operations.
      int n;
      kdu_sample16 **dst_lines16 =
        (jobs[pull_state->active_pull_stripe])[0]->lines16;
      kdu_sample16 **src_lines16 = pull_state->lines16 +
        pull_state->active_pull_stripe*pull_state->stripe_height;
      for (n=0; n < pull_state->active_pull_line; n++)
        dst_lines16[n] = src_lines16[n];
      
      pull_state->active_pull_line = 0;
      if (env == NULL)
        { 
          assert(num_stripes == 1); // No need for scheduling
          return;
        }
      int stripe_idx = pull_state->active_pull_stripe;
      if ((++pull_state->active_pull_stripe) == num_stripes)
        pull_state->active_pull_stripe = 0;
      pull_state->num_stripes_pulled++;
      if (pull_state->num_stripes_pulled==pull_state->num_stripes_in_subband)
        { 
          assert(pull_state->next_stripe_height == 0);
          return;
        }
      pull_state->active_sched_stripe = stripe_idx;
      assert(pull_state->next_stripe_height  > 0);
      assert(pull_state->partial_quanta_remaining == 0);
        // Partial scheduling should be completed well before end of a stripe
      sched_inc -= KD_DEC_SYNC_SCHED_S0_BIT; // Decrement S count
      if (pull_state->num_stripes_released_to_decoder <
          pull_state->num_stripes_in_subband)
        { // The `stripe_idx' stripe needs full or partial scheduling
          pull_state->num_stripes_released_to_decoder++;
          if (pull_state->num_stripes_released_to_decoder ==
              pull_state->num_stripes_in_subband)
            sched_inc += KD_DEC_SYNC_SCHED_L_BIT;
          kdu_interlocked_int32 *cnt =
            (jobs[stripe_idx])[0]->pending_stripe_jobs;
          assert(cnt->get() == 0);
          cnt->set(jobs_per_stripe);
          if (lines_per_scheduled_quantum > 0)
            pull_state->partial_quanta_remaining =
              (pull_state->next_stripe_height-quantum_scheduling_offset) /
              lines_per_scheduled_quantum;
          if (pull_state->partial_quanta_remaining <= 0)
            { // Make the new stripe FULLY SCHEDULABLE immediately
              pull_state->partial_quanta_remaining = 0;
              sched_inc += 3 << (KD_DEC_SYNC_SCHED_U_POS+2*stripe_idx);
            }
          else
            { // Make the new stripe PARTIALLY_SCHEDULABLE
              int q = quanta_per_stripe - pull_state->partial_quanta_remaining;
              q = (q < 0)?0:q;
              assert(q < (1<<KD_DEC_QUANTUM_BITS));
              sched_inc += 2 << (KD_DEC_SYNC_SCHED_U_POS+2*stripe_idx);
              sched_inc += q << KD_DEC_SYNC_SCHED_Q_POS;
            }
        }
    }
  else
    { // We may need to schedule further job quanta for a partially
      // schedulable stripe
      int stripe_idx = pull_state->active_sched_stripe;
      int old_q = pull_state->partial_quanta_remaining;
      if (old_q == 0)
        return;
      int new_q = 0;
      if (lines_per_scheduled_quantum > 0)
        { 
          new_q = (pull_state->active_lines_left-quantum_scheduling_offset) /
          lines_per_scheduled_quantum;
          if (old_q == new_q)
            return;
        }
      pull_state->partial_quanta_remaining = new_q;
      old_q = quanta_per_stripe - old_q;  old_q = (old_q < 0)?0:old_q;
      new_q = quanta_per_stripe - new_q;  new_q = (new_q < 0)?0:new_q;
        // The above two lines make `old_q' and `new_q' equal to the number
        // of initial quanta from the stripe that could previously and now
        // can be marked as schedulable.
      if (new_q >= quanta_per_stripe)
        { // Make the PARTIALLY SCHEDULABLE stripe FULLY SCHEDULABLE
          pull_state->partial_quanta_remaining = new_q = 0;
          sched_inc += 1 << (KD_DEC_SYNC_SCHED_U_POS+2*stripe_idx);
        }
      sched_inc += (new_q-old_q) << KD_DEC_SYNC_SCHED_Q_POS;
    }
  
  assert(env != NULL);
  if (sched_inc == 0)
    return; // Nothing more to do
  
  kdu_int32 old_sched = sync_state->sched.exchange_add(sched_inc);
  kdu_int32 new_sched = old_sched + sched_inc;
#ifdef _DEBUG
  assert(!(old_sched & KD_DEC_SYNC_SCHED_T_BIT)); // Just to be sure
  int q_val = pull_state->partial_quanta_remaining;
  if (q_val != 0)
    { q_val = quanta_per_stripe - q_val; q_val = (q_val<0)?0:q_val; }
  assert(((new_sched & KD_DEC_SYNC_SCHED_Q_MASK) >>
          KD_DEC_SYNC_SCHED_Q_POS) == q_val);
  int s = pull_state->active_sched_stripe;
  assert((q_val==0) || (((new_sched>>(KD_DEC_SYNC_SCHED_U_POS+2*s))&3)==2));
#endif // _DEBUG
  schedule_new_jobs(old_sched,new_sched,env,num_stripes,
                    jobs_per_stripe,jobs_per_quantum);
  if (!(new_sched & KD_DEC_SYNC_SCHED_S_MASK))
    propagate_dependencies(1,0,env); // Next `pull' call might block
}

/*****************************************************************************/
/*                            kd_decoder::init                               */
/*****************************************************************************/

void
  kd_decoder::init(kdu_subband band, kdu_sample_allocator *allocator,
                   bool use_shorts, float normalization, int pull_offset,
                   kdu_thread_env *env, kdu_thread_queue *env_queue,
                   int flags)
{
  assert(this->allocator == NULL);
  this->band = band;
  this->pull_offset = pull_offset;
  this->K_max = (kdu_int16) band.get_K_max();
  this->K_max_prime = (kdu_int16) band.get_K_max_prime();
  assert(K_max_prime >= K_max);
  this->reversible = band.get_reversible();
  this->using_shorts = use_shorts;
  this->starting = this->fully_started = false;
  this->delta = band.get_delta() * normalization;
  
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
  this->jobs_per_quantum = 1 + ((jobs_per_stripe-1) >> KD_DEC_QUANTUM_BITS);
  this->quanta_per_stripe = (kdu_int16)
    (1 + ((jobs_per_stripe-1) / jobs_per_quantum));
  assert(quanta_per_stripe <= (1<<KD_DEC_QUANTUM_BITS));
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
      
      if ((quanta_per_stripe > 1) && (num_stripes > 2) && !is_top)
        this->lines_per_scheduled_quantum = (kdu_int16)
          (1 + ((nominal_block_height-1) / quanta_per_stripe));
      if (!env->attach_queue(this,env_queue,KDU_CODING_THREAD_DOMAIN))
        { 
          KDU_ERROR_DEV(e,0x22081102); e <<
          KDU_TXT("Failed to create thread queue when constructing "
                  "`kdu_decoder' object.  One possible cause is that "
                  "the thread group might not have been created first using "
                  "`kdu_thread_env::create', before passing its reference to "
                  "`kdu_decoder', or an exception may have occurred.  Another "
                  "possible (highly unlikely) cause is that too many thread "
                  "working domains are in use.");
        }
      band.attach_block_notifier(this,env);
      propagate_dependencies(1,1,env);
    }
  
  // Next figure out stripe heights and the memory required by all jobs.
  // Note that the current implementation requires all stripes to have the
  // same height, except possibly the last one, even though this might be
  // wasteful of memory.
  size_t decoder_job_mem = 0;
  int s, sum_stripe_heights=0, stripe_heights[4]={0,0,0,0};
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
      stripe_heights[s] = max_height;
      sum_stripe_heights += max_height;
      decoder_job_mem +=
        kd_decoder_job::calculate_size(max_height,jobs_per_stripe);
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
  this->allocator_bytes = decoder_job_mem + line_buf_mem +
    kd_decoder_pull_state::calculate_size(num_stripes,stripe_heights,
                                          job_ptr_mem);
  if (env != NULL)
    { // Allocate extra space for synchronization variables
      this->allocator_bytes += kd_decoder_sync_state::calculate_size() +
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
  simd_block_zero = NULL;
  simd_block_xfer = NULL;
  bool tr, vf, hf;
  band.get_block_geometry(tr,vf,hf);
  int nominal_width = nominal_block_width;
  if (blocks_across == 1)
    nominal_width = 2*first_block_width-1;
    // NB: the macros below use the `nominal_width' to determine whether a
    // vector accelerated transfer/zeroing function is appropriate.  A function
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
  
  if (using_shorts)
    { // 16-bit sample processing
      KD_SET_SIMD_FUNC_BLOCK_ZERO16(simd_block_zero,nominal_width);
      if (reversible)
        { 
          KD_SET_SIMD_FUNC_BLOCK_XFER_REV16(simd_block_xfer,tr,vf,hf,K_max,
                                            nominal_width);
        }
      else
        { 
          KD_SET_SIMD_FUNC_BLOCK_XFER_IRREV16(simd_block_xfer,tr,vf,hf,K_max,
                                              nominal_width);
        }
    }
  else
    { // 32-bit sample processing
      KD_SET_SIMD_FUNC_BLOCK_ZERO32(simd_block_zero,nominal_width);
      if (reversible)
        { 
          KD_SET_SIMD_FUNC_BLOCK_XFER_REV32(simd_block_xfer,tr,vf,hf,
                                            nominal_width);
        }
      else
        { 
          KD_SET_SIMD_FUNC_BLOCK_XFER_IRREV32(simd_block_xfer,tr,vf,hf,
                                              nominal_width);
        }
    }
#endif // KDU_SIMD_OPTIMIZATIONS
}

/*****************************************************************************/
/*                             kd_decoder::start                             */
/*****************************************************************************/

bool
  kd_decoder::start(kdu_thread_env *env)
{
  if (fully_started || (subband_cols==0) || (subband_rows==0))
    {
      starting = fully_started = true;
      return true;
    }
  if (!starting)
    { // This is the first call to this function since `init'
      starting = true;
      
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
          stripe_heights[s] = max_height;
        }
      
      // Now assign memory and initialize associated objects
      kdu_byte *alloc_block = (kdu_byte *)
      allocator->alloc_block(allocator_offset,allocator_bytes);
      kdu_byte *alloc_lim=alloc_block+allocator_bytes;
      pull_state = (kd_decoder_pull_state *) alloc_block;
      size_t job_ptr_mem = ((size_t) jobs_per_stripe) * sizeof(void *);
      alloc_block +=
        kd_decoder_pull_state::calculate_size(num_stripes,stripe_heights,
                                              job_ptr_mem);
      assert(alloc_block <= alloc_lim); // Avoid possibility of mem overwrite
      pull_state->init(num_stripes,stripe_heights,first_block_height,
                       subband_rows,block_indices.size.y,buffer_offset);
      jobs[0] = (kd_decoder_job **)
        (alloc_block - job_ptr_mem * (size_t) num_stripes);
      for (s=1; s < num_stripes; s++)
        jobs[s] = jobs[s-1] + jobs_per_stripe;
      assert((jobs[num_stripes-1] + jobs_per_stripe) ==
             (kd_decoder_job **) alloc_block);
      kdu_interlocked_int32 *pending_stripe_jobs[4]={NULL,NULL,NULL,NULL}; 
      if (env != NULL)
        { 
          sync_state = (kd_decoder_sync_state *) alloc_block;
          alloc_block += kd_decoder_sync_state::calculate_size();
          if (alloc_block > alloc_lim)
            assert(0); // Avoid possibility of mem overwrite
          sync_state->init();
          for (s=0; s < num_stripes; s++)
            { 
              pending_stripe_jobs[s] = (kdu_interlocked_int32 *) alloc_block;
              alloc_block += KDU_MAX_L2_CACHE_LINE;
              assert(alloc_block <= alloc_lim);
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
          kd_decoder_job *job, *prev_stripe_job=NULL;
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
              job = (jobs[s])[j] = (kd_decoder_job *) alloc_block;
              alloc_block += job->init(stripe_heights[s],prev_stripe_job);
              assert(alloc_block <= alloc_lim);
              job->band = this->band;
              job->owner = this;
              job->block_decoder = &(this->block_decoder);
#ifdef KDU_SIMD_OPTIMIZATIONS
              job->simd_block_zero = this->simd_block_zero;
              job->simd_block_xfer = this->simd_block_xfer;
#endif
              job->K_max = this->K_max;
              job->K_max_prime = this->K_max_prime;
              job->reversible = this->reversible;
              job->using_shorts = this->using_shorts;
              job->delta = this->delta;
              job->num_stripes = this->num_stripes;
              job->which_stripe = s;
              job->grp_offset = grp_offset;
              job->grp_width = width;
              job->grp_blocks = blocks;
              job->first_block_idx = first_block_idx;
              job->pending_stripe_jobs = pending_stripe_jobs[s];
              assert(job->lines16 != NULL);
            }
        }
      
      int alloc_line_samples =
        (raw_line_width+buffer_offset+alignment-1) & ~(alignment-1);
      size_t line_buf_mem=((size_t) alloc_line_samples)<<((using_shorts)?1:2);
      size_t optional_align = (-(int)line_buf_mem) & (KDU_MAX_L2_CACHE_LINE-1);
      if (line_buf_mem > (optional_align*8))
        line_buf_mem += optional_align;
      
      for (s=0; s < num_stripes; s++)
        { 
          kd_decoder_job *job = (jobs[s])[0];
          kdu_sample16 **lines16 = pull_state->lines16 + s*stripe_heights[0];
          for (int m=0; m < stripe_heights[s]; m++)
            { 
              lines16[m] = job->lines16[m] = (kdu_sample16 *) alloc_block;
              alloc_block += line_buf_mem;
            }
        }
      if (alloc_block != alloc_lim)
        { assert(0);
          KDU_ERROR_DEV(e,0x13011202); e <<
          KDU_TXT("Memory allocation/assignment error in `kd_decoder::start'; "
                  "pre-allocated memory block has different size to actual "
                  "required memory block!  Compile and run in debug mode to "
                  "catch this error.");
        }
      
      if (env != NULL)
        bind_jobs((kdu_thread_job **)(jobs[0]),jobs_per_stripe*num_stripes);
    }
  
  // Now we have finished all the allocation and initialization of objects,
  // but we may need to perform some job scheduling
  if (env == NULL)
    { // Nothing to schedule 
      fully_started = true;
      return true;
    }
  
  int num_requested = pull_state->last_stripes_requested;
  int num_released = pull_state->num_stripes_released_to_decoder;
  int num_total = pull_state->num_stripes_in_subband;
  if (num_requested < num_total)
    { // We want to build up the requests for pre-parsed code-block rows so
      // that they get 2 stripes ahead of the last one that is released for
      // scheduling.
      assert(num_requested <
             (num_released+KD_DEC_MAX_STRIPES_REQUESTED_AHEAD));
      band.advance_block_rows_needed(this,1,KD_DEC_QUANTUM_BITS,
                                     (kdu_uint32)(jobs_per_quantum <<
                                                  log2_job_blocks),env);
      pull_state->last_stripes_requested = (++num_requested);
    }
  if (num_released < num_stripes)
    { // Release another stripe for scheduling.
      assert(num_released < num_total);
      int stripe_idx = num_released;
      pull_state->num_stripes_released_to_decoder = (++num_released);
      pull_state->active_sched_stripe = stripe_idx;
      if ((num_released == num_stripes) && (lines_per_scheduled_quantum > 0))
        pull_state->partial_quanta_remaining =
          (pull_state->next_stripe_height-quantum_scheduling_offset) /
          lines_per_scheduled_quantum;
      kdu_int32 sched_inc = 0;
      if (pull_state->partial_quanta_remaining <= 0)
        { // Make the new stripe FULLY SCHEDULABLE immediately
          pull_state->partial_quanta_remaining = 0;
          sched_inc += 3 << (KD_DEC_SYNC_SCHED_U_POS+2*stripe_idx);
        }
      else
        { // Make the new stripe PARTIALLY_SCHEDULABLE
          int q = quanta_per_stripe - pull_state->partial_quanta_remaining;
          q = (q < 0)?0:q;
          assert(q < (1<<KD_DEC_QUANTUM_BITS));
          sched_inc += 2 << (KD_DEC_SYNC_SCHED_U_POS+2*stripe_idx);
          sched_inc += q << KD_DEC_SYNC_SCHED_Q_POS;
        }
      if (num_released == num_total)
        sched_inc += KD_DEC_SYNC_SCHED_L_BIT;
      (jobs[stripe_idx])[0]->pending_stripe_jobs->set(jobs_per_stripe);

      kdu_int32 old_sched = sync_state->sched.exchange_add(sched_inc);
      kdu_int32 new_sched = old_sched + sched_inc;
      schedule_new_jobs(old_sched,new_sched,env,num_stripes,
                        jobs_per_stripe,jobs_per_quantum);
    }
  
  this->fully_started =
    (num_released == num_stripes) &&
    ((num_requested == num_total) ||
     (num_requested == (num_released+KD_DEC_MAX_STRIPES_REQUESTED_AHEAD)));
  
  return this->fully_started;
}
