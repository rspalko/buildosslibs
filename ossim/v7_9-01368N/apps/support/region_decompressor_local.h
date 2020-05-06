/*****************************************************************************/
// File: region_decompressor_local.h [scope = APPS/SUPPORT]
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
   Provides local definitions for the implementation of the
`kdu_region_decompressor' object.
******************************************************************************/

#ifndef REGION_DECOMPRESSOR_LOCAL_H
#define REGION_DECOMPRESSOR_LOCAL_H

#include <string.h>
#include "kdu_region_decompressor.h"

// Declared here
namespace kd_supp_local {
  struct kdrd_interp_kernels;
  struct kdrd_tile_bank;
  struct kdrd_component;
  struct kdrd_channel;
  struct kdrd_channel_buf;
}

/*****************************************************************************/
/*                            Data Type Flags                                */
/*****************************************************************************/
  
#define KDRD_FIX16_TYPE 1 /* 16-bit fixed-point, KDU_FIX_POINT frac bits. */
#define KDRD_INT16_TYPE 2 /* 16-bit absolute integers. */
#define KDRD_FLOAT_TYPE 4 /* 32-bit floats, unit nominal range. */
#define KDRD_INT32_TYPE 8 /* 32-bit absolute integers. */

#define KDRD_ABSOLUTE_TYPE (KDRD_INT16_TYPE | KDRD_INT32_TYPE)
#define KDRD_SHORT_TYPE (KDRD_FIX16_TYPE | KDRD_INT16_TYPE)

/*****************************************************************************/
/*                       SIMD Accelerator Imports                            */
/*****************************************************************************/

#if defined KDU_X86_INTRINSICS
#  define KDU_SIMD_OPTIMIZATIONS
#  include "x86_region_decompressor_local.h"
#elif defined KDU_NEON_INTRINSICS
#  include "neon_region_decompressor_local.h"
#  define KDU_SIMD_OPTIMIZATIONS
#endif

namespace kd_supp_local {
  using namespace kdu_supp;
  
#ifdef KDU_SIMD_OPTIMIZATIONS
  using namespace kd_supp_simd;
#endif
  
/*****************************************************************************/
/*            Prototypes of Functions that could be accelerated              */
/*****************************************************************************/
  
typedef void (*kdrd_convert_and_copy_func)
  (const void *src_line_bufs[], const int src_line_widths[],
   const int src_line_types[], int num_src_lines, int src_precision,
   int missing_src_samples, void *dst, int dst_min, int dst_len,
   int dst_type, int float_exp_bits);
  /* Generic function that concatenates the samples from one or more
     source lines (corresponding lines from adjacent tiles), converting
     them from the relevant source type to the relevant target type and
     writing the results to locations within the `dst' buffer that range from
     `dst_min' to `dst_min'+`dst_len'-1.  All buffers are passed as void *,
     since they may have different types, depending on the actual function
     implementation.
        Some of the required input samples may be missing on the left (as
     identified by `missing_src_samples') and/or on the right.  Such
     samples are synthesized by boundary replication.  It can also happen
     that `missing_src_samples' is -ve, meaning that some initial samples
     from the input `lines' are to be skipped.
        You should note that some of the `src_line_bufs' entries may be NULL,
     but only where the corresponding `src_line_widths' entry is 0.
     Moreover, the last source line is guaranteed not to be empty and its
     width is guaranted to be non-zero, except in the event that `num_lines'
     itself is 0, in which case there is no data at all and the `dst' buffer
     is simply zeroed out.
        It would be possible to implement a single function that addresses all
     conversion+copy requirements, using the arguments to configure itself.
     In practice, though, it is helpful for a more specific function to be
     pre-configured based on the conditions that exist, since most of the
     arguments will hold the same values every time this function is called.
     In special cases, SIMD accelerated versions of the function may be used.
     This is most useful for cases where all source lines have a specific
     type, which happens almost certainly in practice.
        Note that the `dst' buffer and `src_line_bufs' buffers are guaranteed
     to be aligned, since they come directly from `kdu_line_buf' objects.
     The `dst_min' value will be either 0 or -2, in practice, so the manner
     in which it alters the alignment of the written samples is readily
     inferred by the implementation.
        The type codes used by this function (and others) are all 1-bit flags,
     as follows:
        KDRD_FIX16_TYPE=1 (16-bit fixed-point with KDU_FIX_POINT frac bits);
        KDRD_INT16_TYPE=2 (16-bit integers with nominal range `src_precision')
        KDRD_FLOAT_TYPE=4 (32-bit normalized floats -- unit nominal range)
        KDRD_INT32_TYPE=8 (32-bit integers with nominal range `src_precision')
     Importantly, the `dst_type' cannot equal `KDRD_INT16_TYPE', since we only
     use intermediate data types that are either 16-bit fixed-point, or else
     32-bit float/int.
        The `float_exp_bits' will normally be 0.  If non-zero, all input lines
     are expected to have `KDRD_INT32_TYPE' and `dst_type' should be
     `KDRD_FLOAT_TYPE', with conversion being performed by re-interpreting the
     `src_precision' length bit-patterns as floats.  One of two special
     functions is always installed for handling this case.  One handles
     source data that is supposed to have a signed representation, producing
     signed floats.  The ohter handles source data that is supporsed to have
     an unsigned representation (but has been level offset).  After appropriate
     conversion, these functions apply the final scaling or level adjustment
     steps described in the documentation of `kdu_channel_interp'.
   */
  
typedef void (*kdrd_convert_and_add_func)
  (const void *src_line_bufs[], const int src_line_widths[],
   const int src_line_types[], int num_src_lines, int src_precision,
   int missing_src_samples, void *dst, int dst_min, int dst_len, int dst_type,
   int cell_width, int acc_precision, int cell_lines_left, int cell_height,
   int float_exp_bits);
   /* Generic function for converting source samples and doing box-car
      integration.  This function is very similar to the above one, taking
      most of the same arguments.  Additionally, source samples are accumulated
      horizontally in cells of size `cell_width' and accumulated into the
      `dst' buffer, which is sized large enough to allow accumulation within
      32-bit values, even if `dst_type' is KDRD_FIX16_TYPE (16-bit words).
         If `cell_lines_left' is equal to `cell_height', the `dst' buffer
      is zeroed out prior to accumulation, while if `cell_lines_left' is
      equal to 1 on entry, the `dst' buffer is converted back to
      `dst_type' before returning.
         This function has an additional `acc_precision' argument which
      determines the amount by which samples are scaled prior to accumulation.
      We choose to do the scaling first, rather than at the end, so that
      integer accumulation can be efficient.  If `dst_type' is an integer type,
      the accumulation is done using 32-bit integers, and individual source
      samples are converted to an integer representation with `acc_precision'
      bits prior to accumulation.  If `dst_type' is a floating-point type,
      the accumulation is done in floating point after scaling each source
      sample to a nominal range of 2^{acc_precision}; in this case,
      `acc_precision' will always be negative in practice (actually, it is
      sure to be the -ve base-2 log of `cell_width'*`cell_height'.
         It is worth noting that the only `dst_type' values that may appear
      here are KDRD_FIX16_TYPE and KDRD_FLOAT_TYPE.  32-bit integers are
      always converted to a 16-bit fixed-point representation with
      KDU_FIX_POINT precision when `cell_lines_left'=1, taking
      `acc_precision'+log_2(cell_width*cell_height) as the source precision
      from which we are coming down.
         The `float_exp_bits' argument has the same meaning here as in
      `kdrd_convert_and_copy_func'.  Again, one of two special functions
      is expected to be installed for handling this case, where one handles
      signed original data and the other handles unsigned original data.
    */
  
typedef void (*kdrd_white_stretch_func)
  (const kdu_int16 *src, kdu_int16 *dst, int num_samples,
   int stretch_residual);
  /* Implements white stretching for integer sample values that originally had
     low precision, so that the nominal range was from 0 to (2^P)-1, where P
     is very small, or from -2^{P-1} to 2^{P-1}-1.  The objective of
     stretching is to map these values to a higher precision B, where a
     straight left shift by B-P would leave a maximum value smaller than
     it should be by 2^{B-P}-1.  The `stretch_residual' value is described
     in the comments explaining this member variable inside `kdrd_channel'.
        The `src' and `dst' buffers are both obtained from `kdu_line_buf'
     objects and so have all the alignment guarantees offered by the
     `kdu_line_buf' class. */
  
typedef void (*kdrd_transfer_func)
  (const void *src_buf, int src_prec, int src_type, int src_skip,
   int num_samples, void *dst, int dst_prec, int dst_gap, bool leave_signed,
   float src_scale, float src_offset, bool clip_outputs);
    /* Generic function to transfer a channel of source data to an output
       buffer.  The `src_buf' always comes from a `kdu_line_buf' object,
       having one of the `src_type' values:
          KDRD_FIX16_TYPE=1
          KDRD_FLOAT_TYPE=4 or
          KDRD_INT32_TYPE=8.
       Only the last of these types has any sensitivity to the value of
       `src_prec'.
          The `dst' buffer has one of three different representations that must
       be known to the function: bytes; 16-bit words; and 32-bit floats.
       A different instance of this function must be implemented for each of
       these three output formats.  Output samples are separated by `dst_gap'
       sample positions (bytes, words or floats, as appropriate) within the
       `dst' buffer.
          The `src_prec' argument is used only for the `KDRD_INT32_TYPE'
       data type -- i.e., absolute integers.
          The `src_scale' and `src_offset' arguments refer to adjustments that
       may need to be applied to the source samples as a first step.  In
       particular, the samples in `src_buf' should be multiplied by
       `src_scale', after which `src_offset' should be added, where scale
       factors and offsets are always expressed relative to the
       `KDRD_FLOAT_TYPE' representation.  For source type `KDRD_FIX16_TYPE',
       the offset should be multiplied by 2^{`KDU_FIX_POINT'} while for
       absolute integer outputs, the offset should be multiplied by
       2^{`src_prec'}.  These scale and offset values provide the adjustments
       required to realize the "true-zero" and/or "true-max" options
       describe in connection with `kdu_region_decompressor::set_true_scaling'.
       Notionally at least, the scaling and offset are applied first, after
       which the default conversion steps (not true-max and not true-zero)
       are applied.  If `set_true_scaling' has never been called, or if it has
       been used to turn off the "true-zero" and "true-max" options, the
       `src_scale' and `src_offset' arguments passed to functions that
       operate on integer inputs and integer outputs will be 1.0 and 0.0
       respectively.  Some accelerated integer processing functions might not
       handle other values for `src_scale' and `src_offset', in which case
       a non-accelerated version of the function may need to be adopted if
       one of the non-default true-scaling modes has been selected.  For
       floating point source or floating point output types, however, it is
       very cheap to incorporate true scaling; in these cases, the "true-max"
       policy is always used, as explained in the documentation of
       the `set_true_scaling' function.
          For floating point outputs, `dst_prec' = P identifies the target
       nominal range for the output samples.  Specifically, prior to any
       final level adjustment the target nominal range is
       -2^{P-1} to 2^{P-1}-1, unless P=0, in which case it is -0.5 to 0.5.
       However, for floating point outputs the `src_scale' argument already
       incorporates the scaling required to accommodate the difference
       between this range and -2^{P-1} to +2^{P-1}, so the scaled inputs
       only need to be multiplied by 2^P and then level adusted by
       2^{P-1} if `leave_signed' is false.
          The `clip_outputs' argument determines whether or not the converted
       output values should be clipped to their nominal range.  This is certain
       to be true for all functions that produce integer-valued outputs.  For
       floating point outputs, it is true also unless the source samples were
       float-formatted or fixpoint-formatted with a non-zero number of
       integer bits -- i.e., a non-trivial, non-default source format.  The
       reason for not clipping in this case is explained with the
       documentation of the `kdu_region_decompressor::process' functions that
       produce floating point outputs.
          If `dst_gap' > 1, this function is probably being used to write
       to an interleaved buffer.  Accelerated versions of this function may
       be provided for the special case of `dst_gap'=4, which is the most
       common.  For interleaved output buffers, however, it is usually
       possible to get away with using an `kdrd_interleaved_transfer_func'
       function, as described below. */
  
typedef void (*kdrd_interleaved_transfer_func)
  (const void *src0, const void *src1, const void *src2, const void *src3,
   int src_prec, int src_type, int src_skip, int num_pixels,
   kdu_byte *dst, int dst_prec, kdu_uint32 zero_mask, kdu_uint32 fill_mask);
  /* An interleaved transfer function may be used only when the output pixels
     are interleaved into 4-sample pixels, where `dst' points to the first
     sample in the first pixel.  All four source line buffers must have the
     same type and precision, and all must be accessible.  The source buffers
     come directly from `kdu_line_buf' objects, having the corresponding
     alignment guarantees.
        Currently, interleaved transfer functions are defined only for the
     case where the output samples are unsigned bytes.  Interleaved transfer
     functions do not offer the `src_scale', `src_offset' and `clip_outputs'
     arguments that appear in the more general `kdrd_transfer_func' functions,
     so interleaved transfer can be used only when these arguments would be
     1.0, 0.0 and true, respectively -- the default situation.
        The `zero_mask' and `fill_mask' arguments are each interpreted as four
     bytes, corresponding to the successive samples in each interleaved output
     pixel.  The bytes are arranged in native machine order, so that the
     least significant byte of a mask corresponds to the first sample of each
     pixel on a little-endian machine and the most significant byte on a
     big-endian machine.
        Each `zero_mask' byte holds 0 or 0xFF, depending on whether the
     transferred sample data at that location is to be zeroed out or
     preserved, respectively.  Each `fill_mask' byte holds a value that is to
     be OR'd into each sample value after application of the `zero_mask'. */
  
/*****************************************************************************/
/*                            kdrd_interp_kernels                            */
/*****************************************************************************/

#define KDRD_INTERP_KERNEL_STRIDE     14
#define KDRD_SIMD_KERNEL_NONE         0
#define KDRD_SIMD_KERNEL_VERT_FLOATS  1
#define KDRD_SIMD_KERNEL_VERT_FIX16   2
#define KDRD_SIMD_KERNEL_HORZ_FLOATS  3
#define KDRD_SIMD_KERNEL_HORZ_FIX16   4

typedef void (*kdrd_simd_horz_fix16_func)
              (int length, kdu_int16 *src, kdu_int16 *dst, kdu_uint32 phase,
               kdu_uint32 numerator, kdu_uint32 denominator, int pshift,
               void **kernels, int kernel_len, int leadin, int blend_vecs);
typedef void (*kdrd_simd_horz_float_func)
              (int length, float *src, float *dst, kdu_uint32 phase,
               kdu_uint32 numerator, kdu_uint32 denominator, int pshift,
               void **kernels, int kernel_len, int leadin, int blend_vecs);
  /* Prototypes for SIMD accelerated horizontal resampling functions that
     may be available to perform accelerated interpolation operations.  These
     prototypes covers at least two quite different implementation strategies
     for the horizontal interpolation process, as described below.  In each
     case, `kernels' points to an array of sets of interpolation kernels.
        The set used to generate a given vector of V output samples is found
     at `kernels'[p], where p is derived from (`phase'+`off')>>`pshift', where
     `off'=(1<<`pshift')/2.  After generating a group of V samples in `dst',
     the `phase' value is incremented by V*`num'.  If this leaves
     `phase'>=`den', we subtract `den' from `phase' and increment the `src'
     pointer -- we may need to do this many times -- after which we are
     ready to generate the next group of V samples for `dst'.
        The vector length V is not specified in the call to this function,
     since each specific implementation works with a fixed vector length
     that is known at the time when the function pointer is installed; this
     value V is also used to pre-configure the `kernels' data that is passed
     to the function.
        Note that `num' and `den' enter as unsigned 32-bit integers but are
     in fact guaranteed to be strictly less than 2^31 (i.e., positive as
     signed integers).  Also 0 <= `phase' < `den' on entry.  The function
     may exploit these bounds to keep all in-loop phase manipulation in
     32-bit arithmetic, which may help speed things up when running on a
     32-bit architecture.
     -------
     Case 1: `blend_vecs' = 0 and `leadin' = 0
        As explained with the `kdrd_interp_kernels::get_simd_kernel' function,
     this case corresponds to resolution expansion with underlying bilinear
     interpolation (2-tap filters) as the mechanism.  Each of the V outputs
     at dst[m] (m=0,...,V-1) is obtained by taking the inner product (over
     n=0,...,K-1) between the K-element vectors (kernels[p])[m+Vn] and the
     corresponding values of src[n].  A natural implementation is to broadcast
     each `src[n]' value to all V lanes of a vector and multiply by the
     n'th vector fom `kernels[p]', adding the results lane-wise.
     -------
     Case 2: `blend_vecs' = 0 and `leadin' > 0
        As explained with the `kdrd_interp_kernels::get_simd_kernel' function,
     this case corresponds to original scalar interpolation kernels that have
     length 6, where `leading' is 2 during reduction and larger during
     expansion.  Each of the V outputs at dst[m] (m=0,...,V-1) is obtained by
     taking the inner product (over n=0,...,K-1) between the K-element vectors
     (kernels[p])[m+Vn] and src[m-leadin+n], where K=`kernel_len'.  A natural
     implementation is to read K progressively shifted vectors from `src' (one
     for each shift n), multipying each such vector lane-wise, by the n'th
     vector vector from kernels[p], and adding the results lane-wise.
     -------
     Case 2: `blend_vecs' > 0
        In this case, the function is based on the use of shuffling
     instructions to shuffle individual samples (elements) within input
     vectors in order to align them with the samples of the output vector
     to which they contribute (through a resampling filter).  Each output
     vector Y within the `dst' array is formed from a linear combination of
     the elements of a set of input vectors X_0 through X_{B-1}, where B is
     the value of `blend_vecs'.
        The very first output vector spans samples `dst[0]' through `dst[V-1]',
     and the corresponding first input vector X_0 spans input samples from
     `src[-L]' to `src[N-1-L]', where L = (K-2)/2 and K is the value of
     `kernel_len'.  This K value is the length of the underlying resampling
     kernels (mirror image of the resampling filter).  The operation that is
     performed is always equivalent to forming each output sample from the
     inner product between an appropriate length K resampling kernel (whose
     support is from -L to L+1, because K is even) and input samples in the
     range k_n-L to k_n+L+1, where k_n is the location in `src' that lies
     immediately before (or at) the notional location of the output sample at
     `dst[n]'.
        One way to implement the resampling operation on vectors is to
     assign a collection of shuffle (or permutation) vectors S_{b,k} to each
     input vector X_b and each location k \in [0,K) in the resampling kernel,
     such that Y can be written as
              Y = sum_{k=0,...,K-1} M_k * sum_{b=0,...,B-1} S_{b,k}(X_b)
     Here, M_k is a vector that contains the k'th element of the resampling
     kernel associated with each output sample in the vector Y.  The
     shuffle vectors S_{b,k} have entries S_{b,k}[n], whose value identifies
     the specific sample within the vector X_b that is to be multiplied by
     coefficient k of the resampling kernel for output sample n -- since there
     can be only one such input sample for each k, all but one of the
     shuffle indices S_{0,k}[n] through S_{B-1,k}[n] hold special indices
     that map 0 to location n in the shuffle output S_{b,k}(X_b).
        The array at `kernels'[p] consists of a collection of kernel vectors,
     the first K of which hold the M_k multiplier vectors; the next B vectors
     correspond to S_{b,0}; these are followed by the S_{b,1} vectors; and so
     forth, finishing with the B vectors S_{b,K-1}.  These kernels are
     generated by calls to `kdrd_interp_kernels::get_simd_hshuf_kernel'.
        We now discuss variations on the way in which permutations are
     actually represented.  For implementations based on the SSSE3 PSHUFB
     instruction, the shuffle vectors consist of 16 bytes, each of which
     holds the index of the byte (in the range 0 to 15) of the corresponding
     input vector that is to be mapped to the location in question, or the
     value 128 if nothing is to be mapped to that entry.  This exact same
     shuffle vector representation can also be used for implementations based
     on the ARM-NEON VTBX instruction.  In both cases, V will be 8 for
     fixed-point processing and 4 for floating-point processing.
        For fixed-point implementations that use AVX2, there are no shuffle
     instructions capable of operating on a full vector with V=16 samples.  In
     this case, we use the VPSHUFB instruction, which separately permutes
     the bytes of each 128-bit lane, but we need twice as many shuffle vectors.
     To make this consistent with what is described above, the interpretation
     for AVX2 is that the input source vectors are only 128 bits wide,
     holding V/2 samples each, while the permuted outputs are 256 bits wide,
     with all V samples.  If a single 128-bit lane contains all the samples
     required to generate produce inputs for a given k value for all V
     output samples, then B can be as small as 1.  However, we will often find
     that B is twice as large for this case as it would be where both the
     source and output vectors have dimension V.  To reduce the memory
     demands associated with the shuffle vectors, AVX2 implementations
     should only expect to find one set of B shuffle vectors, corresponding
     to k=0.  Rather than provide additional shuffle vectors for each k,
     AVX2 implementations are expected to displace the input pointer by k,
     reading a new set of B 128-bit input vectors and applying the same
     shuffle vectors to these inputs.  This may reduce the value of B that
     is actually required, and reduces the already rather large
     memory footprint of the SIMD kernels required for AVX2 operation.  Note
     that the natural implementation strategy for AVX2 is to use the
     VBROADCASTI128 instruction to read a non-aligned 128-bit source vector and
     simultaneously broadcast it to both lanes of the 256-bit source vector,
     before applying the VPSHUFB instruction.
        For floating-point implementations that use AVX, the full vector
     permutation instruction VPERMPS is employed, working with vectors of
     dimension V=8.  This instruction requires a different encoding to the
     VPSHUFB instruction.  The shuffle vector is organized into 8 32-bit
     integers, which hold values in the range 0 to 7, or else 0x80808080;
     the latter is the signal to copy zero to the corresponding destination
     position.  The VPERMPS instruction itself does not recognize anything
     other than the 3 LSB's of each 32-bit word within the shuffle vector,
     but the most significant bits of each byte can be used with the
     VPBLENDVB instruction to selectively combine permuted source vectors
     when B > 1.  In the special case where B=1, the 0x80808080 code will
     never occur, so VPERMPS is all we need.  In fact, for 2-tap (bilinear)
     expansion, we never need more than the first shuffle vector in any given
     kernel and that may be the only case we bother implementing for
     floating-point AVX processing, since shuffles are more expensive than
     multiplies on common architectures.
     -------
        Regardless of the strategy employed, all kernel coefficients found
     within the initial elements of the `kernels' array have a representation
     in which the true coefficients have been pre-scaled by -(2^15), for
     fixed-point implementations; floating-point implementations involve no
     such scaling.
  */

typedef void (*kdrd_simd_vert_fix16_func)
              (int length, kdu_int16 *src[], kdu_int16 *dst, void *kernel,
               int kernel_len);
typedef void (*kdrd_simd_vert_float_func)
              (int length, float *src[], float *dst, void *kernel,
               int kernel_len);
  /* These function prototypes are used for SIMD-accelerated functions that
     implement vertical resampling. */

#define KDRD_MAX_SIMD_KERNEL_DWORDS (24*8)
  /* The above constant defines the space that we set aside for each SIMD
     kernel, measured in 32-bit dwords.  The following considerations go into
     this allocation:
     1. We allow for up to 8 dwords (256 bits) per vector.
     2. For regular convolution-based resampling, we allow for up to 6 taps in
        the original scalar kernel and scaling factors as small as 0.5 (for
        reduction).  With the maximum V of 16 (supported by 256 bit vectors
        with 16-bit samples), this reduction factor increases the kernel
        length by ceil(15 / 0.5) - 15 = 15, leaving us with a maximum SIMD
        kernel length of 21 vectors = 21*8 dwords.  Note that the region
        decompressor allows scaling factors as small as 1/3, but these can
        only be called for at the smallest of scales, when there are no
        more DWT levels to discard; in such circumstances, construction of
        the SIMD tables could be more costly than performing the resampling
        directly, since the resulting images are usually very small.
     3. For the special case of convolution-based expansion with 2-tap kernels,
        where the kernels are defined differently (see above), the maximum
        increase in kernel length is 15, so the maximum SIMD kernel length
        possible is 17 vectors = 17*8 dwords.
     4. For shuffle-based horizontal resampling, with full length vectors as
        shuffle inputs, we allow for up to B=3 blend vectors, so we require a
        total of K*(1+B) = 24 vectors = 24*8 dwords.
     5. For shuffle-based horizontal resmapling with half-vector permutation
        inputs, we allow for up to B=6, but note that we only need one set of
        blending vectors in each kernel, rather than one set for each kernel
        tap.  This leaves us with a maximum of K+B = 12 vectors, which is less
        than the number required for case 4 above. */
  
struct kdrd_interp_kernels {
   public: // Member functions
     kdrd_interp_kernels()
      { 
        target_expansion_factor = derived_max_overshoot = -1.0F;
        kernel_length = 6;
        simd_kernel_type = KDRD_SIMD_KERNEL_NONE;        
      }
     void init(float expansion_factor, float max_overshoot,
               float zero_overshoot_threshold);
       /* If the arguments supplied here agree with the object's internal
          state, the function does nothing.  Otherwise, the function
          initializes the interpolation kernels for use with the supplied
          `expansion_factor' -- this is the ratio of input sample spacing
          to interpolated sample spacing, where values > 1 correspond to
          expansion of the source data.  Specifically, the normalized bandwidth
          of the interpolation kernels is BW = min{1,`expansion_factor'} and
          the kernels are obtained by windowing and normalizing the function
          sinc((n-sigma)*BW), where sigma is the relevant kernel's centre of
          mass, which ranges from 0.0 to 1.0.  The window has region of support
          covering -2 <= n <= 3.
             The function additionally limits the BIBO gain of the generated
          interpolation kernels, in such a way as to limit overshoot/undershoot
          when interpolating step edges.  The limit is `max_overshoot'
          times the height of the step edge in question for expansion factors
          which are <= 1.  For larger expansion factors, the `max_overshoot'
          value is linearly decreased in such a way that it becomes 0 at
          the point where `expansion_factor' >= `zero_overshoot_threshold'.
          The way in which the maximum overshoot/undershoot is limited is
          by mixing the 6-tap windowed sinc interpolation kernel with a
          2-tap (bi-linear) kernel.  In the extreme case, where `max_overshoot'
          is 0, or `expansion_factor' >= `zero_overshoot_threshold', all
          generated kernels will actually have only two non-zero taps.  This
          case is represented in a special way to facilitate efficient
          implementation. */
     bool copy(kdrd_interp_kernels &src, float expansion_factor,
               float max_overshoot, float zero_overshoot_threshold);
       /* If the arguments agree with the object's internal state, the
          function returns true, doing nothing.  Otherwise, if the arguments
          agree sufficiently well with the `src' object's initialized state,
          that object is copied and the function returns true.  Otherwise,
          the function returns false. */
#ifdef KDU_SIMD_OPTIMIZATIONS
     kdrd_simd_horz_float_func
       get_simd_horz_float_func(int &klen, int &leadin, int &blend_vecs)
       { if (simd_horz_float_func == NULL) return NULL;
         get_simd_kernel(KDRD_SIMD_KERNEL_HORZ_FLOATS,0);
         klen = this->simd_kernel_length;
         leadin = this->simd_horz_leadin;
         blend_vecs = this->simd_horz_float_blend_vecs;
         return simd_horz_float_func; }
       /* Returns a SIMD-accelerated horizontal resampling function for
          floating point samples, if one exists, along with the parameters
          to be supplied as the last three arguments when calling the
          function.  In order to obtain valid quantities to return via the
          last three arguments, the function calls `get_simd_kernel'
          internally, which immediately invalidates any kernel information
          you may have previously obtained for a different type of
          resampling kernel. */
     kdrd_simd_horz_fix16_func
       get_simd_horz_fix16_func(int &klen, int &leadin, int &blend_vecs)
       { if (simd_horz_fix16_func == NULL) return NULL;
         get_simd_kernel(KDRD_SIMD_KERNEL_HORZ_FIX16,0);
         klen = this->simd_kernel_length;
         leadin = this->simd_horz_leadin;
         blend_vecs = this->simd_horz_fix16_blend_vecs;
         return simd_horz_fix16_func; }
       /* As above, but for processing 16-bit fixed-point samples. */
     kdrd_simd_vert_float_func get_simd_vert_float_func(int &klen)
       { klen=kernel_length; return simd_vert_float_func; }
       /* As above, but for vertical resampling of floating point samples;
          here there are no special arguments to the function. */
     kdrd_simd_vert_fix16_func get_simd_vert_fix16_func(int &klen)
       { klen=kernel_length; return simd_vert_fix16_func; }
       /* As above, but for 16-bit fixed-point samples. */
     void *get_simd_kernel(int type, int i);
       /* Call this function to prepare (if necessary) and return the
          internal `simd_kernels' array, such that each entry in the array
          points to an appropriate kernel.  Array indices `i' identify the
          centre of mass, in the range 0.0 to 1.0, in steps of 1/32.  The
          kernels are prepared to match the properties of the relevant SIMD
          functions.  For horizontal kernels, these are the
          `simd_horz_float_func' and `simd_horz_fix16_func' functions,
          while for vertical kernels, they are the `simd_vert_float_func'
          and `simd_vert_fix16_func' functions.  The kernels are for use in
          resampling, as determined by the `type' parameter, which takes one
          of the following values:
          [>>] `KDRD_SIMD_KERNEL_VERT_FLOATS' -- in this case the returned
               array holds `kernel_length' groups of V floats, where V is
               the `simd_vert_float_vector_length' value installed when the
               `simd_vert_float_func' function pointer was initialized.  All
               of the V floats in each vector are identical, being equal to
               the corresponding coefficient from the original kernel.  The
               `simd_kernel_length' value in this case is identical to
               `kernel_length' (see below), which takes one of the values 2
               or 6.
          [>>] `KDRD_SIMD_KERNEL_VERT_FIX16' -- in this case, the returned
               array holds `kernel_length' groups of V 16-bit words, where V is
               the `simd_vert_fix16_vector_length' value installed when the
               `simd_vert_fix16_func' function pointer was initialized.  All
               of the V words in each vector are identical, being equal to the
               corresponding fixed-point coefficient from the original kernel.
               Again, in this case `simd_kernel_length' and `kernel_length' are
               identical.
          [>>] `KDRD_SIMD_KERNEL_HORZ_FLOATS'
               -- If `simd_horz_float_blend_vecs' is 0, the returned array is
               configured for horizontal resampling functions conforming to the
               `kdrd_simd_horz_float_func' prototype that take 0 for their last
               argument.
                  In this case, the returned array contains
               `simd_kernel_length' groups of V floats, where V is the
               `simd_horz_float_vector_length' value installed when the
               `simd_horz_float_func' function pointer was initialized.
               Together, these form V kernels q[n,m] (m=0,...,V-1; n=0,1,...),
               which are used to form V floating point outputs y[m]. Let R
               denote the reciprocal of the `target_expansion_factor' member.
                  If `kernel_length'=6 and R >= 1, the output y[m] is formed
               from the inner product \sum_n q[n,m]x[n+m-2]; in this case,
               q[n,0] is the interpolation kernel for the relevant shift,
               padded with trailing zeros, while q[n,m] is obtained by adding
               (R-1)*m to the shift (sigma_i) associated with index i, and
               positioning the start of the 6-tap interpolation kernel,
               corresponding to sigma = FRAC(sigma_i+(R-1)*m), at location
               floor(sigma_i+(R-1)*m), padding the unfilled positions with
               zeros.  Since R is strictly less than 3, sigma_i+(R-1)*m
               is strictly less than 2V-1, so the start of the last kernel
               cannot exceed position 2V-2.  This ensures that the aligned
               kernels all fit within 2V+4 coefficients, so
               `simd_kernel_length' will be no larger than 2V+4.  The
               `simd_horz_leadin' member will be equal to 2 for this
               case, reflecting the fact that the first sample required to
               form y[m] is x[n+m-2].
                  If `kernel_length'=6 and R < 1, the output y[m] is
               formed from the inner product \sum_n q[n,m]x[n+m-L], where L
               is the value of `simd_horz_leadin'; in this case, q[n,0]
               is the interpolation kernel for the relevant shift, padded
               with L-2 leading zeros.  q[n,m] is obtained by subtracting
               (1-R)*m from the shift sigma_i and positioning the interpolation
               kernel which has centre of mass sigma = FRAC(sigma_i-(1-R)*m)
               at location L - 2 + floor(sigma_i-(1-R)*m), padding the
               unfilled positions with zeros.  Since R > 0, sigma_i-(1-R)*m is
               strictly greater than 1-V, and the start of the last kernel
               commences at position L-1-V or greater.  This means that L need
               be no larger than V+1.  Note, however, that if R is close to 1,
               the value of L could be closer to 2, allowing smaller overall
               SIMD kernel lengths.  With the maximum value of V+1 for
               L=`simd_horz_leadin', `simd_kernel_length' takes its maximum
               possible value of V+7.
                  Otherwise we must have `kernel_length'=2 and R<1 (expansion).
               In this case, the output y[m] is formed from the inner product
               \sum_n q[n,m]x[n], where q[n,0] is the interpolation kernel
               for the relevant shift, padded with `simd_kernel_length'-2
               trailing zeros.  q[n,m] is obtained by adding R*m to the shift
               sigma_i and positioning the interpolation kernel which has
               centre of mass sigma = FRAC(sigma_i+R*m) at the location
               floor(sigma_i+R*m), padding unfilled positions with zeros.
               Since R<1 and m < V, the `simd_kernel_length' value will
               never exceed 1+V.  However, for small R (large expansion
               factors), `simd_kernel_length' may be as small as 3.  Note that
               `simd_horz_leadin' is always 0 in this case.
               -- If `float_horz_blend_vecs' > 0, the returned array is
               configured for horizontal resampling functions that use the
               horizontal shuffling approach documented with the
               `kdrd_simd_horz_float_func' prototype.
                  In this case, the function returns a pointer to an aligned
               array of 1 + `kernel_length' vectors; moreover, `kernel_length'
               will equal `simd_kernel_length' in this case.  The first vector
               in the array is a shuffle mask; the second vector holds the
               first coefficient for the mirror imaged resampling filters
               (inner product kernels) corresponding to each output sample
               in the vector; the third vector holds the second coefficient
               for the mirror imaged resampling filters (inner product
               kernels) corresponding to each output sample in the vector;
               and so forth.
          [>>] `KDRD_SIMD_KERNEL_HORZ_FIX16' -- same as above buf for
               processing vectors of V x 16-bit fixed-point sample
               values rather than V floating point samples, where V is
               the value of `simd_horz_fix16_vector_length' that was installed
               when the `simd_horz_fix16_func' function pointer was
               initialized.
        */
#endif // KDU_SIMD_OPTIMIZATIONS
   public: // Data
     float target_expansion_factor; // As supplied to `init'
     float derived_max_overshoot; // Maximum overshoot for this set of kernels
     float float_kernels[33*KDRD_INTERP_KERNEL_STRIDE]; // See below
     kdu_int32 fix16_kernels[33*KDRD_INTERP_KERNEL_STRIDE]; // Same but * -2^15
     int kernel_length; // 6 or 2 -- see below
     int kernel_coeffs; // 6 or 14 -- see below
   private: // Values initialized when SIMD kernels are installed
     int simd_kernel_type; // Type of simd_kernels created, if any (so far)
#ifdef KDU_SIMD_OPTIMIZATIONS
     kdu_int64 simd_kernels_initialized; // 1 flag bit for each kernel
     int simd_horz_leadin;   // See `get_simd_kernel' for an explanation of
     int simd_kernel_length; // these parameters.
     void *simd_kernels[33]; // 32-byte aligned pointers into `simd_block'
   private: // Values initialized once only, by `init'
     int simd_horz_float_blend_vecs; // These values are initialized when the
     int simd_horz_fix16_blend_vecs; // function pointers below are installed.
     int simd_horz_float_vector_length; // Value of V (see above) for the SIMD
     int simd_horz_fix16_vector_length; // horizontal resampling funcs below
     int simd_horz_float_blend_elt_size; // Num bytes in each blend elt: 1 or 4
     int simd_horz_fix16_blend_halves; // Non-zero if blend vecs permute half
                             // length source vectors and exist only for k=0.
     int simd_horz_float_kernel_leadin; // Value of `simd_horz_leadin' to use
     int simd_horz_fix16_kernel_leadin; // depending on the data type.
     int simd_horz_float_kernel_length; // Value of `simd_kernel_length' to use
     int simd_horz_fix16_kernel_length; // depending on the data type.
     int simd_horz_float_kernel_stride32; // Num 32-bit words required to hold
     int simd_horz_fix16_kernel_stride32; // a single 32-byte aligned kernel.
     kdrd_simd_horz_float_func simd_horz_float_func;
     kdrd_simd_horz_fix16_func simd_horz_fix16_func;
     int simd_vert_float_vector_length; // Value of V for the SIMD vertical
     int simd_vert_fix16_vector_length; // resampling functions below
     kdrd_simd_vert_float_func simd_vert_float_func;
     kdrd_simd_vert_fix16_func simd_vert_fix16_func;
   private: // Storage for SIMD kernels
     kdu_int32 simd_block[33*KDRD_MAX_SIMD_KERNEL_DWORDS+7];
#endif // KDU_SIMD_OPTIMIZATIONS
 };
 /* Notes:
       The `target_expansion_factor' keeps track of the expansion factor for
    which this object was initialized.  The expansion factor may be less than
    or greater than 1; it affects both the bandwidths of the designed kernels
    and also the structure of horizontally extended kernels -- see below.
       The `derived_max_overshoot' value represents the upper bound on the
    relative overshoot/undershoot associated with interpolation of step edges.
    This is the value that was used to design the interpolation kernels found
    in this object.
       The `float_kernels' array holds 33 interpolation kernels, corresponding
    to kernels whose centre of mass, sigma, is uniformly distributed over the
    interval from 0.0 to 1.0, relative to the first of the two central
    coefficients; there are (`filter_length'-2)/2 coefficients before this one.
    The kernel coefficients are separated by `KDRD_INTERP_KERNEL_STRIDE' which
    must, of course, be large enough to accommodate `kernel_length'.  In the
    case where `kernel_length'=6, there are only 6 coefficients in this array
    for each kernel and so `kernel_coeffs'=6 and the last
    `KDRD_INTERP_KERNEL_STRIDE'-`kernel_length' entries in each block of
    `KDRD_INTERP_KERNEL_STRIDE' are left uninitialized.  In the case where
    `kernel_length'=2, it is guaranteed that `target_expansion_factor' > 1
    (the `init' and `copy' functions ensure that this is always the case) and
    the first 2 coefficients of the i'th `KDRD_INTERP_KERNEL_STRIDE'-length
    block hold the values 1-sigma_i and sigma_i, where sigma_i=i/32.0.  In
    this case, however, `kernel_coeffs'=14 and the remaining 12 coefficients
    of the i'th kernel block are initialized to hold kernels q[n,m] of
    length 3 (m=1), 4 (m=2) and 5 (m=3), such that the m'th successive
    output sample can be formed from y[m] = sum_{0 <= n < 2+m} x[n]q[n,m].
    These extra kernels correspond to shifts sigma_i+R*m.  This allows
    a direct implementation of the horizontal interpolation process to
    rapidly compute up to 4 outputs together before determining a new kernel.
  */

/*****************************************************************************/
/*                               kdrd_tile_bank                              */
/*****************************************************************************/

struct kdrd_tile_bank {
  public: // Construction/destruction
    kdrd_tile_bank()
      {
        max_tiles=num_tiles=0; tiles=NULL; engines=NULL;
        queue_bank_idx=0; freshly_created=false;
      }
    ~kdrd_tile_bank()
      {
        if (tiles != NULL) delete[] tiles;
        if (engines != NULL) delete[] engines;
      }
  public: // Data
    int max_tiles; // So that `tiles' and `engines' arrays can be reallocated
    int num_tiles; // 0 if the bank is not currently in use
    kdu_coords first_tile_idx; // Absolute index of first tile in bank
    kdu_dims dims; // Region occupied on ref component's coordinate system
    kdu_tile *tiles; // Array of `max_tiles' tile interfaces
    kdu_multi_synthesis *engines; // Array of `max_tiles' synthesis engines
    kdu_thread_queue env_queue; // Queue for these tiles, if multi-threading
    kdu_long queue_bank_idx; // Index passed to `kdu_thread_env::attach_queue'
    bool freshly_created; // True only when the bank has just been created by
      // `kdu_region_decompressor::start_tile_bank' and has not yet been used
      // to decompress or render any data.
  };

/*****************************************************************************/
/*                               kdrd_component                              */
/*****************************************************************************/

struct kdrd_component {
  public: // Member functions
    kdrd_component()
      { num_tile_lines=0;  max_tiles=1;  initial_empty_tile_lines=0;
        tile_lines=tile_lines_scratch;  tile_bufs=tile_bufs_scratch;
        tile_widths=tile_widths_scratch;  tile_types=tile_types_scratch; }
    ~kdrd_component()
      { 
        if (tile_lines != tile_lines_scratch) delete[] tile_lines;
        if (tile_bufs != tile_bufs_scratch) delete[] tile_bufs;
        if (tile_widths != tile_widths_scratch) delete[] tile_widths;
        if (tile_types != tile_types_scratch) delete[] tile_types;
      }
    void init(int relative_component_index)
      {
        this->rel_comp_idx = relative_component_index;
        bit_depth = 0; is_signed=false; palette_bits = 0;
        num_line_users = needed_line_samples = new_line_samples = 0;
        dims = kdu_dims(); indices.destroy(); num_tile_lines=0;
        for (int t=0; t < max_tiles; t++)
          { tile_lines[t] = NULL; tile_bufs[t]=NULL;
            tile_widths[t]=0; tile_types[t]=0; }
        have_compatible16 = false;  src_types = 0;
      }
    void copy(const kdrd_component &src)
      { // Copies all members across except for the arrays and information that
        // is configured in `kdu_region_decompressor::make_tile_band_current'.
        rel_comp_idx = src.rel_comp_idx;
        bit_depth = src.bit_depth;
        is_signed = src.is_signed;
        palette_bits = src.palette_bits;
        num_line_users = src.num_line_users;
      }
  public: // Data
    int rel_comp_idx; // Index to be used after `apply_input_restrictions'
    int bit_depth;
    bool is_signed;
    int palette_bits; // See below
    int num_line_users; // Num channels using the `tile_line' entries
    int needed_line_samples; // Used for state information in `process_generic'
    int new_line_samples; // Number of newly decompressed samples in `line'
    kdu_dims dims; // Remainder of current tile-bank region; see notes below.
    kdu_line_buf indices; // See notes below
    int src_types; // Union of all type flags found in the `tile_types' array
    bool have_compatible16; // If tile-line can be converted to fix16 w/o loss
  public: // Tile-bank dependent arrays
    int max_tiles; // The arrays below all have `max_tiles' entries
    int num_tile_lines; // Number of tiles, exlcuding trailing 0 width tiles
    int initial_empty_tile_lines; // Add to arrays below to get 1st with W!=0.
    kdu_line_buf **tile_lines;
    const void **tile_bufs; // Tile-line buffer pointers (from `tile_lines')
    int *tile_widths; // Tile-line widths
    int *tile_types; // KDRD_xxxxx_TYPE, as declared and explained earlier
    kdu_line_buf *tile_lines_scratch[8]; // If `max_tiles' < 8, the above
    const void *tile_bufs_scratch[8];    // arrays point to these scratch
    int tile_widths_scratch[8];          // buffers, rather than heap arrays
    int tile_types_scratch[8];
  };
  /* Notes:
        Most members of this structure are filled out by the call to
     `kdu_region_decompressor::start', after which they remain unaffected
     by calls to the `kdu_region_decompressor::process' function.  The dynamic
     members are as follows:
            `new_line_samples', `dims', `tile_lines', `num_tiles', `max_tiles',
            `indices', `have_shorts' and `have_floats'.
        If `palette_bits' is non-zero, the `indices' buffer will be non-empty
     (its `exists' member will return true) and the code-stream sample values
     will be converted to palette indices immediately after (or during)
     decompression.
        The `tile_lines' array is used to keep track of the decompressed
     lines from each of the horizontally adjacent tiles in the current
     tile-bank.  Some of these tile lines may have zero width, but the final
     one may not.  This means that `num_tile_lines' may actually be smaller
     than the number of tiles in the current tile bank.  For each current
     tile line, we also keep track of the width, sample type and actual line
     buffer, within the `tile_widths', `tile_types' and `tile_bufs' arrays.
     All of these arrays have `max_tiles' available entries and may need to
     be resized if this more tiles are involved in a new tile bank.  The
     `num_tile_lines' value is allowed to be 0, but only if the `indices'
     buffer is being used.
 */

/*****************************************************************************/
/*                                kdrd_channel                               */
/*****************************************************************************/

#define KDRD_CHANNEL_VLINES 6    // Size of the `vlines' member array
#define KDRD_CHANNEL_LINE_BUFS 7 // Size of the `line_bufs' member array

struct kdrd_channel {
  public: // Member functions
    void init()
      { 
        source=NULL; lut_fix16=NULL; lut_float=NULL;
        in_line = horz_line = out_line=NULL;
        for (int k=0; k < KDRD_CHANNEL_LINE_BUFS; k++) line_bufs[k].destroy();
        line_bufs_used = 0;
        reset_vlines(); // Also resets `vline_bufs' & sets `num_valid_vlines'=0
        native_precision=0; native_signed=false;
        interp_orig_prec=0; interp_orig_signed=false;
        interp_float_exp_bits = 0; interp_fixpoint_int_bits = 0;
        interp_zeta = 0.0f; interp_normalized_max = 1.0f;
        interp_normalized_natural_zero = 0.0f;
        log2_source_headroom = 0;
        
        line_type = 0; stretch_residual = 0;
        
        subs_product = 1.0f;
        sampling_numerator = sampling_denominator = kdu_coords(1,1);
        sampling_phase = sampling_phase_shift = kdu_coords(0,0);
        boxcar_size = kdu_coords(1,1); missing = kdu_coords(0,0);
        boxcar_log_size = 0;  boxcar_lines_left = 0;

        convert_and_copy_func = NULL;  convert_and_add_func = NULL;
        in_precision = 0; in_line_start = in_line_length = out_line_length = 0;
        can_use_component_samples_directly=false;
        white_stretch_func = NULL;
        
        memset(horz_interp_kernels,0,sizeof(void *)*65);
        memset(vert_interp_kernels,0,sizeof(void *)*65);
#ifdef KDU_SIMD_OPTIMIZATIONS
        memset(simd_horz_interp_kernels,0,sizeof(void *)*65);
        memset(simd_vert_interp_kernels,0,sizeof(void *)*65);
        simd_horz_float_func = NULL;  simd_horz_fix16_func = NULL;
        simd_horz_kernel_len = simd_horz_leadin = simd_horz_blend_vecs = 0;
        simd_vert_float_func = NULL;  simd_vert_fix16_func = NULL;
        simd_vert_kernel_len = 0;
#endif
      }
    kdu_line_buf *get_free_line()
      {
        int idx=0, mask=~line_bufs_used; // Get availability bit mask
        assert((mask & 0x07F) != 0); // Otherwise all buffers used!
        if ((mask & 15) == 0) { idx+=4; mask>>=4; }
        if ((mask & 3) == 0) { idx += 2; mask>>=2; }
        if ((mask & 1) == 0) idx++;
        assert(idx < KDRD_CHANNEL_LINE_BUFS);
        line_bufs_used |= (1<<idx);
        return line_bufs+idx;
      }
    void recycle_line(kdu_line_buf *line)
      {
        int idx=(int)(line-line_bufs);
        if ((idx >= 0) && (idx < KDRD_CHANNEL_LINE_BUFS))
          line_bufs_used &= ~(1<<idx);
      }
    void reset_vlines()
      { 
        num_valid_vlines = 0;
        for (int w=0; w < KDRD_CHANNEL_VLINES; w++)
          { 
            vlines[w] = NULL;
#ifdef KDU_SIMD_OPTIMIZATIONS
            vline_bufs[w] = NULL;
#endif
          }
      }
    bool append_vline(kdu_line_buf *buf)
      { // Put `buf' into first available slot in `vlines' 
        if (num_valid_vlines >= KDRD_CHANNEL_VLINES)
          return false; // Already full
        vlines[num_valid_vlines] = buf;
#ifdef KDU_SIMD_OPTIMIZATIONS
        vline_bufs[num_valid_vlines] = buf->get_buf();
#endif
        num_valid_vlines++;
        return true;
      }
    void roll_vlines()
      { // Shift `vlines' buffers up: releases 1'st buffer & vacates last slot
        recycle_line(vlines[0]);
        num_valid_vlines--;
        for (int w=0; w < num_valid_vlines; w++)
          { 
            vlines[w] = vlines[w+1];
#ifdef KDU_SIMD_OPTIMIZATIONS
            vline_bufs[w] = vline_bufs[w+1];
#endif
          }
        vlines[num_valid_vlines] = NULL;
#ifdef KDU_SIMD_OPTIMIZATIONS
        vline_bufs[num_valid_vlines] = NULL;
#endif
      }
  public: // Resources, transformations and representation info
    kdrd_component *source; // Source component for this channel.
    kdu_sample16 *lut_fix16; // Palette mapping LUT.  NULL if no palette.
    float *lut_float; // Float precision LUT; might not be available.
    kdu_line_buf *in_line; // For boxcar integration/conversion/realigment
    kdu_line_buf *horz_line; // Set to NULL only when we need a new one
    kdu_line_buf *vlines[KDRD_CHANNEL_VLINES];
    kdu_line_buf *out_line; // NULL until we have a valid unconsumed output
    kdu_line_buf line_bufs[KDRD_CHANNEL_LINE_BUFS];
#ifdef KDU_SIMD_OPTIMIZATIONS
    void *vline_bufs[KDRD_CHANNEL_VLINES];
#endif
    int line_bufs_used; // One flag bit for each entry in `line_bufs'
    int native_precision; // Used if `kdu_region_decompressor::convert'
    bool native_signed;   // supplies a `precision_bits' argument of 0.
  
    int interp_orig_prec;          // These interp_... values are all copied
    bool interp_orig_signed;       // from their namesakes in the
    int interp_float_exp_bits;     // `kdu_channel_interp' record that
    int interp_fixpoint_int_bits;  // describes this channel.
    float interp_zeta;
    float interp_normalized_max;
    float interp_normalized_natural_zero;
    int log2_source_headroom; // >0 only for non-default pix formats; see below
  
    int line_type; // For `in_line' and `out_line': FIX16, FLOAT or INT32 only
    kdu_uint16 stretch_residual; // See below
  public: // Coordinates and state variables
    kdu_coords source_alignment;
    int num_valid_vlines;
    float subs_product; // product of two component sub-sampling factors
    kdu_coords sampling_numerator;
    kdu_coords sampling_denominator;
    kdu_coords sampling_phase;
    kdu_coords sampling_phase_shift;
    kdu_coords boxcar_size; // Guaranteed to be powers of 2
    kdu_coords missing;
    int boxcar_log_size; // Log_2(boxcar_size.x*boxcar_size.y)
    int boxcar_lines_left;
  public: // Data transfer and conversion functions and their parameters
    kdrd_convert_and_copy_func convert_and_copy_func; // Configured when making
    kdrd_convert_and_add_func convert_and_add_func;   // a tile-bank current.
    int in_precision; // Precision prior to boxcar renormalization
    int in_line_start, in_line_length, out_line_length;
    bool can_use_component_samples_directly; // See below
    kdrd_white_stretch_func white_stretch_func; // Configured by `start'
  public: // Lookup tables used to implement efficient kernel selection; these
          // are all indexed by `sampling_phase' >> `sampling_phase_shift'.
    void *horz_interp_kernels[65];
    void *vert_interp_kernels[65];
#ifdef KDU_SIMD_OPTIMIZATIONS
    void *simd_horz_interp_kernels[65];
    void *simd_vert_interp_kernels[65];
    kdrd_simd_horz_float_func simd_horz_float_func;
    kdrd_simd_horz_fix16_func simd_horz_fix16_func;
    int simd_horz_kernel_len; // These are the three last arguments that
    int simd_horz_leadin;     // need to be passed to whichever horizontal
    int simd_horz_blend_vecs; // SIMD resampling function is configured, if any
    kdrd_simd_vert_float_func simd_vert_float_func;
    kdrd_simd_vert_fix16_func simd_vert_fix16_func;
    int simd_vert_kernel_len; // Last argument in vertical SIMD resampling call
#endif
    kdu_uint16 horz_phase_table[65]; // Output sample location w.r.t. nearest
    kdu_uint16 vert_phase_table[65]; // src sample x (src sample spacing)/32
    kdrd_interp_kernels v_kernels;
    kdrd_interp_kernels h_kernels;
  };
  /* Notes:
        Except in the case where no processing is performed and no conversions
     are required for any reason, the channel buffers maintained by this
     structure have one of three possible representations:
     [S] 16-bit fixed-point, with KDU_FIX_POINT fraction bits, is used as
         much as possible.  This representation is always used if there is
         a palette `lut' or colour conversion is required.  The `using_shorts'
         flag is set if this representation is employed.
     [F] 32-bit floating-point, with a nominal range of -0.5 to +0.5.  The
         `using_floats' flag is set if this representation is employed.
     [I] 32-bit integers, with the original image component bit-depth, as
         given by `source->bit_depth'.  This is the least used mode; it may
         not be used if there is any resampling (including boxcar
         integration).
     The value of `in_precision' is used to record the precision associated
     with `in_line' before any boxcar renormalization.  For the [F]
     representation, `in_precision' always holds 0.  If there is no
     boxcar integration, `in_precision' holds KDU_FIX_POINT [S] or
     `source->bit_depth' [I].  If there is boxcar integration, only the
     [S] or [F] representations are valid; in the latter case, `in_precision'
     is 0, as mentioned; for [S], `in_precision' is increased beyond
     KDU_FIX_POINT to accommodate accumulation with as little pre-shifting
     as possible, and the buffers are allocated with double width so that
     they can be temporarily type-cast to 32-bit integers for the purpose
     of accumulating boxcar samples without overflow, prior to normalization.
        The conversion from decoded image components to an output channel
     buffer (referenced by `out_buf') involves some or all of the following
     steps.  As these steps are being performed, each of `in_line', `horz_line'
     and `out_line' may transition from NULL to non-NULL and back again to
     keep track of the processing state.
       a) Component values are subjected to any palette `lut' first, if
          required -- the output of this stage is always written to an
          `in_line' buffer and `using_shorts' must be true.
       b) Component values or `lut' outputs may be subjected to a coarse
          "boxcar" sub-sampling process, in which horizontally and/or
          vertically adjacent samples are accumulated in an `in_line'
          buffer.  This is done to implement large sub-sampling factors only,
          and is always followed by a more rigorous subsampling process in
          which the resolution will be reduced by at most a factor of 4,
          using appropriate anti-aliasing interpolation kernels.  Note that
          boxcar integration cells are always aligned at multiples of the
          boxcar cell size, on the canvas coordinate system associated with
          the `source' component.
       c) If neither of the above steps were performed, but raw component
          samples do not have the same representation as the channel line
          buffers, or there are multiple tiles in the tile-bank, or horizontal
          or vertical resampling is required, the source samples are
          transferred to an `in_line' buffer.
       d) In this step, horizontal resolution expansion/reduction processing
          is applied to the samples in `in_line' and the result written to
          the samples in `horz_line'.  If no horizontal processing is required,
          `horz_line' might be identical to `in_line' or even the original line
          of source component samples.
       e) If vertical resolution expansion/reduction is required, the
          vertical filter buffer implemented by `vlines' is rotated by one
          line and `horz_line' becomes the most recent line in this vertical
          buffer; `out_line' is then set to a separate free buffer line and
          vertical processing is performed to generate its samples.  If no
          vertical processing is required, `out_line' is the same as
          `horz_line'.
       f) If `stretch_residual' > 0, the white stretching policy described
          in connection with `kdu_region_decompressor::set_white_stretch' is
          applied to the data in `horz_line'.  If the source
          `bit_depth', P, is greater than or equal to the value of
          `kdu_region_decompressor::white_stretch_precision', B, the value of
          `stretch_residual' will be 0.  Otherwise, `stretch_residual' is set
          to floor(2^{16} * ((1-2^{-B})/(1-2^{-P}) - 1)), which
          necessarily lies in the range 0 to 0xFFFF.  The white stretching
          policy may then be implemented by adding (x*`stretch_residual')/2^16
          to each sample, x, after converting to an unsigned representation.
          In practice, we perform the conversions on signed quantities by
          introducing appropriate offsets.  If white stretching is required,
          the [S] representation must be used.  Note that white stretching
          is never applied to float-formatted or fixpoint-formatted data.
          Float-formatted data has `interp_float_exp_bits' > 0, while
          fixpoint-formatted data may have `interp_fixpoint_int_bits' > 0,
          but might not.  To reliably exclude these source types, we rely
          upon `interp_normalized_max', which is less than 0.5 if and only if
          the original sample values have a regular integer interpretation.
          In fact, the denominator of the `stretch_residual' expression
          given above, 1-2^{-P}, is identical to 0.5+`interp_normalized_max',
          which will be exactly 1.0 in the case of float-formatted or
          `fixpoint_formatted' samples.
       g) Once completed `out_line' buffers are available for all channels,
          any required colour transformation is performed in-place on the
          channel `out_line' buffers.  If colour transformation is required,
          the [S] representation must be used.
     [//]
     We turn our attention now to dimensions and coordinates.  The following
     descripion is written from the perspective that horizontal and vertical
     resampling will be required.  Variations are fairly obvious for cases
     in which either or both operation are not required.
     [>>] The `source_alignment' member records the effect of any image
          component registration offset on the shifts which must be
          implemented during interpolation.  These shifts are expressed
          in multiples of boxcar cells, relative to the `sampling_denominator'.
     [>>] `num_valid_vlines' identifies the number of initial entries in the
          `vlines' buffer which hold valid data.  During vertical resampling,
          this value needs to reach 6 before a new output line can be
          generated.
     [>>] The `sampling_numerator' and `sampling_denominator' members
          dictate the expansion/reduction factors to be applied in each
          direction after any boxcar accumulation, while `sampling_phase'
          identifies the amount of horizonal shift associated with the
          first column of `out_line' and the amount of vertical shift
          associated with the current `out_line' being generated.  More
          specifically, if the spacing between `in_line' samples is
          taken to be 1, the spacing between interpolated output samples
          is equal to `sampling_numerator'/`sampling_denominator'.  The
          phase values are set up so as to always hold non-negative quantities
          in the range 0 to `sampling_denominator'-1, but the notional
          displacement of a sample with phase P and denominator D, relative
          to the "nearest" `in_line' sample is given by
                  sigma = P / D
          The horizontal phase parameter is set so that the first sample in
          the `in_line' is the one which is nearest to (but not past) the
          first sample in `horz_line', while the vertical phase parameter is
          set up so that the third line in the `vlines' buffer is the one which
          is "nearest" to (but not pat) the `out_line' being generated.  Each
          time a new line is generated the `sampling_phase.y' value is
          incremented by `sampling_numerator.y', after which it is brought
          back into the range 0 to `sampling_denominator.y'-1 by shuffling
          lines in the `vlines' buffer and decrementing `num_valid_vlines',
          as required, subtracting `sampling_denominator.y' each time.
     [>>] In practice, we need to reduce the phase index P to an interpolation
          kernel, and we don't want to use explicit division to do this.
          Instead, we use (P + 2^{S-1}) >> S to index one of the lookup tables
          `horz_interp_kernels' or `vert_interp_kernels', as appropriate,
          where S, the value of `sampling_phase_shift', is chosen as small as
          possible such that 2^S > D/64.  The `sampling_numerator' and
          `sampling_denominator' values are scaled, if required, to ensure
          that the denominator is always greater than or equal to 32, unless
          this cannot be done without risking overflow, so as to minimize
          any loss of accuracy which may be incurred by the shift+lookup
          strategy for interpolation kernel selection.  After the quantization
          associated with this indexing strategy, some phases P which are close
          to D may be better represented with sigma=1.0 than the next available
          smaller value.  Thus, even though P is guaranteed to lie in the range
          0 to D-1, we maintain interpolation kernels with centres of mass
          which are distributed over the full range from 0.0 to 1.0.
     [>>] Each boxcar sample in `in_line' has cell size `boxcar_size.x' by
          `boxcar_size.y'.  In practice, some initial source rows might not
          be available for accumulation; these are indicated by
          `missing.y'.  Similarly, some initial source columns might
          not be available and these are indicated by `missing.x'.
          When a new line of component samples becomes available, the
          `missing.y' parameter is examined to determine whether this
          row should be counted multiple times, effectively implementing
          boundary extrapolation -- the value of `missing.y' is
          decremented to reflect any additional contributions, but we note
          that the value can be as large or even larger than `boxcar_size.y',
          in which case the boundary extrapolation extends across multiple
          lines of boxcar accumulation.  Similar considerations apply to the
          re-use of a first sample in each source line in accordance with the
          value of `missing.x'.  It is also worth noting that the `missing.x'
          and `missing.y' values may be negative if a channel does not actually
          need some of the available source component samples/lines.
     [>>] The `boxcar_lines_left' member keeps track of the number of source
          lines which have yet to be accumulated to form a complete
          `in_line'.  This value is always initialized to `boxcar_size.y',
          regardless of the value of `missing', which means that when
          initial source rows are replicated to accommodate `missing.y',
          the replication count must be subtracted from `boxcar_lines_left'.
     [>>] The `in_line_start' and `in_line_length' members identify
          the range of sample indices which must be filled out for
          `in_line'.  `in_line_start' will be equal to -2 if horizontal
          resampling is required (otherwise it is 0), since horizontal
          interpolation kernels extend 2 samples to the left and 3 samples
          to the right (from an inner product perspective), for a total
          length of 6 taps.  The `in_line_length' member holds the total
          number of samples which must be filled out for the `in_line',
          starting from the one identified by `in_line_start'.
     [//]
     The `log2_source_headroom' member is normally set to 0.  If the
     original sample values are identified as having a pixel format of
     `JP2_CHANNEL_FORMAT_FIXPOINT', however, `log2_source_headroom' is the
     number of integer bits in the fixed-point representation, corresponding
     to the amount by which compressed sample values were effectively scaled
     down (well log-base-2 of this scaling factor) so as to accommodate
     the encoding of intensity values larger than the nominal maximum (usually
     for super-luminous regions in high dynamic range imagery).  If the
     original sample values are identified as having a pixel format of
     `JP2_CHANNEL_FORMAT_FLOAT', `log2_source_headroom' is set to one more
     than the maximum positive exponent in the associated custom floating point
     representation, being 2^{E-1}, where E is the number of exponent bits,
     noting that sample values whose bit patterns are re-interpreted as
     floating point numbers cannot have a larger exponent than 2^{E-1}-1,
     without being interpreted as +/- infinity or NaN, and there is an implicit
     leading 1 ahead of the mantissa.  The number of fixed-point integer
     bits and floating-point exponent bits are already recorded in the
     `interp_fixpoint_int_bits' and `interp_float_exp_bits' members.
     The `log2_source_headroom' value can be non-zero only if the channel
     has no palette lookup table (`lut_fix16' is NULL).  The main reason for
     recording `log2_source_headroom' is that it affects the configuration
     of any quality limiter (`kdu_quality_limiter').  In particular, the
     effective squared error contribution for the source component associated
     with this channel needs to be scaled by 2^{2*`log2_source_headroom'} to
     reflect the amount by which quantization errors might be magnified when
     fixed-point or floating-point formatted data are converted to rendered
     outputs.
   */
  
/*****************************************************************************/
/*                              kdrd_channel_buf                             */
/*****************************************************************************/

struct kdrd_channel_buf {
    kdu_byte *buf; // Buffer may have other types (kdu_uint16 or float)
    kdrd_channel *chan; // Actual channel to transfer the data from; see below
    int comp_bit_depth; // Same as `chan->source->bit_depth', for convenience
    int transfer_precision; // From `process' call or `chan->native_precision'
    bool transfer_signed; // From `process' call or `chan->native_signed'
    bool fill; // If true, the buffer should be filled with white/opaque
    float src_scale; // Scale and offset parameters, applied to source
    float src_off; // samples to implement the relevant scaling policy
    bool clip_outputs; // See below
    kdrd_transfer_func transfer_func; // Does transfer from `chan->out_line'
    int ilv_src; // Permutes channels for interleaved transfers (see below)
  };
  /* Notes:
        An array of these objects keep track of information passed across
     the `kdu_region_decompressor::process' interfaces, after some digestion.
     The object also stores state information that depends upon the combination
     of parameters passed to a `process' call and parameters derived from a
     tile-bank when it is started, so that this information need be
     recomputed only when absolutely necessary.
        The `chan' and `buf' entries in a valid instance of this object may
     not be NULL.  If `fill' is true, no source channel is actually used to
     obtain the data, but `chan' must still point to a valid channel for
     reasons of uniformity -- in practice, the first channel will do.
     The reason why `buf' may not be NULL is that instances of this structure
     are assigned during the individual `process' function calls, and those
     calls always assign exactly the same number of entries in the
     `kdu_region_decompressor::channel_bufs' array as there are actual
     channel buffers to be written.
        The `src_scale', `src_off' and `clip_outputs' members are configured
     at the same time as the `transfer_func' member, since they depend upon
     both the source characteristics and the particular `process' function
     that is called, along with its parameters.  These parameters are all
     passed to the `transfer_func' function when it is called; see the
     definition of `kdrd_transfer_func' for an explanation.
        The `transfer_func' member is always initialized to NULL by the
     front-end call to `kdu_region_decompressor::process', but is assigned
     a non-NULL transfer function pointer within `process_generic', since
     the transfer function pointer depends both on the parameters stored here,
     the number of `sample_bytes' passed to `process_generic', and possibly
     the source data types and precisions associated with the current tile
     bank.  The function pointer might be changed each time a new tile
     bank is made current.
        The `ilv_src' member is used only for joint interleaved data transfers
     to a whole collection of channel buffers.  If an interleaved transfer
     function exists, conforming to the `kdrd_interleaved_transfer_func'
     signature, its four source buffer arguments are obtained by using this
     member.  Specifically, for each c=0 through 3, the c'th source buffer
     passed to the interleaved transfer function comes from
     `channel_bufs[d].chan', where d = channel_bufs[c].ilv_src, and
     the base of the interleaved output is at `channel_bufs[d].buf', where
     d = `channel_bufs[0].ilv_src'.
  */
  
} // namespace kd_supp_local

#endif // REGION_DECOMPRESSOR_LOCAL_H
