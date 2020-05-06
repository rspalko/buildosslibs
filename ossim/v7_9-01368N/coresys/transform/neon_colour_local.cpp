/*****************************************************************************/
// File: neon_colour_local.cpp [scope = CORESYS/TRANSFORMS]
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
   Provides SIMD implementations to accelerate colour conversion.  This
source file is required to implement versions of the colour conversion
operations that are based on the ARM-NEON intrinsics.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_NEON) && (defined KDU_NEON_INTRINSICS))

#include <arm_neon.h>
using namespace kdu_core;

namespace kd_core_simd {

// The following constants are reproduced from "colour.cpp"
#define ALPHA_R 0.299              // These are exact expressions from which
#define ALPHA_B 0.114              // the ICT forward and reverse transform
#define ALPHA_RB (ALPHA_R+ALPHA_B) // coefficients may be expressed.
#define ALPHA_G (1-ALPHA_RB)
#define CB_FACT (1/(2*(1-ALPHA_B)))
#define CR_FACT (1/(2*(1-ALPHA_R)))
#define CR_FACT_R (2*(1-ALPHA_R))
#define CB_FACT_B (2*(1-ALPHA_B))
#define CR_FACT_G (2*ALPHA_R*(1-ALPHA_R)/ALPHA_G)
#define CB_FACT_G (2*ALPHA_B*(1-ALPHA_B)/ALPHA_G)

// The following constants are used for floating-point processing
#define vecps_alphaR      ((float) ALPHA_R)
#define vecps_alphaB      ((float) ALPHA_B)
#define vecps_alphaG      ((float) ALPHA_G)
#define vecps_CBfact      ((float) CB_FACT)
#define vecps_CRfact      ((float) CR_FACT)
#define vecps_CBfactB     ((float) CB_FACT_B)
#define vecps_CRfactR     ((float) CR_FACT_R)
#define vecps_neg_CBfactG ((float) -CB_FACT_G)
#define vecps_neg_CRfactG ((float) -CR_FACT_G)

// The following constants are used for fixed-point irreversible processing
// based on the VQRDMULHQ instruction, which effectively multiplies by the
// 16-bit integer scaling factor then divides by 2^15 with a rounding
// offset.
#define neon_alphaR      ((kdu_int16)(0.5+ALPHA_R*(1<<15)))
#define neon_alphaB      ((kdu_int16)(0.5+ALPHA_B*(1<<15)))
#define neon_alphaG      ((kdu_int16)(0.5+ALPHA_G*(1<<15)))
#define neon_CBfact      ((kdu_int16)(0.5+CB_FACT*(1<<15)))
#define neon_CRfact      ((kdu_int16)(0.5+CR_FACT*(1<<15)))
#define neon_CRfactR     ((kdu_int16)(0.5+(CR_FACT_R-1)*(1<<15)))
#define neon_CBfactB     ((kdu_int16)(0.5+(CB_FACT_B-1)*(1<<15)))
#define neon_neg_CRfactG ((kdu_int16)(0.5-CR_FACT_G*(1<<15)))
#define neon_neg_CBfactG ((kdu_int16)(0.5-CB_FACT_G*(1<<15)))


/* ========================================================================= */
/*                NEON Intrinsics for Irreversible Processing                */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  neoni_rgb_to_ycc_irrev16                          */
/*****************************************************************************/

void
  neoni_rgb_to_ycc_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                           int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.
  
  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  int16x8_t alpha_r = vdupq_n_s16(neon_alphaR);
  int16x8_t alpha_b = vdupq_n_s16(neon_alphaB);
  int16x8_t alpha_g = vdupq_n_s16(neon_alphaG);
  int16x8_t cb_fact = vdupq_n_s16(neon_CBfact);
  int16x8_t cr_fact = vdupq_n_s16(neon_CRfact);
  KD_ARM_PREFETCH(src1+32); KD_ARM_PREFETCH(src2+32); KD_ARM_PREFETCH(src3+32);
  int16x8_t y, red, green, blue;
  for (; samples > 8; samples -= 16)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      green = vld1q_s16(src2); red = vld1q_s16(src1); blue = vld1q_s16(src3);
      KD_ARM_PREFETCH(src1+64);
      KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);
      y = vqrdmulhq_s16(green,alpha_g);
      y = vaddq_s16(y,vqrdmulhq_s16(red,alpha_r));
      y = vaddq_s16(y,vqrdmulhq_s16(blue,alpha_b));
      vst1q_s16(src1,y);
      blue = vsubq_s16(blue,y);
      vst1q_s16(src2,vqrdmulhq_s16(blue,cb_fact));
      red = vsubq_s16(red,y);
      vst1q_s16(src3,vqrdmulhq_s16(red,cr_fact));
      src2 += 8;  src1 += 8;  src3 += 8;

      green = vld1q_s16(src2); red = vld1q_s16(src1); blue = vld1q_s16(src3);
      y = vqrdmulhq_s16(green,alpha_g);
      y = vaddq_s16(y,vqrdmulhq_s16(red,alpha_r));
      y = vaddq_s16(y,vqrdmulhq_s16(blue,alpha_b));
      vst1q_s16(src1,y);
      blue = vsubq_s16(blue,y);
      vst1q_s16(src2,vqrdmulhq_s16(blue,cb_fact));
      red = vsubq_s16(red,y);
      vst1q_s16(src3,vqrdmulhq_s16(red,cr_fact));
      src2 += 8;  src1 += 8;  src3 += 8;
    }
  if (samples > 0)
    { 
      green = vld1q_s16(src2); red = vld1q_s16(src1); blue = vld1q_s16(src3);
      y = vqrdmulhq_s16(green,alpha_g);
      y = vaddq_s16(y,vqrdmulhq_s16(red,alpha_r));
      y = vaddq_s16(y,vqrdmulhq_s16(blue,alpha_b));
      vst1q_s16(src1,y);
      blue = vsubq_s16(blue,y);
      vst1q_s16(src2,vqrdmulhq_s16(blue,cb_fact));
      red = vsubq_s16(red,y);
      vst1q_s16(src3,vqrdmulhq_s16(red,cr_fact));      
    }
}

/*****************************************************************************/
/* EXTERN                  neoni_rgb_to_ycc_irrev32                          */
/*****************************************************************************/

void
  neoni_rgb_to_ycc_irrev32(float *src1, float *src2, float *src3, int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);  
  float32x4_t alpha_r = vdupq_n_f32(vecps_alphaR);
  float32x4_t alpha_b = vdupq_n_f32(vecps_alphaB);
  float32x4_t alpha_g = vdupq_n_f32(vecps_alphaG);
  float32x4_t cb_fact = vdupq_n_f32(vecps_CBfact);
  float32x4_t cr_fact = vdupq_n_f32(vecps_CRfact);
  KD_ARM_PREFETCH(src1+16); KD_ARM_PREFETCH(src2+16); KD_ARM_PREFETCH(src3+16);
  float32x4_t y, green, red, blue;
  for (; samples > 4; samples -= 8)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      green=vld1q_f32(src2); red=vld1q_f32(src1); blue=vld1q_f32(src3);
      KD_ARM_PREFETCH(src1+32);
      KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);
      y = vmulq_f32(green,alpha_g);
      y = vmlaq_f32(y,red,alpha_r);
      y = vmlaq_f32(y,blue,alpha_b);
      vst1q_f32(src1,y);
      blue = vsubq_f32(blue,y);
      vst1q_f32(src2,vmulq_f32(blue,cb_fact));
      red = vsubq_f32(red,y);
      vst1q_f32(src3,vmulq_f32(red,cr_fact));
      src2 += 4;  src1 += 4;  src3 += 4;
    
      green=vld1q_f32(src2); red=vld1q_f32(src1); blue=vld1q_f32(src3);
      y = vmulq_f32(green,alpha_g);
      y = vmlaq_f32(y,red,alpha_r);
      y = vmlaq_f32(y,blue,alpha_b);
      vst1q_f32(src1,y);
      blue = vsubq_f32(blue,y);
      vst1q_f32(src2,vmulq_f32(blue,cb_fact));
      red = vsubq_f32(red,y);
      vst1q_f32(src3,vmulq_f32(red,cr_fact));
      src2 += 4;  src1 += 4;  src3 += 4;
    }
  if (samples > 0)
    { 
      green=vld1q_f32(src2); red=vld1q_f32(src1); blue=vld1q_f32(src3);
      y = vmulq_f32(green,alpha_g);
      y = vmlaq_f32(y,red,alpha_r);
      y = vmlaq_f32(y,blue,alpha_b);
      vst1q_f32(src1,y);
      blue = vsubq_f32(blue,y);
      vst1q_f32(src2,vmulq_f32(blue,cb_fact));
      red = vsubq_f32(red,y);
      vst1q_f32(src3,vmulq_f32(red,cr_fact));      
    }
}

/*****************************************************************************/
/* EXTERN               neoni_ycc_to_rgb_irrev16                             */
/*****************************************************************************/

void
  neoni_ycc_to_rgb_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                           int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  int16x8_t cr_fact_r = vdupq_n_s16(neon_CRfactR);
  int16x8_t cr_neg_fact_g = vdupq_n_s16(neon_neg_CRfactG);
  int16x8_t cb_fact_b = vdupq_n_s16(neon_CBfactB);
  int16x8_t cb_neg_fact_g = vdupq_n_s16(neon_neg_CBfactG);
  KD_ARM_PREFETCH(src1+32); KD_ARM_PREFETCH(src2+32); KD_ARM_PREFETCH(src3+32);
  int16x8_t y, cr, cb, tmp;
  for (; samples > 8; samples -= 16)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      y = vld1q_s16(src1);  cr = vld1q_s16(src3);
      KD_ARM_PREFETCH(src1+64);
      KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);
      tmp = vqrdmulhq_s16(cr,cr_fact_r);
      tmp = vaddq_s16(tmp, cr);
      vst1q_s16(src1,vaddq_s16(tmp, y)); // Save Red
      cr = vqrdmulhq_s16(cr, cr_neg_fact_g);
      cb = vld1q_s16(src2); // Load CB
      tmp = vqrdmulhq_s16(cb, cb_fact_b);
      tmp = vaddq_s16(tmp, cb);
      vst1q_s16(src3,vaddq_s16(tmp, y)); // Save Blue
      cb = vqrdmulhq_s16(cb, cb_neg_fact_g);
      y = vqaddq_s16(y,cr);
      vst1q_s16(src2,vqaddq_s16(y,cb)); // Save Green
      src1 += 8;  src3 += 8;  src2 += 8;
      
      y = vld1q_s16(src1);  cr = vld1q_s16(src3);
      tmp = vqrdmulhq_s16(cr,cr_fact_r);
      tmp = vaddq_s16(tmp, cr);
      vst1q_s16(src1,vaddq_s16(tmp, y)); // Save Red
      cr = vqrdmulhq_s16(cr, cr_neg_fact_g);
      cb = vld1q_s16(src2); // Load CB
      tmp = vqrdmulhq_s16(cb, cb_fact_b);
      tmp = vaddq_s16(tmp, cb);
      vst1q_s16(src3,vaddq_s16(tmp, y)); // Save Blue
      cb = vqrdmulhq_s16(cb, cb_neg_fact_g);
      y = vqaddq_s16(y,cr);
      vst1q_s16(src2,vqaddq_s16(y,cb)); // Save Green      
      src1 += 8;  src3 += 8;  src2 += 8;
    }
  if (samples > 0)
    { 
      y = vld1q_s16(src1);  cr = vld1q_s16(src3);
      tmp = vqrdmulhq_s16(cr,cr_fact_r);
      tmp = vaddq_s16(tmp, cr);
      vst1q_s16(src1,vaddq_s16(tmp, y)); // Save Red
      cr = vqrdmulhq_s16(cr, cr_neg_fact_g);
      cb = vld1q_s16(src2); // Load CB
      tmp = vqrdmulhq_s16(cb, cb_fact_b);
      tmp = vaddq_s16(tmp, cb);
      vst1q_s16(src3,vaddq_s16(tmp, y)); // Save Blue
      cb = vqrdmulhq_s16(cb, cb_neg_fact_g);
      y = vqaddq_s16(y,cr);
      vst1q_s16(src2,vqaddq_s16(y,cb)); // Save Green      
    }
}

/*****************************************************************************/
/* EXTERN               neoni_ycc_to_rgb_irrev32                             */
/*****************************************************************************/

void
  neoni_ycc_to_rgb_irrev32(float *src1, float *src2, float *src3, int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);  
  float32x4_t cr_fact_r = vdupq_n_f32(vecps_CRfactR);
  float32x4_t neg_cr_fact_g = vdupq_n_f32(vecps_neg_CRfactG);
  float32x4_t cb_fact_b = vdupq_n_f32(vecps_CBfactB);
  float32x4_t neg_cb_fact_g = vdupq_n_f32(vecps_neg_CBfactG);
  KD_ARM_PREFETCH(src1+16); KD_ARM_PREFETCH(src2+16); KD_ARM_PREFETCH(src3+16);
  float32x4_t y, cb, cr, green;
  for (; samples > 4; samples -= 8)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      y=vld1q_f32(src1); cr=vld1q_f32(src3); cb=vld1q_f32(src2);
      KD_ARM_PREFETCH(src1+32);
      KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);
      green = vmlaq_f32(y,cr,neg_cr_fact_g); // Partial green sum
      vst1q_f32(src1,vmlaq_f32(y,cr,cr_fact_r)); // This is red
      vst1q_f32(src3,vmlaq_f32(y,cb,cb_fact_b)); // This is blue
      vst1q_f32(src2,vmlaq_f32(green,cb,neg_cb_fact_g)); // Completed green
      src1 += 4;  src3 += 4;  src2 += 4;
      
      y=vld1q_f32(src1); cr=vld1q_f32(src3); cb=vld1q_f32(src2);
      green = vmlaq_f32(y,cr,neg_cr_fact_g); // Partial green sum
      vst1q_f32(src1,vmlaq_f32(y,cr,cr_fact_r)); // This is red
      vst1q_f32(src3,vmlaq_f32(y,cb,cb_fact_b)); // This is blue
      vst1q_f32(src2,vmlaq_f32(green,cb,neg_cb_fact_g)); // Completed green      
      src1 += 4;  src3 += 4;  src2 += 4;
    }
  if (samples > 0)
    { 
      y=vld1q_f32(src1); cr=vld1q_f32(src3); cb=vld1q_f32(src2);
      green = vmlaq_f32(y,cr,neg_cr_fact_g); // Partial green sum
      vst1q_f32(src1,vmlaq_f32(y,cr,cr_fact_r)); // This is red
      vst1q_f32(src3,vmlaq_f32(y,cb,cb_fact_b)); // This is blue
      vst1q_f32(src2,vmlaq_f32(green,cb,neg_cb_fact_g)); // Completed green      
    }
}


/* ========================================================================= */
/*                 NEON Intrinsics for Reversible Processing                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                neoni_rgb_to_ycc_rev16                              */
/*****************************************************************************/

void
  neoni_rgb_to_ycc_rev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                         int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  KD_ARM_PREFETCH(src1+32); KD_ARM_PREFETCH(src2+32); KD_ARM_PREFETCH(src3+32);
  int16x8_t y, red, green, blue;
  for (; samples > 8; samples -= 16)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      red=vld1q_s16(src1); green=vld1q_s16(src2); blue=vld1q_s16(src3);    
      KD_ARM_PREFETCH(src1+64);
      KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);
      y = vaddq_s16(red,blue);
      y = vaddq_s16(y,green);
      y = vaddq_s16(y,green); // Now have 2*G + R + B
      vst1q_s16(src1,vshrq_n_s16(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s16(src2,vsubq_s16(blue,green)); // Db = B-G
      vst1q_s16(src3,vsubq_s16(red,green)); // Dr = R-G
      src1 += 8;  src2 += 8;  src3 += 8;
      
      red=vld1q_s16(src1); green=vld1q_s16(src2); blue=vld1q_s16(src3);    
      y = vaddq_s16(red,blue);
      y = vaddq_s16(y,green);
      y = vaddq_s16(y,green); // Now have 2*G + R + B
      vst1q_s16(src1,vshrq_n_s16(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s16(src2,vsubq_s16(blue,green)); // Db = B-G
      vst1q_s16(src3,vsubq_s16(red,green)); // Dr = R-G
      src1 += 8;  src2 += 8;  src3 += 8;
    }
  if (samples > 0)
    { 
      red=vld1q_s16(src1); green=vld1q_s16(src2); blue=vld1q_s16(src3);    
      y = vaddq_s16(red,blue);
      y = vaddq_s16(y,green);
      y = vaddq_s16(y,green); // Now have 2*G + R + B
      vst1q_s16(src1,vshrq_n_s16(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s16(src2,vsubq_s16(blue,green)); // Db = B-G
      vst1q_s16(src3,vsubq_s16(red,green)); // Dr = R-G      
    }
}

/*****************************************************************************/
/* EXTERN                neoni_rgb_to_ycc_rev32                              */
/*****************************************************************************/

void
  neoni_rgb_to_ycc_rev32(kdu_int32 *src1, kdu_int32 *src2, kdu_int32 *src3,
                         int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  KD_ARM_PREFETCH(src1+16); KD_ARM_PREFETCH(src2+16); KD_ARM_PREFETCH(src3+16);
  int32x4_t y, red, green, blue;
  for (; samples > 4; samples -= 8)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      red=vld1q_s32(src1); green=vld1q_s32(src2); blue=vld1q_s32(src3);    
      KD_ARM_PREFETCH(src1+32);
      KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);
      y = vaddq_s32(red,blue);
      y = vaddq_s32(y,green);
      y = vaddq_s32(y,green); // Now have 2*G + R + B
      vst1q_s32(src1,vshrq_n_s32(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s32(src2,vsubq_s32(blue,green)); // Db = B-G
      vst1q_s32(src3,vsubq_s32(red,green)); // Dr = R-G
      src1 += 4;  src2 += 4;  src3 += 4;
      
      red=vld1q_s32(src1); green=vld1q_s32(src2); blue=vld1q_s32(src3);    
      y = vaddq_s32(red,blue);
      y = vaddq_s32(y,green);
      y = vaddq_s32(y,green); // Now have 2*G + R + B
      vst1q_s32(src1,vshrq_n_s32(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s32(src2,vsubq_s32(blue,green)); // Db = B-G
      vst1q_s32(src3,vsubq_s32(red,green)); // Dr = R-G
      src1 += 4;  src2 += 4;  src3 += 4;
    }
  if (samples > 0)
    { 
      red=vld1q_s32(src1); green=vld1q_s32(src2); blue=vld1q_s32(src3);    
      y = vaddq_s32(red,blue);
      y = vaddq_s32(y,green);
      y = vaddq_s32(y,green); // Now have 2*G + R + B
      vst1q_s32(src1,vshrq_n_s32(y,2)); // Y = (2*G+R+B)>>2
      vst1q_s32(src2,vsubq_s32(blue,green)); // Db = B-G
      vst1q_s32(src3,vsubq_s32(red,green)); // Dr = R-G      
    }
}

/*****************************************************************************/
/* EXTERN                neoni_ycc_to_rgb_rev16                              */
/*****************************************************************************/

void
  neoni_ycc_to_rgb_rev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                         int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  KD_ARM_PREFETCH(src1+32); KD_ARM_PREFETCH(src2+32); KD_ARM_PREFETCH(src3+32);
  int16x8_t db, dr, y, green, tmp;
  for (; samples > 8; samples -= 16)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      db=vld1q_s16(src2); dr=vld1q_s16(src3); y=vld1q_s16(src1);
      KD_ARM_PREFETCH(src1+64);
      KD_ARM_PREFETCH(src2+64);
      KD_ARM_PREFETCH(src3+64);
      tmp = vaddq_s16(db,dr);
      tmp = vshrq_n_s16(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s16(y, tmp);
      vst1q_s16(src2,green);
      vst1q_s16(src1,vaddq_s16(dr,green));
      vst1q_s16(src3,vaddq_s16(db,green));
      src2 += 8;  src3 += 8;  src1 += 8;
      
      db=vld1q_s16(src2); dr=vld1q_s16(src3); y=vld1q_s16(src1);
      tmp = vaddq_s16(db,dr);
      tmp = vshrq_n_s16(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s16(y, tmp);
      vst1q_s16(src2,green);
      vst1q_s16(src1,vaddq_s16(dr,green));
      vst1q_s16(src3,vaddq_s16(db,green));
      src2 += 8;  src3 += 8;  src1 += 8;
    }
  if (samples > 0)
    { 
      db=vld1q_s16(src2); dr=vld1q_s16(src3); y=vld1q_s16(src1);
      tmp = vaddq_s16(db,dr);
      tmp = vshrq_n_s16(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s16(y, tmp);
      vst1q_s16(src2,green);
      vst1q_s16(src1,vaddq_s16(dr,green));
      vst1q_s16(src3,vaddq_s16(db,green));      
    }
}

/*****************************************************************************/
/* EXTERN                neoni_ycc_to_rgb_rev32                              */
/*****************************************************************************/

void
  neoni_ycc_to_rgb_rev32(kdu_int32 *src1, kdu_int32 *src2, kdu_int32 *src3,
                         int samples)
{
  // NOTE: All loads and stores here are guaranteed to be 128-bit aligned,
  // but the intrinsics do not exploit this.  Visual Studio builds may benefit
  // from the use of the Microsoft-specific "_ex" suffixed load/store
  // intrinsics that can capture this alignment.

  KD_ARM_PREFETCH(src1);    KD_ARM_PREFETCH(src2);    KD_ARM_PREFETCH(src3);
  KD_ARM_PREFETCH(src1+16); KD_ARM_PREFETCH(src2+16); KD_ARM_PREFETCH(src3+16);
  int32x4_t db, dr, y, green, tmp;
  for (; samples > 4; samples -= 8)
    { // Loop unrolled by a factor of 2 -- process 32 bytes at a time
      db=vld1q_s32(src2); dr=vld1q_s32(src3); y=vld1q_s32(src1);
      KD_ARM_PREFETCH(src1+32);
      KD_ARM_PREFETCH(src2+32);
      KD_ARM_PREFETCH(src3+32);
      tmp = vaddq_s32(db,dr);
      tmp = vshrq_n_s32(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s32(y, tmp);
      vst1q_s32(src2,green);
      vst1q_s32(src1,vaddq_s32(dr,green));
      vst1q_s32(src3,vaddq_s32(db,green));
      src2 += 4;  src3 += 4;  src1 += 4;
      
      db=vld1q_s32(src2); dr=vld1q_s32(src3); y=vld1q_s32(src1);
      tmp = vaddq_s32(db,dr);
      tmp = vshrq_n_s32(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s32(y, tmp);
      vst1q_s32(src2,green);
      vst1q_s32(src1,vaddq_s32(dr,green));
      vst1q_s32(src3,vaddq_s32(db,green));
      src2 += 4;  src3 += 4;  src1 += 4;
    }
  if (samples > 0)
    { 
      db=vld1q_s32(src2); dr=vld1q_s32(src3); y=vld1q_s32(src1);
      tmp = vaddq_s32(db,dr);
      tmp = vshrq_n_s32(tmp,2); // Forms (Db+Dr)>>2
      green = vsubq_s32(y, tmp);
      vst1q_s32(src2,green);
      vst1q_s32(src1,vaddq_s32(dr,green));
      vst1q_s32(src3,vaddq_s32(db,green));
    }
}
  
} // namespace kd_core_simd

#endif // NEON Intrinsics

