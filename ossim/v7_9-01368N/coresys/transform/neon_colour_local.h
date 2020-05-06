/*****************************************************************************/
// File: neon_colour_local.h [scope = CORESYS/TRANSFORMS]
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
   Implements the forward and reverse colour transformations -- both the
reversible (RCT) and the irreversible (ICT = RGB to YCbCr) -- using
ARM-NEON intrinsics.  The intrinsics can be compiled under GCC/CLANG or .NET
and are compatible with both 32-bit and 64-bit builds.  We could consider
including more heavily optimized inline-assembly versions of these functions
which could be selected automatically via the macros defined here; however,
considering that the assembly realization would need to be customized to
32- and 64-bit ARM variants, this seems rather a lot of work for potentially
very little gain right now.
   The implementations themselves are found in the separate source file
"neon_colour_local.cpp", with this header file being used to declare and
select function pointers.
******************************************************************************/

#ifndef NEON_COLOUR_LOCAL_H
#define NEON_COLOUR_LOCAL_H

#include "kdu_arch.h"

namespace kd_core_simd {
  using namespace kdu_core;

/*****************************************************************************/
/* STATIC                  ..._rgb_to_ycc_irrev16                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void
    neoni_rgb_to_ycc_irrev16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int);
#  define NEONI_SET_RGB_TO_YCC_IRREV16(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_rgb_to_ycc_irrev16;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_RGB_TO_YCC_IRREV16(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_RGB_TO_YCC_IRREV16(_tgt) \
  { \
    NEONI_SET_RGB_TO_YCC_IRREV16(_tgt); \
  }

/*****************************************************************************/
/* STATIC                  ..._rgb_to_ycc_irrev32                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_rgb_to_ycc_irrev32(float *,float *,float *,int);
#  define NEONI_SET_RGB_TO_YCC_IRREV32(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_rgb_to_ycc_irrev32;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_RGB_TO_YCC_IRREV32(_tgt) /* Do nothing */
#endif


#define KD_SET_SIMD_FUNC_RGB_TO_YCC_IRREV32(_tgt) \
  { \
    NEONI_SET_RGB_TO_YCC_IRREV32(_tgt); \
  }

/*****************************************************************************/
/* STATIC                  ..._ycc_to_rgb_irrev16                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void
    neoni_ycc_to_rgb_irrev16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int);
#  define NEONI_SET_YCC_TO_RGB_IRREV16(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_ycc_to_rgb_irrev16;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_YCC_TO_RGB_IRREV16(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_YCC_TO_RGB_IRREV16(_tgt) \
  { \
    NEONI_SET_YCC_TO_RGB_IRREV16(_tgt); \
  }

/*****************************************************************************/
/* STATIC                  ..._ycc_to_rgb_irrev32                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_ycc_to_rgb_irrev32(float *,float *,float *,int);
#  define NEONI_SET_YCC_TO_RGB_IRREV32(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_ycc_to_rgb_irrev32;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_YCC_TO_RGB_IRREV32(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_YCC_TO_RGB_IRREV32(_tgt) \
  { \
    NEONI_SET_YCC_TO_RGB_IRREV32(_tgt); \
  }

/*****************************************************************************/
/* STATIC                   ..._rgb_to_ycc_rev16                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_rgb_to_ycc_rev16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int);
#  define NEONI_SET_RGB_TO_YCC_REV16(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_rgb_to_ycc_rev16;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_RGB_TO_YCC_REV16(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_RGB_TO_YCC_REV16(_tgt) \
  { \
    NEONI_SET_RGB_TO_YCC_REV16(_tgt); \
  }

/*****************************************************************************/
/* STATIC                   ..._rgb_to_ycc_rev32                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_rgb_to_ycc_rev32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
#  define NEONI_SET_RGB_TO_YCC_REV32(_tgt) \
if (kdu_get_neon_level() > 0) _tgt=neoni_rgb_to_ycc_rev32;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_RGB_TO_YCC_REV32(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_RGB_TO_YCC_REV32(_tgt) \
  { \
    NEONI_SET_RGB_TO_YCC_REV32(_tgt); \
  }

/*****************************************************************************/
/* STATIC                   ..._ycc_to_rgb_rev16                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_ycc_to_rgb_rev16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int);
#  define NEONI_SET_YCC_TO_RGB_REV16(_tgt) \
   if (kdu_get_neon_level() > 0) _tgt=neoni_ycc_to_rgb_rev16;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_YCC_TO_RGB_REV16(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_YCC_TO_RGB_REV16(_tgt) \
  { \
    NEONI_SET_YCC_TO_RGB_REV16(_tgt); \
  }

/*****************************************************************************/
/* STATIC                   ..._ycc_to_rgb_rev32                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
  extern void neoni_ycc_to_rgb_rev32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
#  define NEONI_SET_YCC_TO_RGB_REV32(_tgt) \
if (kdu_get_neon_level() > 0) _tgt=neoni_ycc_to_rgb_rev32;
#else // NEON Intrinsic implementation not offered
#  define NEONI_SET_YCC_TO_RGB_REV32(_tgt) /* Do nothing */
#endif

#define KD_SET_SIMD_FUNC_YCC_TO_RGB_REV32(_tgt) \
  { \
    NEONI_SET_YCC_TO_RGB_REV32(_tgt); \
  }
  
} // namespace kd_core_simd

#endif // NEON_COLOUR_LOCAL_H
