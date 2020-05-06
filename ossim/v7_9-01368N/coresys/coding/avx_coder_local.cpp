/*****************************************************************************/
// File: avx_coder_local.cpp [scope = CORESYS/CODING]
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
   Provides SIMD implementations to accelerate the conversion and transfer of
data between the block coder and DWT line-based processing engine.  This
source file is required to implement AVX versions of the data transfer
functions -- placing the code in a separate file allows the compiler to
be instructed to use vex-prefixed instructions exclusively, which avoids
processor state transition costs.  There is no harm in including this
source file with all builds, even if AVX is not supported, so long as you
are careful to globally define the `KDU_NO_AVX' compilation directive.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER
#include <assert.h>

using namespace kdu_core;

namespace kd_core_simd {
  
/* ========================================================================= */
/*                         Now for the SIMD functions                        */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                avx_xfer_irrev_decoded_block16                      */
/*****************************************************************************/

void
  avx_xfer_irrev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
                                 int dst_offset_in, int dst_width,
                                 int src_stride, int height,
                                 int K_max, float delta)
{
  float fscale = delta * (float)(1<<KDU_FIX_POINT);
  if (K_max <= 31)
    fscale /= (float)(1<<(31-K_max));
  else
    fscale *= (float)(1<<(K_max-31));
  int mxcsr_orig = _mm_getcsr();
  int mxcsr_cur = mxcsr_orig & ~(3<<13); // Reset rounding control bits
  _mm_setcsr(mxcsr_cur);
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - 2*align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;
  int src_bp_overshoot = 2*((dst_span_bytes+15) & ~15) - 4*src_stride;
  __m256 vec_scale = _mm256_set1_ps(fscale);
  __m256 smask=_mm256_castsi256_ps(_mm256_set1_epi32(0x80000000));
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      __m256 v1, v2, v3, v4, s1, s2, s3, s4;
      __m256i iv1, iv2, iv3, iv4;
      __m128i e1, e2, e3, e4;
      for (n=dst_span_bytes; n > 48; n-=64, dst+=4, src_bp+=128)
        { // Write 4 vectors (64 bytes) at once, with overwrite < 1 vector 
          v1 = _mm256_loadu_ps((float *)(src_bp+0));
          v2 = _mm256_loadu_ps((float *)(src_bp+32));
          v3 = _mm256_loadu_ps((float *)(src_bp+64));
          v4 = _mm256_loadu_ps((float *)(src_bp+96));
          s1 = _mm256_and_ps(v1,smask);     s2 = _mm256_and_ps(v2,smask);
          s3 = _mm256_and_ps(v3,smask);     s4 = _mm256_and_ps(v4,smask);
          v1 = _mm256_andnot_ps(smask,v1);  v2 = _mm256_andnot_ps(smask,v2);
          v3 = _mm256_andnot_ps(smask,v3);  v4 = _mm256_andnot_ps(smask,v4);
          v1 = _mm256_cvtepi32_ps(_mm256_castps_si256(v1));
          v2 = _mm256_cvtepi32_ps(_mm256_castps_si256(v2));
          v3 = _mm256_cvtepi32_ps(_mm256_castps_si256(v3));
          v4 = _mm256_cvtepi32_ps(_mm256_castps_si256(v4));
          v1 = _mm256_mul_ps(v1,vec_scale); v2 = _mm256_mul_ps(v2,vec_scale);
          v3 = _mm256_mul_ps(v3,vec_scale); v4 = _mm256_mul_ps(v4,vec_scale);
          v1 = _mm256_or_ps(v1,s1);         v2 = _mm256_or_ps(v2,s2);
          v3 = _mm256_or_ps(v3,s3);         v4 = _mm256_or_ps(v4,s4);
          iv1 = _mm256_cvtps_epi32(v1);     iv2 = _mm256_cvtps_epi32(v2);
          iv3 = _mm256_cvtps_epi32(v3);     iv4 = _mm256_cvtps_epi32(v4);
          e1 = _mm256_extractf128_si256(iv1,1);
          e2 = _mm256_extractf128_si256(iv2,1);
          e3 = _mm256_extractf128_si256(iv3,1);
          e4 = _mm256_extractf128_si256(iv4,1);
          e1 = _mm_packs_epi32(_mm256_castsi256_si128(iv1),e1);
          e2 = _mm_packs_epi32(_mm256_castsi256_si128(iv2),e2);
          e3 = _mm_packs_epi32(_mm256_castsi256_si128(iv3),e3);
          e4 = _mm_packs_epi32(_mm256_castsi256_si128(iv4),e4);
          _mm_stream_ps((float *) dst,    _mm_castsi128_ps(e1));
          _mm_stream_ps((float *)(dst+1), _mm_castsi128_ps(e2));
          _mm_stream_ps((float *)(dst+2), _mm_castsi128_ps(e3));
          _mm_stream_ps((float *)(dst+3), _mm_castsi128_ps(e4));
            //dst[0]=e1;  dst[1]=e2;  dst[2]=e3;  dst[3]=e4;
        }
      for (; n > 0; n-=16, dst++, src_bp+=32)
        { 
          v1 = _mm256_loadu_ps((float *)(src_bp+0));
          s1 = _mm256_and_ps(v1,smask);
          v1 = _mm256_andnot_ps(smask,v1);
          v1 = _mm256_cvtepi32_ps(_mm256_castps_si256(v1));
          v1 = _mm256_mul_ps(v1,vec_scale);
          v1 = _mm256_or_ps(v1,s1);
          iv1 = _mm256_cvtps_epi32(v1);
          e1 = _mm256_castsi256_si128(iv1);
          e2 = _mm256_extractf128_si256(iv1,1);
          _mm_stream_ps((float *) dst,
                        _mm_castsi128_ps(_mm_packs_epi32(e1,e2)));
          //dst[0] = _mm_packs_epi32(e1,e2);
        }
    }
  _mm_setcsr(mxcsr_orig); // Restore rounding control bits
}

/*****************************************************************************/
/* EXTERN                avx_xfer_irrev_decoded_block32                      */
/*****************************************************************************/

void
  avx_xfer_irrev_decoded_block32(kdu_int32 *src_in, void **dst_refs,
                                 int dst_offset_in, int dst_width,
                                 int src_stride, int height,
                                 int K_max, float delta)
{
  float fscale = delta;
  if (K_max <= 31)
    fscale /= (float)(1<<(31-K_max));
  else
    fscale *= (float)(1<<(K_max-31));
  int mxcsr_orig = _mm_getcsr();
  int mxcsr_cur = mxcsr_orig & ~(3<<13); // Reset rounding control bits
  _mm_setcsr(mxcsr_cur);
  int dst_offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 4*dst_width + align_bytes;
  int src_bp_overshoot = ((dst_span_bytes+31) & ~31) - 4*src_stride;
  __m256 vec_scale = _mm256_set1_ps(fscale);
  __m256 smask=_mm256_castsi256_ps(_mm256_set1_epi32(0x80000000));
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m256 *dst = (__m256 *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 31) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 0; n-=32, dst++, src_bp+=32)
        { 
          __m256 ival = _mm256_loadu_ps((float *)(src_bp+0));
          __m256 sval = _mm256_and_ps(ival,smask); // Save sign bits
          __m256 mval = _mm256_andnot_ps(smask,ival); // Remove sign
          __m256 fval = _mm256_cvtepi32_ps(_mm256_castps_si256(mval));
          fval = _mm256_mul_ps(fval,vec_scale);
          _mm256_stream_ps((float *)dst, _mm256_or_ps(fval,sval));
          //dst[0] = _mm256_or_ps(fval,sval);
        }
    }
  _mm_setcsr(mxcsr_orig); // Restore rounding control bits
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_AVX
