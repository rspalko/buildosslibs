/*****************************************************************************/
// File: avx2_region_decompressor.cpp [scope = APPS/SUPPORT]
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
data for `kdu_region_decompressor', as well as disciplined horizontal and
vertical resampling operations.  The accelerated functions found in this
file take advantage of the AVX and AVX2 instruction sets.  The functions
defined here may be selected at run-time via macros defined in
"x86_region_decompressor_local.h", depending on run-time CPU detection, as
well as build conditions.  To enable compilation of these functions, you
need `KDU_X86_INTRINSICS' to be defined, and `KDU_NO_AVX2' not to be defined.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX2) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER

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
/* EXTERN                avx2_convert_and_copy_to_fix16                      */
/*****************************************************************************/

void
  avx2_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
                                 const int types[], int num_lines,
                                 int src_precision, int missing_src_samples,
                                 void *void_dst, int dst_min, int num_samples,
                                 int dst_type, int float_exp_bits)
{
  assert((dst_type == KDRD_FIX16_TYPE) && (float_exp_bits == 0));
  kdu_int16 *dst = ((kdu_int16 *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case; no need to be efficient at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Work out vector parameters to use in case we have 16-bit absolute ints
  __m128i vec_shift;
  __m256i vec_offset;
  int abs_upshift = KDU_FIX_POINT - src_precision;
  int abs_downshift = 0;
  kdu_int16 abs_offset = 0;
  if (abs_upshift >= 0)
    { 
      vec_shift = _mm_cvtsi32_si128(abs_upshift);
      vec_offset = _mm256_setzero_si256(); // Avoid possible compiler warnings
    }
  else
    { 
      abs_downshift = -abs_upshift;
      abs_upshift = 0;
      vec_shift = _mm_cvtsi32_si128(abs_downshift);
      abs_offset = (kdu_int16)(1 << (abs_downshift-1));
      vec_offset = _mm256_set1_epi16(abs_offset);
    }
  
  // Skip over source samples as required
  const kdu_int16 *src = (const kdu_int16 *)(*(bufs++));
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
          src = (const kdu_int16 *)(*(bufs++));
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
  
  // Now perform the sample conversion process
  kdu_int16 val=0;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      assert(src_type & KDRD_SHORT_TYPE); // Function requires this
      val = ((const kdu_int16 *)src)[0];
      if (src_type & KDRD_ABSOLUTE_TYPE)
        val = ((val<<abs_upshift)+abs_offset)>>abs_downshift;
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = val;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      if (src_len > 0)
        { // Else source type might be 0 (undefined)
          assert(src_type & KDRD_SHORT_TYPE); // Function requires this
          kdu_int16 *dp = dst;
          if (src_len > num_samples)
            src_len = num_samples;
          dst += src_len;
          num_samples -= src_len;
          int lead=(-((_addr_to_kdu_int32(dp))>>1))&15; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          
          if (src_type == KDRD_FIX16_TYPE)
            { // Just copy source to dest
              for (; lead > 0; lead--, src++, dp++)
                *dp = *src;
              for (; src_len > 0; src_len-=16, src+=16, dp+=16)
                ((__m256i *)dp)[0] = _mm256_loadu_si256((__m256i *)src);
            }
          else if (abs_downshift == 0)
            { 
              for (; lead > 0; lead--, src++, dp++)
                *dp = (*src) << abs_upshift;
              for (; src_len > 0; src_len-=16, src+=16, dp+=16)
                { 
                  __m256i val = _mm256_loadu_si256((__m256i *)src);
                  ((__m256i *)dp)[0] = _mm256_sll_epi16(val,vec_shift);
                }
            }
          else
            { 
              for (; lead > 0; lead--, src++, dp++)
                *dp = ((*src)+abs_offset) >> abs_downshift;
              for (; src_len > 0; src_len-=16, src+=16, dp+=16)
                { 
                  __m256i val = _mm256_loadu_si256((__m256i *)src);
                  val = _mm256_add_epi16(val,vec_offset);
                  ((__m256i *)dp)[0] = _mm256_sra_epi16(val,vec_shift);
                }
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src = (const kdu_int16 *)(*(bufs++));
      src_len=*(widths++); src_type=*(types++); num_lines--;
    }
  // Perform right edge padding as required
  for (val=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = val;
}
  
/*****************************************************************************/
/* EXTERN                      avx2_white_stretch                            */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES16 >= 16)
void
  avx2_white_stretch(const kdu_int16 *src, kdu_int16 *dst, int num_samples,
                     int stretch_residual)
{
  kdu_int32 stretch_offset = -((-(stretch_residual<<(KDU_FIX_POINT-1))) >> 16);
  if (stretch_residual <= 0x7FFF)
    { // Use full multiplication-based approach
      __m256i factor = _mm256_set1_epi16((kdu_int16) stretch_residual);
      __m256i offset = _mm256_set1_epi16((kdu_int16) stretch_offset);
      __m256i *sp = (__m256i *) src;
      __m256i *dp = (__m256i *) dst;
      for (; num_samples > 0; num_samples-=16, sp++, dp++)
        { 
          __m256i val = *sp;
          __m256i residual = _mm256_mulhi_epi16(val,factor);
          val = _mm256_add_epi16(val,offset);
          *dp = _mm256_add_epi16(val,residual);
        }
    }
  else
    { // Large stretch residual -- can only happen with 1-bit original data
      int diff=(1<<16)-((int) stretch_residual), downshift = 1;
      while ((diff & 0x8000) == 0)
        { diff <<= 1; downshift++; }
      __m128i shift = _mm_cvtsi32_si128(downshift);
      __m256i offset = _mm256_set1_epi16((kdu_int16) stretch_offset);
      __m256i *sp = (__m256i *) src;
      __m256i *dp = (__m256i *) dst;
      for (; num_samples > 0; num_samples-=16, sp++, dp++)
        { 
          __m256i val = *sp;
          __m256i twice_val = _mm256_add_epi16(val,val);
          __m256i shifted_val = _mm256_sra_epi16(val,shift);
          val = _mm256_sub_epi16(twice_val,shifted_val);
          *dp = _mm256_add_epi16(val,offset);
        }
    }
}
#endif // KDU_ALIGN_SAMPLES16 requirement

/*****************************************************************************/
/* EXTERN               avx2_transfer_fix16_to_bytes_gap1                    */
/*****************************************************************************/

void
  avx2_transfer_fix16_to_bytes_gap1(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs)
  /* This function is installed only if there is no significant source scaling
     or source offset requirement, there is no clipping, and outputs are
     unsigned with at most 8 bit precision. */
{
  assert((src_type == KDRD_FIX16_TYPE) && (gap == 1) &&
         (dst_prec <= 8) && (!leave_signed) && unused_clip_outputs);
  const kdu_int16 *sp = ((const kdu_int16 *)src_buf) + skip_samples;
  kdu_byte *dp = (kdu_byte *)dst;
  
  int downshift = KDU_FIX_POINT-dst_prec;
  kdu_int16 offset, mask;
  offset = (1<<downshift)>>1; // Rounding offset for the downshift
  offset += (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
  mask = (kdu_int16)((-1)<<dst_prec);
  __m256i voff = _mm256_set1_epi16(offset);
  __m256i vmax = _mm256_set1_epi16(~mask);
  __m256i vmin = _mm256_setzero_si256();
  __m128i shift = _mm_cvtsi32_si128(downshift);
  for (; num_samples >= 32; num_samples-=32, sp+=32, dp+=32)
    { // Generate whole output vectors of 32 byte values at a time
      __m256i low = _mm256_loadu_si256((const __m256i *)sp);
      low = _mm256_add_epi16(low,voff); // Add the offset
      low = _mm256_sra_epi16(low,shift); // Apply the downshift
      low = _mm256_max_epi16(low,vmin); // Clip to min value of 0
      low = _mm256_min_epi16(low,vmax); // Clip to max value of `~mask'
      __m256i high = _mm256_loadu_si256((const __m256i *)(sp+16));
      high = _mm256_add_epi16(high,voff); // Add the offset
      high = _mm256_sra_epi16(high,shift); // Apply the downshift
      high = _mm256_max_epi16(high,vmin); // Clip to min value of 0
      high = _mm256_min_epi16(high,vmax); // Clip to max value of `~mask'
      __m256i packed = _mm256_packus_epi16(low,high);
      packed = _mm256_permute4x64_epi64(packed, 0xD8);
      _mm256_storeu_si256((__m256i *)dp,packed);
    }
  for (; num_samples > 0; num_samples--, sp++, dp++)
    { 
      kdu_int16 val = (sp[0]+offset)>>downshift;
      if (val & mask)
        val = (val < 0)?0:~mask;
      dp[0] = (kdu_byte) val;
    }
}

/*****************************************************************************/
/* EXTERN               avx2_transfer_fix16_to_bytes_gap4                    */
/*****************************************************************************/

void
  avx2_transfer_fix16_to_bytes_gap4(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs)
  /* This function is installed only if there is no significant source scaling
     or source offset requirement, there is no clipping, and outputs are
     unsigned with at most 8 bit precision. */
{
  assert((src_type == KDRD_FIX16_TYPE) && (gap == 4) &&
         (dst_prec <= 8) && (!leave_signed) && unused_clip_outputs);
  const kdu_int16 *sp = ((const kdu_int16 *)src_buf) + skip_samples;
  kdu_byte *dp = (kdu_byte *)dst;
  
  int downshift = KDU_FIX_POINT-dst_prec;
  kdu_int16 offset, mask;
  offset = (1<<downshift)>>1; // Rounding offset for the downshift
  offset += (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
  mask = (kdu_int16)((-1)<<dst_prec);
  __m128i voff = _mm_set1_epi16(offset);
  __m128i vmax = _mm_set1_epi16(~mask);
  __m128i vmin = _mm_setzero_si128();
  __m128i shift = _mm_cvtsi32_si128(downshift);
  int align_off = (- _addr_to_kdu_int32(dp)) & 31;
  for (; (align_off > 0) && (num_samples > 0);
       align_off-=4, num_samples--, sp++, dp+=4)
    { 
      kdu_int16 val = (sp[0]+offset)>>downshift;
      if (val & mask)
        val = (val < 0)?0:~mask;
      dp[0] = (kdu_byte) val;
    }
  dp += align_off;
  __m128i align_shift = _mm_cvtsi32_si128(-8*align_off);
  __m256i blend_mask = _mm256_set1_epi32(0x00000080);
  blend_mask = _mm256_sll_epi32(blend_mask,align_shift);
  for (; num_samples >= 8; num_samples-=8, sp+=8, dp+=32)
    { // Generate whole output vectors of 32 byte values at a time
      __m128i val = _mm_loadu_si128((const __m128i *)sp);
      __m256i tgt = *((__m256i *)dp);
      val = _mm_add_epi16(val,voff); // Add the offset
      val = _mm_sra_epi16(val,shift); // Apply the downshift
      val = _mm_max_epi16(val,vmin); // Clip to min value of 0
      val = _mm_min_epi16(val,vmax); // Clip to max value of `~mask'
      __m256i expanded = _mm256_cvtepu16_epi32(val);
      expanded = _mm256_sll_epi32(expanded,align_shift);
      tgt = _mm256_blendv_epi8(tgt,expanded,blend_mask);
      *((__m256i *)dp) = tgt;
    }
  for (dp-=align_off; num_samples > 0; num_samples--, sp++, dp+=4)
    { 
      kdu_int16 val = (sp[0]+offset)>>downshift;
      if (val & mask)
        val = (val < 0)?0:~mask;
      dp[0] = (kdu_byte) val;
    }
}
  
/*****************************************************************************/
/* EXTERN          avx2_interleaved_transfer_fix16_to_bytes                  */
/*****************************************************************************/

void
  avx2_interleaved_transfer_fix16_to_bytes(const void *src0, const void *src1,
                                           const void *src2, const void *src3,
                                           int src_prec, int src_type,
                                           int src_skip, int num_pixels,
                                           kdu_byte *byte_dst, int dst_prec,
                                           kdu_uint32 zmask, kdu_uint32 fmask)
{
  assert((src_type == KDRD_FIX16_TYPE) && (dst_prec <= 8));
  const kdu_int16 *sp0 = ((const kdu_int16 *)src0) + src_skip;
  const kdu_int16 *sp1 = ((const kdu_int16 *)src1) + src_skip;
  const kdu_int16 *sp2 = ((const kdu_int16 *)src2) + src_skip;
  kdu_uint32 *dp = (kdu_uint32 *) byte_dst;
  
  int downshift = KDU_FIX_POINT-dst_prec;
  kdu_int16 offset, mask;
  offset = (1<<downshift)>>1; // Rounding offset for the downshift
  offset += (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
  mask = (kdu_int16)((-1)<<dst_prec);
  
  __m256i voff = _mm256_set1_epi16(offset);
  __m256i vmax = _mm256_set1_epi16(~mask);
  __m256i vmin = _mm256_setzero_si256();
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m256i or_mask = _mm256_set1_epi32((kdu_int32)fmask);
  if (zmask == 0x00FFFFFF)
    { // Only channels 0, 1 and 2 are used; don't bother converting channel 3
      for (; num_pixels >= 16; num_pixels-=16,
           sp0+=16, sp1+=16, sp2+=16, dp+=16)
        { // Generate whole output vectors of 16 x 32-bit pixels at a time
          __m256i val0 = _mm256_loadu_si256((const __m256i *)sp0);
          val0 = _mm256_add_epi16(val0,voff); // Add the offset
          val0 = _mm256_sra_epi16(val0,shift); // Apply the downshift
          val0 = _mm256_max_epi16(val0,vmin); // Clip to min value of 0
          val0 = _mm256_min_epi16(val0,vmax); // Clip to max value of `~mask'
          __m256i val1 = _mm256_loadu_si256((const __m256i *)sp1);
          val1 = _mm256_add_epi16(val1,voff); // Add the offset
          val1 = _mm256_sra_epi16(val1,shift); // Apply the downshift
          val1 = _mm256_max_epi16(val1,vmin); // Clip to min value of 0
          val1 = _mm256_min_epi16(val1,vmax); // Clip to max value of `~mask'
          val1 = _mm256_slli_epi16(val1,8);
          val0 = _mm256_or_si256(val0,val1); // Ilv 1st and 2nd channels
          val0 = _mm256_permute4x64_epi64(val0,0xD8); // Swap middle 2 qwords
          
          __m256i val2 = _mm256_loadu_si256((const __m256i *)sp2);
          val2 = _mm256_add_epi16(val2,voff); // Add the offset
          val2 = _mm256_sra_epi16(val2,shift); // Apply the downshift
          val2 = _mm256_max_epi16(val2,vmin); // Clip to min value of 0
          val2 = _mm256_min_epi16(val2,vmax); // Clip to max value of `~mask'
          val2 = _mm256_permute4x64_epi64(val2,0xD8); // Swap middle 2 qwords
          
          val1 = _mm256_unpacklo_epi16(val0,val2);
          val2 = _mm256_unpackhi_epi16(val0,val2);
          val1 = _mm256_or_si256(val1,or_mask);
          val2 = _mm256_or_si256(val2,or_mask);
          
          _mm256_storeu_si256((__m256i *)dp,val1);
          _mm256_storeu_si256((__m256i *)(dp+8),val2);
        }
      for (; num_pixels > 0; num_pixels--, sp0++, sp1++, sp2++, dp++)
        { 
          kdu_int16 val;
          kdu_uint32 pel;
          val = (sp0[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel = (kdu_uint32)val;
          val = (sp1[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel |= ((kdu_uint32)val)<<8;
          val = (sp2[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel |= ((kdu_uint32)val)<<16;
          *dp = pel | fmask;
        }
    }
  else
    { 
      const kdu_int16 *sp3 = ((const kdu_int16 *)src3) + src_skip;
      __m256i and_mask = _mm256_set1_epi32((kdu_int32)zmask);
      for (; num_pixels >= 16; num_pixels-=16,
           sp0+=16, sp1+=16, sp2+=16, sp3+=16, dp+=16)
        { // Generate whole output vectors of 16 x 32-bit pixels at a time
          __m256i val0 = _mm256_loadu_si256((const __m256i *)sp0);
          val0 = _mm256_add_epi16(val0,voff); // Add the offset
          val0 = _mm256_sra_epi16(val0,shift); // Apply the downshift
          val0 = _mm256_max_epi16(val0,vmin); // Clip to min value of 0
          val0 = _mm256_min_epi16(val0,vmax); // Clip to max value of `~mask'
          __m256i val1 = _mm256_loadu_si256((const __m256i *)sp1);
          val1 = _mm256_add_epi16(val1,voff); // Add the offset
          val1 = _mm256_sra_epi16(val1,shift); // Apply the downshift
          val1 = _mm256_max_epi16(val1,vmin); // Clip to min value of 0
          val1 = _mm256_min_epi16(val1,vmax); // Clip to max value of `~mask'
          val1 = _mm256_slli_epi16(val1,8);
          val0 = _mm256_or_si256(val0,val1); // Ilv 1st and 2nd channels
          val0 = _mm256_permute4x64_epi64(val0,0xD8); // Swap middle 2 qwords
          
          __m256i val2 = _mm256_loadu_si256((const __m256i *)sp2);
          val2 = _mm256_add_epi16(val2,voff); // Add the offset
          val2 = _mm256_sra_epi16(val2,shift); // Apply the downshift
          val2 = _mm256_max_epi16(val2,vmin); // Clip to min value of 0
          val2 = _mm256_min_epi16(val2,vmax); // Clip to max value of `~mask'
          __m256i val3 = _mm256_loadu_si256((const __m256i *)sp3);
          val3 = _mm256_add_epi16(val3,voff); // Add the offset
          val3 = _mm256_sra_epi16(val3,shift); // Apply the downshift
          val3 = _mm256_max_epi16(val3,vmin); // Clip to min value of 0
          val3 = _mm256_min_epi16(val3,vmax); // Clip to max value of `~mask'
          val3 = _mm256_slli_epi16(val3,8);
          val2 = _mm256_or_si256(val2,val3); // Ilv 2nd and 3rd channels
          val2 = _mm256_permute4x64_epi64(val2,0xD8); // Swap middle 2 qwords
          
          val1 = _mm256_unpacklo_epi16(val0,val2);
          val2 = _mm256_unpackhi_epi16(val0,val2);
          val1 = _mm256_and_si256(val1,and_mask);
          val2 = _mm256_and_si256(val2,and_mask);
          val1 = _mm256_or_si256(val1,or_mask);
          val2 = _mm256_or_si256(val2,or_mask);
          
          _mm256_storeu_si256((__m256i *)dp,val1);
          _mm256_storeu_si256((__m256i *)(dp+8),val2);
        }
      for (; num_pixels > 0; num_pixels--, sp0++, sp1++, sp2++, sp3++, dp++)
        { 
          kdu_int16 val;
          kdu_uint32 pel;
          val = (sp0[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel = (kdu_uint32)val;
          val = (sp1[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel |= ((kdu_uint32)val)<<8;
          val = (sp2[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel |= ((kdu_uint32)val)<<16;
          val = (sp3[0]+offset)>>downshift;
          if (val & mask)
            val = (val < 0)?0:~mask;
          pel |= ((kdu_uint32)val)<<24;
          pel &= zmask;
          *dp = pel | fmask;
        }
    }
}

  
/* ========================================================================= */
/*                        Vertical Resampling Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_vert_resample_float                         */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES32 >= 8)
void
  avx2_vert_resample_float(int length, float *src[], float *dst,
                           void *kernel, int kernel_length)
{
  if (kernel_length == 2)
    { 
      float *sp0=(float *)src[2];
      float *sp1=(float *)src[3];
      float *dp =(float *)dst;
      __m256 *kern = (__m256 *) kernel;
      __m256 k0=kern[0], k1=kern[1];
      for (int n=0; n < length; n+=8)
        { 
          __m256 val = _mm256_mul_ps(*((__m256 *)(sp0+n)),k0);
          val = _mm256_fmadd_ps(*((__m256 *)(sp1+n)),k1,val);
          *((__m256 *)(dp+n)) = val;
        }      
    }
  else
    { 
      assert(kernel_length == 6);
      float *sp0=(float *)src[0];
      float *sp1=(float *)src[1];
      float *sp2=(float *)src[2];
      float *sp3=(float *)src[3];
      float *sp4=(float *)src[4];
      float *sp5=(float *)src[5];
      float *dp =(float *)dst;
      __m256 *kern = (__m256 *) kernel;
      __m256 k0=kern[0], k1=kern[1], k2=kern[2],
             k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=8)
        { 
          __m256 v0 = _mm256_mul_ps(*((__m256 *)(sp0+n)),k0);
          __m256 v1 = _mm256_mul_ps(*((__m256 *)(sp1+n)),k1);
          v0 = _mm256_fmadd_ps(*((__m256 *)(sp2+n)),k2,v0);
          v1 = _mm256_fmadd_ps(*((__m256 *)(sp3+n)),k3,v1);
          v0 = _mm256_fmadd_ps(*((__m256 *)(sp4+n)),k4,v0);
          v1 = _mm256_fmadd_ps(*((__m256 *)(sp5+n)),k5,v1);
          *((__m256 *)(dp+n)) = _mm256_add_ps(v0,v1);
        }
    }
}
#endif // KDU_ALIGN_SAMPLES32 requirement

/*****************************************************************************/
/* EXTERN                   avx2_vert_resample_fix16                         */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES16 >= 16)
void
  avx2_vert_resample_fix16(int length, kdu_int16 *src[], kdu_int16 *dst,
                           void *kernel, int kernel_length)
{
  if (kernel_length == 2)
    { 
      kdu_int16 *sp0=(kdu_int16 *)src[2];
      kdu_int16 *sp1=(kdu_int16 *)src[3];
      kdu_int16 *dp = (kdu_int16 *)dst;
      if (((kdu_int16 *) kernel)[16] == 0)
        { // Can just copy from sp0 to dp
          for (int n=0; n < length; n+=16)
            { 
              __m256i val = *((__m256i *)(sp0+n));
              *((__m256i *)(dp+n)) = val;
            }
        }
      else
        { 
          __m256i *kern = (__m256i *) kernel;
          __m256i k0=kern[0], k1=kern[1];
          for (int n=0; n < length; n+=16)
            { 
              __m256i v0, v1;
              v0 = _mm256_mulhrs_epi16(*((__m256i *)(sp0+n)),k0);
              v1 = _mm256_mulhrs_epi16(*((__m256i *)(sp1+n)),k1);
              v0 = _mm256_sub_epi16(_mm256_setzero_si256(),v0);
              *((__m256i *)(dp+n)) = _mm256_sub_epi16(v0,v1);
            }
        }
    }
  else
    { 
      assert(kernel_length == 6);
      kdu_int16 *sp0=(kdu_int16 *)src[0];
      kdu_int16 *sp1=(kdu_int16 *)src[1];
      kdu_int16 *sp2=(kdu_int16 *)src[2];
      kdu_int16 *sp3=(kdu_int16 *)src[3];
      kdu_int16 *sp4=(kdu_int16 *)src[4];
      kdu_int16 *sp5=(kdu_int16 *)src[5];
      kdu_int16 *dp = (kdu_int16 *)dst;
      __m256i *kern = (__m256i *) kernel;
      __m256i k0=kern[0], k1=kern[1], k2=kern[2],
              k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=16)
        { 
          __m256i val, sum=_mm256_setzero_si256();
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp0+n)),k0);
          sum = _mm256_sub_epi16(sum,val);
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp1+n)),k1);
          sum = _mm256_sub_epi16(sum,val);
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp2+n)),k2);
          sum = _mm256_sub_epi16(sum,val);
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp3+n)),k3);
          sum = _mm256_sub_epi16(sum,val);
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp4+n)),k4);
          sum = _mm256_sub_epi16(sum,val);
          val = _mm256_mulhrs_epi16(*((__m256i *)(sp5+n)),k5);
          *((__m256i *)(dp+n)) = _mm256_sub_epi16(sum,val);
        }
    }
}
#endif // KDU_ALIGN_SAMPLES16 requirement

  
/* ========================================================================= */
/*                   Horizontal Resampling Functions (float)                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    avx2_horz_resample_float                        */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES32 >= 8)
void
  avx2_horz_resample_float(int length, float *src, float *dst,
                            kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                            int pshift, void **kernels, int kernel_length,
                            int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to advance to the next vector.
  assert(min_adj < 24); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
      // Amount we need to add to `phase' if the adj = min_adj.  Note that
      // this value is guaranteed to be strictly less than den < 2^31.  This
      // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
      // integer without risk of numeric overflow.
  
  float *sp_base = src;
  __m256 *dp = (__m256 *) dst;
  if (leadin == 0)
    { // In this case, we have to broadcast each of `kernel_length' successive
      // input samples across the entire 8-element vector, before applying the
      // SIMD arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 7));
        // The above conditions should have been checked during func ptr init
      if (kernel_length == 3)
        { 
          for (; length > 0; length-=8, dp++)
            { 
              __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              __m256 sum=_mm256_broadcast_ss(sp_base);
              __m256 val1=_mm256_broadcast_ss(sp_base+1);
              __m256 val2=_mm256_broadcast_ss(sp_base+2);
              sp_base += min_adj;
              if (phase >= den)
                { 
                phase -= den;  sp_base++;
                  assert(phase < den);
                }
              sum = _mm256_mul_ps(sum,kern[0]);
              sum = _mm256_fmadd_ps(val1,kern[1],sum);
              *dp = _mm256_fmadd_ps(val2,kern[2],sum);
            }
        }
      else if (kernel_length == 4)
        { 
          for (; length > 0; length-=8, dp++)
            { 
              __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              __m256 sum=_mm256_broadcast_ss(sp_base);
              __m256 val1=_mm256_broadcast_ss(sp_base+1);
              __m256 val2=_mm256_broadcast_ss(sp_base+2);
              __m256 val3=_mm256_broadcast_ss(sp_base+3);
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              sum = _mm256_mul_ps(sum,kern[0]);
              sum = _mm256_fmadd_ps(val1,kern[1],sum);
              sum = _mm256_fmadd_ps(val2,kern[2],sum);
              *dp = _mm256_fmadd_ps(val3,kern[3],sum);
            }
        }
      else if (kernel_length == 5)
        { 
          for (; length > 0; length-=8, dp++)
            { 
              __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              __m256 sum=_mm256_broadcast_ss(sp_base);
              __m256 val1=_mm256_broadcast_ss(sp_base+1);
              __m256 val2=_mm256_broadcast_ss(sp_base+2);
              __m256 val3=_mm256_broadcast_ss(sp_base+3);
              sum = _mm256_mul_ps(sum,kern[0]);
              __m256 val4=_mm256_broadcast_ss(sp_base+4);
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              sum = _mm256_fmadd_ps(val1,kern[1],sum);
              sum = _mm256_fmadd_ps(val2,kern[2],sum);
              sum = _mm256_fmadd_ps(val3,kern[3],sum);
              *dp = _mm256_fmadd_ps(val4,kern[4],sum);
            }
        }
      else if (kernel_length == 6)
        { 
          for (; length > 0; length-=8, dp++)
            { 
              __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              __m256 sum=_mm256_broadcast_ss(sp_base);
              __m256 val1=_mm256_broadcast_ss(sp_base+1);
              __m256 val2=_mm256_broadcast_ss(sp_base+2);
              __m256 val3=_mm256_broadcast_ss(sp_base+3);
              sum = _mm256_mul_ps(sum,kern[0]);
              __m256 val4=_mm256_broadcast_ss(sp_base+4);
              sum = _mm256_fmadd_ps(val1,kern[1],sum);
              __m256 val5=_mm256_broadcast_ss(sp_base+5);
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              sum = _mm256_fmadd_ps(val2,kern[2],sum);
              sum = _mm256_fmadd_ps(val3,kern[3],sum);
              sum = _mm256_fmadd_ps(val4,kern[4],sum);
              *dp = _mm256_fmadd_ps(val5,kern[5],sum);
            }
        }
      else 
        { 
          for (; length > 0; length-=8, dp++)
            { 
              __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              __m256 sum=_mm256_broadcast_ss(sp_base);
              __m256 val1=_mm256_broadcast_ss(sp_base+1);
              __m256 val2=_mm256_broadcast_ss(sp_base+2);
              __m256 val3=_mm256_broadcast_ss(sp_base+3);
              sum = _mm256_mul_ps(sum,kern[0]);
              __m256 val4=_mm256_broadcast_ss(sp_base+4);
              sum = _mm256_fmadd_ps(val1,kern[1],sum);
              __m256 val5=_mm256_broadcast_ss(sp_base+5);
              sum = _mm256_fmadd_ps(val2,kern[2],sum);
              __m256 val6=_mm256_broadcast_ss(sp_base+6);
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              sum = _mm256_fmadd_ps(val3,kern[3],sum);
              sum = _mm256_fmadd_ps(val4,kern[4],sum);
              sum = _mm256_fmadd_ps(val5,kern[5],sum);
              *dp = _mm256_fmadd_ps(val6,kern[6],sum);
            }
        }
    }
  else
    { 
      assert(kernel_length >= 6);
      sp_base -= leadin;
      for (; length > 0; length-=8, dp++)
        { 
          __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          float *sp = sp_base; // Note; this is not aligned
          __m256 v0, v1, v2, v3, v4, v5, sum;
          v0=_mm256_loadu_ps(sp);   sum = _mm256_mul_ps(v0,kern[0]);
          v1=_mm256_loadu_ps(sp+1); sum = _mm256_fmadd_ps(v1,kern[1],sum);
          v2=_mm256_loadu_ps(sp+2); sum = _mm256_fmadd_ps(v2,kern[2],sum);
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          v3=_mm256_loadu_ps(sp+3); sum = _mm256_fmadd_ps(v3,kern[3],sum);
          v4=_mm256_loadu_ps(sp+4); sum = _mm256_fmadd_ps(v4,kern[4],sum);
          v5=_mm256_loadu_ps(sp+5); sum = _mm256_fmadd_ps(v5,kern[5],sum);
          int kl;
          for (kl=kernel_length-6, sp+=6, kern+=6;
               kl >= 6; kl-=6, kern+=6, sp+=6)
            { 
              v0=_mm256_loadu_ps(sp);   sum = _mm256_fmadd_ps(v0,kern[0],sum);
              v1=_mm256_loadu_ps(sp+1); sum = _mm256_fmadd_ps(v1,kern[1],sum);
              v2=_mm256_loadu_ps(sp+2); sum = _mm256_fmadd_ps(v2,kern[2],sum);
              v3=_mm256_loadu_ps(sp+3); sum = _mm256_fmadd_ps(v3,kern[3],sum);
              v4=_mm256_loadu_ps(sp+4); sum = _mm256_fmadd_ps(v4,kern[4],sum);
              v5=_mm256_loadu_ps(sp+5); sum = _mm256_fmadd_ps(v5,kern[5],sum);
            }
          if (kl >= 3)
            { 
              v0=_mm256_loadu_ps(sp);   sum = _mm256_fmadd_ps(v0,kern[0],sum);
              v1=_mm256_loadu_ps(sp+1); sum = _mm256_fmadd_ps(v1,kern[1],sum);
              v2=_mm256_loadu_ps(sp+2); sum = _mm256_fmadd_ps(v2,kern[2],sum);
              kl -= 3; kern += 3; sp += 3;
            }
          if (kl == 0)
            { *dp = sum; continue; }
          v0=_mm256_loadu_ps(sp);   sum = _mm256_fmadd_ps(v0,kern[0],sum);
          if (kl == 1)
            { *dp = sum; continue; }
          v1=_mm256_loadu_ps(sp+1); sum = _mm256_fmadd_ps(v1,kern[1],sum);
          *dp = sum;
        }
    }
}
#endif // KDU_ALIGN_SAMPLES32 requirement

/*****************************************************************************/
/* EXTERN                 avx2_hshuf_float_2tap_expand                       */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES32 >= 8)
void
  avx2_hshuf_float_2tap_expand(int length, float *src, float *dst,
                                kdu_uint32 phase, kdu_uint32 num,
                                kdu_uint32 den, int pshift, void **kernels,
                                int kernel_len, int leadin, int blend_vecs)
  /* Note: this function works with permutation vectors whose elements are
     32-bit words, not 8-bit bytes.  Each element of a permutation vector
     carries either the index of a source element (in the range 0 to 7) or
     the special value 0x80808080, meaning "no source".  However, since
     we are only doing expansion here, the "no source" case should never
     occur within the first blend vector, and this is actually the only
     one that we need.  As a result, it is sufficient to use the
     VPERMPS instruction to do permutation. */
{
  assert((leadin == 0) && (blend_vecs > 0) && (kernel_len == 2));
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to advance to the next vector.
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  __m256 *dp = (__m256 *) dst;
  __m256 *kern = (__m256 *) kernels[(phase+off)>>pshift];
  for (; length > 0; length-=8, dp++)
    { 
      __m256 ival0 = _mm256_loadu_ps(src);
      __m256 ival1 = _mm256_loadu_ps(src+1);
      __m256i perm = ((__m256i *)kern)[2];
      __m256 fact0=kern[0], fact1=kern[1];
      phase += max_phase_adj;
      src += min_adj;
      ival0 = _mm256_permutevar8x32_ps(ival0,perm);
      ival1 = _mm256_permutevar8x32_ps(ival1,perm);
      if (phase >= den)
        { 
          phase -= den;  src++;
          assert(phase < den);
        }
      ival0 = _mm256_mul_ps(ival0,fact0);
      kern = (__m256 *) kernels[(phase+off)>>pshift];
      *dp = _mm256_fmadd_ps(ival1,fact1,ival0);
    }      
}
#endif // KDU_ALIGN_SAMPLES16 requirement
  
  
/* ========================================================================= */
/*                   Horizontal Resampling Functions (fix16)                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    avx2_horz_resample_fix16                        */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES16 >= 16)
void
  avx2_horz_resample_fix16(int length, kdu_int16 *src, kdu_int16 *dst,
                           kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                           int pshift, void **kernels, int kernel_length,
                           int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x16 = ((kdu_int64) num) << 4; // Possible ovfl without 64 bits
  int min_adj=(int)(num_x16/den); // Minimum value of adj=[(phase+num_x16)/den]
                                  // required to adavnce to the next vector
  assert(min_adj < 48); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x16-(((kdu_int64)min_adj)*den));
      // Amount we need to add to `phase' if the adj = min_adj.  Note that
      // this value is guaranteed to be strictly less than den < 2^31.  This
      // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
      // integer without risk of numeric overflow.
  
  kdu_int16 *sp_base = src;
  __m256i *dp = (__m256i *) dst;
  if (leadin == 0)
    { // In this case, we have to broadcast each of `kernel_length' successive
      // input samples across the entire 8-element vector, before applying the
      // SIMD arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 12));
        // The above conditions should have been checked during func ptr init
      __m256i shuf0 = _mm256_set1_epi32(0x01000100);
      __m256i shuf1 = _mm256_set1_epi32(0x03020302);
      __m256i shuf2 = _mm256_set1_epi32(0x05040504);
      __m256i shuf3 = _mm256_set1_epi32(0x07060706);
      if (kernel_length <= 4)
        { 
          for (; length > 0; length-=16, dp++)
            { 
              __m256i in, *kern = (__m256i *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              in = _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)
                                                               sp_base));
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              __m256i sum = _mm256_setzero_si256();
              __m256i v0 = _mm256_shuffle_epi8(in,shuf0);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
              __m256i v1 = _mm256_shuffle_epi8(in,shuf1);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
              __m256i v2 = _mm256_shuffle_epi8(in,shuf2);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
              if (kernel_length == 4)
                { v0 = _mm256_shuffle_epi8(in,shuf3);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[3])); }
              *dp = sum;
            }
        }
      else if (kernel_length <= 8)
        { 
          for (; length > 0; length-=16, dp++)
            { 
              __m256i in, *kern = (__m256i *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              in = _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)
                                                               sp_base));
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              __m256i sum = _mm256_setzero_si256();
              __m256i v0 = _mm256_shuffle_epi8(in,shuf0);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
              __m256i v1 = _mm256_shuffle_epi8(in,shuf1);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
              __m256i v2 = _mm256_shuffle_epi8(in,shuf2);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
              __m256i v3 = _mm256_shuffle_epi8(in,shuf3);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[3]));

              in = _mm256_srli_si256(in,8); // Access samples 4 to 7
              v0 = _mm256_shuffle_epi8(in,shuf0);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[4]));
              if (kernel_length >= 7)
                { 
                  v1 = _mm256_shuffle_epi8(in,shuf1);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[5]));
                  v2 = _mm256_shuffle_epi8(in,shuf2);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[6]));
                  if (kernel_length == 8) {
                    v3 = _mm256_shuffle_epi8(in,shuf3);
                    sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[7]));
                  }
                }
              else if (kernel_length == 6)
                { 
                  v1 = _mm256_shuffle_epi8(in,shuf1);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[5]));
                }
              *dp = sum;
            }
        }
      else
        { // `kernel_length' = 9, 10, 11 or 12
          for (; length > 0; length-=16, dp++)
            { 
              __m256i in, *kern = (__m256i *) kernels[(phase+off)>>pshift];
              phase += max_phase_adj;
              in = _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)
                                                               sp_base));
              kdu_int16 *sp = sp_base;
              sp_base += min_adj;
              if (phase >= den)
                { 
                  phase -= den;  sp_base++;
                  assert(phase < den);
                }
              __m256i sum = _mm256_setzero_si256();
              __m256i v0 = _mm256_shuffle_epi8(in,shuf0);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
              __m256i v1 = _mm256_shuffle_epi8(in,shuf1);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
              __m256i v2 = _mm256_shuffle_epi8(in,shuf2);
              sum = _mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
              __m256i v3 = _mm256_shuffle_epi8(in,shuf3);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[3]));
              
              in = _mm256_srli_si256(in,8); // Access samples 4 to 7
              v0 = _mm256_shuffle_epi8(in,shuf0);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[4]));
              v1 = _mm256_shuffle_epi8(in,shuf1);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[5]));
              v2 = _mm256_shuffle_epi8(in,shuf2);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[6]));
              v3 = _mm256_shuffle_epi8(in,shuf3);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[7]));

              in = // Access samples 8 to 11
              _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)(sp+8)));
              v0 = _mm256_shuffle_epi8(in,shuf0);
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[8]));
              if (kernel_length >= 11)
                { 
                  v1 = _mm256_shuffle_epi8(in,shuf1);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[9]));
                  v2 = _mm256_shuffle_epi8(in,shuf2);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[10]));
                  if (kernel_length == 12) {
                    v3 = _mm256_shuffle_epi8(in,shuf3);
                    sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[11]));
                  }
                }
              else if (kernel_length == 10)
                { 
                  v1 = _mm256_shuffle_epi8(in,shuf1);
                  sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[9]));
                }
              *dp = sum;
            }
        }
    }
  else
    { 
      assert(kernel_length >= 6);
      sp_base -= leadin; 
      for (; length > 0; length-=16, dp++)
        { 
          __m256i *kern = (__m256i *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          kdu_int16 *sp = sp_base;
          __m256i v0, v1, v2, v3, v4, v5, sum=_mm256_setzero_si256();
          v0 = _mm256_loadu_si256((__m256i *)(sp));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
          v1 = _mm256_loadu_si256((__m256i *)(sp+1));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
          v2 = _mm256_loadu_si256((__m256i *)(sp+2));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          v3 = _mm256_loadu_si256((__m256i *)(sp+3));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[3]));
          v4 = _mm256_loadu_si256((__m256i *)(sp+4));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v4,kern[4]));
          v5 = _mm256_loadu_si256((__m256i *)(sp+5));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v5,kern[5]));
          int kl;
          for (kl=kernel_length-6, sp+=6, kern+=6;
               kl >= 6; kl-=6, kern+=6, sp+=6)
            { 
              v0 = _mm256_loadu_si256((__m256i *)(sp));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
              v1 = _mm256_loadu_si256((__m256i *)(sp+1));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
              v2 = _mm256_loadu_si256((__m256i *)(sp+2));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
              v3 = _mm256_loadu_si256((__m256i *)(sp+3));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v3,kern[3]));
              v4 = _mm256_loadu_si256((__m256i *)(sp+4));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v4,kern[4]));
              v5 = _mm256_loadu_si256((__m256i *)(sp+5));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v5,kern[5]));
            }
          if (kl >= 3)
            { 
              v0 = _mm256_loadu_si256((__m256i *)(sp));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
              v1 = _mm256_loadu_si256((__m256i *)(sp+1));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
              v2 = _mm256_loadu_si256((__m256i *)(sp+2));
              sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v2,kern[2]));
              kl -= 3; kern += 3; sp += 3;
            }
          if (kl == 0)
            { *dp = sum; continue; }
          v0 = _mm256_loadu_si256((__m256i *)(sp));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v0,kern[0]));
          if (kl == 1)
            { *dp = sum; continue; }
          v1 = _mm256_loadu_si256((__m256i *)(sp+1));
          sum=_mm256_sub_epi16(sum,_mm256_mulhrs_epi16(v1,kern[1]));
          *dp = sum;
        }
    } 
}
#endif // KDU_ALIGN_SAMPLES16 requirement
 
  
/*****************************************************************************/
/* EXTERN                 avx2_hshuf_fix16_2tap_expand                       */
/*****************************************************************************/
#if (KDU_ALIGN_SAMPLES16 >= 16)
void
  avx2_hshuf_fix16_2tap_expand(int length, kdu_int16 *src, kdu_int16 *dst,
                                kdu_uint32 phase, kdu_uint32 num,
                                kdu_uint32 den, int pshift, void **kernels,
                                int kernel_len, int leadin, int blend_vecs)
{
  assert((leadin == 0) && (blend_vecs > 0) && (kernel_len == 2));
  int off = (1<<pshift)>>1;
  kdu_int64 num_x16 = ((kdu_int64) num) << 4; // Possible ovfl without 64 bits
  int min_adj=(int)(num_x16/den); // Minimum value of adj=[(phase+num_x8)/den]
                                  // required to adavnce to the next vector.
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x16-(((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  __m256i *dp = (__m256i *) dst;
  __m256i *kern = (__m256i *) kernels[(phase+off)>>pshift];
  if (blend_vecs == 1)
    { // We need only broadcast a 128-bit vector at src and another at src+1
      // each to both lanes of a 256-bit vector and apply the single
      // permutation vector to both.
      for (; length > 0; length-=16, dp++)
        { 
          __m256i ival0 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)src));
          __m256i ival1 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)(src+1)));
          __m256i fact=kern[1], perm=kern[2];
          ival0 = _mm256_shuffle_epi8(ival0,perm);
          ival1 = _mm256_shuffle_epi8(ival1,perm);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m256i *) kernels[(phase+off)>>pshift];
          ival1 = _mm256_sub_epi16(ival1,ival0);
          ival1 = _mm256_mulhrs_epi16(ival1,fact);
          *dp = _mm256_sub_epi16(ival0,ival1);
        }      
    }
  else
    { // As above, but we need to blend two sets of broadcast 128-bit source
      // vectors to form each input to the interpolation kernel.
      assert(blend_vecs == 2);
      for (; length > 0; length-=16, dp++)
        { 
          __m256i ival0 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)src));
          __m256i ival1 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)(src+1)));
          __m256i ival2 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)(src+8)));
          __m256i ival3 = // Compiler should map this to single broadcast load
            _mm256_broadcastsi128_si256(_mm_loadu_si128((__m128i *)(src+9)));
          __m256i fact=kern[1], perm0=kern[2], perm1=kern[3];
          ival0 = _mm256_shuffle_epi8(ival0,perm0);
          ival1 = _mm256_shuffle_epi8(ival1,perm0);
          ival2 = _mm256_shuffle_epi8(ival2,perm1);
          ival3 = _mm256_shuffle_epi8(ival3,perm1);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m256i *) kernels[(phase+off)>>pshift];
          ival0 = _mm256_or_si256(ival0,ival2);
          ival1 = _mm256_or_si256(ival1,ival3);
          ival1 = _mm256_sub_epi16(ival1,ival0);
          ival1 = _mm256_mulhrs_epi16(ival1,fact);
          *dp = _mm256_sub_epi16(ival0,ival1);
        }
    }
}
#endif // KDU_ALIGN_SAMPLES16 requirement
  
} // namespace kd_supp_simd

#endif // !KDU_NO_AVX2
