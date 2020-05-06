/*****************************************************************************/
// File: x86_region_compositor_local.h [scope = CORESYS/TRANSFORMS]
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
   Implements critical layer composition and alpha blending functions using
SSE/SSE2/AVX2 intrinsics.  These can be compiled under GCC or .NET and are
compatible with both 32-bit and 64-bit builds.  AVX2 variants are
imported as external functions, where appropriate, implemented within
"avx2_region_compositor.cpp".
******************************************************************************/

#ifndef X86_REGION_COMPOSITOR_LOCAL_H
#define X86_REGION_COMPOSITOR_LOCAL_H
#include "kdu_arch.h"

#include <emmintrin.h>

namespace kd_supp_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                         Erase and Copy Functions                          */
/* ========================================================================= */

/*****************************************************************************/
/*                            ..._erase_region                               */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_erase_region(kdu_uint32 *dst, int height, int width, int row_gap,
                    kdu_uint32 erase)
{
  __m128i val = _mm_set1_epi32((kdu_int32) erase);
  for (; height > 0; height--, dst+=row_gap)
    { 
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int octets = (width-left)>>3;
      int c, right = width - left - (octets<<3);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--)
            *(dp++) = erase;
        }
      for (c=octets; c > 0; c--, dp+=8)
        { ((__m128i *) dp)[0] = val; ((__m128i *) dp)[1] = val; }
      for (c=right; c > 0; c--)
        *(dp++) = erase;
    }
}
#  define SSE2_SET_ERASE_REGION_FUNC(_func) \
     if (kdu_mmx_level >= 2) {_func=sse2_erase_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_ERASE_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_ERASE_REGION_FUNC(_func) \
  { \
    SSE2_SET_ERASE_REGION_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                          ..._erase_region_float                           */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_erase_region_float(float *dst, int height, int width, int row_gap,
                          float erase[])
{
  __m128 val = _mm_loadu_ps(erase);
  for (; height > 0; height--, dst+=row_gap)
    { 
      float *dp=dst;
      for (int c=width; c > 0; c--, dp+=4)
        _mm_storeu_ps(dp,val); // Unaligned, just in case buffer not aligned on
                               // whole pixel boundary (pixel size = 16 bytes)
    }
}
#  define SSE2_SET_ERASE_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_erase_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_ERASE_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_ERASE_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_ERASE_REGION_FLOAT_FUNC(_func) \
  }

/*****************************************************************************/
/*                             ..._copy_region                               */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_copy_region(kdu_uint32 *dst, kdu_uint32 *src,
                   int height, int width, int dst_row_gap, int src_row_gap)
{
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *dp = dst;
      kdu_uint32 *sp = src;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int octets = (width-left)>>3;
      int c, right = width - left - (octets<<3);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--)
            *(dp++) = *(sp++);
        }
      for (c=octets; c > 0; c--, dp+=8, sp+=8)
        {
          __m128i val0 = _mm_loadu_si128((__m128i *) sp);
          __m128i val1 = _mm_loadu_si128((__m128i *)(sp+4));
          ((__m128i *) dp)[0] = val0;
          ((__m128i *) dp)[1] = val1;
        }
      for (c=right; c > 0; c--)
        *(dp++) = *(sp++);
    }
}
#  define SSE2_SET_COPY_REGION_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_copy_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_COPY_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_COPY_REGION_FUNC(_func) \
  { \
    SSE2_SET_COPY_REGION_FUNC(_func) \
  }

/*****************************************************************************/
/*                          ..._copy_region_float                            */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_copy_region_float(float *dst, float *src, int height, int width,
                         int dst_row_gap, int src_row_gap)
{
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    {
      float *dp=dst, *sp=src;
      for (int c=width; c > 0; c--, dp+=4, sp+=4)
        _mm_storeu_ps(dp,_mm_loadu_ps(sp));
    }
}
#  define SSE2_SET_COPY_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_copy_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_COPY_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_COPY_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_COPY_REGION_FLOAT_FUNC(_func) \
  }

/*****************************************************************************/
/*                             ..._rcopy_region                              */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_rcopy_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int row_gap)
{
  for (; height > 0; height--, dst-=row_gap, src-=row_gap)
    { 
      kdu_uint32 *dp = dst;
      kdu_uint32 *sp = src;
      int right = (_addr_to_kdu_int32(dp) >> 2) & 3;
      int octets = (width-right)>>3;
      int c, left = width - right - (octets<<3);
      if (right)
        { 
          for (c=(right<width)?right:width; c > 0; c--)
            *(--dp) = *(--sp);
        }
      for (c=octets; c > 0; c--)
        { 
          sp -= 8;  dp -= 8;
          __m128i val0 = _mm_loadu_si128((__m128i *) sp);
          __m128i val1 = _mm_loadu_si128((__m128i *)(sp+4));
          ((__m128i *) dp)[0] = val0;
          ((__m128i *) dp)[1] = val1;
        }
      for (c=left; c > 0; c--)
        *(--dp) = *(--sp);
    }
}
#  define SSE2_SET_RCOPY_REGION_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_rcopy_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_RCOPY_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_RCOPY_REGION_FUNC(_func) \
  { \
    SSE2_SET_RCOPY_REGION_FUNC(_func) \
  }

/*****************************************************************************/
/*                          ..._rcopy_region_float                           */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_rcopy_region_float(float *dst, float *src, int height, int width,
                          int row_gap)
{
  for (; height > 0; height--, dst-=row_gap, src-=row_gap)
    { 
      float *dp=dst, *sp=src;
      for (int c=width; c > 0; c--)
        { dp-=4; sp-=4; _mm_storeu_ps(dp,_mm_loadu_ps(sp)); }
    }
}
#  define SSE2_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_rcopy_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_RCOPY_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
  }

  
/* ========================================================================= */
/*                              Blend Functions                              */
/* ========================================================================= */

/*****************************************************************************/
/*                              ..._blend_region                             */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int dst_row_gap, int src_row_gap);
#  define AVX2_SET_BLEND_REGION_FUNC(_func) \
  if (kdu_mmx_level >= 7) {_func=avx2_blend_region; }
#else // No compilation support for AVX2
#  define AVX2_SET_BLEND_REGION_FUNC(_func)
#endif
  
#ifndef KDU_NO_SSE
static void
  sse2_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int dst_row_gap, int src_row_gap)
{
  // Create all-zero double quad-word
  __m128i zero = _mm_setzero_si128();

  // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this to force the source alpha value
  // to 255 as part of the alpha-blending procedure.
  __m128i mask = _mm_cmpeq_epi16(zero,zero);
  mask = _mm_slli_epi32(mask,24);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              // Load 1 source pixel and 1 target pixel
              __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
              __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
              __m128i alpha_shift = _mm_slli_epi32(alpha,7);
              src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha_shift = _mm_slli_epi32(alpha_shift,8);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
              
              // Unpack source and target pixels into words
              src_val = _mm_unpacklo_epi8(src_val,zero);
              dst_val = _mm_unpacklo_epi8(dst_val,zero);
              
              // Copy the alpha factor into all word positions
              __m128i factors = _mm_shufflelo_epi16(alpha,0);
              
              // Get difference between source and target values then scale and
              // add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              __m128i diff = _mm_sub_epi16(src_val,dst_val);
              diff = _mm_slli_epi16(diff,2); // Because max alpha factor = 2^14
              diff = _mm_mulhi_epi16(diff,factors);
              dst_val = _mm_add_epi16(dst_val,diff);
              
              // Finally, pack words into bytes and save the pixel
              dst_val = _mm_packus_epi16(dst_val,dst_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        { 
          // Load 4 source pixels and 4 target pixels
          __m128i src_val = _mm_loadu_si128((__m128i *) sp);
          __m128i dst_val = *((__m128i *) dp);

          // Find normalized alpha factors in the range 0 to 2^14, inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24);
                     // Leaves 8-bit alpha only in each pixel's DWORD
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255          
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14

          // Unpack source and target pixels into words
          __m128i src_low = _mm_unpacklo_epi8(src_val,zero);
          __m128i src_high = _mm_unpackhi_epi8(src_val,zero);
          __m128i dst_low = _mm_unpacklo_epi8(dst_val,zero);
          __m128i dst_high = _mm_unpackhi_epi8(dst_val,zero);

          // Unpack and arrange alpha factors so that red, green, blue and
          // alpha word positions all have the same alpha factor.
          __m128i factors_low = _mm_unpacklo_epi32(alpha,zero);
          __m128i factors_high = _mm_unpackhi_epi32(alpha,zero);
          factors_low = _mm_shufflelo_epi16(factors_low,0);
          factors_low = _mm_shufflehi_epi16(factors_low,0);
          factors_high = _mm_shufflelo_epi16(factors_high,0);
          factors_high = _mm_shufflehi_epi16(factors_high,0);

          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that alpha
          // has already been replaced by 255 in the source, which is correct.
          __m128i diff = _mm_sub_epi16(src_low,dst_low);
          diff = _mm_slli_epi16(diff,2); // Because max alpha factor = 2^14
          diff = _mm_mulhi_epi16(diff,factors_low);
          dst_low = _mm_add_epi16(dst_low,diff);
          diff = _mm_sub_epi16(src_high,dst_high);
          diff = _mm_slli_epi16(diff,2); // Because max alpha factor is 2^14
          diff = _mm_mulhi_epi16(diff,factors_high);
          dst_high = _mm_add_epi16(dst_high,diff);
    
          // Finally, pack `dst_low' and `dst_high' into bytes and save
          *((__m128i *) dp) = _mm_packus_epi16(dst_low,dst_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { 
          // Load 1 source pixel and 1 target pixel
          __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
          __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
        
          // Unpack source and target pixels into words
          src_val = _mm_unpacklo_epi8(src_val,zero);
          dst_val = _mm_unpacklo_epi8(dst_val,zero);
        
          // Copy the alpha factor into all word positions
          __m128i factors = _mm_shufflelo_epi16(alpha,0);
        
          // Get difference between source and target values then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m128i diff = _mm_sub_epi16(src_val,dst_val);
          diff = _mm_slli_epi16(diff,2); // Because max alpha factor = 2^14
          diff = _mm_mulhi_epi16(diff,factors);
          dst_val = _mm_add_epi16(dst_val,diff);
        
          // Finally, pack words into bytes and save the pixel
          dst_val = _mm_packus_epi16(dst_val,dst_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
        }
    }
}
#  define SSE2_SET_BLEND_REGION_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_blend_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_BLEND_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_BLEND_REGION_FUNC(_func) \
  { \
    SSE2_SET_BLEND_REGION_FUNC(_func) \
    AVX2_SET_BLEND_REGION_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                          ..._blend_region_float                           */
/*****************************************************************************/
  
#ifndef KDU_NO_SSE
static void
  sse2_blend_region_float(float *dst, float *src, int height, int width,
                          int dst_row_gap, int src_row_gap)
{ 
  __m128 one_val = _mm_set1_ps(1.0F);
  
  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    {
      float *sp=src, *dp=dst;
      for (int c=width; c > 0; c--, sp+=4, dp+=4)
        { 
          __m128 src_val = _mm_loadu_ps(sp);
          __m128 dst_val = _mm_loadu_ps(dp);
          __m128 alpha = _mm_shuffle_ps(src_val,src_val,0); // Replicates alpha
          src_val = _mm_move_ss(src_val,one_val);
          __m128 diff = _mm_sub_ps(src_val,dst_val);
          diff = _mm_mul_ps(diff,alpha);
          _mm_storeu_ps(dp,_mm_add_ps(dst_val,diff));
        }
    }
}
#  define SSE2_SET_BLEND_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_blend_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_BLEND_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_BLEND_REGION_FLOAT_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                         ..._premult_blend_region                          */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                            int height, int width, int dst_row_gap,
                            int src_row_gap);
#  define AVX2_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  if (kdu_mmx_level >= 7) {_func=avx2_premult_blend_region; }
#else // No compilation support for AVX2
#  define AVX2_SET_PREMULT_BLEND_REGION_FUNC(_func)
#endif

#ifndef KDU_NO_SSE
static void
  sse2_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                            int height, int width, int dst_row_gap,
                            int src_row_gap)
{
  // Create all-zero double quad-word
  __m128i zero = _mm_setzero_si128();

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              // Load 1 source pixel and 1 target pixel
              __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
              __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);

              // Find normalized alpha factor in the range 0 to 2^14, inclusive
              __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
              __m128i alpha_shift = _mm_slli_epi32(alpha,7);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha_shift = _mm_slli_epi32(alpha_shift,8);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.

              // Unpack source and target pixel into words
              src_val = _mm_unpacklo_epi8(src_val,zero);
              dst_val = _mm_unpacklo_epi8(dst_val,zero);

              // Copy alpha factor into red, green, blue & alpha word positions
              __m128i factors = _mm_shufflelo_epi16(alpha,0);

              // Add source and target pixels, then subtract the alpha-scaled
              // target pixel.
              src_val = _mm_add_epi16(src_val,dst_val);
              dst_val = _mm_slli_epi16(dst_val,2); // Because max factor = 2^14
              dst_val = _mm_mulhi_epi16(dst_val,factors);
              src_val = _mm_sub_epi16(src_val,dst_val);

              // Finally, pack words into bytes and save the pixel
              src_val = _mm_packus_epi16(src_val,src_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(src_val);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        {
          // Load 4 source pixels and 4 target pixels
          __m128i src_val = _mm_loadu_si128((__m128i *) sp);
          __m128i dst_val = *((__m128i *) dp);

          // Find normalized alpha factors from 4 source pels
          __m128i alpha = _mm_srli_epi32(src_val,24);
                     // Leaves 8-bit alpha only in each pixel's DWORD
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14

          // Unpack source and target pixels into words
          __m128i src_low = _mm_unpacklo_epi8(src_val,zero);
          __m128i src_high = _mm_unpackhi_epi8(src_val,zero);
          __m128i dst_low = _mm_unpacklo_epi8(dst_val,zero);
          __m128i dst_high = _mm_unpackhi_epi8(dst_val,zero);

          // Unpack and copy alpha factors into the red, green, blue and
          // alpha word positions.
          __m128i factors_low = _mm_unpacklo_epi32(alpha,zero);
          __m128i factors_high = _mm_unpackhi_epi32(alpha,zero);
          factors_low = _mm_shufflelo_epi16(factors_low,0);
          factors_low = _mm_shufflehi_epi16(factors_low,0);
          factors_high = _mm_shufflelo_epi16(factors_high,0);
          factors_high = _mm_shufflehi_epi16(factors_high,0);

          // Add source and target pixels and then subtract the alpha-scaled
          // target pixels.
          src_low = _mm_add_epi16(src_low,dst_low);
          dst_low = _mm_slli_epi16(dst_low,2); // Because max factor is 2^14
          dst_low = _mm_mulhi_epi16(dst_low,factors_low);
          src_low = _mm_sub_epi16(src_low,dst_low);
          src_high = _mm_add_epi16(src_high,dst_high);
          dst_high = _mm_slli_epi16(dst_high,2); // Because max factor is 2^14
          dst_high = _mm_mulhi_epi16(dst_high,factors_high);
          src_high = _mm_sub_epi16(src_high,dst_high);
    
          // Finally, pack `src_low' and `src_high' into bytes and save
          *((__m128i *) dp) = _mm_packus_epi16(src_low,src_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        {
          // Load 1 source pixel and 1 target pixel
          __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
          __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);

          // Find normalized alpha factor in the range 0 to 2^14, inclusive
          __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.

          // Unpack source and target pixel into words
          src_val = _mm_unpacklo_epi8(src_val,zero);
          dst_val = _mm_unpacklo_epi8(dst_val,zero);

          // Copy alpha factor into red, green, blue and alpha word positions
          __m128i factors = _mm_shufflelo_epi16(alpha,0);

          // Add source and target pixels and then subtract the alpha-scaled
          // target pixel.
          src_val = _mm_add_epi16(src_val,dst_val);
          dst_val = _mm_slli_epi16(dst_val,2); // Because max factor is 2^14
          dst_val = _mm_mulhi_epi16(dst_val,factors);
          src_val = _mm_sub_epi16(src_val,dst_val);

          // Finally, pack words into bytes and save the pixel
          src_val = _mm_packus_epi16(src_val,src_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(src_val);
        }
    }
}
#  define SSE2_SET_PREMULT_BLEND_REGION_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_premult_blend_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_PREMULT_BLEND_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  { \
    SSE2_SET_PREMULT_BLEND_REGION_FUNC(_func) \
    AVX2_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                      ..._premult_blend_region_float                       */
/*****************************************************************************/

#ifndef KDU_NO_SSE2
static void
  sse2_premult_blend_region_float(float *dst, float *src,
                                  int height, int width, int dst_row_gap,
                                  int src_row_gap)
{
  __m128 one_val = _mm_set1_ps(1.0F);
  
  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    {
      float *sp=src, *dp=dst;
      for (int c=width; c > 0; c--, sp+=4, dp+=4)
        { 
          __m128 src_val = _mm_loadu_ps(sp);
          __m128 dst_val = _mm_loadu_ps(dp);
          __m128 alpha = _mm_shuffle_ps(src_val,src_val,0); // Replicates alpha
          src_val = _mm_add_ps(src_val,dst_val);
          dst_val = _mm_mul_ps(dst_val,alpha);
          src_val = _mm_sub_ps(src_val,dst_val);
          _mm_storeu_ps(dp,_mm_min_ps(src_val,one_val)); // Clip to 1.0
        }
    }
}
#  define SSE2_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_premult_blend_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                         ..._scaled_blend_region                           */
/*****************************************************************************/

#ifndef KDU_NO_AVX2
extern void
  avx2_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, kdu_int16 alpha_factor_x128);
#  define AVX2_SET_SCALED_BLEND_REGION_FUNC(_func) \
  if (kdu_mmx_level >= 7) {_func=avx2_scaled_blend_region; }
#else // No compilation support for AVX2
#  define AVX2_SET_SCALED_BLEND_REGION_FUNC(_func)
#endif

#ifndef KDU_NO_SSE2
static void
  sse2_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, kdu_int16 alpha_factor_x128)
{
  // Create all-zero double quad-word
  __m128i zero = _mm_setzero_si128();

  // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this to force the source alpha value
  // to 255 as part of the alpha-blending procedure.
  __m128i mask = _mm_cmpeq_epi16(zero,zero);
  mask = _mm_slli_epi32(mask,24);
  
  // Create an XOR mask to handle negative alpha factors
  __m128i xor_mask = zero;
  if (alpha_factor_x128 < 0)
    { 
      alpha_factor_x128 = -alpha_factor_x128;
      xor_mask = _mm_set1_epi32(0x00FFFFFF);
    }
  
  // Create 4 copies of -`alpha_factor_x128' in a 128-bit vector
  __m128i neg_alpha_scale = _mm_set1_epi32(-alpha_factor_x128);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              // Load 1 source pixel and 1 target pixel
              __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
              __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);

              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
              __m128i alpha_shift = _mm_slli_epi32(alpha,7);
              src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255
              src_val = _mm_xor_si128(src_val,xor_mask); // May invert colours
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha_shift = _mm_slli_epi32(alpha_shift,8);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
          
              // Scale and clip the normalized alpha values
              alpha = _mm_madd_epi16(alpha,neg_alpha_scale);
              alpha = _mm_srai_epi32(alpha,6); // Nom. alpha range = 0 to -2^15
              alpha = _mm_packs_epi32(alpha,alpha);// Saturate & pack 2 copies

              // Unpack source and target pixels into words
              src_val = _mm_unpacklo_epi8(src_val,zero);
              dst_val = _mm_unpacklo_epi8(dst_val,zero);

              // Copy the alpha factor into all word positions
              __m128i factors = _mm_shufflelo_epi16(alpha,0);

              // Get difference between source and target values then scale and
              // add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              __m128i diff = _mm_sub_epi16(src_val,dst_val);
              diff = _mm_add_epi16(diff,diff); // Because max alpha fact = 2^15
              diff = _mm_mulhi_epi16(diff,factors);
              dst_val = _mm_sub_epi16(dst_val,diff);// Subtract since alpha -ve

              // Finally, pack words into bytes and save the pixel
              dst_val = _mm_packus_epi16(dst_val,dst_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        { 
          // Load 4 source pixels and 4 target pixels
          __m128i src_val = _mm_loadu_si128((__m128i *) sp);
          __m128i dst_val = *((__m128i *) dp);

          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24);
                     // Leaves 8-bit alpha only in each pixel's DWORD
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255
          src_val = _mm_xor_si128(src_val,xor_mask); // May flip colours
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14
          
          // Scale and clip the normalized alpha values
          alpha = _mm_madd_epi16(alpha,neg_alpha_scale);
          alpha = _mm_srai_epi32(alpha,6); // Nom. range of alpha -> 0 to -2^15
          alpha = _mm_packs_epi32(alpha,alpha); // Saturates and packs 2 copies
          
          // Unpack source and target pixels into words
          __m128i src_low = _mm_unpacklo_epi8(src_val,zero);
          __m128i src_high = _mm_unpackhi_epi8(src_val,zero);
          __m128i dst_low = _mm_unpacklo_epi8(dst_val,zero);
          __m128i dst_high = _mm_unpackhi_epi8(dst_val,zero);

          // Unpack and arrange alpha factors so that red, green, blue and
          // alpha word positions all have the same alpha factor.
          __m128i factors_low, factors_high;
          factors_low = _mm_shufflelo_epi16(alpha,0x00);
          factors_low = _mm_shufflehi_epi16(factors_low,0x55);
          factors_high = _mm_shufflelo_epi16(alpha,0xAA);
          factors_high = _mm_shufflehi_epi16(factors_high,0xFF);

          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m128i diff = _mm_sub_epi16(src_low,dst_low);
          diff = _mm_add_epi16(diff,diff); // Because max alpha factor is 2^15
          diff = _mm_mulhi_epi16(diff,factors_low);
          dst_low = _mm_sub_epi16(dst_low,diff); // Subtract because alpha -ve
          diff = _mm_sub_epi16(src_high,dst_high);
          diff = _mm_add_epi16(diff,diff); // Because max alpha factor is 2^15
          diff = _mm_mulhi_epi16(diff,factors_high);
          dst_high = _mm_sub_epi16(dst_high,diff); // Subtract since alpha -ve
    
          // Finally, pack `dst_low' and `dst_high' into bytes and save
          *((__m128i *) dp) = _mm_packus_epi16(dst_low,dst_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { 
          // Load 1 source pixel and 1 target pixel
          __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
          __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
        
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val,mask); // Sets source alpha to 255
          src_val = _mm_xor_si128(src_val,xor_mask); // May invert colours
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
        
          // Scale and clip the normalized alpha values
          alpha = _mm_madd_epi16(alpha,neg_alpha_scale);
          alpha = _mm_srai_epi32(alpha,6); // Nom. alpha range = 0 to -2^15
          alpha = _mm_packs_epi32(alpha,alpha);// Saturate & pack 2 copies
        
          // Unpack source and target pixels into words
          src_val = _mm_unpacklo_epi8(src_val,zero);
          dst_val = _mm_unpacklo_epi8(dst_val,zero);
        
          // Copy the alpha factor into all word positions
          __m128i factors = _mm_shufflelo_epi16(alpha,0);
        
          // Get difference between source and target values then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m128i diff = _mm_sub_epi16(src_val,dst_val);
          diff = _mm_add_epi16(diff,diff); // Because max alpha fact = 2^15
          diff = _mm_mulhi_epi16(diff,factors);
          dst_val = _mm_sub_epi16(dst_val,diff);// Subtract since alpha -ve
        
          // Finally, pack words into bytes and save the pixel
          dst_val = _mm_packus_epi16(dst_val,dst_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
        }
    }
}
#  define SSE2_SET_SCALED_BLEND_REGION_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_scaled_blend_region; }
#else // No compilation support for SSE2
#  define SSE2_SET_SCALED_BLEND_REGION_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_SCALED_BLEND_REGION_FUNC(_func) \
  { \
    SSE2_SET_SCALED_BLEND_REGION_FUNC(_func) \
    AVX2_SET_SCALED_BLEND_REGION_FUNC(_func) \
  }
  
/*****************************************************************************/
/*                       ..._scaled_blend_region_float                       */
/*****************************************************************************/

#ifndef KDU_NO_SSE
static void
  sse2_scaled_blend_region_float(float *dst, float *src,
                                 int height, int width, int dst_row_gap,
                                 int src_row_gap, float alpha_factor)
{  
  __m128 one_val = _mm_set1_ps(1.0F);
  __m128 zero_val = _mm_set1_ps(0.0F);
  
  // Now for the processing loops
  if (alpha_factor >= 0.0f)
    { 
      __m128 alpha_fact = _mm_set1_ps(alpha_factor);
      for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
        { 
          float *sp=src, *dp=dst;
          for (int c=width; c > 0; c--, sp+=4, dp+=4)
            { 
              __m128 src_val = _mm_loadu_ps(sp);
              __m128 dst_val = _mm_loadu_ps(dp);
              __m128 alpha = _mm_shuffle_ps(src_val,src_val,0); // Rep. alpha
              alpha = _mm_mul_ps(alpha,alpha_fact);
              src_val = _mm_move_ss(src_val,one_val);
              __m128 diff = _mm_sub_ps(src_val,dst_val);
              diff = _mm_mul_ps(diff,alpha);
              dst_val = _mm_add_ps(dst_val,diff);
              dst_val = _mm_min_ps(dst_val,one_val);
              _mm_storeu_ps(dp,_mm_max_ps(dst_val,zero_val));
            }
        }
    }
  else
    { // Use -`alpha_factor' with inverted colour channels
      __m128 alpha_fact = _mm_set1_ps(-alpha_factor);
      for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
        { 
          float *sp=src, *dp=dst;
          for (int c=width; c > 0; c--, sp+=4, dp+=4)
            { 
              __m128 src_val = _mm_loadu_ps(sp);
              __m128 dst_val = _mm_loadu_ps(dp);
              __m128 alpha = _mm_shuffle_ps(src_val,src_val,0); // Rep. alpha
              alpha = _mm_mul_ps(alpha,alpha_fact);
              src_val = _mm_move_ss(src_val,zero_val); // Zero the alpha value
                         // so that 1-src_val will hold 1 in the alpha ch∫ 1 - (src_val+dst_val)
              __m128 neg_diff=_mm_sub_ps(_mm_add_ps(src_val,dst_val),one_val);
              neg_diff = _mm_mul_ps(neg_diff,alpha);
              dst_val = _mm_sub_ps(dst_val,neg_diff);
              dst_val = _mm_min_ps(dst_val,one_val);
              _mm_storeu_ps(dp,_mm_max_ps(dst_val,zero_val));
            }
        }
    }
}
#  define SSE2_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
    if (kdu_mmx_level >= 2) {_func=sse2_scaled_blend_region_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func)
#endif
  
#define KDRC_SIMD_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    SSE2_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
  }  
    
} // namespace kd_supp_simd

#endif // X86_REGION_COMPOSITOR_LOCAL_H
