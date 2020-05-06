/*****************************************************************************/
// File: avx_colour_local.cpp [scope = CORESYS/TRANSFORMS]
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
source file is required to implement AVX versions of the colour conversion
operations -- placing the code in a separate file allows the compiler to
be instructed to use vex-prefixed instructions exclusively, which avoids
processor state transition costs.  There is no harm in including this
source file with all builds, even if AVX is not supported, so long as you
are careful to globally define the `KDU_NO_AVX' compilation directive.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER

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

// The following constants are reproduced from "x86_colour_local.h"
#define vecps_alphaR      ((float) ALPHA_R)
#define vecps_alphaB      ((float) ALPHA_B)
#define vecps_alphaG      ((float) ALPHA_G)
#define vecps_CBfact      ((float) CB_FACT)
#define vecps_CRfact      ((float) CR_FACT)
#define vecps_CBfactB     ((float) CB_FACT_B)
#define vecps_CRfactR     ((float) CR_FACT_R)
#define vecps_neg_CBfactG ((float) -CB_FACT_G)
#define vecps_neg_CRfactG ((float) -CR_FACT_G)

/* ========================================================================= */
/*                         Now for the SIMD functions                        */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    avx_rgb_to_ycc_irrev32                          */
/*****************************************************************************/

void avx_rgb_to_ycc_irrev32(float *src1, float *src2, float *src3, int samples)
{
  __m256 alpha_r = _mm256_set1_ps(vecps_alphaR);
  __m256 alpha_b = _mm256_set1_ps(vecps_alphaB);
  __m256 alpha_g = _mm256_set1_ps(vecps_alphaG);
  __m256 cb_fact = _mm256_set1_ps(vecps_CBfact);
  __m256 cr_fact = _mm256_set1_ps(vecps_CRfact);
  __m256 *sp1=(__m256 *)src1, *sp2=(__m256 *)src2, *sp3=(__m256 *)src3;
  __m256 y, red, green, blue;
  for (; samples > 0; samples-=8, sp1++, sp2++, sp3++)
    { 
      green = sp2[0];  y = _mm256_mul_ps(green,alpha_g);
      red = sp1[0];    blue = sp3[0];
      y = _mm256_add_ps(y,_mm256_mul_ps(red,alpha_r));
      y = _mm256_add_ps(y,_mm256_mul_ps(blue,alpha_b));
      sp1[0] = y;      blue = _mm256_sub_ps(blue,y);
      sp2[0] = _mm256_mul_ps(blue,cb_fact);
      red = _mm256_sub_ps(red,y);   sp3[0] = _mm256_mul_ps(red,cr_fact);
    }      
}

/*****************************************************************************/
/* EXTERN                 avx_ycc_to_rgb_irrev32                             */
/*****************************************************************************/

void avx_ycc_to_rgb_irrev32(float *src1, float *src2, float *src3, int samples)
{
  __m256 cr_fact_r = _mm256_set1_ps(vecps_CRfactR);
  __m256 neg_cr_fact_g = _mm256_set1_ps(vecps_neg_CRfactG);
  __m256 cb_fact_b = _mm256_set1_ps(vecps_CBfactB);
  __m256 neg_cb_fact_g = _mm256_set1_ps(vecps_neg_CBfactG);
  __m256 *sp1=(__m256 *)src1, *sp2=(__m256 *)src2, *sp3=(__m256 *)src3;
  __m256 y, cb, cr, red, green, blue;
  for (; samples > 0; samples -= 8, sp1++, sp2++, sp3++)
    { 
      y = sp1[0];  cr = sp3[0];  red = _mm256_mul_ps(cr,cr_fact_r);
      sp1[0] = _mm256_add_ps(red,y); // Add in luminance to get red & save
      green = _mm256_mul_ps(cr,neg_cr_fact_g);
      green = _mm256_add_ps(green,y); // Y + scaled CR forms most of green
      cb = sp2[0];  blue = _mm256_mul_ps(cb,cb_fact_b);
      sp3[0] = _mm256_add_ps(blue,y); // Add in luminance to get blue & save
      cb = _mm256_mul_ps(cb,neg_cb_fact_g);
      sp2[0] = _mm256_add_ps(green,cb); // Complete and save the green channel
    }
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_AVX
