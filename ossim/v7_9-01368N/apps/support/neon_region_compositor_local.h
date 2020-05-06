/*****************************************************************************/
// File: neon_region_compositor_local.h [scope = CORESYS/SUPPORT]
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
   Implements SIMD accelerated layer composition and alpha blending functions.
This file provides macros to arbitrate the selection of suitable SIMD
functions, if they exist, for ARM processors equipped with the NEON vector
processing unit.  The actual SIMD functions themselves appear within
"neon_region_compositor.cpp".
******************************************************************************/

#ifndef NEON_REGION_COMPOSITOR_LOCAL_H
#define NEON_REGION_COMPOSITOR_LOCAL_H
#include "kdu_arch.h"

namespace kd_supp_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                         Erase and Copy Functions                          */
/* ========================================================================= */

#define NEON_SET_ERASE_REGION_FUNC(_func)
#define NEON_SET_ERASE_REGION_FLOAT_FUNC(_func)
#define NEON_SET_COPY_REGION_FUNC(_func)
#define NEON_SET_COPY_REGION_FLOAT_FUNC(_func)
#define NEON_SET_RCOPY_REGION_FUNC(_func)
#define NEON_SET_RCOPY_REGION_FLOAT_FUNC(_func)

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
//----------------------------------------------------------------------------
extern void
  neon_erase_region(kdu_uint32 *dst, int height, int width, int row_gap,
                    kdu_uint32 erase);
#undef NEON_SET_ERASE_REGION_FUNC
#define NEON_SET_ERASE_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_erase_region; }
//----------------------------------------------------------------------------
extern void
  neon_erase_region_float(float *dst, int height, int width, int row_gap,
                          float erase[]);
#undef NEON_SET_ERASE_REGION_FLOAT_FUNC
#define NEON_SET_ERASE_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_erase_region_float; }
//----------------------------------------------------------------------------
extern void
  neon_copy_region(kdu_uint32 *dst, kdu_uint32 *src,
                   int height, int width, int dst_row_gap, int src_row_gap);
#undef NEON_SET_COPY_REGION_FUNC
#define NEON_SET_COPY_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_copy_region; }
//----------------------------------------------------------------------------
extern void
  neon_copy_region_float(float *dst, float *src, int height, int width,
                         int dst_row_gap, int src_row_gap);
#undef NEON_SET_COPY_REGION_FLOAT_FUNC
#define NEON_SET_COPY_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_copy_region_float; }
//----------------------------------------------------------------------------
extern void
  neon_rcopy_region(kdu_uint32 *dst, kdu_uint32 *src,
                   int height, int width, int row_gap);
#undef NEON_SET_RCOPY_REGION_FUNC
#define NEON_SET_RCOPY_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_rcopy_region; }
//----------------------------------------------------------------------------
extern void
  neon_rcopy_region_float(float *dst, float *src,
                          int height, int width, int row_gap);
#undef NEON_SET_RCOPY_REGION_FLOAT_FUNC
#define NEON_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_rcopy_region_float; }
//----------------------------------------------------------------------------
#endif // !KDU_NO_NEON
  
#define KDRC_SIMD_SET_ERASE_REGION_FUNC(_func) \
  { \
    NEON_SET_ERASE_REGION_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_ERASE_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_ERASE_REGION_FLOAT_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_COPY_REGION_FUNC(_func) \
  { \
    NEON_SET_COPY_REGION_FUNC(_func) \
  }

#define KDRC_SIMD_SET_COPY_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_COPY_REGION_FLOAT_FUNC(_func) \
  }
 
#define KDRC_SIMD_SET_RCOPY_REGION_FUNC(_func) \
  { \
    NEON_SET_RCOPY_REGION_FUNC(_func) \
  }
 
#define KDRC_SIMD_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_RCOPY_REGION_FLOAT_FUNC(_func) \
  }


/* ========================================================================= */
/*                              Blend Functions                              */
/* ========================================================================= */

#define NEON_SET_BLEND_REGION_FUNC(_func)
#define NEON_SET_BLEND_REGION_FLOAT_FUNC(_func)
#define NEON_SET_PREMULT_BLEND_REGION_FUNC(_func)
#define NEON_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func)
#define NEON_SET_SCALED_BLEND_REGION_FUNC(_func)
#define NEON_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func)
  
#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
//----------------------------------------------------------------------------
extern void
  neon_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                    int height, int width, int dst_row_gap, int src_row_gap);
#undef NEON_SET_BLEND_REGION_FUNC
#define NEON_SET_BLEND_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_blend_region; }
//----------------------------------------------------------------------------
extern void
  neon_blend_region_float(float *dst, float *src, int height, int width,
                          int dst_row_gap, int src_row_gap);
#undef NEON_SET_BLEND_REGION_FLOAT_FUNC
#define NEON_SET_BLEND_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_blend_region_float; }
//----------------------------------------------------------------------------
extern void
  neon_premult_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                            int height, int width, int dst_row_gap,
                            int src_row_gap);
#undef NEON_SET_PREMULT_BLEND_REGION_FUNC
#define NEON_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_premult_blend_region; }
//----------------------------------------------------------------------------
extern void
  neon_premult_blend_region_float(float *dst, float *src,
                                  int height, int width, int dst_row_gap,
                                  int src_row_gap);
#undef NEON_SET_PREMULT_BLEND_REGION_FLOAT_FUNC
#define NEON_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_premult_blend_region_float; }
//----------------------------------------------------------------------------
extern void
  neon_scaled_blend_region(kdu_uint32 *dst, kdu_uint32 *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, kdu_int16 alpha_factor_x128);
#undef NEON_SET_SCALED_BLEND_REGION_FUNC
#define NEON_SET_SCALED_BLEND_REGION_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_scaled_blend_region; }
//----------------------------------------------------------------------------
extern void
  neon_scaled_blend_region_float(float *dst, float *src,
                           int height, int width, int dst_row_gap,
                           int src_row_gap, float alpha_factor);
#undef NEON_SET_SCALED_BLEND_REGION_FLOAT_FUNC
#define NEON_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_scaled_blend_region_float; }
//----------------------------------------------------------------------------
#endif // !KDU_NO_NEON
    
#define KDRC_SIMD_SET_BLEND_REGION_FUNC(_func) \
  { \
    NEON_SET_BLEND_REGION_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_BLEND_REGION_FLOAT_FUNC(_func) \
  }
    
#define KDRC_SIMD_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  { \
    NEON_SET_PREMULT_BLEND_REGION_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_PREMULT_BLEND_REGION_FLOAT_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_SCALED_BLEND_REGION_FUNC(_func) \
  { \
    NEON_SET_SCALED_BLEND_REGION_FUNC(_func) \
  }
  
#define KDRC_SIMD_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
  { \
    NEON_SET_SCALED_BLEND_REGION_FLOAT_FUNC(_func) \
  }  
    
} // namespace kd_supp_simd

#endif // NEON_REGION_COMPOSITOR_LOCAL_H
