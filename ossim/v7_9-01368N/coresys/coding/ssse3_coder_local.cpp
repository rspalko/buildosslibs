/*****************************************************************************/
// File: ssse3_coder_local.cpp [scope = CORESYS/CODING]
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
source file is required to implement SSSE3 versions of the data transfer
functions.  There is no harm in including this source file with all builds,
even if SSSE3 is not supported by the compiler, so long as KDU_NO_SSSE3 is
defined or KDU_X86_INTRINSICS is not defined.
******************************************************************************/
#include "kdu_arch.h"
using namespace kdu_core;

#if ((!defined KDU_NO_SSSE3) && (defined KDU_X86_INTRINSICS))

#include <tmmintrin.h>
#include <assert.h>

namespace kd_core_simd {

static union {
  kdu_byte bytes[32];
  __m128i vec[2];
  } local_mask_src128 =
  { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };


/* ========================================================================= */
/*                    SIMD Transfer Functions for Decoding                   */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                ssse3_xfer_rev_decoded_block16                      */
/*****************************************************************************/

void
  ssse3_xfer_rev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
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
  __m128i smask = _mm_setzero_si128();
  smask = _mm_slli_epi32(_mm_cmpeq_epi32(smask,smask),31); // -> 0x80000000
  smask = _mm_sra_epi32(smask,downshift); // Extends sign bit mask
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 0; n-=16, dst++, src_bp+=32)
        { 
          __m128i val1 = _mm_loadu_si128((__m128i *)(src_bp+0));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src_bp+16));
          val1 = _mm_sra_epi32(val1,downshift);
          val2 = _mm_sra_epi32(val2,downshift);
          __m128i signs1 = _mm_and_si128(val1,smask); // Save sign bit
          val1 = _mm_abs_epi32(val1);
          val1 = _mm_add_epi32(val1,signs1); // Leaves 2's complement words
          __m128i signs2 = _mm_and_si128(val2,smask); // Save sign bit
          val2 = _mm_abs_epi32(val2);
          val2 = _mm_add_epi32(val2,signs2); // Leaves 2's complement words
          dst[0] = _mm_packs_epi32(val1,val2);
        }
    }
}

/*****************************************************************************/
/* EXTERN                ssse3_xfer_rev_decoded_block32                      */
/*****************************************************************************/

void
  ssse3_xfer_rev_decoded_block32(kdu_int32 *src_in, void **dst_refs,
                                 int dst_offset_in, int dst_width,
                                 int src_stride, int height,
                                 int K_max, float delta_unused)
{
  int dst_offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 4*dst_width + align_bytes;
  int src_bp_overshoot = ((dst_span_bytes+15) & ~15) - 4*src_stride;
  __m128i downshift = _mm_cvtsi32_si128(31-K_max);
  __m128i smask = _mm_setzero_si128();
  smask = _mm_slli_epi32(_mm_cmpeq_epi32(smask,smask),31); // -> 0x80000000
  smask = _mm_sra_epi32(smask,downshift); // Extends sign bit mask
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m128i *dst = (__m128i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 15) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 16; n-=32, dst+=2, src_bp+=32)
        { // Write 2 vectors (32 bytes) at once, with overwrite of < 1 vector
          __m128i val1 = _mm_loadu_si128((__m128i *)(src_bp+0));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src_bp+16));
          val1 = _mm_sra_epi32(val1,downshift);
          val2 = _mm_sra_epi32(val2,downshift);
          __m128i signs1 = _mm_and_si128(val1,smask); // Save sign info
          val1 = _mm_abs_epi32(val1); // -ve values map to
          dst[0] = _mm_add_epi32(val1,signs1);
          __m128i signs2 = _mm_and_si128(val2,smask); // Save sign info
          val2 = _mm_abs_epi32(val2); // -ve values map to
          dst[1] = _mm_add_epi32(val2,signs2);
        }
      if (n > 0)
        { // Write one more vector
          __m128i val1 = _mm_loadu_si128((__m128i *)(src_bp+0));
          val1 = _mm_sra_epi32(val1,downshift);
          __m128i signs1 = _mm_and_si128(val1,smask); // Save sign info
          val1 = _mm_abs_epi32(val1); // -ve values map to
          dst[0] = _mm_add_epi32(val1,signs1);
          src_bp += 16;
        }
    }
}

/* ========================================================================= */
/*                  SIMD Quantization Functions for Encoding                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 ssse3_quantize32_rev_block16                       */
/*****************************************************************************/

kdu_int32
  ssse3_quantize32_rev_block16(kdu_int32 *dst, void **src_refs,
                               int src_offset, int src_width,
                               int dst_stride, int height,
                               int K_max, float delta_unused)
{
  assert(K_max <= 15);
  __m128i end_mask =
    _mm_loadu_si128((__m128i *)(local_mask_src128.bytes+2*((-src_width)&7)));
  kdu_int16 *nxt_src = ((kdu_int16 *)(src_refs[0])) + src_offset;
  __m128i upshift = _mm_cvtsi32_si128(15-K_max);
  __m128i smask = _mm_setzero_si128();
  __m128i or_val = _mm_setzero_si128();
  __m128i val1, val2, sign1, sign2;
  smask = _mm_slli_epi16(_mm_cmpeq_epi32(smask,smask),15); // -> 0x8000
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m128i *sp = (__m128i *)nxt_src; // not necessarily aligned
      nxt_src = ((kdu_int16 *)(src_refs[1])) + src_offset;
      __m128i *dp = (__m128i *)dst; // not necessarily aligned
      int c=src_width;
      for (; c > 16; c-=16, sp+=2, dp+=4)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = _mm_loadu_si128(sp);         val2 = _mm_loadu_si128(sp+1);
          sign1=_mm_and_si128(smask,val1);    sign2=_mm_and_si128(smask,val2);
          val1=_mm_abs_epi16(val1);           val2=_mm_abs_epi16(val2);
          val1=_mm_sll_epi16(val1,upshift);   val2=_mm_sll_epi16(val2,upshift);
          or_val=_mm_or_si128(or_val,val1);   or_val=_mm_or_si128(or_val,val2);
          val1=_mm_or_si128(val1,sign1);      val2=_mm_or_si128(val2,sign2);
          sign1 = _mm_unpacklo_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp,sign1);
          val1 = _mm_unpackhi_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp+1,val1);
          sign2 = _mm_unpacklo_epi16(_mm_setzero_si128(),val2);
          _mm_storeu_si128(dp+2,sign2);
          val2 = _mm_unpackhi_epi16(_mm_setzero_si128(),val2);
          _mm_storeu_si128(dp+3,val2);
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          val1 = _mm_loadu_si128(sp);
          val2 = _mm_loadu_si128(sp+1);
          val2 = _mm_and_si128(val2,end_mask);
          sign1=_mm_and_si128(smask,val1);    sign2=_mm_and_si128(smask,val2);
          val1=_mm_abs_epi16(val1);           val2=_mm_abs_epi16(val2);
          val1=_mm_sll_epi16(val1,upshift);   val2=_mm_sll_epi16(val2,upshift);
          or_val=_mm_or_si128(or_val,val1);   or_val=_mm_or_si128(or_val,val2);
          val1=_mm_or_si128(val1,sign1);      val2=_mm_or_si128(val2,sign2);
          sign1 = _mm_unpacklo_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp,sign1);
          val1 = _mm_unpackhi_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp+1,val1);
          sign2 = _mm_unpacklo_epi16(_mm_setzero_si128(),val2);
          _mm_storeu_si128(dp+2,sign2);
          val2 = _mm_unpackhi_epi16(_mm_setzero_si128(),val2);
          _mm_storeu_si128(dp+3,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = _mm_loadu_si128(sp);
          val1 = _mm_and_si128(val1,end_mask);
          sign1=_mm_and_si128(smask,val1);
          val1=_mm_abs_epi16(val1);
          val1=_mm_sll_epi16(val1,upshift);
          or_val=_mm_or_si128(or_val,val1);
          val1=_mm_or_si128(val1,sign1);
          sign1 = _mm_unpacklo_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp,sign1);
          val1 = _mm_unpackhi_epi16(_mm_setzero_si128(),val1);
          _mm_storeu_si128(dp+1,val1);
        }
    }
  val1 = _mm_srli_si128(or_val,8);
  or_val = _mm_or_si128(or_val,val1);
  val1 = _mm_srli_epi64(or_val,32);
  or_val = _mm_or_si128(or_val,val1);
  val1 = _mm_slli_epi32(or_val,16);
  or_val = _mm_or_si128(or_val,val1); // OR of 16-bit word is now in bits 16-31
  return _mm_cvtsi128_si32(or_val) & 0x7FFF0000;
}
  
/*****************************************************************************/
/* EXTERN                 ssse3_quantize32_rev_block32                       */
/*****************************************************************************/

kdu_int32
  ssse3_quantize32_rev_block32(kdu_int32 *dst, void **src_refs,
                               int src_offset, int src_width,
                               int dst_stride, int height,
                               int K_max, float delta_unused)
{
  __m128i end_mask =
    _mm_loadu_si128((__m128i *)(local_mask_src128.bytes+4*((-src_width)&3)));
  kdu_int32 *nxt_src = ((kdu_int32 *)(src_refs[0])) + src_offset;
  __m128i upshift = _mm_cvtsi32_si128(31-K_max);
  __m128i or_val = _mm_setzero_si128();
  __m128i smask = _mm_setzero_si128();
  __m128i val1, val2, sign1, sign2;
  smask = _mm_slli_epi32(_mm_cmpeq_epi32(smask,smask),31); // -> 0x80000000
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m128i *sp = (__m128i *)nxt_src;
      nxt_src = ((kdu_int32 *)(src_refs[1])) + src_offset;
      __m128i *dp = (__m128i *)dst;
      int c=src_width;
      for (; c > 8; c-=8, sp+=2, dp+=2)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = _mm_loadu_si128(sp);        val2 = _mm_loadu_si128(sp+1);
          sign1=_mm_and_si128(smask,val1);   sign2=_mm_and_si128(smask,val2);
          val1=_mm_abs_epi32(val1);          val2=_mm_abs_epi32(val2);
          val1=_mm_sll_epi32(val1,upshift);  val2=_mm_sll_epi32(val2,upshift);
          or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
          val1=_mm_or_si128(val1,sign1);     val2=_mm_or_si128(val2,sign2);
          _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
        }
      if (c > 4)
        { // Write two final vectors, with source word masking
          val1 = _mm_loadu_si128(sp);        val2 = _mm_loadu_si128(sp+1);
          val2 = _mm_and_si128(val2,end_mask);
          sign1=_mm_and_si128(smask,val1);   sign2=_mm_and_si128(smask,val2);
          val1=_mm_abs_epi32(val1);          val2=_mm_abs_epi32(val2);
          val1=_mm_sll_epi32(val1,upshift);  val2=_mm_sll_epi32(val2,upshift);
          or_val=_mm_or_si128(or_val,val1);  or_val=_mm_or_si128(or_val,val2);
          val1=_mm_or_si128(val1,sign1);     val2=_mm_or_si128(val2,sign2);
          _mm_storeu_si128(dp,val1);         _mm_storeu_si128(dp+1,val2);
        }
      else
        { // Write one final vector, with source word masking
          val1 = _mm_loadu_si128(sp);
          val1 = _mm_and_si128(val1,end_mask);
          sign1=_mm_and_si128(smask,val1);
          val1=_mm_abs_epi32(val1);
          val1=_mm_sll_epi32(val1,upshift);
          or_val=_mm_or_si128(or_val,val1);
          val1=_mm_or_si128(val1,sign1);
          _mm_storeu_si128(dp,val1);
        }
    }
  val1 = _mm_srli_si128(or_val,8);
  val1 = _mm_or_si128(val1,or_val); // Leave 2 OR'd dwords in low part of val1
  val2 = _mm_srli_si128(val1,4);
  val1 = _mm_or_si128(val1,val2); // Leave 1 OR'd dword in low part of val1
  return _mm_cvtsi128_si32(val1);
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_SSSE3
