/*****************************************************************************/
// File: image_out.cpp [scope = APPS/IMAGE-IO]
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
   Implements image file writing for a variety of different file formats:
currently BMP, PGM, PPM and RAW only.  Readily extendible to include other file
formats without affecting the rest of the system.
******************************************************************************/

// System includes
#include <iostream>
#include <string.h>
#include <math.h>
#include <assert.h>
// Core includes
#include "kdu_utils.h"
#include "kdu_messaging.h"
#include "kdu_sample_processing.h"
// Image includes
#include "kdu_image.h"
#include "image_local.h"

using namespace kd_supp_image_local;

/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* INLINE                       to_little_endian                             */
/*****************************************************************************/

static void
  to_little_endian(kdu_int32 * words, int num_words)
{
  kdu_int32 test = 1;
  kdu_byte *first_byte = (kdu_byte *) &test;
  if (*first_byte)
    return; // Machine uses little-endian architecture already.
  kdu_int32 tmp;
  for (; num_words--; words++)
    {
      tmp = *words;
      *words = ((tmp >> 24) & 0x000000FF) +
               ((tmp >> 8)  & 0x0000FF00) +
               ((tmp << 8)  & 0x00FF0000) +
               ((tmp << 24) & 0xFF000000);
    }
}

/*****************************************************************************/
/* STATIC                    convert_floats_to_bytes                         */
/*****************************************************************************/

static void
  convert_floats_to_bytes(kdu_sample32 *src, kdu_byte *dest, int num,
                          int precision, bool align_lsbs, int orig_precision,
                          bool is_signed, int sample_gap)
  /* Let `precision' be denoted by P and `orig_precision' be denoted by B.
     The `align_lsbs' argument determines how the function should behave if
     P and B differ.  The ultimate objective of the function is to store
     the source samples as P-bit integers, aligned at the most significant
     bit position with bit 7 of each byte which is written.  If P happens
     to exceed 8, it can always be safely reduced to 8, but for this
     introductory description, we will assume that P <= 8.
       If `align_lsbs' is false, original values with B bits are effectively
     packed into integers with P bits, aligning the result at the most
     significant bit position.  In this case, the value of B affects the
     behaviour only if B < P.  In particular, the function notionally performs
     the following steps:
     a) convert each input sample x into an integer y = <2^P * x>;
     b) if B < P, round y to the nearest multiple of 2^{P-B};
     c) add 2^{P-1} to y, so as to make it an unsigned quantity;
     d) clip y to the range [0,2^P-1]
     e) scale y by 2^{8-P} so as to pack it into the MSB's of each byte
     f) if `is_signed' is true, subtract 128.
       If `align_lsbs' is true, the original values with B bits are
     effectively packed into the least significant B bits of the
     P-bit representation before writing the P-bit integer out as an 8-bit
     byte, with MSB's aligned.  In particular, the function notionally
     performs the following steps:
     a) convert each input sample x into an integer y = <2^B * x>;
     b) if not `is_signed', add 2^{B-1} to y and clip to the range [0,2^P-1];
        otherwise, just clip y to the range [-2^{P-1},2^{P-1}-1]
     c) scale y by 2^{8-P} so as to pack it into the MSB's of each byte.
       The above cases and steps can be collapsed into the following
     operations:
     a) Start by clipping x to the interval [x_min,x_max], where
        -- if `align_lsbs' is false, x_min=-0.5, x_max=0.5-2^{-min(B,P)})
        -- else is `is_signed', x_min=-0.5*2^{P-B}, x_max=0.5*2^{P-B}-2^{-B}
        -- else, x_min=-0.5, x_max=2^{P-B}-0.5-2^{-B}
     b) Next, find y = <x * alpha + off>, where <> denotes rounding to the
        nearest integer.
     c) Finally, write out the value y << S, where
        -- if `align_lsbs' is false, S = 8 - min{P,B}
        -- else S = 8 - P
     In step (b), the parameters alpha and off are given by
        -- if `align_lsbs' is false, alpha = 2^{8-S}
        -- else alpha = 2^{8-P+B-S} = 2^B
        -- if `is_signed', off = 0, else off = 0.5*alpha
   */
{
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
     // Based on the equations given above, the `align_lsbs' mode uses the
     // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  if (precision > 8)
    { // Reduce precision safely to 8
      min_precision = (orig_precision < 8)?orig_precision:8;
      if (align_lsbs)
        orig_precision -= (precision-8); // Keeps `diff_precision' the same
      precision = 8;
    }
  if (orig_precision < 0)
    orig_precision = 0; // Just so that we don't have a disaster

  if ((min_precision == 8) && ((diff_precision == 0) || !align_lsbs))
    { // Most common case; just convert directly to 8-bit samples
      float scale16 = (float)(1<<16);
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          kdu_int32 val = (kdu_int32)(src->fval*scale16);
          val = (val+128)>>8; // Often faster than true rounding from floats.
          val += 128;
          if (val & (0xFFFFFFFF<<8))
            val = (val<0)?0:255;
          *dest = (kdu_byte) val;
        }
    }
  else
    { // General case
      float alpha, x_min=-0.5F, x_max=0.5;
      int upshift;
      if (align_lsbs)
        {
          upshift = 8 - precision;
          alpha = (float)(1<<orig_precision);
          if (diff_precision >= 0)
            x_min *= (float)(1<<diff_precision);
          else
            x_min *= 1.0F / (float)(1<<-diff_precision);
          x_max = -x_min;
          if (!is_signed)
            { x_min = -0.5F; x_max = x_max*2.0F - 0.5F; }
        }
      else
        {
          upshift = 8 - min_precision;
          alpha = (float)(1<<min_precision);
        }
      x_max -= 1.0F / alpha;
      float alpha8 = alpha * 256.0F; // So we can do rounding using integers
      int offset = 128; // Half LSB during the rounding process
      if (!is_signed)
        offset += (int)(0.5F*alpha8 + 0.5F);
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          float x = src->fval;
          x = (x >= x_min)?x:x_min;
          x = (x <= x_max)?x:x_max;
          int val = (offset + (int)(x * alpha8)) >> 8;
          *dest = (kdu_byte)(val << upshift);
        }
    }
}

/*****************************************************************************/
/* STATIC                   convert_fixpoint_to_bytes                        */
/*****************************************************************************/

static void
  convert_fixpoint_to_bytes(kdu_sample16 *src, kdu_byte *dest, int num,
                            int precision, bool align_lsbs, int orig_precision,
                            bool is_signed, int sample_gap)
  /* Same as `convert_floats_to_bytes', except that the source samples
     are 16-bit fixed-point quantities with `KDU_FIX_POINT' fraction bits. */
{
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
    // Based on the equations given above, the `align_lsbs' mode uses the
    // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  if (precision > 8)
    { // Reduce precision safely to 8
      min_precision = (orig_precision < 8)?orig_precision:8;
      if (align_lsbs)
        orig_precision -= (precision-8); // Keeps `diff_precision' the same
      precision = 8;
    }
  if (orig_precision < 0)
    orig_precision = 0; // Just so that we don't have a disaster
  
  if ((min_precision == 8) && ((diff_precision == 0) || !align_lsbs))
    { // Most common case; just convert directly to 8-bit samples
      kdu_int16 downshift = KDU_FIX_POINT-8;
      kdu_int16 offset = (1<<downshift)>>1;
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          kdu_int16 val = src->ival;
          val = (val + offset) >> (KDU_FIX_POINT-8);
          val += 128;
          if (val & (0xFFFFFFFF<<8))
            val = (val<0)?0:255;
          *dest = (kdu_byte) val;
        }
    }
  else
    { // General case
      int downshift, upshift, x_min, x_max;
      x_max = 1<<(KDU_FIX_POINT-1);  x_min = -x_max;
      if (align_lsbs)
        {
          upshift = 8 - precision;
          downshift = KDU_FIX_POINT - orig_precision;
          if (diff_precision > 0)
            x_max <<= diff_precision;
          else
            x_max >>= -diff_precision;
          x_min = -x_min;
          if (!is_signed)
            { x_min = -(1<<(KDU_FIX_POINT-1)); x_max += x_max + x_min; }
          if (downshift < 0)
            { upshift -= downshift; downshift = 0; }
        }
      else
        {
          upshift = 8 - min_precision;
          downshift = KDU_FIX_POINT - min_precision;
        }
      x_max -= (1 << downshift);
      int offset = (1 << downshift)>>1; // Rounding offset
      if (!is_signed)
        offset += (1 << (KDU_FIX_POINT-1));
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          int x = src->ival;
          x = (x >= x_min)?x:x_min;
          x = (x <= x_max)?x:x_max;
          *dest = (kdu_byte)(((x+offset)>>downshift) << upshift);
        }
    }
}

/*****************************************************************************/
/* STATIC                     convert_ints_to_bytes                          */
/*****************************************************************************/

static void
  convert_ints_to_bytes(kdu_sample32 *src, kdu_byte *dest, int num,
                        int precision, bool align_lsbs, int orig_precision,
                        bool is_signed, int sample_gap)
  /* Same as `convert_floats_to_bytes', except that the source samples
     are 32-bit quantities with nominal bit-depth `orig_precision'. */
{
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
  // Based on the equations given above, the `align_lsbs' mode uses the
  // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  int input_precision = orig_precision;
  if (precision > 8)
    { // Reduce precision safely to 8
      min_precision = (orig_precision < 8)?orig_precision:8;
      if (align_lsbs)
        orig_precision -= (precision-8); // Keeps `diff_precision' the same
      precision = 8;
    }
  if (orig_precision < 0)
    orig_precision = 0; // Just so that we don't have a disaster
  
  if ((min_precision == 8) && ((diff_precision == 0) || !align_lsbs))
    { // Most common case; just convert directly to 8-bit samples
      kdu_int32 downshift = input_precision-8;
      kdu_int32 offset = (1<<downshift)>>1;

      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          int val = src->ival;
          val = (val+offset)>>downshift;
          val += 128;
          if (val & (0xFFFFFFFF<<8))
            val = (val<0)?0:255;
          *dest = (kdu_byte) val;
        }
    }
  else
    { // General case
      int downshift, upshift, x_min, x_max;
      x_max = 1<<(input_precision-1);  x_min = -x_max;
      if (align_lsbs)
        {
          upshift = 8 - precision;
          downshift = input_precision - orig_precision;
          if (diff_precision > 0)
            x_max <<= diff_precision;
          else
            x_max >>= -diff_precision;
          x_min = -x_min;
          if (!is_signed)
            { x_min = -(1<<(input_precision-1)); x_max += x_max + x_min; }
          if (downshift < 0)
            { upshift -= downshift; downshift = 0; }
        }
      else
        {
          upshift = 8 - min_precision;
          downshift = input_precision - min_precision;
        }
      x_max -= (1 << downshift);
      int offset = (1 << downshift)>>1; // Rounding offset
      if (!is_signed)
        offset += (1 << (input_precision-1));
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          int x = src->ival;
          x = (x >= x_min)?x:x_min;
          x = (x <= x_max)?x:x_max;
          *dest = (kdu_byte)(((x+offset)>>downshift) << upshift);
        }
    }
}

/*****************************************************************************/
/* STATIC                    convert_shorts_to_bytes                         */
/*****************************************************************************/

static void
  convert_shorts_to_bytes(kdu_sample16 *src, kdu_byte *dest, int num,
                          int precision, bool align_lsbs, int orig_precision,
                          bool is_signed, int sample_gap)
  /* Same as `convert_floats_to_bytes', except that the source samples
     are 16-bit quantities with nominal bit-depth `orig_precision'. */
{
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
  // Based on the equations given above, the `align_lsbs' mode uses the
  // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  int input_precision = orig_precision;
  if (precision > 8)
    { // Reduce precision safely to 8
      min_precision = (orig_precision < 8)?orig_precision:8;
      if (align_lsbs)
        orig_precision -= (precision-8); // Keeps `diff_precision' the same
      precision = 8;
    }
  if (orig_precision < 0)
    orig_precision = 0; // Just so that we don't have a disaster
  
  if ((min_precision == 8) && ((diff_precision == 0) || !align_lsbs))
    { // Most common case; just convert directly to 8-bit samples
      kdu_int16 downshift = input_precision-8;
      kdu_int16 offset = (1<<downshift)>>1;

      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          kdu_int16 val = src->ival;
          val = (val+offset)>>downshift;
          val += 128;
          if (val & (0xFFFFFFFF<<8))
            val = (val<0)?0:255;
          *dest = (kdu_byte) val;
        }
    }
  else
    { // General case
      int downshift, upshift, x_min, x_max;
      x_max = 1<<(input_precision-1);  x_min = -x_max;
      if (align_lsbs)
        {
          upshift = 8 - precision;
          downshift = input_precision - orig_precision;
          if (diff_precision > 0)
            x_max <<= diff_precision;
          else
            x_max >>= -diff_precision;
          x_min = -x_min;
          if (!is_signed)
            { x_min = -(1<<(input_precision-1)); x_max += x_max + x_min; }
          if (downshift < 0)
            { upshift -= downshift; downshift = 0; }
        }
      else
        {
          upshift = 8 - min_precision;
          downshift = input_precision - min_precision;
        }
      x_max -= (1 << downshift);
      int offset = (1 << downshift)>>1; // Rounding offset
      if (!is_signed)
        offset += (1 << (input_precision-1));
      for (; num > 0; num--, src++, dest+=sample_gap)
        {
          int x = src->ival;
          x = (x >= x_min)?x:x_min;
          x = (x <= x_max)?x:x_max;
          *dest = (kdu_byte)(((x+offset)>>downshift) << upshift);
        }
    }
}

/*****************************************************************************/
/* INLINE             check_absolute_ints_for_float_forcing                  */
/*****************************************************************************/

static inline void
  check_absolute_ints_for_float_forcing(kdu_line_buf &line,
                                        int exponent_bits,
                                        int orig_precision)
  /* Checks that `line' has an absolute integer representation, generating
     an error message if not.  The function is called in connection with
     all the functions that re-interpret integers as floating point values
     in response to a non-zero forced exponent bit-count from
     `kdu_image_dims::get_forced_precision'. */
{
  if (!line.is_absolute())
    { kdu_error e; e << "Attempting to force re-interpretation of integers "
      "as floating point bit patterns, where the `kdu_line_buf' object "
      "used to supply source samples does not advertise an absolute " 
      "integer representation.  This suggests that you may be applying "
      "precision forcing, with a floating-point conversion requirement, to "
      "data that does not have an associated non-linear point transform of "
      "the SMAG or UMAG variety.  Alternatively, you might be deriving "
      "your `kdu_line_buf' objects from something other than the recommended "
      "`kdu_multi_synthesis' object -- that object guarantees that "
      "line buffers advertise an absolute integer representation when "
      "SMAG/UMAG NLT's are involved.";
    }
  assert(exponent_bits > 0);
  if (orig_precision < (exponent_bits+1))
    { kdu_error e; e << "Attempting to force re-interpretation of " <<
      orig_precision << "-bit integers as floating point values with an " <<
      exponent_bits << "-bit exponent field.  Clearly this is not possible, "
      "since there must be one sign bit and a non-negative number of "
      "mantissa bits in the overall representation.";
    }
}

/*****************************************************************************/
/* STATIC             convert_ints_as_floats_to_bytes                        */
/*****************************************************************************/

static void
  convert_ints_as_floats_to_bytes(kdu_sample32 *src, kdu_byte *dest, int num,
                                  int exponent_bits, int precision,
                                  int orig_precision, bool is_signed,
                                  int sample_gap)
  /* Let N=`orig_precision', P=`precision' and E=`exponent_bits'.  The
     function reads N-bit words from `src', adding the level offset value of
     2^{N-1} if `is_signed' is true, and re-interpreting the resulting
     bit-patterns as N-bit floating point values with an E-bit exponent and
     M=N-1-E mantissa bits.  The resulting floating-point values are clipped,
     scaled and level offset (if `is_signed' is true) to obtain B-bit unsigned
     bytes, where B is the smaller of 8 and `precision'.  The result is
     upshifted by 8-B to produce the output sample values that are written
     to `dest' at intervals of `sample_gap'.  A key step here is to map
     the bit patterns associated with the level adjusted input integers
     to bit patterns that can be directly re-interpreted as IEEE single
     precision floats -- this may involve a change in the number of
     exponent bits (to 8), mantissa bits (to 23) or both. */
{
  assert((exponent_bits > 0) && (exponent_bits < orig_precision));
  if (orig_precision > 32)
    { 
      orig_precision = 32;
      if (exponent_bits > 31)
        exponent_bits = 31;
    }
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  
  float scale = 0.0f;
  if (!is_signed)
    scale = kdu_pwrof2f(precision) - 1.0f;
  else if (precision < 2)
    { kdu_error e; e << "Signed integers that are re-interpreted as floats "
      "can only be forced (scaled) into an output integer representation "
      "that has precision of at least 2 bits/sample."; }
  else
    scale = kdu_pwrof2f(precision-1) - 1.0f;
  
  int mantissa_bits = orig_precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from source exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly bring
      // denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // In this case, this is the smaller exponent bound
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1; // Allow max mantissa
  kdu_int32 pre_adjust = exp_adjust<<mantissa_bits; // Subtract before shifts  

  int cvt_precision = precision;
  int cvt_upshift = 8 - cvt_precision;
  if (cvt_upshift < 0)
    { // Map to 8-bit precision directly
      scale *= kdu_pwrof2f(8-cvt_precision);
      cvt_precision = 8;
      cvt_upshift = 0;
    }
  float cvt_offset = 0.5f + (float)((is_signed)?(1<<(cvt_precision-1)):0);
  float max_fval = (float)((1<<cvt_precision)-1);
  
  if (!is_signed)
    { // Processing unsigned samples
      kdu_int32 in_off = 1<<(orig_precision-1);
      kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
      kdu_int32 in_max = mag_max - in_off;    // integer level offset in place
      for (; num > 0; num--, src++, dest+=sample_gap)
        { 
          kdu_int32 val = src->ival;
          if (val < in_min)
            val = in_min; // Avoid exponent underflow and negative values
          else if (val > in_max)
            val = in_max; // Avoid exponent overflow
          val += in_off; // Remove the integer level offset
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          cast.int_val = val;
          float fval = scale * (denorm_scale * cast.float_val) + cvt_offset;
          assert(fval >= 0.0f);
          if (fval > max_fval)
            fval = max_fval;
          val = (kdu_int32) fval; // Rounding offset is built into `cvt_offset'
          *dest = (kdu_byte)(val << cvt_upshift);
        }
    }
  else
    { // Processing signed samples
      kdu_int32 mag_mask = ~(((kdu_int32)-1) << (orig_precision-1));
      for (; num > 0; num--, src++, dest+=sample_gap)
        { 
          kdu_int32 val = src->ival;
          kdu_int32 sign_bit = val & KDU_INT32_MIN;
          val &= mag_mask; // Keep just the magnitudes
          if (val < pre_adjust)
            val = pre_adjust; // Avoid exponent underflow
          else if (val > mag_max)
            val = mag_max; // Avoid exponent overflow
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = val;
          float fval = scale * (denorm_scale * cast.float_val) + cvt_offset;
          if (fval < 0.0f)
            fval = 0.0f;
          else if (fval > max_fval)
            fval = max_fval;
          val = (kdu_int32) fval; // Rounding offset is built into `cvt_offset'
          *dest = (kdu_byte)(val << cvt_upshift);
        }
    }
}

/*****************************************************************************/
/* STATIC            convert_shorts_as_floats_to_bytes                       */
/*****************************************************************************/

static void
  convert_shorts_as_floats_to_bytes(kdu_sample16 *src, kdu_byte *dest, int num,
                                    int exponent_bits, int precision,
                                    int orig_precision, bool is_signed,
                                    int sample_gap)
  /* As above, but input samples are 16-bit absolute ints. */
{
  assert((exponent_bits > 0) && (exponent_bits < orig_precision));
  if (orig_precision > 16)
    { 
      orig_precision = 16;
      if (exponent_bits > 15)
        exponent_bits = 15;
    }
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  
  float scale = 0.0f;
  if (!is_signed)
    scale = kdu_pwrof2f(precision) - 1.0f;
  else if (precision < 2)
    { kdu_error e; e << "Signed integers that are re-interpreted as floats "
      "can only be forced (scaled) into an output integer representation "
      "that has precision of at least 2 bits/sample."; }
  else
    scale = kdu_pwrof2f(precision-1) - 1.0f;
  
  int mantissa_bits = orig_precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from source exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly bring
      // denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // In this case, this is the smaller exponent bound
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1; // Allow max mantissa
  kdu_int32 pre_adjust = exp_adjust<<mantissa_bits; // Subtract before shifts  

  int cvt_precision = precision;
  int cvt_upshift = 8 - cvt_precision;
  if (cvt_upshift < 0)
    { // Map to 8-bit precision directly
      scale *= kdu_pwrof2f(8-cvt_precision);
      cvt_precision = 8;
      cvt_upshift = 0;
    }
  float cvt_offset = 0.5f + (float)((is_signed)?(1<<(cvt_precision-1)):0);
  float max_fval = (float)((1<<cvt_precision)-1);
  
  if (!is_signed)
    { // Processing unsigned samples
      kdu_int32 in_off = 1<<(orig_precision-1);
      kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
      kdu_int32 in_max = mag_max - in_off;    // integer level offset in place
      for (; num > 0; num--, src++, dest+=sample_gap)
        { 
          kdu_int32 val = src->ival;
          if (val < in_min)
            val = in_min; // Avoid exponent underflow and negative values
          else if (val > in_max)
            val = in_max; // Avoid exponent overflow
          val += in_off; // Remove the integer level offset
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          cast.int_val = val;
          float fval = scale * (denorm_scale * cast.float_val) + cvt_offset;
          assert(fval >= 0.0f);
          if (fval > max_fval)
            fval = max_fval;
          val = (kdu_int32) fval; // Rounding offset is built into `cvt_offset'
          *dest = (kdu_byte)(val << cvt_upshift);
        }
    }
  else
    { // Processing signed samples
      kdu_int32 mag_mask = ~(((kdu_int32)-1) << (orig_precision-1));
      for (; num > 0; num--, src++, dest+=sample_gap)
        { 
          kdu_int32 val = src->ival;
          kdu_int32 sign_bit = val & KDU_INT32_MIN;
          val &= mag_mask; // Keep just the magnitudes
          if (val < pre_adjust)
            val = pre_adjust; // Avoid exponent underflow
          else if (val > mag_max)
            val = mag_max; // Avoid exponent overflow
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = val;
          float fval = scale * (denorm_scale * cast.float_val) + cvt_offset;
          if (fval < 0.0f)
            fval = 0.0f;
          else if (fval > max_fval)
            fval = max_fval;
          val = (kdu_int32) fval; // Rounding offset is built into `cvt_offset'
          *dest = (kdu_byte)(val << cvt_upshift);
        }
    }
}

/*****************************************************************************/
/* STATIC               clip_ints_interpreted_as_floats                      */
/*****************************************************************************/

static void
  clip_ints_interpreted_as_floats(kdu_line_buf &line, int exponent_bits,
                                  int orig_precision, bool is_signed)
  /* This function is called only when writing a raw output file, where there
     is no header information to indicate the meaning of sample values.
     Moreover, it is called only if precision forcing settings indicate
     that `line' has absolute integers whose bits are to be re-interpreted
     as floating point values and written as such, perhaps with a shift
     to compensate for diffeent mantissa sizes and after removing the
     usual mid-point level shift for unsigned quantities.
        The actual conversion steps required to write samples out to the
     raw output file are performed by the `convert_ints_to_words' functions,
     which are used also for processing regular integer sample conversions.
     However, before those functions are called, this function performs
     range bound checks to make sure that quantization errors have not
     corrupted the sample values to the point where the floating point
     representation is not valid.
        Specifically, after removing any level shift (1<<(orig_precision-1)
     for unsigned data, else 0), unsigned values should not appear to be
     negative and exponents should not exceed (1<<exponent_bits)-1, so
     as to avoid confusion with NaN's. */
{
  check_absolute_ints_for_float_forcing(line,exponent_bits,orig_precision);
  
  kdu_int32 exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_bits = orig_precision - 1 - exponent_bits;
  kdu_int32 exp_max = 2*exp_off;
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
  
  int n = line.get_width();
  if (line.get_buf32())
    { // Processing 32-bit integers
      kdu_sample32 *sp = line.get_buf32();
      if (!is_signed)
        { // Accounting for offset, disallow false negatives and clip exponents
          kdu_int32 offset = 1<<(orig_precision-1);
          kdu_int32 min_val = 0 - offset;       // Test bounds violation with
          kdu_int32 max_val = mag_max - offset; // int level offset in place
          for (; n > 0; n--, sp++)
            { 
              kdu_int32 val = sp->ival;
              if (val < min_val)
                sp->ival = min_val;
              else if (val > max_val)
                sp->ival = max_val;
            }
        }
      else
        { // Clip exponent only
          kdu_int32 sign_mask = ((kdu_int32)-1)<<(orig_precision-1);
          kdu_int32 mag_mask = ~sign_mask;
          for (; n > 0; n--, sp++)
            { 
              kdu_int32 val = sp->ival;
              if ((val & mag_mask) > mag_max)
                sp->ival = (val & sign_mask) | mag_max;
            }          
        }
    }
  else
    { // Processing 16-bit integers
      kdu_sample16 *sp = line.get_buf16();
          if (!is_signed)
        { // Accounting for offset, disallow false negatives and clip exponents
          kdu_int32 offset = 1<<(orig_precision-1);
          kdu_int32 min_val = 0 - offset;       // Test bounds violation with
          kdu_int32 max_val = mag_max - offset; // offsets in place
          for (; n > 0; n--, sp++)
            { 
              kdu_int32 val = sp->ival;
              if (val < min_val)
                sp->ival = (kdu_int16)min_val;
              else if (val > max_val)
                sp->ival = (kdu_int16)max_val;
            }
        }
      else
        { // Clip exponent only
          kdu_int32 sign_mask = ((kdu_int32)-1)<<(orig_precision-1);
          kdu_int32 mag_mask = ~sign_mask;
          for (; n > 0; n--, sp++)
            { 
              kdu_int32 val = sp->ival;
              if ((val & mag_mask) > mag_max)
                sp->ival = (kdu_int16)((val & sign_mask) | mag_max);
            }          
        }
    }
}

/*****************************************************************************/
/* STATIC               convert_ints_as_floats_to_floats                     */
/*****************************************************************************/

static void
  convert_ints_as_floats_to_floats(kdu_sample32 *src, kdu_byte *dest,
                                   int num, int exponent_bits,
                                   int orig_precision, bool is_signed,
                                   bool littlendian, int inter_sample_bytes)
  /* This function is similar to `convert_ints_to_words', except that it
     writes 32-bit true IEEE floating point values to `dest', rather than
     integer words.  The floating point values are obtained by re-interpreting
     the input bit patterns in `src' as floating point values with
     N=`orig_precision' bits, E=`exponent_bits' exponent bits and
     N-1-E mantissa bits.  The floating point conversion is the same as
     that performed by `convert_ints_as_floats_to_bytes', except that the
     output values are retained in floating point format and written
     directly to `dest' with 4 bytes per sample.  This function is used to
     write TIFF files with true floating-point sample values. */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = 4;
  assert(inter_sample_bytes >= 4);
  if (orig_precision > 32)
    { 
      orig_precision = 32;
      if (exponent_bits > 31)
        exponent_bits = 31;
    }
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;

  int mantissa_bits = orig_precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from source exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly bring
      // denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // In this case, this is the smaller exponent bound
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1; // Allow max mantissa
  kdu_int32 pre_adjust = exp_adjust<<mantissa_bits; // Subtract before shifts 

  if (!is_signed)
    { // Processing unsigned samples
      kdu_int32 in_off = 1<<(orig_precision-1);
      kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
      kdu_int32 in_max = mag_max - in_off;    // integer level offset in place
      for (; num > 0; num--, src++, dest+=inter_sample_bytes)
        { 
          kdu_int32 val = src->ival;
          if (val < in_min)
            val = in_min; // Avoid exponent underflow and negative values
          else if (val > in_max)
            val = in_max; // Avoid exponent overflow
          val += in_off; // Remove the integer level offset
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          cast.int_val = val;
          cast.float_val *= denorm_scale; // Compensate for `exp_off' < 127
          val = cast.int_val;
          if (littlendian)
            { dest[0] = (kdu_byte) val;       dest[1] = (kdu_byte)(val>>8);
              dest[2] = (kdu_byte)(val>>16);  dest[3] = (kdu_byte)(val>>24); }
          else
            { dest[0] = (kdu_byte)(val>>24);  dest[1] = (kdu_byte)(val>>16);
              dest[2] = (kdu_byte)(val>>8);   dest[3] = (kdu_byte) val; }
        }
    }
  else
    { // Processing signed samples
      kdu_int32 mag_mask = ~(((kdu_int32)-1) << (orig_precision-1));
      for (; num > 0; num--, src++, dest+=inter_sample_bytes)
        { 
          kdu_int32 val = src->ival;
          kdu_int32 sign_bit = val & KDU_INT32_MIN;
          val &= mag_mask; // Keep just the magnitudes
          if (val < pre_adjust)
            val = pre_adjust; // Avoid exponent underflow
          else if (val > mag_max)
            val = mag_max; // Avoid exponent overflow
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = val;
          cast.float_val *= denorm_scale; // Compensate for `exp_off' < 127
          val = cast.int_val;          
          if (littlendian)
            { dest[0] = (kdu_byte) val;       dest[1] = (kdu_byte)(val>>8);
              dest[2] = (kdu_byte)(val>>16);  dest[3] = (kdu_byte)(val>>24); }
          else
            { dest[0] = (kdu_byte)(val>>24);  dest[1] = (kdu_byte)(val>>16);
              dest[2] = (kdu_byte)(val>>8);   dest[3] = (kdu_byte) val; }          
        }
    }
}

/*****************************************************************************/
/* STATIC              convert_shorts_as_floats_to_floats                    */
/*****************************************************************************/

static void
  convert_shorts_as_floats_to_floats(kdu_sample16 *src, kdu_byte *dest,
                                     int num, int exponent_bits,
                                     int orig_precision, bool is_signed,
                                     bool littlendian, int inter_sample_bytes)
  /* As above, but with 16-bit absolute integer inputs. */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = 4;
  assert(inter_sample_bytes >= 4);
  if (orig_precision > 32)
    { 
      orig_precision = 32;
      if (exponent_bits > 31)
        exponent_bits = 31;
    }
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;

  int mantissa_bits = orig_precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from source exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly bring
      // denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // In this case, this is the smaller exponent bound
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1; // Allow max mantissa
  kdu_int32 pre_adjust = exp_adjust<<mantissa_bits; // Subtract before shifts 

  if (!is_signed)
    { // Processing unsigned samples
      kdu_int32 in_off = 1<<(orig_precision-1);
      kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
      kdu_int32 in_max = mag_max - in_off;    // integer level offset in place
      for (; num > 0; num--, src++, dest+=inter_sample_bytes)
        { 
          kdu_int32 val = src->ival;
          if (val < in_min)
            val = in_min; // Avoid exponent underflow and negative values
          else if (val > in_max)
            val = in_max; // Avoid exponent overflow
          val += in_off; // Remove the integer level offset
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          cast.int_val = val;
          cast.float_val *= denorm_scale; // Compensate for `exp_off' < 127
          val = cast.int_val;
          if (littlendian)
            { dest[0] = (kdu_byte) val;       dest[1] = (kdu_byte)(val>>8);
              dest[2] = (kdu_byte)(val>>16);  dest[3] = (kdu_byte)(val>>24); }
          else
            { dest[0] = (kdu_byte)(val>>24);  dest[1] = (kdu_byte)(val>>16);
              dest[2] = (kdu_byte)(val>>8);   dest[3] = (kdu_byte) val; }
        }
    }
  else
    { // Processing signed samples
      kdu_int32 mag_mask = ~(((kdu_int32)-1) << (orig_precision-1));
      for (; num > 0; num--, src++, dest+=inter_sample_bytes)
        { 
          kdu_int32 val = src->ival;
          kdu_int32 sign_bit = val & KDU_INT32_MIN;
          val &= mag_mask; // Keep just the magnitudes
          if (val < pre_adjust)
            val = pre_adjust; // Avoid exponent underflow
          else if (val > mag_max)
            val = mag_max; // Avoid exponent overflow
          val -= pre_adjust; // Compensate for unlikely case: `exp_off' > 127
          if (mantissa_upshift >= 0)
            val <<= mantissa_upshift;
          else
            val >>= mantissa_downshift;
          val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = val;
          cast.float_val *= denorm_scale; // Compensate for `exp_off' < 127
          val = cast.int_val;          
          if (littlendian)
            { dest[0] = (kdu_byte) val;       dest[1] = (kdu_byte)(val>>8);
              dest[2] = (kdu_byte)(val>>16);  dest[3] = (kdu_byte)(val>>24); }
          else
            { dest[0] = (kdu_byte)(val>>24);  dest[1] = (kdu_byte)(val>>16);
              dest[2] = (kdu_byte)(val>>8);   dest[3] = (kdu_byte) val; }          
        }
    }
}

/*****************************************************************************/
/* STATIC                    convert_floats_to_words                         */
/*****************************************************************************/

static void
  convert_floats_to_words(kdu_sample32 *src, kdu_byte *dest, int num,
                          int precision, bool align_lsbs, int orig_precision,
                          bool is_signed, int sample_bytes, bool littlendian,
                          int inter_sample_bytes=0)
  /* Let `precision' be denoted by P and `orig_precision' be denoted by B.
     The `align_lsbs' argument determines how the function should behave if
     P and B differ.  The ultimate objective of the function is to store
     the source samples as P-bit integers, aligned at the least significant
     bit position with the LSB of each word which is written.  The steps
     performed by the function are as follows:
     a) Start by clipping the input x to the interval [x_min,x_max], where
        -- if `align_lsbs' is false, x_min=-0.5, x_max=0.5-2^{-min(B,P)})
        -- else is `is_signed', x_min=-0.5*2^{P-B}, x_max=0.5*2^{P-B}-2^{-B}
        -- else, x_min=-0.5, x_max=2^{P-B}-0.5-2^{-B}
     b) Next, find y = <x * alpha + off>, where <> denotes rounding to the
        nearest integer.
     c) Finally, write out the value y << S, where
        -- if `align_lsbs' is false, S = P - min{P,B}
        -- else S = 0
     In step (b), the parameters alpha and off are given by
        -- if `align_lsbs' is false, alpha = 2^{min{P,B}}
        -- else alpha = 2^B
        -- if `is_signed', off = 0, else off = 0.5*alpha
   */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = sample_bytes;
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
    // Based on the equations given above, the `align_lsbs' mode uses the
    // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  assert(precision <= (8*sample_bytes));
  
  float alpha, x_min=-0.5F, x_max=0.5;
  int upshift;
  if (align_lsbs)
    {
      upshift = 0;
      alpha = (float)(1<<orig_precision);
      if (diff_precision >= 0)
        x_min *= (float)(1<<diff_precision);
      else
        x_min *= 1.0F / (float)(1<<-diff_precision);
      x_max = -x_min;
      if (!is_signed)
        { x_min = -0.5F; x_max = x_max*2.0F - 0.5F; }
    }
  else
    {
      upshift = precision - min_precision;
      alpha = (float)(1<<min_precision);
    }
  x_max -= 1.0F / alpha;
  float offset = 0.5F + ((is_signed)?0.0F:0.5F*alpha);

  if (sample_bytes == 1)
    {
      float alpha8 = alpha * 256.0F; // So we can do integer rounding
      int off8 = (int)(offset * 256.0F + 0.5F);
      if (upshift == 0)
        { // Common case
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              float x = src->fval;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              int val = (off8 + (int)(x * alpha8)) >> 8;
              *dest = (kdu_byte) val;
            }
        }
      else
        {
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              float x = src->fval;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              int val = ((off8 + (int)(x * alpha8)) >> 8) << upshift;
              *dest = (kdu_byte) val;
            }
        }
    }
  else if (sample_bytes == 2)
    {
      float alpha8 = alpha * 256.0F; // So we can do integer rounding
      int off8 = (int)(offset * 256.0F + 0.5F);
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((off8 + (int)(x * alpha8)) >> 8) << upshift;
            dest[0] = (kdu_byte)(val>>8);
            dest[1] = (kdu_byte) val;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((off8 + (int)(x * alpha8)) >> 8) << upshift;
            dest[0] = (kdu_byte) val;
            dest[1] = (kdu_byte)(val>>8);
          }
    }
  else if (sample_bytes == 3)
    {
      float alpha4 = alpha * 16.0F; // So we can do integer rounding
      int off4 = (int)(offset * 16.0F + 0.5F);
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((off4 + (int)(x * alpha4)) >> 4) << upshift;
            dest[0] = (kdu_byte)(val>>16);
            dest[1] = (kdu_byte)(val>>8);
            dest[2] = (kdu_byte) val;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((off4 + (int)(x * alpha4)) >> 4) << upshift;
            dest[0] = (kdu_byte) val;
            dest[1] = (kdu_byte)(val>>8);
            dest[2] = (kdu_byte)(val>>16);
          }
    }
  else if (sample_bytes == 4)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((int) floor(offset + (x * alpha))) << upshift;
            dest[0] = (kdu_byte)(val>>24);
            dest[1] = (kdu_byte)(val>>16);
            dest[2] = (kdu_byte)(val>>8);
            dest[3] = (kdu_byte) val;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            float x = src->fval;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            int val = ((int) floor(offset + (x * alpha))) << upshift;
            dest[0] = (kdu_byte) val;
            dest[1] = (kdu_byte)(val>>8);
            dest[2] = (kdu_byte)(val>>16);
            dest[3] = (kdu_byte)(val>>24);
          }
    }
  else
    assert(0);
}

/*****************************************************************************/
/* STATIC                   convert_fixpoint_to_words                        */
/*****************************************************************************/

static void
  convert_fixpoint_to_words(kdu_sample16 *src, kdu_byte *dest, int num,
                            int precision, bool align_lsbs, int orig_precision,
                            bool is_signed, int sample_bytes,
                            bool littlendian, int inter_sample_bytes=0)
  /* Same as `convert_floats_to_words', except that the source samples
     are 16-bit fixed-point quantities with `KDU_FIX_POINT' fraction bits. */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = sample_bytes;
  int input_precision = KDU_FIX_POINT;
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
    // Based on the equations given above, the `align_lsbs' mode uses the
    // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  assert(precision <= (8*sample_bytes));
  
  int downshift, upshift, x_min, x_max;
  x_max = 1<<(input_precision-1);  x_min = -x_max;
  if (align_lsbs)
    {
      upshift = 0;
      downshift = input_precision - orig_precision;
      if (diff_precision > 0)
        x_max <<= diff_precision;
      else
        x_max >>= -diff_precision;
      x_min = -x_min;
      if (!is_signed)
        { x_min = -(1<<(input_precision-1)); x_max += x_max + x_min; }
      if (downshift < 0)
        { upshift -= downshift; downshift = 0; }
    }
  else
    {
      upshift = precision - min_precision;
      downshift = input_precision - min_precision;
    }
  x_max -= (1 << downshift);
  int offset = (1 << downshift)>>1; // Rounding offset
  if (!is_signed)
    offset += (1 << (input_precision-1));
  
  if (sample_bytes == 1)
    {
      if (upshift == 0)
        { // Common case
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = (x+offset) >> downshift;
              dest[0] = (kdu_byte) x;
            }
        }
      else
        {
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = ((x+offset) >> downshift) << upshift;
              dest[0] = (kdu_byte) x;
            }
        }
    }
  else if (sample_bytes == 2)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte)(x>>8);
            dest[1] = (kdu_byte) x;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte) x;
            dest[1] = (kdu_byte)(x>>8);
          }
    }
  else
    { kdu_error e; e << "Cannot use 16-bit fixed-point represetation for "
      "sample data processing, with high bit-depth decompressed data.  "
      "You may be receiving this error because you are trying to force "
      "a significant increase in the output file's sample bit-depth using "
      "the `-fprec' option to \"kdu_expand\".  If so, you should supply "
      "the `-precise' option as well, to increase the internal "
      "processing precision."; }
}

/*****************************************************************************/
/* STATIC                     convert_ints_to_words                          */
/*****************************************************************************/

static void
  convert_ints_to_words(kdu_sample32 *src, kdu_byte *dest, int num,
                        int precision, bool align_lsbs, int orig_precision,
                        bool is_signed, int sample_bytes, bool littlendian,
                        int inter_sample_bytes=0)
  /* Same as `convert_floats_to_words', except that the source samples
     are 32-bit quantities with nominal bit-depth `orig_precision'. */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = sample_bytes;
  
  int input_precision = orig_precision;
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
  // Based on the equations given above, the `align_lsbs' mode uses the
  // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  assert(precision <= (8*sample_bytes));
  
  int downshift, upshift, x_min, x_max;
  x_max = 1<<(input_precision-1);  x_min = -x_max;
  if (align_lsbs)
    {
      upshift = 0;
      downshift = input_precision - orig_precision;
      if (diff_precision > 0)
        x_max <<= diff_precision;
      else
        x_max >>= -diff_precision;
      x_min = -x_min;
      if (!is_signed)
        { x_min = -(1<<(input_precision-1)); x_max += x_max + x_min; }
      if (downshift < 0)
        { upshift -= downshift; downshift = 0; }
    }
  else
    {
      upshift = precision - min_precision;
      downshift = input_precision - min_precision;
    }
  x_max -= (1 << downshift);
  int offset = (1 << downshift)>>1; // Rounding offset
  if (!is_signed)
    offset += (1 << (input_precision-1));
  
  if (sample_bytes == 1)
    {
      if ((upshift == 0) && (downshift == 0))
        { // Common case
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = x + offset;
              dest[0] = (kdu_byte) x;
            }          
        }
      else
        {
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = ((x+offset) >> downshift) << upshift;
              dest[0] = (kdu_byte) x;
            }
        }          
    }
  else if (sample_bytes == 2)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte)(x>>8);
            dest[1] = (kdu_byte) x;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte) x;
            dest[1] = (kdu_byte)(x>>8);
          }
    }
  else if (sample_bytes == 3)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte)(x>>16);
            dest[1] = (kdu_byte)(x>>8);
            dest[2] = (kdu_byte) x;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte) x;
            dest[1] = (kdu_byte)(x>>8);
            dest[2] = (kdu_byte)(x>>16);
          }
    }
  else if (sample_bytes == 4)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte)(x>>24);
            dest[1] = (kdu_byte)(x>>16);
            dest[2] = (kdu_byte)(x>>8);
            dest[3] = (kdu_byte) x;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte) x;
            dest[1] = (kdu_byte)(x>>8);
            dest[2] = (kdu_byte)(x>>16);
            dest[3] = (kdu_byte)(x>>24);
          }
    }
  else
    assert(0);
}

/*****************************************************************************/
/* STATIC                    convert_shorts_to_words                         */
/*****************************************************************************/

static void
  convert_shorts_to_words(kdu_sample16 *src, kdu_byte *dest, int num,
                          int precision, bool align_lsbs, int orig_precision,
                          bool is_signed, int sample_bytes, bool littlendian,
                          int inter_sample_bytes=0)
  /* Same as `convert_floats_to_words', except that the source samples
     are 16-bit quantities with nominal bit-depth `orig_precision'. */
{
  if (inter_sample_bytes == 0)
    inter_sample_bytes = sample_bytes;
  
  int input_precision = orig_precision;
  int diff_precision = precision-orig_precision;
  int min_precision = (precision<orig_precision)?precision:orig_precision;
    // Based on the equations given above, the `align_lsbs' mode uses the
    // `diff_precision' value, while !`align_lsbs' uses `min_precision'.
  
  assert(precision <= (8*sample_bytes));
  
  int downshift, upshift, x_min, x_max;
  x_max = 1<<(input_precision-1);  x_min = -x_max;
  if (align_lsbs)
    {
      upshift = 0;
      downshift = input_precision - orig_precision;
      if (diff_precision > 0)
        x_max <<= diff_precision;
      else
        x_max >>= -diff_precision;
      x_min = -x_min;
      if (!is_signed)
        { x_min = -(1<<(input_precision-1)); x_max += x_max + x_min; }
      if (downshift < 0)
        { upshift -= downshift; downshift = 0; }
    }
  else
    {
      upshift = precision - min_precision;
      downshift = input_precision - min_precision;
    }
  x_max -= (1 << downshift);
  int offset = (1 << downshift)>>1; // Rounding offset
  if (!is_signed)
    offset += (1 << (input_precision-1));

  if (sample_bytes == 1)
    {
      if ((upshift == 0) && (downshift == 0))
        { // Common case
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = x + offset;
              dest[0] = (kdu_byte) x;          
            }
        }
      else
        {
          for (; num > 0; num--, src++, dest+=inter_sample_bytes)
            {
              int x = src->ival;
              x = (x >= x_min)?x:x_min;
              x = (x <= x_max)?x:x_max;
              x = ((x+offset) >> downshift) << upshift;
              dest[0] = (kdu_byte) x;
            }
        }
    }
  else if (sample_bytes == 2)
    {
      if (!littlendian)
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte)(x>>8);
            dest[1] = (kdu_byte) x;
          }
      else
        for (; num > 0; num--, src++, dest+=inter_sample_bytes)
          {
            int x = src->ival;
            x = (x >= x_min)?x:x_min;
            x = (x <= x_max)?x:x_max;
            x = ((x+offset) >> downshift) << upshift;
            dest[0] = (kdu_byte) x;
            dest[1] = (kdu_byte)(x>>8);
          }
    }
  else
    { kdu_error e; e << "Cannot use 16-bit representation with high "
      "bit-depth data"; }
}


/* ========================================================================= */
/*                                kdu_image_out                              */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdu_image_out::kdu_image_out                       */
/*****************************************************************************/

kdu_image_out::kdu_image_out(const char *fname, kdu_image_dims &dims,
                             int &next_comp_idx, bool &vflip, bool quiet)
{
  const char *suffix;

  out = NULL;
  vflip = false;
  if ((suffix = strrchr(fname,'.')) != NULL)
    {
      if ((strcmp(suffix+1,"pgm") == 0) || (strcmp(suffix+1,"PGM") == 0))
        out = new pgm_out(fname,dims,next_comp_idx);
      else if ((strcmp(suffix+1,"ppm") == 0) || (strcmp(suffix+1,"PPM") == 0))
        out = new ppm_out(fname,dims,next_comp_idx);
      else if ((strcmp(suffix+1,"bmp") == 0) || (strcmp(suffix+1,"BMP") == 0))
        { vflip = true; out = new bmp_out(fname,dims,next_comp_idx); }
      else if ((strcmp(suffix+1,"raw") == 0) || (strcmp(suffix+1,"RAW") == 0))
        out = new raw_out(fname,dims,next_comp_idx,false);
      else if ((strcmp(suffix+1,"rawl") == 0) || (strcmp(suffix+1,"RAWL")==0))
        out = new raw_out(fname,dims,next_comp_idx,true);
      else if ((strcmp(suffix+1,"tif")==0) || (strcmp(suffix+1,"TIF")==0) ||
               (strcmp(suffix+1,"tiff")==0) || (strcmp(suffix+1,"TIFF")==0))
        out = new tif_out(fname,dims,next_comp_idx,quiet);
    }
  if (out == NULL)
    { kdu_error e; e << "Image file, \"" << fname << ", does not have a "
      "recognized suffix.  Valid suffices are currently: "
      "\"bmp\", \"pgm\", \"ppm\", \"tif\", \"tiff\", \"raw\" and \"rawl\".  "
      "Upper or lower case may be used, but must be used consistently.";
    }
}


/* ========================================================================= */
/*                                  pgm_out                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              pgm_out::pgm_out                             */
/*****************************************************************************/

pgm_out::pgm_out(const char *fname, kdu_image_dims &dims, int &next_comp_idx)
{
  comp_idx = next_comp_idx++;
  if (comp_idx >= dims.get_num_components())
    { kdu_error e; e << "Output image files require more image components "
      "(or mapped colour channels) than are available!"; }
  rows = dims.get_height(comp_idx);
  cols = dims.get_width(comp_idx);
  precision = orig_precision = dims.get_bit_depth(comp_idx);
  forced_align_lsbs = false;
  forced_exponent = 0;
  int forced_prec = dims.get_forced_precision(comp_idx,forced_align_lsbs,
                                              forced_exponent);
  if (forced_prec == 0)
    forced_exponent = 0;
  else if (forced_prec > 0)
    precision = forced_prec;
  else if (forced_exponent > 0)
    precision = 8; // Map floats to the natural PGM sample precision
  orig_signed = dims.get_signed(comp_idx);
  if (orig_signed)
    { kdu_warning w;
      w << "Signed sample values will be written to the PGM file as unsigned "
           "8-bit quantities, centered about 128.";
    }
  if ((out = fopen(fname,"wb")) == NULL)
    { kdu_error e;
      e << "Unable to open output image file, \"" << fname <<"\"."; }
  fprintf(out,"P5\n%d %d\n255\n",cols,rows);
  incomplete_lines = free_lines = NULL;
  num_unwritten_rows = rows;
  initial_non_empty_tiles = 0; // Don't know yet.
}

/*****************************************************************************/
/*                              pgm_out::~pgm_out                            */
/*****************************************************************************/

pgm_out::~pgm_out()
{
  if ((num_unwritten_rows > 0) || (incomplete_lines != NULL))
    { kdu_warning w;
      w << "Not all rows of image component "
        << comp_idx << " were completed!";
    }
  image_line_buf *tmp;
  while ((tmp=incomplete_lines) != NULL)
    { incomplete_lines = tmp->next; delete tmp; }
  while ((tmp=free_lines) != NULL)
    { free_lines = tmp->next; delete tmp; }
  fclose(out);
}

/*****************************************************************************/
/*                                pgm_out::put                               */
/*****************************************************************************/

void
  pgm_out::put(int comp_idx, kdu_line_buf &line, int x_tnum)
{
  assert(comp_idx == this->comp_idx);
  if ((initial_non_empty_tiles != 0) && (x_tnum >= initial_non_empty_tiles))
    {
      assert(line.get_width() == 0);
      return;
    }

  image_line_buf *scan, *prev=NULL;
  for (scan=incomplete_lines; scan != NULL; prev=scan, scan=scan->next)
    {
      assert(scan->next_x_tnum >= x_tnum);
      if (scan->next_x_tnum == x_tnum)
        break;
    }
  if (scan == NULL)
    { // Need to open a new line buffer.
      assert(x_tnum == 0); // Must supply samples from left to right.
      if ((scan = free_lines) == NULL)
        scan = new image_line_buf(cols,1);
      free_lines = scan->next;
      if (prev == NULL)
        incomplete_lines = scan;
      else
        prev->next = scan;
      scan->accessed_samples = 0;
      scan->next_x_tnum = 0;
    }
  assert((scan->width-scan->accessed_samples) >= line.get_width());

  if (line.get_buf32() != NULL)
    { 
      if (forced_exponent > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent,
                                                orig_precision);
          convert_ints_as_floats_to_bytes(line.get_buf32(),
                                          scan->buf+scan->accessed_samples,
                                          line.get_width(),forced_exponent,
                                          precision,orig_precision,
                                          orig_signed,1);
        }
      else if (line.is_absolute())
        convert_ints_to_bytes(line.get_buf32(),
                              scan->buf+scan->accessed_samples,
                              line.get_width(),precision,
                              forced_align_lsbs,orig_precision,
                              orig_signed,1);
      else
        convert_floats_to_bytes(line.get_buf32(),
                                scan->buf+scan->accessed_samples,
                                line.get_width(),precision,
                                forced_align_lsbs,orig_precision,
                                orig_signed,1);
    }
  else
    { 
      if (forced_exponent > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent,
                                                orig_precision);
          convert_shorts_as_floats_to_bytes(line.get_buf16(),
                                            scan->buf+scan->accessed_samples,
                                            line.get_width(),forced_exponent,
                                            precision,orig_precision,
                                            orig_signed,1);
        }
      else if (line.is_absolute())
        convert_shorts_to_bytes(line.get_buf16(),
                                scan->buf+scan->accessed_samples,
                                line.get_width(),precision,
                                forced_align_lsbs,orig_precision,
                                orig_signed,1);
      else
        convert_fixpoint_to_bytes(line.get_buf16(),
                                  scan->buf+scan->accessed_samples,
                                  line.get_width(),precision,
                                  forced_align_lsbs,orig_precision,
                                  orig_signed,1);
    }

  scan->next_x_tnum++;
  scan->accessed_samples += line.get_width();
  if (scan->accessed_samples == scan->width)
    { // Write completed line and send it to the free list.
      if (initial_non_empty_tiles == 0)
        initial_non_empty_tiles = scan->next_x_tnum;
      else
        assert(initial_non_empty_tiles == scan->next_x_tnum);
      if (num_unwritten_rows == 0)
        { kdu_error e; e << "Attempting to write too many lines to image "
          "file for component " << comp_idx << "."; }
      if (fwrite(scan->buf,1,(size_t) scan->width,out) != (size_t) scan->width)
        { kdu_error e; e << "Unable to write to image file for component "
          << comp_idx
          << ". File may be write protected, or disk may be full."; }
      num_unwritten_rows--;
      assert(scan == incomplete_lines);
      incomplete_lines = scan->next;
      scan->next = free_lines;
      free_lines = scan;
    }
}


/* ========================================================================= */
/*                                  ppm_out                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              ppm_out::ppm_out                             */
/*****************************************************************************/

ppm_out::ppm_out(const char *fname, kdu_image_dims &dims, int &next_comp_idx)
{
  int n;

  first_comp_idx = next_comp_idx;
  if ((first_comp_idx+2) >= dims.get_num_components())
    { kdu_error e; e << "Output image files require more image components "
      "(or mapped colour channels) than are available!"; }
  rows = dims.get_height(first_comp_idx);
  cols = dims.get_width(first_comp_idx);
  orig_signed = dims.get_signed(first_comp_idx);
  for (n=0; n < 3; n++, next_comp_idx++)
    { 
      if ((rows != dims.get_height(next_comp_idx)) ||
          (cols != dims.get_width(next_comp_idx)) ||
          (orig_signed != dims.get_signed(next_comp_idx)))
        { kdu_error e; e << "Can only write a PPM file with 3 image "
          "components, all having the same dimensions and the same "
          "signed/unsigned characteristics."; }
      precision[n] = orig_precision[n] = dims.get_bit_depth(next_comp_idx);
      forced_align_lsbs[n] = false;
      forced_exponent[n] = 0;
      int forced_prec =
        dims.get_forced_precision(next_comp_idx,forced_align_lsbs[n],
                                  forced_exponent[n]);
      if (forced_prec == 0)
        forced_exponent[n] = 0;
      else if (forced_prec > 0)
        precision[n] = forced_prec;
      else if (forced_exponent[n] > 0)
        precision[n] = 8; // Map floats to the natural PPM sample precision
    }
  if (orig_signed)
    { kdu_warning w;
      w << "Signed sample values will be written to the "
           "PPM file as unsigned 8-bit quantities, centered about 128.";
    }
  if ((out = fopen(fname,"wb")) == NULL)
    { kdu_error e;
      e << "Unable to open output image file, \"" << fname <<"\"."; }
  fprintf(out,"P6\n%d %d\n255\n",cols,rows);

  incomplete_lines = NULL;
  free_lines = NULL;
  num_unwritten_rows = rows;
  initial_non_empty_tiles = 0; // Don't know yet.
}

/*****************************************************************************/
/*                              ppm_out::~ppm_out                            */
/*****************************************************************************/

ppm_out::~ppm_out()
{
  if ((num_unwritten_rows > 0) || (incomplete_lines != NULL))
    { kdu_warning w;
      w << "Not all rows of image components "
        << first_comp_idx << " through " << first_comp_idx+2
        << " were completed!";
    }
  image_line_buf *tmp;
  while ((tmp=incomplete_lines) != NULL)
    { incomplete_lines = tmp->next; delete tmp; }
  while ((tmp=free_lines) != NULL)
    { free_lines = tmp->next; delete tmp; }
  fclose(out);
}

/*****************************************************************************/
/*                                ppm_out::put                               */
/*****************************************************************************/

void
  ppm_out::put(int comp_idx, kdu_line_buf &line, int x_tnum)
{
  int idx = comp_idx - this->first_comp_idx;
  assert((idx >= 0) && (idx <= 2));
  x_tnum = x_tnum*3+idx; // Works so long as components written in order.
  if ((initial_non_empty_tiles != 0) && (x_tnum >= initial_non_empty_tiles))
    {
      assert(line.get_width() == 0);
      return;
    }

  image_line_buf *scan, *prev=NULL;
  for (scan=incomplete_lines; scan != NULL; prev=scan, scan=scan->next)
    {
      assert(scan->next_x_tnum >= x_tnum);
      if (scan->next_x_tnum == x_tnum)
        break;
    }
  if (scan == NULL)
    { // Need to open a new line buffer
      assert(x_tnum == 0); // Must consume in very specific order.
      if ((scan = free_lines) == NULL)
        scan = new image_line_buf(cols,3);
      free_lines = scan->next;
      if (prev == NULL)
        incomplete_lines = scan;
      else
        prev->next = scan;
      scan->accessed_samples = 0;
      scan->next_x_tnum = 0;
    }

  assert((scan->width-scan->accessed_samples) >= line.get_width());

  if (line.get_buf32() != NULL)
    { 
      if (forced_exponent[idx] > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_ints_as_floats_to_bytes(line.get_buf32(),
                                        scan->buf+3*scan->accessed_samples+idx,
                                        line.get_width(),forced_exponent[idx],
                                        precision[idx],orig_precision[idx],
                                        orig_signed,3);
        }
      else if (line.is_absolute())
        convert_ints_to_bytes(line.get_buf32(),
                              scan->buf+3*scan->accessed_samples+idx,
                              line.get_width(),precision[idx],
                              forced_align_lsbs[idx],orig_precision[idx],
                              orig_signed,3);
      else
        convert_floats_to_bytes(line.get_buf32(),
                                scan->buf+3*scan->accessed_samples+idx,
                                line.get_width(),precision[idx],
                                forced_align_lsbs[idx],orig_precision[idx],
                                orig_signed,3);
    }
  else
    { 
      if (forced_exponent[idx] > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_shorts_as_floats_to_bytes(line.get_buf16(),
                                        scan->buf+3*scan->accessed_samples+idx,
                                        line.get_width(),forced_exponent[idx],
                                        precision[idx],orig_precision[idx],
                                        orig_signed,3);
        }
      else if (line.is_absolute())
        convert_shorts_to_bytes(line.get_buf16(),
                                scan->buf+3*scan->accessed_samples+idx,
                                line.get_width(),precision[idx],
                                forced_align_lsbs[idx],orig_precision[idx],
                                orig_signed,3);
      else
        convert_fixpoint_to_bytes(line.get_buf16(),
                                  scan->buf+3*scan->accessed_samples+idx,
                                  line.get_width(),precision[idx],
                                  forced_align_lsbs[idx],orig_precision[idx],
                                  orig_signed,3);
    }

  scan->next_x_tnum++;
  if (idx == 2)
    scan->accessed_samples += line.get_width();
  if (scan->accessed_samples == scan->width)
    { // Write completed line and send it to the free list.
      if (initial_non_empty_tiles == 0)
        initial_non_empty_tiles = scan->next_x_tnum;
      else
        assert(initial_non_empty_tiles == scan->next_x_tnum);
      if (num_unwritten_rows == 0)
        { kdu_error e; e << "Attempting to write too many lines to image "
          "file for components " << first_comp_idx << " through "
          << first_comp_idx+2 << "."; }
      if (fwrite(scan->buf,1,(size_t)(scan->width*3),out) !=
          (size_t)(scan->width*3))
        { kdu_error e; e << "Unable to write to image file for components "
          << first_comp_idx << " through " << first_comp_idx+2
          << ". File may be write protected, or disk may be full."; }
      num_unwritten_rows--;
      assert(scan == incomplete_lines);
      incomplete_lines = scan->next;
      scan->next = free_lines;
      free_lines = scan;
    }
}


/* ========================================================================= */
/*                                  raw_out                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              raw_out::raw_out                             */
/*****************************************************************************/

raw_out::raw_out(const char *fname, kdu_image_dims &dims, int &next_comp_idx,
                 bool littlendian)
{
  comp_idx = next_comp_idx++;
  if (comp_idx >= dims.get_num_components())
    { kdu_error e; e << "Output image files require more image components "
      "(or mapped colour channels) than are available!"; }
  rows = dims.get_height(comp_idx);
  cols = dims.get_width(comp_idx);
  precision = orig_precision = dims.get_bit_depth(comp_idx);
  forced_align_lsbs = false;
  forced_exponent = 0;
  int forced_prec = dims.get_forced_precision(next_comp_idx,forced_align_lsbs,
                                              forced_exponent);
  if (forced_prec == 0)
    forced_exponent = 0;
  else if (forced_prec > 0)
    precision = forced_prec;
  if (forced_exponent > 0)
    forced_align_lsbs = false; // So we can use regular conversion funcs
  is_signed = dims.get_signed(comp_idx);
  sample_bytes = (precision+7)>>3;
  if (sample_bytes > 4)
    { kdu_error e; e << "Unable to accommodate output raw output files "
      "with more than 4 bytes per sample -- i.e., 32 bit precision output "
      "samples.  Looks like you have a real doozy of an image here, but "
      "you can use the \"-fprec\" option to \"kdu_expand\" to force "
      "the output samples to a lower precision, aligning the result at "
      "the least-significant or most-significant bit positions, depending "
      "on how you wish to interpret the sample values."; }
  incomplete_lines = free_lines = NULL;
  num_unwritten_rows = rows;  
  if ((out = fopen(fname,"wb")) == NULL)
    { kdu_error e;
      e << "Unable to open output image file, \"" << fname <<"\".";}
  initial_non_empty_tiles = 0; // Don't know yet.
  this->littlendian = littlendian;
}

/*****************************************************************************/
/*                              raw_out::~raw_out                            */
/*****************************************************************************/

raw_out::~raw_out()
{
  if ((num_unwritten_rows > 0) || (incomplete_lines != NULL))
    { kdu_warning w;
      w << "Not all rows of image component "
        << comp_idx << " were produced!";
    }
  image_line_buf *tmp;
  while ((tmp=incomplete_lines) != NULL)
    { incomplete_lines = tmp->next; delete tmp; }
  while ((tmp=free_lines) != NULL)
    { free_lines = tmp->next; delete tmp; }
  fclose(out);
}

/*****************************************************************************/
/*                                raw_out::put                               */
/*****************************************************************************/

void
  raw_out::put(int comp_idx, kdu_line_buf &line, int x_tnum)
{
  assert(comp_idx == this->comp_idx);
  if ((initial_non_empty_tiles != 0) && (x_tnum >= initial_non_empty_tiles))
    {
      assert(line.get_width() == 0);
      return;
    }

  image_line_buf *scan, *prev=NULL;
  for (scan=incomplete_lines; scan != NULL; prev=scan, scan=scan->next)
    {
      assert(scan->next_x_tnum >= x_tnum);
      if (scan->next_x_tnum == x_tnum)
        break;
    }
  if (scan == NULL)
    { // Need to open a new line buffer.
      assert(x_tnum == 0); // Must supply samples from left to right.
      if ((scan = free_lines) == NULL)
        scan = new image_line_buf(cols,sample_bytes);
      free_lines = scan->next;
      if (prev == NULL)
        incomplete_lines = scan;
      else
        prev->next = scan;
      scan->accessed_samples = 0;
      scan->next_x_tnum = 0;
    }
  assert((scan->width-scan->accessed_samples) >= line.get_width());

  if (forced_exponent > 0)
    { // By and large we can just treat the re-interpreted floats as
      // integers and use the standard conversion functions below.  However,
      // we must be sure that we are processing absolute integers and we
      // must be sure that unsigned values will be written as floats with
      // a sign bit of 0.
      clip_ints_interpreted_as_floats(line,forced_exponent,
                                      orig_precision,is_signed);
    }
  
  if (line.get_buf32() != NULL)
    {
      if (line.is_absolute())
        convert_ints_to_words(line.get_buf32(),
                              scan->buf+sample_bytes*scan->accessed_samples,
                              line.get_width(),precision,forced_align_lsbs,
                              orig_precision,is_signed,
                              sample_bytes,littlendian);
      else
        convert_floats_to_words(line.get_buf32(),
                                scan->buf+sample_bytes*scan->accessed_samples,
                                line.get_width(),precision,forced_align_lsbs,
                                orig_precision,is_signed,
                                sample_bytes,littlendian);
    }
  else
    {
      if (line.is_absolute())
        convert_shorts_to_words(line.get_buf16(),
                                scan->buf+sample_bytes*scan->accessed_samples,
                                line.get_width(),precision,forced_align_lsbs,
                                orig_precision,is_signed,
                                sample_bytes,littlendian);
      else
        convert_fixpoint_to_words(line.get_buf16(),
                                 scan->buf+sample_bytes*scan->accessed_samples,
                                 line.get_width(),precision,forced_align_lsbs,
                                 orig_precision,is_signed,
                                 sample_bytes,littlendian);
    }

  scan->next_x_tnum++;
  scan->accessed_samples += line.get_width();
  if (scan->accessed_samples == scan->width)
    { // Write completed line and send it to the free list.
      if (initial_non_empty_tiles == 0)
        initial_non_empty_tiles = scan->next_x_tnum;
      else
        assert(initial_non_empty_tiles == scan->next_x_tnum);
      if (num_unwritten_rows == 0)
        { kdu_error e; e << "Attempting to write too many lines to image "
          "file for component " << comp_idx << "."; }
      if (fwrite(scan->buf,1,(size_t)(scan->width*scan->sample_bytes),out) !=
          (size_t)(scan->width*scan->sample_bytes))
        { kdu_error e; e << "Unable to write to image file for component "
          << comp_idx
          << ". File may be write protected, or disk may be full."; }
      num_unwritten_rows--;
      assert(scan == incomplete_lines);
      incomplete_lines = scan->next;
      scan->next = free_lines;
      free_lines = scan;
    }
}


/* ========================================================================= */
/*                                  bmp_out                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              bmp_out::bmp_out                             */
/*****************************************************************************/

bmp_out::bmp_out(const char *fname, kdu_image_dims &dims, int &next_comp_idx)
{
  int n;

  first_comp_idx = next_comp_idx;
  num_components = dims.get_num_components() - first_comp_idx;
  if (num_components <= 0)
    { kdu_error e; e << "Output image files require more image components "
      "(or mapped colour channels) than are available!"; }
  if (num_components >= 3)
    num_components = 3;
  else
    num_components = 1;
  rows = dims.get_height(first_comp_idx);  
  cols = dims.get_width(first_comp_idx);
  orig_signed = dims.get_signed(first_comp_idx);
  for (n=0; n < num_components; n++, next_comp_idx++)
    { 
      if ((rows != dims.get_height(next_comp_idx)) ||
          (cols != dims.get_width(next_comp_idx)) ||
          (orig_signed != dims.get_signed(next_comp_idx)))
        { assert(n > 0); num_components = 1; break; }
      forced_align_lsbs[n] = false;
      forced_exponent[n] = 0;
      precision[n] = orig_precision[n] = dims.get_bit_depth(next_comp_idx);
      int forced_prec =
        dims.get_forced_precision(next_comp_idx,forced_align_lsbs[n],
                                  forced_exponent[n]);
      if (forced_prec == 0)
        forced_exponent[n] = 0;
      else if (forced_prec > 0)
        precision[n] = forced_prec;
      else if (forced_exponent[n] > 0)
        precision[n] = 8; // Map floats to the natural BMP sample precision
    }
  next_comp_idx = first_comp_idx + num_components;
  if (orig_signed)
    { kdu_warning w;
      w << "Signed sample values will be written to the "
           "BMP file as unsigned 8-bit quantities, centered about 128.";
    }

  kdu_byte magic[14];
  bmp_header header;
  int header_bytes = 14+sizeof(header);
  assert(header_bytes == 54);
  if (num_components == 1)
    header_bytes += 1024; // Need colour LUT.
  int line_bytes = num_components * cols;
  alignment_bytes = (4-line_bytes) & 3;
  line_bytes += alignment_bytes;
  int file_bytes = line_bytes*rows + header_bytes;
  magic[0] = 'B'; magic[1] = 'M';
  magic[2] = (kdu_byte) file_bytes;
  magic[3] = (kdu_byte)(file_bytes>>8);
  magic[4] = (kdu_byte)(file_bytes>>16);
  magic[5] = (kdu_byte)(file_bytes>>24);
  magic[6] = magic[7] = magic[8] = magic[9] = 0;
  magic[10] = (kdu_byte) header_bytes;
  magic[11] = (kdu_byte)(header_bytes>>8);
  magic[12] = (kdu_byte)(header_bytes>>16);
  magic[13] = (kdu_byte)(header_bytes>>24);
  header.size = 40;
  header.width = cols;
  header.height = rows;
  header.planes_bits = 1; // Set `planes'=1 (mandatory)
  header.planes_bits |= ((num_components==1)?8:24) << 16; // Set bits per pel.
  header.compression = 0;
  header.image_size = 0;

  bool res_units_known;
  double xppm, yppm;
  if (dims.get_resolution(first_comp_idx,res_units_known,xppm,yppm) &&
      (res_units_known || (xppm != yppm)) &&
      dims.allow_save_resolution())
    { // Record display resolution in BMP header
      if (!res_units_known)
        { // Choose a reasonable scale factor -- 72 dpi
          double scale = (72.0*1000.0/25.4) / xppm;
          xppm *= scale;  yppm *= scale;
        }
      header.xpels_per_metre = (kdu_int32)(xppm+0.5);
      header.ypels_per_metre = (kdu_int32)(yppm+0.5);
    }
  else
    header.xpels_per_metre = header.ypels_per_metre = 0;

  header.num_colours_used = header.num_colours_important = 0;
  to_little_endian((kdu_int32 *) &header,10);
  if ((out = fopen(fname,"wb")) == NULL)
    { kdu_error e; e << "Unable to open output image file, \"" << fname <<"\".";}
  fwrite(magic,1,14,out);
  fwrite(&header,1,40,out);
  if (num_components == 1)
    for (n=0; n < 256; n++)
      { fputc(n,out); fputc(n,out); fputc(n,out); fputc(0,out); }
  incomplete_lines = NULL;
  free_lines = NULL;
  num_unwritten_rows = rows;
  initial_non_empty_tiles = 0; // Don't know yet.
}

/*****************************************************************************/
/*                              bmp_out::~bmp_out                            */
/*****************************************************************************/

bmp_out::~bmp_out()
{
  if ((num_unwritten_rows > 0) || (incomplete_lines != NULL))
    { kdu_warning w;
      w << "Not all rows of image components "
        << first_comp_idx << " through "
        << first_comp_idx+num_components-1
        << " were completed!";
    }
  image_line_buf *tmp;
  while ((tmp=incomplete_lines) != NULL)
    { incomplete_lines = tmp->next; delete tmp; }
  while ((tmp=free_lines) != NULL)
    { free_lines = tmp->next; delete tmp; }
  fclose(out);
}

/*****************************************************************************/
/*                                bmp_out::put                               */
/*****************************************************************************/

void
  bmp_out::put(int comp_idx, kdu_line_buf &line, int x_tnum)
{
  int idx = comp_idx - this->first_comp_idx;
  assert((idx >= 0) && (idx < num_components));
  x_tnum = x_tnum*num_components+idx;
  if ((initial_non_empty_tiles != 0) && (x_tnum >= initial_non_empty_tiles))
    {
      assert(line.get_width() == 0);
      return;
    }

  image_line_buf *scan, *prev=NULL;
  for (scan=incomplete_lines; scan != NULL; prev=scan, scan=scan->next)
    {
      assert(scan->next_x_tnum >= x_tnum);
      if (scan->next_x_tnum == x_tnum)
        break;
    }
  if (scan == NULL)
    { // Need to open a new line buffer
      assert(x_tnum == 0); // Must generate in very specific order.
      if ((scan = free_lines) == NULL)
        {
          scan = new image_line_buf(cols+3,num_components);
          for (int k=0; k < alignment_bytes; k++)
            scan->buf[num_components*cols+k] = 0;
        }
      free_lines = scan->next;
      if (prev == NULL)
        incomplete_lines = scan;
      else
        prev->next = scan;
      scan->accessed_samples = 0;
      scan->next_x_tnum = 0;
    }

  assert((cols-scan->accessed_samples) >= line.get_width());
  int comp_offset = (num_components==3)?(2-idx):0;
  kdu_byte *dst = scan->buf+num_components*scan->accessed_samples+comp_offset;
  if (line.get_buf32() != NULL)
    { 
      if (forced_exponent[idx] > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_ints_as_floats_to_bytes(line.get_buf32(),dst,
                                          line.get_width(),
                                          forced_exponent[idx],
                                          precision[idx],orig_precision[idx],
                                          orig_signed,num_components);
        }
      else if (line.is_absolute())
        convert_ints_to_bytes(line.get_buf32(),dst,line.get_width(),
                              precision[idx],forced_align_lsbs[idx],
                              orig_precision[idx],orig_signed,
                              num_components);
      else
        convert_floats_to_bytes(line.get_buf32(),dst,line.get_width(),
                                precision[idx],forced_align_lsbs[idx],
                                orig_precision[idx],orig_signed,
                                num_components);
    }
  else
    { 
      if (forced_exponent[idx] > 0)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_shorts_as_floats_to_bytes(line.get_buf16(),dst,
                                            line.get_width(),
                                            forced_exponent[idx],
                                            precision[idx],orig_precision[idx],
                                            orig_signed,num_components);
        }
      else if (line.is_absolute())
        convert_shorts_to_bytes(line.get_buf16(),dst,line.get_width(),
                                precision[idx],forced_align_lsbs[idx],
                                orig_precision[idx],orig_signed,
                                num_components);
      else
        convert_fixpoint_to_bytes(line.get_buf16(),dst,line.get_width(),
                                  precision[idx],forced_align_lsbs[idx],
                                  orig_precision[idx],orig_signed,
                                  num_components);
    }

  scan->next_x_tnum++;
  if (idx == (num_components-1))
    scan->accessed_samples += line.get_width();
  if (scan->accessed_samples == cols)
    { // Write completed line and send it to the free list.
      if (initial_non_empty_tiles == 0)
        initial_non_empty_tiles = scan->next_x_tnum;
      else
        assert(initial_non_empty_tiles == scan->next_x_tnum);
      if (num_unwritten_rows == 0)
        { kdu_error e; e << "Attempting to write too many lines to image "
          "file for components " << first_comp_idx << " through "
          << first_comp_idx+num_components-1 << "."; }
      if (fwrite(scan->buf,1,(size_t)(cols*num_components+alignment_bytes),
                 out) != (size_t)(cols*num_components+alignment_bytes))
        { kdu_error e; e << "Unable to write to image file for components "
          << first_comp_idx << " through " << first_comp_idx+num_components-1
          << ". File may be write protected, or disk may be full."; }
      num_unwritten_rows--;
      assert(scan == incomplete_lines);
      incomplete_lines = scan->next;
      scan->next = free_lines;
      free_lines = scan;
    }
}


/* ========================================================================= */
/*                                  tif_out                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                              tif_out::tif_out                             */
/*****************************************************************************/

tif_out::tif_out(const char *fname, kdu_image_dims &dims, int &next_comp_idx,
                 bool quiet)
{
  // Initialize state information in case we have to cleanup prematurely
  orig_precision = NULL;
  is_signed = NULL;
  forced_exponent = NULL;
  incomplete_lines = NULL;
  free_lines = NULL;
  num_unwritten_rows = 0;
  initial_non_empty_tiles = 0; // Don't know yet.

  // Find max image components
  first_comp_idx = next_comp_idx;
  num_components = dims.get_num_components() - first_comp_idx;
  if (num_components <= 0)
    { kdu_error e; e << "Output image files require more image components "
      "(or mapped colour channels) than are available!"; }

  // Find the colour space and alpha properties of the file to be written
  int num_colours = 1;
  int icc_profile_len = 0;
  bool have_premultiplied_alpha = false;
  kdu_uint16 photometric_type = KDU_TIFF_PhotometricInterp_BLACKISZERO;
  //kdu_uint16 inkset=0, numberofinks=0;
  if (next_comp_idx > 0)
    num_components = 1; // Just write one component in each non-initial file
  else
    { 
      bool have_unassociated_alpha=false;
      int colour_space_confidence=0;
      jp2_colour_space colour_space=JP2_sLUM_SPACE;
      num_colours =
        dims.get_colour_info(have_premultiplied_alpha,have_unassociated_alpha,
                             colour_space_confidence,colour_space,
                             icc_profile_len);
      if ((num_colours > num_components) || (num_colours == 2))
        num_colours = num_components = 1; // Write one monochrome component
      else if (colour_space_confidence <= 0)
        {
          if (num_colours == 3)
            photometric_type = KDU_TIFF_PhotometricInterp_RGB;
          else if (num_colours != 1)
            num_colours = num_components = 1;
        }
      else if (colour_space == JP2_sLUM_SPACE)
        {
          assert(num_colours == 1);
          photometric_type = KDU_TIFF_PhotometricInterp_BLACKISZERO;
        }
      else if (colour_space == JP2_sRGB_SPACE)
        {
          assert(num_colours == 3);
          photometric_type = KDU_TIFF_PhotometricInterp_RGB;
        }
      else if (colour_space == JP2_CMYK_SPACE)
        {
          assert(num_colours == 4);
          photometric_type = KDU_TIFF_PhotometricInterp_SEPARATED;
          //inkset = KDU_TIFF_InkSet_CMYK;
          //numberofinks = 4;
        }
      else if (colour_space == JP2_bilevel1_SPACE)
        {
          assert(num_colours == 1);
          photometric_type = KDU_TIFF_PhotometricInterp_WHITEISZERO;
        }
      else if (colour_space == JP2_bilevel2_SPACE)
        {
          assert(num_colours == 1);
          photometric_type = KDU_TIFF_PhotometricInterp_BLACKISZERO;
        }
      else if (num_colours == 3)
        {
          photometric_type = KDU_TIFF_PhotometricInterp_RGB;
          kdu_warning w; w << "Trying to save uncommon 3-colour space to "
            "TIFF file (JP2 colour space identifier is "
            << (int) colour_space << ").  "
            "The current TIFF writer module will record this as an "
            "RGB space, possibly eroneously.";
        }
      else if (num_colours > 3)
        {
          photometric_type = KDU_TIFF_PhotometricInterp_SEPARATED;
          //inkset = KDU_TIFF_InkSet_NotCMYK;
          //numberofinks = (kdu_uint16) num_colours;
          kdu_warning w; w << "Trying to save non-CMYK colour space with "
            "more than 3 colour channels to TIFF file (JP2 colour space "
            "identifier is " << (int) colour_space << ").  "
            "The current TIFF writer module will record this as a "
            "separated colour space, but cannot determine TIFF ink names.";
        }
      else
        {
          assert(num_colours == 1);
          photometric_type = KDU_TIFF_PhotometricInterp_BLACKISZERO;
          kdu_warning w; w << "Unrecognized monochromatic colour space "
            "will be recorded in TIFF file as having the BLACK-IS-ZERO "
            "photometric type.";
        }
      if (num_colours >= num_components)
        have_premultiplied_alpha = false; // Alpha has been discarded
      if (have_unassociated_alpha)
        { kdu_warning w; w << "Alpha channel cannot be identified in a TIFF "
          "file since it is of the unassociated (i.e., not premultiplied) "
          "type, and these are not supported by TIFF.  You can save this "
          "to a separate output file."; }
      num_components = num_colours + ((have_premultiplied_alpha)?1:0);
    }

  rows = dims.get_height(first_comp_idx);  
  cols = dims.get_width(first_comp_idx);

  // Find component dimensions and other info.
  is_signed = new bool[num_components];
  orig_precision = new int[num_components];
  precision = 0; // Just for now
  forced_align_lsbs = false; // Just for now
  int n;
  for (n=0; n < num_components; n++, next_comp_idx++)
    { 
      is_signed[n] = dims.get_signed(next_comp_idx);
      int comp_prec = orig_precision[n] = dims.get_bit_depth(next_comp_idx);
      bool align_lsbs = false;
      int forced_exp = 0;
      int forced_prec = dims.get_forced_precision(next_comp_idx,align_lsbs,
                                                  forced_exp);
      if (forced_prec == 0)
        forced_exp = 0;
      else if (forced_prec > 0)
        comp_prec = forced_prec;
      else if (forced_exp > 0)
        comp_prec = 32; // Most natural to map floats directly to 32-bit floats
      if (n == 0)
        { 
          precision = comp_prec;
          forced_align_lsbs = align_lsbs;
          if (forced_exp > 0)
            { // Need to allocate `forced_exponent' array; all components will
              // need to have exponents -- i.e., all components stored in this
              // file must be stored as floats.
              assert(forced_exponent == NULL);
              forced_exponent = new int[num_components];
              forced_exponent[0] = forced_exp;
            }
        }
      if ((rows != dims.get_height(next_comp_idx)) ||
          (cols != dims.get_width(next_comp_idx)) ||
          (comp_prec != precision) || (forced_align_lsbs != align_lsbs) ||
          ((forced_exponent != NULL) && (forced_exp <= 0)) ||
          ((forced_exponent == NULL) && (forced_exp > 0)))
        {
          assert(n > 0);
          num_colours=num_components=1;
          have_premultiplied_alpha=false;
          photometric_type = KDU_TIFF_PhotometricInterp_BLACKISZERO;
          break;
        }
      if (forced_exponent != NULL)
        { // All components must be written as floats but conversion can differ
          assert(forced_exp > 0);
          forced_exponent[n] = forced_exp;
        }
    }
  next_comp_idx = first_comp_idx + num_components;

  // Find the sample, pixel and line dimensions
  if (forced_exponent != NULL)
    precision = 32; // Write as 32-bit IEEE floats regardless
  if (precision <= 8)
    sample_bytes = 1;
  else if (precision <= 16)
    sample_bytes = 2;
  else if (precision <= 32)
    sample_bytes = 4;
  else
    { kdu_error e;
      e << "Cannot write output with sample precision in excess of 32 bits "
           "per sample.  You may like to use the \"-fprec\" option to "
           "\"kdu_expand\" to force the writing of decompressed output "
           "samples with a different precision."; }
  pixel_bytes = sample_bytes * num_components;
  row_bytes = pixel_bytes * cols;
  scanline_width = num_components * precision * cols;
  scanline_width = (scanline_width+7)>>3;

  // Find the resolution info
  bool res_units_known=false;
  double xppm, yppm;
  if (!dims.get_resolution(first_comp_idx,res_units_known,xppm,yppm))
    { xppm = 1.0; yppm = 1.0; }
  kdu_uint16 resolution_unit = KDU_TIFF_ResolutionUnit_CM;
  if (!res_units_known)
    resolution_unit = KDU_TIFF_ResolutionUnit_NONE;
  float xpels_per_cm = (float)(xppm*0.01);
  float ypels_per_cm = (float)(yppm*0.01);

  // Check for XMP, IPTC and GeoJP2 meta data
  jpx_meta_manager meta_manager = dims.get_meta_manager();
  jp2_input_box xmp_box;
  jp2_input_box iptc_box;
  jp2_input_box geo_box;
  if (meta_manager.exists())
    {
      jpx_metanode scn; 
      jpx_metanode mn = meta_manager.access_root();
      int cnt;
      jp2_family_src *jsrc;
      for (cnt=0; (scn=mn.get_descendant(cnt)).exists(); cnt++)
        {
          if (scn.is_xmp_uuid())
            { // Found XMP uuid box
              jp2_locator loc = scn.get_existing(jsrc);
              xmp_box.open(jsrc,loc);
              xmp_box.seek(16); // Seek over the UUID code
              break;
          }
        }
      for (cnt=0; (scn=mn.get_descendant(cnt)).exists(); cnt++)
        {
          if (scn.is_iptc_uuid())
            { // Found IPTC uuid box
              jp2_locator loc = scn.get_existing(jsrc);
              iptc_box.open(jsrc,loc);
              iptc_box.seek(16); // Seek over the UUID code
              break;
            }
        }
      for (cnt=0; (scn=mn.get_descendant(cnt)).exists(); cnt++)
        if (scn.is_geojp2_uuid())
          { // Found GeoJP2 box
            jp2_locator loc = scn.get_existing(jsrc);
            geo_box.open(jsrc,loc);
            geo_box.seek(16); // Seek over the UUID code
            break;
          }
    }

  // Create TIFF directory entries
  kdu_long out_byte_count = ((kdu_long)rows) * ((kdu_long)scanline_width);
  bool use_bigtiff = ((out_byte_count>>1) > ((kdu_long) 1800000000));
                       // 3.6GB gives us space for headers etc.
  
  kdu_tiffdir tiffdir;
  tiffdir.init(tiffdir.is_native_littlendian(),use_bigtiff);
       // Create a TIFF file which uses native byte order.  Everything should
       // work correctly if you choose to force the byte order to be one of
       // little-endian or big-endian, since the `pre_pack_littlendian' member
       // is set (near the end of this function) in such a way as to ensure
       // compliant byte ordering for the written scanline samples.

  tiffdir.write_tag(KDU_TIFFTAG_ImageWidth32,(kdu_uint32) cols);
  tiffdir.write_tag(KDU_TIFFTAG_ImageHeight32,(kdu_uint32) rows);
  tiffdir.write_tag(KDU_TIFFTAG_SamplesPerPixel,(kdu_uint16) num_components);
  tiffdir.write_tag(KDU_TIFFTAG_PhotometricInterp,photometric_type);
  tiffdir.write_tag(KDU_TIFFTAG_PlanarConfig,KDU_TIFF_PlanarConfig_CONTIG);
  tiffdir.write_tag(KDU_TIFFTAG_Compression,KDU_TIFF_Compression_NONE);
  if (dims.allow_save_resolution())
    { 
      tiffdir.write_tag(KDU_TIFFTAG_ResolutionUnit,resolution_unit);
      tiffdir.write_tag(KDU_TIFFTAG_XResolution,xpels_per_cm);
      tiffdir.write_tag(KDU_TIFFTAG_YResolution,ypels_per_cm);
    }
  if (have_premultiplied_alpha)
    tiffdir.write_tag(KDU_TIFFTAG_ExtraSamples,(kdu_uint16) 1);
  for (n=0; n < num_components; n++)
    { 
      tiffdir.write_tag(KDU_TIFFTAG_BitsPerSample,(kdu_uint16) precision);
      kdu_uint16 sampleformat = KDU_TIFF_SampleFormat_UNSIGNED;
      if (forced_exponent != NULL)
        sampleformat = KDU_TIFF_SampleFormat_FLOAT;
      else if (is_signed[n])
        sampleformat = KDU_TIFF_SampleFormat_SIGNED;
      tiffdir.write_tag(KDU_TIFFTAG_SampleFormat,sampleformat);
      if (sampleformat == KDU_TIFF_SampleFormat_FLOAT)
        { // Write floating-point nominal range -- might not be valid, but this
          // is what JPX colour spaces expect when working with compressed
          // true floats.
          float minval=(is_signed[n])?-1.0f:0.0f;
          float maxval=1.0f;
          tiffdir.write_tag(KDU_TIFFTAG_SminSampleValueF,minval);
          tiffdir.write_tag(KDU_TIFFTAG_SmaxSampleValueF,maxval);
        }
    }
  
  if (icc_profile_len > 0)
    { // Write the ICC tag
      const kdu_byte *icc_profile = dims.get_profile();
      tiffdir.write_tag(KDU_TIFFTAG_Icc,icc_profile_len,icc_profile);
    }

  if (xmp_box.exists())
    { // XMP box support contributed by Greg Coats
      kdu_uint32 length_of_xmp_tag = (kdu_uint32)
        (xmp_box.get_box_bytes() - 24);
          // Header is always 24 bytes including length field
      if (!quiet)
        {
          std::cout << "Copying XMP  box info, size = " ;
          std::cout.width(7) ;
          std::cout << length_of_xmp_tag << std::endl;
        }
      kdu_byte *xmp_data_packet = new kdu_byte[length_of_xmp_tag];
      xmp_box.read(xmp_data_packet,length_of_xmp_tag);
      tiffdir.write_tag(/*(((kdu_uint32) 700)<<16)+0x0001 */
                        (kdu_uint32) 0x02bc0001,length_of_xmp_tag,
                        xmp_data_packet);
      delete[] xmp_data_packet;
    }

  if (iptc_box.exists())
    { // IPTC box support contributed by Greg Coats
      kdu_uint32 length_of_iptc_tag = (kdu_uint32)
        (iptc_box.get_box_bytes() - 24);
          // Header is always 24 bytes including length field
      if (!quiet) 
        {
          std::cout << "Copying IPTC box info, size = " ;
          std::cout.width(7) ;
          std::cout << length_of_iptc_tag << std::endl;
        }
      kdu_uint32 written_length = length_of_iptc_tag;
      written_length += (4-written_length) & 3;
      kdu_byte *iptc_data_packet = new kdu_byte[written_length];
      iptc_box.read(iptc_data_packet,length_of_iptc_tag);
      while (length_of_iptc_tag < written_length)
        iptc_data_packet[length_of_iptc_tag++] = 0;
        // Now we are ready to write the IPTC tag with tag-type 4 (Long).
        // Ideally, the tag should be written with tag-type 7 (undefined), or
        // a byte-oriented type.  However, historically, it has been written
        // as 32-bit integers.  As far as we can tell, it seems that a
        // convention has evolved for storing IPTC tags in TIFF files
        // using the correct IPTC byte order but just using the tag-type 4
        // anyway.
      if ((iptc_data_packet[0] != KDU_IPTC_TAG_MARKER) &&
          (iptc_data_packet[3] == KDU_IPTC_TAG_MARKER))
        { // Original JP2 data seems to have been in wrong order --
          // perhaps it was created using a much earlier version of
          // kdu_compress??
          for (kdu_uint32 k=0; k < length_of_iptc_tag; k+=4)
            { 
              kdu_byte tmp = iptc_data_packet[k];
              iptc_data_packet[k] = iptc_data_packet[k+3];
              iptc_data_packet[k+3] = tmp;
              tmp = iptc_data_packet[k+1];
              iptc_data_packet[k+1] = iptc_data_packet[k+2];
              iptc_data_packet[k+2] = tmp;
            }
        }
      tiffdir.write_tag((kdu_uint32) 0x83bb0004,length_of_iptc_tag,
                        iptc_data_packet);
      delete[] iptc_data_packet;
    }

  if (geo_box.exists())
    {
      if (!quiet)
        {
          kdu_uint32 length_of_geo_tag = (kdu_uint32)
            geo_box.get_remaining_bytes();
          std::cout << "Copying Geo  box info, size = " ;
          std::cout.width(7) ;
          std::cout << length_of_geo_tag << std::endl ;
        }
      kdu_tiffdir geotiff;
      if (geotiff.opendir(&geo_box))
        { // Copy GeoTIFF tags across; we will do this in a type-agnostic way
          kdu_uint32 wd=0, ht=0;
          if (!(geotiff.read_tag(KDU_TIFFTAG_ImageWidth16,1,&wd) ||
                geotiff.read_tag(KDU_TIFFTAG_ImageHeight16,1,&ht)))
            { 
              geotiff.read_tag(KDU_TIFFTAG_ImageWidth32,1,&wd);
              geotiff.read_tag(KDU_TIFFTAG_ImageHeight32,1,&ht);
            }
          
          kdu_uint32 tag_type;
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 33550)<<16)) != 0)
            { // The following adjustmens were contributed by Greg Coats
              // who has been using them for some time.  In his original
              // suggestion, the GeoTIFF resolution information was adjusted
              // only if the endianness of the JP2-embedded `geotiff' directory
              // is the same as that of the output file being written.
              // However, I cannot see why this should matter, since the
              // `kdu_tiff::read_tag' and `kdu_tiff::write_tag' functions
              // automatically correct their data to and from the machine's
              // native byte order.
              double pixel_scale[3], scale_x, scale_y;
              geotiff.read_tag(tag_type,3,pixel_scale);
              if (dims.get_resolution_scale_factors(first_comp_idx,
                                                    scale_x,scale_y))
                { // Scale the GeoJP2 resolution information
                  pixel_scale[0] /= scale_x;
                  pixel_scale[1] /= scale_y;
                }
              tiffdir.write_tag(tag_type,3,pixel_scale);
            }
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 33922)<<16)) != 0)
            tiffdir.copy_tag(geotiff,tag_type);
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 34264)<<16)) != 0)
            tiffdir.copy_tag(geotiff,tag_type);
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 34735)<<16)) != 0)
            tiffdir.copy_tag(geotiff,tag_type);
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 34736)<<16)) != 0)
            tiffdir.copy_tag(geotiff,tag_type);
          if ((tag_type=geotiff.open_tag(((kdu_uint32) 34737)<<16)) != 0)
            tiffdir.copy_tag(geotiff,tag_type);
        }
      geotiff.close();
      geo_box.close();
    }

  /*
  // Write the image strip properties -- we will write everything in one strip
  // right after the TIFF directory
  tiffdir.write_tag(KDU_TIFFTAG_RowsPerStrip32,(kdu_uint32) rows);
  if (use_bigtiff)
    tiffdir.write_tag(KDU_TIFFTAG_StripByteCounts64,out_byte_count);
  else
    tiffdir.write_tag(KDU_TIFFTAG_StripByteCounts32,
                      (kdu_uint32)out_byte_count);
  kdu_uint32 header_length = (use_bigtiff)?16:8; // This will change
  kdu_long image_pos = tiffdir.get_dirlength() + header_length;
  tiffdir.write_tag(KDU_TIFFTAG_StripOffsets32,image_pos);
  image_pos = tiffdir.get_dirlength()+header_length; // Length has changed
  tiffdir.create_tag(KDU_TIFFTAG_StripOffsets32); // Reset this tag
  tiffdir.write_tag(KDU_TIFFTAG_StripOffsets32,image_pos); // Write it again
   */
  
  // Write the image strip properties -- we may have to write multiple
  // strips to be sure that no strip exceeds the maximum size of 2GB (I'm
  // still not sure if/why this limit applies to BigTIFF, but no matter).
  int rows_per_strip = (1<<24)/scanline_width; // 16MB strips seems reasonable
  if (rows_per_strip < 1)
    rows_per_strip = 1;
  if (rows_per_strip > rows)
    rows_per_strip = rows;
  int strip_idx, num_strips = 1 + ((rows-1)/rows_per_strip);
  kdu_long strip_bytes = ((kdu_long) rows_per_strip) * scanline_width;
  kdu_long last_strip_bytes =
    (kdu_long)(rows-(num_strips-1)*rows_per_strip) * scanline_width;
  tiffdir.write_tag(KDU_TIFFTAG_RowsPerStrip32,(kdu_uint32) rows_per_strip);
  kdu_uint32 header_length = (use_bigtiff)?16:8;
  if (use_bigtiff)
    { // Generate and write the strip properties for 64-bit TIFF files
      kdu_long *offsets = new kdu_long[2*num_strips];
      kdu_long *byte_counts = offsets + num_strips;
      memset(offsets,0,sizeof(kdu_long)*(size_t)num_strips);
      for (strip_idx=0; strip_idx < num_strips; strip_idx++)
        byte_counts[strip_idx] = strip_bytes;
      byte_counts[num_strips-1] = last_strip_bytes;
      tiffdir.write_tag(KDU_TIFFTAG_StripOffsets64,num_strips,offsets);
      tiffdir.write_tag(KDU_TIFFTAG_StripByteCounts64,num_strips,byte_counts);
      kdu_long image_pos = tiffdir.get_dirlength() + header_length;
      tiffdir.create_tag(KDU_TIFFTAG_StripOffsets64); // Reset this tag
      for (strip_idx=0; strip_idx < num_strips; strip_idx++)
        offsets[strip_idx] = image_pos + strip_bytes * strip_idx;
      tiffdir.write_tag(KDU_TIFFTAG_StripOffsets64,num_strips,offsets);
      delete[] offsets;
      assert(image_pos == (tiffdir.get_dirlength()+header_length));
    }
  else
    { // Generate and write the strip properties for 32-bit TIFF files
      kdu_uint32 *offsets = new kdu_uint32[2*num_strips];
      kdu_uint32 *byte_counts = offsets + num_strips;
      memset(offsets,0,sizeof(kdu_uint32)*(size_t)num_strips);
      for (strip_idx=0; strip_idx < num_strips; strip_idx++)
        byte_counts[strip_idx] = (kdu_uint32) strip_bytes;
      byte_counts[num_strips-1] = (kdu_uint32) last_strip_bytes;
      tiffdir.write_tag(KDU_TIFFTAG_StripOffsets32,num_strips,offsets);
      tiffdir.write_tag(KDU_TIFFTAG_StripByteCounts32,num_strips,byte_counts);
      kdu_long image_pos = tiffdir.get_dirlength() + header_length;
      tiffdir.create_tag(KDU_TIFFTAG_StripOffsets32); // Reset this tag
      for (strip_idx=0; strip_idx < num_strips; strip_idx++)
        offsets[strip_idx] = (kdu_uint32)(image_pos + strip_bytes * strip_idx);
      tiffdir.write_tag(KDU_TIFFTAG_StripOffsets32,num_strips,offsets);
      delete[] offsets;
      assert(image_pos == (tiffdir.get_dirlength()+header_length));
    }

  // Open TIFF file and write everything except the scan lines
  if (!out.open(fname,false,true))
    { kdu_error e;
      e << "Unable to open output image file, \"" << fname << "\"."; }
  tiffdir.write_header(&out,header_length);
  if (!tiffdir.writedir(&out,header_length))
    { kdu_error e; e << "Attempt to write TIFF directory failed.  Output "
      "device might be full."; }
  if ((precision == 16) || (precision == 32))
    pre_pack_littlendian = tiffdir.is_littlendian();
  else
    pre_pack_littlendian = tiffdir.is_native_littlendian();
  num_unwritten_rows = rows;
}

/*****************************************************************************/
/*                             tif_out::~tif_out                             */
/*****************************************************************************/

tif_out::~tif_out()
{
  if ((num_unwritten_rows > 0) || (incomplete_lines != NULL))
    { kdu_warning w;
      w << "Not all rows of image components "
        << first_comp_idx << " through "
        << first_comp_idx+num_components-1
        << " were completed!";
    }
  image_line_buf *tmp;
  while ((tmp=incomplete_lines) != NULL)
    { incomplete_lines = tmp->next; delete tmp; }
  while ((tmp=free_lines) != NULL)
    { free_lines = tmp->next; delete tmp; }
  if (orig_precision != NULL)
    delete[] orig_precision;
  if (is_signed != NULL)
    delete[] is_signed;
  if (forced_exponent != NULL)
    delete[] forced_exponent;
  out.close();
}

/*****************************************************************************/
/*                                tif_out::put                               */
/*****************************************************************************/

void
  tif_out::put(int comp_idx, kdu_line_buf &line, int x_tnum)
{
  int width = line.get_width();
  int idx = comp_idx - this->first_comp_idx;
  assert((idx >= 0) && (idx < num_components));
  x_tnum = x_tnum*num_components+idx;
  if ((initial_non_empty_tiles != 0) && (x_tnum >= initial_non_empty_tiles))
    {
      assert(width == 0);
      return;
    }

  image_line_buf *scan, *prev=NULL;
  for (scan=incomplete_lines; scan != NULL; prev=scan, scan=scan->next)
    {
      assert(scan->next_x_tnum >= x_tnum);
      if (scan->next_x_tnum == x_tnum)
        break;
    }
  if (scan == NULL)
    { // Need to open a new line buffer
      assert(x_tnum == 0); // Must generate in very specific order.
      if ((scan = free_lines) == NULL)
        scan = new image_line_buf(cols+4,pixel_bytes);
                  // Big enough for padding and expanding bits to bytes
      free_lines = scan->next;
      if (prev == NULL)
        incomplete_lines = scan;
      else
        prev->next = scan;
      scan->accessed_samples = 0;
      scan->next_x_tnum = 0;
    }

  assert((cols-scan->accessed_samples) >= width);

  // Extract data from `line' buffer, performing conversions as required
  kdu_byte *dst = scan->buf +
    pixel_bytes*scan->accessed_samples + sample_bytes*idx;
  if (line.get_buf32() != NULL)
    { 
      if (forced_exponent != NULL)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_ints_as_floats_to_floats(line.get_buf32(),dst,width,
                                           forced_exponent[idx],
                                           orig_precision[idx],
                                           is_signed[idx],
                                           pre_pack_littlendian,pixel_bytes);
        }
      else if (line.is_absolute())
        convert_ints_to_words(line.get_buf32(),dst,width,precision,
                              forced_align_lsbs,orig_precision[idx],
                              is_signed[idx],sample_bytes,
                              pre_pack_littlendian,pixel_bytes);
      else
        convert_floats_to_words(line.get_buf32(),dst,width,precision,
                                forced_align_lsbs,orig_precision[idx],
                                is_signed[idx],sample_bytes,
                                pre_pack_littlendian,pixel_bytes);
    }
  else
    { 
      if (forced_exponent != NULL)
        { 
          check_absolute_ints_for_float_forcing(line,forced_exponent[idx],
                                                orig_precision[idx]);
          convert_shorts_as_floats_to_floats(line.get_buf16(),dst,width,
                                             forced_exponent[idx],
                                             orig_precision[idx],
                                             is_signed[idx],
                                             pre_pack_littlendian,pixel_bytes);
        }
      else if (line.is_absolute())
        convert_shorts_to_words(line.get_buf16(),dst,width,precision,
                                forced_align_lsbs,orig_precision[idx],
                                is_signed[idx],sample_bytes,
                                pre_pack_littlendian,pixel_bytes);
      else
        convert_fixpoint_to_words(line.get_buf16(),dst,width,precision,
                                  forced_align_lsbs,orig_precision[idx],
                                  is_signed[idx],sample_bytes,
                                  pre_pack_littlendian,pixel_bytes);
    }

  // Finished writing to line-tile; now see if we can write a TIFF scan-line
  scan->next_x_tnum++;
  if (idx == (num_components-1))
    scan->accessed_samples += line.get_width();
  if (scan->accessed_samples == cols)
    { // Write completed line and send it to the free list.
      if (initial_non_empty_tiles == 0)
        initial_non_empty_tiles = scan->next_x_tnum;
      else
        assert(initial_non_empty_tiles == scan->next_x_tnum);
      if (num_unwritten_rows == 0)
        { kdu_error e; e << "Attempting to write too many lines to image "
          "file for components " << first_comp_idx << " through "
          << first_comp_idx+num_components-1 << "."; }
      
      if ((precision != 8) && (precision != 16) && (precision != 32))
        perform_buffer_pack(scan->buf);
      out.write(scan->buf,scanline_width);

      num_unwritten_rows--;
      assert(scan == incomplete_lines);
      incomplete_lines = scan->next;
      scan->next = free_lines;
      free_lines = scan;
    }
}

/*****************************************************************************/
/*                        tif_out::perform_buffer_pack                       */
/*****************************************************************************/

void
  tif_out::perform_buffer_pack(kdu_byte *dst)
{
  if (sample_bytes == 1)
    {
      kdu_byte *src = dst;
      assert(precision < 8);
      kdu_byte out_val=0; // Used to accumulate packed output bytes
      kdu_byte in_val;
      int n, shift, bits_needed=8;
      for (n=row_bytes; n > 0; n--, src++)
        {
          in_val = *src;
          if (bits_needed > precision)
            {
              out_val = (out_val<<precision) | in_val;
              bits_needed -= precision;
              continue; // `bits_needed' is still > 0
            }
          in_val = *src; // Need to borrow `bits_needed' bits from `in_val'
          shift = precision-bits_needed;
          *(dst++) = (out_val<<bits_needed) | (in_val>>shift);
          out_val = in_val;
          bits_needed += 8 - precision;
        }
      if (bits_needed < 8)
        *(dst++) = (out_val << bits_needed); // Last byte is padded with 0's
    }
  else if (sample_bytes == 2)
    {
      assert((precision > 8) && (precision < 16));
      kdu_uint16 *src = (kdu_uint16 *) dst;
      kdu_uint16 val=0, next_val;
      int n, shift=-8;
      for (n=scanline_width; n > 0; n--, dst++, shift-=8)
        {
          if (shift < 0)
            {
              val <<= -shift;  next_val = *(src++);  shift += precision;
              *dst = (kdu_byte)(val | (next_val>>shift));
              val = next_val;
            }
          else
            *dst = (kdu_byte)(val>>shift);
        }
    }
  else if (sample_bytes == 4)
    {
      assert((precision > 16) && (precision < 32));
      kdu_uint32 *src = (kdu_uint32 *) dst;
      kdu_uint32 val=0, next_val;
      int n, shift=-8;
      for (n=scanline_width; n > 0; n--, dst++, shift-=8)
        {
          if (shift < 0)
            {
              val <<= -shift;  next_val = *(src++);  shift += precision;
              *dst = (kdu_byte)(val | (next_val>>shift));
              val = next_val;
            }
          else
            *dst = (kdu_byte)(val>>shift);
        }
    }
  else
    assert(0);
}
