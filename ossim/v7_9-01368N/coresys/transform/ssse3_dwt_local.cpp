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
   Provides SSSE3-specific DWT accelerators that are selected by the logic
found in "x86_dwt_local.h".  There is no harm in including this source file
with all builds, even if SSSE3 is not supported by the compiler, so long as
KDU_NO_SSSE3 is defined or KDU_X86_INTRINSICS is not defined.
******************************************************************************/
#include "transform_base.h"

#if ((!defined KDU_NO_SSSE3) && (defined KDU_X86_INTRINSICS))

#include <tmmintrin.h>
#include <assert.h>

using namespace kd_core_local;

// The following constants are defined in all DWT accelerator source files
#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

// The factors below are used in SSSE3 implementations of the 9/7 xform where
// the PMULHRSW instruction is available -- it effectively forms the rounded
// product with a 16-bit signed integer factor, divided by 2^15.
static kdu_int16 ssse3_w97_rem[4]; // See `ssse3_dwt_local_static_init'

namespace kd_core_simd {

/* ========================================================================= */
/*                         Safe Static Initializers                          */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 ssse3_dwt_local_static_init                        */
/*****************************************************************************/

void ssse3_dwt_local_static_init()
{ // Static initializers are potentially dangerous, so we initialize here
  kdu_int16 w97_rem[4] =
    {(kdu_int16) floor(0.5 + (W97_FACT_0+1.0)*(double)(1<<15)),
      (kdu_int16) floor(0.5 + W97_FACT_1*(double)(1<<18)),
      (kdu_int16) floor(0.5 + W97_FACT_2*(double)(1<<15)),
      (kdu_int16) floor(0.5 + W97_FACT_3*(double)(1<<15))};
  for (int i=0; i < 4; i++)
    ssse3_w97_rem[i] = w97_rem[i];
}


/* ========================================================================= */
/*                              DWT Functions                                */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  ssse3_vlift_16_9x7_synth                          */
/*****************************************************************************/

void
  ssse3_vlift_16_9x7_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[0]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src1+c));
      val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_add_epi16(tgt,val); // Here is a -1 contribution
      val = _mm_mulhrs_epi16(val,vec_lambda);
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_vlift_16_9x7_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[1]);
  kdu_int16 *src1=src[0], *src2=src[1];
  __m128i roff = _mm_set1_epi16(4);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = *((__m128i *)(src1+c));
      val1 = _mm_mulhrs_epi16(val1,vec_lambda);
      __m128i val2 = *((__m128i *)(src2+c));
      val2 = _mm_mulhrs_epi16(val2,vec_lambda);
      __m128i tgt = *((__m128i *)(dst_in+c));
      val1 = _mm_add_epi16(val1,roff);
      val1 = _mm_add_epi16(val1,val2);
      val1 = _mm_srai_epi16(val1,3);
      tgt = _mm_sub_epi16(tgt,val1);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_vlift_16_9x7_synth_s23(kdu_int16 **src, kdu_int16 *dst_in,
                               kdu_int16 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[step->step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src1+c));
      val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
      __m128i tgt = *((__m128i *)(dst_in+c));
      val = _mm_mulhrs_epi16(val,vec_lambda);
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                 ssse3_vlift_16_9x7_analysis                        */
/*****************************************************************************/

void
  ssse3_vlift_16_9x7_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[0]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src1+c));
      val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_sub_epi16(tgt,val); // Here is a -1 contribution
      val = _mm_mulhrs_epi16(val,vec_lambda);
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_vlift_16_9x7_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[1]);
  kdu_int16 *src1=src[0], *src2=src[1];
  __m128i roff = _mm_set1_epi16(4);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = *((__m128i *)(src1+c));
      val1 = _mm_mulhrs_epi16(val1,vec_lambda);
      __m128i val2 = *((__m128i *)(src2+c));
      val2 = _mm_mulhrs_epi16(val2,vec_lambda);
      __m128i tgt = *((__m128i *)(dst_in+c));
      val1 = _mm_add_epi16(val1,roff);
      val1 = _mm_add_epi16(val1,val2);
      val1 = _mm_srai_epi16(val1,3);
      tgt = _mm_add_epi16(tgt,val1);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_vlift_16_9x7_analysis_s23(kdu_int16 **src, kdu_int16 *dst_in,
                                  kdu_int16 *dst_out, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[step->step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src1+c));
      val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
      __m128i tgt = *((__m128i *)(dst_in+c));
      val = _mm_mulhrs_epi16(val,vec_lambda);
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   ssse3_hlift_16_9x7_synth                         */
/*****************************************************************************/

void
  ssse3_hlift_16_9x7_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[0]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  for (int c=0; c < samples; c+=8)
    { 
      val_u = _mm_add_epi16(val_u,*((__m128i *)(src+c)));
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_add_epi16(tgt,val_u); // Here is a -1 contribution
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda); // Rounded product
      tgt = _mm_sub_epi16(tgt,val_u);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_hlift_16_9x7_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[1]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  __m128i roff = _mm_set1_epi16(4);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src+c));
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda);
      val = _mm_mulhrs_epi16(val,vec_lambda);
      __m128i tgt = *((__m128i *)(dst+c));
      val = _mm_add_epi16(val,roff);
      val = _mm_add_epi16(val,val_u);
      val = _mm_srai_epi16(val,3);
      tgt = _mm_sub_epi16(tgt,val);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }          
}
//-----------------------------------------------------------------------------
void
  ssse3_hlift_16_9x7_synth_s23(kdu_int16 *src, kdu_int16 *dst,
                               int samples, kd_lifting_step *step,
                               bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[step->step_idx]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  for (int c=0; c < samples; c+=8)
    { 
      val_u = _mm_add_epi16(val_u,*((__m128i *)(src+c)));
      __m128i tgt = *((__m128i *)(dst+c));
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda);
      tgt = _mm_sub_epi16(tgt,val_u);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                  ssse3_hlift_16_9x7_analysis                       */
/*****************************************************************************/

void
  ssse3_hlift_16_9x7_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[0]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  for (int c=0; c < samples; c+=8)
    { 
      val_u = _mm_add_epi16(val_u,*((__m128i *)(src+c)));
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_sub_epi16(tgt,val_u); // Here is a -1 contribution
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda); // Rounded product
      tgt = _mm_add_epi16(tgt,val_u);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  ssse3_hlift_16_9x7_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[1]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  __m128i roff = _mm_set1_epi16(4);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = *((__m128i *)(src+c));
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda);
      val = _mm_mulhrs_epi16(val,vec_lambda);
      __m128i tgt = *((__m128i *)(dst+c));
      val = _mm_add_epi16(val,roff);
      val = _mm_add_epi16(val,val_u);
      val = _mm_srai_epi16(val,3);
      tgt = _mm_add_epi16(tgt,val);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }          
}
//-----------------------------------------------------------------------------
void
  ssse3_hlift_16_9x7_analysis_s23(kdu_int16 *src, kdu_int16 *dst,
                                  int samples, kd_lifting_step *step,
                                  bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  __m128i vec_lambda = _mm_set1_epi16(ssse3_w97_rem[step->step_idx]);
  register kdu_int16 *src_u = src+1; // Unaligned pointer
  if (_addr_to_kdu_int32(src) & 0x0F)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      src_u = src; src++;
    }
  __m128i val_u = _mm_loadu_si128((__m128i *)(src_u));
  for (int c=0; c < samples; c+=8)
    { 
      val_u = _mm_add_epi16(val_u,*((__m128i *)(src+c)));
      __m128i tgt = *((__m128i *)(dst+c));
      val_u = _mm_mulhrs_epi16(val_u,vec_lambda);
      tgt = _mm_add_epi16(tgt,val_u);
      val_u = _mm_loadu_si128((__m128i *)(src_u+c+8));
      *((__m128i *)(dst+c)) = tgt;
    }
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_SSSE3
