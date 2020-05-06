/*****************************************************************************/
// File: avx2_dwt_local.cpp [scope = CORESYS/TRANSFORMS]
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
   Provides AVX2-specific DWT accelerators that are selected by the logic
found in "x86_dwt_local.h".  There is no harm in including this source file
with all builds, even if AVX2 is not supported by the compiler, so long as
KDU_NO_AVX2 is defined or KDU_X86_INTRINSICS is not defined.
******************************************************************************/
#include "transform_base.h"

#if ((!defined KDU_NO_AVX2) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER

using namespace kd_core_local;

namespace kd_core_simd {
  
// The following constants are defined in all DWT accelerator source files
#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

// The factors below are used in SSSE3/AVX2 implementations of the 9/7 xform
// where the PMULHRSW instruction is available -- it effectively forms the
// rounded product with a 16-bit signed integer factor, divided by 2^15.
static kdu_int16 ssse3_w97_rem[4]; // See `avx2_dwt_local_static_init'

/* ========================================================================= */
/*                         Safe Static Initializers                          */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 avx2_dwt_local_static_init                         */
/*****************************************************************************/

void avx2_dwt_local_static_init()
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
/*                           Interleave Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                     avx2_interleave_16                             */
/*****************************************************************************/

void avx2_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                        kdu_int16 *dst, int pairs, int upshift)
{
  assert(upshift == 0);
  if (_addr_to_kdu_int32(src1) & 16)
    { // Source addresses are 16-byte aligned, but not 32-byte aligned
      __m128i val1 = *((__m128i *) src1);
      __m128i val2 = *((__m128i *) src2);
      ((__m128i *) dst)[0] = _mm_unpacklo_epi16(val1,val2);
      ((__m128i *) dst)[1] = _mm_unpackhi_epi16(val1,val2);
      src1 += 8; src2 += 8; dst += 16; pairs -= 8;
    }
  __m256i *sp1 = (__m256i *) src1;
  __m256i *sp2 = (__m256i *) src2;
  __m256i *dp = (__m256i *) dst;
  for (; pairs > 8; pairs-=16, sp1++, sp2++, dp+=2)
    { 
      __m256i val1 = *sp1;
      __m256i val2 = *sp2;
      val1 = _mm256_permute4x64_epi64(val1,0xD8);
      val2 = _mm256_permute4x64_epi64(val2,0xD8);
      dp[0] =  _mm256_unpacklo_epi16(val1,val2);
      dp[1] = _mm256_unpackhi_epi16(val1,val2);
    }
  if (pairs > 0)
    { // Need to generate one more group of 16 outputs (8 pairs)
      __m128i val1 = *((__m128i *) sp1);
      __m128i val2 = *((__m128i *) sp2);
      ((__m128i *) dp)[0] = _mm_unpacklo_epi16(val1,val2);
      ((__m128i *) dp)[1] = _mm_unpackhi_epi16(val1,val2);
    }
}

/*****************************************************************************/
/* EXTERN                avx2_upshifted_interleave_16                        */
/*****************************************************************************/

void avx2_upshifted_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                                  kdu_int16 *dst, int pairs, int upshift)
{
  __m128i shift = _mm_cvtsi32_si128(upshift);
  if (_addr_to_kdu_int32(src1) & 16)
    { // Source addresses are 16-byte aligned, but not 32-byte aligned
      __m128i val1 = *((__m128i *) src1);
      val1 = _mm_sll_epi16(val1,shift);
      __m128i val2 = *((__m128i *) src2);
      val2 = _mm_sll_epi16(val2,shift);
      ((__m128i *) dst)[0] = _mm_unpacklo_epi16(val1,val2);
      ((__m128i *) dst)[1] = _mm_unpackhi_epi16(val1,val2);
      src1 += 8; src2 += 8; dst += 16; pairs -= 8;
    }
  __m256i *sp1 = (__m256i *) src1;
  __m256i *sp2 = (__m256i *) src2;
  __m256i *dp = (__m256i *) dst;
  for (; pairs > 8; pairs-=16, sp1++, sp2++, dp+=2)
    { 
      __m256i val1 = *sp1;
      val1 = _mm256_sll_epi16(val1,shift);
      __m256i val2 = *sp2;
      val2 = _mm256_sll_epi16(val2,shift);
      val1 = _mm256_permute4x64_epi64(val1,0xD8);
      val2 = _mm256_permute4x64_epi64(val2,0xD8);
      dp[0] =  _mm256_unpacklo_epi16(val1,val2);
      dp[1] = _mm256_unpackhi_epi16(val1,val2);
    }
  if (pairs > 0)
    { // Need to generate one more group of 16 outputs (8 pairs)
      __m128i val1 = *((__m128i *) sp1);
      val1 = _mm_sll_epi16(val1,shift);
      __m128i val2 = *((__m128i *) sp2);
      val2 = _mm_sll_epi16(val2,shift);
      ((__m128i *) dp)[0] = _mm_unpacklo_epi16(val1,val2);
      ((__m128i *) dp)[1] = _mm_unpackhi_epi16(val1,val2);
    }
}

/*****************************************************************************/
/* EXTERN                     avx2_interleave_32                             */
/*****************************************************************************/

void avx2_interleave_32(kdu_int32 *src1, kdu_int32 *src2,
                        kdu_int32 *dst, int pairs)
{
  if ((_addr_to_kdu_int32(src1) & 16) != 0)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      __m128i val1 = *((__m128i *) src1);
      __m128i val2 = *((__m128i *) src2);
      ((__m128i *) dst)[0] = _mm_unpacklo_epi32(val1,val2);
      ((__m128i *) dst)[1] = _mm_unpackhi_epi32(val1,val2);
      src1 += 4;  src2 += 4; dst += 8; pairs -= 4;
    }
  __m256i *sp1 = (__m256i *) src1;
  __m256i *sp2 = (__m256i *) src2;
  __m256i *dp = (__m256i *) dst;
  for (; pairs > 4; pairs-=8, sp1++, sp2++, dp+=2)
    { 
      __m256i val1 = *sp1;
      __m256i val2 = *sp2;
      val1 = _mm256_permute4x64_epi64(val1,0xD8);
      val2 = _mm256_permute4x64_epi64(val2,0xD8);
      dp[0] = _mm256_unpacklo_epi32(val1,val2);
      dp[1] = _mm256_unpackhi_epi32(val1,val2);
    }
  if (pairs > 0)
    { 
      __m128i val1 = *((__m128i *) sp1);
      __m128i val2 = *((__m128i *) sp2);
      ((__m128i *) dp)[0] = _mm_unpacklo_epi32(val1,val2);
      ((__m128i *) dp)[1] = _mm_unpackhi_epi32(val1,val2);
    }
}

/* ========================================================================= */
/*                          Deinterleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                    avx2_deinterleave_16                            */
/*****************************************************************************/

void avx2_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                          kdu_int16 *dst2, int pairs, int downshift)
{
  assert(downshift == 0);
  __m256i low_mask = _mm256_set1_epi32(0x0000FFFF);
  __m256i *sp = (__m256i *) src;
  __m256i *dp1 = (__m256i *) dst1;
  __m256i *dp2 = (__m256i *) dst2;
  __m256i val1, val2, low1, low2;
  for (; pairs > 0; pairs-=16, sp+=2, dp1++, dp2++)
    { // No need to worry about over-reading `src' by up to 62 bytes
      val1 = sp[0];  val2 = sp[1];
      low1 = _mm256_and_si256(val1,low_mask);
      val1 = _mm256_srli_epi32(val1,16);
      low2 = _mm256_and_si256(val2,low_mask);
      val2 = _mm256_srli_epi32(val2,16);
      *dp1 = _mm256_permute4x64_epi64(_mm256_packus_epi32(low1,low2),0xD8);
      *dp2 = _mm256_permute4x64_epi64(_mm256_packus_epi32(val1,val2),0xD8);
    }
}

/*****************************************************************************/
/* EXTERN              avx2_downshifted_deinterleave_16                      */
/*****************************************************************************/

void avx2_downshifted_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                      kdu_int16 *dst2, int pairs,
                                      int downshift)
{
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m256i vec_offset = _mm256_set1_epi16((kdu_int16)((1<<downshift)>>1));
  __m256i low_mask = _mm256_set1_epi32(0x0000FFFF);
  __m256i *sp = (__m256i *) src;
  __m256i *dp1 = (__m256i *) dst1;
  __m256i *dp2 = (__m256i *) dst2;
  __m256i val1, val2, low1, low2;
  for (; pairs > 0; pairs-=16, sp+=2, dp1++, dp2++)
    { // No need to worry about over-reading `src' by up to 62 bytes
      val1 = sp[0];
      val1 = _mm256_add_epi16(val1,vec_offset);
      val1 = _mm256_sra_epi16(val1,shift);
      val2 = sp[1];
      val2 = _mm256_add_epi16(val2,vec_offset);
      val2 = _mm256_sra_epi16(val2,shift);
      low1 = _mm256_and_si256(val1,low_mask);
      val1 = _mm256_srli_epi32(val1,16);
      low2 = _mm256_and_si256(val2,low_mask);
      val2 = _mm256_srli_epi32(val2,16);
      *dp1 = _mm256_permute4x64_epi64(_mm256_packus_epi32(low1,low2),0xD8);
      *dp2 = _mm256_permute4x64_epi64(_mm256_packus_epi32(val1,val2),0xD8);
    }
}

/*****************************************************************************/
/* EXTERN                    avx2_deinterleave_32                            */
/*****************************************************************************/

void avx2_deinterleave_32(kdu_int32 *src, kdu_int32 *dst1,
                          kdu_int32 *dst2, int pairs)
{
  __m256 *sp = (__m256 *) src;
  __m256 *dp1 = (__m256 *) dst1;
  __m256 *dp2 = (__m256 *) dst2;
  __m256 val1, val2, tmp1, tmp2;
  for (; pairs > 0; pairs-=8, sp+=2, dp1++, dp2++)
    { // No need to worry about over-reading `src' by up to 60 bytes
      tmp1 = sp[0];  tmp2 = sp[1];
      val1 = _mm256_permute2f128_ps(tmp1,tmp2,0x20);
      val2 = _mm256_permute2f128_ps(tmp1,tmp2,0x31);
      *dp1 = _mm256_shuffle_ps(val1,val2,0x88);
      *dp2 = _mm256_shuffle_ps(val1,val2,0xDD);
    }
}


/* ========================================================================= */
/*                  Vertical Lifting Step Functions (16-bit)                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_vlift_16_9x7_synth                          */
/*****************************************************************************/

void
  avx2_vlift_16_9x7_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                             kdu_int16 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[0]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = *((__m256i *)(src1+c));
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_add_epi16(tgt,val); // Here is a -1 contribution
      val = _mm256_mulhrs_epi16(val,vec_lambda);
      tgt = _mm256_sub_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_9x7_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                             kdu_int16 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[1]);
  kdu_int16 *src1=src[0], *src2=src[1];
  __m256i roff = _mm256_set1_epi16(4);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(src1+c));
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i val2 = *((__m256i *)(src2+c));
      val2 = _mm256_mulhrs_epi16(val2,vec_lambda);
      __m256i tgt = *((__m256i *)(dst_in+c));
      val1 = _mm256_add_epi16(val1,roff);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,3);
      tgt = _mm256_sub_epi16(tgt,val1);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_9x7_synth_s23(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[step->step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = *((__m256i *)(src1+c));
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val = _mm256_mulhrs_epi16(val,vec_lambda);
      tgt = _mm256_sub_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                  avx2_vlift_16_9x7_analysis                        */
/*****************************************************************************/

void
  avx2_vlift_16_9x7_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                kdu_int16 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[0]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = *((__m256i *)(src1+c));
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_sub_epi16(tgt,val); // Here is a -1 contribution
      val = _mm256_mulhrs_epi16(val,vec_lambda);
      tgt = _mm256_add_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_9x7_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                kdu_int16 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[1]);
  kdu_int16 *src1=src[0], *src2=src[1];
  __m256i roff = _mm256_set1_epi16(4);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(src1+c));
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i val2 = *((__m256i *)(src2+c));
      val2 = _mm256_mulhrs_epi16(val2,vec_lambda);
      __m256i tgt = *((__m256i *)(dst_in+c));
      val1 = _mm256_add_epi16(val1,roff);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,3);
      tgt = _mm256_add_epi16(tgt,val1);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_9x7_analysis_s23(kdu_int16 **src, kdu_int16 *dst_in,
                                 kdu_int16 *dst_out, int samples,
                                 kd_lifting_step *step, bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[step->step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = *((__m256i *)(src1+c));
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val = _mm256_mulhrs_epi16(val,vec_lambda);
      tgt = _mm256_add_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_vlift_16_2tap_synth                         */
/*****************************************************************************/

void
  avx2_vlift_16_2tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
                           kdu_int16 *dst_out, int samples,
                           kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  kdu_int16 *sp1=src[0];
  kdu_int16 *sp2=sp1; // In case we only have 1 tap
  if (step->support_length == 2)
    { lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
      sp2 = src[1]; }
  __m256i vec_lambda = _mm256_set1_epi32(lambda_coeffs);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(sp1+c));
      __m256i val2 = *((__m256i *)(sp2+c));
      __m256i high = _mm256_unpackhi_epi16(val1,val2);
      __m256i low = _mm256_unpacklo_epi16(val1,val2);
      high = _mm256_madd_epi16(high,vec_lambda);
      high = _mm256_add_epi32(high,vec_offset);
      high = _mm256_sra_epi32(high,downshift);
      low = _mm256_madd_epi16(low,vec_lambda);
      low = _mm256_add_epi32(low,vec_offset);
      low = _mm256_sra_epi32(low,downshift);
      __m256i tgt = *((__m256i *)(dst_in+c));
      __m256i subtend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst_out+c)) = _mm256_sub_epi16(tgt,subtend);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_vlift_16_2tap_analysis                        */
/*****************************************************************************/

void
  avx2_vlift_16_2tap_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only  
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  kdu_int16 *sp1=src[0];
  kdu_int16 *sp2=sp1; // In case we only have 1 tap
  if (step->support_length == 2)
    { lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
      sp2 = src[1]; }
  
  __m256i vec_lambda = _mm256_set1_epi32(lambda_coeffs);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(sp1+c));
      __m256i val2 = *((__m256i *)(sp2+c));
      __m256i high = _mm256_unpackhi_epi16(val1,val2);
      __m256i low = _mm256_unpacklo_epi16(val1,val2);
      high = _mm256_madd_epi16(high,vec_lambda);
      high = _mm256_add_epi32(high,vec_offset);
      high = _mm256_sra_epi32(high,downshift);
      low = _mm256_madd_epi16(low,vec_lambda);
      low = _mm256_add_epi32(low,vec_offset);
      low = _mm256_sra_epi32(low,downshift);
      __m256i tgt = *((__m256i *)(dst_in+c));
      __m256i addend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst_out+c)) = _mm256_add_epi16(tgt,addend);
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_vlift_16_4tap_synth                         */
/*****************************************************************************/

void
  avx2_vlift_16_4tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
                           kdu_int16 *dst_out, int samples,
                           kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeffs0 = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  lambda_coeffs0 |= ((kdu_int32) step->icoeffs[1]) << 16;
  kdu_int32 lambda_coeffs2 = ((kdu_int32) step->icoeffs[2]) & 0x0000FFFF;
  kdu_int16 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int16 *src4=src3; // In case we only have 3 taps
  if (step->support_length == 4)
    { lambda_coeffs2 |= ((kdu_int32) step->icoeffs[3]) << 16;
      src4 = src[3]; }
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeffs0);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeffs2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(src1+c));
      __m256i val2 = *((__m256i *)(src2+c));
      __m256i high0 = _mm256_unpackhi_epi16(val1,val2);
      __m256i low0 = _mm256_unpacklo_epi16(val1,val2);
      high0 = _mm256_madd_epi16(high0,vec_lambda0);
      low0 = _mm256_madd_epi16(low0,vec_lambda0);
      __m256i val3 = *((__m256i *)(src3+c));
      __m256i val4 = *((__m256i *)(src4+c));
      __m256i high1 = _mm256_unpackhi_epi16(val3,val4);
      __m256i low1 = _mm256_unpacklo_epi16(val3,val4);
      high1 = _mm256_madd_epi16(high1,vec_lambda2);
      low1 = _mm256_madd_epi16(low1,vec_lambda2);
      
      __m256i high = _mm256_add_epi32(high0,high1);
      high = _mm256_add_epi32(high,vec_offset); // Add rounding offset
      high = _mm256_sra_epi32(high,downshift);
      __m256i low = _mm256_add_epi32(low0,low1);
      low = _mm256_add_epi32(low,vec_offset); // Add rounding offset
      low = _mm256_sra_epi32(low,downshift);
      
      __m256i tgt = *((__m256i *)(dst_in+c));
      __m256i subtend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst_out+c)) = _mm256_sub_epi16(tgt,subtend);
    }
}

/*****************************************************************************/
/* EXTERN                  avx2_vlift_16_4tap_analysis                       */
/*****************************************************************************/

void
  avx2_vlift_16_4tap_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(!for_synthesis); // This implementation does analysis only
  
  kdu_int32 lambda_coeffs0 = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  lambda_coeffs0 |= ((kdu_int32) step->icoeffs[1]) << 16;
  kdu_int32 lambda_coeffs2 = ((kdu_int32) step->icoeffs[2]) & 0x0000FFFF;
  kdu_int16 *src1=src[0], *src2=src[1], *src3=src[2];
  kdu_int16 *src4=src3; // In case we only have 3 taps
  if (step->support_length == 4)
    { lambda_coeffs2 |= ((kdu_int32) step->icoeffs[3]) << 16;
      src4 = src[3]; }
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeffs0);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeffs2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = *((__m256i *)(src1+c));
      __m256i val2 = *((__m256i *)(src2+c));
      __m256i high0 = _mm256_unpackhi_epi16(val1,val2);
      __m256i low0 = _mm256_unpacklo_epi16(val1,val2);
      high0 = _mm256_madd_epi16(high0,vec_lambda0);
      low0 = _mm256_madd_epi16(low0,vec_lambda0);
      __m256i val3 = *((__m256i *)(src3+c));
      __m256i val4 = *((__m256i *)(src4+c));
      __m256i high1 = _mm256_unpackhi_epi16(val3,val4);
      __m256i low1 = _mm256_unpacklo_epi16(val3,val4);
      high1 = _mm256_madd_epi16(high1,vec_lambda2);
      low1 = _mm256_madd_epi16(low1,vec_lambda2);
      
      __m256i high = _mm256_add_epi32(high0,high1);
      high = _mm256_add_epi32(high,vec_offset); // Add rounding offset
      high = _mm256_sra_epi32(high,downshift);
      __m256i low = _mm256_add_epi32(low0,low1);
      low = _mm256_add_epi32(low,vec_offset); // Add rounding offset
      low = _mm256_sra_epi32(low,downshift);
      
      __m256i tgt = *((__m256i *)(dst_in+c));
      __m256i addend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst_out+c)) = _mm256_add_epi16(tgt,addend);
    }
}

/*****************************************************************************/
/* EXTERN                    avx2_vlift_16_5x3_synth                         */
/*****************************************************************************/

void
  avx2_vlift_16_5x3_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                             kdu_int16 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_sub_epi16(val,*((__m256i *)(src1+c))); // Subtract src 1
      val = _mm256_sub_epi16(val,*((__m256i *)(src2+c))); // Subtract src 2
      val = _mm256_srai_epi16(val,1);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_sub_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_5x3_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                             kdu_int16 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_add_epi16(val,*((__m256i *)(src1+c))); // Add source 1
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c))); // Add source 2
      val = _mm256_srai_epi16(val,2);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_sub_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                  avx2_vlift_16_5x3_analysis                        */
/*****************************************************************************/

void
  avx2_vlift_16_5x3_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                kdu_int16 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_sub_epi16(val,*((__m256i *)(src1+c))); // Subtract src 1
      val = _mm256_sub_epi16(val,*((__m256i *)(src2+c))); // Subtract src 2
      val = _mm256_srai_epi16(val,1);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_add_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_16_5x3_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                kdu_int16 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_add_epi16(val,*((__m256i *)(src1+c))); // Add source 1
      val = _mm256_add_epi16(val,*((__m256i *)(src2+c))); // Add source 2
      val = _mm256_srai_epi16(val,2);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_add_epi16(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}


/* ========================================================================= */
/*                  Vertical Lifting Step Functions (32-bit)                 */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_vlift_32_2tap_irrev                         */
/*****************************************************************************/

void
  avx2_vlift_32_2tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
                           kdu_int32 *dst_out, int samples,
                           kd_lifting_step *step, bool for_synthesis)
 /* Does either analysis or synthesis, working with floating point sample
    values.  The 32-bit integer types on the supplied buffers are only for
    simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length == 1) || (step->support_length == 2));
  float lambda0 = step->coeffs[0];
  float lambda1 = 0.0F;
  float *sp0 = (float *) src[0];
  float *sp1 = sp0; // In case we have only 1 tap
  if (step->support_length == 2)
    { lambda1 = step->coeffs[1]; sp1 = (float *) src[1]; }
  float *dp_in = (float *) dst_in;
  float *dp_out = (float *) dst_out;
  __m256 vec_lambda0, vec_lambda1;
  if (for_synthesis)
    { vec_lambda0=_mm256_set1_ps(-lambda0);
      vec_lambda1=_mm256_set1_ps(-lambda1); }
  else
    { vec_lambda0=_mm256_set1_ps( lambda0);
      vec_lambda1=_mm256_set1_ps( lambda1); }
  for (int c=0; c < samples; c+=8)
    { 
      __m256 tgt = *((__m256 *)(dp_in+c));
      __m256 val0 = *((__m256 *)(sp0+c));
      __m256 val1 = *((__m256 *)(sp1+c));
      tgt = _mm256_fmadd_ps(val0,vec_lambda0,tgt);
      tgt = _mm256_fmadd_ps(val1,vec_lambda1,tgt);
      *((__m256 *)(dp_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_vlift_32_4tap_irrev                         */
/*****************************************************************************/

void
  avx2_vlift_32_4tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
                           kdu_int32 *dst_out, int samples,
                           kd_lifting_step *step, bool for_synthesis)
 /* Does either analysis or synthesis, working with floating point sample
    values.  The 32-bit integer types on the supplied buffers are only for
    simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  float lambda0 = step->coeffs[0];
  float lambda1 = step->coeffs[1];
  float lambda2 = step->coeffs[2];
  float lambda3 = 0.0F;
  float *sp0 = (float *) src[0];
  float *sp1 = (float *) src[1];
  float *sp2 = (float *) src[2];
  float *sp3 = sp2; // In case we only have 3 taps
  if (step->support_length==4)
    { lambda3 = step->coeffs[3]; sp3 = (float *) src[3]; }
  float *dp_in = (float *) dst_in;
  float *dp_out = (float *) dst_out;
  __m256 vec_lambda0, vec_lambda1, vec_lambda2, vec_lambda3;
  if (for_synthesis)
    { vec_lambda0=_mm256_set1_ps(-lambda0);
      vec_lambda1=_mm256_set1_ps(-lambda1);
      vec_lambda2=_mm256_set1_ps(-lambda2);
      vec_lambda3=_mm256_set1_ps(-lambda3); }
  else
    { vec_lambda0=_mm256_set1_ps( lambda0);
      vec_lambda1=_mm256_set1_ps( lambda1);
      vec_lambda2=_mm256_set1_ps( lambda2);
      vec_lambda3=_mm256_set1_ps( lambda3); }
  for (int c=0; c < samples; c+=8)
    { 
      __m256 tgt = *((__m256 *)(dp_in+c));
      __m256 val0 = *((__m256 *)(sp0+c));
      __m256 val1 = *((__m256 *)(sp1+c));
      __m256 val2 = *((__m256 *)(sp2+c));
      __m256 val3 = *((__m256 *)(sp3+c));
      tgt = _mm256_fmadd_ps(val0,vec_lambda0,tgt);
      tgt = _mm256_fmadd_ps(val1,vec_lambda1,tgt);
      tgt = _mm256_fmadd_ps(val2,vec_lambda2,tgt);
      tgt = _mm256_fmadd_ps(val3,vec_lambda3,tgt);
      *((__m256 *)(dp_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_vlift_32_5x3_synth                          */
/*****************************************************************************/

void
  avx2_vlift_32_5x3_synth_s0(kdu_int32 **src, kdu_int32 *dst_in,
                             kdu_int32 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_sub_epi32(val,*((__m256i *)(src1+c))); // Subtract src 1
      val = _mm256_sub_epi32(val,*((__m256i *)(src2+c))); // Subtract src 2
      val = _mm256_srai_epi32(val,1);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_sub_epi32(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_32_5x3_synth_s1(kdu_int32 **src, kdu_int32 *dst_in,
                             kdu_int32 *dst_out, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_add_epi32(val,*((__m256i *)(src1+c))); // Add source 1
      val = _mm256_add_epi32(val,*((__m256i *)(src2+c))); // Add source 2
      val = _mm256_srai_epi32(val,2);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_sub_epi32(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_vlift_32_5x3_analysis                         */
/*****************************************************************************/

void
  avx2_vlift_32_5x3_analysis_s0(kdu_int32 **src, kdu_int32 *dst_in,
                                kdu_int32 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_sub_epi32(val,*((__m256i *)(src1+c))); // Subtract src 1
      val = _mm256_sub_epi32(val,*((__m256i *)(src2+c))); // Subtract src 2
      val = _mm256_srai_epi32(val,1);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_add_epi32(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_vlift_32_5x3_analysis_s1(kdu_int32 **src, kdu_int32 *dst_in,
                                kdu_int32 *dst_out, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val = vec_offset;  // Start with the offset
      val = _mm256_add_epi32(val,*((__m256i *)(src1+c))); // Add source 1
      val = _mm256_add_epi32(val,*((__m256i *)(src2+c))); // Add source 2
      val = _mm256_srai_epi32(val,2);
      __m256i tgt = *((__m256i *)(dst_in+c));
      tgt = _mm256_add_epi32(tgt,val);
      *((__m256i *)(dst_out+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_vlift_32_2tap_rev_synth                       */
/*****************************************************************************/

void
  avx2_vlift_32_2tap_rev_synth(kdu_int32 **src, kdu_int32 *dst_in,
                               kdu_int32 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = 0;
  kdu_int32 *sp1=src[0];
  kdu_int32 *sp2=sp1; // In case we only have 1 tap
  if (step->support_length == 2)
    { lambda_coeff1 = (kdu_int32) step->icoeffs[1]; sp2 = src[1]; }
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_mul_epi32(vec_lambda0,*((__m256i *)(sp1+c)));
      __m256i val1 = _mm256_mul_epi32(vec_lambda1,*((__m256i *)(sp2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst_out+c)) = _mm256_sub_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN                avx2_vlift_32_2tap_rev_analysis                     */
/*****************************************************************************/

void
  avx2_vlift_32_2tap_rev_analysis(kdu_int32 **src, kdu_int32 *dst_in,
                                  kdu_int32 *dst_out, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only
  
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = 0;
  kdu_int32 *sp1=src[0];
  kdu_int32 *sp2=sp1; // In case we only have 1 tap
  if (step->support_length == 2)
    { lambda_coeff1 = (kdu_int32) step->icoeffs[1]; sp2 = src[1]; }
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_mul_epi32(vec_lambda0,*((__m256i *)(sp1+c)));
      __m256i val1 = _mm256_mul_epi32(vec_lambda1,*((__m256i *)(sp2+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst_out+c)) = _mm256_add_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_vlift_32_4tap_rev_synth                       */
/*****************************************************************************/

void
  avx2_vlift_32_4tap_rev_synth(kdu_int32 **src, kdu_int32 *dst_in,
                               kdu_int32 *dst_out, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  kdu_int32 lambda_coeff2 = (kdu_int32) step->icoeffs[2];
  kdu_int32 lambda_coeff3 = 0;
  kdu_int32 *sp0=src[0], *sp1=src[1], *sp2=src[2];
  kdu_int32 *sp3=sp2; // In case 3 taps
  if (step->support_length == 4)
    { lambda_coeff3 = (kdu_int32) step->icoeffs[3]; sp3 = src[3]; }
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeff2);
  __m256i vec_lambda3 = _mm256_set1_epi32(lambda_coeff3);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_mul_epi32(vec_lambda0,*((__m256i *)(sp0+c)));
      __m256i val1 = _mm256_mul_epi32(vec_lambda1,*((__m256i *)(sp1+c)));
      __m256i val2 = _mm256_mul_epi32(vec_lambda2,*((__m256i *)(sp2+c)));
      __m256i val3 = _mm256_mul_epi32(vec_lambda3,*((__m256i *)(sp3+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_add_epi32(val0,val2);
      val0 = _mm256_add_epi32(val0,val3);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst_out+c)) = _mm256_sub_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN               avx2_vlift_32_4tap_rev_analysis                      */
/*****************************************************************************/

void
  avx2_vlift_32_4tap_rev_analysis(kdu_int32 **src, kdu_int32 *dst_in,
                                  kdu_int32 *dst_out, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(!for_synthesis); // This implementation does analysis only
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  kdu_int32 lambda_coeff2 = (kdu_int32) step->icoeffs[2];
  kdu_int32 lambda_coeff3 = 0;
  kdu_int32 *sp0=src[0], *sp1=src[1], *sp2=src[2];
  kdu_int32 *sp3=sp2; // In case 3 taps
  if (step->support_length == 4)
    { lambda_coeff3 = (kdu_int32) step->icoeffs[3]; sp3 = src[3]; }
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeff2);
  __m256i vec_lambda3 = _mm256_set1_epi32(lambda_coeff3);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_mul_epi32(vec_lambda0,*((__m256i *)(sp0+c)));
      __m256i val1 = _mm256_mul_epi32(vec_lambda1,*((__m256i *)(sp1+c)));
      __m256i val2 = _mm256_mul_epi32(vec_lambda2,*((__m256i *)(sp2+c)));
      __m256i val3 = _mm256_mul_epi32(vec_lambda3,*((__m256i *)(sp3+c)));
      __m256i tgt = *((__m256i *)(dst_in+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_add_epi32(val0,val2);
      val0 = _mm256_add_epi32(val0,val3);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst_out+c)) = _mm256_add_epi32(tgt,val0);
    }
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (16-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_hlift_16_9x7_synth                          */
/*****************************************************************************/

void
  avx2_hlift_16_9x7_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[0]);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,val2);
      val2 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi16(tgt,val1); // Here is a -1 contribution
      tgt = _mm256_sub_epi16(tgt,val2);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_9x7_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[1]);
  __m256i roff = _mm256_set1_epi16(4);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      val2 = _mm256_mulhrs_epi16(val2,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      val1 = _mm256_add_epi16(val1,roff);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,3);
      tgt = _mm256_sub_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_9x7_synth_s23(kdu_int16 *src, kdu_int16 *dst,
                              int samples, kd_lifting_step *step,
                              bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[step->step_idx]);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_hlift_16_9x7_analysis                         */
/*****************************************************************************/

void
  avx2_hlift_16_9x7_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[0]);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,val2);
      val2 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi16(tgt,val1); // Here is a -1 contribution
      tgt = _mm256_add_epi16(tgt,val2);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_9x7_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[1]);
  __m256i roff = _mm256_set1_epi16(4);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      val2 = _mm256_mulhrs_epi16(val2,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      val1 = _mm256_add_epi16(val1,roff);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,3);
      tgt = _mm256_add_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_9x7_analysis_s23(kdu_int16 *src, kdu_int16 *dst,
                                 int samples, kd_lifting_step *step,
                                 bool for_synthesis)
{
  assert(((step->step_idx == 2) || (step->step_idx == 3)) && !for_synthesis);
  __m256i vec_lambda = _mm256_set1_epi16(ssse3_w97_rem[step->step_idx]);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_mulhrs_epi16(val1,vec_lambda);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_hlift_16_2tap_synth                         */
/*****************************************************************************/

void
  avx2_hlift_16_2tap_synth(kdu_int16 *src, kdu_int16 *dst, int samples,
                           kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  if (step->support_length == 2)
    lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
  
  __m256i vec_lambda = _mm256_set1_epi32(lambda_coeffs);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i high = _mm256_unpackhi_epi16(val1,val2);
      __m256i low = _mm256_unpacklo_epi16(val1,val2);
      high = _mm256_madd_epi16(high,vec_lambda);
      high = _mm256_add_epi32(high,vec_offset);
      high = _mm256_sra_epi32(high,downshift);
      low = _mm256_madd_epi16(low,vec_lambda);
      low = _mm256_add_epi32(low,vec_offset);
      low = _mm256_sra_epi32(low,downshift);
      __m256i tgt = *((__m256i *)(dst+c));
      __m256i subtend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst+c)) = _mm256_sub_epi16(tgt,subtend);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_hlift_16_2tap_analysis                        */
/*****************************************************************************/

void
  avx2_hlift_16_2tap_analysis(kdu_int16 *src, kdu_int16 *dst, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only
  
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  if (step->support_length == 2)
    lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
  
  __m256i vec_lambda = _mm256_set1_epi32(lambda_coeffs);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i high = _mm256_unpackhi_epi16(val1,val2);
      __m256i low = _mm256_unpacklo_epi16(val1,val2);
      high = _mm256_madd_epi16(high,vec_lambda);
      high = _mm256_add_epi32(high,vec_offset);
      high = _mm256_sra_epi32(high,downshift);
      low = _mm256_madd_epi16(low,vec_lambda);
      low = _mm256_add_epi32(low,vec_offset);
      low = _mm256_sra_epi32(low,downshift);
      __m256i tgt = *((__m256i *)(dst+c));
      __m256i addend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst+c)) = _mm256_add_epi16(tgt,addend);
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_hlift_16_4tap_synth                         */
/*****************************************************************************/

void
  avx2_hlift_16_4tap_synth(kdu_int16 *src, kdu_int16 *dst, int samples,
                           kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeffs0 = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  lambda_coeffs0 |= ((kdu_int32) step->icoeffs[1]) << 16;
  kdu_int32 lambda_coeffs2 = ((kdu_int32) step->icoeffs[2]) & 0x0000FFFF;
  if (step->support_length == 4)
    lambda_coeffs2 |= ((kdu_int32) step->icoeffs[3]) << 16;
  
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeffs0);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeffs2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i high0 = _mm256_unpackhi_epi16(val1,val2);
      __m256i low0 = _mm256_unpacklo_epi16(val1,val2);
      high0 = _mm256_madd_epi16(high0,vec_lambda0);
      low0 = _mm256_madd_epi16(low0,vec_lambda0);
      __m256i val3 = _mm256_loadu_si256((__m256i *)(src+c+2));
      __m256i val4 = _mm256_loadu_si256((__m256i *)(src+c+3));
      __m256i high1 = _mm256_unpackhi_epi16(val3,val4);
      __m256i low1 = _mm256_unpacklo_epi16(val3,val4);
      high1 = _mm256_madd_epi16(high1,vec_lambda2);
      low1 = _mm256_madd_epi16(low1,vec_lambda2);
      
      __m256i high = _mm256_add_epi32(high0,high1);
      high = _mm256_add_epi32(high,vec_offset); // Add rounding offset
      high = _mm256_sra_epi32(high,downshift);
      __m256i low = _mm256_add_epi32(low0,low1);
      low = _mm256_add_epi32(low,vec_offset); // Add rounding offset
      low = _mm256_sra_epi32(low,downshift);
      
      __m256i tgt = *((__m256i *)(dst+c));
      __m256i subtend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst+c)) = _mm256_sub_epi16(tgt,subtend);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_hlift_16_4tap_analysis                        */
/*****************************************************************************/

void
  avx2_hlift_16_4tap_analysis(kdu_int16 *src, kdu_int16 *dst, int samples,
                              kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(!for_synthesis); // This implementation does analysis only
  
  kdu_int32 lambda_coeffs0 = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  lambda_coeffs0 |= ((kdu_int32) step->icoeffs[1]) << 16;
  kdu_int32 lambda_coeffs2 = ((kdu_int32) step->icoeffs[2]) & 0x0000FFFF;
  if (step->support_length == 4)
    lambda_coeffs2 |= ((kdu_int32) step->icoeffs[3]) << 16;
  
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeffs0);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeffs2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i high0 = _mm256_unpackhi_epi16(val1,val2);
      __m256i low0 = _mm256_unpacklo_epi16(val1,val2);
      high0 = _mm256_madd_epi16(high0,vec_lambda0);
      low0 = _mm256_madd_epi16(low0,vec_lambda0);
      __m256i val3 = _mm256_loadu_si256((__m256i *)(src+c+2));
      __m256i val4 = _mm256_loadu_si256((__m256i *)(src+c+3));
      __m256i high1 = _mm256_unpackhi_epi16(val3,val4);
      __m256i low1 = _mm256_unpacklo_epi16(val3,val4);
      high1 = _mm256_madd_epi16(high1,vec_lambda2);
      low1 = _mm256_madd_epi16(low1,vec_lambda2);
      
      __m256i high = _mm256_add_epi32(high0,high1);
      high = _mm256_add_epi32(high,vec_offset); // Add rounding offset
      high = _mm256_sra_epi32(high,downshift);
      __m256i low = _mm256_add_epi32(low0,low1);
      low = _mm256_add_epi32(low,vec_offset); // Add rounding offset
      low = _mm256_sra_epi32(low,downshift);
      
      __m256i tgt = *((__m256i *)(dst+c));
      __m256i addend = _mm256_packs_epi32(low,high);
      *((__m256i *)(dst+c)) = _mm256_add_epi16(tgt,addend);
    }
}

/*****************************************************************************/
/* EXTERN                    avx2_hlift_16_5x3_synth                         */
/*****************************************************************************/

void
  avx2_hlift_16_5x3_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_sub_epi16(vec_offset,val1);
      val1 = _mm256_sub_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,1);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_5x3_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,vec_offset);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,2);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_hlift_16_5x3_analysis                       */
/*****************************************************************************/

void
  avx2_hlift_16_5x3_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_sub_epi16(vec_offset,val1);
      val1 = _mm256_sub_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,1);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_16_5x3_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=16)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi16(val1,vec_offset);
      val1 = _mm256_add_epi16(val1,val2);
      val1 = _mm256_srai_epi16(val1,2);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi16(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (32-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   avx2_hlift_32_2tap_irrev                         */
/*****************************************************************************/

void
  avx2_hlift_32_2tap_irrev(kdu_int32 *src, kdu_int32 *dst, int samples,
                           kd_lifting_step *step, bool for_synthesis)
 /* Does either analysis or synthesis, working with floating point sample
    values.  The 32-bit integer types on the supplied buffers are only for
    simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length == 1) || (step->support_length == 2));
  float lambda0 = step->coeffs[0];
  float lambda1 = (step->support_length==2)?(step->coeffs[1]):0.0F;
  __m256 vec_lambda0, vec_lambda1;
  if (for_synthesis)
    { vec_lambda0=_mm256_set1_ps(-lambda0);
      vec_lambda1=_mm256_set1_ps(-lambda1); }
  else
    { vec_lambda0=_mm256_set1_ps( lambda0);
      vec_lambda1=_mm256_set1_ps( lambda1); }
  float *sp = (float *) src;
  for (int c=0; c < samples; c+=8)
    { 
      __m256 tgt = *((__m256 *)(dst+c));
      __m256 val0 = _mm256_loadu_ps(sp+c);
      __m256 val1 = _mm256_loadu_ps(sp+c+1);
      tgt = _mm256_fmadd_ps(val0,vec_lambda0,tgt);
      tgt = _mm256_fmadd_ps(val1,vec_lambda1,tgt);
      *((__m256 *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_hlift_32_4tap_irrev                         */
/*****************************************************************************/

void
  avx2_hlift_32_4tap_irrev(kdu_int32 *src, kdu_int32 *dst, int samples,
                           kd_lifting_step *step, bool for_synthesis)
 /* Does either analysis or synthesis, working with floating point sample
    values.  The 32-bit integer types on the supplied buffers are only for
    simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  float lambda0 = step->coeffs[0];
  float lambda1 = step->coeffs[1];
  float lambda2 = step->coeffs[2];
  float lambda3 = (step->support_length==4)?(step->coeffs[3]):0.0F;
  __m256 vec_lambda0, vec_lambda1, vec_lambda2, vec_lambda3;
  if (for_synthesis)
    { vec_lambda0=_mm256_set1_ps(-lambda0);
      vec_lambda1=_mm256_set1_ps(-lambda1);
      vec_lambda2=_mm256_set1_ps(-lambda2);
      vec_lambda3=_mm256_set1_ps(-lambda3); }
  else
    { vec_lambda0=_mm256_set1_ps( lambda0);
      vec_lambda1=_mm256_set1_ps( lambda1);
      vec_lambda2=_mm256_set1_ps( lambda2);
      vec_lambda3=_mm256_set1_ps( lambda3); }
  float *sp = (float *) src;
  for (int c=0; c < samples; c+=8)
    { 
      __m256 tgt = *((__m256 *)(dst+c));
      __m256 val0 = _mm256_loadu_ps(sp+c);
      __m256 val1 = _mm256_loadu_ps(sp+c+1);
      __m256 val2 = _mm256_loadu_ps(sp+c+2);
      __m256 val3 = _mm256_loadu_ps(sp+c+3);
      tgt = _mm256_fmadd_ps(val0,vec_lambda0,tgt);
      tgt = _mm256_fmadd_ps(val1,vec_lambda1,tgt);
      tgt = _mm256_fmadd_ps(val2,vec_lambda2,tgt);
      tgt = _mm256_fmadd_ps(val3,vec_lambda3,tgt);
      *((__m256 *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_hlift_32_5x3_synth                          */
/*****************************************************************************/

void
  avx2_hlift_32_5x3_synth_s0(kdu_int32 *src, kdu_int32 *dst, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_sub_epi32(vec_offset,val1);
      val1 = _mm256_sub_epi32(val1,val2);
      val1 = _mm256_srai_epi32(val1,1);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi32(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_32_5x3_synth_s1(kdu_int32 *src, kdu_int32 *dst, int samples,
                             kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi32(val1,vec_offset);
      val1 = _mm256_add_epi32(val1,val2);
      val1 = _mm256_srai_epi32(val1,2);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_sub_epi32(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                  avx2_hlift_32_5x3_analysis                        */
/*****************************************************************************/

void
  avx2_hlift_32_5x3_analysis_s0(kdu_int32 *src, kdu_int32 *dst, int samples,
                                kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  assert(step->icoeffs[0] == -1);
  assert(step->downshift == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_sub_epi32(vec_offset,val1);
      val1 = _mm256_sub_epi32(val1,val2);
      val1 = _mm256_srai_epi32(val1,1);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi32(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
void
  avx2_hlift_32_5x3_analysis_s1(kdu_int32 *src, kdu_int32 *dst, int samples,
                           kd_lifting_step *step, bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m256i vec_offset= _mm256_set1_epi32((1<<step->downshift)>>1);
  assert(step->icoeffs[0] == 1);
  assert(step->downshift == 2);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val1 = _mm256_add_epi32(val1,vec_offset);
      val1 = _mm256_add_epi32(val1,val2);
      val1 = _mm256_srai_epi32(val1,2);
      __m256i tgt = *((__m256i *)(dst+c));
      tgt = _mm256_add_epi32(tgt,val1);
      *((__m256i *)(dst+c)) = tgt;
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_hlift_32_2tap_rev_synth                       */
/*****************************************************************************/

void
  avx2_hlift_32_2tap_rev_synth(kdu_int32 *src, kdu_int32 *dst, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = 0;
  if (step->support_length == 2)
    lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val0 = _mm256_mul_epi32(val0,vec_lambda0);
      val1 = _mm256_mul_epi32(val1,vec_lambda1);
      __m256i tgt = *((__m256i *)(dst+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst+c)) = _mm256_sub_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN               avx2_hlift_32_2tap_rev_analysis                      */
/*****************************************************************************/

void
  avx2_hlift_32_2tap_rev_analysis(kdu_int32 *src, kdu_int32 *dst, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(!for_synthesis); // This implementation does analysis only
  
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = 0;
  if (step->support_length == 2)
    lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c+1));
      val0 = _mm256_mul_epi32(val0,vec_lambda0);
      val1 = _mm256_mul_epi32(val1,vec_lambda1);
      __m256i tgt = *((__m256i *)(dst+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst+c)) = _mm256_add_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN                 avx2_hlift_32_4tap_rev_synth                       */
/*****************************************************************************/

void
  avx2_hlift_32_4tap_rev_synth(kdu_int32 *src, kdu_int32 *dst, int samples,
                               kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  kdu_int32 lambda_coeff2 = (kdu_int32) step->icoeffs[2];
  kdu_int32 lambda_coeff3 = 0;
  if (step->support_length == 4)
    lambda_coeff3 = (kdu_int32) step->icoeffs[3];
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeff2);
  __m256i vec_lambda3 = _mm256_set1_epi32(lambda_coeff3);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+2));
      __m256i val3 = _mm256_loadu_si256((__m256i *)(src+c+3));
      val0 = _mm256_mul_epi32(val0,vec_lambda0);
      val1 = _mm256_mul_epi32(val1,vec_lambda1);
      val2 = _mm256_mul_epi32(val2,vec_lambda2);
      val3 = _mm256_mul_epi32(val3,vec_lambda3);
      __m256i tgt = *((__m256i *)(dst+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_add_epi32(val0,val2);
      val0 = _mm256_add_epi32(val0,val3);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst+c)) = _mm256_sub_epi32(tgt,val0);
    }
}

/*****************************************************************************/
/* EXTERN                avx2_hlift_32_4tap_rev_analysis                     */
/*****************************************************************************/

void
  avx2_hlift_32_4tap_rev_analysis(kdu_int32 *src, kdu_int32 *dst, int samples,
                                  kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  kdu_int32 lambda_coeff0 = (kdu_int32) step->icoeffs[0];
  kdu_int32 lambda_coeff1 = (kdu_int32) step->icoeffs[1];
  kdu_int32 lambda_coeff2 = (kdu_int32) step->icoeffs[2];
  kdu_int32 lambda_coeff3 = 0;
  if (step->support_length == 4)
    lambda_coeff3 = (kdu_int32) step->icoeffs[3];
  __m256i vec_lambda0 = _mm256_set1_epi32(lambda_coeff0);
  __m256i vec_lambda1 = _mm256_set1_epi32(lambda_coeff1);
  __m256i vec_lambda2 = _mm256_set1_epi32(lambda_coeff2);
  __m256i vec_lambda3 = _mm256_set1_epi32(lambda_coeff3);
  __m256i vec_offset = _mm256_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  for (int c=0; c < samples; c+=8)
    { 
      __m256i val0 = _mm256_loadu_si256((__m256i *)(src+c));
      __m256i val1 = _mm256_loadu_si256((__m256i *)(src+c+1));
      __m256i val2 = _mm256_loadu_si256((__m256i *)(src+c+2));
      __m256i val3 = _mm256_loadu_si256((__m256i *)(src+c+3));
      val0 = _mm256_mul_epi32(val0,vec_lambda0);
      val1 = _mm256_mul_epi32(val1,vec_lambda1);
      val2 = _mm256_mul_epi32(val2,vec_lambda2);
      val3 = _mm256_mul_epi32(val3,vec_lambda3);
      __m256i tgt = *((__m256i *)(dst+c));
      val0 = _mm256_add_epi32(val0,vec_offset);
      val0 = _mm256_add_epi32(val0,val1);
      val0 = _mm256_add_epi32(val0,val2);
      val0 = _mm256_add_epi32(val0,val3);
      val0 = _mm256_sra_epi32(val0,downshift);
      *((__m256i *)(dst+c)) = _mm256_add_epi32(tgt,val0);
    }
}
  
} // namespace kd_core_simd

#endif // !KDU_NO_AVX2
