/*****************************************************************************/
// File: avx2_coder_local.cpp [scope = CORESYS/CODING]
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
source file is required to implement AVX2 versions of the data transfer
functions -- placing the code in a separate file allows the compiler to
be instructed to use vex-prefixed instructions exclusively, which avoids
processor state transition costs.  There is no harm in including this
source file with all builds, even if AVX2 is not supported, so long as you
are careful to globally define the `KDU_NO_AVX2' compilation directive.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX2) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER
#include <assert.h>

using namespace kdu_core;

namespace kd_core_simd {
  
static union {
  kdu_byte bytes[64];
  __m256i vec[2];
  } local_mask_src256 =
  { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
  
/* ========================================================================= */
/*                    SIMD Transfer Functions for Decoding                   */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                avx2_xfer_rev_decoded_block16                       */
/*****************************************************************************/
  
void
  avx2_xfer_rev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
                                int dst_offset_in, int dst_width,
                                int src_stride, int height,
                                int K_max, float delta_unused)
{
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - 2*align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;
  int src_bp_overshoot = 2*((dst_span_bytes+31) & ~31) - 4*src_stride;
  __m256i downshift = _mm256_set1_epi32(31-K_max);
  __m256i smask = _mm256_set1_epi32(0x80000000);
  smask = _mm256_srav_epi32(smask,downshift); // Extends sign bit mask
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m256i *dst = (__m256i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 31) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      __m256i val1, val2, signs1, signs2;
      for (n=dst_span_bytes; n > 0; n-=32, dst++, src_bp+=64)
        { 
          val1 = _mm256_loadu_si256((__m256i *)(src_bp+0));
          val2 = _mm256_loadu_si256((__m256i *)(src_bp+32));
          val1 = _mm256_srav_epi32(val1,downshift);
          val2 = _mm256_srav_epi32(val2,downshift);
          signs1 = _mm256_and_si256(val1,smask); // Save sign bit
          signs2 = _mm256_and_si256(val2,smask); // Save sign bit
          val1 = _mm256_abs_epi32(val1);
          val2 = _mm256_abs_epi32(val2);
          val1 = _mm256_add_epi32(val1,signs1); // Leaves 2's complement words
          val2 = _mm256_add_epi32(val2,signs2); // Leaves 2's complement words
          val1 = _mm256_packs_epi32(val1,val2);
          dst[0] = _mm256_permute4x64_epi64(val1,0xD8); // Swap qwords 1 <-> 2
        }
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_xfer_rev_decoded_block32                      */
/*****************************************************************************/

void
  avx2_xfer_rev_decoded_block32(kdu_int32 *src_in, void **dst_refs,
                                int dst_offset_in, int dst_width,
                                int src_stride, int height,
                                int K_max, float delta_unused)
{
  int dst_offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 4*dst_width + align_bytes;
  int src_bp_overshoot = ((dst_span_bytes+31) & ~31) - 4*src_stride;
  __m256i downshift = _mm256_set1_epi32(31-K_max);
  __m256i smask = _mm256_set1_epi32(0x80000000);
  smask = _mm256_srav_epi32(smask,downshift); // Extends sign bit mask
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m256i *dst = (__m256i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 31) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      for (n=dst_span_bytes; n > 32; n-=64, dst+=2, src_bp+=64)
        { // Write 2 vectors (32 bytes) at once, with overwrite of < 1 vector
          __m256i val1 = _mm256_loadu_si256((__m256i *)(src_bp+0));
          __m256i val2 = _mm256_loadu_si256((__m256i *)(src_bp+32));
          val1 = _mm256_srav_epi32(val1,downshift);
          val2 = _mm256_srav_epi32(val2,downshift);
          __m256i signs1 = _mm256_and_si256(val1,smask); // Save sign info
          val1 = _mm256_abs_epi32(val1); // -ve values map to
          dst[0] = _mm256_add_epi32(val1,signs1);
          __m256i signs2 = _mm256_and_si256(val2,smask); // Save sign info
          val2 = _mm256_abs_epi32(val2); // -ve values map to
          dst[1] = _mm256_add_epi32(val2,signs2);
        }
      if (n > 0)
        { // Write one more vector
          __m256i val1 = _mm256_loadu_si256((__m256i *)(src_bp+0));
          val1 = _mm256_srav_epi32(val1,downshift);
          __m256i signs1 = _mm256_and_si256(val1,smask); // Save sign info
          val1 = _mm256_abs_epi32(val1); // -ve values map to
          dst[0] = _mm256_add_epi32(val1,signs1);
          src_bp += 32;
        }
    }
}
  
/*****************************************************************************/
/* EXTERN               avx2_xfer_irrev_decoded_block16                      */
/*****************************************************************************/

void
  avx2_xfer_irrev_decoded_block16(kdu_int32 *src_in, void **dst_refs,
                                  int dst_offset_in, int dst_width,
                                  int src_stride, int height,
                                  int K_max, float delta)
{
  float fscale = delta * kdu_pwrof2f(KDU_FIX_POINT+1+K_max);
  kdu_uint32 iscale = (kdu_uint32)(fscale+0.5f);
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  kdu_byte *src_bp = ((kdu_byte *)src_in) - 2*align_bytes;
  nxt_dst -= align_bytes;    dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;
  int src_bp_overshoot = 2*((dst_span_bytes+31) & ~31) - 4*src_stride;
  __m256i vmask_x0000FFFF = _mm256_set1_epi32(0x0000FFFF);
  __m256i fact_low = _mm256_set1_epi16((kdu_uint16)iscale);
  __m256i fact_high = _mm256_set1_epi16((kdu_uint16)(iscale>>16));
  for (; height > 0; height--, src_bp-=src_bp_overshoot, dst_refs++)
    { 
      __m256i *dst = (__m256i *)nxt_dst;
      assert((_addr_to_kdu_int32(dst) & 31) == 0);
      nxt_dst = ((kdu_byte *)(dst_refs[1])) + dst_offset_bytes;
      __m256i v1, v2, v3, v4, s1, s3;
      for (n=dst_span_bytes; n > 32; n-=64, dst+=2, src_bp+=128)
        { // Write 2 vectors (64 bytes) at once, with overwrite < 1 vector 
          v1 = _mm256_loadu_si256((__m256i *)(src_bp+0));
          v2 = _mm256_loadu_si256((__m256i *)(src_bp+32));
          v3 = _mm256_loadu_si256((__m256i *)(src_bp+64));
          v4 = _mm256_loadu_si256((__m256i *)(src_bp+96));

          v1 = _mm256_srai_epi32(v1,15); // Arrange for low 16 bits of each
          v2 = _mm256_srai_epi32(v2,15); // dword to have only magnitude bits.
          s1 = _mm256_packs_epi32(v1,v2); // Preserve packed signs in s1
          v3 = _mm256_srai_epi32(v3,15);
          v4 = _mm256_srai_epi32(v4,15);
          s3 = _mm256_packs_epi32(v3,v4); // Preserve packed signs in s3
          
          v1 = _mm256_and_si256(v1,vmask_x0000FFFF);
          v2 = _mm256_and_si256(v2,vmask_x0000FFFF);
          v1 = _mm256_packus_epi32(v1,v2); // Preserve full 16 mag bits in v1
          v3 = _mm256_and_si256(v3,vmask_x0000FFFF);
          v4 = _mm256_and_si256(v4,vmask_x0000FFFF);
          v3 = _mm256_packus_epi32(v3,v4); // Preserve full 16 mag bits in v3

          v2 = _mm256_mulhi_epu16(v1,fact_low);
          v1 = _mm256_mullo_epi16(v1,fact_high);
          v4 = _mm256_mulhi_epu16(v3,fact_low);
          v3 = _mm256_mullo_epi16(v3,fact_high);
          v1 = _mm256_avg_epu16(v1,v2); // Adds the two parts, adds a rounding
          v3 = _mm256_avg_epu16(v3,v4); // offset of 1 and divides by 2.

          v1 = _mm256_sign_epi16(v1,s1);
          v3 = _mm256_sign_epi16(v3,s3);
          v1 = _mm256_permute4x64_epi64(v1,0xD8); // Swap qwords 1 <-> 2
          v3 = _mm256_permute4x64_epi64(v3,0xD8); // Swap qwords 1 <-> 2
          _mm256_stream_si256(dst,v1);
          _mm256_stream_si256(dst+1,v3);
        }
      if (n > 0)
        { 
          v1 = _mm256_loadu_si256((__m256i *)(src_bp+0));
          v2 = _mm256_loadu_si256((__m256i *)(src_bp+32));
          v1 = _mm256_srai_epi32(v1,15);
          v2 = _mm256_srai_epi32(v2,15);
          s1 = _mm256_packs_epi32(v1,v2); // Preserve packed signs in s1
          v1 = _mm256_and_si256(v1,vmask_x0000FFFF);
          v2 = _mm256_and_si256(v2,vmask_x0000FFFF);
          v1 = _mm256_packus_epi32(v1,v2); // Preserve full 16 mag bits in v1
          v2 = _mm256_mulhi_epu16(v1,fact_low);
          v1 = _mm256_mullo_epi16(v1,fact_high);
          v1 = _mm256_avg_epu16(v1,v2);
          v1 = _mm256_sign_epi16(v1,s1);
          v1 = _mm256_permute4x64_epi64(v1,0xD8); // Swap qwords 1 <-> 2
          _mm256_stream_si256(dst,v1);
          src_bp += 64;
        }
    }
} 
  
  
/* ========================================================================= */
/*                  SIMD Quantization Functions for Encoding                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 avx2_quantize32_rev_block16                        */
/*****************************************************************************/

kdu_int32
  avx2_quantize32_rev_block16(kdu_int32 *dst, void **src_refs,
                              int src_offset, int src_width,
                              int dst_stride, int height,
                              int K_max, float delta_unused)
{
  assert(K_max <= 15);
  __m256i end_mask =
    _mm256_loadu_si256((__m256i *)(local_mask_src256.bytes +
                                   2*((-src_width)&15)));
  kdu_int16 *nxt_src = ((kdu_int16 *)(src_refs[0])) + src_offset;
  __m128i upshift = _mm_cvtsi32_si128(15-K_max);
  __m256i val1, val2, sign1, sign2, or_val, smask, zero=_mm256_setzero_si256();
  smask = _mm256_slli_epi16(_mm256_cmpeq_epi32(zero,zero),15); // -> 0x8000
  or_val = zero;
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m256i *sp = (__m256i *)nxt_src; // not necessarily aligned
      nxt_src = ((kdu_int16 *)(src_refs[1])) + src_offset;
      __m256i *dp = (__m256i *)dst; // not necessarily aligned
      int c=src_width;
      for (; c > 32; c-=32, sp+=2, dp+=4)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          val2 = _mm256_permute4x64_epi64(val2,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          sign2 = _mm256_and_si256(val2,smask);
          val1 = _mm256_abs_epi16(val1);
          val2 = _mm256_abs_epi16(val2);
          val1 = _mm256_sll_epi16(val1,upshift);
          val2 = _mm256_sll_epi16(val2,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          sign1 = _mm256_unpacklo_epi16(zero,val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(zero,val1);
          _mm256_storeu_si256(dp+1,val1);
          sign2 = _mm256_unpacklo_epi16(zero,val2);
          _mm256_storeu_si256(dp+2,sign2);
          val2 = _mm256_unpackhi_epi16(zero,val2);
          _mm256_storeu_si256(dp+3,val2);
        }
      if (c > 16)
        { // Process two final vectors, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          val2 = _mm256_and_si256(val2,end_mask);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          val2 = _mm256_permute4x64_epi64(val2,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          sign2 = _mm256_and_si256(val2,smask);
          val1 = _mm256_abs_epi16(val1);
          val2 = _mm256_abs_epi16(val2);
          val1 = _mm256_sll_epi16(val1,upshift);
          val2 = _mm256_sll_epi16(val2,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          sign1 = _mm256_unpacklo_epi16(zero,val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(zero,val1);
          _mm256_storeu_si256(dp+1,val1);
          sign2 = _mm256_unpacklo_epi16(zero,val2);
          _mm256_storeu_si256(dp+2,sign2);
          val2 = _mm256_unpackhi_epi16(zero,val2);
          _mm256_storeu_si256(dp+3,val2);          
        }
      else
        { // Process one final vector, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val1 = _mm256_and_si256(val1,end_mask);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          val1 = _mm256_abs_epi16(val1);
          val1 = _mm256_sll_epi16(val1,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          val1 = _mm256_or_si256(val1,sign1);
          sign1 = _mm256_unpacklo_epi16(zero,val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(zero,val1);
          _mm256_storeu_si256(dp+1,val1);
        }
    }
  val1 = _mm256_srli_si256(or_val,8);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi64(or_val,32);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_slli_epi32(or_val,16);
  or_val = _mm256_or_si256(or_val,val1); // OR of 16-bit words in each line is
                                         // now in bits 16-31 of that lane.
  kdu_int32 upper = _mm_cvtsi128_si32(_mm256_extracti128_si256(or_val,1));
  kdu_int32 lower = _mm_cvtsi128_si32(_mm256_castsi256_si128(or_val));  
  return (lower | upper) & 0x7FFF0000;
}
  
/*****************************************************************************/
/* EXTERN                 avx2_quantize32_rev_block32                        */
/*****************************************************************************/

kdu_int32
  avx2_quantize32_rev_block32(kdu_int32 *dst, void **src_refs,
                              int src_offset, int src_width,
                              int dst_stride, int height,
                              int K_max, float delta_unused)
{
  __m256i end_mask =
    _mm256_loadu_si256((__m256i *)(local_mask_src256.bytes +
                                   4*((-src_width)&7)));
  kdu_int32 *nxt_src = ((kdu_int32 *)(src_refs[0])) + src_offset;
  __m256i upshift = _mm256_set1_epi32(31-K_max);
  __m256i val1, val2, sign1, sign2, smask, or_val, zero=_mm256_setzero_si256();
  smask = _mm256_slli_epi32(_mm256_cmpeq_epi32(zero,zero),31); // -> 0x80000000
  or_val = zero;
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m256i *sp = (__m256i *)nxt_src;
      nxt_src = ((kdu_int32 *)(src_refs[1])) + src_offset;
      __m256i *dp = (__m256i *)dst;
      int c=src_width;
      for (; c > 16; c-=16, sp+=2, dp+=2)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          sign1 = _mm256_and_si256(smask,val1);
          sign2 = _mm256_and_si256(smask,val2);
          val1 = _mm256_abs_epi32(val1);
          val2 = _mm256_abs_epi32(val2);
          val1 = _mm256_sllv_epi32(val1,upshift);
          val2 = _mm256_sllv_epi32(val2,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          _mm256_storeu_si256(dp,val1);
          _mm256_storeu_si256(dp+1,val2);
        }
      if (c > 8)
        { // Write two final vectors, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          val2 = _mm256_and_si256(val2,end_mask);
          sign1 = _mm256_and_si256(smask,val1);
          sign2 = _mm256_and_si256(smask,val2);
          val1 = _mm256_abs_epi32(val1);
          val2 = _mm256_abs_epi32(val2);
          val1 = _mm256_sllv_epi32(val1,upshift);
          val2 = _mm256_sllv_epi32(val2,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          _mm256_storeu_si256(dp,val1);
          _mm256_storeu_si256(dp+1,val2);
        }
      else
        { // Write one final vector, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val1 = _mm256_and_si256(val1,end_mask);
          sign1 = _mm256_and_si256(smask,val1);
          val1 = _mm256_abs_epi32(val1);
          val1 = _mm256_sllv_epi32(val1,upshift);
          or_val = _mm256_or_si256(or_val,val1);
          val1 = _mm256_or_si256(val1,sign1);
          _mm256_storeu_si256(dp,val1);
        }
    }
  val1 = _mm256_srli_si256(or_val,8);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi64(or_val,32);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi32(or_val,16);
  or_val = _mm256_or_si256(or_val,val1);
  kdu_int32 upper = _mm_cvtsi128_si32(_mm256_extracti128_si256(or_val,1));
  kdu_int32 lower = _mm_cvtsi128_si32(_mm256_castsi256_si128(or_val));  
  return (lower | upper);
}

/*****************************************************************************/
/* EXTERN                avx2_quantize32_irrev_block16                       */
/*****************************************************************************/
  
kdu_int32
  avx2_quantize32_irrev_block16(kdu_int32 *dst, void **src_refs,
                                int src_offset, int src_width,
                                int dst_stride, int height,
                                int K_max, float delta)
{
  float fscale = 1.0f / delta;
  __m256i end_mask =
    _mm256_loadu_si256((__m256i *)(local_mask_src256.bytes +
                                   2*((-src_width)&15)));
  kdu_int16 *nxt_src = ((kdu_int16 *)(src_refs[0])) + src_offset;
  __m256i val1, val2, sign1, sign2, tmp1, tmp2, or_val, smask;
  fscale *= kdu_pwrof2f(32-K_max-KDU_FIX_POINT); // 2x the true scaling factor
  kdu_int32 iscale = (kdu_int32)(fscale+0.5f);
  __m256i fact_low = _mm256_set1_epi16((kdu_int16)iscale);
  __m256i fact_high = _mm256_set1_epi16((kdu_int16)(iscale>>16));
  or_val =_mm256_setzero_si256();
  smask = _mm256_slli_epi16(_mm256_cmpeq_epi32(or_val,or_val),15); // -> 0x8000
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m256i *sp = (__m256i *)nxt_src; // not necessarily aligned
      nxt_src = ((kdu_int16 *)(src_refs[1])) + src_offset;
      __m256i *dp = (__m256i *)dst; // not necessarily aligned
      int c=src_width;
      for (; c > 32; c-=32, sp+=2, dp+=4)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          val2 = _mm256_permute4x64_epi64(val2,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          sign2 = _mm256_and_si256(val2,smask);
          val1 = _mm256_abs_epi16(val1);          
          val2 = _mm256_abs_epi16(val2);
          tmp1 = _mm256_mulhi_epu16(val1,fact_low);
          val1 = _mm256_mullo_epi16(val1,fact_high);
          tmp2 = _mm256_mulhi_epu16(val2,fact_low);
          val2 = _mm256_mullo_epi16(val2,fact_high);
          val1 = _mm256_adds_epu16(val1,tmp1);
          val1 = _mm256_srli_epi16(val1,1); // Correct 2x factor -> 15 mag bits
          or_val = _mm256_or_si256(or_val,val1);
          val2 = _mm256_adds_epu16(val2,tmp2); // Keeps all 16 mag bits
          val2 = _mm256_srli_epi16(val2,1); // Correct 2x factor -> 15 mag bits
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          sign1 = _mm256_unpacklo_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp+1,val1);
          sign2 = _mm256_unpacklo_epi16(_mm256_setzero_si256(),val2);
          _mm256_storeu_si256(dp+2,sign2);
          val2 = _mm256_unpackhi_epi16(_mm256_setzero_si256(),val2);
          _mm256_storeu_si256(dp+3,val2);
        }
      if (c > 16)
        { // Process two final vectors, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val2 = _mm256_loadu_si256(sp+1);
          val2 = _mm256_and_si256(val2,end_mask);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          val2 = _mm256_permute4x64_epi64(val2,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          sign2 = _mm256_and_si256(val2,smask);
          val1 = _mm256_abs_epi16(val1);          
          val2 = _mm256_abs_epi16(val2);
          tmp1 = _mm256_mulhi_epu16(val1,fact_low);
          val1 = _mm256_mullo_epi16(val1,fact_high);
          tmp2 = _mm256_mulhi_epu16(val2,fact_low);
          val2 = _mm256_mullo_epi16(val2,fact_high);
          val1 = _mm256_adds_epu16(val1,tmp1);
          val1 = _mm256_srli_epi16(val1,1); // Correct 2x factor -> 15 mag bits
          or_val = _mm256_or_si256(or_val,val1);
          val2 = _mm256_adds_epu16(val2,tmp2); // Keeps all 16 mag bits
          val2 = _mm256_srli_epi16(val2,1); // Correct 2x factor -> 15 mag bits
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,sign1);
          val2 = _mm256_or_si256(val2,sign2);
          sign1 = _mm256_unpacklo_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp+1,val1);
          sign2 = _mm256_unpacklo_epi16(_mm256_setzero_si256(),val2);
          _mm256_storeu_si256(dp+2,sign2);
          val2 = _mm256_unpackhi_epi16(_mm256_setzero_si256(),val2);
          _mm256_storeu_si256(dp+3,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = _mm256_loadu_si256(sp);
          val1 = _mm256_and_si256(val1,end_mask);
          val1 = _mm256_permute4x64_epi64(val1,0xD8);
          sign1 = _mm256_and_si256(val1,smask);
          val1 = _mm256_abs_epi16(val1);          
          tmp1 = _mm256_mulhi_epu16(val1,fact_low);
          val1 = _mm256_mullo_epi16(val1,fact_high);
          val1 = _mm256_adds_epu16(val1,tmp1);
          val1 = _mm256_srli_epi16(val1,1); // Correct 2x factor -> 15 mag bits
          or_val = _mm256_or_si256(or_val,val1);
          val1 = _mm256_or_si256(val1,sign1);
          sign1 = _mm256_unpacklo_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp,sign1);
          val1 = _mm256_unpackhi_epi16(_mm256_setzero_si256(),val1);
          _mm256_storeu_si256(dp+1,val1);
        }
    }
  val1 = _mm256_srli_si256(or_val,8);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi64(or_val,32);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_slli_epi32(or_val,16);
  or_val = _mm256_or_si256(or_val,val1); // OR of 16-bit words in each line is
                                         // now in bits 16-31 of that lane.
  kdu_int32 upper = _mm_cvtsi128_si32(_mm256_extracti128_si256(or_val,1));
  kdu_int32 lower = _mm_cvtsi128_si32(_mm256_castsi256_si128(or_val));  
  return (lower | upper) & 0x7FFF0000;
}  
  
/*****************************************************************************/
/* EXTERN                avx2_quantize32_irrev_block32                       */
/*****************************************************************************/

kdu_int32
  avx2_quantize32_irrev_block32(kdu_int32 *dst, void **src_refs,
                                int src_offset, int src_width,
                                int dst_stride, int height,
                                int K_max, float delta)
{
  __m256 end_mask =
    _mm256_loadu_ps(((float *)local_mask_src256.bytes) + ((-src_width)&7));
  float fscale = 1.0F / delta;
  fscale *= kdu_pwrof2f(31-K_max);
  float *nxt_src = ((float *)(src_refs[0])) + src_offset;
  __m256 fval1, fval2, fsign1, fsign2, pscale=_mm256_set1_ps(fscale);
  __m256i or_val = _mm256_setzero_si256();
  __m256i imask =
    _mm256_slli_epi32(_mm256_cmpeq_epi32(or_val,or_val),31); // -> 0x80000000
  __m256 fmask = _mm256_castsi256_ps(imask);
  __m256i val1, val2;
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      __m256 *sp = (__m256 *)nxt_src; // not necessarily aligned
      nxt_src = ((float *)(src_refs[1])) + src_offset;
      __m256i *dp = (__m256i *)dst; // not necessarily aligned
      int c=src_width;
      for (; c > 16; c-=16, sp+=2, dp+=2)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          fval1 = _mm256_loadu_ps((float *)sp);
          fval2 = _mm256_loadu_ps((float *)(sp+1));
          fsign1 = _mm256_and_ps(fmask,fval1);
          fsign2 = _mm256_and_ps(fmask,fval2);
          fval1 = _mm256_mul_ps(fval1,pscale);
          fval2 = _mm256_mul_ps(fval2,pscale);
          fval1 = _mm256_xor_ps(fval1,fsign1);
          fval2 = _mm256_xor_ps(fval2,fsign2);
          val1 = _mm256_cvttps_epi32(fval1);
          val2 = _mm256_cvttps_epi32(fval2);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,_mm256_castps_si256(fsign1));
          val2 = _mm256_or_si256(val2,_mm256_castps_si256(fsign2));
          _mm256_storeu_si256(dp,val1);
          _mm256_storeu_si256(dp+1,val2);
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          fval1 = _mm256_loadu_ps((float *)sp);
          fval2 = _mm256_loadu_ps((float *)(sp+1));
          fval2 = _mm256_and_ps(fval2,end_mask);
          fsign1 = _mm256_and_ps(fmask,fval1);
          fsign2 = _mm256_and_ps(fmask,fval2);
          fval1 = _mm256_mul_ps(fval1,pscale);
          fval2 = _mm256_mul_ps(fval2,pscale);
          fval1 = _mm256_xor_ps(fval1,fsign1);
          fval2 = _mm256_xor_ps(fval2,fsign2);
          val1 = _mm256_cvttps_epi32(fval1);
          val2 = _mm256_cvttps_epi32(fval2);
          or_val = _mm256_or_si256(or_val,val1);
          or_val = _mm256_or_si256(or_val,val2);
          val1 = _mm256_or_si256(val1,_mm256_castps_si256(fsign1));
          val2 = _mm256_or_si256(val2,_mm256_castps_si256(fsign2));
          _mm256_storeu_si256(dp,val1);
          _mm256_storeu_si256(dp+1,val2);
        }
      else
        { // Process one final vector, with source word masking
          fval1 = _mm256_loadu_ps((float *)sp);
          fval1 = _mm256_and_ps(fval1,end_mask);
          fsign1 = _mm256_and_ps(fmask,fval1);
          fval1 = _mm256_mul_ps(fval1,pscale);
          fval1 = _mm256_xor_ps(fval1,fsign1);
          val1 = _mm256_cvttps_epi32(fval1);
          or_val = _mm256_or_si256(or_val,val1);
          val1 = _mm256_or_si256(val1,_mm256_castps_si256(fsign1));
          _mm256_storeu_si256(dp,val1);
        }
    }
  val1 = _mm256_srli_si256(or_val,8);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi64(or_val,32);
  or_val = _mm256_or_si256(or_val,val1);
  val1 = _mm256_srli_epi32(or_val,16);
  or_val = _mm256_or_si256(or_val,val1);
  kdu_int32 upper = _mm_cvtsi128_si32(_mm256_extracti128_si256(or_val,1));
  kdu_int32 lower = _mm_cvtsi128_si32(_mm256_castsi256_si128(or_val));  
  return (lower | upper);
}
  
      
} // namespace kd_core_simd

#endif // !KDU_NO_AVX2
