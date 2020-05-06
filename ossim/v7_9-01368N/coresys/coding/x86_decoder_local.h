/*****************************************************************************/
// File: x86_decoder_local.h [scope = CORESYS/CODING]
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
data between the block coder and DWT line-based processing engine.  The
implementation here is based on MMX/SSE/SSE2/SSSE3/AVX intrinsics.  These
can be compiled under GCC or .NET and are compatible with both 32-bit and
64-bit builds.
   Everything above SSE2 is imported from separately compiled files,
"..._coder_local.cpp" so that the entire code base need not depend on the
more advanced instructions. 
   This file contains optimizations for the reverse (dequantization)
transfer of data from code-blocks to lines.
******************************************************************************/
#ifndef X86_DECODER_LOCAL_H
#define X86_DECODER_LOCAL_H

#include <emmintrin.h>
#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;
  
/* ========================================================================= */
/*                      SIMD functions used for decoding                     */
/* ========================================================================= */
  
/*****************************************************************************/
/* STATIC                    ..._zero_decoded_block16                        */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_zero_decoded_block16(void **dst_refs, int dst_offset_in,
                            int dst_width, int height)
{
  int offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + offset_bytes;
  int align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  int n, span_bytes = 2*dst_width + align_bytes;
  nxt_dst -= align_bytes;    offset_bytes -= align_bytes;
  __m128i zero = _mm_setzero_si128();
  for (; height > 0; height--, dst_refs++)
    { // NB: always safe to read 1 entry beyond end of the `dst_refs' array
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + offset_bytes;
      for (n=span_bytes; n > 48; n-=64, dst+=4)
        { // Write 4 vectors (64 bytes) at once, with overwrite of < 1 vector
          dst[0] = zero;  dst[1] = zero;  dst[2] = zero;  dst[3] = zero;
        }
      for (; n > 0; n-=16, dst++)
        dst[0] = zero;
    }
}
#  define SSE2_SET_BLOCK_ZERO16(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 2) && (_nom_width >= 8)) \
            _tgt = sse2_zero_decoded_block16;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_ZERO16(_tgt,z_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_ZERO16(_tgt,_nw) \
  { \
    SSE2_SET_BLOCK_ZERO16(_tgt,_nw); \
  }

/*****************************************************************************/
/* STATIC                   ..._zero_decoded_block32                         */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_zero_decoded_block32(void **dst_refs, int dst_offset_in,
                            int dst_width, int height)
{
  int offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + offset_bytes;
  int align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  int n, span_bytes = 4*dst_width + align_bytes;
  nxt_dst -= align_bytes;    offset_bytes -= align_bytes;
  __m128i zero = _mm_setzero_si128();
  for (; height > 0; height--, dst_refs++)
    { // NB: always safe to read 1 entry beyond end of the `dst_refs' array
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + offset_bytes;
      for (n=span_bytes; n > 48; n-=64, dst+=4)
        { // Write 4 vectors (64 bytes) at once, with overwrite of < 1 vector
          dst[0] = zero;  dst[1] = zero;  dst[2] = zero;  dst[3] = zero;
        }
      for (; n > 0; n-=16, dst++)
        dst[0] = zero;
    }
}
#  define SSE2_SET_BLOCK_ZERO32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 2) && (_nom_width >= 4)) \
            _tgt = sse2_zero_decoded_block32;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_ZERO32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_ZERO32(_tgt,_nw) \
  { \
    SSE2_SET_BLOCK_ZERO32(_tgt,_nw); \
  }

/*****************************************************************************/
/* STATIC                 ..._xfer_rev_decoded_block16                       */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern void
    avx2_xfer_rev_decoded_block16(kdu_int32 *,void **,
                                  int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_nom_width >= 16)) \
            _tgt = avx2_xfer_rev_decoded_block16;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width)
#endif
  
#ifndef KDU_NO_SSSE3
  extern void
    ssse3_xfer_rev_decoded_block16(kdu_int32 *,void **,
                                   int,int,int,int,int,float);
#  define SSSE3_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_nom_width >= 8)) \
            _tgt = ssse3_xfer_rev_decoded_block16;
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width)
#endif

#if ((!defined KDU_NO_SSE) && (KDU_MIN_MMX_LEVEL < 4))
static void 
  sse2_xfer_rev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
                                int dst_offset_in, int dst_width,
                                int src_stride, int height,
                                int K_max, float delta_unused)
{
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - 2*align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;
  int src_bp_overshoot = 2*((dst_span_bytes+15) & ~15) - 4*src_stride;
  __m128i downshift = _mm_cvtsi32_si128(31-K_max);
  __m128i comp = _mm_setzero_si128(); // Avoid compiler warnings
  comp = _mm_cmpeq_epi32(comp,comp); // Fill with FF's
  __m128i ones = _mm_srli_epi32(comp,31); // Set each DWORD equal to 1
  __m128i kmax = _mm_cvtsi32_si128(K_max);
  comp = _mm_sll_epi32(comp,kmax); // Leaves 1+downshift 1's in MSB's
  comp = _mm_or_si128(comp,ones); // `comp' now holds the amount we have to
        // add after inverting the bits of a downshifted sign-mag quantity
        // which was negative, to restore the correct 2's complement value.
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 0; n-=16, dst++, src_bp+=32)
        { 
          __m128i val1 = _mm_loadu_si128((__m128i *)(src_bp+0));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src_bp+16));
          __m128i ref = _mm_setzero_si128();
          ref = _mm_cmpgt_epi32(ref,val1); // Fills DWORDS with 1's if val<0
          val1 = _mm_xor_si128(val1,ref);
          val1 = _mm_sra_epi32(val1,downshift);
          ref = _mm_and_si128(ref,comp); // Leave the bits we need to add
          val1 = _mm_add_epi32(val1,ref); // Finish conversion to 2's comp
          ref = _mm_setzero_si128();
          ref = _mm_cmpgt_epi32(ref,val2); // Fills DWORDS with 1's if val<0
          val2 = _mm_xor_si128(val2,ref);
          val2 = _mm_sra_epi32(val2,downshift);
          ref = _mm_and_si128(ref,comp); // Leave the bits we need to add
          val2 = _mm_add_epi32(val2,ref); // Finish conversion to 2's comp
          dst[0] = _mm_packs_epi32(val1,val2);
        }
    }
}
#  define SSE2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 2) && (_nom_width >= 8)) \
            _tgt = sse2_xfer_rev_decoded_block16;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_XFER_REV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSE2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nw); \
        SSSE3_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nw); \
        AVX2_SET_BLOCK_XFER_REV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                ..._xfer_rev_decoded_block32                        */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
  extern void
    avx2_xfer_rev_decoded_block32(kdu_int32 *,void **,
                                  int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_XFER_REV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_nom_width >= 8)) \
            _tgt = avx2_xfer_rev_decoded_block32;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_XFER_REV32(_tgt,_nom_width)
#endif
  
#ifndef KDU_NO_SSSE3
  extern void
    ssse3_xfer_rev_decoded_block32(kdu_int32 *,void **,
                                   int,int,int,int,int,float);
#  define SSSE3_SET_BLOCK_XFER_REV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 4) && (_nom_width >= 4)) \
            _tgt = ssse3_xfer_rev_decoded_block32;
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_BLOCK_XFER_REV32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_XFER_REV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSSE3_SET_BLOCK_XFER_REV32(_tgt,_nw); \
        AVX2_SET_BLOCK_XFER_REV32(_tgt,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC               ..._xfer_irrev_decoded_block16                       */
/*****************************************************************************/
  
#ifndef KDU_NO_AVX
  extern void
    avx_xfer_irrev_decoded_block16(kdu_int32 *,void **,
                                   int,int,int,int,int,float);
#  define AVX_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 6) && (_nom_width >= 8)) \
            _tgt = avx_xfer_irrev_decoded_block16;
#else // !KDU_NO_AVX
#  define AVX_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width)
#endif
  
#ifndef KDU_NO_AVX2
  extern void
    avx2_xfer_irrev_decoded_block16(kdu_int32 *,void **,
                                    int,int,int,int,int,float);
#  define AVX2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 7) && (_nom_width >= 16)) \
            _tgt = avx2_xfer_irrev_decoded_block16;
#else // !KDU_NO_AVX2
#  define AVX2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width)
#endif
  
#if ((!defined KDU_NO_SSE) && (KDU_MIN_MMX_LEVEL < 6))
static void
  sse2_xfer_irrev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
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
  __m128 vec_scale = _mm_load1_ps(&fscale);
  __m128i comp = _mm_setzero_si128(); // Avoid compiler warnings
  __m128i ones = _mm_cmpeq_epi32(comp,comp); // Fill with FF's
  comp = _mm_slli_epi32(ones,31); // Each DWORD  now holds 0x80000000
  ones = _mm_srli_epi32(ones,31); // Each DWORD now holds 1
  comp = _mm_or_si128(comp,ones); // Each DWORD now holds 0x800000001
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 0; n-=16, dst++, src_bp+=32)
        { 
          __m128i val1 = _mm_loadu_si128((__m128i *)(src_bp+0));
          __m128i ref = _mm_setzero_si128();
          ref = _mm_cmpgt_epi32(ref,val1); // Fills DWORDS with 1's if val<0
          val1 = _mm_xor_si128(val1,ref);
          ref = _mm_and_si128(ref,comp); // Leave the bits we need to add
          val1 = _mm_add_epi32(val1,ref); // Finish conversion to 2's comp
          __m128 fval1 = _mm_cvtepi32_ps(val1);
          fval1 = _mm_mul_ps(fval1,vec_scale);
          val1 = _mm_cvtps_epi32(fval1);

          __m128i val2 = _mm_loadu_si128((__m128i *)(src_bp+16));
          ref = _mm_setzero_si128();
          ref = _mm_cmpgt_epi32(ref,val2); // Fills DWORDS with 1's if val<0
          val2 = _mm_xor_si128(val2,ref);
          ref = _mm_and_si128(ref,comp); // Leave the bits we need to add
          val2 = _mm_add_epi32(val2,ref); // Finish conversion to 2's comp
          __m128 fval2 = _mm_cvtepi32_ps(val2);
          fval2 = _mm_mul_ps(fval2,vec_scale);
          val2 = _mm_cvtps_epi32(fval2);

          dst[0] = _mm_packs_epi32(val1,val2);
        }
    }
  _mm_setcsr(mxcsr_orig); // Restore rounding control bits
}
#  define SSE2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_mmx_level >= 2) && (_nom_width >= 8)) \
            _tgt = sse2_xfer_irrev_decoded_block16;
#else // !KDU_NO_SSE
#  define SSE2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_XFER_IRREV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        SSE2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nw); \
        AVX_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nw); \
        AVX2_SET_BLOCK_XFER_IRREV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                ..._xfer_irrev_decoded_block32                      */
/*****************************************************************************/

#ifndef KDU_NO_AVX
  extern void
    avx_xfer_irrev_decoded_block32(kdu_int32 *src, void **dst_refs,
                                   int dst_offset, int dst_width,
                                   int src_stride, int height,
                                   int K_max, float delta);
#  define AVX_SET_BLOCK_XFER_IRREV32(_tgt,_nom_width) \
          if ((kdu_mmx_level >= 6) && (_nom_width >= 8)) \
            _tgt = avx_xfer_irrev_decoded_block32;
#else // !KDU_NO_AVX
#  define AVX_SET_BLOCK_XFER_IRREV32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_XFER_IRREV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        AVX_SET_BLOCK_XFER_IRREV32(_tgt,_nw); \
      } \
  }
  
} // namespace kd_core_simd

#endif // X86_DECODER_LOCAL_H
