/*****************************************************************************/
// File: neon_coder_local.cpp [scope = CORESYS/CODING]
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
source file is required to implement ARM-NEON versions of the data transfer
functions.  There is no harm in including this source file with all builds,
even if NEON is not supported by the compiler, so long as KDU_NO_NEON is
defined or KDU_NEON_INTRINSICS is not defined.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>
#include <assert.h>

using namespace kdu_core;

namespace kd_core_simd {
  
static kdu_byte local_mask_src128[32] =
  { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
  
/*===========================================================================*/
/*                  NEON Dequantization/Conversion Functions                 */
/*===========================================================================*/
  
/*****************************************************************************/
/* EXTERN                neoni_xfer_rev_decoded_block16                      */
/*****************************************************************************/

void
  neoni_xfer_rev_decoded_block16(kdu_int32 *src, void **dst_refs,
                                 int dst_offset_in, int dst_width,
                                 int src_stride, int height,
                                 int K_max, float delta_unused)
{
  // NOTE: Stores (but not loads) here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  src = (kdu_int32 *)(((kdu_byte *)src) - 2*align_bytes);
  nxt_dst -= align_bytes;  dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;

  // Prefetch 2 source code-block rows and 2 output code-block rows -- might
  // miss something if not aligned but it is not all that important.
  kdu_int32 *sp = src;
  kdu_int16 *pdp=(kdu_int16 *)nxt_dst; // pdp=prefetch destination pointer
  kdu_int16 *pdp1=(kdu_int16 *)(((kdu_byte *)(dst_refs[1]))+dst_offset_bytes);
  if (height < 2) pdp1 = pdp; // Avoid using invalid address
  for (n=dst_span_bytes; n > 16; n-=32, sp+=16, pdp+=16, pdp1+=16)
    { 
      KD_ARM_PREFETCH(sp);             KD_ARM_PREFETCH(sp+8);
      KD_ARM_PREFETCH(sp+src_stride);  KD_ARM_PREFETCH(sp+src_stride+8);
      KD_ARM_PREFETCH(pdp);            KD_ARM_PREFETCH(pdp1);
    }
  if (n > 0)
    { 
      KD_ARM_PREFETCH(sp);     KD_ARM_PREFETCH(sp+src_stride);
      KD_ARM_PREFETCH(pdp);    KD_ARM_PREFETCH(pdp1);
    }

  // Prepare processing machinery
  int16x8_t shift = vdupq_n_s16(K_max-15); // Neon uses -ve left-shift
  int16x8_t smask = vdupq_n_s16(0x8000); // Mask for sign-bit
  smask = vshlq_s16(smask,shift); // Extends sign bit mask
  int32x4_t in1, in2, in3, in4;
  int16x8_t v1, v2, s1, s2;

  // Process all but the last 2 code-block rows
  for (; height > 2; height--, dst_refs++, src+=src_stride)
    { 
      kdu_int16 *dp = (kdu_int16 *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      pdp = (kdu_int16 *)(((kdu_byte *)(dst_refs[2]))+dst_offset_bytes);
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=16, pdp+=16, dp+=16)
        { // Generate 2 output vectors at a time
          in1 = vld1q_s32(sp);              in2 = vld1q_s32(sp+4);
          in3 = vld1q_s32(sp+8);            in4 = vld1q_s32(sp+12);
          v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
          v2 = vcombine_s16(vshrn_n_s32(in3,16),vshrn_n_s32(in4,16));
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(sp+2*src_stride+8);
          KD_ARM_PREFETCH(pdp);
          v1 = vshlq_s16(v1,shift);         v2 = vshlq_s16(v2,shift);
          s1 = vandq_s16(v1,smask);         v1 = vabsq_s16(v1);
          s2 = vandq_s16(v2,smask);         v2 = vabsq_s16(v2);
          v1 = vaddq_s16(v1,s1);            v2 = vaddq_s16(v2,s2);
          vst1q_s16(dp,v1);                 vst1q_s16(dp+8,v2);
        }
      if (n > 0)
        { // Process one final output vector
          in1 = vld1q_s32(sp);              in2 = vld1q_s32(sp+4);
          v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
          v1 = vshlq_s16(v1,shift);
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(pdp);
          s1 = vandq_s16(v1,smask);         v1 = vabsq_s16(v1);
          v1 = vaddq_s16(v1,s1);            vst1q_s16(dp,v1);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, dst_refs++, src+=src_stride)
    { 
      kdu_int16 *dp = (kdu_int16 *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=16, dp+=16)
        { // Generate 2 output vectors at a time
          in1 = vld1q_s32(sp);              in2 = vld1q_s32(sp+4);
          in3 = vld1q_s32(sp+8);            in4 = vld1q_s32(sp+12);
          v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
          v2 = vcombine_s16(vshrn_n_s32(in3,16),vshrn_n_s32(in4,16));
          v1 = vshlq_s16(v1,shift);         v2 = vshlq_s16(v2,shift);
          s1 = vandq_s16(v1,smask);         v1 = vabsq_s16(v1);
          s2 = vandq_s16(v2,smask);         v2 = vabsq_s16(v2);
          v1 = vaddq_s16(v1,s1);            v2 = vaddq_s16(v2,s2);
          vst1q_s16(dp,v1);                 vst1q_s16(dp+8,v2);
        }
      if (n > 0)
        { // Process one final output vector
          in1 = vld1q_s32(sp);              in2 = vld1q_s32(sp+4);
          v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
          v1 = vshlq_s16(v1,shift);
          s1 = vandq_s16(v1,smask);         v1 = vabsq_s16(v1);
          v1 = vaddq_s16(v1,s1);            vst1q_s16(dp,v1);
        }
    }
}

/*****************************************************************************/
/* EXTERN                neoni_xfer_rev_decoded_block32                      */
/*****************************************************************************/

void
  neoni_xfer_rev_decoded_block32(kdu_int32 *src, void **dst_refs,
                                 int dst_offset_in, int dst_width,
                                 int src_stride, int height,
                                 int K_max, float delta_unused)
{
  // NOTE: Stores (but not loads) here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  int dst_offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  src = (kdu_int32 *)(((kdu_byte *)src) - align_bytes);
  nxt_dst -= align_bytes;  dst_offset_bytes -= align_bytes;  
  int dst_span_bytes = 4*dst_width + align_bytes;
  
  // Prefetch 2 source code-block rows and 2 output code-block rows
  kdu_int32 *sp = src;
  kdu_int32 *pdp=(kdu_int32 *)nxt_dst; // pdp=prefetch destination pointer
  kdu_int32 *pdp1=(kdu_int32 *)(((kdu_byte *)(dst_refs[1]))+dst_offset_bytes);
  if (height < 2) pdp1 = pdp; // Avoid using invalid address
  for (n=dst_span_bytes; n > 32; n-= 64, sp+=16, pdp+=16, pdp1+=16)
    { 
      KD_ARM_PREFETCH(sp);             KD_ARM_PREFETCH(sp+8);
      KD_ARM_PREFETCH(sp+src_stride);  KD_ARM_PREFETCH(sp+src_stride+8);
      KD_ARM_PREFETCH(pdp);            KD_ARM_PREFETCH(pdp+8);
      KD_ARM_PREFETCH(pdp1);           KD_ARM_PREFETCH(pdp1+8);
    }
  if (n > 0)
    { 
      KD_ARM_PREFETCH(sp);     KD_ARM_PREFETCH(sp+src_stride);
      KD_ARM_PREFETCH(pdp);    KD_ARM_PREFETCH(pdp1);
    }

  // Prepare processing machinery
  int32x4_t shift = vdupq_n_s32(K_max-31); // Neon uses -ve left-shift
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  smask = vshlq_s32(smask,shift); // Extends sign bit mask
  int32x4_t v1, v2, s1, s2;

  // Process all but the last 2 code-block rows
  for (; height > 2; height--, dst_refs++, src+=src_stride)
    { 
      kdu_int32 *dp = (kdu_int32 *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      pdp = (kdu_int32 *)(((kdu_byte *)(dst_refs[2]))+dst_offset_bytes);
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=8, pdp+=8, dp+=8)
        { // Generate 2 output vectors at a time, with < 1 vector overwrite
          v1 = vshlq_s32(vld1q_s32(sp),shift);
          v2 = vshlq_s32(vld1q_s32(sp+4),shift);
          s1 = vandq_s32(v1,smask);          v1 = vabsq_s32(v1);
          s2 = vandq_s32(v2,smask);          v2 = vabsq_s32(v2);
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(pdp);
          v1 = vaddq_s32(v1,s1);             vst1q_s32(dp,v1);
          v2 = vaddq_s32(v2,s2);             vst1q_s32(dp+4,v2);
        }
      if (n > 0)
        { 
          v1 = vshlq_s32(vld1q_s32(sp),shift);
          s1 = vandq_s32(v1,smask);          v1 = vabsq_s32(v1);
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(pdp);
          v1 = vaddq_s32(v1,s1);             vst1q_s32(dp,v1);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, dst_refs++, src+=src_stride)
    { 
      kdu_int32 *dp = (kdu_int32 *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=8, dp+=8)
        { // Generate 2 output vectors at a time
          v1 = vshlq_s32(vld1q_s32(sp),shift);
          v2 = vshlq_s32(vld1q_s32(sp+4),shift);
          s1 = vandq_s32(v1,smask);          v1 = vabsq_s32(v1);
          s2 = vandq_s32(v2,smask);          v2 = vabsq_s32(v2);
          v1 = vaddq_s32(v1,s1);             vst1q_s32(dp,v1);
          v2 = vaddq_s32(v2,s2);             vst1q_s32(dp+4,v2);
        }
      if (n > 0)
        { 
          v1 = vshlq_s32(vld1q_s32(sp),shift);
          s1 = vandq_s32(v1,smask);          v1 = vabsq_s32(v1);
          v1 = vaddq_s32(v1,s1);             vst1q_s32(dp,v1);
        }
    }
}

/*****************************************************************************/
/* EXTERN               neoni_xfer_irrev_decoded_block16                     */
/*****************************************************************************/

void
  neoni_xfer_irrev_decoded_block16(kdu_int32 *src, void **dst_refs,
                                   int dst_offset_in, int dst_width,
                                   int src_stride, int height,
                                   int K_max, float delta)
{
  // NOTE: Stores (but not loads) here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  int dst_offset_bytes = 2*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 15;
  src = (kdu_int32 *)(((kdu_byte *)src) - 2*align_bytes);
  nxt_dst -= align_bytes;  dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 2*dst_width + align_bytes;
  
  // Prefetch 2 source code-block rows and 2 output code-block rows -- might
  // miss something if not aligned but it is not all that important.
  kdu_int32 *sp = src;
  kdu_int16 *pdp=(kdu_int16 *)nxt_dst; // pdp=prefetch destination pointer
  kdu_int16 *pdp1=(kdu_int16 *)(((kdu_byte *)(dst_refs[1]))+dst_offset_bytes);
  if (height < 2) pdp1 = pdp; // Avoid using invalid address
  for (n=dst_span_bytes; n > 16; n-=32, sp+=16, pdp+=16, pdp1+=16)
    { 
      KD_ARM_PREFETCH(sp);             KD_ARM_PREFETCH(sp+8);
      KD_ARM_PREFETCH(sp+src_stride);  KD_ARM_PREFETCH(sp+src_stride+8);
      KD_ARM_PREFETCH(pdp);            KD_ARM_PREFETCH(pdp1);
    }
  if (n > 0)
    { 
      KD_ARM_PREFETCH(sp);     KD_ARM_PREFETCH(sp+src_stride);
      KD_ARM_PREFETCH(pdp);    KD_ARM_PREFETCH(pdp1);
    }
  
  // Prepare processing machinery
  float fscale = delta * (float)(1<<KDU_FIX_POINT);
  if (K_max <= 31)
    fscale /= (float)(1<<(31-K_max));
  else
    fscale *= (float)(1<<(K_max-31));
  fscale *= ((float)(1<<16)) * ((float)(1<<15));
  int iscale = (int)(fscale + 0.5F);

  if (iscale < (1<<15))
    { // Common (almost certain) case in which we can use 16-bit multiplies
      int16_t iscale16 = (kdu_int16) iscale;
      int16x8_t smask = vdupq_n_s16(0x8000); // Mask for sign bit
      int32x4_t in1, in2, in3, in4;
      int16x8_t v1, v2, s1, s2;

      // Process all but the last 2 code-block rows
      for (; height > 2; height--, dst_refs++, src+=src_stride)
        { 
          kdu_int16 *dp = (kdu_int16 *)nxt_dst;
          nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
          pdp = (kdu_int16 *)(((kdu_byte *)(dst_refs[2]))+dst_offset_bytes);
          for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=16, pdp+=16,dp+=16)
            { // Generate 2 output vectors at a time
              in1 = vld1q_s32(sp);               in2 = vld1q_s32(sp+4);
              in3 = vld1q_s32(sp+8);             in4 = vld1q_s32(sp+12);
              v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
              v2 = vcombine_s16(vshrn_n_s32(in3,16),vshrn_n_s32(in4,16));
              s1 = vandq_s16(v1,smask);          v1 = vabsq_s16(v1);
              s2 = vandq_s16(v2,smask);          v2 = vabsq_s16(v2);
              v1 = vaddq_s16(v1,s1);             v2 = vaddq_s16(v2,s2);
              v1 = vqrdmulhq_n_s16(v1,iscale16); // Multiply, with 15-bit
              v2 = vqrdmulhq_n_s16(v2,iscale16); // right shift and rounding
              KD_ARM_PREFETCH(sp+2*src_stride);
              KD_ARM_PREFETCH(sp+2*src_stride+8);
              KD_ARM_PREFETCH(pdp);
              vst1q_s16(dp,v1);                  vst1q_s16(dp+8,v2);
            }
          if (n > 0)
            { // Process one final output vector
              KD_ARM_PREFETCH(sp+2*src_stride);
              KD_ARM_PREFETCH(pdp);
              in1 = vld1q_s32(sp);               in2 = vld1q_s32(sp+4);
              v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
              s1 = vandq_s16(v1,smask);          v1 = vabsq_s16(v1);
              v1 = vaddq_s16(v1,s1);
              v1 = vqrdmulhq_n_s16(v1,iscale16); vst1q_s16(dp,v1);
            }
        }

      // Process the last 2 code-block rows without prefetch
      for (; height > 0; height--, dst_refs++, src+=src_stride)
        { 
          kdu_int16 *dp = (kdu_int16 *)nxt_dst;
          nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
          for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=16, dp+=16)
            { // Generate 2 output vectors at a time
              in1 = vld1q_s32(sp);               in2 = vld1q_s32(sp+4);
              in3 = vld1q_s32(sp+8);             in4 = vld1q_s32(sp+12);
              v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
              v2 = vcombine_s16(vshrn_n_s32(in3,16),vshrn_n_s32(in4,16));
              s1 = vandq_s16(v1,smask);          v1 = vabsq_s16(v1);
              s2 = vandq_s16(v2,smask);          v2 = vabsq_s16(v2);
              v1 = vaddq_s16(v1,s1);             v2 = vaddq_s16(v2,s2);
              v1 = vqrdmulhq_n_s16(v1,iscale16); // Multiply, with 15-bit
              v2 = vqrdmulhq_n_s16(v2,iscale16); // right shift and rounding
              vst1q_s16(dp,v1);                  vst1q_s16(dp+8,v2);
            }
          if (n > 0)
            { // Process one final output vector
              in1 = vld1q_s32(sp);               in2 = vld1q_s32(sp+4);
              v1 = vcombine_s16(vshrn_n_s32(in1,16),vshrn_n_s32(in2,16));
              s1 = vandq_s16(v1,smask);          v1 = vabsq_s16(v1);
              v1 = vaddq_s16(v1,s1);
              v1 = vqrdmulhq_n_s16(v1,iscale16); vst1q_s16(dp,v1);
            }
        }
    }
  else
    { // Use 32-bit multiplication
      int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
      int32x4_t v1, v2, s1, s2;
      int16x8_t out;
      
      // Process all but the last 2 code-block rows
      for (; height > 2; height--, dst_refs++, src+=src_stride)
        { 
          kdu_int16 *dp = (kdu_int16 *)nxt_dst;
          nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
          pdp = (kdu_int16 *)(((kdu_byte *)(dst_refs[2]))+dst_offset_bytes);
          for (sp=src, n=dst_span_bytes; n > 0; n-=16, sp+=8, pdp+=8, dp+=8)
            { // Generate output vectors one at a time
              v1 = vld1q_s32(sp);              v2 = vld1q_s32(sp+4);
              s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
              s2 = vandq_s32(v2,smask);        v2 = vabsq_s32(v2);
              v1 = vaddq_s32(v1,s1);           v2 = vaddq_s32(v2,s2);
              v1 = vqrdmulhq_n_s32(v1,iscale); // Multiply, with 31-bit right
              v2 = vqrdmulhq_n_s32(v2,iscale); // shift and rounding
              KD_ARM_PREFETCH(sp+2*src_stride);
              KD_ARM_PREFETCH(pdp);
              out = vcombine_s16(vqmovn_s32(v1), vqmovn_s32(v2));
              vst1q_s16(dp,out);
            }
        }

      // Process the last 2 code-block rows without prefetch
      for (; height > 0; height--, dst_refs++, src+=src_stride)
        { 
          kdu_int16 *dp = (kdu_int16 *)nxt_dst;
          nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
          for (sp=src, n=dst_span_bytes; n > 0; n-=16, sp+=8, dp+=8)
            { // Generate output vectors one at a time
              v1 = vld1q_s32(sp);              v2 = vld1q_s32(sp+4);
              s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
              s2 = vandq_s32(v2,smask);        v2 = vabsq_s32(v2);
              v1 = vaddq_s32(v1,s1);           v2 = vaddq_s32(v2,s2);
              v1 = vqrdmulhq_n_s32(v1,iscale); // Multiply, with 31-bit right
              v2 = vqrdmulhq_n_s32(v2,iscale); // shift and rounding
              out = vcombine_s16(vqmovn_s32(v1), vqmovn_s32(v2));
              vst1q_s16(dp,out);
            }
        }      
    }
}

/*****************************************************************************/
/* EXTERN               neoni_xfer_irrev_decoded_block32                     */
/*****************************************************************************/

void
  neoni_xfer_irrev_decoded_block32(kdu_int32 *src, void **dst_refs,
                                   int dst_offset_in, int dst_width,
                                   int src_stride, int height,
                                   int K_max, float delta)
{
  // NOTE: Stores (but not loads) here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  int dst_offset_bytes = 4*dst_offset_in;
  kdu_byte *nxt_dst=((kdu_byte *)(dst_refs[0])) + dst_offset_bytes;
  int n, align_bytes = _addr_to_kdu_int32(nxt_dst) & 31;
  src = (kdu_int32 *)(((kdu_byte *)src) - align_bytes);
  nxt_dst -= align_bytes;  dst_offset_bytes -= align_bytes;
  int dst_span_bytes = 4*dst_width + align_bytes;
  
  // Prefetch 2 source code-block rows and 2 output code-block row
  kdu_int32 *sp = src;
  float *pdp=(float *)nxt_dst; // pdp=prefetch destination pointer
  float *pdp1=(float *)(((kdu_byte *)(dst_refs[1]))+dst_offset_bytes);
  if (height < 2) pdp1 = pdp; // Avoid using invalid address
  for (n=dst_span_bytes; n > 32; n-= 64, sp+=16, pdp+=16, pdp1+=16)
    { 
      KD_ARM_PREFETCH(sp);             KD_ARM_PREFETCH(sp+8);
      KD_ARM_PREFETCH(sp+src_stride);  KD_ARM_PREFETCH(sp+src_stride+8);
      KD_ARM_PREFETCH(pdp);            KD_ARM_PREFETCH(pdp+8);
      KD_ARM_PREFETCH(pdp1);           KD_ARM_PREFETCH(pdp1+8);
    }
  if (n > 0)
    { 
      KD_ARM_PREFETCH(sp);     KD_ARM_PREFETCH(sp+src_stride);
      KD_ARM_PREFETCH(pdp);    KD_ARM_PREFETCH(pdp1);
    }
  
  // Prepare processing machinery
  float fscale = delta;
  if (K_max <= 31)
    fscale /= (float)(1<<(31-K_max));
  else
    fscale *= (float)(1<<(K_max-31));
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  float32x4_t vec_scale = vdupq_n_f32(fscale);
  int32x4_t v1, v2, s1, s2;
  float32x4_t fv1, fv2;

  // Process all but the last 2 code-block rows
  for (; height > 2; height--, dst_refs++, src+=src_stride)
    { 
      float *dp = (float *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      pdp = (float *)(((kdu_byte *)(dst_refs[2]))+dst_offset_bytes);
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=8, pdp+=8, dp+=8)
        { // Generate 2 output vectors at a time
          v1 = vld1q_s32(sp);              v2 = vld1q_s32(sp+4);
          s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
          s2 = vandq_s32(v2,smask);        v2 = vabsq_s32(v2);
          v1 = vaddq_s32(v1,s1);           v2 = vaddq_s32(v2,s2);
          fv1 = vcvtq_f32_s32(v1);         fv2 = vcvtq_f32_s32(v2);
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(pdp);
          fv1 = vmulq_f32(fv1,vec_scale);  vst1q_f32(dp,fv1);
          fv2 = vmulq_f32(fv2,vec_scale);  vst1q_f32(dp+4,fv2);
        }
      if (n > 0)
        { 
          v1 = vld1q_s32(sp);
          s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
          v1 = vaddq_s32(v1,s1);           fv1 = vcvtq_f32_s32(v1);
          KD_ARM_PREFETCH(sp+2*src_stride);
          KD_ARM_PREFETCH(pdp);
          fv1 = vmulq_f32(fv1,vec_scale);  vst1q_f32(dp,fv1);          
        }
    }

  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, dst_refs++, src+=src_stride)
    { 
      float *dp = (float *)nxt_dst;
      nxt_dst = ((kdu_byte *)dst_refs[1])+dst_offset_bytes;
      for (sp=src, n=dst_span_bytes; n > 16; n-=32, sp+=8, dp+=8)
        { // Generate 2 output vectors at a time
          v1 = vld1q_s32(sp);              v2 = vld1q_s32(sp+4);
          s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
          s2 = vandq_s32(v2,smask);        v2 = vabsq_s32(v2);
          v1 = vaddq_s32(v1,s1);           v2 = vaddq_s32(v2,s2);
          fv1 = vcvtq_f32_s32(v1);         fv2 = vcvtq_f32_s32(v2);
          fv1 = vmulq_f32(fv1,vec_scale);  vst1q_f32(dp,fv1);
          fv2 = vmulq_f32(fv2,vec_scale);  vst1q_f32(dp+4,fv2);
        }
      if (n > 0)
        { 
          v1 = vld1q_s32(sp);
          s1 = vandq_s32(v1,smask);        v1 = vabsq_s32(v1);
          v1 = vaddq_s32(v1,s1);           fv1 = vcvtq_f32_s32(v1);
          fv1 = vmulq_f32(fv1,vec_scale);  vst1q_f32(dp,fv1);          
        }
    }
}


/*===========================================================================*/
/*                   NEON Quantization/Conversion Functions                  */
/*===========================================================================*/

/*****************************************************************************/
/* EXTERN                 neoni_quantize32_rev_block16                       */
/*****************************************************************************/

kdu_int32
  neoni_quantize32_rev_block16(kdu_int32 *dst, void **src_refs,
                               int src_offset, int src_width,
                               int dst_stride, int height,
                               int K_max, float delta_unused)
{
  int c;
  int16x8_t end_mask = vld1q_s16(((kdu_int16 *)local_mask_src128) +
                                 ((-src_width) & 7));
  
  kdu_int16 *nxt_src=((kdu_int16 *)src_refs[0]) + src_offset;
  
  // Prefetch 2 source code-block rows and 2 output code-block row
  kdu_int32 *dp = dst;
  kdu_int16 *psp=nxt_src; // pdsp = prefetch source pointer
  kdu_int16 *psp1=((kdu_int16 *)src_refs[1]) + src_offset;
  if (height < 2) psp1 = psp; // Avoid using invalid address
  for (c=src_width; c > 8; c-=16, dp+=16, psp+=16, psp1+=16)
    { 
      KD_ARM_PREFETCH(psp);            KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);             KD_ARM_PREFETCH(dp+8);
      KD_ARM_PREFETCH(dp+dst_stride);  KD_ARM_PREFETCH(dp+dst_stride+8);
    }
  if (c > 0)
    { 
      KD_ARM_PREFETCH(psp);    KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);     KD_ARM_PREFETCH(dp+dst_stride);
    }

  // Prepare processing machinery
  int32x4_t shift = vdupq_n_s32(31-K_max);
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  int32x4_t or_val = vdupq_n_s32(0);
  int32x4_t val1, val2, signs1, signs2;
  
  // Process all but the last 2 code-block rows
  for (; height > 2; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int16 *sp = (kdu_int16 *)nxt_src;
      nxt_src = ((kdu_int16 *)src_refs[1]) + src_offset;
      psp = ((kdu_int16 *)src_refs[2]) + src_offset;
      for (dp=dst, c=src_width; c > 16; c-=16, sp+=16, psp+=16, dp+=16)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          KD_ARM_PREFETCH(dp+2*dst_stride+8);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vld1_s16(sp+8));  // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+12)); // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);            vst1q_s32(dp+12,val2);
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          KD_ARM_PREFETCH(dp+2*dst_stride+8);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vand_s16(vld1_s16(sp+8),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+12),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);            vst1q_s32(dp+12,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = vmovl_s16(vand_s16(vld1_s16(sp),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+4),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int16 *sp = (kdu_int16 *)nxt_src;
      nxt_src = ((kdu_int16 *)src_refs[1]) + src_offset;
      for (dp=dst, c=src_width; c > 16; c-=16, sp+=16, dp+=16)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vld1_s16(sp+8));  // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+12)); // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);            vst1q_s32(dp+12,val2);
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vand_s16(vld1_s16(sp+8),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+12),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);            vst1q_s32(dp+12,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = vmovl_s16(vand_s16(vld1_s16(sp),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+4),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
    }
  
  // Accumulate the OR'd mag bit-planes and return
  kdu_int32 result = vgetq_lane_s32(or_val,0);
  result |= vgetq_lane_s32(or_val,1);
  result |= vgetq_lane_s32(or_val,2);
  result |= vgetq_lane_s32(or_val,3);
  return (result & 0x7FFFFFFF);
}

/*****************************************************************************/
/* EXTERN                 neoni_quantize32_rev_block32                       */
/*****************************************************************************/

kdu_int32
  neoni_quantize32_rev_block32(kdu_int32 *dst, void **src_refs,
                               int src_offset, int src_width,
                               int dst_stride, int height,
                               int K_max, float delta_unused)
{
  int c;
  int32x4_t end_mask = vld1q_s32(((kdu_int32 *)local_mask_src128) +
                                 ((-src_width) & 3));
  kdu_int32 *nxt_src=((kdu_int32 *)src_refs[0]) + src_offset;
  
  // Prefetch 2 source code-block rows and 2 output code-block rows
  kdu_int32 *dp = dst;
  kdu_int32 *psp=nxt_src; // psp = prefetch source pointer
  kdu_int32 *psp1=((kdu_int32 *)src_refs[1]) + src_offset;
  if (height < 2) psp1 = psp; // Avoid using invalid address
  for (c=src_width; c > 8; c-= 16, psp+=16, psp1+=16, dp+=16)
    { 
      KD_ARM_PREFETCH(psp);            KD_ARM_PREFETCH(psp+8);
      KD_ARM_PREFETCH(psp1);           KD_ARM_PREFETCH(psp1+8);
      KD_ARM_PREFETCH(dp+dst_stride);  KD_ARM_PREFETCH(dp+dst_stride+8);
      KD_ARM_PREFETCH(dp);             KD_ARM_PREFETCH(dp+8);
    }
  if (c > 0)
    { 
      KD_ARM_PREFETCH(psp);    KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);     KD_ARM_PREFETCH(dp+dst_stride);
    }

  // Prepare processing machinery
  int32x4_t shift = vdupq_n_s32(31-K_max);
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  int32x4_t or_val = vdupq_n_s32(0);
  int32x4_t val1, val2, signs1, signs2;
  
  // Process all but the last 2 code-block rows
  for (; height > 2; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int32 *sp = nxt_src;
      nxt_src = ((kdu_int32 *)src_refs[1]) + src_offset;
      psp = ((kdu_int32 *)src_refs[2]) + src_offset;
      for (dp=dst, c=src_width; c > 8; c-=8, sp+=8, psp+=8, dp+=8)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vld1q_s32(sp);            val2 = vld1q_s32(sp+4);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      if (c > 4)
        { // Process two final vectors, with source word masking
          val1 = vld1q_s32(sp);            val2 = vld1q_s32(sp+4);
          val2 = vandq_s32(val2,end_mask);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = vld1q_s32(sp);
          val1 = vandq_s32(val1,end_mask);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          val1 = vshlq_s32(val1,shift);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          or_val = vorrq_s32(or_val,val1);
          val1 = vorrq_s32(val1,signs1);
          vst1q_s32(dp,val1);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int32 *sp = nxt_src;
      nxt_src = ((kdu_int32 *)src_refs[1]) + src_offset;
      for (dp=dst, c=src_width; c > 8; c-=8, sp+=8, dp+=8)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vld1q_s32(sp);            val2 = vld1q_s32(sp+4);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      if (c > 4)
        { // Process two final vectors, with source word masking
          val1 = vld1q_s32(sp);            val2 = vld1q_s32(sp+4);
          val2 = vandq_s32(val2,end_mask);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          val1 = vshlq_s32(val1,shift);    val2 = vshlq_s32(val2,shift);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      else
        { // Process one final vector, with source word masking
          val1 = vld1q_s32(sp);
          val1 = vandq_s32(val1,end_mask);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          val1 = vshlq_s32(val1,shift);
          or_val = vorrq_s32(or_val,val1);
          val1 = vorrq_s32(val1,signs1);
          vst1q_s32(dp,val1);
        }
    }
  
  // Accumulate the OR'd mag bit-planes and return
  kdu_int32 result = vgetq_lane_s32(or_val,0);
  result |= vgetq_lane_s32(or_val,1);
  result |= vgetq_lane_s32(or_val,2);
  result |= vgetq_lane_s32(or_val,3);
  return (result & 0x7FFFFFFF);
}

/*****************************************************************************/
/* EXTERN                neoni_quantize32_irrev_block16                      */
/*****************************************************************************/

kdu_int32
  neoni_quantize32_irrev_block16(kdu_int32 *dst, void **src_refs,
                                 int src_offset, int src_width,
                                 int dst_stride, int height,
                                 int K_max, float delta)
{
  int c;
  int16x8_t end_mask = vld1q_s16(((kdu_int16 *)local_mask_src128) +
                                 ((-src_width) & 7));
  kdu_int16 *nxt_src=((kdu_int16 *)src_refs[0]) + src_offset;
  
  // Prefetch 2 source code-block rows and 2 output code-block row
  kdu_int32 *dp = dst;
  kdu_int16 *psp=nxt_src; // pdsp = prefetch source pointer
  kdu_int16 *psp1=((kdu_int16 *)src_refs[1]) + src_offset;
  if (height < 2) psp1 = psp; // Avoid using invalid address
  for (c=src_width; c > 8; c-=16, dp+=16, psp+=16, psp1+=16)
    { 
      KD_ARM_PREFETCH(psp);            KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);             KD_ARM_PREFETCH(dp+8);
      KD_ARM_PREFETCH(dp+dst_stride);  KD_ARM_PREFETCH(dp+dst_stride+8);
    }
  if (c > 0)
    { 
      KD_ARM_PREFETCH(psp);    KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);     KD_ARM_PREFETCH(dp+dst_stride);
    }
  
  // Prepare processing machinery
  float fscale = 1.0F / (delta * (float)(1<<KDU_FIX_POINT));
  if (K_max <= 31)
    fscale *= (float)(1<<(31-K_max));
  else
    fscale /= (float)(1<<(K_max-31));
  int32x4_t vec_scale = vdupq_n_s32((kdu_int32)(fscale+0.5F));
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  int32x4_t or_val = vdupq_n_s32(0);
  int32x4_t val1, val2, signs1, signs2;

  // Process all but the last 2 code-block rows
  for (; height > 2; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int16 *sp = (kdu_int16 *)nxt_src;
      nxt_src = ((kdu_int16 *)src_refs[1]) + src_offset;
      psp = ((kdu_int16 *)src_refs[2]) + src_offset;
      for (dp=dst, c=src_width; c > 16; c-=16, sp+=16, psp+=16, dp+=16)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          KD_ARM_PREFETCH(dp+2*dst_stride+8);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);

          val1 = vmovl_s16(vld1_s16(sp+8));  // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+12)); // to 32-bit ints
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);              vst1q_s32(dp+12,val2);;
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          val1 = vmovl_s16(vld1_s16(sp));
          val2 = vmovl_s16(vld1_s16(sp+4));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          KD_ARM_PREFETCH(dp+2*dst_stride+8);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vand_s16(vld1_s16(sp+8),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+12),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);              vst1q_s32(dp+12,val2);;
        }
      else
        { // Process one final vector, with source word masking
          val1 = vmovl_s16(vand_s16(vld1_s16(sp),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+4),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      kdu_int16 *sp = (kdu_int16 *)nxt_src;
      nxt_src = ((kdu_int16 *)src_refs[1]) + src_offset;
      for (dp=dst, c=src_width; c > 16; c-=16, sp+=16, dp+=16)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          val1 = vmovl_s16(vld1_s16(sp));    // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+4));  // to 32-bit ints
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vld1_s16(sp+8));  // Move and sign extend
          val2 = vmovl_s16(vld1_s16(sp+12)); // to 32-bit ints
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);              vst1q_s32(dp+12,val2);;
        }
      if (c > 8)
        { // Process two final vectors, with source word masking
          val1 = vmovl_s16(vld1_s16(sp));
          val2 = vmovl_s16(vld1_s16(sp+4));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);
          
          val1 = vmovl_s16(vand_s16(vld1_s16(sp+8),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+12),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp+8,val1);              vst1q_s32(dp+12,val2);;
        }
      else
        { // Process one final vector, with source word masking
          val1 = vmovl_s16(vand_s16(vld1_s16(sp),vget_low_s16(end_mask)));
          val2 = vmovl_s16(vand_s16(vld1_s16(sp+4),vget_high_s16(end_mask)));
          signs1 = vandq_s32(val1,smask);    val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);    val2 = vabsq_s32(val2);
          val1 = vmulq_s32(val1,vec_scale);  val2 = vmulq_s32(val2,vec_scale);
          or_val = vorrq_s32(or_val,val1);   or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);     val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);                vst1q_s32(dp+4,val2);
        }
    }
  
  // Accumulate the OR'd mag bit-planes and return
  kdu_int32 result = vgetq_lane_s32(or_val,0);
  result |= vgetq_lane_s32(or_val,1);
  result |= vgetq_lane_s32(or_val,2);
  result |= vgetq_lane_s32(or_val,3);
  return (result & 0x7FFFFFFF);
}

/*****************************************************************************/
/* EXTERN                neoni_quantize32_irrev_block32                      */
/*****************************************************************************/

kdu_int32
  neoni_quantize32_irrev_block32(kdu_int32 *dst, void **src_refs,
                                 int src_offset, int src_width,
                                 int dst_stride, int height,
                                 int K_max, float delta)
{
  int c;
  int32x4_t end_mask = vld1q_s32(((kdu_int32 *)local_mask_src128) +
                                 ((-src_width) & 3));
  float *nxt_src=((float *)src_refs[0]) + src_offset;
  
  // Prefetch 2 source code-block rows and 2 output code-block rows
  kdu_int32 *dp = dst;
  float *psp=nxt_src; // psp = prefetch source pointer
  float *psp1=((float *)src_refs[1]) + src_offset;
  if (height < 2) psp1 = psp; // Avoid using invalid address
  for (c=src_width; c > 8; c-= 16, psp+=16, psp1+=16, dp+=16)
    { 
      KD_ARM_PREFETCH(psp);            KD_ARM_PREFETCH(psp+8);
      KD_ARM_PREFETCH(psp1);           KD_ARM_PREFETCH(psp1+8);
      KD_ARM_PREFETCH(dp+dst_stride);  KD_ARM_PREFETCH(dp+dst_stride+8);
      KD_ARM_PREFETCH(dp);             KD_ARM_PREFETCH(dp+8);
    }
  if (c > 0)
    { 
      KD_ARM_PREFETCH(psp);    KD_ARM_PREFETCH(psp1);
      KD_ARM_PREFETCH(dp);     KD_ARM_PREFETCH(dp+dst_stride);
    }
  
  // Prepare processing machinery
  float fscale = 1.0F / delta;
  if (K_max <= 31)
    fscale *= (float)(1<<(31-K_max));
  else
    fscale /= (float)(1<<(K_max-31));
  float32x4_t vec_scale = vdupq_n_f32(fscale);
  int32x4_t smask = vdupq_n_s32(0x80000000); // Mask for sign-bit
  int32x4_t or_val = vdupq_n_s32(0);
  float32x4_t fval1, fval2;
  int32x4_t val1, val2, signs1, signs2;

  // Process all but the last 2 code-block rows
  for (; height > 2; height--, src_refs++, dst+=dst_stride)
    { 
      float *sp = nxt_src;
      nxt_src = ((float *)src_refs[1]) + src_offset;
      psp = ((float *)src_refs[2]) + src_offset;
      for (dp=dst, c=src_width; c > 8; c-=8, sp+=8, psp+=8, dp+=8)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          fval1 = vld1q_f32(sp);           fval2 = vld1q_f32(sp+4);
          fval1 = vmulq_f32(fval1,vec_scale);
          fval2 = vmulq_f32(fval2,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          val1 = vcvtq_s32_f32(fval1);     val2 = vcvtq_s32_f32(fval2);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      if (c > 4)
        { // Process two final vectors, with source word masking
          fval1 = vld1q_f32(sp);
          fval2 = vreinterpretq_f32_s32(vandq_s32(vld1q_s32((kdu_int32 *)sp+4),
                                                  end_mask));
          fval1 = vmulq_f32(fval1,vec_scale);
          fval2 = vmulq_f32(fval2,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          val1 = vcvtq_s32_f32(fval1);     val2 = vcvtq_s32_f32(fval2);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      else
        { // Process one final vector, with source word masking
          fval1 = vreinterpretq_f32_s32(vandq_s32(vld1q_s32((kdu_int32 *)sp),
                                                  end_mask));
          fval1 = vmulq_f32(fval1,vec_scale);
          KD_ARM_PREFETCH(psp);
          KD_ARM_PREFETCH(dp+2*dst_stride);
          val1 = vcvtq_s32_f32(fval1);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          or_val = vorrq_s32(or_val,val1);
          val1 = vorrq_s32(val1,signs1);
          vst1q_s32(dp,val1);
        }
    }
  
  // Process the last 2 code-block rows without prefetch
  for (; height > 0; height--, src_refs++, dst+=dst_stride)
    { 
      float *sp = nxt_src;
      nxt_src = ((float *)src_refs[1]) + src_offset;
      for (dp=dst, c=src_width; c > 8; c-=8, sp+=8, dp+=8)
        { // Process 2 vectors at a time, leaving 1 or 2 to use with `end_mask'
          fval1 = vld1q_f32(sp);           fval2 = vld1q_f32(sp+4);
          fval1 = vmulq_f32(fval1,vec_scale);
          fval2 = vmulq_f32(fval2,vec_scale);
          val1 = vcvtq_s32_f32(fval1);     val2 = vcvtq_s32_f32(fval2);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      if (c > 4)
        { // Process two final vectors, with source word masking
          fval1 = vld1q_f32(sp);
          fval2 = vreinterpretq_f32_s32(vandq_s32(vld1q_s32((kdu_int32 *)sp+4),
                                                  end_mask));
          fval1 = vmulq_f32(fval1,vec_scale);
          fval2 = vmulq_f32(fval2,vec_scale);
          val1 = vcvtq_s32_f32(fval1);     val2 = vcvtq_s32_f32(fval2);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          signs2 = vandq_s32(val2,smask);  val2 = vabsq_s32(val2);
          or_val = vorrq_s32(or_val,val1); or_val = vorrq_s32(or_val,val2);
          val1 = vorrq_s32(val1,signs1);   val2 = vorrq_s32(val2,signs2);
          vst1q_s32(dp,val1);              vst1q_s32(dp+4,val2);
        }
      else
        { // Process one final vector, with source word masking
          fval1 = vreinterpretq_f32_s32(vandq_s32(vld1q_s32((kdu_int32 *)sp),
                                                  end_mask));
          fval1 = vmulq_f32(fval1,vec_scale);
          val1 = vcvtq_s32_f32(fval1);
          signs1 = vandq_s32(val1,smask);  val1 = vabsq_s32(val1);
          or_val = vorrq_s32(or_val,val1);
          val1 = vorrq_s32(val1,signs1);
          vst1q_s32(dp,val1);
        }
    }
  
  // Accumulate the OR'd mag bit-planes and return
  kdu_int32 result = vgetq_lane_s32(or_val,0);
  result |= vgetq_lane_s32(or_val,1);
  result |= vgetq_lane_s32(or_val,2);
  result |= vgetq_lane_s32(or_val,3);
  return (result & 0x7FFFFFFF);
}

} // namespace kd_core_simd

#endif // KDU_NEON_INTRINSICS
