/*****************************************************************************/
// File: neon_multi_component_local.cpp [scope = CORESYS/TRANSFORMS]
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
   Provides SIMD implementations to accelerate certain multi-component
transform functions, based on ARM NEON instrinsics.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>
#include <assert.h>

using namespace kdu_core;


namespace kd_core_simd {

/*****************************************************************************/
/* EXTERN                 neoni_multi_line_rev_copy                          */
/*****************************************************************************/
  
void neoni_multi_line_rev_copy(void *in_buf, void *out_buf, int num_samples,
                               bool using_shorts, int rev_offset)
{
  if (using_shorts)
    { 
      int nvecs = (num_samples+7)>>3;
      kdu_int16 *sp=(kdu_int16 *)in_buf, *dp=(kdu_int16 *)out_buf;
      kdu_int16 *dp_lim = dp + 8*nvecs;
      int16x8_t val, vec_off = vdupq_n_s16((kdu_int16) rev_offset);
      for (; dp < dp_lim; sp+=8, dp+=8)
        { 
          val = vld1q_s16(sp);
          val = vqaddq_s16(val,vec_off);
          vst1q_s16(dp,val);
        }
    }
  else
    { 
      int nvecs = (num_samples+3)>>2;
      kdu_int32 *sp=(kdu_int32 *)in_buf, *dp=(kdu_int32 *)out_buf;
      kdu_int32 *dp_lim = dp + 4*nvecs;
      int32x4_t val, vec_off = vdupq_n_s32(rev_offset);
      for (; dp < dp_lim; sp+=4, dp+=4)
        { 
          val = vld1q_s32(sp);
          val = vaddq_s32(val,vec_off);
          vst1q_s32(dp,val);
        }
    }
}  
  
/*****************************************************************************/
/* EXTERN                neoni_multi_line_irrev_copy                         */
/*****************************************************************************/
  
void neoni_multi_line_irrev_copy(void *in_buf, void *out_buf, int num_samples,
                                 bool using_shorts, float irrev_offset)
{
  if (using_shorts)
    { 
      int nvecs = (num_samples+7)>>3;
      kdu_int16 *sp=(kdu_int16 *)in_buf, *dp=(kdu_int16 *)out_buf;
      kdu_int16 *dp_lim = dp + 8*nvecs;
      kdu_int16 off = (kdu_int16)
        floorf(0.5f + irrev_offset*(1<<KDU_FIX_POINT));
      int16x8_t val, vec_off = vdupq_n_s16(off);
      for (; dp < dp_lim; sp+=8, dp+=8)
        { 
          val = vld1q_s16(sp);
          val = vqaddq_s16(val,vec_off);
          vst1q_s16(dp,val);
        }
    }
  else
    { 
      int nvecs = (num_samples+3)>>2;
      float *sp=(float *)in_buf, *dp=(float *)out_buf;
      float *dp_lim = dp + 4*nvecs;
      float32x4_t val, vec_off = vdupq_n_f32(irrev_offset);
      for (; dp < dp_lim; sp+=4, dp+=4)
        { 
          val = vld1q_f32(sp);
          val = vaddq_f32(val,vec_off);
          vst1q_f32(dp,val);
        }
    }
}
  
/*****************************************************************************/
/* EXTERN                 neoni_multi_matrix_float                           */
/*****************************************************************************/

void neoni_multi_matrix_float(void **in_bufs, void **out_bufs,
                              int num_samples, int num_inputs,
                              int num_outputs, float *coeffs, float *offsets)
{
  int nvecs=(num_samples+3)>>2;
  for (int m=0; m < num_outputs; m++)
    { 
      float *dpp=(float *)(out_bufs[m]);
      if (dpp == NULL)
        continue; // Output not required
      float *dp, *dp_lim=dpp+4*nvecs;
      float32x4_t acc, val = vdupq_n_f32(offsets[m]);
      for (dp=dpp; dp < dp_lim; dp+=4)
        vst1q_f32(dp,val);
      for (int n=0; n < num_inputs; n++)
        { 
          float factor = *(coeffs++);
          float *sp=(float *)(in_bufs[n]);
          if ((sp == NULL) || (factor == 0.0f))
            continue; // Input irrelevant
          float32x4_t vec_factor = vdupq_n_f32(factor);
          for (dp=dpp; dp < dp_lim; dp+=4, sp+=4)
            { 
              val = vld1q_f32(sp);  acc = vld1q_f32(dp);
              val = vmlaq_f32(acc,val,vec_factor);
              vst1q_f32(dp,acc);
            }
        }
    }
}
  
/*****************************************************************************/
/* EXTERN                 neoni_multi_matrix_fix16                           */
/*****************************************************************************/

void neoni_multi_matrix_fix16(void **in_bufs, void **out_bufs, kdu_int32 *acc,
                              int num_samples, int num_inputs, int num_outputs,
                              kdu_int16 *coeffs, int downshift, float *offsets)
{
  // Start by aligning the accumulator buffer
  int align_off = (- _addr_to_kdu_int32(acc)) & 15; // byte offset to align 16
  assert((align_off & 3) == 0);
  acc += (align_off>>2);
  
  // Now compute the number of input vectors to process on each line
  int nvecs=(num_samples+7)>>3; // Input vectors each have 8 short words
  for (int m=0; m < num_outputs; m++)
    { 
      kdu_int16 *dp=(kdu_int16 *)(out_bufs[m]);
      if (dp == NULL)
        continue; // Output not required
      kdu_int32 *app=acc;
      kdu_int32 *ap, *ap_lim=app+8*nvecs;
      int32x4_t zero = vdupq_n_s32(0);
      for (ap=app; ap < ap_lim; ap += 8)
        { vst1q_s32(ap,zero); vst1q_s32(ap+4,zero); }
      for (int n=0; n < num_inputs; n++)
        { 
          kdu_int16 factor = *(coeffs++);
          kdu_int16 *sp=(kdu_int16 *)(in_bufs[n]);
          if ((sp == NULL) || (factor == 0))
            continue; // Input irrelevant
          int16x4_t vec_factor = vdup_n_s16(factor);
          int16x4_t in0, in1;
          int32x4_t v0, v1;
          for (ap=app; ap < ap_lim; ap+=8, sp+=8)
            { 
              in0 = vld1_s16(sp);  in1 = vld1_s16(sp+4);
              v0 = vld1q_s32(ap);  v1 = vld1q_s32(ap+4);
              v0 = vmlal_s16(v0,in0,vec_factor);
              v1 = vmlal_s16(v1,in1,vec_factor);
              vst1q_s32(ap,v0);    vst1q_s32(ap+4,v1);
            }
        }
      kdu_int32 off = (kdu_int32)
        floorf(0.5f + offsets[m]*(1<<KDU_FIX_POINT));
      off <<= downshift;
      off += (1 << downshift)>>1;
      int32x4_t vec_off = vdupq_n_s32(off);
      int32x4_t vec_shift = vdupq_n_s32(-downshift); // Using signed upshift
      int32x4_t v0, v1;
      int16x8_t v_out;
      for (ap=app; ap < ap_lim; ap+=8, dp+=8)
        { 
          v0 = vld1q_s32(ap);            v1 = vld1q_s32(ap+4);
          v0 = vaddq_s32(v0,vec_off);    v1 = vaddq_s32(v1,vec_off);
          v0 = vshlq_s32(v0,vec_shift);  v1 = vshlq_s32(v1,vec_shift);
          v_out = vcombine_s16(vqmovn_s32(v0),vqmovn_s32(v1));
          vst1q_s16(dp,v_out);
        }
    }
}
  
/*****************************************************************************/
/* EXTERN                    neoni_smag_int32                                */
/*****************************************************************************/
  
void neoni_smag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                      int precision, bool src_absolute, bool dst_absolute)
{
  assert(precision <= 32);
  kdu_int32 min_val = ((kdu_int32) -1) << (precision-1);
  kdu_int32 max_val = ~min_val;
  if (!src_absolute)
    { // Synthesis conversion from floats to absolute ints
      float *sp = (float *)src;
      kdu_int32 *dp = dst;
      float32x4_t vec_scale = vdupq_n_f32(kdu_pwrof2f(precision));
      float32x4_t vec_fmin = vdupq_n_f32((float)min_val);
      float32x4_t vec_fmax = vdupq_n_f32((float)max_val);
      int32x4_t vec_min = vdupq_n_s32(min_val);
      int32x4_t vec_zero = vdupq_n_s32(0);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          float32x4_t fval = vld1q_f32(sp);
          fval = vmulq_f32(fval,vec_scale);
          fval = vmaxq_f32(fval,vec_fmin);
          fval = vminq_f32(fval,vec_fmax);
          int32x4_t int_val = vcvtq_s32_f32(fval);
          int32x4_t neg_mask = vcltq_s32(int_val,vec_zero);
          int_val = veorq_s32(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = vandq_s32(neg_mask,vec_min); // Leaves min_val or 0
          int_val = vorrq_s32(int_val,neg_mask);
          vst1q_s32(dp,int_val);
        }
    }
  else if (!dst_absolute)
    { // Analysis conversion from absolute ints to floats
      kdu_int32 *sp = (kdu_int32 *)src;
      float *dp = (float *)dst;
      float32x4_t vec_scale = vdupq_n_f32(kdu_pwrof2f(-precision));
      int32x4_t vec_min = vdupq_n_s32(min_val);
      int32x4_t vec_max = vdupq_n_s32(max_val);
      int32x4_t vec_zero = vdupq_n_s32(0);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          int32x4_t int_val = vld1q_s32(sp);
          int32x4_t neg_mask = vcltq_s32(int_val,vec_zero);
          int_val = vmaxq_s32(int_val,vec_min);
          int_val = vminq_s32(int_val,vec_max);
          int_val = veorq_s32(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = vandq_s32(neg_mask,vec_min); // Leaves min_val or 0
          int_val = vorrq_s32(int_val,neg_mask);
          float32x4_t fval = vcvtq_f32_s32(int_val);
          fval = vmulq_f32(fval,vec_scale);
          vst1q_f32(dp,fval);
        }
    }
  else
    { // Analysis/Synthesis conversion between absolute ints
      kdu_int32 *sp=(kdu_int32 *)src, *dp=(kdu_int32 *)dst;
      int32x4_t vec_min = vdupq_n_s32(min_val);
      int32x4_t vec_max = vdupq_n_s32(max_val);
      int32x4_t vec_zero = vdupq_n_s32(0);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          int32x4_t int_val = vld1q_s32(sp);
          int32x4_t neg_mask = vcltq_s32(int_val,vec_zero);
          int_val = vmaxq_s32(int_val,vec_min);
          int_val = vminq_s32(int_val,vec_max);
          int_val = veorq_s32(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = vandq_s32(neg_mask,vec_min); // Leaves min_val or 0
          int_val = vorrq_s32(int_val,neg_mask);
          vst1q_s32(dp,int_val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                    neoni_umag_int32                                */
/*****************************************************************************/

void neoni_umag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                      int precision, bool src_absolute, bool dst_absolute)
{
  assert(precision <= 32);
  kdu_int32 min_val = ((kdu_int32) -1) << (precision-1);
  kdu_int32 max_val = ~min_val;
  if (!src_absolute)
    { // Synthesis conversion from floats to absolute ints
      float *sp = (float *)src;
      kdu_int32 *dp = dst;
      float32x4_t vec_scale = vdupq_n_f32(kdu_pwrof2f(precision));
      float32x4_t vec_fmin = vdupq_n_f32((float)min_val);
      float32x4_t vec_fmax = vdupq_n_f32((float)max_val);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          float32x4_t fval = vld1q_f32(sp);
          fval = vmulq_f32(fval,vec_scale);
          fval = vmaxq_f32(fval,vec_fmin);
          fval = vminq_f32(fval,vec_fmax);
          int32x4_t int_val = vcvtq_s32_f32(fval);
          vst1q_s32(dp,int_val);
        }
    }
  else if (!dst_absolute)
    { // Analysis conversion from absolute ints to floats
      kdu_int32 *sp = (kdu_int32 *)src;
      float *dp = (float *)dst;
      float32x4_t vec_scale = vdupq_n_f32(kdu_pwrof2f(-precision));
      int32x4_t vec_min = vdupq_n_s32(min_val);
      int32x4_t vec_max = vdupq_n_s32(max_val);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          int32x4_t int_val = vld1q_s32(sp);
          int_val = vmaxq_s32(int_val,vec_min);
          int_val = vminq_s32(int_val,vec_max);
          float32x4_t fval = vcvtq_f32_s32(int_val);
          fval = vmulq_f32(fval,vec_scale);
          vst1q_f32(dp,fval);
        }
    }
  else
    { // Analysis/Synthesis conversion between absolute ints
      kdu_int32 *sp=(kdu_int32 *)src, *dp=(kdu_int32 *)dst;
      int32x4_t vec_min = vdupq_n_s32(min_val);
      int32x4_t vec_max = vdupq_n_s32(max_val);
      for (; num_samples > 0; num_samples-=4, sp+=4, dp+=4)
        { 
          int32x4_t int_val = vld1q_s32(sp);
          int_val = vmaxq_s32(int_val,vec_min);
          int_val = vminq_s32(int_val,vec_max);
          vst1q_s32(dp,int_val);
        }
    }
}

  
} // namespace kd_core_simd

#endif // NEON intrinsics
