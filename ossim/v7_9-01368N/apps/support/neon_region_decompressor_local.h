/*****************************************************************************/
// File: neon_region_decompressor_local.h [scope = CORESYS/SUPPORT]
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

#ifndef NEON_REGION_DECOMPRESSOR_LOCAL_H
#define NEON_REGION_DECOMPRESSOR_LOCAL_H
#include "kdu_arch.h"

namespace kd_supp_simd {
  using namespace kdu_core;

/* ========================================================================= */
/*                         Data Conversion Functions                         */
/* ========================================================================= */

/*****************************************************************************/
/*                  ..._convert_and_copy_shorts_to_fix16                     */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
                                 const int types[], int num_lines,
                                 int src_precision, int missing_src_samples,
                                 void *void_dst, int dst_min, int num_samples,
                                 int dst_type, int float_exp_bits);
#  define NEON_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
    if ((kdu_neon_level > 0) && (_src_types & KDRD_SHORT_TYPE)) \
      {_func=neon_convert_and_copy_to_fix16; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types)
#endif
  
/*****************************************************************************/
/* SELECTOR         KDRD_SIMD_SET_CONVERT_COPY_FIX16_FUNC                    */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  { \
    NEON_SET_CONVERT_COPY_FIX16_FUNC(_func,_src_types) \
  }

/*****************************************************************************/
/*              ..._reinterpret_and_copy_to_unsigned_floats                  */
/*****************************************************************************/
  
#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neoni_reinterpret_and_copy_to_unsigned_floats(const void *bufs[],
                                                const int widths[],
                                                const int types[],
                                                int num_lines,
                                                int src_precision,
                                                int missing_src_samples,
                                                void *void_dst, int dst_min,
                                                int num_samples, int dst_type,
                                                int exponent_bits);
#  define NEONI_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec) \
  if ((kdu_neon_level > 0) && (_prec <= 32) && (_prec > _exp_bits) && \
      (_exp_bits <= 8) && ((_prec-1-_exp_bits) <= 23)) \
    {_func=neoni_reinterpret_and_copy_to_unsigned_floats; }
#else // No compilation support for NEON
#  define NEONI_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec)
#endif

/*****************************************************************************/
/*               ..._reinterpret_and_copy_to_signed_floats                   */
/*****************************************************************************/
  
#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neoni_reinterpret_and_copy_to_signed_floats(const void *bufs[],
                                              const int widths[],
                                              const int types[],
                                              int num_lines,
                                              int src_precision,
                                              int missing_src_samples,
                                              void *void_dst, int dst_min,
                                              int num_samples, int dst_type,
                                              int exponent_bits);
#  define NEONI_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec) \
  if ((kdu_neon_level > 0) && (_prec <= 32) && (_prec > _exp_bits) && \
      (_exp_bits <= 8) && ((_prec-1-_exp_bits) <= 23)) \
    {_func=neoni_reinterpret_and_copy_to_signed_floats; }
#else // No compilation support for NEON
#  define NEONI_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec)
#endif

/*****************************************************************************/
/* SELECTOR          KDRD_SIMD_SET_REINTERP_COPY_..._FUNC                    */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_REINTERP_COPY_UF_FUNC(_func,_exp_bits,_prec) \
  { \
    NEONI_SET_REINTERPRET_COPY_UFLOAT_FUNC(_func,_exp_bits,_prec) \
  }

#define KDRD_SIMD_SET_REINTERP_COPY_SF_FUNC(_func,_exp_bits,_prec) \
  { \
    NEONI_SET_REINTERPRET_COPY_SFLOAT_FUNC(_func,_exp_bits,_prec) \
  }

/*****************************************************************************/
/*                             ..._white_stretch                             */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_white_stretch(const kdu_int16 *src, kdu_int16 *dst, int num_samples,
                     int stretch_residual);
#  define NEON_SET_WHITE_STRETCH_FUNC(_func) \
  if (kdu_neon_level > 0) {_func=neon_white_stretch; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_WHITE_STRETCH_FUNC(_func)
#endif
  
/*****************************************************************************/
/* SELECTOR           KDRD_SIMD_SET_WHITE_STRETCH_FUNC                       */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_WHITE_STRETCH_FUNC(_func) \
  { \
    NEON_SET_WHITE_STRETCH_FUNC(_func) \
  }
    
/*****************************************************************************/
/*                    ..._transfer_fix16_to_bytes_gap1                       */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void  
  neon_transfer_fix16_to_bytes_gap1(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs);
  /* This function is installed only if there is no significant source scaling
   or source offset requirement, there is no clipping, and outputs are
   unsigned with at most 8 bit precision. */
  
#  define NEON_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func = neon_transfer_fix16_to_bytes_gap1; }
#else // No compilation support for ARM/NEON
#  define NEON_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func)
#endif
  
/*****************************************************************************/
/*                    ..._transfer_fix16_to_bytes_gap4                       */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void  
  neon_transfer_fix16_to_bytes_gap4(const void *src_buf, int src_p,
                                    int src_type, int skip_samples,
                                    int num_samples, void *dst,
                                    int dst_prec, int gap, bool leave_signed,
                                    float unused_src_scale,
                                    float unused_src_off,
                                    bool unused_clip_outputs);
  /* This function is installed only if there is no significant source scaling
   or source offset requirement, there is no clipping, and outputs are
   unsigned with at most 8 bit precision. */
  
#  define NEON_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func = neon_transfer_fix16_to_bytes_gap4; }
#else // No compilation support for ARM/NEON
#  define NEON_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func)
#endif  
  
/*****************************************************************************/
/* SELECTOR           KDRD_SIMD_SET_XFER_TO_BYTES_FUNC                       */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_XFER_TO_BYTES_FUNC(_func,_src_type,_gap,_prec,_signed) \
  { \
    if ((_src_type == KDRD_FIX16_TYPE) && (_prec <= 8) && !_signed) \
      { \
        if (_gap == 1) \
          { \
            NEON_TRANSFER_FIX16_TO_BYTES_GAP1_FUNC(_func) \
          } \
        else if (_gap == 4) \
          { \
            NEON_TRANSFER_FIX16_TO_BYTES_GAP4_FUNC(_func) \
          } \
      } \
  }
 
/*****************************************************************************/
/*                 ..._interleaved_transfer_fix16_to_bytes                   */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_interleaved_transfer_fix16_to_bytes(const void *src0, const void *src1,
                                           const void *src2, const void *src3,
                                           int src_prec, int src_type,
                                           int src_skip, int num_pixels,
                                           kdu_byte *byte_dst, int dst_prec,
                                           kdu_uint32 zmask, kdu_uint32 fmask);
#  define NEON_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
  if (kdu_neon_level > 0) \
    { _func = neon_interleaved_transfer_fix16_to_bytes; }
#else // No compilation support for ARM/NEON
#  define NEON_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func)
#endif

/*****************************************************************************/
/* SELECTOR       KDRD_SIMD_SET_INTERLEAVED_XFER_TO_BYTES_FUNC               */
/*****************************************************************************/
  
#define KDRD_SIMD_SET_INTERLEAVED_XFER_TO_BYTES_FUNC(_func,_type,_src_prec) \
  { \
    if ((_type == KDRD_FIX16_TYPE) && (_src_prec <= 8)) \
      { \
        NEON_INTERLEAVED_XFER_FIX16_TO_BYTES_FUNC(_func) \
      } \
  }


/* ========================================================================= */
/*                        Vertical Resampling Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/*                           ..._vert_resample_float                         */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_vert_resample_float(int length, float *src[], float *dst,
                           void *kernel, int kernel_length);
#  define NEON_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  if ((kdu_neon_level > 0) && ((_klen == 2) || (_klen == 6))) \
    {_func=neon_vert_resample_float; _vec_len=4; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif
  
/*****************************************************************************/
/*               KDRD_SET_SIMD_VERT_FLOAT_RESAMPLE_FUNC selector             */
/*****************************************************************************/
  
#define KDRD_SET_SIMD_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  { \
    NEON_SET_VERT_FLOAT_RESAMPLE_FUNC(_klen,_func,_vec_len); \
  }
  
/*****************************************************************************/
/*                          ..._vert_resample_fix16                          */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_vert_resample_fix16(int length, kdu_int16 *src[], kdu_int16 *dst,
                           void *kernel, int kernel_length);
#  define NEON_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  if ((kdu_neon_level > 0) && ((_klen == 2) || (_klen == 6))) \
    {_func=neon_vert_resample_fix16; _vec_len=8; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len)
#endif
  
/*****************************************************************************/
/*               KDRD_SET_SIMD_VERT_FIX16_RESAMPLE_FUNC selector             */
/*****************************************************************************/
  
#define KDRD_SET_SIMD_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len) \
  { \
    NEON_SET_VERT_FIX16_RESAMPLE_FUNC(_klen,_func,_vec_len); \
  }
  
  
/* ========================================================================= */
/*                   Horizontal Resampling Functions (float)                 */
/* ========================================================================= */
  
/*****************************************************************************/
/*                          ,,,_horz_resample_float                          */
/*****************************************************************************/

#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_horz_resample_float(int, float *, float *,
                            kdu_uint32, kdu_uint32, kdu_uint32,
                            int, void **, int, int, int);
#define NEON_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb) \
    if ((kdu_neon_level > 0) && ((_klen == 2) || (_klen == 6)) && \
        ((_klen == 6) || ((2.0 * _exp) > 3.0))) \
      { _func=neon_horz_resample_float; _vec_len=4; _bv=0; _bb=0; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bb)
#endif

/*****************************************************************************/
/*               KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC selector             */
/*****************************************************************************/
  
#define KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb)\
  { \
    NEON_SET_HORZ_FLOAT_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bb); \
  }
  /* Inputs:
       _klen is the length of the scalar kernel (2 or 6);
       _exp is the amount of expansion yielded by the kernel (< 1 = reduction)
     Outputs:
       _func becomes the deduced function (not set if there is none available)
       _vlen is the vector length (4 for SSE/SSE2/SSSE3/NEON, 8 for AVX/AVX2)
       _bv becomes the number of blend vectors B per kernel tap (0 if the
           implementation is not based on permutation/shuffle instructions)
       _bb is set to the number of bytes in each permutation element:
           1 if shuffle instructions have 8-bit elements;
           4 if shuffle instructions have 32-bit elements.
           Other values are not defined.
           Blend vectors set each element to the index of the element from
           which they are taken, or else they fill the element with _bb bytes
           that are all equal to 0x80, meaning no element is to be sourced.
  */
  
  
/* ========================================================================= */
/*                   Horizontal Resampling Functions (fix16)                 */
/* ========================================================================= */
  
/*****************************************************************************/
/* STATIC                   ,,,_horz_resample_fix16                          */
/*****************************************************************************/
  
#if (defined KDU_NEON_INTRINSICS) && (!defined KDU_NO_NEON)
extern void
  neon_horz_resample_fix16(int, kdu_int16 *, kdu_int16 *,
                           kdu_uint32, kdu_uint32, kdu_uint32,
                           int, void **, int, int, int);
#define NEON_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh) \
  if ((kdu_neon_level > 0) && ((_klen == 2) || (_klen == 6)) && \
      ((_klen == 6) || ((4.0 * _exp) > 7.0))) \
    { _func=neon_horz_resample_fix16; _vec_len=8; _bv=0; _bh=0; }
#else // No compilation support for ARM/NEON
#  define NEON_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vec_len,_bv,_bh)
#endif
   
/*****************************************************************************/
/*               KDRD_SET_SIMD_HORZ_FIX16_RESAMPLE_FUNC selector             */
/*****************************************************************************/
  
#define KDRD_SET_SIMD_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh)\
  { \
    NEON_SET_HORZ_FIX16_RESAMPLE_FUNC(_klen,_exp,_func,_vlen,_bv,_bh); \
  }
  /* Inputs:
       _klen is the length of the scalar kernel (2 or 6);
       _exp is the amount of expansion yielded by the kernel (< 1 = reduction)
     Outputs:
       _func becomes the deduced function (not set if there is none available)
       _vlen is the vector length (8 for SSE/SSE2/SSSE3/NEON, 16 for AVX2)
       _bv becomes the number of blend vectors B per kernel tap (0 if the
           implementation is not based on permutation/shuffle instructions)
       _bh is meaningful only when _bv > 0, with the following interpretation:
           _bh=0 means that each blend vector performs permutation on a full
                 length source vector.  In this case, kernels are expected
                 to hold _klen * _bv blend vectors.
           _bh=1 means that each blend vector operates on a half-length
                 (_vlen/2-element) source vector, mapping its elements to all
                 `_vlen' elements of the permuted outputs that are blended
                 together to form the kernel inputs.  Note that the "h" in
                 "_bh" is intended to stand for "half".  Also, in this case,
                 the kernels are only required to hold _bv blend vectors,
                 corresponding to the first kernel tap (k=0).  A succession of
                 _klen progressively shifted half-length source vectors are
                 read from the input and exposed to this single set of
                 permutations (blend vectors) to generate the full set of
                 inputs to the interpolation kernels.  In some cases, this
                 allows _bv to be as small as 1, even though the source
                 vectors are only of half length.  See the extensive notes
                 appearing with the definition of `kdrd_simd_horz_fix16_func'
                 for more on this.
           Other values are not defined.
           Blend vectors for fixed-point processing are always byte oriented,
           so there is no need for this macro to provide a "_bb" argument,
           as found in `KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC'.
  */
  
  
} // namespace kd_supp_simd

#endif // NEON_REGION_DECOMPRESSOR_LOCAL_H
