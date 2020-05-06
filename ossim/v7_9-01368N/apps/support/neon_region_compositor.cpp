/*****************************************************************************/
// File: neon_region_compositor.cpp [scope = APPS/SUPPORT]
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
blending operations, based on the ARM NEON instruction set.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>
#include <assert.h>

namespace kd_supp_simd {
  using namespace kdu_core;

  
/* ========================================================================= */
/*                         Erase and Copy Functions                          */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                       neon_erase_region                            */
/*****************************************************************************/

void
  neon_erase_region(kdu_uint32 *dst, int height, int width, int row_gap,
                    kdu_uint32 erase)
{
  uint32x4_t val = vdupq_n_u32(erase);
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
      for (c=octets; c > 0; c--)
        { vst1q_u32(dp,val); dp+=4;  vst1q_u32(dp,val); dp+=4; }
      for (c=right; c > 0; c--)
        *(dp++) = erase;
    }
}

/*****************************************************************************/
/* EXTERN                    neon_erase_region_float                         */
/*****************************************************************************/

void
  neon_erase_region_float(float *dst, int height, int width, int row_gap,
                          float erase[])
{
  float32x4_t val = vld1q_f32(erase);
  for (; height > 0; height--, dst+=row_gap)
    { 
      float *dp=dst;
      for (int c=width; c > 0; c--, dp+=4)
        vst1q_f32(dp,val);
    }
}

/*****************************************************************************/
/* EXTERN                        neon_copy_region                            */
/*****************************************************************************/

void
  neon_copy_region(kdu_uint32 *dst, kdu_uint32 *src, int height, int width,
                   int dst_row_gap, int src_row_gap)
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
      for (c=octets; c > 0; c--)
        { 
          uint32x4_t val0 = vld1q_u32(sp);  sp += 4;
          uint32x4_t val1 = vld1q_u32(sp);  sp += 4;
          vst1q_u32(dp,val0); dp += 4;
          vst1q_u32(dp,val1); dp += 4;
        }
      for (c=right; c > 0; c--)
        *(dp++) = *(sp++);
    }
}

/*****************************************************************************/
/* EXTERN                     neon_copy_region_float                         */
/*****************************************************************************/

void
  neon_copy_region_float(float *dst, float *src, int height, int width,
                         int dst_row_gap, int src_row_gap)
{
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      float *dp=dst, *sp=src;
      for (int c=width; c > 0; c--, dp+=4, sp+=4)
        { 
          float32x4_t val = vld1q_f32(sp);
          vst1q_f32(dp,val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                       neon_rcopy_region                            */
/*****************************************************************************/

void
  neon_rcopy_region(kdu_uint32 *dst, kdu_uint32 *src, int height, int width,
                    int row_gap)
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
          uint32x4_t val0, val1;
          sp -= 4; val0 = vld1q_u32(sp);
          sp -= 4; val1 = vld1q_u32(sp);
          dp -= 4; vst1q_u32(dp,val0);
          dp -= 4; vst1q_u32(dp,val1);
        }
      for (c=left; c > 0; c--)
        *(--dp) = *(--sp);
    }
}

/*****************************************************************************/
/* EXTERN                    neon_rcopy_region_float                         */
/*****************************************************************************/

void
  neon_rcopy_region_float(float *dst, float *src, int height, int width,
                          int row_gap)
{
  for (; height > 0; height--, dst-=row_gap, src-=row_gap)
    { 
      float *dp=dst, *sp=src;
      for (int c=width; c > 0; c--)
        { 
          float32x4_t val;
          sp -= 4;  val = vld1q_f32(sp);
          dp -= 4;  vst1q_f32(dp,val);
        }
    }
}

  
/* ========================================================================= */
/*                               Blend Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                       neon_blend_region                            */
/*****************************************************************************/

void
  neon_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int dst_row_gap, int src_row_gap)
{
  // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this in computing the adjusted value of the
  // background alpha after blending -- correct blending modifies both alpha
  // and colour channels.
  uint32x4_t mask = vdupq_n_u32(0xFF000000);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp=src, *dp=dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              uint32x2_t src_val = vld1_dup_u32(sp);
              uint32x2_t dst_val = vld1_dup_u32(dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              uint32x2_t alpha = vshr_n_u32(src_val,24); // Gets alpha only
              uint32x2_t alpha_shift = vshl_n_u32(alpha,7);
              src_val = vorr_u32(src_val,vget_low_u32(mask));
              alpha = vadd_u32(alpha,alpha_shift);
              alpha_shift = vshl_n_u32(alpha_shift,8);
              alpha = vadd_u32(alpha,alpha_shift);
              alpha = vshr_n_u32(alpha,9);
              
              // Unpack source and target samples to words by zero extension
              uint16x8_t src_16 = vmovl_u8((uint8x8_t)src_val);
              uint16x8_t dst_16 = vmovl_u8((uint8x8_t)dst_val);
                            
              // Get difference between source and target values, then scale
              // and add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              int16x8_t diff = vsubq_s16((int16x8_t)src_16,(int16x8_t)dst_16);
              diff = vaddq_s16(diff,diff); // Equivalent to doubling alpha
              diff = vqdmulhq_lane_s16(diff,(int16x4_t)alpha,0);
              dst_16 = vaddq_u16(dst_16,(uint16x8_t)diff);
              
              // Finally, pack words into bytes and save the pixel
              dst_val=(uint32x2_t)vqmovn_u16(dst_16); // NB: -ve dst impossible
              vst1_lane_u32(dp,dst_val,0);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        { 
          // Load 4 source pixels and 4 target pixels
          uint32x4_t src_val = vld1q_u32(sp);
          uint32x4_t dst_val = vld1q_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14, inclusive,
          // replacing the original alpha value by 255 in `src_val'
          uint32x4_t alpha = vshrq_n_u32(src_val,24); // Gets alpha only
          uint32x4_t alpha_shift = vshlq_n_u32(alpha,7);
          src_val = vorrq_u32(src_val,mask); // Leaves alpha=255 in `src_val'
          alpha = vaddq_u32(alpha,alpha_shift);
          alpha_shift = vshlq_n_u32(alpha_shift,8);
          alpha = vaddq_u32(alpha,alpha_shift);
          alpha = vshrq_n_u32(alpha,9); // Leaves max alpha = 2^14
          
          // Unpack source and target pixels to words by zero extension
          uint16x8_t src_low = vmovl_u8(vget_low_u8((uint8x16_t)src_val));
          uint16x8_t src_high = vmovl_u8(vget_high_u8((uint8x16_t)src_val));
          uint16x8_t dst_low = vmovl_u8(vget_low_u8((uint8x16_t)dst_val));
          uint16x8_t dst_high = vmovl_u8(vget_high_u8((uint8x16_t)dst_val));
          
          // Unpack and arrange alpha factors so that all four word positions
          // of each pixel contain copies of the pixel's scaled source alpha.
          alpha = vsliq_n_u32(alpha,alpha,16); // Leaves 2 alpha words per pel
          int16x8x2_t factors=vzipq_s16((int16x8_t)alpha,(int16x8_t)alpha);
          
          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that alpha
          // has already been replaced by 255 in the source, which is correct.
          int16x8_t diff0 = vsubq_s16((int16x8_t)src_low,(int16x8_t)dst_low);
          int16x8_t diff1 = vsubq_s16((int16x8_t)src_high,(int16x8_t)dst_high);
          diff0 = vaddq_s16(diff0,diff0); // Pre-doubling diff is equivalent to
          diff1 = vaddq_s16(diff1,diff1); // working with alpha in [0,2^15]
          diff0 = vqdmulhq_s16(diff0,factors.val[0]); // Doubling multiply with
          diff1 = vqdmulhq_s16(diff1,factors.val[1]); // high part equiv x2^-15
          dst_low = vaddq_u16(dst_low,(uint16x8_t)diff0);
          dst_high = vaddq_u16(dst_high,(uint16x8_t)diff1);
          
          // Finally, pack `dst_low' and `dst_high' into bytes and save
          dst_val = (uint32x4_t) // Note: narrowing saturates signed words
            vcombine_u8(vqmovn_u16(dst_low),vqmovn_u16(dst_high));
          vst1q_u32(dp,dst_val);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { 
          uint32x2_t src_val = vld1_dup_u32(sp);
          uint32x2_t dst_val = vld1_dup_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          uint32x2_t alpha = vshr_n_u32(src_val,24); // Gets alpha only
          uint32x2_t alpha_shift = vshl_n_u32(alpha,7);
          src_val = vorr_u32(src_val,vget_low_u32(mask));
          alpha = vadd_u32(alpha,alpha_shift);
          alpha_shift = vshl_n_u32(alpha_shift,8);
          alpha = vadd_u32(alpha,alpha_shift);
          alpha = vshr_n_u32(alpha,9);
          
          // Unpack source and target samples to words by zero extension
          uint16x8_t src_16 = vmovl_u8((uint8x8_t)src_val);
          uint16x8_t dst_16 = vmovl_u8((uint8x8_t)dst_val);
          
          // Get difference between source and target values, then scale
          // and add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          int16x8_t diff = vsubq_s16((int16x8_t)src_16,(int16x8_t)dst_16);
          diff = vaddq_s16(diff,diff); // Equivalent to doubling alpha
          diff = vqdmulhq_lane_s16(diff,(int16x4_t)alpha,0);
          dst_16 = vaddq_u16(dst_16,(uint16x8_t)diff);
          
          // Finally, pack words into bytes and save the pixel
          dst_val=(uint32x2_t)vqmovn_u16(dst_16); // NB: -ve dst impossible
          vst1_lane_u32(dp,dst_val,0);
        }      
    }
}

/*****************************************************************************/
/* EXTERN                    neon_blend_region_float                         */
/*****************************************************************************/

void
  neon_blend_region_float(float *dst, float *src, int height, int width,
                          int dst_row_gap, int src_row_gap)
{
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      float *sp=src, *dp=dst;
      for (int c=width; c > 0; c--, sp+=4, dp+=4)
        { 
          float32x4_t src_val = vld1q_f32(sp);
          float32x4_t dst_val = vld1q_f32(dp);
          float32x4_t alpha = vdupq_lane_f32(vget_high_f32(src_val),1);
          src_val = vsetq_lane_f32(1.0f,src_val,3); // Replaces alpha with 1.0f
          float32x4_t diff = vsubq_f32(src_val,dst_val);
          dst_val = vmlaq_f32(dst_val,diff,alpha);
          vst1q_f32(dp,dst_val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                   neon_premult_blend_region                        */
/*****************************************************************************/

void
  neon_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src, int height,
                            int width, int dst_row_gap, int src_row_gap)
{
  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp=src, *dp=dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              uint32x2_t src_val = vld1_dup_u32(sp);
              uint32x2_t dst_val = vld1_dup_u32(dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive
              uint32x2_t alpha = vshr_n_u32(src_val,24); // Gets alpha only
              uint32x2_t alpha_shift = vshl_n_u32(alpha,7);
              alpha = vadd_u32(alpha,alpha_shift);
              alpha_shift = vshl_n_u32(alpha_shift,8);
              alpha = vadd_u32(alpha,alpha_shift);
              alpha = vshr_n_u32(alpha,9);
              
              // Unpack source and target samples to words by zero extension
              int16x8_t src_16 = (int16x8_t)vmovl_u8((uint8x8_t)src_val);
              int16x8_t dst_16 = (int16x8_t)vmovl_u8((uint8x8_t)dst_val);
              
              // Add source and target pixels, then subtract the alpha-scaled
              // target pixel.
              src_16 = vaddq_s16(src_16,dst_16);
              dst_16 = vaddq_s16(dst_16,dst_16); // Equiv. to doubling alpha
              dst_16 = vqdmulhq_lane_s16(dst_16,(int16x4_t)alpha,0);
              src_16 = vsubq_s16(src_16,dst_16);
                           
              // Finally, pack words into bytes and save the pixel.  Note that
              // narrowing must be done on unsigned quantities since the
              // output must be 8-bit unsigned bytes.
              dst_val=(uint32x2_t)vqmovn_u16((uint16x8_t)src_16);
              vst1_lane_u32(dp,dst_val,0);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        { 
          // Load 4 source pixels and 4 target pixels
          uint32x4_t src_val = vld1q_u32(sp);
          uint32x4_t dst_val = vld1q_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14, inclusive
          uint32x4_t alpha = vshrq_n_u32(src_val,24); // Gets alpha only
          uint32x4_t alpha_shift = vshlq_n_u32(alpha,7);
          alpha = vaddq_u32(alpha,alpha_shift);
          alpha_shift = vshlq_n_u32(alpha_shift,8);
          alpha = vaddq_u32(alpha,alpha_shift);
          alpha = vshrq_n_u32(alpha,9); // Leaves max alpha = 2^14
          
          // Unpack source and target pixels to words by zero extension.  Note
          // that expansion must be done as unsigned quantities, to avoid sign
          // extension, but it is convenient to treat the results as signed.
          int16x8_t src_low = (int16x8_t)
            vmovl_u8(vget_low_u8((uint8x16_t)src_val));
          int16x8_t src_high = (int16x8_t)
            vmovl_u8(vget_high_u8((uint8x16_t)src_val));
          int16x8_t dst_low = (int16x8_t)
            vmovl_u8(vget_low_u8((uint8x16_t)dst_val));
          int16x8_t dst_high = (int16x8_t)
            vmovl_u8(vget_high_u8((uint8x16_t)dst_val));
          
          // Unpack and arrange alpha factors so that all four word positions
          // of each pixel contain copies of the pixel's scaled source alpha.
          alpha = vsliq_n_u32(alpha,alpha,16); // Leaves 2 alpha words per pel
          int16x8x2_t factors=vzipq_s16((int16x8_t)alpha,(int16x8_t)alpha);
          
          // Add source and target pixels and then subtract the alpha-scaled
          // target pixels.
          src_low = vaddq_s16(src_low,dst_low);
          src_high = vaddq_s16(src_high,dst_high);
          dst_low = vaddq_s16(dst_low,dst_low);    // This is equivalent to
          dst_high = vaddq_s16(dst_high,dst_high); // doubling alpha
          dst_low = vqdmulhq_s16(dst_low,factors.val[0]);
          dst_high = vqdmulhq_s16(dst_high,factors.val[1]);
          src_low = vsubq_s16(src_low,dst_low);
          src_high = vsubq_s16(src_high,dst_high);
          
          // Finally, pack `src_low' and `src_high' into bytes and save
          dst_val = (uint32x4_t) // Note: narrowing saturates signed words
            vcombine_u8(vqmovn_u16(src_low),vqmovn_u16(src_high));
          vst1q_u32(dp,dst_val);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { 
          uint32x2_t src_val = vld1_dup_u32(sp);
          uint32x2_t dst_val = vld1_dup_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive
          uint32x2_t alpha = vshr_n_u32(src_val,24); // Gets alpha only
          uint32x2_t alpha_shift = vshl_n_u32(alpha,7);
          alpha = vadd_u32(alpha,alpha_shift);
          alpha_shift = vshl_n_u32(alpha_shift,8);
          alpha = vadd_u32(alpha,alpha_shift);
          alpha = vshr_n_u32(alpha,9);
          
          // Unpack source and target samples to words by zero extension
          int16x8_t src_16 = (int16x8_t)vmovl_u8((uint8x8_t)src_val);
          int16x8_t dst_16 = (int16x8_t)vmovl_u8((uint8x8_t)dst_val);
          
          // Add source and target pixels, then subtract the alpha-scaled
          // target pixel.
          src_16 = vaddq_s16(src_16,dst_16);
          dst_16 = vaddq_s16(dst_16,dst_16); // Equiv. to doubling alpha
          dst_16 = vqdmulhq_lane_s16(dst_16,(int16x4_t)alpha,0);
          src_16 = vsubq_s16(src_16,dst_16);
          
          // Finally, pack words into bytes and save the pixel.  Note that
          // narrowing must be done on unsigned quantities since the
          // output must be 8-bit unsigned bytes.
          dst_val=(uint32x2_t)vqmovn_u16((uint16x8_t)src_16);
          vst1_lane_u32(dp,dst_val,0);
        }
    }  
}

/*****************************************************************************/
/* EXTERN                neon_premult_blend_region_float                     */
/*****************************************************************************/

void
  neon_premult_blend_region_float(float *dst, float *src, int height,
                                  int width, int dst_row_gap, int src_row_gap)
{
  float32x4_t one_val = vdupq_n_f32(1.0f);
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      float *sp=src, *dp=dst;
      for (int c=width; c > 0; c--, sp+=4, dp+=4)
        { 
          float32x4_t src_val = vld1q_f32(sp);
          float32x4_t dst_val = vld1q_f32(dp);
          float32x4_t alpha = vdupq_lane_f32(vget_high_f32(src_val),1);
          src_val = vaddq_f32(src_val,dst_val);
          src_val = vmlsq_f32(src_val,dst_val,alpha);
          src_val = vminq_f32(src_val,one_val); // Clip to 1.0 after pre-blend
          vst1q_f32(dp,src_val);
        }
    }
}

/*****************************************************************************/
/* EXTERN                   neon_scaled_blend_region                         */
/*****************************************************************************/

void
  neon_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, kdu_int16 alpha_factor_x128)
{
    // Create a mask containing 0xFF in the alpha byte position of each
  // original pixel.  We will use this in computing the adjusted value of the
  // background alpha after blending -- correct blending modifies both alpha
  // and colour channels.
  uint32x4_t mask = vdupq_n_u32(0xFF000000);
  
  // Create an XOR mask to handle negative alpha factors
  uint32x4_t xor_mask = vdupq_n_u32(0);
  if (alpha_factor_x128 < 0)
    { 
      alpha_factor_x128 = -alpha_factor_x128;
      xor_mask = vdupq_n_u32(0x00FFFFFF);
    }
  
  // Create a scaled alpha clipping limit that prevents values smaller than
  // -2^15.
  int32x4_t alpha_min = vdupq_n_s32(-(1<<15));
  
  // Create 4 copies of -`alpha_factor_x128' in a 128-bit vector
  int32x4_t neg_alpha_scale = vdupq_n_s32(-alpha_factor_x128);

  // Now for the processing loop
  for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
    { 
      kdu_uint32 *sp=src, *dp=dst;
      int left = (-(_addr_to_kdu_int32(dp) >> 2)) & 3;
      int c, quads = (width-left)>>2;
      int right = width - left - (quads<<2);
      if (left)
        { 
          for (c=(left<width)?left:width; c > 0; c--, sp++, dp++)
            { 
              uint32x2_t src_val = vld1_dup_u32(sp);
              uint32x2_t dst_val = vld1_dup_u32(dp);
              
              // Find normalized alpha factor in the range 0 to 2^14 inclusive,
              // replacing the original alpha value by 255 in `src_val'
              int32x2_t alpha = (int32x2_t)vshr_n_u32(src_val,24);
              int32x2_t alpha_shift = vshl_n_s32(alpha,7);
              src_val = vorr_u32(src_val,vget_low_u32(mask));
              src_val = veor_u32(src_val,vget_low_u32(xor_mask));
              alpha = vadd_s32(alpha,alpha_shift);
              alpha_shift = vshl_n_s32(alpha_shift,8);
              alpha = vadd_s32(alpha,alpha_shift);
              alpha = vshr_n_s32(alpha,9);
              
              // Scale and clip the normalized alpha values
              alpha = vmul_s32(alpha,vget_low_s32(neg_alpha_scale));
              alpha = vshr_n_s32(alpha,6); // Nom. alpha range = 0 to -2^15
              alpha = vmax_s32(alpha,vget_low_s32(alpha_min));            
              
              // Unpack source and target samples to words by zero extension
              uint16x8_t src_16 = vmovl_u8((uint8x8_t)src_val);
              uint16x8_t dst_16 = vmovl_u8((uint8x8_t)dst_val);
                            
              // Get difference between source and target values, then scale
              // and add this difference back into the target value; note that
              // alpha has already been replaced by 255 in the source.
              int16x8_t diff = vsubq_s16((int16x8_t)src_16,(int16x8_t)dst_16);
              diff = vqdmulhq_lane_s16(diff,(int16x4_t)alpha,0);
              dst_16 = vsubq_u16(dst_16,(uint16x8_t)diff);
              
              // Finally, pack words into bytes and save the pixel
              dst_val=(uint32x2_t)vqmovn_u16(dst_16); // NB: -ve dst impossible
              vst1_lane_u32(dp,dst_val,0);
            }
        }
      for (c=quads; c > 0; c--, sp+=4, dp+=4)
        { 
          // Load 4 source pixels and 4 target pixels
          uint32x4_t src_val = vld1q_u32(sp);
          uint32x4_t dst_val = vld1q_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14, inclusive,
          // replacing the original alpha value by 255 in `src_val'
          int32x4_t alpha = (int32x4_t)vshrq_n_u32(src_val,24);
          int32x4_t alpha_shift = vshlq_n_s32(alpha,7);
          src_val = vorrq_u32(src_val,mask); // Leaves alpha=255 in `src_val'
          src_val = veorq_u32(src_val,xor_mask); // May flip colours
          alpha = vaddq_s32(alpha,alpha_shift);
          alpha_shift = vshlq_n_s32(alpha_shift,8);
          alpha = vaddq_s32(alpha,alpha_shift);
          alpha = vshrq_n_s32(alpha,9); // Leaves max alpha = 2^14
          
          // Scale and clip the normalized alpha values
          alpha = vmulq_s32(alpha,neg_alpha_scale);
          alpha = vshrq_n_s32(alpha,6); // Nom. alpha range = 0 to -2^15
          alpha = vmaxq_s32(alpha,alpha_min);            
          
          // Unpack source and target pixels to words by zero extension
          uint16x8_t src_low = vmovl_u8(vget_low_u8((uint8x16_t)src_val));
          uint16x8_t src_high = vmovl_u8(vget_high_u8((uint8x16_t)src_val));
          uint16x8_t dst_low = vmovl_u8(vget_low_u8((uint8x16_t)dst_val));
          uint16x8_t dst_high = vmovl_u8(vget_high_u8((uint8x16_t)dst_val));
          
          // Unpack and arrange alpha factors so that all four word positions
          // of each pixel contain copies of the pixel's scaled source alpha.
          alpha = vsliq_n_u32(alpha,alpha,16); // Leaves 2 alpha words per pel
          int16x8x2_t factors=vzipq_s16((int16x8_t)alpha,(int16x8_t)alpha);
          
          // Get difference between source and target values, then scale and
          // add this difference back into the target value; note that alpha
          // has already been replaced by 255 in the source, which is correct.
          int16x8_t diff0 = vsubq_s16((int16x8_t)src_low,(int16x8_t)dst_low);
          int16x8_t diff1 = vsubq_s16((int16x8_t)src_high,(int16x8_t)dst_high);
          diff0 = vqdmulhq_s16(diff0,factors.val[0]); // Doubling multiply with
          diff1 = vqdmulhq_s16(diff1,factors.val[1]); // high part equiv x2^-15
          dst_low = vsubq_u16(dst_low,(uint16x8_t)diff0);
          dst_high = vsubq_u16(dst_high,(uint16x8_t)diff1);
          
          // Finally, pack `dst_low' and `dst_high' into bytes and save
          dst_val = (uint32x4_t) // Note: narrowing saturates signed words
            vcombine_u8(vqmovn_u16(dst_low),vqmovn_u16(dst_high));
          vst1q_u32(dp,dst_val);
        }
      for (c=right; c > 0; c--, sp++, dp++)
        { 
          uint32x2_t src_val = vld1_dup_u32(sp);
          uint32x2_t dst_val = vld1_dup_u32(dp);
          
          // Find normalized alpha factor in the range 0 to 2^14 inclusive,
          // replacing the original alpha value by 255 in `src_val'
          int32x2_t alpha = (int32x2_t)vshr_n_u32(src_val,24);
          int32x2_t alpha_shift = vshl_n_s32(alpha,7);
          src_val = vorr_u32(src_val,vget_low_u32(mask));
          src_val = veor_u32(src_val,vget_low_u32(xor_mask));
          alpha = vadd_s32(alpha,alpha_shift);
          alpha_shift = vshl_n_s32(alpha_shift,8);
          alpha = vadd_s32(alpha,alpha_shift);
          alpha = vshr_n_s32(alpha,9);
          
          // Scale and clip the normalized alpha values
          alpha = vmul_s32(alpha,vget_low_s32(neg_alpha_scale));
          alpha = vshr_n_s32(alpha,6); // Nom. alpha range = 0 to -2^15
          alpha = vmax_s32(alpha,vget_low_s32(alpha_min));            
          
          // Unpack source and target samples to words by zero extension
          uint16x8_t src_16 = vmovl_u8((uint8x8_t)src_val);
          uint16x8_t dst_16 = vmovl_u8((uint8x8_t)dst_val);
          
          // Get difference between source and target values, then scale
          // and add this difference back into the target value; note that
          // alpha has already been replaced by 255 in the source.
          int16x8_t diff = vsubq_s16((int16x8_t)src_16,(int16x8_t)dst_16);
          diff = vqdmulhq_lane_s16(diff,(int16x4_t)alpha,0);
          dst_16 = vsubq_u16(dst_16,(uint16x8_t)diff);
          
          // Finally, pack words into bytes and save the pixel
          dst_val=(uint32x2_t)vqmovn_u16(dst_16); // NB: -ve dst impossible
          vst1_lane_u32(dp,dst_val,0);
        }      
    }
}

/*****************************************************************************/
/* EXTERN                neon_scaled_blend_region_float                      */
/*****************************************************************************/

void
  neon_scaled_blend_region_float(float *dst, float *src,
                                 int height, int width, int dst_row_gap,
                                 int src_row_gap, float alpha_factor)
{
  float32x4_t one_val = vdupq_n_f32(1.0f);
  float32x4_t zero_val = vdupq_n_f32(0.0f);
  if (alpha_factor >= 0.0f)
    { 
      float32x4_t alpha_fact = vdupq_n_f32(alpha_factor);
      for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
        { 
          float *sp=src, *dp=dst;
          for (int c=width; c > 0; c--, sp+=4, dp+=4)
            { 
              float32x4_t src_val = vld1q_f32(sp);
              float32x4_t dst_val = vld1q_f32(dp);
              float32x4_t alpha = vdupq_lane_f32(vget_high_f32(src_val),1);
              alpha = vmulq_f32(alpha,alpha_fact);
              src_val = vsetq_lane_f32(1.0f,src_val,3); // Replace alpha with 1
              float32x4_t diff = vsubq_f32(src_val,dst_val);
              dst_val = vmlaq_f32(dst_val,diff,alpha);
              dst_val = vminq_f32(dst_val,one_val);
              dst_val = vmaxq_f32(dst_val,zero_val);
              vst1q_f32(dp,dst_val);
            }
        }
    }
  else
    { // Use -`alpha_factor' with inverted colour channels
      float32x4_t alpha_fact = vdupq_n_f32(-alpha_factor);
      for (; height > 0; height--, dst+=dst_row_gap, src+=src_row_gap)
        { 
          float *sp=src, *dp=dst;
          for (int c=width; c > 0; c--, sp+=4, dp+=4)
            { 
              float32x4_t src_val = vld1q_f32(sp);
              float32x4_t dst_val = vld1q_f32(dp);
              float32x4_t alpha = vdupq_lane_f32(vget_high_f32(src_val),1);
              alpha = vmulq_f32(alpha,alpha_fact);
              src_val = vsetq_lane_f32(0.0f,src_val,3); // Zero alpha position
                        // so that 1-src_val will hold 1 in the alpha channel
              // Now form diff = (1-src_val) - dst_val = 1 - (src_val+dst_val)
              float32x4_t neg_diff = vsubq_f32(vaddq_f32(src_val,dst_val),
                                               one_val);
              dst_val = vmlsq_f32(dst_val,neg_diff,alpha);
              dst_val = vminq_f32(dst_val,one_val);
              dst_val = vmaxq_f32(dst_val,zero_val);
              vst1q_f32(dp,dst_val);
            }
        }
    }
}
  
} // namespace kd_supp_simd

#endif // !KDU_NO_NEON
