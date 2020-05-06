/*****************************************************************************/
// File: avx2_colour_local.cpp [scope = CORESYS/TRANSFORMS]
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
source file is required to implement AVX2 versions of the colour conversion
operations.  There is no harm in including this source file with all builds,
even if AVX2 is not supported, so long as you are careful to globally define
the `KDU_NO_AVX2' compilation directive.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX2) && (defined KDU_X86_INTRINSICS))

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

// The following constants are reproduced from "x86_colour_local.h"
#define ssse3_alphaR      ((kdu_int16)(0.5+ALPHA_R*(1<<15)))
#define ssse3_alphaB      ((kdu_int16)(0.5+ALPHA_B*(1<<15)))
#define ssse3_alphaG      ((kdu_int16)(0.5+ALPHA_G*(1<<15)))
#define ssse3_CBfact      ((kdu_int16)(0.5+CB_FACT*(1<<15)))
#define ssse3_CRfact      ((kdu_int16)(0.5+CR_FACT*(1<<15)))
#define ssse3_CRfactR     ((kdu_int16)(0.5+(CR_FACT_R-1)*(1<<15)))
#define ssse3_CBfactB     ((kdu_int16)(0.5+(CB_FACT_B-1)*(1<<15)))
#define ssse3_neg_CRfactG ((kdu_int16)(0.5-CR_FACT_G*(1<<15)))
#define ssse3_neg_CBfactG ((kdu_int16)(0.5-CB_FACT_G*(1<<15)))


/* ========================================================================= */
/*                SIMD functions for Irreversible Processing                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_rgb_to_ycc_irrev16                          */
/*****************************************************************************/

void
  avx2_rgb_to_ycc_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                          int samples)
{
  __m256i alpha_r = _mm256_set1_epi16(ssse3_alphaR);
  __m256i alpha_b = _mm256_set1_epi16(ssse3_alphaB);
  __m256i alpha_g = _mm256_set1_epi16(ssse3_alphaG);
  __m256i cb_fact = _mm256_set1_epi16(ssse3_CBfact);
  __m256i cr_fact = _mm256_set1_epi16(ssse3_CRfact);
  for (int c = 0; c < samples; c += 16)
    { 
      __m256i green = *((__m256i *)(src2 + c));
      __m256i y = _mm256_mulhrs_epi16(green, alpha_g);
      __m256i red = *((__m256i *)(src1 + c));
      __m256i blue = *((__m256i *)(src3 + c));
      y = _mm256_add_epi16(y, _mm256_mulhrs_epi16(red, alpha_r));
      y = _mm256_add_epi16(y, _mm256_mulhrs_epi16(blue, alpha_b));
      *((__m256i *)(src1 + c)) = y;
      blue = _mm256_sub_epi16(blue, y);
      *((__m256i *)(src2 + c)) = _mm256_mulhrs_epi16(blue, cb_fact);
      red = _mm256_sub_epi16(red, y);
      *((__m256i *)(src3 + c)) = _mm256_mulhrs_epi16(red, cr_fact);
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_rgb_to_ycc_irrev32                          */
/*****************************************************************************/

void
  avx2_rgb_to_ycc_irrev32(float *src1, float *src2, float *src3, int samples)
  /* This function is identical to `avx_rgb_to_ycc_irrev32' except that it
     takes advantage of the multiply-accumulate functions in AVX2. */
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
      green = sp2[0];  red = sp1[0];  blue = sp3[0];
      y = _mm256_mul_ps(green, alpha_g);
      y = _mm256_fmadd_ps(red, alpha_r, y);
      y = _mm256_fmadd_ps(blue,alpha_b,y);
      sp1[0] = y;
      blue = _mm256_sub_ps(blue,y);
      sp2[0] = _mm256_mul_ps(blue,cb_fact);
      red = _mm256_sub_ps(red,y);
      sp3[0] = _mm256_mul_ps(red,cr_fact);
    }      
}

/*****************************************************************************/
/* EXTERN                avx2_ycc_to_rgb_irrev16                             */
/*****************************************************************************/

void
  avx2_ycc_to_rgb_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                          int samples)
{
  __m256i cr_fact_r = _mm256_set1_epi16(ssse3_CRfactR);
  __m256i cr_neg_fact_g = _mm256_set1_epi16(ssse3_neg_CRfactG);
  __m256i cb_fact_b = _mm256_set1_epi16(ssse3_CBfactB);
  __m256i cb_neg_fact_g = _mm256_set1_epi16(ssse3_neg_CBfactG);
  for (int c = 0; c < samples; c += 16)
    { 
      __m256i y = *((__m256i *)(src1 + c));
      __m256i tmp = *((__m256i *)(src3 + c)); // Load CR
      __m256i cr = tmp;
      tmp = _mm256_mulhrs_epi16(tmp, cr_fact_r);
      tmp = _mm256_add_epi16(tmp, cr);
      *((__m256i *)(src1 + c)) = _mm256_adds_epi16(tmp, y); // Save Red
      cr = _mm256_mulhrs_epi16(cr, cr_neg_fact_g);
      tmp = *((__m256i *)(src2 + c)); // Load CB
      __m256i cb = tmp;
      tmp = _mm256_mulhrs_epi16(tmp, cb_fact_b);
      tmp = _mm256_add_epi16(tmp, cb);
      *((__m256i *)(src3 + c)) = _mm256_adds_epi16(tmp, y); // Save Blue
      cb = _mm256_mulhrs_epi16(cb, cb_neg_fact_g);
      y = _mm256_adds_epi16(y, cr);
      *((__m256i *)(src2 + c)) = _mm256_adds_epi16(y, cb); // Save Green
    }
}

/*****************************************************************************/
/* EXTERN                avx2_ycc_to_rgb_irrev32                             */
/*****************************************************************************/

void
  avx2_ycc_to_rgb_irrev32(float *src1, float *src2, float *src3, int samples)
  /* This function is identical to `avx_ycc_to_rgb_irrev32' except that it
     takes advantage of the multiply-accumulate functions in AVX2. */
{
  __m256 cr_fact_r = _mm256_set1_ps(vecps_CRfactR);
  __m256 neg_cr_fact_g = _mm256_set1_ps(vecps_neg_CRfactG);
  __m256 cb_fact_b = _mm256_set1_ps(vecps_CBfactB);
  __m256 neg_cb_fact_g = _mm256_set1_ps(vecps_neg_CBfactG);
  __m256 *sp1=(__m256 *)src1, *sp2=(__m256 *)src2, *sp3=(__m256 *)src3;
  __m256 y, cb, cr, green;
  for (; samples > 0; samples -= 8, sp1++, sp2++, sp3++)
    { 
      y = sp1[0];  cr = sp3[0];  cb = sp2[0];
      green = _mm256_fmadd_ps(cr, neg_cr_fact_g, y); // Partial green sum
      sp1[0] = _mm256_fmadd_ps(cr, cr_fact_r, y); // This is red
      sp3[0] = _mm256_fmadd_ps(cb,cb_fact_b,y); // This is blue
      sp2[0] = _mm256_fmadd_ps(cb,neg_cb_fact_g,green); // Completed green
    }
}


/* ========================================================================= */
/*                 SIMD functions for Reversible Processing                  */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 avx2_rgb_to_ycc_rev16                              */
/*****************************************************************************/

void
  avx2_rgb_to_ycc_rev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                        int samples)
{
  for (int c = 0; c < samples; c += 16)
    { 
      __m256i red = *((__m256i *)(src1 + c));
      __m256i green = *((__m256i *)(src2 + c));
      __m256i blue = *((__m256i *)(src3 + c));
      __m256i y = _mm256_adds_epi16(red, blue);
      y = _mm256_adds_epi16(y, green);
      y = _mm256_adds_epi16(y, green); // Now have 2*G + R + B
      *((__m256i *)(src1 + c)) = _mm256_srai_epi16(y, 2); // Y=(2*G+R+B)>>2
      *((__m256i *)(src2 + c)) = _mm256_subs_epi16(blue, green); // Db = B-G
      *((__m256i *)(src3 + c)) = _mm256_subs_epi16(red, green); // Dr = R-G
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_rgb_to_ycc_rev32                              */
/*****************************************************************************/

void
  avx2_rgb_to_ycc_rev32(kdu_int32 *src1, kdu_int32 *src2, kdu_int32 *src3,
                        int samples)
{
  for (int c = 0; c < samples; c += 8)
    { 
      __m256i red = *((__m256i *)(src1 + c));
      __m256i green = *((__m256i *)(src2 + c));
      __m256i blue = *((__m256i *)(src3 + c));
      __m256i y = _mm256_add_epi32(red, blue);
      y = _mm256_add_epi32(y, green);
      y = _mm256_add_epi32(y, green); // Now have 2*G + R + B
      *((__m256i *)(src1 + c)) = _mm256_srai_epi32(y, 2); // Y=(2*G+R+B)>>2
      *((__m256i *)(src2 + c)) = _mm256_sub_epi32(blue, green); // Db = B-G
      *((__m256i *)(src3 + c)) = _mm256_sub_epi32(red, green); // Dr = R-G
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_ycc_to_rgb_rev16                              */
/*****************************************************************************/

void
  avx2_ycc_to_rgb_rev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                        int samples)
{
  for (int c = 0; c < samples; c += 16)
    { 
      __m256i db = *((__m256i *)(src2 + c));
      __m256i dr = *((__m256i *)(src3 + c));
      __m256i y = *((__m256i *)(src1 + c));
      __m256i tmp = _mm256_adds_epi16(db, dr);
      tmp = _mm256_srai_epi16(tmp, 2); // Forms (Db+Dr)>>2
      __m256i green = _mm256_subs_epi16(y, tmp);
      *((__m256i *)(src2 + c)) = green;
      *((__m256i *)(src1 + c)) = _mm256_adds_epi16(dr, green);
      *((__m256i *)(src3 + c)) = _mm256_adds_epi16(db, green);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_ycc_to_rgb_rev32                              */
/*****************************************************************************/

void
  avx2_ycc_to_rgb_rev32(kdu_int32 *src1, kdu_int32 *src2, kdu_int32 *src3,
                        int samples)
{
  for (int c = 0; c < samples; c += 8)
    { 
      __m256i db = *((__m256i *)(src2 + c));
      __m256i dr = *((__m256i *)(src3 + c));
      __m256i y = *((__m256i *)(src1 + c));
      __m256i tmp = _mm256_add_epi32(db, dr);
      tmp = _mm256_srai_epi32(tmp, 2); // Forms (Db+Dr)>>2
      __m256i green = _mm256_sub_epi32(y, tmp);
      *((__m256i *)(src2 + c)) = green;
      *((__m256i *)(src1 + c)) = _mm256_add_epi32(dr, green);
      *((__m256i *)(src3 + c)) = _mm256_add_epi32(db, green);
  }
}

} // namespace kd_core_simd

#endif // !KDU_NO_AVX2

