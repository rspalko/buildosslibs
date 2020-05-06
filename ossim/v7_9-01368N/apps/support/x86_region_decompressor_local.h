/*****************************************************************************/
// File: msvc_region_decompressor_local.h [scope = CORESYS/TRANSFORMS]
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
   Finds SIMD implementations to acelerate the conversion and transfer of
data for `kdu_region_decompressor', as well as disciplined horizontal and
vertical resampling operations.  This file provides macros to arbitrate the
selection of suitable SIMD functions, if they exist, and also provides the
actual implementations for those functions that use at most SSE2 intrinsics.
The implementations of SIMD functions that require more advanced instruction
sets are found in separate files such as "ssse3_region_decompressor.cpp"
and "avx2_region_decompressor.cpp".
******************************************************************************/

#ifndef X86_REGION_DECOMPRESSOR_LOCAL_H
#define X86_REGION_DECOMPRESSOR_LOCAL_H
#include "kdu_arch.h"

#include <emmintrin.h>

namespace kd_supp_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                         Data Conversion Functions                         */
/* ========================================================================= */

/*****************************************************************************/
/*                  ..._convert_and_copy_shorts_to_fix16                     */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
                                 const int types[], int num_lines,
                                 int src_precision, int missing_src_samples,
                                 void *void_dst, int dst_min, int num_samples,
                                 int dst_type, int float_exp_bits);
#  define AVX2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  if ((kdu_mmx_level >= 7) && (_src_types & KDRD_SHORT_TYPE)) \
    {_func=avx2_convert_and_copy_to_fix16; }
#else // No compilation support for AVX2
#  define AVX2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
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
  __m128i vec_shift, vec_offset;
  int abs_upshift = KDU_FIX_POINT - src_precision;
  int abs_downshift = 0;
  kdu_int16 abs_offset = 0;
  if (abs_upshift >= 0)
    { 
      vec_shift = _mm_cvtsi32_si128(abs_upshift);
      vec_offset = _mm_setzero_si128(); // Avoid possible compiler warnings
    }
  else
    { 
      abs_downshift = -abs_upshift;
      abs_upshift = 0;
      vec_shift = _mm_cvtsi32_si128(abs_downshift);
      abs_offset = (kdu_int16)(1 << (abs_downshift-1));
      vec_offset = _mm_set1_epi16(abs_offset);
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
      val = src[0];
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
          int lead=(-((_addr_to_kdu_int32(dp))>>1))&7; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
      
          if (src_type == KDRD_FIX16_TYPE)
            { // Just copy source to dest
              for (; lead > 0; lead--, src++, dp++)
                *dp = *src;
              for (; src_len > 0; src_len-=8, src+=8, dp+=8)
                ((__m128i *)dp)[0] = _mm_loadu_si128((__m128i *)src);
            }
          else if (abs_downshift == 0)
            { 
              for (; lead > 0; lead--, src++, dp++)
                *dp = (*src) << abs_upshift;
              for (; src_len > 0; src_len-=8, src+=8, dp+=8)
                { 
                  __m128i val = _mm_loadu_si128((__m128i *)src);
                  ((__m128i *)dp)[0] = _mm_sll_epi16(val,vec_shift);
                }
            }
          else
            { 
              for (; lead > 0; lead--, src++, dp++)
                *dp = ((*src)+abs_offset) >> abs_downshift;
              for (; src_len > 0; src_len-=8, src+=8, dp+=8)
                { 
                  __m128i val = _mm_loadu_si128((__m128i *)src);
                  val = _mm_add_epi16(val,vec_offset);
                  ((__m128i *)dp)[0] = _mm_sra_epi16(val,vec_shift);
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
#  define SSE2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  if ((kdu_mmx_level >= 2) && (_src_types & KDRD_SHORT_TYPE)) \
    {_func=sse2_convert_and_copy_to_fix16; }
#else // No compilation support for SSE2
#  define SSE2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types)
#endif

/*****************************************************************************/
/* SELECTOR         KDRD_SIMD_SET_CONVERT_COPY_FIX16_FUNC                    */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  { \
    SSE2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
    AVX2_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  }

/*****************************************************************************/
/*              ..._reinterpret_and_copy_to_unsigned_floats                  */
/*****************************************************************************/
  
#ifndef KDU_NO_SSE4
extern void
  sse4_reinterpret_and_copy_to_unsigned_floats(const void *bufs[],
                                               const int widths[],
                                               const int types[],
                                               int num_lines,
                                               int src_precision,
                                               int missing_src_samples,
                                               void *void_dst, int dst_min,
                                               int num_samples, int dst_type,
                                               int exponent_bits);
#  define SSE4_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec) \
  if ((kdu_mmx_level >= 5) && (_prec <= 32) && (_prec > _exp_bits) && \
      (_exp_bits <= 8) && ((_prec-1-_exp_bits) <= 23)) \
    {_func=sse4_reinterpret_and_copy_to_unsigned_floats; }
#else // No compilation support for SSE4.1
#  define SSE4_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec)
#endif

/*****************************************************************************/
/*               ..._reinterpret_and_copy_to_signed_floats                   */
/*****************************************************************************/
  
#ifndef KDU_NO_SSE4
extern void
  sse4_reinterpret_and_copy_to_signed_floats(const void *bufs[],
                                             const int widths[],
                                             const int types[],
                                             int num_lines,
                                             int src_precision,
                                             int missing_src_samples,
                                             void *void_dst, int dst_min,
                                             int num_samples, int dst_type,
                                             int exponent_bits);
#  define SSE4_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec) \
  if ((kdu_mmx_level >= 5) && (_prec <= 32) && (_prec > _exp_bits) && \
      (_exp_bits <= 8) && ((_prec-1-_exp_bits) <= 23)) \
    {_func=sse4_reinterpret_and_copy_to_signed_floats; }
#else // No compilation support for SSE4.1
#  define SSE4_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec)
#endif

/*****************************************************************************/
/* SELECTOR          KDRD_SIMD_SET_REINTERP_COPY_..._FUNC                    */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_REINTERP_COPY_UF_FUNC(_func,_exp_bits,_prec) \
  { \
    SSE4_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec) \
  }

#define KDRD_SIMD_SET_REINTERP_COPY_SF_FUNC(_func,_exp_bits,_prec) \
  { \
    SSE4_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec) \
  }

/*****************************************************************************/
/*                             ..._white_stretch                             */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void
  avx2_white_stretch(const kdu_int16 *src, kdu_int16 *dst, int num_samples,
                     int stretch_residual);
#  define AVX2_SET_WHITE_STRETCH_FUNC(_func) \
  if (kdu_mmx_level >= 7) {_func=avx2_white_stretch; }
#else // No compilation support for AVX2
#  define AVX2_SET_WHITE_STRETCH_FUNC(_func)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_white_stretch(const kdu_int16 *src, kdu_int16 *dst, int num_samples,
                     int stretch_residual)
{
  kdu_int32 stretch_offset = -((-(stretch_residual<<(KDU_FIX_POINT-1))) >> 16);
  num_samples = (num_samples+7)>>3;
  if (stretch_residual <= 0x7FFF)
    { // Use full multiplication-based approach
      __m128i factor = _mm_set1_epi16((kdu_int16) stretch_residual);
      __m128i offset = _mm_set1_epi16((kdu_int16) stretch_offset);
      __m128i *sp = (__m128i *) src;
      __m128i *dp = (__m128i *) dst;
      for (int c=0; c < num_samples; c++)
        {
          __m128i val = sp[c];
          __m128i residual = _mm_mulhi_epi16(val,factor);
          val = _mm_add_epi16(val,offset);
          dp[c] = _mm_add_epi16(val,residual);
        }
    }
  else
    { // Large stretch residual -- can only happen with 1-bit original data
      int diff=(1<<16)-((int) stretch_residual), downshift = 1;
      while ((diff & 0x8000) == 0)
        { diff <<= 1; downshift++; }
      __m128i shift = _mm_cvtsi32_si128(downshift);
      __m128i offset = _mm_set1_epi16((kdu_int16) stretch_offset);
      __m128i *sp = (__m128i *) src;
      __m128i *dp = (__m128i *) dst;
      for (int c=0; c < num_samples; c++)
        {
          __m128i val = sp[c];
          __m128i twice_val = _mm_add_epi16(val,val);
          __m128i shifted_val = _mm_sra_epi16(val,shift);
          val = _mm_sub_epi16(twice_val,shifted_val);
          dp[c] = _mm_add_epi16(val,offset);
        }
    }
}
#  define SSE2_SET_WHITE_STRETCH_FUNC(_func) \
  if (kdu_mmx_level >= 2) {_func=sse2_white_stretch; }
#else // No compilation support for SSE2
#  define SSE2_SET_WHITE_STRETCH_FUNC(_func)
#endif

/*****************************************************************************/
/* SELECTOR           KDRD_SIMD_SET_WHITE_STRETCH_FUNC                       */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_WHITE_STRETCH_FUNC(_func) \
  { \
    SSE2_SET_WHITE_STRETCH_FUNC(_func) \
    AVX2_SET_WHITE_STRETCH_FUNC(_func) \
  }

/*****************************************************************************/
/*                    ..._transfer_fix16_to_bytes_gap1                       */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_transfer_fix16_to_bytes_gap1(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs);
  /* This function is installed only if there is no significant source scaling
     or source offset requirement, there is no clipping, and outputs are
     unsigned with at most 8 bit precision. */

#  define AVX2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
    if (kdu_mmx_level >= 7) \
      { _func = avx2_transfer_fix16_to_bytes_gap1; }
#else // No compilation support for AVX2
#  define AVX2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_transfer_fix16_to_bytes_gap1(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs)
{ /* This function is installed only if there is no significant source scaling
     or source offset requirement, there is no clipping, and outputs are
     unsigned with at most 8 bit precision. */
  assert((src_type == KDRD_FIX16_TYPE) && (gap == 1) &&
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
  for (; num_samples >= 16; num_samples-=16, sp+=16, dp+=16)
    { // Generate whole output vectors of 16 byte values at a time
      __m128i low = _mm_loadu_si128((const __m128i *)sp);
      low = _mm_add_epi16(low,voff); // Add the offset
      low = _mm_sra_epi16(low,shift); // Apply the downshift
      low = _mm_max_epi16(low,vmin); // Clip to min value of 0
      low = _mm_min_epi16(low,vmax); // Clip to max value of `~mask'
      __m128i high = _mm_loadu_si128((const __m128i *)(sp+8));
      high = _mm_add_epi16(high,voff); // Add the offset
      high = _mm_sra_epi16(high,shift); // Apply the downshift
      high = _mm_max_epi16(high,vmin); // Clip to min value of 0
      high = _mm_min_epi16(high,vmax); // Clip to max value of `~mask'
      __m128i packed = _mm_packus_epi16(low,high);
      _mm_storeu_si128((__m128i *)dp,packed);
    }
  for (; num_samples > 0; num_samples--, sp++, dp++)
    { 
      kdu_int16 val = (sp[0]+offset)>>downshift;
      if (val & mask)
        val = (val < 0)?0:~mask;
      dp[0] = (kdu_byte) val;
    }
}
#  define SSE2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
    if (kdu_mmx_level >= 2) \
      { _func = sse2_transfer_fix16_to_bytes_gap1; }
#else // No compilation support for SSE2
#  define SSE2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func)
#endif
  
/*****************************************************************************/
/*                    ..._transfer_fix16_to_bytes_gap4                       */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_transfer_fix16_to_bytes_gap4(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs);
  /* This function is installed only if there is no significant source scaling
     or source offset requirement, there is no clipping, and outputs are
     unsigned with at most 8 bit precision. */

#  define AVX2_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func) \
  if (kdu_mmx_level >= 7) \
    { _func = avx2_transfer_fix16_to_bytes_gap4; }
#else // No compilation support for AVX2
#  define AVX2_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR           KDRD_SIMD_SET_XFER_TO_BYTES_FUNC                       */
/*****************************************************************************/

#define KDRD_SIMD_SET_XFER_TO_BYTES_FUNC(_func,_src_type,_gap,_prec,_signed) \
  { \
    if ((_src_type == KDRD_FIX16_TYPE) && (_prec <= 8) && !_signed) \
      { \
        if (_gap == 1) \
          { \
            SSE2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
            AVX2_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
          } \
        else if (_gap == 4) \
          { \
            AVX2_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func) \
          } \
      } \
  }

/*****************************************************************************/
/*                 ..._interleaved_transfer_fix16_to_bytes                   */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_interleaved_transfer_fix16_to_bytes(const void *src0, const void *src1,
                                           const void *src2, const void *src3,
                                           int src_prec, int src_type,
                                           int src_skip, int num_pixels,
                                           kdu_byte *byte_dst, int dst_prec,
                                           kdu_uint32 zmask, kdu_uint32 fmask);
#  define AVX2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
  if (kdu_mmx_level >= 7) \
    { _func = avx2_interleaved_transfer_fix16_to_bytes; }
#else // No compilation support for AVX2
#  define AVX2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_interleaved_transfer_fix16_to_bytes(const void *src0, const void *src1,
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

  __m128i voff = _mm_set1_epi16(offset);
  __m128i vmax = _mm_set1_epi16(~mask);
  __m128i vmin = _mm_setzero_si128();
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m128i or_mask = _mm_set1_epi32((kdu_int32)fmask);
  if (zmask == 0x00FFFFFF)
    { // Only channels 0, 1 and 2 are used; don't bother converting channel 3
      for (; num_pixels >= 8; num_pixels-=8, sp0+=8, sp1+=8, sp2+=8, dp+=8)
        { // Generate whole output vectors of 8 x 32-bit pixels at a time
          __m128i val0 = _mm_loadu_si128((const __m128i *)sp0);
          val0 = _mm_add_epi16(val0,voff); // Add the offset
          val0 = _mm_sra_epi16(val0,shift); // Apply the downshift
          val0 = _mm_max_epi16(val0,vmin); // Clip to min value of 0
          val0 = _mm_min_epi16(val0,vmax); // Clip to max value of `~mask'
          __m128i val1 = _mm_loadu_si128((const __m128i *)sp1);
          val1 = _mm_add_epi16(val1,voff); // Add the offset
          val1 = _mm_sra_epi16(val1,shift); // Apply the downshift
          val1 = _mm_max_epi16(val1,vmin); // Clip to min value of 0
          val1 = _mm_min_epi16(val1,vmax); // Clip to max value of `~mask'
          val1 = _mm_slli_epi16(val1,8);
          val1 = val0 = _mm_or_si128(val0,val1); // Ilv 1st and 2nd channels
          
          __m128i val2 = _mm_loadu_si128((const __m128i *)sp2);
          val2 = _mm_add_epi16(val2,voff); // Add the offset
          val2 = _mm_sra_epi16(val2,shift); // Apply the downshift
          val2 = _mm_max_epi16(val2,vmin); // Clip to min value of 0
          val2 = _mm_min_epi16(val2,vmax); // Clip to max value of `~mask'
          
          val0 = _mm_unpacklo_epi16(val0,val2);
          val1 = _mm_unpackhi_epi16(val1,val2);
          val0 = _mm_or_si128(val0,or_mask);
          val1 = _mm_or_si128(val1,or_mask);
          
          _mm_storeu_si128((__m128i *)dp,val0);
          _mm_storeu_si128((__m128i *)(dp+4),val1);
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
    { // All four channels are used
      const kdu_int16 *sp3 = ((const kdu_int16 *)src3) + src_skip;
      __m128i and_mask = _mm_set1_epi32((kdu_int32)zmask);
      for (; num_pixels >= 8; num_pixels-=8,
           sp0+=8, sp1+=8, sp2+=8, sp3+=8, dp+=8)
        { // Generate whole output vectors of 8 x 32-bit pixels at a time
          __m128i val0 = _mm_loadu_si128((const __m128i *)sp0);
          val0 = _mm_add_epi16(val0,voff); // Add the offset
          val0 = _mm_sra_epi16(val0,shift); // Apply the downshift
          val0 = _mm_max_epi16(val0,vmin); // Clip to min value of 0
          val0 = _mm_min_epi16(val0,vmax); // Clip to max value of `~mask'
          __m128i val1 = _mm_loadu_si128((const __m128i *)sp1);
          val1 = _mm_add_epi16(val1,voff); // Add the offset
          val1 = _mm_sra_epi16(val1,shift); // Apply the downshift
          val1 = _mm_max_epi16(val1,vmin); // Clip to min value of 0
          val1 = _mm_min_epi16(val1,vmax); // Clip to max value of `~mask'
          val1 = _mm_slli_epi16(val1,8);
          val1 = val0 = _mm_or_si128(val0,val1); // Ilv 1st and 2nd channels
      
          __m128i val2 = _mm_loadu_si128((const __m128i *)sp2);
          val2 = _mm_add_epi16(val2,voff); // Add the offset
          val2 = _mm_sra_epi16(val2,shift); // Apply the downshift
          val2 = _mm_max_epi16(val2,vmin); // Clip to min value of 0
          val2 = _mm_min_epi16(val2,vmax); // Clip to max value of `~mask'
          __m128i val3 = _mm_loadu_si128((const __m128i *)sp3);
          val3 = _mm_add_epi16(val3,voff); // Add the offset
          val3 = _mm_sra_epi16(val3,shift); // Apply the downshift
          val3 = _mm_max_epi16(val3,vmin); // Clip to min value of 0
          val3 = _mm_min_epi16(val3,vmax); // Clip to max value of `~mask'      
          val3 = _mm_slli_epi16(val3,8);
          val2 = _mm_or_si128(val2,val3); // Interleave 2nd and 3rd channels

          val0 = _mm_unpacklo_epi16(val0,val2);
          val1 = _mm_unpackhi_epi16(val1,val2);
          val0 = _mm_and_si128(val0,and_mask);
          val1 = _mm_and_si128(val1,and_mask);
          val0 = _mm_or_si128(val0,or_mask);
          val1 = _mm_or_si128(val1,or_mask);
      
          _mm_storeu_si128((__m128i *)dp,val0);
          _mm_storeu_si128((__m128i *)(dp+4),val1);
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
#  define SSE2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
    if (kdu_mmx_level >= 2) \
      { _func = sse2_interleaved_transfer_fix16_to_bytes; }
#else // No compilation support for SSE2
#  define SSE2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func)
#endif

/*****************************************************************************/
/* SELECTOR       KDRD_SIMD_SET_INTERLEAVED_XFER_TO_BYTES_FUNC               */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_INTERLEAVED_XFER_TO_BYTES_FUNC(_func,_type,_src_prec) \
  { \
    if ((_type == KDRD_FIX16_TYPE) && (_src_prec <= 8)) \
      { \
        SSE2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
        AVX2_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
      } \
  }


/* ========================================================================= */
/*                        Vertical Resampling Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/*                           ..._vert_resample_float                         */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
extern void
  avx2_vert_resample_float(int length, float *src[], float *dst,
                           void *kernel, int kernel_length);
#  define AVX2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  if ((kdu_mmx_level >= 7) && ((_klen == 2) || (_klen == 6))) \
    {_func=avx2_vert_resample_float; _vec_len=8; }
#else // No compilation support for AVX2
#  define AVX2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_vert_resample_float(int length, float *src[], float *dst,
                           void *kernel, int kernel_length)
{
  if (kernel_length == 2)
    { 
      float *sp0=(float *)src[2];
      float *sp1=(float *)src[3];
      float *dp =(float *)dst;
      __m128 *kern = (__m128 *) kernel;
      __m128 k0=kern[0], k1=kern[1];
      for (int n=0; n < length; n+=4)
        {
          __m128 val = _mm_mul_ps(*((__m128 *)(sp0+n)),k0);
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp1+n)),k1));
          *((__m128 *)(dp+n)) = val;
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
      __m128 *kern = (__m128 *) kernel;
      __m128 k0=kern[0], k1=kern[1], k2=kern[2],
             k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=4)
        { 
          __m128 val = _mm_mul_ps(*((__m128 *)(sp0+n)),k0);
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp1+n)),k1));
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp2+n)),k2));
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp3+n)),k3));
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp4+n)),k4));
          val = _mm_add_ps(val,_mm_mul_ps(*((__m128 *)(sp5+n)),k5));
          *((__m128 *)(dp+n)) = val;
        }
    }
}
#  define SSE2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len) \
     if ((kdu_mmx_level >= 2) && ((_klen == 2) || (_klen == 6))) \
       {_func=sse2_vert_resample_float; _vec_len=4; }
#else // No compilation support for SSE2
#  define SSE2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif

/*****************************************************************************/
/*               KDRD_SET_SIMD_VERT_FLOAT_RESAMPLE_FUNC selector             */
/*****************************************************************************/

#define KDRD_SET_SIMD_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  { \
    SSE2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len); \
    AVX2_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len); \
  }

/*****************************************************************************/
/*                          ..._vert_resample_fix16                          */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void
  avx2_vert_resample_fix16(int length, kdu_int16 *src[], kdu_int16 *dst,
                           void *kernel, int kernel_length);
#  define AVX2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  if ((kdu_mmx_level >= 7) && ((_klen == 2) || (_klen == 6))) \
    {_func=avx2_vert_resample_fix16; _vec_len=16; }
#else // No compilation support for AVX2
#  define AVX2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_vert_resample_fix16(int length, kdu_int16 *src[], kdu_int16 *dst,
                           void *kernel, int kernel_length)
{
  if (kernel_length == 2)
    { 
      kdu_int16 *sp0=(kdu_int16 *)src[2];
      kdu_int16 *sp1=(kdu_int16 *)src[3];
      kdu_int16 *dp = (kdu_int16 *)dst;
      if (((kdu_int16 *) kernel)[8] == 0)
      { // Can just copy from sp0 to dp
        for (int n=0; n < length; n+=8)
          { 
            __m128i val = *((__m128i *)(sp0+n));
            *((__m128i *)(dp+n)) = val;
          }
      }
    else
      { 
        __m128i *kern = (__m128i *) kernel;
        __m128i k0=kern[0], k1=kern[1];
        for (int n=0; n < length; n+=8)
          { 
            __m128i val, sum=_mm_setzero_si128();
            val = *((__m128i *)(sp0+n)); val = _mm_adds_epi16(val,val);
            val = _mm_mulhi_epi16(val,k0); sum = _mm_sub_epi16(sum,val);
            val = *((__m128i *)(sp1+n)); val = _mm_adds_epi16(val,val);
            val = _mm_mulhi_epi16(val,k1); sum = _mm_sub_epi16(sum,val);
            *((__m128i *)(dp+n)) = sum;
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
      __m128i *kern = (__m128i *) kernel;
      __m128i k0=kern[0], k1=kern[1], k2=kern[2],
      k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=8)
        { 
          __m128i val, sum=_mm_setzero_si128();
          val = *((__m128i *)(sp0+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k0); sum = _mm_sub_epi16(sum,val);
          val = *((__m128i *)(sp1+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k1); sum = _mm_sub_epi16(sum,val);
          val = *((__m128i *)(sp2+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k2); sum = _mm_sub_epi16(sum,val);
          val = *((__m128i *)(sp3+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k3); sum = _mm_sub_epi16(sum,val);
          val = *((__m128i *)(sp4+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k4); sum = _mm_sub_epi16(sum,val);
          val = *((__m128i *)(sp5+n)); val = _mm_adds_epi16(val,val);
          val = _mm_mulhi_epi16(val,k5); sum = _mm_sub_epi16(sum,val);
          *((__m128i *)(dp+n)) = sum;
        }
    }
}
#  define SSE2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len) \
     if ((kdu_mmx_level >= 2) && ((_klen == 2) || (_klen == 6))) \
       {_func=sse2_vert_resample_fix16; _vec_len=8; }
#else // No compilation support for SSE2
#  define SSE2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif

/*****************************************************************************/
/*               KDRD_SET_SIMD_VERT_FIX16_RESAMPLE_FUNC selector             */
/*****************************************************************************/

#define KDRD_SET_SIMD_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  { \
    SSE2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len); \
    AVX2_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len); \
  }


/* ========================================================================= */
/*                   Horizontal Resampling Functions (float)                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          ,,,_horz_resample_float                          */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
extern void
  avx2_horz_resample_float(int, float *, float *,
                           kdu_uint32, kdu_uint32, kdu_uint32,
                           int, void **, int, int, int);
#define AVX2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
    if ((kdu_mmx_level >= 7) && ((_klen == 2) || (_klen == 6)) && \
        ((_klen == 6) || ((2.0 * _exp) > 3.0))) \
      { _func=avx2_horz_resample_float; _vec_len=8; _bv=0; _bb=0; }
#else // No compilation support for AVX2
#  define AVX2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif
  
#ifndef KDU_NO_SSSE3
extern void
  ssse3_horz_resample_float(int, float *, float *,
                            kdu_uint32, kdu_uint32, kdu_uint32,
                            int, void **, int, int, int);
#define SSSE3_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
   if ((kdu_mmx_level >= 4) && ((_klen == 2) || (_klen == 6)) && \
       ((_klen == 6) || ((2.0 * _exp) > 3.0))) \
     { _func=ssse3_horz_resample_float; _vec_len=4; _bv=0; _bb=0; }
#else // No compilation support for SSSE3
#  define SSSE3_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif

#ifndef KDU_NO_SSE
static void
  sse2_horz_resample_float(int length, float *src, float *dst,
                           kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                           int pshift, void **kernels, int kernel_length,
                           int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x4 = ((kdu_int64) num) << 2; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x4/den); // Minimum value of adj=[(phase+num_x4)/den]
                                   // required to adavnce to the next octet.
  assert(min_adj < 12); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x4 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  float *sp_base = (float *) src;
  __m128 *dp = (__m128 *) dst;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 4 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 4));
        // The above conditions should have been checked during func ptr init    
      for (; length > 0; length-=4, dp++)
        { 
          __m128 *kern = (__m128 *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          __m128 ival = _mm_loadu_ps(sp_base);
          __m128 val, sum;
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          val = _mm_shuffle_ps(ival,ival,0x00);
          sum = _mm_mul_ps(val,kern[0]);
          val = _mm_shuffle_ps(ival,ival,0x55); 
          sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[1]));
          val = _mm_shuffle_ps(ival,ival,0xAA);
          sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[2]));
          if (kernel_length > 3)
            { 
              val = _mm_shuffle_ps(ival,ival,0xFF);
              sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[3]));
            }
          *dp = sum;
        }
    }
  else
    { 
      sp_base -= leadin;
      for (; length > 0; length-=4, dp++)
        { 
          __m128 *kern = (__m128 *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          float *sp = sp_base; // Note; this is not aligned
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }     
          __m128 val, sum=_mm_setzero_ps();
          int kl;
          for (kl=kernel_length; kl > 3; kl-=4, kern+=4, sp+=4)
            { 
              val = _mm_loadu_ps(sp+0);
              val = _mm_mul_ps(val,kern[0]); sum = _mm_add_ps(sum,val);
              val = _mm_loadu_ps(sp+1);
              val = _mm_mul_ps(val,kern[1]); sum = _mm_add_ps(sum,val);
              val = _mm_loadu_ps(sp+2);
              val = _mm_mul_ps(val,kern[2]); sum = _mm_add_ps(sum,val);
              val = _mm_loadu_ps(sp+3);
              val = _mm_mul_ps(val,kern[3]); sum = _mm_add_ps(sum,val);
            }
          if (kl > 0)
            { 
              val = _mm_loadu_ps(sp+0);
              val = _mm_mul_ps(val,kern[0]); sum = _mm_add_ps(sum,val);
              if (kl > 1)
                { 
                  val = _mm_loadu_ps(sp+1);
                  val = _mm_mul_ps(val,kern[1]); sum = _mm_add_ps(sum,val);
                  if (kl > 2)
                    { 
                      val = _mm_loadu_ps(sp+2);
                      val = _mm_mul_ps(val,kern[2]); sum = _mm_add_ps(sum,val);
                    }
                }
            }
          *dp = sum;
        }
    }
}
#define SSE2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
   if ((kdu_mmx_level >= 2) && ((_klen == 2) || (_klen == 6)) && \
       ((_klen == 6) || ((2.0 * _exp) > 3.0))) \
     { _func=sse2_horz_resample_float; _vec_len=4; _bv=0; _bb=0; }
#else // No compilation support for SSE2
#  define SSE2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif

/*****************************************************************************/
/*                       ..._hshuf_float_2tap_expand                         */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
extern void
  avx2_hshuf_float_2tap_expand(int,float *,float *,
                               kdu_uint32,kdu_uint32,kdu_uint32,
                               int,void **,int,int,int);
#define AVX2_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
  if (kdu_mmx_level >= 7) \
    { _func=avx2_hshuf_float_2tap_expand; _vec_len=8; \
      _bv=(_exp>(7.1F/6.0F))?1:2;  _bb=4; }
#else // !KDU_NO_AVX2
#  define AVX2_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif
  
#ifndef KDU_NO_SSSE3
extern void
  ssse3_hshuf_float_2tap_expand(int,float *,float *,
                                kdu_uint32,kdu_uint32,kdu_uint32,
                                int,void **,int,int,int);
#define SSSE3_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
   if (kdu_mmx_level >= 4) \
     { _func=ssse3_hshuf_float_2tap_expand; _vec_len=4; _bv=2; _bb=1; }
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif

/*****************************************************************************/
/*               KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC selector             */
/*****************************************************************************/

#define KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb)\
  { \
    SSE2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
    SSSE3_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
    AVX2_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
    if ((_klen == 2) && (_exp > 1.0F)) \
      { \
        SSSE3_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
        AVX2_SET_HSHUF_FLOAT_2TAP_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
      } \
  }
  /* Inputs:
       _klen is the length of the scalar kernel (2 or 6);
       _exp is the amount of expansion yielded by the kernel (< 1 = reduction)
     Outputs:
       _func becomes the deduced function (not set if there is none available)
       _vlen is the vector length (4 for SSE/SSE2/SSSE3, 8 for AVX/AVX2)
       _bv becomes the number of blend vectors B per kernel tap (0 if the
           implementation is not based on permutation/shuffle instructions)
       _bb is set to the number of bytes in each permutation element:
           1 if shuffle instructions have 8-bit elements;
           4 if shuffle instructions have 32-bit elements.
           Other values are not defined.
           Blend vectors set each element to the index of the element from
           which they are taken, or else they fill the element with _bb bytes
           that are all equal to 0x80, meaning no element is to be sourced.
  */
  

/* ========================================================================= */
/*                   Horizontal Resampling Functions (fix16)                 */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                   ,,,_horz_resample_fix16                          */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void
  avx2_horz_resample_fix16(int, kdu_int16 *, kdu_int16 *,
                           kdu_uint32, kdu_uint32, kdu_uint32,
                           int, void **, int, int, int);
#define AVX2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
    if ((kdu_mmx_level >= 7) && \
        (((_klen == 6) && (_exp > 0.5)) || \
         ((_klen == 2) && ((2.0 * _exp) > 3.0)))) \
      { _func=avx2_horz_resample_fix16; _vec_len=16; _bv=0; _bh=0; }
#else // No compilation support for SSSE3
#  define AVX2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif
  
#ifndef KDU_NO_SSSE3
extern void
  ssse3_horz_resample_fix16(int, kdu_int16 *, kdu_int16 *,
                            kdu_uint32, kdu_uint32, kdu_uint32,
                            int, void **, int, int, int);
#define SSSE3_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
   if ((kdu_mmx_level >= 4) && ((_klen == 2) || (_klen == 6)) && \
       ((_klen == 6) || ((2.0 * _exp) > 3.0))) \
     { _func=ssse3_horz_resample_fix16; _vec_len=8; _bv=0; _bh=0; }
#else // No compilation support for SSSE3
#  define SSSE3_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif

#ifndef KDU_NO_SSE
static void
  sse2_horz_resample_fix16(int length, kdu_int16 *src, kdu_int16 *dst,
                          kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                          int pshift, void **kernels, int kernel_length,
                          int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to adavnce to the next octet.
  assert(min_adj < 24); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  kdu_int16 *sp_base = src;
  __m128i *dp = (__m128i *) dst;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 8 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 6));
        // The above conditions should have been checked during func ptr init    
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          __m128i val, ival = _mm_loadu_si128((__m128i *) sp_base);          
          sp_base += min_adj;
          ival = _mm_adds_epi16(ival,ival);
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          __m128i sum = _mm_setzero_si128();
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0x00),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[0]));
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0x55),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[1]));
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0xAA),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[2]));
          if (kernel_length > 3)
            { 
              val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0xFF),0x00);
              sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[3]));
              if (kernel_length > 4)
                { 
                  val=_mm_shuffle_epi32(_mm_shufflehi_epi16(ival,0x00),0xAA);
                  sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[4]));
                  if (kernel_length > 5)
                    { 
                      val=_mm_shuffle_epi32(_mm_shufflehi_epi16(ival,0x55),0xAA);
                      sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[5]));
                    }
                }
            }
          *dp = sum;
        }
    }
  else
    { 
      sp_base -= leadin; 
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          kdu_int16 *sp = sp_base;
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          __m128i val, sum=_mm_setzero_si128();
          int kl;
          for (kl=kernel_length; kl > 7; kl-=8, kern+=8, sp+=8)
            { 
              val=_mm_loadu_si128((__m128i *)(sp+0)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[0]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+1)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[1]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+2)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[2]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+3)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[3]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+4)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[4]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+5)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[5]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+6)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[6]); sum=_mm_sub_epi16(sum,val);
              val=_mm_loadu_si128((__m128i *)(sp+7)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[7]); sum=_mm_sub_epi16(sum,val);
            }
          if (kl > 0)
            { 
              val=_mm_loadu_si128((__m128i *)(sp+0)); val=_mm_adds_epi16(val,val);
              val = _mm_mulhi_epi16(val,kern[0]); sum=_mm_sub_epi16(sum,val);
              if (kl > 1)
                { 
                  val=_mm_loadu_si128((__m128i *)(sp+1));
                  val=_mm_adds_epi16(val,val);
                  val = _mm_mulhi_epi16(val,kern[1]);
                  sum = _mm_sub_epi16(sum,val);
                  if (kl > 2)
                    { 
                      val=_mm_loadu_si128((__m128i *)(sp+2));
                      val=_mm_adds_epi16(val,val);
                      val = _mm_mulhi_epi16(val,kern[2]);
                      sum = _mm_sub_epi16(sum,val);
                      if (kl > 3)
                        { 
                          val=_mm_loadu_si128((__m128i *)(sp+3));
                          val=_mm_adds_epi16(val,val);
                          val = _mm_mulhi_epi16(val,kern[3]);
                          sum = _mm_sub_epi16(sum,val);
                          if (kl > 4)
                            { 
                              val=_mm_loadu_si128((__m128i *)(sp+4));
                              val=_mm_adds_epi16(val,val);
                              val = _mm_mulhi_epi16(val,kern[4]);
                              sum = _mm_sub_epi16(sum,val);
                              if (kl > 5)
                                { 
                                  val=_mm_loadu_si128((__m128i *)(sp+5));
                                  val=_mm_adds_epi16(val,val);
                                  val = _mm_mulhi_epi16(val,kern[5]);
                                  sum = _mm_sub_epi16(sum,val);
                                  if (kl > 6)
                                    { 
                                      val=_mm_loadu_si128((__m128i *)(sp+6));
                                      val=_mm_adds_epi16(val,val);
                                      val = _mm_mulhi_epi16(val,kern[6]);
                                      sum = _mm_sub_epi16(sum,val);
                                    }
                                }
                            }
                        }
                    }
                }
            }
          *dp = sum;
        }
    }
}
#define SSE2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
     if ((kdu_mmx_level >= 2) && ((_klen == 2) || (_klen == 6)) && \
         ((_klen == 6) || ((4.0 * _exp) > 7.0))) \
       { _func=sse2_horz_resample_fix16; _vec_len=8; _bv=0; _bh=0; }
#else // No compilation support for SSE2
#  define SSE2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif

/*****************************************************************************/
/*                       ..._hshuf_fix16_2tap_expand                         */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void
  avx2_hshuf_fix16_2tap_expand(int,kdu_int16 *,kdu_int16 *,
                               kdu_uint32,kdu_uint32,kdu_uint32,
                               int,void **,int,int,int);
#define AVX2_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
  if (kdu_mmx_level >= 7) \
    { _func=avx2_hshuf_fix16_2tap_expand; _vec_len=16; _bh=1; \
      _bv = (_exp > (15.0F/7.0F))?1:2; }
#else // !KDU_NO_AVX2
#  define AVX2_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif
  
#ifndef KDU_NO_SSSE3
extern void
  ssse3_hshuf_fix16_2tap_expand(int,kdu_int16 *,kdu_int16 *,
                                kdu_uint32,kdu_uint32,kdu_uint32,
                                int,void **,int,int,int);
#define SSSE3_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
   if (kdu_mmx_level >= 4) \
     { _func=ssse3_hshuf_fix16_2tap_expand; _vec_len=8; \
       _bv=(_exp>(7.1F/6.0F))?1:2; _bh=0; }
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif

/*****************************************************************************/
/*                       ..._hshuf_fix16_6tap_expand                         */
/*****************************************************************************/

#ifndef KDU_NO_SSSE3
extern void
  ssse3_hshuf_fix16_6tap_expand(int,kdu_int16 *,kdu_int16 *,
                                kdu_uint32,kdu_uint32,kdu_uint32,
                                int,void **,int,int,int);
#define SSSE3_SET_HSHUF_FIX16_6TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
   if (kdu_mmx_level >= 4) \
     { \
       int tmp_bv = 2; \
       while (7.1f > ((10+8*(tmp_bv-2))*_exp)) tmp_bv++; \
       if (tmp_bv <= 3) \
         { _func=ssse3_hshuf_fix16_6tap_expand; _vec_len=8; \
           _bv=tmp_bv; _bh=0; } \
     }
#else // !KDU_NO_SSSE3
#  define SSSE3_SET_HSHUF_FIX16_6TAP_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif

/*****************************************************************************/
/*               KDRD_SET_SIMD_HORZ_FIX16_RESAMPLE_FUNC selector             */
/*****************************************************************************/

#define KDRD_SET_SIMD_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh)\
{ \
  SSE2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); \
  SSSE3_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); \
  if ((_klen == 2) && (_exp > 1.0F)) \
    { SSSE3_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); } \
  else if (_klen == 6) \
    { SSSE3_SET_HSHUF_FIX16_6TAP_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); } \
  AVX2_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); \
  if ((_klen == 2) && (_exp > 1.0F)) \
    { AVX2_SET_HSHUF_FIX16_2TAP_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); } \
}
  /* Inputs:
       _klen is the length of the scalar kernel (2 or 6);
       _exp is the amount of expansion yielded by the kernel (< 1 = reduction)
     Outputs:
       _func becomes the deduced function (not set if there is none available)
       _vlen is the vector length (8 for SSE/SSE2/SSSE3, 16 for AVX2)
       _bv becomes the number of blend vectors B per kernel tap (0 if the
           implementation is not based on permutation/shuffle instructions)
       _bh is meaningful only when _bv > 0, with the following interpretation:
           _bh=0 means that each blend vector performs permutation on a full
                 length source vector.  In this case, kernels are expected
                 to hold _klen * _bv blend vectors.
           _bh=1 means that each blend vector operates on a half-length
                 (_vlen/2-element) source vector, mapping its elements to all
                 `_vlen' elements of the permuted outputs that are blended
                 together to form the kernel inputs.  Note that the "h" in
                 "_bh" is intended to stand for "half".  Also, in this case,
                 the kernels are only required to hold _bv blend vectors,
                 corresponding to the first kernel tap (k=0).  A succession of
                 _klen progressively shifted half-length source vectors are
                 read from the input and exposed to this single set of
                 permutations (blend vectors) to generate the full set of
                 inputs to the interpolation kernels.  In some cases, this
                 allows _bv to be as small as 1, even though the source
                 vectors are only of half length.  See the extensive notes
                 appearing with the definition of `kdrd_simd_horz_fix16_func'
                 for more on this.
           Other values are not defined.
           Blend vectors for fixed-point processing are always byte oriented,
           so there is no need for this macro to provide a "_bb" argument,
           as found in `KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC'.
  */
  
  
  
} // namespace kd_supp_simd

#endif // X86_REGION_DECOMPRESSOR_LOCAL_H
