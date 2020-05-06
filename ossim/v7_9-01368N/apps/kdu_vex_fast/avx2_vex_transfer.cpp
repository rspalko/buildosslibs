/*****************************************************************************/
// File: avx2_vex_transfer.cpp [scope = APPS/VEX_FAST]
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
sample data produced by the "kdu_vex_fast" demo app into frame buffers.  The
This file provides implementations that require AVX2 support.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_AVX2) && (defined KDU_X86_INTRINSICS))

#ifdef _MSC_VER
#  include <intrin.h>
#else
#  include <immintrin.h>
#endif // !_MSC_VER

#include <assert.h>

namespace kd_supp_simd {
  using namespace kdu_core;

typedef union { kdu_uint32 dwords[8]; __m256i m256i; } kd_u256;

// The following permutation control vector is used by the
// `avx2_vex_mono16_to_argb8' and `avx2_vex_rgb16_to_argb8' functions,
// where an explanation is provided.
static kd_u256 vex_argb8_permd; // See `avx2_vex_transfer_static_init'

/* ========================================================================= */
/*                         Safe Static Initializers                          */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN               avx2_vex_transfer_static_init                        */
/*****************************************************************************/

void avx2_vex_transfer_static_init()
{ // Static initializers are potentially dangerous, so we initialize here
  kdu_uint32 argb8_permd[8] = {0,4,2,6,1,5,3,7};
  for (int n=0; n < 8; n++)
    vex_argb8_permd.dwords[n] = argb8_permd[n];
}


/* ========================================================================= */
/*                    SIMD functions used by `kdu_vex_fast'                  */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                  avx2_vex_mono16_to_xrgb8                          */
/*****************************************************************************/

void avx2_vex_mono16_to_xrgb8(kdu_int16 *src, kdu_byte *dst,
                              int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 31) == 0);
  assert((width & 7) == 0);
  
  // Start by configuring a permutation control vector that rearranges the 32
  // packed bytes produced by running _mm256_packus_epi16 on two vectors of
  // shorts.  On entry, the input vector has organization (high lane to low):
  //     [Bytes 24-31, Bytes 8-15 | Bytes 16-23, Bytes 0-7]
  // On exit, the vector needs to be ready for two cascades of two unpack
  // operations that expand the vector into four vectors, but these operations
  // work on 128-bit lanes.  This means that we need:
  //     [B28-31 B20-23 B12-15 B4-7 |  B24-27 B16-19 B8-11 B0-3]
  // To achieve this, `perm_ctl' needs to hold (high to low dwords):
  //     [7, 3, 5, 1 | 6, 2, 4, 0]
  __m256i perm_ctl = vex_argb8_permd.m256i;
  
  // Now configure the rest of the parameters and do the conversion
  kdu_int16 off16 = (kdu_int16)((255<<downshift)>>1);
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m256i offset = _mm256_set1_epi16(off16);
  __m256i ones = _mm256_setzero_si256();  ones = _mm256_xor_si256(ones,ones);
  __m256i *sp = (__m256i *) src;
  __m256i *dp = (__m256i *) dst;
  for (; width >= 32; width-=32, sp+=2, dp+=4)
    { // Generate output pixels in multiples of 32 at a time
      __m256i val0=sp[0], val1=sp[1];
      val0=_mm256_add_epi16(val0,offset);  val1=_mm256_add_epi16(val1,offset);
      val0=_mm256_sra_epi16(val0,shift);   val1=_mm256_sra_epi16(val1,shift);
      __m256i lum = _mm256_packus_epi16(val0,val1);
      lum = _mm256_permutevar8x32_epi32(lum,perm_ctl);
      __m256i lum_x2 = _mm256_unpacklo_epi8(lum,lum);
      __m256i lum_ones = _mm256_unpacklo_epi8(lum,ones);
      _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(lum_x2,lum_ones));
      _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(lum_x2,lum_ones));
      lum_x2 = _mm256_unpackhi_epi8(lum,lum);
      lum_ones = _mm256_unpackhi_epi8(lum,ones);
      _mm256_stream_si256(dp+2,_mm256_unpacklo_epi16(lum_x2,lum_ones));
      _mm256_stream_si256(dp+3,_mm256_unpackhi_epi16(lum_x2,lum_ones));
    }
  if (width > 0)
    { // Generate the final 1, 2 or 3 output vectors
      __m256i val0=sp[0], val1=sp[1];
      val0=_mm256_add_epi16(val0,offset);  val1=_mm256_add_epi16(val1,offset);
      val0=_mm256_sra_epi16(val0,shift);   val1=_mm256_sra_epi16(val1,shift);
      __m256i lum = _mm256_packus_epi16(val0,val1);
      lum = _mm256_permutevar8x32_epi32(lum,perm_ctl);
      __m256i lum_x2 = _mm256_unpacklo_epi8(lum,lum);
      __m256i lum_ones = _mm256_unpacklo_epi8(lum,ones);
      _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(lum_x2,lum_ones));
      if (width > 1)
        { 
          _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(lum_x2,lum_ones));
          if (width > 2)
            { 
              lum_x2 = _mm256_unpackhi_epi8(lum,lum);
              lum_ones = _mm256_unpackhi_epi8(lum,ones);
              _mm256_stream_si256(dp+2,_mm256_unpacklo_epi16(lum_x2,lum_ones));
            }
        }
    }
}

/*****************************************************************************/
/* EXTERN                   avx2_vex_rgb16_to_xrgb8                          */
/*****************************************************************************/

void avx2_vex_rgb16_to_xrgb8(kdu_int16 *red_src, kdu_int16 *green_src,
                             kdu_int16 *blue_src, kdu_byte *dst,
                             int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 31) == 0);
  assert((width & 7) == 0);

  // Configure permutation control -- see `avx2_vex_mono16_to_xrgb8' for a
  // detailed explanation.
  __m256i perm_ctl = vex_argb8_permd.m256i;

  // Now configure the rest of the parameters and do the conversion
  kdu_int16 off16 = (kdu_int16)((255<<downshift)>>1);
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m256i offset = _mm256_set1_epi16(off16);
  __m256i ones = _mm256_setzero_si256();  ones = _mm256_xor_si256(ones,ones);
  __m256i *rp=(__m256i *)red_src;
  __m256i *gp=(__m256i *)green_src;
  __m256i *bp=(__m256i *)blue_src;
  __m256i *dp = (__m256i *) dst;
  if (downshift == 0)
    { // Processing must have been reversible; no downshifts required
      for (; width >= 32; width-=32, rp+=2, gp+=2, bp+=2, dp+=4)
        { // Generate output pixels in multiples of 32 at a time
          __m256i val0, val1;
          val0=rp[0];  val1=rp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          __m256i red = _mm256_packus_epi16(val0,val1);
          red = _mm256_permutevar8x32_epi32(red,perm_ctl);
          val0=gp[0];  val1=gp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          __m256i green = _mm256_packus_epi16(val0,val1);
          green = _mm256_permutevar8x32_epi32(green,perm_ctl);
          val0=bp[0];  val1=bp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          __m256i blue = _mm256_packus_epi16(val0,val1);
          blue = _mm256_permutevar8x32_epi32(blue,perm_ctl);
          
          __m256i blue_green = _mm256_unpacklo_epi8(blue,green);
          __m256i red_ones = _mm256_unpacklo_epi8(red,ones);
          _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(blue_green,red_ones));
          blue_green = _mm256_unpackhi_epi8(blue,green);
          red_ones = _mm256_unpackhi_epi8(red,ones);
          _mm256_stream_si256(dp+2,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+3,_mm256_unpackhi_epi16(blue_green,red_ones));
        }
    }
  else if (downshift == (KDU_FIX_POINT-8))
    { // Almost certain to be the shift for irreversible processing
      for (; width >= 32; width-=32, rp+=2, gp+=2, bp+=2, dp+=4)
        { // Generate output pixels in multiples of 32 at a time
          __m256i val0, val1;
          val0=rp[0];  val1=rp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_srai_epi16(val0,KDU_FIX_POINT-8);
          val1=_mm256_srai_epi16(val1,KDU_FIX_POINT-8);
          __m256i red = _mm256_packus_epi16(val0,val1);
          red = _mm256_permutevar8x32_epi32(red,perm_ctl);
          val0=gp[0];  val1=gp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_srai_epi16(val0,KDU_FIX_POINT-8);
          val1=_mm256_srai_epi16(val1,KDU_FIX_POINT-8);
          __m256i green = _mm256_packus_epi16(val0,val1);
          green = _mm256_permutevar8x32_epi32(green,perm_ctl);
          val0=bp[0];  val1=bp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_srai_epi16(val0,KDU_FIX_POINT-8);
          val1=_mm256_srai_epi16(val1,KDU_FIX_POINT-8);
          __m256i blue = _mm256_packus_epi16(val0,val1);
          blue = _mm256_permutevar8x32_epi32(blue,perm_ctl);
          
          __m256i blue_green = _mm256_unpacklo_epi8(blue,green);
          __m256i red_ones = _mm256_unpacklo_epi8(red,ones);
          _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(blue_green,red_ones));
          blue_green = _mm256_unpackhi_epi8(blue,green);
          red_ones = _mm256_unpackhi_epi8(red,ones);
          _mm256_stream_si256(dp+2,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+3,_mm256_unpackhi_epi16(blue_green,red_ones));
        }      
    }
  else
    { // General downshift is more costly than immediate one
      for (; width >= 32; width-=32, rp+=2, gp+=2, bp+=2, dp+=4)
        { // Generate output pixels in multiples of 32 at a time
          __m256i val0, val1;
          val0=rp[0];  val1=rp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_sra_epi16(val0,shift);
          val1=_mm256_sra_epi16(val1,shift);
          __m256i red = _mm256_packus_epi16(val0,val1);
          red = _mm256_permutevar8x32_epi32(red,perm_ctl);
          val0=gp[0];  val1=gp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_sra_epi16(val0,shift);
          val1=_mm256_sra_epi16(val1,shift);
          __m256i green = _mm256_packus_epi16(val0,val1);
          green = _mm256_permutevar8x32_epi32(green,perm_ctl);
          val0=bp[0];  val1=bp[1];
          val0=_mm256_add_epi16(val0,offset);
          val1=_mm256_add_epi16(val1,offset);
          val0=_mm256_sra_epi16(val0,shift);
          val1=_mm256_sra_epi16(val1,shift);
          __m256i blue = _mm256_packus_epi16(val0,val1);
          blue = _mm256_permutevar8x32_epi32(blue,perm_ctl);

          __m256i blue_green = _mm256_unpacklo_epi8(blue,green);
          __m256i red_ones = _mm256_unpacklo_epi8(red,ones);
          _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(blue_green,red_ones));
          blue_green = _mm256_unpackhi_epi8(blue,green);
          red_ones = _mm256_unpackhi_epi8(red,ones);
          _mm256_stream_si256(dp+2,_mm256_unpacklo_epi16(blue_green,red_ones));
          _mm256_stream_si256(dp+3,_mm256_unpackhi_epi16(blue_green,red_ones));
        }
    }
  if (width > 0)
    { // Generate the final 1, 2 or 3 output vectors
      __m256i val0, val1;
      val0=rp[0];  val1=rp[1];
      val0=_mm256_add_epi16(val0,offset);  val1=_mm256_add_epi16(val1,offset);
      val0=_mm256_sra_epi16(val0,shift);   val1=_mm256_sra_epi16(val1,shift);
      __m256i red = _mm256_packus_epi16(val0,val1);
      red = _mm256_permutevar8x32_epi32(red,perm_ctl);
      val0=gp[0];  val1=gp[1];
      val0=_mm256_add_epi16(val0,offset);  val1=_mm256_add_epi16(val1,offset);
      val0=_mm256_sra_epi16(val0,shift);   val1=_mm256_sra_epi16(val1,shift);
      __m256i green = _mm256_packus_epi16(val0,val1);
      green = _mm256_permutevar8x32_epi32(green,perm_ctl);
      val0=bp[0];  val1=bp[1];
      val0=_mm256_add_epi16(val0,offset);  val1=_mm256_add_epi16(val1,offset);
      val0=_mm256_sra_epi16(val0,shift);   val1=_mm256_sra_epi16(val1,shift);
      __m256i blue = _mm256_packus_epi16(val0,val1);
      blue = _mm256_permutevar8x32_epi32(blue,perm_ctl);
      __m256i blue_green = _mm256_unpacklo_epi8(blue,green);
      __m256i red_ones = _mm256_unpacklo_epi8(red,ones);
      _mm256_stream_si256(dp+0,_mm256_unpacklo_epi16(blue_green,red_ones));
      if (width > 1)
        { 
          _mm256_stream_si256(dp+1,_mm256_unpackhi_epi16(blue_green,red_ones));
          if (width > 2)
            { 
              blue_green = _mm256_unpackhi_epi8(blue,green);
              red_ones = _mm256_unpackhi_epi8(red,ones);
              _mm256_stream_si256(dp+2,
                                  _mm256_unpacklo_epi16(blue_green,red_ones));
 
            }
        }
    }
}
  
} // namespace kd_supp_simd

#endif // !KDU_NO_AVX2
