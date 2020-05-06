/*****************************************************************************/
// File: transform_base.h [scope = CORESYS/TRANSFORMS]
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
   Provides local definitions common to both the DWT analysis and the DWT
synthesis implementations in "analysis.cpp" and "synthesis.cpp" that do
not rely on anything other than "kdu_ubiquitous.h" and "kdu_arch.h".  This
allows the header to be safely imported by source files that may be compiled
with different options so that we do not risk creating multiple versions of
exported inline functions that can create problems in some compilation
environments.
******************************************************************************/

#ifndef TRANSFORM_BASE_H
#define TRANSFORM_BASE_H

#include <assert.h>
#include <math.h>
#include "kdu_ubiquitous.h"
#include "kdu_arch.h"

// Objects defined here:
namespace kd_core_local {
  struct kd_lifting_step;
} // namespace kd_core_local

namespace kd_core_local {
  using namespace kdu_core;
  
// Function pointer prototypes defined here, primarily for SIMD acceleration
typedef void (*kd_deinterleave_16_func)
  (kdu_int16 *src, kdu_int16 *dst1, kdu_int16 *dst2, int pairs, int downshift);
typedef void (*kd_deinterleave_32_func)
  (kdu_int32 *src, kdu_int32 *dst1, kdu_int32 *dst2, int pairs);

typedef void (*kd_interleave_16_func)
  (kdu_int16 *src1, kdu_int16 *src2, kdu_int16 *dst, int pairs, int upshift);
typedef void (*kd_interleave_32_func)
  (kdu_int32 *src1, kdu_int32 *src2, kdu_int32 *dst, int pairs);

typedef void (*kd_v_lift_16_func)
  (kdu_int16 **src, kdu_int16 *dst_in, kdu_int16 *dst_out, int num_samples,
   kd_lifting_step *step, bool synthesis);
typedef void (*kd_v_lift_32_func)
  (kdu_int32 **src, kdu_int32 *dst_in, kdu_int32 *dst_out, int num_samples,
   kd_lifting_step *step, bool synthesis);
  /* The above functions perform a single vertical DWT lifting step, for
     analysis or synthesis.  The function pointer may be assigned to a
     function that is analysis-specific or synthesis-specific, in which
     case the last argument is ignored.  Even the `step' argument may be
     ignored, if the function that is actually assigned is designed for
     implementing only one type of transform kernel.  The first prototype is
     for 16-bit reversible or irreversible (fixed-point) transforms.  The
     second is for 32-bit reversible (integer) or irreversible (float)
     transforms.  In the case of floating point data, the `src[i]', `dst_in'
     and `dst_out' pointers are actually `kdu_int32 *' casts of the relevant
     floating point array addresses. */

typedef void (*kd_h_lift_16_func)
  (kdu_int16 *src, kdu_int16 *dst, int num_samples,
   kd_lifting_step *step, bool synthesis);
typedef void (*kd_h_lift_32_func)
  (kdu_int32 *src, kdu_int32 *dst, int num_samples,
   kd_lifting_step *step, bool synthesis);
  /* Same as above, but for a horizontal lifting step. */

/*****************************************************************************/
/*                             kd_lifting_step                               */
/*****************************************************************************/

struct kd_lifting_step {
  public: // Lifting step descriptors
    kdu_byte step_idx; // Runs from 0 to N-1 where N is the number of steps
    kdu_byte support_length;
    kdu_byte downshift;
    kdu_byte extend; // Used only for horizontal lifting steps
    kdu_int16 support_min;
    kdu_int16 rounding_offset;
    float *coeffs; // Valid indices run from 0 to `support_length'
    int *icoeffs; // Valid indices run from 0 to `support_length'
    bool reversible;
    kdu_byte kernel_id; // One of Ckernels_W5X3, Ckernels_W9X7 or Ckernels_ATK
    bool vert_add_shorts_first; // See below
    bool hor_add_shorts_first; // See below
  public: // Function pointers to allow SIMD acceleration
    kd_v_lift_16_func vlift_16_func;
    kd_v_lift_32_func vlift_32_func;
    kd_h_lift_16_func hlift_16_func;
    kd_h_lift_32_func hlift_32_func;
    void reset_func_ptrs()
      { vlift_16_func = NULL; hlift_16_func = NULL;
        vlift_32_func = NULL; hlift_32_func = NULL; }
  public: // Lifting step functions that are suitable for vertical and/or
          // multi-component DWT processing.  It is convenient to place
          // these implementations here, in-line.  It is assumed that any
          // SIMD accelerator function pointers have already been configured
          // for analysis if the present functions are used for analysis, else
          // for synthesis if the prsent functions are used for synthesis.
    void perform_lifting_step(kdu_int16 **src_bufs, kdu_int16 *dst_in,
                              kdu_int16 *dst_out, int width, int start_loc,
                              bool for_synthesis)
      { 
        if (width <= 0) return;
        int k = KDU_ALIGN_SAMPLES16;
        while (start_loc > k) { start_loc -= k; dst_in += k; dst_out += k; }
        width += start_loc;
        if (vlift_16_func != NULL)
          vlift_16_func(src_bufs,dst_in,dst_out,width,this,for_synthesis);
        else if (for_synthesis)
          { 
            if ((support_length==2) && (icoeffs[0]==icoeffs[1]))
              { 
                kdu_int16 *sp1=src_bufs[0], *sp2=src_bufs[1];
                kdu_int32 shift = this->downshift;
                kdu_int32 offset = (1<<shift)>>1;
                kdu_int32 val, i_lambda=this->icoeffs[0];
                if (i_lambda == 1)
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];
                      dst_out[k]=dst_in[k] - (kdu_int16)((offset+val)>>shift);
                  }
                else if (i_lambda == -1)
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];
                      dst_out[k]=dst_in[k] - (kdu_int16)((offset-val)>>shift);
                  }
                else
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];  val *= i_lambda;
                      dst_out[k]=dst_in[k] - (kdu_int16)((offset+val)>>shift);
                  }
              }
            else
              { // More general 16-bit processing
                kdu_int32 shift = this->downshift;
                kdu_int32 offset = this->rounding_offset;
                int t, sum, *fpp, support=this->support_length;
                for (k=start_loc; k < width; k++)
                  { 
                    for (fpp=this->icoeffs, sum=offset, t=0; t < support; t++)
                      sum += fpp[t] * (src_bufs[t])[k];
                    dst_out[k] = dst_in[k] - (kdu_int16)(sum >> shift);
                  }
              }
          }
        else
          { 
            if ((support_length==2) && (icoeffs[0]==icoeffs[1]))
              { 
                kdu_int16 *sp1=src_bufs[0], *sp2=src_bufs[1];
                kdu_int32 shift = this->downshift;
                kdu_int32 offset = (1<<shift)>>1;
                kdu_int32 val, i_lambda=this->icoeffs[0];
                if (i_lambda == 1)
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];
                      dst_out[k]=dst_in[k] + (kdu_int16)((offset+val)>>shift);
                    }
                else if (i_lambda == -1)
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];
                      dst_out[k]=dst_in[k] + (kdu_int16)((offset-val)>>shift);
                    }
                else
                  for (k=start_loc; k < width; k++)
                    { 
                      val = sp1[k];  val += sp2[k];  val *= i_lambda;
                      dst_out[k]=dst_in[k] + (kdu_int16)((offset+val)>>shift);
                    }
              }
            else
              { // More general 16-bit processing
                kdu_int32 shift = this->downshift;
                kdu_int32 offset = this->rounding_offset;
                int t, sum, *fpp, support=this->support_length;
                for (k=start_loc; k < width; k++)
                  { 
                    for (fpp=this->icoeffs, sum=offset, t=0; t < support; t++)
                      sum += fpp[t] * (src_bufs[t])[k];
                    dst_out[k] = dst_in[k] + (kdu_int16)(sum >> shift);
                  }
              }            
          }
      }
    void perform_lifting_step(kdu_int32 **src_bufs, kdu_int32 *dst_in,
                              kdu_int32 *dst_out, int width, int start_loc,
                              bool for_synthesis)
      { 
        if (width <= 0) return;
        int k = KDU_ALIGN_SAMPLES32;
        while (start_loc > k) { start_loc -= k; dst_in += k; dst_out += k; }
        width += start_loc;
        if (vlift_32_func != NULL)
          vlift_32_func(src_bufs,dst_in,dst_out,width,this,for_synthesis);
        else if (for_synthesis)
          { 
            if ((support_length==2) && (coeffs[0]==coeffs[1]))
              { // Special case of symmetric least-dissimilar filters
                if (!this->reversible)
                  { 
                    float lambda = coeffs[0];
                    float *sp1=(float *)(src_bufs[0]);
                    float *sp2=(float *)(src_bufs[1]);
                    float *dp_in=(float *)dst_in, *dp_out=(float *)dst_out;
                    for (k=start_loc; k < width; k++)
                      dp_out[k] = dp_in[k] - lambda*(sp1[k]+sp2[k]);
                  }
                else
                  { 
                    kdu_int32 *sp1=src_bufs[0], *sp2=src_bufs[1];
                    kdu_int32 shift = this->downshift;
                    kdu_int32 offset = this->rounding_offset;
                    kdu_int32 i_lambda = this->icoeffs[0];
                    if (i_lambda == 1)
                      for (k=start_loc; k < width; k++)
                        dst_out[k]=dst_in[k] - ((offset+sp1[k]+sp2[k])>>shift);
                    else if (i_lambda == -1)
                      for (k=start_loc; k < width; k++)
                        dst_out[k]=dst_in[k] - ((offset-sp1[k]-sp2[k])>>shift);
                  else
                    for (k=start_loc; k < width; k++)
                      dst_out[k] = dst_in[k] -
                        ((offset + i_lambda*(sp1[k]+sp2[k]))>>shift);
                  }
              }
            else
              { // More general 32-bit processing
                int t, support=this->support_length;
                if (!this->reversible)
                  { 
                    float *dp_in=(float *)dst_in, *dp_out=(float *)dst_out;
                    for (t=0; t < support; t++, dp_in=dp_out)
                      { 
                        float *sp = (float *)src_bufs[t];
                        float lambda = this->coeffs[t];
                        for (k=start_loc; k < width; k++)
                          dp_out[k] = dp_in[k] - lambda*sp[k];
                      }
                  }
                else
                  { 
                    kdu_int32 shift = this->downshift;
                    kdu_int32 offset = this->rounding_offset;
                    int sum, *fpp, support=this->support_length;
                    for (k=start_loc; k < width; k++)
                      { 
                        for (fpp=icoeffs, sum=offset, t=0; t < support; t++)
                          sum += fpp[t] * (src_bufs[t])[k];
                        dst_out[k] = dst_in[k] - (sum >> shift);
                      }
                  }
              }
          }
        else
          { 
            if ((support_length==2) && (coeffs[0]==coeffs[1]))
              { // Special case of symmetric least-dissimilar filters
                if (!this->reversible)
                  { 
                    float lambda = coeffs[0];
                    float *sp1=(float *)(src_bufs[0]);
                    float *sp2=(float *)(src_bufs[1]);
                    float *dp_in=(float *)dst_in, *dp_out=(float *)dst_out;
                    for (k=start_loc; k < width; k++)
                      dp_out[k] = dp_in[k] + lambda*(sp1[k]+sp2[k]);
                  }
                else
                  { 
                    kdu_int32 *sp1=src_bufs[0], *sp2=src_bufs[1];
                    kdu_int32 shift = this->downshift;
                    kdu_int32 offset = this->rounding_offset;
                    kdu_int32 i_lambda = this->icoeffs[0];
                    if (i_lambda == 1)
                      for (k=start_loc; k < width; k++)
                        dst_out[k]=dst_in[k] + ((offset+sp1[k]+sp2[k])>>shift);
                    else if (i_lambda == -1)
                      for (k=start_loc; k < width; k++)
                        dst_out[k]=dst_in[k] + ((offset-sp1[k]-sp2[k])>>shift);
                    else
                      for (k=start_loc; k < width; k++)
                        dst_out[k] = dst_in[k] +
                          ((offset + i_lambda*(sp1[k]+sp2[k]))>>shift);
                  }
              }
            else
              { // More general 32-bit processing
                int t, support=this->support_length;
                if (!this->reversible)
                  { 
                    float *dp_in=(float *)dst_in, *dp_out=(float *)dst_out;
                    for (t=0; t < support; t++, dp_in=dp_out)
                      { 
                        float *sp = (float *)src_bufs[t];
                        float lambda = this->coeffs[t];
                        for (k=start_loc; k < width; k++)
                          dp_out[k] = dp_in[k] + lambda*sp[k];
                      }
                  }
                else
                  { 
                    kdu_int32 shift = this->downshift;
                    kdu_int32 offset = this->rounding_offset;
                    int sum, *fpp, support=this->support_length;
                    for (k=start_loc; k < width; k++)
                      { 
                        for (fpp=icoeffs, sum=offset, t=0; t < support; t++)
                          sum += fpp[t] * (src_bufs[t])[k];
                        dst_out[k] = dst_in[k] + (sum >> shift);
                      }
                  }
              }            
          }
      }
      /* The above functions implement a single synthesis lifting step,
         updating the samples in `dst_in' to generate new values that are
         written to `dst_out', based on the source samples found in the buffers
         referenced `src_bufs'[n], for 0 <= n < N, where N is the value of
         `support_length'.  Note that all buffers are considered to represent
         image lines for vertical DWT processing.  For multi-component DWT
         processing, each buffer represents a line of some image component
         (or plane).
            16-bit line buffers are guaranteed to be aligned at multiples
         of 2*`KDU_ALIGN_SAMPLES16' bytes, but the first valid sample is given
         by `start_loc' while the number of valid samples is given by `width'.
            Similarly, 32-bit line buffers are guaranteed to be aligned at
         multiples of 4*`KDU_ALIGN_SAMPLES32' bytes, but the first valid sample
         is given by `start_loc', etc.  If the processing is irreversible
         (`reversible'=false), all supplied buffers are internally cast to
         (float *) from (kdu_int32 *).
            The implementation may process additional samples to the left or
         to the right of this valid region, to the extent required for aligned
         vector processing instructions.
            All remaining information required to implement the lifting step is
         provided by the `step' object, including whether the sample values
         are represented reversibly or irreversibly.
            Note that the `dst_in' buffer may be identical to the `dst_out'
         buffer.  Note also that the contents of the `src_bufs' array may be
         overwritten by this function, depending on its implementation. */
  };
  /* Notes:
       If `step_idx' is even, this lifting step updates odd-indexed (high-pass)
     samples, based on even-indexed input samples.  If `step_idx' is odd, it
     updates even-indexed (low-pass) samples, based on odd-indexed input
     samples.
       The `support_min' and `support_length' members identify the values of
     Ns and Ls such that the lifting step implements
     X_s[2k+1-p]=TRUNC(sum_{Ns<=n<Ls+Ns} Cs[n]*X_{s-1}[2k+p+2n]).  If the
     step is to be implemented using floating point arithmetic, the `TRUNC'
     operator does nothing and Cs[Ns+n] = `coeffs'[n].  If the step is to be
     implemented using integer arithmetic, Cs[Ns+n] = `icoeffs'[n]/2^downshift
     and TRUNC(x) = floor(x + `rounding_offset' / 2^downshift).
       The `extend' member identifies the amount by which the source sequence
     must be extended to the left and to the right in order to avoid treating
     boundaries specially during horizontal lifting.  This depends on both
     the lifting step kernel's region of support (`support_min' and
     `support_length') and the parity of the first and last samples on each
     horizontal line.  Although the left and right extensions are often
     different, it is much more convenient to just keep the maximum of the
     two values.
       The `vert_add_shorts_first' and `hor_add_shorts_first' members are true
     if the corresponding (vertical or horizontal) lifting implementation for
     16-bit samples adds pairs of input samples together, creating a 16-bit
     quantity prior to multiplication.  In this case, the sample normalization
     processes must also ensure that the 16-bit sum will not overflow.  The
     value of this flag is set at the same time that any SIMD acceleration
     function pointers are written to the `vlift_16_func' and
     `hlift_16_func' members. */
  
} // namespace kd_core_local

#endif // TRANSFORM_BASE_H
