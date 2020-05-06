/*****************************************************************************/
// File: x86_vex_transfer_local.h [scope = APPS/VEX_FAST]
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
function prototypes offered here are defined in "kdu_vex.h".  This file
provides macros to arbitrate the selection of appropriate SIMD functions, if
there are any.  The file should be included only if `KDU_X86_INTRINSICS'
is defined.
******************************************************************************/
#ifndef X86_VEX_TRANSFER_LOCAL_H
#define X86_VEX_TRANSFER_LOCAL_H
#include "kdu_arch.h"

#include <emmintrin.h>

namespace kd_supp_simd {
  using namespace kdu_core;

// Safe "static initializer" logic
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_AVX2)
extern void avx2_vex_transfer_static_init();
static bool avx2_vex_transfer_static_inited=false;
# define AVX2_VEX_TRANSFER_DO_STATIC_INIT() \
if (!avx2_vex_transfer_static_inited) \
  { if (kdu_mmx_level >= 7) avx2_vex_transfer_static_init(); \
    avx2_vex_transfer_static_inited=true; }
#else // No compilation support for AVX2
# define AVX2_VEX_TRANSFER_DO_STATIC_INIT() /* Nothing to do */
#endif
//-----------------------------------------------------------------------------


/*****************************************************************************/
/*                Implementations of `vex_mono_to_xrgb8_func'                */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void avx2_vex_mono16_to_xrgb8(kdu_int16 *,kdu_byte *,int,int);
#  define AVX2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 7) && (_align >= 32)) \
        _tgt = (vex_mono_to_xrgb8_func) avx2_vex_mono16_to_xrgb8;
#else // No compile-time support for AVX2-based transfer functions
#  define AVX2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
static void sse2_vex_mono16_to_xrgb8(kdu_int16 *src, kdu_byte *dst,
                                     int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 15) == 0);
  assert((width & 3) == 0);
  kdu_int16 off16 = (kdu_int16)((255<<downshift)>>1);
  __m128i *sp = (__m128i *) src;
  __m128i *dp = (__m128i *) dst;
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m128i offset = _mm_set1_epi16(off16);
  __m128i ones = _mm_setzero_si128();  ones = _mm_xor_si128(ones,ones);
  int quads = (width+3)>>2;
  int s, sextets = quads >> 2;
  for (s=0; s < sextets; s++)
    { // Generate output pixels in multiples of 16 (64 bytes) at a time
      __m128i val0=sp[2*s], val1=sp[2*s+1];
      val0 = _mm_add_epi16(val0,offset);
      val1 = _mm_add_epi16(val1,offset);
      val0 = _mm_sra_epi16(val0,shift);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i lum = _mm_packus_epi16(val0,val1);
      __m128i lum_x2 = _mm_unpacklo_epi8(lum,lum);
      __m128i lum_ones = _mm_unpacklo_epi8(lum,ones);
      _mm_stream_si128(dp+4*s+0,_mm_unpacklo_epi16(lum_x2,lum_ones));
      _mm_stream_si128(dp+4*s+1,_mm_unpackhi_epi16(lum_x2,lum_ones));
      lum_x2 = _mm_unpackhi_epi8(lum,lum);
      lum_ones = _mm_unpackhi_epi8(lum,ones);
      _mm_stream_si128(dp+4*s+2,_mm_unpacklo_epi16(lum_x2,lum_ones));
      _mm_stream_si128(dp+4*s+3,_mm_unpackhi_epi16(lum_x2,lum_ones));
    }
  quads -= sextets<<2;
  for (int c=s<<1; quads > 0; quads-=2, c++)
    { // Generate output pixels in multiples of 8 (32 bytes) at a time
      __m128i val0=sp[c];
      val0 = _mm_add_epi16(val0,offset);   val0 = _mm_sra_epi16(val0,shift);
      __m128i lum = _mm_packus_epi16(val0,val0);
      __m128i lum_x2 = _mm_unpacklo_epi8(lum,lum);
      __m128i lum_ones = _mm_unpacklo_epi8(lum,ones);
      _mm_stream_si128(dp+2*c+0,_mm_unpacklo_epi16(lum_x2,lum_ones));
      if (quads > 1)
        _mm_stream_si128(dp+2*c+1,_mm_unpackhi_epi16(lum_x2,lum_ones));
    }
}
#  define SSE2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 2) && (_align >= 16)) \
        _tgt = (vex_mono_to_xrgb8_func) sse2_vex_mono16_to_xrgb8;
#else // No compile-time support for SSE2-based transfer functions
#  define SSE2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
static void sse2_vex_mono32f_to_xrgb8(float *src, kdu_byte *dst,
                                      int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 15) == 0);
  assert((width & 3) == 0);
  int mxcsr_orig = _mm_getcsr();
  int mxcsr_cur = mxcsr_orig & ~(3<<13); // Reset rounding control bits
  _mm_setcsr(mxcsr_cur);
  __m128 *sp = (__m128 *) src;
  __m128 v0f, v1f, v2f, v3f, scale = _mm_set1_ps(256.0F);
  __m128i *dp = (__m128i *) dst;
  __m128i v0, v1, v2, v3, off = _mm_set1_epi16(128);
  __m128i ones = _mm_setzero_si128();  ones = _mm_xor_si128(ones,ones);
  for (; width >= 16; width-=16, sp+=4, dp+=4)
    { // Generate output pixels in multiples of 16 (64 bytes) at a time
      v0f=sp[0];  v1f=sp[1];        v2f=sp[2];  v3f=sp[3];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v2f = _mm_mul_ps(v2f,scale);  v3f = _mm_mul_ps(v3f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v2 = _mm_cvtps_epi32(v2f);    v3 = _mm_cvtps_epi32(v3f);
      v0 = _mm_packs_epi32(v0,v1);  v2 = _mm_packs_epi32(v2,v3);
      v0 = _mm_adds_epi16(v0,off);  v2 = _mm_adds_epi16(v2,off);
      __m128i lum = _mm_packus_epi16(v0,v2);
      __m128i lum_x2 = _mm_unpacklo_epi8(lum,lum);
      __m128i lum_ones = _mm_unpacklo_epi8(lum,ones);
      _mm_stream_si128(dp+0,_mm_unpacklo_epi16(lum_x2,lum_ones));
      _mm_stream_si128(dp+1,_mm_unpackhi_epi16(lum_x2,lum_ones));
      lum_x2 = _mm_unpackhi_epi8(lum,lum);
      lum_ones = _mm_unpackhi_epi8(lum,ones);
      _mm_stream_si128(dp+2,_mm_unpacklo_epi16(lum_x2,lum_ones));
      _mm_stream_si128(dp+3,_mm_unpackhi_epi16(lum_x2,lum_ones));
    }
  for (; width > 0; width-=8, sp+=2, dp+=2)
    { 
      v0f=sp[0];  v1f=sp[1];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v0 = _mm_packs_epi32(v0,v1);  __m128i lum = _mm_packus_epi16(v0,v0);
      __m128i lum_x2 = _mm_unpacklo_epi8(lum,lum);
      __m128i lum_ones = _mm_unpacklo_epi8(lum,ones);
      _mm_stream_si128(dp+0,_mm_unpacklo_epi16(lum_x2,lum_ones));
      if (width > 4)
        _mm_stream_si128(dp+1,_mm_unpackhi_epi16(lum_x2,lum_ones));
    }
  _mm_setcsr(mxcsr_orig); // Restore rounding control bits
}
#  define SSE2_SET_MONO32F_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 2) && (_align >= 16)) \
        _tgt = (vex_mono_to_xrgb8_func) sse2_vex_mono32f_to_xrgb8;
#else // No compile-time support for SSE2-based transfer functions
#  define SSE2_SET_MONO32F_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------


#define VEX_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width,_absolute,_shorts) \
{ \
  if (_shorts) \
    { \
      SSE2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width); \
      AVX2_SET_MONO16_TO_XRGB8_FUNC(_tgt,_align,_width); \
    } \
  else if (!_absolute) \
    { \
      SSE2_SET_MONO32F_TO_XRGB8_FUNC(_tgt,_align,_width); \
    } \
  AVX2_VEX_TRANSFER_DO_STATIC_INIT(); \
}


/*****************************************************************************/
/*                 Implementations of `vex_rgb_to_xrgb8_func'                */
/*****************************************************************************/

#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void avx2_vex_rgb16_to_xrgb8(kdu_int16 *,kdu_int16 *,kdu_int16 *,
                                    kdu_byte *,int,int);
#  define AVX2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 7) && (_align >= 32)) \
        _tgt = (vex_rgb_to_xrgb8_func) avx2_vex_rgb16_to_xrgb8;
#else // No compile-time support for AVX2-based transfer functions
#  define AVX2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
static void sse2_vex_rgb16_to_xrgb8(kdu_int16 *red, kdu_int16 *green,
                                    kdu_int16 *blue, kdu_byte *dst,
                                    int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 15) == 0);
  assert((width & 3) == 0);
  kdu_int16 off16 = (kdu_int16)((255<<downshift)>>1);
  __m128i *rp = (__m128i *)red;
  __m128i *gp = (__m128i *)green;
  __m128i *bp = (__m128i *)blue;
  __m128i *dp = (__m128i *) dst;
  __m128i shift = _mm_cvtsi32_si128(downshift);
  __m128i offset = _mm_set1_epi16(off16);
  __m128i ones = _mm_setzero_si128();  ones = _mm_xor_si128(ones,ones);
  int quads = (width+3)>>2;
  int s, sextets = quads >> 2;
  for (s=0; s < sextets; s++)
    { // Generate output pixels in multiples of 16 (64 bytes) at a time
      __m128i val0=rp[2*s], val1=rp[2*s+1];
      val0 = _mm_add_epi16(val0,offset);
      val1 = _mm_add_epi16(val1,offset);
      val0 = _mm_sra_epi16(val0,shift);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i red = _mm_packus_epi16(val0,val1);
      val0=gp[2*s], val1=gp[2*s+1];
      val0 = _mm_add_epi16(val0,offset);
      val1 = _mm_add_epi16(val1,offset);
      val0 = _mm_sra_epi16(val0,shift);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i green = _mm_packus_epi16(val0,val1);
      val0=bp[2*s], val1=bp[2*s+1];
      val0 = _mm_add_epi16(val0,offset);
      val1 = _mm_add_epi16(val1,offset);
      val0 = _mm_sra_epi16(val0,shift);
      val1 = _mm_sra_epi16(val1,shift);
      __m128i blue = _mm_packus_epi16(val0,val1);
      __m128i blue_green = _mm_unpacklo_epi8(blue,green);
      __m128i red_ones = _mm_unpacklo_epi8(red,ones);
      _mm_stream_si128(dp+4*s+0,_mm_unpacklo_epi16(blue_green,red_ones));
      _mm_stream_si128(dp+4*s+1,_mm_unpackhi_epi16(blue_green,red_ones));
      blue_green = _mm_unpackhi_epi8(blue,green);
      red_ones = _mm_unpackhi_epi8(red,ones);
      _mm_stream_si128(dp+4*s+2,_mm_unpacklo_epi16(blue_green,red_ones));
      _mm_stream_si128(dp+4*s+3,_mm_unpackhi_epi16(blue_green,red_ones));
    }
  quads -= sextets<<2;
  for (int c=s<<1; quads > 0; quads-=2, c++)
    { // Generate output pixels in multiples of 8 (32 bytes) at a time
      __m128i val0=rp[c];
      val0 = _mm_add_epi16(val0,offset);
      val0 = _mm_sra_epi16(val0,shift);
      __m128i red = _mm_packus_epi16(val0,val0);
      val0=gp[c];
      val0 = _mm_add_epi16(val0,offset);
      val0 = _mm_sra_epi16(val0,shift);
      __m128i green = _mm_packus_epi16(val0,val0);
      val0=bp[c];
      val0 = _mm_add_epi16(val0,offset);
      val0 = _mm_sra_epi16(val0,shift);
      __m128i blue = _mm_packus_epi16(val0,val0);
      __m128i blue_green = _mm_unpacklo_epi8(blue,green);
      __m128i red_ones = _mm_unpacklo_epi8(red,ones);
      _mm_stream_si128(dp+2*c+0,_mm_unpacklo_epi16(blue_green,red_ones));
      if (quads > 1)
        _mm_stream_si128(dp+2*c+1,_mm_unpackhi_epi16(blue_green,red_ones));
    }
}
#  define SSE2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 2) && (_align >= 16)) \
        _tgt = (vex_rgb_to_xrgb8_func) sse2_vex_rgb16_to_xrgb8;
#else // No compile-time support for SSE2-based transfer functions
#  define SSE2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_AVX2) && (KDU_ALIGN_SAMPLES16 >= 16)
extern void avx2_vex_rgb16_to_xrgb8(kdu_int16 *,kdu_int16 *,kdu_int16 *,
                                    kdu_byte *,int,int);
#  define AVX2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 7) && (_align >= 32)) \
        _tgt = (vex_rgb_to_xrgb8_func) avx2_vex_rgb16_to_xrgb8;
#else // No compile-time support for AVX2-based transfer functions
#  define AVX2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------
#if (!defined KDU_NO_SSE) && (KDU_ALIGN_SAMPLES16 >= 8)
static void sse2_vex_rgb32f_to_xrgb8(float *red, float *green, float *blue,
                                     kdu_byte *dst, int width, int downshift)
{
  assert((_addr_to_kdu_int32(dst) & 15) == 0);
  assert((width & 3) == 0);
  int mxcsr_orig = _mm_getcsr();
  int mxcsr_cur = mxcsr_orig & ~(3<<13); // Reset rounding control bits
  _mm_setcsr(mxcsr_cur);
  __m128 *rp=(__m128 *)red, *gp=(__m128 *)green, *bp=(__m128 *)blue;
  __m128 v0f, v1f, v2f, v3f, scale = _mm_set1_ps(256.0F);
  __m128i *dp = (__m128i *) dst;
  __m128i v0, v1, v2, v3, off = _mm_set1_epi16(128);
  __m128i ones = _mm_setzero_si128();  ones = _mm_xor_si128(ones,ones);
  for (; width >= 16; width-=16, rp+=4, gp+=4, bp+=4, dp+=4)
    { // Generate output pixels in multiples of 16 (64 bytes) at a time
      v0f=rp[0];  v1f=rp[1];        v2f=rp[2];  v3f=rp[3];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v2f = _mm_mul_ps(v2f,scale);  v3f = _mm_mul_ps(v3f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v2 = _mm_cvtps_epi32(v2f);    v3 = _mm_cvtps_epi32(v3f);
      v0 = _mm_packs_epi32(v0,v1);  v2 = _mm_packs_epi32(v2,v3);
      v0 = _mm_adds_epi16(v0,off);  v2 = _mm_adds_epi16(v2,off);
      __m128i red = _mm_packus_epi16(v0,v2);
      v0f=gp[0];  v1f=gp[1];        v2f=gp[2];  v3f=gp[3];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v2f = _mm_mul_ps(v2f,scale);  v3f = _mm_mul_ps(v3f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v2 = _mm_cvtps_epi32(v2f);    v3 = _mm_cvtps_epi32(v3f);
      v0 = _mm_packs_epi32(v0,v1);  v2 = _mm_packs_epi32(v2,v3);
      v0 = _mm_adds_epi16(v0,off);  v2 = _mm_adds_epi16(v2,off);
      __m128i green = _mm_packus_epi16(v0,v2);
      v0f=bp[0];  v1f=bp[1];        v2f=bp[2];  v3f=bp[3];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v2f = _mm_mul_ps(v2f,scale);  v3f = _mm_mul_ps(v3f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v2 = _mm_cvtps_epi32(v2f);    v3 = _mm_cvtps_epi32(v3f);
      v0 = _mm_packs_epi32(v0,v1);  v2 = _mm_packs_epi32(v2,v3);
      v0 = _mm_adds_epi16(v0,off);  v2 = _mm_adds_epi16(v2,off);
      __m128i blue = _mm_packus_epi16(v0,v2);
      __m128i blue_green = _mm_unpacklo_epi8(blue,green);
      __m128i red_ones = _mm_unpacklo_epi8(red,ones);
      _mm_stream_si128(dp+0,_mm_unpacklo_epi16(blue_green,red_ones));
      _mm_stream_si128(dp+1,_mm_unpackhi_epi16(blue_green,red_ones));
      blue_green = _mm_unpackhi_epi8(blue,green);
      red_ones = _mm_unpackhi_epi8(red,ones);
      _mm_stream_si128(dp+2,_mm_unpacklo_epi16(blue_green,red_ones));
      _mm_stream_si128(dp+3,_mm_unpackhi_epi16(blue_green,red_ones));
    }
  for (; width > 0; width-=8, rp+=2, gp+=2, bp+=2, dp+=2)
    { 
      v0f=rp[0];  v1f=rp[1];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v0 = _mm_packs_epi32(v0,v1);  __m128i red = _mm_packus_epi16(v0,v0);
      v0f=gp[0];  v1f=gp[1];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v0 = _mm_packs_epi32(v0,v1);  __m128i green = _mm_packus_epi16(v0,v0);
      v0f=bp[0];  v1f=bp[1];
      v0f = _mm_mul_ps(v0f,scale);  v1f = _mm_mul_ps(v1f,scale);
      v0 = _mm_cvtps_epi32(v0f);    v1 = _mm_cvtps_epi32(v1f);
      v0 = _mm_packs_epi32(v0,v1);  __m128i blue = _mm_packus_epi16(v0,v0);
      __m128i blue_green = _mm_unpacklo_epi8(blue,green);
      __m128i red_ones = _mm_unpacklo_epi8(red,ones);
      _mm_stream_si128(dp+0,_mm_unpacklo_epi16(blue_green,red_ones));
      if (width > 4)
        _mm_stream_si128(dp+1,_mm_unpackhi_epi16(blue_green,red_ones));
    }
}
#  define SSE2_SET_RGB32F_TO_XRGB8_FUNC(_tgt,_align,_width) \
      if ((kdu_mmx_level >= 2) && (_align >= 16)) \
        _tgt = (vex_rgb_to_xrgb8_func) sse2_vex_rgb32f_to_xrgb8;
#else // No compile-time support for SSE2-based transfer functions
#  define SSE2_SET_RGB32F_TO_XRGB8_FUNC(_tgt,_align,_width) /* Does nothing */
#endif
//-----------------------------------------------------------------------------


#define VEX_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width,_absolute,_shorts) \
{ \
  if (_shorts) \
    { \
      SSE2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width); \
      AVX2_SET_RGB16_TO_XRGB8_FUNC(_tgt,_align,_width); \
    } \
  else if (!_absolute) \
    { \
      SSE2_SET_RGB32F_TO_XRGB8_FUNC(_tgt,_align,_width); \
    } \
  AVX2_VEX_TRANSFER_DO_STATIC_INIT(); \
}
  
} // namespace kd_supp_simd

#endif // X86_VEX_TRANSFER_LOCAL_H
