/*****************************************************************************/
// File: neon_encoder_local.h [scope = CORESYS/CODING]
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
data between DWT line-based processing engine and block encoder.  The
implementation here is based on ARM-NEON intrinsics.  These
can be compiled under GCC or .NET and are compatible with both 32-bit and
64-bit builds.
   The implementations themselves are provided in the separately compiled
file, "neon_coder_local.cpp" so that the entire code base need not depend on
support for NEON instructions.
   This file contains optimizations for the reverse (dequantization)
transfer of data from code-blocks to lines.
******************************************************************************/
#ifndef NEON_ENCODER_LOCAL_H
#define NEON_ENCODER_LOCAL_H

#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                      SIMD functions used for encoding                     */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                 ..._quantize32_rev_block16                         */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern kdu_int32
    neoni_quantize32_rev_block16(kdu_int32 *,void **,
                                 int,int,int,int,int,float);
#  define NEONI_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width) \
          if ((kdu_neon_level > 0) && (_nom_width >= 8)) \
            _tgt = neoni_quantize32_rev_block16;
#else // No compilation support for ARM-NEON
#  define NEONI_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        NEONI_SET_BLOCK_QUANT32_REV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                 ..._quantize32_rev_block32                         */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern kdu_int32
    neoni_quantize32_rev_block32(kdu_int32 *,void **,
                                 int,int,int,int,int,float);
#  define NEONI_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width) \
          if ((kdu_neon_level > 0) && (_nom_width >= 4)) \
            _tgt = neoni_quantize32_rev_block32;
#else // No compilation support for ARM-NEON
#  define NEONI_SET_BLOCK_QUANT32_REV32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_REV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        NEONI_SET_BLOCK_QUANT32_REV32(_tgt,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                ..._quantize32_irrev_block16                        */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern kdu_int32
    neoni_quantize32_irrev_block16(kdu_int32 *,void **,
                                   int,int,int,int,int,float);
#  define NEONI_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width) \
          if ((kdu_neon_level > 0) && (_nom_width >= 8)) \
            _tgt = neoni_quantize32_irrev_block16;
#else // No compilation support for ARM-NEON
#  define NEONI_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV16(_tgt,_tr,_vf,_hf,_kmax,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        NEONI_SET_BLOCK_QUANT32_IRREV16(_tgt,_kmax,_nw); \
      } \
  }

/*****************************************************************************/
/* STATIC                 ..._quantize32_irrev_block32                       */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern kdu_int32
    neoni_quantize32_irrev_block32(kdu_int32 *,void **,
                                   int,int,int,int,int,float);
#  define NEONI_SET_BLOCK_QUANT32_IRREV32(_tgt,_nom_width) \
          if ((kdu_neon_level > 0) && (_nom_width >= 4)) \
            _tgt = neoni_quantize32_irrev_block32;
#else // No compilation support for ARM-NEON
#  define NEONI_SET_BLOCK_QUANT32_IRREV32(_tgt,_nom_width)
#endif

#define KD_SET_SIMD_FUNC_BLOCK_QUANT32_IRREV32(_tgt,_tr,_vf,_hf,_nw) \
  { \
    if (!(_tr || _vf || _hf)) \
      { \
        NEONI_SET_BLOCK_QUANT32_IRREV32(_tgt,_nw); \
      } \
  }
  
} // namespace kd_core_simd

#endif // NEON_ENCODER_LOCAL_H
