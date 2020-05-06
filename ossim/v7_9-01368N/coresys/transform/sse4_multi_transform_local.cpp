/*****************************************************************************/
// File: sse4_multi_component_local.cpp [scope = CORESYS/TRANSFORMS]
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
transform functions, where the accelerators require support for
SSE through SSE4.1 instruction sets only.  The functions are not built if
`KDU_NO_SSE4' is defined or `KDU_X86_INTRINSICS' is not defined.  They
are not used at run-time unless the processor indicates support for the
relevant instruction sets.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_SSE4) && (defined KDU_X86_INTRINSICS))

#include <smmintrin.h>
#include <assert.h>

using namespace kdu_core;


namespace kd_core_simd {
  
/*****************************************************************************/
/* EXTERN                     sse4_smag_int32                                */
/*****************************************************************************/
  
void sse4_smag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                     int precision, bool src_absolute, bool dst_absolute)
{
  assert(precision <= 32);
  kdu_int32 min_val = ((kdu_int32) -1) << (precision-1);
  kdu_int32 max_val = ~min_val;
  if (!src_absolute)
    { // Synthesis conversion from floats to absolute ints
      int mxcsr_orig = _mm_getcsr();
      int mxcsr_cur = mxcsr_orig & ~(3<<13); // Force "round to nearest" mode
      _mm_setcsr(mxcsr_cur);
      __m128 *sp = (__m128 *)src;
      __m128i *dp = (__m128i *)dst;
      __m128 vec_scale = _mm_set1_ps(kdu_pwrof2f(precision));
      __m128 vec_fmin = _mm_set1_ps((float)min_val);
      __m128 vec_fmax = _mm_set1_ps((float)max_val);
      __m128i vec_min = _mm_set1_epi32(min_val);
      __m128i vec_zero = _mm_setzero_si128();
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128 fval = *sp;
          fval = _mm_mul_ps(fval,vec_scale);
          fval = _mm_max_ps(fval,vec_fmin);
          fval = _mm_min_ps(fval,vec_fmax);
          __m128i int_val = _mm_cvtps_epi32(fval);
          __m128i neg_mask = _mm_cmplt_epi32(int_val,vec_zero);
          int_val = _mm_xor_si128(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = _mm_and_si128(neg_mask,vec_min); // Leaves min_val or 0
          int_val = _mm_or_si128(int_val,neg_mask);
          *dp = int_val;
        }
       _mm_setcsr(mxcsr_orig); // Restore rounding control bits
    }
  else if (!dst_absolute)
    { // Analysis conversion from absolute ints to floats
      __m128i *sp = (__m128i *)src;
      __m128 *dp = (__m128 *)dst;
      __m128 vec_scale = _mm_set1_ps(kdu_pwrof2f(-precision));
      __m128i vec_min = _mm_set1_epi32(min_val);
      __m128i vec_max = _mm_set1_epi32(max_val);
      __m128i vec_zero = _mm_setzero_si128();
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128i int_val = *sp;
          __m128i neg_mask = _mm_cmplt_epi32(int_val,vec_zero);
          int_val = _mm_max_epi32(int_val,vec_min);
          int_val = _mm_min_epi32(int_val,vec_max);
          int_val = _mm_xor_si128(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = _mm_and_si128(neg_mask,vec_min); // Leaves min_val or 0
          int_val = _mm_or_si128(int_val,neg_mask);
          __m128 fval = _mm_cvtepi32_ps(int_val);
          fval = _mm_mul_ps(fval,vec_scale);
          *dp = fval;
        }
    }
  else
    { // Analysis/Synthesis conversion between absolute ints
      __m128i *sp=(__m128i *)src, *dp=(__m128i *)dst;
      __m128i vec_min = _mm_set1_epi32(min_val);
      __m128i vec_max = _mm_set1_epi32(max_val);
      __m128i vec_zero = _mm_setzero_si128();
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128i int_val = *sp;
          __m128i neg_mask = _mm_cmplt_epi32(int_val,vec_zero);
          int_val = _mm_max_epi32(int_val,vec_min);
          int_val = _mm_min_epi32(int_val,vec_max);
          int_val = _mm_xor_si128(int_val,neg_mask); // 1's comp of -ve samples
          neg_mask = _mm_and_si128(neg_mask,vec_min); // Leaves min_val or 0
          int_val = _mm_or_si128(int_val,neg_mask);
          *dp = int_val;
        }
    }
}

/*****************************************************************************/
/* EXTERN                     sse4_umag_int32                                */
/*****************************************************************************/

void sse4_umag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                     int precision, bool src_absolute, bool dst_absolute)
{
  assert(precision <= 32);
  kdu_int32 min_val = ((kdu_int32) -1) << (precision-1);
  kdu_int32 max_val = ~min_val;
  if (!src_absolute)
    { // Synthesis conversion from floats to absolute ints
      int mxcsr_orig = _mm_getcsr();
      int mxcsr_cur = mxcsr_orig & ~(3<<13); // Force "round to nearest" mode
      _mm_setcsr(mxcsr_cur);
      __m128 *sp = (__m128 *)src;
      __m128i *dp = (__m128i *)dst;
      __m128 vec_scale = _mm_set1_ps(kdu_pwrof2f(precision));
      __m128 vec_fmin = _mm_set1_ps((float)min_val);
      __m128 vec_fmax = _mm_set1_ps((float)max_val);
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128 fval = *sp;
          fval = _mm_mul_ps(fval,vec_scale);
          fval = _mm_max_ps(fval,vec_fmin);
          fval = _mm_min_ps(fval,vec_fmax);
          __m128i int_val = _mm_cvtps_epi32(fval);
          *dp = int_val;
        }
      _mm_setcsr(mxcsr_orig); // Restore rounding control bits
    }
  else if (!dst_absolute)
    { // Analysis conversion from absolute ints to floats
      __m128i *sp = (__m128i *)src;
      __m128 *dp = (__m128 *)dst;
      __m128 vec_scale = _mm_set1_ps(kdu_pwrof2f(-precision));
      __m128i vec_min = _mm_set1_epi32(min_val);
      __m128i vec_max = _mm_set1_epi32(max_val);
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128i int_val = *sp;
          int_val = _mm_max_epi32(int_val,vec_min);
          int_val = _mm_min_epi32(int_val,vec_max);
          __m128 fval = _mm_cvtepi32_ps(int_val);
          fval = _mm_mul_ps(fval,vec_scale);
          *dp = fval;
        }
    }
  else
    { // Analysis/Synthesis conversion between absolute ints
      __m128i *sp=(__m128i *)src, *dp=(__m128i *)dst;
      __m128i vec_min = _mm_set1_epi32(min_val);
      __m128i vec_max = _mm_set1_epi32(max_val);
      for (; num_samples > 0; num_samples-=4, sp++, dp++)
        { 
          __m128i int_val = *sp;
          int_val = _mm_max_epi32(int_val,vec_min);
          int_val = _mm_min_epi32(int_val,vec_max);
          *dp = int_val;
        }
    }
}

  
} // namespace kd_core_simd

#endif // !KDU_NO_SSE4
