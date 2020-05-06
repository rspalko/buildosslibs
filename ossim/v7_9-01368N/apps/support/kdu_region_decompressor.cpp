/*****************************************************************************/
// File: kdu_region_decompressor.cpp [scope = APPS/SUPPORT]
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
   Implements the incremental, region-based decompression services of the
"kdu_region_decompressor" object.  These services should prove useful
to many interactive applications which require JPEG2000 rendering capabilities.
******************************************************************************/

#include <assert.h>
#include <string.h>
#include <math.h> // Just used for pre-computing interpolation kernels
#include "kdu_arch.h"
#include "kdu_utils.h"
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "region_decompressor_local.h"
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
     kdu_error _name("E(kdu_region_decompressor.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_region_decompressor.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu Region Decompressor:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu Region Decompressor:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


/* ========================================================================= */
/*                kdrc_convert_and_copy_func implementations                 */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC              local_convert_and_copy_to_fix16                       */
/*****************************************************************************/

static void
  local_convert_and_copy_to_fix16(const void *bufs[], const int widths[],
                                  const int types[], int num_lines,
                                  int src_precision, int missing_src_samples,
                                  void *void_dst, int dst_min, int num_samples,
                                  int dst_type, int float_exp_bits)
{
  assert((dst_type == KDRD_FIX16_TYPE) && (float_exp_bits == 0));
  kdu_int16 *dst = ((kdu_int16 *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Now perform the sample conversion process
  kdu_int16 val=0;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      int upshift = KDU_FIX_POINT -
        ((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT);
      int downshift = (upshift >= 0)?0:-upshift;
      kdu_int32 offset = (1<<downshift)>>1;
      if (src_type & KDRD_SHORT_TYPE)
        { 
          val = ((const kdu_int16 *)src)[0];
          val = (upshift>0)?(val<<upshift):((val+offset)>>downshift);
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { 
          float fval = ((const float *)src)[0] * (1<<KDU_FIX_POINT);
          val=(fval>=0.0F)?
            ((kdu_int16)(fval+0.5F)):(-(kdu_int16)(-fval+0.5F));
        }
      else
        { 
          kdu_int32 val32 = ((const kdu_int32 *)src)[0];
          val = (kdu_int16)
            (upshift > 0)?(val32<<upshift):((val32+offset)>>downshift);
        }
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = val;
      num_samples -= missing_src_samples;
    }
  while (num_samples > 0)
    { 
      num_samples += n; // Don't worry; we'll effectively take n away again
      dst -= n; // Allows us to address source and dest samples with n
      kdu_int16 *dp = dst;
      if (src_len > num_samples)
        src_len = num_samples;
      dst += src_len;
      num_samples -= src_len;
    
      int upshift = KDU_FIX_POINT -
        ((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT);
      int downshift = (upshift >= 0)?0:-upshift;
      kdu_int32 offset = (1<<downshift)>>1;
      if (src_type & KDRD_SHORT_TYPE)
        { // 16-bit source samples
          const kdu_int16 *sp = (const kdu_int16 *)src;
          if (upshift == 0)
            for (; n < src_len; n++)
              dp[n] = sp[n];
          else if (upshift > 0)
            for (; n < src_len; n++)
              dp[n] = sp[n] << upshift;
          else
            for (; n < src_len; n++)
              dp[n] = (sp[n]+offset) >> downshift;
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { // floating point source samples
          const float *sp = (const float *)src;
          for (; n < src_len; n++)
            { 
              float fval = sp[n] * (1<<KDU_FIX_POINT);
              dp[n] = (fval>=0.0F)?
                ((kdu_int16)(fval+0.5F)):(-(kdu_int16)(-fval+0.5F));
            }
        }
      else
        { // absolute 32-bit source samples
          const kdu_int32 *sp = (const kdu_int32 *)src;
          if (upshift >= 0)
            for (; n < src_len; n++)
              dp[n] = (kdu_int16)(sp[n] << upshift);
          else
            for (; n < src_len; n++)
              dp[n] = (kdu_int16)((sp[n]+offset) >> downshift);
        }
    
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (val=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = val;
}

/*****************************************************************************/
/* STATIC              local_convert_and_copy_to_int32                       */
/*****************************************************************************/

static void
  local_convert_and_copy_to_int32(const void *bufs[], const int widths[],
                                  const int types[], int num_lines,
                                  int src_precision, int missing_src_samples,
                                  void *void_dst, int dst_min, int num_samples,
                                  int dst_type, int float_exp_bits)
  /* Note that the `src_precision' value is guaranteed also to be the
     precision of the written 32-bit output samples here. */
{
  assert((dst_type == KDRD_INT32_TYPE) && (float_exp_bits == 0));
  kdu_int32 *dst = ((kdu_int32 *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Now perform the sample conversion process
  kdu_int32 val=0;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type & KDRD_SHORT_TYPE)
        { 
          int upshift =
            (src_type & KDRD_ABSOLUTE_TYPE)?0:(src_precision-KDU_FIX_POINT);
          int downshift = (upshift >= 0)?0:-upshift;
          kdu_int32 offset = (1<<downshift)>>1;
          val = ((const kdu_int16 *)src)[0];
          val = (upshift>0)?(val<<upshift):((val+offset)>>downshift);
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { 
          float fval = ((const float *)src)[0] * (1<<src_precision);
          val=(fval>=0.0F)?
            ((kdu_int32)(fval+0.5F)):(-(kdu_int32)(-fval+0.5F));
        }
      else
        val = ((const kdu_int32 *)src)[0];
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = val;
      num_samples -= missing_src_samples;
    }

  while (num_samples > 0)
    { 
      num_samples += n; // Don't worry; we'll effectively take n away again
      dst -= n; // Allows us to address source and dest samples with n
      kdu_int32 *dp = dst;
      if (src_len > num_samples)
        src_len = num_samples;
      dst += src_len;
      num_samples -= src_len;
      
      if (src_type & KDRD_SHORT_TYPE)
        { // 16-bit source samples
          const kdu_int16 *sp = (const kdu_int16 *)src;
          int upshift =
            (src_type & KDRD_ABSOLUTE_TYPE)?0:(src_precision-KDU_FIX_POINT);
          int downshift = (upshift >= 0)?0:-upshift;
          kdu_int32 offset = (1<<downshift)>>1;
          if (upshift == 0)
            for (; n < src_len; n++)
              dp[n] = sp[n];
          else if (upshift > 0)
            for (; n < src_len; n++)
              dp[n] = ((kdu_int32)sp[n]) << upshift;
          else
            for (; n < src_len; n++)
              dp[n] = (sp[n]+offset) >> downshift;
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { // floating point source samples
          const float *sp = (const float *)src;
          float scale = (float)(1<<src_precision);
          for (; n < src_len; n++)
            { 
              float fval = sp[n] * scale;
              dp[n] = (fval>=0.0F)?
                ((kdu_int32)(fval+0.5F)):(-(kdu_int32)(-fval+0.5F));
            }
        }
      else
        { // absolute 32-bit source samples
          const kdu_int32 *sp = (const kdu_int32 *)src;
          for (; n < src_len; n++)
            dp[n] = sp[n];
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (val=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = val;
}

/*****************************************************************************/
/* STATIC              local_convert_and_copy_to_float                       */
/*****************************************************************************/

static void
  local_convert_and_copy_to_float(const void *bufs[], const int widths[],
                                  const int types[], int num_lines,
                                  int src_precision, int missing_src_samples,
                                  void *void_dst, int dst_min, int num_samples,
                                  int dst_type, int float_exp_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (float_exp_bits == 0));
  float *dst = ((float *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Now perform the sample conversion process
  float val=0.0f;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      float scale = 1.0F;
      if (src_type != KDRD_FLOAT_TYPE)
        scale = 1.0F /
          (1<<((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT));
      if (src_type & KDRD_SHORT_TYPE)
        val = ((const kdu_int16 *)src)[0] * scale;
      else if (src_type == KDRD_FLOAT_TYPE)
        val = ((const float *)src)[0];
      else
        val = ((float)((const kdu_int32 *)src)[0]) * scale;
      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = val;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      num_samples += n; // Don't worry; we'll effectively take n away again
      dst -= n; // Allows us to address source and dest samples with n
      float *dp = dst;
      if (src_len > num_samples)
        src_len = num_samples;
      dst += src_len;
      num_samples -= src_len;
      
      if (src_type & KDRD_SHORT_TYPE)
        { // 16-bit source samples
          float scale = 1.0F /
            ((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT);
          const kdu_int16 *sp = (const kdu_int16 *)src;
          for (; n < src_len; n++)
            dp[n] = ((float)sp[n])*scale;
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { // floating point source samples
          const float *sp = (const float *)src;
          for (; n < src_len; n++)
            dp[n] = sp[n];
        }
      else
        { // absolute 32-bit source samples
          float scale = 1.0F / (1<<src_precision);
          const kdu_int32 *sp = (const kdu_int32 *)src;
          for (; n < src_len; n++)
            dp[n] = ((float)sp[n])*scale;
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (val=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = val;
}

/*****************************************************************************/
/* STATIC         local_reinterpret_and_copy_unsigned_floats                 */
/*****************************************************************************/

static void
  local_reinterpret_and_copy_unsigned_floats(const void *bufs[],
                                             const int widths[],
                                             const int types[], int num_lines,
                                             int precision,
                                             int missing_src_samples,
                                             void *void_dst, int dst_min,
                                             int num_samples, int dst_type,
                                             int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0));
  float *dst = ((float *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Prepare the conversion parameters
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  if (precision > 32)
    precision = 32;
  else if (precision < 2)
    precision = 2;
  if (exponent_bits > (precision-1))
    exponent_bits = precision-1;
  int mantissa_bits = precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly
      // bring denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // This is the smaller exponent bound here
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
  kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
  kdu_int32 in_off = 1<<(precision-1);
  kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
  kdu_int32 in_max = mag_max - in_off; // integer level offset in place
  float out_scale = denorm_scale; // Compensate for `exp_off' < 127

  // Now perform the sample conversion process
  float fval=0.0f;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      kdu_int32 int_val = ((const kdu_int32 *)src)[0];
      if (int_val < in_min)
        int_val = in_min; // Avoid exponent underflow and negative values
      else if (int_val > in_max)
        int_val = in_max; // Avoid exponent overflow
      int_val += in_off; // Remove the integer level offset
      int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
      if (mantissa_upshift >= 0)
        int_val <<= mantissa_upshift;
      else
        int_val >>= mantissa_downshift;
      cast.int_val = int_val;
      fval = cast.float_val;
      fval = fval * out_scale - 0.5f; // Final level adjustment

      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      num_samples += n; // Don't worry; we'll effectively take n away again
      dst -= n; // Allows us to address source and dest samples with n
      float *dp = dst;
      if (src_len > num_samples)
        src_len = num_samples;
      dst += src_len;
      num_samples -= src_len;
      
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      const kdu_int32 *sp = (const kdu_int32 *)src;
      for (; n < src_len; n++)
        { 
          kdu_int32 int_val = sp[n];
          if (int_val < in_min)
            int_val = in_min; // Avoid exponent underflow and negative values
          else if (int_val > in_max)
            int_val = in_max; // Avoid exponent overflow
          int_val += in_off; // Remove the integer level offset
          int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
          if (mantissa_upshift >= 0)
            int_val <<= mantissa_upshift;
          else
            int_val >>= mantissa_downshift;
          cast.int_val = int_val;
          fval = cast.float_val;
          dp[n] = fval * out_scale - 0.5f; // Final level adjustment
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}

/*****************************************************************************/
/* STATIC         local_reinterpret_and_copy_signed_floats                 */
/*****************************************************************************/

static void
  local_reinterpret_and_copy_signed_floats(const void *bufs[],
                                           const int widths[],
                                           const int types[], int num_lines,
                                           int precision,
                                           int missing_src_samples,
                                           void *void_dst, int dst_min,
                                           int num_samples, int dst_type,
                                           int exponent_bits)
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0));
  float *dst = ((float *) void_dst) + dst_min;
  
  if ((num_lines < 1) || (num_samples < 1))
    { // Pathalogical case
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  if (missing_src_samples >= num_samples)
    missing_src_samples = num_samples-1;
  
  // Prepare the conversion parameters
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  if (precision > 32)
    precision = 32;
  else if (precision < 2)
    precision = 2;
  if (exponent_bits > (precision-1))
    exponent_bits = precision-1;
  int mantissa_bits = precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly
      // bring denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // This is the smaller exponent bound here
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
  kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
  kdu_int32 mag_mask = ~(((kdu_int32)-1) << (precision-1));
  float out_scale = denorm_scale; // Compensates for `exp_off' < 127
  out_scale *= 0.5f; // KDU scales signed values by 0.5

  // Now perform the sample conversion process
  float fval=0.0f;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      kdu_int32 int_val = ((const kdu_int32 *)src)[0];
      kdu_int32 sign_bit = int_val & KDU_INT32_MIN;
      int_val &= mag_mask;      
      if (int_val < pre_adjust)
        int_val = pre_adjust; // Avoid exponent underflow
      else if (int_val > mag_max)
        int_val = mag_max; // Avoid exponent overflow
      int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
      if (mantissa_upshift >= 0)
        int_val <<= mantissa_upshift;
      else
        int_val >>= mantissa_downshift;
      int_val |= sign_bit; // Now we have true IEEE single precision floats
      cast.int_val = int_val;
      fval = cast.float_val;
      fval = fval * out_scale;

      for (int m=missing_src_samples; m > 0; m--)
        *(dst++) = fval;
      num_samples -= missing_src_samples;
    }
  
  while (num_samples > 0)
    { 
      num_samples += n; // Don't worry; we'll effectively take n away again
      dst -= n; // Allows us to address source and dest samples with n
      float *dp = dst;
      if (src_len > num_samples)
        src_len = num_samples;
      dst += src_len;
      num_samples -= src_len;
      
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      const kdu_int32 *sp = (const kdu_int32 *)src;
      for (; n < src_len; n++)
        { 
          kdu_int32 int_val = sp[n];
          kdu_int32 sign_bit = int_val & KDU_INT32_MIN;
          int_val &= mag_mask;      
          if (int_val < pre_adjust)
            int_val = pre_adjust; // Avoid exponent underflow
          else if (int_val > mag_max)
            int_val = mag_max; // Avoid exponent overflow
          int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
          if (mantissa_upshift >= 0)
            int_val <<= mantissa_upshift;
          else
            int_val >>= mantissa_downshift;
          int_val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = int_val;
          fval = cast.float_val;
          dp[n] = fval * out_scale;
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}


/* ========================================================================= */
/*                 kdrc_convert_and_add_func implementations                 */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     local_convert_and_add                          */
/*****************************************************************************/

static void
  local_convert_and_add(const void *bufs[], const int widths[],
                        const int types[], int num_lines, int src_precision,
                        int missing_src_samples, void *void_dst,
                        int dst_min, int num_cells, int dst_type,
                        int cell_width, int acc_precision,
                        int cell_lines_left, int cell_height,
                        int float_exp_bits)
  /* Similar to `convert_and_copy_to_floats', apart from the cell aggregation
     process and conversion back down to a 16-bit fixed-point result at
     the end. */
{
  assert((dst_type != KDRD_FLOAT_TYPE) && (float_exp_bits == 0));
  kdu_int32 *dst = ((kdu_int32 *) void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  if ((num_lines < 1) || (num_cells < 1))
    return;
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  
  int needed_samples = num_cells*cell_width;
  if (missing_src_samples >= needed_samples)
    missing_src_samples = needed_samples-1;
  
  // Now perform the sample conversion process
  kdu_int32 val=0;
  int ccounter = cell_width;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      int upshift = acc_precision -
        ((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT);
      int downshift = (upshift >= 0)?0:-upshift;
      kdu_int32 offset = (1<<downshift)>>1;
      if (src_type & KDRD_SHORT_TYPE)
        { 
          val = ((const kdu_int16 *)src)[0];
          val = (upshift>0)?(val<<upshift):((val+offset)>>downshift);
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { 
          float fval = ((const float *)src)[0] * (1<<acc_precision);
          val=(fval>=0.0F)?
            ((kdu_int32)(fval+0.5F)):(-(kdu_int32)(-fval+0.5F));
        }
      else
        { 
          val = ((const kdu_int32 *)src)[0];
          val = (upshift>0)?(val<<upshift):((val+offset)>>downshift);
        }
      for (int m=missing_src_samples; m > 0; m--, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          *dst += val;
        }
      needed_samples -= missing_src_samples;
    }
  
  while (needed_samples > 0)
    { 
      needed_samples += n; // Don't worry; we'll effectively take n away again
      if (src_len > needed_samples)
        src_len = needed_samples;
      needed_samples -= src_len;
      int upshift = acc_precision -
        ((src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT);
      int downshift = (upshift >= 0)?0:-upshift;
      kdu_int32 offset = (1<<downshift)>>1;      
      if (src_type & KDRD_SHORT_TYPE)
        { // 16-bit source samples
          const kdu_int16 *sp = (const kdu_int16 *)src;
          if (upshift >= 0)
            for (; n < src_len; n++, ccounter--)
              { 
                if (ccounter == 0)
                  { dst++; ccounter=cell_width; }
                *dst += (val = ((kdu_int32) sp[n]) << upshift);
              }
          else
            for (; n < src_len; n++, ccounter--)
              { 
                if (ccounter == 0)
                  { dst++; ccounter=cell_width; }
                *dst += (val = (sp[n]+offset) >> downshift);
              }
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { // floating point source samples
          float scale = (float)(1<<acc_precision);
          const float *sp = (const float *)src;
          for (; n < src_len; n++, ccounter--)
            { 
              if (ccounter == 0)
                { dst++; ccounter=cell_width; }
              float fval = sp[n] * scale;
              val = (fval>=0.0F)?
                ((kdu_int32)(fval+0.5F)):(-(kdu_int32)(-fval+0.5F));
              *dst += val;
            }
        }
      else
        { // absolute 32-bit source samples
          const kdu_int32 *sp = (const kdu_int32 *)src;
          if (upshift >= 0)
            for (; n < src_len; n++, ccounter--)
              { 
                if (ccounter == 0)
                  { dst++; ccounter=cell_width; }
                *dst += (val = sp[n] << upshift);
              }
          else
            for (; n < src_len; n++, ccounter--)
              { 
                if (ccounter == 0)
                  { dst++; ccounter=cell_width; }
                *dst += (val = (sp[n]+offset) >> downshift);
              }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (; needed_samples > 0; needed_samples--, ccounter--)
    { 
      if (ccounter == 0)
        { dst++; ccounter=cell_width; }
      *dst += val;
    }
  
  // See if we need to finish by generating a normalized 16-bit result
  if (cell_lines_left == 1)
    { 
      kdu_int32 *sp = ((kdu_int32 *) void_dst) + dst_min;
      kdu_int16 *dp = ((kdu_int16 *) void_dst) + dst_min;
      int in_precision=acc_precision, cell_area=cell_width*cell_height;
      for (; cell_area > 1; cell_area>>=1) in_precision++;
      int shift = in_precision - KDU_FIX_POINT;
      assert(shift > 0);
      kdu_int32 offset = (1<<shift)>>1;
      for (; num_cells > 0; num_cells--)
        *(dp++) = (kdu_int16)((*(sp++) + offset) >> shift);
    }
}

/*****************************************************************************/
/* STATIC                  local_convert_and_add_float                       */
/*****************************************************************************/

static void
  local_convert_and_add_float(const void *bufs[], const int widths[],
                              const int types[], int num_lines,
                              int src_precision, int missing_src_samples,
                              void *void_dst, int dst_min, int num_cells,
                              int dst_type, int cell_width, int acc_precision,
                              int cell_lines_left, int cell_height,
                              int float_exp_bits)
  /* Similar to `convert_and_copy_to_floats', apart from the cell aggregation
     process. */
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (float_exp_bits == 0));
         
  float *dst = ((float *) void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  if ((num_lines < 1) || (num_cells < 1))
    return;
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  
  int needed_samples = num_cells*cell_width;
  if (missing_src_samples >= needed_samples)
    missing_src_samples = needed_samples-1;
  
  // Now perform the sample conversion process
  assert(acc_precision < 0);
  float val=0.0f;
  int ccounter = cell_width;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      int scale_bits = -acc_precision;
      if (src_type & KDRD_SHORT_TYPE)
        { 
          scale_bits +=
            (src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT;
          val = ((const kdu_int16 *)src)[0] * (1.0f / (1<<scale_bits));
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        val = ((const float *)src)[0] * (1.0f / (1<<scale_bits));
      else
        { 
          scale_bits += src_precision; 
          val = ((const kdu_int32 *)src)[0] * (1.0f / (1<<scale_bits));
        }
      for (int m=missing_src_samples; m > 0; m--, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          *dst += val;
        }
      needed_samples -= missing_src_samples;
    }
  
  while (needed_samples > 0)
    { 
      needed_samples += n; // Don't worry; we'll effectively take n away again
      if (src_len > needed_samples)
        src_len = needed_samples;
      needed_samples -= src_len;
      int scale_bits = -acc_precision;
      if (src_type & KDRD_SHORT_TYPE)
        { // 16-bit source samples
          const kdu_int16 *sp = (const kdu_int16 *)src;
          scale_bits +=
            (src_type & KDRD_ABSOLUTE_TYPE)?src_precision:KDU_FIX_POINT;
          assert(scale_bits >= 0);
          float scale = 1.0f / (1<<scale_bits);
          for (; n < src_len; n++, ccounter--)
            { 
              if (ccounter == 0)
                { dst++; ccounter=cell_width; }
              val = sp[n] * scale;
              *dst += val;
            }
        }
      else if (src_type == KDRD_FLOAT_TYPE)
        { // floating point source samples
          float scale = 1.0f / (1<<scale_bits);
          const float *sp = (const float *)src;
          for (; n < src_len; n++, ccounter--)
            { 
              if (ccounter == 0)
                { dst++; ccounter=cell_width; }
              val = sp[n] * scale;
              *dst += val;
            }
        }
      else
        { // absolute 32-bit source samples
          const kdu_int32 *sp = (const kdu_int32 *)src;
          scale_bits += src_precision;
          float scale = 1.0f / (1<<scale_bits);
          for (; n < src_len; n++, ccounter--)
            { 
              if (ccounter == 0)
                { dst++; ccounter=cell_width; }
              val = ((float)sp[n]) * scale;
              *dst += val;
            }
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (; needed_samples > 0; needed_samples--, ccounter--)
    { 
      if (ccounter == 0)
        { dst++; ccounter=cell_width; }
      *dst += val;
    }
}

/*****************************************************************************/
/* STATIC         local_reinterpret_and_add_unsigned_floats                  */
/*****************************************************************************/

static void
  local_reinterpret_and_add_unsigned_floats(const void *bufs[],
                                            const int widths[],
                                            const int types[], int num_lines,
                                            int precision,
                                            int missing_src_samples,
                                            void *void_dst, int dst_min,
                                            int num_cells, int dst_type,
                                            int cell_width, int acc_precision,
                                            int cell_lines_left,
                                            int cell_height, int exponent_bits)
  /* Similar to `local_reinterpret_and_copy_unsigned_floats', apart from the
     cell aggregation process. */
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0));
         
  float *dst = ((float *) void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  if ((num_lines < 1) || (num_cells < 1))
    return;
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  
  int needed_samples = num_cells*cell_width;
  if (missing_src_samples >= needed_samples)
    missing_src_samples = needed_samples-1;
  
  // Prepare the conversion parameters
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  if (precision > 32)
    precision = 32;
  else if (precision < 2)
    precision = 2;
  if (exponent_bits > (precision-1))
    exponent_bits = precision-1;
  int mantissa_bits = precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly
      // bring denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // This is the smaller exponent bound here
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
  kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
  kdu_int32 in_off = 1<<(precision-1);
  kdu_int32 in_min = pre_adjust - in_off; // Test bounds violation with
  kdu_int32 in_max = mag_max - in_off; // integer level offset in place
  float out_scale = denorm_scale; // Compensate for `exp_off' < 127
  float out_off = -0.5f; // Puts back the level offset
  assert(acc_precision < 0);
  out_scale *= kdu_pwrof2f(acc_precision);
  out_off *= kdu_pwrof2f(acc_precision);
  
  // Now perform the sample conversion process
  float fval=0.0f;
  int ccounter = cell_width;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      kdu_int32 int_val = ((const kdu_int32 *)src)[0];
      if (int_val < in_min)
        int_val = in_min; // Avoid exponent underflow and negative values
      else if (int_val > in_max)
        int_val = in_max; // Avoid exponent overflow
      int_val += in_off; // Remove the integer level offset
      int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
      if (mantissa_upshift >= 0)
        int_val <<= mantissa_upshift;
      else
        int_val >>= mantissa_downshift;
      cast.int_val = int_val;
      fval = cast.float_val;
      fval = fval * out_scale + out_off;

      for (int m=missing_src_samples; m > 0; m--, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          *dst += fval;
        }
      needed_samples -= missing_src_samples;
    }
  
  while (needed_samples > 0)
    { 
      needed_samples += n; // Don't worry; we'll effectively take n away again
      if (src_len > needed_samples)
        src_len = needed_samples;
      needed_samples -= src_len;
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      const kdu_int32 *sp = (const kdu_int32 *)src;
      for (; n < src_len; n++, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          kdu_int32 int_val = sp[n];
          if (int_val < in_min)
            int_val = in_min; // Avoid exponent underflow and negative values
          else if (int_val > in_max)
            int_val = in_max; // Avoid exponent overflow
          int_val += in_off; // Remove the integer level offset
          int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
          if (mantissa_upshift >= 0)
            int_val <<= mantissa_upshift;
          else
            int_val >>= mantissa_downshift;
          cast.int_val = int_val;
          fval = cast.float_val;
          fval = fval * out_scale + out_off;
          *dst += fval;
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (; needed_samples > 0; needed_samples--, ccounter--)
    { 
      if (ccounter == 0)
        { dst++; ccounter=cell_width; }
      *dst += fval;
    }
}

/*****************************************************************************/
/* STATIC          local_reinterpret_and_add_signed_floats                   */
/*****************************************************************************/

static void
  local_reinterpret_and_add_signed_floats(const void *bufs[],
                                          const int widths[],
                                          const int types[], int num_lines,
                                          int precision,
                                          int missing_src_samples,
                                          void *void_dst, int dst_min,
                                          int num_cells, int dst_type,
                                          int cell_width, int acc_precision,
                                          int cell_lines_left,
                                          int cell_height, int exponent_bits)
  /* Similar to `local_reinterpret_and_copy_signed_floats', apart from the
     cell aggregation process. */
{
  assert((dst_type == KDRD_FLOAT_TYPE) && (exponent_bits > 0));
         
  float *dst = ((float *) void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  if ((num_lines < 1) || (num_cells < 1))
    return;
  
  // Skip over source samples as required
  const void *src = *(bufs++);
  int src_len = *(widths++), src_type = *(types++);  num_lines--;
  int n=0; // Use this to index the samples in the source
  while (missing_src_samples < 0)
    { 
      n = -missing_src_samples;
      if (n < src_len)
        missing_src_samples = 0; // No need to keep looping
      else if (num_lines > 0)
        { 
          missing_src_samples += src_len;
          src=*(bufs++); src_len=*(widths++); src_type=*(types++);
          num_lines--; n=0;
        }
      else
        { 
          assert(src_len > 0); // Last source line is not allowed to be empty
          n = src_len-1; missing_src_samples=0;
        }
    }
  
  int needed_samples = num_cells*cell_width;
  if (missing_src_samples >= needed_samples)
    missing_src_samples = needed_samples-1;
  
  // Prepare the conversion parameters
  union {
    kdu_int32 int_val;
    float float_val;
  } cast;
  if (precision > 32)
    precision = 32;
  else if (precision < 2)
    precision = 2;
  if (exponent_bits > (precision-1))
    exponent_bits = precision-1;
  int mantissa_bits = precision - 1 - exponent_bits;
  int exp_off = (1<<(exponent_bits-1)) - 1;
  int mantissa_upshift = 23 - mantissa_bits; // Shift to 32-bit IEEE floats
  int mantissa_downshift = -mantissa_upshift; // May have to downshift
  kdu_int32 exp_adjust = exp_off - 127; // Subtract this from src exponents
  kdu_int32 exp_max = 254 + exp_adjust;
  float denorm_scale = 1.0f;
  if (exp_adjust < 0)
    { // Implement as multiplication of the converted floats to correctly
      // bring denormals out of their denormalized state.
      denorm_scale = kdu_pwrof2f(-exp_adjust);
      exp_adjust = 0;
      exp_max = 2*exp_off; // This is the smaller exponent bound here
    }
  kdu_int32 mag_max = ((exp_max+1)<<mantissa_bits)-1;
  kdu_int32 pre_adjust=exp_adjust<<mantissa_bits; // Subtract before shifts
  kdu_int32 mag_mask = ~(((kdu_int32)-1) << (precision-1));
  float out_scale = denorm_scale; // Compensates for `exp_off' < 127
  out_scale *= 0.5f; // KDU scales signed values by 0.5
  assert(acc_precision < 0);
  out_scale *= kdu_pwrof2f(acc_precision);
  
  // Now perform the sample conversion process
  float fval=0.0f;
  int ccounter = cell_width;
  if (missing_src_samples)
    { // Generate a single value and replicate it
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      kdu_int32 int_val = ((const kdu_int32 *)src)[0];
      kdu_int32 sign_bit = int_val & KDU_INT32_MIN;
      int_val &= mag_mask;      
      if (int_val < pre_adjust)
        int_val = pre_adjust; // Avoid exponent underflow
      else if (int_val > mag_max)
        int_val = mag_max; // Avoid exponent overflow
      int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
      if (mantissa_upshift >= 0)
        int_val <<= mantissa_upshift;
      else
        int_val >>= mantissa_downshift;
      int_val |= sign_bit; // Now we have true IEEE single precision floats
      cast.int_val = int_val;
      fval = cast.float_val;
      fval *= out_scale;

      for (int m=missing_src_samples; m > 0; m--, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          *dst += fval;
        }
      needed_samples -= missing_src_samples;
    }
  
  while (needed_samples > 0)
    { 
      needed_samples += n; // Don't worry; we'll effectively take n away again
      if (src_len > needed_samples)
        src_len = needed_samples;
      needed_samples -= src_len;
      if (src_type != KDRD_INT32_TYPE)
        assert(0);
      const kdu_int32 *sp = (const kdu_int32 *)src;
      for (; n < src_len; n++, ccounter--)
        { 
          if (ccounter == 0)
            { dst++; ccounter=cell_width; }
          kdu_int32 int_val = sp[n];
          kdu_int32 sign_bit = int_val & KDU_INT32_MIN;
          int_val &= mag_mask;      
          if (int_val < pre_adjust)
            int_val = pre_adjust; // Avoid exponent underflow
          else if (int_val > mag_max)
            int_val = mag_max; // Avoid exponent overflow
          int_val -= pre_adjust; // Compensate for unlikely case: exp_off > 127
          if (mantissa_upshift >= 0)
            int_val <<= mantissa_upshift;
          else
            int_val >>= mantissa_downshift;
          int_val |= sign_bit; // Now we have true IEEE single precision floats
          cast.int_val = int_val;
          fval = cast.float_val;
          fval *= out_scale;
          *dst += fval;
        }
      
      // Advance to next line
      if (num_lines == 0)
        break; // All out of data
      src=*(bufs++); src_len=*(widths++); src_type=*(types++);
      num_lines--; n=0;
    }
  
  // Perform right edge padding as required
  for (; needed_samples > 0; needed_samples--, ccounter--)
    { 
      if (ccounter == 0)
        { dst++; ccounter=cell_width; }
      *dst += fval;
    }
}

/*****************************************************************************/
/* INLINE                configure_conversion_function                       */
/*****************************************************************************/

static inline void
  configure_conversion_function(kdrd_channel *chan)
  /* Installs appropriate an `convert_and_copy' or `convert_and_add' function
     in the supplied channel, depending on whether or not boxcar integration
     is employed (`convert_and_add' is used only for boxcar integration).
        The configured function depends on the channel's line-type, but
     SIMD acceleration functions may depend on other things, such as whether
     or not all component tile-lines have a specific data type.
        This function is always invoked from the `make_tile_bank_current'
     function in `kdu_region_decompressor', after configuring the relevant
     channel members, except where no conversion function is required, which
     happens only if `chan->can_use_component_samples_directly' is true or
     `chan->lut' is non-NULL.  In the latter case, one of the static
     `perform_palette_map' or `map_and_convert' functions are used instead of
     a custom conversion function. */
{
  if (chan->interp_float_exp_bits > 0)
    { // Need special conversion functions for float-reinterpreted samples
      if (chan->source->src_types != KDRD_INT32_TYPE)
        { KDU_ERROR(e,0x03021601); e <<
          KDU_TXT("Attempting to force re-interpretation of integers as "
                  "floating point bit patterns, where the source line buffers "
                  "do not employ an absolute integer representation.  This "
                  "suggests that the special \"reinterpret-as-float\" "
                  "format found in a JPX pixel format (pxfm) box has been "
                  "used to describe codestream samples that do not have "
                  "an associated non-linear point transform of the SMAG "
                  "or UMAG variety.");
        }
      if (chan->boxcar_log_size > 0)
        { 
          if (chan->interp_orig_signed)
            chan->convert_and_add_func =
              local_reinterpret_and_add_signed_floats;
          else
            chan->convert_and_add_func =
              local_reinterpret_and_add_unsigned_floats;          
        }
      else
        { 
          if (chan->interp_orig_signed)
            { 
              chan->convert_and_copy_func =
                local_reinterpret_and_copy_signed_floats;              
#ifdef KDU_SIMD_OPTIMIZATIONS
              KDRD_SIMD_SET_REINTERP_COPY_SF_FUNC(chan->convert_and_copy_func,
                                                  chan->interp_float_exp_bits,
                                                  chan->interp_orig_prec);
#endif
            }
          else
            { 
              chan->convert_and_copy_func =
                local_reinterpret_and_copy_unsigned_floats;          
#ifdef KDU_SIMD_OPTIMIZATIONS
              KDRD_SIMD_SET_REINTERP_COPY_UF_FUNC(chan->convert_and_copy_func,
                                                  chan->interp_float_exp_bits,
                                                  chan->interp_orig_prec);
#endif
            }
        }
    }
  else if (chan->boxcar_log_size > 0)
    { // Need to install a `convert_and_add' function
      if (chan->line_type == KDRD_FIX16_TYPE)
        chan->convert_and_add_func = local_convert_and_add;
      else if (chan->line_type == KDRD_FLOAT_TYPE)
        chan->convert_and_add_func = local_convert_and_add_float;
      else
        assert(0);
    }
  else
    { // Install a `convert_and_copy' function
      if (chan->line_type == KDRD_FIX16_TYPE)
        { 
          chan->convert_and_copy_func = local_convert_and_copy_to_fix16;
#ifdef KDU_SIMD_OPTIMIZATIONS
          KDRD_SIMD_SET_CONVERT_COPY_FIX16_FUNC(chan->convert_and_copy_func,
                                                chan->source->src_types);
#endif
        }
      else if (chan->line_type == KDRD_FLOAT_TYPE)
        chan->convert_and_copy_func = local_convert_and_copy_to_float;
      else if (chan->line_type == KDRD_INT32_TYPE)
        chan->convert_and_copy_func = local_convert_and_copy_to_int32;
      else
        assert(0);
    }
}


/* ========================================================================= */
/*                     kdrc_transfer_func implementations                    */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                local_transfer_fix16_to_bytes                       */
/*****************************************************************************/

static void
  local_transfer_fix16_to_bytes(const void *src_buf, int src_p, int src_type,
                                int skip_samples, int num_samples,
                                void *dst, int dst_prec, int gap,
                                bool leave_signed, float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_FIX16_TYPE) && unused_clip_outputs); // Always clip
  const kdu_int16 *sp = ((const kdu_int16 *)src_buf) + skip_samples;
  kdu_byte *dp = (kdu_byte *)dst;
  if ((fabsf(src_scale-1.0f) < 1.0f/512.0f) && (fabsf(src_off) < 1.0f))
    { // No special scaling required (at least to reach 8-bit accuracy) and
      // any source offset is not too large -- no risk of overflow
      if (dst_prec <= 8)
        { // Normal case, in which we want to fit the data completely within
          // the 8-bit output samples
          int downshift = KDU_FIX_POINT-dst_prec;
          kdu_int16 val, offset, mask;
          offset = (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
          offset += (kdu_int16) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          offset += 1<<(downshift-1); // Rounding offset for the downshift
          mask = (kdu_int16)((-1) << dst_prec);
          if (leave_signed)
            { // Unusual case in which we want a signed result
              kdu_int16 post_offset = (kdu_int16)(1<<(dst_prec-1));
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = (sp[0]+offset)>>downshift;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_byte)(val-post_offset);
                }
            }
          else
            { // Typical case of conversion to 8-bit result
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = (sp[0]+offset)>>downshift;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_byte) val;
                }
            }
        }
      else
        { // Unusual case in which the output precision is insufficient.  In
          // this case, clipping should be based upon the 8-bit output
          // precision after conversion to a signed or unsigned representation
          // as appropriate.
          int upshift=0, downshift = KDU_FIX_POINT-dst_prec;
          if (downshift < 0)
            { upshift=-downshift; downshift=0; }
          kdu_int16 min, max, val, offset=(kdu_int16)((1<<downshift)>>1);
          if (leave_signed)
            { 
              min = (kdu_int16)(-128>>upshift);
              max = (kdu_int16)(127>>upshift);
            }
          else
            { 
              offset += (1<<KDU_FIX_POINT)>>1;
              min = 0;  max = (kdu_int16)(255>>upshift);
            }
          offset += (int) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = (sp[0]+offset) >> downshift;
              if (val < min)
                val = min;
              else if (val > max)
                val = max;
              val <<= upshift; // Upshift is almost certainly 0
              *dp = (kdu_byte) val;
            }
        }
    }
  else if ((src_scale <= 7.0f) && (dst_prec <= 8) && (src_off < 1.0f))
    { // Same as above, but we need to pre-scale the source samples.  To do
      // this, we multiply by 2^12 * `src_scale', downshifting the result
      // by 12.  These steps are readily combined with the existing downshift,
      // so we only have an extra 16x16-bit multiplication, producing
      // intermediate 32-bit results that are offset and downshifted back to
      // 16 bit precision.
      kdu_int16 factor = (kdu_int16)(src_scale * (1<<12) + 0.5f);
      int downshift = KDU_FIX_POINT-dst_prec;
      int val, offset, mask;
      offset = (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
      downshift += 12;
      offset <<= 12;
      offset += (int) floorf(src_off * (1<<(12+KDU_FIX_POINT)) + 0.5f);
      offset += (1<<downshift)>>1; // Rounding offset for the downshift
      mask = (kdu_int16)((-1) << dst_prec);
      if (leave_signed)
        { // Unusual case in which we want a signed result
          int post_offset = (int)(1<<(dst_prec-1));
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = sp[0]; val *= factor; val = (val+offset)>>downshift;
              if (val & mask)
                val = (val < 0)?0:~mask;
              *dp = (kdu_byte)(val-post_offset);
            }
        }
      else
        { // Typical case of conversion to 8-bit result
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = sp[0]; val *= factor; val = (val+offset)>>downshift;
              if (val & mask)
                val = (val < 0)?0:~mask;
              *dp = (kdu_byte) val;
            }
        }
    }
  else
    { // Handle very large scaling factors or large offsets using floating
      // point arithmetic
      src_scale *= kdu_pwrof2f(-KDU_FIX_POINT);
      float dst_scale = kdu_pwrof2f(dst_prec);
      float scale = src_scale * dst_scale;
      float offset = (src_off + 0.5f) * dst_scale; // Takes us to unsigned
      float min_fval = 0.0f;
      float max_fval = dst_scale-1.0f;
      if (leave_signed)
        { 
          offset -= 0.5f*dst_scale;
          min_fval -= 0.5f*dst_scale;
          max_fval -= 0.5f*dst_scale;
        }
      if (dst_prec > 8)
        { // Adjust upper and lower bounds to avoid 8-bit overflow
          min_fval = (leave_signed)?-128.0f:0.0f;
          max_fval = (leave_signed)?127.0f:255.0f;
        }
      offset += 0.5f; // Pre-rounding offset
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = (float) sp[0];  fval = (fval * scale) + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          kdu_int32 ival = (kdu_int32) floorf(fval);
          *dp = (kdu_byte) ival; // Forces wrap-around if appropriate
        }
    }
}

/*****************************************************************************/
/* STATIC                local_transfer_int32_to_bytes                       */
/*****************************************************************************/

static void
  local_transfer_int32_to_bytes(const void *src_buf, int src_prec,
                                int src_type, int skip_samples,
                                int num_samples, void *dst, int dst_prec,
                                int gap, bool leave_signed, float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_INT32_TYPE) && unused_clip_outputs); // Always clip
  const kdu_int32 *sp = ((const kdu_int32 *)src_buf) + skip_samples;
  kdu_byte *dp = (kdu_byte *)dst;
  if ((fabsf(src_scale-1.0f) < 1.0f/512.0f) && (fabsf(src_off) < 1.0f))
    { // No special scaling required (at least to reach 8-bit accuracy) and
      // any source offset is not too large -- no risk of overflow
      if (dst_prec <= 1.0f/512.0f)
        { // Normal case, in which we want to fit the data completely within
          // the 8-bit output samples
          int downshift = src_prec - dst_prec;
          kdu_int32 val, mask = (kdu_int32)((-1) << dst_prec);
          kdu_int32 offset=1<<(src_prec-1); // Cvt signed to unsigned
          offset += (kdu_int32) floorf(src_off * (1<<src_prec) + 0.5f);
          if (downshift >= 0)
            { // Normal case
              offset += (1<<downshift)>>1; // Rounding offset for the downshift
              if (leave_signed)
                { 
                  kdu_int32 post_offset = 1<<(dst_prec-1);
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)>>downshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_byte)(val-post_offset);
                    }
                }
              else
                { // Typical case of conversion to unsigned result
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)>>downshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_byte) val;
                    }
                }
            }
          else
            { // Unusual case, in which original precision is less than 8
              int upshift = -downshift;
              if (leave_signed)
                { 
                  kdu_int32 post_offset = ((1<<dst_prec)>>1);
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)<<upshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_byte)(val-post_offset);
                    }
                }
              else
                { // Conversion to unsigned result
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)<<upshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_byte) val;
                    }
                }
            }
        }
      else
        { // Unusual case in which the output precision is insufficient.  In
          // this case, clipping should be based upon the 8-bit output
          // precision after conversion to a signed or unsigned representation
          // as appropriate.
          int upshift=0, downshift = src_prec - dst_prec;
          if (downshift < 0)
            { upshift=-downshift; downshift=0; }
          kdu_int32 min, max, val, offset=(kdu_int32)((1<<downshift)>>1);
          if (leave_signed)
            { 
              min = (-128>>upshift);
              max = (127>>upshift);
            }
          else
            { 
              offset += (1<<src_prec)>>1;
              min = 0;  max = (255>>upshift);
            }
          offset += (int) floorf(src_off * (1<<src_prec) + 0.5f);
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = (sp[0]+offset) >> downshift;
              if (val < min)
                val = min;
              else if (val > max)
                val = max;
              val <<= upshift;
              *dp = (kdu_byte) val;
            }
        }
    }
  else
    { // Handle non-trivial scaling factors and large offsetes using floating
      // point arithmetic.
      src_scale *= kdu_pwrof2f(-src_prec);
      float dst_scale = kdu_pwrof2f(dst_prec);
      float scale = src_scale * dst_scale;
      float offset = (src_off + 0.5f) * dst_scale; // Takes us to unsigned
      float min_fval = 0.0f;
      float max_fval = dst_scale-1.0f;
      if (leave_signed)
        { 
          offset -= 0.5f*dst_scale;
          min_fval -= 0.5f*dst_scale;
          max_fval -= 0.5f*dst_scale;
        }
      if (dst_prec > 8)
        { // Adjust upper and lower bounds to avoid 8-bit overflow
          min_fval = (leave_signed)?-128.0f:0.0f;
          max_fval = (leave_signed)?127.0f:255.0f;
        }
      offset += 0.5f; // Pre-rounding offset
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = (float) sp[0];  fval = (fval * scale) + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          kdu_int32 ival = (kdu_int32) floorf(fval);
          *dp = (kdu_byte) ival; // Forces wrap-around if appropriate
        }
    }
}

/*****************************************************************************/
/* STATIC                local_transfer_float_to_bytes                       */
/*****************************************************************************/

static void
  local_transfer_float_to_bytes(const void *src_buf, int src_p, int src_type,
                                int skip_samples, int num_samples,
                                void *dst, int dst_prec, int gap,
                                bool leave_signed, float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_FLOAT_TYPE) && unused_clip_outputs); // Always clip
  const float *sp = ((const float *)src_buf) + skip_samples;
  kdu_byte *dp = (kdu_byte *)dst;
  float dst_scale = kdu_pwrof2f(dst_prec);
  float scale = src_scale * dst_scale;
  float offset = (src_off + 0.5f) * dst_scale; // Converts to unsigned outputs
  float min_fval = 0.0f;
  float max_fval = dst_scale-1.0f;
  if (leave_signed)
    { 
      offset -= 0.5f*dst_scale;
      min_fval -= 0.5f*dst_scale;
      max_fval -= 0.5f*dst_scale;
    }
  if (dst_prec > 8)
    { // Adjust upper and lower bounds to avoid 8-bit overflow
      min_fval = (leave_signed)?-128.0f:0.0f;
      max_fval = (leave_signed)?127.0f:255.0f;
    }
  offset += 0.5f; // Pre-rounding offset
  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
    { 
      float fval = sp[0];  fval = (fval * scale) + offset;
      fval = kdu_fminf(fval,max_fval);
      fval = kdu_fmaxf(fval,min_fval);
      kdu_int32 ival = (kdu_int32) floorf(fval);
      *dp = (kdu_byte) ival; // Forces wrap-around if appropriate
    }
}

/*****************************************************************************/
/* STATIC                 local_transfer_fill_to_bytes                       */
/*****************************************************************************/

static void
  local_transfer_fill_to_bytes(const void *src_buf, int src_p, int src_type,
                               int skip_samples, int num_samples,
                               void *dst, int dst_prec, int gap,
                               bool leave_signed, float unused_src_scale,
                               float unused_src_off, bool unused_clip_outputs)
{
  kdu_byte *dp = (kdu_byte *)dst;
  kdu_byte fill_val = 0xFF;
  if (dst_prec < 8)
    fill_val = (kdu_byte)((1<<dst_prec)-1);
  if (leave_signed)
    fill_val >>= 1;
  for (; num_samples > 0; num_samples--, dp+=gap)
    *dp = fill_val;
}

/*****************************************************************************/
/* STATIC                local_transfer_fix16_to_words                       */
/*****************************************************************************/

static void
  local_transfer_fix16_to_words(const void *src_buf, int src_p, int src_type,
                                int skip_samples, int num_samples,
                                void *dst, int dst_prec, int gap,
                                bool leave_signed, float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_FIX16_TYPE) && unused_clip_outputs); // Always clip
  const kdu_int16 *sp = ((const kdu_int16 *)src_buf) + skip_samples;
  kdu_uint16 *dp = (kdu_uint16 *)dst;
  if ((fabs(src_scale-1.0f) < 1.0f/(1<<17)) && (fabsf(src_off) < 1.0f))
    { // No special scaling required (at least to reach 16-bit accuracy) and
      // any source offset is not too large -- no risk of overflow
      int downshift = KDU_FIX_POINT-dst_prec;
      kdu_int16 val, offset, mask;
      if (downshift >= 0)
        { 
          offset = (1<<downshift)>>1; // Rounding offset for the downshift
          offset += (1<<KDU_FIX_POINT)>>1; // Convert from signed to unsigned
          offset += (kdu_int16) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          mask = (kdu_int16)((-1) << dst_prec);
          if (leave_signed)
            { 
              kdu_int16 post_offset = (kdu_int16)(1<<(dst_prec-1));
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = (sp[0]+offset)>>downshift;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_uint16)(val-post_offset);
                }
            }
          else
            { // Typical case
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = (sp[0]+offset)>>downshift;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_uint16) val;
                }
            }
        }
      else if (dst_prec <= 16)
        { // Need to upshift, but result still fits in 16 bits
          int upshift = -downshift;
          mask = (kdu_int16)(0xFFFF << KDU_FIX_POINT); // Apply the mask first
          offset = (1<<KDU_FIX_POINT)>>1; // Conversion from signed to unsigned
          offset += (kdu_int16) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          if (leave_signed)
            { 
              kdu_int16 post_offset = (kdu_int16)(1<<(dst_prec-1));
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = sp[0]+offset;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_uint16)((val-post_offset) << upshift);
                }
            }
          else
            { 
              for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                { 
                  val = sp[0]+offset;
                  if (val & mask)
                    val = (val < 0)?0:~mask;
                  *dp = (kdu_uint16)(val << upshift);
                }
            }
        }
      else
        { // Unusual case in which the output precision is insufficient.  In
          // this case, clipping should be based upon the 16-bit output
          // precision after conversion to a signed or unsigned representation
          // as appropriate.
          int upshift = -downshift;
          kdu_int32 min, max, val, offset=0;
          if (leave_signed)
            { 
              min = (-(1<<15)) >> upshift;
              max = ((1<<15)-1) >> upshift;
            }
          else
            { 
              offset += (1<<KDU_FIX_POINT)>>1;
              min = 0;  max = ((1<<16)-1) >> upshift;
            }
          offset += (int) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = sp[0] + offset;
              if (val < min)
                val = min;
              else if (val > max)
                val = max;
              val <<= upshift;
              *dp = (kdu_uint16) val;
            }
        }
    }
  else
    { // Handle non-trivial scaling factors and large offsets using floating
      // point arithmetic.
      src_scale *= kdu_pwrof2f(-KDU_FIX_POINT);
      float dst_scale = kdu_pwrof2f(dst_prec);
      float scale = src_scale * dst_scale;
      float offset = (src_off + 0.5f) * dst_scale; // Takes us to unsigned
      float min_fval = 0.0f;
      float max_fval = dst_scale-1.0f;
      if (leave_signed)
        { 
          offset -= 0.5f*dst_scale;
          min_fval -= 0.5f*dst_scale;
          max_fval -= 0.5f*dst_scale;
        }
      if (dst_prec > 16)
        { // Adjust upper and lower bounds to avoid 8-bit overflow
          min_fval = (leave_signed)?-32768.0f:0.0f;
          max_fval = (leave_signed)?32767.0f:65535.0f;
        }
      offset += 0.5f; // Pre-rounding offset
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = (float) sp[0];  fval = (fval * scale) + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          kdu_int32 ival = (kdu_int32) floorf(fval);
          *dp = (kdu_uint16) ival; // Forces wrap-around if appropriate
        }
    }
}

/*****************************************************************************/
/* STATIC                local_transfer_int32_to_words                       */
/*****************************************************************************/

static void
  local_transfer_int32_to_words(const void *src_buf, int src_prec,
                                int src_type, int skip_samples,
                                int num_samples, void *dst, int dst_prec,
                                int gap, bool leave_signed,  float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_INT32_TYPE) && unused_clip_outputs); // Always clip
  const kdu_int32 *sp = ((const kdu_int32 *)src_buf) + skip_samples;
  kdu_uint16 *dp = (kdu_uint16 *)dst;
  if ((fabs(src_scale-1.0f) < 1.0f/(1<<17)) && (fabsf(src_off) < 1.0f))
    { // No special scaling required (at least to reach 16-bit accuracy) and
      // any source offset is not too large -- no risk of overflow
      if (dst_prec <= 16)
        { // Normal case, in which we want to fit the data completely within
          // the 16-bit output samples
          int downshift = src_prec - dst_prec;
          kdu_int32 val, mask = (kdu_int32)((-1) << dst_prec);
          kdu_int32 offset=(1<<src_prec)>>1; // Cvt signed to unsigned
          offset += (kdu_int32) floorf(src_off * (1<<src_prec) + 0.5f);
          if (downshift >= 0)
            { // Normal case
              offset += (1<<downshift)>>1; // Rounding offset for the downshift
              if (leave_signed)
                { 
                  kdu_int32 post_offset = 1<<(dst_prec-1);
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)>>downshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_uint16)(val-post_offset);
                    }
                }
              else
                { // Typical case
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)>>downshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_uint16) val;
                    }
                }
            }
          else
            { // Unusual case, in which original precision is less than 16
              int upshift = -downshift;
              if (leave_signed)
                { 
                  kdu_int32 post_offset = 1<<(dst_prec-1);
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)<<upshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_uint16)(val-post_offset);
                    }
                }
              else
                { 
                  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
                    { 
                      val = (sp[0]+offset)<<upshift;
                      if (val & mask)
                        val = (val < 0)?0:~mask;
                      *dp = (kdu_uint16) val;
                    }                  
                }
            }
        }
      else
        { // Unusual case in which the output precision is insufficient.  In
          // this case, clipping should be based upon the 16-bit output
          // precision after conversion to a signed or unsigned representation
          // as appropriate.
          int upshift=0, downshift = src_prec - dst_prec;
          if (downshift < 0)
            { upshift=-downshift; downshift=0; }
          kdu_int32 min, max, val, offset=(kdu_int32)((1<<downshift)>>1);
          if (leave_signed)
            { 
              min = (-(1<<15)) >> upshift;
              max = ((1<<15)-1) >> upshift;
            }
          else
            { 
              offset += (1<<src_prec)>>1;
              min = 0;  max = ((1<<16)-1) >> upshift;
            }
          offset += (int) floorf(src_off * (1<<KDU_FIX_POINT) + 0.5f);
          for (; num_samples > 0; num_samples--, sp++, dp+=gap)
            { 
              val = (sp[0]+offset) >> downshift;
              if (val < min)
                val = min;
              else if (val > max)
                val = max;
              val <<= upshift;
              *dp = (kdu_uint16) val;
            }
        }
    }
  else
    { // Handle non-trivial scaling factors and large offsets using floating
      // point arithmetic.
      src_scale *= kdu_pwrof2f(-src_prec);
      float dst_scale = kdu_pwrof2f(dst_prec);
      float scale = src_scale * dst_scale;
      float offset = (src_off + 0.5f) * dst_scale; // Takes us to unsigned
      float min_fval = 0.0f;
      float max_fval = dst_scale-1.0f;
      if (leave_signed)
        { 
          offset -= 0.5f*dst_scale;
          min_fval -= 0.5f*dst_scale;
          max_fval -= 0.5f*dst_scale;
        }
      if (dst_prec > 16)
        { // Adjust upper and lower bounds to avoid 8-bit overflow
          min_fval = (leave_signed)?-32768.0f:0.0f;
          max_fval = (leave_signed)?32767.0f:65535.0f;
        }
      offset += 0.5f; // Pre-rounding offset
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = (float) sp[0];  fval = (fval * scale) + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          kdu_int32 ival = (kdu_int32) floorf(fval);
          *dp = (kdu_uint16) ival; // Forces wrap-around if appropriate
        }
    }
}

/*****************************************************************************/
/* STATIC                local_transfer_float_to_words                       */
/*****************************************************************************/

static void
  local_transfer_float_to_words(const void *src_buf, int src_p, int src_type,
                                int skip_samples, int num_samples,
                                void *dst, int dst_prec, int gap,
                                bool leave_signed, float src_scale,
                                float src_off, bool unused_clip_outputs)
{
  assert((src_type == KDRD_FLOAT_TYPE) && unused_clip_outputs); // Always clip
  const float *sp = ((const float *)src_buf) + skip_samples;
  kdu_uint16 *dp = (kdu_uint16 *)dst;
  float dst_scale = kdu_pwrof2f(dst_prec);
  float scale = src_scale * dst_scale;
  float offset = (src_off + 0.5f) * dst_scale; // Converts to unsigned outputs
  float min_fval = 0.0f;
  float max_fval = dst_scale-1.0f;
  if (leave_signed)
    { 
      offset -= 0.5f*dst_scale;
      min_fval -= 0.5f*dst_scale;
      max_fval -= 0.5f*dst_scale;
    }
  if (dst_prec > 16)
    { // Adjust upper and lower bounds to avoid 8-bit overflow
      min_fval = (leave_signed)?-32768.0f:0.0f;
      max_fval = (leave_signed)?32767.0f:65535.0f;
    }
  offset += 0.5f; // Pre-rounding offset
  for (; num_samples > 0; num_samples--, sp++, dp+=gap)
    { 
      float fval = sp[0];  fval = (fval * scale) + offset;
      fval = kdu_fminf(fval,max_fval);
      fval = kdu_fmaxf(fval,min_fval);
      kdu_int32 ival = (kdu_int32) floorf(fval);
      *dp = (kdu_uint16) ival; // Forces wrap-around if appropriate
    }
}

/*****************************************************************************/
/* STATIC                 local_transfer_fill_to_words                       */
/*****************************************************************************/

static void
  local_transfer_fill_to_words(const void *src_buf, int src_p, int src_type,
                               int skip_samples, int num_samples,
                               void *dst, int dst_prec, int gap,
                               bool leave_signed, float unused_src_scale,
                               float unused_src_off, bool unused_clip_outputs)
{
  kdu_uint16 *dp = (kdu_uint16 *)dst;
  kdu_uint16 fill_val = 0xFFFF;
  if (dst_prec < 16)
    fill_val = (kdu_uint16)((1<<dst_prec)-1);
  if (leave_signed)
    fill_val >>= 1;
  for (; num_samples > 0; num_samples--, dp+=gap)
    *dp = fill_val;
}

/*****************************************************************************/
/* STATIC                local_transfer_fix16_to_floats                      */
/*****************************************************************************/

static void
  local_transfer_fix16_to_floats(const void *src_buf, int src_p, int src_type,
                                 int skip_samples, int num_samples,
                                 void *dst, int dst_prec, int gap,
                                 bool leave_signed, float src_scale,
                                 float src_off, bool clip_outputs)
{
  assert(src_type == KDRD_FIX16_TYPE);
  const kdu_int16 *sp = ((const kdu_int16 *)src_buf) + skip_samples;
  float *dp = (float *)dst;
  src_scale *= kdu_pwrof2f(-KDU_FIX_POINT);
  assert(dst_prec >= 0);
  float dst_scale = kdu_pwrof2f(dst_prec);
  float scale = src_scale * dst_scale;
  float offset = (src_off + 0.5f) * dst_scale; // Converts to unsigned outputs
  float min_fval = 0.0f;
  float max_fval = dst_scale;
  if (dst_prec > 0)
    max_fval -= 1.0f;
  if (leave_signed)
    { 
      offset -= 0.5f*dst_scale;
      min_fval -= 0.5f*dst_scale;
      max_fval -= 0.5f*dst_scale;
    }
  if (clip_outputs)
    { 
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          *dp = fval;
        }
    }
  else
    { // In reality, we do not expect to ever excite this branch with fix16
      // inputs.
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          *dp = fval;
        }
    }
}

/*****************************************************************************/
/* STATIC               local_transfer_int32_to_floats                       */
/*****************************************************************************/

static void
  local_transfer_int32_to_floats(const void *src_buf, int src_prec,
                                 int src_type, int skip_samples,
                                 int num_samples, void *dst, int dst_prec,
                                 int gap, bool leave_signed, float src_scale,
                                 float src_off, bool clip_outputs)
{
  assert(src_type == KDRD_INT32_TYPE);
  const kdu_int32 *sp = ((const kdu_int32 *)src_buf) + skip_samples;
  float *dp = (float *)dst;
  src_scale *= kdu_pwrof2f(-src_prec);
  assert(dst_prec >= 0);
  float dst_scale = kdu_pwrof2f(dst_prec);
  float scale = src_scale * dst_scale;
  float offset = (src_off + 0.5f) * dst_scale; // Converts to unsigned outputs
  float min_fval = 0.0f;
  float max_fval = dst_scale;
  if (dst_prec > 0)
    max_fval -= 1.0f;
  if (leave_signed)
    { 
      offset -= 0.5f*dst_scale;
      min_fval -= 0.5f*dst_scale;
      max_fval -= 0.5f*dst_scale;
    }
  if (clip_outputs)
    { 
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          *dp = fval;
        }
    }
  else
    { // In reality, we do not expect to ever excite this branch with fix16
      // inputs.
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          *dp = fval;
        }
    }
}

/*****************************************************************************/
/* STATIC               local_transfer_float_to_floats                       */
/*****************************************************************************/

static void
  local_transfer_float_to_floats(const void *src_buf, int src_p, int src_type,
                                 int skip_samples, int num_samples,
                                 void *dst, int dst_prec, int gap,
                                 bool leave_signed,  float src_scale,
                                 float src_off, bool clip_outputs)
{
  assert(src_type == KDRD_FLOAT_TYPE);
  const float *sp = ((const float *)src_buf) + skip_samples;
  float *dp = (float *)dst;
  assert(dst_prec >= 0);
  float dst_scale = kdu_pwrof2f(dst_prec);
  float scale = src_scale * dst_scale;
  float offset = (src_off + 0.5f) * dst_scale; // Converts to unsigned outputs
  float min_fval = 0.0f;
  float max_fval = dst_scale;
  if (dst_prec > 0)
    max_fval -= 1.0f;
  if (leave_signed)
    { 
      offset -= 0.5f*dst_scale;
      min_fval -= 0.5f*dst_scale;
      max_fval -= 0.5f*dst_scale;
    }
  if (clip_outputs)
    { 
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          fval = kdu_fminf(fval,max_fval);
          fval = kdu_fmaxf(fval,min_fval);
          *dp = fval;
        }
    }
  else
    { // This branch will be used for float-formatted or non-trivial
      // fixpoint-formatted source data that is likely to represent HDR content
      for (; num_samples > 0; num_samples--, sp++, dp+=gap)
        { 
          float fval = sp[0]*scale + offset;
          *dp = fval;
        }
    }
}

/*****************************************************************************/
/* STATIC                 local_transfer_fill_to_floats                      */
/*****************************************************************************/

static void
  local_transfer_fill_to_floats(const void *src_buf, int src_p, int src_type,
                                int skip_samples, int num_samples,
                                void *dst, int dst_prec, int gap,
                                bool leave_signed, float unused_src_scale,
                                float unused_src_off, bool unused_clip_outputs)
{
  float *dp = (float *)dst;
  float fill_val = (leave_signed)?0.5f:1.0f;
  for (; num_samples > 0; num_samples--, dp+=gap)
    *dp = fill_val;
}

/*****************************************************************************/
/* INLINE                 configure_transfer_functions                       */
/*****************************************************************************/

static inline kdrd_interleaved_transfer_func
  configure_transfer_functions(kdrd_channel_buf *channel_bufs,
                               int num_channel_bufs, int sample_bytes,
                               int skip_samples, int num_samples,
                               int pixel_gap, bool true_zero,
                               bool true_max, float cc_normalized_max)
  /* This function fills in the `kdrd_channel_buf::transfer_func' pointer,
     but also modifies the `kdrd_channel_buf::src_scale' value and the
     `kdrd_channel_buf::src_off' value for each channel buffer,
     based on the properties of each underlying channel buffer (found via
     `kdrd_channel_buf::chan'), the other `kdrd_channel_buf' members, and the
     `sample_bytes' argument.
        The may also modify the `kdrd_channel_buf::clip_outputs' flags if the
     target representation is floats (`sample_bytes'=4).  In all other cases,
     `clip_outputs' will enter as true for each channel and the function will,
     in any case ensure that this is the case.  For float outputs, the
     `clip_outputs' may enter as false, in which case the function leaves it
     false if the source data has a non-trivial non-default pixel format,
     as explained in the documentation of the floating point process
     functions offered by `kdu_region_decompressor'.
        Note that the values installed for `src_scale' and
     `src_offset' depend on whether the `true_zero' and/or `true_max'
     true-scaling modes are asserted, whether or not the output
     has a floating point representation, whether or not the source has
     a non-trivial float- or fixpoint-formatted representation (rather
     than the usual integer-formatted one), along with the precisions and
     signed/unsigned attributes.  These members are passed as the last 2
     arguments to the `transfer_func' when it is called.
        The `skip_cols' and `num_cols' arguments are the values that will be
     passed to each `kdrd_transfer_func' function that is installed, which
     might have an impact on the availability of certain SIMD accelerators.
        In the special case where the entire collection of `channel_bufs'
     can be written by a single interleaved transfer function, that function
     is returned here and the `kdrd_channel_buf::transfer_func' members are
     left NULL.  Moreover, in this case, the present function sets
     `channel_bufs[c].ilv_src', in accordance with the documentation of that
     member in the notes following the definition of `kdrd_channel_buf'.  In
     practice, the function will never return an interleaved transfer function
     unless `num_channel_bufs' and `pixel_gap' are both 4.
        If the `cc_normalized_max' argument is non-negative, it is used in
     place of the `kdrd_channel::interp_normalized_max' members in calculating
     source scaling parameters; moreover, in this case the
     `kdrd_channel::interp_normalized_natural_zero' member is ignored and
     treated as if it were -0.5.  These conditions correspond to the processing
     adjustments that must be made if a colour conversion operation (hence cc)
     occurs prior to channel transfer. */
{
  int c;
  
  // Start by working out the `src_scale', `src_off' and `clip_outputs' members
  bool float_out = false;
  if (sample_bytes == 4)
    float_out = true_max = true; // Float outputs always gets true-max scaling.
  bool any_non_trivial_src_scale=false, any_non_trivial_src_off=false;
  for (c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs + c;
      kdrd_channel *chan = cb->chan;
      bool non_trivial_src_format = ((chan->interp_float_exp_bits > 0) ||
                                     (chan->interp_fixpoint_int_bits != 0));
      bool t_max = true_max || non_trivial_src_format;
      if (!(float_out && non_trivial_src_format))
        { 
          assert(float_out || cb->clip_outputs); // Integer-based process funcs
                               // should have set `clip_outputs'=true already.
          cb->clip_outputs = true;
        }
      cb->src_scale = 1.0f;  // Set default scale and offset first
      cb->src_off = 0.0f;
      if (!(t_max || true_zero))
        continue; // Common case; no scale or offset
      float source_max = chan->interp_normalized_max;
      float source_nat_zero = chan->interp_normalized_natural_zero;
      float zeta = chan->interp_zeta;
      if (cc_normalized_max >= 0.0f)
        { 
          source_max = cc_normalized_max;
          source_nat_zero = -0.5f;
        }
      else if (t_max && !true_zero)
        { 
          if ((!chan->interp_orig_signed) || (source_max <= 0.01f))
            { // Stretch input range of -0.5 to normalized_max to full output
              // range, keeping the 0.5 notional level offset in the output.
              // The second condition above exists to handle the pathalogical
              // case in which a signed representation has only 1 bit, which
              // makes representation of both +ve and -ve values impossible,
              // so the scaling is not well defined for signed originals; in
              // this case we will just treat them as if they were unsigned
              // originals.
              float den = 0.5f + source_max;
              float num = 1.0f;
              if ((!float_out) || (cb->transfer_precision > 0))
                num -= kdu_pwrof2f(-cb->transfer_precision);
              cb->src_scale = num / den;
              cb->src_off = 0.5f*cb->src_scale - 0.5f;
            }
          else
            { // Stretch positive part of input range to positive part of
              // output range, with the level offset of 0.5 in place.
              float den = source_max;
              float num = 0.5f;
              if ((!float_out) || (cb->transfer_precision > 0))
                num -= kdu_pwrof2f(-cb->transfer_precision);
              cb->src_scale = num / den;
              cb->src_off = 0.0f;
            }
        }
      else if (true_zero)
        { // True zero without true max
          if ((zeta >= 0.0f) && (zeta < 1.0f))
            { // These conditions should always hold.
              if (chan->interp_orig_signed && !cb->transfer_signed)
                { // Signed to unsigned conversion
                  cb->src_scale = (1.0f-zeta) / 0.5f;
                  cb->src_off = zeta - 0.5f;
                }
              else if (cb->transfer_signed && !chan->interp_orig_signed)
                { // Unsigned to signed conversion
                  cb->src_scale = 0.5f / (0.5f-source_nat_zero);
                  cb->src_off = (0.5f-zeta)*cb->src_scale - 0.5f;
                }
            }
        }
      else if (true_max)
        { // Complete true scaling
          float den = (source_max - source_nat_zero);
          float max_out = 0.5f;
          if ((!float_out) || (cb->transfer_precision > 0))
            max_out -= kdu_pwrof2f(-cb->transfer_precision);
          if ((den > 0.0f) && (zeta >= 0.0f) && (zeta < 1.0f))
            { // These three conditions should always hold
              if (!cb->transfer_signed)
                { // Map to unsigned values, so that nat-zero -> zeta-0.5
                  float zero_out=zeta-0.5f;
                  cb->src_scale = (max_out-zero_out) / den;
                  cb->src_off = (zero_out - source_nat_zero*cb->src_scale);
                }
              else
                { // Map to signed values so that nat-zero -> 0
                  cb->src_scale = max_out / den;
                  cb->src_off = - source_nat_zero*cb->src_scale;
                }
            }
        }
      if (cb->src_scale != 1.0f)
        any_non_trivial_src_scale = true;
      if (cb->src_off != 0.0f)
        any_non_trivial_src_off = true;
    }
  
#ifdef KDU_SIMD_OPTIMIZATIONS
  if ((pixel_gap == 4) && (num_channel_bufs == 4) && (sample_bytes == 1) &&
      !(any_non_trivial_src_scale || any_non_trivial_src_off))
    { // See if we have sufficient conditions for interleaved transfer
      int src_type = channel_bufs[0].chan->line_type;
      int dst_prec = channel_bufs[0].transfer_precision;
      kdu_byte *base = channel_bufs[0].buf;
      base -= _addr_to_kdu_int32(base) & 3; // Align on 4-byte boundary
      channel_bufs[0].ilv_src = -1; channel_bufs[1].ilv_src = -1;
      channel_bufs[2].ilv_src = -1; channel_bufs[3].ilv_src = -1;
      for (c=0; c < num_channel_bufs; c++)
        { 
          kdrd_channel_buf *cb = channel_bufs + c;
          if ((cb->chan->line_type != src_type) ||
              (cb->transfer_precision != dst_prec) || cb->transfer_signed)
            break; // Require consistent conversions and unsigned outputs
          int dst_idx = (int)(cb->buf - base);
          if ((dst_idx < 0) || (dst_idx > 3))
            break; // Not aligned or not interleaved
          channel_bufs[dst_idx].ilv_src = c;
        }
      if ((c==num_channel_bufs) &&
          ((channel_bufs[0].ilv_src | channel_bufs[1].ilv_src |
            channel_bufs[2].ilv_src | channel_bufs[3].ilv_src) >= 0))
        { // Have a full set of interleaved 8-bit buffers
          kdrd_interleaved_transfer_func ilv_func = NULL;
          KDRD_SIMD_SET_INTERLEAVED_XFER_TO_BYTES_FUNC(ilv_func,src_type,
                                                       dst_prec);
          if (ilv_func != NULL)
            return ilv_func;
        }
    }
#endif // KDU_SIMD_OPTIMIZATIONS
  
  for (c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs + c;
      kdrd_channel *chan = cb->chan;
      if (sample_bytes == 1)
        { // Transferring to bytes
          if (cb->fill)
            cb->transfer_func = local_transfer_fill_to_bytes;
          else
            { 
              if (chan->line_type == KDRD_FIX16_TYPE)
                cb->transfer_func = local_transfer_fix16_to_bytes;
              else if (chan->line_type == KDRD_FLOAT_TYPE)
                cb->transfer_func = local_transfer_float_to_bytes;
              else if (chan->line_type == KDRD_INT32_TYPE)
                cb->transfer_func = local_transfer_int32_to_bytes;
              else
                assert(0);
#ifdef KDU_SIMD_OPTIMIZATIONS
              if ((fabsf(cb->src_scale-1.0f) < 1.0f/512.0f) &&
                  (fabsf(cb->src_off) < 1.0f/512.0f))
                KDRD_SIMD_SET_XFER_TO_BYTES_FUNC(cb->transfer_func,
                                                 chan->line_type,pixel_gap,
                                                 cb->transfer_precision,
                                                 cb->transfer_signed);
#endif // KDU_SIMD_OPTIMIZATIONS
            }
        }
      else if (sample_bytes == 2)
        { // Transferring to 16-bit words
          if (cb->fill)
            cb->transfer_func = local_transfer_fill_to_words;          
          else if (chan->line_type == KDRD_FIX16_TYPE)
            cb->transfer_func = local_transfer_fix16_to_words;
          else if (chan->line_type == KDRD_FLOAT_TYPE)
            cb->transfer_func = local_transfer_float_to_words;
          else if (chan->line_type == KDRD_INT32_TYPE)
            cb->transfer_func = local_transfer_int32_to_words;
          else
            assert(0);
        }
      else if (sample_bytes == 4)
        { // Transferring to 32-bit floats
          if (cb->fill)
            cb->transfer_func = local_transfer_fill_to_floats;
          else if (chan->line_type == KDRD_FIX16_TYPE)
            cb->transfer_func = local_transfer_fix16_to_floats;
          else if (chan->line_type == KDRD_FLOAT_TYPE)
            cb->transfer_func = local_transfer_float_to_floats;
          else if (chan->line_type == KDRD_INT32_TYPE)
            cb->transfer_func = local_transfer_int32_to_floats;
          else
            assert(0);
        }
      else
        assert(0);
    }
  return NULL;
}


/* ========================================================================= */
/*                  kdrc_white_stretch_func implementations                  */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    local_white_stretch                             */
/*****************************************************************************/

static void
  local_white_stretch(const kdu_int16 *sp, kdu_int16 *dp, int num_cols,
                      int stretch_residual)
{
  kdu_int32 val, stretch_factor=stretch_residual;
  kdu_int32 offset = -((-(stretch_residual<<(KDU_FIX_POINT-1))) >> 16);
  for (; num_cols > 0; num_cols--, sp++, dp++)
    { 
      val = *sp;
      val += ((val*stretch_factor)>>16) + offset;
      *dp = (kdu_int16) val;
    }
}

/*****************************************************************************/
/* INLINE              configure_white_stretch_function                      */
/*****************************************************************************/

static inline void
  configure_white_stretch_function(kdrd_channel *chan)
  /* This function fills in the `chan->white_stretch_func' pointer with
     a function that will perform the relevant white stretching operation. */
{
  chan->white_stretch_func = local_white_stretch;
#ifdef KDU_SIMD_OPTIMIZATIONS
  KDRD_SIMD_SET_WHITE_STRETCH_FUNC(chan->white_stretch_func);
#endif
}


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    reduce_ratio_to_ints                            */
/*****************************************************************************/

static bool
  reduce_ratio_to_ints(kdu_long &num, kdu_long &den)
  /* Divides `num' and `den' by their common factors until both are reduced
     to values which can be represented as 32-bit signed integers, or else
     no further common factors can be found.  In the latter case the
     function returns false. */
{
  if ((num <= 0) || (den <= 0))
    return false;
  if ((num % den) == 0)
    { num = num / den;  den = 1; }

  kdu_long test_fac = 2;
  while ((num > 0x7FFFFFFF) || (den > 0x7FFFFFFF))
    {
      while (((num % test_fac) != 0) || ((den % test_fac) != 0))
        {
          test_fac++;
          if ((test_fac >= num) || (test_fac >= den))
            return false;
        }
      num = num / test_fac;
      den = den / test_fac;
    }
  return true;
}

/*****************************************************************************/
/* STATIC                   find_canvas_cover_dims                           */
/*****************************************************************************/

static kdu_dims
  find_canvas_cover_dims(kdu_dims render_dims, kdu_codestream codestream,
                         kdrd_channel *channels, int num_channels,
                         bool on_transformed_canvas)
  /* This function returns the location and dimension of the region on the
     codestream's canvas coordinate system, required to render the region
     identified by `render_dims'.  If `on_transformed_canvas' is true, the
     resulting canvas cover region is to be described on the transformed
     canvas associated with an prevailing geometric transformations associated
     with the most recent call to `codestream.change_appearance'.  This is
     appropriate if the region is to be compared with regions returned by
     `codestream.get_tile_dims', or in fact just about any other codestream
     dimension reporting function, apart from `codestream.map_region'.  If
     you want a region suitable for supplying to
     `codestream.apply_input_restrictions', it must be prepared in a manner
     which is independent of geometric transformations, and this is done using
     `codestream.map_region' -- in this case, set `on_transformed_canvas'
     to false. */
{
  kdu_coords canvas_min, canvas_lim;
  for (int c=0; c < num_channels; c++)
    {
      kdrd_channel *chan = channels + c;
      kdu_long num, den, aln;
      kdu_coords min = render_dims.pos;
      kdu_coords max = min + render_dims.size - kdu_coords(1,1);
      
      num = chan->sampling_numerator.x;
      den = chan->sampling_denominator.x;
      aln = chan->source_alignment.x;
      aln += ((chan->boxcar_size.x-1)*den) / (2*chan->boxcar_size.x);
      min.x = long_floor_ratio(num*min.x-aln,den);
      max.x = long_ceil_ratio(num*max.x-aln,den);
      if (chan->sampling_numerator.x != chan->sampling_denominator.x)
        { min.x -= 2; max.x += 3; }
      
      num = chan->sampling_numerator.y;
      den = chan->sampling_denominator.y;
      aln = chan->source_alignment.y;
      aln += ((chan->boxcar_size.y-1)*den) / (2*chan->boxcar_size.y);
      min.y = long_floor_ratio(num*min.y-aln,den);
      max.y = long_ceil_ratio(num*max.y-aln,den);
      if (chan->sampling_numerator.y != chan->sampling_denominator.y)
        { min.y -= 2; max.y += 3; }
      
      // Convert to canvas coords
      kdu_dims chan_region;
      chan_region.pos = min;
      chan_region.size = max-min+kdu_coords(1,1);
      chan_region.pos.x *= chan->boxcar_size.x;
      chan_region.size.x *= chan->boxcar_size.x;
      chan_region.pos.y *= chan->boxcar_size.y;
      chan_region.size.y *= chan->boxcar_size.y;      
      
      kdu_coords lim;
      if (on_transformed_canvas)
        {
          kdu_coords subs;
          codestream.get_subsampling(chan->source->rel_comp_idx,subs,true);
          min = chan_region.pos;
          lim = min + chan_region.size;
          min.x *= subs.x;   min.y *= subs.y;
          lim.x *= subs.x;   lim.y *= subs.y;
        }
      else
        {
          kdu_dims canvas_region;
          codestream.map_region(chan->source->rel_comp_idx,chan_region,
                                canvas_region,true);
          min = canvas_region.pos;
          lim = min + canvas_region.size;
        }
      if ((c == 0) || (min.x < canvas_min.x))
        canvas_min.x = min.x;
      if ((c == 0) || (min.y < canvas_min.y))
        canvas_min.y = min.y;
      if ((c == 0) || (lim.x > canvas_lim.x))
        canvas_lim.x = lim.x;
      if ((c == 0) || (lim.y > canvas_lim.y))
        canvas_lim.y = lim.y;
    }
  kdu_dims result;
  result.pos = canvas_min;
  result.size = canvas_lim-canvas_min;
  return result;
}

/*****************************************************************************/
/* STATIC                       reset_line_buf                               */
/*****************************************************************************/

static void                    
  reset_line_buf(kdu_line_buf *buf)
{
  int num_samples = buf->get_width();
  if (buf->get_buf32() != NULL)
    {
      kdu_sample32 *sp = buf->get_buf32();
      if (buf->is_absolute())
        while (num_samples--)
          (sp++)->ival = 0;
      else
        while (num_samples--)
          (sp++)->fval = 0.0F;
    }
  else
    {
      kdu_sample16 *sp = buf->get_buf16();
      while (num_samples--)
        (sp++)->ival = 0;
    }
}

/*****************************************************************************/
/* STATIC               adjust_fixpoint_formatted_line                       */
/*****************************************************************************/

static void
  adjust_fixpoint_formatted_line(void *buf, int buf_min, int buf_len,
                                 int buf_line_type, bool is_signed,
                                 int fixpoint_int_bits)
  /* This function performs the post-conversion steps for fixpoint-formatted
     image component samples that are described in connection with the
     `kdu_channel_interp' function.  It is assumed that the source data has
     already been converted to a floating point representation, so
     `buf_line_type' must be `KDRD_FLOAT_TYPE'.  If `is_signed' is true, the
     only conversion required is to scale the sample values by 2^I, where I
     is the `fixpoint_int_bits' value; that way, the nominal range is expanded
     to reflect the (usually) added headroom represented by the fixed-point
     representation.  If `is_signed' is false, this scaling must be accompanied
     by an adjustment in the level shift for unsigned data, which means adding
     0.5*(2^I - 1). */
{
  assert(buf_line_type == KDRD_FLOAT_TYPE);
  float *bp = ((float *)buf) + buf_min;
  float scale, offset=0.0f;
  if (fixpoint_int_bits > 0)
    scale = (float)(1<<fixpoint_int_bits);
  else
    scale = 1.0f / (float)(1<<-fixpoint_int_bits); // Unlikely
  if (!is_signed)
    offset = 0.5f * (scale - 1.0f);
  for (; buf_len > 0; buf_len--, bp++)
    { 
      float fval = *bp;
      *bp = fval * scale + offset;
    }
}

/*****************************************************************************/
/* STATIC             convert_samples_to_palette_indices                     */
/*****************************************************************************/

static void
  convert_samples_to_palette_indices(kdu_line_buf *line, int bit_depth,
                                     bool is_signed, int palette_bits,
                                     kdu_line_buf *indices,
                                     int dst_offset)
{
  int i, width=line->get_width();
  kdu_sample16 *dp = indices->get_buf16();
  assert((dp != NULL) && indices->is_absolute() &&
         (indices->get_width() >= (width+dst_offset)));
  dp += dst_offset;
  kdu_sample16 *sp16 = line->get_buf16();
  kdu_sample32 *sp32 = line->get_buf32();
  
  if (line->is_absolute())
    { // Convert absolute source integers to absolute palette indices
      if (sp16 != NULL)
        {
          kdu_int16 offset = (kdu_int16)((is_signed)?0:((1<<bit_depth)>>1));
          kdu_int16 val, mask = ((kdu_int16)(-1))<<palette_bits;
          for (i=0; i < width; i++)
            {
              val = sp16[i].ival + offset;
              if (val & mask)
                val = (val<0)?0:(~mask);
              dp[i].ival = val;
            }
        }
      else if (sp32 != NULL)
        {
          kdu_int32 offset = (is_signed)?0:((1<<bit_depth)>>1);
          kdu_int32 val, mask = ((kdu_int32)(-1))<<palette_bits;
          for (i=0; i < width; i++)
            {
              val = sp32[i].ival + offset;
              if (val & mask)
                val = (val<0)?0:(~mask);
              dp[i].ival = (kdu_int16) val;
            }
        }
      else
        assert(0);
    }
  else
    { // Convert fixed/floating point samples to absolute palette indices
      if (sp16 != NULL)
        {
          kdu_int16 offset=(kdu_int16)((is_signed)?0:((1<<KDU_FIX_POINT)>>1));
          int downshift = KDU_FIX_POINT-palette_bits; assert(downshift > 0);
          offset += (kdu_int16)((1<<downshift)>>1);
          kdu_int16 val, mask = ((kdu_int16)(-1))<<palette_bits;
          for (i=0; i < width; i++)
            {
              val = (sp16[i].ival + offset) >> downshift;
              if (val & mask)
                val = (val<0)?0:(~mask);
              dp[i].ival = val;
            }
        }
      else if (sp32 != NULL)
        {
          float scale = (float)(1<<palette_bits);
          float offset = 0.5F + ((is_signed)?0.0F:(0.5F*scale));
          kdu_int32 val, mask = ((kdu_int32)(-1))<<palette_bits;
          for (i=0; i < width; i++)
            {
              val = (kdu_int32)((sp32[i].fval * scale) + offset);
              if (val & mask)
                val = (val<0)?0:(~mask);
              dp[i].ival = (kdu_int16) val;
            }
        }
      else
        assert(0);
    }
}

/*****************************************************************************/
/* STATIC               perform_palette_map (fix16)                          */
/*****************************************************************************/

static void
  perform_palette_map(kdu_line_buf *src, int missing_source_samples,
                      kdu_sample16 *lut, void *void_dst,
                      int dst_min, int num_samples, int dst_type)
  /* This function uses the 16-bit `src' samples as indices into the palette
     lookup table, writing the results to `dst'.  The number of output
     samples to be generated is `num_samples', but we note that some
     samples may be missing from the start and/or end of the `src' buffer,
     in which case sample replication is employed.  It can also happen that
     `missing_source_samples' is -ve, meaning that some initial samples
     from the `src' buffer are to be skipped. */
{
  assert(dst_type == KDRD_FIX16_TYPE);
  kdu_int16 *dst = ((kdu_int16 *) void_dst) + dst_min;
  int src_len = src->get_width();
  if (src_len == 0)
    { // Pathalogical case; no source data at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  kdu_int16 *sp = (kdu_int16 *) src->get_buf16();
  kdu_int16 val = sp[0];
  if (missing_source_samples < 0)
    {
      sp -= missing_source_samples;
      src_len += missing_source_samples; 
      missing_source_samples = 0;
      val = (src_len > 0)?sp[0]:(sp[src_len-1]);
    }
  val = lut[val].ival;
  if (missing_source_samples >= num_samples)
    missing_source_samples = num_samples-1;
  num_samples -= missing_source_samples;
  for (; missing_source_samples > 0; missing_source_samples--)
    *(dst++) = val;
  if (src_len > num_samples)
    src_len = num_samples;
  num_samples -= src_len;
  for (; src_len > 0; src_len--)
    *(dst++) = lut[*(sp++)].ival;
  for (val=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = val;
}

/*****************************************************************************/
/* STATIC                perform_palette_map (float)                         */
/*****************************************************************************/

static void
  perform_palette_map(kdu_line_buf *src, int missing_source_samples,
                      float *lut, void *void_dst,
                      int dst_min, int num_samples, int dst_type)
  /* Same as the fix16 version of the function, except that the palette's
     entries are floats and `void_dst' points to a floating point target
     buffer. */
{
  assert(dst_type == KDRD_FLOAT_TYPE);
  float *dst = ((float *) void_dst) + dst_min;
  int src_len = src->get_width();
  if (src_len == 0)
    { // Pathalogical case; no source data at all
      for (; num_samples > 0; num_samples--)
        *(dst++) = 0;
      return;
    }
  kdu_int16 *sp = (kdu_int16 *) src->get_buf16();
  kdu_int16 idx_val = sp[0];
  if (missing_source_samples < 0)
    {
      sp -= missing_source_samples;
      src_len += missing_source_samples; 
      missing_source_samples = 0;
      idx_val = (src_len > 0)?sp[0]:(sp[src_len-1]);
    }
  float fval = lut[idx_val];
  if (missing_source_samples >= num_samples)
    missing_source_samples = num_samples-1;
  num_samples -= missing_source_samples;
  for (; missing_source_samples > 0; missing_source_samples--)
    *(dst++) = fval;
  if (src_len > num_samples)
    src_len = num_samples;
  num_samples -= src_len;
  for (; src_len > 0; src_len--)
    *(dst++) = lut[*(sp++)];
  for (fval=dst[-1]; num_samples > 0; num_samples--)
    *(dst++) = fval;
}

/*****************************************************************************/
/* STATIC                 map_and_integrate (fix16)                          */
/*****************************************************************************/

static void
  map_and_integrate(kdu_line_buf *src, int missing_source_samples,
                    kdu_sample16 *lut, void *void_dst,
                    int dst_min, int num_cells, int dst_type,
                    int cell_width, int acc_precision, int cell_lines_left,
                    int cell_height)
  /* This function is very similar to `perform_palette_map', except for
     three things.  Firstly, the `lut' outputs may need to be downshifted to
     accommodate an `acc_precision' value which is less than KDU_FIX_POINT
     (it will never be more).  Secondly, the `lut' outputs are accumulated
     into the `dst' buffer, rather than being copied.  Finally, each set of
     `cell_width' consecutive values are added together before being
     accumulated into a single `dst' entry.  After accounting for any
     `missing_source_samples' and sufficiently replicating the last sample
     in the `src' line, the source data may be considered to consist of
     exactly `num_cells' full accumulation cells where the `dst'
     array is considered to have `num_cells' entries.
        As with `kdrc_convert_and_add_func' functions, this function takes two
     additional arguments, `cell_lines_left' and `cell_height', which are used
     to initiate and complete the accumulation process vertically.  If
     `cell_lines_left' is equal to `cell_height', the `dst' buffer
     is zeroed out prior to accumulation, while if `cell_lines_left' is
     equal to 1 on entry, the `dst' buffer is renormalized before returning.
   */
{
  assert(dst_type == KDRD_FIX16_TYPE);
  kdu_int32 *dst = ((kdu_int32 *)void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  int src_len = src->get_width();
  if (src_len == 0)
    { // Pathalogical case; no source data at all
      return;
    }
  kdu_int16 *sp = (kdu_int16 *) src->get_buf16();
  kdu_int32 val = sp[0];
  if (missing_source_samples < 0)
    {
      sp -= missing_source_samples;
      src_len += missing_source_samples; 
      missing_source_samples = 0;
      val = (src_len > 0)?sp[0]:(sp[src_len-1]);
    }
  int shift = KDU_FIX_POINT-acc_precision;
  assert(shift >= 0);
  kdu_int32 offset = (1<<shift)>>1;
  
  int needed_samples = num_cells*cell_width;
  val = (lut[val].ival + offset)>>shift;
  if (missing_source_samples >= needed_samples)
    missing_source_samples = needed_samples-1;
  needed_samples -= missing_source_samples;
  int cell_counter = cell_width;
  for (; missing_source_samples > 0; missing_source_samples--, cell_counter--)
    {
      if (cell_counter == 0)
        { dst++; cell_counter=cell_width; }
      *dst += val;
    }
  if (src_len > needed_samples)
    src_len = needed_samples;
  needed_samples -= src_len;
  if (shift == 0)
    { // Fast implementation for common case of no shifting
      for (; src_len > 0; src_len--, cell_counter--)
        {
          if (cell_counter == 0)
            { dst++; cell_counter=cell_width; }
          *dst += (val = lut[*(sp++)].ival);
        }      
    }
  else
    {
      for (; src_len > 0; src_len--, cell_counter--)
        {
          if (cell_counter == 0)
            { dst++; cell_counter=cell_width; }
          *dst += (val = (lut[*(sp++)].ival + offset)>>shift);
        }
    }
  for (; needed_samples > 0; needed_samples--, cell_counter--)
    {
      if (cell_counter == 0)
        { dst++; cell_counter=cell_width; }
      *dst += val;
    }
  
  // See if we need to finish by generating a normalized 16-bit result
  if (cell_lines_left == 1)
    { 
      kdu_int32 *sp = ((kdu_int32 *) void_dst) + dst_min;
      kdu_int16 *dp = ((kdu_int16 *) void_dst) + dst_min;
      int in_precision=acc_precision, cell_area=cell_width*cell_height;
      for (; cell_area > 1; cell_area>>=1) in_precision++;
      int shift = in_precision - KDU_FIX_POINT;
      assert(shift > 0);
      kdu_int32 offset = (1<<shift)>>1;
      for (; num_cells > 0; num_cells--)
        *(dp++) = (kdu_int16)((*(sp++) + offset) >> shift);
    }
}

/*****************************************************************************/
/* STATIC                 map_and_integrate (float)                          */
/*****************************************************************************/

static void
  map_and_integrate(kdu_line_buf *src, int missing_source_samples,
                    float *lut, void *void_dst,
                    int dst_min, int num_cells, int dst_type,
                    int cell_width, int acc_precision, int cell_lines_left,
                    int cell_height)
  /* Same as the fix16 version of the function, except that the palette's
     entries are floats and `void_dst' points to a floating point accumulation
     buffer. */
{
  assert(dst_type == KDRD_FLOAT_TYPE);
  float *dst = ((float *)void_dst) + dst_min;
  if (cell_lines_left == cell_height)
    memset(dst,0,(size_t)(num_cells<<2));
  int src_len = src->get_width();
  if (src_len == 0)
    { // Pathalogical case; no source data at all
      return;
    }
  kdu_int16 *sp = (kdu_int16 *) src->get_buf16();
  kdu_int32 idx_val = sp[0];
  if (missing_source_samples < 0)
    { 
      sp -= missing_source_samples;
      src_len += missing_source_samples; 
      missing_source_samples = 0;
      idx_val = (src_len > 0)?sp[0]:(sp[src_len-1]);
    }
  
  int needed_samples = num_cells*cell_width;
  if (missing_source_samples >= needed_samples)
    missing_source_samples = needed_samples-1;

  assert(acc_precision < 0);
  float scale = kdu_pwrof2f(acc_precision);
  float fval = scale * lut[idx_val];
  needed_samples -= missing_source_samples;
  int cell_counter = cell_width;
  for (; missing_source_samples > 0; missing_source_samples--, cell_counter--)
    {
      if (cell_counter == 0)
        { dst++; cell_counter=cell_width; }
      *dst += fval;
    }
  if (src_len > needed_samples)
    src_len = needed_samples;
  needed_samples -= src_len;
  for (; src_len > 0; src_len--, cell_counter--)
    { 
      if (cell_counter == 0)
        { dst++; cell_counter=cell_width; }
      *dst += (fval = scale * lut[*(sp++)]);
    }      
  for (; needed_samples > 0; needed_samples--, cell_counter--)
    {
      if (cell_counter == 0)
        { dst++; cell_counter=cell_width; }
      *dst += fval;
    }
}




/*****************************************************************************/
/* STATIC                 do_horz_resampling_float                           */
/*****************************************************************************/

static void
  do_horz_resampling_float(int length, kdu_line_buf *src, kdu_line_buf *dst,
                           int phase, int num, int den, int pshift,
                           int kernel_length, float *kernels[])
  /* This function implements disciplined horizontal resampling for floating
     point sample data.  The function generates `length' outputs, writing them
     into the `dst' line.
        If `kernel_length' is 6, each output is formed by taking the inner
     product between a 6-sample kernel and 6 samples from the `src' line.  The
     first output (at `dst'[0]) is formed by taking an inner product with
     the source samples `src'[-2] throught `src'[3] with (`kernels'[p])[0]
     through (`kernels'[p])[6], where p is equal to (`phase'+`off')>>`pshift'
     and `off' is equal to (1<<`pshift')/2.  After generating each sample in
     `dst', the `phase' value is incremented by `num'.  If this leaves
     `phase' >= `den', we subtract `den' from `phase' and increment the
     `src' pointer -- we may need to do this multiple times -- after which
     we are ready to generate the next sample for `dst'.
        Otherwise, `kernel_length' is 2 and each output is formed by taking
     the inner product between a 2-tap kernel and 2 samples from the `src'
     line; the first such output is formed by taking an inner product with
     the source samples `src'[0] and `src'[1].  In this special case, the
     `kernels' array actually holds 4 kernels, having lengths 2, 3, 4 and 5,
     which can be used to simultaneously form 4 successive output samples.
     The function can (but does not need to) take advantage of this, allowing
     the phase to be evaluated up to 4 times less frequently than would
     otherwise be required. */
{
  int off = (1<<pshift)>>1;
  float *sp = (float *) src->get_buf32();
  float *dp = (float *) dst->get_buf32();
  if (kernel_length == 6)
    {
      for (; length > 0; length--, dp++)
        {
          float *kern = kernels[((kdu_uint32)(phase+off))>>pshift];
          phase += num;
          *dp = sp[-2]*kern[0] + sp[-1]*kern[1] + sp[0]*kern[2] +
          sp[1]*kern[3] + sp[2]*kern[4] + sp[3]*kern[5];
          while (((kdu_uint32) phase) >= ((kdu_uint32) den))
            { phase -= den; sp++; }
        }
    }
  else
    { 
      assert(kernel_length == 2);

      // Note: we could provide targeted code fragments for various
      // special cases here.  In particular if `phase'=0 and `den'=2*`num' or
      // `den'=4*`num', the implementation can be particularly trivial.

      if (den < (INT_MAX>>2))
        { // Just to be on the safe side
          int num4 = num<<2;
          for (; length > 3; length-=4, dp+=4)
            {
              float *kern = kernels[((kdu_uint32)(phase+off))>>pshift];
              phase += num4;
              dp[0] = sp[0]*kern[0] + sp[1]*kern[1];
              dp[1] = sp[0]*kern[2] + sp[1]*kern[3] + sp[2]*kern[4];
              dp[2] = sp[0]*kern[5] + sp[1]*kern[6] + sp[2]*kern[7]
                    + sp[3]*kern[8];
              dp[3] = sp[0]*kern[9] + sp[1]*kern[10] + sp[2]*kern[11]
                    + sp[3]*kern[12]+ sp[4]*kern[13];
              while (((kdu_uint32) phase) >= ((kdu_uint32) den))
                { phase -= den; sp++; }
            }
        }
      for (; length > 0; length--, dp++)
        {
          float *kern = kernels[((kdu_uint32)(phase+off))>>pshift];
          phase += num; *dp = sp[0]*kern[0] + sp[1]*kern[1];
          if (((kdu_uint32) phase) >= ((kdu_uint32) den))
            { phase -= den; sp++; } // Can only happen once, because 2-tap
                                    // filters are only used for expansion
        }
    }
}

/*****************************************************************************/
/* STATIC                 do_horz_resampling_fix16                           */
/*****************************************************************************/

static void
  do_horz_resampling_fix16(int length, kdu_line_buf *src, kdu_line_buf *dst,
                           int phase, int  num, int den, int pshift,
                           int kernel_length, kdu_int32 *kernels[])
  /* This function is the same as `do_horz_resampling_float', except that
     it processes 16-bit integers with a nominal precision of KDU_FIX_POINT.
     The interpolation kernels in this case have a negated fixed-point
     representation with 15 fraction bits -- this means that after taking the
     integer-valued inner product, the result v must be negated and divided
     by 2^15, leaving (16384-v)>>15. */
{
  int off = (1<<pshift)>>1;
  kdu_int16 *sp = (kdu_int16 *) src->get_buf16();
  kdu_int16 *dp = (kdu_int16 *) dst->get_buf16();
  if (kernel_length == 6)
    { 
      for (; length > 0; length--, dp++)
        { 
          kdu_int32 sum, *kern = kernels[((kdu_uint32)(phase+off))>>pshift];
          phase += num;
          sum = sp[-2]*kern[0] + sp[-1]*kern[1] + sp[0]*kern[2] +
                sp[1]*kern[3] + sp[2]*kern[4] + sp[3]*kern[5];
          *dp = (kdu_int16)(((1<<14)-sum)>>15);
          while (((kdu_uint32) phase) >= ((kdu_uint32) den))
            { phase -= den; sp++; }
        }
    }
  else
    {
      assert(kernel_length == 2);
      if (den < (INT_MAX>>2))
        { // Just to be on the safe side
          int num4 = num<<2;
          if ((phase == 0) && (num4 == (den+den)))
            { // Special case of aligned interpolation by 2
              int k0=(*kernels)[2], k1=(*kernels)[3];
              for (; length > 3; length-=4, dp+=4, sp+=2)
                { 
                  dp[0] = sp[0];
                  dp[1] = (kdu_int16)(((1<<14) - sp[0]*k0 - sp[1]*k1)>>15);
                  dp[2] = sp[1];
                  dp[3] = (kdu_int16)(((1<<14) - sp[1]*k0 - sp[2]*k1)>>15);
                }
            }
          else if ((phase == 0) && (num4 == den))
            { // Special case of aligned interpolation by 4
              int k0=(*kernels)[2], k1=(*kernels)[3];
              int k2=(*kernels)[5], k3=(*kernels)[6];
              int k4=(*kernels)[9], k5=(*kernels)[10];
              for (; length > 3; length-=4, dp+=4, sp++)
                { 
                  dp[0] = sp[0];
                  dp[1] = (kdu_int16)(((1<<14) - sp[0]*k0 - sp[1]*k1)>>15);
                  dp[2] = (kdu_int16)(((1<<14) - sp[0]*k2 - sp[1]*k3)>>15);
                  dp[3] = (kdu_int16)(((1<<14) - sp[0]*k4 - sp[1]*k5)>>15);                  
                }
            }
          else
            { // General case
              for (; length > 3; length-=4, dp+=4)
                { 
                  kdu_int32 sum;
                  kdu_int32 *kern=kernels[((kdu_uint32)(phase+off))>>pshift];
                  phase += num4;
                  sum = sp[0]*kern[0] + sp[1]*kern[1];
                  dp[0] = (kdu_int16)(((1<<14)-sum)>>15);
                  sum = sp[0]*kern[2] + sp[1]*kern[3] + sp[2]*kern[4];
                  dp[1] = (kdu_int16)(((1<<14)-sum)>>15);
                  sum = sp[0]*kern[5] + sp[1]*kern[6] + sp[2]*kern[7]
                      + sp[3]*kern[8];
                  dp[2] = (kdu_int16)(((1<<14)-sum)>>15);
                  sum = sp[0]*kern[9] + sp[1]*kern[10] + sp[2]*kern[11]
                      + sp[3]*kern[12] + sp[4]*kern[13];
                  dp[3] = (kdu_int16)(((1<<14)-sum)>>15);
                  while (((kdu_uint32) phase) >= ((kdu_uint32) den))
                    { phase -= den; sp++; }
                }
            }
        }
      for (; length > 0; length--, dp++)
        {
          kdu_int32 sum, *kern = kernels[((kdu_uint32)(phase+off))>>pshift];
          phase += num; sum = sp[0]*kern[0] + sp[1]*kern[1];
          *dp = (kdu_int16)(((1<<14)-sum)>>15);
          if (((kdu_uint32) phase) >= ((kdu_uint32) den))
            { phase -= den; sp++; } // Can only happen once, because 2-tap
                                    // filters are only used for expansion
        }
    }
}

/*****************************************************************************/
/* STATIC                 do_vert_resampling_float                           */
/*****************************************************************************/

static void
  do_vert_resampling_float(int length, kdu_line_buf *src[], kdu_line_buf *dst,
                           int kernel_length, float *kernel)
  /* This function implements disciplined vertical resampling for floating
     point sample data.  The function generates `length' outputs, writing them
     into the `dst' line.  Each output is formed by taking the inner product
     between a 6-sample `kernel' and corresponding entries from each of the
     6 lines referenced from the `src' array.  If `kernel_length'=2, the
     first 2 and last 2 lines are ignored and the central lines are formed
     using only `kernel'[0] and `kernel'[1]. */
{
  if (kernel_length == 6)
    { 
      float *sp0=(float *)(src[0]->get_buf32());
      float *sp1=(float *)(src[1]->get_buf32());
      float *sp2=(float *)(src[2]->get_buf32());
      float *sp3=(float *)(src[3]->get_buf32());
      float *sp4=(float *)(src[4]->get_buf32());
      float *sp5=(float *)(src[5]->get_buf32());
      float k0=kernel[0], k1=kernel[1], k2=kernel[2],
            k3=kernel[3], k4=kernel[4], k5=kernel[5];
      float *dp = (float *)(dst->get_buf32());
      for (int n=0; n < length; n++)
        dp[n] = (sp0[n]*k0 + sp1[n]*k1 + sp2[n]*k2 +
                 sp3[n]*k3 + sp4[n]*k4 + sp5[n]*k5);
    }
  else
    {
      assert(kernel_length == 2);
      float *sp0=(float *)(src[2]->get_buf32());
      float *sp1=(float *)(src[3]->get_buf32());
      float k0=kernel[0], k1=kernel[1];
      float *dp = (float *)(dst->get_buf32());
      for (int n=0; n < length; n++)
        dp[n] = sp0[n]*k0 + sp1[n]*k1;
    }
}

/*****************************************************************************/
/* STATIC                 do_vert_resampling_fix16                           */
/*****************************************************************************/

static void
  do_vert_resampling_fix16(int length, kdu_line_buf *src[], kdu_line_buf *dst,
                           int kernel_length, kdu_int32 *kernel)
  /* This function is the same as `do_vert_resampling_float', except that
     it processes 16-bit integers with a nominal precision of KDU_FIX_POINT.
     The interpolation `kernel' in this case have a negated fixed-point
     representation with 15 fraction bits -- this means that after taking the
     integer-valued inner product, the result v must be negated and divided
     by 2^15, leaving (16384-v)>>15. */
{
  if (kernel_length == 6)
    { 
      kdu_int16 *sp0=(kdu_int16 *)(src[0]->get_buf16());
      kdu_int16 *sp1=(kdu_int16 *)(src[1]->get_buf16());
      kdu_int16 *sp2=(kdu_int16 *)(src[2]->get_buf16());
      kdu_int16 *sp3=(kdu_int16 *)(src[3]->get_buf16());
      kdu_int16 *sp4=(kdu_int16 *)(src[4]->get_buf16());
      kdu_int16 *sp5=(kdu_int16 *)(src[5]->get_buf16());
      kdu_int32 k0=kernel[0], k1=kernel[1], k2=kernel[2],
                k3=kernel[3], k4=kernel[4], k5=kernel[5];
      kdu_int16 *dp = (kdu_int16 *)(dst->get_buf16());
      for (int n=0; n < length; n++)
        { 
          kdu_int32 sum = (sp0[n]*k0 + sp1[n]*k1 + sp2[n]*k2 +
                           sp3[n]*k3 + sp4[n]*k4 + sp5[n]*k5);
          dp[n] = ((1<<14)-sum)>>15;
        }
    }
  else
    {
      assert(kernel_length == 2);
      kdu_int16 *sp0=(kdu_int16 *)(src[2]->get_buf16());
      kdu_int16 *sp1=(kdu_int16 *)(src[3]->get_buf16());
      kdu_int32 k0=kernel[0], k1=kernel[1];
      kdu_int16 *dp = (kdu_int16 *)(dst->get_buf16());
      if (k1 == 0)
        memcpy(dp,sp0,(size_t)(length<<1));
      else if (k0 == 0)
        memcpy(dp,sp1,(size_t)(length<<1));
      else if (k0 == k1)
        for (int n=0; n < length; n++)
          dp[n] = (kdu_int16)((((int) sp0[n]) + ((int) sp1[n]) + 1) >> 1);
      else
        for (int n=0; n < length; n++)
          { 
            kdu_int32 sum = sp0[n]*k0 + sp1[n]*k1;
            dp[n] = ((1<<14)-sum)>>15;
          }
    }
}


/* ========================================================================= */
/*                            kdrd_interp_kernels                            */
/* ========================================================================= */

#ifndef M_PI
#  define M_PI 3.1415926535
#endif

/*****************************************************************************/
/*                         kdrd_interp_kernels::init                         */
/*****************************************************************************/

void
  kdrd_interp_kernels::init(float expansion_factor, float max_overshoot,
                            float zero_overshoot_threshold)
{
  if (max_overshoot < 0.0F)
    max_overshoot = 0.0F;
  assert(expansion_factor > 0.0F);
  int kernel_len = 6;
  if (expansion_factor > 1.0F)
    { 
      if ((max_overshoot == 0.0F) ||
          (expansion_factor >= zero_overshoot_threshold))
        { max_overshoot = 0.0F; kernel_len = 2; }
      else
        max_overshoot*=(expansion_factor-1.0F)/(zero_overshoot_threshold-1.0F);
    }
  if ((expansion_factor == this->target_expansion_factor) &&
      (max_overshoot == this->derived_max_overshoot) &&
      (kernel_len == this->kernel_length))
    return;
  this->target_expansion_factor = expansion_factor;
  this->derived_max_overshoot = max_overshoot;
  this->simd_kernel_type = KDRD_SIMD_KERNEL_NONE;
  this->kernel_length = kernel_len;
  this->kernel_coeffs = (kernel_len == 2)?14:kernel_len;
  float bw = (expansion_factor < 1.0F)?expansion_factor:1.0F;

  // Generate a valid `rate' in exactly the same manner that is used by
  // `get_simd_kernel'.
  double rate;
  if (target_expansion_factor <= 0.0F)
    { assert(0); rate = 2.99; }
  else
    rate = 1.0 / target_expansion_factor;
  if (rate >= 3.0)
    { assert(0); rate = 2.99; }
  
  // Start by generating the floating-point kernels
  int k, n;
  float *kernel = float_kernels;
  if (kernel_length == 2)
    { // Special case, setting up 2-tap kernels with horizontal extension
      for (k=0; k < 33; k++, kernel+=KDRD_INTERP_KERNEL_STRIDE)
        { 
          float x, sigma = k * (1.0F/32.0F);
          int ncoeffs=2, lim_n=2;
          for (n=0; ncoeffs <= 5; ncoeffs++, lim_n+=ncoeffs,
               sigma += (float)rate)
            {
              for (x=sigma; x > 1.0F; n++, x-=1.0F)
                kernel[n] = 0.0F;
              kernel[n++] = 1.0F - x;
              kernel[n++] = x;
              for (; n < lim_n; n++)
                kernel[n] = 0.0F;
            }
          assert((n <= KDRD_INTERP_KERNEL_STRIDE) && (n == kernel_coeffs));
        }
    }
  else
    { // Regular case, setting up 6-tap kernels
      assert(kernel_length == 6);
      // Generate the first half of the floating point kernels first
      for (k=0; k <= 16; k++, kernel+=KDRD_INTERP_KERNEL_STRIDE)
        {
          float gain=0.0F, sigma = k * (1.0F/32.0F);
          for (n=0; n < 6; n++)
            {
              double x = (n-2.0F-sigma)*M_PI;
              if ((x > -0.0001) && (x < 0.0001))
                kernel[n] = (float) bw;
              else
                kernel[n] = (float)(sin(bw*x)/x);
              kernel[n] *= 1.0F + (float) cos(x * 1.0/3.0);
              gain += kernel[n];
            }
          gain = 1.0F / gain;
          float step_overshoot = 0.0F, ovs_acc=0.0F;
          for (n=0; n < 6; n++)
            { 
              kernel[n] *= gain;
              ovs_acc += kernel[n];
              if (ovs_acc < -step_overshoot)
                step_overshoot = -ovs_acc;
              else if (ovs_acc > (1.0F+step_overshoot))
                step_overshoot = ovs_acc - 1.0F;
            }
          if (step_overshoot > max_overshoot)
            { // Form a weighted average of the windowed sinc kernel and the
              // 2-tap kernel whose coefficients are all positive (has no step
              // overshoot).
              float frac = max_overshoot / step_overshoot; // Fraction to keep
              for (n=0; n < 6; n++)
                kernel[n] *= frac;
              kernel[2] += (1.0F-frac)*(1.0F-sigma);
              kernel[3] += (1.0F-frac)*sigma;
              for (step_overshoot=0.0F, ovs_acc=0.0F, n=0; n < 6; n++)
                {
                  ovs_acc += kernel[n];
                  if (ovs_acc < -step_overshoot)
                    step_overshoot = -ovs_acc;
                  else if (ovs_acc > (1.0F+step_overshoot))
                    step_overshoot = ovs_acc - 1.0F;
                }
              assert((step_overshoot < (max_overshoot+0.001F)) &&
                     (step_overshoot > (max_overshoot-0.001F)));
            }
        }
  
      // Now generate the second half of the floating point kernels by mirror
      // imaging the first half
      float *ref_kernel = kernel-2*KDRD_INTERP_KERNEL_STRIDE+kernel_length-1;
      for (; k <= 32; k++, kernel+=KDRD_INTERP_KERNEL_STRIDE,
           ref_kernel-=KDRD_INTERP_KERNEL_STRIDE)
        for (n=0; n < kernel_length; n++)
          kernel[n] = ref_kernel[-n];
    }
  
  // Now generate the fixed-point kernels
  kernel = float_kernels;
  kdu_int32 *kernel16 = fix16_kernels;
  for (k=0; k < 33; k++,
       kernel+=KDRD_INTERP_KERNEL_STRIDE, kernel16+=KDRD_INTERP_KERNEL_STRIDE)
    for (n=0; n < kernel_coeffs; n++)
      kernel16[n] = -((kdu_int32) floor(0.5 + kernel[n]*(1<<15)));
  
  // Finally initialize the SIMD machinery, if any
#ifdef KDU_SIMD_OPTIMIZATIONS
  simd_kernels_initialized = 0;
  simd_horz_leadin = simd_kernel_length = 0;
  
  simd_horz_float_blend_vecs = 0;
  simd_horz_fix16_blend_vecs = 0;
  simd_horz_float_vector_length = 0;
  simd_horz_fix16_vector_length = 0;
  simd_horz_float_blend_elt_size = 0;
  simd_horz_fix16_blend_halves = 0;
  simd_horz_float_kernel_leadin = 0;
  simd_horz_fix16_kernel_leadin = 0;
  simd_horz_float_kernel_length = 0;
  simd_horz_fix16_kernel_length = 0;
  simd_horz_float_kernel_stride32 = 0;
  simd_horz_fix16_kernel_stride32 = 0;
  simd_horz_float_func = NULL;
  simd_horz_fix16_func = NULL;
  KDRD_SET_SIMD_HORZ_FLOAT_RESAMPLE_FUNC(kernel_length,expansion_factor,
                                         simd_horz_float_func,
                                         simd_horz_float_vector_length,
                                         simd_horz_float_blend_vecs,
                                         simd_horz_float_blend_elt_size);
  KDRD_SET_SIMD_HORZ_FIX16_RESAMPLE_FUNC(kernel_length,expansion_factor,
                                         simd_horz_fix16_func,
                                         simd_horz_fix16_vector_length,
                                         simd_horz_fix16_blend_vecs,
                                         simd_horz_fix16_blend_halves);
  
  simd_vert_float_vector_length = 0;
  simd_vert_fix16_vector_length = 0;
  simd_vert_float_func = NULL;
  simd_vert_fix16_func = NULL;
  KDRD_SET_SIMD_VERT_FLOAT_RESAMPLE_FUNC(kernel_length,
                                         simd_vert_float_func,
                                         simd_vert_float_vector_length);
  KDRD_SET_SIMD_VERT_FIX16_RESAMPLE_FUNC(kernel_length,
                                         simd_vert_fix16_func,
                                         simd_vert_fix16_vector_length);
  
  // Work out the `simd_horz_xxxx_kernel_yyyy' values now, where "yyyy" stands
  // for "leadin", "length" and "stride32", based on the information we have
  // received from the above macros.  If the kernels turn out to be too large
  // to accommodate within the space we have set aside for SIMD kernels, we
  // must force the corresponding function pointer to NULL.
  if (simd_horz_float_func != NULL)
    { 
      int vec_len = simd_horz_float_vector_length;
      int kernel_stride32 = 0; // Worked out below, measured in 32-bit dwords
      assert((vec_len > 0) && (vec_len <= 8));
      if (simd_horz_float_blend_vecs == 0)
        { // Regular convolution implementation
          if (kernel_length == 2)
            { 
              assert(rate < 1.0);
              simd_horz_float_kernel_leadin = 0;
              simd_horz_float_kernel_length = 3 + (int)(rate * (vec_len-1));
            }
          else if (rate < 1.0)
            { 
              assert(kernel_length == 6);
              simd_horz_float_kernel_leadin = 3+(int)((1.0-rate)*(vec_len-1));
              simd_horz_float_kernel_length = simd_horz_float_kernel_leadin+4;
            }
          else
            { 
              assert(kernel_length == 6);
              simd_horz_float_kernel_leadin = 2;
              simd_horz_float_kernel_length = 7+(int)((rate-1.0)*(vec_len-1));
            }
          kernel_stride32 = simd_horz_float_kernel_length * vec_len;
        }
      else
        { // Shuffle-based implementation
          simd_horz_float_kernel_leadin = 0;
          simd_horz_float_kernel_length = kernel_length;
          int bv = simd_horz_float_blend_vecs;
          kernel_stride32 = simd_horz_float_kernel_length * vec_len * (bv+1);
        }
      kernel_stride32 += (8 - kernel_stride32) & 7; // Multiple of 8 dwords
      if (kernel_stride32 > KDRD_MAX_SIMD_KERNEL_DWORDS)
        simd_horz_float_func = NULL; // Insufficient space to support function
      else
        simd_horz_float_kernel_stride32 = kernel_stride32;
    }
  if (simd_horz_fix16_func != NULL)
    { 
      int vec_len = simd_horz_fix16_vector_length;
      int kernel_stride32 = 0; // Worked out below, measured in 32-bit dwords
      assert((vec_len > 0) && (vec_len <= 16));
      if (simd_horz_fix16_blend_vecs == 0)
        { // Regular convolution implementation
          if (kernel_length == 2)
            { 
              assert(rate < 1.0);
              simd_horz_fix16_kernel_leadin = 0;
              simd_horz_fix16_kernel_length = 3 + (int)(rate * (vec_len-1));
            }
          else if (rate < 1.0)
            { 
              assert(kernel_length == 6);
              simd_horz_fix16_kernel_leadin = 3+(int)((1.0-rate)*(vec_len-1));
              simd_horz_fix16_kernel_length = simd_horz_fix16_kernel_leadin+4;
            }
          else
            { 
              assert(kernel_length == 6);
              simd_horz_fix16_kernel_leadin = 2;
              simd_horz_fix16_kernel_length = 7+(int)((rate-1.0)*(vec_len-1));
            }
          kernel_stride32 = (simd_horz_fix16_kernel_length * vec_len + 1) >> 1;
        }
      else if (!simd_horz_fix16_blend_halves)
        { // Shuffle-based implementation with full vectors
          simd_horz_fix16_kernel_leadin = 0;
          simd_horz_fix16_kernel_length = kernel_length;
          int bv = simd_horz_fix16_blend_vecs;
          kernel_stride32=(simd_horz_fix16_kernel_length*vec_len*(bv+1)+1)>>1;
        }
      else
        { // Shuffle-based implementation with half-sized input vectors
          // supplied to permutation operations, but only one set of blend
          // vectors required per kernel, rather thane one for each
          // kernel tap.
          simd_horz_fix16_kernel_leadin = 0;
          simd_horz_fix16_kernel_length = kernel_length;
          int bv = simd_horz_fix16_blend_vecs;
          kernel_stride32 = (vec_len*(simd_horz_fix16_kernel_length+bv)+1)>>1;
        }
      kernel_stride32 += (8 - kernel_stride32) & 7; // Multiple of 8 dwords
      if (kernel_stride32 > KDRD_MAX_SIMD_KERNEL_DWORDS)
        simd_horz_fix16_func = NULL; // Insufficient space to support function
      else
        simd_horz_fix16_kernel_stride32 = kernel_stride32;
    }  
#endif // KDU_SIMD_OPTIMIZATIONS
}

/*****************************************************************************/
/*                         kdrd_interp_kernels::copy                         */
/*****************************************************************************/

bool
  kdrd_interp_kernels::copy(kdrd_interp_kernels &src, float expansion_factor,
                            float max_overshoot,
                            float zero_overshoot_threshold)
{
  if (max_overshoot < 0.0F)
    max_overshoot = 0.0F;
  assert(expansion_factor > 0.0F);
  int kernel_len=6;
  if (expansion_factor > 1.0F)
    { 
      if ((max_overshoot == 0.0F) ||
          (expansion_factor >= zero_overshoot_threshold))
        { max_overshoot = 0.0F; kernel_length = 2; }
      else
        max_overshoot*=(expansion_factor-1.0F)/(zero_overshoot_threshold-1.0F);
    }
  if ((expansion_factor == this->target_expansion_factor) &&
      (max_overshoot == this->derived_max_overshoot) &&
      (kernel_len == this->kernel_length))
    return true;
  if ((max_overshoot < (0.95F*src.derived_max_overshoot)) ||
      (max_overshoot > (1.05F*src.derived_max_overshoot)))
    return false;
  if ((src.target_expansion_factor < (0.95F*src.target_expansion_factor)) ||
      (src.target_expansion_factor > (1.05F*src.target_expansion_factor)))
    return false;
  if (src.kernel_length != kernel_len)
    return false; // Kernels for two-tap expansion are in a special form
  this->target_expansion_factor = expansion_factor;
  this->derived_max_overshoot = src.derived_max_overshoot;
  memcpy(this->float_kernels,src.float_kernels,
         sizeof(float)*33*KDRD_INTERP_KERNEL_STRIDE);
  memcpy(this->fix16_kernels,src.fix16_kernels,
         sizeof(kdu_int32)*33*KDRD_INTERP_KERNEL_STRIDE);
  this->kernel_length = src.kernel_length;
  this->kernel_coeffs = src.kernel_coeffs;
  this->simd_kernel_type = KDRD_SIMD_KERNEL_NONE;
#ifdef KDU_SIMD_OPTIMIZATIONS
  simd_kernels_initialized = 0;
  simd_horz_leadin = simd_kernel_length = 0; // These need to be recreated
  simd_horz_float_blend_vecs = src.simd_horz_float_blend_vecs;
  simd_horz_fix16_blend_vecs = src.simd_horz_fix16_blend_vecs;
  simd_horz_float_vector_length = src.simd_horz_float_vector_length;
  simd_horz_fix16_vector_length = src.simd_horz_fix16_vector_length;
  simd_horz_float_blend_elt_size = src.simd_horz_float_blend_elt_size;
  simd_horz_fix16_blend_halves = src.simd_horz_fix16_blend_halves;
  simd_horz_float_kernel_leadin = src.simd_horz_float_kernel_leadin;
  simd_horz_fix16_kernel_leadin = src.simd_horz_fix16_kernel_leadin;
  simd_horz_float_kernel_length = src.simd_horz_float_kernel_length;
  simd_horz_fix16_kernel_length = src.simd_horz_fix16_kernel_length;
  simd_horz_float_kernel_stride32 = src.simd_horz_float_kernel_stride32;
  simd_horz_fix16_kernel_stride32 = src.simd_horz_fix16_kernel_stride32;
  simd_horz_float_func = src.simd_horz_float_func;
  simd_horz_fix16_func = src.simd_horz_fix16_func;
  simd_vert_float_vector_length = src.simd_vert_float_vector_length;
  simd_vert_fix16_vector_length = src.simd_vert_fix16_vector_length;
  simd_vert_float_func = src.simd_vert_float_func;
  simd_vert_fix16_func = src.simd_vert_fix16_func;
#endif // KDU_SIMD_OPTIMIZATIONS
  return true;
}

/*****************************************************************************/
/*                   kdrd_interp_kernels::get_simd_kernel                    */
/*****************************************************************************/
#ifdef KDU_SIMD_OPTIMIZATIONS
void *
  kdrd_interp_kernels::get_simd_kernel(int type, int which)
{
  int n;
  double rate;
  if (target_expansion_factor <= 0.0F)
    { assert(0); rate = 2.99; }
  else
    rate = 1.0 / target_expansion_factor;
  if (rate >= 3.0)
    { assert(0); rate = 2.99; }
  assert(type != KDRD_SIMD_KERNEL_NONE);
  if (type != this->simd_kernel_type)
    { 
      simd_kernel_type = type;
      simd_kernels_initialized = 0;
    }
  if (simd_kernels_initialized == 0)
    { 
      int kernel_stride32=0; // Will be number of dwords required per kernel
      if (type == KDRD_SIMD_KERNEL_VERT_FLOATS)
        { 
          simd_horz_leadin = 0;
          simd_kernel_length = kernel_length;
          kernel_stride32 = simd_kernel_length*simd_vert_float_vector_length;
        }
      else if (type == KDRD_SIMD_KERNEL_VERT_FIX16)
        { 
          simd_horz_leadin = 0;
          simd_kernel_length = kernel_length;
          kernel_stride32 = simd_kernel_length*simd_vert_fix16_vector_length;          
        }
      else if (type == KDRD_SIMD_KERNEL_HORZ_FLOATS)
        { // Work out the SIMD kernel length and leadin values based on the
          // vector length, the resampling rate, whether or not `blend_vecs'
          // is non-zero, and whether or not blend vectors use half-length
          // source vectors.
          if (simd_horz_float_func == NULL)
            return NULL;
          simd_horz_leadin = simd_horz_float_kernel_leadin;
          simd_kernel_length = simd_horz_float_kernel_length;
          kernel_stride32 = simd_horz_float_kernel_stride32;
        }
      else if (type == KDRD_SIMD_KERNEL_HORZ_FIX16)
        { // As above, but for fixed-point vectors with V <= 16 assumed
          if (simd_horz_fix16_func == NULL)
            return NULL;
          simd_horz_leadin = simd_horz_fix16_kernel_leadin;
          simd_kernel_length = simd_horz_fix16_kernel_length;
          kernel_stride32 = simd_horz_fix16_kernel_stride32;
        }
      else
        assert(0);
      assert(kernel_stride32 <= KDRD_MAX_SIMD_KERNEL_DWORDS); // Was checked in
                                                              // `init'.
      
      kdu_int32 *storage = this->simd_block;
      kdu_int32 addr = _addr_to_kdu_int32(storage);
      storage += (8 - (addr >> 2)) & 7; // Align on 32-byte boundary
      assert((_addr_to_kdu_int32(storage) & 31) == 0);
      for (n=0; n < 33; n++, storage += kernel_stride32)
        simd_kernels[n] = storage;
    }
  
  if ((simd_kernels_initialized >> which) & 1)
    return simd_kernels[which]; // Already initialized

  if (type == KDRD_SIMD_KERNEL_VERT_FLOATS)
    { 
      int vec_len = simd_vert_float_vector_length;
      float *dp = (float *) simd_kernels[which];
      float *sp = float_kernels + KDRD_INTERP_KERNEL_STRIDE*which;
      for (n=0; n < kernel_length; n++, dp+=vec_len)
        for (int v=0; v < vec_len; v++)
          dp[v] = sp[n];
    }
  else if (type == KDRD_SIMD_KERNEL_VERT_FIX16)
    { 
      int vec_len = simd_vert_fix16_vector_length;
      kdu_int16 *dp = (kdu_int16 *) simd_kernels[which];
      kdu_int32 *sp = fix16_kernels + KDRD_INTERP_KERNEL_STRIDE*which;
      for (n=0; n < kernel_length; n++, dp+=vec_len)
        { 
          kdu_int16 val = (kdu_int16) sp[n];
          assert(sp[n] == (kdu_int32) val);
          for (int v=0; v < vec_len; v++)
            dp[v] = val;
        }
    }
  else if (type == KDRD_SIMD_KERNEL_HORZ_FLOATS)
    { 
      double real_pos = which*(1.0/32.0);
      int vec_len = simd_horz_float_vector_length;
      if (simd_horz_float_blend_vecs == 0)
        { // Regular convolution kernel
          float *dpp = (float *) simd_kernels[which];
          int m, offset, k=which;
          if (kernel_length == 6)
            { 
              offset = simd_horz_leadin-2;
              real_pos += offset;
              for (m=0; m < vec_len; m++, real_pos+=rate-1.0, dpp++)
                { 
                  if (m > 0)
                    { 
                      offset = (int) real_pos;
                      k = (int)((real_pos - offset)*32.0 + 0.5);
                    }
                  assert((offset >= 0) && (offset <= (simd_kernel_length-6)) &&
                         (k >= 0) && (k <= 32));
                  float *dp=dpp;
                  float *sp = float_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
                  for (n=0; n < offset; n++, dp+=vec_len)
                    *dp = 0.0F;
                  for (k=0; k < 6; k++, dp+=vec_len)
                    *dp = sp[k];
                  for (n+=6; n < simd_kernel_length; n++, dp+=vec_len)
                    *dp = 0.0F;
                }
            }
          else
            { 
              assert((kernel_length == 2) && (rate < 1.0));
              offset = 0;
              for (m=0; m < vec_len; m++, real_pos+=rate, dpp++)
                { 
                  if (m > 0)
                    { 
                      offset = (int) real_pos;
                      k = (int)((real_pos-offset)*32.0 + 0.5);
                    }
                  float *dp=dpp;
                  float *sp = float_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
                  for (n=0; n < offset; n++, dp+=vec_len)
                    *dp = 0.0F;
                  for (k=0; k < 2; k++, dp+=vec_len)
                    *dp = sp[k];
                  for (n+=2; n < simd_kernel_length; n++, dp+=vec_len)
                    *dp = 0.0F;
                  assert(n == simd_kernel_length);
                }
            }
        }
      else
        { // Shuffle based floating-point kernel
          int offset=0, k=which;
          int blend_vecs = simd_horz_float_blend_vecs;
          float *factors = (float *) simd_kernels[which];
          kdu_byte *shuf = (kdu_byte *)(factors + kernel_length*vec_len);
          for (n=0; n < vec_len; n++, real_pos+=rate)
            { // Derive the kernel parameters for the n'th output sample of the
              // vector.
              if (n > 0)
                { 
                  offset = (int) real_pos;
                  k = (int)((real_pos - offset)*32.0 + 0.5);
                }
              assert((offset >= 0) && (k >= 0) && (k <= 32));
              float *sp = float_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
              if (simd_horz_float_blend_elt_size == 1)
                { // Byte oriented permutation
                  kdu_byte *shuf_p = shuf + 4*n;
                  for (int p=0; p < kernel_length; p++)
                    { 
                      factors[n+vec_len*p] = sp[p];
                      int input_idx = offset+p;
                      for (int b=0; b < blend_vecs; b++,
                           input_idx-=vec_len, shuf_p+=4*vec_len)
                        { 
                          if ((input_idx < 0) || (input_idx >= vec_len))
                            { // Input vector b non-contributing
                              shuf_p[0]=shuf_p[1]=shuf_p[2]=shuf_p[3] = 128;
                            }
                          else
                            { 
                              shuf_p[0] = 4*input_idx + 0;
                              shuf_p[1] = 4*input_idx + 1;
                              shuf_p[2] = 4*input_idx + 2;
                              shuf_p[3] = 4*input_idx + 3;
                            }
                        }
                      assert(input_idx < 0); // else `blendvecs' was too small
                    }
                }
              else if (simd_horz_float_blend_elt_size == 4)
                { // Float oriented permutation
                  kdu_int32 *shuf_d = ((kdu_int32 *)shuf) + n;
                  for (int p=0; p < kernel_length; p++)
                    { 
                      factors[n+vec_len*p] = sp[p];
                      int input_idx = offset+p;
                      for (int b=0; b < blend_vecs; b++,
                           input_idx-=vec_len, shuf_d+=vec_len)
                        { 
                          if ((input_idx < 0) || (input_idx >= vec_len))
                            shuf_d[0] = 0x80808080; // Non contributing
                          else
                            shuf_d[0] = input_idx;
                        }
                      assert(input_idx < 0); // else `blendvecs' was too small
                    }
                }
              else
                assert(0);
            }
        }
    }
  else
    { // 16-bit horizontal resampling
      double real_pos = which*(1.0/32.0);
      int vec_len = simd_horz_fix16_vector_length;
      if (simd_horz_fix16_blend_vecs == 0)
        { // Regular convolution kernel
          kdu_int16 *dpp = (kdu_int16 *) simd_kernels[which];
          int m, offset, k=which;
          if (kernel_length == 6)
            { 
              offset = simd_horz_leadin-2;
              real_pos += offset;
              for (m=0; m < vec_len; m++, real_pos+=rate-1.0, dpp++)
                { 
                  if (m > 0)
                    { 
                      offset = (int) real_pos;
                      k = (int)((real_pos - offset)*32.0 + 0.5);
                    }
                  assert((offset >= 0) && (offset <= (simd_kernel_length-6)) &&
                         (k >= 0) && (k <= 32));
                  kdu_int32 *sp = fix16_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
                  kdu_int16 *dp=dpp;
                  for (n=0; n < offset; n++, dp+=vec_len)
                    *dp = 0;
                  for (k=0; k < 6; k++, dp+=vec_len)
                    *dp = (kdu_int16) sp[k];
                  for (n+=6; n < simd_kernel_length; n++, dp+=vec_len)
                    *dp = 0;
                }
            }
          else
            { 
              assert((kernel_length == 2) && (rate < 1.0));
              offset = 0;
              for (m=0; m < vec_len; m++, real_pos+=rate, dpp++)
                { 
                  if (m > 0)
                    { 
                      offset = (int) real_pos;
                      k = (int)((real_pos - offset)*32.0 + 0.5);
                    }
                  kdu_int32 *sp = fix16_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
                  kdu_int16 *dp=dpp;
                  for (n=0; n < offset; n++, dp+=vec_len)
                    *dp = 0;
                  for (k=0; k < 2; k++, dp+=vec_len)
                    *dp = (kdu_int16) sp[k];
                  for (n+=2; n < simd_kernel_length; n++, dp+=vec_len)
                    *dp = 0;
                }
            }
        }
      else
        { // Shuffle based 16-bit fixed-point kernel
          int offset=0, k=which;
          int blend_vecs = simd_horz_fix16_blend_vecs;
          kdu_int16 *factors = (kdu_int16 *) simd_kernels[which];
          kdu_byte *shuf = (kdu_byte *)(factors + kernel_length*vec_len);
          for (n=0; n < vec_len; n++, real_pos+=rate)
            { // Derive the kernel parameters for the n'th output sample of the
              // vector.
              if (n > 0)
                { 
                  offset = (int) real_pos;
                  k = (int)((real_pos - offset)*32.0 + 0.5);
                }
              assert((offset >= 0) && (k >= 0) && (k <= 32));
              kdu_int32 *sp = fix16_kernels + KDRD_INTERP_KERNEL_STRIDE*k;
              kdu_byte *shuf_p = shuf + 2*n;
              if (!simd_horz_fix16_blend_halves)
                { // Generate a full set of shuffle (blend) vectors, each
                  // operating on full length source vectors
                  for (int p=0; p < kernel_length; p++)
                    { 
                      assert((sp[p] >= -0x00008000) && (sp[p] < 0x00007FFF));
                      factors[n+vec_len*p] = (kdu_int16) sp[p];                  
                      int input_idx = offset+p;
                      for (int b=0; b < blend_vecs; b++,
                           input_idx-=vec_len, shuf_p+=2*vec_len)
                        { 
                          if ((input_idx < 0) || (input_idx >= vec_len))
                            { // Input vector b non-contributing
                              shuf_p[0] = shuf_p[1] = 128;
                            }
                          else
                            { 
                              shuf_p[0] = 2*input_idx + 0;
                              shuf_p[1] = 2*input_idx + 1;
                            }
                        }
                      assert(input_idx < 0); // else `blendvecs' was too small
                    }
                }
              else
                { // Generate only one set of shuffle (blend) vectors, each
                  // operating on half-length source vectors
                  for (int p=0; p < kernel_length; p++)
                    { 
                      assert((sp[p] >= -0x00008000) && (sp[p] < 0x00007FFF));
                      factors[n+vec_len*p] = (kdu_int16) sp[p];                  
                    }
                  int input_idx = offset;
                  int half_vec_len = vec_len>>1;
                  for (int b=0; b < blend_vecs; b++,
                       input_idx-=half_vec_len, shuf_p+=2*vec_len)
                    { 
                      if ((input_idx < 0) || (input_idx >= half_vec_len))
                        { // Input vector b non-contributing
                          shuf_p[0] = shuf_p[1] = 128;
                        }
                      else
                        { 
                          shuf_p[0] = 2*input_idx + 0;
                          shuf_p[1] = 2*input_idx + 1;
                        }                      
                    }
                  assert(input_idx < 0); // else `blendvecs' was too small
                }
            }
        }
    }
  
  simd_kernels_initialized |= ((kdu_int64) 1) << which;
  return simd_kernels[which];
}
#endif // KDU_SIMD_OPTIMIZATIONS


/* ========================================================================= */
/*                            kdu_channel_interp                             */
/* ========================================================================= */

/*****************************************************************************/
/*                         kdu_channel_interp::init                          */
/*****************************************************************************/

bool kdu_channel_interp::init(int original_precision, bool original_signed,
                              float zeta_val, int data_format,
                              const int *format_params)
{
  if (original_precision <= 0)
    original_precision = 1; // Just in case
  
  // Start by initializing as if the format were default, even if it is not,
  // as a fallback in case something goes wrong.
  if (zeta_val < 0.0f)
    zeta_val = 0.0f;
  else if (zeta_val > 0.75f)
    zeta_val = 0.75f;
  
  this->orig_prec = original_precision;
  this->orig_signed = original_signed;
  this->zeta = zeta_val;
  this->float_exp_bits = 0;
  this->fixpoint_int_bits = 0;
  float one_lsb = kdu_pwrof2f(-orig_prec);
  normalized_max = 0.5f - one_lsb;
  normalized_zero = (orig_signed)?0.0f:-0.5f;
  normalized_natural_zero = (orig_signed)?0.0f:(zeta_val-0.5f);
  if (normalized_natural_zero > (normalized_max-one_lsb))
    normalized_natural_zero = normalized_max-one_lsb;
  if (normalized_natural_zero < normalized_zero)
    normalized_natural_zero = normalized_zero;
  
  if (data_format == JP2_CHANNEL_FORMAT_DEFAULT)
    return true;
  else if ((data_format == JP2_CHANNEL_FORMAT_FIXPOINT) &&
           (format_params != NULL))
    { // Conversion steps are expected, such that the normalized_... quantities
      // are almost identical.
      this->fixpoint_int_bits = format_params[0];
      normalized_max = 0.5f;
      return true;
    }
  else if ((data_format == JP2_CHANNEL_FORMAT_FLOAT) &&
           (format_params != NULL))
    { // Conversion steps are expected, such that the normalized_... quantities
      // are almost identical.
      int exp_bits = format_params[0];
      if ((exp_bits >= orig_prec) || (exp_bits <= 0))
        return false; // Cannot process format, leave default initialization
      this->float_exp_bits = exp_bits;
      normalized_max = 0.5f;
      return true;
    }
  
  // If we get here, the format was not understood, but we have a default
  // initialization anyway.
  return false;
}


/* ========================================================================= */
/*                           kdu_channel_mapping                             */
/* ========================================================================= */

/*****************************************************************************/
/*                 kdu_channel_mapping::kdu_channel_mapping                  */
/*****************************************************************************/

kdu_channel_mapping::kdu_channel_mapping()
{
  num_channels = 0;
  source_components = NULL;
  default_rendering_precision = NULL;
  default_rendering_signed = NULL;
  channel_interp = NULL;
  fix16_palette = NULL;
  float_palette = NULL;
  clear();
}

/*****************************************************************************/
/*                        kdu_channel_mapping::clear                         */
/*****************************************************************************/

void
  kdu_channel_mapping::clear()
{
  
  if (source_components != NULL)
    delete[] source_components;
  source_components = NULL;
  if (default_rendering_precision != NULL)
    delete[] default_rendering_precision;
  default_rendering_precision = NULL;
  if (default_rendering_signed != NULL)
    delete[] default_rendering_signed;
  default_rendering_signed = NULL;
  if (channel_interp != NULL)
    delete[] channel_interp;
  channel_interp = NULL;
  if (fix16_palette != NULL)
    { 
      for (int c=0; c < num_channels; c++)
        if (fix16_palette[c] != NULL)
          delete[] fix16_palette[c];
      delete[] fix16_palette;
    }
  fix16_palette = NULL;
  if (float_palette != NULL)
    { 
      for (int c=0; c < num_channels; c++)
        if (float_palette[c] != NULL)
          delete[] float_palette[c];
      delete[] float_palette;
    }
  float_palette = NULL;
  num_channels = 0;
  num_colour_channels = 0;
  palette_bits = 0;
  colour_converter.clear();
}

/*****************************************************************************/
/*                   kdu_channel_mapping::set_num_channels                   */
/*****************************************************************************/

void
  kdu_channel_mapping::set_num_channels(int num)
{
  assert(num >= 0);
  if (num_channels >= num)
    {
      num_channels = num;
      return;
    }
  
  int *new_comps = new int[num];
  int *new_precs = new int[num];
  bool *new_signed = new bool[num];
  kdu_channel_interp *new_interp = new kdu_channel_interp[num];
  int c = 0;
  if (source_components != NULL)
    { 
      for (c=0; (c < num_channels) && (c < num); c++)
        { 
          new_comps[c] = source_components[c];
          new_precs[c] = default_rendering_precision[c];
          new_signed[c] = default_rendering_signed[c];
          new_interp[c] = channel_interp[c];
        }
      delete[] source_components;
      source_components = NULL;
      delete[] default_rendering_precision;
      default_rendering_precision = NULL;
      delete[] default_rendering_signed;
      default_rendering_signed = NULL;
      delete[] channel_interp;
      channel_interp = NULL;
    }
  source_components = new_comps;
  default_rendering_precision = new_precs;
  default_rendering_signed = new_signed;
  channel_interp = new_interp;
  for (; c < num; c++)
    { 
      source_components[c] = -1;
      default_rendering_precision[c] = 8;
      default_rendering_signed[c] = false;
      channel_interp[c].init(8,0,0.0f); // Initialize with something at least.
    }

  kdu_sample16 **new_fix16_palette = new kdu_sample16 *[num];
  memset(new_fix16_palette,0,sizeof(kdu_sample16 *)*(size_t)num);
  if (fix16_palette != NULL)
    { 
      for (c=0; (c < num_channels) && (c < num); c++)
        new_fix16_palette[c] = fix16_palette[c];
      for (int d=c; d < num_channels; d++)
        if (fix16_palette[d] != NULL)
          delete[] fix16_palette[d];
      delete[] fix16_palette;
    }
  fix16_palette = new_fix16_palette;

  float **new_float_palette = new float *[num];
  memset(new_float_palette,0,sizeof(float *)*(size_t)num);
  if (float_palette != NULL)
    { 
      for (c=0; (c < num_channels) && (c < num); c++)
        new_float_palette[c] = float_palette[c];
      for (int d=c; d < num_channels; d++)
        if (float_palette[d] != NULL)
          delete[] float_palette[d];
      delete[] float_palette;
    }
  float_palette = new_float_palette;

  num_channels = num;
}

/*****************************************************************************/
/*                  kdu_channel_mapping::configure (simple)                  */
/*****************************************************************************/

bool
  kdu_channel_mapping::configure(int num_identical_channels, int bit_depth,
                                 bool is_signed)
{
  clear();
  set_num_channels(num_identical_channels);
  int c;
  for (c=0; c < num_channels; c++)
    { 
      source_components[c] = 0;
      default_rendering_precision[c] = bit_depth;
      default_rendering_signed[c] = is_signed;
      channel_interp[c].init(bit_depth,is_signed,0.0f);
    }
  num_colour_channels = num_channels;
  return true;
}

/*****************************************************************************/
/*                    kdu_channel_mapping::configure (raw)                   */
/*****************************************************************************/

bool
  kdu_channel_mapping::configure(kdu_codestream codestream)
{
  clear();
  set_num_channels((codestream.get_num_components(true) >= 3)?3:1);
  kdu_coords ref_subs; codestream.get_subsampling(0,ref_subs,true);
  int c;
  for (c=0; c < num_channels; c++)
    { 
      source_components[c] = c;
      default_rendering_precision[c] = codestream.get_bit_depth(c,true);
      default_rendering_signed[c] = codestream.get_signed(c,true);
      channel_interp[c].init(default_rendering_precision[c],
                             default_rendering_signed[c],0.0f);
      kdu_coords subs; codestream.get_subsampling(c,subs,true);
      if (subs != ref_subs)
        break;
    }
  if (c < num_channels)
    num_channels = 1;
  num_colour_channels = num_channels;
  return true;
}

/*****************************************************************************/
/*                    kdu_channel_mapping::configure (JPX)                   */
/*****************************************************************************/

bool
  kdu_channel_mapping::configure(jp2_colour colr, jp2_channels chnl,
                                 int codestream_idx, jp2_palette plt,
                                 jp2_dimensions codestream_dimensions)
{
  clear();
  if (!colour_converter.init(colr))
    return false;
  set_num_channels(chnl.get_num_colours());
  num_colour_channels = num_channels;
  if (num_channels <= 0)
    { KDU_ERROR(e,0); e <<
        KDU_TXT("JP2 object supplied to "
        "`kdu_channel_mapping::configure' has no colour channels!");
    }

  int c, last_valid_c=-1;
  for (c=0; c < num_channels; c++)
    { 
      int lut_idx, stream, format, fmt_params[3]={0,0,0};
      if (chnl.get_colour_mapping(c,source_components[c],
                                  lut_idx,stream,format,fmt_params))
        last_valid_c = c;
      else if (last_valid_c >= 0)
        chnl.get_colour_mapping(last_valid_c,source_components[c],
                                lut_idx,stream,format,fmt_params);    
      else
        { KDU_ERROR(e,0x16021602); e <<
          KDU_TXT("Cannot configure channel mappings; no valid colour "
                  "channel mappings are available.");              
        }
        
      float zeta = colr.get_natural_unsigned_zero_point(c);
      if (stream != codestream_idx)
        { 
          clear();
          return false;
        }
      if (lut_idx >= 0)
        { // Set up palette lookup tables
          default_rendering_precision[c] = plt.get_bit_depth(lut_idx);
          default_rendering_signed[c] = plt.get_signed(lut_idx);
          if (!channel_interp[c].init(default_rendering_precision[c],
                                      default_rendering_signed[c],
                                      zeta,format,fmt_params))
            { // Problem must be an unsupported data format
              clear();
              return false;
            }
          int i, num_entries = plt.get_num_entries();
          assert(num_entries <= 1024);
          palette_bits = 1;
          while ((1<<palette_bits) < num_entries)
            palette_bits++;
          assert(fix16_palette[c] == NULL);
          assert(float_palette[c] == NULL);
          fix16_palette[c] = new kdu_sample16[(int)(1<<palette_bits)];
          float_palette[c] = new float[(int)(1<<palette_bits)];
          plt.get_lut(lut_idx,fix16_palette[c],format,fmt_params[0]);
          plt.get_lut(lut_idx,float_palette[c],format,fmt_params[0]);
          for (i=num_entries; i < (1<<palette_bits); i++)
            { 
              (fix16_palette[c])[i] = (fix16_palette[c])[num_entries-1];
              (float_palette[c])[i] = (float_palette[c])[num_entries-1];
            }
        }
      else
        { 
          default_rendering_precision[c] =
            codestream_dimensions.get_bit_depth(source_components[c]);
          default_rendering_signed[c] =
            codestream_dimensions.get_signed(source_components[c]);
          if (!channel_interp[c].init(default_rendering_precision[c],
                                      default_rendering_signed[c],
                                      zeta,format,fmt_params))
            { // Problem must be an unsupported data format
              clear();
              return false;
            }
        }
      if (channel_interp[c].float_exp_bits > 0)
        default_rendering_precision[c] = 0;
      else if ((default_rendering_precision[c] -=
                channel_interp[c].fixpoint_int_bits) < 0)
        default_rendering_precision[c] = 0;
    }
  return true;  
}

/*****************************************************************************/
/*                    kdu_channel_mapping::configure (jp2)                   */
/*****************************************************************************/

bool
  kdu_channel_mapping::configure(jp2_source *jp2_in, bool ignore_alpha)
{
  jp2_channels chnl = jp2_in->access_channels();
  jp2_palette plt = jp2_in->access_palette();
  jp2_colour colr = jp2_in->access_colour();
  jp2_dimensions dims = jp2_in->access_dimensions();
  if (!configure(colr,chnl,0,plt,dims))
    { KDU_ERROR(e,1); e <<
        KDU_TXT("Cannot perform colour conversion from the colour "
                "description embedded in a JP2 (or JP2-compatible) data "
                "source, to the sRGB colour space.  This should not happen "
                "with truly JP2-compatible descriptions.");
    }
  if (!ignore_alpha)
    add_alpha_to_configuration(chnl,0,plt,dims);
  return true;
}

/*****************************************************************************/
/*              kdu_channel_mapping::add_alpha_to_configuration              */
/*****************************************************************************/

bool
  kdu_channel_mapping::add_alpha_to_configuration(jp2_channels chnl,
                                 int codestream_idx, jp2_palette plt,
                                 jp2_dimensions codestream_dimensions,
                                 bool ignore_premultiplied_alpha)
{
  int scan_channels = chnl.get_num_colours();
  set_num_channels(num_colour_channels);
  int c, alpha_comp_idx=-1, alpha_lut_idx=-1;
  int alpha_format=-1, alpha_fmt_params[3]={0,0,0};
  for (c=0; c < scan_channels; c++)
    { 
      int lut_idx, tmp_idx, stream, format, fmt_params[3]={0,0,0};
      if (chnl.get_opacity_mapping(c,tmp_idx,lut_idx,stream,
                                   format,fmt_params) &&
          (stream == codestream_idx))
        { // See if we can find a consistent alpha channel
          if (c == 0)
            { 
              alpha_comp_idx = tmp_idx;
              alpha_lut_idx = lut_idx;
              alpha_format = format;
              alpha_fmt_params[0] = fmt_params[0];
              alpha_fmt_params[1] = fmt_params[1];
              alpha_fmt_params[2] = fmt_params[2];
            }
          else if ((alpha_comp_idx != tmp_idx) ||
                   (alpha_lut_idx != lut_idx) ||
                   (alpha_format != format) ||
                   (alpha_fmt_params[0] != fmt_params[0]) ||
                   (alpha_fmt_params[1] != fmt_params[1]) ||
                   (alpha_fmt_params[2] != fmt_params[2]))
            alpha_comp_idx=alpha_lut_idx = -1; // Channels use different alpha
        }
      else
        alpha_comp_idx = alpha_lut_idx = -1; // Not all channels have alpha
    }

  if ((alpha_comp_idx < 0) && !ignore_premultiplied_alpha)
    for (c=0; c < scan_channels; c++)
      { 
        int lut_idx, tmp_idx, stream, format, fmt_params[3]={0,0,0};
        if (chnl.get_premult_mapping(c,tmp_idx,lut_idx,stream,
                                     format,fmt_params) &&
            (stream == codestream_idx))
          { // See if we can find a consistent alpha channel
            if (c == 0)
              { 
                alpha_comp_idx = tmp_idx;
                alpha_lut_idx = lut_idx;
                alpha_format = format;
                alpha_fmt_params[0] = fmt_params[0];
                alpha_fmt_params[1] = fmt_params[1];
                alpha_fmt_params[2] = fmt_params[2];
              }
            else if ((alpha_comp_idx != tmp_idx) ||
                     (alpha_lut_idx != lut_idx) ||
                     (alpha_format != format) ||
                     (alpha_fmt_params[0] != fmt_params[0]) ||
                     (alpha_fmt_params[1] != fmt_params[1]) ||
                     (alpha_fmt_params[2] != fmt_params[2]))
              alpha_comp_idx=alpha_lut_idx=-1; // Channels use different alpha
          }
        else
          alpha_comp_idx = alpha_lut_idx = -1; // Not all channels have alpha
      }

  if (alpha_comp_idx < 0)
    return false;

  set_num_channels(num_colour_channels+1);
  c = num_colour_channels; // Index of entry for the alpha channel
  source_components[c] = alpha_comp_idx;
  if (alpha_lut_idx >= 0)
    { 
      default_rendering_precision[c] = plt.get_bit_depth(alpha_lut_idx);
      default_rendering_signed[c] = plt.get_signed(alpha_lut_idx);
      if (!channel_interp[c].init(default_rendering_precision[c],
                                  default_rendering_signed[c],0.0f,
                                  alpha_format,alpha_fmt_params))
        { // Problem must be an unsupported data format
          clear();
          return false;
        }
      int i, num_entries = plt.get_num_entries();
      assert(num_entries <= 1024);
      palette_bits = 1;
      while ((1<<palette_bits) < num_entries)
        palette_bits++;
      fix16_palette[c] = new kdu_sample16[(int)(1<<palette_bits)];
      float_palette[c] = new float[(int)(1<<palette_bits)];
      plt.get_lut(alpha_lut_idx,fix16_palette[c],
                  alpha_format,alpha_fmt_params[0]);
      plt.get_lut(alpha_lut_idx,float_palette[c],
                  alpha_format,alpha_fmt_params[0]);
      for (i=num_entries; i < (1<<palette_bits); i++)
        { 
          (fix16_palette[c])[i] = (fix16_palette[c])[num_entries-1];
          (float_palette[c])[i] = (float_palette[c])[num_entries-1];
        }
    }
  else
    { 
      default_rendering_precision[c] =
        codestream_dimensions.get_bit_depth(alpha_comp_idx);
      default_rendering_signed[c] =
        codestream_dimensions.get_signed(alpha_comp_idx);
      if (!channel_interp[c].init(default_rendering_precision[c],
                                  default_rendering_signed[c],0.0f,
                                  alpha_format,alpha_fmt_params))
        { // Problem must be an unsupported data format
          clear();
          return false;
        }
    }
  if (channel_interp[c].float_exp_bits > 0)
    default_rendering_precision[c] = 0;
  else if ((default_rendering_precision[c] -=
            channel_interp[c].fixpoint_int_bits) < 0)
    default_rendering_precision[c] = 0;

  return true;
}


/* ========================================================================= */
/*                         kdu_region_decompressor                           */
/* ========================================================================= */

/*****************************************************************************/
/*             kdu_region_decompressor::kdu_region_decompressor              */
/*****************************************************************************/

kdu_region_decompressor::kdu_region_decompressor()
{
  precise = false;
  fastest = false;
  want_true_zero = false;
  want_true_max = false;
  white_stretch_precision = 0;
  zero_overshoot_interp_threshold = 2;
  max_interp_overshoot = 0.4F;
  env=NULL;
  next_queue_bank_idx = 0;
  tile_banks = new kdrd_tile_bank[2];
  current_bank = background_bank = NULL;
  min_tile_bank_width = 0;
  codestream_failure = false;
  codestream_failure_exception = KDU_NULL_EXCEPTION;
  discard_levels = 0;
  max_channels = num_channels = num_colour_channels = 0;
  channels = NULL;
  colour_converter = NULL;
  cc_normalized_max = -1.0f;
  max_components = num_components = 0;
  components = NULL;
  component_indices = NULL;
  max_channel_bufs = num_channel_bufs = 0;
  channel_bufs = NULL;
  limiter = NULL;
  limiter_ppi_x = limiter_ppi_y = -1.0f;
}

/*****************************************************************************/
/*             kdu_region_decompressor::~kdu_region_decompressor             */
/*****************************************************************************/

kdu_region_decompressor::~kdu_region_decompressor()
{
  codestream_failure = true; // Force premature termination, if required
  finish();
  if (components != NULL)
    delete[] components;
  if (component_indices != NULL)
    delete[] component_indices;
  if (channels != NULL)
    delete[] channels;
  if (channel_bufs != NULL)
    delete[] channel_bufs;
  if (tile_banks != NULL)
    delete[] tile_banks;
  if (limiter != NULL)
    delete limiter;
}

/*****************************************************************************/
/*                 kdu_region_decompressor::find_render_dims                 */
/*****************************************************************************/

kdu_dims
  kdu_region_decompressor::find_render_dims(kdu_dims codestream_dims,
                                kdu_coords ref_comp_subs,
                                kdu_coords ref_comp_expand_numerator,
                                kdu_coords ref_comp_expand_denominator)
{
  kdu_long val, num, den;
  
  if (ref_comp_subs.x < 1)
    ref_comp_subs.x = 1;   // Avoid pathalogical conditions generating errors
  if (ref_comp_subs.y < 1) // here; they can be caught properly elsewhere.
    ref_comp_subs.y = 1;
  
  kdu_coords min = codestream_dims.pos;
  kdu_coords lim = min + codestream_dims.size;
  min.x = long_ceil_ratio(min.x,ref_comp_subs.x);
  lim.x = long_ceil_ratio(lim.x,ref_comp_subs.x); 
  min.y = long_ceil_ratio(min.y,ref_comp_subs.y);
  lim.y = long_ceil_ratio(lim.y,ref_comp_subs.y);

  kdu_coords HxD = ref_comp_expand_numerator;
  HxD.x = (HxD.x-1)>>1;  HxD.y = (HxD.y-1)>>1;

  num = ref_comp_expand_numerator.x; den = ref_comp_expand_denominator.x;
  val = num*min.x;  val -= HxD.x;
  min.x = long_ceil_ratio(val,den);
  val = num*lim.x;  val -= HxD.x;
  lim.x = long_ceil_ratio(val,den);

  num = ref_comp_expand_numerator.y; den = ref_comp_expand_denominator.y;
  val = num*min.y;  val -= HxD.y;
  min.y = long_ceil_ratio(val,den);
  val = num*lim.y;  val -= HxD.y;
  lim.y = long_ceil_ratio(val,den);

  kdu_dims render_dims;
  render_dims.pos = min;
  render_dims.size = lim - min;
  
  return render_dims;
}

/*****************************************************************************/
/*              kdu_region_decompressor::find_codestream_point               */
/*****************************************************************************/

kdu_coords
  kdu_region_decompressor::find_codestream_point(kdu_coords render_point,
                                kdu_coords ref_comp_subs,
                                kdu_coords ref_comp_expand_numerator,
                                kdu_coords ref_comp_expand_denominator,
                                bool allow_fractional_mapping)
{
  kdu_long num, den, val;
  kdu_coords result;
  
  if (ref_comp_subs.x < 1)
    ref_comp_subs.x = 1;   // Avoid pathalogical conditions generating errors
  if (ref_comp_subs.y < 1) // here; they can be caught properly elsewhere.
    ref_comp_subs.y = 1;

  num = ref_comp_expand_numerator.x; den = ref_comp_expand_denominator.x;
  if (allow_fractional_mapping && (num > den) && (ref_comp_subs.x > 1))
    { 
      den *= ref_comp_subs.x;  ref_comp_subs.x = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val = den * render_point.x + ((num-1)>>1);
  result.x = long_floor_ratio(val,num) * ref_comp_subs.x;
  
  num = ref_comp_expand_numerator.y; den = ref_comp_expand_denominator.y;
  if (allow_fractional_mapping && (num > den) && (ref_comp_subs.y > 1))
    { 
      den *= ref_comp_subs.y;  ref_comp_subs.y = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val = den * render_point.y + ((num-1)>>1);
  result.y = long_floor_ratio(val,num) * ref_comp_subs.y;

  return result;
}

/*****************************************************************************/
/*                kdu_region_decompressor::find_render_point                 */
/*****************************************************************************/

kdu_coords
  kdu_region_decompressor::find_render_point(kdu_coords render_point,
                                kdu_coords ref_comp_subs,
                                kdu_coords ref_comp_expand_numerator,
                                kdu_coords ref_comp_expand_denominator,
                                bool allow_fractional_mapping)
{
  kdu_coords result;
  kdu_long num, den, sub, val;
  
  if (ref_comp_subs.x < 1)
    ref_comp_subs.x = 1;   // Avoid pathalogical conditions generating errors
  if (ref_comp_subs.y < 1) // here; they can be caught properly elsewhere.
    ref_comp_subs.y = 1;
  
  num = ref_comp_expand_numerator.x; den = ref_comp_expand_denominator.x;
  sub = ref_comp_subs.x;  val = render_point.x;
  if (allow_fractional_mapping && (num > den) && (sub > 1))
    { 
      den *= sub;  sub = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val += val-sub; sub += sub; val = long_ceil_ratio(val,sub);
  val = val * num - ((num-1)>>1);  val += val+num; den += den;
  result.x = long_floor_ratio(val,den);
  
  num = ref_comp_expand_numerator.y; den = ref_comp_expand_denominator.y;
  sub = ref_comp_subs.y;  val = render_point.y;
  if (allow_fractional_mapping && (num > den) && (sub > 1))
    { 
      den *= sub;  sub = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val += val-sub; sub += sub; val = long_ceil_ratio(val,sub);
  val = val * num - ((num-1)>>1);  val += val+num; den += den;
  result.y = long_floor_ratio(val,den);
  
  return result;
}  

/*****************************************************************************/
/*           kdu_region_decompressor::find_codestream_cover_dims             */
/*****************************************************************************/

kdu_dims
  kdu_region_decompressor::find_codestream_cover_dims(kdu_dims render_dims,
                                kdu_coords ref_comp_subs,
                                kdu_coords ref_comp_expand_numerator,
                                kdu_coords ref_comp_expand_denominator,
                                bool allow_fractional_mapping)
{
  kdu_long num, num_x2, den, val;
  kdu_coords min, lim;
  
  if (ref_comp_subs.x < 1)
    ref_comp_subs.x = 1;   // Avoid pathalogical conditions generating errors
  if (ref_comp_subs.y < 1) // here; they can be caught properly elsewhere.
    ref_comp_subs.y = 1;
  
  min = render_dims.pos; lim = min + render_dims.size;
  
  num = ref_comp_expand_numerator.x; den = ref_comp_expand_denominator.x;
  if (allow_fractional_mapping && (num > den) && (ref_comp_subs.x > 1))
    { 
      den *= ref_comp_subs.x;  ref_comp_subs.x = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val = min.x*den + ((num-1)>>1);  val += val-num; num_x2 = num+num;
  min.x = long_ceil_ratio(val,num_x2);
  val = lim.x*den + ((num-1)>>1); val += val-num;
  lim.x = long_ceil_ratio(val,num_x2);
  
  num = ref_comp_expand_numerator.y; den = ref_comp_expand_denominator.y;
  if (allow_fractional_mapping && (num > den) && (ref_comp_subs.y > 1))
    { 
      den *= ref_comp_subs.y;  ref_comp_subs.y = 1;
      while (((num >> 32) || (den >> 32)) && (num > 1) && (den > 1))
        { num = (num+1) >> 1; den = (den+1) >> 1; }
    }
  val = min.y*den + ((num-1)>>1);  val += val-num; num_x2 = num+num;
  min.y = long_ceil_ratio(val,num_x2);
  val = lim.y*den + ((num-1)>>1); val += val-num;
  lim.y = long_ceil_ratio(val,num_x2);
    
  min.x = min.x * ref_comp_subs.x + 1 - ((ref_comp_subs.x+1)>>1);
  lim.x = lim.x * ref_comp_subs.x + 1 - ((ref_comp_subs.x+1)>>1);
  min.y = min.y * ref_comp_subs.y + 1 - ((ref_comp_subs.y+1)>>1);
  lim.y = lim.y * ref_comp_subs.y + 1 - ((ref_comp_subs.y+1)>>1);
  
  kdu_dims result; result.pos = min;  result.size = lim-min;
  return result;
}  

/*****************************************************************************/
/*                 kdu_region_decompressor::set_num_channels                 */
/*****************************************************************************/

void
  kdu_region_decompressor::set_num_channels(int num)
{
  if (num > max_channels)
    {
      int new_max_channels = num;
      kdrd_channel *new_channels =
        new kdrd_channel[new_max_channels];
      if (channels != NULL)
        delete[] channels;
      channels = new_channels;      
      max_channels = new_max_channels;
    }
  num_channels = num_colour_channels = num;
  for (int c=0; c < num_channels; c++)
    channels[c].init();
}

/*****************************************************************************/
/*                   kdu_region_decompressor::add_component                  */
/*****************************************************************************/

kdrd_component *
  kdu_region_decompressor::add_component(int comp_idx)
{
  int n;
  for (n=0; n < num_components; n++)
    if (component_indices[n] == comp_idx)
      return components + n;
  if (num_components == max_components)
    { // Allocate a new array
      int new_max_comps = max_components + num_components + 1; // Grow fast
      kdrd_component *new_comps = new kdrd_component[new_max_comps];
      for (n=0; n < num_components; n++)
        new_comps[n].copy(components[n]);
      if (components != NULL)
        {
          for (int k=0; k < num_channels; k++)
            if (channels[k].source != NULL)
              { // Fix up pointers
                int off = (int)(channels[k].source-components);
                assert((off >= 0) && (off < num_components));
                channels[k].source = new_comps + off;
              }
          delete[] components;
        }
      components = new_comps;

      int *new_indices = new int[new_max_comps];
      for (n=0; n < num_components; n++)
        new_indices[n] = component_indices[n];
      if (component_indices != NULL)
        delete[] component_indices;
      component_indices = new_indices;
    
      max_components = new_max_comps;
    }

  n = num_components++;
  component_indices[n] = comp_idx;
  components[n].init(n);
  return components + n;
}

/*****************************************************************************/
/*            kdu_region_decompressor::get_safe_expansion_factors            */
/*****************************************************************************/

void
  kdu_region_decompressor::get_safe_expansion_factors(
                           kdu_codestream codestream,
                           kdu_channel_mapping *mapping, int single_component,
                           int discard_levels, double &min_prod,
                           double &max_x, double &max_y,
                           kdu_component_access_mode access_mode)
{
  min_prod = max_x = max_y = 1.0;
  int ref_idx;
  if (mapping == NULL)
    ref_idx = single_component;
  else if (mapping->num_channels > 0)
    ref_idx = mapping->source_components[0];
  else
    return;
  
  codestream.apply_input_restrictions(0,0,discard_levels,0,NULL,access_mode);
  int n=0, comp_idx=ref_idx;
  kdu_coords ref_subs, this_subs;
  codestream.get_subsampling(ref_idx,ref_subs,true);
  double ref_prod = ((double) ref_subs.x) * ((double) ref_subs.y);
  if (ref_prod < 1.0)
    ref_prod = 1.0; // Avoid pathalogical conditions generating errors here
  do {
    codestream.get_subsampling(comp_idx,this_subs,true);
    double this_prod = ((double) this_subs.x) * ((double) this_subs.y);
    if (this_prod < 1.0)
      this_prod = 1.0; // Avoid pathalogical conditions generating errors here
    if ((min_prod*this_prod) > ref_prod)
      min_prod = ref_prod / this_prod;
  } while ((mapping != NULL) && ((++n) < mapping->num_channels) &&
           ((comp_idx=mapping->source_components[n]) >= 0));
  min_prod *= 1.0 / (1 << (KDU_FIX_POINT+9));

  kdu_dims ref_dims;
  codestream.get_dims(ref_idx,ref_dims,true);
  if (ref_dims.size.x < 1)
    ref_dims.size.x = 1;      // Avoid errors under pathalogical circumstances
  if (ref_dims.size.y < 1)    // that will be caught later when `start' is
    ref_dims.size.y = 1;      // called anyway.
  double safe_lim = (double) 0x70000000;
  if (safe_lim > (double) ref_dims.size.x)
    max_x = safe_lim / (double) ref_dims.size.x;
  if (safe_lim > (double) ref_dims.size.y)
    max_y = safe_lim / (double) ref_dims.size.y;
}

/*****************************************************************************/
/*              kdu_region_decompressor::get_rendered_image_dims             */
/*****************************************************************************/

kdu_dims
  kdu_region_decompressor::get_rendered_image_dims(kdu_codestream codestream,
                           kdu_channel_mapping *mapping, int single_component,
                           int discard_levels, kdu_coords expand_numerator,
                           kdu_coords expand_denominator,
                           kdu_component_access_mode access_mode)
{
  if (this->codestream.exists())
    { KDU_ERROR_DEV(e,2); e <<
        KDU_TXT("The `kdu_region_decompressor::get_rendered_image_dims' "
        "function should not be called with a `codestream' argument between "
        "calls to `kdu_region_decompressor::start' and "
        "`kdu_region_decompressor::finish'."); }
  int ref_idx;
  if (mapping == NULL)
    ref_idx = single_component;
  else if (mapping->num_channels > 0)
    ref_idx = mapping->source_components[0];
  else
    return kdu_dims();
  
  if (expand_numerator.x < 1) expand_numerator.x = 1;
  if (expand_numerator.y < 1) expand_numerator.y = 1;
  if (expand_denominator.x < 1) expand_denominator.x = 1;
  if (expand_denominator.y < 1) expand_denominator.y = 1;

  kdu_dims canvas_dims; codestream.get_dims(-1,canvas_dims,true);
  kdu_coords ref_subs; codestream.get_subsampling(ref_idx,ref_subs,true);
  return find_render_dims(canvas_dims,ref_subs,
                          expand_numerator,expand_denominator);
}

/*****************************************************************************/
/*              kdu_region_decompressor::set_quality_limiting                */
/*****************************************************************************/

void
  kdu_region_decompressor::set_quality_limiting(const kdu_quality_limiter *obj,
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
}

/*****************************************************************************/
/*                      kdu_region_decompressor::start                       */
/*****************************************************************************/

bool
  kdu_region_decompressor::start(kdu_codestream codestream,
                                 kdu_channel_mapping *mapping,
                                 int single_component, int discard_levels,
                                 int max_layers, kdu_dims region,
                                 kdu_coords expand_numerator,
                                 kdu_coords expand_denominator, bool precise,
                                 kdu_component_access_mode access_mode,
                                 bool fastest, kdu_thread_env *env,
                                 kdu_thread_queue *env_queue)
{
  int c;

  if ((tile_banks[0].num_tiles > 0) || (tile_banks[1].num_tiles > 0))
    { KDU_ERROR_DEV(e,0x10010701); e <<
        KDU_TXT("Attempting to call `kdu_region_decompressor::start' without "
        "first invoking the `kdu_region_decompressor::finish' to finish a "
        "previously installed region.");
    }
  this->precise = precise;
  this->fastest = fastest;
  if ((this->env = env) != NULL)
    env->attach_queue(&local_env_queue,env_queue,NULL);
  
  this->next_queue_bank_idx = 0;
  this->codestream = codestream;
  codestream_failure = false;
  this->discard_levels = discard_levels;
  num_components = 0;

  colour_converter = NULL;
  cc_normalized_max = -1.0f;
  full_render_dims.pos = full_render_dims.size = kdu_coords(0,0);
  
  if (expand_numerator.x < 1) expand_numerator.x = 1;
  if (expand_numerator.y < 1) expand_numerator.y = 1;
  if (expand_denominator.x < 1) expand_denominator.x = 1;
  if (expand_denominator.y < 1) expand_denominator.y = 1;

  try { // Protect the following code
      // Set up components and channels.
      if (mapping == NULL)
        { 
          set_num_channels(1);
          channels[0].source = add_component(single_component);
          channels[0].lut_fix16 = NULL;
          channels[0].lut_float = NULL;
        }
      else
        {
          if (mapping->num_channels < 1)
            { KDU_ERROR(e,3); e <<
                KDU_TXT("The `kdu_channel_mapping' object supplied to "
                        "`kdu_region_decompressor::start' does not define any "
                        "channels at all.");
            }
          set_num_channels(mapping->num_channels);
          num_colour_channels = mapping->num_colour_channels;
          if (num_colour_channels > num_channels)
            { KDU_ERROR(e,4); e <<
                KDU_TXT("The `kdu_channel_mapping' object supplied to "
                        "`kdu_region_decompressor::start' specifies more "
                        "colour channels than the total number of channels.");
            }
          colour_converter = mapping->get_colour_converter();
          if (!(colour_converter->exists() &&
                colour_converter->is_non_trivial()))
            colour_converter = NULL;
          for (c=0; c < mapping->num_channels; c++)
            { 
              kdrd_channel *cp = channels + c;
              cp->source = add_component(mapping->source_components[c]);
              cp->lut_fix16 = NULL;  cp->lut_float = NULL;
              if (mapping->palette_bits > 0)
                { 
                  cp->lut_fix16 = mapping->fix16_palette[c];
                  cp->lut_float = mapping->float_palette[c];
                  if (cp->lut_fix16 != NULL)
                    cp->source->palette_bits = mapping->palette_bits;
                }
            }
        }

      // Configure component sampling and data representation information.
      if ((expand_denominator.x < 1) || (expand_denominator.y < 1))
        { KDU_ERROR_DEV(e,5); e <<
            KDU_TXT("Invalid expansion ratio supplied to "
            "`kdu_region_decompressor::start'.  The numerator and denominator "
            "terms expressed by the `expand_numerator' and "
            "`expand_denominator' arguments must be strictly positive.");
        }

      codestream.apply_input_restrictions(num_components,component_indices,
                                          discard_levels,max_layers,NULL,
                                          access_mode);
      for (c=0; c < num_components; c++)
        { 
          kdrd_component *comp = components + c;
          comp->bit_depth = codestream.get_bit_depth(comp->rel_comp_idx,true);
          if (comp->bit_depth <= 0)
            { KDU_ERROR(e,0x16021603); e <<
              KDU_TXT("One or more of the codestream image components "
                      "required to render the image does not exist!  The "
                      "file format is most likely corrupt.");
            }
          comp->is_signed = codestream.get_signed(comp->rel_comp_idx,true);
          comp->num_line_users = 0; // Adjust when scanning the channels below
          kdu_dims dims; codestream.get_dims(comp->rel_comp_idx,dims,true);
          if (dims.is_empty())
            { KDU_ERROR(e,0x17021602); e <<
              KDU_TXT("One of more of the codestream image components "
                      "required to render the image is entirely empty -- "
                      "i.e., it has no compressed samples whatsoever!");
            }
        }
          
      // Configure the channels for precision, interpretation and white stretch
      for (c=0; c < num_channels; c++)
        { 
          kdrd_channel *cp = channels + c;
          if (mapping == NULL)
            { // Fill in key attributes based on
              kdu_channel_interp itrp;
              itrp.init(cp->source->bit_depth,cp->source->is_signed,0.0f);
              cp->native_precision = cp->source->bit_depth;
              cp->native_signed = cp->source->is_signed;
              cp->interp_orig_prec = cp->source->bit_depth;
              cp->interp_orig_signed = cp->source->is_signed;
              cp->interp_zeta = 0.0f;
              cp->interp_normalized_max = itrp.normalized_max;
              cp->interp_normalized_natural_zero=itrp.normalized_natural_zero;
            }
          else
            { 
              cp->native_precision = mapping->default_rendering_precision[c];
              cp->native_signed = mapping->default_rendering_signed[c];
              cp->interp_orig_prec = mapping->channel_interp[c].orig_prec;
              cp->interp_orig_signed = mapping->channel_interp[c].orig_signed;
              if (((cp->interp_float_exp_bits =
                    mapping->channel_interp[c].float_exp_bits) > 0) &&
                  (cp->lut_fix16 == NULL))
                { // Update `log2_source_headroom'
                  this->precise = precise = true;
                  if (cp->interp_float_exp_bits > 6)
                    cp->log2_source_headroom = 32;
                  else
                    cp->log2_source_headroom=1<<(cp->interp_float_exp_bits-1);
                }
              if (((cp->interp_fixpoint_int_bits =
                    mapping->channel_interp[c].fixpoint_int_bits) > 0) &&
                  (cp->lut_fix16 == NULL))
                { // Update `log2_source_headroom'
                  this->precise = precise = true;
                  cp->log2_source_headroom = cp->interp_fixpoint_int_bits;
                }
              cp->interp_zeta = mapping->channel_interp[c].zeta;
              cp->interp_normalized_max =
                mapping->channel_interp[c].normalized_max;
              cp->interp_normalized_natural_zero =
                mapping->channel_interp[c].normalized_natural_zero;
              if (((cp->interp_float_exp_bits > 0) ||
                   (cp->interp_fixpoint_int_bits != 0)) &&
                  (cp->lut_fix16 != NULL) && (cp->lut_float == NULL))
                { KDU_ERROR_DEV(e,0x30011601); e <<
                  KDU_TXT("Channel-mapping object passed to "
                          "`kdu_region_decompressor::start' identifies a "
                          "palette lookup table for some channel that is "
                          "only available at low precision, yet the "
                          "channel-interp record for the same channel "
                          "identifies a non-trivial non-default data format "
                          "(float-interpreted or fixpoint-interpreted "
                          "integers with a non-trivial integer part).  These "
                          "channels require a floating-point precision "
                          "version of the palette lookup table -- read the "
                          "documentation for "
                          "`kdu_channel_mapping::float_palette'.");
                }
            }

          cp->source->num_line_users++;
          cp->stretch_residual = 0;
          cp->white_stretch_func = NULL;
          if (white_stretch_precision > 0)
            { 
              float num = 1.0f - kdu_pwrof2f(-white_stretch_precision);
              float den = 0.5f + cp->interp_normalized_max;
              assert((den <= 1.0f) && (den >= 0.5f));
              if (den < num)
                { // Necessarily false for float- and fixpoint-formatted data
                  int residual = (int)(((num - den) / den) * (float)(1<<16));
                  assert(residual <= 0xFFFF);
                  cp->stretch_residual = (kdu_uint16) residual;
                  configure_white_stretch_function(cp);
                  cp->interp_normalized_max = num - 0.5f; // After stretch
                }
            }
        }
      if (colour_converter != NULL)
        cc_normalized_max = channels[0].interp_normalized_max;
    
      // Find sampling parameters
      kdrd_component *ref_comp = channels[0].source;
      kdu_coords ref_subs, this_subs;
      codestream.get_subsampling(ref_comp->rel_comp_idx,ref_subs,true);
      float sum_reciprocal_subs_product = 0.0f;
      this->min_tile_bank_width = 3; // Insist on 3 samples from each component
                                     // before interpolation
      for (c=0; c < num_channels; c++)
        { 
          kdrd_component *comp = channels[c].source;
          codestream.get_subsampling(comp->rel_comp_idx,this_subs,true);
          channels[c].subs_product = (float)(this_subs.x * this_subs.y);
          sum_reciprocal_subs_product += 1.0f / channels[c].subs_product;
          
          if ((min_tile_bank_width*ref_subs.x) < (3*this_subs.x))
            min_tile_bank_width = (3*this_subs.x+ref_subs.x-1) / ref_subs.x;
          
          kdu_long num_x, num_y, den_x, den_y;
          num_x = ((kdu_long) expand_numerator.x ) * ((kdu_long) this_subs.x);
          den_x = ((kdu_long) expand_denominator.x ) * ((kdu_long) ref_subs.x);
          num_y = ((kdu_long) expand_numerator.y) * ((kdu_long) this_subs.y);
          den_y = ((kdu_long) expand_denominator.y) * ((kdu_long) ref_subs.y);
          
          if (num_x != den_x)
            while ((num_x < 32) && (den_x < (1<<30)))
              { num_x+=num_x; den_x+=den_x; }
          if (num_y != den_y)
            while ((num_y < 32) && (den_y < (1<<30)))
              { num_y+=num_y; den_y+=den_y; }
          
          kdu_coords boxcar_radix;
          while (den_x > (3*num_x))
            { boxcar_radix.x++; num_x+=num_x; }
          while (den_y > (3*num_y))
            { boxcar_radix.y++; num_y+=num_y; }
          
          if (num_x == den_x)
            num_x = den_x = 1;
          if (num_y == den_y)
            num_y = den_y = 1;
          
          if (!(reduce_ratio_to_ints(num_x,den_x) &&
                reduce_ratio_to_ints(num_y,den_y)))
            { KDU_ERROR_DEV(e,7); e <<
                KDU_TXT("Unable to represent all component "
                "expansion factors as rational numbers whose numerator and "
                "denominator can both be expressed as 32-bit signed "
                "integers.  This is a very unusual condition.");
            }
          kdu_coords phase_shift;
          while ((((kdu_long) 1)<<(phase_shift.x+6)) < num_x)
            phase_shift.x++;
          while ((((kdu_long) 1)<<(phase_shift.y+6)) < num_y)
            phase_shift.y++;
          assert((((num_x-1) >> phase_shift.x) < 64) &&
                 (((num_y-1) >> phase_shift.y) < 64));
          
          channels[c].boxcar_log_size = boxcar_radix.x+boxcar_radix.y;
          if (channels[c].boxcar_log_size > (KDU_FIX_POINT+9))
            { KDU_ERROR_DEV(e,0x15090901); e <<
              KDU_TXT("The `expand_numerator' and `expand_denominator' "
                      "parameters supplied to "
                      "`kdu_region_decompressor::start' "
                      "represent a truly massive reduction in resolution "
                      "through subsampling (on the order of many millions).  "
                      "Apart from being quite impractical, such large "
                      "subsampling factors violate internal implementation "
                      "requirements.");
            }
          channels[c].boxcar_size.x = (1 << boxcar_radix.x);
          channels[c].boxcar_size.y = (1 << boxcar_radix.y);
          channels[c].sampling_numerator.x = (int) den_x;
          channels[c].sampling_denominator.x = (int) num_x;
          channels[c].sampling_numerator.y = (int) den_y;
          channels[c].sampling_denominator.y = (int) num_y;
          codestream.get_relative_registration(comp->rel_comp_idx,
                                             ref_comp->rel_comp_idx,
                                             channels[c].sampling_denominator,
                                             channels[c].source_alignment,
                                             true);
          channels[c].source_alignment.x =
            (channels[c].source_alignment.x +
             ((1<<boxcar_radix.x)>>1)) >> boxcar_radix.x;
          channels[c].source_alignment.y =
            (channels[c].source_alignment.y +
             ((1<<boxcar_radix.y)>>1)) >> boxcar_radix.y;
          channels[c].sampling_phase_shift = phase_shift;
          
          // Now generate interpolation kernels
          float ratio, thld=(float)zero_overshoot_interp_threshold;
          if (channels[c].sampling_denominator.x !=
              channels[c].sampling_numerator.x) 
            {
              ratio = (((float) channels[c].sampling_denominator.x) /
                       ((float) channels[c].sampling_numerator.x));
            if ((c == 0) ||
                !channels[c].h_kernels.copy(channels[c-1].h_kernels,ratio,
                                            max_interp_overshoot,thld))
              channels[c].h_kernels.init(ratio,max_interp_overshoot,thld);
            }
          if (channels[c].sampling_denominator.y !=
              channels[c].sampling_numerator.y) 
            {
              ratio = (((float) channels[c].sampling_denominator.y) /
                       ((float) channels[c].sampling_numerator.y));
              if ((!channels[c].v_kernels.copy(channels[c].h_kernels,ratio,
                                               max_interp_overshoot,thld)) &&
                  ((c == 0) ||
                   !channels[c].v_kernels.copy(channels[c-1].v_kernels,ratio,
                                               max_interp_overshoot,thld)))
                channels[c].v_kernels.init(ratio,max_interp_overshoot,thld);
            }
          
          // Finally we can fill out the phase tables
          int i;
          for (i=0; i < 65; i++)
            {
              double sigma;
              int min_phase, max_phase;
              
              min_phase = (i<<phase_shift.x)-((1<<phase_shift.x)>>1);
              max_phase = ((i+1)<<phase_shift.x)-1-((1<<phase_shift.x)>>1);
              if (min_phase < 0)
                min_phase = 0;
              if (max_phase >= channels[c].sampling_denominator.x)
                max_phase = channels[c].sampling_denominator.x-1;
              sigma = (((double)(min_phase+max_phase)) /
                       (2.0*channels[c].sampling_denominator.x));
              channels[c].horz_phase_table[i] = (kdu_uint16)(sigma*32+0.5);
              if (channels[c].horz_phase_table[i] > 32)
                channels[c].horz_phase_table[i] = 32;
              
              min_phase = (i<<phase_shift.y)-((1<<phase_shift.y)>>1);
              max_phase = ((i+1)<<phase_shift.y)-1-((1<<phase_shift.y)>>1);
              if (min_phase < 0)
                min_phase = 0;
              if (max_phase >= channels[c].sampling_denominator.y)
                max_phase = channels[c].sampling_denominator.y-1;
              sigma = (((double)(min_phase+max_phase)) /
                       (2.0*channels[c].sampling_denominator.y));
              channels[c].vert_phase_table[i] = (kdu_uint16)(sigma*32+0.5);
              if (channels[c].vert_phase_table[i] > 32)
                channels[c].vert_phase_table[i] = 32;
            }
        }

      // Apply level/layer restrictions and find `full_render_dims'
      codestream.apply_input_restrictions(num_components,component_indices,
                                          discard_levels,max_layers,NULL,
                                          access_mode);
      kdu_dims canvas_dims; codestream.get_dims(-1,canvas_dims,true);
      full_render_dims =
        find_render_dims(canvas_dims,ref_subs,
                         expand_numerator,expand_denominator);
      if ((full_render_dims & region) != region)
        { KDU_ERROR_DEV(e,9); e <<
            KDU_TXT("The `region' passed into "
            "`kdu_region_decompressor::start' does not lie fully within the "
            "region occupied by the full image on the rendering canvas.  The "
            "error is probably connected with a misunderstanding of the "
            "way in which codestream dimensions are mapped to rendering "
            "coordinates through the rational upsampling process offered by "
            "the `kdu_region_decompressor' object.  It is best to use "
            "`kdu_region_decompressor::get_rendered_image_dims' to find the "
            "full image dimensions on the rendering canvas.");
        }
      this->original_expand_numerator = expand_numerator;
      this->original_expand_denominator = expand_denominator;
    
      // Configure quality limiter, if there is one
      if (limiter != NULL)
        { 
          float ppi_x=limiter_ppi_x, ppi_y=limiter_ppi_y;
          ppi_x *= ref_subs.x; // Sample density of an un-subsampled component
          ppi_y *= ref_subs.y; // would be this much higher.
          ppi_x *= expand_denominator.x;  // Original PPI specs apply to the
          ppi_y *= expand_denominator.y;  // expanded reference component.  We
          ppi_x /= expand_numerator.x;    // have to divide by any expansion
          ppi_y /= expand_numerator.y;    // amount to get to the raw component
          limiter->set_display_resolution(ppi_x,ppi_y);
          float av_reciprocal_subs_product = (sum_reciprocal_subs_product /
                                              (float)num_channels);
          for (c=0; c < num_channels; c++)
            { 
              float sq_weight=1.0f;  bool is_chroma=false;
              if (colour_converter != NULL)
                colour_converter->get_channel_info(c,sq_weight,is_chroma);
              sq_weight *= channels[c].subs_product*av_reciprocal_subs_product;
              if (channels[c].lut_fix16 != NULL)
                sq_weight *= kdu_pwrof2f(64); // Avoid any significant qlimit
              else if (channels[c].log2_source_headroom > 0)
                { 
                  int log2_err_scale = channels[c].log2_source_headroom;
                  if (log2_err_scale > 32)
                    log2_err_scale = 32; // Avoid getting close to float ovfl
                  sq_weight *= kdu_pwrof2f(2*log2_err_scale);
                }
              if (want_true_zero && channels[c].interp_orig_signed)
                sq_weight *= 4.0f; // Signed data typically stretched by 2 so
                                   // that the positive part maps to the full
                                   // output dynamic range -- unless outputs
                                   // are signed, but then we are less
                                   // interested in visual quality limiting.
              limiter->set_comp_info(channels[c].source->rel_comp_idx,
                                     sq_weight,is_chroma);
            }
        }

      // Find a region on the code-stream canvas which provides sufficient
      // coverage for all image components, taking into account interpolation
      // kernel supports and boxcar integration processes.
      kdu_dims canvas_region =
        find_canvas_cover_dims(region,codestream,channels,num_channels,false);
      codestream.apply_input_restrictions(num_components,component_indices,
                                          discard_levels,max_layers,
                                          &canvas_region,access_mode,
                                          NULL,limiter);
      codestream.get_dims(ref_comp->rel_comp_idx,this->ref_comp_dims,true);

      // Prepare for processing tiles within the region.
      codestream.get_valid_tiles(valid_tiles);
      next_tile_idx = valid_tiles.pos;
    }
  catch (kdu_exception exc) {
      codestream_failure_exception = exc;
      if (env != NULL)
        env->handle_exception(exc);
      finish();
      return false;
    }
  catch (std::bad_alloc &) {
      codestream_failure_exception = KDU_MEMORY_EXCEPTION;
      if (env != NULL)
        env->handle_exception(KDU_MEMORY_EXCEPTION);
      finish();
      return false;
    }  
  return true;
}

/*****************************************************************************/
/*                 kdu_region_decompressor::start_tile_bank                  */
/*****************************************************************************/

bool
  kdu_region_decompressor::start_tile_bank(kdrd_tile_bank *bank,
                                           kdu_long suggested_tile_mem,
                                           kdu_dims incomplete_region)
{
  assert(bank->num_tiles == 0); // Make sure it is not currently open
  bank->queue_bank_idx = 0;
  bank->freshly_created = true;

  // Start by finding out how many tiles will be in the bank, and the region
  // they occupy on the canvas
  kdrd_component *ref_comp = channels[0].source;
  if (suggested_tile_mem < 1)
    suggested_tile_mem = 1;
  kdu_dims canvas_cover_dims =
    find_canvas_cover_dims(incomplete_region,codestream,
                           channels,num_channels,true);
  int num_tiles=0, mem_height=100;
  kdu_long half_suggested_tile_mem = suggested_tile_mem >> 1;
  kdu_coords idx;
  int tiles_left_on_row = valid_tiles.pos.x+valid_tiles.size.x-next_tile_idx.x;
  int ref_comp_samples_left = ref_comp_dims.size.x + ref_comp_dims.pos.x;
  while (((next_tile_idx.y-valid_tiles.pos.y) < valid_tiles.size.y) &&
         ((next_tile_idx.x-valid_tiles.pos.x) < valid_tiles.size.x) &&
         (suggested_tile_mem > 0))
    { 
      idx = next_tile_idx;   next_tile_idx.x++;
      kdu_dims full_dims, dims;
      codestream.get_tile_dims(idx,-1,full_dims,true);
      if (!full_dims.intersects(canvas_cover_dims))
        { // Discard tile immediately
          kdu_tile tt=codestream.open_tile(idx,env);
          if (tt.exists()) tt.close(env);
          continue;
        }
      codestream.get_tile_dims(idx,ref_comp->rel_comp_idx,dims,true);
      if (num_tiles == 0)
        { 
          bank->dims = dims;
          bank->first_tile_idx = idx;
          ref_comp_samples_left -= dims.pos.x;
          assert(ref_comp_samples_left >= 0);
        }
      else
        bank->dims.size.x += dims.size.x;
      if (dims.size.y < mem_height)
        mem_height = dims.size.y;
      suggested_tile_mem -= dims.size.x * mem_height;
      num_tiles++;
      tiles_left_on_row--;
      if ((suggested_tile_mem < 1) &&
          ((bank->dims.size.x < min_tile_bank_width) ||
           (ref_comp_samples_left < min_tile_bank_width)))
        suggested_tile_mem=1; // Cannot afford to truncate the tile-bank here
      else if (tiles_left_on_row < num_tiles)
        { // We seem to be more than half way through; perhaps we should quit
          // now, or go all the way.
          if ((suggested_tile_mem < half_suggested_tile_mem) &&
              (tiles_left_on_row > 2))
            break; // Come back and finish the remaining tiles next time
          if (tiles_left_on_row <= 2)
            suggested_tile_mem = 1; // Make sure we include these tiles
        }
    }

  if ((next_tile_idx.x-valid_tiles.pos.x) == valid_tiles.size.x)
    { // Advance `next_tile_idx' to the start of the next row of tiles
      next_tile_idx.x = valid_tiles.pos.x;
      next_tile_idx.y++;
    }

  if (num_tiles == 0)
    return true; // Nothing to do; must have discarded some tiles

  // Allocate the necessary storage in the `kdrd_tile_bank' object
  if (num_tiles > bank->max_tiles)
    {
      if (bank->tiles != NULL)
        { delete[] bank->tiles; bank->tiles = NULL; }
      if (bank->engines != NULL)
        { delete[] bank->engines; bank->engines = NULL; }
      bank->max_tiles = num_tiles;
      bank->tiles = new kdu_tile[bank->max_tiles];
      bank->engines = new kdu_multi_synthesis[bank->max_tiles];
    }
  bank->num_tiles = num_tiles; // Only now is it completely safe to record
                      // `num_tiles' in `bank' -- in case of thrown exceptions

  // Now open all the tiles, checking whether or not an error will occur
  int tnum;
  for (idx=bank->first_tile_idx, tnum=0; tnum < num_tiles; tnum++, idx.x++)
    bank->tiles[tnum] = codestream.open_tile(idx,env);
  if ((codestream.get_min_dwt_levels() < discard_levels) ||
      !codestream.can_flip(true))
    {
      for (tnum=0; tnum < num_tiles; tnum++)
        bank->tiles[tnum].close(env);
      bank->num_tiles = 0;
      return false;
    }
  
  // Create processing queue (if required) and all tile processing engines
  if (env != NULL)
    env->attach_queue(&(bank->env_queue),&local_env_queue,NULL,
                      (bank->queue_bank_idx=next_queue_bank_idx++));
  int processing_stripe_height = 1;
  bool double_buffering = false;
  if ((env != NULL) && (bank->dims.size.y >= 64))
    { // Assign a heuristic double buffering height
      double_buffering = true;
      processing_stripe_height = 32;
    }
  for (tnum=0; tnum < num_tiles; tnum++)
    bank->engines[tnum].create(codestream,bank->tiles[tnum],precise,false,
                               fastest,processing_stripe_height,
                               env,&(bank->env_queue),double_buffering);
  return true;
}

/*****************************************************************************/
/*                  kdu_region_decompressor::close_tile_bank                 */
/*****************************************************************************/

void
  kdu_region_decompressor::close_tile_bank(kdrd_tile_bank *bank)
{
  if (bank->num_tiles == 0)
    return;

  int tnum;
  if (env != NULL)
    env->terminate(&(bank->env_queue),false);
  for (tnum=0; tnum < bank->num_tiles; tnum++)
    if ((!codestream_failure) && bank->tiles[tnum].exists())
      { 
        try { 
          bank->tiles[tnum].close(env);
        }
        catch (kdu_exception exc) {
          codestream_failure = true;
          codestream_failure_exception = exc;
          if (env != NULL)
            env->handle_exception(exc);
        }
        catch (std::bad_alloc &) {
          codestream_failure = true;
          codestream_failure_exception = KDU_MEMORY_EXCEPTION;
          if (env != NULL)
            env->handle_exception(KDU_MEMORY_EXCEPTION);
        }
      }
  for (tnum=0; tnum < bank->num_tiles; tnum++)
    if (bank->engines[tnum].exists())
      bank->engines[tnum].destroy();
  bank->num_tiles = 0;
}

/*****************************************************************************/
/*             kdu_region_decompressor::make_tile_bank_current               */
/*****************************************************************************/

void
  kdu_region_decompressor::make_tile_bank_current(kdrd_tile_bank *bank,
                                                  kdu_dims incomplete_region)
{
  assert(bank->num_tiles > 0);
  current_bank = bank;

  // Establish rendering dimensions for the current tile bank.  The mapping
  // between rendering dimensions and actual code-stream dimensions is
  // invariably based upon the first channel as the reference component.
  render_dims = find_render_dims(bank->dims,kdu_coords(1,1),
                                 original_expand_numerator,
                                 original_expand_denominator);
  render_dims &= incomplete_region;

  // Fill in tile-bank specific fields for each component and pre-allocate
  // component line buffers.
  int c, w;
  aux_allocator.restart();
  for (c=0; c < num_components; c++)
    { 
      kdrd_component *comp = components + c;
      codestream.get_tile_dims(bank->first_tile_idx,
                               comp->rel_comp_idx,comp->dims,true);
      if (bank->num_tiles > 1)
        {
          kdu_coords last_tile_idx = bank->first_tile_idx;
          last_tile_idx.x += bank->num_tiles-1;
          kdu_dims last_tile_dims;
          codestream.get_tile_dims(last_tile_idx,
                                   comp->rel_comp_idx,last_tile_dims,true);
          assert((last_tile_dims.pos.y == comp->dims.pos.y) &&
                 (last_tile_dims.size.y == comp->dims.size.y));
          comp->dims.size.x =
            last_tile_dims.pos.x+last_tile_dims.size.x-comp->dims.pos.x;
        }
      comp->new_line_samples = 0;
      comp->needed_line_samples = comp->dims.size.x;
      comp->num_tile_lines = 0;
      comp->src_types = 0;
      comp->have_compatible16 = false;
      if ((comp->dims.size.y > 0) && (comp->num_line_users > 0))
        { 
          comp->num_tile_lines = bank->num_tiles; // May reduce this later
          if (comp->num_tile_lines > comp->max_tiles)
            { 
              comp->max_tiles = comp->num_tile_lines;
              if (comp->max_tiles > 8)
                { // Allocate new arrays
                  kdu_line_buf **new_lines=new kdu_line_buf *[comp->max_tiles];
                  if (comp->tile_lines != comp->tile_lines_scratch)
                    delete[] comp->tile_lines;
                  comp->tile_lines = new_lines;
                  const void **new_bufs=new const void *[comp->max_tiles];
                  if (comp->tile_bufs != comp->tile_bufs_scratch)
                    delete[] comp->tile_bufs;
                  comp->tile_bufs = new_bufs;
                  int *new_widths=new int[comp->max_tiles];
                  if (comp->tile_widths != comp->tile_widths_scratch)
                    delete[] comp->tile_widths;
                  comp->tile_widths = new_widths;
                  int *new_types=new int[comp->max_tiles];
                  if (comp->tile_types != comp->tile_types_scratch)
                    delete[] comp->tile_types;
                  comp->tile_types = new_types;
                }
              // Reset all array entries
              for (w=0; w < comp->max_tiles; w++)
                { comp->tile_lines[w]=NULL; comp->tile_bufs[w]=NULL;
                  comp->tile_widths[w]=0;   comp->tile_types[w]=0; }
            }
          comp->initial_empty_tile_lines = 0;
          for (w=0; w < bank->num_tiles; w++)
            { 
              comp->tile_lines[w] = NULL; // Make sure we do not accidentally
              comp->tile_bufs[w] = NULL;  // attempt to use old line buffers
              comp->tile_widths[w] =
                bank->engines[w].get_size(comp->rel_comp_idx).x;
              if (comp->tile_widths[w] <= 0)
                { 
                  if (w == comp->initial_empty_tile_lines)
                    comp->initial_empty_tile_lines++;
                  continue; // Don't configure any source data type
                }
              if (bank->engines[w].is_line_precise(comp->rel_comp_idx))
                { 
                  if (!bank->engines[w].is_line_absolute(comp->rel_comp_idx))
                    comp->tile_types[w] = KDRD_FLOAT_TYPE;
                  else
                    comp->tile_types[w] = KDRD_INT32_TYPE;
                }
              else if (!bank->engines[w].is_line_absolute(comp->rel_comp_idx))
                comp->tile_types[w] = KDRD_FIX16_TYPE;
              else
                { 
                  comp->tile_types[w] = KDRD_INT16_TYPE;
                  if (comp->bit_depth <= KDU_FIX_POINT)
                    comp->have_compatible16 = true;
                }
              comp->src_types |= comp->tile_types[w];
            }
        }
      if (comp->src_types == 0)
        comp->src_types = KDRD_FIX16_TYPE; // So we have something to test for
      while ((comp->num_tile_lines > 0) &&
             (comp->tile_widths[comp->num_tile_lines-1] <= 0))
        comp->num_tile_lines--;
      comp->indices.destroy(); // Works even if never allocated
      if ((comp->palette_bits > 0) || (comp->num_tile_lines == 0))
        comp->indices.pre_create(&aux_allocator,comp->dims.size.x,
                                 true,true,0,0);
    }
  
  // Now fill in the tile-bank specific state variables for each channel,
  // pre-allocating channel line buffer resources.
  for (c=0; c < num_channels; c++)
    { 
      kdrd_channel *chan = channels + c;
      kdrd_component *comp = chan->source;
      assert(comp->src_types != 0);
      if ((chan->interp_float_exp_bits > 0) ||
          (chan->interp_fixpoint_int_bits != 0))
        { // We insist on using floats for `JP2_CHANNEL_FORMAT_FLOAT'
          // and non-trivial `JP2_CHANNEL_FORMAT_FIXPOINT' formatted samples
          // or palette values.  Keeps things a lot simpler.
          chan->line_type = KDRD_FLOAT_TYPE;
          assert((chan->lut_fix16 == NULL) || (chan->lut_float != NULL));
        }
      else if ((chan->lut_float != NULL) && precise &&
               (chan->interp_orig_prec > KDU_FIX_POINT))
        { // Keep the lookup table outputs at floating point precision
          chan->line_type = KDRD_FLOAT_TYPE;
        }
      else if ((chan->lut_fix16 != NULL) ||
               (comp->src_types & KDRD_FIX16_TYPE) ||
               comp->have_compatible16 ||
               (colour_converter != NULL) ||
               (chan->stretch_residual != 0))
        { // If any single tile line is of sufficiently low precision to use
          // the FIX16 channel type, or if colour conversion is reqired (always
          // done at lower precision), of if a low precision palette is
          // to be used, or a white-stretch is required (we no longer
          // recommend the white stretch algorithm), use FIX16 for the
          // entire channel line.
          chan->line_type = KDRD_FIX16_TYPE;
        }
      else
        { // Need to use a higher precision representation -- could be float
          // or 32-bit integer
          chan->line_type = KDRD_FLOAT_TYPE;
          if ((chan->sampling_numerator == chan->sampling_denominator) &&
              !(comp->src_types & KDRD_FLOAT_TYPE))
            { // Passing 32-bit absolute ints straight through from source comp
              chan->line_type = KDRD_INT32_TYPE;
            }
        }
      
      if (chan->line_type == KDRD_FLOAT_TYPE)
        chan->in_precision = 0;
      else if (chan->line_type == KDRD_FIX16_TYPE)
        {
          chan->in_precision = KDU_FIX_POINT + chan->boxcar_log_size;
          if (chan->in_precision > (16+KDU_FIX_POINT))
            chan->in_precision = 16+KDU_FIX_POINT;          
        }
      else if (chan->line_type == KDRD_INT32_TYPE)
        chan->in_precision = comp->bit_depth;
      else
        assert(0);

      // Calculate the minimum and maximum indices for boxcar cells
      // (same as image component samples if there is no boxcar accumulation)
      // associated with `render_dims', along with the sampling phase
      // for the upper left hand corner of `render_dims'.
      kdu_long val, num, den, aln;
      kdu_coords min = render_dims.pos;
      kdu_coords max = min + render_dims.size - kdu_coords(1,1);
      
      num = chan->sampling_numerator.x;
      den = chan->sampling_denominator.x;
      aln = chan->source_alignment.x;
      aln += ((chan->boxcar_size.x-1)*den) / (2*chan->boxcar_size.x);
      min.x = long_floor_ratio((val=num*min.x-aln),den);
      chan->sampling_phase.x = (int)(val-min.x*den);
      max.x = long_floor_ratio(num*max.x-aln,den);

      num = chan->sampling_numerator.y;
      den = chan->sampling_denominator.y;
      aln = chan->source_alignment.y;
      aln += ((chan->boxcar_size.y-1)*den) / (2*chan->boxcar_size.y);
      min.y = long_floor_ratio((val=num*min.y-aln),den); // Rounding
      chan->sampling_phase.y = (int)(val-min.y*den);
      max.y = long_floor_ratio(num*max.y-aln,den);
      
      chan->in_line_start = 0;
      chan->in_line_length = 1+max.x-min.x;
      chan->out_line_length = render_dims.size.x;
      if (chan->sampling_numerator.x != chan->sampling_denominator.x)
        { // Allow for 5-tap interpolation kernels
          min.x -= 2;
          chan->in_line_start = -2;
          chan->in_line_length += 5;
        }
      if (chan->sampling_numerator.y != chan->sampling_denominator.y)
        min.y -= 2;

      chan->missing.x = comp->dims.pos.x - min.x*chan->boxcar_size.x;
      chan->missing.y = comp->dims.pos.y - min.y*chan->boxcar_size.y;
      
      chan->boxcar_lines_left = chan->boxcar_size.y;
      
      chan->in_line = chan->horz_line = chan->out_line = NULL;
      chan->reset_vlines();
      chan->line_bufs_used = 0;
      int line_buf_width = chan->in_line_length+chan->in_line_start;
      int line_buf_lead = -chan->in_line_start;
      int min_line_buf_width = line_buf_width;
      int min_line_buf_lead = line_buf_lead;
#ifdef KDU_SIMD_OPTIMIZATIONS
      if (chan->sampling_numerator.x != chan->sampling_denominator.x)
        {
          if (chan->line_type == KDRD_FLOAT_TYPE)
            { min_line_buf_lead += 3; min_line_buf_width += 9; }
          else
            { min_line_buf_lead += 7; min_line_buf_width += 21; }
        }
#endif
      // Adjust line buffer width to accommodate boxcar integration
      if ((chan->in_precision > KDU_FIX_POINT) &&
          (chan->line_type == KDRD_FIX16_TYPE))
        line_buf_width += line_buf_width + line_buf_lead;
      
      // Make sure line buffers can accommodate interpolated output lines
      if (line_buf_width < chan->out_line_length)
        line_buf_width = chan->out_line_length;
      
      // Make sure bufers are large enough to accommodate filtering extensions
      if (line_buf_lead < min_line_buf_lead)
        line_buf_lead = min_line_buf_lead;
      if (line_buf_width < min_line_buf_width)
        line_buf_width = min_line_buf_width;
      
      // Pre-create the lines
      for (w=0; w < KDRD_CHANNEL_LINE_BUFS; w++)
        chan->line_bufs[w].destroy();
      for (w=0; w < KDRD_CHANNEL_LINE_BUFS; w++)
        chan->line_bufs[w].pre_create(&aux_allocator,line_buf_width,
                                      ((chan->line_type &
                                        KDRD_ABSOLUTE_TYPE) != 0),
                                      ((chan->line_type &
                                        KDRD_SHORT_TYPE) != 0),
                                      line_buf_lead,0);
      
      // Determine whether copying from tile-component lines can be avoided
      chan->can_use_component_samples_directly =
        (chan->lut_fix16 == NULL) && (chan->interp_float_exp_bits <= 0) &&
        (chan->interp_fixpoint_int_bits == 0) &&
        (comp->num_tile_lines==1) && (chan->missing.x == 0) &&
        (chan->sampling_numerator == chan->sampling_denominator) &&
        (chan->out_line_length <= comp->dims.size.x) &&
        (chan->line_type == comp->src_types);
      
      chan->convert_and_copy_func = NULL;
      chan->convert_and_add_func = NULL;
      if ((chan->lut_fix16 == NULL) &&
          !chan->can_use_component_samples_directly)
        configure_conversion_function(chan);
    }

  // Perform final resource allocation
  aux_allocator.finalize(codestream);
  for (c=0; c < num_components; c++)
    {
      kdrd_component *comp = components + c;
      if ((comp->palette_bits > 0) || (comp->num_tile_lines == 0))
        {
          comp->indices.create();
          if (comp->dims.size.y == 0)
            reset_line_buf(&(comp->indices));
        }
    }
  for (c=0; c < num_channels; c++)
    {
      kdrd_channel *chan = channels + c;
      for (w=0; w < KDRD_CHANNEL_LINE_BUFS; w++)
        {
          chan->line_bufs[w].create();
#ifdef KDU_SIMD_OPTIMIZATIONS
          if ((chan->line_type == KDRD_FLOAT_TYPE) &&
              (chan->sampling_numerator != chan->sampling_denominator))
            { // Initialize floats, so extensions don't create exceptions
              kdu_sample32 *sp = chan->line_bufs[w].get_buf32();
              int width = chan->line_bufs[w].get_width();
              width += (4-width)&3; // Round to a multiple of 4
              if (chan->sampling_numerator.x != chan->sampling_denominator.x)
                { sp -= 6; width += 6; }
              for (; width > 0; width--, sp++)
                sp->fval = 0.0F;
            }
#endif // KDU_SIMD_OPTIMIZATIONS
        }
    }
  
  // Fill out the interpolation kernel lookup tables
  for (c=0; c < num_channels; c++)
    {
      kdrd_channel *chan = channels + c;
      if (chan->sampling_numerator.x != chan->sampling_denominator.x)
        for (w=0; w < 65; w++)
          {
            int sigma_x32 = chan->horz_phase_table[w];
            assert(sigma_x32 <= 32);
#ifdef KDU_SIMD_OPTIMIZATIONS
            if (chan->line_type == KDRD_FLOAT_TYPE)
              { // Try installing a shuffle-based resampling function first
                int type = KDRD_SIMD_KERNEL_HORZ_FLOATS;
                if (w == 0)
                  { 
                    chan->simd_horz_float_func =
                      chan->h_kernels.get_simd_horz_float_func(
                                               chan->simd_horz_kernel_len,
                                               chan->simd_horz_leadin,
                                               chan->simd_horz_blend_vecs);
                    chan->simd_horz_fix16_func = NULL;
                  }
                if (chan->simd_horz_float_func != NULL)
                  chan->simd_horz_interp_kernels[w] =
                    chan->h_kernels.get_simd_kernel(type,sigma_x32);
              }
            else
              { // Try installing a shuffle-based resampling function first
                int type = KDRD_SIMD_KERNEL_HORZ_FIX16;
                if (w == 0)
                  { 
                    chan->simd_horz_fix16_func =
                      chan->h_kernels.get_simd_horz_fix16_func(
                                               chan->simd_horz_kernel_len,
                                               chan->simd_horz_leadin,
                                               chan->simd_horz_blend_vecs);
                    chan->simd_horz_float_func = NULL;
                  }
                if (chan->simd_horz_fix16_func != NULL)
                  chan->simd_horz_interp_kernels[w] =
                    chan->h_kernels.get_simd_kernel(type,sigma_x32);
              }
#endif // KDU_SIMD_OPTIMIZATIONS
            int idx = sigma_x32 * KDRD_INTERP_KERNEL_STRIDE;
            if (chan->line_type == KDRD_FLOAT_TYPE)
              chan->horz_interp_kernels[w] = chan->h_kernels.float_kernels+idx;
            else
              chan->horz_interp_kernels[w] = chan->h_kernels.fix16_kernels+idx;
          }
          
      if (chan->sampling_numerator.y != chan->sampling_denominator.y)
        for (w=0; w < 65; w++)
          { 
            int sigma_x32 = chan->vert_phase_table[w];
            assert(sigma_x32 <= 32);
#ifdef KDU_SIMD_OPTIMIZATIONS
            if (chan->line_type == KDRD_FLOAT_TYPE)
              { 
                int type = KDRD_SIMD_KERNEL_VERT_FLOATS;
                if (w == 0)
                  { 
                    chan->simd_vert_float_func =
                      chan->v_kernels.get_simd_vert_float_func(
                                               chan->simd_vert_kernel_len);
                    chan->simd_vert_fix16_func = NULL;
                  }
                if (chan->simd_vert_float_func != NULL)
                  chan->simd_vert_interp_kernels[w] =
                    chan->v_kernels.get_simd_kernel(type,sigma_x32);
              }
            else
              { 
                int type = KDRD_SIMD_KERNEL_VERT_FIX16;
                if (w == 0)
                  { 
                    chan->simd_vert_fix16_func =
                    chan->v_kernels.get_simd_vert_fix16_func(
                                             chan->simd_vert_kernel_len);
                    chan->simd_vert_float_func = NULL;
                  }
                if (chan->simd_vert_fix16_func != NULL)
                  chan->simd_vert_interp_kernels[w] =
                    chan->v_kernels.get_simd_kernel(type,sigma_x32);
              }
#endif // KDU_SIMD_OPTIMIZATIONS
            int idx = sigma_x32 * KDRD_INTERP_KERNEL_STRIDE;
            if (chan->line_type == KDRD_FLOAT_TYPE)
              chan->vert_interp_kernels[w] = chan->v_kernels.float_kernels+idx;
            else
              chan->vert_interp_kernels[w] = chan->v_kernels.fix16_kernels+idx;
          }
    }
}

/*****************************************************************************/
/*                       kdu_region_decompressor::finish                     */
/*****************************************************************************/

bool
  kdu_region_decompressor::finish(kdu_exception *exc, bool do_cs_terminate)
{
  bool success;
  int b, c;

  // Make sure all tile banks are closed and their queues terminated
  if (current_bank != NULL)
    { // Make sure we close this one first, in case there are two open banks
      close_tile_bank(current_bank);
    }
  if (tile_banks != NULL)
    for (b=0; b < 2; b++)
      close_tile_bank(tile_banks+b);
  current_bank = background_bank = NULL;
  if (env != NULL)
    { 
      kdu_exception exc_code;
      if (!(env->terminate(&local_env_queue,false,&exc_code) ||
            codestream_failure))
        { // New exception arose inside `terminate'
          codestream_failure = true;
          codestream_failure_exception = exc_code;
        }
      if (do_cs_terminate &&
          !(env->cs_terminate(codestream,&exc_code) || codestream_failure))
        { // New exception arose inside `do_cs_terminate'
          codestream_failure = true;
          codestream_failure_exception = exc_code;
        }
    }

  success = !codestream_failure;
  if ((exc != NULL) && !success)
    *exc = codestream_failure_exception;
  codestream_failure = false;
  env = NULL;

  // Reset any resource pointers which may no longer be valid
  for (c=0; c < num_components; c++)
    components[c].init(0);
  for (c=0; c < num_channels; c++)
    channels[c].init();
  codestream = kdu_codestream(); // Invalidate the internal pointer for safety
  aux_allocator.restart(); // Get ready to use the allocation object again.
  full_render_dims.pos = full_render_dims.size = kdu_coords(0,0);
  num_components = num_channels = 0;
  return success;
}

/*****************************************************************************/
/*                  kdu_region_decompressor::process (bytes)                 */
/*****************************************************************************/

bool
  kdu_region_decompressor::process(kdu_byte **chan_bufs,
                                   bool expand_monochrome,
                                   int pixel_gap, kdu_coords buffer_origin,
                                   int row_gap, int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region, int precision_bits,
                                   bool measure_row_gap_in_pixels)
{
  int extra_mono_channels = 0;
  num_channel_bufs = num_channels;
  if (expand_monochrome && (num_colour_channels == 1))
    { 
      extra_mono_channels = 2;
      num_channel_bufs += 2;
    }
  if (num_channel_bufs > max_channel_bufs)
    {
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  int c;
  bool have_null_bufs = false; // Remove these later
  for (c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs+c;
      cb->buf = chan_bufs[c];
      if (cb->buf == NULL)
        have_null_bufs = true;
      if (c <= extra_mono_channels)
        cb->chan = channels;
      else
        cb->chan = channels + c - extra_mono_channels;
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = precision_bits;
      cb->transfer_signed = false;
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = true;
      if (precision_bits <= 0)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 8;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->fill = false;
      cb->transfer_func = NULL;
    }
  if (have_null_bufs)
    for (c=0; c < num_channel_bufs; c++)
      if (channel_bufs[c].buf == NULL)
        { 
          num_channel_bufs--;
          for (int d=c; d < num_channel_bufs; d++)
            channel_bufs[d] = channel_bufs[d+1];
        }
  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  return process_generic(1,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*                  kdu_region_decompressor::process (words)                 */
/*****************************************************************************/

bool
  kdu_region_decompressor::process(kdu_uint16 **chan_bufs,
                                   bool expand_monochrome,
                                   int pixel_gap, kdu_coords buffer_origin,
                                   int row_gap, int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region, int precision_bits,
                                   bool measure_row_gap_in_pixels)
{
  int extra_mono_channels = 0;
  num_channel_bufs = num_channels;
  if (expand_monochrome && (num_colour_channels == 1))
    { 
      extra_mono_channels = 2;
      num_channel_bufs += 2;
    }
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  int c;
  bool have_null_bufs = false; // Remove these later
  for (c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs+c;
      cb->buf = (kdu_byte *)(chan_bufs[c]);
      if (cb->buf == NULL)
        have_null_bufs = true;
      if (c <= extra_mono_channels)
        cb->chan = channels;
      else
        cb->chan = channels + c - extra_mono_channels;
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = precision_bits;
      cb->transfer_signed = false;
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = true;
      if (precision_bits <= 0)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 16;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->fill = false;
      cb->transfer_func = NULL;
    }
  if (have_null_bufs)
    for (c=0; c < num_channel_bufs; c++)
      if (channel_bufs[c].buf == NULL)
        { 
          num_channel_bufs--;
          for (int d=c; d < num_channel_bufs; d++)
            channel_bufs[d] = channel_bufs[d+1];
        }
  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  return process_generic(2,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*                 kdu_region_decompressor::process (floats)                 */
/*****************************************************************************/

bool 
  kdu_region_decompressor::process(float **chan_bufs, bool expand_monochrome,
                                   int pixel_gap, kdu_coords buffer_origin,
                                   int row_gap, int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region,
                                   bool normalize,
                                   bool measure_row_gap_in_pixels,
                                   bool always_clip_outputs)
{
  int extra_mono_channels = 0;
  num_channel_bufs = num_channels;
  if (expand_monochrome && (num_colour_channels == 1))
    { 
      extra_mono_channels = 2;
      num_channel_bufs += 2;
    }
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  int c;
  bool have_null_bufs = false; // Remove these later
  for (c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs+c;
      cb->buf = (kdu_byte *)(chan_bufs[c]);
      if (cb->buf == NULL)
        have_null_bufs = true;
      if (c <= extra_mono_channels)
        cb->chan = channels;
      else
        cb->chan = channels + c - extra_mono_channels;
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = 0; // Forces floats to use full range of [0,1]
      cb->transfer_signed = false;
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = always_clip_outputs;
      if (!normalize)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 0;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->fill = false;
      cb->transfer_func = NULL;
    }
  if (have_null_bufs)
    for (c=0; c < num_channel_bufs; c++)
      if (channel_bufs[c].buf == NULL)
        { 
          num_channel_bufs--;
          for (int d=c; d < num_channel_bufs; d++)
            channel_bufs[d] = channel_bufs[d+1];
        }
  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  return process_generic(4,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*                 kdu_region_decompressor::process (packed)                 */
/*****************************************************************************/

bool
  kdu_region_decompressor::process(kdu_int32 *buffer,
                                   kdu_coords buffer_origin,
                                   int row_gap, int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region)
{
  if (num_colour_channels == 2)
    { KDU_ERROR_DEV(e,0x12060600); e <<
        KDU_TXT("The convenient, packed 32-bit integer version of "
        "`kdu_region_decompressor::process' may not be used if the number "
        "of colour channels equals 2.");
    }

  num_channel_bufs = 4;
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  
  // Start by filling in the `chan' and `fill' members of each channel-buf
  channel_bufs[0].chan = channels;
  if (num_colour_channels < 3)
    { // Replicate the first channel for all three colours
      channel_bufs[1].chan = channel_bufs[2].chan = channels;
    }
  else
    { 
      channel_bufs[1].chan = channels+1;
      channel_bufs[2].chan = channels+2;
    }
  channel_bufs[0].fill = channel_bufs[1].fill = channel_bufs[2].fill = false;
  if (num_channels > num_colour_channels)
    { // Get alpha from first non-colour channel
      channel_bufs[3].fill = false;
      channel_bufs[3].chan = channels+num_colour_channels;
    }
  else
    { 
      channel_bufs[3].fill = true;
      channel_bufs[3].chan = channels; // Req'd to have a valid `chan' pointer
    }
  
  // Fill in all other channel-buf members
  for (int c=0; c < num_channel_bufs; c++)
    { 
      channel_bufs[c].buf = NULL;   // Fill this in below
      channel_bufs[c].comp_bit_depth = channel_bufs[c].chan->source->bit_depth;
      channel_bufs[c].transfer_precision = 8;
      channel_bufs[c].transfer_signed = false;
      channel_bufs[c].src_scale = 1.0f;
      channel_bufs[c].src_off = 0.0f;
      channel_bufs[c].clip_outputs = true;
      channel_bufs[c].transfer_func = NULL;
    }
  
  // Finally, enter the `buf' pointers into each channel-buf
  kdu_byte *buf = (kdu_byte *) buffer;
  int test_endian = 1;
  if (((kdu_byte *) &test_endian)[0] == 0)
    { // Big-endian byte order: nominally ARGB
      channel_bufs[0].buf = (buf+1);
      channel_bufs[1].buf = (buf+2);
      channel_bufs[2].buf = (buf+3);
      channel_bufs[3].buf = buf+0; // Alpha
    }
  else
    { // Little-endian byte order: nominally BGRA
      channel_bufs[0].buf = (buf+2);
      channel_bufs[1].buf = (buf+1);
      channel_bufs[2].buf = (buf+0);
      channel_bufs[3].buf = buf+3; // Alpha
    }
  
  return process_generic(1,4,buffer_origin,row_gap*4,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*            kdu_region_decompressor::process (interleaved bytes)           */
/*****************************************************************************/

bool
  kdu_region_decompressor::process(kdu_byte *buffer,
                                   int *chan_offsets,
                                   int pixel_gap,
                                   kdu_coords buffer_origin,
                                   int row_gap,
                                   int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region,
                                   int precision_bits,
                                   bool measure_row_gap_in_pixels,
                                   int expand_monochrome, int fill_alpha,
                                   int max_colour_channels)
{
  num_channel_bufs = num_channels;
  if ((num_colour_channels == 1) && (expand_monochrome > 1))
    num_channel_bufs += expand_monochrome-1;
  else
    expand_monochrome = 1;
  fill_alpha -= (num_channels-num_colour_channels);
  if (fill_alpha < 0)
    fill_alpha = 0;
  else
    num_channel_bufs += fill_alpha;
  int keep_colour_channels = num_colour_channels;
  int skip_colour_channels = 0;
  if ((max_colour_channels > 0) &&
      (max_colour_channels < num_colour_channels))
    { 
      skip_colour_channels = num_colour_channels - max_colour_channels;
      keep_colour_channels = max_colour_channels;
      num_channel_bufs -= skip_colour_channels;
    }
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  
  for (int c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs + c;
      cb->buf = buffer + chan_offsets[c];
      
      // Fill in the `fill' and `chan' members
      cb->fill = false;
      if (c < expand_monochrome)
        cb->chan = channels; // All these are mono channels
      else if (c < keep_colour_channels)
        { 
          assert(expand_monochrome == 1);
          cb->chan = channels + c;
        }
      else if ((c+skip_colour_channels) < num_channels)
        cb->chan = channels + c + skip_colour_channels;
      else
        { 
          cb->fill = true;
          cb->chan = channels; // Required to have a valid `chan' pointer
        }
      
      // Fill in the remaining members
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = precision_bits;
      cb->transfer_signed = false; 
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = true;
      if (cb->fill)
        { // There is no real channel associated with the filled buffers, so
          // there can be no concept of a native precision or signed/unsigned
          // characteristic, as explained in the API documentation.
          assert(fill_alpha > 0);
          if ((precision_bits < 1) || (precision_bits > 8))
            cb->transfer_precision = 8; // Have to assume 0xFF fill
        }
      else if (precision_bits <= 0)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 8;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->transfer_func = NULL;
    }  
  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  bool result;
  result=process_generic(1,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
  return result;
}

/*****************************************************************************/
/*            kdu_region_decompressor::process (interleaved words)           */
/*****************************************************************************/

bool 
  kdu_region_decompressor::process(kdu_uint16 *buffer,
                                   int *chan_offsets,
                                   int pixel_gap,
                                   kdu_coords buffer_origin,
                                   int row_gap,
                                   int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region,
                                   int precision_bits,
                                   bool measure_row_gap_in_pixels,
                                   int expand_monochrome, int fill_alpha,
                                   int max_colour_channels)
{
  num_channel_bufs = num_channels;
  if ((num_colour_channels == 1) && (expand_monochrome > 1))
    num_channel_bufs += expand_monochrome-1;
  else
    expand_monochrome = 1;
  fill_alpha -= (num_channels-num_colour_channels);
  if (fill_alpha < 0)
    fill_alpha = 0;
  else
    num_channel_bufs += fill_alpha;
  int keep_colour_channels = num_colour_channels;
  int skip_colour_channels = 0;
  if ((max_colour_channels > 0) &&
      (max_colour_channels < num_colour_channels))
    { 
      skip_colour_channels = num_colour_channels - max_colour_channels;
      keep_colour_channels = max_colour_channels;
      num_channel_bufs -= skip_colour_channels;
    }
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }
  
  for (int c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs + c;
      cb->buf = (kdu_byte *)(buffer + chan_offsets[c]);
      
      // Fill in the `fill' and `chan' members
      cb->fill = false;
      if (c < expand_monochrome)
        cb->chan = channels; // All these are mono channels
      else if (c < keep_colour_channels)
        { 
          assert(expand_monochrome == 1);
          cb->chan = channels + c;
        }
      else if ((c+skip_colour_channels) < num_channels)
        cb->chan = channels + c + skip_colour_channels;
      else
        { 
          cb->fill = true;
          cb->chan = channels; // Required to have a valid `chan' pointer
        }

      // Fill in the remaining members
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = precision_bits;
      cb->transfer_signed = false; 
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = true;
      if (cb->fill)
        { // There is no real channel associated with the filled buffers, so
          // there can be no concept of a native precision or signed/unsigned
          // characteristic, as explained in the API documentation.
          assert(fill_alpha > 0);
          if ((precision_bits < 1) || (precision_bits > 16))
            cb->transfer_precision = 16; // Have to assume 0xFFFF fill
        }
      else if (precision_bits <= 0)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 16;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->transfer_func = NULL;
    }  
  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  return process_generic(2,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*            kdu_region_decompressor::process (interleaved floats)          */
/*****************************************************************************/

bool 
  kdu_region_decompressor::process(float *buffer, int *chan_offsets,
                                   int pixel_gap, kdu_coords buffer_origin,
                                   int row_gap, int suggested_increment,
                                   int max_region_pixels,
                                   kdu_dims &incomplete_region,
                                   kdu_dims &new_region, bool normalize,
                                   bool measure_row_gap_in_pixels,
                                   int expand_monochrome, int fill_alpha,
                                   int max_colour_channels,
                                   bool always_clip_outputs)
{
  num_channel_bufs = num_channels;
  if ((num_colour_channels == 1) && (expand_monochrome > 1))
    num_channel_bufs += expand_monochrome-1;
  else
    expand_monochrome = 1;
  fill_alpha -= (num_channels-num_colour_channels);
  if (fill_alpha < 0)
    fill_alpha = 0;
  else
    num_channel_bufs += fill_alpha;
  int keep_colour_channels = num_colour_channels;
  int skip_colour_channels = 0;
  if ((max_colour_channels > 0) &&
      (max_colour_channels < num_colour_channels))
    { 
      skip_colour_channels = num_colour_channels - max_colour_channels;
      keep_colour_channels = max_colour_channels;
      num_channel_bufs -= skip_colour_channels;
    }
  if (num_channel_bufs > max_channel_bufs)
    { 
      max_channel_bufs = num_channel_bufs;
      if (channel_bufs != NULL)
        { delete[] channel_bufs; channel_bufs = NULL; }
      channel_bufs = new kdrd_channel_buf[max_channel_bufs];
    }

  for (int c=0; c < num_channel_bufs; c++)
    { 
      kdrd_channel_buf *cb = channel_bufs + c;
      cb->buf = (kdu_byte *)(buffer + chan_offsets[c]);
      
      // Fill in the `fill' and `chan' members
      cb->fill = false;
      if (c < expand_monochrome)
        cb->chan = channels; // All these are mono channels
      else if (c < keep_colour_channels)
        { 
          assert(expand_monochrome == 1);
          cb->chan = channels + c;
        }
      else if ((c+skip_colour_channels) < num_channels)
        cb->chan = channels + c + skip_colour_channels;
      else
        { 
          cb->fill = true;
          cb->chan = channels; // Required to have a valid `chan' pointer
        }

      // Fill in the remaining members
      cb->comp_bit_depth = cb->chan->source->bit_depth;
      cb->transfer_precision = 0; // Forces floats to use full range of [0,1]
      cb->transfer_signed = false;
      cb->src_scale = 1.0f;
      cb->src_off = 0.0f;
      cb->clip_outputs = always_clip_outputs;
      if (cb->fill)
        { // There is no real channel associated with the filled buffers, so
          // there can be no concept of a native precision or signed/unsigned
          // characteristic, as explained in the API documentation.
          assert(fill_alpha > 0);
        }
      else if (!normalize)
        { 
          if ((cb->transfer_precision = cb->chan->native_precision) <= 0)
            cb->transfer_precision = 0;
          cb->transfer_signed = cb->chan->native_signed;
        }
      cb->transfer_func = NULL;
    }

  if (measure_row_gap_in_pixels)
    row_gap *= pixel_gap;
  return process_generic(4,pixel_gap,buffer_origin,row_gap,suggested_increment,
                         max_region_pixels,incomplete_region,new_region);
}

/*****************************************************************************/
/*                  kdu_region_decompressor::process_generic                 */
/*****************************************************************************/

bool
  kdu_region_decompressor::process_generic(int sample_bytes, int pixel_gap,
                                           kdu_coords buffer_origin,
                                           int row_gap,
                                           int suggested_increment,
                                           int max_region_pixels,
                                           kdu_dims &incomplete_region,
                                           kdu_dims &new_region)
{
  new_region.size = kdu_coords(0,0); // In case we decompress nothing
  if (codestream_failure || !incomplete_region)
    return false;
  
  try { // Protect, in case a fatal error is generated by the decompressor
      int suggested_ref_comp_samples = suggested_increment;
      if ((suggested_increment <= 0) && (row_gap == 0))
        { // Need to consider `max_region_pixels' argument
          kdu_long num = channels[0].sampling_numerator.x;
          num *= channels[0].sampling_numerator.y;
          kdu_long den = channels[0].sampling_denominator.x;
          den *= channels[0].sampling_denominator.y;
          double scale = ((double) num) / ((double) den);
          suggested_ref_comp_samples = 1 + (int)(scale * max_region_pixels);
        }
    
      if ((current_bank == NULL) && (background_bank != NULL))
        {
          make_tile_bank_current(background_bank,incomplete_region);
          background_bank = NULL;
        }
      if (current_bank == NULL)
        {
          kdrd_tile_bank *new_bank = tile_banks + 0;
          if (!start_tile_bank(new_bank,suggested_ref_comp_samples,
                               incomplete_region))
            return false; // Trying to discard too many resolution levels or
                          // else trying to flip an unflippable image
          if (new_bank->num_tiles == 0)
            { // Opened at least one tile, but all such tiles
              // were found to lie outside the `conservative_ref_comp_region'
              // and so were closed again immediately.
              if ((next_tile_idx.x == valid_tiles.pos.x) &&
                  (next_tile_idx.y >= (valid_tiles.pos.y+valid_tiles.size.y)))
                { // No more tiles can lie within the `incomplete_region'
                  incomplete_region.pos.y += incomplete_region.size.y;
                  incomplete_region.size.y = 0;
                  return false;
                }
              return true;
            }
          make_tile_bank_current(new_bank,incomplete_region);
        }
      if ((env != NULL) && (background_bank == NULL) &&
          (next_tile_idx.y < (valid_tiles.pos.y+valid_tiles.size.y)))
        {
          background_bank = tile_banks + ((current_bank==tile_banks)?1:0);
          if (!start_tile_bank(background_bank,suggested_ref_comp_samples,
                               incomplete_region))
            return false; // Trying to discard too many resolution levels or
                          // else trying to flip an unflippable image
          if (background_bank->num_tiles == 0)
            { // Opened at least one tile, but all such tiles were found to
              // lie outside the `conservative_ref_comp_region' and so
              // were closed again immediately. No problem; just don't do any
              // background processing at present.
              background_bank = NULL;
            }
        }

      bool last_bank_in_row =
        ((render_dims.pos.x+render_dims.size.x) >=
         (incomplete_region.pos.x+incomplete_region.size.x));
      bool first_bank_in_new_row = current_bank->freshly_created &&
        (render_dims.pos.x <= incomplete_region.pos.x);
      if ((last_bank_in_row || first_bank_in_new_row) &&
          (render_dims.pos.y > incomplete_region.pos.y))
        { // Incomplete region must have shrunk horizontally and the
          // application has no way of knowing that we have already rendered
          // some initial lines of the new, narrower incomplete region.
          int y = render_dims.pos.y - incomplete_region.pos.y;
          incomplete_region.size.y -= y;
          incomplete_region.pos.y += y;
        }
      kdu_dims incomplete_bank_region = render_dims & incomplete_region;
      if (!incomplete_bank_region)
        { // No intersection between current tile bank and incomplete region.
          close_tile_bank(current_bank);
          current_bank = NULL;
                // When we consider multiple concurrent banks later on, we will
                // need to modify the code here.
          return true; // Let the caller come back for more tile processing
        }
      current_bank->freshly_created = false; // We are about to decompress data
    
      // Determine an appropriate number of rendered output lines to process
      // before returning.  Note that some or all of these lines might not
      // intersect with the incomplete region.
      new_region = incomplete_bank_region;
      new_region.size.y = 0;
      kdu_long new_lines =
        1 + (suggested_ref_comp_samples / current_bank->dims.size.x);
          // Starts off as a bound on the number of decompressed lines
      kdu_long den = channels[0].sampling_denominator.y;
      kdu_long num = channels[0].sampling_numerator.y;
      if (den > num)
        new_lines = (new_lines * den) / num;
          // Now `new_lines' is a bound on the number of rendered lines
      if (new_lines > (kdu_long) incomplete_bank_region.size.y)
        new_lines = incomplete_bank_region.size.y;
      if ((row_gap == 0) &&
          ((new_lines*(kdu_long)new_region.size.x) >
           (kdu_long)max_region_pixels))
        new_lines = max_region_pixels / new_region.size.x;
      if (new_lines <= 0)
        { KDU_ERROR_DEV(e,13); e <<
            KDU_TXT("Channel buffers supplied to "
            "`kdr_region_decompressor::process' are too small to "
            "accommodate even a single line of the new region "
            "to be decompressed.  You should be careful to ensure that the "
            "buffers are at least as large as the width indicated by the "
            "`incomplete_region' argument passed to the `process' "
            "function.  Also, be sure to identify the buffer sizes "
            "correctly through the `max_region_pixels' argument supplied "
            "to that function.");
        }

      // Align buffers at start of new region and set `row_gap' if necessary
      int c;
      if (row_gap == 0)
        row_gap = new_region.size.x * pixel_gap;
      else
        {
          size_t buf_offset = ((size_t) sample_bytes) *
            (((size_t)(new_region.pos.y-buffer_origin.y))*(size_t) row_gap +
             ((size_t)(new_region.pos.x-buffer_origin.x))*(size_t) pixel_gap);
          for (c=0; c < num_channel_bufs; c++)
            channel_bufs[c].buf += buf_offset;
        }
      if (row_gap <= 0)
        { KDU_ERROR_DEV(e,0x16021608); e <<
          KDU_TXT("Buffer dimensions exceed internal representation range!  "
                  "You may be able to render this source at a reduced scale.");
        }
    
      // Adjust `row_gap' to count bytes, rather than samples
      row_gap *= sample_bytes;
    
      // Determine and process new region.
      int skip_cols = new_region.pos.x - render_dims.pos.x;
      int num_cols = new_region.size.x;
      kdrd_interleaved_transfer_func ilv_xfer_func =
        configure_transfer_functions(channel_bufs,num_channel_bufs,
                                     sample_bytes,skip_cols,num_cols,
                                     pixel_gap,want_true_zero,
                                     want_true_max,cc_normalized_max);
      union {
        kdu_uint32 dword;
        kdu_byte bytes[4];
      } ilv_fill_mask;
      union {
        kdu_uint32 dword;
        kdu_byte bytes[4];
      } ilv_zero_mask;
      ilv_fill_mask.dword = 0; // Do not fill anything in
      ilv_zero_mask.dword = 0xFFFFFFFF; // Do not zero anything out
      kdrd_channel *ilv_chans[4]={NULL,NULL,NULL,NULL};
      kdu_byte *ilv_buffer_base = NULL;
      if (ilv_xfer_func != NULL)
        { 
          for (c=0; c < 4; c++)
            { 
              int d = channel_bufs[c].ilv_src;
              kdrd_channel_buf *cb = channel_bufs + d;
              ilv_chans[c] = cb->chan;
              if (cb->fill)
                { 
                  ilv_zero_mask.bytes[c] = 0;
                  ilv_fill_mask.bytes[c] = 0xFF;
                  if (cb->transfer_precision < 8)
                    ilv_fill_mask.bytes[c] >>= (8-cb->transfer_precision);
                }
            }
          ilv_buffer_base = channel_bufs[channel_bufs[0].ilv_src].buf;
        }
    
      while (new_lines > 0)
        {
          // Decompress new image component lines as necessary.
          bool anything_needed = false;
          for (c=0; c < num_components; c++)
            {
              kdrd_component *comp = components + c;
              if (comp->needed_line_samples > 0)
                { // Vertical interpolation process needs more data
                  if (comp->dims.size.y <= 0)
                    { // This tile never had any lines in this component.  We
                      // will just use the zeroed `indices' buffer
                      assert(comp->num_tile_lines == 0);
                      comp->new_line_samples = comp->needed_line_samples;
                      comp->needed_line_samples = 0;
                    }
                  else
                    {
                      comp->new_line_samples = 0;
                      anything_needed = true;
                    }
                }
            }
          if (anything_needed)
            {
              int tnum;
              for (tnum=0; tnum < current_bank->num_tiles; tnum++)
                {
                  kdu_multi_synthesis *engine = current_bank->engines + tnum;
                  for (c=0; c < num_components; c++)
                    { 
                      kdrd_component *comp = components + c;
                      if (comp->needed_line_samples <= comp->new_line_samples)
                        continue;
                      kdu_line_buf *line =
                        engine->get_line(comp->rel_comp_idx,env);
                      if ((line == NULL) || (line->get_width() == 0))
                        continue;
                      if (comp->num_line_users > 0)
                        { 
                          assert(tnum < comp->num_tile_lines);
                          comp->tile_lines[tnum] = line;
                          comp->tile_bufs[tnum] = line->get_buf();
                          assert(line->get_width() == comp->tile_widths[tnum]);
                        }
                      if (comp->palette_bits > 0)
                        convert_samples_to_palette_indices(line,
                                         comp->bit_depth,comp->is_signed,
                                         comp->palette_bits,&(comp->indices),
                                         comp->new_line_samples);
                      comp->new_line_samples += line->get_width();
                   }
                }
              for (c=0; c < num_components; c++)
                {
                  kdrd_component *comp = components + c;
                  if (comp->needed_line_samples > 0)
                    {
                      assert(comp->new_line_samples ==
                             comp->needed_line_samples);
                      comp->needed_line_samples = 0;
                      comp->dims.size.y--;
                      comp->dims.pos.y++;
                    }
                }
            }

          // Perform horizontal interpolation/mapping operations for channel
          // lines whose component lines were recently created
          for (c=0; c < num_channels; c++)
            { 
              kdrd_channel *chan = channels + c;
              kdrd_component *comp = chan->source;
              if (comp->new_line_samples == 0)
                continue;
              if ((chan->out_line != NULL) || (chan->horz_line != NULL))
                continue; // Already have something ready for this channel
              if (chan->missing.y < 0)
                { // This channel doesn't need the most recently generated line
                  chan->missing.y++;
                  continue;
                }
              
              if (chan->can_use_component_samples_directly)
                { 
                  assert((comp->num_tile_lines==1) && (chan->missing.x == 0));
                  chan->horz_line = comp->tile_lines[0];
                }
              else
                { 
                  if (chan->in_line == NULL)
                    chan->in_line = chan->get_free_line();
                  int dst_min = chan->in_line_start;
                  int dst_len = chan->in_line_length;
                  const void **src_line_bufs = comp->tile_bufs;
                  int *src_line_widths = comp->tile_widths;
                  int *src_line_types = comp->tile_types;
                  const void *idx_line_buf=NULL;
                  int idx_line_type=KDRD_FIX16_TYPE;
                  int num_src_lines = comp->num_tile_lines;
                  src_line_bufs += comp->initial_empty_tile_lines;
                  src_line_widths += comp->initial_empty_tile_lines;
                  src_line_types += comp->initial_empty_tile_lines;
                  num_src_lines -= comp->initial_empty_tile_lines;
                  if (num_src_lines <= 0)
                    { // Use the all-zero line stored in `comp->indices'
                      num_src_lines = 1;
                      idx_line_buf = comp->indices.get_buf();
                      src_line_bufs = &idx_line_buf;
                      src_line_widths = &(comp->dims.size.x);
                      src_line_types = &idx_line_type;
                    }
                  if (chan->lut_fix16 == NULL)
                    { 
                      int float_exp_bits = chan->interp_float_exp_bits;
                      void *dst_buf = chan->in_line->get_buf();
                      if (chan->boxcar_log_size == 0)
                        chan->convert_and_copy_func(src_line_bufs,
                                                    src_line_widths,
                                                    src_line_types,
                                                    num_src_lines,
                                                    comp->bit_depth,
                                                    chan->missing.x,
                                                    dst_buf,dst_min,dst_len,
                                                    chan->line_type,
                                                    float_exp_bits);
                      else
                        chan->convert_and_add_func(src_line_bufs,
                                                   src_line_widths,
                                                   src_line_types,
                                                   num_src_lines,
                                                   comp->bit_depth,
                                                   chan->missing.x,
                                                   dst_buf,dst_min,dst_len,
                                                   chan->line_type,
                                                   chan->boxcar_size.x,
                                                   (chan->in_precision -
                                                    chan->boxcar_log_size),
                                                   chan->boxcar_lines_left,
                                                   chan->boxcar_size.y,
                                                   float_exp_bits);
                      int fixpoint_int_bits = chan->interp_fixpoint_int_bits;
                      if (fixpoint_int_bits != 0)
                        adjust_fixpoint_formatted_line(dst_buf,dst_min,dst_len,
                                                       chan->line_type,
                                                       comp->is_signed,
                                                       fixpoint_int_bits);
                    }
                  else if (chan->line_type == KDRD_FLOAT_TYPE)
                    { // Use the high precision palette lookup table
                      assert(chan->in_precision == KDU_FIX_POINT);
                      if (chan->boxcar_log_size == 0)
                        perform_palette_map(&(comp->indices),chan->missing.x,
                                            chan->lut_float,
                                            chan->in_line->get_buf(),
                                            dst_min,dst_len,chan->line_type);
                      else
                        map_and_integrate(&(comp->indices),chan->missing.x,
                                          chan->lut_float,
                                          chan->in_line->get_buf(),
                                          dst_min,dst_len,chan->line_type,
                                          chan->boxcar_size.x,
                                          (chan->in_precision -
                                           chan->boxcar_log_size),
                                          chan->boxcar_lines_left,
                                          chan->boxcar_size.y);
                    }
                  else
                    { // Use the fix16 palette lookup table
                      assert(chan->line_type == KDRD_FIX16_TYPE);
                      assert(chan->in_precision == KDU_FIX_POINT);
                      if (chan->boxcar_log_size == 0)
                        perform_palette_map(&(comp->indices),chan->missing.x,
                                            chan->lut_fix16,
                                            chan->in_line->get_buf(),
                                            dst_min,dst_len,chan->line_type);
                      else
                        map_and_integrate(&(comp->indices),chan->missing.x,
                                          chan->lut_fix16,
                                          chan->in_line->get_buf(),
                                          dst_min,dst_len,chan->line_type,
                                          chan->boxcar_size.x,
                                          (chan->in_precision -
                                           chan->boxcar_log_size),
                                          chan->boxcar_lines_left,
                                          chan->boxcar_size.y);
                    }
                  
                  chan->boxcar_lines_left--;
                  if (chan->boxcar_lines_left > 0)
                    continue;

                  if (chan->sampling_numerator.x==chan->sampling_denominator.x)
                    chan->horz_line = chan->in_line;
                  else
                    { // Perform disciplined horizontal resampling
                      chan->horz_line = chan->get_free_line();
                      if (chan->line_type == KDRD_FLOAT_TYPE)
                        { 
#ifdef KDU_SIMD_OPTIMIZATIONS
                          if (chan->simd_horz_float_func != NULL)
                            chan->simd_horz_float_func(
                                  chan->out_line_length,
                                  &(chan->in_line->get_buf32()->fval),
                                  &(chan->horz_line->get_buf32()->fval),
                                  (kdu_uint32)chan->sampling_phase.x,
                                  (kdu_uint32)chan->sampling_numerator.x,
                                  (kdu_uint32)chan->sampling_denominator.x,
                                  chan->sampling_phase_shift.x,
                                  chan->simd_horz_interp_kernels,
                                  chan->simd_horz_kernel_len,
                                  chan->simd_horz_leadin,
                                  chan->simd_horz_blend_vecs);
                          else
#endif // KDU_SIMD_OPTIMIZATIONS
                          do_horz_resampling_float(
                                 chan->out_line_length,
                                 chan->in_line,chan->horz_line,
                                 chan->sampling_phase.x,
                                 chan->sampling_numerator.x,
                                 chan->sampling_denominator.x,
                                 chan->sampling_phase_shift.x,
                                 chan->h_kernels.kernel_length,
                                 (float **)chan->horz_interp_kernels);
                        }
                      else
                        {
#ifdef KDU_SIMD_OPTIMIZATIONS
                          if (chan->simd_horz_fix16_func != NULL)
                            chan->simd_horz_fix16_func(
                                  chan->out_line_length,
                                  &(chan->in_line->get_buf16()->ival),
                                  &(chan->horz_line->get_buf16()->ival),
                                  (kdu_uint32)chan->sampling_phase.x,
                                  (kdu_uint32)chan->sampling_numerator.x,
                                  (kdu_uint32)chan->sampling_denominator.x,
                                  chan->sampling_phase_shift.x,
                                  chan->simd_horz_interp_kernels,
                                  chan->simd_horz_kernel_len,
                                  chan->simd_horz_leadin,
                                  chan->simd_horz_blend_vecs);
                          else
#endif // KDU_SIMD_OPTIMIZATIONS                            
                          do_horz_resampling_fix16(
                                 chan->out_line_length,
                                 chan->in_line,chan->horz_line,
                                 chan->sampling_phase.x,
                                 chan->sampling_numerator.x,
                                 chan->sampling_denominator.x,
                                 chan->sampling_phase_shift.x,
                                 chan->h_kernels.kernel_length,
                                 (kdu_int32 **)chan->horz_interp_kernels);
                        }
                      chan->recycle_line(chan->in_line);
                    }
                  chan->in_line = NULL;
                }
            }
                    
          // Now generate channel output lines wherever we can and see whether
          // or not all channels are ready.
          bool all_channels_ready = true;
          for (c=0; c < num_channels; c++)
            {
              kdrd_channel *chan = channels + c;
              if (chan->out_line != NULL)
                continue;
              if (chan->horz_line == NULL)
                { all_channels_ready = false; continue; }
              if (chan->sampling_numerator.y == chan->sampling_denominator.y)
                {
                  chan->out_line = chan->horz_line;
                  chan->horz_line = NULL;
                }
              else
                { // Perform disciplined vertical resampling
                  chan->append_vline(chan->horz_line);
                  if (chan->num_valid_vlines < KDRD_CHANNEL_VLINES)
                    chan->horz_line = NULL; // Wait until all vlines available
                  else
                    { // Generate an output line now
                      chan->out_line = chan->get_free_line();
                      int s = chan->sampling_phase_shift.y;
                      kdu_uint32 p =
                        ((kdu_uint32)(chan->sampling_phase.y+((1<<s)>>1)))>>s;
                      if (chan->line_type == KDRD_FLOAT_TYPE)
                        { 
#ifdef KDU_SIMD_OPTIMIZATIONS
                          if (chan->simd_vert_float_func != NULL)
                            chan->simd_vert_float_func(
                                  chan->out_line_length,
                                  (float **)chan->vline_bufs,
                                  &(chan->out_line->get_buf32()->fval),
                                  chan->simd_vert_interp_kernels[p],
                                  chan->simd_vert_kernel_len);
                        else
#endif // KDU_SIMD_OPTIMIZATIONS
                          do_vert_resampling_float(
                                   chan->out_line_length,
                                   chan->vlines,chan->out_line,
                                   chan->v_kernels.kernel_length,(float *)
                                   (chan->vert_interp_kernels[p]));
                        }
                      else
                        { 
#ifdef KDU_SIMD_OPTIMIZATIONS
                          if (chan->simd_vert_fix16_func != NULL)
                            chan->simd_vert_fix16_func(
                                       chan->out_line_length,
                                       (kdu_int16 **)chan->vline_bufs,
                                       &(chan->out_line->get_buf16()->ival),
                                       chan->simd_vert_interp_kernels[p],
                                       chan->simd_vert_kernel_len);
                          else
#endif // KDU_SIMD_OPTIMIZATIONS
                          do_vert_resampling_fix16(
                                   chan->out_line_length,
                                   chan->vlines,chan->out_line,
                                   chan->v_kernels.kernel_length,(kdu_int32 *)
                                   (chan->vert_interp_kernels[p]));
                        }
                      chan->sampling_phase.y += chan->sampling_numerator.y;
                      while (((kdu_uint32) chan->sampling_phase.y) >=
                             ((kdu_uint32) chan->sampling_denominator.y))
                        {
                          chan->horz_line = NULL; // So we make a new one
                          chan->sampling_phase.y-=chan->sampling_denominator.y;
                          chan->roll_vlines();
                          assert(chan->num_valid_vlines > 0);
                            // Otherwise should be using boxcar subsampling
                            // to avoid over-extending the vertical interp
                            // kernels.
                        }
                    }
                }
              if (chan->out_line == NULL)
                { all_channels_ready = false; continue; }
              if (chan->stretch_residual > 0)
                { 
                  assert(chan->line_type == KDRD_FIX16_TYPE);
                  kdu_int16 *in16 = &(chan->out_line->get_buf16()->ival);
                  kdu_int16 *out16 = in16;
                  if ((chan->source->num_tile_lines > 0) &&
                      (chan->out_line == chan->source->tile_lines[0]))
                    { // Don't do white stretching in place
                      chan->out_line = chan->get_free_line();
                      out16 = &(chan->out_line->get_buf16()->ival);
                    }
                  chan->white_stretch_func(in16,out16,chan->out_line_length,
                                           chan->stretch_residual);
                }
            }
          
          // Mark the source component lines which we have fully consumed
          // and determine the components from which we need more
          // decompressed samples.
          for (c=0; c < num_channels; c++)
            {
              kdrd_channel *chan = channels+c;
              kdrd_component *comp = chan->source;
              if (comp->new_line_samples > 0)
                {
                  if (chan->missing.y > 0)
                    chan->missing.y--; // Keep using the same samples
                  else if (comp->dims.size.y > 0)
                    comp->new_line_samples = 0;
                }
              if ((chan->horz_line == NULL) &&
                  ((chan->out_line == NULL) || all_channels_ready))
                {
                  if (chan->in_line == NULL)
                    chan->boxcar_lines_left = chan->boxcar_size.y;
                  if (comp->new_line_samples == 0)
                    comp->needed_line_samples = comp->dims.size.x;
                }
            }
          
          if (!all_channels_ready)
            continue;
          
          if (render_dims.pos.y == incomplete_bank_region.pos.y)
            { // Line has a non-empty intersection with the incomplete region
              if (colour_converter != NULL)
                { // In this case, we apply `colour_converter' once for each
                  // vertical output line.  We need to do this if the channels
                  // have different vertical interpolation properties.
                  if (num_colour_channels < 3)
                    colour_converter->convert_lum(*(channels[0].out_line),
                                                  render_dims.size.x);
                  else if (num_colour_channels == 3)
                    colour_converter->convert_rgb(*(channels[0].out_line),
                                                  *(channels[1].out_line),
                                                  *(channels[2].out_line),
                                                  render_dims.size.x);
                  else
                    colour_converter->convert_rgb4(*(channels[0].out_line),
                                                   *(channels[1].out_line),
                                                   *(channels[2].out_line),
                                                   *(channels[3].out_line),
                                                   render_dims.size.x);
                }

              if (ilv_xfer_func != NULL)
                { 
                  assert(num_channel_bufs == 4);
                  ilv_xfer_func(ilv_chans[0]->out_line->get_buf(),
                                ilv_chans[1]->out_line->get_buf(),
                                ilv_chans[2]->out_line->get_buf(),
                                ilv_chans[3]->out_line->get_buf(),
                                channel_bufs[0].comp_bit_depth,
                                ilv_chans[0]->line_type,skip_cols,num_cols,
                                ilv_buffer_base,channel_bufs->transfer_precision,
                                ilv_zero_mask.dword,ilv_fill_mask.dword);
                  ilv_buffer_base += row_gap;
                }
              else
                for (c=0; c < num_channel_bufs; c++)
                  { 
                    kdrd_channel_buf *cb = channel_bufs + c;
                    kdrd_channel *chan = cb->chan;
                    cb->transfer_func(chan->out_line->get_buf(),
                                      cb->comp_bit_depth,chan->line_type,
                                      skip_cols,num_cols,cb->buf,
                                      cb->transfer_precision,pixel_gap,
                                      cb->transfer_signed,
                                      cb->src_scale,cb->src_off,
                                      cb->clip_outputs);
                    cb->buf += row_gap; // Already measured in bytes
                  }

              incomplete_bank_region.pos.y++;
              incomplete_bank_region.size.y--;
              new_region.size.y++; // Transferred data region grows by one row.
              if (last_bank_in_row)
                {
                  int y = (render_dims.pos.y+1) - incomplete_region.pos.y;
                  assert(y > 0);
                  incomplete_region.pos.y += y;
                  incomplete_region.size.y -= y;
                }
            }
          
          // Mark all the channel output lines as consumed so we can generate
          // some new ones.
          for (c=0; c < num_channels; c++)
            {
              channels[c].recycle_line(channels[c].out_line);
              channels[c].out_line = NULL;
            }

          new_lines--;
          render_dims.pos.y++;
          render_dims.size.y--;
        }
    
      if (!incomplete_bank_region)
        { // Done all the processing we need for this tile.
          close_tile_bank(current_bank);
          current_bank = NULL;
          return true;
        }
    }
  catch (kdu_exception exc)
    {
      codestream_failure = true;
      codestream_failure_exception = exc;
      if (env != NULL)
        env->handle_exception(exc);
      return false;
    }
  catch (std::bad_alloc &)
    {
      codestream_failure = true;
      codestream_failure_exception = KDU_MEMORY_EXCEPTION;
      if (env != NULL)
        env->handle_exception(KDU_MEMORY_EXCEPTION);
      return false;
    }
  return true;
}
