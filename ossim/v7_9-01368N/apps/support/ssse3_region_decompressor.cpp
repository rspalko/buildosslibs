/*****************************************************************************/
// File: ssse3_region_decompressor.cpp [scope = APPS/SUPPORT]
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
   Provides SIMD implementations to accelerate horizontal and vertical
resampling operations on behalf of the `kdu_region_decompressor' object.
The functions implemented in this source file require at most SSSE3 support,
but the file can be successfully included in build environments that do not
offer SSSE3 support, so long as `KDU_NO_SSSE3' is defined, or
`KDU_X86_INTRINSICS' is not defined.
******************************************************************************/
#include "kdu_arch.h"

#if ((!defined KDU_NO_SSSE3) && (defined KDU_X86_INTRINSICS))

#include <tmmintrin.h>
#include <assert.h>

#  define kdrd_alignr_ps(_a,_b,_sh) \
  _mm_castsi128_ps(_mm_alignr_epi8(_mm_castps_si128(_a), \
                                   _mm_castps_si128(_b),_sh))

namespace kd_supp_simd {
  using namespace kdu_core;


/* ========================================================================= */
/*                       Horizontal Resampling Functions                     */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                   ssse3_horz_resample_float                        */
/*****************************************************************************/

void
  ssse3_horz_resample_float(int length, float *src, float *dst,
                            kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                            int pshift, void **kernels, int kernel_length,
                            int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x4 = ((kdu_int64) num) << 2; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x4/den); // Minimum value of adj=[(phase+num_x4)/den]
                                   // required to advance to the next vector.
  assert(min_adj < 12); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x4 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  float *sp_base = src;
  __m128 *dp = (__m128 *) dst;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 4 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 4));
        // The above conditions should have been checked during func ptr init
      for (; length > 0; length-=4, dp++)
        { 
          __m128 *kern = (__m128 *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          __m128 ival = _mm_loadu_ps(sp_base);
          __m128 val, sum;
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          val = _mm_shuffle_ps(ival,ival,0x00);
          sum = _mm_mul_ps(val,kern[0]);
          val = _mm_shuffle_ps(ival,ival,0x55); 
          sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[1]));
          val = _mm_shuffle_ps(ival,ival,0xAA);
          sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[2]));
          if (kernel_length > 3)
            { 
              val = _mm_shuffle_ps(ival,ival,0xFF);
              sum = _mm_add_ps(sum,_mm_mul_ps(val,kern[3]));
            }
          *dp = sum;
        }
    }
  else
    { 
      sp_base -= leadin;
      for (; length > 0; length-=4, dp++)
        { 
          __m128 *kern = (__m128 *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          float *sp = sp_base; // Note; this is not aligned
          __m128 val, val1, val2, sum=_mm_setzero_ps();
          val1 = _mm_loadu_ps(sp); sp += 4;
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          int kl;
          for (kl=kernel_length; kl > 3; kl-=4, kern+=4)
            { 
              val2 = _mm_loadu_ps(sp); sp += 4;
              val = _mm_mul_ps(val1,kern[0]); sum = _mm_add_ps(sum,val);
              val = kdrd_alignr_ps(val2,val1,4);
              val = _mm_mul_ps(val,kern[1]); sum = _mm_add_ps(sum,val);
              val = kdrd_alignr_ps(val2,val1,8);
              val = _mm_mul_ps(val,kern[2]); sum = _mm_add_ps(sum,val);
              val = kdrd_alignr_ps(val2,val1,12);
              val = _mm_mul_ps(val,kern[3]); sum = _mm_add_ps(sum,val);
              val1 = val2;
            }
          if (kl > 0)
            { 
              val = _mm_mul_ps(val1,kern[0]); sum = _mm_add_ps(sum,val);
              if (kl > 1)
                { 
                  val2 = _mm_loadu_ps(sp);
                  val = kdrd_alignr_ps(val2,val1,4);
                  val = _mm_mul_ps(val,kern[1]); sum = _mm_add_ps(sum,val);
                  if (kl > 2)
                    { 
                      val = kdrd_alignr_ps(val2,val1,8);
                      val = _mm_mul_ps(val,kern[2]); sum = _mm_add_ps(sum,val);
                    }
                }
            }
          *dp = sum;
        }
    }
}

/*****************************************************************************/
/* EXTERN                   ssse3_horz_resample_fix16                        */
/*****************************************************************************/

void
  ssse3_horz_resample_fix16(int length, kdu_int16 *src, kdu_int16 *dst,
                            kdu_uint32 phase, kdu_uint32 num, kdu_uint32 den,
                            int pshift, void **kernels, int kernel_length,
                            int leadin, int blend_vecs)
{
  assert(blend_vecs == 0); // This is the non-shuffle-based implementation
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to adavnce to the next vector.
  assert(min_adj < 24); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
  // Amount we need to add to `phase' if the adj = min_adj.  Note that
  // this value is guaranteed to be strictly less than den < 2^31.  This
  // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
  // integer without risk of numeric overflow.
  
  kdu_int16 *sp_base = src;
  __m128i *dp = (__m128i *) dst;
  if (leadin == 0)
    { // In this case, we have to expand `kernel_length' successive input
      // samples each into 8 duplicate copies before applying the SIMD
      // arithmetic.
      assert((kernel_length >= 3) && (kernel_length <= 6));
        // The above conditions should have been checked during func ptr init
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          __m128i val, ival = _mm_loadu_si128((__m128i *) sp_base);          
          sp_base += min_adj;
          ival = _mm_adds_epi16(ival,ival);
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          __m128i sum = _mm_setzero_si128();
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0x00),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[0]));
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0x55),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[1]));
          val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0xAA),0x00);
          sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[2]));
          if (kernel_length > 3)
            { 
              val=_mm_shuffle_epi32(_mm_shufflelo_epi16(ival,0xFF),0x00);
              sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[3]));
              if (kernel_length > 4)
                { 
                  val=_mm_shuffle_epi32(_mm_shufflehi_epi16(ival,0x00),0xAA);
                  sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[4]));
                  if (kernel_length > 5)
                    {
                    val=_mm_shuffle_epi32(_mm_shufflehi_epi16(ival,0x55),0xAA);
                    sum=_mm_sub_epi16(sum,_mm_mulhi_epi16(val,kern[5]));
                    }
                }
            }
          *dp = sum;
        }
    }
  else
    { 
      sp_base -= leadin; 
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          phase += max_phase_adj;
          __m128i *sp = (__m128i *) sp_base; // Note; this is not aligned        
          __m128i val, val1, val2, sum=_mm_setzero_si128();
          val1 = _mm_loadu_si128(sp++); val1 = _mm_adds_epi16(val1,val1);
          sp_base += min_adj;
          if (phase >= den)
            { 
              phase -= den;  sp_base++;
              assert(phase < den);
            }
          int kl;
          for (kl=kernel_length; kl > 7; kl-=8, kern+=8)
            { 
              val2= _mm_loadu_si128(sp++); val2 = _mm_adds_epi16(val2,val2);
              val = _mm_mulhi_epi16(val1,kern[0]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,2);
              val = _mm_mulhi_epi16(val,kern[1]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,4);
              val = _mm_mulhi_epi16(val,kern[2]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,6);
              val = _mm_mulhi_epi16(val,kern[3]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,8);
              val = _mm_mulhi_epi16(val,kern[4]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,10);
              val = _mm_mulhi_epi16(val,kern[5]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,12);
              val = _mm_mulhi_epi16(val,kern[6]); sum=_mm_sub_epi16(sum,val);
              val = _mm_alignr_epi8(val2,val1,14);
              val = _mm_mulhi_epi16(val,kern[7]); sum=_mm_sub_epi16(sum,val);
              val1 = val2;
            }
          if (kl > 0)
            { 
              val = _mm_mulhi_epi16(val1,kern[0]); sum=_mm_sub_epi16(sum,val);
              if (kl > 1)
                { 
                  val2 = _mm_loadu_si128(sp); val2 = _mm_adds_epi16(val2,val2);
                  val = _mm_alignr_epi8(val2,val1,2);
                  val = _mm_mulhi_epi16(val,kern[1]);
                  sum = _mm_sub_epi16(sum,val);
                  if (kl > 2)
                    { 
                      val = _mm_alignr_epi8(val2,val1,4);
                      val = _mm_mulhi_epi16(val,kern[2]);
                      sum = _mm_sub_epi16(sum,val);
                      if (kl > 3)
                        { 
                          val = _mm_alignr_epi8(val2,val1,6);
                          val = _mm_mulhi_epi16(val,kern[3]);
                          sum = _mm_sub_epi16(sum,val);
                          if (kl > 4)
                            { 
                              val = _mm_alignr_epi8(val2,val1,8);
                              val = _mm_mulhi_epi16(val,kern[4]);
                              sum = _mm_sub_epi16(sum,val);
                              if (kl > 5)
                                { 
                                  val = _mm_alignr_epi8(val2,val1,10);
                                  val = _mm_mulhi_epi16(val,kern[5]);
                                  sum = _mm_sub_epi16(sum,val);
                                  if (kl > 6)
                                    { 
                                      val = _mm_alignr_epi8(val2,val1,12);    
                                      val = _mm_mulhi_epi16(val,kern[6]);
                                      sum = _mm_sub_epi16(sum,val);
                                    }
                                }
                            }
                        }
                    }
                }
            }
          *dp = sum;
        }
    }
}

/*****************************************************************************/
/* EXTERN                ssse3_hshuf_float_2tap_expand                       */
/*****************************************************************************/

void
  ssse3_hshuf_float_2tap_expand(int length, float *src, float *dst,
                                kdu_uint32 phase, kdu_uint32 num,
                                kdu_uint32 den, int pshift, void **kernels,
                                int kernel_len, int leadin, int blend_vecs)
{
  assert((leadin == 0) && (blend_vecs > 0) && (kernel_len == 2));
  int off = (1<<pshift)>>1;
  kdu_int64 num_x4 = ((kdu_int64) num) << 2; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x4/den); // Minimum value of adj=[(phase+num_x4)/den]
                                   // required to adavnce to the next vector.
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x4 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  __m128 *dp = (__m128 *) dst;
  __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
  if (blend_vecs == 1)
    { // Sufficient to displace `ival0' in order to get `ival1'
      for (; length > 0; length-=4, dp++)
        { 
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i perm=kern[2];
          __m128 fact0 = ((__m128 *)kern)[0];
          __m128 fact1 = ((__m128 *)kern)[1];
          __m128i ival1 = _mm_srli_si128(ival0,4);
          ival0 = _mm_shuffle_epi8(ival0,perm);
          ival1 = _mm_shuffle_epi8(ival1,perm);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m128i *) kernels[(phase+off)>>pshift];
          __m128 val0 = _mm_mul_ps(_mm_castsi128_ps(ival0),fact0);
          __m128 val1 = _mm_mul_ps(_mm_castsi128_ps(ival1),fact1);
          *dp = _mm_add_ps(val0,val1);
        }      
    }
  else
    { // Expansion factor very close to 1 -- need to read shifted input
      // vector to be sure of getting all required inputs.
      for (; length > 0; length-=4, dp++)
        { 
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i ival1 = _mm_loadu_si128((__m128i *)(src+1));
          __m128i perm=kern[2];
          __m128 fact0 = ((__m128 *)kern)[0];
          __m128 fact1 = ((__m128 *)kern)[1];
          ival0 = _mm_shuffle_epi8(ival0,perm);
          ival1 = _mm_shuffle_epi8(ival1,perm);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m128i *) kernels[(phase+off)>>pshift];
          __m128 val0 = _mm_mul_ps(_mm_castsi128_ps(ival0),fact0);
          __m128 val1 = _mm_mul_ps(_mm_castsi128_ps(ival1),fact1);
          *dp = _mm_add_ps(val0,val1);
        }
    }
}

/*****************************************************************************/
/* EXTERN                 ssse3_hshuf_fix16_2tap_expand                      */
/*****************************************************************************/

void
  ssse3_hshuf_fix16_2tap_expand(int length, kdu_int16 *src, kdu_int16 *dst,
                                kdu_uint32 phase, kdu_uint32 num,
                                kdu_uint32 den, int pshift, void **kernels,
                                int kernel_len, int leadin, int blend_vecs)
{
  assert((leadin == 0) && (blend_vecs > 0) && (kernel_len == 2));
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to adavnce to the next vector.
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  __m128i *dp = (__m128i *) dst;
  __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
  if (blend_vecs == 1)
    { // Sufficient to displace `ival0' in order to get `ival1'
      for (; length > 0; length-=8, dp++)
        { 
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i fact=kern[1];
          __m128i perm=kern[2];
          __m128i ival1 = _mm_srli_si128(ival0,2);
          ival0 = _mm_shuffle_epi8(ival0,perm);
          ival1 = _mm_shuffle_epi8(ival1,perm);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m128i *) kernels[(phase+off)>>pshift];
          ival1 = _mm_sub_epi16(ival1,ival0);
          ival1 = _mm_mulhrs_epi16(ival1,fact);
          *dp = _mm_sub_epi16(ival0,ival1);
        }      
    }
  else
    { // Expansion factor very close to 1 -- need to read shifted input
      // vector to be sure of getting all required inputs.
      for (; length > 0; length-=8, dp++)
        { 
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i ival1 = _mm_loadu_si128((__m128i *)(src+1));
          __m128i fact=kern[1];
          __m128i perm=kern[2];
          ival0 = _mm_shuffle_epi8(ival0,perm);
          ival1 = _mm_shuffle_epi8(ival1,perm);
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          kern = (__m128i *) kernels[(phase+off)>>pshift];
          ival1 = _mm_sub_epi16(ival1,ival0);
          ival1 = _mm_mulhrs_epi16(ival1,fact);
          *dp = _mm_sub_epi16(ival0,ival1);
        }
    }
}

/*****************************************************************************/
/* EXTERN                 ssse3_hshuf_fix16_6tap_expand                      */
/*****************************************************************************/

void
  ssse3_hshuf_fix16_6tap_expand(int length, kdu_int16 *src, kdu_int16 *dst,
                                kdu_uint32 phase, kdu_uint32 num,
                                kdu_uint32 den, int pshift, void **kernels,
                                int kernel_len, int leadin, int blend_vecs)
{
  assert((leadin == 0) && (kernel_len == 6));
  int off = (1<<pshift)>>1;
  kdu_int64 num_x8 = ((kdu_int64) num) << 3; // Possible ovfl without 64 bits
  int min_adj = (int)(num_x8/den); // Minimum value of adj=[(phase+num_x8)/den]
                                   // required to adavnce to the next vector.
  assert(min_adj < 24); // R = num/den is guaranteed to be strictly < 3
  kdu_uint32 max_phase_adj = (kdu_uint32)(num_x8 - (((kdu_int64)min_adj)*den));
    // Amount we need to add to `phase' if the adj = min_adj.  Note that
    // this value is guaranteed to be strictly less than den < 2^31.  This
    // means that `phase' + `max_phase_adj' fits within a 32-bit unsigned
    // integer without risk of numeric overflow.
  
  src -= 2; // 6-tap input always starts at `src[-2]'
  __m128i *dp = (__m128i *) dst;
  if (blend_vecs == 2)
    { 
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i ival1 = _mm_loadu_si128((__m128i *)(src+8));
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          __m128i mval, sum = _mm_setzero_si128();
          mval = _mm_shuffle_epi8(ival0,kern[6]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[7]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[0]));
          mval = _mm_shuffle_epi8(ival0,kern[8]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[9]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[1]));
          mval = _mm_shuffle_epi8(ival0,kern[10]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[11]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[2]));
          mval = _mm_shuffle_epi8(ival0,kern[12]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[13]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[3]));
          mval = _mm_shuffle_epi8(ival0,kern[14]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[15]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[4]));
          mval = _mm_shuffle_epi8(ival0,kern[16]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[17]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[5]));
          *dp = sum;
        }
    }
  else if (blend_vecs == 3)
    { 
      for (; length > 0; length-=8, dp++)
        { 
          __m128i *kern = (__m128i *) kernels[(phase+off)>>pshift];
          __m128i ival0 = _mm_loadu_si128((__m128i *) src);
          __m128i ival1 = _mm_loadu_si128((__m128i *)(src+8));
          __m128i ival2 = _mm_loadu_si128((__m128i *)(src+16));
          phase += max_phase_adj;
          src += min_adj;
          if (phase >= den)
            { 
              phase -= den;  src++;
              assert(phase < den);
            }
          __m128i mval, sum = _mm_setzero_si128();
          mval = _mm_shuffle_epi8(ival0,kern[6]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[7]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[8]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[0]));
          mval = _mm_shuffle_epi8(ival0,kern[9]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[10]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[11]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[1]));
          mval = _mm_shuffle_epi8(ival0,kern[12]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[13]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[14]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[2]));
          mval = _mm_shuffle_epi8(ival0,kern[15]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[16]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[17]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[3]));
          mval = _mm_shuffle_epi8(ival0,kern[18]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[19]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[20]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[4]));
          mval = _mm_shuffle_epi8(ival0,kern[21]);
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival1,kern[22]));
          mval = _mm_add_epi16(mval,_mm_shuffle_epi8(ival2,kern[23]));
          sum = _mm_sub_epi16(sum,_mm_mulhrs_epi16(mval,kern[5]));
          *dp = sum;
        }      
    }
  else
    assert(0);
}

} // namespace kd_supp_simd

#endif // !KDU_NO_SSSE3
