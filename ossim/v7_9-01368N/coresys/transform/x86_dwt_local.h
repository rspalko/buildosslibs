/*****************************************************************************/
// File: x86_dwt_local.h [scope = CORESYS/TRANSFORMS]
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
   Implements various critical functions for DWT analysis and synthesis using
MMX/SSE/SSE2/SSSE3/AVX2 intrinsics.  These can be compiled under GCC or .NET
and are compatible with both 32-bit and 64-bit builds.
   Everything above SSE2 is imported from separately compiled files,
"..._dwt_local.cpp" so that the entire code base need not depend on the
more advanced instructions. 
******************************************************************************/

#ifndef X86_DWT_LOCAL_H
#define X86_DWT_LOCAL_H

#include <emmintrin.h> // Only need support for SSE2 and below in this file
#include "transform_base.h"

namespace kd_core_simd {
  using namespace kd_core_local;

#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

// The factors below are used in SSE2/MMX implementations of the 9/7 xform
static kdu_int16 simd_w97_rem[4] =
  {(kdu_int16) floor(0.5 + (W97_FACT_0+2.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_1*(double)(1<<19)),
   (kdu_int16) floor(0.5 + (W97_FACT_2-1.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_3*(double)(1<<16))};
static kdu_int16 simd_w97_preoff[4] =
  {(kdu_int16) floor(0.5 + 0.5/(W97_FACT_0+2.0)),
   0,
   (kdu_int16) floor(0.5 + 0.5/(W97_FACT_2-1.0)),
   (kdu_int16) floor(0.5 + 0.5/W97_FACT_3)};

// Safe "static initializer" logic
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_AVX2)
  extern void avx2_dwt_local_static_init();
  static bool avx2_dwt_local_static_inited=false;
# define AVX2_DWT_DO_STATIC_INIT() \
    if (!avx2_dwt_local_static_inited) \
      { if (kdu_mmx_level >= 7) avx2_dwt_local_static_init(); \
        avx2_dwt_local_static_inited=true; }
#else // No compilation support for AVX2
# define AVX2_DWT_DO_STATIC_INIT() /* Nothing to do */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_SSSE3)
  extern void ssse3_dwt_local_static_init();
  static bool ssse3_dwt_local_static_inited=false;
# define SSSE3_DWT_DO_STATIC_INIT() \
    if (!ssse3_dwt_local_static_inited) \
      { if (kdu_mmx_level >= 4) ssse3_dwt_local_static_init(); \
        ssse3_dwt_local_static_inited=true; }
#else // No compilation support for SSSE3
# define SSSE3_DWT_DO_STATIC_INIT() /* Nothing to do */
#endif
//-----------------------------------------------------------------------------


/* ========================================================================= */
/*                            Interleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/*                            ..._interleave_16                              */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
//-----------------------------------------------------------------------------
  extern void
    avx2_upshifted_interleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int,int);
  extern void
    avx2_interleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int,int);
//-----------------------------------------------------------------------------
#  define AVX2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) \
if ((kdu_mmx_level >= 7) && (_pairs >= 16)) \
  { \
    if (_upshift == 0) _tgt = avx2_interleave_16; \
    else _tgt = avx2_upshifted_interleave_16; \
  }
#else // No compilation support for AVX2
#  define AVX2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static inline void
  sse2_upshifted_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                               kdu_int16 *dst, int pairs,
                               int upshift)
{
  __m128i shift = _mm_cvtsi32_si128(upshift);
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      __m128i val1 = *((__m128i *)(src1-4));
      val1 = _mm_sll_epi16(val1,shift);
      __m128i val2 = *((__m128i *)(src2-4));
      val2 = _mm_sll_epi16(val2,shift);
      *((__m128i *) dst) = _mm_unpackhi_epi16(val1,val2);
      src1 += 4; src2 += 4; dst += 8; pairs -= 4;
    }
  __m128i *sp1 = (__m128i *) src1;
  __m128i *sp2 = (__m128i *) src2;
  __m128i *dp = (__m128i *) dst;
  for (; pairs > 4; pairs-=8, sp1++, sp2++, dp+=2)
    { 
      __m128i val1 = *sp1;
      val1 = _mm_sll_epi16(val1,shift);
      __m128i val2 = *sp2;
      val2 = _mm_sll_epi16(val2,shift);
      dp[0] = _mm_unpacklo_epi16(val1,val2);
      dp[1] = _mm_unpackhi_epi16(val1,val2);
    }
  if (pairs > 0)
    { // Need to generate one more group of 8 outputs (4 pairs)
      __m128i val1 = *sp1;
      val1 = _mm_sll_epi16(val1,shift);
      __m128i val2 = *sp2;
      val2 = _mm_sll_epi16(val2,shift);
      dp[0] = _mm_unpacklo_epi16(val1,val2);
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                     kdu_int16 *dst, int pairs, int upshift)
{
  assert(upshift == 0);
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      __m128i val1 = *((__m128i *)(src1-4));
      __m128i val2 = *((__m128i *)(src2-4));
      *((__m128i *) dst) = _mm_unpackhi_epi16(val1,val2);
      src1 += 4; src2 += 4; dst += 8; pairs -= 4;
    }
  __m128i *sp1 = (__m128i *) src1;
  __m128i *sp2 = (__m128i *) src2;
  __m128i *dp = (__m128i *) dst;
  for (; pairs > 4; pairs-=8, sp1++, sp2++, dp+=2)
    { 
      __m128i val1 = *sp1;
      __m128i val2 = *sp2;
      dp[0] = _mm_unpacklo_epi16(val1,val2);
      dp[1] = _mm_unpackhi_epi16(val1,val2);
    }
  if (pairs > 0)
    { // Need to generate one more group of 8 outputs (4 pairs)
      __m128i val1 = *sp1;
      __m128i val2 = *sp2;
      dp[0] = _mm_unpacklo_epi16(val1,val2);
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) \
if ((kdu_mmx_level >= 2) && (_pairs >= 8)) \
  { \
    if (_upshift == 0) _tgt = sse2_interleave_16; \
    else _tgt = sse2_upshifted_interleave_16; \
  }
#else // No compilation support for SSE2
#  define SSE2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------
  

#if (!defined KDU_NO_MMX64) && (KDU_MMX_LEVEL < 2)
//-----------------------------------------------------------------------------
static inline void
  mmx_upshifted_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                              kdu_int16 *dst, int pairs, int upshift)
{
  __m64 shift = _mm_cvtsi32_si64(upshift);
  __m64 *sp1 = (__m64 *) src1;
  __m64 *sp2 = (__m64 *) src2;
  __m64 *dp = (__m64 *) dst;
  for (; pairs > 0; pairs-=4, sp1++, sp2++, dp+=2)
    { 
      __m64 val1 = *sp1;
      val1 = _mm_sll_pi16(val1,shift);
      __m64 val2 = *sp2;
      val2 = _mm_sll_pi16(val2,shift);
      dp[0] = _mm_unpacklo_pi16(val1,val2);
      dp[1] = _mm_unpackhi_pi16(val1,val2);
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
static inline void
  mmx_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                    kdu_int16 *dst, int pairs, int upshift)
{
  assert(upshift == 0);
  __m64 *sp1 = (__m64 *) src1;
  __m64 *sp2 = (__m64 *) src2;
  __m64 *dp = (__m64 *) dst;
  for (; pairs > 0; pairs-=4, sp1++, sp2++, dp+=2)
    {
    __m64 val1 = *sp1;
    __m64 val2 = *sp2;
    dp[0] = _mm_unpacklo_pi16(val1,val2);
    dp[1] = _mm_unpackhi_pi16(val1,val2);
    }
  _mm_empty(); // Clear MMX registers for use by FPU  
}
//-----------------------------------------------------------------------------
#  define MMX_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) \
if ((kdu_mmx_level >= 1) && (_pairs >= 8)) \
  { \
    if (_upshift == 0) _tgt = mmx_interleave_16; \
    else _tgt = mmx_upshifted_interleave_16; \
  }
#else // No compilation support for MMX
#  define MMX_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_INTERLEAVE_16_FUNC(_tgt,_pairs,_upshift) \
  { \
    MMX_SET_INTERLEAVE_16(_tgt,_pairs,_upshift); \
    SSE2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift); \
    AVX2_SET_INTERLEAVE_16(_tgt,_pairs,_upshift); \
    AVX2_DWT_DO_STATIC_INIT(); \
  }

/*****************************************************************************/
/*                            ..._interleave_32                              */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
//-----------------------------------------------------------------------------
  extern void avx2_interleave_32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
//-----------------------------------------------------------------------------
#  define AVX2_SET_INTERLEAVE_32(_tgt,_pairs) \
if ((kdu_mmx_level >= 7) && (_pairs >= 8)) \
  { \
    _tgt = avx2_interleave_32; \
  }
#else // No compilation support for AVX2
#  define AVX2_SET_INTERLEAVE_32(_tgt,_pairs) /* Do nothing */
#endif
//-----------------------------------------------------------------------------


#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES32 >= 4)
//-----------------------------------------------------------------------------
static inline void
  sse2_interleave_32(kdu_int32 *src1, kdu_int32 *src2,
                     kdu_int32 *dst, int pairs)
{
  bool odd_start = ((_addr_to_kdu_int32(src1) & 8) != 0);
  if (odd_start)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      src1 += 2;  src2 += 2; dst += 4; pairs -= 2;
    }
  __m128i *sp1 = (__m128i *) src1;
  __m128i *sp2 = (__m128i *) src2;
  __m128i *dp = (__m128i *) dst;
  if (odd_start)
    dp[-1] = _mm_unpackhi_epi32(sp1[-1],sp2[-1]);
  for (; pairs > 2; pairs-=4, sp1++, sp2++, dp+=2)
    { 
      __m128i val1 = *sp1;
      __m128i val2 = *sp2;
      dp[0] = _mm_unpacklo_epi32(val1,val2);
      dp[1] = _mm_unpackhi_epi32(val1,val2);
    }
  if (pairs > 0)
    dp[0] = _mm_unpacklo_epi32(sp1[0],sp2[0]); // Odd tail
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_INTERLEAVE_32(_tgt,_pairs) \
if ((kdu_mmx_level >= 2) && (_pairs >= 4)) \
  { \
    _tgt = sse2_interleave_32; \
  }
#else // No compilation support for SSE2
#  define SSE2_SET_INTERLEAVE_32(_tgt,_pairs) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_INTERLEAVE_32_FUNC(_tgt,_pairs) \
  { \
    SSE2_SET_INTERLEAVE_32(_tgt,_pairs); \
    AVX2_SET_INTERLEAVE_32(_tgt,_pairs); \
    AVX2_DWT_DO_STATIC_INIT(); \
  }



/* ========================================================================= */
/*                          Deinterleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/*                           ..._deinterleave_16                             */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
//-----------------------------------------------------------------------------
  extern void avx2_downshifted_deinterleave_16(kdu_int16 *,kdu_int16 *,
                                               kdu_int16 *,int,int);
  extern void avx2_deinterleave_16(kdu_int16 *,kdu_int16 *,
                                   kdu_int16 *,int,int);
//-----------------------------------------------------------------------------
#  define AVX2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) \
if ((kdu_mmx_level >= 7) && (_pairs >= 16)) \
  { \
    if (_downshift == 0) _tgt = avx2_deinterleave_16; \
    else _tgt = avx2_downshifted_deinterleave_16; \
  }
#else // No compilation support for AVX2
#  define AVX2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static inline void
  sse2_downshifted_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                   kdu_int16 *dst2, int pairs,
                                   int downshift)
{
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m128i vec_offset = _mm_set1_epi16((kdu_int16)((1<<downshift)>>1));
  __m128i *sp = (__m128i *) src;
  __m128i *dp1 = (__m128i *) dst1;
  __m128i *dp2 = (__m128i *) dst2;
  for (; pairs > 4; pairs-=8, sp+=2, dp1++, dp2++)
    { 
      __m128i val1 = sp[0];
      val1 = _mm_add_epi16(val1,vec_offset);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i val2 = sp[1];
      val2 = _mm_add_epi16(val2,vec_offset);
      val2 = _mm_sra_epi16(val2,shift);
      __m128i low1 = _mm_slli_epi32(val1,16);
      low1 = _mm_srai_epi32(low1,16);
      __m128i low2 = _mm_slli_epi32(val2,16);
      low2 = _mm_srai_epi32(low2,16);
      *dp1 = _mm_packs_epi32(low1,low2);
      __m128i high1 = _mm_srai_epi32(val1,16);
      __m128i high2 = _mm_srai_epi32(val2,16);
      *dp2 = _mm_packs_epi32(high1,high2);
    }
  if (pairs > 0)
    { // Need to read one more group of 4 pairs
      __m128i val1 = sp[0];
      val1 = _mm_add_epi16(val1,vec_offset);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i low1 = _mm_slli_epi32(val1,16);
      low1 = _mm_srai_epi32(low1,16);
      *dp1 = _mm_packs_epi32(low1,low1);
      __m128i high1 = _mm_srai_epi32(val1,16);
      *dp2 = _mm_packs_epi32(high1,high1);
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                       kdu_int16 *dst2, int pairs, int downshift)
{
  assert(downshift == 0);
  __m128i *sp = (__m128i *) src;
  __m128i *dp1 = (__m128i *) dst1;
  __m128i *dp2 = (__m128i *) dst2;
  for (; pairs > 4; pairs-=8, sp+=2, dp1++, dp2++)
    { 
      __m128i val1 = sp[0];
      __m128i val2 = sp[1];
      __m128i low1 = _mm_slli_epi32(val1,16);
      low1 = _mm_srai_epi32(low1,16);
      __m128i low2 = _mm_slli_epi32(val2,16);
      low2 = _mm_srai_epi32(low2,16);
      *dp1 = _mm_packs_epi32(low1,low2);
      __m128i high1 = _mm_srai_epi32(val1,16);
      __m128i high2 = _mm_srai_epi32(val2,16);
      *dp2 = _mm_packs_epi32(high1,high2);
    }
  if (pairs > 0)
    { // Need to read one more group of 4 pairs
      __m128i val1 = sp[0];
      __m128i low1 = _mm_slli_epi32(val1,16);
      low1 = _mm_srai_epi32(low1,16);
      *dp1 = _mm_packs_epi32(low1,low1);
      __m128i high1 = _mm_srai_epi32(val1,16);
      *dp2 = _mm_packs_epi32(high1,high1);
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) \
if ((kdu_mmx_level >= 2) && (_pairs >= 8)) \
  { \
    if (_downshift == 0) _tgt = sse2_deinterleave_16; \
    else _tgt = sse2_downshifted_deinterleave_16; \
  }
#else // No compilation support for SSE2
#  define SSE2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------


#if (!defined KDU_NO_MMX64) && (KDU_MMX_LEVEL < 2)
//-----------------------------------------------------------------------------
static inline void
  mmx_downshifted_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                  kdu_int16 *dst2, int pairs,
                                  int downshift)
{
  __m64 shift = _mm_cvtsi32_si64(downshift);
  __m64 vec_offset = _mm_set1_pi16((kdu_int16)((1<<downshift)>>1));
  __m64 *sp = (__m64 *) src;
  __m64 *dp1 = (__m64 *) dst1;
  __m64 *dp2 = (__m64 *) dst2;
  for (; pairs > 0; pairs-=4, sp+=2, dp1++, dp2++)
    { 
      __m64 val1 = sp[0];
      val1 = _mm_add_pi16(val1,vec_offset);
      val1 = _mm_sra_pi16(val1,shift);
      __m64 val2 = sp[1];
      val2 = _mm_add_pi16(val2,vec_offset);
      val2 = _mm_sra_pi16(val2,shift);
      __m64 low1 = _mm_slli_pi32(val1,16);
      low1 = _mm_srai_pi32(low1,16); // Leaves sign extended words 0 & 2
      __m64 low2 = _mm_slli_pi32(val2,16);
      low2 = _mm_srai_pi32(low2,16); // Leaves sign extended words 4 & 6
      *dp1 = _mm_packs_pi32(low1,low2); // Packs and saves words 0,2,4,6
      __m64 high1 = _mm_srai_pi32(val1,16); // Leaves sign extended words 1 & 3
      __m64 high2 = _mm_srai_pi32(val2,16); // Leaves sign extended words 5 & 7
      *dp2 = _mm_packs_pi32(high1,high2); // Packs and saves words 1,3,5,7
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
static inline void
  mmx_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                      kdu_int16 *dst2, int pairs, int downshift)
{
  assert(downshift == 0);
  __m64 *sp = (__m64 *) src;
  __m64 *dp1 = (__m64 *) dst1;
  __m64 *dp2 = (__m64 *) dst2;
  for (; pairs > 0; pairs-=4, sp+=2, dp1++, dp2++)
    { 
      __m64 val1 = sp[0];
      __m64 val2 = sp[1];
      __m64 low1 = _mm_slli_pi32(val1,16);
      low1 = _mm_srai_pi32(low1,16); // Leaves sign extended words 0 & 2
      __m64 low2 = _mm_slli_pi32(val2,16);
      low2 = _mm_srai_pi32(low2,16); // Leaves sign extended words 4 & 6
      *dp1 = _mm_packs_pi32(low1,low2); // Packs and saves words 0,2,4,6
      __m64 high1 = _mm_srai_pi32(val1,16); // Leaves sign extended words 1 & 3
      __m64 high2 = _mm_srai_pi32(val2,16); // Leaves sign extended words 5 & 7
      *dp2 = _mm_packs_pi32(high1,high2); // Packs and saves words 1,3,5,7
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
#  define MMX_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) \
if ((kdu_mmx_level >= 1) && (_pairs >= 8)) \
  { \
    if (_downshift == 0) _tgt = mmx_deinterleave_16; \
    else _tgt = mmx_downshifted_deinterleave_16; \
  }
#else // No compilation support for MMX
#  define MMX_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_DEINTERLEAVE_16_FUNC(_tgt,_pairs,_downshift) \
{ \
  MMX_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift); \
  SSE2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift); \
  AVX2_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift); \
  AVX2_DWT_DO_STATIC_INIT(); \
}


/*****************************************************************************/
/*                           ..._deinterleave_32                             */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
//-----------------------------------------------------------------------------
  extern void avx2_deinterleave_32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
//-----------------------------------------------------------------------------
#  define AVX2_SET_DEINTERLEAVE_32(_tgt,_pairs) \
   if ((kdu_mmx_level >= 7) && (_pairs >= 8)) \
      { \
        _tgt = avx2_deinterleave_32; \
      }
#else // No compilation support for AVX2
#  define AVX2_SET_DEINTERLEAVE_32(_tgt,_pairs) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_DEINTERLEAVE_32_FUNC(_tgt,_pairs) \
{ \
  AVX2_SET_DEINTERLEAVE_32(_tgt,_pairs); \
  AVX2_DWT_DO_STATIC_INIT(); \
}



/* ========================================================================= */
/*                 Vertical Lifting Step Functions (16-bit)                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              avx2_vlift_16_...                            */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_2tap_synth(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                             int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_4tap_synth(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                             int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_2tap_analysis(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_4tap_analysis(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_5x3_synth_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                               int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_5x3_synth_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                               int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_5x3_analysis_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                  int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_5x3_analysis_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                  int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_9x7_synth_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                               int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_9x7_synth_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                               int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_9x7_synth_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_16_9x7_analysis_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                  int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_9x7_analysis_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                  int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_16_9x7_analysis_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define AVX2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 7) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_16_5x3_synth_s0; \
          else \
            _func = avx2_vlift_16_5x3_synth_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_16_9x7_synth_s0; \
          else if (_step->step_idx == 1) \
            _func = avx2_vlift_16_9x7_synth_s1; \
          else \
            _func = avx2_vlift_16_9x7_synth_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = avx2_vlift_16_2tap_synth; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = avx2_vlift_16_4tap_synth; _add_first = false; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_16_5x3_analysis_s0; \
          else \
            _func = avx2_vlift_16_5x3_analysis_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_16_9x7_analysis_s0; \
          else if (_step->step_idx == 1) \
            _func = avx2_vlift_16_9x7_analysis_s1; \
          else \
            _func = avx2_vlift_16_9x7_analysis_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = avx2_vlift_16_2tap_analysis; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = avx2_vlift_16_4tap_analysis; _add_first = false; \
        } \
    } \
}
#else // No compilation support for AVX2
#  define AVX2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/*                             ssse3_vlift_16_...                            */
/*****************************************************************************/

#if (!defined KDU_NO_SSSE3) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
  extern void
    ssse3_vlift_16_9x7_synth_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    ssse3_vlift_16_9x7_synth_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    ssse3_vlift_16_9x7_synth_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                 int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    ssse3_vlift_16_9x7_analysis_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
  extern void
    ssse3_vlift_16_9x7_analysis_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
  extern void
    ssse3_vlift_16_9x7_analysis_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                    int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define SSSE3_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
  if (kdu_mmx_level >= 4) \
    { \
      if (_synthesis) \
        { \
          if (_step->kernel_id == Ckernels_W9X7) \
            { \
              _add_first = (_step->step_idx != 1); \
              if (_step->step_idx == 0) \
                _func = ssse3_vlift_16_9x7_synth_s0; \
              else if (_step->step_idx == 1) \
                _func = ssse3_vlift_16_9x7_synth_s1; \
              else \
                _func = ssse3_vlift_16_9x7_synth_s23; \
            } \
        } \
      else \
        { \
          if (_step->kernel_id == Ckernels_W9X7) \
            { \
              _add_first = (_step->step_idx != 1); \
              if (_step->step_idx == 0) \
                _func = ssse3_vlift_16_9x7_analysis_s0; \
              else if (_step->step_idx == 1) \
                _func = ssse3_vlift_16_9x7_analysis_s1; \
              else \
                _func = ssse3_vlift_16_9x7_analysis_s23; \
            } \
        } \
    }
#else // No compilation support for SSSE3
#  define SSSE3_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif


/*****************************************************************************/
/*                             sse2_vlift_16_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void sse2_vlift_16_2tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
                                     kdu_int16 *dst_out, int samples,
                                     kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  __m128i *sp1 = (__m128i *) src[0];
  __m128i *sp2 = sp1; // In case we only have 1 tap
  if (step->support_length == 2)
    { lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
      sp2 = (__m128i *) src[1]; }
  __m128i vec_lambda = _mm_set1_epi32(lambda_coeffs);
  __m128i vec_offset = _mm_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i *dp_in = (__m128i *) dst_in;
  __m128i *dp_out = (__m128i *) dst_out;
  samples = (samples+7)>>3;
  for (int c=0; c < samples; c++)
    { 
      __m128i val1 = sp1[c];
      __m128i val2 = sp2[c];
      __m128i high = _mm_unpackhi_epi16(val1,val2);
      __m128i low = _mm_unpacklo_epi16(val1,val2);
      high = _mm_madd_epi16(high,vec_lambda);
      high = _mm_add_epi32(high,vec_offset);
      high = _mm_sra_epi32(high,downshift);
      low = _mm_madd_epi16(low,vec_lambda);
      low = _mm_add_epi32(low,vec_offset);
      low = _mm_sra_epi32(low,downshift);
      __m128i tgt = dp_in[c];
      __m128i subtend = _mm_packs_epi32(low,high);
      tgt = _mm_sub_epi16(tgt,subtend);
      dp_out[c] = tgt;
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_16_4tap_synth(kdu_int16 **src, kdu_int16 *dst_in,
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
  __m128i vec_offset = _mm_set1_epi32(step->rounding_offset);
  __m128i vec_lambda0 = _mm_set1_epi32(lambda_coeffs0);
  __m128i vec_lambda2 = _mm_set1_epi32(lambda_coeffs2);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = _mm_load_si128((__m128i *)(src1+c));
      __m128i val2 = _mm_load_si128((__m128i *)(src2+c));
      __m128i high0 = _mm_unpackhi_epi16(val1,val2);
      __m128i low0 = _mm_unpacklo_epi16(val1,val2);
      high0 = _mm_madd_epi16(high0,vec_lambda0);
      low0 = _mm_madd_epi16(low0,vec_lambda0);
      
      __m128i val3 = _mm_load_si128((__m128i *)(src3+c));
      __m128i val4 = _mm_load_si128((__m128i *)(src4+c));
      __m128i high1 = _mm_unpackhi_epi16(val3,val4);
      __m128i low1 = _mm_unpacklo_epi16(val3,val4);
      high1 = _mm_madd_epi16(high1,vec_lambda2);
      low1 = _mm_madd_epi16(low1,vec_lambda2);
      
      __m128i high = _mm_add_epi32(high0,high1);
      high = _mm_add_epi32(high,vec_offset); // Add rounding offset
      high = _mm_sra_epi32(high,downshift);
      __m128i low = _mm_add_epi32(low0,low1);
      low = _mm_add_epi32(low,vec_offset); // Add rounding offset
      low = _mm_sra_epi32(low,downshift);
      
      __m128i tgt = *((__m128i *)(dst_in+c));
      __m128i subtend = _mm_packs_epi32(low,high);
      tgt = _mm_sub_epi16(tgt,subtend);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_16_5x3_synth_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                       kdu_int16 *dst_out, int samples,
                                       kd_lifting_step *step,
                                       bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i vec_offset= _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_sub_epi16(val,*((__m128i *)(src1+c))); // Subtract src 1
      val = _mm_sub_epi16(val,*((__m128i *)(src2+c))); // Subtract src 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
static void sse2_vlift_16_5x3_synth_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                       kdu_int16 *dst_out, int samples,
                                       kd_lifting_step *step,
                                       bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i vec_offset= _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_add_epi16(val,*((__m128i *)(src1+c))); // Add source 1
      val = _mm_add_epi16(val,*((__m128i *)(src2+c))); // Add source 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_16_5x3_analysis_s0(kdu_int16 **src, kdu_int16 *dst_in,
                                          kdu_int16 *dst_out, int samples,
                                          kd_lifting_step *step,
                                          bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i vec_offset= _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_sub_epi16(val,*((__m128i *)(src1+c))); // Subtract source 1
      val = _mm_sub_epi16(val,*((__m128i *)(src2+c))); // Subtract source 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}  
static void sse2_vlift_16_5x3_analysis_s1(kdu_int16 **src, kdu_int16 *dst_in,
                                          kdu_int16 *dst_out, int samples,
                                          kd_lifting_step *step,
                                          bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i vec_offset= _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_add_epi16(val,*((__m128i *)(src1+c))); // Add source 1
      val = _mm_add_epi16(val,*((__m128i *)(src2+c))); // Add source 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_16_9x7_synth(kdu_int16 **src, kdu_int16 *dst_in,
                                    kdu_int16 *dst_out, int samples,
                                    kd_lifting_step *step, bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(for_synthesis); // This implementation does synthesis only
  __m128i vec_lambda = _mm_set1_epi16(simd_w97_rem[step_idx]);
  __m128i vec_offset = _mm_set1_epi16(simd_w97_preoff[step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_add_epi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_add_epi16(tgt,val); // Another -1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m128i roff = _mm_setzero_si128();
      __m128i tmp = _mm_cmpeq_epi16(roff,roff); // Set to all 1's
      roff = _mm_sub_epi16(roff,tmp); // Leaves each word in `roff' = 1
      roff = _mm_slli_epi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val1 = *((__m128i *)(src1+c));
          val1 = _mm_mulhi_epi16(val1,vec_lambda);
          __m128i val2 = _mm_setzero_si128();
          val2 = _mm_sub_epi16(val2,*((__m128i *)(src2+c)));
          val2 = _mm_mulhi_epi16(val2,vec_lambda);
          __m128i val = _mm_sub_epi16(val1,val2); // +ve minus -ve source
          val = _mm_add_epi16(val,roff); // Add rounding offset
          val = _mm_srai_epi16(val,3);
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_sub_epi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_16_9x7_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                                       kdu_int16 *dst_out, int samples,
                                       kd_lifting_step *step,
                                       bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(!for_synthesis); // This implementation does analysis only
  __m128i vec_lambda = _mm_set1_epi16(simd_w97_rem[step_idx]);
  __m128i vec_offset = _mm_set1_epi16(simd_w97_preoff[step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_sub_epi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_sub_epi16(tgt,val); // Another -1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_epi16(tgt,val); // Final contribution
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m128i roff = _mm_setzero_si128();
      __m128i tmp = _mm_cmpeq_epi16(roff,roff); // Set to all 1's
      roff = _mm_sub_epi16(roff,tmp); // Leaves each word in `roff' equal to 1
      roff = _mm_slli_epi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val1 = *((__m128i *)(src1+c));      // Get +ve source 1
          val1 = _mm_mulhi_epi16(val1,vec_lambda); // * lambda, discard 16 LSBs
          __m128i val2 = _mm_setzero_si128();
          val2 = _mm_sub_epi16(val2,*((__m128i *)(src2+c))); // -ve source 2
          val2 = _mm_mulhi_epi16(val2,vec_lambda); // * lambda, discard 16 LSBs
          __m128i val = _mm_sub_epi16(val1,val2); // Sub -ve from +ve source
          val = _mm_add_epi16(val,roff); // Add rounding offset
          val = _mm_srai_epi16(val,3); // Shift result to the right by 3
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_add_epi16(tgt,val); // Update destination samples
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          tgt = _mm_add_epi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_epi16(tgt,val); // Final contribution
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = *((__m128i *)(src1+c));
          val = _mm_add_epi16(val,*((__m128i *)(src2+c)));
          __m128i tgt = *((__m128i *)(dst_in+c));
          val = _mm_add_epi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_epi16(tgt,val); // Final contribution
          *((__m128i *)(dst_out+c)) = tgt;
        }
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 2) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
             _add_first = true; \
            if (_step->step_idx == 0) \
              _func = sse2_vlift_16_5x3_synth_s0; \
            else \
              _func = sse2_vlift_16_5x3_synth_s1; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _add_first = (_step->step_idx != 1); \
            _func = sse2_vlift_16_9x7_synth; \
          } \
        else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
          { \
            _func = sse2_vlift_16_2tap_synth; _add_first = false; \
          } \
        else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
          { \
            _func = sse2_vlift_16_4tap_synth; _add_first = false; \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
             _add_first = true; \
            if (_step->step_idx == 0) \
              _func = sse2_vlift_16_5x3_analysis_s0; \
            else \
              _func = sse2_vlift_16_5x3_analysis_s1; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _add_first = (_step->step_idx != 1); \
            _func = sse2_vlift_16_9x7_analysis; \
          } \
      } \
  }
#else // No compilation support for SSE2
#  define SSE2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif


/*****************************************************************************/
/* STATIC                      mmx_vlift_16_...                              */
/*****************************************************************************/

#if ((!defined KDU_NO_MMX64) && (KDU_MMX_LEVEL < 2))
//-----------------------------------------------------------------------------
static void mmx_vlift_16_5x3_synth(kdu_int16 **src, kdu_int16 *dst_in,
                                   kdu_int16 *dst_out, int samples,
                                   kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This implementation does synthesis only
  __m64 downshift = _mm_cvtsi32_si64(step->downshift);
  __m64 vec_offset = _mm_set1_pi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step->icoeffs[0] == 1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset;  // Start with the offset
        val = _mm_add_pi16(val,*((__m64 *)(src1+c))); // Add source 1
        val = _mm_add_pi16(val,*((__m64 *)(src2+c))); // Add source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst_in+c));
        tgt = _mm_sub_pi16(tgt,val);
        *((__m64 *)(dst_out+c)) = tgt;
      }
  else if (step->icoeffs[0] == -1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset;  // Start with the offset
        val = _mm_sub_pi16(val,*((__m64 *)(src1+c))); // Subtract source 1
        val = _mm_sub_pi16(val,*((__m64 *)(src2+c))); // Subtract source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst_in+c));
        tgt = _mm_sub_pi16(tgt,val);
        *((__m64 *)(dst_out+c)) = tgt;
      }
  else
    assert(0);
  _mm_empty();
}
//-----------------------------------------------------------------------------
static void mmx_vlift_16_5x3_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                                      kdu_int16 *dst_out, int samples,
                                      kd_lifting_step *step,
                                      bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis); // This implementation does analysis only
  __m64 downshift = _mm_cvtsi32_si64(step->downshift);
  __m64 vec_offset = _mm_set1_pi16((kdu_int16)((1<<step->downshift)>>1));
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step->icoeffs[0] == 1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset;  // Start with the offset
        val = _mm_add_pi16(val,*((__m64 *)(src1+c))); // Add source 1
        val = _mm_add_pi16(val,*((__m64 *)(src2+c))); // Add source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst_in+c));
        tgt = _mm_add_pi16(tgt,val);
        *((__m64 *)(dst_out+c)) = tgt;
      }
  else if (step->icoeffs[0] == -1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset;  // Start with the offset
        val = _mm_sub_pi16(val,*((__m64 *)(src1+c))); // Subtract source 1
        val = _mm_sub_pi16(val,*((__m64 *)(src2+c))); // Subtract source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst_in+c));
        tgt = _mm_add_pi16(tgt,val);
        *((__m64 *)(dst_out+c)) = tgt;
      }
  else
    assert(0);
  _mm_empty();
}
//-----------------------------------------------------------------------------
static void mmx_vlift_16_9x7_synth(kdu_int16 **src, kdu_int16 *dst_in,
                                   kdu_int16 *dst_out, int samples,
                                   kd_lifting_step *step, bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This implementation does synthesis only
  __m64 vec_lambda = _mm_set1_pi16(simd_w97_rem[step_idx]);
  __m64 vec_offset = _mm_set1_pi16(simd_w97_preoff[step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_add_pi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_add_pi16(tgt,val); // Another -1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m64 roff = _mm_setzero_si64();
      __m64 tmp = _mm_cmpeq_pi16(roff,roff); // Set to all 1's
      roff = _mm_sub_pi16(roff,tmp); // Leaves each word in `roff' = 1
      roff = _mm_slli_pi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val1 = *((__m64 *)(src1+c));
          val1 = _mm_mulhi_pi16(val1,vec_lambda);
          __m64 val2 = _mm_setzero_si64();
          val2 = _mm_sub_pi16(val2,*((__m64 *)(src2+c)));
          val2 = _mm_mulhi_pi16(val2,vec_lambda);
          __m64 val = _mm_sub_pi16(val1,val2); // +ve minus -ve source
          val = _mm_add_pi16(val,roff); // Add rounding offset
          val = _mm_srai_pi16(val,3);
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_sub_pi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
static void mmx_vlift_16_9x7_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                                      kdu_int16 *dst_out, int samples,
                                      kd_lifting_step *step,
                                      bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis); // This implementation does analysis only
  __m64 vec_lambda = _mm_set1_pi16(simd_w97_rem[step_idx]);
  __m64 vec_offset = _mm_set1_pi16(simd_w97_preoff[step_idx]);
  kdu_int16 *src1=src[0], *src2=src[1];
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_sub_pi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_sub_pi16(tgt,val); // Another -1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m64 roff = _mm_setzero_si64();
      __m64 tmp = _mm_cmpeq_pi16(roff,roff); // Set to all 1's
      roff = _mm_sub_pi16(roff,tmp); // Leaves each word in `roff' equal to 1
      roff = _mm_slli_pi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val1 = *((__m64 *)(src1+c));               // Get +ve source 1
          val1 = _mm_mulhi_pi16(val1,vec_lambda); // * lambda & discard 16 LSBs
          __m64 val2 = _mm_setzero_si64();
          val2 = _mm_sub_pi16(val2,*((__m64 *)(src2+c))); // Get -ve source 2
          val2 = _mm_mulhi_pi16(val2,vec_lambda); // * lambda & discard 16 LSBs
          __m64 val = _mm_sub_pi16(val1,val2); // Subract -ve from +ve source
          val = _mm_add_pi16(val,roff); // Add rounding offset
          val = _mm_srai_pi16(val,3); // Shift result to the right by 3
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_add_pi16(tgt,val); // Update destination samples
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          tgt = _mm_add_pi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src1+c));
          val = _mm_add_pi16(val,*((__m64 *)(src2+c)));
          __m64 tgt = *((__m64 *)(dst_in+c));
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst_out+c)) = tgt;
        }
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
#  define MMX_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 1) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = mmx_vlift_16_5x3_synth; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = mmx_vlift_16_9x7_synth; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = mmx_vlift_16_5x3_analysis; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = mmx_vlift_16_9x7_analysis; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
  }
#else // No compilation support for MMX
#  define MMX_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif


/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  MMX_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSE2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSSE3_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  AVX2_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSSE3_DWT_DO_STATIC_INIT(); \
  AVX2_DWT_DO_STATIC_INIT(); \
}


/* ========================================================================= */
/*                 Vertical Lifting Step Functions (32-bit)                  */
/* ========================================================================= */

/*****************************************************************************/
/*                             avx2_vlift_32_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_32_2tap_irrev(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                             int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_32_4tap_irrev(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                             int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_32_2tap_rev_synth(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                 int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_32_4tap_rev_synth(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                 int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_32_2tap_rev_analysis(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                    int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_32_4tap_rev_analysis(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                    int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_32_5x3_synth_s0(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                               int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_32_5x3_synth_s1(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                               int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_vlift_32_5x3_analysis_s0(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                  int, kd_lifting_step *, bool);
  extern void
    avx2_vlift_32_5x3_analysis_s1(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                  int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define AVX2_SET_VLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_mmx_level >= 7) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_32_5x3_synth_s0; \
          else \
            _func = avx2_vlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_vlift_32_2tap_rev_synth; \
          else if (_step->support_length <= 4) \
            _func = avx2_vlift_32_4tap_rev_synth; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = avx2_vlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = avx2_vlift_32_5x3_analysis_s0; \
          else \
            _func = avx2_vlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_vlift_32_2tap_rev_analysis; \
          else if (_step->support_length <= 4) \
            _func = avx2_vlift_32_4tap_rev_analysis; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = avx2_vlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for AVX2
#  define AVX2_SET_VLIFT_32_FUNC(_func,_step,_synthesis)
#endif

/*****************************************************************************/
/*                             sse2_vlift_32_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES32 >= 4)
//-----------------------------------------------------------------------------
static void sse2_vlift_32_2tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
                                     kdu_int32 *dst_out, int samples,
                                     kd_lifting_step *step, bool for_synthesis)
  /* Does either analysis or synthesis, working with floating point sample
     values.  The 32-bit integer types on the supplied buffers are only for
     simplicity of invocation; they must be cast to floats. */
{
  assert((step->support_length == 1) || (step->support_length == 2));
  float lambda0 = step->coeffs[0];
  float lambda1 = 0.0F;
  __m128 *sp0 = (__m128 *) src[0];
  __m128 *sp1 = sp0; // In case we have only 1 tap
  if (step->support_length == 2)
    { lambda1 = step->coeffs[1]; sp1 = (__m128 *) src[1]; }
  __m128 *dp_in = (__m128 *) dst_in;
  __m128 *dp_out = (__m128 *) dst_out;
  __m128 val0 = sp0[0];
  __m128 val1 = sp1[0];
  __m128 vec_lambda0, vec_lambda1;
  if (for_synthesis)
    { vec_lambda0=_mm_set1_ps(-lambda0); vec_lambda1=_mm_set1_ps(-lambda1); }
  else
    { vec_lambda0=_mm_set1_ps( lambda0); vec_lambda1=_mm_set1_ps( lambda1); }
  samples = (samples+3)>>2;
  for (int c=0; c < samples; c++)
    { 
      __m128 tgt = dp_in[c];
      __m128 prod0 = _mm_mul_ps(val0,vec_lambda0);
      __m128 prod1 = _mm_mul_ps(val1,vec_lambda1);
      prod0 = _mm_add_ps(prod0,prod1);
      val0 = sp0[c+1];
      val1 = sp1[c+1];
      dp_out[c] = _mm_add_ps(tgt,prod0);
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_32_4tap_irrev(kdu_int32 **src, kdu_int32 *dst_in,
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
  __m128 *sp0 = (__m128 *) src[0];
  __m128 *sp1 = (__m128 *) src[1];
  __m128 *sp2 = (__m128 *) src[2];
  __m128 *sp3 = sp2; // In case we only have 3 taps
  if (step->support_length==4)
    { lambda3 = step->coeffs[3]; sp3 = (__m128 *) src[3]; }
  __m128 *dp_in = (__m128 *) dst_in;
  __m128 *dp_out = (__m128 *) dst_out;
  __m128 val0 = sp0[0];
  __m128 val1 = sp1[0];
  __m128 val2 = sp2[0];
  __m128 val3 = sp3[0];
  __m128 vec_lambda0, vec_lambda1, vec_lambda2, vec_lambda3;
  if (for_synthesis)
    { vec_lambda0=_mm_set1_ps(-lambda0); vec_lambda1=_mm_set1_ps(-lambda1);
      vec_lambda2=_mm_set1_ps(-lambda2); vec_lambda3=_mm_set1_ps(-lambda3); }
  else
    { vec_lambda0=_mm_set1_ps( lambda0); vec_lambda1=_mm_set1_ps( lambda1);
      vec_lambda2=_mm_set1_ps( lambda2); vec_lambda3=_mm_set1_ps( lambda3); }
  samples = (samples+3)>>2;
  for (int c=0; c < samples; c++)
    { 
      __m128 tgt = dp_in[c];
      __m128 prod0 = _mm_mul_ps(val0,vec_lambda0);
      __m128 prod1 = _mm_mul_ps(val1,vec_lambda1);
      prod0 = _mm_add_ps(prod0,prod1);
      __m128 prod2 = _mm_mul_ps(val2,vec_lambda2);
      __m128 prod3 = _mm_mul_ps(val3,vec_lambda3);
      prod2 = _mm_add_ps(prod2,prod3);
      val0 = sp0[c+1];
      val1 = sp1[c+1];
      prod0 = _mm_add_ps(prod0,prod2);
      val2 = sp2[c+1];
      val3 = sp3[c+1];
      dp_out[c] = _mm_add_ps(tgt,prod0);
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_32_5x3_synth_s0(kdu_int32 **src, kdu_int32 *dst_in,
                                       kdu_int32 *dst_out, int samples,
                                       kd_lifting_step *step,
                                       bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m128i vec_offset= _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=4)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_sub_epi32(val,*((__m128i *)(src1+c))); // Subtract src 1
      val = _mm_sub_epi32(val,*((__m128i *)(src2+c))); // Subtract src 2
      val = _mm_sra_epi32(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_sub_epi32(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
static void sse2_vlift_32_5x3_synth_s1(kdu_int32 **src, kdu_int32 *dst_in,
                                       kdu_int32 *dst_out, int samples,
                                       kd_lifting_step *step,
                                       bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m128i vec_offset= _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=4)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_add_epi32(val,*((__m128i *)(src1+c))); // Add source 1
      val = _mm_add_epi32(val,*((__m128i *)(src2+c))); // Add source 2
      val = _mm_sra_epi32(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_sub_epi32(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static void sse2_vlift_32_5x3_analysis_s0(kdu_int32 **src, kdu_int32 *dst_in,
                                          kdu_int32 *dst_out, int samples,
                                          kd_lifting_step *step,
                                          bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m128i vec_offset= _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=4)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_sub_epi32(val,*((__m128i *)(src1+c))); // Subtract src 1
      val = _mm_sub_epi32(val,*((__m128i *)(src2+c))); // Subtract src 2
      val = _mm_sra_epi32(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_add_epi32(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
static void sse2_vlift_32_5x3_analysis_s1(kdu_int32 **src, kdu_int32 *dst_in,
                                          kdu_int32 *dst_out, int samples,
                                          kd_lifting_step *step,
                                          bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m128i vec_offset= _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  kdu_int32 *src1=src[0], *src2=src[1];  
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=4)
    { 
      __m128i val = vec_offset;  // Start with the offset
      val = _mm_add_epi32(val,*((__m128i *)(src1+c))); // Add source 1
      val = _mm_add_epi32(val,*((__m128i *)(src2+c))); // Add source 2
      val = _mm_sra_epi32(val,downshift);
      __m128i tgt = *((__m128i *)(dst_in+c));
      tgt = _mm_add_epi32(tgt,val);
      *((__m128i *)(dst_out+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_VLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_mmx_level >= 2) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = sse2_vlift_32_5x3_synth_s0; \
          else \
            _func = sse2_vlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = sse2_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = sse2_vlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = sse2_vlift_32_5x3_analysis_s0; \
          else \
            _func = sse2_vlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = sse2_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = sse2_vlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for SSE2
#  define SSE2_SET_VLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_32_FUNC(_func,_step,_synthesis) \
{ \
  SSE2_SET_VLIFT_32_FUNC(_func,_step,_synthesis); \
  AVX2_SET_VLIFT_32_FUNC(_func,_step,_synthesis); \
  AVX2_DWT_DO_STATIC_INIT(); \
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (16-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/*                              avx2_hlift_16_...                            */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_2tap_synth(kdu_int16 *, kdu_int16 *, int,
                             kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_4tap_synth(kdu_int16 *, kdu_int16 *, int,
                             kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_2tap_analysis(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_4tap_analysis(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_5x3_synth_s0(kdu_int16 *, kdu_int16 *, int,
                               kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_5x3_synth_s1(kdu_int16 *, kdu_int16 *, int,
                               kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_5x3_analysis_s0(kdu_int16 *, kdu_int16 *, int,
                                  kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_5x3_analysis_s1(kdu_int16 *, kdu_int16 *, int,
                                  kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_9x7_synth_s0(kdu_int16 *, kdu_int16 *, int,
                               kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_9x7_synth_s1(kdu_int16 *, kdu_int16 *, int,
                               kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_9x7_synth_s23(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_16_9x7_analysis_s0(kdu_int16 *, kdu_int16 *, int,
                                  kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_9x7_analysis_s1(kdu_int16 *, kdu_int16 *, int,
                                  kd_lifting_step *, bool);
  extern void
    avx2_hlift_16_9x7_analysis_s23(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define AVX2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 7) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_16_5x3_synth_s0; \
          else \
            _func = avx2_hlift_16_5x3_synth_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_16_9x7_synth_s0; \
          else if (_step->step_idx == 1) \
            _func = avx2_hlift_16_9x7_synth_s1; \
          else \
            _func = avx2_hlift_16_9x7_synth_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = avx2_hlift_16_2tap_synth; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = avx2_hlift_16_4tap_synth; _add_first = false; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_16_5x3_analysis_s0; \
          else \
            _func = avx2_hlift_16_5x3_analysis_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_16_9x7_analysis_s0; \
          else if (_step->step_idx == 1) \
            _func = avx2_hlift_16_9x7_analysis_s1; \
          else \
            _func = avx2_hlift_16_9x7_analysis_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = avx2_hlift_16_2tap_analysis; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = avx2_hlift_16_4tap_analysis; _add_first = false; \
        } \
    } \
}
#else // No compilation support for AVX2
#  define AVX2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* EXTERN                      ssse3_hlift_16_...                            */
/*****************************************************************************/

#if (!defined KDU_NO_SSSE3) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
  extern void
    ssse3_hlift_16_9x7_synth_s0(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    ssse3_hlift_16_9x7_synth_s1(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    ssse3_hlift_16_9x7_synth_s23(kdu_int16 *, kdu_int16 *, int,
                                 kd_lifting_step *, bool);
  extern void
    ssse3_hlift_16_9x7_analysis_s0(kdu_int16 *, kdu_int16 *, int,
                                    kd_lifting_step *, bool);
  extern void
    ssse3_hlift_16_9x7_analysis_s1(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
  extern void
    ssse3_hlift_16_9x7_analysis_s23(kdu_int16 *, kdu_int16 *, int,
                                    kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define SSSE3_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 4) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = ssse3_hlift_16_9x7_synth_s0; \
          else if (_step->step_idx == 1) \
            _func = ssse3_hlift_16_9x7_synth_s1; \
          else \
            _func = ssse3_hlift_16_9x7_synth_s23; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = ssse3_hlift_16_9x7_analysis_s0; \
          else if (_step->step_idx == 1) \
            _func = ssse3_hlift_16_9x7_analysis_s1; \
          else \
            _func = ssse3_hlift_16_9x7_analysis_s23; \
        } \
    } \
}
#else // No compilation support for SSSE3
#  define SSSE3_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* STATIC                      sse2_hlift_16_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_2tap_synth(kdu_int16 *src, kdu_int16 *dst,
                           int samples, kd_lifting_step *step,
                           bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  assert(for_synthesis); // This implementation does synthesis only
  kdu_int32 lambda_coeffs = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  if (step->support_length == 2)
    lambda_coeffs |= ((kdu_int32) step->icoeffs[1]) << 16;
  
  __m128i vec_lambda = _mm_set1_epi32(lambda_coeffs);
  __m128i vec_offset = _mm_set1_epi32(step->rounding_offset);
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i mask = _mm_setzero_si128(); // Avoid compiler warnings
  mask = _mm_cmpeq_epi32(mask,mask); // Fill register with 1's
  mask = _mm_srli_epi32(mask,16); // Leaves mask for low words in each dword
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val0 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c+1));
      val0 = _mm_madd_epi16(val0,vec_lambda);
      val0 = _mm_add_epi32(val0,vec_offset);
      val0 = _mm_sra_epi32(val0,downshift);
      val1 = _mm_madd_epi16(val1,vec_lambda);
      val1 = _mm_add_epi32(val1,vec_offset);
      val1 = _mm_sra_epi32(val1,downshift);
      __m128i tgt = *((__m128i *)(dst+c));
      val0 = _mm_and_si128(val0,mask); // Zero out high words of `val0'
      val1 = _mm_slli_epi32(val1,16); // Move `val1' results into high words
      tgt = _mm_sub_epi16(tgt,val0);
      tgt = _mm_sub_epi16(tgt,val1);
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_4tap_synth(kdu_int16 *src, kdu_int16 *dst,
                           int samples, kd_lifting_step *step,
                           bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  assert(for_synthesis); // This implementation does synthesis only
  kdu_int32 lambda_coeffs0 = ((kdu_int32) step->icoeffs[0]) & 0x0000FFFF;
  lambda_coeffs0 |= ((kdu_int32) step->icoeffs[1]) << 16;
  kdu_int32 lambda_coeffs2 = ((kdu_int32) step->icoeffs[2]) & 0x0000FFFF;
  if (step->support_length == 4)
    lambda_coeffs2 |= ((kdu_int32) step->icoeffs[3]) << 16;
  
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  __m128i vec_offset = _mm_set1_epi32(step->rounding_offset);
  __m128i vec_lambda0 = _mm_set1_epi32(lambda_coeffs0);
  __m128i vec_lambda2 = _mm_set1_epi32(lambda_coeffs2);
  __m128i mask = _mm_setzero_si128(); // Avoid compiler warnings
  mask = _mm_cmpeq_epi32(mask,mask); // Fill register with 1's
  mask = _mm_srli_epi32(mask,16); // Leaves mask for low words in each dword
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val0 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+2));
      val0 = _mm_madd_epi16(val0,vec_lambda0);
      val2 = _mm_madd_epi16(val2,vec_lambda2);
      val0 = _mm_add_epi32(val0,val2); // Add products
      val0 = _mm_add_epi32(val0,vec_offset); // Add rounding offset
      val0 = _mm_sra_epi32(val0,downshift); // Shift -> results in low words
      val0 = _mm_and_si128(val0,mask); // Zero out high order words
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c+1));
      __m128i val3 = _mm_loadu_si128((__m128i *)(src+c+3));
      val1 = _mm_madd_epi16(val1,vec_lambda0);
      val3 = _mm_madd_epi16(val3,vec_lambda2);
      val1 = _mm_add_epi32(val1,val3); // Add products
      val1 = _mm_add_epi32(val1,vec_offset); // Add rounding offset
      val1 = _mm_sra_epi32(val1,downshift); // Shift -> results in low words
      val1 = _mm_slli_epi32(val1,16); // Move result to high order words
      __m128i tgt = *((__m128i *)(dst+c)); // Load target value
      val0 = _mm_or_si128(val0,val1); // Put the low and high words together
      tgt = _mm_sub_epi16(tgt,val0);
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_5x3_synth_s0(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  __m128i vec_offset = _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
      __m128i val = vec_offset; // Start with offset
      val = _mm_sub_epi16(val,val1);    // Subtract source 1
      val = _mm_sub_epi16(val,val2);  // Subtract source 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst+c)) = tgt;
    }
}  
static inline void
  sse2_hlift_16_5x3_synth_s1(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  __m128i vec_offset = _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
      __m128i val = _mm_add_epi16(val1,vec_offset);
      val = _mm_add_epi16(val,val2);
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_sub_epi16(tgt,val);
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_5x3_analysis_s0(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  __m128i vec_offset = _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  assert(step->icoeffs[0] == -1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
      __m128i val = vec_offset; // Start with offset
      val = _mm_sub_epi16(val,val1);    // Subtract source 1
      val = _mm_sub_epi16(val,val2);  // Subtract source 2
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst+c)) = tgt;
    }
}
static inline void
  sse2_hlift_16_5x3_analysis_s1(kdu_int16 *src, kdu_int16 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  __m128i vec_offset = _mm_set1_epi16((kdu_int16)((1<<step->downshift)>>1));
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  assert(step->icoeffs[0] == 1);
  for (int c=0; c < samples; c+=8)
    { 
      __m128i val1 = _mm_loadu_si128((__m128i *)(src+c));
      __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
      __m128i val = _mm_add_epi16(val1,vec_offset);
      val = _mm_add_epi16(val,val2);
      val = _mm_sra_epi16(val,downshift);
      __m128i tgt = *((__m128i *)(dst+c));
      tgt = _mm_add_epi16(tgt,val);
      *((__m128i *)(dst+c)) = tgt;
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_9x7_synth(kdu_int16 *src, kdu_int16 *dst,
                          int samples, kd_lifting_step *step,
                          bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(for_synthesis); // This implementation does synthesis only
  __m128i vec_lambda = _mm_set1_epi16(simd_w97_rem[step_idx]);
  __m128i vec_offset = _mm_set1_epi16(simd_w97_preoff[step_idx]);
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_add_epi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_add_epi16(tgt,val); // Another -1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m128i roff = _mm_setzero_si128();
      __m128i tmp = _mm_cmpeq_epi16(roff,roff); // Set to all 1's
      roff = _mm_sub_epi16(roff,tmp); // Leaves each word in `roff' = 1
      roff = _mm_slli_epi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=8)
        { 
          __m128i tmp = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val1 = _mm_setzero_si128();
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val2 = _mm_mulhi_epi16(val2,vec_lambda);
          val1 = _mm_sub_epi16(val1,tmp);
          val1 = _mm_mulhi_epi16(val1,vec_lambda);
          __m128i val = _mm_sub_epi16(val2,val1); // +ve minus -ve source
          val = _mm_add_epi16(val,roff); // Add rounding offset
          val = _mm_srai_epi16(val,3);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_sub_epi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_sub_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_16_9x7_analysis(kdu_int16 *src, kdu_int16 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(!for_synthesis); // This implementation does analysis only
  __m128i vec_lambda = _mm_set1_epi16(simd_w97_rem[step_idx]);
  __m128i vec_offset = _mm_set1_epi16(simd_w97_preoff[step_idx]);
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_sub_epi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_sub_epi16(tgt,val); // Another -1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_epi16(tgt,val); // Final contribution
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m128i roff = _mm_setzero_si128();
      __m128i tmp = _mm_cmpeq_epi16(roff,roff); // Set to all 1's
      roff = _mm_sub_epi16(roff,tmp); // Leaves each word in `roff' = 1
      roff = _mm_slli_epi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=8)
        { 
          __m128i tmp = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val1 = _mm_setzero_si128();
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val2 = _mm_mulhi_epi16(val2,vec_lambda);
          val1 = _mm_sub_epi16(val1,tmp);
          val1 = _mm_mulhi_epi16(val1,vec_lambda);
          __m128i val = _mm_sub_epi16(val2,val1); // +ve minus -ve source
          val = _mm_add_epi16(val,roff); // Add rounding offset
          val = _mm_srai_epi16(val,3);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_add_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          tgt = _mm_add_epi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_add_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=8)
        { 
          __m128i val = _mm_loadu_si128((__m128i *)(src+c));
          __m128i val2 = _mm_loadu_si128((__m128i *)(src+c+1));
          val = _mm_add_epi16(val,val2);
          __m128i tgt = *((__m128i *)(dst+c));
          val = _mm_add_epi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_epi16(val,vec_lambda);
          tgt = _mm_add_epi16(tgt,val);
          *((__m128i *)(dst+c)) = tgt;
        }
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 2) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = sse2_hlift_16_5x3_synth_s0; \
          else \
            _func = sse2_hlift_16_5x3_synth_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          _func = sse2_hlift_16_9x7_synth; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = sse2_hlift_16_2tap_synth; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = sse2_hlift_16_4tap_synth; _add_first = false; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = sse2_hlift_16_5x3_analysis_s0; \
          else \
            _func = sse2_hlift_16_5x3_analysis_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          _func = sse2_hlift_16_9x7_analysis; \
        } \
    } \
}
#else // No compilation support for SSE2
#  define SSE2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif


/*****************************************************************************/
/* STATIC                      mmx_hlift_16_...                              */
/*****************************************************************************/

#if ((!defined KDU_NO_MMX64) && (KDU_MMX_LEVEL < 2))
//-----------------------------------------------------------------------------
static inline void
  mmx_hlift_16_5x3_synth(kdu_int16 *src, kdu_int16 *dst,
                         int samples, kd_lifting_step *step,
                         bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This implementation does synthesis only
  __m64 vec_offset = _mm_set1_pi16((kdu_int16)((1<<step->downshift)>>1));
  __m64 downshift = _mm_cvtsi32_si64(step->downshift);
  if (step->icoeffs[0] == 1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset; // Start with offset
        val = _mm_add_pi16(val,*((__m64 *)(src+c)));    // Add source 1
        val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));  // Add source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst+c));
        tgt = _mm_sub_pi16(tgt,val);
        *((__m64 *)(dst+c)) = tgt;
      }
  else if (step->icoeffs[0] == -1)
    for (register int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset; // Start with offset
        val = _mm_sub_pi16(val,*((__m64 *)(src+c)));    // Subtract source 1
        val = _mm_sub_pi16(val,*((__m64 *)(src+c+1)));  // Subtract source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst+c));
        tgt = _mm_sub_pi16(tgt,val);
        *((__m64 *)(dst+c)) = tgt;
      }
  else
    assert(0);
  _mm_empty();
}
//-----------------------------------------------------------------------------
static inline void
  mmx_hlift_16_5x3_analysis(kdu_int16 *src, kdu_int16 *dst,
                            int samples, kd_lifting_step *step,
                            bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis); // This implementation does analysis only
  __m64 vec_offset = _mm_set1_pi16((kdu_int16)((1<<step->downshift)>>1));
  __m64 downshift = _mm_cvtsi32_si64(step->downshift);
  if (step->icoeffs[0] == 1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset; // Start with offset
        val = _mm_add_pi16(val,*((__m64 *)(src+c)));   // Add source 1
        val = _mm_add_pi16(val,*((__m64 *)(src+c+1))); // Add source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst+c));
        tgt = _mm_add_pi16(tgt,val);
        *((__m64 *)(dst+c)) = tgt;
      }
  else if (step->icoeffs[0] == -1)
    for (int c=0; c < samples; c+=4)
      { 
        __m64 val = vec_offset; // Start with offset
        val = _mm_sub_pi16(val,*((__m64 *)(src+c)));   // Subtract source 1
        val = _mm_sub_pi16(val,*((__m64 *)(src+c+1))); // Subtract source 2
        val = _mm_sra_pi16(val,downshift);
        __m64 tgt = *((__m64 *)(dst+c));
        tgt = _mm_add_pi16(tgt,val);
        *((__m64 *)(dst+c)) = tgt;
      }
  else
    assert(0);
  _mm_empty();
}
//-----------------------------------------------------------------------------
static inline void
  mmx_hlift_16_9x7_synth(kdu_int16 *src, kdu_int16 *dst,
                         int samples, kd_lifting_step *step,
                         bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(for_synthesis); // This implementation does synthesis only
  __m64 vec_lambda = _mm_set1_pi16(simd_w97_rem[step_idx]);
  __m64 vec_offset = _mm_set1_pi16(simd_w97_preoff[step_idx]);
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_add_pi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_add_pi16(tgt,val); // Another -1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 1)
    { 
      __m64 roff = _mm_setzero_si64();
      __m64 tmp = _mm_cmpeq_pi16(roff,roff); // Set to all 1's
      roff = _mm_sub_pi16(roff,tmp); // Leaves each word in `roff' = 1
      roff = _mm_slli_pi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val1 = *((__m64 *)(src+c));
          val1 = _mm_mulhi_pi16(val1,vec_lambda);
          __m64 val2 = _mm_setzero_si64();
          val2 = _mm_sub_pi16(val2,*((__m64 *)(src+c+1)));
          val2 = _mm_mulhi_pi16(val2,vec_lambda);
          __m64 val = _mm_sub_pi16(val1,val2); // +ve minus -ve source
          val = _mm_add_pi16(val,roff); // Add rounding offset
          val = _mm_srai_pi16(val,3);
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_sub_pi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          val = _mm_add_pi16(val,vec_offset); // Add rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda);
          tgt = _mm_sub_pi16(tgt,val);
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
static inline void
  mmx_hlift_16_9x7_analysis(kdu_int16 *src, kdu_int16 *dst,
                            int samples, kd_lifting_step *step,
                            bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(!for_synthesis); // This implementation does analysis only
  __m64 vec_lambda = _mm_set1_pi16(simd_w97_rem[step_idx]);
  __m64 vec_offset = _mm_set1_pi16(simd_w97_preoff[step_idx]);
  if (step_idx == 0)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_sub_pi16(tgt,val); // Here is a -1 contribution
          tgt = _mm_sub_pi16(tgt,val); // Another -1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst+c)) = tgt;
      }
    }
  else if (step_idx == 1)
    { 
      __m64 roff = _mm_setzero_si64();
      __m64 tmp = _mm_cmpeq_pi16(roff,roff); // Set to all 1's
      roff = _mm_sub_pi16(roff,tmp); // Leaves each word in `roff' equal to 1
      roff = _mm_slli_pi16(roff,2); // Leave each word in `roff' = 4
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val1 = *((__m64 *)(src+c));                 // Get +ve source 1
          val1 = _mm_mulhi_pi16(val1,vec_lambda); // * lambda & discard 16 LSBs
          __m64 val2 = _mm_setzero_si64();
          val2 = _mm_sub_pi16(val2,*((__m64 *)(src+c+1))); // Get -ve source 2
          val2 = _mm_mulhi_pi16(val2,vec_lambda); // * lambda & discard 16 LSBs
          __m64 val = _mm_sub_pi16(val1,val2); // Subract -ve from +ve source
          val = _mm_add_pi16(val,roff); // Add rounding offset
          val = _mm_srai_pi16(val,3); // Shift result to the right by 3
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_add_pi16(tgt,val); // Update destination samples
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  else if (step_idx == 2)
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          tgt = _mm_add_pi16(tgt,val); // Here is a +1 contribution
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  else
    { 
      for (int c=0; c < samples; c+=4)
        { 
          __m64 val = *((__m64 *)(src+c));
          val = _mm_add_pi16(val,*((__m64 *)(src+c+1)));
          __m64 tgt = *((__m64 *)(dst+c));
          val = _mm_add_pi16(val,vec_offset); // Add the rounding pre-offset
          val = _mm_mulhi_pi16(val,vec_lambda); // * lambda & discard 16 LSBs
          tgt = _mm_add_pi16(tgt,val); // Final contribution
          *((__m64 *)(dst+c)) = tgt;
        }
    }
  _mm_empty(); // Clear MMX registers for use by FPU
}
//-----------------------------------------------------------------------------
#  define MMX_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_mmx_level >= 1) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _func = mmx_hlift_16_5x3_synth; _add_first = true; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _func = mmx_hlift_16_9x7_synth; \
          _add_first = (_step->step_idx != 1); \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _func = mmx_hlift_16_5x3_analysis; _add_first = true; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _func = mmx_hlift_16_9x7_analysis; \
          _add_first = (_step->step_idx != 1); \
        } \
    } \
}
#else // No compilation support for MMX
#  define MMX_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  MMX_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSE2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSSE3_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  AVX2_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  SSSE3_DWT_DO_STATIC_INIT(); \
  AVX2_DWT_DO_STATIC_INIT(); \
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (32-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/*                             avx2_hlift_32_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES32 >= 8)
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_32_2tap_irrev(kdu_int32 *, kdu_int32 *, int,
                             kd_lifting_step *, bool);
  extern void
    avx2_hlift_32_4tap_irrev(kdu_int32 *, kdu_int32 *, int,
                             kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_32_2tap_rev_synth(kdu_int32 *, kdu_int32 *, int,
                                 kd_lifting_step *, bool);
  extern void
    avx2_hlift_32_4tap_rev_synth(kdu_int32 *, kdu_int32 *, int,
                                 kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_32_2tap_rev_analysis(kdu_int32 *, kdu_int32 *, int,
                                    kd_lifting_step *, bool);
  extern void
    avx2_hlift_32_4tap_rev_analysis(kdu_int32 *, kdu_int32 *, int,
                                    kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_32_5x3_synth_s0(kdu_int32 *, kdu_int32 *, int,
                               kd_lifting_step *, bool);
  extern void
    avx2_hlift_32_5x3_synth_s1(kdu_int32 *, kdu_int32 *, int,
                               kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    avx2_hlift_32_5x3_analysis_s0(kdu_int32 *, kdu_int32 *, int,
                                  kd_lifting_step *, bool);
  extern void
    avx2_hlift_32_5x3_analysis_s1(kdu_int32 *, kdu_int32 *, int,
                                  kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define AVX2_SET_HLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_mmx_level >= 7) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_32_5x3_synth_s0; \
          else \
            _func = avx2_hlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_hlift_32_2tap_rev_synth; \
          else if (_step->support_length <= 4) \
            _func = avx2_hlift_32_4tap_rev_synth; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = avx2_hlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = avx2_hlift_32_5x3_analysis_s0; \
          else \
            _func = avx2_hlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_hlift_32_2tap_rev_analysis; \
          else if (_step->support_length <= 4) \
            _func = avx2_hlift_32_4tap_rev_analysis; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = avx2_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = avx2_hlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for AVX2
#  define AVX2_SET_HLIFT_32_FUNC(_func,_step,_synthesis)
#endif

/*****************************************************************************/
/* STATIC                      sse2_hlift_32_...                             */
/*****************************************************************************/

#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES32 >= 4)
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_32_2tap_irrev(kdu_int32 *src, kdu_int32 *dst,
                           int samples, kd_lifting_step *step,
                           bool for_synthesis)
{
  assert((step->support_length == 1) || (step->support_length == 2));
  int quad_bytes = ((samples+3) & ~3)<<2;
  float lambda0 = step->coeffs[0];
  float lambda1 = (step->support_length==2)?(step->coeffs[1]):0.0F;
  __m128 *dp = (__m128 *) dst; // Always aligned
  __m128 *dp_lim = (__m128 *)(((kdu_byte *) dp)+quad_bytes);
  float *sp = (float *)src;
  __m128 val0=_mm_loadu_ps(sp); // Pre-load first operand
  __m128 val1=_mm_loadu_ps(sp+1); // Pre-load second operand
  __m128 vec_lambda0, vec_lambda1;
  if (for_synthesis)
    { vec_lambda0=_mm_set1_ps(-lambda0); vec_lambda1=_mm_set1_ps(-lambda1); }
  else
    { vec_lambda0=_mm_set1_ps( lambda0); vec_lambda1=_mm_set1_ps( lambda1); }
  for (; dp < dp_lim; dp++, sp+=4)
    { 
      __m128 prod0 = _mm_mul_ps(val0,vec_lambda0);
      __m128 prod1 = _mm_mul_ps(val1,vec_lambda1);
      __m128 tgt = *dp;
      val0 = _mm_loadu_ps(sp+4);
      prod0 = _mm_add_ps(prod0,prod1);
      val1 = _mm_loadu_ps(sp+5);
      *dp = _mm_add_ps(tgt,prod0);
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_32_4tap_irrev(kdu_int32 *src, kdu_int32 *dst,
                           int samples, kd_lifting_step *step,
                           bool for_synthesis)
{
  assert((step->support_length >= 3) && (step->support_length <= 4));
  int quad_bytes = ((samples+3) & ~3)<<2;
  float lambda0 = step->coeffs[0];
  float lambda1 = step->coeffs[1];
  float lambda2 = step->coeffs[2];
  float lambda3 = (step->support_length==4)?(step->coeffs[3]):0.0F;
  __m128 *dp = (__m128 *) dst; // Always aligned
  __m128 *dp_lim = (__m128 *)(((kdu_byte *) dp)+quad_bytes);
  float *sp = (float *) src;
  __m128 val0=_mm_loadu_ps(sp); // Pre-load first operand
  __m128 val1=_mm_loadu_ps(sp+1); // Pre-load second operand
  __m128 val2=_mm_loadu_ps(sp+2); // Pre-load second operand
  __m128 val3=_mm_loadu_ps(sp+3); // Pre-load second operand
  __m128 vec_lambda0, vec_lambda1, vec_lambda2, vec_lambda3;
  if (for_synthesis)
    { vec_lambda0=_mm_set1_ps(-lambda0); vec_lambda1=_mm_set1_ps(-lambda1);
      vec_lambda2=_mm_set1_ps(-lambda2); vec_lambda3=_mm_set1_ps(-lambda3); }
  else
    { vec_lambda0=_mm_set1_ps( lambda0); vec_lambda1=_mm_set1_ps( lambda1);
      vec_lambda2=_mm_set1_ps( lambda2); vec_lambda3=_mm_set1_ps( lambda3); }
  for (; dp < dp_lim; dp++, sp+=4)
    { 
      __m128 prod0 = _mm_mul_ps(val0,vec_lambda0);
      __m128 prod1 = _mm_mul_ps(val1,vec_lambda1);
      prod0 = _mm_add_ps(prod0,prod1);
      __m128 prod2 = _mm_mul_ps(val2,vec_lambda2);
      __m128 prod3 = _mm_mul_ps(val3,vec_lambda3);
      __m128 tgt = *dp;
      prod2 = _mm_add_ps(prod2,prod3);
      val0 = _mm_loadu_ps(sp+4);
      val1 = _mm_loadu_ps(sp+5);
      prod0 = _mm_add_ps(prod0, prod2);
      val2 = _mm_loadu_ps(sp+6);
      *dp = _mm_add_ps(tgt,prod0);
      val3 = _mm_loadu_ps(sp+7);
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_32_5x3_synth_s0(kdu_int32 *src, kdu_int32 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 0) && for_synthesis);
  int quad_bytes = ((samples+3) & ~3)<<2;
  int src_aligned = ((_addr_to_kdu_int32(src) & 0x0F) == 0);
  __m128i vec_offset = _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i *dp = (__m128i *) dst; // Always aligned
  __m128i *dp_lim = (__m128i *)(((kdu_byte *) dp)+quad_bytes);
  __m128i *sp_a = (__m128i *) src; // Aligned pointer
  __m128i *sp_u = (__m128i *)(src+1); // Unaligned pointer
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  if (!src_aligned)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      sp_a = (__m128i *)(src+1); // Aligned pointer
      sp_u = (__m128i *) src; // Unaligned pointer
    }
  __m128i val_u = _mm_loadu_si128(sp_u); // Preload unaligned dqword
  assert(step->icoeffs[0] == -1);
  for (; dp < dp_lim; dp++)
    { 
      __m128i val = *(sp_a++);
      __m128i tgt = *dp;
      val = _mm_sub_epi32(vec_offset,val);
      val = _mm_sub_epi32(val,val_u);
      val_u = _mm_loadu_si128(++sp_u); // Pre-load unaligned dqword
      val = _mm_sra_epi32(val,downshift);
      *dp = _mm_sub_epi32(tgt,val);
    }
}
static inline void
  sse2_hlift_32_5x3_synth_s1(kdu_int32 *src, kdu_int32 *dst,
                             int samples, kd_lifting_step *step,
                             bool for_synthesis)
{
  assert((step->step_idx == 1) && for_synthesis);
  int quad_bytes = ((samples+3) & ~3)<<2;
  int src_aligned = ((_addr_to_kdu_int32(src) & 0x0F) == 0);
  __m128i vec_offset = _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i *dp = (__m128i *) dst; // Always aligned
  __m128i *dp_lim = (__m128i *)(((kdu_byte *) dp)+quad_bytes);
  __m128i *sp_a = (__m128i *) src; // Aligned pointer
  __m128i *sp_u = (__m128i *)(src+1); // Unaligned pointer
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  if (!src_aligned)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      sp_a = (__m128i *)(src+1); // Aligned pointer
      sp_u = (__m128i *) src; // Unaligned pointer
    }
  __m128i val_u = _mm_loadu_si128(sp_u); // Preload unaligned dqword
  assert(step->icoeffs[0] == 1);
  for (; dp < dp_lim; dp++)
    { 
      __m128i val = *(sp_a++);
      __m128i tgt = *dp;
      val = _mm_add_epi32(val,vec_offset);
      val = _mm_add_epi32(val,val_u);
      val_u = _mm_loadu_si128(++sp_u); // Pre-load unaligned dqword
      val = _mm_sra_epi32(val,downshift);
      *dp = _mm_sub_epi32(tgt,val);
    }
}
//-----------------------------------------------------------------------------
static inline void
  sse2_hlift_32_5x3_analysis_s0(kdu_int32 *src, kdu_int32 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 0) && !for_synthesis);
  int quad_bytes = ((samples+3) & ~3)<<2;
  int src_aligned = ((_addr_to_kdu_int32(src) & 0x0F) == 0);
  __m128i vec_offset = _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i *dp = (__m128i *) dst; // Always aligned
  __m128i *dp_lim = (__m128i *)(((kdu_byte *) dp)+quad_bytes);
  __m128i *sp_a = (__m128i *) src; // Aligned pointer
  __m128i *sp_u = (__m128i *)(src+1); // Unaligned pointer
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  if (!src_aligned)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      sp_a = (__m128i *)(src+1); // Aligned pointer
      sp_u = (__m128i *) src; // Unaligned pointer
    }
  __m128i val_u = _mm_loadu_si128(sp_u); // Preload unaligned dqword
  assert(step->icoeffs[0] == -1);
  for (; dp < dp_lim; dp++)
    { 
      __m128i val = *(sp_a++);
      __m128i tgt = *dp;
      val = _mm_sub_epi32(vec_offset,val);
      val = _mm_sub_epi32(val,val_u);
      val_u = _mm_loadu_si128(++sp_u); // Pre-load unaligned dqword
      val = _mm_sra_epi32(val,downshift);
      *dp = _mm_add_epi32(tgt,val);
    }
}
static inline void
  sse2_hlift_32_5x3_analysis_s1(kdu_int32 *src, kdu_int32 *dst,
                                int samples, kd_lifting_step *step,
                                bool for_synthesis)
{
  assert((step->step_idx == 1) && !for_synthesis);
  int quad_bytes = ((samples+3) & ~3)<<2;
  int src_aligned = ((_addr_to_kdu_int32(src) & 0x0F) == 0);
  __m128i vec_offset = _mm_set1_epi32((1<<step->downshift)>>1);
  __m128i *dp = (__m128i *) dst; // Always aligned
  __m128i *dp_lim = (__m128i *)(((kdu_byte *) dp)+quad_bytes);
  __m128i *sp_a = (__m128i *) src; // Aligned pointer
  __m128i *sp_u = (__m128i *)(src+1); // Unaligned pointer
  __m128i downshift = _mm_cvtsi32_si128(step->downshift);
  if (!src_aligned)
    { // Make sure `src_u' holds the unaligned of the two source addresses
      sp_a = (__m128i *)(src+1); // Aligned pointer
      sp_u = (__m128i *) src; // Unaligned pointer
    }
  __m128i val_u = _mm_loadu_si128(sp_u); // Preload unaligned dqword
  assert(step->icoeffs[0] == 1);
  for (; dp < dp_lim; dp++)
    { 
      __m128i val = *(sp_a++);
      __m128i tgt = *dp;
      val = _mm_add_epi32(val,vec_offset);
      val = _mm_add_epi32(val,val_u);
      val_u = _mm_loadu_si128(++sp_u); // Pre-load unaligned dqword
      val = _mm_sra_epi32(val,downshift);
      *dp = _mm_add_epi32(tgt,val);
    }
}
//-----------------------------------------------------------------------------
#  define SSE2_SET_HLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_mmx_level >= 2) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = sse2_hlift_32_5x3_synth_s0; \
          else \
            _func = sse2_hlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = sse2_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = sse2_hlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = sse2_hlift_32_5x3_analysis_s0; \
          else \
            _func = sse2_hlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = sse2_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = sse2_hlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for SSE2
#  define SSE2_SET_HLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_32_FUNC(_func,_step,_synthesis) \
{ \
  SSE2_SET_HLIFT_32_FUNC(_func,_step,_synthesis); \
  AVX2_SET_HLIFT_32_FUNC(_func,_step,_synthesis); \
  AVX2_DWT_DO_STATIC_INIT(); \
}
  
} // namespace kd_core_simd

#endif // X86_DWT_LOCAL_H
