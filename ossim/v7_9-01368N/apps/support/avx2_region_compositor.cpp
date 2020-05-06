/*****************************************************************************/
// File: avx2_region_compositor.cpp [scope = APPS/SUPPORT]
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
   Provides SIMD implementations to accelerate layer composition and alpha
blending operations, taking advantage of the AVX and AVX2 instruction sets.
The functions defined here may be selected at run-time via macros defined
in "x86_region_compositor_local.h", depending on run-time CPU detection, as
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

namespace kd_supp_simd {
  using namespace kdu_core;


/* ========================================================================= */
/*                               Blend Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                       avx2_blend_region                            */
/*****************************************************************************/

void
  avx2_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int dst_row_gap, int src_row_gap)
{
  // Create all-zero double quad-word
  __m256i zero = _mm256_setzero_si256();
  
  // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this to force the source alpha value
  // to 255 as part of the alpha-blending procedure.
  __m256i mask = _mm256_cmpeq_epi16(zero,zero);
  mask = _mm256_slli_epi32(mask,24);
  
  // Create a shuffle vector for duplicating the 16 LSB's of each dword into
  // both 16-bit words of the dword
  __m128i tmp = _mm_set_epi32(0x0D0C0D0C,0x09080908,0x05040504,0x01000100);
  __m256i alpha_shuffle = _mm256_broadcastsi128_si256(tmp);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 7;
      int c, octets = (width-left)>>3;
      int right = width - left - (octets<<3);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { // Process 1 pixel at a time using 128-bit operands
              // Load 1 source pixel and 1 target pixel
              __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
              __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
              __m128i alpha_shift = _mm_slli_epi32(alpha,7);
              src_val = _mm_or_si128(src_val, // Set source alpha to 255
                                     _mm256_castsi256_si128(mask));              
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha_shift = _mm_slli_epi32(alpha_shift,8);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
              
              // Unsigned extend source and target samples to words
              src_val = _mm_cvtepu8_epi16(src_val);
              dst_val = _mm_cvtepu8_epi16(dst_val);
              
              // Copy the alpha factor into all word positions
              alpha = _mm_shufflelo_epi16(alpha,0);
              
              // Get difference between source and target values then scale and
              // add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              __m128i diff = _mm_sub_epi16(src_val,dst_val);
              diff = _mm_slli_epi16(diff,2); // Because max alpha factor = 2^14
              diff = _mm_mulhi_epi16(diff,alpha);
              dst_val = _mm_add_epi16(dst_val,diff);
              
              // Finally, pack words into bytes and save the pixel
              dst_val = _mm_packus_epi16(dst_val,dst_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
            }
        }
      for (c=octets; c > 0; c--, sp+=8, dp+=8)
        { // Process 8 pixels (32 samples) at a time, using 256-bit operands
          // Load 8 source pixels
          __m256i src_low, dst_low, src_high, dst_high, alpha_low, alpha_high;
          src_low = _mm256_loadu_si256((__m256i *) sp);
          dst_low = *((__m256i *) dp);
          
          // Find normalized alpha factor in the range 0 to 2^14, inclusive,
          // replacing the original alpha value by 255 in `src_val'
          alpha_low = _mm256_srli_epi32(src_low,24); // Get alpha only
          __m256i alpha_shift = _mm256_slli_epi32(alpha_low,7);
          src_low = _mm256_or_si256(src_low,mask); // Sets source alpha to 255
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_shift = _mm256_slli_epi32(alpha_shift,8);
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_low = _mm256_srli_epi32(alpha_low,9); // Leave max alpha = 2^14
          
          // Unpack source pixels to two vectors, with 16 bits per sample
          src_high = _mm256_unpackhi_epi8(src_low,zero);
          dst_high = _mm256_unpackhi_epi8(dst_low,zero);
          src_low  = _mm256_unpacklo_epi8(src_low,zero);
          dst_low  = _mm256_unpacklo_epi8(dst_low,zero);
          
          // Duplicate the 16-bit alpha values to make two copies in each
          // 32-bit original pixel, then unpack to two vectors, with alpha
          // duplicated into every 16-bit source sample.
          alpha_low = _mm256_shuffle_epi8(alpha_low,alpha_shuffle);
          alpha_high = _mm256_unpackhi_epi32(alpha_low,alpha_low);
          alpha_low = _mm256_unpacklo_epi32(alpha_low,alpha_low);
          
          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that alpha
          // has already been replaced by 255 in the source, which is correct.
          __m256i diff_low = _mm256_sub_epi16(src_low,dst_low);
          __m256i diff_high = _mm256_sub_epi16(src_high,dst_high);
          diff_low = _mm256_slli_epi16(diff_low,2); // Adjust for the fact that
          diff_high = _mm256_slli_epi16(diff_high,2); // max alpha factor=2^14
          diff_low = _mm256_mulhi_epi16(diff_low,alpha_low);
          diff_high = _mm256_mulhi_epi16(diff_high,alpha_high);
          dst_low = _mm256_add_epi16(dst_low,diff_low);
          dst_high = _mm256_add_epi16(dst_high,diff_high);
          
          // Finally, pack `dst_low' and `dst_high' down into bytes and save
          *((__m256i *) dp) = _mm256_packus_epi16(dst_low,dst_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { // Process 1 pixel at a time using 128-bit operands
          // Load 1 source pixel and 1 target pixel
          __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
          __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val, // Set source alpha to 255
                                 _mm256_castsi256_si128(mask));              
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
          
          // Unsigned extend source and target samples to words
          src_val = _mm_cvtepu8_epi16(src_val);
          dst_val = _mm_cvtepu8_epi16(dst_val);
          
          // Copy the alpha factor into all word positions
          alpha = _mm_shufflelo_epi16(alpha,0);
          
          // Get difference between source and target values then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m128i diff = _mm_sub_epi16(src_val,dst_val);
          diff = _mm_slli_epi16(diff,2); // Because max alpha factor = 2^14
          diff = _mm_mulhi_epi16(diff,alpha);
          dst_val = _mm_add_epi16(dst_val,diff);
          
          // Finally, pack words into bytes and save the pixel
          dst_val = _mm_packus_epi16(dst_val,dst_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_premult_blend_region                        */
/*****************************************************************************/

void
  avx2_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src, int height,
                            int width, int dst_row_gap, int src_row_gap)
{
  // Create all-zero double quad-word
  __m256i zero = _mm256_setzero_si256();
   
  // Create a shuffle vector for duplicating the 16 LSB's of each dword into
  // both 16-bit words of the dword
  __m128i tmp = _mm_set_epi32(0x0D0C0D0C,0x09080908,0x05040504,0x01000100);
  __m256i alpha_shuffle = _mm256_broadcastsi128_si256(tmp);
  
  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 7;
      int c, octets = (width-left)>>3;
      int right = width - left - (octets<<3);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { // Process 1 pixel at a time using 128-bit operands
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
              
              // Unsigned extend source and target samples to words
              src_val = _mm_cvtepu8_epi16(src_val);
              dst_val = _mm_cvtepu8_epi16(dst_val);
              
              // Copy the alpha factor into all word positions
              alpha = _mm_shufflelo_epi16(alpha,0);
              
              // Add source and target pixels, then subtract the alpha-scaled
              // target pixel.
              src_val = _mm_add_epi16(src_val,dst_val);
              dst_val = _mm_slli_epi16(dst_val,2); // Because max factor = 2^14
              dst_val = _mm_mulhi_epi16(dst_val,alpha);
              src_val = _mm_sub_epi16(src_val,dst_val);
              
              // Finally, pack words into bytes and save the pixel
              src_val = _mm_packus_epi16(src_val,src_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(src_val);
            }
        }
      for (c=octets; c > 0; c--, sp+=8, dp+=8)
        { // Process 8 pixels (32 samples) at a time, using 256-bit operands
          // Load 8 source pixels and zero extend into two vectors, each with
          // 4 pixels (16 samples) of 16-bit words.
          __m256i src_low, dst_low, src_high, dst_high, alpha_low, alpha_high;
          src_low = _mm256_loadu_si256((__m256i *) sp);
          dst_low = *((__m256i *) dp);
          
          // Find normalized alpha factor in the range 0 to 2^14, inclusive
          alpha_low = _mm256_srli_epi32(src_low,24); // Get alpha only
          __m256i alpha_shift = _mm256_slli_epi32(alpha_low,7);
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_shift = _mm256_slli_epi32(alpha_shift,8);
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_low = _mm256_srli_epi32(alpha_low,9); // Leave max alpha = 2^14
          
          // Unpack source pixels to two vectors, with 16 bits per sample
          src_high = _mm256_unpackhi_epi8(src_low,zero);
          dst_high = _mm256_unpackhi_epi8(dst_low,zero);
          src_low  = _mm256_unpacklo_epi8(src_low,zero);
          dst_low  = _mm256_unpacklo_epi8(dst_low,zero);
          
          // Duplicate the 16-bit alpha values to make two copies in each
          // 32-bit original pixel, then unpack to two vectors, with alpha
          // duplicated into every 16-bit source sample.
          alpha_low = _mm256_shuffle_epi8(alpha_low,alpha_shuffle);
          alpha_high = _mm256_unpackhi_epi32(alpha_low,alpha_low);
          alpha_low = _mm256_unpacklo_epi32(alpha_low,alpha_low);

          // Add source and target pixels and then subtract the alpha-scaled
          // target pixels.
          src_low = _mm256_add_epi16(src_low,dst_low);
          src_high = _mm256_add_epi16(src_high,dst_high);
          dst_low = _mm256_slli_epi16(dst_low,2); // Adjust for the fact that
          dst_high = _mm256_slli_epi16(dst_high,2); // max alpha is 2^14
          dst_low = _mm256_mulhi_epi16(dst_low,alpha_low);
          dst_high = _mm256_mulhi_epi16(dst_high,alpha_high);
          src_low = _mm256_sub_epi16(src_low,dst_low);
          src_high = _mm256_sub_epi16(src_high,dst_high);

          // Finally, pack `src_low' and `src_high' down into bytes and save
          *((__m256i *) dp) = _mm256_packus_epi16(src_low,src_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { // Process 1 pixel at a time using 128-bit operands
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
        
          // Unsigned extend source and target samples to words
          src_val = _mm_cvtepu8_epi16(src_val);
          dst_val = _mm_cvtepu8_epi16(dst_val);
          
          // Copy the alpha factor into all word positions
          alpha = _mm_shufflelo_epi16(alpha,0);
        
          // Add source and target pixels, then subtract the alpha-scaled
          // target pixel.
          src_val = _mm_add_epi16(src_val,dst_val);
          dst_val = _mm_slli_epi16(dst_val,2); // Because max factor = 2^14
          dst_val = _mm_mulhi_epi16(dst_val,alpha);
          src_val = _mm_sub_epi16(src_val,dst_val);
          
          // Finally, pack words into bytes and save the pixel
          src_val = _mm_packus_epi16(src_val,src_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(src_val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_scaled_blend_region                         */
/*****************************************************************************/

void
  avx2_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, kdu_int16 alpha_factor_x128)
{
  // Create all-zero double quad-word
  __m256i zero = _mm256_setzero_si256();
  
  // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this to force the source alpha value
  // to 255 as part of the alpha-blending procedure.
  __m256i mask = _mm256_cmpeq_epi16(zero,zero);
  mask = _mm256_slli_epi32(mask,24);

  // Create an XOR mask to handle negative alpha factors
  __m256i xor_mask = zero;
  if (alpha_factor_x128 < 0)
    { 
      alpha_factor_x128 = -alpha_factor_x128;
      xor_mask = _mm256_set1_epi32(0x00FFFFFF);
    }
  
  // Create 8 copies of -`alpha_factor_x128' in a 256-bit vector
  __m256i neg_alpha_scale = _mm256_set1_epi32(-alpha_factor_x128);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp = src;
      kdu_uint32 *dp = dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 7;
      int c, octets = (width-left)>>3;
      int right = width - left - (octets<<3);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { // Process 1 pixel at a time using 128-bit operands
              // Load 1 source pixel and 1 target pixel
              __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
              __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
              __m128i alpha_shift = _mm_slli_epi32(alpha,7);
              src_val = _mm_or_si128(src_val, // Set source alpha to 255
                                     _mm256_castsi256_si128(mask));              
              src_val = _mm_xor_si128(src_val,
                                      _mm256_castsi256_si128(xor_mask));
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha_shift = _mm_slli_epi32(alpha_shift,8);
              alpha = _mm_add_epi32(alpha,alpha_shift);
              alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
              
              // Scale and clip the normalized alpha values
              alpha = _mm_madd_epi16(alpha,
                                     _mm256_castsi256_si128(neg_alpha_scale));
              alpha = _mm_srai_epi32(alpha,6); // Nom. alpha range = 0 to -2^15
              alpha = _mm_packs_epi32(alpha,alpha);// Saturate & pack 2 copies
              
              // Unsigned extend source and target samples to words
              src_val = _mm_cvtepu8_epi16(src_val);
              dst_val = _mm_cvtepu8_epi16(dst_val);
              
              // Copy the alpha factor into all word positions
              alpha = _mm_shufflelo_epi16(alpha,0);
              
              // Get difference between source and target values then scale and
              // add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              __m128i diff = _mm_sub_epi16(src_val,dst_val);
              diff = _mm_add_epi16(diff,diff); // Because |alpha| \in [0,2^15]
              diff = _mm_mulhi_epi16(diff,alpha);
              dst_val = _mm_sub_epi16(dst_val,diff); // Sub because alpha -ve
              
              // Finally, pack words into bytes and save the pixel
              dst_val = _mm_packus_epi16(dst_val,dst_val);
              *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
            }
        }
      for (c=octets; c > 0; c--, sp+=8, dp+=8)
        { // Process 8 pixels (32 samples) at a time, using 256-bit operands
          // Load 8 source pixels
          __m256i src_low, dst_low, src_high, dst_high, alpha_low, alpha_high;
          src_low = _mm256_loadu_si256((__m256i *) sp);
          dst_low = *((__m256i *) dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          alpha_low = _mm256_srli_epi32(src_low,24); // Get alpha only
          __m256i alpha_shift = _mm256_slli_epi32(alpha_low,7);
          src_low = _mm256_or_si256(src_low,mask); // Sets source alpha to 255
          src_low = _mm256_xor_si256(src_low,xor_mask);
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_shift = _mm256_slli_epi32(alpha_shift,8);
          alpha_low = _mm256_add_epi32(alpha_low,alpha_shift);
          alpha_low = _mm256_srli_epi32(alpha_low,9); // Leave max alpha = 2^14
          
          // Unpack source pixels to two vectors, with 16 bits per sample
          src_high = _mm256_unpackhi_epi8(src_low,zero);
          dst_high = _mm256_unpackhi_epi8(dst_low,zero);
          src_low  = _mm256_unpacklo_epi8(src_low,zero);
          dst_low  = _mm256_unpacklo_epi8(dst_low,zero);
          
          // Scale and clip the normalized alpha values
          alpha_low = _mm256_madd_epi16(alpha_low,neg_alpha_scale);
          alpha_low = _mm256_srai_epi32(alpha_low,6); // nom. range ->[-2^15,0]
          alpha_low = _mm256_packs_epi32(alpha_low,alpha_low);
             // Saturates and leaves 4 16-bit alpha values in the low qword
             // and a copy thereof in the high qword of each 128-bit lane
          
          // Rearrange the alpha values so that each pixel's 32-bit dword
          // holds two copies of its 16-bit alpha value, then unpack the
          // 32-bit pixels into two vectors, with alpha duplicated into every
          // 16-bit source sample.
          alpha_low = _mm256_unpacklo_epi16(alpha_low,alpha_low);
          alpha_high = _mm256_unpackhi_epi32(alpha_low,alpha_low);
          alpha_low = _mm256_unpacklo_epi32(alpha_low,alpha_low);
          
          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m256i diff_low = _mm256_sub_epi16(src_low,dst_low);
          __m256i diff_high = _mm256_sub_epi16(src_high,dst_high);
          diff_low = _mm256_add_epi16(diff_low,diff_low);    // Because |alpha|
          diff_high = _mm256_add_epi16(diff_high,diff_high); // \in [0,2^15]
          diff_low = _mm256_mulhi_epi16(diff_low,alpha_low);
          diff_high = _mm256_mulhi_epi16(diff_high,alpha_high);
          dst_low = _mm256_sub_epi16(dst_low,diff_low);    // Subtract because
          dst_high = _mm256_sub_epi16(dst_high,diff_high); // sc. alpha is -ve
          
          // Finally, pack `dst_low' and `dst_high' down into bytes and save
          *((__m256i *) dp) = _mm256_packus_epi16(dst_low,dst_high);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { // Process 1 pixel at a time using 128-bit operands
          // Load 1 source pixel and 1 target pixel
          __m128i src_val = _mm_cvtsi32_si128((kdu_int32) *sp);
          __m128i dst_val = _mm_cvtsi32_si128((kdu_int32) *dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          __m128i alpha = _mm_srli_epi32(src_val,24); // Get alpha only
          __m128i alpha_shift = _mm_slli_epi32(alpha,7);
          src_val = _mm_or_si128(src_val, // Set source alpha to 255
                                 _mm256_castsi256_si128(mask));              
          src_val = _mm_xor_si128(src_val,
                                  _mm256_castsi256_si128(xor_mask));
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha_shift = _mm_slli_epi32(alpha_shift,8);
          alpha = _mm_add_epi32(alpha,alpha_shift);
          alpha = _mm_srli_epi32(alpha,9); // Leave max alpha = 2^14.
          
          // Scale and clip the normalized alpha values
          alpha = _mm_madd_epi16(alpha,
                                 _mm256_castsi256_si128(neg_alpha_scale));
          alpha = _mm_srai_epi32(alpha,6); // Nom. alpha range = 0 to -2^15
          alpha = _mm_packs_epi32(alpha,alpha);// Saturate & pack 2 copies
          
          // Unsigned extend source and target samples to words
          src_val = _mm_cvtepu8_epi16(src_val);
          dst_val = _mm_cvtepu8_epi16(dst_val);
          
          // Copy the alpha factor into all word positions
          alpha = _mm_shufflelo_epi16(alpha,0);
          
          // Get difference between source and target values then scale and
          // add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          __m128i diff = _mm_sub_epi16(src_val,dst_val);
          diff = _mm_add_epi16(diff,diff); // Because |alpha| \in [0,2^15]
          diff = _mm_mulhi_epi16(diff,alpha);
          dst_val = _mm_sub_epi16(dst_val,diff); // Sub because alpha -ve
          
          // Finally, pack words into bytes and save the pixel
          dst_val = _mm_packus_epi16(dst_val,dst_val);
          *dp = (kdu_uint32) _mm_cvtsi128_si32(dst_val);
        }
    }
}

} // namespace kd_supp_simd

#endif // !KDU_NO_AVX2
