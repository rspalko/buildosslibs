/*****************************************************************************/
// File: neon_region_decompressor.cpp [scope = APPS/SUPPORT]
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
file take advantage of the ARM-NEON instruction set.  The functions
defined here may be selected at run-time via macros defined in
"neon_region_decompressor_local.h", depending on run-time CPU detection, as
well as build conditions.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>
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
/* EXTERN                neon_convert_and_copy_to_fix16                      */
/*****************************************************************************/

void
  neon_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
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
  int16x8_t vec_shift;
  int abs_upshift = KDU_FIX_POINT - src_precision;
  int abs_downshift = 0;
  kdu_int16 abs_offset = 0;
  vec_shift = vdupq_n_s16((kdu_int16)abs_upshift); // This is a signed shift
  if (abs_upshift < 0)
    { 
      abs_downshift = -abs_upshift;
      abs_upshift = 0;
      abs_offset = (kdu_int16)(1 << (abs_downshift-1));
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
          int lead=(-((_addr_to_kdu_int32(dp))>>1))&7; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          
          if (src_type == KDRD_FIX16_TYPE)
            { // Just copy source to dest
              for (; lead > 0; lead--, src++, dp++)
                *dp = *src;
              for (; src_len > 0; src_len-=8, src+=8, dp+=8)
                { int16x8_t val = vld1q_s16(src); vst1q_s16(dp,val); }
            }
          else
            { 
              for (; lead > 0; lead--, src++, dp++)
                *dp = (*src) << abs_upshift;
              for (; src_len > 0; src_len-=8, src+=8, dp+=8)
                { 
                  int16x8_t val = vld1q_s16(src);
                  val = vrshlq_s16(val,vec_shift); // SRA + round if shift -ve
                  vst1q_s16(dp,val);
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
/* EXTERN         neoni_reinterpret_and_copy_to_unsigned_floats              */
/*****************************************************************************/

void
  neoni_reinterpret_and_copy_to_unsigned_floats(const void *bufs[],
                                                const int widths[],
                                                const int types[],
                                                int num_lines,
                                                int precision,
                                                int missing_src_samples,
                                                void *void_dst, int dst_min,
                                                int num_samples, int dst_type,
                                                int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0) &&
         (precision <= 32) && (precision > exponent_bits) &&
         (exponent_bits <= 8) && ((precision-1-exponent_bits) <= 23));
  float *dst = ((float *) void_dst) + dst_min;

  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case; no need to be efficient at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }

  // Skip over source samples as required
  const kdu_int32 *src = (const kdu_int32 *)(*(bufs++));
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
          src = (const kdu_int32 *)(*(bufs++));
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

  // Prepare the conversion parameters
  int mantissa_bits = precision - 1 - exponent_bits;
  assert(mantissa_bits >= 0);
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  assert((mantissa_upshift >= 0) && // If these two conditions do not hold
         (exp_off <= 127)); // the accelerator should not have been installed.
  float denorm_scale = kdu_pwrof2f(127-exp_off); // For normalizing denormals
  int exp_max = 2*exp_off;

  int32x4_t vec_in_off = vdupq_n_s32(1<<(precision-1));
  int32x4_t vec_in_min = vdupq_n_s32(0);
  int32x4_t vec_in_max = vdupq_n_s32(((exp_max+1)<<mantissa_bits)-1);
  vec_in_min = vsubq_s32(vec_in_min,vec_in_off);
  vec_in_max = vsubq_s32(vec_in_max,vec_in_off);
  int32x4_t vec_upshift = vdupq_n_s32(mantissa_upshift);
  float32x4_t vec_out_scale = vdupq_n_f32(denorm_scale);
  float32x4_t vec_half = vdupq_n_f32(0.5f);

  // Now perform the sample conversion process
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      int32x4_t in_vec = vdupq_n_s32(src[0]);
      in_vec = vmaxq_s32(in_vec,vec_in_min);
      in_vec = vminq_s32(in_vec,vec_in_max);
      in_vec = vaddq_s32(in_vec,vec_in_off);
      in_vec = vshlq_s32(in_vec,vec_upshift);
      float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
      out_vec = vmulq_f32(out_vec,vec_out_scale);
      out_vec = vsubq_f32(out_vec,vec_half);
      float fval = vgetq_lane_f32(out_vec,0);      
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }

  while (num_samples > 0)
    { 
      if (src_len > 0)
        { // Else source type might be 0 (undefined)
          if (src_type != KDRD_INT32_TYPE)
            assert(0);
          float *dp = dst;
          if (src_len > num_samples)
            src_len = num_samples;
          dst += src_len;
          num_samples -= src_len;
          int lead=(-((_addr_to_kdu_int32(dp))>>2))&3; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          for (; lead > 0; lead--, src++, dp++)
            { // Do conversion vector by vector
              int32x4_t in_vec = vdupq_n_s32(src[0]);
              in_vec = vmaxq_s32(in_vec,vec_in_min);
              in_vec = vminq_s32(in_vec,vec_in_max);
              in_vec = vaddq_s32(in_vec,vec_in_off);
              in_vec = vshlq_s32(in_vec,vec_upshift);
              float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
              out_vec = vmulq_f32(out_vec,vec_out_scale);
              out_vec = vsubq_f32(out_vec,vec_half);
              dp[0] = vgetq_lane_f32(out_vec,0);      
            }
          for (; src_len > 0; src_len-=4, src+=4, dp+=4)
            { // Do vector conversion, 4 floats at a time
              int32x4_t in_vec = vld1q_s32(src);
              in_vec = vmaxq_s32(in_vec,vec_in_min);
              in_vec = vminq_s32(in_vec,vec_in_max);
              in_vec = vaddq_s32(in_vec,vec_in_off);
              in_vec = vshlq_s32(in_vec,vec_upshift);
              float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
              out_vec = vmulq_f32(out_vec,vec_out_scale);
              out_vec = vsubq_f32(out_vec,vec_half);
              vst1q_f32(dp,out_vec);
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src = (const kdu_int32 *)(*(bufs++));
      src_len=*(widths++); src_type=*(types++); num_lines--;
    }
  // Perform right edge padding as required
  for (float fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}

/*****************************************************************************/
/* EXTERN          neoni_reinterpret_and_copy_to_signed_floats               */
/*****************************************************************************/

void
  neoni_reinterpret_and_copy_to_signed_floats(const void *bufs[],
                                              const int widths[],
                                              const int types[],
                                              int num_lines,
                                              int precision,
                                              int missing_src_samples,
                                              void *void_dst, int dst_min,
                                              int num_samples, int dst_type,
                                              int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0) &&
         (precision <= 32) && (precision > exponent_bits) &&
         (exponent_bits <= 8) && ((precision-1-exponent_bits) <= 23));
  float *dst = ((float *) void_dst) + dst_min;

  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case; no need to be efficient at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }

  // Skip over source samples as required
  const kdu_int32 *src = (const kdu_int32 *)(*(bufs++));
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
          src = (const kdu_int32 *)(*(bufs++));
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

  // Prepare the conversion parameters
  int mantissa_bits = precision - 1 - exponent_bits;
  assert(mantissa_bits >= 0);
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  assert((mantissa_upshift >= 0) && // If these two conditions do not hold
         (exp_off <= 127)); // the accelerator should not have been installed.
  float denorm_scale = kdu_pwrof2f(127-exp_off); // For normalizing denormals
  int exp_max = 2*exp_off;

  int32x4_t vec_mag_max = vdupq_n_s32(((exp_max+1)<<mantissa_bits)-1);
  int32x4_t vec_sign_mask = vdupq_n_s32(KDU_INT32_MIN);
  int32x4_t vec_mag_mask = vdupq_n_s32(~(((kdu_int32)-1)<<(precision-1)));
  int32x4_t vec_upshift = vdupq_n_s32(mantissa_upshift);
  float32x4_t vec_out_scale = vdupq_n_f32(denorm_scale*0.5f);

  // Now perform the sample conversion process
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      int32x4_t in_vec = vdupq_n_s32(src[0]);
      int32x4_t sign_vec = vandq_s32(in_vec,vec_sign_mask);
      in_vec = vandq_s32(in_vec,vec_mag_mask);
      in_vec = vminq_s32(in_vec,vec_mag_max);
      in_vec = vshlq_s32(in_vec,vec_upshift);
      in_vec = vorrq_s32(in_vec,sign_vec);
      float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
      out_vec = vmulq_f32(out_vec,vec_out_scale);
      float fval = vgetq_lane_f32(out_vec,0);    
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }

  while (num_samples > 0)
    { 
      if (src_len > 0)
        { // Else source type might be 0 (undefined)
          if (src_type != KDRD_INT32_TYPE)
            assert(0);
          float *dp = dst;
          if (src_len > num_samples)
            src_len = num_samples;
          dst += src_len;
          num_samples -= src_len;
          int lead=(-((_addr_to_kdu_int32(dp))>>2))&3; // Non-aligned samples
          if ((src_len -= lead) < 0)
            lead += src_len;
          for (; lead > 0; lead--, src++, dp++)
            { // Do conversion vector by vector
              int32x4_t in_vec = vdupq_n_s32(src[0]);
              int32x4_t sign_vec = vandq_s32(in_vec,vec_sign_mask);
              in_vec = vandq_s32(in_vec,vec_mag_mask);
              in_vec = vminq_s32(in_vec,vec_mag_max);
              in_vec = vshlq_s32(in_vec,vec_upshift);
              in_vec = vorrq_s32(in_vec,sign_vec);
              float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
              out_vec = vmulq_f32(out_vec,vec_out_scale);
              dp[0] = vgetq_lane_f32(out_vec,0);    
            }
          for (; src_len > 0; src_len-=4, src+=4, dp+=4)
            { // Do vector conversion, 4 floats at a time
              int32x4_t in_vec = vld1q_s32(src);
              int32x4_t sign_vec = vandq_s32(in_vec,vec_sign_mask);
              in_vec = vandq_s32(in_vec,vec_mag_mask);
              in_vec = vminq_s32(in_vec,vec_mag_max);
              in_vec = vshlq_s32(in_vec,vec_upshift);
              in_vec = vorrq_s32(in_vec,sign_vec);
              float32x4_t out_vec = vreinterpretq_f32_s32(in_vec);
              out_vec = vmulq_f32(out_vec,vec_out_scale);
              vst1q_f32(dp,out_vec);
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src = (const kdu_int32 *)(*(bufs++));
      src_len=*(widths++); src_type=*(types++); num_lines--;
    }
  // Perform right edge padding as required
  for (float fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}
  
/*****************************************************************************/
/* EXTERN                      neon_white_stretch                            */
/*****************************************************************************/

void
  neon_white_stretch(const kdu_int16 *src, kdu_int16 *dst, int num_samples,
                     int stretch_residual)
{
  kdu_int32 stretch_offset = -((-(stretch_residual<<(KDU_FIX_POINT-1))) >> 16);
  if (stretch_residual <= 0x7FFF)
    { // Use full multiplication-based approach
      int16x8_t factor = vdupq_n_s16((kdu_int16)(stretch_residual>>1)); // half
      int16x8_t offset = vdupq_n_s16((kdu_int16) stretch_offset);
      for (; num_samples > 0; num_samples-=8, src+=8, dst+=8)
        { 
          int16x8_t val = vld1q_s16(src);
          int16x8_t residual = vqdmulhq_s16(val,factor); // doubling multiply
          val = vaddq_s16(val,offset);
          vst1q_s16(dst,vaddq_s16(val,residual));
        }
    }
  else
    { // Large stretch residual -- can only happen with 1-bit original data
      int diff=(1<<16)-((int) stretch_residual), downshift = 1;
      while ((diff & 0x8000) == 0)
        { diff <<= 1; downshift++; }
      int16x8_t neg_shift = vdupq_n_s16((kdu_int16)(-downshift));
      int16x8_t offset = vdupq_n_s16((kdu_int16)stretch_offset);
      for (; num_samples > 0; num_samples-=8, src+=8, dst+=8)
        { 
          int16x8_t val = vld1q_s16(src);
          int16x8_t shifted_val = vshlq_s16(val,neg_shift);
          int16x8_t twice_val = vaddq_s16(val,val);
          val = vsubq_s16(twice_val,shifted_val);
          vst1q_s16(dst,vaddq_s16(val,offset));
        }
    }
}

/*****************************************************************************/
/* EXTERN               neon_transfer_fix16_to_bytes_gap1                    */
/*****************************************************************************/

void
  neon_transfer_fix16_to_bytes_gap1(const void *src_buf, int src_p,
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
  int16x8_t voff = vdupq_n_s16(offset);
  int16x8_t vmax = vdupq_n_s16(~mask);
  int16x8_t vmin = vdupq_n_s16(0);
  int16x8_t neg_shift = vdupq_n_s16((kdu_int16)(-downshift));
  for (; num_samples >= 16; num_samples-=16, sp+=16, dp+=16)
    { // Generate whole output vectors of 16 byte values at a time
      int16x8_t low = vld1q_s16(sp);
      low = vaddq_s16(low,voff);
      low = vshlq_s16(low,neg_shift); // -ve left shift = SRA
      low = vmaxq_s16(low,vmin); // Clip to min value of 0
      low = vminq_s16(low,vmax); // Clip to max value of `~mask'
      int16x8_t high = vld1q_s16(sp+8);
      high = vaddq_s16(high,voff);
      high = vshlq_s16(high,neg_shift); // -ve left shift = SRA
      high = vmaxq_s16(high,vmin); // Clip to min value of 0
      high = vminq_s16(high,vmax); // Clip to max value of `~mask'
      uint8x16_t packed = vcombine_u8(vmovn_u16((uint16x8_t)low),
                                     vmovn_u16((uint16x8_t)high));
      vst1q_u8(dp,packed);
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
/* EXTERN               neon_transfer_fix16_to_bytes_gap4                    */
/*****************************************************************************/

void
  neon_transfer_fix16_to_bytes_gap4(const void *src_buf, int src_p,
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
  int16x8_t voff = vdupq_n_s16(offset);
  int16x8_t vmax = vdupq_n_s16(~mask);
  int16x8_t vmin = vdupq_n_s16(0);
  int16x8_t neg_shift = vdupq_n_s16((kdu_int16)(-downshift));
  if ((_addr_to_kdu_int32(dp) & 1) == 0)
    { // Modify the first byte of each word
      uint16x8_t sel_mask = vdupq_n_u16(0x00FF); // Replace 8 LSB's of word
      for (; num_samples >= 9; num_samples-=8, sp+=8, dp+=32)    
        { 
          int16x8_t val = vld1q_s16(sp);
          int16x8x2_t tgt = vld2q_s16((kdu_int16 *)dp);
          val = vaddq_s16(val,voff);
          val = vshlq_s16(val,neg_shift); // -ve left shift = SRA
          val = vmaxq_s16(val,vmin); // Clip to min value of 0
          val = vminq_s16(val,vmax); // Clip to max value of `~mask'
          tgt.val[0] = vbslq_s16(sel_mask,val,tgt.val[0]);
          vst2q_s16((kdu_int16 *)dp,tgt);
        }
    }
  else
    { // Modify the second byte of each word
      dp--;
      uint16x8_t sel_mask = vdupq_n_u16(0xFF00); // Replace 8 MSB's of word
      for (; num_samples >= 9; num_samples-=8, sp+=8, dp+=32)    
        { 
          int16x8_t val = vld1q_s16(sp);
          int16x8x2_t tgt = vld2q_s16((kdu_int16 *)dp);
          val = vaddq_s16(val,voff);
          val = vshlq_s16(val,neg_shift); // -ve left shift = SRA
          val = vmaxq_s16(val,vmin); // Clip to min value of 0
          val = vminq_s16(val,vmax); // Clip to max value of `~mask'
          val = vshlq_n_s16(val,8); // Move each source val to 8 MSB's of word
          tgt.val[0] = vbslq_s16(sel_mask,val,tgt.val[0]);
          vst2q_s16((kdu_int16 *)dp,tgt);
        }
      dp++;
    }
  for (; num_samples > 0; num_samples--, sp++, dp+=4)
    { 
      kdu_int16 val = (sp[0]+offset)>>downshift;
      if (val & mask)
        val = (val < 0)?0:~mask;
      dp[0] = (kdu_byte) val;
    }
}
  
/*****************************************************************************/
/* EXTERN          neon_interleaved_transfer_fix16_to_bytes                  */
/*****************************************************************************/

void
  neon_interleaved_transfer_fix16_to_bytes(const void *src0, const void *src1,
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
  int16x8_t voff = vdupq_n_s16(offset);
  int16x8_t vmax = vdupq_n_s16(~mask);
  int16x8_t vmin = vdupq_n_s16(0);
  int16x8_t neg_shift = vdupq_n_s16((kdu_int16)(-downshift));
  if (zmask == 0x00FFFFFF)
    { // Only channels 0, 1 and 2 are used; don't bother converting channel 3
      int16x8_t high_or_mask = vdupq_n_s16((kdu_int16)(fmask >> 16));
      for (; num_pixels >= 8; num_pixels-=8, sp0+=8, sp1+=8, sp2+=8, dp+=8)
        { // Generate whole output vectors of 8 x 32-bit pixels at a time
          int16x8x2_t vec;
          vec.val[0] = vld1q_s16(sp0);
          vec.val[0] = vaddq_s16(vec.val[0],voff);
          vec.val[0] = vshlq_s16(vec.val[0],neg_shift);
          vec.val[0] = vmaxq_s16(vec.val[0],vmin);
          vec.val[0] = vminq_s16(vec.val[0],vmax);
          int16x8_t tmp = vld1q_s16(sp1);
          tmp = vaddq_s16(tmp,voff);
          tmp = vshlq_s16(tmp,neg_shift);
          tmp = vmaxq_s16(tmp,vmin);
          tmp = vminq_s16(tmp,vmax);
          vec.val[0] = vsliq_n_s16(vec.val[0],tmp,8); // tmp -> 8 MSBs of word

          vec.val[1] = vld1q_s16(sp2);
          vec.val[1] = vaddq_s16(vec.val[1],voff);
          vec.val[1] = vshlq_s16(vec.val[1],neg_shift);
          vec.val[1] = vmaxq_s16(vec.val[1],vmin);
          vec.val[1] = vminq_s16(vec.val[1],vmax);
          vec.val[1] = vorrq_s16(vec.val[1],high_or_mask);
          
          vst2q_s16((kdu_int16 *)dp,vec);
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
      int16x8_t low_or_mask = vdupq_n_s16((kdu_int16) fmask);
      int16x8_t high_or_mask = vdupq_n_s16((kdu_int16)(fmask >> 16));
      int16x8_t low_and_mask = vdupq_n_s16((kdu_int16) zmask);
      int16x8_t high_and_mask = vdupq_n_s16((kdu_int16)(zmask >> 16));
      for (; num_pixels >= 8; num_pixels-=8,
           sp0+=8, sp1+=8, sp2+=8, sp3+=8, dp+=8)
        { // Generate whole output vectors of 8 x 32-bit pixels at a time
          int16x8x2_t vec;
          vec.val[0] = vld1q_s16(sp0);
          vec.val[0] = vaddq_s16(vec.val[0],voff);
          vec.val[0] = vshlq_s16(vec.val[0],neg_shift);
          vec.val[0] = vmaxq_s16(vec.val[0],vmin);
          vec.val[0] = vminq_s16(vec.val[0],vmax);
          int16x8_t low_tmp = vld1q_s16(sp1);
          low_tmp = vaddq_s16(low_tmp,voff);
          low_tmp = vshlq_s16(low_tmp,neg_shift);
          low_tmp = vmaxq_s16(low_tmp,vmin);
          low_tmp = vminq_s16(low_tmp,vmax);
          vec.val[0] = vsliq_n_s16(vec.val[0],low_tmp,8); // tmp -> 8 MSBs
          vec.val[0] = vandq_s16(vec.val[0],low_and_mask);
          vec.val[0] = vorrq_s16(vec.val[0],low_or_mask);

          vec.val[1] = vld1q_s16(sp2);
          vec.val[1] = vaddq_s16(vec.val[1],voff);
          vec.val[1] = vshlq_s16(vec.val[1],neg_shift);
          vec.val[1] = vmaxq_s16(vec.val[1],vmin);
          vec.val[1] = vminq_s16(vec.val[1],vmax);
          int16x8_t high_tmp = vld1q_s16(sp3);
          high_tmp = vaddq_s16(high_tmp,voff);
          high_tmp = vshlq_s16(high_tmp,neg_shift);
          high_tmp = vmaxq_s16(high_tmp,vmin);
          high_tmp = vminq_s16(high_tmp,vmax);
          vec.val[1] = vsliq_n_s16(vec.val[1],high_tmp,8); // tmp -> 8 MSBs
          vec.val[1] = vandq_s16(vec.val[1],high_and_mask);
          vec.val[1] = vorrq_s16(vec.val[1],high_or_mask);
          
          vst2q_s16((kdu_int16 *)dp,vec);
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
/* EXTERN                   neon_vert_resample_float                         */
/*****************************************************************************/

void
  neon_vert_resample_float(int length, float *src[], float *dst,
                           void *kernel, int kernel_length)
{
  if (kernel_length == 2)
    { 
      float *sp0=(float *)src[2];
      float *sp1=(float *)src[3];
      float *dp =(float *)dst;
      float32x4_t *kern = (float32x4_t *) kernel;
      float32x4_t k0=kern[0], k1=kern[1];
      for (int n=0; n < length; n+=4)
        { 
          float32x4_t v0=vld1q_f32(sp0+n), v1=vld1q_f32(sp1+n);
          v0 = vmulq_f32(v0,k0);  v0 = vmlaq_f32(v0,v1,k1);
          vst1q_f32(dp+n,v0);
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
      float32x4_t *kern = (float32x4_t *) kernel;
      float32x4_t k0=kern[0], k1=kern[1], k2=kern[2],
                  k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=4)
        { 
          float32x4_t v0=vld1q_f32(sp0+n), v1=vld1q_f32(sp1+n);
          v0 = vmulq_f32(v0,k0);           v1 = vmulq_f32(v1,k1);
          float32x4_t v2=vld1q_f32(sp2+n), v3=vld1q_f32(sp3+n);
          v0 = vmlaq_f32(v0,v2,k2);        v1 = vmlaq_f32(v1,v3,k3);
          float32x4_t v4=vld1q_f32(sp4+n), v5=vld1q_f32(sp5+n);
          v0 = vmlaq_f32(v0,v4,k4);        v1 = vmlaq_f32(v1,v5,k5);
          vst1q_f32(dp+n,vaddq_f32(v0,v1));
        }
    }
}
  
/*****************************************************************************/
/* EXTERN                   neon_vert_resample_fix16                         */
/*****************************************************************************/

void
  neon_vert_resample_fix16(int length, kdu_int16 *src[], kdu_int16 *dst,
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
              int16x8_t val = vld1q_s16(sp0+n);
              vst1q_s16(dp+n,val);
            }
        }
      else
        { 
          int16x8_t *kern = (int16x8_t *) kernel;
          int16x8_t k0=kern[0], k1=kern[1];
          for (int n=0; n < length; n+=8)
            { 
              int16x8_t v0=vld1q_s16(sp0+n), v1=vld1q_s16(sp1+n);
              v0 = vqrdmulhq_s16(v0,k0);  v1 = vqrdmulhq_s16(v1,k1);
              v0 = vnegq_s16(v0);         v0 = vsubq_s16(v0,v1);
              vst1q_s16(dp+n,v0);
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
      int16x8_t *kern = (int16x8_t *) kernel;
      int16x8_t k0=kern[0], k1=kern[1], k2=kern[2],
                k3=kern[3], k4=kern[4], k5=kern[5];
      for (int n=0; n < length; n+=8)
        { 
          int16x8_t v0=vld1q_s16(sp0+n), v1=vld1q_s16(sp1+n);
          v0 = vqrdmulhq_s16(v0,k0);     v1 = vqrdmulhq_s16(v1,k1);
          v0 = vnegq_s16(v0);            v0 = vsubq_s16(v0,v1);
          int16x8_t v2=vld1q_s16(sp2+n), v3=vld1q_s16(sp3+n);
          v2 = vqrdmulhq_s16(v2,k2);     v3 = vqrdmulhq_s16(v3,k3);
          v0 = vsubq_s16(v0,v2);         v0 = vsubq_s16(v0,v3);
          int16x8_t v4=vld1q_s16(sp4+n), v5=vld1q_s16(sp5+n);
          v4 = vqrdmulhq_s16(v4,k4);     v5 = vqrdmulhq_s16(v5,k5);
          v0 = vsubq_s16(v0,v4);         v0 = vsubq_s16(v0,v5);
          vst1q_s16(dp+n,v0);
        }
    }
}
  

/* ========================================================================= */
/*                       Horizontal Resampling Functions                     */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    neon_horz_resample_float                        */
/*****************************************************************************/
  
void
  neon_horz_resample_float(int length, float *src, float *dp,
                           kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                           int pshift, void **kernels, int kernel_length,
                           int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x4 = ((kdu_int64) num) << 2; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x4/den); // Minimum value of adj=[(phase+num_x4)/den]
                                   // required to advance to the next vector.
  assert(min_adj < 12); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x4 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  float *sp_base = src;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 4 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 4));
      // The above conditions should have been checked during func ptr init
      for (; length > 0; length-=4, dp+=4)
        { 
          float32x4_t *kern = (float32x4_t *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          float32x4_t input = vld1q_f32(sp_base);
          float32x4_t fact0=kern[0], fact1=kern[1], fact2=kern[2];
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          fact0 = vmulq_lane_f32(fact0,vget_low_f32(input),0);
          fact0 = vmlaq_lane_f32(fact0,fact1,vget_low_f32(input),1);
          fact0 = vmlaq_lane_f32(fact0,fact2,vget_high_f32(input),0);
          if (kernel_length > 3)
            { 
              float32x4_t fact3=kern[3];
              fact0 = vmlaq_lane_f32(fact0,fact3,vget_high_f32(input),1);
            }
          vst1q_f32(dp,fact0);
        }
    }
  else
    { 
      sp_base -= leadin;
      for (; length > 0; length-=4, dp+=4)
        { 
          float32x4_t *kern = (float32x4_t *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          float *sp = sp_base; // Note; this is not aligned
          float32x4_t input0, input1, val1, val2, val3;
          float32x4_t fact0, fact1, fact2, fact3;
          input0 = vld1q_f32(sp);  sp += 4;    fact0 = kern[0];
          input1 = vld1q_f32(sp);  sp += 4;
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          float32x4_t sum=vmulq_f32(input0,fact0);
          int kl;
          for (kl=kernel_length; kl > 4; kl-=4, kern+=4)
            { 
              fact1=kern[1];  fact2=kern[2];  fact3=kern[3];  fact0=kern[4];
              val1 = vextq_f32(input0,input1,1);
              sum = vmlaq_f32(sum,val1,fact1);
              val2 = vextq_f32(input0,input1,2);
              sum = vmlaq_f32(sum,val2,fact2);
              val3 = vextq_f32(input0,input1,3);
              sum = vmlaq_f32(sum,val3,fact3);
              input0 = input1;  input1 = vld1q_f32(sp);  sp += 4;
              sum = vmlaq_f32(sum,input0,fact0);
            }
          if (kl == 1)
            { vst1q_f32(dp,sum); continue; }
          fact1 = kern[1];  val1 = vextq_f32(input0,input1,1);
          sum = vmlaq_f32(sum,val1,fact1);
          if (kl == 2)
            { vst1q_f32(dp,sum); continue; }
          fact2 = kern[2];  val2 = vextq_f32(input0,input1,2);
          sum = vmlaq_f32(sum,val2,fact2);
          if (kl == 3)
            { vst1q_f32(dp,sum); continue; }
          fact3 = kern[3];  val3 = vextq_f32(input0,input1,3);
          sum = vmlaq_f32(sum,val3,fact3);
          vst1q_f32(dp,sum);
        }
    }
}
  
/*****************************************************************************/
/* EXTERN                   neon_horz_resample_fix16                         */
/*****************************************************************************/

void
  neon_horz_resample_fix16(int length, kdu_int16 *src, kdu_int16 *dp,
                           kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                           int pshift, void **kernels, int kernel_length,
                           int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to adavnce to the next vector.
  assert(min_adj < 24); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  kdu_int16 *sp_base = src;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 8 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 6));
        // The above conditions should have been checked during func ptr init
      for (; length > 0; length-=8, dp += 8)
        { 
          int16x8_t *kern = (int16x8_t *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          int16x8_t input = vld1q_s16(sp_base);
          int16x8_t fact0=kern[0], fact1=kern[1], fact2=kern[2];
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          int16x8_t val = vqdmulhq_lane_s16(fact0,vget_low_s16(input),0);
          int16x8_t sum = vnegq_s16(val);
          val = vqdmulhq_lane_s16(fact1,vget_low_s16(input),1);
          sum = vsubq_s16(sum,val);
          val = vqdmulhq_lane_s16(fact2,vget_low_s16(input),2);
          sum = vsubq_s16(sum,val);
          if (kernel_length > 3)
            { 
              fact0 = kern[3];
              val = vqdmulhq_lane_s16(fact0,vget_low_s16(input),3);
              sum = vsubq_s16(sum,val);
              if (kernel_length > 4)
                { 
                  fact1 = kern[4];
                  val = vqdmulhq_lane_s16(fact1,vget_high_s16(input),0);
                  sum = vsubq_s16(sum,val);
                  if (kernel_length > 5)
                    { 
                      fact2 = kern[5];
                      val = vqdmulhq_lane_s16(fact2,vget_high_s16(input),1);
                      sum = vsubq_s16(sum,val);
                    }
                }
            }
          vst1q_s16(dp,sum);
        }
    }
  else
    { 
      assert(kernel_length >= 6);
      sp_base -= leadin; 
      for (; length > 0; length-=8, dp += 8)
        { 
          int kl = kernel_length;
          int16x8_t *kern = (int16x8_t *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          kdu_int16 *sp = sp_base;
          int16x8_t input0=vld1q_s16(sp); sp += 8;
          int16x8_t input1=vld1q_s16(sp); sp += 8;
          int16x8_t k0, k1, k2, k3, v0, v1, v2, v3, sum;
          k0 = kern[0];  k1 = kern[1];      k2 = kern[2];  k3 = kern[3];
          v0 = vqdmulhq_s16(input0,k0);
          v1 = vextq_s16(input0,input1,1);  v1 = vqdmulhq_s16(v1,k1);
          v2 = vextq_s16(input0,input1,2);  v2 = vqdmulhq_s16(v2,k2);
          v3 = vextq_s16(input0,input1,3);  v3 = vqdmulhq_s16(v3,k3);
          sum = vnegq_s16(v0);              sum = vsubq_s16(sum,v1);
          sum = vsubq_s16(sum,v2);          sum = vsubq_s16(sum,v3);
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }

          k0 = kern[4];  k1 = kern[5];      k2 = kern[6];  k3 = kern[7];
          v0 = vextq_s16(input0,input1,4);  v0 = vqdmulhq_s16(v0,k0);
          v1 = vextq_s16(input0,input1,5);  v1 = vqdmulhq_s16(v1,k1);
          v2 = vextq_s16(input0,input1,6);  v2 = vqdmulhq_s16(v2,k2);
          v3 = vextq_s16(input0,input1,7);  v3 = vqdmulhq_s16(v3,k3);
          sum = vsubq_s16(sum,v0);          sum = vsubq_s16(sum,v1);
          if (kl <= 8)
            { // Very common case where kernel length is 7 or 8
              if (kl >= 7)
                { 
                  sum = vsubq_s16(sum,v2);
                  if (kl == 8)
                    sum = vsubq_s16(sum,v3);
                }
              vst1q_s16(dp,sum); continue;
            }
          sum = vsubq_s16(sum,v2);          sum = vsubq_s16(sum,v3);
          input0 = input1;  input1 = vld1q_s16(sp);  sp += 8;
          kl-=8; kern+=8;
          for (; kl > 8; kl-=8, kern+=8)
            { 
              k0 = kern[0];  k1 = kern[1];  k2 = kern[2];  k3 = kern[3];
              v0 = vqdmulhq_s16(input0,k0);
              v1 = vextq_s16(input0,input1,1);  v1 = vqdmulhq_s16(v1,k1);
              v2 = vextq_s16(input0,input1,2);  v2 = vqdmulhq_s16(v2,k2);
              v3 = vextq_s16(input0,input1,3);  v3 = vqdmulhq_s16(v3,k3);
              sum = vsubq_s16(sum,v0);          sum = vsubq_s16(sum,v1);
              sum = vsubq_s16(sum,v2);          sum = vsubq_s16(sum,v3);
              k0 = kern[4];  k1 = kern[5];      k2 = kern[6];  k3 = kern[7];
              v0 = vextq_s16(input0,input1,4);  v0 = vqdmulhq_s16(v0,k0);
              v1 = vextq_s16(input0,input1,5);  v1 = vqdmulhq_s16(v1,k1);
              v2 = vextq_s16(input0,input1,6);  v2 = vqdmulhq_s16(v2,k2);
              v3 = vextq_s16(input0,input1,7);  v3 = vqdmulhq_s16(v3,k3);
              sum = vsubq_s16(sum,v0);          sum = vsubq_s16(sum,v1);
              sum = vsubq_s16(sum,v2);          sum = vsubq_s16(sum,v3);
              input0 = input1;  input1 = vld1q_s16(sp);  sp += 8;
            }
          
          // If we get here, we have between 1 and 8 more kernel taps to go,
          // so kl ranges from 1 to 8
          k0 = kern[0];  v0 = vqdmulhq_s16(input0,k0);
          sum = vsubq_s16(sum,v0);
          if (kl == 1)
            { vst1q_s16(dp,sum); continue; }
          k1 = kern[1];  v1 = vextq_s16(input0,input1,1);
          v1 = vqdmulhq_s16(v1,k1);             sum = vsubq_s16(sum,v1);
          if (kl == 2)
            { vst1q_s16(dp,sum); continue; }
          k2 = kern[2];  v1 = vextq_s16(input0,input1,2);
          v2 = vqdmulhq_s16(v2,k2);             sum = vsubq_s16(sum,v2);
          if (kl == 3)
            { vst1q_s16(dp,sum); continue; }
          k3 = kern[3];  v3 = vextq_s16(input0,input1,3);
          v3 = vqdmulhq_s16(v3,k3);             sum = vsubq_s16(sum,v3);
          if (kl == 4)
            { vst1q_s16(dp,sum); continue; }
          k0 = kern[4];  v0 = vextq_s16(input0,input1,4);
          v0 = vqdmulhq_s16(v0,k0);             sum = vsubq_s16(sum,v0);
          if (kl == 5)
            { vst1q_s16(dp,sum); continue; }
          k1 = kern[1];  v1 = vextq_s16(input0,input1,5);
          v1 = vqdmulhq_s16(v1,k1);             sum = vsubq_s16(sum,v1);
          if (kl == 6)
            { vst1q_s16(dp,sum); continue; }
          k2 = kern[2];  v1 = vextq_s16(input0,input1,6);
          v2 = vqdmulhq_s16(v2,k2);             sum = vsubq_s16(sum,v2);
          if (kl == 7)
            { vst1q_s16(dp,sum); continue; }
          k3 = kern[3];  v3 = vextq_s16(input0,input1,7);
          v3 = vqdmulhq_s16(v3,k3);             sum = vsubq_s16(sum,v3);
          vst1q_s16(dp,sum);
        }
    }
}
   
} // namespace kd_supp_simd

#endif // !KDU_NO_NEON
