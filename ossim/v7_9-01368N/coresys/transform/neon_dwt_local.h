/*****************************************************************************/
// File: neon_dwt_local.h [scope = CORESYS/TRANSFORMS]
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
NEON intrinsics.  These can be compiled under GCC or .NET and are compatible
with both 32-bit and 64-bit builds.
   The implementations themselves are found in the separate source file
"neon_dwt_local.cpp", with this header file being used to declare and
 select function pointers.
******************************************************************************/

#ifndef NEON_DWT_LOCAL_H
#define NEON_DWT_LOCAL_H

#include "transform_base.h"

namespace kd_core_simd {
  using namespace kd_core_local;

// Safe "static initializer" logic
//-----------------------------------------------------------------------------
#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void neon_dwt_local_static_init();
static bool neon_dwt_local_static_inited=false;
# define NEON_DWT_DO_STATIC_INIT() \
if (!neon_dwt_local_static_inited) \
  { if (kdu_neon_level > 0) neon_dwt_local_static_init(); \
    neon_dwt_local_static_inited=true; }
#else // No compilation support for ARM-NEON
# define NEON_DWT_DO_STATIC_INIT() /* Nothing to do */
#endif
//-----------------------------------------------------------------------------


/* ========================================================================= */
/*                            Interleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/*                            ..._interleave_16                              */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_upshifted_interleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int,int);
  extern void
    neoni_interleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int,int);
//-----------------------------------------------------------------------------
#  define NEONI_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) \
if ((kdu_neon_level > 0) && (_pairs >= 16)) \
  { \
    if (_upshift == 0) _tgt = neoni_interleave_16; \
    else _tgt = neoni_upshifted_interleave_16; \
  }
#else // No compilation support for NEON
#  define NEONI_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------
#define KD_SET_SIMD_INTERLEAVE_16_FUNC(_tgt,_pairs,_upshift) \
  { \
    NEONI_SET_INTERLEAVE_16(_tgt,_pairs,_upshift); \
  }

/*****************************************************************************/
/*                            ..._interleave_32                              */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void neoni_interleave_32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
//-----------------------------------------------------------------------------
#  define NEONI_SET_INTERLEAVE_32(_tgt,_pairs) \
if ((kdu_neon_level > 0) && (_pairs >= 8)) \
  { \
    _tgt = neoni_interleave_32; \
  }
#else // No compilation support for NEON
#  define NEONI_SET_INTERLEAVE_32(_tgt,_pairs) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_INTERLEAVE_32_FUNC(_tgt,_pairs) \
  { \
    NEONI_SET_INTERLEAVE_32(_tgt,_pairs); \
  }



/* ========================================================================= */
/*                          Deinterleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/*                           ..._deinterleave_16                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_downshifted_deinterleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,
                                      int,int);
  extern void
    neoni_deinterleave_16(kdu_int16 *,kdu_int16 *,kdu_int16 *,int,int);
//-----------------------------------------------------------------------------
#  define NEONI_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) \
if ((kdu_neon_level > 0) && (_pairs >= 16)) \
  { \
    if (_downshift == 0) _tgt = neoni_deinterleave_16; \
    else _tgt = neoni_downshifted_deinterleave_16; \
  }
#else // No compilation support for NEON
#  define NEONI_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_DEINTERLEAVE_16_FUNC(_tgt,_pairs,_downshift) \
{ \
  NEONI_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift); \
}


/*****************************************************************************/
/*                           ..._deinterleave_32                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_deinterleave_32(kdu_int32 *,kdu_int32 *,kdu_int32 *,int);
//-----------------------------------------------------------------------------
#  define NEONI_SET_DEINTERLEAVE_32(_tgt,_pairs) \
   if ((kdu_neon_level > 0) && (_pairs >= 8)) \
      { \
        _tgt = neoni_deinterleave_32; \
      }
#else // No compilation support for ARM-NEON
#  define NEONI_SET_DEINTERLEAVE_32(_tgt,_pairs) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_DEINTERLEAVE_32_FUNC(_tgt,_pairs) \
{ \
  NEONI_SET_DEINTERLEAVE_32(_tgt,_pairs); \
}



/* ========================================================================= */
/*                 Vertical Lifting Step Functions (16-bit)                  */
/* ========================================================================= */

/*****************************************************************************/
/*                             neoni_vlift_16_...                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_2tap_synth(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                              int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_4tap_synth(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                              int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_2tap_analysis(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                 int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_4tap_analysis(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                 int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_5x3_synth_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_5x3_synth_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_5x3_analysis_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_5x3_analysis_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_9x7_synth_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_9x7_synth_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_9x7_synth_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                 int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_16_9x7_analysis_s0(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_9x7_analysis_s1(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                   int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_16_9x7_analysis_s23(kdu_int16 **, kdu_int16 *, kdu_int16 *,
                                    int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define NEONI_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_neon_level > 0) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_16_5x3_synth_s0; \
          else \
            _func = neoni_vlift_16_5x3_synth_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_16_9x7_synth_s0; \
          else if (_step->step_idx == 1) \
            _func = neoni_vlift_16_9x7_synth_s1; \
          else \
            _func = neoni_vlift_16_9x7_synth_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = neoni_vlift_16_2tap_synth; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = neoni_vlift_16_4tap_synth; _add_first = false; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_16_5x3_analysis_s0; \
          else \
            _func = neoni_vlift_16_5x3_analysis_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_16_9x7_analysis_s0; \
          else if (_step->step_idx == 1) \
            _func = neoni_vlift_16_9x7_analysis_s1; \
          else \
            _func = neoni_vlift_16_9x7_analysis_s23; \
        } \
      else if ((_step->support_length > 0) && (_step->support_length <= 2)) \
        { \
          _func = neoni_vlift_16_2tap_analysis; _add_first = false; \
        } \
      else if ((_step->support_length > 2) && (_step->support_length <= 4)) \
        { \
          _func = neoni_vlift_16_4tap_analysis; _add_first = false; \
        } \
    } \
}
#else // No compilation support for NEON
#  define NEONI_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  NEONI_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  NEON_DWT_DO_STATIC_INIT() \
}


/* ========================================================================= */
/*                 Vertical Lifting Step Functions (32-bit)                  */
/* ========================================================================= */

/*****************************************************************************/
/*                            neoni_vlift_32_...                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_32_2tap_irrev(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                              int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_32_4tap_irrev(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                              int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_32_2tap_rev_synth(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                  int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_32_4tap_rev_synth(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                  int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_32_2tap_rev_analysis(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                     int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_32_4tap_rev_analysis(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                     int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_32_5x3_synth_s0(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_32_5x3_synth_s1(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_vlift_32_5x3_analysis_s0(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                   int, kd_lifting_step *, bool);
  extern void
    neoni_vlift_32_5x3_analysis_s1(kdu_int32 **, kdu_int32 *, kdu_int32 *,
                                   int, kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define NEONI_SET_VLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_neon_level > 0) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_32_5x3_synth_s0; \
          else \
            _func = neoni_vlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_vlift_32_2tap_rev_synth; \
          else if (_step->support_length <= 4) \
            _func = neoni_vlift_32_4tap_rev_synth; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = neoni_vlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = neoni_vlift_32_5x3_analysis_s0; \
          else \
            _func = neoni_vlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && _step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_vlift_32_2tap_rev_analysis; \
          else if (_step->support_length <= 4) \
            _func = neoni_vlift_32_4tap_rev_analysis; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_vlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = neoni_vlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for ARM-NEON
#  define NEONI_SET_VLIFT_32_FUNC(_func,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_32_FUNC(_func,_step,_synthesis) \
{ \
  NEONI_SET_VLIFT_32_FUNC(_func,_step,_synthesis); \
  NEON_DWT_DO_STATIC_INIT() \
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (16-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/*                             neoni_hlift_16_...                            */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_16_5x3_synth_s0(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_5x3_synth_s1(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_16_5x3_analysis_s0(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_5x3_analysis_s1(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_16_9x7_synth_s0(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_9x7_synth_s1(kdu_int16 *, kdu_int16 *, int,
                                kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_9x7_synth_s23(kdu_int16 *, kdu_int16 *, int,
                                 kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_16_9x7_analysis_s0(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_9x7_analysis_s1(kdu_int16 *, kdu_int16 *, int,
                                   kd_lifting_step *, bool);
  extern void
    neoni_hlift_16_9x7_analysis_s23(kdu_int16 *, kdu_int16 *, int,
                                    kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define NEONI_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_neon_level > 0) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_16_5x3_synth_s0; \
          else \
            _func = neoni_hlift_16_5x3_synth_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_16_9x7_synth_s0; \
          else if (_step->step_idx == 1) \
            _func = neoni_hlift_16_9x7_synth_s1; \
          else \
            _func = neoni_hlift_16_9x7_synth_s23; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          _add_first = true; \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_16_5x3_analysis_s0; \
          else \
            _func = neoni_hlift_16_5x3_analysis_s1; \
        } \
      else if (_step->kernel_id == Ckernels_W9X7) \
        { \
          _add_first = (_step->step_idx != 1); \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_16_9x7_analysis_s0; \
          else if (_step->step_idx == 1) \
            _func = neoni_hlift_16_9x7_analysis_s1; \
          else \
            _func = neoni_hlift_16_9x7_analysis_s23; \
        } \
    } \
}
#else // No compilation support for ARM-NEON
#  define NEONI_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  NEONI_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
  NEON_DWT_DO_STATIC_INIT() \
}


/* ========================================================================= */
/*                  Horizontal Lifting Step Functions (32-bit)               */
/* ========================================================================= */

/*****************************************************************************/
/*                            neoni_hlift_32_...                             */
/*****************************************************************************/

#ifdef KDU_NEON_INTRINSICS
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_32_2tap_irrev(kdu_int32 *, kdu_int32 *, int,
                              kd_lifting_step *, bool);
  extern void
    neoni_hlift_32_4tap_irrev(kdu_int32 *, kdu_int32 *, int,
                              kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_32_5x3_synth_s0(kdu_int32 *, kdu_int32 *, int,
                                kd_lifting_step *, bool);
  extern void
    neoni_hlift_32_5x3_synth_s1(kdu_int32 *, kdu_int32 *, int,
                                kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
  extern void
    neoni_hlift_32_5x3_analysis_s0(kdu_int32 *, kdu_int32 *, int,
                                   kd_lifting_step *, bool);
  extern void
    neoni_hlift_32_5x3_analysis_s1(kdu_int32 *, kdu_int32 *, int,
                                   kd_lifting_step *, bool);
//-----------------------------------------------------------------------------
#  define NEONI_SET_HLIFT_32_FUNC(_func,_step,_synthesis) \
if (kdu_neon_level > 0) \
{ \
  if (_synthesis) \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_32_5x3_synth_s0; \
          else \
            _func = neoni_hlift_32_5x3_synth_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = neoni_hlift_32_4tap_irrev; \
        } \
    } \
  else \
    { \
      if (_step->kernel_id == Ckernels_W5X3) \
        { \
          if (_step->step_idx == 0) \
            _func = neoni_hlift_32_5x3_analysis_s0; \
          else \
            _func = neoni_hlift_32_5x3_analysis_s1; \
        } \
      else if ((_step->support_length > 0) && !_step->reversible) \
        { \
          if (_step->support_length <= 2) \
            _func = neoni_hlift_32_2tap_irrev; \
          else if (_step->support_length <= 4) \
            _func = neoni_hlift_32_4tap_irrev; \
        } \
    } \
}
#else // No compilation support for ARM-NEON
#  define NEONI_SET_HLIFT_32_FUNC(_func,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_32_FUNC(_func,_step,_synthesis) \
{ \
  NEONI_SET_HLIFT_32_FUNC(_func,_step,_synthesis); \
  NEON_DWT_DO_STATIC_INIT() \
}

} // namespace kd_core_simd

#endif // NEON_DWT_LOCAL_H
