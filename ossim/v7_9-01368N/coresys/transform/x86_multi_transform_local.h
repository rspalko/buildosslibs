/*****************************************************************************/
// File: x86_multi_transform_local.h [scope = CORESYS/TRANSFORMS]
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
   Mediates access to SIMD accelerated implementations of key multi-component
transform functions, which may use any of the MMX/SSE/AVX families of
instruction sets.  Everything above SSE2 is imported from separately
compiled files, rather than being implemented directly within this header
file, so that the entire code base need not depend on the more advanced
instructions that might not be found in all target platforms.
******************************************************************************/

#ifndef X86_MULTI_TRANSFORM_LOCAL_H
#define X86_MULTI_TRANSFORM_LOCAL_H

#include <emmintrin.h> // Only need support for SSE2 and below in this file

#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                            Line Copy Functions                            */
/* ========================================================================= */
  
/*****************************************************************************/
/*                          ..._multi_line_rev_copy                          */
/*****************************************************************************/

#if (!defined KDU_NO_SSE2) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void
  sse2_multi_line_rev_copy(void *in_buf, void *out_buf, int num_samples,
                           bool using_shorts, int rev_offset)
{
  __m128i *sp=(__m128i *)in_buf, *dp=(__m128i *)out_buf;
  if (using_shorts)
    { 
      int nvecs = (num_samples+7)>>3;
      __m128i *dp_lim = dp + nvecs;
      __m128i vec_off = _mm_set1_epi16((kdu_int16) rev_offset);
      for (; dp < dp_lim; sp++, dp++)
        *dp = _mm_adds_epi16(*sp,vec_off);
    }
  else
    { 
      int nvecs = (num_samples+3)>>2;
      __m128i *dp_lim = dp + nvecs;
      __m128i vec_off = _mm_set1_epi32(rev_offset);
      for (; dp < dp_lim; sp++, dp++)
        *dp = _mm_add_epi32(*sp,vec_off);
    }
}  
//-----------------------------------------------------------------------------
#  define SSE2_SET_MULTI_LINE_REV_COPY_FUNC(_func) \
  if (kdu_mmx_level >= 2) \
    { _func=sse2_multi_line_rev_copy; }
#else // No compilation support for SSE2
#  define SSE2_SET_MULTI_LINE_REV_COPY_FUNC(_func)
#endif  

/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_REV_COPY_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_REV_COPY_FUNC(_func) \
  { \
    SSE2_SET_MULTI_LINE_REV_COPY_FUNC(_func); \
  }

/*****************************************************************************/
/*                         ..._multi_line_irrev_copy                         */
/*****************************************************************************/

#if (!defined KDU_NO_SSE2) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void
  sse2_multi_line_irrev_copy(void *in_buf, void *out_buf, int num_samples,
                             bool using_shorts, float irrev_offset)
{
  if (using_shorts)
    { 
      int nvecs = (num_samples+7)>>3;
      __m128i *sp=(__m128i *)in_buf, *dp=(__m128i *)out_buf;
      __m128i *dp_lim = dp + nvecs;
      kdu_int16 off = (kdu_int16)
        floorf(0.5f + irrev_offset*(1<<KDU_FIX_POINT));
      __m128i vec_off = _mm_set1_epi16(off);
      for (; dp < dp_lim; sp++, dp++)
        *dp = _mm_adds_epi16(*sp,vec_off);
    }
  else
    { 
      int nvecs = (num_samples+3)>>2;
      __m128 *sp=(__m128 *)in_buf, *dp=(__m128 *)out_buf;
      __m128 *dp_lim = dp + nvecs;
      __m128 vec_off = _mm_set1_ps(irrev_offset);
      for (; dp < dp_lim; sp++, dp++)
        *dp = _mm_add_ps(*sp,vec_off);
    }
}  
//-----------------------------------------------------------------------------
#  define SSE2_SET_MULTI_LINE_IRREV_COPY_FUNC(_func) \
  if (kdu_mmx_level >= 2) \
    { _func=sse2_multi_line_irrev_copy; }
#else // No compilation support for SSE2
#  define SSE2_SET_MULTI_LINE_IRREV_COPY_FUNC(_func)
#endif  

/*****************************************************************************/
/* SELECTOR              KD_SET_SIMD_MC_IRREV_COPY_FUNC                      */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_IRREV_COPY_FUNC(_func) \
  { \
    SSE2_SET_MULTI_LINE_IRREV_COPY_FUNC(_func); \
  }

  
/* ========================================================================= */
/*                             Matrix Transforms                             */
/* ========================================================================= */

/*****************************************************************************/
/*                          ..._multi_matrix_float                           */
/*****************************************************************************/
  
#if (!defined KDU_NO_SSE2) && (KDU_ALIGN_SAMPLES32 >= 4)
//-----------------------------------------------------------------------------
static void sse2_multi_matrix_float(void **in_bufs, void **out_bufs,
                                    int num_samples, int num_inputs,
                                    int num_outputs, float *coeffs,
                                    float *offsets)
{
  int nvecs=(num_samples+3)>>2;
  for (int m=0; m < num_outputs; m++)
    { 
      __m128 *dpp=(__m128 *)(out_bufs[m]);
      if (dpp == NULL)
        continue; // Output not required
      __m128 *dp, *dp_lim=dpp+nvecs;
      __m128 val = _mm_set1_ps(offsets[m]);
      for (dp=dpp; dp < dp_lim; dp++)
        *dp = val;
      for (int n=0; n < num_inputs; n++)
        { 
          float factor = *(coeffs++);
          __m128 *sp=(__m128 *)(in_bufs[n]);
          if ((sp == NULL) || (factor == 0.0f))
            continue; // Input irrelevant
          __m128 vec_factor = _mm_set1_ps(factor);
          for (dp=dpp; dp < dp_lim; dp++, sp++)
            *dp = _mm_add_ps(*dp,_mm_mul_ps(*sp,vec_factor));
        }
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_MATRIX_FLOAT_FUNC(_func) \
  if (kdu_mmx_level >= 2) \
    { _func=sse2_multi_matrix_float; }
#else // No compilation support for SSE2
#  define SSE2_SET_MATRIX_FLOAT_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_MATRIX32_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_MATRIX32_FUNC(_func) \
  { \
    SSE2_SET_MATRIX_FLOAT_FUNC(_func); \
  }

/*****************************************************************************/
/*                          ..._multi_matrix_fix16                           */
/*****************************************************************************/
  
#if (!defined KDU_NO_SSE2) && (KDU_ALIGN_SAMPLES32 >= 4)
//-----------------------------------------------------------------------------
static void
  sse2_multi_matrix_fix16(void **in_bufs, void **out_bufs, kdu_int32 *acc,
                          int num_samples, int num_inputs, int num_outputs,
                          kdu_int16 *coeffs, int downshift, float *offsets)
{
  // Start by aligning the accumulator buffer
  int align_off = (- _addr_to_kdu_int32(acc)) & 15; // byte offset to align 16
  assert((align_off & 3) == 0);
  acc += (align_off>>2);
  
  // Now compute the number of input vectors to process on each line
  int nvecs=(num_samples+7)>>3; // Input vectors each have 8 short words
  for (int m=0; m < num_outputs; m++)
    { 
      __m128i *dp=(__m128i *)(out_bufs[m]);
      if (dp == NULL)
        continue; // Output not required
      __m128i *app=(__m128i *)acc;
      __m128i *ap, *ap_lim=app+2*nvecs;
      __m128i val = _mm_setzero_si128();
      for (ap=app; ap < ap_lim; ap+=2)
        { ap[0]=val; ap[1]=val; }
      for (int n=0; n < num_inputs; n++)
        { 
          kdu_int16 factor = *(coeffs++);
          __m128i *sp=(__m128i *)(in_bufs[n]);
          if ((sp == NULL) || (factor == 0))
            continue; // Input irrelevant
          __m128i vec_factor = _mm_set1_epi16(factor);
          for (ap=app; ap < ap_lim; ap+=2, sp++)
            { 
              val = *sp;
              __m128i low = _mm_mullo_epi16(val,vec_factor);
              __m128i high = _mm_mulhi_epi16(val,vec_factor);
              __m128i acc0=ap[0], acc1=ap[1];
              ap[0] = _mm_add_epi32(acc0,_mm_unpacklo_epi16(low,high));
              ap[1] = _mm_add_epi32(acc1,_mm_unpackhi_epi16(low,high));
            }
        }
      kdu_int32 off = (kdu_int32)
        floorf(0.5f + offsets[m]*(1<<KDU_FIX_POINT));
      off <<= downshift;
      off += (1 << downshift)>>1;
      __m128i vec_off=_mm_set1_epi32(off);
      __m128i vec_shift=_mm_cvtsi32_si128(downshift);
      for (ap=app; ap < ap_lim; ap+=2, dp++)
        { 
          __m128i v0=ap[0], v1=ap[1];
          v0 = _mm_add_epi32(v0,vec_off);
          v1 = _mm_add_epi32(v1,vec_off);
          v0 = _mm_sra_epi32(v0,vec_shift);
          v1 = _mm_sra_epi32(v1,vec_shift);
          *dp = _mm_packs_epi32(v0,v1);
        }
    }
}
#  define SSE2_SET_MATRIX_FIX16_FUNC(_func) \
  if (kdu_mmx_level >= 2) \
    {_func=sse2_multi_matrix_fix16; }
#else // No compilation support for SSE2
#  define SSE2_SET_MATRIX_FIX16_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_MATRIX16_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_MATRIX16_FUNC(_func) \
  { \
    SSE2_SET_MATRIX_FIX16_FUNC(_func); \
  }
  
  
/* ========================================================================= */
/*                     NLT SMAG/UMAG Conversion Functions                    */
/* ========================================================================= */

/*****************************************************************************/
/*                             ..._smag_int32                                */
/*****************************************************************************/

#if (!defined KDU_NO_SSE4) && (KDU_ALIGN_SAMPLES32 >= 4)
extern void
  sse4_smag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                  int precision, bool src_absolute, bool dst_absolute);
#  define SSE4_SET_SMAG32_FUNC(_func,_prec) \
  if ((kdu_mmx_level >= 5) && (_prec <= 32)) \
    {_func=sse4_smag_int32; }
#else // No compilation support for SSE4.1
#  define SSE4_SET_SMAG32_FUNC(_func,_prec)
#endif
  
/*****************************************************************************/
/* SELECTOR                KD_SET_SIMD_MC_SMAG32_FUNC                        */
/*****************************************************************************/

#define KD_SET_SIMD_MC_SMAG32_FUNC(_func,_prec) \
  { \
    SSE4_SET_SMAG32_FUNC(_func,_prec); \
  }
  
/*****************************************************************************/
/*                             ..._umag_int32                                */
/*****************************************************************************/

#if (!defined KDU_NO_SSE4) && (KDU_ALIGN_SAMPLES32 >= 4)
extern void
  sse4_umag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                  int precision, bool src_absolute, bool dst_absolute);
#  define SSE4_SET_UMAG32_FUNC(_func,_prec) \
  if ((kdu_mmx_level >= 5) && (_prec <= 32)) \
    {_func=sse4_umag_int32; }
#else // No compilation support for SSE4.1
#  define SSE4_SET_UMAG32_FUNC(_func,_prec)
#endif

/*****************************************************************************/
/* SELECTOR                KD_SET_SIMD_MC_UMAG32_FUNC                        */
/*****************************************************************************/

#define KD_SET_SIMD_MC_UMAG32_FUNC(_func,_prec) \
  { \
    SSE4_SET_UMAG32_FUNC(_func,_prec); \
  }
  
  
} // namespace kd_core_simd

#endif // X86_MULTI_TRANSFORM_LOCAL_H
