/*****************************************************************************/
// File: neon_multi_transform_local.h [scope = CORESYS/TRANSFORMS]
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
   Mediates access to NEON accelerated implementations of key multi-component
transform operations for the ARM processor.  Actual implementations are
provided in the separate file "neon_multi_transform_local.cpp".
******************************************************************************/

#ifndef NEON_MULTI_TRANSFORM_LOCAL_H
#define NEON_MULTI_TRANSFORM_LOCAL_H

#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                            Line Copy Functions                            */
/* ========================================================================= */
  
/*****************************************************************************/
/*                          ..._multi_line_rev_copy                          */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_multi_line_rev_copy(void *in_buf, void *out_buf, int num_samples,
                            bool using_shorts, int rev_offset);
#  define NEONI_SET_MULTI_LINE_REV_COPY_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func=neoni_multi_line_rev_copy; }
#else // No compilation support for NEON
#  define NEONI_SET_MULTI_LINE_REV_COPY_FUNC(_func)
#endif  

/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_REV_COPY_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_REV_COPY_FUNC(_func) \
  { \
    NEONI_SET_MULTI_LINE_REV_COPY_FUNC(_func); \
  }

/*****************************************************************************/
/*                         ..._multi_line_irrev_copy                         */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_multi_line_irrev_copy(void *in_buf, void *out_buf, int num_samples,
                              bool using_shorts, float irrev_offset);
#  define NEONI_SET_MULTI_LINE_IRREV_COPY_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func=neoni_multi_line_irrev_copy; }
#else // No compilation support for NEON
#  define NEONI_SET_MULTI_LINE_IRREV_COPY_FUNC(_func)
#endif  

/*****************************************************************************/
/* SELECTOR              KD_SET_SIMD_MC_IRREV_COPY_FUNC                      */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_IRREV_COPY_FUNC(_func) \
  { \
    NEONI_SET_MULTI_LINE_IRREV_COPY_FUNC(_func); \
  }

  
/* ========================================================================= */
/*                             Matrix Transforms                             */
/* ========================================================================= */

/*****************************************************************************/
/*                          ..._multi_matrix_float                           */
/*****************************************************************************/
  
#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_multi_matrix_float(void **in_bufs, void **out_bufs,
                           int num_samples, int num_inputs,
                           int num_outputs, float *coeffs, float *offsets);
#  define NEONI_SET_MATRIX_FLOAT_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func=neoni_multi_matrix_float; }
#else // No compilation support for NEON
#  define NEONI_SET_MATRIX_FLOAT_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_MATRIX32_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_MATRIX32_FUNC(_func) \
  { \
    NEONI_SET_MATRIX_FLOAT_FUNC(_func); \
  }

/*****************************************************************************/
/*                          ..._multi_matrix_fix16                           */
/*****************************************************************************/
  
#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_multi_matrix_fix16(void **in_bufs, void **out_bufs, kdu_int32 *acc,
                           int num_samples, int num_inputs, int num_outputs,
                           kdu_int16 *coeffs, int downshift, float *offsets);
#  define NEONI_SET_MATRIX_FIX16_FUNC(_func) \
  if (kdu_neon_level > 0) \
    {_func=neoni_multi_matrix_fix16; }
#else // No compilation support for NEON
#  define NEONI_SET_MATRIX_FIX16_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR               KD_SET_SIMD_MC_MATRIX16_FUNC                       */
/*****************************************************************************/
  
#define KD_SET_SIMD_MC_MATRIX16_FUNC(_func) \
  { \
    NEONI_SET_MATRIX_FIX16_FUNC(_func); \
  }
  
  
/* ========================================================================= */
/*                     NLT SMAG/UMAG Conversion Functions                    */
/* ========================================================================= */

/*****************************************************************************/
/*                             ..._smag_int32                                */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_smag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                   int precision, bool src_absolute, bool dst_absolute);
#  define NEONI_SET_SMAG32_FUNC(_func,_prec) \
  if ((kdu_neon_level > 0) && (_prec <= 32)) \
    {_func=neoni_smag_int32; }
#else // No compilation support for NEON
#  define NEONI_SET_SMAG32_FUNC(_func,_prec)
#endif
  
/*****************************************************************************/
/* SELECTOR                KD_SET_SIMD_MC_SMAG32_FUNC                        */
/*****************************************************************************/

#define KD_SET_SIMD_MC_SMAG32_FUNC(_func,_prec) \
  { \
    NEONI_SET_SMAG32_FUNC(_func,_prec); \
  }
  
/*****************************************************************************/
/*                             ..._umag_int32                                */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
extern void
  neoni_umag_int32(kdu_int32 *src, kdu_int32 *dst, int num_samples,
                   int precision, bool src_absolute, bool dst_absolute);
#  define NEONI_SET_UMAG32_FUNC(_func,_prec) \
  if ((kdu_neon_level > 0) && (_prec <= 32)) \
    {_func=neoni_umag_int32; }
#else // No compilation support for NEON
#  define NEONI_SET_UMAG32_FUNC(_func,_prec)
#endif

/*****************************************************************************/
/* SELECTOR                KD_SET_SIMD_MC_UMAG32_FUNC                        */
/*****************************************************************************/

#define KD_SET_SIMD_MC_UMAG32_FUNC(_func,_prec) \
  { \
    NEONI_SET_UMAG32_FUNC(_func,_prec); \
  }
  
  
} // namespace kd_core_simd

#endif // NEON_MULTI_TRANSFORM_LOCAL_H
