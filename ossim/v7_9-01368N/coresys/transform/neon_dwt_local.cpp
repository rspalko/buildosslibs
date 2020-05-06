/*****************************************************************************/
// File: neon_dwt_local.cpp [scope = CORESYS/TRANSFORMS]
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
   Provides SIMD implementations to accelerate DWT analsysis/synthesis.  This
source file is required to implement versions of the DWT processing operators
that are based on the ARM-NEON intrinsics.
******************************************************************************/
#include "transform_base.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>

using namespace kd_core_local;

namespace kd_core_simd {
  
// The following constants are defined in all DWT accelerator source files
#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

// The factors below are used for fixed-point implementation of the 9/7
// transform, based on the VQRDMULHQ instruction, which effectively
// multiplies by the 16-bit integer factor then divides by 2^15 with a
// rounding offset.
static kdu_int16 neon_w97_rem[4]; // See `neon_dwt_local_static_init'


/* ========================================================================= */
/*                         Safe Static Initializers                          */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  neon_dwt_local_static_init                        */
/*****************************************************************************/

void neon_dwt_local_static_init()
{ // Static initializers are potentially dangerous, so we initialize here
  kdu_int16 w97_rem[4] =
    { (kdu_int16) floor(0.5 + (W97_FACT_0+1.0)*(double)(1<<15)),
      (kdu_int16) floor(0.5 + W97_FACT_1*(double)(1<<16)),
      (kdu_int16) floor(0.5 + W97_FACT_2*(double)(1<<15)),
      (kdu_int16) floor(0.5 + W97_FACT_3*(double)(1<<15))};
  for (int i=0; i < 4; i++)
    neon_w97_rem[i] = w97_rem[i];
}


/* ========================================================================= */
/*                           Interleave Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    neoni_interleave_16                             */
/*****************************************************************************/

void neoni_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                         kdu_int16 *dst, int pairs, int upshift)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  KD_ARM_PREFETCH(src1);       KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);    KD_ARM_PREFETCH(src2+32);
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      int16x4x2_t vp;
      vp.val[0] = vld1_s16(src1);  src1 += 4;
      vp.val[1] = vld1_s16(src2);  src2 += 4;
      vst2_s16(dst,vp); dst += 8;
    }
  for (; pairs > 12; pairs-=16)
    { // Interleave 16 input pairs at a time -- four aligned vectors
      int16x8x2_t vp1, vp2;
      KD_ARM_PREFETCH(src1+64);    KD_ARM_PREFETCH(src2+64);
      vp1.val[0] = vld1q_s16(src1);  src1 += 8;
      vp1.val[1] = vld1q_s16(src2);  src2 += 8;
      vst2q_s16(dst,vp1); dst += 16;
      vp2.val[0] = vld1q_s16(src1);  src1 += 8;
      vp2.val[1] = vld1q_s16(src2);  src2 += 8;
      vst2q_s16(dst,vp2); dst += 16;
    }
  for (; pairs > 0; pairs-=4)
    { // Safe to process 4 input pairs at a time
      int16x4x2_t vp;
      vp.val[0] = vld1_s16(src1);  src1 += 4;
      vp.val[1] = vld1_s16(src2);  src2 += 4;
      vst2_s16(dst,vp); dst += 8;
    }
}

/*****************************************************************************/
/* EXTERN               neoni_upshifted_interleave_16                        */
/*****************************************************************************/

void neoni_upshifted_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                                   kdu_int16 *dst, int pairs, int upshift)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  KD_ARM_PREFETCH(src1);       KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);    KD_ARM_PREFETCH(src2+32);
  int16x8_t shift = vdupq_n_s16((kdu_int16) upshift);
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      int16x4x2_t vp;
      vp.val[0] = vshl_s16(vld1_s16(src1),vget_low_s16(shift));  src1 += 4;
      vp.val[1] = vshl_s16(vld1_s16(src2),vget_low_s16(shift));  src2 += 4;
      vst2_s16(dst,vp); dst += 8;
    }
  for (; pairs > 12; pairs-=16)
    { // Interleave and shift 16 input pairs at a time -- four aligned vectors
      int16x8x2_t vp1, vp2;
      KD_ARM_PREFETCH(src1+64);    KD_ARM_PREFETCH(src2+64);
      vp1.val[0] = vshlq_s16(vld1q_s16(src1),shift);  src1 += 8;
      vp1.val[1] = vshlq_s16(vld1q_s16(src2),shift);  src2 += 8;
      vst2q_s16(dst,vp1); dst += 16;
      vp2.val[0] = vshlq_s16(vld1q_s16(src1),shift);  src1 += 8;
      vp2.val[1] = vshlq_s16(vld1q_s16(src2),shift);  src2 += 8;
      vst2q_s16(dst,vp2); dst += 16;
    }
  for (; pairs > 0; pairs-=4)
    { // Safe to process 4 input pairs at a time
      int16x4x2_t vp;
      vp.val[0] = vshl_s16(vld1_s16(src1),vget_low_s16(shift));  src1 += 4;
      vp.val[1] = vshl_s16(vld1_s16(src2),vget_low_s16(shift));  src2 += 4;
      vst2_s16(dst,vp); dst += 8;
    }
}

/*****************************************************************************/
/* EXTERN                    neoni_interleave_32                             */
/*****************************************************************************/

void neoni_interleave_32(kdu_int32 *src1, kdu_int32 *src2,
                         kdu_int32 *dst, int pairs)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  KD_ARM_PREFETCH(src1);       KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);    KD_ARM_PREFETCH(src2+16);
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      int32x2x2_t vp;
      vp.val[0] = vld1_s32(src1);  src1 += 2;
      vp.val[1] = vld1_s32(src2);  src2 += 2;
      vst2_s32(dst,vp); dst += 4;
    }
  for (; pairs > 6; pairs-=8)
    { // Interleave 8 input pairs at a time -- four aligned vectors
      int32x4x2_t vp1, vp2;
      KD_ARM_PREFETCH(src1+32);    KD_ARM_PREFETCH(src2+32);
      vp1.val[0] = vld1q_s32(src1);  src1 += 4;
      vp1.val[1] = vld1q_s32(src2);  src2 += 4;
      vst2q_s32(dst,vp1); dst += 8;
      vp2.val[0] = vld1q_s32(src1);  src1 += 4;
      vp2.val[1] = vld1q_s32(src2);  src2 += 4;
      vst2q_s32(dst,vp2); dst += 8;
    }
  for (; pairs > 0; pairs-=2)
    { // Safe to process 2 input pairs at a time
      int32x2x2_t vp;
      vp.val[0] = vld1_s32(src1);  src1 += 2;
      vp.val[1] = vld1_s32(src2);  src2 += 2;
      vst2_s32(dst,vp); dst += 4;
    }
}

/* ========================================================================= */
/*                          Deinterleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   neoni_deinterleave_16                            */
/*****************************************************************************/

void neoni_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                           kdu_int16 *dst2, int pairs, int downshift)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src);    KD_ARM_PREFETCH(src+32);   KD_ARM_PREFETCH(src+64);
  int16x8x2_t vp1, vp2;
  for (; pairs > 8; pairs-=16)
    { // Deinterleave 16 input pairs at a time -- four aligned input vectors
      KD_ARM_PREFETCH(src+96);
      vp1 = vld2q_s16(src);  src += 16;  // Reads 16 input words to 2 vectors
      vp2 = vld2q_s16(src);  src += 16;  // Reads and deinterleaves 2 more vecs
      vst1q_s16(dst1,vp1.val[0]);  dst1 += 8;
      vst1q_s16(dst2,vp1.val[1]);  dst2 += 8;
      vst1q_s16(dst1,vp2.val[0]);  dst1 += 8;
      vst1q_s16(dst2,vp2.val[1]);  dst2 += 8;
    }
  if (pairs > 0)
    { // Need to generate one more output vector per channel -- i.e., 8 pairs
      vp1 = vld2q_s16(src);
      vst1q_s16(dst1,vp1.val[0]);
      vst1q_s16(dst2,vp1.val[1]);
    }
}

/*****************************************************************************/
/* EXTERN             neoni_downshifted_deinterleave_16                      */
/*****************************************************************************/

void neoni_downshifted_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                       kdu_int16 *dst2, int pairs,
                                       int downshift)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src);    KD_ARM_PREFETCH(src+32);   KD_ARM_PREFETCH(src+64);
  int16x8_t shift = vdupq_n_s16((kdu_int16)(-downshift)); // -ve left shift
  int16x8x2_t vp1, vp2;
  for (; pairs > 8; pairs-=16)
    { // Deinterleave 16 input pairs at a time -- four aligned input vectors
      KD_ARM_PREFETCH(src+96);
      vp1 = vld2q_s16(src);  src += 16;  // Reads 16 input words to 2 vectors
      vp2 = vld2q_s16(src);  src += 16;  // Reads and deinterleaves 2 more vecs
      vst1q_s16(dst1,vrshlq_s16(vp1.val[0],shift));  dst1 += 8;
      vst1q_s16(dst2,vrshlq_s16(vp1.val[1],shift));  dst2 += 8;
      vst1q_s16(dst1,vrshlq_s16(vp2.val[0],shift));  dst1 += 8;
      vst1q_s16(dst2,vrshlq_s16(vp2.val[1],shift));  dst2 += 8;
    }
  if (pairs > 0)
    { // Need to generate one more output vector per channel -- i.e., 8 pairs
      vp1 = vld2q_s16(src);
      vst1q_s16(dst1,vrshlq_s16(vp1.val[0],shift));
      vst1q_s16(dst2,vrshlq_s16(vp1.val[1],shift));
    }
}

/*****************************************************************************/
/* EXTERN                   neoni_deinterleave_32                            */
/*****************************************************************************/

void neoni_deinterleave_32(kdu_int32 *src, kdu_int32 *dst1,
                           kdu_int32 *dst2, int pairs)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src);    KD_ARM_PREFETCH(src+16);   KD_ARM_PREFETCH(src+32);
  int32x4x2_t vp1, vp2;
  for (; pairs > 4; pairs-=8)
    { // Deinterleave 8 input pairs at a time -- four aligned input vectors
      KD_ARM_PREFETCH(src+48);
      vp1 = vld2q_s32(src);  src += 8;  // Reads 8 input words to 2 vectors
      vp2 = vld2q_s32(src);  src += 8;  // Reads and deinterleaves 2 more vecs
      vst1q_s32(dst1,vp1.val[0]);  dst1 += 4;
      vst1q_s32(dst2,vp1.val[1]);  dst2 += 4;
      vst1q_s32(dst1,vp2.val[0]);  dst1 += 4;
      vst1q_s32(dst2,vp2.val[1]);  dst2 += 4;
    }
  if (pairs > 0)
    { // Need to generate one more output vector per channel -- i.e., 8 pairs
      vp1 = vld2q_s32(src);
      vst1q_s32(dst1,vp1.val[0]);
      vst1q_s32(dst2,vp1.val[1]);
    }
}


/* ========================================================================= */
/*                  Vertical Lifting Step Functions (16-bit)                 */
/* ========================================================================= */

// NOTE: All loads and stores in all of the vertical lifting functions are
// guaranteed to be 128-bit aligned, but the intrinsics do not exploit this.
// Visual Studio builds may benefit from the use of the Microsoft-specific
// "_ex" suffixed load/store intrinsics that can capture this alignment.

/*****************************************************************************/
/* EXTERN                  neoni_vlift_16_9x7_synth                          */
/*****************************************************************************/

void
  neoni_vlift_16_9x7_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[0]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
   }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_9x7_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[1]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vrhaddq_s16(val,vld1q_s16(src2));  src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vrhaddq_s16(val,vld1q_s16(src2));  src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vrhaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_9x7_synth_s23(kdu_int16 **src, kdu_int16 *dst_in,
                               kdu_int16 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[step->step_idx]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                 neoni_vlift_16_9x7_analysis                        */
/*****************************************************************************/

void
  neoni_vlift_16_9x7_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[0]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_9x7_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[1]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vrhaddq_s16(val,vld1q_s16(src2));  src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vrhaddq_s16(val,vld1q_s16(src2));  src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vrhaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_9x7_analysis_s23(kdu_int16 **src, kdu_int16 *dst_in,
                                  kdu_int16 *dst_out, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[step->step_idx]);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
      
      val = vld1q_s16(src1);                   src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));    src2 += 8;
      tgt = vld1q_s16(dst_in);                 dst_in += 8;
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src1);
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_vlift_16_2tap_synth                         */
/*****************************************************************************/

void
  neoni_vlift_16_2tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0];
  kdu_int16 *src2=src1; // In case we only have 1 tap
  kdu_int16 c0 = (kdu_int16)(step->icoeffs[0]);
  kdu_int16 c1 = 0;
  if (step->support_length == 2)
    { c1 = (kdu_int16)(step->icoeffs[1]); src2=src[1]; }
  int16x4_t lambda1=vdup_n_s16(c0), lambda2=vdup_n_s16(c1);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int16x4_t in1, in2;
  int32x4_t sum1, sum2;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 0; samples-=8)
    { // Process 8 samples at a time in batches of 4
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      sum1 = vmlal_s16(vec_offset,in1,lambda1);
      sum1 = vmlal_s16(sum1,in2,lambda2);
      sum1 = vshlq_s32(sum1,shift);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      sum2 = vmlal_s16(vec_offset,in1,lambda1);
      sum2 = vmlal_s16(sum2,in2,lambda2);
      sum2 = vshlq_s32(sum2,shift);
      int16x8_t tgt = vld1q_s16(dst_in);       dst_in += 8;
      int16x8_t subtend = vcombine_s16(vqmovn_s32(sum1), vqmovn_s32(sum2));
      tgt = vsubq_s16(tgt,subtend);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
}

/*****************************************************************************/
/* EXTERN                neoni_vlift_16_2tap_analysis                        */
/*****************************************************************************/

void
  neoni_vlift_16_2tap_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                               kdu_int16 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0];
  kdu_int16 *src2=src1; // In case we only have 1 tap
  kdu_int16 c0 = (kdu_int16)(step->icoeffs[0]);
  kdu_int16 c1 = 0;
  if (step->support_length == 2)
    { c1 = (kdu_int16)(step->icoeffs[1]); src2=src[1]; }
  int16x4_t lambda1=vdup_n_s16(c0), lambda2=vdup_n_s16(c1);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int16x4_t in1, in2;
  int32x4_t sum1, sum2;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 0; samples-=8)
    { // Process 8 samples at a time in batches of 4
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      sum1 = vmlal_s16(vec_offset,in1,lambda1);
      sum1 = vmlal_s16(sum1,in2,lambda2);
      sum1 = vshlq_s32(sum1,shift);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      sum2 = vmlal_s16(vec_offset,in1,lambda1);
      sum2 = vmlal_s16(sum2,in2,lambda2);
      sum2 = vshlq_s32(sum2,shift);
      int16x8_t tgt = vld1q_s16(dst_in);       dst_in += 8;
      int16x8_t addend = vcombine_s16(vqmovn_s32(sum1), vqmovn_s32(sum2));
      tgt = vaddq_s16(tgt,addend);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }  
}

/*****************************************************************************/
/* EXTERN                  neoni_vlift_16_4tap_synth                         */
/*****************************************************************************/

void
  neoni_vlift_16_4tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int16 *src4=src3; // In case we have only 3 taps
  kdu_int16 c0 = (kdu_int16)(step->icoeffs[0]);
  kdu_int16 c1 = (kdu_int16)(step->icoeffs[1]);
  kdu_int16 c2 = (kdu_int16)(step->icoeffs[2]);
  kdu_int16 c3 = 0;
  if (step->support_length==4)
    { c3 = (kdu_int16)(step->icoeffs[3]); src4=src[3]; }
  int16x4_t lambda1=vdup_n_s16(c0), lambda2=vdup_n_s16(c1);
  int16x4_t lambda3=vdup_n_s16(c2), lambda4=vdup_n_s16(c3);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int16x4_t in1, in2, in3, in4;
  int32x4_t sum1, sum2;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src3);                 KD_ARM_PREFETCH(src4);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  KD_ARM_PREFETCH(src3+32);              KD_ARM_PREFETCH(src4+32);
  for (; samples > 0; samples-=8)
    { // Process 8 samples at a time in batches of 4
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);                KD_ARM_PREFETCH(src4+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      in3 = vld1_s16(src3);                    src3 += 4;
      in4 = vld1_s16(src4);                    src4 += 4;
      sum1 = vmlal_s16(vec_offset,in1,lambda1);
      sum1 = vmlal_s16(sum1,in2,lambda2);
      sum1 = vmlal_s16(sum1,in3,lambda3);
      sum1 = vmlal_s16(sum1,in4,lambda4);
      sum1 = vshlq_s32(sum1,shift);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      in3 = vld1_s16(src3);                    src3 += 4;
      in4 = vld1_s16(src4);                    src4 += 4;
      sum2 = vmlal_s16(vec_offset,in1,lambda1);
      sum2 = vmlal_s16(sum2,in2,lambda2);
      sum2 = vmlal_s16(sum2,in3,lambda3);
      sum2 = vmlal_s16(sum2,in4,lambda4);
      sum2 = vshlq_s32(sum2,shift);
      int16x8_t tgt = vld1q_s16(dst_in);       dst_in += 8;
      int16x8_t subtend = vcombine_s16(vqmovn_s32(sum1), vqmovn_s32(sum2));
      tgt = vsubq_s16(tgt,subtend);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }
}

/*****************************************************************************/
/* EXTERN                 neoni_vlift_16_4tap_analysis                       */
/*****************************************************************************/

void
  neoni_vlift_16_4tap_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                               kdu_int16 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(!for_synthesis); // This implementation does analysis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int16 *src4=src3; // In case we have only 3 taps
  kdu_int16 c0 = (kdu_int16)(step->icoeffs[0]);
  kdu_int16 c1 = (kdu_int16)(step->icoeffs[1]);
  kdu_int16 c2 = (kdu_int16)(step->icoeffs[2]);
  kdu_int16 c3 = 0;
  if (step->support_length==4)
    { c3 = (kdu_int16)(step->icoeffs[3]); src4=src[3]; }
  int16x4_t lambda1=vdup_n_s16(c0), lambda2=vdup_n_s16(c1);
  int16x4_t lambda3=vdup_n_s16(c2), lambda4=vdup_n_s16(c3);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int16x4_t in1, in2, in3, in4;
  int32x4_t sum1, sum2;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src3);                 KD_ARM_PREFETCH(src4);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  KD_ARM_PREFETCH(src3+32);              KD_ARM_PREFETCH(src4+32);
  for (; samples > 0; samples-=8)
    { // Process 8 samples at a time in batches of 4
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);                KD_ARM_PREFETCH(src4+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      in3 = vld1_s16(src3);                    src3 += 4;
      in4 = vld1_s16(src4);                    src4 += 4;
      sum1 = vmlal_s16(vec_offset,in1,lambda1);
      sum1 = vmlal_s16(sum1,in2,lambda2);
      sum1 = vmlal_s16(sum1,in3,lambda3);
      sum1 = vmlal_s16(sum1,in4,lambda4);
      sum1 = vshlq_s32(sum1,shift);
      in1 = vld1_s16(src1);                    src1 += 4;
      in2 = vld1_s16(src2);                    src2 += 4;
      in3 = vld1_s16(src3);                    src3 += 4;
      in4 = vld1_s16(src4);                    src4 += 4;
      sum2 = vmlal_s16(vec_offset,in1,lambda1);
      sum2 = vmlal_s16(sum2,in2,lambda2);
      sum2 = vmlal_s16(sum2,in3,lambda3);
      sum2 = vmlal_s16(sum2,in4,lambda4);
      sum2 = vshlq_s32(sum2,shift);
      int16x8_t tgt = vld1q_s16(dst_in);       dst_in += 8;
      int16x8_t addend = vcombine_s16(vqmovn_s32(sum1), vqmovn_s32(sum2));
      tgt = vaddq_s16(tgt,addend);
      vst1q_s16(dst_out,tgt);                  dst_out += 8;
    }  
}

/*****************************************************************************/
/* EXTERN                   neoni_vlift_16_5x3_synth                         */
/*****************************************************************************/

void
  neoni_vlift_16_5x3_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vsubq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vsubq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
      val = vsubq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vsubq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;      
    }
  if (samples > 0)
    { 
      val = vsubq_s16(vec_offset,vld1q_s16(src1));
      val = vsubq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_5x3_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vaddq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
      val = vaddq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vaddq_s16(vec_offset,vld1q_s16(src1));
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }  
}

/*****************************************************************************/
/* EXTERN                 neoni_vlift_16_5x3_analysis                        */
/*****************************************************************************/

void
  neoni_vlift_16_5x3_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vsubq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vsubq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
      val = vsubq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vsubq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;      
    }
  if (samples > 0)
    { 
      val = vsubq_s16(vec_offset,vld1q_s16(src1));
      val = vsubq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_16_5x3_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+32);            KD_ARM_PREFETCH(dst_out+32);
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int16x8_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+32);              KD_ARM_PREFETCH(src2+32);
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+64);                KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(dst_in+64);              KD_ARM_PREFETCH(dst_out+64);
      val = vaddq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
      val = vaddq_s16(vec_offset,vld1q_s16(src1));  src1 += 8;
      val = vaddq_s16(val,vld1q_s16(src2));         src2 += 8;
      tgt = vld1q_s16(dst_in);                      dst_in += 8;
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);                       dst_out += 8;
    }
  if (samples > 0)
    { 
      val = vaddq_s16(vec_offset,vld1q_s16(src1));
      val = vaddq_s16(val,vld1q_s16(src2));
      tgt = vld1q_s16(dst_in);
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst_out,tgt);
    }    
}

/* ========================================================================= */
/*                  Vertical Lifting Step Functions (32-bit)                 */
/* ========================================================================= */

// NOTE: All loads and stores in all of the vertical lifting functions are
// guaranteed to be 128-bit aligned, but the intrinsics do not exploit this.
// Visual Studio builds may benefit from the use of the Microsoft-specific
// "_ex" suffixed load/store intrinsics that can capture this alignment.

/*****************************************************************************/
/* EXTERN                  neoni_vlift_32_2tap_irrev                         */
/*****************************************************************************/

void
  neoni_vlift_32_2tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
                            kdu_int32 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
  /* Does either analysis or synthesis, working with floating point sample
     values.  The 32-bit integer types on the supplied buffers are only for
     simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length == 1) || (step->support_length == 2));
  float c0 = step->coeffs[0];
  float c1 = 0.0F;
  float *src1 = (float *) src[0];
  float *src2 = src1; // In case we only have 1 tap
  if (step->support_length==2)
    { c1 = step->coeffs[1];  src2 = (float *) src[1]; }
  float *dp_in = (float *) dst_in;
  float *dp_out = (float *) dst_out;
  KD_ARM_PREFETCH(dp_in);                KD_ARM_PREFETCH(dp_out);
  KD_ARM_PREFETCH(dp_in+16);             KD_ARM_PREFETCH(dp_out+16);
  float32x4_t lambda1, lambda2;
  if (for_synthesis)
    { lambda1=vdupq_n_f32(-c0); lambda2=vdupq_n_f32(-c1); }
  else
    { lambda1=vdupq_n_f32(c0); lambda2=vdupq_n_f32(c1); }
  float32x4_t in1, in2;
  float32x4_t tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dp_in+32);               KD_ARM_PREFETCH(dp_out+32);
      tgt = vld1q_f32(dp_in);                  dp_in += 4;
      in1 = vld1q_f32(src1);                   src1 += 4;
      in2 = vld1q_f32(src2);                   src2 += 4;
      tgt = vmlaq_f32(tgt,in1,lambda1);
      tgt = vmlaq_f32(tgt,in2,lambda2);
      vst1q_f32(dp_out,tgt);                   dp_out += 4;
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_vlift_32_4tap_irrev                         */
/*****************************************************************************/

void
  neoni_vlift_32_4tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
                            kdu_int32 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
  /* Does either analysis or synthesis, working with floating point sample
     values.  The 32-bit integer types on the supplied buffers are only for
     simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  float c0 = step->coeffs[0];
  float c1 = step->coeffs[1];
  float c2 = step->coeffs[2];
  float c3 = 0.0F;
  float *src1 = (float *) src[0];
  float *src2 = (float *) src[1];
  float *src3 = (float *) src[2];
  float *src4 = src3; // In case we only have 3 taps
  if (step->support_length==4)
    { c3 = step->coeffs[3];  src4 = (float *) src[3]; }
  float *dp_in = (float *) dst_in;
  float *dp_out = (float *) dst_out;
  KD_ARM_PREFETCH(dp_in);                KD_ARM_PREFETCH(dp_out);
  KD_ARM_PREFETCH(dp_in+16);             KD_ARM_PREFETCH(dp_out+16);
  float32x4_t lambda1, lambda2, lambda3, lambda4;
  if (for_synthesis)
    { lambda1=vdupq_n_f32(-c0); lambda2=vdupq_n_f32(-c1);
      lambda3=vdupq_n_f32(-c2); lambda4=vdupq_n_f32(-c3); }
  else
    { lambda1=vdupq_n_f32(c0); lambda2=vdupq_n_f32(c1);
      lambda3=vdupq_n_f32(c2); lambda4=vdupq_n_f32(c3); }
  float32x4_t in1, in2, in3, in4;
  float32x4_t tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src3);                 KD_ARM_PREFETCH(src4);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  KD_ARM_PREFETCH(src3+16);              KD_ARM_PREFETCH(src4+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);                KD_ARM_PREFETCH(src4+32);
      KD_ARM_PREFETCH(dp_in+32);               KD_ARM_PREFETCH(dp_out+32);
      
      tgt = vld1q_f32(dp_in);                  dp_in += 4;
      in1 = vld1q_f32(src1);                   src1 += 4;
      in2 = vld1q_f32(src2);                   src2 += 4;
      in3 = vld1q_f32(src3);                   src3 += 4;
      in4 = vld1q_f32(src4);                   src4 += 4;
      tgt = vmlaq_f32(tgt,in1,lambda1);
      tgt = vmlaq_f32(tgt,in2,lambda2);
      tgt = vmlaq_f32(tgt,in3,lambda3);
      tgt = vmlaq_f32(tgt,in4,lambda4);
      vst1q_f32(dp_out,tgt);                   dp_out += 4;
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_vlift_32_5x3_synth                          */
/*****************************************************************************/

void
  neoni_vlift_32_5x3_synth_s0(kdu_int32 **src, kdu_int32 *dst_in,
                              kdu_int32 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int32x4_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      val = vsubq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vsubq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
      val = vsubq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vsubq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
    }
  if (samples > 0)
    { 
      val = vsubq_s32(vec_offset,vld1q_s32(src1));
      val = vsubq_s32(val,vld1q_s32(src2));
      tgt = vld1q_s32(dst_in);
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_32_5x3_synth_s1(kdu_int32 **src, kdu_int32 *dst_in,
                              kdu_int32 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int32x4_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      val = vaddq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vaddq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
      val = vaddq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vaddq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
    }
  if (samples > 0)
    { 
      val = vaddq_s32(vec_offset,vld1q_s32(src1));
      val = vaddq_s32(val,vld1q_s32(src2));
      tgt = vld1q_s32(dst_in);
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                neoni_vlift_32_5x3_analysis                         */
/*****************************************************************************/

void
  neoni_vlift_32_5x3_analysis_s0(kdu_int32 **src, kdu_int32 *dst_in,
                                 kdu_int32 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int32x4_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      val = vsubq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vsubq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
      val = vsubq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vsubq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
    }
  if (samples > 0)
    { 
      val = vsubq_s32(vec_offset,vld1q_s32(src1));
      val = vsubq_s32(val,vld1q_s32(src2));
      tgt = vld1q_s32(dst_in);
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);
    }  
}
//-----------------------------------------------------------------------------
void
  neoni_vlift_32_5x3_analysis_s1(kdu_int32 **src, kdu_int32 *dst_in,
                                 kdu_int32 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int32x4_t val, tgt;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      val = vaddq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vaddq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
      val = vaddq_s32(vec_offset,vld1q_s32(src1));  src1 += 4;
      val = vaddq_s32(val,vld1q_s32(src2));         src2 += 4;
      tgt = vld1q_s32(dst_in);                      dst_in += 4;
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);                       dst_out += 4;
    }
  if (samples > 0)
    { 
      val = vaddq_s32(vec_offset,vld1q_s32(src1));
      val = vaddq_s32(val,vld1q_s32(src2));
      tgt = vld1q_s32(dst_in);
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst_out,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                neoni_vlift_32_2tap_rev_synth                       */
/*****************************************************************************/

void
  neoni_vlift_32_2tap_rev_synth(kdu_int32 **src, kdu_int32 *dst_in,
                                kdu_int32 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0];
  kdu_int32 *src2=src1; // In case we only have 1 tap
  kdu_int32 c0 = step->icoeffs[0];
  kdu_int32 c1 = 0;
  if (step->support_length==2)
    { c1 = step->icoeffs[1]; src2=src[1]; }
  int32x4_t lambda1=vdupq_n_s32(c0), lambda2=vdupq_n_s32(c1);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int32x4_t in1, in2;
  int32x4_t sum;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      in1 = vld1q_s32(src1);                   src1 += 4;
      in2 = vld1q_s32(src2);                   src2 += 4;
      sum = vmlaq_s32(vec_offset,in1,lambda1);
      sum = vmlaq_s32(sum,in2,lambda2);
      sum = vshlq_s32(sum,shift);
      int32x4_t tgt = vld1q_s32(dst_in);       dst_in += 4;
      tgt = vsubq_s32(tgt,sum);
      vst1q_s32(dst_out,tgt);                  dst_out += 4;
    }
}

/*****************************************************************************/
/* EXTERN               neoni_vlift_32_2tap_rev_analysis                     */
/*****************************************************************************/

void
  neoni_vlift_32_2tap_rev_analysis(kdu_int32 **src, kdu_int32 *dst_in,
                                   kdu_int32 *dst_out, int samples,
                                   kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0];
  kdu_int32 *src2=src1; // In case we only have 1 tap
  kdu_int32 c0 = step->icoeffs[0];
  kdu_int32 c1 = 0;
  if (step->support_length==2)
    { c1 = step->icoeffs[1]; src2=src[1]; }
  int32x4_t lambda1=vdupq_n_s32(c0), lambda2=vdupq_n_s32(c1);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int32x4_t in1, in2;
  int32x4_t sum;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      in1 = vld1q_s32(src1);                   src1 += 4;
      in2 = vld1q_s32(src2);                   src2 += 4;
      sum = vmlaq_s32(vec_offset,in1,lambda1);
      sum = vmlaq_s32(sum,in2,lambda2);
      sum = vshlq_s32(sum,shift);
      int32x4_t tgt = vld1q_s32(dst_in);       dst_in += 4;
      tgt = vaddq_s32(tgt,sum);
      vst1q_s32(dst_out,tgt);                  dst_out += 4;
    }
}

/*****************************************************************************/
/* EXTERN                neoni_vlift_32_4tap_rev_synth                       */
/*****************************************************************************/

void
  neoni_vlift_32_4tap_rev_synth(kdu_int32 **src, kdu_int32 *dst_in,
                                kdu_int32 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int32 *src4=src3; // In case we only have 3 taps
  kdu_int32 c0 = step->icoeffs[0];
  kdu_int32 c1 = step->icoeffs[1];
  kdu_int32 c2 = step->icoeffs[2];
  kdu_int32 c3 = 0;
  if (step->support_length==4)
    { c3 = step->icoeffs[3]; src4 = src[3]; }
  int32x4_t lambda1=vdupq_n_s32(c0), lambda2=vdupq_n_s32(c1);
  int32x4_t lambda3=vdupq_n_s32(c2), lambda4=vdupq_n_s32(c3);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int32x4_t in1, in2, in3, in4;
  int32x4_t sum;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src3);                 KD_ARM_PREFETCH(src4);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  KD_ARM_PREFETCH(src3+16);              KD_ARM_PREFETCH(src4+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);                KD_ARM_PREFETCH(src4+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      in1 = vld1q_s32(src1);                   src1 += 4;
      in2 = vld1q_s32(src2);                   src2 += 4;
      in3 = vld1q_s32(src3);                   src3 += 4;
      in4 = vld1q_s32(src4);                   src4 += 4;
      sum = vmlaq_s32(vec_offset,in1,lambda1);
      sum = vmlaq_s32(sum,in2,lambda2);
      sum = vmlaq_s32(sum,in3,lambda3);
      sum = vmlaq_s32(sum,in4,lambda4);
      sum = vshlq_s32(sum,shift);
      int32x4_t tgt = vld1q_s32(dst_in);       dst_in += 4;
      tgt = vsubq_s32(tgt,sum);
      vst1q_s32(dst_out,tgt);                  dst_out += 4;
    }
}

/*****************************************************************************/
/* EXTERN              neoni_vlift_32_4tap_rev_analysis                      */
/*****************************************************************************/

void
  neoni_vlift_32_4tap_rev_analysis(kdu_int32 **src, kdu_int32 *dst_in,
                                   kdu_int32 *dst_out, int samples,
                                   kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(!for_synthesis); // This implementation does analysis only
  KD_ARM_PREFETCH(dst_in);               KD_ARM_PREFETCH(dst_out);
  KD_ARM_PREFETCH(dst_in+16);            KD_ARM_PREFETCH(dst_out+16);
  kdu_int32 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int32 *src4=src3; // In case we only have 3 taps
  kdu_int32 c0 = step->icoeffs[0];
  kdu_int32 c1 = step->icoeffs[1];
  kdu_int32 c2 = step->icoeffs[2];
  kdu_int32 c3 = 0;
  if (step->support_length==4)
    { c3 = step->icoeffs[3]; src4 = src[3]; }
  int32x4_t lambda1=vdupq_n_s32(c0), lambda2=vdupq_n_s32(c1);
  int32x4_t lambda3=vdupq_n_s32(c2), lambda4=vdupq_n_s32(c3);
  int32x4_t vec_offset = vdupq_n_s32(step->rounding_offset);
  int32x4_t shift = vdupq_n_s32(-step->downshift); // -ve left shift
  int32x4_t in1, in2, in3, in4;
  int32x4_t sum;
  KD_ARM_PREFETCH(src1);                 KD_ARM_PREFETCH(src2);
  KD_ARM_PREFETCH(src3);                 KD_ARM_PREFETCH(src4);
  KD_ARM_PREFETCH(src1+16);              KD_ARM_PREFETCH(src2+16);
  KD_ARM_PREFETCH(src3+16);              KD_ARM_PREFETCH(src4+16);
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(src1+32);                KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);                KD_ARM_PREFETCH(src4+32);
      KD_ARM_PREFETCH(dst_in+32);              KD_ARM_PREFETCH(dst_out+32);
      in1 = vld1q_s32(src1);                   src1 += 4;
      in2 = vld1q_s32(src2);                   src2 += 4;
      in3 = vld1q_s32(src3);                   src3 += 4;
      in4 = vld1q_s32(src4);                   src4 += 4;
      sum = vmlaq_s32(vec_offset,in1,lambda1);
      sum = vmlaq_s32(sum,in2,lambda2);
      sum = vmlaq_s32(sum,in3,lambda3);
      sum = vmlaq_s32(sum,in4,lambda4);
      sum = vshlq_s32(sum,shift);
      int32x4_t tgt = vld1q_s32(dst_in);       dst_in += 4;
      tgt = vaddq_s32(tgt,sum);
      vst1q_s32(dst_out,tgt);                  dst_out += 4;
    }  
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (16-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  neoni_hlift_16_9x7_synth                          */
/*****************************************************************************/

void
  neoni_hlift_16_9x7_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[0]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;      
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      tgt = vaddq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    } 
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_9x7_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[1]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));   src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));   src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }  
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_9x7_synth_s23(kdu_int16 *src, kdu_int16 *dst,
                               int samples, kd_lifting_step *step,
                               bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[step->step_idx]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }  
}

/*****************************************************************************/
/* EXTERN                neoni_hlift_16_9x7_analysis                         */
/*****************************************************************************/

void
  neoni_hlift_16_9x7_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[0]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;      
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      tgt = vsubq_s16(tgt,val);   // Here is a -1 contribution (wrt analysis)
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }  
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_9x7_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[1]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));   src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));   src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vrhaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_9x7_analysis_s23(kdu_int16 *src, kdu_int16 *dst,
                                  int samples, kd_lifting_step *step,
                                  bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  int16x8_t vec_lambda = vdupq_n_s16(neon_w97_rem[step->step_idx]);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));     src += 7;
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                      dst += 8;
    }
  if (samples > 0)
    { 
      val = vld1q_s16(src);                    src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vqrdmulhq_s16(val,vec_lambda);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                   neoni_hlift_16_5x3_synth                         */
/*****************************************************************************/

void
  neoni_hlift_16_5x3_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
    }
  if (samples > 0)
    { 
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_5x3_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
    }
  if (samples > 0)
    { 
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vsubq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }  
}

/*****************************************************************************/
/* EXTERN                  neoni_hlift_16_5x3_analysis                       */
/*****************************************************************************/

void
  neoni_hlift_16_5x3_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
    }
  if (samples > 0)
    { 
      val = vsubq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vsubq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,1);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_16_5x3_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  int16x8_t vec_offset = vdupq_n_s16((kdu_int16)((1<<step->downshift)>>1));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+32);    KD_ARM_PREFETCH(dst+32);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int16x8_t val, tgt;
  for (; samples > 8; samples-=16)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+64);                 KD_ARM_PREFETCH(dst+64);
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));          src += 7;
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);                           dst += 8;
    }
  if (samples > 0)
    { 
      val = vaddq_s16(vec_offset,vld1q_s16(src));   src += 1;
      val = vaddq_s16(val,vld1q_s16(src));
      tgt = vld1q_s16(dst);
      val = vshrq_n_s16(val,2);
      tgt = vaddq_s16(tgt,val);
      vst1q_s16(dst,tgt);
    }
}

/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (32-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  neoni_hlift_32_2tap_irrev                         */
/*****************************************************************************/

void
  neoni_hlift_32_2tap_irrev(kdu_int32 *src, kdu_int32 *dst, int samples,
                            kd_lifting_step *step, bool for_synthesis)
  /* Does either analysis or synthesis, working with floating point sample
     values.  The 32-bit integer types on the supplied buffers are only for
     simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length == 1) || (step->support_length == 2));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  float c0 = step->coeffs[0];
  float c1 = 0.0F;
  if (step->support_length==2)
    c1 = step->coeffs[1];
  float32x4_t lambda1, lambda2;
  if (for_synthesis)
    { lambda1=vdupq_n_f32(-c0); lambda2=vdupq_n_f32(-c1); }
  else
    { lambda1=vdupq_n_f32(c0); lambda2=vdupq_n_f32(c1); }
  float *sp=(float *)src, *dp=(float *)dst;
  float32x4_t in1, in2;
  float32x4_t tgt;
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(sp+32);                  KD_ARM_PREFETCH(dp+32);
      tgt = vld1q_f32(dp);
      in1 = vld1q_f32(sp);                     sp += 1;
      in2 = vld1q_f32(sp);                     sp += 3;
      tgt = vmlaq_f32(tgt,in1,lambda1);
      tgt = vmlaq_f32(tgt,in2,lambda2);
      vst1q_f32(dp,tgt);                       dp += 4;
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_hlift_32_4tap_irrev                         */
/*****************************************************************************/

void
  neoni_hlift_32_4tap_irrev(kdu_int32 *src, kdu_int32 *dst, int samples,
                            kd_lifting_step *step, bool for_synthesis)
  /* Does either analysis or synthesis, working with floating point sample
     values.  The 32-bit integer types on the supplied buffers are only for
     simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  float c0 = step->coeffs[0];
  float c1 = step->coeffs[1];
  float c2 = step->coeffs[2];
  float c3 = 0.0F;
  if (step->support_length==4)
    c3 = step->coeffs[3];
  float32x4_t lambda1, lambda2, lambda3, lambda4;
  if (for_synthesis)
    { lambda1=vdupq_n_f32(-c0); lambda2=vdupq_n_f32(-c1);
      lambda3=vdupq_n_f32(-c2); lambda4=vdupq_n_f32(-c3); }
  else
    { lambda1=vdupq_n_f32(c0); lambda2=vdupq_n_f32(c1);
      lambda3=vdupq_n_f32(c2); lambda4=vdupq_n_f32(c3); }
  float *sp=(float *)src, *dp=(float *)dst;
  float32x4_t in1, in2, in3, in4;
  float32x4_t tgt;
  for (; samples > 0; samples-=4)
    { // Process 4 samples at a time, producing one output vector
      KD_ARM_PREFETCH(sp+32);                  KD_ARM_PREFETCH(dp+32);
      tgt = vld1q_f32(dp);
      in1 = vld1q_f32(sp);                     sp += 1;
      in2 = vld1q_f32(sp);                     sp += 1;
      in3 = vld1q_f32(sp);                     sp += 1;
      in4 = vld1q_f32(sp);                     sp += 1;
      tgt = vmlaq_f32(tgt,in1,lambda1);
      tgt = vmlaq_f32(tgt,in2,lambda2);
      tgt = vmlaq_f32(tgt,in3,lambda3);
      tgt = vmlaq_f32(tgt,in4,lambda4);
      vst1q_f32(dp,tgt);                       dp += 4;
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_hlift_32_5x3_synth                          */
/*****************************************************************************/

void
  neoni_hlift_32_5x3_synth_s0(kdu_int32 *src, kdu_int32 *dst, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int32x4_t val, tgt;
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+32);                 KD_ARM_PREFETCH(dst+32);
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
    }
  if (samples > 0)
    { 
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_32_5x3_synth_s1(kdu_int32 *src, kdu_int32 *dst, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int32x4_t val, tgt;
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+32);                 KD_ARM_PREFETCH(dst+32);
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
    }
  if (samples > 0)
    { 
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vsubq_s32(tgt,val);
      vst1q_s32(dst,tgt);
    }
}

/*****************************************************************************/
/* EXTERN                 neoni_hlift_32_5x3_analysis                        */
/*****************************************************************************/

void
  neoni_hlift_32_5x3_analysis_s0(kdu_int32 *src, kdu_int32 *dst, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  int32x4_t val, tgt;
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+32);                 KD_ARM_PREFETCH(dst+32);
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
    }
  if (samples > 0)
    { 
      val = vsubq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vsubq_s32(val,vld1q_s32(src));
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,1);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);
    }
}
//-----------------------------------------------------------------------------
void
  neoni_hlift_32_5x3_analysis_s1(kdu_int32 *src, kdu_int32 *dst, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  int32x4_t vec_offset = vdupq_n_s32((1<<step->downshift)>>1);
  KD_ARM_PREFETCH(src);       KD_ARM_PREFETCH(dst);
  KD_ARM_PREFETCH(src+16);    KD_ARM_PREFETCH(dst+16);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  int32x4_t val, tgt;
  for (; samples > 4; samples-=8)
    { // Process two vectors at a time
      KD_ARM_PREFETCH(src+32);                 KD_ARM_PREFETCH(dst+32);
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));          src += 3;
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);                           dst += 4;
    }
  if (samples > 0)
    { 
      val = vaddq_s32(vec_offset,vld1q_s32(src));   src += 1;
      val = vaddq_s32(val,vld1q_s32(src));
      tgt = vld1q_s32(dst);
      val = vshrq_n_s32(val,2);
      tgt = vaddq_s32(tgt,val);
      vst1q_s32(dst,tgt);
    }  
}
  
} // namespace kd_core_simd

#endif // NEON Intrinsics
