/*****************************************************************************/
// File: gcc_dwt_altivec_local.h [scope = CORESYS/TRANSFORMS]
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
in-line Altivec PIM code.
******************************************************************************/

#ifndef GCC_DWT_ALTIVEC_LOCAL_H
#define GCC_DWT_ALTIVEC_LOCAL_H

#include <altivec.h>
#include "transform_base.h"

namespace kd_core_simd {
  using namespace kd_core_local;

#define W97_FACT_0 ((float) -1.586134342)
#define W97_FACT_1 ((float) -0.052980118)
#define W97_FACT_2 ((float)  0.882911075)
#define W97_FACT_3 ((float)  0.443506852)

static kdu_int16 simd_w97_rem[4] =
  {(kdu_int16) floor(0.5 + (W97_FACT_0+2.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_1*(double)(1<<19)),
   (kdu_int16) floor(0.5 + (W97_FACT_2-1.0)*(double)(1<<16)),
   (kdu_int16) floor(0.5 + W97_FACT_3*(double)(1<<16))};

/* ========================================================================= */
/*                            Interleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     ..._interleave_16                              */
/*****************************************************************************/

#if (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void vec_upshifted_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                                        kdu_int16 *dst, int pairs,
                                        int upshift)
{
  vector signed short in1, in2;
  unsigned short short_upshift = upshift;
  vector unsigned short shift = vec_lde(0, &(short_upshift));
     // loads the shift somewhere in the vector register
  shift = vec_splat(vec_perm(shift, shift,
                             vec_lvsl(0, &(short_upshift))), 0);
     // splats it to all elements.
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      in1 = vec_sl((vector signed short) vec_ld(-8, src1), shift);
      in2 = vec_sl((vector signed short) vec_ld(-8, src2), shift);
      vec_st(vec_mergel(in1, in2), 0x00, dst);
      src1 += 4;
      src2 += 4;
      dst += 8;
      pairs -= 4;
    }
  for(; pairs > 4; pairs-=8, src1+=8, src2+=8, dst+=16)
    { 
      in1 = vec_sl((vector signed short) vec_ld(0, src1), shift);
      in2 = vec_sl((vector signed short) vec_ld(0, src2), shift);
      vec_st(vec_mergeh(in1, in2), 0x00, dst);
      vec_st(vec_mergel(in1, in2), 0x10, dst);
    }
  if (pairs > 0)
    { // Need to generate one more group of 8 outputs
      in1 = vec_sl((vector signed short) vec_ld(0, src1), shift);
      in2 = vec_sl((vector signed short) vec_ld(0, src2), shift);
      vec_st(vec_mergeh(in1, in2), 0x00, dst);
    }
}
//-----------------------------------------------------------------------------
static void vec_interleave_16(kdu_int16 *src1, kdu_int16 *src2,
                              kdu_int16 *dst, int pairs, int upshift)
{
  assert(upshift == 0);
  vector signed short in1, in2;
  if (_addr_to_kdu_int32(src1) & 8)
    { // Source addresses are 8-byte aligned, but not 16-byte aligned
      in1 = vec_ld(-8, src1);
      in2 = vec_ld(-8, src2);
      vec_st(vec_mergel(in1, in2), 0x00, dst);
      src1 += 4;
      src2 += 4;
      dst += 8;
      pairs -= 4;
    }
  for(; pairs > 4; pairs-=8, src1+=8, src2+=8, dst+=16)
    { 
      in1 = vec_ld(0, src1);
      in2 = vec_ld(0, src2);
      vec_st(vec_mergeh(in1, in2), 0x00, dst);
      vec_st(vec_mergel(in1, in2), 0x10, dst);
    }
  if (pairs > 0)
    { // Need to generate one more group of 8 outputs
      in1 = vec_ld(0, src1);
      in2 = vec_ld(0, src2);
      vec_st(vec_mergeh(in1, in2), 0x00, dst);
    }
}
//-----------------------------------------------------------------------------
#  define VEC_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) \
if (kdu_altivec_exists && (_pairs >= 8)) \
  { \
    if (_upshift == 0) _tgt = vec_interleave_16; \
    else _tgt = vec_upshifted_interleave_16; \
  }
#else // No alignment support for 128-bit Altivec implementations
#  define VEC_SET_INTERLEAVE_16(_tgt,_pairs,_upshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_INTERLEAVE_16_FUNC(_tgt,_pairs,_upshift) \
{ \
  VEC_SET_INTERLEAVE_16(_tgt,_pairs,_upshift); \
}

/*****************************************************************************/
/* STATIC                     ..._interleave_32                              */
/*****************************************************************************/

#define KD_SET_SIMD_INTERLEAVE_32_FUNC(_tgt,_pairs) /* Do nothing */


/* ========================================================================= */
/*                          Deinterleave Functions                           */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    ..._deinterleave_16                             */
/*****************************************************************************/

#if (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void vec_downshifted_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                            kdu_int16 *dst2, int pairs,
                                            int downshift)
{
  vector signed short in1, in2;
  int count = (pairs+7)>>3;
  
  // The vector instruction vec_pack can be used to deinterleave the low
  // halfwords of each vector.  This control vector does the same thing
  // with the high halfwords when used with vec_perm.
  vector unsigned char packhigh =
  (vector unsigned char){0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d,
                         0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1c, 0x1d};
  unsigned short short_downshift = downshift;
  vector unsigned short shift = vec_lde(0, &(short_downshift));
  // loads the shift somewhere in the vector register
  shift = vec_splat(vec_perm(shift, shift,
                             vec_lvsl(0, &(short_downshift))), 0);
  // splats it to all elements.
  
  // The compiler should do this loop using the count register.
  for(; count; count--)
    { 
      in1 = vec_sra((vector signed short) vec_ld(0x00, src), shift);
      in2 = vec_sra((vector signed short) vec_ld(0x10, src), shift);
      vec_st(vec_perm(in1, in2, packhigh), 0x00, dst1);
      vec_st(vec_pack((vector signed int) in1,
                      (vector signed int) in2), 0x00, dst2);
      src += 16;
    dst1 += 8;
      dst2 += 8;
    }
}
//-----------------------------------------------------------------------------
static void vec_deinterleave_16(kdu_int16 *src, kdu_int16 *dst1,
                                kdu_int16 *dst2, int pairs, int downshift)
{
  assert(downshift == 0);
  vector signed short in1, in2;
  int count = (pairs+7)>>3;
  
  // The vector instruction vec_pack can be used to deinterleave the low
  // halfwords of each vector.  This control vector does the same thing with
  // the high halfwords when used with vec_perm.
  vector unsigned char packhigh =
  (vector unsigned char){0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d,
                         0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1c, 0x1d};
  
  // The compiler should do this loop using the count register.
  for(; count; count--)
    { 
      in1 = vec_ld(0x00, src);
      in2 = vec_ld(0x10, src);
      vec_st(vec_perm(in1, in2, packhigh), 0x00, dst1);
      vec_st(vec_pack((vector signed int) in1,
                      (vector signed int) in2), 0x00, dst2);
      src += 16;
      dst1 += 8;
      dst2 += 8;
    }
}
//-----------------------------------------------------------------------------
#  define VEC_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) \
if (kdu_altivec_exists && (_pairs >= 8)) \
{ \
  if (_downshift == 0) _tgt = vec_deinterleave_16; \
  else _tgt = vec_downshifted_deinterleave_16; \
}
#else // No alignment support for 128-bit Altivec implementations
#  define VEC_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift) /* Do nothing */
#endif
//-----------------------------------------------------------------------------

#define KD_SET_SIMD_DEINTERLEAVE_16_FUNC(_tgt,_pairs,_downshift) \
{ \
  VEC_SET_DEINTERLEAVE_16(_tgt,_pairs,_downshift); \
}

/*****************************************************************************/
/* STATIC                    ..._deinterleave_32                             */
/*****************************************************************************/

#define KD_SET_SIMD_DEINTERLEAVE_32_FUNC(_tgt,_pairs) \
{ \
}


/* ========================================================================= */
/*                      Vertical Lifting Step Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       vec_vlift_16_...                             */
/*****************************************************************************/

#if (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void
  vec_vlift_16_5x3_synth(kdu_int16 **src, kdu_int16 *dst_in,
                         kdu_int16 *dst_out, int samples,
                         kd_lifting_step *step, bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This one only does synthesis
  if (samples <= 0)
    return;
  kdu_int16 *src1=src[0], *src2=src[1];
  int count = (samples+7)>>3;
  int int_coeff = step->icoeffs[0];
  kdu_uint16 short_downshift = (kdu_uint16)(step->downshift);
  
  vector signed short one = vec_splat_s16(1);
  
  vector unsigned short v_downshift = vec_lde(0, &(short_downshift));
  // loads the downshift somewhere in the vector register
  v_downshift = vec_splat(vec_perm(v_downshift, v_downshift,
                                   vec_lvsl(0, &(short_downshift))), 0);
  // splats the downshift to all elements.
  
  vector signed short v_offset = vec_sra(vec_sl(one, v_downshift),
                                         (vector unsigned short) one);
  if (int_coeff == 1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in1 = vec_ld(0, src1);
          vector signed short in2 = vec_ld(0, src2);
          vector signed short out1 = vec_ld(0, dst_in);
          
          vec_st(vec_sub(out1,
                         vec_sra(vec_add(v_offset, 
                                         vec_add(in1, in2)),
                                 v_downshift)),
                 0, 
                 dst_out);
          src1 += 8;
          src2 += 8;
          dst_in += 8;
          dst_out += 8;
        }
    }
  else if (int_coeff == -1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in1 = vec_ld(0, src1);
          vector signed short in2 = vec_ld(0, src2);
          vector signed short out1 = vec_ld(0, dst_in);
          
          vec_st(vec_sub(out1,
                         vec_sra(vec_sub(vec_sub(v_offset, 
                                                 in1), 
                                         in2),
                                 v_downshift)),
                 0, 
                 dst_out);
          src1 += 8;
          src2 += 8;
          dst_in += 8;
          dst_out += 8;
        }
    }
  else
    assert(0);
}
//-----------------------------------------------------------------------------
static void
  vec_vlift_16_5x3_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  assert(!for_synthesis); // This one only does analysis
  if (samples <= 0)
    return;
  kdu_int16 *src1=src[0], *src2=src[1];
  
  int count = (samples+7)>>3;
  int int_coeff = step->icoeffs[0];
  kdu_uint16 short_downshift = (kdu_uint16)(step->downshift);
  
  vector signed short one = vec_splat_s16(1);
  
  vector unsigned short v_downshift = vec_lde(0, &(short_downshift));
    // loads the downshift somewhere in the vector register
  v_downshift = vec_splat(vec_perm(v_downshift, v_downshift,
                                   vec_lvsl(0, &(short_downshift))), 0);
    // splats the downshift to all elements.
  
  vector signed short v_offset = vec_sra(vec_sl(one, v_downshift),
                                         (vector unsigned short) one);
  
  if (int_coeff == 1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in1 = vec_ld(0, src1);
          vector signed short in2 = vec_ld(0, src2);
          vector signed short out1 = vec_ld(0, dst_in);
          
          vec_st(vec_add(out1,
                         vec_sra(vec_add(v_offset, 
                                         vec_add(in1, in2)),
                                 v_downshift)),
                 0, 
                 dst_out);
          src1 += 8;
          src2 += 8;
          dst_in += 8;
          dst_out += 8;
        }
    }
  else if (int_coeff == -1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in1 = vec_ld(0, src1);
          vector signed short in2 = vec_ld(0, src2);
          vector signed short out1 = vec_ld(0, dst_in);
          
          vec_st(vec_add(out1,
                         vec_sra(vec_sub(vec_sub(v_offset, 
                                                 in1), 
                                         in2),
                                 v_downshift)),
                 0, 
                 dst_out);
          src1 += 8;
          src2 += 8;
          dst_in += 8;
          dst_out += 8;
        }
    }
  else
    assert(0);
}
//-----------------------------------------------------------------------------
static void
  vec_vlift_16_9x7_synth(kdu_int16 **src, kdu_int16 *dst_in,
                         kdu_int16 *dst_out, int samples,
                         kd_lifting_step *step, bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(for_synthesis); // This one only does synthesis
  if (samples <= 0)
    return;
  kdu_int16 *src1=src[0], *src2=src[1];
  
  int count = (samples+7)>>3;
  kdu_int16 remainder = simd_w97_rem[step_idx];
  
  vector signed short v_remainder = vec_lde(0, &(remainder));
    // loads the remainder somewhere in the vector register
  v_remainder = vec_splat(vec_perm(v_remainder, v_remainder,
                                   vec_lvsl(0, &(remainder))), 0);
    // splats it to all elements
  vector signed short zero = vec_splat_s16(0);
  vector signed short one = vec_splat_s16(1);
  
  // Since the remainder will only be used for multiplication and downshift
  // with vec_mradds, it needs to be shifted right by one.
  v_remainder = vec_sra(v_remainder, (vector unsigned short) one);
  
  switch (step_idx) {
    case 0: 
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(vec_add(vec_add(out1, sources), 
                                   sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 1:
      { // Add source samples; multiply by remainder, then downshift.
        // The actual lifting factor is -0.05298
        
        vector unsigned short v_postshift =
        (vector unsigned short) vec_splat_s16(3);
        vector signed short v_postoffset =
        vec_sra(vec_sl(one, v_postshift), (vector unsigned short) one);
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vec_st(vec_sub(out1,
                           vec_sra(vec_add(vec_add(vec_mradds(in1,
                                                              v_remainder,
                                                              zero),
                                                   vec_mradds(in2,
                                                              v_remainder,
                                                              zero)),
                                           v_postoffset),
                                   v_postshift)),
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        
        // The compiler should do this loop using the count register; it may
        // unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(vec_sub(out1, sources),
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        
        // The compiler should do this loop using the count register; it may
        // unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(out1, 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}
//-----------------------------------------------------------------------------
static void
  vec_vlift_16_9x7_analysis(kdu_int16 **src, kdu_int16 *dst_in,
                            kdu_int16 *dst_out, int samples,
                            kd_lifting_step *step, bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(!for_synthesis); // This one only does analysis
  if (samples <= 0)
    return;
  kdu_int16 *src1=src[0], *src2=src[1];
  
  int count = (samples+7)>>3;
  kdu_int16 remainder = simd_w97_rem[step_idx];
  
  vector signed short v_remainder = vec_lde(0, &(remainder));
    // loads the remainder somewhere in the vector register
  v_remainder = vec_splat(vec_perm(v_remainder, v_remainder,
                                   vec_lvsl(0, &(remainder))), 0);
    // splats it to all elements
  vector signed short zero = vec_splat_s16(0);
  vector signed short one = vec_splat_s16(1);
  
  // Since the remainder will only be used for multiplication and downshift
  // with vec_mradds, it needs to be shifted right by one.
  v_remainder = vec_sra(v_remainder, (vector unsigned short) one);
  
  switch (step_idx) {
    case 0: 
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
      
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(vec_sub(vec_sub(out1, sources), 
                                   sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 1:
      { // Add source samples; multiply by remainder, then downshift.
        // The actual lifting factor is -0.05298
        
        vector unsigned short v_postshift =
        (vector unsigned short) vec_splat_s16(3);
        vector signed short v_postoffset =
        vec_sra(vec_sl(one, v_postshift), (vector unsigned short) one);
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vec_st(vec_add(out1,
                           vec_sra(vec_add(vec_add(vec_mradds(in1,
                                                              v_remainder,
                                                              zero),
                                                   vec_mradds(in2,
                                                              v_remainder,
                                                              zero)),
                                           v_postoffset),
                                   v_postshift)),
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        
        // The compiler should do this loop using the count register; it may
        // unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(vec_add(out1, sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        
        // The compiler should do this loop using the count register; it may
        // unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in1 = vec_ld(0, src1);
            vector signed short in2 = vec_ld(0, src2);
            vector signed short out1 = vec_ld(0, dst_in);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(out1,
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst_out);
            src1 += 8;
            src2 += 8;
            dst_in += 8;
            dst_out += 8;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}
//-----------------------------------------------------------------------------
#  define VEC_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_altivec_exists) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vec_vlift_16_5x3_synth; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vec_vlift_16_9x7_synth; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vec_vlift_16_5x3_analysis; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vec_vlift_16_9x7_analysis; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
  }
#else // No alignment support for 128-bit Altivec implementations
#  define VEC_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  VEC_SET_VLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
}

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_VLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_VLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */


/* ========================================================================= */
/*                     Horizontal Lifting Step Functions                     */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       vec_hlift_16_...                             */
/*****************************************************************************/

#if (KDU_ALIGN_SAMPLES16 >= 8)
//-----------------------------------------------------------------------------
static void vec_hlift_16_5x3_synth(kdu_int16 *src, kdu_int16 *dst,
                                   int samples, kd_lifting_step *step,
                                   bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(for_synthesis); // This one is for synthesis only
  if (samples <= 0)
    return;
  
  int count = (samples+7)>>3;
  int int_coeff = step->icoeffs[0];
  kdu_uint16 short_downshift = (kdu_uint16)(step->downshift);
  
  vector signed short one = vec_splat_s16(1);
  
  vector unsigned short v_downshift = vec_lde(0, &(short_downshift));
    // loads the downshift somewhere in the vector register
  v_downshift = vec_splat(vec_perm(v_downshift, v_downshift,
                                   vec_lvsl(0, &(short_downshift))), 0);
    // splats the downshift to all elements.
  
  vector signed short v_offset = vec_sra(vec_sl(one, v_downshift),
                                         (vector unsigned short) one);
  
  // Set up permute vectors to align two versions of the source for proper
  // calculations.
  vector unsigned char perm1 = vec_lvsl(0, src);
  vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(2));
  vector signed short in0 = vec_ld(0, src);
  
  if (int_coeff == 1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in16 = vec_ld(16, src);
          vector signed short out1 = vec_ld(0, dst);
          vector signed short in1 = vec_perm(in0, in16, perm1);
          vector signed short in2 = vec_perm(in0, in16, perm2);
          
          vec_st(vec_sub(out1, 
                         vec_sra(vec_add(v_offset, 
                                         vec_add(in1, in2)),
                                 v_downshift)),
                 0, 
                 dst);
          in0 = in16;
          src += 8;
          dst += 8;
        }
    }
  else if (int_coeff == -1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in16 = vec_ld(16, src);
          vector signed short out1 = vec_ld(0, dst);
          vector signed short in1 = vec_perm(in0, in16, perm1);
          vector signed short in2 = vec_perm(in0, in16, perm2);
          
          vec_st(vec_sub(out1, 
                         vec_sra(vec_sub(vec_sub(v_offset, 
                                                 in1), 
                                         in2),
                                 v_downshift)),
                 0, 
                 dst);
          in0 = in16;
          src += 8;
          dst += 8;
        }   
    }
  else
    assert(0);
}
//-----------------------------------------------------------------------------
static void vec_hlift_16_5x3_analysis(kdu_int16 *src, kdu_int16 *dst,
                                      int samples, kd_lifting_step *step,
                                      bool for_synthesis)
{
  assert((step->support_length == 2) && (step->icoeffs[0]==step->icoeffs[1]));
  assert(!for_synthesis); // This one is for analysis only
  if (samples <= 0)
    return;

  int count = (samples+7)>>3;
  int int_coeff = step->icoeffs[0];
  kdu_uint16 short_downshift = (kdu_uint16)(step->downshift);
  
  vector signed short one = vec_splat_s16(1);
  
  vector unsigned short v_downshift = vec_lde(0, &(short_downshift));
    // loads the downshift somewhere in the vector register
  v_downshift = vec_splat(vec_perm(v_downshift, v_downshift,
                                   vec_lvsl(0, &(short_downshift))), 0);
    // splats the downshift to all elements.
  
  vector signed short v_offset = vec_sra(vec_sl(one, v_downshift),
                                         (vector unsigned short) one);
  
  // Set up permute vectors to align two versions of the source for proper
  // calculations.
  vector unsigned char perm1 = vec_lvsl(0, src);
  vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(2));
  vector signed short in0 = vec_ld(0, src);
  
  if (int_coeff == 1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in16 = vec_ld(16, src);
          vector signed short out1 = vec_ld(0, dst);
          vector signed short in1 = vec_perm(in0, in16, perm1);
          vector signed short in2 = vec_perm(in0, in16, perm2);
          vec_st(vec_add(out1,
                         vec_sra(vec_add(v_offset, 
                                         vec_add(in1, in2)),
                                 v_downshift)),
                 0, 
                 dst);
          in0 = in16;
          src += 8;
          dst += 8;
        }
    }
  else if (int_coeff == -1)
    { 
      // The compiler should do this loop using the count register; it
      // may unroll it if it's smart enough.
      for(; count; count--)
        { 
          vector signed short in16 = vec_ld(16, src);
          vector signed short out1 = vec_ld(0, dst);
          vector signed short in1 = vec_perm(in0, in16, perm1);
          vector signed short in2 = vec_perm(in0, in16, perm2);
          vec_st(vec_add(out1, 
                         vec_sra(vec_sub(vec_sub(v_offset, in1), 
                                         in2),
                                 v_downshift)),
                 0, 
                 dst);
          in0 = in16;
          src += 8;
          dst += 8;
        }
    }
  else
    assert(0);

}
//-----------------------------------------------------------------------------
static void vec_hlift_16_9x7_synth(kdu_int16 *src, kdu_int16 *dst,
                                   int samples, kd_lifting_step *step,
                                   bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(for_synthesis); // This one is for synthesis only
  if (samples <= 0)
    return;
  
  int count = (samples+7)>>3;
  kdu_int16 remainder = simd_w97_rem[step_idx];
  
  vector signed short v_remainder = vec_lde(0, &(remainder));
    // loads the remainder somewhere in the vector register
  v_remainder = vec_splat(vec_perm(v_remainder, v_remainder,
                                   vec_lvsl(0, &(remainder))), 0);
    // splats it to all elements
  vector signed short zero = vec_splat_s16(0);
  vector signed short one = vec_splat_s16(1);
  
  // Since the remainder will only be used for multiplication and downshift
  // with vec_mradds, it needs to be shifted right by one.
  v_remainder = vec_sra(v_remainder, (vector unsigned short) one);
  
  // Set up permute vectors to align two versions of the source for proper
  // calculations.
  vector unsigned char perm1 = vec_lvsl(0, src);
  vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(2));
  
  vector signed short in0 = vec_ld(0, src);
  
  switch (step_idx) {
    case 0: 
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(vec_add(vec_add(out1, sources),
                                   sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 1:
      { // Add source samples; multiply by remainder, then downshift.
        // The actual lifting factor is -0.05298
        
        vector unsigned short v_postshift =
        (vector unsigned short) vec_splat_s16(3);
        vector signed short v_postoffset =
        vec_sra(vec_sl(one, v_postshift), (vector unsigned short) one);
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vec_st(vec_sub(out1,
                           vec_sra(vec_add(vec_add(vec_mradds(in1,
                                                              v_remainder,
                                                              zero),
                                                   vec_mradds(in2,
                                                              v_remainder,
                                                              zero)),
                                           v_postoffset),
                                   v_postshift)),
                   0, 
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(vec_sub(out1, sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_sub(out1, 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}
//-----------------------------------------------------------------------------
static void vec_hlift_16_9x7_analysis(kdu_int16 *src, kdu_int16 *dst,
                                     int samples, kd_lifting_step *step,
                                     bool for_synthesis)
{
  int step_idx = step->step_idx;
  assert((step_idx >= 0) && (step_idx < 4));
  assert(!for_synthesis); // This one is for analysis only
  if (samples <= 0)
    return;

  int count = (samples+7)>>3;
  kdu_int16 remainder = simd_w97_rem[step_idx];
  
  vector signed short v_remainder = vec_lde(0, &(remainder));
    // loads the remainder somewhere in the vector register
  v_remainder = vec_splat(vec_perm(v_remainder, v_remainder,
                                   vec_lvsl(0, &(remainder))), 0);
    // splats it to all elements
  vector signed short zero = vec_splat_s16(0);
  vector signed short one = vec_splat_s16(1);
  
  // Since the remainder will only be used for multiplication and downshift
  // with vec_mradds, it needs to be shifted right by one.
  v_remainder = vec_sra(v_remainder, (vector unsigned short) one);
  
  // Set up permute vectors to align two versions of the source for proper
  // calculations.
  vector unsigned char perm1 = vec_lvsl(0, src);
  vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(2));
  
  vector signed short in0 = vec_ld(0, src);
  
  switch (step_idx) {
    case 0: 
      { // Integer part of lifting step factor is -2.
        // The actual lifting factor is -1.586134
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(vec_sub(vec_sub(out1, sources), sources), 
                           vec_mradds(sources, v_remainder, zero)), 
                   0,
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 1:
      { // Add source samples; multiply by remainder, then downshift.
        // The actual lifting factor is -0.05298
        
        vector unsigned short v_postshift =
        (vector unsigned short) vec_splat_s16(3);
        vector signed short v_postoffset =
        vec_sra(vec_sl(one, v_postshift), (vector unsigned short) one);
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vec_st(vec_add(out1,
                           vec_sra(vec_add(vec_add(vec_mradds(in1,
                                                              v_remainder,
                                                              zero),
                                                   vec_mradds(in2,
                                                              v_remainder,
                                                              zero)),
                                           v_postoffset),
                                   v_postshift)),
                   0, 
                   dst);
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 2:
      { // Integer part of lifting step factor is 1.
        // The actual lifting factor is 0.882911
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(vec_add(out1, sources),
                           vec_mradds(sources, v_remainder, zero)), 
                   0,
                   dst);
            
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    case 3:
      { // Integer part of lifting step factor is 0
        // The actual lifting factor is 0.443507
        
        // The compiler should do this loop using the count register; it
        // may unroll it if it's smart enough.
        for(; count; count--)
          { 
            vector signed short in16 = vec_ld(16, src);
            vector signed short out1 = vec_ld(0, dst);
            vector signed short in1 = vec_perm(in0, in16, perm1);
            vector signed short in2 = vec_perm(in0, in16, perm2);
            vector signed short sources = vec_add(in1, in2);
            vec_st(vec_add(out1, 
                           vec_mradds(sources, v_remainder, zero)), 
                   0, 
                   dst);
            
            in0 = in16;
            src += 8;
            dst += 8;
          }
      }
      break;
    default:
      assert(0);
  } // end of switch statement
}
//-----------------------------------------------------------------------------
#  define VEC_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
if (kdu_altivec_exists) \
  { \
    if (_synthesis) \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vec_hlift_16_5x3_synth; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vec_hlift_16_9x7_synth; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
    else \
      { \
        if (_step->kernel_id == Ckernels_W5X3) \
          { \
            _func = vec_hlift_16_5x3_analysis; _add_first = true; \
          } \
        else if (_step->kernel_id == Ckernels_W9X7) \
          { \
            _func = vec_hlift_16_9x7_analysis; \
            _add_first = (_step->step_idx != 1); \
          } \
      } \
  }
#else // No alignment support for 128-bit Altivec implementations
#  define VEC_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis)
#endif

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_16_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis) \
{ \
  VEC_SET_HLIFT_16_FUNC(_func,_add_first,_step,_synthesis); \
}

/*****************************************************************************/
/* MACRO               KD_SET_SIMD_HLIFT_32_FUNC selector                    */
/*****************************************************************************/

#define KD_SET_SIMD_HLIFT_32_FUNC(_func,_step,_synthesis) /* Do nothing */
  
} // namespace kd_core_simd

#endif // GCC_DWT_ALTIVEC_LOCAL_H
