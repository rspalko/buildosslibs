/*****************************************************************************/
// File: multi_transform_local.h [scope = CORESYS/TRANSFORMS]
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
   Local definitions used by "multi_transform.cpp".  These are not to be
included from any other scope.
******************************************************************************/

#ifndef MULTI_TRANSFORM_LOCAL_H
#define MULTI_TRANSFORM_LOCAL_H

#include <assert.h>
#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "transform_base.h"

// Defined here:
namespace kd_core_local {
  struct kd_multi_nlt;
  struct kd_multi_line;
  struct kd_multi_collection;
  class kd_multi_job;
  class kd_multi_queue;
  class kd_multi_component;
  class kd_multi_block;
  class kd_multi_null_block;
  class kd_multi_matrix_block;
  class kd_multi_dependency_block;
  class kd_multi_block;
  class kd_multi_null_block;
  class kd_multi_matrix_block;
  class kd_multi_rxform_block;
  class kd_multi_dependency_block;
  struct kd_multi_dwt_level;
  class kd_multi_dwt_block;
  class kd_multi_transform;
  class kd_multi_synthesis;
  class kd_multi_analysis;
}

namespace kd_core_local {
  using namespace kdu_core;

/*****************************************************************************/
/*                              kd_multi_nlt                                 */
/*****************************************************************************/

typedef void (*kd_multi_usmag32_func)
  (kdu_int32 *src, kdu_int32 *dst, int num_samples, int precision,
   bool src_absolute, bool dst_absolute);
typedef void (*kd_multi_usmag16_func)
  (kdu_int16 *src, kdu_int16 *dst, int num_samples, int precision,
  bool src_absolute, bool dst_absolute);

struct kd_multi_nlt {
  public: // Member functions
    kd_multi_nlt()
      { 
        descriptor=0; out_precision=0; out_absolute=false;
        lut_dmin = lut_scale = 0.0f; lut_entries=0; lut_owner=NULL; lut=NULL;
        usmag32_func = NULL; usmag16_func = NULL;
      }
    ~kd_multi_nlt()
      { if ((lut != NULL) && (lut_owner == this)) delete[] lut; }
    kdu_line_buf *process(kdu_line_buf *src, kdu_line_buf *dst);
      /* Performs the relevant non-linear point transformation on the samples
         in `src', writing the results to `dst' and returning `dst'.  If
         `src' and `dst' are identical, conversion is performed in-situ.
         Otherwise, either `src' or `dst' will be a pointer to the `ext_line'
         member.  The SMAG and UMAG NLT types are unique in that they may
         involve conversions between absolute integers and fixed-point or
         floating-point representations, in which case `ext_line' must exist
         and will hold the absolute integer representation.  See the notes
         below for more information on SMAG and UMAG. */
  public: // Data
    kdu_line_buf ext_line; // See below
    int descriptor; // From `kdu_tile::get_nlt_descriptors'
    int out_precision;
    bool out_absolute; // See below
    float lut_dmin; // Subtract this from input samples
    float lut_scale; // Multiply this by the result to get an lut index
    int lut_entries; // Zero only if `sign_mag_precision' != 0
    kd_multi_nlt *lut_owner; // LUT may be shared with other objects
    float *lut; // Always has one extra entry to facilitate interpolation
  public: // Function pointers that facilitate SIMD acceleration
    kd_multi_usmag32_func usmag32_func; // These each handle either an SMAG or
    kd_multi_usmag16_func usmag16_func; // a UMAG conversion, as configured.
  };
  /* One of these objects is created for each unique descriptor based on
     calls to `kdu_tile::get_nlt_descriptors', in which case it appears
     within an owning `kd_multi_line' object.
        In many cases, non-linear point transforms can be performed in place,
     within the multi-line object's line buffer.  However, in cases where
     the multi-line object has multiple consumers, or the `out_absolute'
     differs from the multi-line object's `reversible' member, the `ext_line'
     object is instantiated to hold a representation of the samples that is
     seen on the outside of the `kdu_multi_analysis' or
     `kdu_multi_synthesis' objects.
        We start with the sign-magnitude transforms.  If `lut' is NULL, the
     4 lsb's of descriptor will contain one of the NLT type codes
     `NLType_SMAG' or `NLType_UMAG'.  In both cases, `out_absolute' will be
     true, regardless of whether the owning `kd_multi_line' object has a
     reversible or an irreversible representation.  That is, the SMAG/UMAG
     transforms always work with absolute integers, but the transform may
     involve a conversion to/from a fixed-point or floating-point
     representation.  During synthesis (i.e., decompression), the
     representation used by `kd_multi_line' is converted first to absolute
     integers, if necessary, clipping the integers strictly to the range
     -2^{P-1} to 2^{P-1}-1, after which SMAG additionally inverts the P-1
     least significant bits of any negative value, where P is the
     `out_precision' value.  During analysis (i.e., compression), the
     clipping and bit-flipping (SMAG only) process is performed first, after
     which the result may need to be transformed to a fixed- or floating-point
     representation if `kd_multi_line::reversible' is false.
        If `lut' is non-NULL, the non-linear point transform to
     be implemented involves processing the samples directly in their
     mid-level adjusted and potentially normalized form.  Each input sample
     x is converted to y = (x-lut_dmin)*lut_scale and then clipped to the
     range [0,lut_entries-1], after which we form b=floor(y) and f=y-b.
     The final result is obtained from lut[b] + f*(lut[b]-lut[b+1]).  For
     this to work, we ensure that the `lut' array actually has `num_entries'+1
     accessible entries, the last of which is obtained by replication.
     Currently all lut-based processing uses the floating point lookup table,
     after which the result may need to be converted back to an integer
     representation.  Of course, there are many ways to accelerate the
     non-linear transform method, including the use of vector gather
     intrinsics and integer-valued lookup tables. */
  
/*****************************************************************************/
/*                              kd_multi_line                                */
/*****************************************************************************/

typedef void (*kd_multi_line_rev_copy_func)
  (void *in_buf, void *out_buf, int num_samples,
   bool using_shorts, int rev_offset);
  /* Used to accelerate calls to `kd_multi_line::copy' and
     `kd_multi_line::apply_offset' where the input and output lines both
     have a reversible representation (absolute 16- or 32-bit integers);
     `rev_offset' is added to each sample as it is copied.  The input and
     output buffers can be the same or different. */
typedef void (*kd_multi_line_irrev_copy_func)
  (void *in_buf, void *out_buf, int num_samples,
   bool using_shorts, float irrev_offset);
  /* Similar to `kd_multi_line_rev_copy_func', but this one is used to
     copy or apply an offset in the case where the input and output lines both
     have an irreversible (float- or 16-bit fixed-point) representation,
     and both have the same original bit-depths.  There are cases in which
     the input and output lines have different original bit-depths or where
     the input line is reversible while the output is reversible, but these
     are unusual and no accelerators are provided. */
  
struct kd_multi_line {
  public: // Member functions
    kd_multi_line()
      { 
        row_idx = -1;  num_consumers = outstanding_consumers = 0;
        reversible = need_irreversible = need_precise = is_constant = false;
        lin_bit_depth = 0; rev_offset = 0; irrev_offset = 0.0F;
        nlt_line = NULL;  bypass=NULL;  block=NULL;  collection_idx = -1;
        rev_copy_func = NULL;  irrev_copy_func = NULL;
      }
    ~kd_multi_line()
      { if (nlt_line != NULL) delete nlt_line; }
    void initialize() { reset(rev_offset,irrev_offset); }
      /* Do not call this function until after the `line' member has been
         created.  Sets the line's contents equal to the value of the
         `rev_offset' or `irrev_offset' member, depending on the
         `reversible' flag.  Normally, this function need only be called
         if `is_constant' is true. */
    void reset(int rev_off, float irrev_off);
      /* Sets the contents of the line to `rev_off' if it has a reversible
         representation, else `irrev_off'. */
    void apply_offset(int rev_off, float irrev_off);
      /* Adds the offset defined by `rev_off' or `irrev_off', whichever
         is appropriate, to the embedded `line's sample values.  Ignores the
         internal `rev_offset' and `irrev_offset' members, although these may
         well be the values used to derive the `rev_off' and `irrev_off'
         arguments. */
    void copy(kd_multi_line *src, int rev_offset, float irrev_offset);
      /* Copies the contents of `src' to the present line.  The function
         ignores any offsets which may be recorded in either object, using
         the offset and scaling factors supplied via the function's
         arguments instead (of course, these may be derived from one or
         both objects' members).  Both lines must have the same width.
         If the target line is reversible, the source line must also be
         reversible, the `irrev_offset' argument is ignored and `rev_offset'
         is added to the source sample values as they are copied.  If the
         target line is irreversible, the `rev_offset' argument is ignored
         and `irrev_offset' is added to the source samples after applying any
         required conversions: if the source samples are also irreversible,
         conversion means scaling by 2^{src->bit_depth} / 2^{this->bit_depth};
         if the source samples are reversible, conversion means scaling by
         1 / 2^{this->bit_depth}. */
  public: // Data
    kdu_line_buf line;
    kdu_coords size; // Size of intermediate component to which line belongs
    int row_idx; // First valid row is 0 -- initialized to -1
    int num_consumers; // See below
    union {
        int outstanding_consumers; // See below
        bool waiting_for_inversion; // See below
      };
    bool reversible;   // True if producer or consumer needs reversible data
    bool need_irreversible; // If producer or consumer needs irreversible data
    bool need_precise; // If producer or consumer needs 32-bit samples
    bool is_constant; // True if there is no generator for this line
    int lin_bit_depth; // Used throughout any MCT transformation
    int rev_offset;
    float irrev_offset;
    kd_multi_nlt *nlt_line; // See below -- may identify a different bit-depth
    kd_multi_line *bypass; // See below
    kd_multi_block *block;
    int collection_idx; // See below
  public: // Members used to facilitate SIMD acceleration
    kd_multi_line_rev_copy_func rev_copy_func; // At most one of these will be
    kd_multi_line_irrev_copy_func irrev_copy_func; // non-NULL.
  };
  /* Notes:
        Each `kd_multi_line' object represents a single intermediate component.
     We describe the flow of information in terms of generators and consumers,
     taking the perspective of the decompressor, since this is the more
     complex case.  During compression, however, the roles of generator
     and consumer are interchanged.
        The line associated with an intermediate component has at most one
     generator, but potentially multiple consumers.  Consumers include
     transform blocks and the application, which is a consumer for
     all final output components.  During decompression, the line cannot be
     advanced until all consumers have finished using its contents -- i.e.,
     until `outstanding_consumers' goes to 0.  When it does, a new line is
     generated, `row_idx' is advanced, and `outstanding_consumers' is reset
     to `num_consumers'.  During compression, the `outstanding_consumers'
     member is not used.  In this case, the `waiting_for_inversion' flag is
     involved -- it is set to true if the row identified by `row_idx' has
     been written by a consumer, but has not yet been used by the transform
     block.
        If `block' is non-NULL, the line is generated by the referenced
     transform block.  Otherwise, the line either corresponds to a
     codestream image component or a constant component (`is_constant' is
     true).  For codestream components, the `collection_idx' identifies the
     index of the entry in the `kd_component_collection::components'
     array within the codestream component collection (maintained by
     `kd_multi_transform') which points to this line.  This is the only
     piece of information required to pull or push a new line to the
     relevant codestream component processing engine.  In all other cases,
     `collection_idx' will be negative.
        If `bypass' is non-NULL, all accesses to this line should be
     treated as accesses to the line identified by `bypass'.  In this
     case, all offsets are collapsed into the line referenced by `bypass'
     so that no arithmetic is required here.  Also, in this case the
     `line' resource remains empty.  `bypass' pointers are found during
     a transform optimization phase; they serve principally to avoid
     line copying when implementing null transforms.
        Note that no line which has `is_constant' set will be bypassed.  Also
     note that all line references found in `kd_multi_block::dependencies'
     or `kd_multi_collection::components' arrays are adjusted to ensure that
     they do not point to lines which have been bypassed -- they always
     point to the lines which actually have the source data of interest.
     This simplifies the processing of data.
        The `nlt_line' member is NULL unless there is a non-linear transform
     associated with the component and this object belongs to the output
     collection.  If there is one, this object's `process' function should
     be used to transform data supplied to `kd_multi_analysis::exchange_line'
     or retrieved by `kd_multi_synthesis::get_line'.  In the latter case, if
     this object's `num_consumers' member differs from 1, the processed
     imagery is written to `nlt_line->ext_line' and that is the line that
     should be returned to the application.
  */
  
/*****************************************************************************/
/*                              kd_multi_block                               */
/*****************************************************************************/

class kd_multi_block {
  public: // Member functions
    kd_multi_block()
      {
        is_null_transform = true; // Changed by derived constructors
        num_components = 0;  components = NULL;  next = prev = NULL;
        num_dependencies = num_available_dependencies = 0; dependencies = NULL;
        outstanding_consumers = 0;
      }
    virtual ~kd_multi_block()
      {
        if (components != NULL) delete[] components;
        if (dependencies != NULL) delete[] dependencies;
      }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner,
                 bool for_synthesis) = 0;
      /* This function does most of the work of configuring the transform
         block.  It creates the `components' and `dependencies' array and
         inserts output component references into the `output_collection'.
         It initializes all transform coefficients and offset vectors based
         directly on the parameters supplied via the `kdu_tile' interface --
         these are based on the assumption that imagery flows through the
         processing system at the bit-depths declared in the codestream's
         `CBD' and `SIZ' marker segments.  Later, once these bit-depths
         have been discovered, the `normalize_coefficients' function should
         be called to adapt the transform coefficients and offset vectors
         to Kakadu's normalization conventions -- as advertized in the
         description of `kdu_multi_analysis' and `kdu_multi_synthesis'.
         Note, however, that normalization only impacts irreversible
         processing. */
    virtual void normalize_coefficients() { return; }
      /* This function is called after initialization and after the
         bit-depth information associated with codestream and output image
         components has been propagated as far as possible into the
         network.  The function need not be implemented by null transforms
         or reversible transforms.  For irreversible transforms which
         actually do some processing, however, the transform coefficients
         may need to be normalized to account for the fact that irreversible
         image sample representations have a nominal range of 1, whereas
         the coefficients have been initialized based on the assumption
         that they have a nominal range of 2^B where B is the relevant
         bit-depth.  The normalization of line offsets, recorded in
         `kd_multi_line::irrev_offset' is handled by the general framework
         in `kd_multi_transform::construct'.  In fact, this is guaranteed
         to have been done prior to calling the present function. */
    virtual bool propagate_bit_depths(bool need_input_bit_depth,
                                      bool need_output_bit_depth)
      { return false; }
      /* This function is called to see if it is possible to infer the
         bit-depths of one or more dependencies from the bit-depths of
         the block's output components, or vice-versa.  In many cases, this
         is a non-trivial problem, so for most block types, the function
         immediately returns false, meaning that nothing is changed.  If,
         however, the bit-depth of any input or any output can be
         inferred, the function returns true.  For DWT blocks this can
         often be done.  The `need_input_bit_depth' argument is true if
         one or more dependencies has an unknown bit-depth, while the
         `need_output_bit_depth' argument is true if one or more
         block output components has an unknown bit-depth. */
    virtual void perform_transform() { return; }
      /* This function need not be implemented by null transforms.  It
         is called automatically by the framework when `outstanding_consumers'
         is 0 and all dependency lines have become available.  The function
         should write appropriate values to all non-constant output lines
         (the ones in the `components' array), applying any required
         offsets.  The function should not adjust the state information in the
         base object (e.g., `outstanding_consumers') or any of the output
         lines, since this is done generically by the caller, for all
         transform types. */
    virtual const char *prepare_for_inversion()
      { return "Unimplemented multi-component transform block inversion "
               "procedure."; }
      /* If the transform block can be inverted, based on the entries in the
         `components' array which have non-zero `num_consumers' values
         (remember that consumers for the forward transform are generators
         for its inverse), the function returns NULL.  Otherwise, the
         function returns a constant text string, explaining why inversion
         was not possible.  If this ends up causing the multi-component
         transform network to be non-invertible, the reason message will
         be used to provide additional explanation to the user.  In any case,
         if the function returns non-NULL, no attempt will be made to call
         this object's `perform_inverse' function and the framework will
         decrement the `num_consumers' count associated with all of its
         dependencies and define all of its components to be constant --
         meaning that downstream transform blocks need not write to them.
            If the transform block can be inverted, this function should
         leave the `num_outstanding_consumers' member equal to the number
         of lines in the `components' array which must be filled in by
         the application (or by inverting a downstream transform block)
         before the `perform_inverse' function can be executed. */
    virtual void perform_inverse() { return; }
      /* This function need not be implemented by null transforms.  It is
         called automatically by the framework when `outstanding_consumers'
         is 0 and all dependency lines have become free for writing.  The
         function will only be called if `prepare_for_inversion' previously
         returned true (during network configuration).  The function is
         expected to write the results to those lines referenced by its
         `dependencies' array, skipping over those whose reference is NULL,
         and subtracting whatever offsets are specified by the lines
         which are written. */
    virtual void install_accelerators(bool for_synthesis) { return; }
      /* Called after the network has been constructed, so that as much
         information as possible is available, on which to base to decision
         as to whether any heavily optmized accelerator functions can be
         installed.  Not all transform block types need have accelerators,
         and accelerators can be installed earlier, e.g. by `initialize', if
         the finalized network configuration is not required. */
  public: // Data
    bool is_null_transform; // See below
    int num_components; // Number of lines produce/manipuated by this block
    kd_multi_line *components;
    int num_dependencies;
    kd_multi_line **dependencies;
    int num_available_dependencies;
    int outstanding_consumers; // Sum of all outstanding line consumers
    kd_multi_block *next;
    kd_multi_block *prev;
  };
  /* Notes:
        This class manages a single transform block in the multi-component
     transformation process (forwards or backwards).  As with
     `kd_multi_line', the names `generator', `consumer' and `dependency'
     are selected with the decompressor (synthesis) in mind, since this is
     the more complex case.  During compression, the information flows in
     the opposite direction, but the names are not changed (for convenience).
        Each transform block "owns" a set of component lines, as its
     resources.  In most cases, these correspond to the output components,
     which are passed along to the next transform stage, or as final
     decompressed output components.  However, some transform blocks may
     need a larger set of working components than the number of output
     (or even input) components which they produce (or consume).  In any
     event, these owned component lines are described by the `num_components'
     and `components' members.
        The `num_dependencies' and `dependencies' fields identify the
     collection of input lines which are required for this transform block
     to produce output lines (during compression, the roles are reversed,
     so that `dependencies' actually identifies the lines which should be
     filled in when the output lines all become available and are processed;
     as mentioned, though, we describe things here primarily from the
     decompressor's perspective).  The pointers in the `dependencies'
     array refer either to codestream component lines or lines produced as
     outputs by other transform blocks.  It is possible that one or more
     entries in the `dependencies' array contains a NULL pointer.  In this
     case, the input line should be taken to be identically 0 for the
     purpose of performing the transform.
        The `num_available_dependencies' member keeps track of the number of
     initial lines from the `dependencies' array which are already available
     for processing.  During decompression, this means that their
     `kd_multi_line::row_idx' field is equal to the index of the
     next row which needs to be generated by this block.  During compression
     (where the transform block must be inverted), this means that their
     `kd_multi_line::waiting_for_inversion' member is false.  In either case,
     the value of this member must reach `num_dependencies' before a new set
     of lines may be processed by the transform engine.
        The `outstanding_consumers' member holds the sum of the
     `kd_multi_line::outstanding_consumers' members from all lines in the
     `components' array.  This value must reach zero before a new set of
     lines can be processed by the transform engine.
        If `is_null_transform' is true, the transform block does no actual
     processing.  Instead, the first `num_dependencies' of the lines in
     `components' are directly taken from their corresponding dependencies
     and any remaining lines in the `components' array have their
     `is_constant' flag set to true.  In this case, the
     `outstanding_consumers' and `num_available_dependencies' members
     should be ignored. */

/*****************************************************************************/
/*                            kd_multi_null_block                            */
/*****************************************************************************/

class kd_multi_null_block: public kd_multi_block {
  public: // Member functions
    kd_multi_null_block()
      { is_null_transform = true; }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner, bool for_synthesis);
  };

/*****************************************************************************/
/*                           kd_multi_matrix_block                           */
/*****************************************************************************/

typedef void (*kd_multi_matrix32_func)
  (void **in_bufs, void **out_bufs, int num_samples,
   int num_inputs, int num_outputs, float *coeffs, float *offsets);
  /* Performs a complete matrix transform.  Inputs are the block dependencies,
     while outputs are its components.  The `coeffs' array is in raster order
     with each row corresponding to one output.  The `offsets' are to be
     added to each result.  The `in_bufs' and `out_bufs' arrays all hold
     pointers to floating-point buffers.  Note that input or output buffers
     can be NULL; NULL inputs are taken to be constant 0, while NULL outputs
     are unwanted outputs. */
typedef void (*kd_multi_matrix16_func)
  (void **in_bufs, void **out_bufs, kdu_int32 *acc, int num_samples,
   int num_inputs, int num_outputs, kdu_int16 *coeffs, int downshift,
   float *offsets);
  /* As above, but the `in_bufs' and `out_bufs' arrays hold pointers to
     16-bit fixed-point sample buffers, and the `acc' buffer is used to
     perform intermediate accumulation of multiplication results, which
     are then offset and downshifted.  The offsets need to be multiplied by
     2^{KDU_FIX_POINT} and upshifted by `downshift' before adding to the
     accumulator.  Note that the `acc' buffer might not be aligned on anything
     other than a 32-bit boundary, but it is guaranteed to point to a memory
     block that is 3*`KDU_ALIGN_SAMPLES32' samples larger than `num_samples',
     so that the implementation can do its own alignment and safely work
     with vectors. */
  
class kd_multi_matrix_block: public kd_multi_block {
  public: // Member functions
    kd_multi_matrix_block()
      { 
        is_null_transform = false;
        coefficients = inverse_coefficients = NULL;
        short_coefficients = NULL;  short_accumulator = NULL;  work = NULL;
        out_offsets = NULL; in_line_bufs=NULL; out_line_bufs=NULL;
        matrix32_func = NULL; matrix16_func = NULL;
      }
    virtual ~kd_multi_matrix_block()
      { 
        if (coefficients != NULL) delete[] coefficients;
        if (inverse_coefficients != NULL) delete[] inverse_coefficients;
        if (short_coefficients != NULL) delete[] short_coefficients;
        if (short_accumulator != NULL) delete[] short_accumulator;
        if (work != NULL) delete[] work;
        if (out_offsets != NULL) delete[] out_offsets;
        if (in_line_bufs != NULL) delete[] in_line_bufs;
        if (out_line_bufs != NULL) delete[] out_line_bufs;
      }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner, bool for_synthesis);
    virtual void normalize_coefficients();
    virtual void perform_transform();
    virtual const char *prepare_for_inversion();
    virtual void perform_inverse();
    virtual void install_accelerators(bool for_synthesis);
      /* Installs at most one of `matrix32_func' or `matrix16_func'and creates
         the associated arrays, `out_offsets', `out_line_bufs' and
         `in_line_bufs', if an accelerator function is installed. */
  private: // Helper functions
    void create_short_coefficients(int width);
      /* This function creates the `short_coefficients' to represent
         the `coefficients' array in fixed-point, choosing an appropriate
         value for `short_downshift'.  It also creates the `short_accumulator'
         array for holding intermediate results. */
    void create_short_inverse_coefficients(int width);
      /* Same as `create_short_coefficients', but creates the
         `short_coefficients' array to hold a fixed-point representation of
         the `inverse_coefficients' array, rather than the `coefficients'
         array. */
  private: // Data
    float *coefficients;
    float *inverse_coefficients; // Created by `prepare_for_inversion'
    kdu_int16 *short_coefficients;
    kdu_int32 *short_accumulator; // Working buffer for fixed-point processing
    int short_downshift;
    double *work; // Working memory for creating inverse transforms
  public: // Members to facilitate SIMD acceleration
    float *out_offsets; // These arrays are dimensioned for forward or inverse
    void **in_line_bufs; // matrix transformation, depending on how the
    void **out_line_bufs; // `install_accelerators' function is called.
    kd_multi_matrix32_func matrix32_func; // These point to accelerator funcs
    kd_multi_matrix16_func matrix16_func; // if available; NULL if any of the
      // dependencies has a reversible representation, which can happen during
      // forward (i.e., decompression) transformation, but should be rare.
  };

/*****************************************************************************/
/*                           kd_multi_rxform_block                           */
/*****************************************************************************/

class kd_multi_rxform_block: public kd_multi_block {
  public: // Member functions
    kd_multi_rxform_block()
      {
        is_null_transform = false;
        coefficients = NULL;  accumulator = NULL;
      }
    virtual ~kd_multi_rxform_block()
      {
        if (coefficients != NULL) delete[] coefficients;
        if (accumulator != NULL) delete[] accumulator;
      }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner, bool for_synthesis);
    virtual void perform_transform();
    virtual const char *prepare_for_inversion();
    virtual void perform_inverse();
  private: // Data
    int *coefficients;
    kdu_int32 *accumulator;
  };

/*****************************************************************************/
/*                         kd_multi_dependency_block                         */
/*****************************************************************************/

class kd_multi_dependency_block: public kd_multi_block {
  public: // Member functions
    kd_multi_dependency_block(bool is_reversible)
      {
        is_null_transform=false;  this->is_reversible = is_reversible;
        num_coeffs=0; short_matrix=NULL;  short_downshift=0; accumulator=NULL;
        i_matrix = i_offsets = NULL;  f_matrix = f_offsets = NULL;
      }
    virtual ~kd_multi_dependency_block()
      {
        if (i_matrix != NULL) delete[] i_matrix;
        if (i_offsets != NULL) delete[] i_offsets;
        if (f_matrix != NULL) delete[] f_matrix;
        if (f_offsets != NULL) delete[] f_offsets;
        if (short_matrix != NULL) delete[] short_matrix;
        if (accumulator != NULL) delete[] accumulator;
      }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner, bool for_synthesis);
    virtual void normalize_coefficients();
    virtual void perform_transform();
    virtual const char *prepare_for_inversion();
    virtual void perform_inverse();
  private: // Helper functions
    void create_short_matrix();
  private: // Data
    bool is_reversible;
    int num_coeffs;
    int *i_matrix, *i_offsets;    // Note: matrices are square, even if only
    float *f_matrix, *f_offsets;  // lower triangular part is used
    kdu_int16 *short_matrix; // See below
    int short_downshift;
    kdu_int32 *accumulator;
  };

/*****************************************************************************/
/*                            kd_multi_dwt_level                             */
/*****************************************************************************/

struct kd_multi_dwt_level {
  public: // Member functions
    kd_multi_dwt_level()
      {
        canvas_min = canvas_size = canvas_low_size = canvas_high_size = 0;
        region_min = region_size = region_low_size = region_high_size = 0;
        components = NULL;  dependencies = NULL;
      }
    ~kd_multi_dwt_level()
      {
        if (components != NULL) delete[] components;
        if (dependencies != NULL) delete[] dependencies;
      }
  public: // data
    int canvas_min, canvas_size, canvas_low_size, canvas_high_size;
    int region_min, region_size, region_low_size, region_high_size;
    kd_multi_line **components; // See below
    kd_multi_line ***dependencies; // See below
    int normalizing_shift; // See below
    float low_range, high_range; // See below
  };
  /* Notes:
        The `canvas_min' and `canvas_size' members define the region occupied
     by the complete DWT on its own 1D canvas.  `canvas_low_size' and
     `canvas_high_size' hold the number of low- and high-pass subband samples
     found within this region.
        The `region_min', `region_size', `region_low_size' and
     `region_high_size' members play the same roles as `canvas_min' through
     `canvas_high_size', except for a region of interest within the canvas;
     the region of interest is derived from the set of active image components
     identified by `kdu_tile::get_mct_dwt_info'.
        The `components' array here contains `region_size' pointers into the
     `kd_multi_dwt_block' object's `components' array.
        The `dependencies' array contains `region_size' pointers
     into the `kd_multi_dwt_block' object's `dependencies' array.  NULL
     entries in this array correspond to lines which are derived from the
     next lower DWT stage (during DWT synthesis -- i.e., decompression);
     non-NULL pointers which point to NULL entries in the
     `kd_multi_dwt_block::dependencies' array correspond to lines which must
     be initialized to 0 prior to DWT synthesis; otherwise, the relevant
     dependency (input) is to be copied to the corresponding `components'
     line prior to DWT synthesis at this level.
        The `normalizing_shift' member is the amount to downshift the inputs
     to this level during analysis or upshift the outputs from this level
     during synthesis so as to avoid dynamic range violations during
     fixed-point irreversible processing.
        The `low_range' and `high_range' members identify the amount by which
     low and high subband samples produced at this level (during analysis)
     are scaled, relative to the unit dynamic range that would be expected
     if the DWT were applied using low- and high-pass analysis filters with
     unit pass-band gain, assuming that the input components all have a
     unit dynamic range.  These scaling factors are of interest only when
     working with irreversible transforms, since they affect the amount by
     which samples must be scaled as they are transferred between the
     lines referenced via the `components' and `dependencies' arrays.  These
     range values depart from 1.0 only because we prefer to avoid introducing
     subband gains into each DWT stage. */

/*****************************************************************************/
/*                            kd_multi_dwt_block                             */
/*****************************************************************************/

class kd_multi_dwt_block: public kd_multi_block {
  public: // Member functions
    kd_multi_dwt_block()
      {
        is_null_transform=false;  num_levels=0;  levels=NULL;
        num_steps=max_step_length=0;  steps=NULL;
        num_coefficients=0;  f_coefficients=NULL;  i_coefficients=NULL;
        src_bufs32 = NULL;  assert(src_bufs16 == NULL);
      }
    virtual ~kd_multi_dwt_block()
      {
        if (levels != NULL) delete[] levels;
        if (steps != NULL) delete[] steps;
        if (f_coefficients != NULL) delete[] f_coefficients;
        if (i_coefficients != NULL) delete[] i_coefficients;
        if (src_bufs32 != NULL) { delete[] src_bufs32;  src_bufs32=NULL; }
        assert(src_bufs16 == NULL); // `src_bufs32' & `src_bufs16' are aliased
      }
    virtual void
      initialize(int stage_idx, int block_idx, kdu_tile tile,
                 int num_block_inputs, int num_block_outputs,
                 kd_multi_collection *input_collection,
                 kd_multi_collection *output_collection,
                 kd_multi_transform *owner, bool for_synthesis);
    virtual void normalize_coefficients();
    virtual bool propagate_bit_depths(bool need_input_bit_depth,
                                      bool need_output_bit_depth);
    virtual void perform_transform();
    virtual const char *prepare_for_inversion();
    virtual void perform_inverse();
  private: // Data
    int num_levels;
    kd_multi_dwt_level *levels; // See below
    bool is_reversible;
    bool symmetric, sym_extension;
    int num_steps;
    kd_lifting_step *steps;
    int num_coefficients; // Total number of lifting step coefficients
    float *f_coefficients; // Resource referenced by `kd_lifting_step::fcoeffs'
    int *i_coefficients; // Resource referenced by `kd_lifting_step::icoeffs'
    int max_step_length; // Longest lifting step support
    union {
        kdu_sample32 **src_bufs32;   // These have `max_step_length' entries
        kdu_sample16 **src_bufs16;
      };
  };
  /* Notes:
       The `levels' array runs in reverse order, starting from the lowest
     DWT level (i.e., the one containing the L-band).
  */

/*****************************************************************************/
/*                           kd_multi_collection                             */
/*****************************************************************************/

struct kd_multi_collection {
  public: // Member functions
    kd_multi_collection()
      { num_components=0;  components=NULL;  next=prev=NULL; }
    ~kd_multi_collection()
      { delete[] components; }
  public: // Data
    int num_components;
    kd_multi_line **components;
    kd_multi_collection *next;
    kd_multi_collection *prev;
  };
  /* Notes:
        This structure is used to manage the collection of components at
     the interface between two transform stages, or at the input to the
     first codestream stage or output from the last codestream stage.  Lists
     of component collections start at the codestream components and end
     at the output image components, produced during decompression.  The
     interpretation is the same during compression, but the output
     components become inputs components and vice-versa.
        The `components' array contains `num_components' pointers to the
     lines which represent each component in the collection.  Most of these
     lines are "owned" by `kd_multi_block' objects.  However, if the
     referenced line's `kd_multi_line::block' member is NULL, the line
     belongs either to the `kd_multi_transform::constant_output_lines' array
     or else it belongs to the `kd_multi_transform::codestream_components'
     array.  The latter is used exclusively by the first collection in
     the `kd_multi_transform' object -- the one referenced by
     `kd_multi_transform::codestream_collection'. */

/*****************************************************************************/
/*                              kd_multi_job                                 */
/*****************************************************************************/

class kd_multi_job : public kdu_thread_job {
  public: // Member functions
    static void do_mt_analysis(kd_multi_job *job, kdu_thread_env *caller);
    static void do_mt_synthesis(kd_multi_job *job, kdu_thread_env *caller);
      /* Upon initialization, one of the above two functions is passed to
         `set_job_func' if `num_stripes' > 1 -- otherwise, we are not
         going to be scheduling any jobs, so the DWT processing will be done
         within the `kd_multi_component::new_stripe_ready_for_analysis'
         and `kd_multi_component::get_new_synthesized_stripe' functions. */
  private: // Data
    friend class kd_multi_queue;
    kd_multi_queue *queue; // Points to the queue we are embedded inside
  };

/*****************************************************************************/
/*                             kd_multi_queue                                */
/*****************************************************************************/

/* The following definitions allow for up to 255 stripes, whereas we currently
   only use 2 stripes (double-buffering) for the asynchronous DWT.  Even so,
   we have plenty of spare bits in the 32-bit `sync_MDW' variable whose
   bit-fields are defined by these macros. */
#define KD_MULTI_XFORM_SYNC_D_POS  0
#define KD_MULTI_XFORM_SYNC_D0_BIT \
                               ((kdu_int32)(1<<KD_MULTI_XFORM_SYNC_D_POS))
#define KD_MULTI_XFORM_SYNC_D_MASK \
                               ((kdu_int32)(255<<KD_MULTI_XFORM_SYNC_D_POS))
#define KD_MULTI_XFORM_SYNC_M_POS  16
#define KD_MULTI_XFORM_SYNC_M0_BIT \
                               ((kdu_int32)(1<<KD_MULTI_XFORM_SYNC_M_POS))
#define KD_MULTI_XFORM_SYNC_M_MASK \
                               ((kdu_int32)(255<<KD_MULTI_XFORM_SYNC_M_POS))
#define KD_MULTI_XFORM_SYNC_W_POS  30
#define KD_MULTI_XFORM_SYNC_W_BIT \
                               ((kdu_int32)(1<<KD_MULTI_XFORM_SYNC_W_POS))

/* The following definitions allow for up to 2047 subbands of a tile-component,
   whereas the JPEG2000 Part-2 standard limits the number of subbands to
   1+48*32=1537, and typical applications involve no more than about 30
   subbands. */
#define KD_MULTI_XFORM_DSTATE_MAX_POS    0
#define KD_MULTI_XFORM_DSTATE_MAX_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_MAX_POS))
#define KD_MULTI_XFORM_DSTATE_MAX_MASK \
                          ((kdu_int32)(2047<<KD_MULTI_XFORM_DSTATE_MAX_POS))
#define KD_MULTI_XFORM_DSTATE_GUARD_POS 11
#define KD_MULTI_XFORM_DSTATE_GUARD_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_GUARD_POS))
#define KD_MULTI_XFORM_DSTATE_RUN_POS   12
#define KD_MULTI_XFORM_DSTATE_RUN_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_RUN_POS))
#define KD_MULTI_XFORM_DSTATE_LLA_POS   13
#define KD_MULTI_XFORM_DSTATE_LLA_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_LLA_POS))
#define KD_MULTI_XFORM_DSTATE_LLA_GUARD_POS   14
#define KD_MULTI_XFORM_DSTATE_LLA_GUARD_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_LLA_GUARD_POS))
#define KD_MULTI_XFORM_DSTATE_T_POS     15
#define KD_MULTI_XFORM_DSTATE_T_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_T_POS))
#define KD_MULTI_XFORM_DSTATE_NUM_POS   16
#define KD_MULTI_XFORM_DSTATE_NUM_BIT \
                          ((kdu_int32)(1<<KD_MULTI_XFORM_DSTATE_NUM_POS))
#define KD_MULTI_XFORM_DSTATE_NUM_MASK \
                       ((kdu_int32)(0xFFFFFFFF<<KD_MULTI_XFORM_DSTATE_NUM_POS))

class kd_multi_queue : public kdu_thread_queue {
  public: // Member functions
    kd_multi_queue()
      { 
        dstate.set(0); save_dstate=0;
        comp = NULL; num_stripes = max_stripe_rows = max_buffer_rows = 0;
        rows_left_in_stripe = next_stripe_row_idx = comp_rows_left = 0;
        stripes_left_in_component = 0; buffer = active_stripe = NULL;
        sync_MDW = NULL; ready_for_pull=false; acc_new_dependencies = 0;
        terminate_asap=have_all_scheduled=ignore_dependency_updates=false;
      }
    virtual ~kd_multi_queue()
      {
        if (pull_ifc.exists()) pull_ifc.destroy();
        if (push_ifc.exists()) push_ifc.destroy();
      }
    void init(kdu_thread_env *env);
      /* This function is called only when the `comp' object (in which we
         are embedded) has been fully initialized.  It is expected that
         `push_ifc' and `pull_ifc' have already been set, and that the
         sample allocator has been finalized, so that `container->buffer' and
         `container->sync_MDW' hold valid pointers.
            Note: if there is a `pull_ifc' present, the initialized object's
         `is_ready_for_pull' member will remain false, so that no calls to
         `pull_ifc.pull' may occur.  Once the `pull_ifc.start' function has
         been called sufficiently to return true, the `ready_for_pull'
         function needs to be invoked.
            Note also that in the case where `num_stripes'=1 and there is only
         one row in the stripe, this function always sets the
         `KD_MULTI_XFORM_DSTATE_LLA_BIT' (last-line-accessed) within the
         `dstate' member and the other functions never change its value.
       */
    virtual bool
      update_dependencies(kdu_int32 new_dependencies,
                          kdu_int32 delta_max_dependencies,
                          kdu_thread_entity *caller);
      /* Overrides the base version of this function to implement the
         logic required to schedule the DWT processing job if required.
         This is the only place where jobs get scheduled by this queue.
         To understand job scheduling, see the extensive comments appearing
         in the notes below this class definition.
         [//]
         Unlike the base implementation, this function manipulates a single
         atomic variable `dstate' that is organized into 3 one-bit flags
         (T, RUN and LLA), a count of the current number of dependencies
         (NUM) and a count of the maximum number of dependencies that could
         occur at any point in the future (MAX).  The base implementation
         keeps only the latter two quantities and stores them in separate
         atomic variables.  The organization of the `dstate' variable is
         governed by macros of the form `KD_MULTI_XFORM_DSTATE_...'.  The
         interpretation of its fields is as follows:
         -- NUM is the D value referred to in the base function's documentation
            (i.e., the number of potentially blocking dependencies formed by
            accumulating the `new_dependencies' arguments encountered in
            calls to this function).  However, positive values of
            `new_dependencies' are temporarily accumulated in the separate
            `acc_new_dependencies' member (non-atomic) and only added back
            into the `dstate' variable after pushing (analysis) or pulling
            (synthesis) a line to/from the DWT analysis/synthesis machinery,
            if the value is found to have increased.
         -- MAX is the M value referred to in the base function's documentation
            (i.e., the maximum number of potentially blocking dependencies
            formed by accumulating the `delta_max_dependencies' arguments
            encountered in calls to this function.
         -- RUN [1 bit] indicates whether or not a DWT processing job is in
            flight.  This flag is used for both multi-threaded (asynchronous)
            and synchronous (`num_stripes'=1) cases to ensure that dependency
            information is not propagated to a parent queue while DWT
            processing is in-flight, since that can garble the sequence of
            update messages received by a parent.  For the asynchronous DWT
            case (`num_stripes' > 1), this bit is set from within the present
            function immediately before scheduling a DWT job and is reset from
            within the relevant DWT job function,
            `kd_multi_job::do_mt_synthesis/analysis'.  For the synchronous
            DWT case (`num_stripes'=1), this bit is set immediately before
            commencing the DWT `push'/`pull' calls and is reset once they
            have completed, at the same point at which the NUM field is
            adjusted to reflect positive dependencies accumulated in
            the non-atomic `acc_new_dependencies' variable.  In the special
            case where the `kdu_thread_queue::dependencies_can_propagate'
            function returns false, however, synchonized manipulation of
            the RUN bit can be avoided in the synchronous DWT case.
         -- LLA [1 bit] is the "last-line-accessed" flag that is used only
            when `num_stripes'=1.  This bit is set when the last available
            line of the stripe is accessed by the MCT machinery.  Specifically,
            during synthesis, the LLA bit is set when the last available line
            of the stripe is read for the first time (it may be read
            multiple times); during analysis, the LLA bit is set when the
            last available line of the stripe is made the current line,
            which actually happens after the previous line has been written.
            These definitions mean that in both cases, if the stripe only has
            one line the LLA bit remains permanently set.  In the case where
            the `kdu_thread_queue::dependencies_can_propagate' function
            returns false, manipulation of this bit is also skipped.
         -- T [1 bit] is set by the `request_termination' function to indicate
            that processing within this object should terminate as soon as
            possible.  The presence of the T bit prevents the scheduling of
            new jobs and causes `all_done' to be invoked when a currently
            running job completes.  The T bit is only of interest for the
            case in which the DWT runs in scheduled jobs (`num_stripes' > 1).
         [//]
         In the following, we discuss what happens when `num_stripes' is 1,
         meaning that the DWT runs synchronously in the same thread as
         multi-component transformation steps.  In this case there is no
         job scheduling here, but the function is responsible for passing
         dependency information along to a parent queue, if any.  The presence
         or absence of a parent queue is established during initialization,
         allowing the function to return immediately, without doing anything
         in the event that there is no parent.  When `num_stripes' is 1,
         the present function is only invoked by descendant queues (block
         encoding/decoding queues); however, the `dstate' member which it
         manipulates is also manipulated by calls to the auxiliary functions
         `lla_set', `dwt_start', `dwt_end' and `dwt_continue'.
         Specifically: `lla_set' is called when the last available line of
         the stripe is accessed by the MCT machinery; `dwt_start' is called
         when the synchronous DWT processing commences; `dwt_continue' is
         called after each line has been pushed/pulled to the DWT processing
         machinery, to determine whether or not the DWT processing should
         finish up due to the appearance of dependencies; and `dwt_end' is
         called once all DWT processing for the stripe buffer has been
         completed.  The `dstate.LLA' bit is reset, if appropriate, within
         the call to `dwt_end' or within a call to `dwt_continue' that
         returns false.  The most important feature of these auxiliary
         functions is that they manage the `dstate.RUN' flag and monitor
         changes in the `dstate' variable between the point at which the
         RUN bit was set by `dwt_start' and the point at which it is reset
         by `dwt_end' or `dwt_continue'.  While the RUN bit is set, no
         dependency information is propagated to a parent queue via calls
         to the present function.  When the RUN bit is reset within
         `dwt_end' or `dwt_continue', any dependency changes that have
         occurred are passed along via calls to `propagate_dependencies'.
         These steps ensure that dependency changes are only computed and
         propagated from points at which we can be sure that all dependency
         increments (positive changes in the number of dependencies) have
         been accounted for.  Otherwise, it can happen that the number of
         dependencies appears to go from, say 1 to 0, when in fact it has
         gone from 2 to 1 but the dependency increment that logically
         preceded the dependency decrements has not yet been processed due
         to delays experienced by its thread of execution.
            Subject to the above restrictions on the point at which
         dependencies can be propagated, the `propagate_dependencies' method
         is invoked with a `new_dependencies' value of 1, whenever the
         `dstate' variable transitions to a state in which the LLA bit is
         present and the NUM field identifies a positive number of
         dependencies upon descendant queues (`kdu_encoder' or `kdu_decoder'
         objects).  Similarly, `propagate_dependencies' is invoked with a
         `new_dependencies' value of -1 when the `dstate' variable transitions
         to a state in which these conditions do not both hold.
            In single-threaded DWT mode, a parent queue becomes free from
         the possibility of ever encountering blocking conditions in the
         future only once this is true for all descendant queues (`kdu_encoder'
         or `kdu_decoder' objects).  Accordingly, once the MAX field of
         the `dstate' variable transitions to 0, we invoke
         `propagate_dependencies' with a `delta_max_dependencies' argument of
         -1.  Again, though, the call to `propagate_dependencies' is delayed,
         if necessary, until the RUN bit is 0.  This ensures that a parent
         queue will never encounter the condition where there appear to be
         outstanding dependencies at the point when it is informed that there
         will not be any such dependencies in the future.
            Increments in the MAX field occur only during initialization of
         the descendant objects.  If the value becomes non-zero during
         this phase, the `propagate_dependencies' function is invoked with a
         `delta_max_dependencies' argument of 1.
      */
    void lla_set(kdu_thread_env *caller)
      { 
        if (ignore_dependency_updates) return;
        kdu_int32 old_state=dstate.exchange_add(KD_MULTI_XFORM_DSTATE_LLA_BIT);
        kdu_int32 new_state=old_state+KD_MULTI_XFORM_DSTATE_LLA_BIT;
        assert(!(new_state & KD_MULTI_XFORM_DSTATE_LLA_GUARD_BIT));
        if ((new_state & KD_MULTI_XFORM_DSTATE_NUM_MASK) == 0)
          propagate_dependencies(1,0,caller);
      }
      /* This function is called only in the synchronous DWT mode
         (i.e., when `num_stripes'=1). */
    void dwt_start()
      { 
        if (ignore_dependency_updates) return;
        save_dstate = dstate.exchange_add(KD_MULTI_XFORM_DSTATE_RUN_BIT);
        assert(!(save_dstate & KD_MULTI_XFORM_DSTATE_RUN_BIT));
      }
      /* This function is called immediately before performing synchronous
         DWT operations; it sets the `dstate.RUN' bit and records the previous
         state of the `dstate' member for later reference. */
    bool dwt_continue(kdu_thread_env *caller, bool leave_lla_set)
      { 
        if (acc_new_dependencies == 0) return true;
        assert(!ignore_dependency_updates);
        kdu_int32 old_state, new_state, end_mask, delta_state =
          (acc_new_dependencies << KD_MULTI_XFORM_DSTATE_NUM_POS);
        if (leave_lla_set)
          end_mask = ~(KD_MULTI_XFORM_DSTATE_RUN_BIT);
        else
          end_mask = ~(KD_MULTI_XFORM_DSTATE_RUN_BIT |
                       KD_MULTI_XFORM_DSTATE_LLA_BIT);
        do { // Enter compare-and-set loop
          old_state = dstate.get();
          new_state = (old_state | KD_MULTI_XFORM_DSTATE_LLA_BIT) + delta_state;
          if (new_state & KD_MULTI_XFORM_DSTATE_NUM_MASK)
            new_state &= end_mask; // Dependencies exist; halt the DWT for now
        } while (!dstate.compare_and_set(old_state,new_state));
        acc_new_dependencies = 0; // We have transferred these to `dstate'
        if (new_state & KD_MULTI_XFORM_DSTATE_RUN_BIT)
          return true;
        sync_dwt_propagate_dependencies(save_dstate,new_state,caller);
        return false;
      }
      /* This function is called each line has been pushed to or pulled from
         the DWT processing machinery in the synchronous DWT mode
         (`num_stripes'=1), unless there are no more stripe lines available
         for processing.  The function returns true if processing can continue.
         The `leave_lla_set' argument should be set to true if exactly one
         line has been pushed to or pulled from the DWT processing machinery
         since `dwt_start' was last called, in which case, the LLA condition
         that must have existed prior to DWT processing must remain if this
         function turns out to return false.  This argument ensures that the
         LLA bit remains set, if appropriate, so that the next call to
         `kd_multi_component::advance_stripe_line' need not manipulate it --
         this saves unnecessary manipulation of the synchronized `dstate'
         variable. */
    void dwt_end(kdu_thread_env *caller, bool leave_lla_set)
      { 
        if (ignore_dependency_updates) return;
        kdu_int32 old_state, new_state, end_mask, delta_state =
          (acc_new_dependencies << KD_MULTI_XFORM_DSTATE_NUM_POS);
        if (leave_lla_set)
          end_mask = ~(KD_MULTI_XFORM_DSTATE_RUN_BIT);
        else
          end_mask = ~(KD_MULTI_XFORM_DSTATE_RUN_BIT |
                       KD_MULTI_XFORM_DSTATE_LLA_BIT);
        do { // Enter compare-and-set loop
          old_state = dstate.get();
          new_state = (old_state | KD_MULTI_XFORM_DSTATE_LLA_BIT) + delta_state;
          new_state &= end_mask;
        } while (!dstate.compare_and_set(old_state,new_state));
        acc_new_dependencies = 0; // We have transferred these to `dstate'
        sync_dwt_propagate_dependencies(save_dstate,new_state,caller);
      }
      /* This function is called in place of `dwt_continue' if the last line
         of a stripe has just been pushed to or pulled from the DWT
         machinery.  It does the same things as a call to `dwt_continue'
         that returns false. */
    void sync_dwt_propagate_dependencies(kdu_int32 old_dstate,
                                         kdu_int32 new_dstate,
                                         kdu_thread_entity *caller);
      /* This function issues all relevant calls to `propagate_dependencies'
         associated with a change in the `dstate' member from `old_dstate' to
         `new_dstate', when the synchronous DWT mode is being used (i.e., when
         `num_stripes'=1).  From `update_dependencies', the function is called
         if the RUN bit is zero.  `dwt_continue' and `dwt_end' also call the
         function if the RUN bit becomes zero, passing in the `dstate' member
         immediately prior to the call to `dwt_start' as the `old_dstate' value
         and the new value of `dstate' immediately after the RUN bit became
         zero as the `new_dstate' value.  In this way, calls to this function
         cover all changes in `dstate', except that changes which occurred
         while the RUN bit was set are aggregated into a single call. */
    void pass_on_dependencies(int new_dependencies, int delta_max_dependencies,
                              kdu_thread_entity *caller)
      { this->propagate_dependencies(new_dependencies,delta_max_dependencies,
                                     caller); }
      /* Exists only to make the protected base function
         `kdu_thread_queue::propagate_dependencies' available to
         `kd_multi_component'. */
    void set_ready_for_pull(kdu_thread_env *env)
      { 
        if (ready_for_pull || !pull_ifc.exists()) return;
        ready_for_pull = true;
        if ((env != NULL) && (num_stripes > 1))
          update_dependencies(-1,0,env);
      }
      /* This function should be invoked after the `pull_ifc' has had its
         `start' function invoked to the point where it returns true.  Until
         this function is called, no calls to `kdu_pull_ifc::pull' will be
         delivered, synchronously or otherwise. */
  protected: // Configuration functions
    virtual int get_max_jobs();
      /* Returns 1 if DWT is performed asynchronously
         (i.e., if `comp->num_stripes' > 1).  Otherwise, returns 0. */
    virtual void request_termination(kdu_thread_entity *caller);
      /* This function takes note of the termination request and uses it
         to ensure that the `all_done' function can be called as soon as
         possible.  The function does not really need to do anything if
         `num_stripes' = 1, because in this case the queue does not
         schedule its own jobs. */
  private: // Data
    friend class kd_multi_job;
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep1)
    kdu_interlocked_int32 dstate; // See below
    kdu_int32 save_dstate; // Saves state between `dwt_start' and `dwt_end'
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep2)
  public: // Just these interfaces are public; they must be initialized
          // manually before `init' is invoked and before the object is
          // passed to `kdu_thread_entity::add_queue'.
    kd_multi_job job; // Essentially just a handle to the queue itself
    kd_multi_component *comp; // Points to our own container
    kdu_push_ifc push_ifc; // Used for analysis
    kdu_pull_ifc pull_ifc; // Used for synthesis
    bool ready_for_pull; // Must be true before `pull_ifc.pull' is called
  public: // The following member is made public only for synchronous DWT usage
    int comp_rows_left; // See below
  private: // Back to private data; these are used only if `num_stripes' > 1
    int num_stripes; // Number of stripes in the `buffer'
    int max_stripe_rows; // Separates stripes in the `buffer'
    int max_buffer_rows; // Product of above two quantities
    int rows_left_in_stripe; // Number of rows left to process
    int next_stripe_row_idx; // Next line to process in active stripe
    int stripes_left_in_component; // See below
    kdu_line_buf *active_stripe; // Points to start of active stripe in buffer
    kdu_line_buf *buffer; // `num_stripes'*`max_stripe_rows' line bufs
    kdu_interlocked_int32 *sync_MDW; // See below
    int acc_new_dependencies; // See below
    bool ignore_dependency_updates; // If synchronous DWT and no parent queue
    bool terminate_asap; // Allows faster response to `request_termination'
    bool have_all_scheduled; // See below
  };
  /* Notes:
        The members of this object are manipulated only by threads which
     actually perform DWT analysis/synthesis processing, although members of
     the base `kdu_thread_queue' object might be manipulated by other
     threads (especially block encoding/decoding threads that report
     dependency changes), which is why we separate the base from the
     extended object's variables via an L2 cache line of empty space.  Most
     of the members of this object are not used if `num_stripes' is 1,
     because their values would change synchronously with their namesakes
     within the `kd_multi_component' object.
        The `buffer' member points to an array with
     `num_stripes'*`max_stripe_rows' `kdu_line_buf' objects.  The array is
     actually embedded within a larger block of memory that is allocated
     from `kdu_sample_allocator' memory in a fancy way.  In practice, the
     value of `num_stripes' is set to 1 if and only if DWT processing is
     to be carried out synchronously, without scheduling jobs.  Values > 1
     (e.g., 2 in the simple case of double buffering) are used if DWT
     processing is to be carried out asynchronously via scheduled jobs.
        The `active_stripe' member points either to `buffer' or
     `buffer'+`max_stripe_rows', depending on which stripe is currently
     active for processing.
        The `rows_left_in_stripe' member indicates the number of rows that
     belong to the current `active_stripe', but have not yet been
     processed, while `next_stripe_row_idx' indicates the next row to be
     processed (synthesized or analyzed) within the active
     stripe.  The sum of these two quantities is always the total number
     of valid rows in the current `active_stripe'.
        The `comp_rows_left' member holds the number of image component rows
     that remain, starting from the first row of the `active_stripe'.  When
     the `active_stripe' has been fully processed, this value is decreased
     by `next_stripe_row_idx', which necessarily holds the total number of
     rows in the active stripe that was just processed.
        The `stripes_left_in_component' member holds the number of stripes
     that have yet to be fully processed, including the active stripe.  When
     the active stripe has been fully processed, this value is decreased
     by 1.
        The `dstate' member is a local replacement for the base
     object's `dependency_count' and `max_dependency_count' variables, but
     with extra fields to keep track of the state of running jobs and
     termination requests in a synchronous fashion.  The organization of
     the `dstate' variable into NUM, MAX, RUN, LLA and T fields is
     explained in the comments that follow the `update_dependencies' function,
     which also provide a thorough description of how it is used in the
     case where `num_stripes' = 1.  In what follows, we explain what happens
     for the case where `num_stripes' > 1, where DWT processing is performed
     by scheduled jobs.
        The `acc_new_dependencies' member accumulates all positive
     `new_dependencies' values passed to the `update_dependencies'
     function.  Negative values are accumulated immediately within
     the `dstate.NUM' field.  The positive values can arise only from within
     calls to `kdu_push_ifc::push' or `kdu_pull_ifc::pull'.  Each time such a
     push/pull call completes (i.e., after a new line of the image component
     has been synthesized or analyzed), the DWT processing job
     (`kd_multi_job::do_mt_analysis/synthesis') atomically adds the contents
     of `acc_new_dependencies' to the `dstate.NUM' bit field, resetting
     `acc_new_dependencies' to 0 as it does so.  In this way, it is able to
     determine whether or not potentially blocking conditions have arisen
     that might affect a subsequent push/pull call; if so, the
     job finishes and will be rescheduled automatically once the outstanding
     dependencies have been removed by subband encoding/decoding operations.
     Similar operations are performed for the case of `num_stripes'=1 where
     the DWT is done synchronously, rather than by multi-threaded processing
     jobs.
        The `have_all_scheduled' flag is set to true when a call to
     `schedule_job' passes true for the `all_scheduled' argument, or when
     a multi-threaded analysis or synthesis job function itself invokes
     the `all_scheduled' function.  The flag prevents multiple declarations
     of the all-scheduled condition and, more importantly, prevents the
     `do_mt_analysis' or `do_mt_synthesis' job functions from returning
     until all processing is complete -- if they return they will never be
     invoked again because no more jobs can be scheduled.
        The `sync_MDW' member points to a shared interlocked variable
     that actually resides within storage allocated by the
     `kdu_sample_allocator' within its own `KDU_L2_MAX_CACHE_LINE'-aligned
     block of memory.  By "shared" we mean that in multi-stripe
     applications, it is accessed both by DWT processing threads and by
     multi-component processing threads (of course, these might sometimes be
     the same thread); the pointer will be NULL if `num_stripes' < 2.
        This variable is best understood as a set of non-negative quantities
     (or bit-fields), denoted M, D and W.  M and D are guaranteed to lie in
     the range 0 to `num_stripes', while W is a 1-bit flag.  The bit-field
     locations and masks are defined by the macros `KD_MULTI_XFORM_SYNC_...'
     The semantic interpretation of the three bit-fields is as follows:
     -- M holds the number of stripes that are available to the MCT machinery
        and contain one or more individual lines which have not yet been read
        or written by the MCT machinery.
     -- D holds the number of stripes that are available to the DWT machinery
        and contain one or more individual lines which have not yet been read
        or written by the DWT machinery.
     -- W is a 1-bit flag that is set when the MCT machinery needs access to
        a stripe, but M=0 meaning none is available.  W should never be set
        unless M=0 -- changes are performed atomically using compare-and-set
        loops.
     The following is a brief synopsis of how M, D and W are updated.
     * During ANALYSIS:
        -- (M,D,W) is initialized to (`num_stripes',0,0).
        -- Whenever the MCT finishes generating a full stripe of image
           component rows, M is decremented and D is incremented.  If this
           leaves M=0, the `comp->line.line' member is reset (using
           `kdu_line_buf::destroy') so that the next time the MCT transform
           machinery requires access to a line from this image component,
           `comp->get_first_line_of_stripe' must be called.  That function
           deposits a reference to the relevant thread's current
           `kdu_thread_entity_condition' object in `comp->wakeup' and then
           enters a compare-and-set loop which sets the W flag to 1 if
           M is still 0, waiting if it does so, for the deposited condition
           object to be signalled -- `kdu_thread_entity::wait_for_condition'
           is used to realize the wait.
        -- Whenever the DWT analysis machinery finishes processing a stripe,
           D is decremented and M is incremented, simultaneously testing 
           and resetting W to 0.  If this causes M to transition from 0 to 1
           and W was 1, any `comp->wakeup' condition is signalled.
     * During SYNTHESIS:
        -- (M,D,W) is initialized to (0,`num_stripes',0).
        -- Whenever the MCT machinery causes `comp->rows_left_in_stripe' to
           become 0, M is decremented, without touching D.  This is done to
           reflect the fact that the next attempt to retrieve a line from
           this image component may potentially block the caller.
        -- When the MCT machinery finishes working with a stripe (this
           happens on the next line transition after the one that rendered
           `comp->rows_left_in_stripe' 0) the value of D is incremented
           without touching M.
        -- When the MCT machinery needs to access the first line of a stripe,
           if it encounters the M=0 condition, a compare-and-set loop is used
           to set W=1 (so long as M=0 persists) after first setting
           `comp->wakeup' to reference the thread's current
           `kdu_thread_entity_condition' object; the thread then waits inside
           `kdu_thread_entity::wait_for_condition' for the condition to be
           signalled.
        -- Whenever the DWT synthesis machinery finishes processing a stripe,
           D is decremented and M is incremented.  At the same time, the
           W bit is tested and reset.  If M transitions from 0 to 1 and
           the W flag was set, any `comp->wakeup' condition is signalled.
     * In BOTH CASES:
        -- Whenever D transitions to 0, the DWT processing job finishes, being
           careful to add 2^16 to `dependency_state' as it does so, which means
           that a new DWT job cannot be scheduled until the blocking
           dependency created by D=0 is cleared.
        -- Whenever D transitions from 0, `dependency_state' is decremented
           by (2^16) through a call to `update_dependencies'; this may result
           in the DWT job being rescheduled.
        -- Whenever M transitions to 0, any parent queue's
           `update_dependencies' function is invoked with a `new_dependencies'
           value of 1 to reflect the fact that M=0 presents a potentially
           blocking dependency for users of the `kdu_multi_analysis' or
           `kdu_multi_synthesis' object.
        -- Whenever M transitions from 0, any parent queue's
           `update_dependencies' function is invoked with a `new_dependencies'
           value of -1, meaning that the current image component no longer
           presents a potentially blocking dependency for users of the
           `kdu_multi_analysis' or `kdu_multi_synthesis' object.
  */

/*****************************************************************************/
/*                            kd_multi_component                             */
/*****************************************************************************/

class kd_multi_component {
  public: // Member functions
    kd_multi_component()
      {
        comp_idx=0; num_stripes = max_stripe_rows = max_buffer_rows = 0;
        rows_left_in_stripe = rows_left_in_component = next_stripe_row_idx = 0;
        buffer = active_stripe = NULL; sync_MDW = NULL;
        wakeup=NULL; buffer_base=sync_base=0; tmp_buffer=NULL;
      }
    ~kd_multi_component()
      { 
        if (tmp_buffer != NULL) delete[] tmp_buffer;
      }
    void advance_stripe_line(kdu_thread_env *env, bool lla_already_set)
      { 
        line.line = active_stripe[next_stripe_row_idx++];
        if (next_stripe_row_idx == max_stripe_rows)
          next_stripe_row_idx = 0;
        rows_left_in_component--;  rows_left_in_stripe--;
        if ((rows_left_in_stripe > 0) || (rows_left_in_component == 0)) return;
        assert(rows_left_in_stripe == 0);
        if (num_stripes == 1)
          { if (!lla_already_set) queue.lla_set(env); }
        else if (queue.pull_ifc.exists())
          reached_last_line_of_multi_stripe(env);
      }
      /* During multi-component analysis, this function is invoked after
         writing a stripe line.  During multi-component synthesis, this
         function is invoked before reading a stripe line.  In the case of
         synchronous DWT processing (`num_stripes'=1), the function invokes
         `queue.lla_set' if `rows_left_in_stripe' becomes 0 and there is at
         least one line left in the image component and `lla_already_set' is
         false.  In the case of asynchronous DWT synthesis processing
         (`num_stripes' > 1 and `queue.pull_ifc' exists), the
         `reached_last_line_of_multi_stripe' function is called if
         `rows_left_in_stripe' becomes 0 and there is at least one line
         left in the image component. */
    void new_stripe_ready_for_analysis(kdu_thread_env *env);
      /* This function gets called when we find that `rows_left_in_stripe'
         is 0 during multi-component analysis transformation.  This
         means that a new stripe is now ready for DWT analysis.  This is
         where we increment D and decrement M, following the policies
         outlined in the notes below `kd_multi_queue'.  If M is left equal
         to 0, `line.line.destroy()' is called so that the
         `get_first_line_of_stripe' function must be invoked next time the
         image component's `line' buffer is required.  Otherwise, this
         function also arranges for `line.line' to reference the first line
         of the new stripe and adjusts `rows_left_in_stripe' and
         `next_stripe_row_idx' accordingly. */
    void get_first_line_of_stripe(kdu_thread_env *env);
      /* This function handles the case in which `line.line' is found to
         hold no valid image component line buffer when the line is actually
         required.  This is true when the object is first used and then
         later when M goes to 0 inside `new_stripe_ready_for_analysis'.
         This function might block the caller if M is still 0.  The function
         is used only during multi-component analysis transformation. */
    void reached_last_line_of_multi_stripe(kdu_thread_env *env);
      /* Handles the case in which the above function was called with
         `num_stripes' > 1.  This is where we decrement M, following the
         policies outlined in the notes below `kd_multi_queue'. */
    void get_new_synthesized_stripe(kdu_thread_env *env);
      /* This function gets called when we need to advance the `line'
         member to reference the next image component row, but find that
         `rows_left_in_stripe' is 0.  This is where we increment D, following
         the policies outlined in the notes below `kdu_multi_queue'.  This
         function might block the caller if M also happens to be 0.  The
         function returns with `line.line' set to the first line of the new
         active stripe and `rows_left_in_stripe' and `next_stripe_row_idx'
         adjusted accordingly.  The function is used only during
         multi-component synthesis transformation. */
  public: // Data
    int comp_idx;
    kd_multi_line line;
    int num_stripes; // Number of stripes in the `buffer'
    int max_stripe_rows; // Separates stripes in the `buffer'
    int max_buffer_rows; // Product of above two quantities
    int rows_left_in_stripe; // Number of rows left in active stripe
    int next_stripe_row_idx; // Next line to process in active stripe
    int rows_left_in_component; // See below
    kdu_line_buf *active_stripe; // Points to start of active stripe in buffer
    kdu_line_buf *buffer; // `num_stripes'*`max_stripe_rows' line bufs
    kdu_interlocked_int32 *sync_MDW; // Points to shared state variable
    kdu_thread_entity_condition *wakeup; // See notes below `kd_multi_queue'
    size_t buffer_base, sync_base; // For use with `kdu_sample_allocator'
    kdu_line_buf *tmp_buffer; // See below
    kdu_byte _sep1[KDU_MAX_L2_CACHE_LINE-2*sizeof(size_t)-sizeof(void *)];
    kd_multi_queue queue;
    kdu_byte _trailer[KDU_MAX_L2_CACHE_LINE];
  };
  /* Notes:
        This object is used to form the `codestream_components' array
     in `kd_multi_transform'.  It manages all DWT analysis/synthesis
     operations, as required, together with intermediate buffering for
     image component lines prior to analysis or following synthesis.
     In multi-threaded application, DWT analysis/synthesis processing
     proceeds entirely within the space spanned by the `queue' member,
     while the multi-component processing machinery works with the
     remaining member variables, often in a different thread; for this
     reason, these two sets of working variables are separated sufficiently
     to ensure that the fall within different L2 cache lines to avoid
     false sharing bottlenecks on multi-processing platforms.
        Most of the member variables are identical to their namesakes within
     `kd_multi_queue' and this is deliberate.  The main difference is that
     the state variables that appear here refer to the state of the
     multi-component processing machinery while those that appear within
     `kd_multi_queue' refer to the state of the DWT processing machinery.
     During analysis, the `active_stripe' here advances ahead of that
     in `kdu_multi_queue', while the reverse is the case during synthesis.
        The `tmp_buffer', `buffer_base' and `sync_base' members deserve
     further explanation.  During the initialization phase, `tmp_buffer'
     is allocated from the heap to hold `num_stripes'*`max_stripe_rows'
     `kdu_stripe_buffer' objects.  The `kdu_sample_allocator' object
     is then used to pre-allocate all the necessary line buffers into this
     `tmp_buffer' array's members; it is also used to reserve an
     L2-aligned block of memory large enough to hold the final `buffers'
     array and another L2-aligned block of memory large enough to hold the
     interlocked variable that will eventually be referenced by
     `sync_MDW' (although this is not required if `num_stripes' is 1).
     Following pre-allocation, once the `kdu_sample_allocator' object is
     finalized, the `buffers' and `sync_MDW' pointers can be initialized
     to point into the reserved memory locations and the `tmp_buffers'
     contents are copied to the aligned block of memory associated with
     `buffers', whereupon the `tmp_buffers' array is deleted.
        `comp_idx' is the index of the corresponding codestream image
     component, as required by the `kdu_tile::access_component' function.
     This is not necessarily the ordinal position of the present structure
     within the `kd_multi_transform::codestream_components' array, since
     elements in the array follow the same order as the elements of the
     `kd_multi_collection::components' array in the object referenced by
     `kd_multi_transform::codestream_collection'.
        `line' is used to maintain the state of the current line being
     referenced from the `kd_multi_transform::codestream_collection' object.
     Its internal `line.line' member actually points to a line buffer
     within the `active_stripe' -- `next_stripe_row_idx' identifies the
     line that will next be used to initialize `line.line'.
        `rows_left_in_stripe' indicates the number of available rows within
     the current `active_stripe', not including the one referenced by
     `line.line'.  It follows, that if the stripe has only one row, this
     member will always be 0 in practice.
        `rows_left_in_component' is the total number of rows left in the
     current image component, not including the line referenced by `line.line'.
     This value is decremented each time `rows_left_in_stripe' decreases
     and `next_stripe_row_idx' advances.
        In the synchronous DWT mode (i.e., when `num_stripes'=1), the
     `next_stripe_row_idx' member is interpreted as a pointer into a circular
     buffer, so that the `rows_left_in_stripe' value need not be the difference
     between the stripe height and the value of `next_stripe_row_idx'.
  */

/*****************************************************************************/
/*                            kd_multi_transform                             */
/*****************************************************************************/

class kd_multi_transform {
  /* Common base for `kd_multi_analysis' and `kd_multi_synthesis'. */
  public: // Member functions
    kd_multi_transform()
      {
        use_ycc=false; xform_blocks=block_tail=NULL;
        codestream_collection=output_collection=NULL;
        constant_output_lines=NULL; codestream_components = NULL;
        allocator = NULL;  max_scratch_ints = max_scratch_floats = 0;
        scratch_ints=NULL;  scratch_floats=NULL;
      }
    virtual ~kd_multi_transform();
    int *get_scratch_ints(int num_elts);
    float *get_scratch_floats(int num_elts);
  protected: // Utility functions
    void construct(kdu_codestream codestream, kdu_tile tile,
                   kdu_thread_env *env, kdu_thread_queue *env_queue,
                   int flags, int buffer_rows, bool for_synthesis);
      /* Does almost all the work of `kd_multi_analysis::create' and
         `kd_multi_synthesis::create'.  In the former case the function is
         called with `for_synthesis'=false, while in the latter case
         `for_synthesis'=true.  That argument is used only to
         pre-configure platform-specific optimization functions that might
         be specific to synthesis or analysis. */
    void create_resources(kdu_codestream codestream, kdu_thread_env *env);
      /* Called from within `kd_multi_analysis::create' or
         `kd_multi_synthesis::create' after first calling `construct' and
         then creating all `kdu_push_ifc'- and `kdu_pull_ifc'-derived engines.
         This function pre-creates all the `kdu_line_buf' resources, then
         finalizes the `allocator' object and finalizes the creation
         of all `kdu_line_buf' resources.  The function also initializes
         the contents of all lines which have no generator. */
  private: // Helper functions for `construct'
    bool propagate_knowledge(bool force_precise,
                             bool force_hanging_constants_to_reversible);
      /* This function is called from `construct' once all transform blocks
         and lines have been constructed.  At this point, the bit-depth of
         each codestream component line and each output component line should
         be known; all other lines should have a 0 bit-depth.  Also, at this
         point, the `reversible', `need_irreversible' and `need_precise'
         flags might have been set only for some lines -- typically
         codestream component lines, or lines produced by transform blocks
         with known requirements.  Finally, dimensions should be known only
         for output image components and codestream image components.  Each
         time this function is called, it passes through the network,
         propagating the available knowledge wherever possible.  If nothing
         changes, the function returns false and does not need to be called
         again.  Otherwise, it must be invoked iteratively.
           The final argument should initially be false.  However, this may
         lead to some constant lines remaining in an indeterminate state
         with regard to reversibility.  A second iterative invokation of
         the function with `force_hanging_constants_to_reversible' will
         then fix the problem. */
  protected: // Data
    bool use_ycc;
    kd_multi_block *xform_blocks;
    kd_multi_block *block_tail; // Tail of `xform_blocks' list
    kd_multi_component *codestream_components;
    kd_multi_collection *codestream_collection; // Linked list of collections
    kd_multi_collection *output_collection;
    kd_multi_line *constant_output_lines;
    kdu_sample_allocator *allocator; // May be internal or external
    kdu_sample_allocator internal_allocator;
  private:
    int max_scratch_ints;
    int *scratch_ints;
    int max_scratch_floats;
    float *scratch_floats;
  };

/*****************************************************************************/
/*                            kd_multi_synthesis                             */
/*****************************************************************************/

class kd_multi_synthesis : public kd_multi_synthesis_base,
                           public kd_multi_transform {
  public: // Member functions
    kd_multi_synthesis() { output_row_counters=NULL; }
    virtual ~kd_multi_synthesis();
    virtual void terminate_queues(kdu_thread_env *env);
    kdu_long create(kdu_codestream codestream, kdu_tile tile,
                    kdu_thread_env *env, kdu_thread_queue *env_queue,
                    int flags, int buffer_rows,
                    kdu_sample_allocator *external_allocator);
    virtual bool start(kdu_thread_env *env);
    virtual kdu_coords get_size(int comp_idx);
    virtual kdu_line_buf *get_line(int comp_idx, kdu_thread_env *env);
    virtual bool is_line_precise(int comp_idx);
    virtual bool is_line_absolute(int comp_idx);
  private: // Helper functions
    kdu_line_buf *get_line(kd_multi_line *line, int tgt_row_idx,
                           kdu_thread_env *env);
      /* Recursive function which does all the work of the public `get_line'
         function. */
  private: // Data
    int *output_row_counters;
    bool fully_started;
  };

/*****************************************************************************/
/*                             kd_multi_analysis                             */
/*****************************************************************************/

class kd_multi_analysis : public kd_multi_analysis_base,
                          public kd_multi_transform {
  public: // Member functions
    kd_multi_analysis() { source_row_counters=NULL; }
    virtual ~kd_multi_analysis();
    virtual void terminate_queues(kdu_thread_env *env);
    kdu_long create(kdu_codestream codestream, kdu_tile tile,
                    kdu_thread_env *env, kdu_thread_queue *env_queue,
                    int flags, kdu_roi_image *roi, int buffer_rows,
                    kdu_sample_allocator *external_allocator);
    virtual kdu_coords get_size(int comp_idx);
    virtual kdu_line_buf *
      exchange_line(int comp_idx, kdu_line_buf *written, kdu_thread_env *env);
    virtual bool is_line_precise(int comp_idx);
    virtual bool is_line_absolute(int comp_idx);
  private: // Helper functions
    void prepare_network_for_inversion();
      /* This function determines whether or not the set of transform blocks
         is sufficient to produce all codestream components.  If
         not, an error will be generated.  Where there are multiple ways to
         generate a given codestream component, no decision is taken here
         regarding which method will be used -- the outcome in this case
         may depend on the order in which the transform blocks are inverted,
         which depends on the order in which the application pushes image
         data into the `kdu_multi_analysis' interface. */
    void advance_line(kd_multi_line *line, int new_row_idx,
                      kdu_thread_env *env);
      /* This function is the workhorse for the public `exchange_line'
         function.  The function should be called only after writing new
         contents to the `line->line' buffer.  The present function
         increments `line->row_idx' to reflect the location of the
         row which has just been written -- the first time the function
         is called, `row_idx' will be incremented from -1 to 0.  This
         new value should be identical to the `new_row_idx' argument.
         If possible, the function runs the `perform_inverse' function of
         any transform block which owns the line; for codestream component
         lines, the function pushes the line directly to the relevant
         component's processing engine.  If any of these functions succeed,
         the `waiting_for_inversion' member is set to false.  Otherwise, it
         is set to true.
            Whenever a transform block can be inverted, the function
         recursively invokes itself on each of its dependencies.  Note also
         that where a dependency has multiple consumers, if the line is found
         to have already been written by another consumer, the relevant
         `kd_multi_block::dependencies' entry is set to NULL here, before
         invoking `kd_multi_block::perform_inverse'.  This means that the
         latter function is always free to write directly to its dependency
         line buffers, whenever it is invoked.  It also means that the
         present function will never be invoked on a line which reports
         having more than one consumer. */
  private: // Data
    int *source_row_counters;
  };

} // namespace kd_core_local

#endif // MULTI_TRANSFORM_LOCAL_H
