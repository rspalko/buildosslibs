/*****************************************************************************/
// File: kdu_region_compositor.cpp [scope = APPS/SUPPORT]
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
  Implements the `kdu_region_compositor' object defined within
`kdu_region_compositor.h".
******************************************************************************/

#include <assert.h>
#include <string.h>
#include <math.h>
#include "kdu_arch.h"
#include "kdu_utils.h"
#include "region_compositor_local.h"
#include "kdu_client_window.h"
using namespace kd_supp_local;

/* Note Carefully:
      If you want to be able to use the "kdu_text_extractor" tool to
   extract text from calls to `kdu_error' and `kdu_warning' so that it
   can be separately registered (possibly in a variety of different
   languages), you should carefully preserve the form of the definitions
   below, starting from #ifdef KDU_CUSTOM_TEXT and extending to the
   definitions of KDU_WARNING_DEV and KDU_ERROR_DEV.  All of these
   definitions are expected by the current, reasonably inflexible
   implementation of "kdu_text_extractor".
      The only things you should change when these definitions are ported to
   different source files are the strings found inside the `kdu_error'
   and `kdu_warning' constructors.  These strings may be arbitrarily
   defined, as far as "kdu_text_extractor" is concerned, except that they
   must not occupy more than one line of text.
*/
#ifdef KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("E(kdu_region_compositor.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_region_compositor.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu Region Compositor:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu Region Compositor:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers

#define KDRC_OVERLAY_LOG2_SEGMENT_SIZE 7
 // Use 128x128 overlay segments

  static kdu_int32 kdrc_alpha_lut[256];
   /* The above LUT maps the 8-bit alpha value to a multiplier in the range
      0 to 2^14 (inclusive). */

#if defined KDU_X86_INTRINSICS
#  define KDU_SIMD_OPTIMIZATIONS
#  include "x86_region_compositor_local.h"
#elif defined KDU_NEON_INTRINSICS
#  include "neon_region_compositor_local.h"
#  define KDU_SIMD_OPTIMIZATIONS
#endif

#ifdef KDU_SIMD_OPTIMIZATIONS
using namespace kd_supp_simd;
#endif

class kdrc_alpha_lut_init {
  public:
    kdrc_alpha_lut_init()
      {
        for (int n=0; n < 256; n++)
          { 
            kdrc_alpha_lut[n] = (kdu_int32)((n + (n<<7) + (n<<15)) >> 9);
#ifdef KDU_PENTIUM_MSVC
            kdrc_alpha_lut4[4*n] = kdrc_alpha_lut4[4*n+1] =
              kdrc_alpha_lut4[4*n+2] = kdrc_alpha_lut4[4*n+3] =
                (kdu_int16) kdrc_alpha_lut[n];
#endif // KDU_PENTIUM_MSVC
          }
      }
  } init_alpha_lut;


/* ========================================================================= */
/*                 Potentially Accelerated Non-Member Functions              */
/* ========================================================================= */

/*****************************************************************************/
/* INLINE                    erase_buffer_region                             */
/*****************************************************************************/
static kdrc_erase_region_func               _erase_region=NULL;
//-----------------------------------------------------------------------------
static void local_erase_region(kdu_uint32 *dst, int height, int width,
                               int row_gap, kdu_uint32 erase)
{
  int m, n;
  kdu_uint32 *dp;
  for (m=height; m > 0; m--, dst+=row_gap)
    for (dp=dst, n=width; n > 0; n--)
      *(dp++) = erase;
}
//-----------------------------------------------------------------------------
static inline void
  erase_buffer_region(kdu_uint32 *cbuf, int row_gap, kdu_dims &buf_dims,
                      kdu_dims &erase_dims, kdu_uint32 bgnd)
  /* This function assumes that `erase_dims' is already a subset of
     `buf_dims'. */
{
  assert((erase_dims & buf_dims) == erase_dims);
  cbuf += (erase_dims.pos.x - buf_dims.pos.x)
       + row_gap * (erase_dims.pos.y - buf_dims.pos.y);
  if (_erase_region == NULL)
    { 
      _erase_region = local_erase_region;
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_ERASE_REGION_FUNC(_erase_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _erase_region(cbuf,erase_dims.size.y,erase_dims.size.x,row_gap,bgnd);
}

/*****************************************************************************/
/* INLINE                erase_float_buffer_region                           */
/*****************************************************************************/
static kdrc_erase_region_float_func         _erase_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_erase_region_float(float *dst, int height, int width,
                                     int row_gap, float erase[])
{
  int m, n;
  float *dp, b0=erase[0], b1=erase[1], b2=erase[2], b3=erase[3];
  for (m=height; m > 0; m--, dst+=row_gap)
    for (dp=dst, n=width; n > 0; n--, dp+=4)
      { dp[0]=b0; dp[1]=b1; dp[2]=b2; dp[3]=b3; }
}
//-----------------------------------------------------------------------------
static inline void
  erase_float_buffer_region(float *cbuf, int row_gap, kdu_dims &buf_dims,
                            kdu_dims &erase_dims, kdu_uint32 bgnd)
  /* Note that `row_gap' counts the number of floating point samples (not
     pixels) between rows of the buffer. */
{
  float fbgnd[4] = { ((bgnd>>24) & 0xFF) * 1.0F/255.0F,
                     ((bgnd>>16) & 0xFF) * 1.0F/255.0F,
                     ((bgnd>> 8) & 0xFF) * 1.0F/255.0F,
                     (bgnd       & 0xFF) * 1.0F/255.0F };
  assert((erase_dims & buf_dims) == erase_dims);
  cbuf += (erase_dims.pos.x - buf_dims.pos.x)*4
       + row_gap * (erase_dims.pos.y - buf_dims.pos.y);
  if (_erase_region_float == NULL)
    { 
      _erase_region_float = local_erase_region_float;
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_ERASE_REGION_FLOAT_FUNC(_erase_region_float);
      
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _erase_region_float(cbuf,erase_dims.size.y,erase_dims.size.x,
                      row_gap,fbgnd);
}

/*****************************************************************************/
/* INLINE                     copy_buffer_region                             */
/*****************************************************************************/
static kdrc_copy_region_func                _copy_region=NULL;
//-----------------------------------------------------------------------------
static void local_copy_region(kdu_uint32 *dst, kdu_uint32 *src, int height,
                              int width, int dst_row_gap, int src_row_gap)
{ 
  int m, n;
  kdu_uint32 *sp, *dp;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--)
      *(dp++) = *(sp++);
}
//-----------------------------------------------------------------------------
static inline void
  copy_buffer_region(kdu_uint32 *dst, int dst_row_gap, kdu_dims &dst_dims,
                     kdu_uint32 *src, int src_row_gap, kdu_dims &src_dims,
                     kdu_dims &copy_dims)
  /* This function assumes that `erase_dims' is already a subset of
     `dst_dims' and `src_dims'. */
{
  assert(((copy_dims & src_dims) == copy_dims) &&
         ((copy_dims & dst_dims) == copy_dims));
  src += (copy_dims.pos.x - src_dims.pos.x)
      + src_row_gap * (copy_dims.pos.y - src_dims.pos.y);
  dst += (copy_dims.pos.x - dst_dims.pos.x)
      + dst_row_gap * (copy_dims.pos.y - dst_dims.pos.y);
  if (_copy_region == NULL)
    { 
      _copy_region = local_copy_region;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_COPY_REGION_FUNC(_copy_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _copy_region(dst,src,copy_dims.size.y,copy_dims.size.x,
               dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE                  copy_float_buffer_region                          */
/*****************************************************************************/
static kdrc_copy_region_float_func          _copy_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_copy_region_float(float *dst, float *src, int height,
                                    int width, int dst_row_gap,
                                    int src_row_gap)
{ 
  int m, n;
  float *sp, *dp;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--, sp+=4, dp+=4)
      { dp[0]=sp[0]; dp[1]=sp[1]; dp[2]=sp[2]; dp[3]=sp[3]; }
}
//-----------------------------------------------------------------------------
static inline void
  copy_float_buffer_region(float *dst, int dst_row_gap, kdu_dims &dst_dims,
                           float *src, int src_row_gap, kdu_dims &src_dims,
                           kdu_dims &copy_dims)
{
  assert(((copy_dims & src_dims) == copy_dims) &&
         ((copy_dims & dst_dims) == copy_dims));
  src += (copy_dims.pos.x - src_dims.pos.x)*4
      + src_row_gap * (copy_dims.pos.y - src_dims.pos.y);
  dst += (copy_dims.pos.x - dst_dims.pos.x)*4
      + dst_row_gap * (copy_dims.pos.y - dst_dims.pos.y);
  if (_copy_region_float == NULL)
    { 
      _copy_region_float = local_copy_region_float;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_COPY_REGION_FLOAT_FUNC(_copy_region_float);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _copy_region_float(dst,src,copy_dims.size.y,copy_dims.size.x,
                     dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE                    rcopy_buffer_region                             */
/*****************************************************************************/
static kdrc_rcopy_region_func               _rcopy_region=NULL;
//-----------------------------------------------------------------------------
static void local_rcopy_region(kdu_uint32 *dst, kdu_uint32 *src, int height,
                               int width, int row_gap)
{
  int m, n;
  kdu_uint32 *sp, *dp;
  for (m=height; m > 0; m--, src-=row_gap, dst-=row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--)
      *(--dp) = *(--sp);
}
//-----------------------------------------------------------------------------
static inline void
  rcopy_buffer_region(kdu_uint32 *dst, int dst_row_gap, kdu_dims &dst_dims,
                      kdu_uint32 *src, int src_row_gap, kdu_dims &src_dims,
                      kdu_dims &copy_dims)
  /* Same as `copy_buffer_region', except that the copy is performed backwards.
     This is important for cases where the `src' and `dst' arguments point
     to a common block of physical memory, where a forward copy would overwrite
     values that have not yet been copied. */
{
  assert((dst == src) && (dst_row_gap == src_row_gap));
  assert(((copy_dims & src_dims) == copy_dims) &&
         ((copy_dims & dst_dims) == copy_dims));
  src += (copy_dims.pos.x + copy_dims.size.x - src_dims.pos.x)
      + src_row_gap * (copy_dims.pos.y + copy_dims.size.y - 1 - src_dims.pos.y);
  dst += (copy_dims.pos.x + copy_dims.size.x - dst_dims.pos.x)
      + dst_row_gap * (copy_dims.pos.y + copy_dims.size.y - 1 - dst_dims.pos.y);
     // Note: the `src' and `dst' pointers reference locations immediately
     // beyond the first sample to be copied (bottom right sample of region)
  if (_rcopy_region == NULL)
    { 
      _rcopy_region = local_rcopy_region;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_RCOPY_REGION_FUNC(_rcopy_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _rcopy_region(dst,src,copy_dims.size.y,copy_dims.size.x,dst_row_gap);
}

/*****************************************************************************/
/* STATIC                 rcopy_float_buffer_region                          */
/*****************************************************************************/
static kdrc_rcopy_region_float_func         _rcopy_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_rcopy_region_float(float *dst, float *src, int height,
                                     int width, int row_gap)
{
  int m, n;
  float *sp, *dp;
  for (m=height; m > 0; m--, src-=row_gap, dst-=row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--, sp-=4, dp-=4)
      { dp[-4]=sp[-4]; dp[-3]=sp[-3]; dp[-2]=sp[-2]; dp[-1]=sp[-1]; }
}
//-----------------------------------------------------------------------------
static inline void
  rcopy_float_buffer_region(float *dst, int dst_row_gap, kdu_dims &dst_dims,
                            float *src, int src_row_gap, kdu_dims &src_dims,
                            kdu_dims &copy_dims)
{
  assert((dst == src) && (dst_row_gap == src_row_gap));
  assert(((copy_dims & src_dims) == copy_dims) &&
         ((copy_dims & dst_dims) == copy_dims));
  src += (copy_dims.pos.x + copy_dims.size.x - src_dims.pos.x)*4
      + src_row_gap * (copy_dims.pos.y + copy_dims.size.y - 1 - src_dims.pos.y);
  dst += (copy_dims.pos.x + copy_dims.size.x - dst_dims.pos.x)*4
      + dst_row_gap * (copy_dims.pos.y + copy_dims.size.y - 1 - dst_dims.pos.y);
     // Note: the `src' and `dst' pointers reference locations immediately
     // beyond the first sample to be copied (bottom right sample of region)
  if (_rcopy_region_float == NULL)
    { 
      _rcopy_region_float = local_rcopy_region_float;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_RCOPY_REGION_FLOAT_FUNC(_rcopy_region_float);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _rcopy_region_float(dst,src,copy_dims.size.y,copy_dims.size.x,
                      dst_row_gap);
}

/*****************************************************************************/
/* INLINE                    blend_buffer_region                             */
/*****************************************************************************/
static kdrc_blend_region_func               _blend_region=NULL;
//-----------------------------------------------------------------------------
static void local_blend_region(kdu_uint32 *dst, kdu_uint32 *src, int height,
                               int width, int dst_row_gap, int src_row_gap)
{
  int m, n;
  kdu_uint32 *sp, *dp;
  kdu_uint32 src_val, tgt_val;
  kdu_int32 aval, red, green, blue, diff, alpha;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--)
      { 
        src_val = *(sp++); tgt_val = *dp;
        alpha = kdrc_alpha_lut[src_val>>24];
        aval =  (tgt_val>>24) & 0x0FF;
        red   = (tgt_val>>16) & 0x0FF;
        green = (tgt_val>>8) & 0x0FF;
        blue  = tgt_val & 0x0FF;
        diff = 255                 - aval;       aval  += (diff*alpha)>>14;
        diff = ((src_val>>16) & 0x0FF) - red;    red   += (diff*alpha)>>14;
        diff = ((src_val>> 8) & 0x0FF) - green;  green += (diff*alpha)>>14;
        diff = (src_val & 0x0FF)       - blue;   blue  += (diff*alpha)>>14;
        if (aval & 0xFFFFFF00)
          aval = (aval<0)?0:255;
        if (red & 0xFFFFFF00)
          red = (red<0)?0:255;
        if (green & 0xFFFFFF00)
          green = (green<0)?0:255;
        if (blue & 0xFFFFFF00)
          blue = (blue<0)?0:255;
        *(dp++) = (kdu_uint32)((aval<<24)+(red<<16)+(green<<8)+blue);
      }
}
//-----------------------------------------------------------------------------
static inline void
  blend_buffer_region(kdu_uint32 *dst, int dst_row_gap, kdu_dims &dst_dims,
                      kdu_uint32 *src, int src_row_gap, kdu_dims &src_dims,
                      kdu_dims &blend_dims)
  /* Does regular alpha-blending.  This function assumes that `blend_dims'
     is already a subset of `dst_dims' and `src_dims'. */
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims));
  src += (blend_dims.pos.x - src_dims.pos.x)
      + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)
      + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_blend_region == NULL)
    { 
      _blend_region = local_blend_region;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_BLEND_REGION_FUNC(_blend_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _blend_region(dst,src,blend_dims.size.y,blend_dims.size.x,
                dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE                  blend_float_buffer_region                         */
/*****************************************************************************/
static kdrc_blend_region_float_func         _blend_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_blend_region_float(float *dst, float *src, int height,
                                     int width, int dst_row_gap,
                                     int src_row_gap)
{
  int m, n;
  float *sp, *dp, alpha, val, diff;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--, sp+=4, dp+=4)
      { 
        alpha = sp[0];
        diff = 1.0F  - (val=dp[0]); dp[0] = val + (diff*alpha);
        diff = sp[1] - (val=dp[1]); dp[1] = val + (diff*alpha);
        diff = sp[2] - (val=dp[2]); dp[2] = val + (diff*alpha);
        diff = sp[3] - (val=dp[3]); dp[3] = val + (diff*alpha);
      }
}
//-----------------------------------------------------------------------------
static inline void
  blend_float_buffer_region(float *dst, int dst_row_gap, kdu_dims &dst_dims,
                            float *src, int src_row_gap, kdu_dims &src_dims,
                            kdu_dims &blend_dims)
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims));
  src += (blend_dims.pos.x - src_dims.pos.x)*4
      + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)*4
      + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_blend_region_float == NULL)
    { 
      _blend_region_float = local_blend_region_float;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_BLEND_REGION_FLOAT_FUNC(_blend_region_float);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _blend_region_float(dst,src,blend_dims.size.y,blend_dims.size.x,
                      dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE                  pre_blend_buffer_region                           */
/*****************************************************************************/
static kdrc_premult_blend_region_func       _premult_blend_region=NULL;
//-----------------------------------------------------------------------------
static void local_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                                       int height, int width,
                                       int dst_row_gap, int src_row_gap)
{
  int m, n;
  kdu_uint32 *sp, *dp;
  kdu_uint32 src_val, tgt_val;
  kdu_int32 aval, red, green, blue, tmp, alpha;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--)
      { 
        src_val = *(sp++); tgt_val = *dp;
        alpha = kdrc_alpha_lut[src_val>>24];
        aval  = (src_val>>24) & 0x0FF;
        red   = (src_val>>16) & 0x0FF;
        green = (src_val>>8) & 0x0FF;
        blue  = src_val & 0x0FF;
        tmp = ((tgt_val>>24) & 0x0FF);  aval  += tmp-((tmp*alpha)>>14);
        tmp = ((tgt_val>>16) & 0x0FF);  red   += tmp-((tmp*alpha)>>14);
        tmp = ((tgt_val>> 8) & 0x0FF);  green += tmp-((tmp*alpha)>>14);
        tmp = (tgt_val & 0x0FF)      ;  blue  += tmp-((tmp*alpha)>>14);
        if (aval & 0xFFFFFF00)
          aval = (aval<0)?0:255;
        if (red & 0xFFFFFF00)
          red = (red<0)?0:255;
        if (green & 0xFFFFFF00)
          green = (green<0)?0:255;
        if (blue & 0xFFFFFF00)
          blue = (blue<0)?0:255;
        *(dp++) = (kdu_uint32)((aval<<24)+(red<<16)+(green<<8)+blue);
      }  
}
//-----------------------------------------------------------------------------
static inline void
  pre_blend_buffer_region(kdu_uint32 *dst, int dst_row_gap, kdu_dims &dst_dims,
                          kdu_uint32 *src, int src_row_gap, kdu_dims &src_dims,
                          kdu_dims &blend_dims)
  /* Same as `blend_buffer_region', but does pre-multiplied blending. */
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims));
  src += (blend_dims.pos.x - src_dims.pos.x)
      + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)
      + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_premult_blend_region == NULL)
    { 
      _premult_blend_region = local_premult_blend_region;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_PREMULT_BLEND_REGION_FUNC(_premult_blend_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _premult_blend_region(dst,src,blend_dims.size.y,blend_dims.size.x,
                        dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE               pre_blend_float_buffer_region                        */
/*****************************************************************************/
static kdrc_premult_blend_region_float_func _premult_blend_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_premult_blend_region_float(float *dst, float *src,
                                             int height, int width,
                                             int dst_row_gap, int src_row_gap)
{
  int m, n;
  float *sp, *dp, alpha, tmp0, tmp1, tmp2, tmp3;
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--, sp+=4, dp+=4)
      { 
        alpha = sp[0];
        tmp0 = dp[0]; tmp0 += alpha - tmp0 * alpha;
        tmp1 = dp[1]; tmp1 += sp[1] - tmp1 * alpha;
        tmp2 = dp[2]; tmp2 += sp[2] - tmp2 * alpha;
        tmp3 = dp[3]; tmp3 += sp[3] - tmp3 * alpha;
        dp[0] = (tmp0 > 1.0F)?1.0F:tmp0;
        dp[1] = (tmp1 > 1.0F)?1.0F:tmp1;
        dp[2] = (tmp2 > 1.0F)?1.0F:tmp2;
        dp[3] = (tmp3 > 1.0F)?1.0F:tmp3;
      }
}
//-----------------------------------------------------------------------------
static inline void
  pre_blend_float_buffer_region(float *dst, int dst_row_gap,
                                kdu_dims &dst_dims,
                                float *src, int src_row_gap,
                                kdu_dims &src_dims, kdu_dims &blend_dims)
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims));
  src += (blend_dims.pos.x - src_dims.pos.x)*4
      + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)*4
      + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_premult_blend_region_float == NULL)
    {
    _premult_blend_region_float = local_premult_blend_region_float;  
#   ifdef KDU_SIMD_OPTIMIZATIONS
    KDRC_SIMD_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_premult_blend_region_float);
#   endif // KDU_SIMD_OPTIMIZATIONS
    }  
  _premult_blend_region_float(dst,src,blend_dims.size.y,blend_dims.size.x,
                              dst_row_gap,src_row_gap);
}

/*****************************************************************************/
/* INLINE                   mod_blend_buffer_region                          */
/*****************************************************************************/
static kdrc_scaled_blend_region_func        _scaled_blend_region=NULL;
//-----------------------------------------------------------------------------
static void local_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                                      int height, int width,
                                      int dst_row_gap, int src_row_gap,
                                      kdu_int16 alpha_factor_x128)
{
  int m, n;
  kdu_uint32 *sp, *dp, xor_mask=0;
  kdu_uint32 src_val, tgt_val;
  kdu_int32 aval, red, green, blue, diff, alpha;
  if (alpha_factor_x128 < 0)
    { xor_mask = 0x00FFFFFF; alpha_factor_x128 = -alpha_factor_x128; }
  for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
    for (sp=src, dp=dst, n=width; n > 0; n--)
      { 
        src_val = xor_mask ^ *(sp++); tgt_val = *dp;
        alpha = kdrc_alpha_lut[src_val>>24];
        alpha = (alpha * alpha_factor_x128) >> 6;
        if (alpha > (1<<15))
          alpha = 1<<15;
        aval =  (tgt_val>>24) & 0x0FF;
        red   = (tgt_val>>16) & 0x0FF;
        green = (tgt_val>>8) & 0x0FF;
        blue  = tgt_val & 0x0FF;
        diff = 255                     - aval;   aval  += (diff*alpha)>>15;
        diff = ((src_val>>16) & 0x0FF) - red;    red   += (diff*alpha)>>15;
        diff = ((src_val>> 8) & 0x0FF) - green;  green += (diff*alpha)>>15;
        diff = (src_val & 0x0FF)       - blue;   blue  += (diff*alpha)>>15;
        if (aval & 0xFFFFFF00)
          aval = (aval<0)?0:255;
        if (red & 0xFFFFFF00)
          red = (red<0)?0:255;
        if (green & 0xFFFFFF00)
          green = (green<0)?0:255;
        if (blue & 0xFFFFFF00)
          blue = (blue<0)?0:255;
        *(dp++) = (kdu_uint32)((aval<<24)+(red<<16)+(green<<8)+blue);
      }
}
//-----------------------------------------------------------------------------
static inline void
  mod_blend_buffer_region(kdu_uint32 *dst, int dst_row_gap, kdu_dims &dst_dims,
                          kdu_uint32 *src, int src_row_gap, kdu_dims &src_dims,
                          kdu_dims &blend_dims, kdu_int16 factor_x128)
  /* Same as `blend_buffer_region', but scales overlay alpha channel by
     `factor_x128' / 128, inverting colour channels if `factor_x128' < 0. */
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims));
  src += (blend_dims.pos.x - src_dims.pos.x)
      + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)
      + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_scaled_blend_region == NULL)
    { 
      _scaled_blend_region = local_scaled_blend_region;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_SCALED_BLEND_REGION_FUNC(_scaled_blend_region);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }
  _scaled_blend_region(dst,src,blend_dims.size.y,blend_dims.size.x,
                       dst_row_gap,src_row_gap,factor_x128);
}

/*****************************************************************************/
/* STATIC                mod_blend_float_buffer_region                       */
/*****************************************************************************/
static kdrc_scaled_blend_region_float_func  _scaled_blend_region_float=NULL;
//-----------------------------------------------------------------------------
static void local_scaled_blend_region_float(float *dst, float *src, int height,
                                            int width, int dst_row_gap,
                                            int src_row_gap, float alpha_fact)
{
  int m, n;
  float *sp, *dp, alpha, val0, val1, val2, val3, diff;
  if (alpha_fact >= 0.0f)
    { 
      for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
        for (sp=src, dp=dst, n=width; n > 0; n--, sp+=4, dp+=4)
          { 
            alpha = sp[0] * alpha_fact;
            diff = 1.0F  - (val0=dp[0]);  val0 += (diff*alpha);
            diff = sp[1] - (val1=dp[1]);  val1 += (diff*alpha);
            diff = sp[2] - (val2=dp[2]);  val2 += (diff*alpha);
            diff = sp[3] - (val3=dp[3]);  val3 += (diff*alpha);
            val0 = (val0 < 0.0F)?0.0F:val0;
            val1 = (val1 < 0.0F)?0.0F:val1;
            val2 = (val2 < 0.0F)?0.0F:val2;
            val3 = (val3 < 0.0F)?0.0F:val3;
            dp[0] = (val0 > 1.0F)?1.0F:val0;
            dp[1] = (val1 > 1.0F)?1.0F:val1;
            dp[2] = (val2 > 1.0F)?1.0F:val2;
            dp[3] = (val3 > 1.0F)?1.0F:val3;
          }
    }
  else
    { 
      for (m=height; m > 0; m--, src+=src_row_gap, dst+=dst_row_gap)
        for (sp=src, dp=dst, n=width; n > 0; n--, sp+=4, dp+=4)
          { 
            alpha = sp[0] * alpha_fact;
            diff = 1.0F  - (val0=dp[0]);  val0 += (diff*alpha);
            diff = 1.0F - sp[1] - (val1=dp[1]);  val1 += (diff*alpha);
            diff = 1.0F - sp[2] - (val2=dp[2]);  val2 += (diff*alpha);
            diff = 1.0F - sp[3] - (val3=dp[3]);  val3 += (diff*alpha);
            val0 = (val0 < 0.0F)?0.0F:val0;
            val1 = (val1 < 0.0F)?0.0F:val1;
            val2 = (val2 < 0.0F)?0.0F:val2;
            val3 = (val3 < 0.0F)?0.0F:val3;
            dp[0] = (val0 > 1.0F)?1.0F:val0;
            dp[1] = (val1 > 1.0F)?1.0F:val1;
            dp[2] = (val2 > 1.0F)?1.0F:val2;
            dp[3] = (val3 > 1.0F)?1.0F:val3;
          }
    }
}
//-----------------------------------------------------------------------------
static inline void
  mod_blend_float_buffer_region(float *dst, int dst_row_gap,
                                kdu_dims &dst_dims,
                                float *src, int src_row_gap,
                                kdu_dims &src_dims,
                                kdu_dims &blend_dims, float factor)
  /* Same as `blend_float_buffer_region', but scales overlay alpha channel by
     `factor' / 256, inverting colour channels if `factor' < 0. */
{
  assert(((blend_dims & src_dims) == blend_dims) &&
         ((blend_dims & dst_dims) == blend_dims) && (factor != 0));
  src += (blend_dims.pos.x - src_dims.pos.x)*4
    + src_row_gap * (blend_dims.pos.y - src_dims.pos.y);
  dst += (blend_dims.pos.x - dst_dims.pos.x)*4
    + dst_row_gap * (blend_dims.pos.y - dst_dims.pos.y);
  if (_scaled_blend_region_float == NULL)
    { 
      _scaled_blend_region_float = local_scaled_blend_region_float;  
#     ifdef KDU_SIMD_OPTIMIZATIONS
      KDRC_SIMD_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_scaled_blend_region_float);
#     endif // KDU_SIMD_OPTIMIZATIONS
    }  
  _scaled_blend_region_float(dst,src,blend_dims.size.y,blend_dims.size.x,
                             dst_row_gap,src_row_gap,factor);
}


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     adjust_geometry_flags                          */
/*****************************************************************************/

static void
  adjust_geometry_flags(bool &transpose, bool &vflip, bool &hflip,
                        jpx_composited_orientation src_orientation)
  /* This function adjusts the `transpose', `vflip' and `hflip' values to
     include the effects of an initial change in geometry signalled by
     `src_orientation'.  The interpretation of these geometric transformation
     flags is described in connection with `kdu_codestream::change_appearance'
     and is identical to that described for `jpx_composited_orientation'. */
{ 
  src_orientation.append(jpx_composited_orientation(transpose,vflip,hflip));
  transpose = src_orientation.transpose;
  vflip = src_orientation.vflip;
  hflip = src_orientation.hflip;
}

/*****************************************************************************/
/* STATIC                map_jpx_roi_to_compositing_grid                     */
/*****************************************************************************/

static void
  map_jpx_roi_to_compositing_grid(jpx_roi *dest, const jpx_roi *src,
                                  kdu_coords image_offset,
                                  kdu_coords subsampling,
                                  bool transpose, bool vflip, bool hflip,
                                  kdu_coords expansion_numerator,
                                  kdu_coords expansion_denominator,
                                  kdu_coords compositing_offset)
  /* This function does all the work of `kdu_overlay_params::map_jpx_regions'.
     The functionality is also used when searching metadata overlays for
     regions of interest which contain a location on the rendering grid. */
{
  *dest = *src;
  kdu_coords tmp, vertex[4];
  int top_v=0, bottom_v=0, left_v=0, right_v=0;
  int v, num_vertices;
  if (dest->is_elliptical)
    { 
      num_vertices = 4;
      vertex[0] = src->region.pos;
      vertex[1] = vertex[0] + src->region.size - kdu_coords(1,1);
      vertex[2].x = src->region.pos.x + (src->region.size.x>>1);
      vertex[2].y = src->region.pos.y + (src->region.size.y>>1);
      vertex[3] = vertex[2] + src->elliptical_skew;
    }
  else if (!(dest->flags & JPX_QUADRILATERAL_ROI))
    { 
      num_vertices = 2;
      vertex[0] = src->region.pos;
      vertex[1] = vertex[0] + src->region.size - kdu_coords(1,1);
    }
  else
    {
      num_vertices = 4;
      for (v=0; v < 4; v++)
        vertex[v] = src->vertices[v];
      if (vflip ^ hflip ^ transpose)
        { // Reverse order of vertices
          tmp = vertex[1];  vertex[1] = vertex[3];   vertex[3] = tmp;
        }
    }

  if (transpose)
    subsampling.transpose();

  // Map each vertex
  for (v=0; v < num_vertices; v++)
    {
      vertex[v] += image_offset;
      vertex[v].to_apparent(transpose,vflip,hflip);
      vertex[v] =
        kdu_region_decompressor::find_render_point(vertex[v],subsampling,
                                                   expansion_numerator,
                                                   expansion_denominator,true);
      vertex[v] -= compositing_offset;
      if (vertex[v].y < vertex[top_v].y)
        top_v = v;
      else if (vertex[v].y > vertex[bottom_v].y)
        bottom_v = v;
      if (vertex[v].x < vertex[left_v].x)
        left_v = v;
      else if (vertex[v].x > vertex[right_v].x)
        right_v = v;
    }
  
  // Now create the mapped region of interest from the mapped vertices
  if (dest->is_elliptical)
    { 
      kdu_coords centre = vertex[2];
      kdu_coords skew = vertex[3] - centre;
      kdu_coords twice_extent = vertex[1] - vertex[0];
      if (twice_extent.x < 0)
        { skew.y = -skew.y; twice_extent.x = -twice_extent.x; }
      if (twice_extent.y < 0)
        { skew.x = -skew.x; twice_extent.y = -twice_extent.y; }
      kdu_coords extent;
      extent.x = (twice_extent.x+1)>>1;
      extent.y = (twice_extent.y+1)>>1;
      dest->init_ellipse(centre,extent,skew,
                         src->is_encoded,src->coding_priority);
    }
  else
    { 
      dest->region.pos.y = vertex[top_v].y;
      dest->region.pos.x = vertex[left_v].x;
      dest->region.size.y = vertex[bottom_v].y + 1 - dest->region.pos.y;
      dest->region.size.x = vertex[right_v].x + 1 - dest->region.pos.x;
      if (num_vertices == 4)
        for (v=0; v < 4; v++)
          dest->vertices[v] = vertex[(top_v+v) & 3];
    }
}


/*****************************************************************************/
/* STATIC                  erase_mask_buffer_region                          */
/*****************************************************************************/

static void
  erase_mask_buffer_region(kdu_uint32 *cbuf, int row_gap, kdu_dims &buf_dims,
                           kdu_dims &erase_dims, kdu_uint32 mask,
                           kdu_uint32 bgnd)
  /* Similar to `erase_buffer_region', but this function erases only a
     subset of the 4 channels (alpha, r, g, b) as determined by the `mask'.
     Each byte position in `mask' is expected to hold 0xFF or 0x00, but this
     is not checked.  A value of 0xFF means that the channel is erased. */
{
  assert((erase_dims & buf_dims) == erase_dims);
  cbuf += (erase_dims.pos.x - buf_dims.pos.x)
       + row_gap * (erase_dims.pos.y - buf_dims.pos.y);
  bgnd |= mask;
  mask = ~mask;
  int m, n;
  kdu_uint32 *dp;
  for (m=erase_dims.size.y; m > 0; m--, cbuf+=row_gap)
    for (dp=cbuf, n=erase_dims.size.x; n > 0; n--, dp++)
      *dp = (*dp & mask) | bgnd;
}

/*****************************************************************************/
/* STATIC               erase_float_mask_buffer_region                       */
/*****************************************************************************/

static void
  erase_float_mask_buffer_region(float *cbuf, int row_gap, kdu_dims &buf_dims,
                                 kdu_dims &erase_dims, kdu_uint32 mask,
                                 kdu_uint32 bgnd)
  /* Same as above function but for floating-point buffers.  Note that in
     this case, `row_gap' counts the number of floating point samples (not
     pixels) between rows of the buffer. */
{
  assert((erase_dims & buf_dims) == erase_dims);
  cbuf += (erase_dims.pos.x - buf_dims.pos.x)*4
       + row_gap * (erase_dims.pos.y - buf_dims.pos.y);
  for (int c=3; c >= 0; c--, mask>>=8, bgnd>>=8)
    { 
      if ((mask & 0xFF) == 0)
        continue;
      int m, n;
      float *dp, *dpp, fbgnd = (bgnd & 0xFF) * 1.0F/255.0F;
      for (dpp=cbuf+c, m=erase_dims.size.y; m > 0; m--, dpp+=row_gap)
        for (dp=dpp, n=erase_dims.size.x; n > 0; n--, dp+=4)
          *dp = fbgnd;
    }
}
                          
/*****************************************************************************/
/* STATIC                   initialize_buffer_surface                        */
/*****************************************************************************/

static void
  initialize_buffer_surface(kdu_compositor_buf *buffer, kdu_dims region,
                            kdu_compositor_buf *old_buffer,
                            kdu_dims old_region,
                            kdu_uint32 bgnd, bool can_skip_erase=false)
  /* This function is used by both `kdrc_layer' and `kdu_region_compositor'
     to initialize a buffer surface after it has been created, moved or
     resized.  If `old_buffer' is NULL, the new buffer surface is simply
     initialized to the `bgnd' value.  Otherwise, any region of the old
     buffer which intersects with the new buffer region is copied across and
     the remaining regions are initialized with the `bgnd' value.  It is
     important to realize that `buffer' and `old_buffer' may refer to exactly
     the same buffer, so that copying must be performed in the right order
     (top to bottom, bottom to top, left to right or right to left) so as to
     avoid overwriting data which has yet to be copied.
        If `can_skip_erase' is true, only copy operations will be performed;
     erasure of samples which do not overlap with the old buffer region can be
     skipped in this case. */
{
  int row_gap;
  kdu_uint32 *buf=buffer->get_buf(row_gap,false);
  float *fbuf = (buf!=NULL)?NULL:buffer->get_float_buf(row_gap,false);
  
  int old_row_gap=0;
  kdu_uint32 *old_buf = NULL;
  float *old_fbuf = NULL;
  if (old_buffer != NULL)
    { 
      old_buf = (buf==NULL)?NULL:old_buffer->get_buf(old_row_gap,false);
      old_fbuf=(fbuf==NULL)?NULL:old_buffer->get_float_buf(old_row_gap,false);
    }

  if (old_region.is_empty() || (old_region != region))
    buffer->set_invalid();
    
  kdu_dims isect;
  if ((old_buffer == NULL) || ((isect = region & old_region).is_empty()))
    { // We only need to erase the buffer
      if (can_skip_erase)
        return;
      if (buf != NULL)
        erase_buffer_region(buf,row_gap,region,region,bgnd);
      else
        erase_float_buffer_region(fbuf,row_gap,region,region,bgnd);
      return;
    }

  // Next, copy the intersecting region (carefully)
  if ((old_buffer == buffer) &&
      ((old_region.pos.y > region.pos.y) ||
       ((old_region.pos.y == region.pos.y) &&
        (old_region.pos.x > region.pos.x))))
    { // Need to copy in bottom-top, right-left order to avoid overwriting
      // data that has not yet been copied
      if (buf != NULL)
        rcopy_buffer_region(buf,row_gap,region,
                            old_buf,old_row_gap,old_region,isect);
      else
        rcopy_float_buffer_region(fbuf,row_gap,region,
                                  old_fbuf,old_row_gap,old_region,isect);
    }
  else
    { 
      if (buf != NULL)
        copy_buffer_region(buf,row_gap,region,
                           old_buf,old_row_gap,old_region,isect);
      else
        copy_float_buffer_region(fbuf,row_gap,region,
                                 old_fbuf,old_row_gap,old_region,isect);      
    }
  
  // Finally, erase the non-intersecting regions of the new buffer
  if (!can_skip_erase)
    { 
      kdu_dims edims;
      kdu_coords regmin, reglim, commin, comlim;
      regmin = region.pos;  reglim = regmin + region.size;
      commin = isect.pos;   comlim = commin + isect.size;
      for (edims.pos.y=regmin.y; edims.pos.y < reglim.y;
           edims.pos.y+=edims.size.y)
        { // Walk through top, centre and bottom stip that might need erasing
          bool mid_strip = false;
          if (edims.pos.y >= comlim.y)
            edims.size.y = reglim.y; // Erase bottom strip
          else if (edims.pos.y < commin.y)
            edims.size.y = commin.y; // Erase top strip
          else
            { mid_strip = true; edims.size.y = comlim.y; }
          edims.size.y -= edims.pos.y;
          for (edims.pos.x=regmin.x; edims.pos.x < reglim.x;
               edims.pos.x += edims.size.x)
            { // Walk through left, centre, right blocks that may need erasing
              if ((edims.pos.x >= comlim.x) || !mid_strip)
                edims.size.x = reglim.x; // Erase remainder of strip
              else if (edims.pos.x < commin.x)
                edims.size.x = commin.x; // Erase left portion of strip
              else
                { // intersecting block that we've already copied; don't erase
                  edims.size.x = comlim.x - edims.pos.x;
                  assert(edims == isect);
                  continue;
                }
              edims.size.x -= edims.pos.x;
              if (buf != NULL)
                erase_buffer_region(buf,row_gap,region,edims,bgnd);
              else
                erase_float_buffer_region(fbuf,row_gap,region,edims,bgnd);
            }
        }
    }
}

/*****************************************************************************/
/* STATIC                paint_race_track (32-bit pixels)                    */
/*****************************************************************************/

static void
  paint_race_track(kdu_overlay_params *paint_params, kdu_uint32 colr,
                   int radius, kdu_uint32 *centre, int extent,
                   int max_left, int max_right, int min_y, int max_y)
  /* The purpose of this function is to draw a horizontally aligned (possibly
     clipped) "race track".  The full race track consists of semi-circular
     end points of `radius' r, centred on the left at `centre' and on the right
     at `centre'+`extent', together with horizontal lines r rows above and r
     rows below the line segment which runs between these two centres.
        The `min_y' and `max_y' arguments identify the minimum and maximum
     vertical displacements which are contained within the clipping boundary,
     expressed relative to the vertical position of `centre'.
        The `max_left' and `max_right' arguments identify the maximum
     displacement to the left and right of `centre' for samples which are
     located within the clipping boundary.
        Note that `extent' might be -ve, but this can only happen if `centre'
     lies on the left clipping boundary (i.e., `max_left' is 0) or
     `centre'+`extent' lies on the right clipping boundary (i.e.,
     `max_right' = `extent'), so the algorithm will then paint only a part
     of one semi-circle and the fact that `extent' is -ve will not prove
     problematic. */
{
  if (min_y < -radius) min_y = -radius;
  if (max_y > radius) max_y = radius;
  int n, num_pts;
  const int *pts = paint_params->get_ring_points(min_y,max_y,num_pts);
  if (min_y == -radius)
    { // Draw upper line segment of race-track
      kdu_uint32 *dp=centre+pts[1];
      for (n=0; n <= extent; n++)
        dp[n] = colr;
    }
  if (max_y == radius)
    { // Draw lower line segment of race-track
      kdu_uint32 *dp=centre+pts[2*num_pts-1];
      for (n=0; n <= extent; n++)
        dp[n] = colr;
    }
  
  const int *pp;
  if (max_left > 0)
    { // Draw the clipped left semi-circle
      int dx, min_dx=-max_right, max_dx=max_left;
      if (min_dx <= max_dx)
        {
          kdu_uint32 *dp = centre;
          for (pp=pts, n=num_pts; n > 0; n--, pp+=2)
            if (((dx=pp[0]) >= min_dx) && (dx <= max_dx))
              dp[pp[1]-dx] = colr;
        }
    }
  
  if (max_right > extent)
    { // Draw the clipped right semi-circle
      int dx, min_dx=-(max_left+extent), max_dx=max_right-extent;
      if (min_dx <= max_dx)
        {
          kdu_uint32 *dp = centre+extent;
          for (pp=pts, n=num_pts; n > 0; n--, pp+=2)
            if (((dx=pp[0]) >= min_dx) && (dx <= max_dx))
              dp[pp[1]+dx] = colr;
        }
    }
}

/*****************************************************************************/
/* STATIC                   paint_race_track (floats)                        */
/*****************************************************************************/

static void
  paint_race_track(kdu_overlay_params *paint_params, float colr[],
                   int radius, float *centre, int extent,
                   int max_left, int max_right, int min_y, int max_y)
  /* Same as previous function, except that this one is used with
     buffers that have a floating-point representation. */
{
  if (min_y < -radius) min_y = -radius;
  if (max_y > radius) max_y = radius;
  int n, num_pts;
  const int *pts = paint_params->get_ring_points(min_y,max_y,num_pts);
  if (min_y == -radius)
    { // Draw upper line segment of race-track
      float *dp=centre+pts[1];
      for (n=0; n <= (4*extent); n+=4)
        { dp[n]=colr[0]; dp[n+1]=colr[1]; dp[n+2]=colr[2]; dp[n+3]=colr[3]; }
    }
  if (max_y == radius)
    { // Draw lower line segment of race-track
      float *dp=centre+pts[2*num_pts-1];
      for (n=0; n <= (4*extent); n+=4)
        { dp[n]=colr[0]; dp[n+1]=colr[1]; dp[n+2]=colr[2]; dp[n+3]=colr[3]; }
    }
  
  const int *pp;
  if (max_left > 0)
    { // Draw the clipped left semi-circle
      int dx, min_dx=-max_right, max_dx=max_left;
      if (min_dx <= max_dx)
        {
          float *dp = centre;
          for (pp=pts, n=num_pts; n > 0; n--, pp+=2)
            if (((dx=pp[0]) >= min_dx) && (dx <= max_dx))
              {
                dx = pp[1] - (dx << 2);
                dp[dx+0]=colr[0]; dp[dx+1]=colr[1];
                dp[dx+2]=colr[2]; dp[dx+3]=colr[3];
              }
        }
    }
  
  if (max_right > extent)
    { // Draw the clipped right semi-circle
      int dx, min_dx=-(max_left+extent), max_dx=max_right-extent;
      if (min_dx <= max_dx)
        {
          float *dp = centre+4*extent;
          for (pp=pts, n=num_pts; n > 0; n--, pp+=2)
            if (((dx=pp[0]) >= min_dx) && (dx <= max_dx))
              {
                dx = pp[1] + (dx << 2);
                dp[dx+0]=colr[0]; dp[dx+1]=colr[1];
                dp[dx+2]=colr[2]; dp[dx+3]=colr[3];
              }        
        }
    }
}


/* ========================================================================= */
/*                      kdrc_overlay_product_expression                      */
/* ========================================================================= */

/*****************************************************************************/
/*                 kdrc_overlay_expression::add_product_term                 */
/*****************************************************************************/

bool kdrc_overlay_expression::add_product_term(jpx_metanode node,
                                                       int effect)
{
  kdrc_overlay_expression *scan, *prev=NULL;
  if (effect >= 0)
    {
      for (scan=this; scan != NULL; prev=scan, scan=scan->next_in_product)
        if ((scan->positive_node == node) || !scan->positive_node)
          break;
      if (scan == NULL)
        {
          assert(prev != NULL);
          scan = prev->next_in_product = new kdrc_overlay_expression;
        }
      if (scan->positive_node == node)
        return false;
      scan->positive_node = node;
      return true;
    }
  else
    {
      for (scan=this; scan != NULL; prev=scan, scan=scan->next_in_product)
        if ((scan->negative_node == node) || !scan->negative_node)
          break;
      if (scan == NULL)
        {
          assert(prev != NULL);
          scan = prev->next_in_product = new kdrc_overlay_expression;
        }
      if (scan->negative_node == node)
        return false;
      scan->negative_node = node;
      return true;
    }
}

/*****************************************************************************/
/*                    kdrc_overlay_expression::evaluate                      */
/*****************************************************************************/

bool kdrc_overlay_expression::evaluate(jpx_metanode roi_node)
{
  kdu_uint32 exclusion_box_type = jp2_roi_description_4cc;
  int exclusion_flags = JPX_PATH_TO_EXCLUDE_BOX | JPX_PATH_TO_EXCLUDE_PARENTS;
  kdrc_overlay_expression *scan;
  for (scan=this; scan != NULL; scan=scan->next_in_product)
    {
      if (scan->positive_node.exists() &&
          !roi_node.find_path_to(scan->positive_node,
                                 JPX_PATH_TO_DIRECT|JPX_PATH_TO_REVERSE,
                                 JPX_PATH_TO_DIRECT|JPX_PATH_TO_REVERSE,
                                 1,&exclusion_box_type,&exclusion_flags,
                                 true))
        break;
      if (scan->negative_node.exists() &&
          roi_node.find_path_to(scan->negative_node,
                                JPX_PATH_TO_DIRECT|JPX_PATH_TO_REVERSE,
                                JPX_PATH_TO_DIRECT|JPX_PATH_TO_REVERSE,
                                1,&exclusion_box_type,&exclusion_flags,
                                true).exists())
        break;
    }
  if (scan == NULL)
    return true; // All terms in the product expression are true
  if (next_in_sum != NULL)
    return next_in_sum->evaluate(roi_node);
  return false;
}


/* ========================================================================= */
/*                           kdrc_trapezoid_follower                         */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdrc_trapezoid_follower::init                      */
/*****************************************************************************/

void
  kdrc_trapezoid_follower::init(kdu_coords left_top, kdu_coords left_bottom,
                                kdu_coords right_top, kdu_coords right_bottom,
                                int min_y, int max_y)
{
  int dx1 = left_bottom.x-left_top.x, dy1=left_bottom.y-left_top.y;
  int dx2 = right_bottom.x-right_top.x, dy2=right_bottom.y-right_top.y;
  if (left_top.y > min_y) min_y = left_top.y;
  if (right_top.y > min_y) min_y = right_top.y;
  if (left_bottom.y < max_y) max_y = left_bottom.y;
  if (right_bottom.y < max_y) max_y = right_bottom.y;
  this->y = min_y;
  this->y_end = max_y;
  y = (y >= left_top.y)?y:left_top.y;
  y = (y >= right_top.y)?y:right_top.y;
  y_end = (y_end <= left_bottom.y)?y_end:left_bottom.y;
  y_end = (y_end <= right_bottom.y)?y_end:right_bottom.y;
  y_min = y;
  if (y_end < y)
    return; // Nothing to configure
  
  x1 = left_top.x;
  if (dy1 == 0)
    inc1 = next_inc1 = end_inc1 = 0.0;
  else
    {
      assert(dy1 > 0);
      inc1 = next_inc1 = end_inc1 = ((double) dx1) / ((double) dy1);
      if (inc1 > 1.0)
        inc1 -= 0.5*(inc1-1.0);
      else if (inc1 < -1.0)
        {
          x1 += 0.5*(inc1+1.0);
          if (y_end == left_bottom.y)
            end_inc1 -= 0.5*(inc1+1.0);
        }
      if (y > left_top.y)
        {
          x1 += inc1;
          inc1 = next_inc1;
          x1 += inc1*(y-left_top.y-1);
          if (y == left_bottom.y)
            x1 += end_inc1 - inc1;
        }
    }
  
  x2 = right_top.x;
  if (dy2 == 0)
    inc2 = next_inc2 = end_inc2 = 0.0;
  else
    {
      assert(dy2 > 0);
      inc2 = next_inc2 = end_inc2 = ((double) dx2) / ((double) dy2);
      if (inc2 > 1.0)
        {
          x2 += 0.5*(inc2-1.0);
          if (y_end == right_bottom.y)
            end_inc2 -= 0.5*(inc2-1.0);
        }
      else if (inc2 < -1.0)
        inc2 -= 0.5*(inc2+1.0);
      if (y > right_top.y)
        {
          x2 += inc2;
          inc2 = next_inc2;
          x2 += inc2*(y-right_top.y-1);
          if (y == right_bottom.y)
            x2 += end_inc2 - inc2;
        }
    }
}

/*****************************************************************************/
/*                   kdrc_trapezoid_follower::limit_max_y                    */
/*****************************************************************************/

void
  kdrc_trapezoid_follower::limit_max_y(int new_limit)
{
  if (new_limit >= y_end)
    return;
  y_end = new_limit;
  end_inc1 = next_inc1;
  end_inc2 = next_inc2;
}

/*****************************************************************************/
/*                   kdrc_trapezoid_follower::limit_min_y                    */
/*****************************************************************************/

void
  kdrc_trapezoid_follower::limit_min_y(int new_limit)
{
  if (new_limit <= y)
    return;
  x1 += inc1; x2 += inc2;
  inc1 = next_inc1; inc2 = next_inc2;
  x1 += inc1 * (new_limit-y); x2 += inc2 * (new_limit-y);
  y = y_min = new_limit;
  if (y == y_end)
    { x1 += end_inc1-inc1; x2 += end_inc2-inc2; }
}


/* ========================================================================= */
/*                               kdrc_overlay                                */
/* ========================================================================= */

/*****************************************************************************/
/*                         kdrc_overlay::kdrc_overlay                        */
/*****************************************************************************/

kdrc_overlay::kdrc_overlay(jpx_meta_manager meta_manager,
                           int codestream_idx, int log2_segment_size)
{
  this->compositor = NULL;
  this->meta_manager = meta_manager;
  params.codestream_idx = codestream_idx;
  min_composited_size = 8; // Allow application to change this later
  expansion_numerator = expansion_denominator =
    buffer_region.size = kdu_coords(0,0);
  buffer = NULL;
  free_nodes = NULL;
  free_segs = NULL;
  this->log2_seg_size = log2_segment_size;
  num_segs = first_unprocessed_seg = 0;
  seg_refs = NULL;
}

/*****************************************************************************/
/*                        kdrc_overlay::~kdrc_overlay                        */
/*****************************************************************************/

kdrc_overlay::~kdrc_overlay()
{
  deactivate(); // Always safe to call this function
  assert(compositor == NULL);

  while (free_nodes != NULL)
    {
      kdrc_roinode *tmp = free_nodes;
      free_nodes = tmp->next;
      delete tmp;
    }
  while (free_segs != NULL)
    {
      kdrc_overlay_segment *tmp = free_segs;
      free_segs = tmp->next;
      delete tmp;
    }
  if (seg_refs != NULL)
    delete[] seg_refs;
}

/*****************************************************************************/
/*                           kdrc_overlay::activate                          */
/*****************************************************************************/

void
  kdrc_overlay::activate(kdu_region_compositor *compositor,
                         int max_border_size)
{
  this->compositor = compositor;
  params.max_painting_border = max_border_size;
}

/*****************************************************************************/
/*                          kdrc_overlay::deactivate                         */
/*****************************************************************************/

void
  kdrc_overlay::deactivate()
{
  compositor = NULL;
  buffer = NULL;
  buffer_region.size = kdu_coords(0,0);
  if (seg_refs != NULL)
    {
      kdrc_overlay_segment *seg;
      for (int n=0; n < num_segs; n++)
        if ((seg=seg_refs[n]) != NULL)
          recycle_seg_list(seg);
      delete[] seg_refs;
      seg_refs = NULL;
    }
  num_segs = first_unprocessed_seg = 0;
  seg_indices.size = kdu_coords(0,0);
}

/*****************************************************************************/
/*                         kdrc_overlay::set_geometry                        */
/*****************************************************************************/

void
  kdrc_overlay::set_geometry(kdu_coords image_offset, kdu_coords subsampling,
                             bool transpose, bool vflip, bool hflip,
                             kdu_coords expansion_numerator,
                             kdu_coords expansion_denominator,
                             kdu_coords compositing_offset,
                             int compositing_layer_idx)
{
  bool any_change = (image_offset != this->image_offset) ||
    (subsampling != this->subsampling) || (transpose != this->transpose) ||
    (vflip != this->vflip) || (hflip != this->hflip) ||
    (expansion_numerator != this->expansion_numerator) ||
    (expansion_denominator != this->expansion_denominator) ||
    (compositing_offset != this->compositing_offset) ||
    (compositing_layer_idx != params.compositing_layer_idx);
  if (!any_change)
    return;
  this->image_offset = image_offset;
  this->subsampling = subsampling;
  this->transpose = transpose;
  this->vflip = vflip;
  this->hflip = hflip;
  this->expansion_numerator = expansion_numerator;
  this->expansion_denominator = expansion_denominator;
  this->compositing_offset = compositing_offset;
  params.compositing_layer_idx = compositing_layer_idx;

  kdu_long val;
  kdu_coords num = expansion_numerator;
  kdu_coords den = expansion_denominator;
  if (transpose)
    { num.transpose();  den.transpose(); } // Map to original geometry
  val = min_composited_size-1;  val *= subsampling.x;
  val = val * den.x;  val = val / num.x;
  min_size = (int) val;
  val = min_composited_size-1;  val *= subsampling.y;
  val = val * den.y;  val = val / num.y;
  if (min_size > (int) val)
    min_size = (int) val;
  min_size += 1;
  if (seg_refs != NULL)
    {
      kdrc_overlay_segment *seg;
      for (int n=0; n < num_segs; n++)
        if ((seg=seg_refs[n]) != NULL)
          recycle_seg_list(seg);
      delete[] seg_refs;
      seg_refs = NULL;
    }
  num_segs = first_unprocessed_seg = 0;
  buffer_region.size = kdu_coords(0,0);
  seg_indices.size = kdu_coords(0,0);
  buffer = NULL; // So we don't risk reusing an old buffer with wrong geometry
}

/*****************************************************************************/
/*                     kdrc_overlay::set_buffer_surface                      */
/*****************************************************************************/

bool
  kdrc_overlay::set_buffer_surface(kdu_compositor_buf *buffer,
                                   kdu_dims buffer_region,
                                   bool look_for_new_metadata,
                                   kdrc_overlay_expression *dependencies,
                                   bool mark_all_segs_for_processing)
{
  assert(compositor != NULL);
  assert((expansion_numerator.x > 0) &&
         (expansion_numerator.y > 0) &&
         (expansion_denominator.x > 0) &&
         (expansion_denominator.y > 0));
  kdu_compositor_buf *old_buffer = this->buffer;
  kdu_dims old_buffer_region = this->buffer_region;
  this->buffer = buffer;
  this->buffer_region = buffer_region;
  
  // Determine whether preserved segments can be copied from an old buffer
  // surface.  The following cases exist:
  // 1) Segments can be individually copied if the old and new buffer have
  //    physically disjoint memory.
  // 2) Segments can be copied in contiguous stripes in top-to-bottom,
  //    left-to-right fashion, so long as the target of the copy operation
  //    precedes the source, within a common block of physical memory.
  // 3) Segments can be copied in contiguous stripes in bottom-to-top,
  //    right-to-left fashion, if the target of the copy operation follows
  //    the source, within a common block of physical memory.
  // 4) No copying is required if the same block of physical memory is used
  //    and segments occupy exactly the same locations before and after the
  //    buffer surface configuration call.
  // 5) There is either no `buffer' or no `old_buffer', in which case each
  //    preserved segment must be marked for regeneration.
  bool regen_common_segs = (buffer == NULL) || (old_buffer == NULL);
  bool copy_common_segs = ((buffer != old_buffer) && !regen_common_segs);
  bool copy_common_stripes_fwd = ((buffer != NULL) && (buffer == old_buffer));
  bool copy_common_stripes_bwd = false;
  if (copy_common_stripes_fwd &&
      ((old_buffer_region.pos.y > buffer_region.pos.y) ||
       ((old_buffer_region.pos.y == buffer_region.pos.y) &&
        (old_buffer_region.pos.x > buffer_region.pos.x))))
    { copy_common_stripes_bwd = true; copy_common_stripes_fwd = false; }
  kdu_uint32 *buf=NULL, *old_buf=NULL;
  float *fbuf=NULL, *old_fbuf=NULL;
  int buf_row_gap=0, old_buf_row_gap=0;
  if (copy_common_segs || copy_common_stripes_fwd || copy_common_stripes_bwd)
    { 
      buf = buffer->get_buf(buf_row_gap,true);
      old_buf = old_buffer->get_buf(old_buf_row_gap,true);
      if (buf == NULL)
        { 
          fbuf = buffer->get_float_buf(buf_row_gap,true);
          old_fbuf = old_buffer->get_float_buf(old_buf_row_gap,true);
        }
      assert(((buf != NULL) && (old_buf != NULL)) ||
             ((fbuf != NULL) && (old_fbuf != NULL)));
    }
  
  // Find the new set of active segment indices
  kdu_dims old_seg_indices = this->seg_indices;
  kdu_coords min_idx=buffer_region.pos;
  kdu_coords lim_idx=min_idx+buffer_region.size;
  min_idx.x >>= log2_seg_size;
  min_idx.y >>= log2_seg_size;
  lim_idx.x = 1 + ((lim_idx.x-1) >> log2_seg_size);
  lim_idx.y = 1 + ((lim_idx.y-1) >> log2_seg_size);
  seg_indices.pos = min_idx;
  seg_indices.size = lim_idx-min_idx;
  
  // Set up new seg_refs array
  kdrc_overlay_segment **old_seg_refs = this->seg_refs; // May be able to reuse
  if ((old_seg_indices != seg_indices) || (seg_refs == NULL))
    {
      num_segs = (int) seg_indices.area();
      seg_refs = new kdrc_overlay_segment *[num_segs];
      memset(seg_refs,0,sizeof(kdrc_overlay_segment *)*((size_t) num_segs));
    }

  // Find segment indices which can be re-used
  kdu_dims common_seg_indices = seg_indices & old_seg_indices;
  if (!common_seg_indices.is_empty())
    { // Remove boundary segments from the common region unless the buffer
      // region is unchanged on that boundary.  This policy is slightly
      // conservative, in the sense that some boundary segments might
      // occasionally be removed even if they can be re-used as-is, but this
      // is very unlikely.
      if (buffer_region.pos.x != old_buffer_region.pos.x)
        { common_seg_indices.pos.x++; common_seg_indices.size.x--; }
      if ((common_seg_indices.size.x > 0) &&
          ((buffer_region.pos.x+buffer_region.size.x) !=
           (old_buffer_region.pos.x+old_buffer_region.size.x)))
        common_seg_indices.size.x--;
      if (buffer_region.pos.y != old_buffer_region.pos.y)
        { common_seg_indices.pos.y++; common_seg_indices.size.y--; }
      if ((common_seg_indices.size.y > 0) &&
          ((buffer_region.pos.y+buffer_region.size.y) !=
           (old_buffer_region.pos.y+old_buffer_region.size.y)))
        common_seg_indices.size.y--;
    }

  // Re-use or recycle each old segment, copying the buffer surfaces of
  // any re-used segments, where appropriate.  This is a little tricky because
  // we cannot safely copy re-used segments which belong to the same physical
  // buffer unless we do so on a stripe basis -- otherwise, we can find that
  // copied data overwrites data that needs to be copied in the future.
  int seg_num, y_inc=1, y_lim=old_seg_indices.size.y;
  kdu_coords idx;
  kdrc_overlay_segment *seg;
  if (copy_common_stripes_bwd)
    { // Visit segment stripes in reverse order
      idx.y = y_lim-1;  y_lim = -1;  y_inc = -1;
    }
  for (; idx.y != y_lim; idx.y += y_inc)
    { 
      kdu_dims common_stripe; // Accumulates stripe to copy, if there is one
      kdrc_overlay_segment **old_sref =
        old_seg_refs + idx.y*old_seg_indices.size.x;
      for (idx.x=0; idx.x < old_seg_indices.size.x; idx.x++, old_sref++)
        { 
          if ((seg = *old_sref) == NULL)
            continue;
          *old_sref = NULL;
          int tmp;
          if (((tmp = (idx.y + old_seg_indices.pos.y -
                       common_seg_indices.pos.y)) < 0) ||
              (tmp >= common_seg_indices.size.y) ||
              ((tmp=(idx.x + old_seg_indices.pos.x -
                     common_seg_indices.pos.x)) < 0) ||
              (tmp >= common_seg_indices.size.x))
            recycle_seg_list(seg); // Not a common segment
          else
            { 
              assert((seg->region & buffer_region) == seg->region);
              seg_num = (idx.x+old_seg_indices.pos.x-seg_indices.pos.x) +
                ((idx.y+old_seg_indices.pos.y-seg_indices.pos.y) *
                 seg_indices.size.x);
              assert((seg_num >= 0) && (seg_num < num_segs) &&
                     (seg_refs[seg_num] == NULL));
              seg_refs[seg_num] = seg;
              if (seg->is_visible)
                { // Need to either copy or regenerate the segment
                  if (regen_common_segs)
                    { // Regenerate everything
                      assert(seg->head != NULL);
                      seg->first_unpainted = seg->head;
                      seg->needs_erase = seg->needs_process = true;
                    }
                  else if (copy_common_segs)
                    { 
                      if (buf != NULL)
                        copy_buffer_region(buf,buf_row_gap,buffer_region,
                                           old_buf,old_buf_row_gap,
                                           old_buffer_region,seg->region);
                      else
                        copy_float_buffer_region(fbuf,buf_row_gap,
                                                 buffer_region,old_fbuf,
                                                 old_buf_row_gap,
                                                 old_buffer_region,
                                                 seg->region);
                    }
                  else if (copy_common_stripes_fwd || copy_common_stripes_bwd)
                    common_stripe.augment(seg->region);
                }
            }
        }
      if (common_stripe.is_empty())
        continue; // No stripes to copy
      if (copy_common_stripes_fwd)
        { 
          if (buf != NULL)
            copy_buffer_region(buf,buf_row_gap,buffer_region,
                               old_buf,old_buf_row_gap,
                               old_buffer_region,common_stripe);
          else
            copy_float_buffer_region(fbuf,buf_row_gap,buffer_region,
                                     old_fbuf,old_buf_row_gap,
                                     old_buffer_region,common_stripe);
        }
      else
        { 
          assert(copy_common_stripes_bwd);
          if (buf != NULL)
            rcopy_buffer_region(buf,buf_row_gap,buffer_region,
                               old_buf,old_buf_row_gap,
                               old_buffer_region,common_stripe);
          else
            rcopy_float_buffer_region(fbuf,buf_row_gap,buffer_region,
                                     old_fbuf,old_buf_row_gap,
                                     old_buffer_region,common_stripe);
        }
    }

  // Delete `old_seg_refs' if appropriate
  if ((old_seg_refs != NULL) && (old_seg_refs != seg_refs))
    { delete[] old_seg_refs; old_seg_refs = NULL; }
  
  // Now generate new segments, as required, and fill out the flags that
  // tell the processing machinery what needs to be done.
  first_unprocessed_seg = num_segs; // Until we find an unprocessed one
  bool have_metadata = false;
  for (seg_num=0, idx.y=0; idx.y < seg_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < seg_indices.size.x; idx.x++, seg_num++)
      { 
        if (((seg = seg_refs[seg_num]) != NULL) && !look_for_new_metadata)
          { 
            if (seg->head != NULL)
              have_metadata = true;
            if (mark_all_segs_for_processing)
              seg->needs_process = true;
            if (seg->needs_process && (seg_num < first_unprocessed_seg))
              first_unprocessed_seg = seg_num;
            continue; // Segment has been re-used already
          }
        
        kdu_dims seg_region;
        seg_region.pos.x = (idx.x+seg_indices.pos.x)<<log2_seg_size;
        seg_region.pos.y = (idx.y+seg_indices.pos.y)<<log2_seg_size;
        seg_region.size.x = (1<<log2_seg_size);
        seg_region.size.y = (1<<log2_seg_size);
        seg_region &= buffer_region;
        if (seg == NULL)
          seg_refs[seg_num] = seg = seg_alloc(seg_region);
        assert(seg->next == NULL); // Currently, we do not support lists
        if (mark_all_segs_for_processing)
          seg->needs_process = true;
        kdu_dims cs_region = seg_region;
        cs_region.pos.x -= params.max_painting_border;
        cs_region.pos.y -= params.max_painting_border;
        cs_region.size.x += 2*params.max_painting_border;
        cs_region.size.y += 2*params.max_painting_border;
        map_from_compositing_grid(cs_region);
        meta_manager.load_matches(1,&params.codestream_idx,0,NULL);
        jpx_metanode mn;
        bool need_seg_repaint = false;
        while ((mn =
                meta_manager.enumerate_matches(mn,params.codestream_idx,
                                               params.compositing_layer_idx,
                                               false,cs_region,min_size,
                                               false,true)).exists())
          { 
            int cs_width = mn.get_width();
            if (cs_width < min_size)
              continue; // Should not happen because `min_size' was passed to
                        // `enumerate_matches' above
            kdu_dims bounding_box = mn.get_bounding_box();
            kdu_long sort_area = bounding_box.area();
            map_to_compositing_grid(bounding_box);
            bounding_box.pos.x -= params.max_painting_border;
            bounding_box.pos.y -= params.max_painting_border;
            bounding_box.size.x += 2*params.max_painting_border;
            bounding_box.size.y += 2*params.max_painting_border;
            bounding_box &= seg_region;
            if (bounding_box.is_empty())
              continue;
            have_metadata = true;
            kdrc_roinode *scan, *prev=NULL;
            for (scan=seg->head; scan != NULL; prev=scan, scan=scan->next)
              if ((scan->node == mn) || (scan->sort_area < sort_area))
                break;
            if ((scan != NULL) && (scan->node == mn))
              continue; // Node already exists on the list; change nothing
            kdrc_roinode *elt = node_alloc();
            elt->node = mn;
            elt->bounding_box = bounding_box;
            elt->sort_area = sort_area;
            elt->cs_width = cs_width;
            elt->next = scan;
            if (prev == NULL)
              seg->head = elt;
            else
              prev->next = elt;
            elt->hidden = false;
            if (dependencies != NULL)
              elt->hidden = !dependencies->evaluate(mn);
            if (!elt->hidden)
              { 
                seg->is_visible = true;
                need_seg_repaint = true;
              }
          }
        if (need_seg_repaint)
          { 
            seg->needs_erase = true;
            seg->first_unpainted = seg->head;
            while ((seg->first_unpainted != NULL) &&
                   seg->first_unpainted->hidden)
              seg->first_unpainted = seg->first_unpainted->next;
            if (seg->first_unpainted != NULL)
              seg->needs_process = true;
          }
        if (seg->needs_process && (seg_num < first_unprocessed_seg))
          first_unprocessed_seg = seg_num;
      }

  return have_metadata;
}

/*****************************************************************************/
/*                       kdrc_overlay::update_config                         */
/*****************************************************************************/

void
  kdrc_overlay::update_config(int min_display_size,
                              kdrc_overlay_expression *dependencies,
                              const kdu_uint32 *aux_params, int num_aux_params,
                              bool dependencies_changed,
                              bool aux_params_changed,
                              bool blending_factor_changed)
{
  if (min_display_size < 1)
    min_display_size = 1;
  if ((min_display_size == min_composited_size) &&
      !(dependencies_changed || aux_params_changed ||
        blending_factor_changed))
    return;

  params.orig_aux_params = params.cur_aux_params = aux_params;
  params.num_orig_aux_params = params.num_cur_aux_params = num_aux_params;
  
  bool scan_for_new_elements =
    (min_display_size < min_composited_size) && !buffer_region.is_empty();
  min_composited_size = (min_display_size < 1)?1:min_display_size;
  if ((expansion_numerator.x == 0) || (expansion_numerator.y == 0) ||
      (expansion_denominator.x == 0) || (expansion_denominator.y == 0))
    return; // Geometry not set yet

  kdu_long val;
  kdu_coords num = expansion_numerator;
  kdu_coords den = expansion_denominator;
  if (transpose)
    { num.transpose();  den.transpose(); } // Map to original geometry
  val = min_composited_size-1;  val *= subsampling.x;
  val = val * den.x;  val = val / num.x;
  min_size = (int) val;
  val = min_composited_size-1;  val *= subsampling.y;
  val = val * den.y;  val = val / num.y;
  if (min_size > (int) val)
    min_size = (int) val;
  min_size += 1;

  first_unprocessed_seg = num_segs; // Until proven otherwise
  for (int seg_num=0; seg_num < num_segs; seg_num++)
    { 
      kdrc_overlay_segment *seg=seg_refs[seg_num];
      assert((seg != NULL) && (seg->next == NULL)); // Don't support lists yet
      assert((seg->region & buffer_region) == seg->region);

      kdrc_roinode *scan, *prev, *next;
      if (aux_params_changed)
        { // Have to repaint the entire segment
          seg->first_unpainted = seg->head;
          seg->needs_erase = seg->needs_process = true;
        }
      bool need_seg_repaint = false;
      bool have_visible_elements = false;
      for (prev=NULL, scan=seg->head; scan != NULL; prev=scan, scan=next)
        { 
          next = scan->next;
          if (scan->cs_width < min_size)
            { // Remove element
              if (prev == NULL)
                seg->head = next;
              else
                prev->next = next;
              scan->next = free_nodes;
              free_nodes = scan;
              scan = prev; // So `prev' won't change
              if (!scan->hidden)
                need_seg_repaint = true;
              continue;
            }
          if (dependencies_changed)
            {
              bool should_hide = false;
              if (dependencies != NULL)
                should_hide = !dependencies->evaluate(scan->node);
              if (should_hide != scan->hidden)
                need_seg_repaint = true;
              scan->hidden = should_hide;
            }
          if (!scan->hidden)
            have_visible_elements = true;
        }
    
      if (scan_for_new_elements)
        {
          kdu_dims cs_region = seg->region;
          cs_region.pos.x -= params.max_painting_border;
          cs_region.pos.y -= params.max_painting_border;
          cs_region.size.x += 2*params.max_painting_border;
          cs_region.size.y += 2*params.max_painting_border;
          map_from_compositing_grid(cs_region);
          meta_manager.load_matches(1,&params.codestream_idx,0,NULL);
          jpx_metanode mn;
          while ((mn =
                  meta_manager.enumerate_matches(mn,params.codestream_idx,
                                                 params.compositing_layer_idx,
                                                 false,cs_region,min_size,
                                                 false,true)).exists())
            { 
              int cs_width = mn.get_width();
              if (cs_width < min_size)
                continue; // Should not happen because `min_size' was passed to
                          // `enumerate_matches' above
              kdu_dims bounding_box = mn.get_bounding_box();
              kdu_long sort_area = bounding_box.area();
              map_to_compositing_grid(bounding_box);
              bounding_box.pos.x -= params.max_painting_border;
              bounding_box.pos.y -= params.max_painting_border;
              bounding_box.size.x += 2*params.max_painting_border;
              bounding_box.size.y += 2*params.max_painting_border;
              bounding_box &= seg->region;
              if (bounding_box.is_empty())
                continue;
              prev = NULL;
              for (scan=seg->head; scan != NULL; prev=scan, scan=scan->next)
                if ((scan->node == mn) || (scan->sort_area < sort_area))
                  break;
              if ((scan != NULL) && (scan->node == mn))
                continue; // Node already exists on the list; change nothing
              kdrc_roinode *elt = node_alloc();
              elt->node = mn;
              elt->bounding_box = bounding_box;
              elt->sort_area = sort_area;
              elt->cs_width = cs_width;
              elt->next = scan;
              if (prev == NULL)
                seg->head = elt;
              else
                prev->next = elt;
              elt->hidden = false;
              if (dependencies != NULL)
                elt->hidden = !dependencies->evaluate(mn);
              if (!elt->hidden)
                { 
                  have_visible_elements = true;
                  need_seg_repaint = true;
                }
            }
        }

      if (seg->is_visible &&
          (blending_factor_changed || !have_visible_elements))
        seg->needs_process = true; // Have to regenerate composition over seg
      seg->is_visible = have_visible_elements;
      if (need_seg_repaint)
        { 
          seg->needs_erase = true;
          seg->first_unpainted = seg->head;
          while ((seg->first_unpainted != NULL) &&
                 seg->first_unpainted->hidden)
            seg->first_unpainted = seg->first_unpainted->next;
          if (seg->first_unpainted != NULL)
            seg->needs_process = true;
        }
      if (seg->needs_process && (seg_num < first_unprocessed_seg))
        first_unprocessed_seg = seg_num;
    }
}

/*****************************************************************************/
/*                          kdrc_overlay::process                            */
/*****************************************************************************/

bool
  kdrc_overlay::process(kdu_dims &seg_region, bool &painted_something)
{
  if (buffer == NULL)
    return false;
  kdrc_overlay_segment *seg = NULL;
  
  while ((first_unprocessed_seg < num_segs) &&
         (!(seg = seg_refs[first_unprocessed_seg])->needs_process))
    {
      assert(seg->first_unpainted == NULL);
      first_unprocessed_seg++;
    }
  if (first_unprocessed_seg >= num_segs)
    return false;
  
  kdrc_roinode *scan;
  for (scan=seg->first_unpainted; scan != NULL; scan=scan->next)
    { 
      if (scan->hidden)
        continue;
      if (seg->needs_erase)
        { // Don't need to erase until there is something to paint
          int row_gap=0;
          kdu_uint32 *ibuf = buffer->get_buf(row_gap,false);
          if (ibuf != NULL)
            erase_buffer_region(ibuf,row_gap,buffer_region,seg->region,
                                0x00FFFFFF);
          else
            { 
              float *fbuf = buffer->get_float_buf(row_gap,false);
              erase_float_buffer_region(fbuf,row_gap,buffer_region,seg->region,
                                        0x00FFFFFF);
            }
          seg->needs_erase = false;
        }
      if (!compositor->custom_paint_overlay(buffer,buffer_region,
                                            scan->bounding_box,
                                            scan->node,&params,image_offset,
                                            subsampling,transpose,vflip,hflip,
                                            expansion_numerator,
                                            expansion_denominator,
                                            compositing_offset,
                                            compositor->colour_order))
        compositor->paint_overlay(buffer,buffer_region,scan->bounding_box,
                                  scan->node,&params,image_offset,
                                  subsampling,transpose,vflip,hflip,
                                  expansion_numerator,expansion_denominator,
                                  compositing_offset);
      params.restore_aux_params();
      painted_something = true;
    }
  if (seg->is_visible && seg->needs_erase)
    { // Strange; we didn't actually have anything to paint???
      assert(0);
      seg->is_visible = false;
    }
  seg->first_unpainted = NULL;
  seg->needs_process = false;
  seg_region = seg->region;
  first_unprocessed_seg++;
  while ((first_unprocessed_seg < num_segs) &&
         (!(seg = seg_refs[first_unprocessed_seg])->needs_process))
    {
      assert(seg->first_unpainted == NULL);
      first_unprocessed_seg++;
    }
  
  return true;
}

/*****************************************************************************/
/*                         kdrc_overlay::count_nodes                         */
/*****************************************************************************/

void kdrc_overlay::count_nodes(int &total_nodes, int &hidden_nodes)
{
  for (int seg_num=0; seg_num < num_segs; seg_num++)
    {
      kdrc_overlay_segment *seg = seg_refs[seg_num];
      kdrc_roinode *scan;
      for (scan=seg->head; scan != NULL; scan=scan->next)
        {
          total_nodes++;
          if (scan->hidden)
            hidden_nodes++;
        }
    }
}

/*****************************************************************************/
/*                            kdrc_overlay::search                           */
/*****************************************************************************/

jpx_metanode
  kdrc_overlay::search(kdu_coords point)
{
  kdrc_roinode *best = NULL;
  kdu_coords off;
  for (int seg_num=0; seg_num < num_segs; seg_num++)
    {
      kdrc_overlay_segment *seg = seg_refs[seg_num];
      if ((seg == NULL) || (seg->head == NULL))
        continue;
      off = point - seg->region.pos;
      if ((off.x < 0) || (off.x >= seg->region.size.x) ||
          (off.y < 0) || (off.y >= seg->region.size.y))
        continue;
      kdrc_roinode *scan;
      for (scan=seg->head; scan != NULL; scan=scan->next)
        {
          /*
          if (scan->hidden)
            continue;
           */
          off = point - scan->bounding_box.pos;
          if ((off.x < 0) || (off.x >= scan->bounding_box.size.x) ||
              (off.y < 0) || (off.y >= scan->bounding_box.size.y))
            continue;
          int num_regions = scan->node.get_num_regions();
          const jpx_roi *regn = scan->node.get_regions();
          for (; num_regions > 0; num_regions--, regn++)
            {
              jpx_roi mapped_rgn;
              map_jpx_roi_to_compositing_grid(&mapped_rgn,regn,
                                              image_offset,subsampling,
                                              transpose,vflip,hflip,
                                              expansion_numerator,
                                              expansion_denominator,
                                              compositing_offset);
              if (!mapped_rgn.contains(point))
                continue;
              if ((best == NULL) || (best->sort_area > scan->sort_area))
                best = scan;
              break;
            }
        }
    }
  
  if (best != NULL)
    return best->node;
  return jpx_metanode(NULL);
}

/*****************************************************************************/
/*                  kdrc_overlay::map_from_compositing_grid                  */
/*****************************************************************************/

void
  kdrc_overlay::map_from_compositing_grid(kdu_dims &region)
{
  region.pos += compositing_offset;
  kdu_coords subs = subsampling;
  if (transpose)
    subs.transpose();
  region =
    kdu_region_decompressor::find_codestream_cover_dims(region,subs,
                                                        expansion_numerator,
                                                        expansion_denominator,
                                                        true);
  region.from_apparent(transpose,vflip,hflip);
  region.pos -= image_offset;
}

/*****************************************************************************/
/*                   kdrc_overlay::map_to_compositing_grid                   */
/*****************************************************************************/

void
  kdrc_overlay::map_to_compositing_grid(kdu_dims &region)
{
  region.pos += image_offset;
  kdu_coords subs = subsampling;
  if (transpose)
    subs.transpose();
  region.to_apparent(transpose,vflip,hflip);
  region =
    kdu_region_decompressor::find_render_cover_dims(region,subs,
                                                    expansion_numerator,
                                                    expansion_denominator,
                                                    true);
  region.pos -= compositing_offset;
}


/* ========================================================================= */
/*                              kdrc_codestream                              */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdrc_codestream::init (raw)                        */
/*****************************************************************************/

bool
  kdrc_codestream::init(kdu_compressed_source *source, kdu_thread_env *env)
{
  if (ifc.exists())
    return true; // Already initialized
  this->env = env;
  ifc.create(source,env);
  if (persistent)
    {
      ifc.set_persistent();
      ifc.augment_cache_threshold(cache_threshold);
    }
  ifc.get_dims(-1,canvas_dims);
  return true;
}

/*****************************************************************************/
/*                       kdrc_codestream::init (JP2/JPX)                     */
/*****************************************************************************/

bool
  kdrc_codestream::init(jpx_codestream_source stream, kdu_thread_env *env)
{
  if (ifc.exists())
    return true; // Already initialized
  if (!stream.stream_ready())
    { 
      canvas_dims.pos = kdu_coords(0,0);
      canvas_dims.size = stream.access_dimensions().get_size();
      return false;
    }
  this->env = env;
  stream.open_stream(&source_box);
  ifc.create(&source_box,env);
  if (persistent)
    { 
      ifc.set_persistent();
      ifc.augment_cache_threshold(cache_threshold);
    }
  ifc.get_dims(-1,canvas_dims);
  return true;
}

/*****************************************************************************/
/*                         kdrc_codestream::init (MJ2)                       */
/*****************************************************************************/

bool
  kdrc_codestream::init(mj2_video_source *track, int frame_idx, int field_idx,
                        kdu_thread_env *env)
{
  if (ifc.exists())
    return true; // Already initialized
  this->env = env;
  track->seek_to_frame(frame_idx);
  track->open_stream(field_idx,&source_box);
  ifc.create(&source_box,env);
  if (persistent)
    {
      ifc.set_persistent();
      ifc.augment_cache_threshold(cache_threshold);
    }
  ifc.get_dims(-1,canvas_dims);
  return true;
}

/*****************************************************************************/
/*                         kdrc_codestream::restart                          */
/*****************************************************************************/

bool
  kdrc_codestream::restart(mj2_video_source *track,
                           int frame_idx, int field_idx)
{
  assert(ifc.exists());
  source_box.close();
  track->seek_to_frame(frame_idx);
  track->open_stream(field_idx,&source_box);
  ifc.restart(&source_box,env);
  return true;
}

/*****************************************************************************/
/*                           kdrc_codestream::attach                         */
/*****************************************************************************/

void
  kdrc_codestream::attach(kdrc_stream *user)
{
  assert(user->codestream == NULL);
  user->next_codestream_user = head;
  user->prev_codestream_user = NULL;
  if (head != NULL)
    {
      assert(head->prev_codestream_user == NULL);
      head->prev_codestream_user = user;
    }
  head = user;
  user->codestream = this;

  // Scan the remaining users for this code-stream to make sure they are not
  // in the midst of processing.
  kdrc_stream *scan=head->next_codestream_user;
  for (; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();
  assert(!in_use);

  if (ifc.exists())
    { // Remove any appearance changes or input restrictions
      ifc.change_appearance(false,false,false);
      ifc.apply_input_restrictions(0,0,0,0,NULL,KDU_WANT_OUTPUT_COMPONENTS);
    }
}

/*****************************************************************************/
/*                           kdrc_codestream::detach                         */
/*****************************************************************************/

void
  kdrc_codestream::detach(kdrc_stream *user)
{
  assert(user->codestream == this);
  if (user->prev_codestream_user == NULL)
    {
      assert(user == head);
      head = user->next_codestream_user;
      if (head != NULL)
        head->prev_codestream_user = NULL;
    }
  else
    {
      assert(user != head);
      user->prev_codestream_user->next_codestream_user =
        user->next_codestream_user;
    }
  if (user->next_codestream_user != NULL)
    user->next_codestream_user->prev_codestream_user =
      user->prev_codestream_user;
  user->codestream = NULL;
  user->prev_codestream_user = user->next_codestream_user = NULL;
  if (head == NULL)
    { 
      if (ifc.exists() && (env != NULL))
        env->cs_terminate(ifc);
      env = NULL;
      delete this;
    }
}

/*****************************************************************************/
/*                       kdrc_codestream::move_to_head                       */
/*****************************************************************************/

void
  kdrc_codestream::move_to_head(kdrc_stream *user)
{
  assert(user->codestream == this);

  // First unlink from the list
  if (user->prev_codestream_user == NULL)
    {
      assert(user == head);
      head = user->next_codestream_user;
      if (head != NULL)
        head->prev_codestream_user = NULL;
    }
  else
    {
      assert(user != head);
      user->prev_codestream_user->next_codestream_user =
        user->next_codestream_user;
    }
  if (user->next_codestream_user != NULL)
    user->next_codestream_user->prev_codestream_user =
      user->prev_codestream_user;

  // Now link back in at head
  user->next_codestream_user = head;
  user->prev_codestream_user = NULL;
  if (head != NULL)
    {
      assert(head->prev_codestream_user == NULL);
      head->prev_codestream_user = user;
    }
  head = user;

  // Scan all users for this code-stream to make sure they are not
  // in the midst of processing.
  kdrc_stream *scan;
  for (scan=head; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();
  assert(!in_use);
}

/*****************************************************************************/
/*                       kdrc_codestream::move_to_tail                       */
/*****************************************************************************/

void
  kdrc_codestream::move_to_tail(kdrc_stream *user)
{
  assert(user->codestream == this);

  // First unlink from the list
  if (user->prev_codestream_user == NULL)
    {
      assert(user == head);
      head = user->next_codestream_user;
      if (head != NULL)
        head->prev_codestream_user = NULL;
    }
  else
    {
      assert(user != head);
      user->prev_codestream_user->next_codestream_user =
        user->next_codestream_user;
    }
  if (user->next_codestream_user != NULL)
    user->next_codestream_user->prev_codestream_user =
      user->prev_codestream_user;

  // Now link back in at tail
  kdrc_stream *prev=NULL, *scan;
  for (scan=head; scan != NULL; prev=scan, scan=scan->next_codestream_user);
  user->prev_codestream_user = prev;
  user->next_codestream_user = NULL;
  if (prev == NULL)
    head = user;
  else
    prev->next_codestream_user = user;

  // Scan all users for this code-stream to make sure they are not
  // in the midst of processing.
  for (scan=head; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();
  assert(!in_use);
}


/* ========================================================================= */
/*                         kdrc_stream_scaling_params                        */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdrc_stream_scaling_params::configure                   */
/*****************************************************************************/

bool
  kdrc_stream_scaling_params::configure(const kdu_dims &full_source_dims,
                                        const kdu_dims &full_target_dims,
                                        bool transpose, bool vflip,
                                        bool hflip, float scale,
                                        int &invalid_scale_code,
                                        kdu_dims target_frame,
                                        kdu_codestream codestream,
                                        int active_component,
                                        int single_component,
                                        kdu_component_access_mode access_mode,
                                        kdu_channel_mapping *mapping,
                                        int max_discard_levels,
                                        const kdu_coords active_subsampling[],
                                        float min_resampling_factor,
                                        float &min_supported_scale,
                                        float &max_supported_scale)
{
  this->scale = scale;
  
  // Find scaling factors relative to the high resolution codestream canvas
  double scale_x, scale_y;
  scale_x = full_target_dims.size.x / (double) full_source_dims.size.x;
  scale_y = full_target_dims.size.y / (double) full_source_dims.size.y;
  scale_x *= scale;
  scale_y *= scale;
  
  // Now we are ready to determine the number of discard levels which we
  // should use to approximate the required scaling factors, thereby limiting
  // the amount of explicit scaling required.
  double discard_thresh = min_resampling_factor;
  if (discard_thresh < 0.501)
    discard_thresh = 0.501;
  else if (discard_thresh > 0.999)
    discard_thresh = 0.999;
  discard_levels = 0;
  while ((discard_levels < max_discard_levels) &&
         (((scale_x*active_subsampling[discard_levels].x) < discard_thresh) ||
          ((scale_y*active_subsampling[discard_levels].y) < discard_thresh)))
    discard_levels++;
  
  // Convert `scale_x' and `scale_y' to measure scaling factors relative to
  // the active image component, after applying `discard_levels'.
  scale_x *= active_subsampling[discard_levels].x;
  scale_y *= active_subsampling[discard_levels].y;
  
  // Now for the second phase in which we approximate the scaling factors
  // as rational quantities with reasonable choices for the numerator and
  // denominator, then apply the determined scaling factors and number of
  // discard levels to determine the geometry of the composited imagery.
  // ---------------
  
  // Remember ideal scaling factors before we make any approximations
  double ideal_scale_x = scale_x;
  double ideal_scale_y = scale_y;
  
  // Begin by working out tolerances based on the full size of the target
  // frame onto which we are compositing things (prior to accounting for any
  // re-orientation).  The tolerances here are the maximum acceptable
  // values for abs(scale_x-ideal_scale_x) and abs(scale_y-ideal_scale_y).
  target_frame.augment(full_target_dims);
  double extent_x = target_frame.size.x * scale;
  double extent_y = target_frame.size.y * scale;
  double tol_x = ideal_scale_x * (0.5 / (1.0+extent_x));
  double tol_y = ideal_scale_y * (0.5 / (1.0+extent_y));
  
  // Avoid any rational expansion/contraction if we are extremely close to
  // the right scale already
  if ((scale_x > (1.0-tol_x)) && (scale_x < (1.0+tol_x)))
    scale_x = 1.0;
  if ((scale_y > (1.0-tol_y)) && (scale_y < (1.0+tol_y)))
    scale_y = 1.0;
  
  // Now check that the expansion factors lie within a valid range
  if (codestream.exists())
    { 
      double min_prod=1.0, max_x=1.0, max_y=1.0;
      kdu_region_decompressor::get_safe_expansion_factors(codestream,mapping,
                                                          single_component,
                                                          discard_levels,
                                                          min_prod,max_x,max_y,
                                                          access_mode);
      if ((scale_x > (max_x*1.1)) || (scale_y > (max_y*1.1)))
        { 
          invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_LARGE;
          double ratio = scale_x / max_x;
          if (ratio*max_y < scale_y)
            ratio = scale_y / max_y;
          max_supported_scale = (float)(scale / ratio);
          return false;
        }
      if ((scale_x*scale_y) < (0.9*min_prod))
        { 
          invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_SMALL;
          min_supported_scale=(float)(scale*sqrt(min_prod/(scale_x*scale_y)));
          return false;
        }
    }
  
  // Convert scale factors into rational quantities
  if (scale_x == 1.0)
    expansion_numerator.x = expansion_denominator.x = 1;
  else
    { 
      expansion_denominator.x = full_source_dims.size.x;
      assert(expansion_denominator.x > 0);
      while ((expansion_denominator.x > 1) &&
             ((ideal_scale_x * expansion_denominator.x) > (double)(1<<30)))
        expansion_denominator.x >>= 1;
      expansion_numerator.x = (int)
      ceil(ideal_scale_x*expansion_denominator.x);
      scale_x = expansion_numerator.x / ((double) expansion_denominator.x);
      double err = scale_x - ideal_scale_x;
      while (((err > tol_x) || (err < -tol_x)) &&
             (expansion_numerator.x < (1<<29)) &&
             (expansion_denominator.x < (1<<29)))
        { 
          expansion_denominator.x <<= 1;
          expansion_numerator.x = (int)
          ceil(ideal_scale_x * expansion_denominator.x);
          scale_x = expansion_numerator.x / ((double) expansion_denominator.x);
          err = scale_x - ideal_scale_x;
        }
    }
  
  if (scale_y == 1.0)
    expansion_numerator.y = expansion_denominator.y = 1;
  else
    { 
      expansion_denominator.y = full_source_dims.size.y;
      assert(expansion_denominator.y > 0);
      while ((expansion_denominator.y > 1) &&
             ((ideal_scale_y * expansion_denominator.y) > (double)(1<<30)))
        expansion_denominator.y >>= 1;
      expansion_numerator.y = (int)
      ceil(ideal_scale_y*expansion_denominator.y);
      scale_y = expansion_numerator.y / ((double) expansion_denominator.y);
      double err = scale_y - ideal_scale_y;
      while (((err > tol_y) || (err < -tol_y)) &&
             (expansion_numerator.y < (1<<29)) &&
             (expansion_denominator.y < (1<<29)))
        { 
          expansion_denominator.y <<= 1;
          expansion_numerator.y = (int)
          ceil(ideal_scale_y * expansion_denominator.y);
          scale_y = expansion_numerator.y / ((double) expansion_denominator.y);
          err = scale_y - ideal_scale_y;
        }
    }
  
  // Work out the rendered image dimensions and find the location of the
  // scaled target region on the compositing grid.
  scale_x = scale*scale_x/ideal_scale_x; // Amount by which target dims
  scale_y = scale*scale_y/ideal_scale_y; // are actually being scaled
  kdu_dims active_comp_dims;
  if (codestream.exists())
    { // In this case, we restrict the codestream to `full_source_dims',
      // apply appearance changes, and measure the active component dimensions
      // directly.
      codestream.apply_input_restrictions(0,0,discard_levels,0,
                                          &full_source_dims,
                                          access_mode);
      codestream.change_appearance(transpose,vflip,hflip);
      codestream.get_dims(active_component,active_comp_dims,true);
    }
  else
    { 
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      kdu_coords subs = active_subsampling[discard_levels];
      min.x = ceil_ratio(min.x,subs.x);  min.y = ceil_ratio(min.y,subs.y);
      lim.x = ceil_ratio(lim.x,subs.x);  lim.y = ceil_ratio(lim.y,subs.y);
      active_comp_dims.pos = min;
      active_comp_dims.size = lim - min;
      active_comp_dims.to_apparent(transpose,vflip,hflip);
    }
  if (active_comp_dims.is_empty())
    { KDU_ERROR(e,0x16021605); e <<
      KDU_TXT("The reference codestream image component on which rendering "
              "is to be based has no samples at all in the original "
              "codestream!");
    }
  
  kdu_dims composited_region;
  composited_region.pos.x = (int) (full_target_dims.pos.x * scale_x);
  composited_region.pos.y = (int) (full_target_dims.pos.y * scale_y);
    // The above two lines very deliberately round the scaled composited
    // region towards 0 rather than to the nearest integer or taking the floor
    // or ceiling function.  In most cases, the `full_target_dims.pos' values
    // are non-negative and rounding towards zero has the effect of preventing
    // gaps from opening up between composited images that were supposed to
    // lie side-by-side at full resolution.  In the event that one or both
    // of the `full_target_dims.pos' values is negative, this is because
    // the original image is natively flipped (i.e., flipped when the
    // composited region is presented in original orientation); in this case,
    // the value of `full_target_dims.pos' actually represents the negated
    // location of the right/lower edge of the region to which the image is
    // being composited so, again, rounding towards zero has the effect of
    // closing up any gaps that might appear between composited images that
    // were supposed to lie side-by-side at full resolution.
  
  if (transpose)
    { 
      expansion_numerator.transpose();
      expansion_denominator.transpose();
    }
  
  kdu_coords min_numerator;
  min_numerator.x=ceil_ratio(expansion_denominator.x,active_comp_dims.size.x);
  min_numerator.y=ceil_ratio(expansion_denominator.y,active_comp_dims.size.y);
  if ((expansion_numerator.x < min_numerator.x) ||
      (expansion_numerator.y < min_numerator.y))
    { 
      invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_SMALL;
      float ratio_x = ((float)(min_numerator.x+1)) / expansion_numerator.x;
      float ratio_y = ((float)(min_numerator.y+1)) / expansion_numerator.y;
      if (ratio_x > ratio_y)
        min_supported_scale = scale * ratio_x;
      else
        min_supported_scale = scale * ratio_y;
      return false;
    }
  full_region = // Composited region, expressed on the rendering-grid
    kdu_region_decompressor::find_render_dims(active_comp_dims,kdu_coords(1,1),
                                            expansion_numerator,
                                            expansion_denominator);
  
  composited_region.size = full_region.size;
  if (transpose)
    composited_region.size.transpose(); // Flip dimensions back to original
                                // orientation, since this is the one for
                                // which `composited_region.pos' was computed.
  
  // Check that the composited region is not shifted too far
  kdu_long lim_x = composited_region.pos.x +
    ((kdu_long) composited_region.size.x);
  kdu_long lim_y = composited_region.pos.y +
    ((kdu_long) composited_region.size.y);
  kdu_long lim_max = (lim_x > lim_y)?lim_x:lim_y;
  if (lim_max > 0x7FFFF000)
    { 
      invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_LARGE;
      max_supported_scale = scale * (((float) 0x7FFF0000) / lim_max);
      return false;
    }
  composited_region.to_apparent(transpose,vflip,hflip);
  
  // Calculate the offset between the composited region on the compositing
  // and rendering grids.
  compositing_offset = full_region.pos - composited_region.pos;
  return true;
}


/* ========================================================================= */
/*                                kdrc_stream                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdrc_stream::kdrc_stream                         */
/*****************************************************************************/

kdrc_stream::kdrc_stream(kdu_region_compositor *owner,
                         bool persistent, int cache_threshold,
                         kdu_thread_env *env, kdu_thread_queue *env_queue)
{
  this->owner = owner;
  this->had_ifc = false;
  this->persistent = persistent;
  this->cache_threshold = cache_threshold;
  this->error_level = 0;
  this->colour_order = KDU_ARGB_ORDER;
  this->env = env;
  this->env_queue = env_queue;
  mj2_track = NULL;
  mj2_frame_idx = mj2_field_idx = 0;
  alpha_only = false;
  have_alpha = false;
  alpha_is_premultiplied = false;
  max_supported_scale = min_supported_scale = -1.0F;

  overlay = NULL;
  max_display_layers = 1<<16;
  have_valid_scale = processing = false;
  buffer = NULL;
  int endian_test = 1;
  *((char *)(&endian_test)) = '\0';
  little_endian = (endian_test==0);
  is_complete = false;
  priority = KDRC_PRIORITY_MAX;
  istream_ref = owner->assign_new_istream_ref();
  codestream_idx = -1;
  colour_init_src = -1;
  layer = NULL;
  codestream = NULL;
  next_codestream_user = prev_codestream_user = NULL;
  active_component = single_component = reference_component = -1;
  component_access_mode = KDU_WANT_OUTPUT_COMPONENTS;
  
  this->next = owner->streams;
  owner->streams = this;
  
  decompressor.set_quality_limiting(owner->limiter,
                                    owner->limiter_ppi_x,
                                    owner->limiter_ppi_y);
}

/*****************************************************************************/
/*                          kdrc_stream::~kdrc_stream                        */
/*****************************************************************************/

kdrc_stream::~kdrc_stream()
{
  stop_processing();
  if (codestream != NULL)
    {
      codestream->detach(this);
      codestream = NULL;
    }
  if (owner != NULL)
    { // Unlink from `owner->streams' list
      if (this == owner->streams)
        owner->streams = this->next;
      else
        { 
          kdrc_stream *prev=owner->streams, *scan=prev->next;
          for (; scan != NULL; prev=scan, scan=scan->next)
            if (scan == this)
              { prev->next = this->next; break; }
        }
    }
  if (overlay != NULL)
    delete overlay;
}

/*****************************************************************************/
/*                  kdrc_stream::inititialize_codestream_ifc                 */
/*****************************************************************************/

bool kdrc_stream::initialize_codestream_ifc()
{
  assert(codestream != NULL);
  if (codestream->ifc.exists())
    return true;
  if (jpx_stream.exists())
    codestream->init(jpx_stream,env);
  else if (mj2_track != NULL)
    codestream->init(mj2_track,mj2_frame_idx,mj2_field_idx,env);
  else
    assert(0);
  return codestream->ifc.exists();
}

/*****************************************************************************/
/*                           kdrc_stream::init (raw)                         */
/*****************************************************************************/

void
  kdrc_stream::init(kdrc_codestream *new_cs, kdrc_stream *sibling)
{
  assert(codestream == NULL);
  mj2_track = NULL;
  mj2_frame_idx = mj2_field_idx = 0;
  assert(overlay == NULL);

  alpha_only = false;
  have_alpha = false;
  alpha_is_premultiplied = false;
  codestream_idx = 0;
  colour_init_src = -1;
  single_component = -1;
  component_access_mode = KDU_WANT_OUTPUT_COMPONENTS;
  assert(codestream == NULL);
  if (sibling != NULL)
    {
      sibling->codestream->attach(this);
      assert(codestream == sibling->codestream);
    }
  else
    {
      new_cs->attach(this);
      assert(codestream == new_cs);
    }
  if (codestream->ifc.exists())
    { 
      mapping.configure(codestream->ifc);
      max_discard_levels = codestream->ifc.get_min_dwt_levels();
      can_flip = codestream->ifc.can_flip(false);
      had_ifc = true;
    }
  else
    { // Make some assumptions until the codestream header becomes available
      mapping.configure(1,8,false); // Simple default initialization
      max_discard_levels = 32;
      can_flip = true;
      had_ifc = false;
    }
  reference_component = mapping.get_source_component(0);
  active_component = reference_component;
  configure_subsampling();
  have_valid_scale = processing = false;
  invalidate_surface();
}

/*****************************************************************************/
/*                           kdrc_stream::init (JPX)                         */
/*****************************************************************************/

void
  kdrc_stream::init(jpx_codestream_source stream, jpx_layer_source layer,
                    jpx_source *jpx_src, bool alpha_only, kdrc_stream *sibling,
                    int overlay_log2_segment_size)
{
  assert(codestream == NULL);
  mj2_track = NULL;
  mj2_frame_idx = mj2_field_idx = 0;
  assert(overlay == NULL);
  this->jpx_stream = stream;
  codestream_idx = stream.get_codestream_id();
  overlay = new kdrc_overlay(jpx_src->access_meta_manager(),codestream_idx,
                             overlay_log2_segment_size);
  int layer_idx = layer.get_layer_id();
  this->colour_init_src = layer_idx;
  this->layer = NULL; // Changed by `kdrc_layer' object.
  this->alpha_only = alpha_only;
  this->have_alpha = false; // Until proven otherwise
  this->alpha_is_premultiplied = false;
  single_component = -1;
  component_access_mode = KDU_WANT_OUTPUT_COMPONENTS;
  jp2_channels channels = layer.access_channels();
  jp2_palette palette = stream.access_palette();
  jp2_dimensions dimensions = stream.access_dimensions();
  if (alpha_only)
    { 
      mapping.clear();
      if (mapping.add_alpha_to_configuration(channels,codestream_idx,
                                             palette,dimensions,true))
        have_alpha = true;
      else if (mapping.add_alpha_to_configuration(channels,codestream_idx,
                                                  palette,dimensions,false))
        have_alpha = alpha_is_premultiplied = true;
      else
        { KDU_ERROR(e,0); e <<
          KDU_TXT("Complex opacity representation for compositing "
                  "layer (index, starting from 0, equals ") << layer_idx <<
          KDU_TXT(") cannot be implemented without the "
                  "inclusion of multiple distinct alpha blending channels.");
        }
    }
  else
    { 
      // Hunt for a good colour space
      jp2_colour tmp_colour, colour;
      int which, tmp_prec, prec, lim_prec=128;
      do {
        prec = -10000;
        colour = jp2_colour(NULL);
        which = 0;
        while ((tmp_colour=layer.access_colour(which++)).exists())
          { 
            tmp_prec = tmp_colour.get_precedence();
            if (tmp_prec >= lim_prec)
              continue;
            if (tmp_prec > prec)
              { 
                prec = tmp_prec;
                colour = tmp_colour;
              }
          }
        if (!colour)
          { KDU_ERROR(e,1); e <<
            KDU_TXT("Unable to find any colour description which "
                    "can be used by the present implementation to render "
                    "compositing layer (index, starting from 0, equals ")
            << layer_idx <<
            KDU_TXT(") to sRGB."); }
        lim_prec = prec;
      } while (!mapping.configure(colour,channels,codestream_idx,
                                  palette,dimensions));
      if (mapping.add_alpha_to_configuration(channels,codestream_idx,
                                             palette,dimensions,true))
        have_alpha = true;
      else if (mapping.add_alpha_to_configuration(channels,codestream_idx,
                                                  palette,dimensions,false))
        have_alpha = alpha_is_premultiplied = true;
    }
  assert(mapping.num_channels ==
         (mapping.num_colour_channels + ((have_alpha)?1:0)));
  reference_component = mapping.get_source_component(0);
  active_component = reference_component;

  assert(codestream == NULL);
  if (sibling != NULL)
    {
      sibling->codestream->attach(this);
      assert(codestream == sibling->codestream);
    }
  else
    { 
      kdrc_codestream *cs = new kdrc_codestream(persistent,cache_threshold);
      cs->attach(this);
      assert(codestream == cs);
    }
  if (initialize_codestream_ifc())
    { 
      max_discard_levels = codestream->ifc.get_min_dwt_levels();
      can_flip = codestream->ifc.can_flip(false);
      had_ifc = true;
    }
  else
    { // Make some assumptions until such point as the codestream actually
      // does exist
      max_discard_levels = 32;
      can_flip = true;
      had_ifc = false;
    }
  configure_subsampling();
  have_valid_scale = processing = false;
  invalidate_surface();
}

/*****************************************************************************/
/*                           kdrc_stream::init (MJ2)                         */
/*****************************************************************************/

void
  kdrc_stream::init(mj2_video_source *track, int frame_idx, int field_idx,
                    kdrc_stream *sibling)
{
  assert(codestream == NULL);
  this->mj2_track = track;
  mj2_frame_idx = frame_idx;
  mj2_field_idx = field_idx;
  assert(overlay == NULL);
  alpha_only = false;
  have_alpha = false;
  alpha_is_premultiplied = false;

  if ((field_idx < 0) || (field_idx > 1) ||
      ((track->get_field_order() == KDU_FIELDS_NONE) && (field_idx != 0)))
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("Invalid field index passed to `kdrc_stream::init' "
        "when initializing the codestream management for a Motion JPEG2000 "
        "track.");
    }
  track->seek_to_frame(frame_idx);
  codestream_idx = track->get_stream_idx(field_idx);
  int track_idx = ((int) track->get_track_idx()) - 1;
  this->colour_init_src = track_idx;
  this->layer = NULL; // Changed by `kdrc_layer' object.
  
  single_component = -1;
  component_access_mode = KDU_WANT_OUTPUT_COMPONENTS;
  jp2_channels channels = track->access_channels();
  jp2_palette palette = track->access_palette();
  jp2_dimensions dimensions = track->access_dimensions();
  jp2_colour colour = track->access_colour();
  
  // Hunt for a good colour space
  if (!mapping.configure(colour,channels,0,palette,dimensions))
    { KDU_ERROR(e,3); e <<
      KDU_TXT("Unable to find any colour description which "
              "can be used by the present implementation to render MJ2 track "
              "(index, starting from 0, equals ") << track_idx <<
      KDU_TXT(") to sRGB.");
    }
  
  if (track->get_graphics_mode() == MJ2_GRAPHICS_ALPHA)
    { 
      mapping.add_alpha_to_configuration(channels,0,palette,dimensions,true);
      have_alpha = true;
    }
  else if ((track->get_graphics_mode() == MJ2_GRAPHICS_PREMULT_ALPHA) &&
           mapping.add_alpha_to_configuration(channels,0,palette,
                                              dimensions,false))
    { 
      have_alpha = true;
      alpha_is_premultiplied = true;
    }
  reference_component = mapping.get_source_component(0);
  active_component = reference_component;
  
  assert(codestream == NULL);
  if (sibling != NULL)
    {
      sibling->codestream->attach(this);
      assert(codestream == sibling->codestream);
    }
  else
    { 
      kdrc_codestream *cs = new kdrc_codestream(persistent,cache_threshold);
      cs->attach(this);
      assert(codestream == cs);
    }
  if (initialize_codestream_ifc())
    { 
      codestream->ifc.enable_restart(); // So we can restart
      max_discard_levels = codestream->ifc.get_min_dwt_levels();
      can_flip = codestream->ifc.can_flip(false);
      had_ifc = true;
    }
  else
    { 
      max_discard_levels = 32;
      can_flip = true;
      had_ifc = false;
    }
  configure_subsampling();
  have_valid_scale = processing = false;
  invalidate_surface();
}

/*****************************************************************************/
/*                         kdrc_stream::change_frame                         */
/*****************************************************************************/

void
  kdrc_stream::change_frame(int frame_idx)
{
  if ((mj2_track == NULL) || (frame_idx == mj2_frame_idx))
    return;
  assert(codestream != NULL);

  stop_processing();
  mj2_frame_idx = frame_idx;
  mj2_track->seek_to_frame(frame_idx);
  codestream_idx = mj2_track->get_stream_idx(mj2_field_idx);
  if ((codestream != NULL) && codestream->ifc.exists() &&
      !codestream->restart(mj2_track,frame_idx,mj2_field_idx))
    {
      codestream->detach(this);
      codestream = NULL;
    }
  if (codestream == NULL)
    { 
      kdrc_codestream *cs = new kdrc_codestream(persistent,cache_threshold);
      cs->attach(this);
      assert(codestream == cs);
    }
  if (initialize_codestream_ifc())
    { 
      codestream->ifc.enable_restart(); // So we can restart
      max_discard_levels = codestream->ifc.get_min_dwt_levels();
      can_flip = codestream->ifc.can_flip(false);
      had_ifc = true;
    }
  else
    { 
      max_discard_levels = 32;
      can_flip = true;
      had_ifc = false;
    }
  configure_subsampling();
  invalidate_surface();
}

/*****************************************************************************/
/*                        kdrc_stream::set_error_level                       */
/*****************************************************************************/

void
  kdrc_stream::set_error_level(int error_level)
{
  assert(codestream != NULL);
  this->error_level = error_level;
  if (codestream->ifc.exists())
    { 
      switch (error_level) {
        case 0: codestream->ifc.set_fast(); break;
        case 1: codestream->ifc.set_fussy(); break;
        case 2: codestream->ifc.set_resilient(false); break;
        default: codestream->ifc.set_resilient(true);
      }
    }
}

/*****************************************************************************/
/*                    kdrc_stream::update_quality_limiting                   */
/*****************************************************************************/

void
  kdrc_stream::update_quality_limiting()
{
  decompressor.set_quality_limiting(owner->limiter,
                                    owner->limiter_ppi_x,
                                    owner->limiter_ppi_y);
}

/*****************************************************************************/
/*                            kdrc_stream::set_mode                          */
/*****************************************************************************/

int
  kdrc_stream::set_mode(int single_idx, kdu_component_access_mode access_mode)
{
  if (single_idx < 0)
    access_mode = KDU_WANT_OUTPUT_COMPONENTS;
  if ((single_idx == this->single_component) &&
      (access_mode == this->component_access_mode))
    return single_component;
  min_supported_scale = max_supported_scale = -1.0;

  // Scan all users for this code-stream to make sure they are not
  // in the midst of processing.
  kdrc_stream *scan;
  for (scan=codestream->head; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();

  this->single_component = single_idx;
  this->component_access_mode = access_mode;
  if (single_component >= 0)
    active_component = single_component;
  else
    active_component = reference_component;
  configure_subsampling(); // May change `single_component' if it is invalid

  have_valid_scale = false;
  saved_full_source_dims = saved_full_target_dims = kdu_dims();
  saved_source_sampling = saved_source_denominator = kdu_coords();
  buffer = NULL;
  invalidate_surface();
  return single_component;
}

/*****************************************************************************/
/*                         kdrc_stream::set_thread_env                       */
/*****************************************************************************/
  
void
  kdrc_stream::set_thread_env(kdu_thread_env *env, kdu_thread_queue *env_queue)
{
  if ((env != this->env) && processing)
    { kdu_error e; e << "Attempting to change the access thread "
      "associated with a `kdu_region_compositor' object, or "
      "move between multi-threaded and single-threaded access, "
      "while processing in the previous thread or environment is "
      "still going on."; }
  if (codestream != NULL)
    codestream->set_thread_env(env);
  this->env = env;
  this->env_queue = env_queue;
}

/*****************************************************************************/
/*                      kdrc_stream::configure_subsampling                   */
/*****************************************************************************/

void
  kdrc_stream::configure_subsampling()
{
  int d;
  assert(active_component >= 0);
  if (max_discard_levels > 32)
    max_discard_levels = 32; // Just in case
  max_supported_scale = min_supported_scale = -1.0F; // revert to unknown
  max_nonempty_levels = max_discard_levels;
  if (codestream->ifc.exists())
    { 
      for (d=max_discard_levels; d >= 0; d--)
        { 
          codestream->ifc.apply_input_restrictions(0,0,d,0,NULL,
                                                   component_access_mode);
          if ((d == max_discard_levels) && (single_component >= 0))
            { // Check that `single_component' is valid
              int num_comps = codestream->ifc.get_num_components(true);
              if (num_comps <= single_component)
                single_component = active_component = num_comps-1;
            }
          kdu_dims dims;
          codestream->ifc.get_dims(active_component,dims,true);
          if (dims.is_empty() && (d > 1) && (d <= max_nonempty_levels))
            max_nonempty_levels = d-1;
          kdu_coords subs;
          codestream->ifc.get_subsampling(active_component,subs,true);
          active_subsampling[d] = subs; // (0,0) if dims.is_empty().
        }
      
    }
  else
    { 
      kdu_coords subs;  subs.x = subs.y = 1;
      for (d=0; d <= max_discard_levels; d++, subs.x+=subs.x, subs.y+=subs.y)
        active_subsampling[d] = subs;
    }
  if (max_discard_levels > max_nonempty_levels)
    max_discard_levels = max_nonempty_levels;
}

/*****************************************************************************/
/*                       kdrc_stream::invalidate_surface                     */
/*****************************************************************************/

void
  kdrc_stream::invalidate_surface()
{
  stop_processing();
  valid_region.pos = active_region.pos;
  valid_region.size = kdu_coords(0,0);
  region_in_process=incomplete_region=partially_complete_region = valid_region;
  is_complete = false;
  priority = KDRC_PRIORITY_MAX;
  if (buffer != NULL)
    buffer->reset_rendered_rows();
}

/*****************************************************************************/
/*                      kdrc_stream::get_components_in_use                   */
/*****************************************************************************/

int
  kdrc_stream::get_components_in_use(int *indices)
{
  int i, c, n=0;

  if (single_component >= 0)
    indices[n++] = single_component;
  else
    for (c=0; c < mapping.num_channels; c++)
      {
        int idx = mapping.source_components[c];
        for (i=0; i < n; i++)
          if (idx == indices[i])
            break;
        if (i == n)
          indices[n++] = idx;
      }
  for (i=n; i < 4; i++)
    indices[i] = -1;
  return (component_access_mode==KDU_WANT_OUTPUT_COMPONENTS)?n:(-n);
}

/*****************************************************************************/
/*                           kdrc_stream::set_scale                          */
/*****************************************************************************/

bool
  kdrc_stream::set_scale(kdu_dims full_source_dims, kdu_dims full_target_dims,
                         kdu_coords source_sampling,
                         kdu_coords source_denominator,
                         bool transpose, bool vflip, bool hflip,
                         float notional_scale, float rendering_scale,
                         int &invalid_scale_code, kdu_dims target_frame)
{
  bool did_have_ifc = this->had_ifc;
  if ((!did_have_ifc) && this->initialize_codestream_ifc())
    { // Need to re-configure the information that depends upon the
      // `kdu_codestream' interface, now that it exists.
      assert(codestream->ifc.exists());
      if (error_level != 0)
        set_error_level(error_level);
      max_discard_levels = codestream->ifc.get_min_dwt_levels();
      can_flip = codestream->ifc.can_flip(false);
      had_ifc = true;
      configure_subsampling();
    }
  invalid_scale_code = 0;
  bool no_change = true;
  if ((saved_full_source_dims != full_source_dims) ||
      (saved_full_target_dims != full_target_dims) ||
      (saved_source_sampling != source_sampling) ||
      (saved_source_denominator != source_denominator) ||
      (this->had_ifc != did_have_ifc))
    {
      no_change = false;
      min_supported_scale = max_supported_scale = -1.0F; // revert to unknown
    }
  no_change = (no_change && have_valid_scale &&
               (this->vflip == vflip) && (this->hflip == hflip) &&
               (this->transpose == transpose) &&
               (this->notional_params.scale == notional_scale) &&
               (this->rendering_params.scale == rendering_scale));

  have_valid_scale = false;
  if ((vflip || hflip) && !can_flip)
    { // Required flipping is not supported
      invalid_scale_code |= KDU_COMPOSITOR_CANNOT_FLIP;
      return false;
    }
  if ((notional_scale < min_supported_scale) ||
      (rendering_scale < min_supported_scale))
    {
      invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_SMALL;
      return false;
    }
  if ((max_supported_scale > 0.0F) &&
      ((notional_scale > max_supported_scale) ||
       (rendering_scale > max_supported_scale)))
    {
      invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_LARGE;
      return false;
    }

  // Scan all users for this code-stream to make sure they are not
  // in the midst of processing.
  kdrc_stream *scan;
  for (scan=codestream->head; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();

  this->vflip = vflip;
  this->hflip = hflip;
  this->transpose = transpose;
  saved_full_source_dims = full_source_dims;
  saved_full_target_dims = full_target_dims;
  saved_source_sampling = source_sampling;
  saved_source_denominator = source_denominator;
  
  // Determine the dimensions against which source measurements will
  // be made by `full_source_dims', `source_sampling' and `source_denominator'.
  // We will call this the "source canvas".
  kdu_dims source_canvas_dims = codestream->canvas_dims;
  if ((colour_init_src < 0) || (single_component >= 0))
    { // In this case source canvas is that of the active image component.
      kdu_coords min=source_canvas_dims.pos;
      kdu_coords lim=min+source_canvas_dims.size;
      min.x = ceil_ratio(min.x,active_subsampling[0].x);
      min.y = ceil_ratio(min.y,active_subsampling[0].y);
      lim.x = ceil_ratio(lim.x,active_subsampling[0].x);
      lim.y = ceil_ratio(lim.y,active_subsampling[0].y);
      source_canvas_dims.pos = min;
      source_canvas_dims.size = lim-min;
    }
  
  // Next fill in any missing values for `full_source_dims' or
  // `full_target_dims'.
  bool synthesized_full_source_dims = false;
  if (!full_source_dims)
    { 
      full_source_dims.pos = kdu_coords(0,0);
      full_source_dims.size.x =
      long_ceil_ratio(((kdu_long) source_canvas_dims.size.x) *
                      source_sampling.x,source_denominator.x);
      full_source_dims.size.y =
      long_ceil_ratio(((kdu_long) source_canvas_dims.size.y) *
                      source_sampling.y,source_denominator.y);
      synthesized_full_source_dims = true;
    }
  if (!full_target_dims)
    { // The tests below look for a condition left behind by flipping that
      // was may have been applied to match `kdrc_layer::src_orientation'
      full_target_dims.size = full_source_dims.size;
      if (full_target_dims.pos.x != 0)
        full_target_dims.pos.x = 1 - full_target_dims.size.x;
      if (full_target_dims.pos.y != 0)
        full_target_dims.pos.y = 1 - full_target_dims.size.y;
    }
  
  // Now convert `full_source_dims' to dimensions on the source canvas
  if (synthesized_full_source_dims)
    full_source_dims = source_canvas_dims;
  else
    { 
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x = long_ceil_ratio(((kdu_long) min.x)*source_denominator.x,
                              source_sampling.x);
      min.y = long_ceil_ratio(((kdu_long) min.y)*source_denominator.y,
                              source_sampling.y);
      lim.x = long_ceil_ratio(((kdu_long) lim.x)*source_denominator.x,
                              source_sampling.x);
      lim.y = long_ceil_ratio(((kdu_long) lim.y)*source_denominator.y,
                              source_sampling.y);
      full_source_dims.pos = min; full_source_dims.size = lim-min;
      full_source_dims.pos += source_canvas_dims.pos;
    }
  
  if (source_canvas_dims != codestream->canvas_dims)
    { // Only happens in single-component mode
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x *= active_subsampling[0].x;  lim.x *= active_subsampling[0].x;
      min.y *= active_subsampling[0].y;  lim.y *= active_subsampling[0].y;
      full_source_dims.pos = min; full_source_dims.size = lim-min;
    }
  
  // Now we can configure the scaling machinery and associated regions and
  // offsets.
  kdu_codestream ifc;
  if (had_ifc)
    ifc = codestream->ifc;
  if (!notional_params.configure(full_source_dims,full_target_dims,
                                 transpose,vflip,hflip,notional_scale,
                                 invalid_scale_code,target_frame,ifc,
                                 active_component,single_component,
                                 component_access_mode,
                                 (single_component<0)?&mapping:NULL,
                                 max_discard_levels,active_subsampling,
                                 owner->scaling_min_resampling_factor,
                                 min_supported_scale,
                                 max_supported_scale))
    return false;
  if (rendering_scale == notional_scale)
    rendering_params = notional_params;
  else if (!rendering_params.configure(full_source_dims,full_target_dims,
                                       transpose,vflip,hflip,rendering_scale,
                                       invalid_scale_code,target_frame,ifc,
                                       active_component,single_component,
                                       component_access_mode,
                                       (single_component<0)?&mapping:NULL,
                                       max_discard_levels,active_subsampling,
                                       owner->scaling_min_resampling_factor,
                                       min_supported_scale,
                                       max_supported_scale))
    return false;
  
  
  if (!no_change)
    {
      buffer = NULL;
      invalidate_surface();
    }
  have_valid_scale = true;

  // Pass on geometry info to the overlay manager, if appropriate
  if (overlay != NULL)
    overlay->set_geometry(source_canvas_dims.pos,
                          active_subsampling[rendering_params.discard_levels],
                          transpose,vflip,hflip,
                          rendering_params.expansion_numerator,
                          rendering_params.expansion_denominator,
                          rendering_params.compositing_offset,
                          colour_init_src);

  return true;
}

/*****************************************************************************/
/*                    kdrc_stream::find_supported_scales                     */
/*****************************************************************************/

void
  kdrc_stream::find_supported_scales(float &min_scale, float &max_scale,
                                     kdu_dims full_source_dims,
                                     kdu_dims full_target_dims,
                                     kdu_coords source_sampling,
                                     kdu_coords source_denominator)
{
  if ((saved_full_source_dims != full_source_dims) ||
      (saved_full_target_dims != full_target_dims) ||
      (saved_source_sampling != source_sampling) ||
      (saved_source_denominator != source_denominator))
    return;  
  if (min_scale < this->min_supported_scale)
    min_scale = this->min_supported_scale;
  if ((this->max_supported_scale > 0.0F) &&
      ((max_scale < 0.0F) || (max_scale > this->max_supported_scale)))
    max_scale = this->max_supported_scale;
}

/*****************************************************************************/
/*                     kdrc_stream::find_optimal_scale                       */
/*****************************************************************************/

float
  kdrc_stream::find_optimal_scale(float anchor_scale, float min_scale,
                                  float max_scale, bool avoid_subsampling,
                                  kdu_dims full_source_dims,
                                  kdu_dims full_target_dims,
                                  kdu_coords source_sampling,
                                  kdu_coords source_denominator)
{
  if (anchor_scale < min_scale)
    anchor_scale = min_scale;
  if (anchor_scale > max_scale)
    anchor_scale = max_scale;
  
  // First determine the dimensions against which source measurements will
  // be made by `full_source_dims', `source_sampling' and `source_denominator'.
  // We will call this the "source canvas".
  kdu_dims source_canvas_dims = codestream->canvas_dims;
  if ((colour_init_src < 0) || (single_component >= 0))
    { // In this case source canvas is that of the active image component.
      kdu_coords min=source_canvas_dims.pos;
      kdu_coords lim=min+source_canvas_dims.size;
      min.x = ceil_ratio(min.x,active_subsampling[0].x);
      min.y = ceil_ratio(min.y,active_subsampling[0].y);
      lim.x = ceil_ratio(lim.x,active_subsampling[0].x);
      lim.y = ceil_ratio(lim.y,active_subsampling[0].y);
      source_canvas_dims.pos = min;
      source_canvas_dims.size = lim-min;
    }
  
  // Next fill in any missing values for `full_source_dims' or
  // `full_target_dims'
  bool synthesized_full_source_dims = false;
  if (!full_source_dims)
    {
      full_source_dims.pos = kdu_coords(0,0);
      full_source_dims.size.x =
        long_ceil_ratio(((kdu_long) source_canvas_dims.size.x) *
                        source_sampling.x,source_denominator.x);
      full_source_dims.size.y =
        long_ceil_ratio(((kdu_long) source_canvas_dims.size.y) *
                        source_sampling.y,source_denominator.y);
      synthesized_full_source_dims = true;
    }
  if (!full_target_dims)
    { // The tests below look for a condition left behind by flipping that
      // was may have been applied to match `kdrc_layer::src_orientation'
      full_target_dims.size = full_source_dims.size;
      if (full_target_dims.pos.x != 0)
        full_target_dims.pos.x = 1 - full_target_dims.size.x;
      if (full_target_dims.pos.y != 0)
        full_target_dims.pos.y = 1 - full_target_dims.size.y;
    }
  
  // Now convert `full_source_dims' to dimensions on the source canvas
  if (synthesized_full_source_dims)
    full_source_dims = source_canvas_dims;
  else
    {
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x = long_ceil_ratio(((kdu_long) min.x)*source_denominator.x,
                              source_sampling.x);
      min.y = long_ceil_ratio(((kdu_long) min.y)*source_denominator.y,
                              source_sampling.y);
      lim.x = long_ceil_ratio(((kdu_long) lim.x)*source_denominator.x,
                              source_sampling.x);
      lim.y = long_ceil_ratio(((kdu_long) lim.y)*source_denominator.y,
                              source_sampling.y);
      full_source_dims.pos = min; full_source_dims.size = lim-min;
      full_source_dims.pos += source_canvas_dims.pos;
    }
  
  // Next convert `full_source_dims' to dimensions on the high resolution
  // codestream canvas
  if (source_canvas_dims != codestream->canvas_dims)
    { // Only happens in single-component mode
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x *= active_subsampling[0].x;  lim.x *= active_subsampling[0].x;
      min.y *= active_subsampling[0].y;  lim.y *= active_subsampling[0].y;
      full_source_dims.pos = min; full_source_dims.size = lim-min;
    }
  
  // Now find scaling factors relative to the high resolution codestream canvas
  double scale_x, scale_y;
  scale_x = full_target_dims.size.x / (double) full_source_dims.size.x;
  scale_y = full_target_dims.size.y / (double) full_source_dims.size.y;
  scale_x *= anchor_scale;
  scale_y *= anchor_scale;

  // At this point `scale_x' and `scale_y' hold the amount by which the
  // codestream canvas dimensions must be scaled in order to achieve the
  // `anchor_scale' factor.

  double best_fact=-1.0, best_fact_ratio=1.0;
  double fallback_fact=-1.0, fallback_fact_ratio=1.0;
  double max_fact = max_scale / anchor_scale;
  double min_fact = min_scale / anchor_scale;

  // Pass through all the possible discard levels
  int d; // Simulated number of discard levels
  for (d=0; d <= max_discard_levels; d++)
    { 
      double active_x = scale_x*active_subsampling[d].x;
      double active_y = scale_y*active_subsampling[d].y;
     
      int i;
      double factors[4];
      factors[0] = ceil(active_x) / active_x;
      factors[1] = floor(active_x) / active_x;
      factors[2] = ceil(active_y) / active_y;
      factors[3] = floor(active_y) / active_y;
      for (i=0; i < 4; i++)
        {
          double fact = factors[i];
          if (fact == 0.0)
            continue;
          double fact_ratio = (fact < 1.0)?(1.0/fact):fact;
          if ((fact >= min_fact) && (fact <= max_fact))
            {
              if ((best_fact <= 0.0) || (fact_ratio < best_fact_ratio))
                { best_fact = fact; best_fact_ratio = fact_ratio; }
            }
          else if ((fallback_fact <= 0.0) ||
                   (fact_ratio < fallback_fact_ratio))
            { fallback_fact = fact; fallback_fact_ratio = fact_ratio; }
        }
      if (((active_x*min_fact) > 1.0) && ((active_y*min_fact) > 1.0))
        break; // No point in continuing with more discard levels
      if ((d == max_discard_levels) &&
          ((active_x < 1.0) || (active_y < 1.0)) && !avoid_subsampling)
        { // Need to do resolution reduction anyway; might as well hit the
          // anchor scale exactly, unless it is way too small
          best_fact = 1.0;
        }
    }
  if (best_fact < 0.0)
    best_fact = (avoid_subsampling)?fallback_fact:1.0;
  assert(best_fact > 0.0);

  return (float)(best_fact * anchor_scale);
}

/*****************************************************************************/
/*                 kdrc_stream::get_component_scale_factors                  */
/*****************************************************************************/

void
  kdrc_stream::get_component_scale_factors(kdu_dims full_source_dims,
                                           kdu_dims full_target_dims,
                                           kdu_coords source_sampling,
                                           kdu_coords source_denominator,
                                           double &scale_x, double &scale_y)
{
  // First determine the dimensions against which source measurements will
  // be made by `full_source_dims', `source_sampling' and `source_denominator'.
  // We will call this the "source canvas".
  kdu_dims source_canvas_dims=codestream->canvas_dims;
  if ((colour_init_src < 0) || (single_component >= 0))
    { // In this case the source canvas is that of the active image component.
      kdu_coords min=source_canvas_dims.pos;
      kdu_coords lim=min+source_canvas_dims.size;
      min.x = ceil_ratio(min.x,active_subsampling[0].x);
      min.y = ceil_ratio(min.y,active_subsampling[0].y);
      lim.x = ceil_ratio(lim.x,active_subsampling[0].x);
      lim.y = ceil_ratio(lim.y,active_subsampling[0].y);
      source_canvas_dims.pos = min;
      source_canvas_dims.size = lim-min;
    }

  // Next fill in any missing values for `full_source_dims' or
  // `full_target_dims'
  bool synthesized_full_source_dims = false;
  if (!full_source_dims)
    {
      full_source_dims.pos = kdu_coords(0,0);
      full_source_dims.size.x =
        long_ceil_ratio(((kdu_long) source_canvas_dims.size.x) *
                        source_sampling.x,source_denominator.x);
      full_source_dims.size.y =
        long_ceil_ratio(((kdu_long) source_canvas_dims.size.y) *
                        source_sampling.y,source_denominator.y);
      synthesized_full_source_dims = true;
    }
  if (!full_target_dims)
    { // The tests below look for a condition left behind by flipping that
      // was may have been applied to match `kdrc_layer::src_orientation'
      full_target_dims.size = full_source_dims.size;
      if (full_target_dims.pos.x != 0)
        full_target_dims.pos.x = 1 - full_target_dims.size.x;
      if (full_target_dims.pos.y != 0)
        full_target_dims.pos.y = 1 - full_target_dims.size.y;
    }
  
  // Now convert `full_source_dims' to dimensions on the source canvas
  if (synthesized_full_source_dims)
    full_source_dims = source_canvas_dims;
  else
    {
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x = long_ceil_ratio(((kdu_long) min.x)*source_denominator.x,
                              source_sampling.x);
      min.y = long_ceil_ratio(((kdu_long) min.y)*source_denominator.y,
                              source_sampling.y);
      lim.x = long_ceil_ratio(((kdu_long) lim.x)*source_denominator.x,
                              source_sampling.x);
      lim.y = long_ceil_ratio(((kdu_long) lim.y)*source_denominator.y,
                              source_sampling.y);
      full_source_dims.pos = min; full_source_dims.size = lim-min;
      full_source_dims.pos += source_canvas_dims.pos;
    }
  
  // Next convert `full_source_dims' to dimensions on the high resolution
  // codestream canvas
  if (source_canvas_dims != codestream->canvas_dims)
    {
      kdu_coords min = full_source_dims.pos;
      kdu_coords lim = min + full_source_dims.size;
      min.x *= active_subsampling[0].x;  lim.x *= active_subsampling[0].x;
      min.y *= active_subsampling[0].y;  lim.y *= active_subsampling[0].y;
      full_source_dims.pos = min; full_source_dims.size = lim-min;
    }
  
  // Now find scaling factors relative to the high resolution codestream canvas
  scale_x = full_target_dims.size.x / (double) full_source_dims.size.x;
  scale_y = full_target_dims.size.y / (double) full_source_dims.size.y;

  // Finally make scaling factors relative to the active image component
  scale_x *= active_subsampling[0].x;
  scale_y *= active_subsampling[0].y;
}

/*****************************************************************************/
/*                 kdrc_stream::find_min_max_jpip_woi_scales                 */
/*****************************************************************************/

bool
  kdrc_stream::find_min_max_jpip_woi_scales(double min_scale[],
                                            double max_scale[])
{
  if (!have_valid_scale)
    return false;

  kdu_coords source_sampling = saved_source_sampling;
  kdu_coords source_denominator = saved_source_denominator;
  kdu_dims full_source_dims = saved_full_source_dims;
  kdu_dims full_target_dims = saved_full_target_dims;
  
  // Find a reasonable tolerance threshold so that dimension rounding will
  // not hurt us.
  double cs_x = codestream->canvas_dims.size.x; // Evaluate the size of the
  double cs_y = codestream->canvas_dims.size.y; // codestream computed by JPIP
                                                // formulae at current scale
  cs_x *= ((double) source_sampling.x) / source_denominator.x;
  cs_y *= ((double) source_sampling.y) / source_denominator.y;
  if ((!full_source_dims.is_empty()) && (!full_target_dims.is_empty()))
    {
      cs_x *= ((double) full_target_dims.size.x) / full_source_dims.size.x;
      cs_y *= ((double) full_target_dims.size.y) / full_source_dims.size.y;
    }
  if ((colour_init_src < 0) || (single_component >= 0))
    { // In this case imagery is scaled based on active component dimensions,
      // so corresponding hi-res codestream canvas dimensions may be larger
      cs_x *= active_subsampling[0].x;
      cs_y *= active_subsampling[0].y;
    }
  cs_x *= notional_params.scale;
  cs_y *= notional_params.scale;
  double tol = 2.0 / ((cs_x < cs_y)?cs_x:cs_y);
  if (tol > 0.5)
    tol = 0.5; // Just to make sure nothing really weird happens
  
  // Find scaling factors which would match the current number of discard
  // levels exactly (as far as a JPIP server is concerned), if applied to
  // the total composition dimensions.
  kdu_coords ref_subs = active_subsampling[0];
  kdu_coords active_subs = active_subsampling[notional_params.discard_levels];
  if (transpose)
    {
      active_subs.transpose();
      ref_subs.transpose();
    }
  double active_scale_x = ((double) ref_subs.x) *
    (((double) notional_params.expansion_denominator.x) /
     ((double) notional_params.expansion_numerator.x));
  double active_scale_y = ((double) ref_subs.y) *
    (((double) notional_params.expansion_denominator.y) /
     ((double) notional_params.expansion_numerator.y));
  double active_area = active_scale_x * active_scale_y;
  
  // Find a single minimum scaling factor for each rounding case
  if (notional_params.discard_levels < max_discard_levels)
    {
      kdu_coords low_subs =
        active_subsampling[notional_params.discard_levels+1];
      if (transpose)
        low_subs.transpose();
      double low_scale_x = active_scale_x *
        ((double) active_subs.x) / ((double) low_subs.x);
      double low_scale_y = active_scale_y *
        ((double) active_subs.y) / ((double) low_subs.y);
      double low_area = low_scale_x * low_scale_y;
      double min_area = (active_area + low_area) * 0.5 * (1.0+tol);
      double tmp_scale = sqrt(min_area);
      if (tmp_scale > min_scale[0])
        min_scale[0] = tmp_scale;
      tmp_scale = (low_scale_x < low_scale_y)?low_scale_x:low_scale_y;
      tmp_scale *= (1.0+tol);
      if (tmp_scale > min_scale[1])
        min_scale[1] = tmp_scale;
    }
  else
    { // Don't have information about a larger number of discard levels, but
      // the server might discover one for some image components.  Assume worst
      // case in which only one of the dimensions is halved if we go to more
      // discard levels -- can happen in Part-2.
      double min_area = active_area * 0.75 * (1.0+tol);
      double tmp_scale = sqrt(min_area);
      if (tmp_scale > min_scale[0])
        min_scale[0] = tmp_scale;
      double min1 = active_scale_x*0.5;
      min1 = (min1 < active_scale_y)?min1:active_scale_y;
      double min2 = active_scale_y*0.5;
      min2 = (min2 < active_scale_x)?min2:active_scale_x;
      tmp_scale = (min1 > min2)?min1:min2;
      tmp_scale *= (1.0+tol);
      if (tmp_scale > min_scale[1])
        min_scale[1] = tmp_scale;
    }
     
  // Find a single maximum scale for each rounding case
  if (notional_params.discard_levels > 0)
    { 
      kdu_coords high_subs =
        active_subsampling[notional_params.discard_levels-1];
      if (transpose)
        high_subs.transpose();
      double high_scale_x = active_scale_x *
        ((double) active_subs.x) / ((double) high_subs.x);
      double high_scale_y = active_scale_y *
        ((double) active_subs.y) / ((double) high_subs.y);
      double high_area = high_scale_x * high_scale_y;
      double max_area = (active_area + high_area) * 0.5 * (1.0-tol);
      double tmp_scale = sqrt(max_area);
      if (tmp_scale < max_scale[0])
        max_scale[0] = tmp_scale;
      tmp_scale = (high_scale_x < high_scale_y)?high_scale_x:high_scale_y;
      tmp_scale *= (1.0-tol);
      if (tmp_scale < max_scale[1])
        max_scale[1] = tmp_scale;
    }

  return true;
}

/*****************************************************************************/
/*                   kdrc_stream::find_full_notional_region                  */
/*****************************************************************************/

kdu_dims
  kdrc_stream::find_full_notional_region(bool apply_cropping) const
{
  if (!have_valid_scale)
    return kdu_dims();
  const kdrc_stream_scaling_params &params = notional_params;

  kdu_dims result;
  if (apply_cropping)
    result = params.full_region;
  else
    { 
      kdu_coords subsampling = active_subsampling[params.discard_levels];
      result = codestream->canvas_dims;
      if (transpose)
        { // Convert to orientation used by expansion factors
          subsampling.transpose();
          result.transpose();
        }
      result =
        kdu_region_decompressor::find_render_dims(result,subsampling,
                                                params.expansion_numerator,
                                                params.expansion_denominator);
      if (transpose)
        result.transpose(); // Convert back to original geometric alignment
      result.to_apparent(transpose,vflip,hflip); // Apply geometric changes
    }

  result.pos -= params.compositing_offset;
  return result;
}

/*****************************************************************************/
/*                  kdrc_stream::find_full_rendering_region                  */
/*****************************************************************************/

kdu_dims
  kdrc_stream::find_full_rendering_region(bool apply_cropping) const
{
  if (!have_valid_scale)
    return kdu_dims();
  const kdrc_stream_scaling_params &params = rendering_params;
  
  kdu_dims result;
  if (apply_cropping)
    result = params.full_region;
  else
    { 
      kdu_coords subsampling = active_subsampling[params.discard_levels];
      result = codestream->canvas_dims;
      if (transpose)
        { // Convert to orientation used by expansion factors
          subsampling.transpose();
          result.transpose();
        }
      result =
        kdu_region_decompressor::find_render_dims(result,subsampling,
                                                params.expansion_numerator,
                                                params.expansion_denominator);
      if (transpose)
        result.transpose(); // Convert back to original geometric alignment
      result.to_apparent(transpose,vflip,hflip); // Apply geometric changes
    }
  
  result.pos -= params.compositing_offset;
  return result;
}

/*****************************************************************************/
/*                       kdrc_stream::get_packet_stats                       */
/*****************************************************************************/

void
  kdrc_stream::get_packet_stats(kdu_dims region, int max_region_layers,
                                kdu_long &precinct_samples,
                                kdu_long &packet_samples,
                                kdu_long &max_packet_samples)
{
  if (!(have_valid_scale && had_ifc && !region.is_empty()))
    return;

  const kdrc_stream_scaling_params &params = notional_params;

  kdrc_stream *scan;
  for (scan=codestream->head; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();

  region.pos += params.compositing_offset; // Adjust to rendering grid
  region &= params.full_region;
  
  // Map `region' to the reference component's coordinate system
  kdu_long num, den;
  kdu_coords min = region.pos;
  kdu_coords lim = min + region.size;

  num = params.expansion_denominator.x;
  den = params.expansion_numerator.x;
  min.x = long_ceil_ratio(num*min.x,den);
  lim.x = long_floor_ratio(num*lim.x,den);
  if (min.x >= lim.x)
    {
      min.x = long_floor_ratio(num*min.x,den);
      lim.x = long_ceil_ratio(num*lim.x,den);
    }

  num = params.expansion_denominator.y;
  den = params.expansion_numerator.y;
  min.y = long_ceil_ratio(num*min.y,den);
  lim.y = long_floor_ratio(num*lim.y,den);
  if (min.y >= lim.y)
    {
      min.y = long_floor_ratio(num*min.y,den);
      lim.y = long_ceil_ratio(num*lim.y,den);
    }

  kdu_dims ref_region;
  ref_region.pos = min;
  ref_region.size = lim - min;

  // Convert to a region on the high resolution codestream canvas
  int ref_idx = (single_component >= 0)?single_component:reference_component;
  kdu_dims canvas_region;
  codestream->ifc.change_appearance(transpose,vflip,hflip); // Just to be sure
  codestream->ifc.apply_input_restrictions(0,0,params.discard_levels,0,NULL,
                                           component_access_mode);
  codestream->ifc.map_region(ref_idx,ref_region,canvas_region,true);
  
  // Restrict to the region of interest, as well as the components of interest
  if (single_component < 0)
    codestream->ifc.apply_input_restrictions(mapping.num_channels,
                                             mapping.source_components,
                                             params.discard_levels,
                                             max_region_layers,
                                             &canvas_region,
                                             KDU_WANT_OUTPUT_COMPONENTS);
  else
    codestream->ifc.apply_input_restrictions(single_component,1,
                                             params.discard_levels,
                                             max_region_layers,
                                             &canvas_region,
                                             component_access_mode);

  // Scan through all the relevant precincts
  kdu_dims valid_tiles;  codestream->ifc.get_valid_tiles(valid_tiles);
  kdu_coords t_idx;
  for (t_idx.y=0; t_idx.y < valid_tiles.size.y; t_idx.y++)
    for (t_idx.x=0; t_idx.x < valid_tiles.size.x; t_idx.x++)
      {
        kdu_tile tile = codestream->ifc.open_tile(t_idx+valid_tiles.pos);
        int tile_layers = tile.get_num_layers();
        int c, num_tile_comps=tile.get_num_components();
        for (c=0; c < num_tile_comps; c++)
          {
            kdu_tile_comp tc = tile.access_component(c);
            if (!tc)
              continue;
            int r, num_resolutions=tc.get_num_resolutions();
            for (r=0; r < num_resolutions; r++)
              {
                kdu_resolution res = tc.access_resolution(r);
                kdu_dims valid_precs; res.get_valid_precincts(valid_precs);
                kdu_coords p_idx, abs_p_idx;
                kdu_long p_samples;
                int p_packets;
                for (p_idx.y=0; p_idx.y < valid_precs.size.y; p_idx.y++)
                  for (p_idx.x=0; p_idx.x < valid_precs.size.x; p_idx.x++)
                    {
                      abs_p_idx = p_idx + valid_precs.pos;
                      p_samples = res.get_precinct_samples(abs_p_idx);
                      p_packets = res.get_precinct_packets(abs_p_idx);
                      assert(p_packets <= tile_layers);
                      precinct_samples += p_samples;
                      packet_samples += (p_samples * p_packets);
                      max_packet_samples += (p_samples * tile_layers);
                    }
              }
          }
        tile.close();
      }
}

/*****************************************************************************/
/*                      kdrc_stream::set_buffer_surface                      */
/*****************************************************************************/

void
  kdrc_stream::set_buffer_surface(kdu_compositor_buf *buffer,
                                  kdu_dims buffer_region,
                                  bool start_from_scratch)
{
  assert(have_valid_scale);
  
  const kdrc_stream_scaling_params &params = rendering_params;
  
  this->buffer = buffer;
  buffer_region.pos += params.compositing_offset; // Adjust to rendering grid
  this->buffer_origin = buffer_region.pos;
  buffer_region &= params.full_region;
  bool no_change = (buffer_region == this->active_region);
  this->active_region = buffer_region;

  if (start_from_scratch)
    {
      stop_processing();
      valid_region.pos = active_region.pos;
      valid_region.size = kdu_coords(0,0);
      region_in_process = incomplete_region =
        partially_complete_region = valid_region;
      is_complete = false;
      priority = KDRC_PRIORITY_MAX;
      return;
    }

  if (no_change)
    return;

  // Intersect with current region in process
  if (processing)
    {
      region_in_process &= active_region;
      incomplete_region &= active_region;
      partially_complete_region &= active_region;
      if (!incomplete_region)
        stop_processing();
    }

  // Intersect with valid region
  valid_region &= active_region;
  
  update_completion_status();
}

/*****************************************************************************/
/*                          kdrc_stream::map_region                          */
/*****************************************************************************/

kdu_dims
  kdrc_stream::map_region(kdu_dims region, bool require_intersection)
{
  if (!have_valid_scale)
    return kdu_dims();
  
  const kdrc_stream_scaling_params &params = notional_params;
  
  region.pos += params.compositing_offset; // Adjust to rendering grid
  if (require_intersection)
    {
      region &= params.full_region;
      if (!region)
        return kdu_dims();
    }
  
  kdu_coords subs = active_subsampling[params.discard_levels];
  if (transpose)
    subs.transpose();
  kdu_coords tl_point=region.pos, br_point=tl_point+region.size-kdu_coords(1,1);
  tl_point =
    kdu_region_decompressor::find_codestream_point(tl_point,subs,
                                                 params.expansion_numerator,
                                                 params.expansion_denominator,
                                                 true);
  br_point =
    kdu_region_decompressor::find_codestream_point(br_point,subs,
                                                 params.expansion_numerator,
                                                 params.expansion_denominator,
                                                 true);
  region.pos = tl_point;
  region.size = br_point - tl_point + kdu_coords(1,1);
  region.from_apparent(transpose,vflip,hflip);
  region.pos -= codestream->canvas_dims.pos;
  return region;
}

/*****************************************************************************/
/*                      kdrc_stream::inverse_map_region                      */
/*****************************************************************************/

kdu_dims
  kdrc_stream::inverse_map_region(kdu_dims region)
{
  if (!have_valid_scale)
    return kdu_dims();
  
  const kdrc_stream_scaling_params &params = notional_params;

  // Find region on the codestream canvas coordinate system
  if (region.is_empty())
    region = codestream->canvas_dims;
  else
    region.pos += codestream->canvas_dims.pos;
  
  // Convert region to rendering grid
  kdu_coords subs = active_subsampling[params.discard_levels];
  if (transpose)
    subs.transpose();
  region.to_apparent(transpose,vflip,hflip);
  region =
    kdu_region_decompressor::find_render_cover_dims(region,subs,
                                                  params.expansion_numerator,
                                                  params.expansion_denominator,
                                                  true);
  
  // Subtract composing offset and convert back to region
  region.pos -= params.compositing_offset;
  return region;
}

/*****************************************************************************/
/*                           kdrc_stream::process                            */
/*****************************************************************************/

bool
  kdrc_stream::process(int suggested_increment, kdu_dims &new_region,
                       int &invalid_scale_code, bool intensity_true_zero,
                       bool intensity_true_max)
{
  invalid_scale_code = 0;
  assert(buffer != NULL);
  int row_gap;
  bool processing_floats = false;
  kdu_uint32 *buf32=NULL;
  float *buf_float=NULL;
  if ((buf32 = buffer->get_buf(row_gap,false)) == NULL)
    {
      processing_floats = true;
      buf_float = buffer->get_float_buf(row_gap,false);
    }
  
  new_region.size = kdu_coords(0,0);
  if (active_region.is_empty())
    { 
      update_completion_status();
      return true;
    }
  
  kdrc_stream_scaling_params &params = rendering_params;
  
  if (!processing)
    { 
      // Decide on region to process
      if (valid_region.area() <= (active_region.area()>>2))
        { // Start from scratch
          valid_region.pos = active_region.pos;
          valid_region.size = kdu_coords(0,0);
          region_in_process = active_region;
          buffer->reset_rendered_rows();
        }
      else
        { /* The new region to be processed should share a boundary with
             `valid_reg' so that the two regions can be added together to
             form a new rectangular valid region.  Of the four possibilities,
             pick the one which leads to the largest region being processed. */
          kdu_coords valid_min = valid_region.pos;
          kdu_coords valid_lim = valid_min + valid_region.size;
          kdu_coords active_min = active_region.pos;
          kdu_coords active_lim = active_min + active_region.size;
          int needed_left = valid_min.x - active_min.x;
          int needed_right = active_lim.x - valid_lim.x;
          int needed_above = valid_min.y - active_min.y;
          int needed_below = active_lim.y - valid_lim.y;
          assert((needed_left >= 0) && (needed_right >= 0) &&
                 (needed_above >= 0) && (needed_below >= 0));
          kdu_dims region_left = valid_region;
          region_left.pos.x = active_min.x; region_left.size.x = needed_left;
          kdu_dims region_right = valid_region;
          region_right.pos.x = valid_lim.x; region_right.size.x = needed_right;
          kdu_dims region_above = valid_region;
          region_above.pos.y = active_min.y; region_above.size.y=needed_above;
          kdu_dims region_below = valid_region;
          region_below.pos.y = valid_lim.y; region_below.size.y = needed_below;
          region_in_process = region_left;
          if ((region_in_process.area() < region_right.area()) ||
              !region_in_process)
            region_in_process = region_right;
          if ((region_in_process.area() < region_above.area()) ||
              !region_in_process)
            region_in_process = region_above;
          if ((region_in_process.area() < region_below.area()) ||
              !region_in_process)
            region_in_process = region_below;
        }

      incomplete_region = region_in_process;
      partially_complete_region.pos = region_in_process.pos;
      partially_complete_region.size = kdu_coords(0,0);
    
      if (region_in_process.is_empty())
        { 
          update_completion_status();
          return true;
        }
    
      if (had_ifc)
        { // Start the decompressor
          assert(!codestream->in_use);
          codestream->ifc.change_appearance(transpose,vflip,hflip);
          if (!(intensity_true_zero || intensity_true_max))
            decompressor.set_white_stretch((processing_floats)?0:8);
          else
            decompressor.set_true_scaling(intensity_true_zero,
                                          intensity_true_max);
          decompressor.set_interpolation_behaviour(
                                    owner->scaling_max_interp_overshoot,
                                    owner->scaling_bilinear_interp_threshold);
          if (!decompressor.start(codestream->ifc,
                                  ((single_component<0)?(&mapping):NULL),
                                  single_component,params.discard_levels,
                                  max_display_layers,region_in_process,
                                  params.expansion_numerator,
                                  params.expansion_denominator,
                                  processing_floats,component_access_mode,
                                  !processing_floats,env,env_queue))
            throw KDU_ERROR_EXCEPTION; // `kdu_error' exception prob. caught
          else
            processing = codestream->in_use = true;
        }
      else
        { // In this case we can immediately fill `region_in_process' with
          // a mid-level constant value, making `incomplete_region' empty
          // and setting `new_region' to `region_in_process'.
          kdu_coords active_offset = active_region.pos - buffer_origin;
          if (processing_floats)
            { 
              buf_float += (active_offset.x<<2) + row_gap*active_offset.y;
              if (alpha_only && (single_component < 0))
                erase_float_mask_buffer_region(buf_float,row_gap,active_region,
                                               region_in_process,0xFF000000,
                                               0x80808080);
              else if ((single_component < 0) && (layer != NULL) &&
                       layer->have_alpha_channel &&
                       (mapping.num_colour_channels == mapping.num_channels))
                erase_float_mask_buffer_region(buf_float,row_gap,active_region,
                                               region_in_process,0x00FFFFFF,
                                               0x80808080);
              else
                erase_float_buffer_region(buf_float,row_gap,active_region,
                                          region_in_process,0xFF808080);
            }
          else
            { 
              buf32 += active_offset.x + row_gap*active_offset.y;
              if (alpha_only && (single_component < 0))
                erase_mask_buffer_region(buf32,row_gap,active_region,
                                         region_in_process,0xFF000000,
                                         0x80808080);
              else if ((single_component < 0) && (layer != NULL) &&
                       layer->have_alpha_channel &&
                       (mapping.num_colour_channels == mapping.num_channels))
                erase_mask_buffer_region(buf32,row_gap,active_region,
                                         region_in_process,0x00FFFFFF,
                                         0x80808080);
              else
                erase_buffer_region(buf32,row_gap,active_region,
                                    region_in_process,0xFF808080);
            }
          incomplete_region.size.y = 0;
          new_region = region_in_process;
          if ((valid_region.pos.y == active_region.pos.y) &&
              ((region_in_process.size.x + valid_region.size.x) >=
               (active_region.size.x)))
            { // Otherwise, one or more initial buffer rows cannot be valid yet          
              int first_new_y = new_region.pos.y - active_region.pos.y;
              int lim_y = first_new_y + region_in_process.size.y;
              buffer->note_new_rendered_rows(first_new_y,lim_y);
            }
          
          // Convert to compositing grid
          new_region.pos -= params.compositing_offset;
        }
    }

  if (processing)
    { // Process some more data
      assert(had_ifc);
      bool process_result;
      if (alpha_only && (single_component < 0))
        { // Generating a separate alpha channel here
          if (processing_floats)
            process_result =
              decompressor.process(&buf_float,false,4,buffer_origin,row_gap,
                                   suggested_increment,0,incomplete_region,
                                   new_region,true,false);
          else
            {
              kdu_byte *buf8 = ((kdu_byte *) buf32) + ((little_endian)?3:0);
              process_result =
                decompressor.process(&buf8,false,4,buffer_origin,row_gap,
                                     suggested_increment,0,incomplete_region,
                                     new_region);
            }
        }
      else
        { 
          int fill_alpha = 1;
          if ((single_component < 0) && (layer != NULL) &&
              layer->have_alpha_channel &&
              (mapping.num_colour_channels == mapping.num_channels))
            fill_alpha = 0; // There is an alpha channel, but it is not
                            // rendered from this codestream.  This means that
                            // we might run the risk of overwriting alpha
                            // in calls to `kdu_region_decompressor::process'
          int chan_offs[4] = {1,2,3,0}; // Alpha, Red, Green, Blue
          if (processing_floats)
            { 
              if (colour_order != KDU_ARGB_ORDER)
                { chan_offs[0]=3; chan_offs[1]=2; chan_offs[2]=1; }
              process_result =
                decompressor.process(buf_float,chan_offs,4,buffer_origin,
                                     row_gap,suggested_increment,0,
                                     incomplete_region,new_region,true,false,3,
                                     fill_alpha,3);
            }
          else
            { // Processing 32-bit pixels
              kdu_byte *base8=(kdu_byte *) buf32;
              if (little_endian)
                { 
                  chan_offs[3] = 3; // Place Alpha (if any) in last byte
                  if (colour_order == KDU_ARGB_ORDER)
                    { chan_offs[0]=2; chan_offs[1]=1; chan_offs[2]=0; }
                  else
                    { chan_offs[0]=0; chan_offs[1]=1; chan_offs[2]=2; }
                }
              else if (colour_order != KDU_ARGB_ORDER)
                { chan_offs[0]=3; chan_offs[1]=2; chan_offs[2]=1; }
              process_result =
                decompressor.process(base8,chan_offs,4,buffer_origin,
                                     row_gap,suggested_increment,0,
                                     incomplete_region,new_region,8,true,3,
                                     fill_alpha,3);
            }
        }
      if ((!process_result) || !incomplete_region)
        {
          processing = codestream->in_use = false;
          kdu_exception failure_exception;
          if (!decompressor.finish(&failure_exception))
            { // Code-stream failure; must destroy
              kdu_rethrow(failure_exception);
            }
          else 
            { 
              max_discard_levels = codestream->ifc.get_min_dwt_levels();
              if (max_nonempty_levels < max_discard_levels)
                max_discard_levels = max_nonempty_levels;
              if (max_discard_levels < params.discard_levels)
                {
                params.discard_levels = max_discard_levels;
                have_valid_scale = false;
                invalid_scale_code |= KDU_COMPOSITOR_TRY_SCALE_AGAIN;
                invalidate_surface();
                return false;
                }
              else if ((hflip || vflip) &&
                       !(can_flip = codestream->ifc.can_flip(false)))
                {
                hflip = vflip = have_valid_scale = false;
                invalid_scale_code |= KDU_COMPOSITOR_CANNOT_FLIP;
                invalidate_surface();
                return false;
                }
            }
        }
      else
        { // Adjust `partially_complete_region' as well as the range of
          // fully rendered buffer rows
          kdu_coords tst = (new_region.pos + new_region.size) -
            (partially_complete_region.pos + partially_complete_region.size);
          if (tst.y > 0)
            partially_complete_region.size.y += tst.y;
          if (tst.x > 0)
            partially_complete_region.size.x += tst.x;
          if ((valid_region.pos.y == active_region.pos.y) &&
              ((region_in_process.size.x + valid_region.size.x) >=
               (active_region.size.x)))
            { // Otherwise, one or more initial buffer rows cannot be valid yet          
              int first_new_y = new_region.pos.y - active_region.pos.y;
              int lim_y = incomplete_region.pos.y - active_region.pos.y;
              buffer->note_new_rendered_rows(first_new_y,lim_y);
            }
        }
      
      // Convert to compositing grid
      new_region.pos -= params.compositing_offset;
    }
  if (!processing)
    { // Combine newly completed region with the existing valid region
      valid_region &= active_region; // Just to be sure
      region_in_process &= active_region; // Just to be sure
      if (valid_region.is_empty())
        valid_region = region_in_process;
      else
        { 
          kdu_coords a_min = valid_region.pos;
          kdu_coords a_lim = a_min + valid_region.size;
          kdu_coords b_min = region_in_process.pos;
          kdu_coords b_lim = b_min + region_in_process.size;
          if ((a_min.x == b_min.x) && (a_lim.x == b_lim.x))
            { // Regions have same horizontal edge profile
              if ((a_min.y <= b_lim.y) && (a_lim.y >= b_min.y))
                { // Union of regions is another rectangle
                  a_min.y = (a_min.y < b_min.y)?a_min.y:b_min.y;
                  a_lim.y = (a_lim.y > b_lim.y)?a_lim.y:b_lim.y;
                }
            }
          else if ((a_min.y == b_min.y) && (a_lim.y == b_lim.y))
            { // Regions have same vertical edge profile
              if ((a_min.x <= b_lim.x) && (a_lim.x >= b_min.x))
                { // Union of regions is another rectangle
                  a_min.x = (a_min.x < b_min.x)?a_min.x:b_min.x;
                  a_lim.x = (a_lim.x > b_lim.x)?a_lim.x:b_lim.x;
                }
            }
          valid_region.pos = a_min;
          valid_region.size = a_lim - a_min;
        }
    }

  update_completion_status();
  return true;
}

/*****************************************************************************/
/*                    kdrc_stream::find_non_pending_rects                    */
/*****************************************************************************/

int
  kdrc_stream::find_non_pending_rects(kdu_dims region, kdu_dims rects[])
{
  assert(have_valid_scale);
  
  const kdrc_stream_scaling_params &params = rendering_params;
  
  kdu_dims active = active_region; active.pos -= params.compositing_offset;
  kdu_dims valid = valid_region; valid.pos -= params.compositing_offset;
  valid &= region;
  kdu_dims intersection = region & active;
  if (intersection.is_empty())
    {
      rects[0] = region;
      return 1;
    }
  
  int num_rects = 0;
  int left = intersection.pos.x - region.pos.x;
  int right = (region.pos.x+region.size.x)
            - (intersection.pos.x+intersection.size.x);
  int top = intersection.pos.y - region.pos.y;
  int bottom = (region.pos.y+region.size.y)
             - (intersection.pos.y+intersection.size.y);
  assert((left >= 0) && (right >= 0) && (top >= 0) && (bottom >= 0));
  kdu_dims new_region;
  if (top > 0)
    {
      new_region = region;
      new_region.size.y = top;
      rects[num_rects++] = new_region;
    }
  if (bottom > 0)
    {
      new_region = region;
      new_region.pos.y = intersection.pos.y+intersection.size.y;
      new_region.size.y = bottom;
      rects[num_rects++] = new_region;
    }
  if (left > 0)
    {
      new_region = intersection;
      new_region.pos.x = region.pos.x;
      new_region.size.x = left;
      rects[num_rects++] = new_region;
    }
  if (right > 0)
    {
      new_region = intersection;
      new_region.pos.x = intersection.pos.x+intersection.size.x;
      new_region.size.x = right;
      rects[num_rects++] = new_region;
    }
  if (!valid.is_empty())
    { // Valid region will not be refreshed
      rects[num_rects++] = valid;
    }
  if (processing)
    {
      kdu_dims partial = partially_complete_region;
      partial.pos -= params.compositing_offset;
      partial &= region;
      if (!partial.is_empty())
        rects[num_rects++] = partial;
    }  
  return num_rects;
}


/* ========================================================================= */
/*                                kdrc_layer                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                          kdrc_layer::kdrc_layer                           */
/*****************************************************************************/

kdrc_layer::kdrc_layer(kdu_region_compositor *owner)
{
  this->owner = owner;
  this->num_streams = 0;
  for (int s=0; s < 2; s++)
    {
      streams[s] = NULL;
      stream_sampling[s] = stream_denominator[s] = kdu_coords(1,1);
    }
  mj2_track = NULL;
  mj2_frame_idx = mj2_field_handling = 0;
  mj2_pending_frame_change = false;

  have_alpha_channel = alpha_is_premultiplied = have_overlay_info = false;
  have_valid_scale = false;
  buffer = compositing_buffer = NULL;
  waiting_for_set_buffer_surface = true;

  overlay = NULL;
  overlay_buffer = NULL;
  max_overlay_border = 0;
  overlay_buffer_size = kdu_coords(0,0);

  ilayer_ref = owner->assign_new_ilayer_ref();
  colour_init_src = -1;
  direct_codestream_idx = -1;
  direct_component_idx = -1;
  direct_access_mode = KDU_WANT_OUTPUT_COMPONENTS; // Any valid value for now
  buffer_is_composition_surface = false;
  
  next = prev = NULL;
}

/*****************************************************************************/
/*                         kdrc_layer::~kdrc_layer                           */
/*****************************************************************************/

kdrc_layer::~kdrc_layer()
{
  if (overlay != NULL)
    overlay->deactivate();
  for (int s=0; s < num_streams; s++)
    if (streams[s] != NULL)
      {
        streams[s]->layer = NULL;
        delete streams[s]; // Automatically unlinks it from owner streams list
        streams[s] = NULL;
      }
  if (buffer != NULL)
    owner->internal_delete_buffer(buffer);
  if (overlay_buffer != NULL)
    owner->internal_delete_buffer(overlay_buffer);
}

/*****************************************************************************/
/*                             kdrc_layer::init (JPX)                        */
/*****************************************************************************/

void
  kdrc_layer::init(jpx_layer_source layer, kdu_dims full_source_dims,
                   kdu_dims full_target_dims,
                   jpx_composited_orientation orientation)
{
  this->jpx_layer = layer;
  this->mj2_track = NULL;
  this->mj2_frame_idx = 0;
  this->mj2_field_handling = 0;
  this->mj2_pending_frame_change = false;
  this->full_source_dims = full_source_dims;
  this->full_target_dims = full_target_dims;
  this->src_orientation = orientation;
  this->have_valid_scale = false;
  
  this->colour_init_src = layer.get_layer_id();
  this->direct_codestream_idx = -1;
  this->direct_component_idx = -1;
  
  jp2_channels channels = layer.access_channels();
  int c, comp_idx, lut_idx, stream_idx, format;
  channels.get_colour_mapping(0,comp_idx,lut_idx,stream_idx,format);
  if (streams[0] == NULL)
    { 
      streams[0] =
        owner->add_active_stream(stream_idx,colour_init_src,false,false);
      if (streams[0] == NULL)
        { KDU_ERROR(e,4); e <<
          KDU_TXT("Unable to create compositing layer "
                  "(index, starting from 0, equals ")
          << colour_init_src <<
          KDU_TXT("), since its primary codestream cannot be opened.");
        }
      streams[0]->set_mode(-1,direct_access_mode); // No single component mode
      streams[0]->layer = this;
    }
  int main_stream_idx = stream_idx;
  int aux_stream_idx = -1;

  // Now go looking for alpha information
  have_alpha_channel = false;
  alpha_is_premultiplied = false;
  if (streams[0]->get_num_channels() > streams[0]->get_num_colours())
    {
      have_alpha_channel = true;
      alpha_is_premultiplied = streams[0]->is_alpha_premultiplied();
    }
  else
    { // See if alpha is in a different code-stream
      int aux_comp_idx=-1, aux_lut_idx=-1;
      for (c=0; c < channels.get_num_colours(); c++)
        {
          if ((!channels.get_opacity_mapping(c,comp_idx,lut_idx,
                                             stream_idx,format)) ||
              (stream_idx == main_stream_idx))
            { // Unable to find or use alpha information
              aux_stream_idx = -1;
              break;
            }
          else if (c == 0)
            {
              aux_stream_idx = stream_idx;
              aux_comp_idx = comp_idx;
              aux_lut_idx = lut_idx;
            }
          else if ((stream_idx != aux_stream_idx) ||
                   (comp_idx != aux_comp_idx) || (lut_idx != aux_lut_idx))
            { KDU_WARNING(w,0); w <<
                KDU_TXT("Unable to render compositing layer "
                "(index, starting from 0, equals ")
                << colour_init_src <<
                KDU_TXT(") with alpha blending, since there are multiple "
                        "distinct alpha channels for a single set of colour "
                        "channels.");
             aux_stream_idx = -1;
             break;
            }
        }
      if ((aux_stream_idx >= 0) && (num_streams != 1))
        { // If `num_streams' is 1 we have already tried and failed to add the
          // auxiliary stream.
          if (streams[1] == NULL)
            {
              streams[1] =
                owner->add_active_stream(aux_stream_idx,colour_init_src,
                                         false,true);
              if (streams[1] == NULL)
                {
                  aux_stream_idx = -1;
                  KDU_WARNING(w,1); w <<
                  KDU_TXT("Unable to render compositing layer "
                          "(index, starting from 0, equals ")
                  << colour_init_src <<
                  KDU_TXT(") with alpha blending, since the codestream "
                          "containing the alpha data cannot be opened.");
                }
              else
                {
                  streams[1]->set_mode(-1,direct_access_mode);
                  streams[1]->layer = this;
                }
            }
          if (streams[1] != NULL)
            {
              have_alpha_channel = true;
              alpha_is_premultiplied = streams[1]->is_alpha_premultiplied();
            }
        }
    }
  num_streams = (streams[1]==NULL)?1:2;

  // Now find the sampling factors
  kdu_coords align, sampling, denominator;
  c = 0;
  while ((stream_idx=layer.get_codestream_registration(c++,align,sampling,
                                                       denominator)) >= 0)
    if (stream_idx == main_stream_idx)
      {
        stream_sampling[0] = sampling;
        stream_denominator[0] = denominator;
      }
    else if (stream_idx == aux_stream_idx)
      {
        stream_sampling[1] = sampling;
        stream_denominator[1] = denominator;
      }
}

/*****************************************************************************/
/*                            kdrc_layer::init (MJ2)                         */
/*****************************************************************************/

void
  kdrc_layer::init(mj2_video_source *track, int frame_idx,
                   int field_handling, kdu_dims full_source_dims,
                   kdu_dims full_target_dims,
                   jpx_composited_orientation orientation)
{
  this->jpx_layer = jpx_layer_source();
  this->mj2_track = track;
  this->mj2_frame_idx = frame_idx;
  this->mj2_field_handling = field_handling;
  this->mj2_pending_frame_change = false;
  this->full_source_dims = full_source_dims;
  this->full_target_dims = full_target_dims;
  this->src_orientation = orientation;
  this->have_valid_scale = false;

  this->colour_init_src = (int)(track->get_track_idx() - 1);
  this->direct_codestream_idx = -1;
  this->direct_component_idx = -1;
  
  int field_idx = field_handling & 1;
  if ((frame_idx < 0) || (frame_idx >= track->get_num_frames()))
    { KDU_ERROR_DEV(e,5); e <<
        KDU_TXT("Unable to create imagery layer for MJ2 track "
                "(index starting from 1) ")
        << colour_init_src+1 <<
        KDU_TXT(": requested frame index is out of range.");
    }
  if ((field_idx == 1) && (track->get_field_order() == KDU_FIELDS_NONE))
    { KDU_ERROR_DEV(e,6); e <<
        KDU_TXT("Unable to create imagery layer for MJ2 track "
                "(index starting from 1) ")
        << colour_init_src+1 <<
        KDU_TXT(": requested field does not exist (source is "
        "progressive, not interlaced).");
    }

  track->seek_to_frame(frame_idx);
  if (!track->can_open_stream(field_idx))
    return; // Need to finish initialization in a later call to `init'
  int stream_idx = track->get_stream_idx(field_idx);
  assert(stream_idx >= 0);

  if (streams[0] == NULL)
    {
      streams[0] = owner->add_active_stream(stream_idx,colour_init_src,
                                            false,false);
      if (streams[0] == NULL)
        { KDU_ERROR(e,7); e <<
          KDU_TXT("Unable to create imagery layer for MJ2 track "
                  "(index starting from 1) ")
          << colour_init_src+1 <<
          KDU_TXT(": codestream cannot be opened.");
        }
      streams[0]->set_mode(-1,direct_access_mode); // No single component mode
      streams[0]->layer = this;
    }
  num_streams = 1;

  // Now go looking for alpha information
  have_alpha_channel = alpha_is_premultiplied = false;
  if (streams[0]->get_num_channels() > streams[0]->get_num_colours())
    {
      have_alpha_channel = true;
      alpha_is_premultiplied = streams[0]->is_alpha_premultiplied();
    }

  // Now set the sampling factors
  stream_sampling[0] = stream_denominator[0] = kdu_coords(1,1);
}

/*****************************************************************************/
/*                           kdrc_layer::init (direct)                       */
/*****************************************************************************/

void
  kdrc_layer::init(int stream_idx, int comp_idx,
                   kdu_component_access_mode access_mode,
                   kdu_dims full_source_dims, kdu_dims full_target_dims,
                   jpx_composited_orientation orientation)
{
  this->jpx_layer = jpx_layer_source();
  this->mj2_track = NULL;
  this->mj2_frame_idx = 0;
  this->mj2_field_handling = 0;
  this->mj2_pending_frame_change = false;
  this->full_source_dims = full_source_dims;
  this->full_target_dims = full_target_dims;
  this->src_orientation = orientation;
  this->have_valid_scale = false;

  this->colour_init_src = -1;
  this->direct_codestream_idx = stream_idx;
  this->direct_component_idx = comp_idx;
  this->direct_access_mode = access_mode;

  if (streams[0] == NULL)
    {
      streams[0] = owner->add_active_stream(stream_idx,-1,(comp_idx>=0),false);
      if (streams[0] == NULL)
        { KDU_ERROR(e,0x26031001); e <<
          KDU_TXT("Unable to create imagery layer for direct rendering of "
                  "image components from codestream (index starting from 0) ")
          << stream_idx <<
          KDU_TXT(": codestream cannot be opened.");
        }
      direct_component_idx = // Set up single component mode if required
        streams[0]->set_mode(direct_component_idx,direct_access_mode);
      streams[0]->layer = this;
    }
  num_streams = 1;

  // Neither alpha nor stream sampling information are available
  have_alpha_channel = alpha_is_premultiplied = false;
  stream_sampling[0] = stream_denominator[0] = kdu_coords(1,1);
}

/*****************************************************************************/
/*                         kdrc_layer::change_frame                          */
/*****************************************************************************/

bool
  kdrc_layer::change_frame(int frame_idx, bool all_or_nothing)
{
  if (mj2_track == NULL)
    return false;
  assert(streams[0] != NULL);
  if ((frame_idx == mj2_frame_idx) && !mj2_pending_frame_change)
    return true;

  if ((frame_idx < 0) || (frame_idx >= mj2_track->get_num_frames()))
    { KDU_ERROR_DEV(e,8); e <<
        KDU_TXT("Requested frame index for MJ2 track (index starting from 1) ")
        << colour_init_src+1 <<
        KDU_TXT(" is out of range.");
    }

  mj2_frame_idx = frame_idx;
  mj2_pending_frame_change = true;

  int s;
  for (s=0; s < num_streams; s++)
    if (streams[s] != NULL)
      {
        int fld_idx=s, frm_idx=mj2_frame_idx;
        if (mj2_field_handling & 1)
          fld_idx = 1-s;
        if ((fld_idx==0) && (mj2_field_handling == 3))
          frm_idx++;
        mj2_track->seek_to_frame(frm_idx);
        if (!mj2_track->can_open_stream(fld_idx))
          return false;
        if (!all_or_nothing)
          streams[s]->change_frame(frm_idx);
      }

  if (all_or_nothing)
    { // Make the frame changes here, now we know they can succeed
      for (s=0; s < 2; s++)
        if (streams[s] != NULL)
          {
            int fld_idx=s, frm_idx=mj2_frame_idx;
            if (mj2_field_handling & 1)
              fld_idx = 1-s;
            if ((fld_idx==0) && (mj2_field_handling == 3))
              frm_idx++;
            mj2_track->seek_to_frame(frm_idx);
            streams[s]->change_frame(frm_idx);
          }
    }

  mj2_pending_frame_change = false;
  return true;
}

/*****************************************************************************/
/*                           kdrc_layer::activate                            */
/*****************************************************************************/

void
  kdrc_layer::activate(kdu_dims full_source_dims, kdu_dims full_target_dims,
                       jpx_composited_orientation orientation,
                       int frame_idx, int field_handling)
{
  int s;
  
  assert(overlay == NULL);
  ilayer_ref = owner->assign_new_ilayer_ref();
  if (buffer != NULL)
    buffer->renovate();
  
  if ((mj2_track != NULL) && (streams[0] != NULL) &&
      ((num_streams < 2) || (streams[1] != NULL)) &&
      ((field_handling != mj2_field_handling) ||
       (((frame_idx != mj2_frame_idx) || mj2_pending_frame_change) &&
         !change_frame(frame_idx,true))))
    { // Delete streams and re-initialize
      for (s=0; s < num_streams; s++)
        {
          delete streams[s];
          streams[s] = NULL;
        }
      have_valid_scale = false;
      init(mj2_track,frame_idx,field_handling,full_source_dims,
           full_target_dims,orientation);
      return;
    }
  
  bool missing_streams = false;
  for (s=0; s < num_streams; s++)
    if (streams[s] == NULL)
      missing_streams = true;
    else
      {
        streams[s]->is_active = true;
        streams[s]->istream_ref = owner->assign_new_istream_ref();
      }
  if (missing_streams)
    {
      have_valid_scale = false;
      if (jpx_layer.exists())
        init(jpx_layer,full_source_dims,full_target_dims,orientation);
      else if (mj2_track != NULL)
        init(mj2_track,frame_idx,field_handling,full_source_dims,
             full_target_dims,orientation);
      else if (direct_codestream_idx >= 0)
        init(direct_codestream_idx,direct_component_idx,
             direct_access_mode,full_source_dims,full_target_dims,
             orientation);
      else
        assert(0);
      return;
    }

  if ((full_target_dims != this->full_target_dims) ||
      (full_source_dims != this->full_source_dims))
    have_valid_scale = false;
  this->full_source_dims = full_source_dims;
  this->full_target_dims = full_target_dims;
  this->src_orientation = orientation;
}

/*****************************************************************************/
/*                          kdrc_layer::deactivate                           */
/*****************************************************************************/

void
  kdrc_layer::deactivate()
{
  waiting_for_set_buffer_surface = true;
  for (int s=0; s < num_streams; s++)
    if (streams[s] != NULL)
      {
        streams[s]->is_active = false;
        if (streams[s]->codestream != NULL)
          streams[s]->codestream->move_to_tail(streams[s]);
      }
  if (overlay != NULL)
    {
      overlay->deactivate();
      overlay = NULL;
    }
  have_overlay_info = false;
  buffer_is_composition_surface = false;
  if (overlay_buffer != NULL)
    {
      owner->internal_delete_buffer(overlay_buffer);
      overlay_buffer = NULL;
    }
  max_overlay_border = 0;
}

/*****************************************************************************/
/*                          kdrc_layer::set_scale                            */
/*****************************************************************************/

bool
  kdrc_layer::set_scale(bool transpose, bool vflip, bool hflip,
                        float notional_scale, float rendering_scale,
                        int &invalid_scale_code,
                        kdu_dims composited_frame_dims)
{
  if (streams[0] == NULL)
    return false; // Not successfully initialized yet
  if (mj2_pending_frame_change)
    change_frame();
  composited_frame_dims.from_apparent(src_orientation.transpose,
                                      src_orientation.vflip,
                                      src_orientation.hflip);

  adjust_geometry_flags(transpose,vflip,hflip,src_orientation);
  bool no_change = (have_valid_scale &&
                    (this->vflip == vflip) && (this->hflip == hflip) &&
                    (this->transpose == transpose) &&
                    (this->notional_scale == notional_scale) &&
                    (this->rendering_scale == rendering_scale));
  have_valid_scale = false;
  this->vflip = vflip;
  this->hflip = hflip;
  this->transpose = transpose;
  this->notional_scale = notional_scale;
  this->rendering_scale = rendering_scale;

  for (int s=0; s < num_streams; s++)
    if (streams[s] != NULL)
      { 
        if (!streams[s]->set_scale(full_source_dims,full_target_dims,
                                   stream_sampling[s],stream_denominator[s],
                                   transpose,vflip,hflip,notional_scale,
                                   rendering_scale,invalid_scale_code,
                                   composited_frame_dims))
          return false; // Scale too small, too large, or cannot flip,
                        // depending on `invalid_scale_code'.
        kdu_dims n_region = streams[s]->find_full_notional_region(true);
        kdu_dims r_region = streams[s]->find_full_rendering_region(true);
        if (s == 0)
          { 
            notional_layer_region = n_region;
            rendering_layer_region = r_region;
          }
        else
          { 
            notional_layer_region &= n_region;
            rendering_layer_region &= r_region;
          }
      }

  have_valid_scale = true;

  compositing_buffer = NULL;
  waiting_for_set_buffer_surface = true;
  if (no_change)
    return true; // No need to invalidate the layer buffer
  
  // Invalidate the layer buffer(s)
  if (buffer != NULL)
    {
      owner->internal_delete_buffer(buffer);
      buffer = NULL;
    }
  if (overlay_buffer != NULL)
    {
      owner->internal_delete_buffer(overlay_buffer);
      overlay_buffer = NULL;
    }
  buffer_is_composition_surface = false;
  return true;
}

/*****************************************************************************/
/*                     kdrc_layer::find_supported_scales                     */
/*****************************************************************************/

void
  kdrc_layer::find_supported_scales(float &min_scale, float &max_scale)
{
  for (int s=0; s < num_streams; s++)
    if (streams[s] != NULL)
      streams[s]->find_supported_scales(min_scale,max_scale,
                                        full_source_dims,full_target_dims,
                                        stream_sampling[s],
                                        stream_denominator[s]);
}

/*****************************************************************************/
/*                      kdrc_layer::find_optimal_scale                       */
/*****************************************************************************/

float
  kdrc_layer::find_optimal_scale(float anchor_scale, float min_scale,
                                 float max_scale, bool avoid_subsampling)
{
  if (streams[0] == NULL)
    return anchor_scale;
  else
    return streams[0]->find_optimal_scale(anchor_scale,min_scale,max_scale,
                                          avoid_subsampling,
                                          full_source_dims,full_target_dims,
                                          stream_sampling[0],
                                          stream_denominator[0]);
}

/*****************************************************************************/
/*                  kdrc_layer::get_component_scale_factors                  */
/*****************************************************************************/

void
  kdrc_layer::get_component_scale_factors(kdrc_stream *stream,
                                          double &scale_x, double &scale_y)
{
  int s;
  for (s=0; s < num_streams; s++)
    if (stream == streams[s])
      {
        stream->get_component_scale_factors(full_source_dims,full_target_dims,
                                            stream_sampling[s],
                                            stream_denominator[s],
                                            scale_x,scale_y);
        break;
      }
  assert(s != 2);
}

/*****************************************************************************/
/*                      kdrc_layer::set_buffer_surface                       */
/*****************************************************************************/

void
  kdrc_layer::set_buffer_surface(kdu_dims rendering_region,
                                 kdu_dims rendering_visible_region,
                                 kdu_compositor_buf *compositing_buffer,
                                 kdrc_overlay_expression *overlay_dependencies)
{
  // Install the compositing buffer, regardless of whether we are initialized.
  // This allows us to erase the compositing buffer if we are the bottom
  // layer in a composition, whenever `do_composition' is called.
  waiting_for_set_buffer_surface = false;
  this->compositing_buffer = compositing_buffer;
  this->compositing_buffer_region = rendering_region;

  assert(streams[0] != NULL);
  
  assert(have_valid_scale);
  
  buffer_is_composition_surface = false; // App cannot assume validity of
     // any compositing buffer in any context where this function is called.

  bool read_access_required =
    (compositing_buffer != NULL) || ((overlay != NULL) && have_overlay_info);

  // First, set up the regular buffer
  kdu_uint32 local_bkgnd_erase = (have_alpha_channel)?0x00FFFFFF:0xFFFFFFFF;
  kdu_compositor_buf *old_buffer = buffer;
  kdu_dims old_region = buffer_region;
  buffer_region = rendering_visible_region & rendering_layer_region;
  kdu_coords new_size = buffer_region.size;
  bool start_from_scratch = (old_buffer == NULL) ||
    ((old_region != buffer_region) && !old_buffer->is_read_access_allowed());
  if ((buffer == NULL) || (new_size.x > buffer_size.x) ||
      (new_size.y > buffer_size.y))
    buffer = owner->internal_allocate_buffer(new_size,buffer_size,
                                             read_access_required);
  else if (!buffer->set_read_accessibility(read_access_required))
    start_from_scratch = true;
  buffer->set_geometry(rendering_scale,buffer_region,rendering_layer_region);  
  if (start_from_scratch)
    { 
      if (owner->needs_surface_init())
        initialize_buffer_surface(buffer,buffer_region,NULL,kdu_dims(),
                                  local_bkgnd_erase);
    }
  else if ((buffer != old_buffer) || (buffer_region != old_region))
    initialize_buffer_surface(buffer,buffer_region,old_buffer,old_region,
                              local_bkgnd_erase,!owner->needs_surface_init());
  if ((old_buffer != NULL) && (old_buffer != buffer))
    owner->internal_delete_buffer(old_buffer);
  // Next, see if we need to create an overlay buffer
  if ((overlay != NULL) && (!have_overlay_info) &&
      overlay->set_buffer_surface(NULL,buffer_region,false,
                                  overlay_dependencies,false))
    {
      have_overlay_info = true; // Activate overlays
      if (compositing_buffer == NULL)
        { // Must be the only layer.  It is safe here to just create a
          // separate compositing layer and donate it to our `owner', because
          // `buffer_is_composition_surface' must be false.
          assert(!read_access_required);
          assert((next == NULL) && (prev == NULL) &&
                 (rendering_region == rendering_visible_region) &&
                 (overlay_buffer == NULL));
          kdu_compositor_buf *compbuf;
          kdu_coords compbuf_size;
          compbuf=owner->internal_allocate_buffer(new_size,compbuf_size,true);
          compbuf->set_geometry(rendering_scale,buffer_region,
                                rendering_layer_region);
          owner->donate_compositing_buffer(compbuf,buffer_region,compbuf_size);
          this->compositing_buffer = compositing_buffer = compbuf;
          if (!buffer->set_read_accessibility(true))
            { // Write-only buffer became read-write.  Initialize from scratch
              if (owner->needs_surface_init())
                initialize_buffer_surface(buffer,buffer_region,
                                          NULL,kdu_dims(),0xFFFFFFFF);
              start_from_scratch = true;
            }
          if (owner->needs_surface_init())
            initialize_buffer_surface(compbuf,buffer_region,
                                      buffer,buffer_region,0xFFFFFFFF);
        }
    }

  // Set up the overlay buffer
  if ((overlay != NULL) && have_overlay_info)
    { 
      kdu_compositor_buf *old_buffer = overlay_buffer;
      if ((overlay_buffer == NULL) || (new_size.x > overlay_buffer_size.x) ||
          (new_size.y > overlay_buffer_size.y))
        overlay_buffer =
          owner->internal_allocate_buffer(new_size,overlay_buffer_size,true);
      overlay_buffer->set_geometry(rendering_scale,buffer_region,
                                   rendering_layer_region);
      overlay->set_buffer_surface(overlay_buffer,buffer_region,false,
                                  overlay_dependencies,false);
      if ((old_buffer != NULL) && (old_buffer != overlay_buffer))
        owner->internal_delete_buffer(old_buffer);
    }
  
  // Set the stream buffer surfaces
  for (int s=0; s < 2; s++)
    if (streams[s] != NULL)
      streams[s]->set_buffer_surface(buffer,buffer_region,
                                     start_from_scratch);
}

/*****************************************************************************/
/*                     kdrc_layer::take_layer_buffer                         */
/*****************************************************************************/

kdu_compositor_buf *
  kdrc_layer::take_layer_buffer()
{
  kdu_compositor_buf *result = buffer;
  if (result == NULL)
    return NULL;
  assert(compositing_buffer == NULL);
  buffer_is_composition_surface = false;
  buffer =
    owner->internal_allocate_buffer(buffer_region.size,buffer_size,false);
  buffer->set_geometry(rendering_scale,buffer_region,rendering_layer_region);
  assert(!have_alpha_channel);
  if (owner->needs_surface_init())
    initialize_buffer_surface(buffer,buffer_region,NULL,kdu_dims(),0xFFFFFFFF);
  for (int s=0; s < 2; s++)
    if (streams[s] != NULL)
      streams[s]->set_buffer_surface(buffer,buffer_region,true);
  return result;
}

/*****************************************************************************/
/*                     kdrc_layer::measure_visible_area                      */
/*****************************************************************************/

kdu_long
  kdrc_layer::measure_visible_area(kdu_dims notional_region,
                                   bool assume_visible_through_alpha)
{
  if (!have_valid_scale)
    return 0;
  notional_region &= notional_layer_region;
  kdu_long result = notional_region.area();
  kdrc_layer *scan;
  for (scan=this->next; (scan != NULL) && (result > 0); scan=scan->next)
    {
      if (scan->have_alpha_channel && assume_visible_through_alpha)
        continue;
      result -= scan->measure_visible_area(notional_region,
                                           assume_visible_through_alpha);
    }
  if (result < 0)
    result = 0; // Should never happen
  return result;
}

/*****************************************************************************/
/*                   kdrc_layer::get_visible_packet_stats                    */
/*****************************************************************************/

void
  kdrc_layer::get_visible_packet_stats(kdrc_stream *stream,
                                       kdu_dims notional_region,
                                       int max_region_layers,
                                       kdu_long &precinct_samples,
                                       kdu_long &packet_samples,
                                       kdu_long &max_packet_samples)
{
  kdrc_layer *scan = next;
  while ((scan != NULL) && scan->have_alpha_channel)
    scan = scan->next;

  if (scan == NULL)
    stream->get_packet_stats(notional_region,max_region_layers,
                             precinct_samples,packet_samples,
                             max_packet_samples);
  else
    { // Remove the portion of `region' which is covered by `scan'
      kdu_dims cover_dims = scan->notional_layer_region;
      if (!cover_dims.intersects(notional_region))
        scan->get_visible_packet_stats(stream,notional_region,
                                       max_region_layers,precinct_samples,
                                       packet_samples,max_packet_samples);
      else
        {
          kdu_coords min = notional_region.pos;
          kdu_coords lim = min + notional_region.size;
          kdu_coords cover_min = cover_dims.pos;
          kdu_coords cover_lim = cover_min + cover_dims.size;
          kdu_dims visible_region;
          if (cover_min.x > min.x)
            { // Entire left part of region is visible
              assert(cover_min.x < lim.x);
              visible_region.pos.x = min.x;
              visible_region.size.x = cover_min.x - min.x;
              visible_region.pos.y = min.y;
              visible_region.size.y = lim.y - min.y;
              scan->get_visible_packet_stats(stream,
                                             visible_region,max_region_layers,
                                             precinct_samples,packet_samples,
                                             max_packet_samples);
            }
          else
            cover_min.x = min.x;
          if (cover_lim.x < lim.x)
            { // Entire right part of region is visible
              assert(cover_lim.x > min.x);
              visible_region.pos.x = cover_lim.x;
              visible_region.size.x = lim.x - cover_lim.x;
              visible_region.pos.y = min.y;
              visible_region.size.y = lim.y - min.y;
              scan->get_visible_packet_stats(stream,
                                             visible_region,max_region_layers,
                                             precinct_samples,packet_samples,
                                             max_packet_samples);
            }
          else
            cover_lim.x = lim.x;
          if (cover_min.x < cover_lim.x)
            { // Central region fully is partially or fully covered
              visible_region.pos.x = cover_min.x;
              visible_region.size.x = cover_lim.x - cover_min.x;
              if (cover_min.y > min.y)
                { // Upper left part of region is visible
                  assert(cover_min.y < lim.y);
                  visible_region.pos.y = min.y;
                  visible_region.size.y = cover_min.y - min.y;
                  scan->get_visible_packet_stats(stream,visible_region,
                                                 max_region_layers,
                                                 precinct_samples,
                                                 packet_samples,
                                                 max_packet_samples);
                }
              if (cover_lim.y < lim.y)
                { // Lower left part of region is visible
                  assert(cover_lim.y > min.y);
                  visible_region.pos.y = cover_lim.y;
                  visible_region.size.y = lim.y - cover_lim.y;
                  scan->get_visible_packet_stats(stream,visible_region,
                                                 max_region_layers,
                                                 precinct_samples,
                                                 packet_samples,
                                                 max_packet_samples);
                }
            }
        }
    }
}

/*****************************************************************************/
/*                      kdrc_layer::configure_overlay                        */
/*****************************************************************************/

void
  kdrc_layer::configure_overlay(bool enable, int min_display_size,
                                int max_border_size,
                                kdrc_overlay_expression *dependencies,
                                const kdu_uint32 *aux_params,
                                int num_aux_params,
                                bool dependencies_changed,
                                bool aux_params_changed,
                                bool blending_factor_changed)
{
  if ((streams[0] == NULL) || !jpx_layer)
    return;

  if (!enable)
    { // Turn off overlays, but leave `have_overlay_info' equal to true so that
      // `process_overlay' will be called and have a chance to retract the
      // compositing buffer, if appropriate.
      max_overlay_border = 0;
      if (overlay != NULL)
        { 
          overlay->deactivate();
          overlay = NULL;
        }
      if (overlay_buffer != NULL)
        {
          owner->internal_delete_buffer(overlay_buffer);
          overlay_buffer = NULL;
        }
    }
  else
    {
      bool border_changed = (max_border_size != max_overlay_border);
      max_overlay_border = max_border_size;
      if (overlay == NULL)
        { // Overlays were previously disabled
          overlay = streams[0]->get_overlay();
          if (overlay != NULL)
            overlay->activate(owner,max_border_size);
          update_overlay(have_overlay_info,dependencies);
              // If `have_overlay_info' is true before calling the above
              // function, we must have come back here since overlays were
              // previously disabled, without first getting a chance to invoke
              // `process_overlay' -- that function would have marked the
              // entire surface for update. This is a rare condition, so we
              // just set `start_from_scratch' equal to true in the call to
              // `update_overlay'.
        }
      else if (border_changed)
        update_overlay(true,dependencies);
      overlay->update_config(min_display_size,dependencies,
                             aux_params,num_aux_params,
                             dependencies_changed,
                             aux_params_changed,
                             blending_factor_changed);
    }
}

/*****************************************************************************/
/*                        kdrc_layer::update_overlay                         */
/*****************************************************************************/

void
  kdrc_layer::update_overlay(bool start_from_scratch,
                             kdrc_overlay_expression *overlay_dependencies)
{
  if ((streams[0] == NULL) || (overlay == NULL) || !have_valid_scale)
    return;

  if (start_from_scratch)
    {
      overlay->deactivate();
      overlay->activate(owner,max_overlay_border);
    }
  if (waiting_for_set_buffer_surface)
    return;
  
  if (overlay->set_buffer_surface(overlay_buffer,buffer_region,true,
                                  overlay_dependencies,start_from_scratch))
    { 
      if (!have_overlay_info)
        { // Need to create an overlay buffer for the first time
          have_overlay_info = true;
          if (compositing_buffer == NULL)
            { // Must be the only layer; we need to create a separate
              // compositing buffer for the first time.
              assert((next == NULL) && (prev == NULL) &&
                     (overlay_buffer == NULL));
              if (buffer_is_composition_surface)
                { // Have to make current `buffer' the new composition buffer
                  // so the application can continue using the buffer recovered
                  // from `kdu_region_compositor::get_composition_buffer'
                  owner->donate_compositing_buffer(buffer,buffer_region,
                                                   buffer_size);
                  compositing_buffer = buffer;
                  buffer = owner->internal_allocate_buffer(buffer_region.size,
                                                           buffer_size,true);
                  buffer->set_geometry(rendering_scale,buffer_region,
                                       rendering_layer_region);
                  bool start_stream_from_scratch = false;
                  if (!compositing_buffer->set_read_accessibility(true))
                    { // Write-only buffer became read-write.
                      if (owner->needs_surface_init())
                        initialize_buffer_surface(compositing_buffer,
                                                  buffer_region,NULL,
                                                  kdu_dims(),0xFFFFFFFF);
                      start_stream_from_scratch = true;
                    }
                  initialize_buffer_surface(buffer,buffer_region,
                                            compositing_buffer,buffer_region,
                                            0xFFFFFFFF);
                  for (int s=0; s < 2; s++)
                    if (streams[s] != NULL)
                      streams[s]->set_buffer_surface(buffer,buffer_region,
                                                  start_stream_from_scratch);
                }
              else
                { // We can keep our `buffer' and create a separate composition
                  // buffer.
                  kdu_coords compbuf_size;
                  kdu_compositor_buf *compbuf =
                    owner->internal_allocate_buffer(buffer_region.size,
                                                    compbuf_size,true);
                  compbuf->set_geometry(rendering_scale,buffer_region,
                                        rendering_layer_region);
                  owner->donate_compositing_buffer(compbuf,buffer_region,
                                                   compbuf_size);
                  compositing_buffer = compbuf;
                  
                  bool start_stream_from_scratch = false;
                  if (!buffer->set_read_accessibility(true))
                    { // Write-only buffer became read-write.
                      if (owner->needs_surface_init())
                        initialize_buffer_surface(buffer,buffer_region,
                                                  NULL,kdu_dims(),0xFFFFFFFF);
                      start_stream_from_scratch = true;
                    }
                  if (owner->needs_surface_init())
                    initialize_buffer_surface(compbuf,buffer_region,
                                              buffer,buffer_region,0xFFFFFFFF);
                  if (start_stream_from_scratch)
                    for (int s=0; s < 2; s++)
                      if (streams[s] != NULL)
                        streams[s]->invalidate_surface();
                }
            }

          if (overlay_buffer == NULL)
            { 
              overlay_buffer =
                owner->internal_allocate_buffer(buffer_region.size,
                                                overlay_buffer_size,true);
              overlay->set_buffer_surface(overlay_buffer,buffer_region,false,
                                          overlay_dependencies,false);
            }
        }
    }
}

/*****************************************************************************/
/*                        kdrc_layer::process_overlay                        */
/*****************************************************************************/

bool
  kdrc_layer::process_overlay(kdu_dims &new_region, bool &painted_something)
{
  if (!have_overlay_info)
    return false;
  if (overlay == NULL)
    { // Must have only just disabled overlays
      have_overlay_info = false;
      kdu_compositor_buf *old_buffer = buffer;
      new_region = buffer_region;
      if ((compositing_buffer != NULL) &&
          owner->retract_compositing_buffer(buffer_size))
        { // We no longer need a separate compositing buffer; swap current
          // compositing buffer into current buffer position so application
          // sees no change
          buffer = compositing_buffer;
          compositing_buffer = NULL;
          assert(!have_alpha_channel);
          initialize_buffer_surface(buffer,buffer_region,old_buffer,
                                    buffer_region,0xFFFFFFFF);
             // The above copy is important because we are changing our local
             // working `buffer' to the one that used to be the compositing
             // buffer.
          buffer->set_read_accessibility(false);
          for (int s=0; s < 2; s++)
            if (streams[s] != NULL)
              streams[s]->set_buffer_surface(buffer,buffer_region,false);
          owner->internal_delete_buffer(old_buffer);
        }
      return true;
    }
  return overlay->process(new_region,painted_something);
}

/*****************************************************************************/
/*                          kdrc_layer::map_region                           */
/*****************************************************************************/

bool
  kdrc_layer::map_region(kdu_dims &region, kdrc_stream * &stream)
{
  if ((!have_valid_scale) || (streams[0] == NULL))
    return false;
  kdu_dims result = streams[0]->map_region(region,true);
  if (result.is_empty())
    return false;
  region = result;
  stream = streams[0];
  return true;
}

/*****************************************************************************/
/*                           kdrc_layer::get_opacity                         */
/*****************************************************************************/

float
  kdrc_layer::get_opacity(kdu_coords point)
{
  convert_notional_point(point);
  kdu_coords off = point - buffer_region.pos;
  if ((off.x < 0) || (off.x >= buffer_region.size.x) ||
      (off.y < 0) || (off.y >= buffer_region.size.y))
    return 0.0F;
  if (!have_alpha_channel)
    return 1.0F;
  if (buffer == NULL)
    return 1.0F;
  int row_gap;
  kdu_uint32 *buf32 = buffer->get_buf(row_gap,true);
  if (buf32 != NULL)
    return (buf32[row_gap*off.y + off.x]>>24) * (1.0F / 255.0F);
  float *buf_float = buffer->get_float_buf(row_gap,true);
  if (buf_float != NULL)
    {
      float result = buf_float[row_gap*off.y + (off.x<<2)];
      result = (result < 0.0F)?0.0F:result;
      result = (result > 1.0F)?1.0F:result;
      return result;
    }
  assert(0);
  return 1.0F;
}

/*****************************************************************************/
/*                        kdrc_layer::get_overlay_info                       */
/*****************************************************************************/

void kdrc_layer::get_overlay_info(int &total_nodes, int &hidden_nodes)
{
  if (overlay != NULL)
    overlay->count_nodes(total_nodes,hidden_nodes);
}

/*****************************************************************************/
/*                         kdrc_layer::search_overlay                        */
/*****************************************************************************/

jpx_metanode
  kdrc_layer::search_overlay(kdu_coords point, kdrc_stream * &stream,
                             bool &is_opaque)
{
  is_opaque = false;
  if (overlay == NULL)
    return jpx_metanode(NULL);
  convert_notional_point(point);
  kdu_coords off = point - buffer_region.pos;
  if ((off.x < 0) || (off.x >= buffer_region.size.x) ||
      (off.y < 0) || (off.y >= buffer_region.size.y))
    return jpx_metanode(NULL);
  is_opaque = !have_alpha_channel;
  jpx_metanode result = overlay->search(point);
  if (result.exists())
    stream = streams[0];
  return result;
}

/*****************************************************************************/
/*                          kdrc_layer::do_composition                       */
/*****************************************************************************/

void
  kdrc_layer::do_composition(kdu_dims region,
                             kdu_int16 ovl_fact_x128,
                             kdu_uint32 *erase_background)
{
  if (compositing_buffer == NULL)
    return; // Nothing to do
  region &= compositing_buffer_region;
  if (!region)
    return; // Nothing to do
  int comp_row_gap=0;
  kdu_uint32 *cbuf = compositing_buffer->get_buf(comp_row_gap,true);
  float *fcbuf=NULL;
  if (cbuf == NULL)
    fcbuf = compositing_buffer->get_float_buf(comp_row_gap,true);
  assert((cbuf != NULL) || (fcbuf != NULL));
  
  kdu_dims com_region = region & this->buffer_region;
       // `com_region' is the common region for compositing
  if (com_region.is_empty())
    com_region.pos = region.pos; // Just to make sure everything works below

  if (erase_background != NULL)
    { // First layer; erase the region first before painting, but let's be
      // clever about this and do it by parts if we are an opaque layer.
      kdu_dims edims;
      kdu_coords regmin, reglim, commin, comlim;
      regmin = region.pos;  reglim = regmin + region.size;
      commin = com_region.pos;  comlim = commin + com_region.size;
      for (edims.pos.y=regmin.y; edims.pos.y < reglim.y;
           edims.pos.y+=edims.size.y)
        { // Walk through top, centre and bottom strip that might need erasing
          bool mid_strip = false;
          if (have_alpha_channel || (edims.pos.y >= comlim.y))
            edims.size.y = reglim.y; // Erase everything that is left
          else if (edims.pos.y < commin.y)
            edims.size.y = commin.y;
          else
            { mid_strip = true; edims.size.y = comlim.y; }
          edims.size.y -= edims.pos.y;
          for (edims.pos.x=regmin.x; edims.pos.x < reglim.x;
               edims.pos.x += edims.size.x)
            { // Walk through left, centre, right blocks that may need erasing
              if (have_alpha_channel || (edims.pos.x >= comlim.x) ||
                  !mid_strip)
                edims.size.x = reglim.x; // Erase everything that is left
              else if (edims.pos.x < commin.x)
                edims.size.x = commin.x;
              else
                { 
                  edims.size.x = comlim.x - edims.pos.x;
                  assert(edims == com_region);
                  continue;
                }
              edims.size.x -= edims.pos.x;
              if (cbuf != NULL)
                erase_buffer_region(cbuf,comp_row_gap,
                                    compositing_buffer_region,edims,
                                    *erase_background);
              else
                erase_float_buffer_region(fcbuf,comp_row_gap,
                                          compositing_buffer_region,edims,
                                          *erase_background);
            }
        }
    }

  // From now on, we can restrict all composition processing to the common
  // region between the supplied `region' and the layer's `buffer_region'.
  region = com_region;
  if ((streams[0] == NULL) || !region)
    return;

  int src_row_gap=0;
  assert(have_valid_scale && (buffer != NULL));
  kdu_uint32 *src_buf = (cbuf==NULL)?NULL:buffer->get_buf(src_row_gap,true);
  float *src_fbuf =
    (fcbuf==NULL)?NULL:buffer->get_float_buf(src_row_gap,true);
  if (!have_alpha_channel)
    { // Just copy the region
      if (cbuf != NULL)
        copy_buffer_region(cbuf,comp_row_gap,compositing_buffer_region,
                           src_buf,src_row_gap,buffer_region,region);
      else
        copy_float_buffer_region(fcbuf,comp_row_gap,compositing_buffer_region,
                                 src_fbuf,src_row_gap,buffer_region,region);
    }
  else if (!alpha_is_premultiplied)
    { // Alpha blend the region
      if (cbuf != NULL)
        blend_buffer_region(cbuf,comp_row_gap,compositing_buffer_region,
                            src_buf,src_row_gap,buffer_region,region);
      else
        blend_float_buffer_region(fcbuf,comp_row_gap,compositing_buffer_region,
                                  src_fbuf,src_row_gap,buffer_region,region);
    }
  else
    { // Alpha blend the region, using premultiplied alpha
      if (cbuf != NULL)
        pre_blend_buffer_region(cbuf,comp_row_gap,compositing_buffer_region,
                                src_buf,src_row_gap,buffer_region,region);
      else
        pre_blend_float_buffer_region(fcbuf,comp_row_gap,
                                      compositing_buffer_region,src_fbuf,
                                      src_row_gap,buffer_region,region);
    }

  if ((overlay_buffer == NULL) || (ovl_fact_x128 == 0) || (overlay == NULL))
    return;

  // Alpha blend the overlay information
  int ovl_row_gap=0;
  kdu_uint32 *ovl_buf =
    (cbuf==NULL)?NULL:overlay_buffer->get_buf(ovl_row_gap,true);
  float *ovl_fbuf =
    (fcbuf==NULL)?NULL:overlay_buffer->get_float_buf(ovl_row_gap,true);
  kdu_dims ovl_seg_indices, seg_regn;
  overlay->find_seg_indices(region,ovl_seg_indices);
  kdu_coords idx;
  for (idx.y=0; idx.y < ovl_seg_indices.size.y; idx.y++)
    for (idx.x=0; idx.x < ovl_seg_indices.size.x; idx.x++)
      { 
        seg_regn = region;
        if (!overlay->get_visible_seg_region(idx+ovl_seg_indices.pos,seg_regn))
          continue;
        if (ovl_fact_x128 == 128)
          { // No scaling of overlay alpha channel required
            if (cbuf != NULL)
              blend_buffer_region(cbuf,comp_row_gap,compositing_buffer_region,
                                  ovl_buf,ovl_row_gap,buffer_region,seg_regn);
            else
              blend_float_buffer_region(fcbuf,comp_row_gap,
                                        compositing_buffer_region,ovl_fbuf,
                                        ovl_row_gap,buffer_region,seg_regn);
          }
        else
          { // Need to scale overlay alpha channel by factor / 256 -- may
            // also need to invert the colour channels
            if (cbuf != NULL)
              mod_blend_buffer_region(cbuf,comp_row_gap,
                                      compositing_buffer_region,
                                      ovl_buf,ovl_row_gap,buffer_region,
                                      seg_regn,ovl_fact_x128);
            else
              mod_blend_float_buffer_region(fcbuf,comp_row_gap,
                                            compositing_buffer_region,ovl_fbuf,
                                            ovl_row_gap,buffer_region,
                                            seg_regn,ovl_fact_x128*(1.0F/128));
          }
      }
}


/* ========================================================================= */
/*                              kdrc_refresh                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                         kdrc_refresh::add_region                          */
/*****************************************************************************/

void
  kdrc_refresh::add_region(kdu_dims region)
{
  kdrc_refresh_elt *scan, *prev, *next;
  for (prev=NULL, scan=list; scan != NULL; prev=scan, scan=next)
    {
      next = scan->next;
      kdu_dims intersection = scan->region & region;
      if (!intersection)
        continue;
      if (intersection == region)
        return; // region is entirely contained in existing element
      if (intersection == scan->region)
        { // Existing element is entirely contained in new region
          if (prev == NULL)
            list = next;
          else
            prev->next = next;
          scan->next = free_elts;
          free_elts = scan;
          scan = prev; // So `prev' does not change
          continue;
        }

      // See if we should shrink either the new region or the existing region
      if ((intersection.pos.x == region.pos.x) && 
          (intersection.size.x == region.size.x))
        { // See if we can reduce the height of the new region
          int int_min = intersection.pos.y;
          int int_lim = int_min + intersection.size.y;
          int reg_min = region.pos.y;
          int reg_lim = reg_min + region.size.y;
          if (int_min == reg_min)
            {
              region.pos.y = int_lim;
              region.size.y = reg_lim - int_lim;
            }
          else if (int_lim == reg_lim)
            region.size.y = int_min - reg_min;
        }
      else if ((intersection.pos.y == region.pos.y) &&
               (intersection.size.y == region.size.y))
        { // See if we can reduce the width of the new region
          int int_min = intersection.pos.x;
          int int_lim = int_min + intersection.size.x;
          int reg_min = region.pos.x;
          int reg_lim = reg_min + region.size.x;
          if (int_min == reg_min)
            {
              region.pos.x = int_lim;
              region.size.x = reg_lim - int_lim;
            }
          else if (int_lim == reg_lim)
            region.size.x = int_min - reg_min;
        }
      else if ((intersection.pos.x == scan->region.pos.x) &&
               (intersection.size.x == scan->region.size.x))
        { // See if we can reduce the height of the existing region
          int int_min = intersection.pos.y;
          int int_lim = int_min + intersection.size.y;
          int reg_min = scan->region.pos.y;
          int reg_lim = reg_min + scan->region.size.y;
          if (int_min == reg_min)
            {
              scan->region.pos.y = int_lim;
              scan->region.size.y = reg_lim - int_lim;
            }
          else if (int_lim == reg_lim)
            scan->region.size.y = int_min - reg_min;
        }
      else if ((intersection.pos.y == scan->region.pos.y) &&
               (intersection.size.y == scan->region.size.y))
        { // See if we can reduce the width of the existing region
          int int_min = intersection.pos.x;
          int int_lim = int_min + intersection.size.x;
          int reg_min = scan->region.pos.x;
          int reg_lim = reg_min + scan->region.size.x;
          if (int_min == reg_min)
            {
              scan->region.pos.x = int_lim;
              scan->region.size.x = reg_lim - int_lim;
            }
          else if (int_lim == reg_lim)
            scan->region.size.x = int_min - reg_min;
        }
    }

  kdrc_refresh_elt *elt = free_elts;
  if (elt == NULL)
    elt = new kdrc_refresh_elt;
  else
    free_elts = elt->next;
  elt->next = list;
  list = elt;
  elt->region = region;
  if ((min_y_count == 0) || (region.pos.y < min_y_val))
    { min_y_count = 1; min_y_val = region.pos.y; }
  else if (region.pos.y == min_y_val)
    min_y_count++;
}

/*****************************************************************************/
/*                             kdrc_refresh::reset                           */
/*****************************************************************************/

void
  kdrc_refresh::reset()
{
  kdrc_refresh_elt *tmp;
  while ((tmp=list) != NULL)
    {
      list = tmp->next;
      tmp->next = free_elts;
      free_elts = tmp;
    }
  min_y_val = min_y_count = 0;
}

/*****************************************************************************/
/*                    kdrc_refresh::pop_largest_region                       */
/*****************************************************************************/

bool
  kdrc_refresh::pop_largest_region(kdu_dims &region)
{
  if (list == NULL)
    return false;
  kdrc_refresh_elt *scan, *prev, *best=NULL, *best_prev=NULL;
  for (prev=NULL, scan=list; scan != NULL; prev=scan, scan=scan->next)
    if ((best == NULL) || (best->region.area() < scan->region.area()))
      { best=scan; best_prev=prev; }
  region = best->region;
  if (best_prev == NULL)
    list = best->next;
  else
    best_prev->next = best->next;
  best->next = free_elts; free_elts = best;
  if (region.pos.y <= min_y_val)
    { 
      if ((region.pos.y < min_y_val) || (min_y_count == 0))
        { // Should not be possible
          assert(0);
          recalculate_min_y();
        }
      else
        { 
          min_y_count--;
          if ((min_y_count <= 0) && (list != NULL))
            recalculate_min_y();
        }
    }
  return true;
}

/*****************************************************************************/
/*                      kdrc_refresh::pop_and_aggregate                      */
/*****************************************************************************/

bool
  kdrc_refresh::pop_and_aggregate(kdu_dims &aggregated_region,
                                  kdu_dims &new_region,
                                  kdu_long &aggregated_area,
                                  float aggregation_threshold)
{
  kdrc_refresh_elt *scan, *prev;
  kdu_coords agg_min = aggregated_region.pos;
  kdu_coords agg_lim = agg_min + aggregated_region.size;
  for (prev=NULL, scan=list; scan != NULL; prev=scan, scan=scan->next)
    { 
      kdu_coords new_min = scan->region.pos;
      kdu_coords new_lim = new_min + scan->region.size;
      new_min.x = (new_min.x < agg_min.x)?new_min.x:agg_min.x;
      new_min.y = (new_min.y < agg_min.y)?new_min.y:agg_min.y;
      new_lim.x = (new_lim.x > agg_lim.x)?new_lim.x:agg_lim.x;
      new_lim.y = (new_lim.y > agg_lim.y)?new_lim.y:agg_lim.y;
      kdu_long reg_area=(new_lim.x-new_min.x); reg_area*=(new_lim.y-new_min.y);
      kdu_long new_agg_area = aggregated_area + scan->region.area();
      if ((reg_area * aggregation_threshold) > (float) new_agg_area)
        continue;
      
      // If we get here, we have decided to pop and aggregate `scan'
      aggregated_area = new_agg_area;
      aggregated_region.pos = new_min;
      aggregated_region.size = new_lim - new_min;
      new_region = scan->region;
      if (prev == NULL)
        list = scan->next;
      else
        prev->next = scan->next;
      scan->next = free_elts;
      free_elts = scan;
      if (new_region.pos.y <= min_y_val)
        { 
          if ((new_region.pos.y < min_y_val) || (min_y_count == 0))
            { // Should not be possible
              assert(0);
              recalculate_min_y();
            }
          else
            { 
              min_y_count--;
              if ((min_y_count <= 0) && (list != NULL))
                recalculate_min_y();
            }
        }
      return true;
    }
  new_region = kdu_dims();
  return false;
}

/*****************************************************************************/
/*                      kdrc_refresh::adjust (region)                        */
/*****************************************************************************/

void
  kdrc_refresh::adjust(kdu_dims buffer_region)
{
  kdrc_refresh_elt *scan, *prev, *next;
  for (prev=NULL, scan=list; scan != NULL; prev=scan, scan=next)
    {
      next = scan->next;
      scan->region &= buffer_region;
      if (!scan->region)
        {
          if (prev == NULL)
            list = next;
          else
            prev->next = next;
          scan->next = free_elts;
          free_elts = scan;
          scan = prev; // So `prev' does not change
        }
    }
  recalculate_min_y();
}

/*****************************************************************************/
/*                      kdrc_refresh::adjust (stream)                        */
/*****************************************************************************/

void
  kdrc_refresh::adjust(kdrc_stream *stream)
{
  kdu_dims rects[6];
  kdrc_refresh_elt *scan, *old_list = list;
  list = NULL;
  while ((scan=old_list) != NULL)
    {
      old_list = scan->next;
      int n, num_rects = stream->find_non_pending_rects(scan->region,rects);
      assert(num_rects <= 6);
      for (n=0; n < num_rects; n++)
        add_region(rects[n]);
      scan->next = free_elts;
      free_elts = scan;
    }
  recalculate_min_y();
}


/* ========================================================================= */
/*                            kdu_overlay_params                             */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdu_overlay_params::push_aux_params                     */
/*****************************************************************************/

void kdu_overlay_params::push_aux_params(const kdu_uint32 *aux_params,
                                         int num_aux_params)
{
  if (num_aux_params < 0) num_aux_params = 0; // Just in case
  if (num_aux_params > max_tmp_aux_params)
    { 
      max_tmp_aux_params = num_aux_params;
      if (tmp_aux_params!=NULL)
        { delete[] tmp_aux_params; tmp_aux_params = NULL; }
      tmp_aux_params = new kdu_uint32[max_tmp_aux_params];
    }
  num_cur_aux_params = num_aux_params; cur_aux_params=tmp_aux_params;
  memcpy(tmp_aux_params,aux_params,((size_t) num_aux_params)<<2); 
}

/*****************************************************************************/
/*                kdu_overlay_params::configure_ring_points                  */
/*****************************************************************************/

void kdu_overlay_params::configure_ring_points(int stride, int radius)
{
  if (radius > max_painting_border)
    radius = max_painting_border;
  if (radius <= 0)
    cur_radius = 0;
  if ((stride != cur_stride) || (all_ring_points == NULL))
    { // Set up the lookup tables
      cur_ring_points = cur_ring_prefices = NULL;  cur_radius = 0;
      if (ring_handle != NULL)
        {
          delete[] ring_handle;
          ring_handle = all_ring_points = all_ring_prefices = NULL;
        }
      int max_points = // Conservative estimate of total number of ring points
        (2*max_painting_border+1)*(max_painting_border+1);
      int r, x, y, num_prefices=0;
      for (r=0; r <= max_painting_border; r++)
        num_prefices += 2*r+1;
      ring_handle = new int[2*max_points+num_prefices];
      all_ring_points = ring_handle + num_prefices;
      all_ring_prefices = ring_handle;
      int *prefices = all_ring_prefices;
      int *points = all_ring_points;
      for (r=0; r <= max_painting_border; r++)
        {
          int lim_rad_sq = 1 + (((2*r+1)*(2*r+1)+1)>>2);
          int min_rad_sq = 1 + (((2*r-1)*(2*r-1)+1)>>2);
          int *pp = points;
          for (y=-r; y <= r; y++, prefices++)
            {
              for (x=0; x <= r; x++)
                {
                  int rad_sq = x*x + y*y;
                  if ((rad_sq >= min_rad_sq) && (rad_sq < lim_rad_sq))
                    { // Found a point on the ring
                      *(pp++) = x;
                      *(pp++) = y * stride;
                    }
                }
              *prefices = ((int)(pp-points)) >> 1;
            }
          points = pp;
        }
      assert(points <= (all_ring_points + 2*max_points));
      cur_stride = stride;
    }
  assert(all_ring_points != NULL);
  cur_ring_prefices = all_ring_prefices;
  cur_ring_points = all_ring_points;
  for (cur_radius=0; cur_radius < radius; cur_radius++)
    {
      cur_ring_prefices += 2*cur_radius+1;
      cur_ring_points += cur_ring_prefices[-1]*2;
    }
}

/*****************************************************************************/
/*                   kdu_overlay_params::map_jpx_regions                     */
/*****************************************************************************/

jpx_roi *
  kdu_overlay_params::map_jpx_regions(const jpx_roi *reg, int num_regions,
                                      kdu_coords image_offset,
                                      kdu_coords subsampling,
                                      bool transpose, bool vflip, bool hflip,
                                      kdu_coords expansion_numerator,
                                      kdu_coords expansion_denominator,
                                      kdu_coords compositing_offset)
{
  if (num_regions <= 0)
    return NULL;
  if (num_regions > max_rois)
    {
      max_rois = num_regions;
      if (roi_buf != NULL)
        { delete[] roi_buf; roi_buf = NULL; }
      roi_buf = new jpx_roi[max_rois];
    }
  jpx_roi *dest = roi_buf;
  for (; num_regions > 0; num_regions--, reg++, dest++)
    map_jpx_roi_to_compositing_grid(dest,reg,image_offset,subsampling,
                                    transpose,vflip,hflip,expansion_numerator,
                                    expansion_denominator,compositing_offset);
  return roi_buf;
}


/* ========================================================================= */
/*                          kdu_region_compositor                            */
/* ========================================================================= */

/*****************************************************************************/
/*            kdu_region_compositor::kdu_region_compositor (blank)           */
/*****************************************************************************/

kdu_region_compositor::kdu_region_compositor(kdu_thread_env *env,
                                             kdu_thread_queue *env_queue)
{
  init(env,env_queue);
}

/*****************************************************************************/
/*            kdu_region_compositor::kdu_region_compositor (raw)             */
/*****************************************************************************/

kdu_region_compositor::kdu_region_compositor(kdu_compressed_source *source,
                                             int persistent_cache_threshold)
{
  init(NULL,NULL);
  create(source,persistent_cache_threshold);
}

/*****************************************************************************/
/*            kdu_region_compositor::kdu_region_compositor (JPX)             */
/*****************************************************************************/

kdu_region_compositor::kdu_region_compositor(jpx_source *source,
                                             int persistent_cache_threshold)
{
  init(NULL,NULL);
  create(source,persistent_cache_threshold);
}

/*****************************************************************************/
/*            kdu_region_compositor::kdu_region_compositor (MJ2)             */
/*****************************************************************************/

kdu_region_compositor::kdu_region_compositor(mj2_source *source,
                                             int persistent_cache_threshold)
{
  init(NULL,NULL);
  create(source,persistent_cache_threshold);
}

/*****************************************************************************/
/*                       kdu_region_compositor::init                         */
/*****************************************************************************/

void
  kdu_region_compositor::init(kdu_thread_env *env, kdu_thread_queue *env_queue)
{
  this->raw_src = NULL;
  this->jpx_src = NULL;
  this->mj2_src = NULL;
  this->colour_order = KDU_ARGB_ORDER;
  this->error_level = 0;
  this->scaling_min_resampling_factor = 0.6f;
  this->scaling_max_interp_overshoot = 0.4f;
  this->scaling_bilinear_interp_threshold = 2;
  this->intensity_true_zero = false;
  this->intensity_true_max = false;
  this->persistent_codestreams = true;
  this->codestream_cache_threshold = 256000;
  this->process_aggregation_threshold = 0.9F;
  this->limiter = NULL;
  this->limiter_ppi_x = this->limiter_ppi_y = -1.0f;
  in_pre_destroy = false;
  composition_buffer = NULL;
  notional_buffer_region = kdu_dims();
  rendering_buffer_region = kdu_dims();
  buffer_size = kdu_coords();
  buffer_background = 0xFFFFFFFF;
  processing_complete = true;
  have_valid_scale = false;
  max_quality_layers = 1<<16;
  vflip = hflip = transpose = false;
  composition_invalid = true;
  notional_scale = 1.0F;
  rendering_scale = 1.0F;
  invalid_scale_code = 0;
  queue_head = queue_tail = queue_free = NULL;
  active_layers = last_active_layer = inactive_layers = NULL;
  streams = NULL;
  can_skip_surface_initialization = false;
  initialize_surfaces_on_next_refresh = false;
  enable_overlays = false;
  overlay_log2_segment_size = KDRC_OVERLAY_LOG2_SEGMENT_SIZE;
  overlay_min_display_size = 8;
  overlay_factor_x128 = 128;
  overlay_dependencies = NULL;
  nominal_overlay_max_painting_border = 0;
  nominal_overlay_num_aux_params = 0;
  nominal_overlay_aux_params = NULL;
  scaled_overlay_params_invalid = false;
  scaled_overlay_max_painting_border = 0;
  scaled_overlay_num_aux_params = 0;
  scaled_overlay_aux_params = NULL;
  refresh_mgr = new kdrc_refresh();
  if ((env != NULL) && !env->exists())
    env = NULL;
  this->env = env;
  this->env_queue = (env==NULL)?NULL:env_queue;
}

/*****************************************************************************/
/*                      kdu_region_compositor::create (raw)                  */
/*****************************************************************************/

void
  kdu_region_compositor::create(kdu_compressed_source *source,
                                int persistent_cache_threshold)
{
  if ((raw_src != NULL) || (jpx_src != NULL) || (mj2_src != NULL))
    { KDU_ERROR_DEV(e,0x04040501); e <<
      KDU_TXT("Attempting to invoke `kdu_region_compositor::create' on an "
              "object which has already been created.");
    }

  this->raw_src = source;
  this->persistent_codestreams = (persistent_cache_threshold >= 0);
  this->codestream_cache_threshold = persistent_cache_threshold;
}

/*****************************************************************************/
/*                    kdu_region_compositor::create (JPX/JP2)                */
/*****************************************************************************/

void
  kdu_region_compositor::create(jpx_source *source,
                                int persistent_cache_threshold)
{
  if ((raw_src != NULL) || (jpx_src != NULL) || (mj2_src != NULL))
    { KDU_ERROR_DEV(e,0x04040502); e <<
      KDU_TXT("Attempting to invoke `kdu_region_compositor::create' on an "
              "object which has already been created.");
    }

  this->jpx_src = source;
  this->persistent_codestreams = (persistent_cache_threshold >= 0);
  this->codestream_cache_threshold = persistent_cache_threshold;
}

/*****************************************************************************/
/*                     kdu_region_compositor::create (MJ2)                   */
/*****************************************************************************/

void
  kdu_region_compositor::create(mj2_source *source,
                                int persistent_cache_threshold)
{
  if ((raw_src != NULL) || (jpx_src != NULL) || (mj2_src != NULL))
    { KDU_ERROR_DEV(e,0x04040503); e <<
      KDU_TXT("Attempting to invoke `kdu_region_compositor::create' on an "
              "object which has already been created.");
    }

  this->mj2_src = source;
  this->persistent_codestreams = (persistent_cache_threshold >= 0);
  this->codestream_cache_threshold = persistent_cache_threshold;
}

/*****************************************************************************/
/*                    kdu_region_compositor::pre_destroy                     */
/*****************************************************************************/

void
  kdu_region_compositor::pre_destroy()
{
  in_pre_destroy = true;
  remove_ilayer(kdu_ilayer_ref(),true);
  if (composition_buffer != NULL)
    { internal_delete_buffer(composition_buffer); composition_buffer = NULL; }
  if (refresh_mgr != NULL)
    { delete refresh_mgr; refresh_mgr = NULL; }
  flush_composition_queue();
  while ((queue_tail=queue_free) != NULL)
    {
      queue_free = queue_tail->next;
      delete queue_tail;
    }
  if (overlay_dependencies != NULL)
    {
      delete overlay_dependencies;
      overlay_dependencies = NULL;
    }
  if (nominal_overlay_aux_params != NULL)
    { 
      delete nominal_overlay_aux_params;
      nominal_overlay_aux_params = NULL;
      nominal_overlay_num_aux_params = 0;
    }
  if (scaled_overlay_aux_params != NULL)
    { 
      delete scaled_overlay_aux_params;
      scaled_overlay_aux_params = NULL;
      scaled_overlay_num_aux_params = 0;
    }
  if (limiter != NULL)
    { delete limiter; limiter = NULL; }
}

/*****************************************************************************/
/*                  kdu_region_compositor::set_colour_order                  */
/*****************************************************************************/

void
  kdu_region_compositor::set_colour_order(kdu_colour_order order)
{
  this->colour_order = order;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    scan->set_colour_order(order);
}

/*****************************************************************************/
/*                  kdu_region_compositor::set_error_level                   */
/*****************************************************************************/

void
  kdu_region_compositor::set_error_level(int level)
{
  this->error_level = level;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    scan->set_error_level(level);
}

/*****************************************************************************/
/*                kdu_region_compositor::set_quality_limiting                */
/*****************************************************************************/

void
  kdu_region_compositor::set_quality_limiting(const kdu_quality_limiter *obj,
                                              float ppi_x, float ppi_y)
{
  if (this->limiter != NULL)
    { delete this->limiter; this->limiter = NULL; }
  if (obj != NULL)
    { 
      this->limiter = obj->duplicate();
      this->limiter_ppi_x = ppi_x;
      this->limiter_ppi_y = ppi_y;
    }
  else
    limiter_ppi_x = limiter_ppi_y = -1.0f;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    scan->update_quality_limiting();  
}

/*****************************************************************************/
/*           kdu_region_compositor::set_surface_initialization_mode          */
/*****************************************************************************/

void kdu_region_compositor::set_surface_initialization_mode(bool pre_init)
{ 
  bool can_skip_now = !pre_init;
  if (can_skip_now == this->can_skip_surface_initialization)
    return;
  initialize_surfaces_on_next_refresh =
  (can_skip_surface_initialization && !can_skip_now);
  can_skip_surface_initialization = can_skip_now;
  if (can_skip_now && !composition_invalid)
    { 
      refresh_mgr->reset();
      refresh_mgr->add_region(rendering_buffer_region);
    }
}

/*****************************************************************************/
/*                    kdu_region_compositor::add_ilayer                      */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::add_ilayer(int layer_src,
                                    kdu_dims full_source_dims,
                                    kdu_dims full_target_dims,
                                    bool transpose, bool vflip, bool hflip,
                                    int frame_idx, int field_handling)
{
  if (full_target_dims.is_empty())
    full_target_dims = kdu_dims();
      // The above, seemingly innocent operation, ensures that flipping the
      // `full_target_dims' region in any direction will leave the
      // corresponding coordinate of `full_target_dims.pos' equal to 1 (as
      // opposed to 0) which satisfies the expectations of
      // `kdrc_stream::set_scale', as required by `kdrc_layer::init'.
  full_target_dims.from_apparent(transpose,vflip,hflip);
          // Convert to geometry prior to any flipping/transposition.

  fixed_composition_dims = kdu_dims();
  int match_colour_init_src = -1;       // These are for searching for
  int match_direct_codestream_idx = -1; // a matching inactive layer
  jpx_layer_source jpx_layer;
  if (raw_src != NULL)
    {
      frame_idx = field_handling = 0;
      if (layer_src != 0)
        { KDU_ERROR_DEV(e,9); e <<
            KDU_TXT("Invalid `layer_src' argument supplied to "
                    "`kdu_region_compositor::add_ilayer'.  Must be 0 for raw "
                    "raw codestream data sources.");
        }
      match_direct_codestream_idx = 0;
    }
  else if (jpx_src != NULL)
    { // Try to open the requested JPX compositing layer
      frame_idx = field_handling = 0;
      int available_layers;
      bool all_found = jpx_src->count_compositing_layers(available_layers);
      jpx_layer = jpx_src->access_layer(layer_src,false);
      if (!jpx_layer)
        {
          if ((layer_src >= 0) &&
              ((layer_src < available_layers) || !all_found))
            return kdu_ilayer_ref(); // Check back once more data is in cache
          KDU_ERROR_DEV(e,10); e <<
            KDU_TXT("Attempting to use a non-existent JPX compositing "
                    "layer in call to `kdu_region_compositor::add_ilayer'.");
        }
      match_colour_init_src = layer_src;
    }
  else if (mj2_src != NULL)
    { 
      int track_type = mj2_src->get_track_type((kdu_uint32)(layer_src+1));
      if (track_type == MJ2_TRACK_MAY_EXIST)
        return kdu_ilayer_ref(); // Check back once more data is in the cache
      if (track_type != MJ2_TRACK_IS_VIDEO)
        { KDU_ERROR_DEV(e,11); e <<
          KDU_TXT("Attempting to add a non-existent or non-video "
                  "Motion JPEG2000 track via "
                  "`kdu_region_compositor::add_ilayer'.");
        }
    }
  else
    { KDU_ERROR_DEV(e,0x09021001); e <<
      KDU_TXT("The `kdu_region_comositor::add_ilayer' function cannot be "
              "used without first installing a data source.");
    }

  // Look for the layer on the inactive list
  kdrc_layer *scan, *prev;
  for (prev=NULL, scan=inactive_layers; scan != NULL;
       prev=scan, scan=scan->next)
    if ((scan->colour_init_src == match_colour_init_src) &&
        (scan->direct_codestream_idx == match_direct_codestream_idx) &&
        (scan->direct_component_idx < 0))
      { // Found it; remove it from the list
        if (prev == NULL)
          inactive_layers = scan->next;
        else
          prev->next = scan->next;
        scan->next = scan->prev = NULL;
        break;
      }

  bool need_layer_init=false;
  if (scan == NULL)
    { // Create the layer from scratch
      scan = new kdrc_layer(this);
      need_layer_init = true;
    }

  // Append to the end of the active layers list and complete the configuration
  if (last_active_layer == NULL)
    {
      assert(active_layers == NULL);
      active_layers = last_active_layer = scan;
    }
  else
    {
      scan->prev = last_active_layer;
      last_active_layer->next = scan;
      last_active_layer = scan;
    }

  composition_invalid = true;
  jpx_composited_orientation orientation(transpose,vflip,hflip);
  try {
      if (!need_layer_init)
        scan->activate(full_source_dims,full_target_dims,orientation,
                       frame_idx,field_handling);
      else if (jpx_src != NULL)
        scan->init(jpx_layer,full_source_dims,full_target_dims,orientation);
      else if (mj2_src != NULL)
        { 
          mj2_video_source *track =
            mj2_src->access_video_track((kdu_uint32)(layer_src+1));
          scan->init(track,frame_idx,field_handling,
                     full_source_dims,full_target_dims,orientation);
          
        }
      else
        scan->init(0,-1,KDU_WANT_OUTPUT_COMPONENTS,
                   full_source_dims,full_target_dims,orientation);
    }
  catch (...) {
      remove_ilayer(scan->ilayer_ref,true);
      throw;
    }
  return scan->ilayer_ref;
}

/*****************************************************************************/
/*               kdu_region_compositor::add_primitive_ilayer                 */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::add_primitive_ilayer(int stream_idx,
                                              int &single_component_idx,
                                              kdu_component_access_mode mode,
                                              kdu_dims full_source_dims,
                                              kdu_dims full_target_dims,
                                              bool transpose, bool vflip,
                                              bool hflip)
{
  if (full_target_dims.is_empty())
    full_target_dims = kdu_dims();
      // The above, seemingly innocent operation, ensures that flipping the
      // `full_target_dims' region in any direction will leave the
      // corresponding coordinate of `full_target_dims.pos' equal to 1 (as
      // opposed to 0) which satisfies the expectations of
      // `kdrc_stream::set_scale', as required by `kdrc_layer::init'.
  full_target_dims.from_apparent(transpose,vflip,hflip);
          // Convert to geometry prior to any flipping/transposition

  fixed_composition_dims = kdu_dims();
  if (single_component_idx < 0)
    {
      single_component_idx = -1;
      mode = KDU_WANT_OUTPUT_COMPONENTS;
    }
  
  // Check for valid codestream index
  if (raw_src != NULL)
    {
      if (stream_idx != 0)
        { KDU_ERROR_DEV(e,15); e <<
          KDU_TXT("Invalid codestream index passed to "
                  "`kdu_region_compositor::add_primitive_ilayer'.");
        }
    }
  else if (jpx_src != NULL)
    {
      int available_codestreams;
      bool all_found = jpx_src->count_codestreams(available_codestreams);
      jpx_codestream_source jpx_stream=jpx_src->access_codestream(stream_idx);
      if (!jpx_stream)
        {
          if ((stream_idx >= 0) &&
              ((stream_idx < available_codestreams) || !all_found))
            return kdu_ilayer_ref();
          KDU_ERROR_DEV(e,16); e <<
          KDU_TXT("Invalid codestream index passed to "
                  "`kdu_region_compositor::add_primitive_ilayer'.");
        }
    }
  else
    {
      assert(mj2_src != NULL);
      kdu_uint32 track_idx=0;
      int frame_idx=0, field_idx=0;
      bool can_translate =
        mj2_src->find_stream(stream_idx,track_idx,frame_idx,field_idx);
      if (!can_translate)
        return kdu_ilayer_ref();
      if (track_idx == 0)
        { KDU_ERROR_DEV(e,17); e <<
          KDU_TXT("Invalid codestream index passed to "
                  "`kdu_region_compositor::add_primitive_ilayer'.");
        }
      mj2_video_source *mj2_track = mj2_src->access_video_track(track_idx);
      if (mj2_track == NULL)
        return kdu_ilayer_ref(); // Track is not yet ready to be opened
      mj2_track->seek_to_frame(frame_idx);
      if (!mj2_track->can_open_stream(field_idx))
        return kdu_ilayer_ref(); // Codestream main header not yet available
    }

  // Look for the layer on the inactive list
  kdrc_layer *scan, *prev;
  for (prev=NULL, scan=inactive_layers; scan != NULL;
       prev=scan, scan=scan->next)
    if ((scan->colour_init_src < 0) &&
        (scan->direct_codestream_idx == stream_idx) &&
        (scan->direct_component_idx == single_component_idx))
      { // Found it; remove it from the list
        if (prev == NULL)
          inactive_layers = scan->next;
        else
          prev->next = scan->next;
        scan->next = scan->prev = NULL;
        break;
      }

  bool need_layer_init=false;
  if (scan == NULL)
    { // Create the layer from scratch
      scan = new kdrc_layer(this);
      need_layer_init = true;
    }

  // Append to the end of the active layers list and complete the configuration
  if (last_active_layer == NULL)
    {
      assert(active_layers == NULL);
      active_layers = last_active_layer = scan;
    }
  else
    {
      scan->prev = last_active_layer;
      last_active_layer->next = scan;
      last_active_layer = scan;
    }

  composition_invalid = true;
  try {
      if (!need_layer_init)
        scan->activate(full_source_dims,full_target_dims,
                       jpx_composited_orientation(transpose,vflip,hflip),0,0);
      else
        {
          scan->init(stream_idx,single_component_idx,mode,
                     full_source_dims,full_target_dims,
                     jpx_composited_orientation(transpose,vflip,hflip));
          single_component_idx = scan->direct_component_idx;
        }
    }
  catch (...) {
      remove_ilayer(scan->ilayer_ref,true);
      throw;
    }
  return scan->ilayer_ref;
}

/*****************************************************************************/
/*                 kdu_region_compositor::change_ilayer_frame                */
/*****************************************************************************/

bool
  kdu_region_compositor::change_ilayer_frame(kdu_ilayer_ref ilayer_ref,
                                             int new_frame_idx)
{
  if (mj2_src == NULL)
    return (new_frame_idx == 0);
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    if (scan->ilayer_ref == ilayer_ref)
      break;
  if (scan == NULL)
    { KDU_ERROR_DEV(e,12); e <<
        KDU_TXT("The `ilayer_ref' instance supplied to "
        "`kdu_region_compositor::change_ilayer_frame', does not "
        "correspond to any currently active imagery layer.");
    }
  processing_complete = false;
  return scan->change_frame(new_frame_idx,false);
}

/*****************************************************************************/
/*                     kdu_region_compositor::set_frame                      */
/*****************************************************************************/

void
  kdu_region_compositor::set_frame(jpx_frame_expander *expander,
                                   kdu_coords offset)
{
  jpx_composition composition;
  if ((jpx_src == NULL) || !(composition = jpx_src->access_composition()))
    { KDU_ERROR_DEV(e,13); e <<
        KDU_TXT("Invoking `kdu_region_compositor::set_frame' on an "
        "object whose data source does not offer any composition or "
        "animation instructions!");
    }

  fixed_composition_dims.pos = kdu_coords(0,0);
  composition.get_global_info(fixed_composition_dims.size);
  if (offset != kdu_coords(0,0))
    { 
      kdu_dims this_frame_dims = fixed_composition_dims;
      this_frame_dims.pos = offset;
      fixed_composition_dims.augment(this_frame_dims);
    }
  if (expander == NULL)
    return;
  
  // Remove all current imagery layers non-permanently
  remove_ilayer(kdu_ilayer_ref(),false);

  // Find the relevant compositing layers
  int m, num_frame_members = expander->get_num_members();
  for (m=0; m < num_frame_members; m++)
    { 
      int layer_idx=0, instruction_idx;
      kdu_dims source_dims, target_dims;
      jpx_composited_orientation orientation;
      bool covers_composition;
      expander->get_member(m,instruction_idx,layer_idx,covers_composition,
                           source_dims,target_dims,orientation);    
      jpx_layer_source layer = jpx_src->access_layer(layer_idx,false);
      if (!layer)
        { KDU_ERROR_DEV(e,14); e <<
            KDU_TXT("Attempting to invoke `set_frame' with a "
            "`jpx_frame_expander' object whose `construct' function did not "
            "return true when you invoked it.");
        }
      if (target_dims.is_empty())
        continue; // Should not happen

      // Now add the layer to the tail of the compositing list
      kdrc_layer *scan, *prev;
          
      // Look on the inactive list first
      for (prev=NULL, scan=inactive_layers; scan != NULL;
           prev=scan, scan=scan->next)
        if (scan->colour_init_src == layer_idx)
          { // Found it; just remove it from the list for now
            if (prev == NULL)
              inactive_layers = scan->next;
            else
              prev->next = scan->next;
            scan->next = scan->prev = NULL;
            break;
          }
          
      bool need_layer_init = false;
      if (scan == NULL)
        { // Create a new layer from scratch
          need_layer_init = true;
          scan = new kdrc_layer(this);
        }
    
      // Add the new layer to the top of the active list
      scan->next = active_layers;
      if (active_layers == NULL)
        {
          assert(last_active_layer == NULL);
          last_active_layer = active_layers = scan;
        }
      else
        {
          assert(last_active_layer != NULL);
          active_layers->prev = scan;
          active_layers = scan;
        }

      target_dims.pos += offset; // Must do this before `from_apparent'
      target_dims.from_apparent(orientation.transpose,
                                orientation.vflip,orientation.hflip);
      
      // Perform initialization/reactivation
      try {
          if (need_layer_init)
            scan->init(layer,source_dims,target_dims,orientation);
          else
            scan->activate(source_dims,target_dims,orientation,0,0);
        }
      catch (...) {
          remove_ilayer(scan->ilayer_ref,true);
          throw;
        }
      composition_invalid = true;
    }
}

/*****************************************************************************/
/*                     kdu_region_compositor::add_frame                      */
/*****************************************************************************/

bool
  kdu_region_compositor::add_frame(jpx_frame_expander *expander,
                                   kdu_coords offset)
{
  jpx_composition composition;
  if ((jpx_src == NULL) || fixed_composition_dims.is_empty() ||
      !(composition = jpx_src->access_composition()))
    { KDU_ERROR_DEV(e,0x03021501); e <<
        KDU_TXT("Invoking `kdu_region_compositor::add_frame' without first "
                "calling `set_frame'.  Multi-frame composition must always "
                "commence with `set_frame'.");
    }
  
  kdu_dims new_frame_dims;
  new_frame_dims.pos = offset;
  composition.get_global_info(new_frame_dims.size);
  fixed_composition_dims.augment(new_frame_dims);
  if (expander == NULL)
    return true;
  
  // Find the relevant compositing layers
  bool have_incomplete_layers = false;
  int m, num_frame_members = expander->get_num_members();
  for (m=0; m < num_frame_members; m++)
    { 
      int layer_idx=0, instruction_idx;
      kdu_dims source_dims, target_dims;
      jpx_composited_orientation orientation;
      bool covers_composition;
      expander->get_member(m,instruction_idx,layer_idx,covers_composition,
                           source_dims,target_dims,orientation);    
      jpx_layer_source layer = jpx_src->access_layer(layer_idx,false);
      if ((!layer) || target_dims.is_empty())
        { // If `expander's constructor returned true, there should be no
          // empty `target_dims' regions returned by `get_member' and this
          // avoids possible adverse interactions between the `offset'
          // that needs to be introduced and the way in which the
          // `kdrc_stream::set_scale' function works out how to fill in
          // empty `target_dims' values, taking orientation transforms
          // into account.
          have_incomplete_layers = true;
          continue;
        }

      // Now add the layer to the tail of the compositing list
      kdrc_layer *scan, *prev;
          
      // Look on the inactive list first
      for (prev=NULL, scan=inactive_layers; scan != NULL;
           prev=scan, scan=scan->next)
        if (scan->colour_init_src == layer_idx)
          { // Found it; just remove it from the list for now
            if (prev == NULL)
              inactive_layers = scan->next;
            else
              prev->next = scan->next;
            scan->next = scan->prev = NULL;
            break;
          }
          
      bool need_layer_init = false;
      if (scan == NULL)
        { // Create a new layer from scratch
          need_layer_init = true;
          scan = new kdrc_layer(this);
        }
    
      // Add the new layer to the top of the active list
      scan->next = active_layers;
      if (active_layers == NULL)
        {
          assert(last_active_layer == NULL);
          last_active_layer = active_layers = scan;
        }
      else
        {
          assert(last_active_layer != NULL);
          active_layers->prev = scan;
          active_layers = scan;
        }
    
      target_dims.pos += offset; // Must do this before `from_apparent'
      target_dims.from_apparent(orientation.transpose,
                                orientation.vflip,orientation.hflip);
      
      // Perform initialization/reactivation
      try {
          if (need_layer_init)
            scan->init(layer,source_dims,target_dims,orientation);
          else
            scan->activate(source_dims,target_dims,orientation,0,0);
        }
      catch (...) {
          remove_ilayer(scan->ilayer_ref,true);
          throw;
        }
      composition_invalid = true;
    }
  
  return !have_incomplete_layers;
}

/*****************************************************************************/
/*             kdu_region_compositor::waiting_for_stream_headers             */
/*****************************************************************************/

bool
  kdu_region_compositor::waiting_for_stream_headers()
{
  kdrc_layer *lscan;
  for (lscan=active_layers; lscan != NULL; lscan=lscan->next)
    if (!lscan->had_codestream_headers())
      return true;
  return false;
}

/*****************************************************************************/
/*                    kdu_region_compositor::remove_ilayer                   */
/*****************************************************************************/

bool
  kdu_region_compositor::remove_ilayer(kdu_ilayer_ref ilayer_ref,
                                       bool permanent)
{
  bool found_something = false;
  
  // Move all matching layers to the inactive list first
  kdrc_layer *prev, *scan, *next;
  for (prev=NULL, scan=active_layers; scan != NULL; prev=scan, scan=next)
    {
      next = scan->next;
      if ((scan->ilayer_ref == ilayer_ref) || !ilayer_ref)
        { // Move to inactive list
          found_something = true;
          composition_invalid = true;
          scan->deactivate();
          if (prev == NULL)
            {
              assert(scan == active_layers);
              active_layers = next;
            }
          else
            prev->next = next;
          if (next == NULL)
            {
              assert(scan == last_active_layer);
              last_active_layer = prev;
            }
          else
            {
              assert(scan != last_active_layer);
              next->prev = prev;
            }

          scan->prev = NULL;
          scan->next = inactive_layers;
          inactive_layers = scan;
          scan = prev; // so that `prev' does not change
        }
    }
  if (!permanent)
    return found_something;

  // Delete all matching layers from the inactive list
  for (prev=NULL, scan=inactive_layers; scan != NULL; prev=scan, scan=next)
    {
      next = scan->next;
      if ((scan->ilayer_ref == ilayer_ref) || !ilayer_ref)
        { // Move to inactive list
          found_something = true;
          if (prev == NULL)
            inactive_layers = next;
          else
            prev->next = next;
          delete scan;
          scan = prev; // so that `prev' does not change
        }
    }
  return found_something;
}

/*****************************************************************************/
/*               kdu_region_compositor::cull_inactive_ilayers                */
/*****************************************************************************/

void
  kdu_region_compositor::cull_inactive_ilayers(int max_inactive)
{
  kdrc_layer *scan, *prev;
  for (prev=NULL, scan=inactive_layers;
       (scan != NULL) && (max_inactive > 0);
       prev=scan, scan=scan->next, max_inactive--);
  while (scan != NULL)
    {
      if (prev == NULL)
        inactive_layers = scan->next;
      else
        prev->next = scan->next;
      delete scan;
      scan = (prev==NULL)?inactive_layers:prev->next;
    }
}

/*****************************************************************************/
/*                      kdu_region_compositor::set_scale                     */
/*****************************************************************************/

void
  kdu_region_compositor::set_scale(bool transpose, bool vflip,
                                   bool hflip, float scale,
                                   float  rendering_scale_adjustment)
{
  float r_scale = scale * rendering_scale_adjustment;
  invalid_scale_code = 0;
  bool no_change = have_valid_scale &&
      (transpose == this->transpose) && (vflip == this->vflip) &&
      (hflip == this->hflip) && (scale == this->notional_scale) &&
      (r_scale == this->rendering_scale);
  if ((!composition_invalid) && no_change)
    return; // No change
  this->transpose = transpose;
  this->vflip = vflip;
  this->hflip = hflip;
  this->notional_scale = scale;
  this->rendering_scale = r_scale;
  have_valid_scale = true;
  composition_invalid = true;
  scaled_overlay_params_invalid = true;
  if (!no_change)
    { 
      notional_buffer_region = kdu_dims();
      rendering_buffer_region = kdu_dims();
      refresh_mgr->reset();
    }
}

/*****************************************************************************/
/*            kdu_region_compositor::update_scaled_overlay_params            */
/*****************************************************************************/

bool kdu_region_compositor::update_scaled_overlay_params()
{ 
  float factor = rendering_scale / notional_scale;
  scaled_overlay_max_painting_border = (int)
    ceil(factor * nominal_overlay_max_painting_border);
  float inv_factor = 1.0f / factor;

  // Figure out how many auxiliary parameters there will be after scaling
  int num_params, row_len, num_scaled_params = 0;
  kdu_uint32 *params;
  for (num_params=nominal_overlay_num_aux_params,
       params=nominal_overlay_aux_params;
       num_params > 0;
       num_params-=row_len, params+=row_len)
    { // process a row
      int Bm = (int)params[0];
      if ((Bm == 0) || (num_params < (3+Bm)))
        { 
          num_scaled_params += 2;
          break;
        }
      row_len = 3 + Bm;
      int scaled_Bm = (int)(factor*Bm+0.5f);
      if (scaled_Bm == 0)
        { 
          num_scaled_params += 2;
          break;
        }
      else
        num_scaled_params += 3+scaled_Bm;
    }
  bool result = false;
  if (num_scaled_params != scaled_overlay_num_aux_params)
    { // Resize the array
      result = true;
      if (scaled_overlay_aux_params != NULL)
        delete[] scaled_overlay_aux_params;
      scaled_overlay_aux_params = NULL;
      scaled_overlay_num_aux_params = num_scaled_params;
      if (num_scaled_params > 0)
        scaled_overlay_aux_params = new kdu_uint32[num_scaled_params];
    }
  
  // Generate the scaled auxiliary parameters
  kdu_uint32 *scaled_params;
  for (num_params=nominal_overlay_num_aux_params,
       params=nominal_overlay_aux_params,
       scaled_params=scaled_overlay_aux_params;
       num_params > 0;
       num_params-=row_len, params+=row_len)
    { 
      int Bm = (int)params[0];
      if ((Bm == 0) || (num_params < (3+Bm)))
        { 
          *(scaled_params++) = 0;
          *(scaled_params++) = (num_params>1)?params[1]:0;
          break;
        }
      int scaled_Bm = (int)(factor*Bm+0.5f);      
      *(scaled_params++) = (kdu_uint32)scaled_Bm;
      *(scaled_params++) = params[1]; // Interior colour preserved
      if (scaled_Bm == 0)
        break;
      row_len = 3 + Bm;
      float pos = 0.5f*inv_factor;
      int p, src_p;
      for (p=src_p=0; p < scaled_Bm; p++, pos+=inv_factor)
        { 
          while (pos > 0.5f)
            { pos-=1.0f; src_p++; }
          if (src_p >= Bm) src_p = Bm-1; // Just in case
          *(scaled_params++) = params[2+src_p];
        }
      *(scaled_params++) = (kdu_uint32)
        ceil(factor * params[row_len-1]);
    }
  assert(scaled_params ==
         (scaled_overlay_aux_params+scaled_overlay_num_aux_params));
  return result;
}

/*****************************************************************************/
/*                  kdu_region_compositor::set_buffer_surface                */
/*****************************************************************************/

void
  kdu_region_compositor::set_buffer_surface(kdu_dims region,
                                            kdu_int32 bckgnd)
{
  kdu_uint32 background = (kdu_uint32) bckgnd;
  
  bool start_from_scratch =
    (composition_buffer != NULL) && (background != buffer_background);
  buffer_background = background;

  if (composition_invalid)
    { 
      notional_buffer_region = region;
      rendering_buffer_region = kdu_dims(); // Will be derived from current
                          // buffer region when we update the configuration.
      return; // Defer further processing until we update the configuration
    }

  kdu_dims old_rendering_region = this->rendering_buffer_region;
  notional_buffer_region = region & notional_composition_dims;
  derive_rendering_buffer_region();
  if (composition_buffer != NULL)
    { 
      kdu_compositor_buf *old_buffer = composition_buffer;
      kdu_coords new_size = rendering_buffer_region.size;
      if ((new_size.x > buffer_size.x) || (new_size.y > buffer_size.y))
        composition_buffer =
          internal_allocate_buffer(new_size,buffer_size,true);
      composition_buffer->set_geometry(rendering_scale,
                                       rendering_buffer_region,
                                       rendering_composition_dims);
      if (!start_from_scratch)
        initialize_buffer_surface(composition_buffer,rendering_buffer_region,
                                  old_buffer,old_rendering_region,
                                  buffer_background,
                                  can_skip_surface_initialization);
      else if (!can_skip_surface_initialization)
        initialize_buffer_surface(composition_buffer,rendering_buffer_region,
                                  NULL,kdu_dims(),buffer_background);
      if (old_buffer != composition_buffer)
        internal_delete_buffer(old_buffer);
    }
  set_layer_buffer_surfaces();

  // Adjust the `refresh_mgr' object to include any new regions.
  kdu_dims common_region = old_rendering_region & rendering_buffer_region;
  if (start_from_scratch || !common_region)
    {
      refresh_mgr->reset();
      refresh_mgr->add_region(rendering_buffer_region);
    }
  else
    { // Adjust what is still to be refreshed, and add new elements based on
      // the portion of the buffer which is new
      refresh_mgr->adjust(rendering_buffer_region);
      kdu_coords common_min = common_region.pos;
      kdu_coords common_lim = common_min + common_region.size;
      kdu_coords new_min = rendering_buffer_region.pos;
      kdu_coords new_lim = new_min + rendering_buffer_region.size;
      int needed_left = common_min.x - new_min.x;
      int needed_right = new_lim.x - common_lim.x;
      int needed_above = common_min.y - new_min.y;
      int needed_below = new_lim.y - common_lim.y;
      assert((needed_left >= 0) && (needed_right >= 0) &&
             (needed_above >= 0) && (needed_below >= 0));
      kdu_dims region_left = common_region;
      region_left.pos.x = new_min.x; region_left.size.x = needed_left;
      kdu_dims region_right = common_region;
      region_right.pos.x = common_lim.x; region_right.size.x = needed_right;
      kdu_dims region_above = rendering_buffer_region;
      region_above.pos.y = new_min.y; region_above.size.y = needed_above;
      kdu_dims region_below = rendering_buffer_region;
      region_below.pos.y = common_lim.y; region_below.size.y = needed_below;
      if (!region_left.is_empty())
        refresh_mgr->add_region(region_left);
      if (!region_right.is_empty())
        refresh_mgr->add_region(region_right);
      if (!region_above.is_empty())
        refresh_mgr->add_region(region_above);
      if (!region_below.is_empty())
        refresh_mgr->add_region(region_below);
    }

  if (!can_skip_surface_initialization)
    { // If we are skipping surface initialization, all erasing/compositing is
      // done at the end, after decompression and overlay generation, so we
      // will not be invoking `find_completed_rects' during `process' and we
      // want to make sure that the erasing/compositing is performed for all
      // regions added to the refresh manager above.  Otherwise, we are going
      // to be incrementally adding regions to the refresh manager as imager
      // is decompressed or overlay segments are processed, so we will remove
      // those regions now, thereby ensuring that compositing can proceed as
      // soon as all relevant image layers are available, but no sooner.
      kdrc_stream *sp;
      for (sp=streams; sp != NULL; sp=sp->next)
        if (sp->is_active)
          refresh_mgr->adjust(sp);
    }
}

/*****************************************************************************/
/*                  kdu_region_compositor::find_optimal_scale                */
/*****************************************************************************/

float
  kdu_region_compositor::find_optimal_scale(kdu_dims region,
                                            float anchor_scale,
                                            float min_scale, float max_scale,
                                            kdu_istream_ref *istream_ref,
                                            int *component_idx,
                                            bool avoid_subsampling)
{
  kdrc_stream *best_stream=NULL;
  kdrc_layer *scan, *best_layer=NULL;
  kdu_long layer_area, best_layer_area=0;
  float min_supported_scale=-1.0F, max_supported_scale=-1.0F;

  // Start by determining whether or not a global compositing frame limits
  // the maximum scale
  if (!fixed_composition_dims.is_empty())
    {
      kdu_coords frame_lim =
        fixed_composition_dims.pos+fixed_composition_dims.size;
      int max_lim = (frame_lim.x > frame_lim.y)?frame_lim.x:frame_lim.y;
      if (max_lim > 0)
        max_supported_scale = 0x7FFF0000 / ((float) max_lim);
    }
  
  if (active_layers != NULL)
    {
      for (scan=active_layers; scan != NULL; scan=scan->next)
        scan->find_supported_scales(min_supported_scale,max_supported_scale);
      if (region.is_empty() || !have_valid_scale)
        best_layer = last_active_layer;
      else
        for (scan=last_active_layer; scan != NULL; scan=scan->prev)
          {
            layer_area = scan->measure_visible_area(region,false);
            if ((best_layer == NULL) || (layer_area > best_layer_area))
              { best_layer_area = layer_area;  best_layer = scan; }
          }
      if (best_layer != NULL)
        {
          best_stream = best_layer->get_stream(0);
          if (best_stream == NULL)
            best_layer = NULL;
        }
    }
  
  float result;
  if (best_layer != NULL)
    result = best_layer->find_optimal_scale(anchor_scale,min_scale,max_scale,
                                            avoid_subsampling);
  else
    {
      if (anchor_scale < min_scale)
        result = min_scale;
      else if (anchor_scale > max_scale)
        result = max_scale;
      else
        result = anchor_scale;
    }
  if (result < min_supported_scale)
    result = min_supported_scale;
  if ((max_supported_scale > 0.0F) && (result > max_supported_scale))
    result = max_supported_scale;

  if (best_stream == NULL)
    {
      if (istream_ref != NULL)
        *istream_ref = kdu_istream_ref();
      if (component_idx != NULL)
        *component_idx = -1;
    }
  else
    { 
      if (istream_ref != NULL)
        *istream_ref = best_stream->istream_ref;
      if (component_idx != NULL)
        *component_idx = best_stream->get_primary_component_idx();
    }

  return result;
}

/*****************************************************************************/
/*                  kdu_region_compositor::get_num_ilayers                   */
/*****************************************************************************/

int
  kdu_region_compositor::get_num_ilayers()
{
  int count = 0;
  for (kdrc_layer *scan=active_layers; scan != NULL; scan=scan->next)
    count++;
  return count;
}

/*****************************************************************************/
/*                kdu_region_compositor::assign_new_ilayer_ref               */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::assign_new_ilayer_ref()
{
  kdu_ilayer_ref result = last_assigned_ilayer_ref;
  bool unique = false;
  while (!unique)
    { 
      result.ref++;
      unique = !result.is_null();
      kdrc_layer *scan;
      for (scan=active_layers; unique && (scan != NULL); scan=scan->next)
        if (scan->ilayer_ref == result)
          unique = false;
      for (scan=inactive_layers; unique && (scan != NULL); scan=scan->next)
        if (scan->ilayer_ref == result)
          unique = false;
    }
  last_assigned_ilayer_ref = result;
  return result;
}

/*****************************************************************************/
/*               kdu_region_compositor::assign_new_istream_ref               */
/*****************************************************************************/

kdu_istream_ref
  kdu_region_compositor::assign_new_istream_ref()
{
  kdu_istream_ref result = last_assigned_istream_ref;
  bool unique = false;
  while (!unique)
    { 
      result.ref++;
      unique = !result.is_null();
      kdrc_stream *scan;
      for (scan=streams; unique && (scan != NULL); scan=scan->next)
        if (scan->istream_ref == result)
          unique = false;
    }
  last_assigned_istream_ref = result;
  return result;
}

/*****************************************************************************/
/*                  kdu_region_compositor::add_active_stream                 */
/*****************************************************************************/

kdrc_stream *
  kdu_region_compositor::add_active_stream(int codestream_idx,
                                           int colour_init_src,
                                           bool single_component_only,
                                           bool alpha_only)
{  
  // Search for an existing inactive stream we can use
  kdrc_stream *scan, *sibling=NULL;
  for (scan=streams; scan != NULL; scan=scan->next)
    { 
      if (scan->codestream_idx != codestream_idx)
        continue;
      assert(scan->layer != NULL);
      sibling = scan; // Found an istream which shares the same codestream
      if (scan->is_active)
        continue; // Only steal inactive streams
      if (colour_init_src < 0)
        {
          if (single_component_only || (scan->colour_init_src < 0))
            break; // Found compatible istream
        }
      else if (colour_init_src == scan->colour_init_src)
        break;
    }
  
  if (scan != NULL)
    { // Make this stream active, assigning a new `istream_ref'
      if (scan->codestream == NULL)
        { KDU_ERROR_DEV(e,20); e <<
          KDU_TXT("Attempting to open a codestream which has "
                  "already been found to contain an error.");
        }
      scan->is_active = true;
      scan->istream_ref = assign_new_istream_ref();
      scan->layer->disconnect_stream(scan);
      scan->codestream->move_to_head(scan);
      return scan;      
    }
  
  // Create a new istream from scratch
  jpx_codestream_source jpx_stream;
  mj2_video_source *mj2_track = NULL;
  int mj2_frame_idx=0, mj2_field_idx=0;
  if (jpx_src != NULL)
    jpx_stream = jpx_src->access_codestream(codestream_idx,false);
  else if (mj2_src != NULL)
    { 
      kdu_uint32 track_idx=0;
      mj2_src->find_stream(codestream_idx,track_idx,mj2_frame_idx,
                           mj2_field_idx);
      mj2_track = mj2_src->access_video_track(track_idx);
      assert(mj2_track != NULL);
      sibling = NULL; // Do not share codestreams between multiple active
                      // motion ilayers, since we need to be able to change
                      // the frame index separately for each motion istream.
    }
  else
    assert(raw_src != NULL);
  
  bool init_failed = false;
  scan = new kdrc_stream(this,persistent_codestreams,
                         codestream_cache_threshold,env,env_queue);
  scan->is_active = true;
  if (colour_init_src < 0)
    { // Create istream for direct codestream access only
      kdrc_codestream *new_cs = NULL;
      if (sibling == NULL)
        { 
          new_cs = new kdrc_codestream(persistent_codestreams,
                                       codestream_cache_threshold);
          try {
            if (raw_src != NULL)
              new_cs->init(raw_src,env);
            else if (jpx_stream.exists())
              new_cs->init(jpx_stream,env);
            else if (mj2_track != NULL)
              new_cs->init(mj2_track,mj2_frame_idx,mj2_field_idx,env);
            else
              init_failed = true;
          } catch (...) {
            init_failed = true;
            delete new_cs;
            new_cs = NULL;
          }
        }
      if (!init_failed)          
        scan->init(new_cs,sibling);
      scan->codestream_idx = codestream_idx;
      assert(scan->colour_init_src < 0);
    }
  else if (jpx_src != NULL)
    { 
      jpx_layer_source jpx_layer=jpx_src->access_layer(colour_init_src,false);
      try {
        scan->init(jpx_stream,jpx_layer,jpx_src,alpha_only,sibling,
                   overlay_log2_segment_size);
      } catch (...) { 
        init_failed = true;
      }
      assert(scan->colour_init_src == colour_init_src);
    }
  else if (mj2_src != NULL)
    { 
      try {
        scan->init(mj2_track,mj2_frame_idx,mj2_field_idx,sibling);
      } catch (...) { init_failed = true; }
      assert(scan->colour_init_src == colour_init_src);
    }
  else
    assert(0);
  
  // Set parameters
  if (init_failed)
    {
      delete scan; // Automatically unlinks it
      return NULL;
    }

  scan->set_colour_order(colour_order);
  scan->set_error_level(error_level);
  scan->set_max_quality_layers(max_quality_layers);
  return scan;
}

/*****************************************************************************/
/*                  kdu_region_compositor::get_next_istream                  */
/*****************************************************************************/

kdu_istream_ref
  kdu_region_compositor::get_next_istream(kdu_istream_ref last_istream_ref,
                                          bool only_active_istreams,
                                          bool no_duplicates,
                                          int codestream_idx)
{
  kdrc_stream *scan = streams;
  if (!last_istream_ref.is_null())
    for (; scan != NULL; scan=scan->next)
      if (scan->istream_ref == last_istream_ref)
        { 
          scan = scan->next;
          break;
        }
  for (; scan != NULL; scan=scan->next)
    {
      if ((codestream_idx >= 0) && (scan->codestream_idx != codestream_idx))
        continue;
      if (only_active_istreams && !scan->is_active)
        continue;
      if ((!no_duplicates) || (scan->codestream->head == scan))
        return scan->istream_ref;
    }
  return kdu_istream_ref();    
}

/*****************************************************************************/
/*                  kdu_region_compositor::get_next_ilayer                   */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::get_next_ilayer(kdu_ilayer_ref last_ilayer_ref,
                                         int layer_src, int direct_cs_idx)
{
  kdrc_layer *scan=last_active_layer;
  if (!last_ilayer_ref.is_null())
    for (; scan != NULL; scan=scan->prev)
      if (scan->ilayer_ref == last_ilayer_ref)
        { 
          scan = scan->prev;
          break;
        }
  if (raw_src != NULL)
    layer_src = -1;
  for (; scan != NULL; scan=scan->prev)
    {
      if ((layer_src >= 0) && (scan->colour_init_src != layer_src))
        continue;
      if ((direct_cs_idx >= 0) &&
          (scan->direct_codestream_idx != direct_cs_idx))
        continue;
      return scan->ilayer_ref;
    }
  return kdu_ilayer_ref();
}

/*****************************************************************************/
/*               kdu_region_compositor::get_next_visible_ilayer              */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::get_next_visible_ilayer(
                                             kdu_ilayer_ref last_ilayer_ref,
                                             kdu_dims region)
{
  kdrc_layer *scan = active_layers;
  if (!last_ilayer_ref.is_null())
    for (; scan != NULL; scan=scan->next)
      if (scan->ilayer_ref == last_ilayer_ref)
        { 
          scan = scan->next;
          break;
        }
  for (; scan != NULL; scan=scan->next)
    if (scan->measure_visible_area(region,true) > 0)
      return scan->ilayer_ref;
  return kdu_ilayer_ref();
}

/*****************************************************************************/
/*                  kdu_region_compositor::access_codestream                 */
/*****************************************************************************/

kdu_codestream
  kdu_region_compositor::access_codestream(kdu_istream_ref istream_ref)
{
  kdu_codestream result;
  if (istream_ref.is_null())
    return result;
  kdrc_stream *scan = streams;
  for (; scan != NULL; scan=scan->next)
    if (scan->istream_ref == istream_ref)
      break;
  if (scan != NULL)
    result = scan->codestream->ifc;
  for (; scan != NULL; scan=scan->next_codestream_user)
    scan->stop_processing();
  return result;
}

/*****************************************************************************/
/*                   kdu_region_compositor::get_istream_info                 */
/*****************************************************************************/

int
  kdu_region_compositor::get_istream_info(kdu_istream_ref istream_ref,
                                          int &codestream_idx,
                                          kdu_ilayer_ref *ilayer_ref,
                                          int *components_in_use,
                                          int max_components_in_use,
                                          int *principle_component_idx,
                                          float *principle_component_scale_x,
                                          float *principle_component_scale_y,
                                          bool *transpose_val,
                                          bool *vflip_val, bool *hflip_val)
{
  if (istream_ref.is_null())
    return 0;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->istream_ref == istream_ref)
      break;
  if ((scan == NULL) || !scan->is_active)
    return 0;
  codestream_idx = scan->codestream_idx;
  if (ilayer_ref != NULL)
    *ilayer_ref = scan->layer->ilayer_ref;
  if (principle_component_idx != NULL)
    *principle_component_idx = scan->get_primary_component_idx();
  if ((principle_component_scale_x != NULL) ||
      (principle_component_scale_y != NULL))
    { 
      double scale_x=1.0, scale_y=1.0;
      scan->layer->get_component_scale_factors(scan,scale_x,scale_y);
      if (principle_component_scale_x != NULL)
        *principle_component_scale_x = (float) scale_x;
      if (principle_component_scale_y != NULL)
        *principle_component_scale_y = (float) scale_y;
    }
  if ((transpose_val != NULL) || (vflip_val != NULL) || (hflip_val != NULL))
    { 
      jpx_composited_orientation orientation =
        scan->layer->get_src_orientation();
      if (transpose_val != NULL)
        *transpose_val = orientation.transpose;
      if (vflip_val != NULL)
        *vflip_val = orientation.vflip;
      if (hflip_val != NULL)
        *hflip_val = orientation.hflip;
    }
  int indices[4];
  int result = scan->get_components_in_use(indices);
  if (components_in_use == NULL)
    return result;
  for (int n=0; n < max_components_in_use; n++)
    { 
      if ((n < result) || (n < -result))
        components_in_use[n] = indices[n];
      else
        components_in_use[n] = -1;
    }
  return result;
}

/*****************************************************************************/
/*                  kdu_region_compositor::get_ilayer_info                   */
/*****************************************************************************/

int
  kdu_region_compositor::get_ilayer_info(kdu_ilayer_ref ilayer_ref,
                                         int &layer_src,
                                         int &direct_codestream_idx,
                                         bool &is_opaque,
                                         int *frame_idx, int *field_handling)
{
  if (ilayer_ref.is_null())
    return 0;
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    if (scan->ilayer_ref == ilayer_ref)
      break;
  if (scan == NULL)
    return 0;
  if (raw_src != NULL)
    layer_src = direct_codestream_idx = 0;
  else
    {
      layer_src = scan->colour_init_src;
      direct_codestream_idx = scan->direct_codestream_idx;
    }
  is_opaque = !scan->have_alpha_channel;
  if (frame_idx != NULL)
    *frame_idx = scan->mj2_frame_idx;
  if (field_handling != NULL)
    *field_handling = scan->mj2_field_handling;
  return scan->get_num_streams();
}
  
/*****************************************************************************/
/*                 kdu_region_compositor::get_ilayer_stream                  */
/*****************************************************************************/

kdu_istream_ref
  kdu_region_compositor::get_ilayer_stream(kdu_ilayer_ref ilayer_ref,
                                           int which, int codestream_idx)
{
  kdu_istream_ref result;
  if (ilayer_ref.is_null())
    return result;
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    if (scan->ilayer_ref == ilayer_ref)
      break;
  if (scan != NULL)
    {
      kdrc_stream *stream = NULL;
      if (which >= 0)
        stream = scan->get_stream(which);
      else
        { 
          for (which=0; (stream=scan->get_stream(which)) != NULL; which++)
            if (stream->codestream_idx == codestream_idx)
              break;
        }
      if (stream != NULL)
        result = stream->istream_ref;
    }
  return result;
}
  
/*****************************************************************************/
/*               kdu_region_compositor::get_codestream_packets               */
/*****************************************************************************/

bool
  kdu_region_compositor::get_codestream_packets(kdu_istream_ref istream_ref,
                                          kdu_dims region,
                                          kdu_long &visible_precinct_samples,
                                          kdu_long &visible_packet_samples,
                                          kdu_long &max_visible_packet_samples,
                                          int max_region_layers)
{
  visible_precinct_samples = 0;
  visible_packet_samples = 0;
  max_visible_packet_samples = 0;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->istream_ref == istream_ref)
      break;
  if ((scan == NULL) || (!scan->is_active) ||
      (!scan->is_scale_valid()) || !have_valid_scale)
    return false;
  region = scan->find_full_notional_region(true) & region;
  if (!region)
    return false;
  if (max_region_layers < 0)
    max_region_layers = 0;
  scan->layer->get_visible_packet_stats(scan,region,max_region_layers,
                                        visible_precinct_samples,
                                        visible_packet_samples,
                                        max_visible_packet_samples);
  return (visible_precinct_samples != 0);
}

/*****************************************************************************/
/*                      kdu_region_compositor::find_point                    */
/*****************************************************************************/

kdu_ilayer_ref
  kdu_region_compositor::find_point(kdu_coords point, int enumerator,
                                    float visibility_threshold)
{
  if (composition_invalid && !update_composition())
    return kdu_ilayer_ref();
  float visibility = 1.0F;
  kdrc_layer *scan;
  for (scan=last_active_layer;
       (scan != NULL) && (visibility > visibility_threshold);
       scan=scan->prev)
    { 
      kdrc_stream *stream = scan->get_stream(0);
      if (stream == NULL)
        continue;
      kdu_dims region = stream->find_full_notional_region(true);
      if ((point.x >= region.pos.x) && (point.y >= region.pos.y) &&
          (point.x < (region.pos.x+region.size.x)) &&
          (point.y < (region.pos.y+region.size.y)))
        { 
          float opacity = scan->get_opacity(point);
          if ((visibility * opacity) <= visibility_threshold)
            { 
              visibility *= 1.0F - opacity; // Looking through this layer
              continue;
            }
          if (enumerator != 0)
            { 
              enumerator--;
              visibility *= 1.0F - opacity; // Looking through this layer
              continue;
            }
          return scan->ilayer_ref;
        }
    }
  return kdu_ilayer_ref();
}

/*****************************************************************************/
/*                      kdu_region_compositor::map_region                    */
/*****************************************************************************/

kdu_istream_ref
  kdu_region_compositor::map_region(kdu_dims &notional_region,
                                    kdu_istream_ref istream_ref)
{
  kdu_istream_ref result;
  if (composition_invalid && !update_composition())
    return result;
  kdrc_stream *strm=NULL;
  if (!istream_ref.is_null())
    {
      for (strm=streams; strm != NULL; strm=strm->next)
        if (strm->istream_ref == istream_ref)
          {
            kdu_dims reg;
            if (strm->is_active &&
                (!(reg = strm->map_region(notional_region,false)).is_empty()))
              {
                notional_region = reg;
                result = strm->istream_ref;
              }
            break;
          }
      return result;
    }
  
  // Invoke on layers in order, so that the top-most matching layer is used
  kdrc_layer *scan;
  for (scan=last_active_layer; scan != NULL; scan=scan->prev)
    if (scan->map_region(notional_region,strm))
      {
        result = strm->istream_ref;
        break;
      }
  return result;
}

/*****************************************************************************/
/*                 kdu_region_compositor::inverse_map_region                 */
/*****************************************************************************/

kdu_dims
  kdu_region_compositor::inverse_map_region(kdu_dims region,
                                            kdu_istream_ref istream_ref)
{
  if (istream_ref.is_null())
    { // Special case of a region which is associated with the renderered
      // result, apart from scale and geometry adjustments
      if (!region)
        return notional_composition_dims;
      region.to_apparent(transpose,vflip,hflip);
      kdu_coords lim = region.pos + region.size;
      region.pos.x = (int) floor(((double) region.pos.x) * notional_scale);
      region.pos.y = (int) floor(((double) region.pos.y) * notional_scale);
      region.size.x = ((int) ceil(((double) lim.x) * notional_scale));
      region.size.y = ((int) ceil(((double) lim.y) * notional_scale));
      region.size -= region.pos;
      return region;
    }
  
  if (composition_invalid && !update_composition())
    return kdu_dims();
  
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->istream_ref == istream_ref)
      {  
        if (!scan->is_active)
          break;
        return scan->inverse_map_region(region);
      }
  return kdu_dims(); // Cannot find a match
}


/*****************************************************************************/
/*                 kdu_region_compositor::find_ilayer_region                 */
/*****************************************************************************/

kdu_dims
  kdu_region_compositor::find_ilayer_region(kdu_ilayer_ref ilayer_ref,
                                            bool apply_cropping)
{
  if (composition_invalid && !update_composition())
    return kdu_dims();
  if (!have_valid_scale)
    return kdu_dims();
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    if (scan->ilayer_ref == ilayer_ref)
      break;
  if (scan == NULL)
    return kdu_dims();
  kdrc_stream *stream;
  kdu_dims result, region;
  for (int w=0; (stream=scan->get_stream(w)) != NULL; w++)
    {
      region = stream->find_full_notional_region(apply_cropping);
      if (w == 0)
        result = region;
      else
        result &= region;
    }
  return result;
}

/*****************************************************************************/
/*                 kdu_region_compositor::find_istream_region                */
/*****************************************************************************/

kdu_dims
  kdu_region_compositor::find_istream_region(kdu_istream_ref istream_ref,
                                             bool apply_cropping)
{
  if (composition_invalid && !update_composition())
    return kdu_dims();
  if (!have_valid_scale)
    return kdu_dims();
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->istream_ref == istream_ref)
      break;
  if (scan == NULL)
    return kdu_dims();
  return scan->find_full_notional_region(apply_cropping);
}

/*****************************************************************************/
/*                      kdu_region_compositor::refresh                       */
/*****************************************************************************/

bool
  kdu_region_compositor::refresh(bool *new_imagery)
{
  if (!persistent_codestreams)
    return true;
  bool have_new_imagery = false; // True if codestream headers newly available
                                 // for codestreams associated with some layer
  bool need_invalidate_composition = false; // True if newly available
       // codestream headers cause a change in the computed dimensions
       // for some layer such that a separate compositing buffer is now
       // needed where it was not before, or the dimensions of the composition
       // buffer, as seen by the application, must change.
  kdrc_layer *lscan;
  if (have_valid_scale)
    { 
      for (lscan=active_layers; lscan != NULL; lscan=lscan->next)
        if ((!lscan->had_codestream_headers()) &&
            lscan->check_codestream_headers())
          { 
            have_new_imagery = true;
            kdu_dims old_lyr_dims = lscan->get_rendering_layer_region();
            lscan->set_scale(transpose,vflip,hflip,notional_scale,
                             rendering_scale,invalid_scale_code,
                             fixed_composition_dims);            
            kdu_dims new_lyr_dims = lscan->get_rendering_layer_region();
            if (!composition_invalid)
              { 
                if ((fixed_composition_dims.is_empty() &&
                     (new_lyr_dims != old_lyr_dims)) ||
                    ((composition_buffer == NULL) &&
                     !new_lyr_dims.contains(rendering_composition_dims)))
                  need_invalidate_composition = true;
              }
          }
    }
    
  if (new_imagery != NULL)
    { 
      *new_imagery = have_new_imagery;
      if (!(*new_imagery))
        return true; // No change in dimensions, but no refreshing to do
    }

  processing_complete = false;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    scan->invalidate_surface();
  for (lscan=active_layers; lscan != NULL; lscan=lscan->next)
    lscan->update_overlay(false,overlay_dependencies);

  if (!composition_invalid)
    { 
      if (need_invalidate_composition)
        {
          composition_invalid = true;
          return false;
        }
      if (have_new_imagery)
        set_layer_buffer_surfaces();
      refresh_mgr->reset();
      refresh_mgr->add_region(rendering_buffer_region);
      if (!can_skip_surface_initialization)
        { // See `set_buffer_surface' for why we don't want to call the
          // `refresh_mgr->adjust' function if surface pre-intialization is
          // off -- this is the case where all erase/composition operations
          // are performed once image/overlay processing is completed and
          // `process' does not call `find_completed_rects'.
          kdrc_stream *sp;
          for (sp=streams; sp != NULL; sp=sp->next)
            if (sp->is_active)
              refresh_mgr->adjust(sp);
        }
    }
  if (initialize_surfaces_on_next_refresh)
    {
      initialize_surfaces_on_next_refresh = false;
      if (!(composition_invalid || !rendering_buffer_region))
        { // Otherwise initialization will take place when buffer is allocated
          if (composition_buffer != NULL)
            initialize_buffer_surface(composition_buffer,
                                      rendering_buffer_region,
                                      NULL,kdu_dims(),buffer_background);
          else if ((active_layers != NULL) && (active_layers->next == NULL))
            initialize_buffer_surface(active_layers->get_layer_buffer(),
                                      rendering_buffer_region,NULL,kdu_dims(),
                                      0xFFFFFFFF);
        }
    }
  return true;
}
  
/*****************************************************************************/
/*                  kdu_region_compositor::update_overlays                   */
/*****************************************************************************/

void
  kdu_region_compositor::update_overlays(bool start_from_scratch)
{
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    {
      scan->update_overlay(start_from_scratch,overlay_dependencies);
      if (scan->have_overlay_info)
        processing_complete = false;
    }
}

/*****************************************************************************/
/*                 kdu_region_compositor::configure_overlays                 */
/*****************************************************************************/

void
  kdu_region_compositor::configure_overlays(bool enable, int min_display_size,
                                            float blending_factor,
                                            int max_painting_border,
                                            jpx_metanode dependency,
                                            int dependency_effect,
                                            const kdu_uint32 *aux_params,
                                            int num_aux_params)
{
  if (max_painting_border > 16)
    max_painting_border = 16; // Avoid reckless allocation of memory later
  bool dependencies_changed = false;
  if ((!dependency) && (dependency_effect == 0))
    { // Delete the dependency expression
      if (overlay_dependencies != NULL)
        {
          dependencies_changed = true;
          delete overlay_dependencies;
          overlay_dependencies = NULL;
        }
    }
  else if ((dependency_effect == 0) &&
           ((overlay_dependencies == NULL) ||
            !overlay_dependencies->equals(dependency)))
    { // Start from scratch
      if (overlay_dependencies != NULL)
        { delete overlay_dependencies; overlay_dependencies = NULL; }
      overlay_dependencies = new kdrc_overlay_expression;
      dependencies_changed =
        overlay_dependencies->add_product_term(dependency,0);
    }
  else if ((dependency_effect == 1) || (dependency_effect == -1))
    { // Add new sum term to the sum-of-products expression
      overlay_dependencies->add_sum_term();
      dependencies_changed =
        overlay_dependencies->add_product_term(dependency,dependency_effect);
    }
  else if ((dependency_effect == 2) || (dependency_effect == -2))
    { // Add new product term to the latest sum term in the expression
      if (overlay_dependencies == NULL)
        overlay_dependencies = new kdrc_overlay_expression;
      dependencies_changed =
        overlay_dependencies->add_product_term(dependency,dependency_effect);
    }
  
  kdu_int16 factor_x128 = 0x7FFF;
  if (blending_factor < 0.0F)
    {
      if (blending_factor > -(((float)(0x07FFF))/128.0F))
        factor_x128 = (kdu_int16) floor(0.5-128.0F*blending_factor);
      factor_x128 = -factor_x128;
    }
  else if (blending_factor < (((float)(0x07FFF))/128.0F))
    factor_x128 = (kdu_int16) floor(0.5+128.0F*blending_factor);
  bool blending_changed = (factor_x128 != this->overlay_factor_x128);
  
  int p;
  bool aux_params_changed = false;
  if ((num_aux_params < 0) || (aux_params == NULL))
    { num_aux_params = 0; aux_params = NULL; }
  if (num_aux_params != this->nominal_overlay_num_aux_params)
    aux_params_changed = true;
  else
    for (p=0; p < num_aux_params; p++)
      if (aux_params[p] != nominal_overlay_aux_params[p])
        { aux_params_changed = true; break; }

  if ((enable == this->enable_overlays) &&
      (max_painting_border == this->nominal_overlay_max_painting_border) &&
      (min_display_size == this->overlay_min_display_size) &&
      (!dependencies_changed) && (!blending_changed) && (!aux_params_changed))
    return;
  this->enable_overlays = enable;
  this->nominal_overlay_max_painting_border = max_painting_border;
  this->overlay_factor_x128 = factor_x128;
  this->overlay_min_display_size = min_display_size;
  if (num_aux_params != this->nominal_overlay_num_aux_params)
    {
      if (nominal_overlay_aux_params != NULL)
        delete[] nominal_overlay_aux_params;
      nominal_overlay_aux_params = NULL;
      nominal_overlay_num_aux_params = num_aux_params;
      if (num_aux_params > 0)
        nominal_overlay_aux_params = new kdu_uint32[num_aux_params];
    }
  if (aux_params_changed)
    { 
      for (p=0; p < num_aux_params; p++)
        nominal_overlay_aux_params[p] = aux_params[p];
      update_scaled_overlay_params();
    }
  if (!composition_invalid)
    { // Otherwise, the ensuing steps will be performed in `update_composition'
      kdrc_layer *scan;
      for (scan=active_layers; scan != NULL; scan=scan->next)
        {
          scan->configure_overlay(enable,overlay_min_display_size,
                                  scaled_overlay_max_painting_border,
                                  overlay_dependencies,
                                  scaled_overlay_aux_params,
                                  scaled_overlay_num_aux_params,
                                  dependencies_changed,aux_params_changed,
                                  blending_changed);
          if (scan->have_overlay_info)
            processing_complete = false;
        }
      if ((composition_buffer != NULL) && !processing_complete)
        composition_buffer->set_invalid(); // Avoid tempting application to
             // do incremental screen refresh if overlays are rendered in
             // multiple calls to `process', since this would cause visual
             // tearing.  Buffer content is not erased but should be treated
             // as undergoing potentially large visual changes.
    }
}

/*****************************************************************************/
/*                   kdu_region_compositor::get_overlay_info                 */
/*****************************************************************************/

bool
  kdu_region_compositor::get_overlay_info(int &total_nodes, int &hidden_nodes)
{
  if (!enable_overlays)
    return false;
  total_nodes = hidden_nodes = 0;
  kdrc_layer *scan;
  for (scan=active_layers; scan != NULL; scan=scan->next)
    scan->get_overlay_info(total_nodes,hidden_nodes);
  return true;
}

/*****************************************************************************/
/*                    kdu_region_compositor::search_overlays                 */
/*****************************************************************************/

jpx_metanode
  kdu_region_compositor::search_overlays(kdu_coords point,
                                         kdu_istream_ref &istream_ref,
                                         float visibility_threshold)
{
  jpx_metanode result;
  if ((active_layers == NULL) || !enable_overlays)
    return result; // Returns empty interface
  kdrc_layer *scan;
  float visibility = 1.0F;
  for (scan=last_active_layer;
       (scan != NULL) && (visibility > visibility_threshold);
       scan=scan->prev)
    {
      bool is_opaque;
      kdrc_stream *stream=NULL;
      result = scan->search_overlay(point,stream,is_opaque);
      if (result.exists())
        {
          istream_ref = stream->istream_ref;
          break;
        }
      if (is_opaque)
        break;
      visibility *= 1.0F - scan->get_opacity(point);
    }
  return result;
}

/*****************************************************************************/
/*                   kdu_region_compositor::halt_processing                  */
/*****************************************************************************/

void
  kdu_region_compositor::halt_processing()
{
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->is_active)
      scan->stop_processing();
}

/*****************************************************************************/
/*              kdu_region_compositor::get_total_composition_dims            */
/*****************************************************************************/

bool
  kdu_region_compositor::get_total_composition_dims(kdu_dims &dims)
{
  if (composition_invalid && !update_composition())
    return false;
  dims = notional_composition_dims;
  return true;
}

/*****************************************************************************/
/*               kdu_region_compositor::get_composition_buffer               */
/*****************************************************************************/

kdu_compositor_buf *
  kdu_region_compositor::get_composition_buffer(kdu_dims &notional_region,
                                                bool working_only)
{
  if ((!working_only) && (queue_head != NULL))
    { 
      notional_region = queue_head->notional_buffer_region;
      return queue_head->composition_buffer;
    }
  if (composition_invalid && !update_composition())
    return NULL;
  notional_region = notional_buffer_region;
  if (composition_buffer != NULL)
    return composition_buffer;
  assert((active_layers != NULL) && (active_layers->next == NULL) &&
         (active_layers == last_active_layer));
  active_layers->buffer_is_composition_surface = true;
  return active_layers->get_layer_buffer();
}

/*****************************************************************************/
/*              kdu_region_compositor::push_composition_buffer               */
/*****************************************************************************/

bool
  kdu_region_compositor::push_composition_buffer(kdu_long custom_stamp,
                                                 int custom_id_val)
{
  if (!is_processing_complete())
    return false;
  kdrc_queue *elt=queue_free;
  while ((elt != NULL) && (elt->composition_buffer != NULL))
    { // We are responsible for recycling buffers released by calls to
      // `pop_composition_buffer' that may have occurred in a non-processing
      // thread -- see comments on multi-threaded design patterns appearing
      // with the API documentation for this function.
      internal_delete_buffer(elt->composition_buffer);
      elt->composition_buffer = NULL;
      elt = elt->next;
    }
  if ((elt = queue_free) != NULL)
    queue_free = elt->next;
  else
    elt = new kdrc_queue;
  if (queue_tail == NULL)
    queue_head = queue_tail = elt;
  else
    queue_tail = queue_tail->next = elt;
  elt->next = NULL;
  elt->custom_id_val = custom_id_val;
  elt->custom_stamp = custom_stamp;
  elt->notional_buffer_region = this->notional_buffer_region;
  if (composition_buffer != NULL)
    { 
      elt->composition_buffer = composition_buffer;
      composition_buffer =
        internal_allocate_buffer(rendering_buffer_region.size,
                                 buffer_size,true);
      composition_buffer->set_geometry(rendering_scale,
                                       rendering_buffer_region,
                                       rendering_composition_dims);
      if (!can_skip_surface_initialization)
        initialize_buffer_surface(composition_buffer,rendering_buffer_region,
                                  NULL,kdu_dims(),buffer_background);
      for (kdrc_layer *lp=active_layers; lp != NULL; lp=lp->next)
        lp->change_compositing_buffer(composition_buffer);
    }
  else
    {
      assert((active_layers != NULL) && (active_layers->next == NULL) &&
             (active_layers == last_active_layer));
      elt->composition_buffer = active_layers->take_layer_buffer();
    }

  processing_complete = false;
  refresh_mgr->reset();
  refresh_mgr->add_region(rendering_buffer_region);
  if (!can_skip_surface_initialization)
    { // See `set_buffer_surface' for explanation of why we don't want to call
      // `refresh_mgr->adjust' when we are in the mode where buffer surface
      // initialization is being skipped.
      kdrc_stream *sp;
      for (sp=streams; sp != NULL; sp=sp->next)
        if (sp->is_active)
          refresh_mgr->adjust(sp);
    }
  return true;
}

/*****************************************************************************/
/*           kdu_region_compositor::replace_composition_queue_tail           */
/*****************************************************************************/

bool
  kdu_region_compositor::replace_composition_queue_tail(kdu_long custom_stamp,
                                                        int custom_id_val)
{
  kdrc_queue *elt=queue_head, *prev=NULL;
  if (elt != NULL)
    { 
      for (; elt->next != NULL; prev=elt, elt=elt->next);
      assert(elt == queue_tail);
      if ((queue_tail = prev) == NULL)
        queue_head = NULL;
      else
        queue_tail->next = NULL;
      elt->next = queue_free;
      queue_free = elt;
    }
  return push_composition_buffer(custom_stamp,custom_id_val);
}

/*****************************************************************************/
/*              kdu_region_compositor::pop_composition_buffer                */
/*****************************************************************************/

bool
  kdu_region_compositor::pop_composition_buffer()
{
  kdrc_queue *elt = queue_head;
  if (elt == NULL)
    return false;
  if ((queue_head = elt->next) == NULL)
    queue_tail = NULL;
  elt->next = queue_free;
  queue_free = elt;
  return true;
}

/*****************************************************************************/
/*             kdu_region_compositor::inspect_composition_queue              */
/*****************************************************************************/

kdu_compositor_buf *
  kdu_region_compositor::inspect_composition_queue(int elt_idx,
                                                   kdu_long *custom_stamp,
                                                   int *custom_id_val,
                                                   kdu_dims *notional_region)
{
  if (elt_idx < 0)
    return NULL;
  kdrc_queue *elt = queue_head;
  while ((elt_idx > 0) && (elt != NULL))
    {
      elt_idx--;
      elt = elt->next;
    }
  if (elt == NULL)
    return NULL;
  if (custom_id_val != NULL)
    *custom_id_val = elt->custom_id_val;
  if (custom_stamp != NULL)
    *custom_stamp = elt->custom_stamp;
  if (notional_region != NULL)
    *notional_region = elt->notional_buffer_region;
  return elt->composition_buffer;
}

/*****************************************************************************/
/*              kdu_region_compositor::flush_composition_queue               */
/*****************************************************************************/

void
  kdu_region_compositor::flush_composition_queue()
{
  while ((queue_tail=queue_head) != NULL)
    { 
      queue_head = queue_tail->next;
      queue_tail->next = queue_free;
      queue_free = queue_tail;
    }
  kdrc_queue *elt=queue_free;
  while ((elt != NULL) && (elt->composition_buffer != NULL))
    { 
      internal_delete_buffer(elt->composition_buffer);
      elt->composition_buffer = NULL;
      elt = elt->next;
    }
}

/*****************************************************************************/
/*                    kdu_region_compositor::set_thread_env                  */
/*****************************************************************************/

kdu_thread_env *
  kdu_region_compositor::set_thread_env(kdu_thread_env *env,
                                        kdu_thread_queue *env_queue)
{
  if ((env != NULL) && !env->exists())
    env = NULL;
  if (env == NULL)
    env_queue = NULL;
  if ((env != this->env) || (env_queue != this->env_queue))
    {
      kdrc_stream *scan;
      for (scan=streams; scan != NULL; scan=scan->next)
        scan->set_thread_env(env,env_queue);
    }
  kdu_thread_env *old_env = this->env;
  this->env = env;
  this->env_queue = env_queue;
  return old_env;
}

/*****************************************************************************/
/*                       kdu_region_compositor::process                      */
/*****************************************************************************/

bool
  kdu_region_compositor::process(int suggested_increment,
                                 kdu_dims &new_region, int flags)
{
  invalid_scale_code = 0;
  new_region.size.x = new_region.size.y = 0;
  if (composition_invalid && !update_composition())
    return false;
  if (processing_complete)
    return false;
  if (flags & KDU_COMPOSIT_DEFER_REGION)
    flags &= ~KDU_COMPOSIT_DEFER_PROCESSING; // Can't defer everything!

  bool cost_threshold_exceeded = false;
  if (!(flags & KDU_COMPOSIT_DEFER_PROCESSING))
    {
      // Start by performing outstanding overlay processing, pushing results to
      // the refresh manager
      kdrc_layer *ovl;
      for (ovl=active_layers;
           (ovl != NULL) && !cost_threshold_exceeded;
           ovl=ovl->next)
        if (ovl->have_overlay_info)
          {
            kdu_long processing_cost = 0;
            while (!cost_threshold_exceeded)
              { 
                bool painted_something = false;
                kdu_dims seg_region;
                if (!ovl->process_overlay(seg_region,painted_something))
                  break;
                if (!can_skip_surface_initialization)
                  find_completed_rects(seg_region,active_layers,0);
                if (painted_something)
                  { 
                    processing_cost += (seg_region.area()+15)>>4;
                    if (processing_cost >= (kdu_long) suggested_increment)
                      cost_threshold_exceeded = true;
                  }
              }
          }
    }

  kdrc_stream *scan;
  if ((!cost_threshold_exceeded) && !(flags & KDU_COMPOSIT_DEFER_PROCESSING))
    { 
      // Process codestream data, following the dynamically generated priority
      // information which is generated so as to keep multiple codestreams
      // alive doing background processing, where possible.
      processing_complete = true; // Until proven otherwise
      
      kdrc_stream *stream=NULL;
      for (stream=NULL, scan=streams; scan != NULL; scan=scan->next)
        if (scan->is_active && scan->can_process_now() &&
            ((stream == NULL) || (scan->priority > stream->priority)))
          stream = scan;
      if (stream != NULL)
        { 
          kdu_dims proc_region;
          if (!stream->process(suggested_increment,proc_region,
                               invalid_scale_code,intensity_true_zero,
                               intensity_true_max))
            { // Scale has been found to be invalid
              assert(invalid_scale_code != 0);
              have_valid_scale = false;
              composition_invalid = true;
              processing_complete = false;
              return false;
            }
          else if (!stream->is_complete)
            processing_complete = false;
          if (!(can_skip_surface_initialization || proc_region.is_empty()))
            find_completed_rects(proc_region,active_layers,0);                
        }
    }
  
  int lim_rendered_y = INT_MAX;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->is_active && !scan->is_complete)
      { 
        processing_complete = false;
        int lim_y = scan->get_min_unrendered_y();
        if (lim_y < lim_rendered_y)
          lim_rendered_y = lim_y;
      }
  
  if ((processing_complete ||
       !(can_skip_surface_initialization ||
         (flags & KDU_COMPOSIT_DEFER_REGION))) &&
      !refresh_mgr->is_empty())
    {
      // Pop regions from the refresh manager, so as to create a (mostly)
      // novel rectangular `new_region'.
      if (refresh_mgr->pop_largest_region(new_region))
        { 
          int min_new_y = new_region.pos.y;
          kdu_long new_content_area = new_region.area();
          kdu_dims refresh_region = new_region;
          do {
            kdrc_layer *lpp;
            if (refresh_region.pos.y < min_new_y)
              min_new_y = refresh_region.pos.y;
            for (lpp=active_layers; lpp != NULL; lpp=lpp->next)
              lpp->do_composition(refresh_region,overlay_factor_x128,
                                  (lpp ==
                                   active_layers)?(&buffer_background):NULL);
          } while (refresh_mgr->pop_and_aggregate(new_region,refresh_region,
                                              new_content_area,
                                              process_aggregation_threshold));
          if (!refresh_mgr->is_empty())
            { 
              processing_complete = false;
              if (composition_buffer != NULL)
                { 
                  int lim_new_y = refresh_mgr->get_min_unrefreshed_y();
                  if (lim_rendered_y < lim_new_y)
                    lim_new_y = lim_rendered_y;
                  min_new_y -= rendering_buffer_region.pos.y;
                  lim_new_y -= rendering_buffer_region.pos.y;
                  composition_buffer->note_new_rendered_rows(min_new_y,
                                                             lim_new_y);
                }
            }
          else if (composition_buffer != NULL)
            { 
              int lim_new_y = (rendering_buffer_region.size.y +
                               rendering_buffer_region.pos.y);
              if (lim_rendered_y < lim_new_y)
                lim_new_y = lim_rendered_y;
              min_new_y -= rendering_buffer_region.pos.y;
              lim_new_y -= rendering_buffer_region.pos.y;
              composition_buffer->note_new_rendered_rows(min_new_y,lim_new_y);
            }
        }
    }
  if (processing_complete && (composition_buffer != NULL))
    composition_buffer->note_new_rendered_rows(rendering_buffer_region.size.y,
                                               rendering_buffer_region.size.y);
  
  return true;
}

/*****************************************************************************/
/*         kdu_region_compositor::is_codestream_processing_complete          */
/*****************************************************************************/

bool
  kdu_region_compositor::is_codestream_processing_complete()
{
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->is_active && !scan->is_complete)
      return false;
  return true;
}

/*****************************************************************************/
/*               kdu_region_compositor::set_max_quality_layers               */
/*****************************************************************************/

void
  kdu_region_compositor::set_max_quality_layers(int max_quality_layers)
{
  this->max_quality_layers = max_quality_layers;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    scan->set_max_quality_layers(max_quality_layers);
}

/*****************************************************************************/
/*          kdu_region_compositor::get_max_available_quality_layers          */
/*****************************************************************************/

int
  kdu_region_compositor::get_max_available_quality_layers()
{
  int val, result = 0;
  kdrc_stream *scan;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->is_active && scan->had_codestream_header())
      { 
        kdu_params *root = scan->codestream->ifc.access_siz();
        kdu_params *cod = root->access_cluster(COD_params);
        if ((cod != NULL) && cod->get(Clayers,0,0,val) && (val > result))
          result = val;
        if ((val = scan->codestream->ifc.get_max_tile_layers()) > result)
          result = val;
      }
  return result;
}

/*****************************************************************************/
/*             kdu_region_compositor::find_compatible_jpip_window            */
/*****************************************************************************/

bool
  kdu_region_compositor::find_compatible_jpip_window(kdu_coords &fsiz,
                                                     kdu_dims &roi_dims,
                                                     int &round_direction,
                                                     kdu_dims notional_region)
{
  if (composition_invalid && !update_composition())
    return false;
  if (!notional_composition_dims)
    return false;
  kdrc_stream *scan;
  
  double min_scale[2]={0.001,0.001};
  double max_scale[2]={2000000.0,2000000.0};
  bool scale_info_available = false;
  for (scan=streams; scan != NULL; scan=scan->next)
    if (scan->is_active &&
        scan->find_min_max_jpip_woi_scales(min_scale,max_scale))
      scale_info_available = true;
  double scale = 1.0;
  round_direction = 0;
  if (scale_info_available)
    {
      if (min_scale[0] > max_scale[0])
        round_direction = 1;
      if (min_scale[round_direction] > max_scale[round_direction])
        scale = min_scale[round_direction];
      else if ((min_scale[round_direction] >= 1.0) ||
               (max_scale[round_direction] <= 1.0))
        { 
          if (max_scale[round_direction] > 1000000.0) // No upper bound
            scale = 2.0 * min_scale[round_direction];
          else
            scale = 0.5*(min_scale[round_direction] +
                         max_scale[round_direction]);
        }
    }
  
  jpx_composition composition;
  if ((jpx_src != NULL) && (!fixed_composition_dims.is_empty()) &&
      (composition = jpx_src->access_composition()).exists())
    { 
      kdu_dims frame_dims;
      composition.get_global_info(frame_dims.size);
      if (frame_dims != fixed_composition_dims)
        { // Looks like a multi-frame composition; we will base `fsiz' off
          // the global frame dimensions and ignore the region of interest.
          // If we had only a single-frame composition, `frame_dims' would
          // be identical to `fixed_composition_dims' and then
          // `notional_composition_dims' would be a scaled (and perhaps
          // geometrically modified) version of this, using `notional_scale'.
          // Accordingly, we just need to scale `frame_dims.size' by
          // `notional_scale'*`scale' to get `fsiz' and then set
          // `roi_dims' to refer to the full frame.
          scale *= notional_scale;
          if ((frame_dims.size.x * scale) > (double) INT_MAX)
            scale = ((double) INT_MAX) / frame_dims.size.x;
          if ((frame_dims.size.y * scale) > (double) INT_MAX)
            scale = ((double) INT_MAX) / frame_dims.size.y;
          fsiz.x = (int) floor(0.5 + frame_dims.size.x * scale);
          fsiz.y = (int) floor(0.5 + frame_dims.size.y * scale);
          roi_dims.pos = kdu_coords();
          roi_dims.size = fsiz;
          return true;
        }
    }
  
  // Regular processing path is here
  kdu_dims full_dims = notional_composition_dims;
  full_dims.from_apparent(transpose,vflip,hflip);
  if ((full_dims.size.x * scale) > (double) INT_MAX)
    scale = ((double) INT_MAX) / full_dims.size.x;
  if ((full_dims.size.y * scale) > (double) INT_MAX)
    scale = ((double) INT_MAX) / full_dims.size.y;
  fsiz.x = (int) floor(0.5 + full_dims.size.x * scale);
  fsiz.y = (int) floor(0.5 + full_dims.size.y * scale);

  notional_region.from_apparent(transpose,vflip,hflip);
  notional_region &= full_dims;
  notional_region.pos -= full_dims.pos;
  if (notional_region.is_empty())
    {
      roi_dims.pos = kdu_coords();
      roi_dims.size = fsiz;
    }
  else
    {
      kdu_coords lim = notional_region.pos + notional_region.size;

      scale = ((double) fsiz.x) / ((double) full_dims.size.x);
      roi_dims.pos.x = (int) floor(notional_region.pos.x * scale);
      if ((lim.x * scale) >= (double) INT_MAX)
        lim.x = INT_MAX;
      else
        lim.x = (int) ceil(lim.x * scale);
      
      scale = ((double) fsiz.y) / ((double) full_dims.size.y);
      roi_dims.pos.y = (int) floor(notional_region.pos.y * scale);
      if ((lim.y * scale) >= (double) INT_MAX)
        lim.y = INT_MAX;
      else
        lim.y = (int) ceil(lim.y * scale);
      
      roi_dims.size = lim - roi_dims.pos;
    }
  return true;
}

/*****************************************************************************/
/*               kdu_region_compositor::load_metadata_matches                */
/*****************************************************************************/

bool kdu_region_compositor::load_metadata_matches()
{
  if (jpx_src == NULL)
    return false;
  jpx_meta_manager meta_manager = jpx_src->access_meta_manager();
  
  bool result = false;
  int *cs_indices=NULL, *lyr_indices=NULL;
  try {
    int n, num_cs=0, num_lyr=0;
    kdrc_layer *lyr;
    for (lyr=active_layers; lyr != NULL; lyr=lyr->next)
      num_lyr++;
    kdrc_stream *strm;
    for (strm=streams; strm != NULL; strm=strm->next)
      if (strm->is_active)
        num_cs++;

    lyr_indices = new int[num_lyr];
    cs_indices = new int[num_cs];
    
    num_cs = num_lyr = 0;
    for (lyr=active_layers; lyr != NULL; lyr=lyr->next)
      { 
        for (n=0; n < num_lyr; n++)
          if (lyr_indices[n] == lyr->colour_init_src)
            break;
        if (n == num_lyr)
          lyr_indices[num_lyr++] = lyr->colour_init_src;
      }
    for (strm=streams; strm != NULL; strm=strm->next)
      if (strm->is_active)
        { 
          for (n=0; n < num_cs; n++)
            if (cs_indices[n] == strm->codestream_idx)
              break;
          if (n == num_cs)
            cs_indices[num_cs++] = strm->codestream_idx;
        }

    result = meta_manager.load_matches(num_cs,cs_indices,num_lyr,lyr_indices);
  }
  catch (...) {
    if (cs_indices != NULL)
      delete[] cs_indices;
    if (lyr_indices != NULL)
      delete[] lyr_indices;
    throw;
  }
  delete[] cs_indices; cs_indices = NULL;
  delete[] lyr_indices; lyr_indices = NULL;
  return result;
}
  
/*****************************************************************************/
/*                  kdu_region_compositor::generate_metareq                  */
/*****************************************************************************/

int
  kdu_region_compositor::generate_metareq(kdu_window *client_window,
                                          int anchor_flags,
                                          kdu_dims notional_region,
                                          int num_box_types,
                                          const kdu_uint32 box_types[],
                                          int num_descend_types,
                                          const kdu_uint32 descend_types[],
                                          bool priority,
                                          kdu_int32 max_descend_depth)
{
  if (jpx_src == NULL)
    return 0;
  jpx_meta_manager meta_manager = jpx_src->access_meta_manager();
  if (!meta_manager)
    return 0;
  
  // Now generate the metadata requests
  int num_metareqs = 0;
  if ((anchor_flags & KDU_MRQ_GLOBAL) && (num_descend_types > 0))
    { // The global anchors don't depend on the composition
      jpx_metanode node;
      while ((node = meta_manager.enumerate_matches(node,-1,-1,false,
                                                    kdu_dims(),0)).exists())
        { 
          kdu_uint32 box_type = node.get_box_type();
          assert((box_type != jp2_number_list_4cc) &&
                 (box_type != jp2_roi_description_4cc));
          int b;
          for (b=0; b < num_descend_types; b++)
            if (box_type == descend_types[b])
              break;
          if (b >= num_descend_types)
            continue;
          num_metareqs += node.generate_metareq(client_window,num_box_types,
                                                box_types,num_descend_types,
                                                descend_types,priority,
                                                max_descend_depth);
        }
    }
  
  if (anchor_flags & KDU_MRQ_STREAM)
    { // Look for "rendered result" numlists
      jpx_metanode node;
      while ((node=meta_manager.enumerate_matches(node,-1,-1,true,kdu_dims(),
                                                  0,true,true,false)).exists())
        num_metareqs += node.generate_metareq(client_window,
                                              num_box_types,box_types,
                                              num_descend_types,
                                              descend_types,priority,
                                              max_descend_depth);
    }
  
  if (!(anchor_flags & (KDU_MRQ_STREAM | KDU_MRQ_WINDOW)))
    return num_metareqs;

  kdrc_layer *layer;
  for (layer=active_layers; layer != NULL; layer=layer->next)
    { 
      int n, nstrms = layer->get_num_streams();
      for (n=0; n < nstrms; n++)
        { 
          kdrc_stream *stream = layer->get_stream(n);
          int layer_idx = layer->colour_init_src;
          int stream_idx = stream->codestream_idx;
          kdu_dims cs_region;
          if (notional_region.is_empty())
            cs_region = stream->find_full_notional_region(true);
          else
            cs_region = stream->map_region(notional_region,true);
          if (cs_region.is_empty())
            continue; // This ilayer does not intersect `region'

          if (anchor_flags & KDU_MRQ_STREAM)
            { 
              jpx_metanode node;
              while ((node =
                      meta_manager.enumerate_matches(node,stream_idx,layer_idx,
                                                     false,kdu_dims(),0,true,
                                                     true,false)).exists())
                num_metareqs += node.generate_metareq(client_window,
                                                      num_box_types,box_types,
                                                      num_descend_types,
                                                      descend_types,priority,
                                                      max_descend_depth);
            }
          if (anchor_flags & KDU_MRQ_WINDOW)
            { 
              jpx_metanode node;
              while ((node =
                      meta_manager.enumerate_matches(node,stream_idx,layer_idx,
                                                     false,cs_region,1,false,
                                                     true,false)).exists())
                num_metareqs += node.generate_metareq(client_window,
                                                      num_box_types,box_types,
                                                      num_descend_types,
                                                      descend_types,priority,
                                                      max_descend_depth);
            }
        }
    }
  
  return 0;
}

/*****************************************************************************/
/*              kdu_region_compositor::set_layer_buffer_surfaces             */
/*****************************************************************************/

void
  kdu_region_compositor::set_layer_buffer_surfaces()
{
  kdu_dims visible_region = rendering_buffer_region;
  kdu_coords visible_min = visible_region.pos;
  kdu_coords visible_lim = visible_min + visible_region.size;
  kdu_dims opaque_region;
  for (kdrc_layer *scan=last_active_layer; scan != NULL; scan=scan->prev)
    { 
      scan->set_buffer_surface(rendering_buffer_region,visible_region,
                               composition_buffer,overlay_dependencies);
      processing_complete = false;
      if (scan->have_alpha_channel)
        continue; // No effect on opaque region
    
      // Adjust rendering visible region for opaque covering by this layer
      kdu_dims opaque_region = scan->get_rendering_buffer_region();
      kdu_coords opaque_min = opaque_region.pos;
      kdu_coords opaque_lim = opaque_min + opaque_region.size;
      if ((opaque_min.x==visible_min.x) && (opaque_lim.x==visible_lim.x))
        { // May be able to reduce height of visible region
          if ((opaque_min.y < visible_lim.y) &&
              (opaque_lim.y >= visible_lim.y))
            visible_lim.y = opaque_min.y;
          else if ((opaque_lim.y > visible_min.y) &&
                   (opaque_min.y <= visible_min.y))
            visible_min.y = opaque_lim.y;
          if (visible_min.y < rendering_buffer_region.pos.y)
            visible_min.y = rendering_buffer_region.pos.y;
          if (visible_lim.y < visible_min.y)
            visible_lim.y = visible_min.y;
          visible_region.pos.y = visible_min.y;
          visible_region.size.y = visible_lim.y - visible_min.y;
        }
      else if ((opaque_min.y==visible_min.y) && (opaque_lim.y==visible_lim.y))
        { // May be able to reduce width of visible region
          if ((opaque_min.x < visible_lim.x) &&
              (opaque_lim.x >= visible_lim.x))
            visible_lim.x = opaque_min.x;
          else if ((opaque_lim.x > visible_min.x) &&
                   (opaque_min.x <= visible_min.x))
            visible_min.x = opaque_lim.x;
          if (visible_min.x < rendering_buffer_region.pos.x)
            visible_min.x = rendering_buffer_region.pos.x;
          if (visible_lim.x < visible_min.x)
            visible_lim.x = visible_min.x;
          visible_region.pos.y = visible_min.y;
          visible_region.size.y = visible_lim.y - visible_min.y;
        }
    }
}

/*****************************************************************************/
/*           kdu_region_compositor::derive_rendering_buffer_region           */
/*****************************************************************************/

void
  kdu_region_compositor::derive_rendering_buffer_region()
{
  if ((rendering_scale == notional_scale) || (notional_scale <= 0.0f))
    rendering_buffer_region = notional_buffer_region;
  else
    { 
      double scale = ((double)rendering_scale) / ((double)notional_scale);
      kdu_coords min = notional_buffer_region.pos;
      kdu_coords lim = min + notional_buffer_region.size;
      min.x = (int) floor(min.x*scale);
      min.y = (int) floor(min.y*scale);
      lim.x = (int) ceil(lim.x*scale);
      lim.y = (int) ceil(lim.y*scale);
      rendering_buffer_region.pos = min;
      rendering_buffer_region.size = lim - min;
    }
  rendering_buffer_region &= rendering_composition_dims;
}

/*****************************************************************************/
/*                  kdu_region_compositor::update_composition                */
/*****************************************************************************/

bool
  kdu_region_compositor::update_composition()
{
  if (in_pre_destroy)
    return false;
  invalid_scale_code = 0;
  if (!have_valid_scale)
    return false;
  if (active_layers == NULL)
    return false;

  // Start by checking whether a fixed composition frame is being excessively
  // scaled.
  if (!fixed_composition_dims.is_empty())
    { 
      float max_scale =
        (notional_scale >= rendering_scale)?notional_scale:rendering_scale;
      kdu_coords frame_lim =
        fixed_composition_dims.pos+fixed_composition_dims.size;
      if (((((double) frame_lim.x) * max_scale) > (double) 0x7FFFF000) ||
          ((((double) frame_lim.y) * max_scale) > (double) 0x7FFFF000))
        {
          invalid_scale_code |= KDU_COMPOSITOR_SCALE_TOO_LARGE;
          return (have_valid_scale=false);
        }
    }

  // Now set the scale of every layer and hence find notional and rendering
  // scaled versions of total composition size
  bool need_separate_composition_buffer = false;
  kdu_dims layer_dims;
  kdrc_layer *scan;
  notional_composition_dims = kdu_dims();
  rendering_composition_dims = kdu_dims();
  for (scan=active_layers; scan != NULL; scan=scan->next)
    { 
      if ((scan != active_layers) || scan->have_alpha_channel ||
          scan->have_overlay_info)
        need_separate_composition_buffer = true;
      if (!scan->set_scale(transpose,vflip,hflip,notional_scale,
                           rendering_scale,invalid_scale_code,
                           fixed_composition_dims))
        return (have_valid_scale=false);
      notional_composition_dims.augment(scan->get_notional_layer_region());
      rendering_composition_dims.augment(scan->get_rendering_layer_region());
    }  

  if (rendering_composition_dims.is_empty())
    need_separate_composition_buffer = true;
  if (!fixed_composition_dims.is_empty())
    { // The composition needs to be performed within a fixed frame
      
      // Start by working out the notional frame size by scaling the fixed
      // frame by `notional_scale', being careful to round the scaled frame
      // boundaries towards zero so as to minimize the likelihood that the
      // compoition surface will appear larger than its composited contents
      // when rendered at reduced resolution.
      kdu_coords min = fixed_composition_dims.pos;
      kdu_coords lim = min + fixed_composition_dims.size;
      min.x = (int)(notional_scale * (double) min.x);
      min.y = (int)(notional_scale * (double) min.y);
      lim.x = (int)(notional_scale * (double) lim.x);
      lim.y = (int)(notional_scale * (double) lim.y);
      kdu_dims frame_dims;
      frame_dims.pos = min;
      frame_dims.size = lim-min;
      frame_dims.to_apparent(transpose,vflip,hflip);
      notional_composition_dims = frame_dims;

      // If `rendering_scale' is identical to `notional_scale', we adopt
      // the same rounding policy above for the `rendering_composition_dims'.
      // However, if `rendering_scale' is different, we round outwards from
      // the the `notional_composition_dims' region, following the same
      // procedure as `derive_rendering_buffer_region', whose objective is
      // to ensure that rendering buffer regions in some sense cover the
      // corresponding notional buffer regions.  Since we will be intersecting
      // rendering buffer regions with `rendering_composition_dims', it is
      // important that this cover property is retained, else we could even
      // wind up with a situation where the derived `rendering_buffer_region'
      // becomes empty once intersected with the `rendering_composition_dims'.
      if ((rendering_scale == notional_scale) || (notional_scale <= 0.0f))
        { 
          frame_dims = notional_composition_dims;
        }
      else
        { 
          double rel_scale=((double)rendering_scale)/((double)notional_scale);
          min = notional_composition_dims.pos;
          lim = min + notional_composition_dims.size;
          min.x = (int) floor(min.x*rel_scale);
          min.y = (int) floor(min.y*rel_scale);
          lim.x = (int) ceil(lim.x*rel_scale);
          lim.y = (int) ceil(lim.y*rel_scale);
          frame_dims.pos = min;
          frame_dims.size = lim-min;
        }

      rendering_composition_dims &= frame_dims; // Different rounding policies
                  // may have left individual layers (and hence the overall
                  // composition) hanging over the edge of our global
                  // `frame_dims' by a little bit (<= 1 sample).  If we do not
                  // pick a separate composition buffer, this could leave
                  // buffer dimensions larger than the reported overall
                  // rendering frame size even if that is the same as the
                  // notional frame size, which can cause some confusion for
                  // applications.
      if (rendering_composition_dims != frame_dims)
        need_separate_composition_buffer = true;
      rendering_composition_dims = frame_dims;
      
      // Finish by informing the individual compositing layers of the
      // fact that their overall dimensions are constrained by
      // `notional_composition_dims' and `rendering_composition_dims', since
      // these can sometimes be slightly (1 row/column) smaller than the layer
      // dimensions computed individuall above.
      for (scan=active_layers; scan != NULL; scan=scan->next)
        scan->apply_layer_region_constraints(notional_composition_dims,
                                             rendering_composition_dims);
    }
  
  // Next, find the compositing buffer region and create a separate
  // compositing buffer if necessary
  notional_buffer_region &= notional_composition_dims;
  derive_rendering_buffer_region();
  if (need_separate_composition_buffer)
    { 
      kdu_coords new_size = rendering_buffer_region.size;
      if ((composition_buffer == NULL) ||
          (new_size.x > buffer_size.x) || (new_size.y > buffer_size.y))
        {
          if (composition_buffer != NULL)
            {
              internal_delete_buffer(composition_buffer);
              composition_buffer = NULL;
            }
          composition_buffer =
            internal_allocate_buffer(new_size,buffer_size,true);
        }
      composition_buffer->set_geometry(rendering_scale,rendering_buffer_region,
                                       rendering_composition_dims);
      if (!can_skip_surface_initialization) 
        initialize_buffer_surface(composition_buffer,rendering_buffer_region,
                                  NULL,kdu_dims(),buffer_background);
      composition_buffer->set_invalid();
    }
  else
    { // Can re-use first layer's compositing buffer
      if (composition_buffer != NULL)
        {
          internal_delete_buffer(composition_buffer);
          composition_buffer = NULL;
          buffer_size = kdu_coords(0,0);
        }
    }

  // Finally, set the buffer surface for all layers and perform whatever
  // recomposition is possible
  set_layer_buffer_surfaces();
  bool aux_params_changed = false;
  if (scaled_overlay_params_invalid)
    { 
      scaled_overlay_params_invalid = false;
      aux_params_changed = update_scaled_overlay_params();
    }
  for (scan=active_layers; scan != NULL; scan=scan->next)
    scan->configure_overlay(enable_overlays,overlay_min_display_size,
                            scaled_overlay_max_painting_border,
                            overlay_dependencies,
                            scaled_overlay_aux_params,
                            scaled_overlay_num_aux_params,
                            false,aux_params_changed,false);

  refresh_mgr->reset();
  refresh_mgr->add_region(rendering_buffer_region);
  if (!can_skip_surface_initialization)
    { // See `set_buffer_surface' for an explanation of why we don't want
      // to invoke `refresh_mgr->adjust' when surfaces are not pre-initialized.
      kdrc_stream *sp;
      for (sp=streams; sp != NULL; sp=sp->next)
        if (sp->is_active)
          refresh_mgr->adjust(sp);
    }

  processing_complete = false; // Force at least one call to `process'
  composition_invalid = false;
  return true;
}

/*****************************************************************************/
/*              kdu_region_compositor::internal_allocate_buffer              */
/*****************************************************************************/

kdu_compositor_buf *
  kdu_region_compositor::internal_allocate_buffer(kdu_coords min_size,
                                                  kdu_coords &actual_size,
                                                  bool read_access_required)
{
  kdu_compositor_buf *container =
    allocate_buffer(min_size,actual_size,read_access_required);
  if (container == NULL)
    { 
      if (min_size.x < 1) min_size.x = 1; // No harm in this
      if (min_size.y < 1) min_size.y = 1; // No harm in this
      int stride = min_size.x;
      int align_dwords = (KDU_ALIGN_SAMPLES16>>1);
      int aligned_stride = stride + ((-stride) & (align_dwords-1));
      if (aligned_stride >= stride) // Always true unless alignment would cause
        stride = aligned_stride;    // numerical overflow      
      kdu_int64 tally_pels = stride; tally_pels *= (kdu_int64) min_size.y;
      tally_pels += align_dwords; // Allow for alignment adjustments
      size_t num_pels = (size_t) tally_pels;
      if (tally_pels != (kdu_int64) num_pels)
        { // Must be 32-bit platform and `num_pels' >= 2^32!!
          throw std::bad_alloc();
        }
      if ((tally_pels >= KDRC_MAX_BUFFER_PELS) ||
          (min_size.x >= KDRC_MAX_BUFFER_WIDTH))
        { // Don't allow ridiculously large compositing buffers.  The
          // region compositor is intended to be used for rendering and
          // compositing modest size images or regions from potentially
          // huge images.
          KDU_ERROR_DEV(e,0x17021601); e <<
          KDU_TXT("Attempting to allocate a composition buffer that is "
                  "ridiculously large or ridiculously wide!  The memory "
                  "sub-system might allow this, but the region-compositor "
                  "is intended for rendering modest size images or regions "
                  "within potentially huge images, not for rendering "
                  "enormous images in one hit!  Try rendering a smaller "
                  "region or rendering at a reduced scale.");
        }
      kdu_uint32 *handle = new(std::nothrow) kdu_uint32[num_pels];
      if (handle == NULL)
        { 
          cull_inactive_ilayers(0);
          handle = new kdu_uint32[num_pels];
          if (handle == NULL)
            throw std::bad_alloc();
        }
      actual_size.x = stride;
      actual_size.y = min_size.y;
      if ((container = new(std::nothrow) kdu_compositor_buf) == NULL)
        { delete[] handle; throw std::bad_alloc(); } // Just in case
      int align_off = (-(_addr_to_kdu_int32(handle) >> 2)) & (align_dwords-1);
      kdu_uint32 *aligned_buf = handle + align_off;
      container->init(aligned_buf,actual_size.x);
      container->set_read_accessibility(read_access_required);
      container->post_init(actual_size,handle);
    }
  else
    container->post_init(actual_size,NULL); // Externally allocated
  container->set_invalid();
  return container;
}

/*****************************************************************************/
/*               kdu_region_compositor::internal_delete_buffer               */
/*****************************************************************************/

void
  kdu_region_compositor::internal_delete_buffer(kdu_compositor_buf *buffer)
{
  if (buffer->need_external_delete())
    this->delete_buffer(buffer);
  else
    delete buffer;
}

/*****************************************************************************/
/*                    kdu_region_compositor::paint_overlay                   */
/*****************************************************************************/

void
  kdu_region_compositor::paint_overlay(kdu_compositor_buf *buffer,
                                       kdu_dims buffer_region,
                                       kdu_dims clip_region,
                                       jpx_metanode node,
                                       kdu_overlay_params *paint_params,
                                       kdu_coords image_offset,
                                       kdu_coords subsampling, bool transpose,
                                       bool vflip, bool hflip,
                                       kdu_coords expansion_numerator,
                                       kdu_coords expansion_denominator,
                                       kdu_coords compositing_offset)
{
  // Prepare for buffer access within the `clip_region'
  clip_region &= buffer_region;
  if (!clip_region)
    {
      assert(0); // Should already be limited to `buffer_region'
      return;
    }
  kdu_coords buffer_offset = // Offset to start of `clip_region'
  clip_region.pos - buffer_region.pos;
  int buffer_row_gap;
  float *buf_float = NULL;
  kdu_uint32 *buf32 = NULL;
  if ((buf32 = buffer->get_buf(buffer_row_gap,false)) == NULL)
    { 
      buf_float = buffer->get_float_buf(buffer_row_gap,false);
      buf_float += buffer_offset.y*buffer_row_gap + 4*buffer_offset.x;
    }
  else
    buf32 += buffer_offset.y*buffer_row_gap + buffer_offset.x;
  
  // Prepare mapped regions
  int reg_idx, num_regions = node.get_num_regions();
  jpx_roi *reg, *regions =
  paint_params->map_jpx_regions(node.get_regions(),num_regions,image_offset,
                                subsampling,transpose,vflip,hflip,
                                expansion_numerator,expansion_denominator,
                                compositing_offset+clip_region.pos);
    // Note: the above call leaves transformed copies of the regions of
    // interest in `regions', with all regions' coordinates expressed relative
    // to the origin of the `clip_region'.
  
  // Determine whether the overall average "width" of the complete ROI
  double roi_cumulative_length=0.0;
  double roi_cumulative_area=0.0;
  for (reg=regions, reg_idx=0; reg_idx < num_regions; reg_idx++, reg++)
    {
      double width, length;
      reg->measure_span(width,length);
      roi_cumulative_area += width*length;
      roi_cumulative_length += length;
    }
  if (roi_cumulative_length <= 0.0)
    return;
  double roi_avg_width = roi_cumulative_area / roi_cumulative_length;
  
  // Find the relevant "row" of auxiliary parameters
  int num_params = paint_params->get_num_aux_params();
  int first_row_param=0, row_params=num_params;
  int border=paint_params->get_max_painting_border();
  while (row_params > 0)
    {
      border = (int) paint_params->get_aux_param(first_row_param);
      if ((border == 0) || (row_params < (3+border)))
        break; // No threshold available, so this is the last parameter row
      if (roi_avg_width >= (double)
          paint_params->get_aux_param(first_row_param+2+border))
        break; // Threshold for this parameter row is satisfied
      row_params -= 3 + border;
      first_row_param += 3 + border;
      border = 0; // Default border in case entire next parameter row missing
    }
  if (border > (int) paint_params->get_max_painting_border())
    border = (int) paint_params->get_max_painting_border();
  if (border > 16)
    border = 16; // Reasonable upper bound
  int last_b = border; // Index of the last available border colour
  kdu_uint32 COLR_0 = 0;
  kdu_uint32 COLR_last = (colour_order==KDU_ARGB_ORDER)?0xFF0000FF:0xFFFF0000;
  if (row_params < (2+border))
    last_b = row_params-2;
  if (last_b < 0)
    { COLR_0 = 0; last_b = 0; }
  else
    {
      COLR_0 = paint_params->get_aux_param(first_row_param+1);
      if (last_b > 0)
        COLR_last = paint_params->get_aux_param(first_row_param+1+last_b);
    }
  kdu_uint32 a_dec = 0;
  if (last_b < border)
    a_dec = ((COLR_last>>24)/(kdu_uint32)(border+1-last_b))<<24;
  
  // Paint from the outer edge of the border, working inwards
  for (int b=border; b >= 0; b--)
    { // Start with the outer-most border and work into the interior
      kdu_uint32 colr_val;
      if (b == 0)
        colr_val = COLR_0;
      else if (b < last_b)
        colr_val = paint_params->get_aux_param(first_row_param+1+b);
      else
        colr_val = COLR_last - a_dec * (b-last_b);
      paint_params->configure_ring_points(buffer_row_gap,b);
      
      // Scan through the individual regions in the ROI
      for (reg=regions, reg_idx=0; reg_idx < num_regions; reg_idx++, reg++)
        {
          if (reg->is_elliptical)
            { // Paint an ellipse
              int x_min=reg->region.pos.x;
              int y_min=reg->region.pos.y;
              int x_max=x_min+reg->region.size.x-1;
              int y_max=y_min+reg->region.size.y-1;
              if ((x_min >= (clip_region.size.x+b)) || (x_max < -b) ||
                  (y_min >= (clip_region.size.y+b)) || (y_max < -b))
                continue;
              double cy = 0.5*y_max+0.5*y_min;
              double cx = 0.5*x_max+0.5*x_min;
              double Ho = 0.5*(y_max-y_min); // Outer vertical half-width
              double Wo = 0.5*(x_max-x_min); // Outer horizontal half-width
              if (Ho < 0.5) Ho = 0.5;  // Minimum outer half-height of 0.5
              if (Wo < 0.5) Wo = 0.5; // Minimum outer half-width of 0.5
              double inv_Ho = 1.0/Ho;
              double alpha = 0.0;
              double Wi = Wo;
              if (reg->elliptical_skew.x != 0)
                {
                  alpha = reg->elliptical_skew.x * inv_Ho;
                  double gamma_sq = (alpha*reg->elliptical_skew.y) / Wo;
                  if (gamma_sq >= 1.0)
                    Wi = 0.5; // Minimum inner half-width is 0.5
                  else if ((Wi *= sqrt(1.0-gamma_sq)) < 0.5)
                    Wi = 0.5; // Minimum inner half-width is 0.5
                }
              double inv_Ho_sq = inv_Ho*inv_Ho;
              if (y_min < -b)
                y_min = -b;
              if (y_max >= (clip_region.size.y+b))
                y_max = clip_region.size.y+b-1;
              if (buf32 != NULL)
                { // 32-bit pixel overlay painting
                  kdu_uint32 colr=colr_val;
                  kdu_uint32 *dp=buf32 + y_min*buffer_row_gap;
                  for (; y_min <= y_max; y_min++, dp+=buffer_row_gap)
                    {
                      double y = y_min - cy;
                      double delta_x = Wi * sqrt(1.0-y*y*inv_Ho_sq);
                      double skewed_cx = cx-y*alpha;
                      int c1 = (int) floor(skewed_cx-delta_x+0.5);
                      int c2 = (int) ceil(skewed_cx+delta_x-0.5);
                      c1 = (c1 < 0)?0:c1;
                      c2 = (c2>=clip_region.size.x)?(clip_region.size.x-1):c2;
                      if (b == 0)
                        for (; c1 <= c2; c1++)
                          dp[c1] = colr;
                      else
                        paint_race_track(paint_params,colr,b,dp+c1,c2-c1,
                                         c1,clip_region.size.x-1-c1,
                                         -y_min,clip_region.size.y-1-y_min);
                    }
                }
              else
                { // floating-point overlay painting
                  float colr[4] =
                    { ((colr_val>>24)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>16)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>8)&0x0FF)*(1.0F/255.0F),
                      ((colr_val)&0x0FF)*(1.0F/255.0F) };
                  float *dp=buf_float + y_min*buffer_row_gap;
                  for (; y_min <= y_max; y_min++, dp+=buffer_row_gap)
                    { 
                      double y = y_min - cy;
                      double delta_x = Wi * sqrt(1.0-y*y*inv_Ho_sq);
                      double skewed_cx = cx-y*alpha;
                      int c1 = (int) floor(skewed_cx-delta_x+0.5);
                      int c2 = (int) ceil(skewed_cx+delta_x-0.5);
                      c1 = (c1 < 0)?0:c1;
                      c2 = (c2>=clip_region.size.x)?(clip_region.size.x-1):c2;
                      if (b == 0)
                        for (c1<<=2, c2<<=2; c1 <= c2; c1+=4)
                          {
                            dp[c1  ] = colr[0]; dp[c1+1] = colr[1];
                            dp[c1+2] = colr[2]; dp[c1+3] = colr[3];
                          }
                      else
                        paint_race_track(paint_params,colr,b,dp+4*c1,c2-c1,
                                         c1,clip_region.size.x-1-c1,
                                         -y_min,clip_region.size.y-1-y_min);
                    }
                }
            }
          else if (!(reg->flags & JPX_QUADRILATERAL_ROI))
            { // Paint rectangle bounded by vertex[top_v] and vertex[top_v+2]
              int x_min=reg->region.pos.x;
              int y_min=reg->region.pos.y;
              int x_max=x_min+reg->region.size.x-1;
              int y_max=y_min+reg->region.size.y-1;
              if ((x_min >= (clip_region.size.x+b)) || (x_max < -b) ||
                  (y_min >= (clip_region.size.y+b)) || (y_max < -b))
                continue;
              if (x_min < 0) x_min = 0;
              if (x_max >= clip_region.size.x) x_max = clip_region.size.x-1;
              if (y_min < 0) y_min = 0;
              if (y_max >= clip_region.size.y) y_max = clip_region.size.y-1;
              int n, x_span=x_max-x_min, y_span=y_max-y_min;
              if (buf32 != NULL)
                { // 32-bit pixel overlay painting
                  kdu_uint32 *dp, colr=colr_val;
                  if (b == 0)
                    { // Paint interior
                      for (dp=buf32+y_min*buffer_row_gap+x_min;
                           y_span >= 0; y_span--, dp+=buffer_row_gap)
                        for (n=0; n <= x_span; n++)
                          dp[n] = colr;
                    }
                  else
                    { // Paint border
                      if (y_min > 0)
                        { // Paint top edge and corners
                          dp = buf32 + y_min*buffer_row_gap + x_min;
                          int dy_max = (y_span<0)?y_span:-1;
                          paint_race_track(paint_params,colr,b,dp,x_span,
                                           x_min,clip_region.size.x-1-x_min,
                                           -y_min,dy_max);
                        }
                      if (y_max < (clip_region.size.y-1)) // Paint bottom edge
                        {
                          int dy_min = (y_span < 0)?(-y_span):1;
                          dp = buf32 + y_max*buffer_row_gap + x_min;
                          paint_race_track(paint_params,colr,b,dp,x_span,
                                           x_min,clip_region.size.x-1-x_min,
                                           dy_min,clip_region.size.y-1-y_max);
                        }
                      if (x_min >= b) // Paint left edge
                        for (dp=buf32+y_min*buffer_row_gap+(x_min-b),
                             n=y_span; n >= 0; n--, dp+=buffer_row_gap)
                          *dp = colr;
                      if ((x_max+b) < clip_region.size.x) // Paint right edge
                        for (dp=buf32+y_min*buffer_row_gap+(x_max+b),
                             n=y_span; n >= 0; n--, dp+=buffer_row_gap)
                          *dp = colr;
                    }
                }
              else
                { // floating-point overlay painting
                  float *dp, colr[4] =
                    { ((colr_val>>24)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>16)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>8)&0x0FF)*(1.0F/255.0F),
                      ((colr_val)&0x0FF)*(1.0F/255.0F) };
                  if (b == 0)
                    { // Paint interior
                      for (dp=buf_float+y_min*buffer_row_gap+4*x_min;
                           y_span >= 0; y_span--, dp+=buffer_row_gap)
                        for (n=0; n <= (4*x_span); n+=4)
                          { dp[n]=  colr[0]; dp[n+1]=colr[1];
                            dp[n+2]=colr[2]; dp[n+3]=colr[3]; }
                    }
                  else
                    { // Paint border
                      if (y_min > 0)
                        { // Paint top edge and corners
                          dp = buf_float + y_min*buffer_row_gap + 4*x_min;
                          int dy_max = (y_span<0)?y_span:-1;
                          paint_race_track(paint_params,colr,b,dp,x_span,
                                           x_min,clip_region.size.x-1-x_min,
                                           -y_min,dy_max);
                        }
                      if (y_max < (clip_region.size.y-1)) // Paint bottom edge
                        {
                          int dy_min = (y_span < 0)?(-y_span):1;
                          dp = buf_float + y_max*buffer_row_gap + 4*x_min;
                          paint_race_track(paint_params,colr,b,dp,x_span,
                                           x_min,clip_region.size.x-1-x_min,
                                           dy_min,clip_region.size.y-1-y_max);
                        }
                      if (x_min >= b) // Paint left edge
                        for (dp=buf_float+y_min*buffer_row_gap+4*(x_min-b),
                             n=y_span; n >= 0; n--, dp+=buffer_row_gap)
                          { dp[0]=colr[0]; dp[1]=colr[1];
                            dp[2]=colr[2]; dp[3]=colr[3]; }
                      if ((x_max+b) < clip_region.size.x) // Paint right edge
                        for (dp=buf_float+y_min*buffer_row_gap+4*(x_max+b),
                             n=y_span; n >= 0; n--, dp+=buffer_row_gap)
                          { dp[0]=colr[0]; dp[1]=colr[1];
                            dp[2]=colr[2]; dp[3]=colr[3]; }
                    }
                }
            }
          else
            { // Paint quadrilateral bounded by all four vertices
              kdrc_trapezoid_follower trap[3];
              trap[0].init(reg->vertices[0],reg->vertices[3],
                           reg->vertices[0],reg->vertices[1],
                           -b,clip_region.size.y+b-1);
              if ((reg->vertices[2].y < reg->vertices[1].y) &&
                  (reg->vertices[2].y < reg->vertices[3].y))
                { // Vertex 2 lies above vertices 1 and 3 which join to top
                  kdu_long V1m0_y = (((kdu_long) reg->vertices[1].y) -
                                     ((kdu_long) reg->vertices[0].y));
                  kdu_long V1m0_x = (((kdu_long) reg->vertices[1].x) -
                                     ((kdu_long) reg->vertices[0].x));       
                  kdu_long V2m0_y = (((kdu_long) reg->vertices[2].y) -
                                     ((kdu_long) reg->vertices[0].y));
                  kdu_long V2m0_x = (((kdu_long) reg->vertices[2].x) -
                                     ((kdu_long) reg->vertices[0].x));
                  kdu_long V3m0_y = (((kdu_long) reg->vertices[3].y) -
                                     ((kdu_long) reg->vertices[0].y));
                  kdu_long V3m0_x = (((kdu_long) reg->vertices[3].x) -
                                     ((kdu_long) reg->vertices[0].x));
                  if (((V2m0_y*V3m0_x) <= (V2m0_x*V3m0_y)) &&
                      ((V2m0_y*V1m0_x) >= (V2m0_x*V1m0_y))) 
                    { // Vertex 2 lies between the left & right edges of trap-0
                      trap[0].limit_max_y(reg->vertices[2].y);
                      trap[1].init(reg->vertices[0],reg->vertices[3],
                                   reg->vertices[2],reg->vertices[3],
                                   -b,clip_region.size.y+b-1);
                      trap[2].init(reg->vertices[2],reg->vertices[1],
                                   reg->vertices[0],reg->vertices[1],
                                   -b,clip_region.size.y+b-1);
                    }
                  else if (reg->vertices[3].y < reg->vertices[1].y)
                    { // Vertex 2 lies to the left of the left edge of trap-0
                      trap[1].init(reg->vertices[2],reg->vertices[1],
                                   reg->vertices[2],reg->vertices[3],
                                   -b,clip_region.size.y+b-1);
                      trap[2].init(reg->vertices[2],reg->vertices[1],
                                   reg->vertices[0],reg->vertices[1],
                                   -b,clip_region.size.y+b-1);
                      trap[2].limit_min_y(reg->vertices[3].y);
                    }
                  else
                    { // Vertex 2 lies to the right of the right edge of trap-0
                      trap[1].init(reg->vertices[2],reg->vertices[1],
                                   reg->vertices[2],reg->vertices[3],
                                   -b,clip_region.size.y+b-1);
                      trap[2].init(reg->vertices[0],reg->vertices[3],
                                   reg->vertices[2],reg->vertices[3],
                                   -b,clip_region.size.y+b-1);
                      trap[2].limit_min_y(reg->vertices[1].y);
                    }
                }
              else if (reg->vertices[3].y < reg->vertices[1].y)
                { // Vertex 3 lies above vertices 1 and 2
                  trap[1].init(reg->vertices[3],reg->vertices[2],
                               reg->vertices[0],reg->vertices[1],
                               -b,clip_region.size.y+b-1);
                  if (reg->vertices[2].y < reg->vertices[1].y)
                    trap[2].init(reg->vertices[2],reg->vertices[1],
                                 reg->vertices[0],reg->vertices[1],
                                 -b,clip_region.size.y+b-1);
                  else
                    trap[2].init(reg->vertices[3],reg->vertices[2],
                                 reg->vertices[1],reg->vertices[2],
                                 -b,clip_region.size.y+b-1);
                }
              else
                { // Vertex 1 lies above vertices 2 and 3
                  trap[1].init(reg->vertices[0],reg->vertices[3],
                               reg->vertices[1],reg->vertices[2],
                               -b,clip_region.size.y+b-1);
                  if (reg->vertices[2].y < reg->vertices[3].y)
                    trap[2].init(reg->vertices[0],reg->vertices[3],
                                 reg->vertices[2],reg->vertices[3],
                                 -b,clip_region.size.y+b-1);
                  else
                    trap[2].init(reg->vertices[3],reg->vertices[2],
                                 reg->vertices[1],reg->vertices[2],
                                 -b,clip_region.size.y+b-1);
                }
              int max_x = clip_region.size.x-1;
              int max_y = clip_region.size.y-1;
              double x1, x2;
              if (buf32 != NULL)
                { // 32-bit pixel overlay painting
                  kdu_uint32 *dp, colr=colr_val;
                  for (int t=0; t < 3; t++)
                    { 
                      kdrc_trapezoid_follower *trp = trap+t;
                      int y = trp->get_y_min();
                      dp = buf32 + y*buffer_row_gap;
                      for (; trp->get_next_line(x1,x2);
                           dp+=buffer_row_gap, y++)
                        { 
                          int c1 = (int) floor(x1+0.5);
                          int c2 = (int) floor(x2+0.5);
                          c1 = (c1 < 0)?0:c1;
                          c2 = (c2 > max_x)?max_x:c2;
                          if (b == 0)
                            for (; c1 <= c2; c1++)
                              dp[c1] = colr;
                          else
                            paint_race_track(paint_params,colr,b,dp+c1,c2-c1,
                                             c1,max_x-c1,-y,max_y-y);
                        }
                    }
                }
              else
                { // floating-point overlay painting
                  float *dp, colr[4] =
                    { ((colr_val>>24)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>16)&0x0FF)*(1.0F/255.0F),
                      ((colr_val>>8)&0x0FF)*(1.0F/255.0F),
                      ((colr_val)&0x0FF)*(1.0F/255.0F) };
                  for (int t=0; t < 3; t++)
                    { 
                      kdrc_trapezoid_follower *trp = trap+t;
                      int y = trp->get_y_min();
                      dp = buf_float + y*buffer_row_gap;
                      for (; trp->get_next_line(x1,x2);
                           dp+=buffer_row_gap, y++)
                        { 
                          int c1 = (int) floor(x1+0.5);
                          int c2 = (int) floor(x2+0.5);
                          c1 = (c1 < 0)?0:c1;
                          c2 = (c2 > max_x)?max_x:c2;
                          if (b == 0)
                            for (c1<<=2, c2<<=2; c1 <= c2; c1+=4)
                              { 
                                dp[c1  ] = colr[0]; dp[c1+1] = colr[1];
                                dp[c1+2] = colr[2]; dp[c1+3] = colr[3];
                              }
                          else
                            paint_race_track(paint_params,colr,b,dp+4*c1,c2-c1,
                                             c1,max_x-c1,-y,max_y-y);
                        }
                    }
                }
            }
        }
    }
}

/*****************************************************************************/
/*              kdu_region_compositor::donate_compositing_buffer             */
/*****************************************************************************/

void
  kdu_region_compositor::donate_compositing_buffer(kdu_compositor_buf *buffer,
                                                   kdu_dims buffer_region,
                                                   kdu_coords buffer_size)
{
  assert(this->composition_buffer == NULL);
  assert(this->rendering_buffer_region == buffer_region);
  assert((active_layers != NULL) && active_layers->have_overlay_info);
  this->composition_buffer  = buffer;
  this->buffer_size = buffer_size;
}

/*****************************************************************************/
/*             kdu_region_compositor::retract_compositing_buffer             */
/*****************************************************************************/

bool
  kdu_region_compositor::retract_compositing_buffer(kdu_coords &buffer_size)
{
  assert(active_layers != NULL);
  if ((composition_buffer == NULL) || (active_layers->next != NULL) ||
      active_layers->have_alpha_channel || active_layers->have_overlay_info ||
      !fixed_composition_dims.is_empty())
    return false;
  composition_buffer = NULL;
  buffer_size = this->buffer_size;
  return true;
}

/*****************************************************************************/
/*                kdu_region_compositor::find_completed_rects                */
/*****************************************************************************/

bool
  kdu_region_compositor::find_completed_rects(kdu_dims &rendered_region,
                                              kdrc_layer *layer,
                                              int layer_elt,
                                              kdrc_layer **first_isect_layer,
                                              kdrc_layer **last_isect_layer)
{
  // Begin by determining if `region' belongs to a single overlay segment
  kdu_coords single_seg_idx;
  single_seg_idx.x = rendered_region.pos.x >> overlay_log2_segment_size;
  single_seg_idx.y = rendered_region.pos.y >> overlay_log2_segment_size;
  bool have_single_seg =
    ((rendered_region.pos.x+rendered_region.size.x) <=
     ((single_seg_idx.x+1)<<overlay_log2_segment_size)) &&
    ((rendered_region.pos.y+rendered_region.size.y) <=
     ((single_seg_idx.y+1)<<overlay_log2_segment_size));
    
  // Now progressively remove portions of `region' which we are sure will be
  // produced by future stream or overlay processing steps.  We remove portions
  // of `region' based on future overlay processing only if `region' fits
  // within a single overlay processing segment.  This makes things a lot
  // simpler and probably covers all cases of interest.
  kdu_dims rects[6];
  for (; layer != NULL; layer_elt=-1, layer=layer->next)
    {
      kdu_dims layer_region = layer->get_rendering_layer_region();
      if (!rendered_region.intersects(layer_region))
        continue;
      if (last_isect_layer != NULL)
        *last_isect_layer = layer;
      if ((first_isect_layer != NULL) && (*first_isect_layer == NULL))
        *first_isect_layer = layer;
      for (; layer_elt <= 2; layer_elt++)
        {
          int num_rects = 0;
          if (layer_elt < 2)
            {
              kdrc_stream *stream = layer->get_stream(layer_elt);
              if (stream == NULL)
                continue;
              if ((num_rects =
                   stream->find_non_pending_rects(rendered_region,rects))==0)
                {
                  rendered_region.size = kdu_coords(0,0);
                  return false;
                }
              assert(num_rects <= 6);
            }
          else
            {
              kdu_dims seg_rect;
              if ((!have_single_seg) ||
                  !layer->get_unprocessed_overlay_seg(single_seg_idx,seg_rect))
                continue;
              seg_rect &= rendered_region;
              if (seg_rect.is_empty())
                continue;
              if (seg_rect == rendered_region)
                { // Region covered by other unprocessed region
                  rendered_region.size = kdu_coords(0,0);
                  return false;
                }
          
              // Remove `seg_rect' from rendering_region, leaving up to 4 rects
              kdu_coords cover_min = seg_rect.pos;
              kdu_coords cover_lim = cover_min + seg_rect.size;
              kdu_coords region_min = rendered_region.pos;
              kdu_coords region_lim = region_min + rendered_region.size;
              int delta;
              kdu_dims *rc = rects;
              if ((delta = cover_min.y - region_min.y) > 0)
                { 
                  rc->pos.x=rendered_region.pos.x;
                  rc->size.x=rendered_region.size.x;
                  rc->pos.y=region_min.y;
                  rc->size.y=delta;
                  rendered_region.pos.y=cover_min.y;
                  rendered_region.size.y=region_lim.y-cover_min.y;
                  rc++; num_rects++;
                }
              if ((delta = region_lim.y - cover_lim.y) > 0)
                { 
                  rc->pos.x=rendered_region.pos.x;
                  rc->size.x=rendered_region.size.x;
                  rc->pos.y=cover_lim.y;
                  rc->size.y=delta;
                  rendered_region.size.y=cover_lim.y-rendered_region.pos.y;
                  rc++; num_rects++;
                }
              if ((delta = cover_min.x - region_min.x) > 0)
                { 
                  rc->pos.y=rendered_region.pos.y;
                  rc->size.y=rendered_region.size.y;
                  rc->pos.x=region_min.x;
                  rc->size.x=delta;
                  rc++; num_rects++;
                }
              if ((delta = region_lim.x - cover_lim.x) > 0)
                { 
                  rc->pos.y=rendered_region.pos.y;
                  rc->size.y=rendered_region.size.y;
                  rc->pos.x=cover_lim.x;
                  rc->size.x=delta;
                  rc++; num_rects++;
                }
            }
          assert(num_rects > 0);
          rendered_region = rects[0];
          if (num_rects > 1)
            {
              kdrc_layer *next_layer = layer;
              int next_layer_elt = layer_elt+1;
              if (next_layer_elt > 2)
                { next_layer=next_layer->next; next_layer_elt=0; }
              for (int n=1; n < num_rects; n++)
                find_completed_rects(rects[n],next_layer,next_layer_elt);
            }
        }
    }
  if ((first_isect_layer == NULL) || (last_isect_layer == NULL))
    {
      refresh_mgr->add_region(rendered_region);
      return false;
    }
  
  return true;
}
