/*****************************************************************************/
// File: gcc_dwt_sparcvis_local.h [scope = CORESYS/TRANSFORMS]
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
   Implements various critical functions for DWT analysis and synthesis,
taking advantage of the UltraSPARC's VIS SIMD instructions.
******************************************************************************/

#ifndef GCC_DWT_SPARCVIS_LOCAL_H
#define GCC_DWT_SPARCVIS_LOCAL_H

#include "transform_base.h"

namespace kd_core_simd {
  using namespace kd_core_local;

/* ========================================================================= */
/*                          Convenient Definitions                           */
/* ========================================================================= */

typedef double kdvis_d64; // Use for 64-bit VIS registers

union kdvis_4x16 {
    kdvis_d64 d64;
    kdu_int16 v[4]; // Most significant word has idx 0: SPARC is big-endian
  };

typedef float kdvis_f32; // Use for 32-bit VIS registers

union kdvis_4x8 {
    kdvis_f32 f32;
    kdu_byte v[4]; // Must significant byte has idx 0; SPARC is big-endian
  };

#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

static kdu_int16 simd_w97_rem[4] =
  {(kdu_int16) floor(0.5 + (W97_FACT_0+2.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_1*(double)(1<<19)),
   (kdu_int16) floor(0.5 + (W97_FACT_2-1.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_3*(double)(1<<16))};


/* ========================================================================= */
/*                            Interleave Functions                           */
/* ========================================================================= */

#define KD_SET_SIMD_INTERLEAVE_16_FUNC(_tgt,_pairs,_upshift)
#define KD_SET_SIMD_DEINTERLEAVE_16_FUNC(_tgt,_pairs,_downshift)
#define KD_SET_SIMD_INTERLEAVE_32_FUNC(_tgt,_pairs)
#define KD_SET_SIMD_DEINTERLEAVE_32_FUNC(_tgt,_pairs)

/* ========================================================================= */
/*                      Vertical Lifting Step Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       vis_vlift_16_...                             */
/*****************************************************************************/

static void
  vis_vlift_16_5x3_synth(kdu_int16 **src, kdu_int16 *dst_in,
                         kdu_int16 *dst_out, int samples,
                         kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This one only does synthesis
  if (samples <= 0)
    return;
  
  int n, quads = (samples+3)>>2;
  int int_coeff = step->icoeffs[0];
  int downshift = step->downshift;
  assert(downshift > 0);
  register kdvis_d64 s1, s2, d, sum;
  register kdvis_d64 *sp1 = (kdvis_d64 *) src[0];
  register kdvis_d64 *sp2 = (kdvis_d64 *) src[1];
  register kdvis_d64 *dp_in  = (kdvis_d64 *) dst_in;
  register kdvis_d64 *dp_out = (kdvis_d64 *) dst_out;
  if (int_coeff == 1)
    { 
      kdvis_4x8 q_scale;
      q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
      (kdu_byte)(0x80 >> (downshift-1));
      register kdvis_f32 scale = q_scale.f32;
      for (n=quads; n > 0; n--)
        { 
          s1 = *(sp1++); s2 = *(sp2++);
          asm("fpadd16 %1, %2, %0"  : "=e"(sum): "e"(s1),    "e"(s2));
          d = *(dp_in++);
          asm("fmul8x16 %1, %2, %0" : "=e"(sum): "f"(scale), "e"(sum));
          asm("fpsub16 %1, %2, %0"  : "=e"(d)  : "e"(d),     "e"(sum));
          *(dp_out++) = d;
        }
    }
  else if (int_coeff == -1)
    { 
      kdvis_4x16 q_scale;
      q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
      (kdu_int16)(int_coeff << (16-downshift));
      register kdvis_d64 scale = q_scale.d64;
      for (n=quads; n > 0; n--)
        { 
          s1 = *(sp1++); s2 = *(sp2++);
          asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s2));
          d = *(dp_in++);
          asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
          asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
          *(dp_out++) = d;
        }
    }
  else
    assert(0);
}
//-----------------------------------------------------------------------------
static void
  vis_vlift_16_5x3_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis); // This one only does analysis
  if (samples <= 0)
    return;
  
  int n, quads = (samples+3)>>2;
  int int_coeff = step->icoeffs[0];
  int downshift = step->downshift;
  assert(downshift > 0);
  register kdvis_d64 s1, s2, d, sum;
  register kdvis_d64 *sp1 = (kdvis_d64 *) src[0];
  register kdvis_d64 *sp2 = (kdvis_d64 *) src[1];
  register kdvis_d64 *dp_in  = (kdvis_d64 *) dst_in;
  register kdvis_d64 *dp_out = (kdvis_d64 *) dst_out;
  if (int_coeff == 1)
    { 
      kdvis_4x8 q_scale;
      q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
      (kdu_byte)(0x80 >> (downshift-1));
      register kdvis_f32 scale = q_scale.f32;
      for (n=quads; n > 0; n--)
        { 
          s1 = *(sp1++); s2 = *(sp2++);
          asm("fpadd16 %1, %2, %0"  : "=e"(sum): "e"(s1),    "e"(s2));
          d = *(dp_in++);
          asm("fmul8x16 %1, %2, %0" : "=e"(sum): "f"(scale), "e"(sum));
          asm("fpadd16 %1, %2, %0"  : "=e"(d)  : "e"(d),     "e"(sum));
          *(dp_out++) = d;
        }
    }
  else if (int_coeff == -1)
    { 
      kdvis_4x16 q_scale;
      q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
      (kdu_int16)(int_coeff << (16-downshift));
      register kdvis_d64 scale = q_scale.d64;
      for (n=quads; n > 0; n--)
        { 
          s1 = *(sp1++); s2 = *(sp2++);
          asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s2));
          d = *(dp_in++);
          asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
          asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
          *(dp_out++) = d;
        }
    }
  else
    assert(0);
}
//-----------------------------------------------------------------------------
static void
  vis_vlift_16_9x7_synth(kdu_int16 **src, kdu_int16 *dst_in,
                         kdu_int16 *dst_out, int samples,
                         kd_lifting_step *step, bool for_synthesis)
{
  assert(for_synthesis);
  if (samples <= 0)
    return;
  int n, quads = (samples+3)>>2;
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  kdvis_4x16 q_lambda;
  q_lambda.v[0] = q_lambda.v[1] =
  q_lambda.v[2] = q_lambda.v[3] = simd_w97_rem[step_idx];
  register kdvis_d64 factor = q_lambda.d64;
  register kdvis_d64 s1, s2, d, sum;
  register kdvis_d64 *sp1 = (kdvis_d64 *) src[0];
  register kdvis_d64 *sp2 = (kdvis_d64 *) src[1];
  register kdvis_d64 *dp_in  = (kdvis_d64 *) dst_in;
  register kdvis_d64 *dp_out  = (kdvis_d64 *) dst_out;
  
  switch (step_idx) {
    case 0:
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
        register kdvis_d64 acc;
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(acc): "e"(sum), "e"(sum));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fpsub16 %1, %2, %0"    : "=e"(acc): "e"(s1),  "e"(acc));
            asm("fpadd16 %1, %2, %0"    : "=e"(acc): "e"(acc), "e"(s2));
            asm("fpsub16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(acc));
            *(dp_out++) = d;
          }
      }
      break;
    case 1:
      { // Pre-multiply each source sample by remainder then add and downshift;
        // pre-multiplication avoids unnecessarily large intermediate results.
        // The actual lifting factor is -0.05298
        kdvis_4x8 q_scale;
        q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
        (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3
        register kdvis_f32 scale = q_scale.f32;
        register kdvis_d64 hi1, hi2, lo1, lo2;
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fmul8sux16 %1, %2, %0" : "=e"(hi1): "e"(s1),  "e"(factor));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(lo1): "e"(s1),  "e"(factor));
            asm("fmul8sux16 %1, %2, %0" : "=e"(hi2): "e"(s2),  "e"(factor));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(lo2): "e"(s2),  "e"(factor));
            d = *(dp_in++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(hi1), "e"(lo1));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(hi2));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(lo2));
            asm("fmul8x16 %1, %2, %0"   : "=e"(sum): "f"(scale), "e"(sum));
            asm("fpsub16 %1, %2, %0"    : "=e"(d)  : "e"(d),     "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(s1));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(s2));
            asm("fpsub16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fpsub16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}
//-----------------------------------------------------------------------------
static void
  vis_vlift_16_9x7_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  assert(!for_synthesis);
  if (samples <= 0)
    return;
  int n, quads = (samples+3)>>2;
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  kdvis_4x16 q_lambda;
  q_lambda.v[0] = q_lambda.v[1] =
  q_lambda.v[2] = q_lambda.v[3] = simd_w97_rem[step_idx];
  register kdvis_d64 factor = q_lambda.d64;
  register kdvis_d64 s1, s2, d, sum;
  register kdvis_d64 *sp1 = (kdvis_d64 *) src[0];
  register kdvis_d64 *sp2 = (kdvis_d64 *) src[1];
  register kdvis_d64 *dp_in  = (kdvis_d64 *) dst_in;
  register kdvis_d64 *dp_out  = (kdvis_d64 *) dst_out;
  
  switch (step_idx) {
    case 0:
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
        register kdvis_d64 acc;
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(acc): "e"(sum), "e"(sum));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fpsub16 %1, %2, %0"    : "=e"(acc): "e"(s1),  "e"(acc));
            asm("fpadd16 %1, %2, %0"    : "=e"(acc): "e"(acc), "e"(s2));
            asm("fpadd16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(acc));
            *(dp_out++) = d;
          }
      }
      break;
    case 1:
      { // Pre-multiply each source sample by remainder then add and downshift;
        // pre-multiplication avoids unnecessarily large intermediate results.
        // The actual lifting factor is -0.05298
        kdvis_4x8 q_scale;
        q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
        (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3
        register kdvis_f32 scale = q_scale.f32;
        register kdvis_d64 hi1, hi2, lo1, lo2;
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fmul8sux16 %1, %2, %0" : "=e"(hi1): "e"(s1),  "e"(factor));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(lo1): "e"(s1),  "e"(factor));
            asm("fmul8sux16 %1, %2, %0" : "=e"(hi2): "e"(s2),  "e"(factor));
            asm("fmul8ulx16 %1, %2, %0" : "=e"(lo2): "e"(s2),  "e"(factor));
            d = *(dp_in++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(hi1), "e"(lo1));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(hi2));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(lo2));
            asm("fmul8x16 %1, %2, %0"   : "=e"(sum): "f"(scale), "e"(sum));
            asm("fpadd16 %1, %2, %0"    : "=e"(d)  : "e"(d),     "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(s1));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(sum), "e"(s2));
            asm("fpadd16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        for (n=quads; n > 0; n--)
          { 
            s1 = *(sp1++); s2 = *(sp2++);
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fmul8sux16 %1, %2, %0" : "=e"(s1) : "e"(sum), "e"(factor));
            d = *(dp_in++);
            asm("fmul8ulx16 %1, %2, %0" : "=e"(s2) : "e"(sum), "e"(factor));
            asm("fpadd16 %1, %2, %0"    : "=e"(sum): "e"(s1),  "e"(s2));
            asm("fpadd16 %1, %2, %0"    : "=e"(d)  : "e"(d),   "e"(sum));
            *(dp_out++) = d;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_sparcvis_exists) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vis_vlift_16_5x3_synth; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vis_vlift_16_9x7_synth; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vis_vlift_16_5x3_analysis; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vis_vlift_16_9x7_analysis; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
  }

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */


/* ========================================================================= */
/*                     Horizontal Lifting Step Functions                     */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       vis_hlift_16_...                             */
/*****************************************************************************/

static void vis_hlift_16_5x3_synth(kdu_int16 *src, kdu_int16 *dst,
                                   int samples, kd_lifting_step *step,
                                   bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis);
  if (samples <= 0)
    return;
  
  int n, quads = (samples+3)>>2;
  int int_coeff = step->icoeffs[0];
  int downshift = step->downshift;
  assert(downshift > 0);
  register kdvis_d64 s1, s2, s3, d, sum;
  register kdvis_d64 *sp = (kdvis_d64 *) src;
  register kdvis_d64 *dp = (kdvis_d64 *) dst;
  assert((_addr_to_kdu_int32(dp) & 7) == 0); // `dst' must be aligned on 8-byte boundary
  
  if ((_addr_to_kdu_int32(sp) & 7) == 0)
    { // In this case, sp[0] is aligned; add to non-aligned sp[2].
      register int two=2;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (two));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);
      
      if (int_coeff == 1)
        { 
          kdvis_4x8 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> (downshift-1));
          register kdvis_f32 scale = q_scale.f32;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            { 
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s3));
              d = *dp;
              asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
              asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else if (int_coeff == -1)
        { 
          kdvis_4x16 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_int16)(int_coeff << (16-downshift));
          register kdvis_d64 scale = q_scale.d64;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            { 
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s3));
              d = *dp;
              asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
              asm("fpsub16 %1, %2, %0"   : "=e" (d) : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else
        assert(0);
    }
  else
    { // In this case, sp[2] is aligned; add to non-aligned sp[0].
      register int zero=0;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (zero));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);
      
      if (int_coeff == 1)
        { 
          kdvis_4x8 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> (downshift-1));
          register kdvis_f32 scale = q_scale.f32;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            { 
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),    "e"(s3));
              d = *dp;
              asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
              asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else if (int_coeff == -1)
        { 
          kdvis_4x16 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_int16)(int_coeff << (16-downshift));
          register kdvis_d64 scale = q_scale.d64;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            { 
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),    "e"(s3));
              d = *dp;
              asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
              asm("fpsub16 %1, %2, %0"   : "=e" (d) : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else
        assert(0);
    }
}
//-----------------------------------------------------------------------------
static void vis_hlift_16_5x3_analysis(kdu_int16 *src, kdu_int16 *dst,
                                      int samples, kd_lifting_step *step,
                                      bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis);
  if (samples <= 0)
    return;

  int n, quads = (samples+3)>>2;
  int int_coeff = step->icoeffs[0];
  int downshift = step->downshift;
  assert(downshift > 0);
  register kdvis_d64 s1, s2, s3, d, sum;
  register kdvis_d64 *sp = (kdvis_d64 *) src;
  register kdvis_d64 *dp = (kdvis_d64 *) dst;
  assert((_addr_to_kdu_int32(dp) & 7) == 0); // `dst' must be aligned on 8-byte boundary

  if ((_addr_to_kdu_int32(sp) & 7) == 0)
    { // In this case, sp[0] is aligned; add to non-aligned sp[2].
      register int two=2;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (two));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);

      if (int_coeff == 1)
        {
	        kdvis_4x8 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> (downshift-1));
          register kdvis_f32 scale = q_scale.f32;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            {
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s3));
              d = *dp;
              asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
              asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else if (int_coeff == -1)
        {
          kdvis_4x16 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_int16)(int_coeff << (16-downshift));
          register kdvis_d64 scale = q_scale.d64;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            {
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),    "e"(s3));
              d = *dp;
              asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
              asm("fpadd16 %1, %2, %0"   : "=e" (d) : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else
        assert(0);
    }
  else
    { // In this case, sp[2] is aligned; add to non-aligned sp[0].
      register int zero=0;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (zero));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);

      if (int_coeff == 1)
        {
	        kdvis_4x8 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> (downshift-1));
          register kdvis_f32 scale = q_scale.f32;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            {
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),    "e"(s3));
              d = *dp;
              asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
              asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else if (int_coeff == -1)
        {
	        kdvis_4x16 q_scale;
          q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_int16)(int_coeff << (16-downshift));
          register kdvis_d64 scale = q_scale.d64;
          for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
            {
              s2 = *(sp++);
              asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),    "e"(s2));
              asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),    "e"(s3));
              d = *dp;
              asm("fmul8sux16 %1, %2, %0": "=e"(sum): "e"(scale), "e"(sum));
              asm("fpadd16 %1, %2, %0"   : "=e" (d) : "e"(d),     "e"(sum));
              *(dp++) = d;
            }
        }
      else
        assert(0);
    }
}
//-----------------------------------------------------------------------------
static void vis_hlift_16_9x7_synth(kdu_int16 *src, kdu_int16 *dst,
                                   int samples, kd_lifting_step *step,
                                   bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis);
  if (samples <= 0)
    return;
  
  int n, quads = (samples+3)>>2;
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  kdvis_4x16 q_lambda;
  q_lambda.v[0] = q_lambda.v[1] =
  q_lambda.v[2] = q_lambda.v[3] = simd_w97_rem[step_idx];
  register kdvis_d64 factor = q_lambda.d64;
  register kdvis_d64 s1, s2, s3, d, sum;
  register kdvis_d64 *sp = (kdvis_d64 *) src;
  register kdvis_d64 *dp = (kdvis_d64 *) dst;
  assert((_addr_to_kdu_int32(dp) & 7) == 0); // `dst' must be aligned on 8-byte boundary
  
  if ((_addr_to_kdu_int32(sp) & 7) == 0)
    { // In this case, sp[0] is aligned; add to non-aligned sp[2]
      register int two=2;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (two));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);
      
      switch (step_idx) {
        case 0: 
          { // Integer part of lifting step factor is -2.
            // The actual lifting factor is -1.586134
            register kdvis_d64 acc;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(sum), "e"(sum));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpsub16 %1, %2, %0"   : "=e"(acc): "e"(s1) , "e"(acc));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(acc), "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(acc));
                *(dp++) = d;
              }
          }
          break;
        case 1:
          { // Add source samples; multiply by remainder, then downshift.
            // The actual lifting factor is -0.05298
            kdvis_4x8 q_scale;
            q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3
            
            register kdvis_f32 scale = q_scale.f32;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
                asm("fpsub16 %1, %2, %0"   : "=e" (d) : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 2:
          { // Integer part of lifting step factor is 1.
            // The actual lifting factor is 0.882911
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s1));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 3:
          { // Integer part of lifting step factor is 0
            // The actual lifting factor is 0.443507
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        default:
          assert(0);
      } // End of switch statement
    }
  else
    { // In this case, sp[2] is aligned; add to non-aligned sp[0]
      register int zero=0;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (zero));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);
      
      switch (step_idx) {
        case 0: 
          { // Integer part of lifting step factor is -2.
            // The actual lifting factor is -1.586134
            register kdvis_d64 acc;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(sum), "e"(sum));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpsub16 %1, %2, %0"   : "=e"(acc): "e"(s1) , "e"(acc));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(acc), "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(acc));
                *(dp++) = d;
              }
          }
          break;
        case 1:
          { // Add source samples; multiply by remainder, then downshift.
            // The actual lifting factor is -0.05298
            kdvis_4x8 q_scale;
            q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
            (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3
            
            register kdvis_f32 scale = q_scale.f32;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
                asm("fpsub16 %1, %2, %0"   : "=e" (d) : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 2:
          { // Integer part of lifting step factor is 1.
            // The actual lifting factor is 0.882911
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s1));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 3:
          { // Integer part of lifting step factor is 0
            // The actual lifting factor is 0.443507
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              { 
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fpsub16 %1, %2, %0"   : "=e"(d)  : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        default:
          assert(0);
      } // end of switch statement
    }
}
//-----------------------------------------------------------------------------
static void vis_hlift_16_9x7_analysis(kdu_int16 *src, kdu_int16 *dst,
                                      int samples, kd_lifting_step *step,
                                      bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis);
  if (samples <= 0)
    return;

  int n, quads = (samples+3)>>2;
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  kdvis_4x16 q_lambda;
  q_lambda.v[0] = q_lambda.v[1] =
    q_lambda.v[2] = q_lambda.v[3] = simd_w97_rem[step_idx];
  register kdvis_d64 factor = q_lambda.d64;
  register kdvis_d64 s1, s2, s3, d, sum;
  register kdvis_d64 *sp = (kdvis_d64 *) src;
  register kdvis_d64 *dp = (kdvis_d64 *) dst;
  assert((_addr_to_kdu_int32(dp) & 7) == 0); // `dst' must be aligned on 8-byte boundary

  if ((_addr_to_kdu_int32(sp) & 7) == 0)
    { // In this case, sp[0] is aligned; add to non-aligned sp[2]
      register int two=2;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (two));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);

      switch (step_idx) {
        case 0: 
          { // Integer part of lifting step factor is -2.
            // The actual lifting factor is -1.586134
            register kdvis_d64 acc;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(sum), "e"(sum));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpsub16 %1, %2, %0"   : "=e"(acc): "e"(s1) , "e"(acc));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(acc), "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(acc));
                *(dp++) = d;
              }
          }
          break;
        case 1:
          { // Add source samples; multiply by remainder, then downshift.
            // The actual lifting factor is -0.05298
            kdvis_4x8 q_scale;
            q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
              (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3

            register kdvis_f32 scale = q_scale.f32;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
                asm("fpadd16 %1, %2, %0"   : "=e" (d) : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 2:
          { // Integer part of lifting step factor is 1.
            // The actual lifting factor is 0.882911
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s1));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 3:
          { // Integer part of lifting step factor is 0
            // The actual lifting factor is 0.443507
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        default:
          assert(0);
        } // End of switch statement
    }
  else
    { // In this case, sp[2] is aligned; add to non-aligned sp[0]
      register int zero=0;
      asm("alignaddr %1, %2, %0" : "=r" (sp) : "r" (sp), "r" (zero));
      assert((_addr_to_kdu_int32(sp) & 7) == 0);

      switch (step_idx) {
        case 0: 
          { // Integer part of lifting step factor is -2.
            // The actual lifting factor is -1.586134
            register kdvis_d64 acc;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(sum), "e"(sum));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpsub16 %1, %2, %0"   : "=e"(acc): "e"(s1) , "e"(acc));
                asm("fpadd16 %1, %2, %0"   : "=e"(acc): "e"(acc), "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(acc));
                *(dp++) = d;
              }
          }
          break;
        case 1:
          { // Add source samples; multiply by remainder, then downshift.
            // The actual lifting factor is -0.05298
            kdvis_4x8 q_scale;
            q_scale.v[0] = q_scale.v[1] = q_scale.v[2] = q_scale.v[3] =
              (kdu_byte)(0x80 >> 2);  // Emulates downshift by 3

            register kdvis_f32 scale = q_scale.f32;
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fmul8x16 %1, %2, %0"  : "=e"(sum): "f"(scale), "e"(sum));
                asm("fpadd16 %1, %2, %0"   : "=e" (d) : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 2:
          { // Integer part of lifting step factor is 1.
            // The actual lifting factor is 0.882911
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s1));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(sum), "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d)  , "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        case 3:
          { // Integer part of lifting step factor is 0
            // The actual lifting factor is 0.443507
            for (s1=*(sp++), n=quads; n > 0; n--, s1=s2)
              {
                s2 = *(sp++);
                asm("faligndata %1, %2, %0": "=e"(s3) : "e"(s1),  "e"(s2));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s2),  "e"(s3));
                asm("fmul8sux16 %1, %2, %0": "=e"(s1) : "e"(sum), "e"(factor));
                d = *dp;
                asm("fmul8ulx16 %1, %2, %0": "=e"(s3) : "e"(sum), "e"(factor));
                asm("fpadd16 %1, %2, %0"   : "=e"(sum): "e"(s1),  "e"(s3));
                asm("fpadd16 %1, %2, %0"   : "=e"(d)  : "e"(d),   "e"(sum));
                *(dp++) = d;
              }
          }
          break;
        default:
          assert(0);
        } // end of switch statement
    }
}


/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_sparcvis_exists) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vis_hlift_16_5x3_synth; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vis_hlift_16_9x7_synth; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vis_hlift_16_5x3_analysis; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vis_hlift_16_9x7_analysis; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
  }

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */

} // namespace kd_core_simd

#endif // GCC_DWT_SPARCVIS_LOCAL_H
