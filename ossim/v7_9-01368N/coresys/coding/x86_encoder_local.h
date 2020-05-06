/*****************************************************************************/
// File: x86_encoder_local.h [scope = CORESYS/CODING]
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
data between the DWT line-based processing engine and block encoder.  The
implementation here is based on MMX/SSE/SSE2/SSSE3/AVX intrinsics.  These
can be compiled under GCC or .NET and are compatible with both 32-bit and
64-bit builds.
   Everything above SSE2 is imported from separately compiled files,
"..._coder_local.cpp" so that the entire code base need not depend on the
 more advanced instructions.
   This file contains optimizations for the forward (quantization)
transfer of data from lines to code-blocks.
******************************************************************************/
#ifndef X86_ENCODER_LOCAL_H
#define X86_ENCODER_LOCAL_H

#include <emmintrin.h>
#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;
  
/* ========================================================================= */
/*                      SIMD functions used for encoding                     */
/* ========================================================================= */
  
/*****************************************************************************/
/* STATIC                 ..._quantize32_rev_block16                         */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern kdu_int32
    avx2_quantize32_rev_block16(kdu_int32 *,void **,
                                int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_kmax <= 15) && (_nom_width >= 16)) \
            _tgt = avx2_quantize32_rev_block16;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width)
#endif
  
#ifndef KDU_NO_SSSE3
  extern kdu_int32
    ssse3_quantize32_rev_block16(kdu_int32 *,void **,
                                 int,int,int,int,int,float);
#  define SSSE3_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_kmax <= 15) && (_nom_width >= 8)) \
            _tgt = ssse3_quantize32_rev_block16;
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width)
#endif
  
#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSSE3_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nw); \
        AVX2_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                ..._quantize32_rev_block32                          */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern kdu_int32
    avx2_quantize32_rev_block32(kdu_int32 *,void **,
                                int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_nom_width >= 8)) \
            _tgt = avx2_quantize32_rev_block32;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width)
#endif
  
#ifndef KDU_NO_SSSE3
  extern kdu_int32
    ssse3_quantize32_rev_block32(kdu_int32 *,void **,
                                 int,int,int,int,int,float);
#  define SSSE3_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_nom_width >= 4)) \
            _tgt = ssse3_quantize32_rev_block32;
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSSE3_SET_BLOCK_QUANT32_REV32(_tgt,_nw); \
        AVX2_SET_BLOCK_QUANT32_REV32(_tgt,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                ..._quantize32_irrev_block16                        */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern kdu_int32
    avx2_quantize32_irrev_block16(kdu_int32 *,void **,
                                  int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_kmax <= 15) && (_nom_width >= 16)) \
            _tgt = avx2_quantize32_irrev_block16;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width)
#endif

#ifndef KDU_NO_SSE
static kdu_int32
  sse2_quantize32_irrev_block16(kdu_int32 *dst, void **src_refs,
                                int src_offset, int src_width,
                                int dst_stride, int height,
                                int K_max, float delta)
{
  __m128i mask_src128[2] =
    { _mm_cmpeq_epi32(_mm_setzero_si128(),_mm_setzero_si128()),
      _mm_setzero_si128() };
  __m128i end_mask =
    _mm_loadu_si128((__m128i *)(((kdu_byte *)mask_src128) +
                                2*((-src_width)&7)));  
  float fscale = 1.0F / delta;
  fscale *= kdu_pwrof2f(15-K_max-KDU_FIX_POINT);
  kdu_int16 *nxt_src = ((kdu_int16 *)(src_refs[0])) + src_offset;
  __m128 fval1, fval2, pscale = _mm_set1_ps(fscale);
  __m128i or_val = _mm_setzero_si128();
  __m128i smask = _mm_setzero_si128();
  __m128i val1, val2, sign1, sign2;
  smask = _mm_slli_epi32(_mm_cmpeq_epi32(smask,smask),31); // -> 0x80000000
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m128i *sp = (__m128i *)nxt_src; // not necessarily aligned
      nxt_src = ((kdu_int16 *)(src_refs[1])) + src_offset;
      __m128i *dp = (__m128i *)dst; // not necessarily aligned
      for (int c=src_width; c > 8; c-=8, sp++, dp+=2)
        { // Process all but the last vector
          __m128i in_val = _mm_loadu_si128(sp);
          val1 = _mm_unpacklo_epi16(_mm_setzero_si128(),in_val);
          val2 = _mm_unpackhi_epi16(_mm_setzero_si128(),in_val);
          sign1 = _mm_and_si128(smask,val1); sign2 = _mm_and_si128(smask,val2);
          fval1 = _mm_cvtepi32_ps(val1);     fval2 = _mm_cvtepi32_ps(val2);
          fval1 = _mm_mul_ps(fval1,pscale);  fval2 = _mm_mul_ps(fval2,pscale);
          fval1 = _mm_xor_ps(fval1,_mm_castsi128_ps(sign1));
          fval2 = _mm_xor_ps(fval2,_mm_castsi128_ps(sign2));
          val1 = _mm_cvttps_epi32(fval1);    val2 = _mm_cvttps_epi32(fval2);
          or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
          val1 = _mm_or_si128(val1,sign1);   val2 = _mm_or_si128(val2,sign2);
          _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
        }
      { // Process the last vector, with source word masking
        __m128i in_val = _mm_loadu_si128(sp);
        in_val = _mm_and_si128(in_val,end_mask);
        val1 = _mm_unpacklo_epi16(_mm_setzero_si128(),in_val);
        val2 = _mm_unpackhi_epi16(_mm_setzero_si128(),in_val);
        sign1 = _mm_and_si128(smask,val1); sign2 = _mm_and_si128(smask,val2);
        fval1 = _mm_cvtepi32_ps(val1);     fval2 = _mm_cvtepi32_ps(val2);
        fval1 = _mm_mul_ps(fval1,pscale);  fval2 = _mm_mul_ps(fval2,pscale);
        fval1 = _mm_xor_ps(fval1,_mm_castsi128_ps(sign1));
        fval2 = _mm_xor_ps(fval2,_mm_castsi128_ps(sign2));
        val1 = _mm_cvttps_epi32(fval1);    val2 = _mm_cvttps_epi32(fval2);
        or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
        val1 = _mm_or_si128(val1,sign1);   val2 = _mm_or_si128(val2,sign2);
        _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
      }
    }
  val1 = _mm_srli_si128(or_val,8);
  val1 = _mm_or_si128(val1,or_val); // Leave 2 OR'd dwords in low part of val1
  val2 = _mm_srli_si128(val1,4);
  val1 = _mm_or_si128(val1,val2); // Leaves 1 OR'd dword in low part of val1
  return _mm_cvtsi128_si32(val1);
}
#  define SSE2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_nom_width >= 8)) \
             _tgt = sse2_quantize32_irrev_block16;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSE2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nw); \
        AVX2_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                 ..._quantize32_irrev_block32                       */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern kdu_int32
    avx2_quantize32_irrev_block32(kdu_int32 *,void **,
                                  int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_QUANT32_IRREV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_nom_width >= 8)) \
            _tgt = avx2_quantize32_irrev_block32;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_QUANT32_IRREV32(_tgt,_nom_width)
#endif
  
#ifndef KDU_NO_SSE
static kdu_int32
  sse2_quantize32_irrev_block32(kdu_int32 *dst, void **src_refs,
                                int src_offset, int src_width,
                                int dst_stride, int height,
                                int K_max, float delta)
{
  __m128i mask_src128[2] =
    { _mm_cmpeq_epi32(_mm_setzero_si128(),_mm_setzero_si128()),
      _mm_setzero_si128() };
  __m128 end_mask = _mm_loadu_ps(((float *)mask_src128) + ((-src_width)&3));  
  float fscale = 1.0F / delta;
  if (K_max <= 31)
    fscale *= (float)(1<<(31-K_max));
  else
    fscale /= (float)(1<<(K_max-31));
  float *nxt_src = ((float *)(src_refs[0])) + src_offset;
  __m128 fval1, fval2, fsign1, fsign2, pscale = _mm_set1_ps(fscale);
  __m128i or_val = _mm_setzero_si128();
  __m128i imask = _mm_setzero_si128();
  imask = _mm_slli_epi32(_mm_cmpeq_epi32(imask,imask),31); // -> 0x80000000
  __m128 fmask = _mm_castsi128_ps(imask);
  __m128i val1, val2;
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m128 *sp = (__m128 *)nxt_src; // not necessarily aligned
      nxt_src = ((float *)(src_refs[1])) + src_offset;
      __m128i *dp = (__m128i *)dst; // not necessarily aligned
      int c=src_width;
      for (; c > 8; c-=8, sp+=2, dp+=2)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          fval1 = _mm_loadu_ps((float *)sp);
          fval2 = _mm_loadu_ps((float *)(sp+1));
          fsign1 = _mm_and_ps(fmask,fval1);  fsign2 = _mm_and_ps(fmask,fval2);
          fval1 = _mm_mul_ps(fval1,pscale);  fval2 = _mm_mul_ps(fval2,pscale);
          fval1 = _mm_xor_ps(fval1,fsign1);  fval2 = _mm_xor_ps(fval2,fsign2);
          val1 = _mm_cvttps_epi32(fval1);    val2 = _mm_cvttps_epi32(fval2);
          or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
          val1 = _mm_or_si128(val1,_mm_castps_si128(fsign1));
          val2 = _mm_or_si128(val2,_mm_castps_si128(fsign2));
          _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
        }
      if (c > 4)
        { // Process two final vectors, with source word masking
          fval1 = _mm_loadu_ps((float *)sp);
          fval2 = _mm_loadu_ps((float *)(sp+1));
          fval2 = _mm_and_ps(fval2,end_mask);
          fsign1 = _mm_and_ps(fmask,fval1);  fsign2 = _mm_and_ps(fmask,fval2);
          fval1 = _mm_mul_ps(fval1,pscale);  fval2 = _mm_mul_ps(fval2,pscale);
          fval1 = _mm_xor_ps(fval1,fsign1);  fval2 = _mm_xor_ps(fval2,fsign2);
          val1 = _mm_cvttps_epi32(fval1);    val2 = _mm_cvttps_epi32(fval2);
          or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
          val1 = _mm_or_si128(val1,_mm_castps_si128(fsign1));
          val2 = _mm_or_si128(val2,_mm_castps_si128(fsign2));
          _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
        }
      else
        { // Process one final vectors, with source word masking
          fval1 = _mm_loadu_ps((float *)sp);
          fval1 = _mm_and_ps(fval1,end_mask);
          fsign1 = _mm_and_ps(fmask,fval1);
          fval1 = _mm_mul_ps(fval1,pscale);
          fval1 = _mm_xor_ps(fval1,fsign1);
          val1 = _mm_cvttps_epi32(fval1);
          or_val=_mm_or_si128(or_val,val1);
          val1 = _mm_or_si128(val1,_mm_castps_si128(fsign1));
          _mm_storeu_si128(dp,val1);
        }
    }
  val1 = _mm_srli_si128(or_val,8);
  val1 = _mm_or_si128(val1,or_val); // Leave 2 OR'd dwords in low part of val1
  val2 = _mm_srli_si128(val1,4);
  val1 = _mm_or_si128(val1,val2); // Leaves 1 OR'd dword in low part of val1
  return _mm_cvtsi128_si32(val1);
}
#  define SSE2_SET_BLOCK_QUANT32_IRREV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_nom_width >= 4)) \
            _tgt = sse2_quantize32_irrev_block32;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_QUANT32_IRREV32(_tgt)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSE2_SET_BLOCK_QUANT32_IRREV32(_tgt,_nw); \
        AVX2_SET_BLOCK_QUANT32_IRREV32(_tgt,_nw); \
      } \
  }
  
} // namespace kd_core_simd

#endif // X86_ENCODER_LOCAL_H
