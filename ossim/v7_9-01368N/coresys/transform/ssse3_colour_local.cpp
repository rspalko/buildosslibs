/*****************************************************************************/
// File: ssse3_dwt_local.cpp [scope = CORESYS/TRANSFORMS]
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
   Provides SSSE3-specific colour transform accelerators that are selected
by the logic found in "x86_colour_local.h".  There is no harm in including this
source file with all builds, even if SSSE3 is not supported by the compiler,
so long as KDU_NO_SSSE3 is defined or KDU_X86_INTRINSICS is not defined.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_SSSE3) && (defined KDU_X86_INTRINSICS))

#include <tmmintrin.h>
#include <assert.h>

using namespace kdu_core;

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

// The constants below are used for SIMD processing with SSSE3 rounding.
// These vector values are related to the non-vector values through:
//  ssse3_CBfact  = CB_FACT     =  0.564; ssse3_CRfact  = CR_FACT     = 0.713
//  ssse3_CRfactR = CR_FACT_R-1 =  0.402; ssse3_CBfactB = CB_FACT_B-1 = 0.772
//  ssse3_CRfactG = -CR_FACT_G  = -0.714; ssse3_CBfactG = -CB_FACT_G  = -0.344
#define ssse3_alphaR      ((kdu_int16)(0.5+ALPHA_R*(1<<15)))
#define ssse3_alphaB      ((kdu_int16)(0.5+ALPHA_B*(1<<15)))
#define ssse3_alphaG      ((kdu_int16)(0.5+ALPHA_G*(1<<15)))
#define ssse3_CBfact      ((kdu_int16)(0.5+CB_FACT*(1<<15)))
#define ssse3_CRfact      ((kdu_int16)(0.5+CR_FACT*(1<<15)))
#define ssse3_CRfactR     ((kdu_int16)(0.5+(CR_FACT_R-1)*(1<<15)))
#define ssse3_CBfactB     ((kdu_int16)(0.5+(CB_FACT_B-1)*(1<<15)))
#define ssse3_neg_CRfactG ((kdu_int16)(0.5-CR_FACT_G*(1<<15)))
#define ssse3_neg_CBfactG ((kdu_int16)(0.5-CB_FACT_G*(1<<15)))

namespace kd_core_simd {

/*****************************************************************************/
/* EXTERN                  ssse3_rgb_to_ycc_irrev16                          */
/*****************************************************************************/

void
  ssse3_rgb_to_ycc_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                           int samples)
{
  __m128i alpha_r = _mm_set1_epi16(ssse3_alphaR);
  __m128i alpha_b = _mm_set1_epi16(ssse3_alphaB);
  __m128i alpha_g = _mm_set1_epi16(ssse3_alphaG);
  __m128i cb_fact = _mm_set1_epi16(ssse3_CBfact);
  __m128i cr_fact = _mm_set1_epi16(ssse3_CRfact);
  for (int c=0; c < samples; c += 8)
    { 
      __m128i green = *((__m128i *)(src2 + c));
      __m128i y = _mm_mulhrs_epi16(green,alpha_g);
      __m128i red = *((__m128i *)(src1 + c));
      __m128i blue = *((__m128i *)(src3 + c));
      y = _mm_add_epi16(y,_mm_mulhrs_epi16(red,alpha_r));
      y = _mm_add_epi16(y,_mm_mulhrs_epi16(blue,alpha_b));
      *((__m128i *)(src1 + c)) = y;
      blue = _mm_sub_epi16(blue,y);
      *((__m128i *)(src2 + c)) = _mm_mulhrs_epi16(blue,cb_fact);
      red = _mm_sub_epi16(red,y);
      *((__m128i *)(src3 + c)) = _mm_mulhrs_epi16(red,cr_fact);
    }
}

/*****************************************************************************/
/* EXTERN                  ssse3_ycc_to_rgb_irrev16                          */
/*****************************************************************************/

void
  ssse3_ycc_to_rgb_irrev16(kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *src3,
                           int samples)
{
  __m128i cr_fact_r = _mm_set1_epi16(ssse3_CRfactR);
  __m128i cr_neg_fact_g = _mm_set1_epi16(ssse3_neg_CRfactG);
  __m128i cb_fact_b = _mm_set1_epi16(ssse3_CBfactB);
  __m128i cb_neg_fact_g = _mm_set1_epi16(ssse3_neg_CBfactG);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i y = *((__m128i *)(src1 + c));
      __m128i tmp = *((__m128i *)(src3 + c)); // Load CR
      __m128i cr = tmp;
      tmp = _mm_mulhrs_epi16(tmp,cr_fact_r);
      tmp = _mm_add_epi16(tmp,cr);
      *((__m128i *)(src1 + c)) = _mm_adds_epi16(tmp,y); // Save Red
      cr = _mm_mulhrs_epi16(cr,cr_neg_fact_g);
      tmp = *((__m128i *)(src2 + c)); // Load CB
      __m128i cb = tmp;
      tmp = _mm_mulhrs_epi16(tmp,cb_fact_b);
      tmp = _mm_add_epi16(tmp,cb);
      *((__m128i *)(src3 + c)) = _mm_adds_epi16(tmp,y); // Save Blue
      cb = _mm_mulhrs_epi16(cb,cb_neg_fact_g);
      y = _mm_adds_epi16(y,cr);
      *((__m128i *)(src2 + c)) = _mm_adds_epi16(y,cb); // Save Green
    }
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_SSSE3
