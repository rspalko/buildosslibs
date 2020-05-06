/*****************************************************************************/
// File: ssse3_region_decompressor.cpp [scope = APPS/SUPPORT]
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
   Provides SIMD implementations to accelerate sample data conversions
for "kdu_region_decompressor", where the accelerator functions require
support for SSE through SSE4.1 instruction sets only.  The functions
are not built if `KDU_NO_SSE4' is defined, or `KDU_X86_INTRINSICS' is
not defined.  They are not used at run-time unless the processor indicates
support for the relevant instruction sets.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_SSE4) && (defined KDU_X86_INTRINSICS))

#include <smmintrin.h>
#include <math.h>
#include <assert.h>

// Convenience macros reproduced from "region_decompressor_local.h"
#define KDRD_FIX16_TYPE 1 /* 16-bit fixed-point, KDU_FIX_POINT frac bits. */
#define KDRD_INT16_TYPE 2 /* 16-bit absolute integers. */
#define KDRD_FLOAT_TYPE 4 /* 32-bit floats, unit nominal range. */
#define KDRD_INT32_TYPE 8 /* 32-bit absolute integers. */

#define KDRD_ABSOLUTE_TYPE (KDRD_INT16_TYPE | KDRD_INT32_TYPE)
#define KDRD_SHORT_TYPE (KDRD_FIX16_TYPE | KDRD_INT16_TYPE)

namespace kd_supp_simd {
  using namespace kdu_core;


/* ========================================================================= */
/*                          Data Conversion Functions                        */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN         sse4_reinterpret_and_copy_to_unsigned_floats               */
/*****************************************************************************/

void
  sse4_reinterpret_and_copy_to_unsigned_floats(const void *bufs[],
                                               const int widths[],
                                               const int types[],
                                               int num_lines,
                                               int precision,
                                               int missing_src_samples,
                                               void *void_dst, int dst_min,
                                               int num_samples, int dst_type,
                                               int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0) &&
         (precision <= 32) && (precision > exponent_bits) &&
         (exponent_bits <= 8) && ((precision-1-exponent_bits) <= 23));
  float *dst = ((float *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case; no need to be efficient at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const kdu_int32 *src = (const kdu_int32 *)(*(bufs++));
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  while (missing_src_samples < 0)
    { 
      int n = -missing_src_samples;
      src += n;
      if ((src_len -= n) > 0)
        { missing_src_samples = 0; break; }
      else if (num_lines > 0)
        { 
          missing_src_samples = src_len; // Necessarily <= 0
          src = (const kdu_int32 *)(*(bufs++));
          src_len=*(widths++); src_type=*(types++); num_lines--;
        }
      else
        { // Need to replicate the last source sample
          assert((src_len+n) > 0); // Last source line required to be non-empty
          src += src_len-1; // Takes us to the last source sample
          src_len = 1; // Always use this last sample
          missing_src_samples = 0; break;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Prepare the conversion parameters
  int mantissa_bits = precision - 1 - exponent_bits;
  assert(mantissa_bits >= 0);
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  assert((mantissa_upshift >= 0) && // If these two conditions do not hold
         (exp_off <= 127)); // the accelerator should not have been installed.
  float denorm_scale = kdu_pwrof2f(127-exp_off); // For normalizing denormals
  int exp_max = 2*exp_off;
  
  __m128i vec_in_off = _mm_set1_epi32(1<<(precision-1));
  __m128i vec_in_min = _mm_setzero_si128();
  __m128i vec_in_max = _mm_set1_epi32(((exp_max+1)<<mantissa_bits)-1);
  vec_in_min = _mm_sub_epi32(vec_in_min,vec_in_off);
  vec_in_max = _mm_sub_epi32(vec_in_max,vec_in_off);
  __m128i vec_upshift = _mm_cvtsi32_si128(mantissa_upshift);
  __m128 vec_out_scale = _mm_set1_ps(denorm_scale);
  __m128 vec_half = _mm_set1_ps(0.5f);
  
  // Now perform the sample conversion process
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      __m128i in_vec = _mm_cvtsi32_si128(src[0]);
      in_vec = _mm_max_epi32(in_vec,vec_in_min);
      in_vec = _mm_min_epi32(in_vec,vec_in_max);
      in_vec = _mm_add_epi32(in_vec,vec_in_off);
      in_vec = _mm_sll_epi32(in_vec,vec_upshift);
      __m128 out_vec = _mm_castsi128_ps(in_vec);
      out_vec = _mm_mul_ps(out_vec,vec_out_scale);
      out_vec = _mm_sub_ps(out_vec,vec_half);
      float fval = _mm_cvtss_f32(out_vec);      
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      if (src_len > 0)
        { // Else source type might be 0 (undefined)
          if (src_type != KDRD_INT32_TYPE)
            assert(0);
          float *dp = dst;
          if (src_len > num_samples)
            src_len = num_samples;
          dst += src_len;
          num_samples -= src_len;
          int lead=(-((_addr_to_kdu_int32(dp))>>2))&3; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          for (; lead > 0; lead--, src++, dp++)
            { // Do conversion vector by vector
              __m128i in_vec = _mm_cvtsi32_si128(src[0]);
              in_vec = _mm_max_epi32(in_vec,vec_in_min);
              in_vec = _mm_min_epi32(in_vec,vec_in_max);
              in_vec = _mm_add_epi32(in_vec,vec_in_off);
              in_vec = _mm_sll_epi32(in_vec,vec_upshift);
              __m128 out_vec = _mm_castsi128_ps(in_vec);
              out_vec = _mm_mul_ps(out_vec,vec_out_scale);
              out_vec = _mm_sub_ps(out_vec,vec_half);
              dp[0] = _mm_cvtss_f32(out_vec);
            }
          for (; src_len > 0; src_len-=4, src+=4, dp+=4)
            { // Do vector conversion, 4 floats at a time
              __m128i in_vec = _mm_loadu_si128((__m128i *)src);
              in_vec = _mm_max_epi32(in_vec,vec_in_min);
              in_vec = _mm_min_epi32(in_vec,vec_in_max);
              in_vec = _mm_add_epi32(in_vec,vec_in_off);
              in_vec = _mm_sll_epi32(in_vec,vec_upshift);
              __m128 out_vec = _mm_castsi128_ps(in_vec);
              out_vec = _mm_mul_ps(out_vec,vec_out_scale);
              out_vec = _mm_sub_ps(out_vec,vec_half);
              ((__m128 *)dp)[0] = out_vec;
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src = (const kdu_int32 *)(*(bufs++));
      src_len=*(widths++); src_type=*(types++); num_lines--;
    }
  // Perform right edge padding as required
  for (float fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}
  
/*****************************************************************************/
/* EXTERN          sse4_reinterpret_and_copy_to_signed_floats                */
/*****************************************************************************/

void
  sse4_reinterpret_and_copy_to_signed_floats(const void *bufs[],
                                             const int widths[],
                                             const int types[],
                                             int num_lines,
                                             int precision,
                                             int missing_src_samples,
                                             void *void_dst, int dst_min,
                                             int num_samples, int dst_type,
                                             int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0) &&
         (precision <= 32) && (precision > exponent_bits) &&
         (exponent_bits <= 8) && ((precision-1-exponent_bits) <= 23));
  float *dst = ((float *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case; no need to be efficient at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const kdu_int32 *src = (const kdu_int32 *)(*(bufs++));
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  while (missing_src_samples < 0)
    { 
      int n = -missing_src_samples;
      src += n;
      if ((src_len -= n) > 0)
        { missing_src_samples = 0; break; }
      else if (num_lines > 0)
        { 
          missing_src_samples = src_len; // Necessarily <= 0
          src = (const kdu_int32 *)(*(bufs++));
          src_len=*(widths++); src_type=*(types++); num_lines--;
        }
      else
        { // Need to replicate the last source sample
          assert((src_len+n) > 0); // Last source line required to be non-empty
          src += src_len-1; // Takes us to the last source sample
          src_len = 1; // Always use this last sample
          missing_src_samples = 0; break;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Prepare the conversion parameters
  int mantissa_bits = precision - 1 - exponent_bits;
  assert(mantissa_bits >= 0);
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  assert((mantissa_upshift >= 0) && // If these two conditions do not hold
         (exp_off <= 127)); // the accelerator should not have been installed.
  float denorm_scale = kdu_pwrof2f(127-exp_off); // For normalizing denormals
  int exp_max = 2*exp_off;
  
  __m128i vec_mag_max = _mm_set1_epi32(((exp_max+1)<<mantissa_bits)-1);
  __m128i vec_sign_mask = _mm_set1_epi32(KDU_INT32_MIN);
  __m128i vec_mag_mask = _mm_set1_epi32(~(((kdu_int32)-1)<<(precision-1)));
  __m128i vec_upshift = _mm_cvtsi32_si128(mantissa_upshift);
  __m128 vec_out_scale = _mm_set1_ps(denorm_scale*0.5f);
  
  // Now perform the sample conversion process
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      __m128i in_vec = _mm_cvtsi32_si128(src[0]);
      __m128i sign_vec = _mm_and_si128(in_vec,vec_sign_mask);
      in_vec = _mm_and_si128(in_vec,vec_mag_mask);
      in_vec = _mm_min_epi32(in_vec,vec_mag_max);
      in_vec = _mm_sll_epi32(in_vec,vec_upshift);
      in_vec = _mm_or_si128(in_vec,sign_vec);
      __m128 out_vec = _mm_castsi128_ps(in_vec);
      out_vec = _mm_mul_ps(out_vec,vec_out_scale);
      float fval = _mm_cvtss_f32(out_vec);      
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      if (src_len > 0)
        { // Else source type might be 0 (undefined)
          if (src_type != KDRD_INT32_TYPE)
            assert(0);
          float *dp = dst;
          if (src_len > num_samples)
            src_len = num_samples;
          dst += src_len;
          num_samples -= src_len;
          int lead=(-((_addr_to_kdu_int32(dp))>>2))&3; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          for (; lead > 0; lead--, src++, dp++)
            { // Do conversion vector by vector
              __m128i in_vec = _mm_cvtsi32_si128(src[0]);
              __m128i sign_vec = _mm_and_si128(in_vec,vec_sign_mask);
              in_vec = _mm_and_si128(in_vec,vec_mag_mask);
              in_vec = _mm_min_epi32(in_vec,vec_mag_max);
              in_vec = _mm_sll_epi32(in_vec,vec_upshift);
              in_vec = _mm_or_si128(in_vec,sign_vec);
              __m128 out_vec = _mm_castsi128_ps(in_vec);
              out_vec = _mm_mul_ps(out_vec,vec_out_scale);
              dp[0] = _mm_cvtss_f32(out_vec);
            }
          for (; src_len > 0; src_len-=4, src+=4, dp+=4)
            { // Do vector conversion, 4 floats at a time
              __m128i in_vec = _mm_loadu_si128((__m128i *)src);
              __m128i sign_vec = _mm_and_si128(in_vec,vec_sign_mask);
              in_vec = _mm_and_si128(in_vec,vec_mag_mask);
              in_vec = _mm_min_epi32(in_vec,vec_mag_max);
              in_vec = _mm_sll_epi32(in_vec,vec_upshift);
              in_vec = _mm_or_si128(in_vec,sign_vec);
              __m128 out_vec = _mm_castsi128_ps(in_vec);
              out_vec = _mm_mul_ps(out_vec,vec_out_scale);
              ((__m128 *)dp)[0] = out_vec;
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src = (const kdu_int32 *)(*(bufs++));
      src_len=*(widths++); src_type=*(types++); num_lines--;
    }
  // Perform right edge padding as required
  for (float fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}
  
} // namespace kd_supp_simd

#endif // !KDU_NO_SSE4
