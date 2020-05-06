/*****************************************************************************/
// File: kdu_region_decompressor.h [scope = APPS/SUPPORT]
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
   Defines incremental, robust, region-based decompression services through
the `kdu_region_decompressor' object.  These services should prove useful
to many interactive applications which require JPEG2000 rendering capabilities.
******************************************************************************/

#ifndef KDU_REGION_DECOMPRESSOR_H
#define KDU_REGION_DECOMPRESSOR_H

#include "kdu_compressed.h"
#include "kdu_sample_processing.h"
#include "jp2.h"

// Objects declared here
namespace kdu_supp {
  struct kdu_channel_interp;
  struct kdu_channel_mapping;
  class kdu_region_decompressor;
}

// Objects declared elsewhere
namespace kd_supp_local {
  struct kdrd_tile_bank;
  struct kdrd_channel;
  struct kdrd_component;
  struct kdrd_channel_buf;
}

namespace kdu_supp {
  using namespace kdu_core;
  
/*****************************************************************************/
/*                             kdu_channel_interp                            */
/*****************************************************************************/
  
struct kdu_channel_interp {
  /* [SYNOPSIS]
       This structure describes the precise interpretation of the sample
       values recovered after decompressing codestream content and, if
       appropriate, after passing it through a palette lookup table.  From
       the perspective of the JPEG2000 standard(s) the values recovered after
       decompression or by palette lookup are signed or unsigned integers with
       a given bit-depth P.  The `orig_prec' member holds the value of P,
       while `orig_signed' indicates whether the original integers were
       considered to be P-bit signed values or P-bit unsigned values.
       [//]
       All of Kakadu's processing machinery works with level adjusted numerics
       that are either absolute or relative.  More precisely, the values
       recovered by Kakadu's decompression machinery are either signed integers
       in the range -2^{P-1} to 2^{P-1}-1, or else they are floating point or
       fixed point quantities that can be interpreted as real values in the
       range -0.5 to +0.5.  After dividing the absolute representations by 2^P,
       all quantities can be regarded as real-valued numbers x, with a nominal
       range of -0.5 to +0.5 that has not necessarily been hard limited.  This
       is the "normalized" representation that is described by this structure.
       [//]
       Considering first the simple case of P-bit unsigned integers (from
       the JPEG2000 perspective), in the level adjusted and normalized
       representation described above, the true maximum intensity actually
       corresponds to x=0.5-2^{-P}, while the true zero corresponds to x=-0.5.
       These are the values of `normalized_max' and `normalized_zero',
       respectively.
       [//]
       Where the JPEG2000 structures identify P-bit signed integers, the
       corresponding values for `normalized_max' and `normalized_zero' become
       0.5-2^{-P} and 0.0, respectively.  There are two ways to map signed data
       to rendered B-bit outputs.  One is to treat them as though they had been
       unsigned, so that the zero value maps to 2^{B-1}.  The second
       way is to map the range from 0 to 0.5-2^{-P} to the full output range
       from 0 to 2^B-1, truncating negative values.  This second method is
       the one known as "true zero" in the documentation accompanying the
       `kdu_region_decompressor::set_true_scaling' function.  Both approaches
       can be useful.
       [//]
       As explained with `kdu_region_decompressor::set_true_scaling', for some
       colour spaces the natural zero point for a B-bit unsigned output value
       is 2^B * `zeta' where the `zeta' might not be 0.  In particular, for the
       chrominance components of opponent colour spaces, the `zeta' member
       will usually be 0.5 (but 0.375 is the default value for the third
       channel of a CIELab colour space).  Even the luminance channel of some
       opponent colour spaces has a non-zero `zeta' value; for example, the
       natural zero point of the luminance channel in the `JP2_YCbCr1_SPACE'
       is 16 for 8-bit unsigned representations, meaning that `zeta'=16/256.
       [//]
       The `zeta' value can have an effect on the way channel values are mapped
       from an input to an output representation, whenever either or both of
       the representations is unsigned.  It's effect on unsigned to unsigned
       mappings, however, is very subtle and is actually non-existent if the
       "default scaling" rather than "true scaling" mode is in force.  See
       `kdu_region_decompressor::set_true_scaling' for more on these modes
       and how `zeta' is used in the different scaling modes.
       [//]
       The `normalized_zero' member itself does not include the effect of
       `zeta'; instead, the `normalized_natural_zero' member is provided
       for this purpose.  So, for example, in a YCbCr opponent space, with
       unsigned 8-bit inputs, the `normalized_max' value for all channels
       would be 0.5-1/256, the `normalized_zero' value for all channels would
       be -0.5, but the `normalized_natural_zero' value would be -0.5 for the
       Y channel (with `zeta'=0) and 0.0 for the Cb and Cr channels (with
       `zeta'=0.5).
       [//]
       IS15444-2/AMD3 introduces the Pixel Format box which provides
       alternate ways to interpret the sample values recovered by decompression
       and (potentially) palette lookup.  This structure currently accommodates
       two of these alternate interpretations, corresponding to the
       `JP2_CHANNEL_FORMAT_FLOAT' and `JP2_CHANNEL_FORMAT_FIXPOINT' formats
       that are described in connection with the `jp2_channels' interface.
       [//]
       A `JP2_CHANNEL_FORMAT_FLOAT' representation involves re-interpretation
       of the P-bit integer bit patterns recovered from decoding codestream
       content and (potentially) palette lookup as floating point bit patterns,
       whose most significant bit is always a sign bit (must be zero if the
       representation is declared as unsigned), followed by E exponent bits
       (level offset by 2^{E-1}-1, and an M=P-1-E bit mantissa (denormalized if
       the E-bit offset exponent is zero).  This floating point case is
       identified by a non-zero `float_exp_bits' value, which corresponds to E
       and must be strictly positive.  In order to re-interpret x as a floating
       point value, it must be denormalized (scaled by 2^P) and the level
       offset (if any) must be removed -- that is, values declared as unsigned
       must be level shifted by 2^{P-1}.  In practice, absolute integers will
       always be used for decompressed codestream samples that represent
       floating point bit patterns, so there is at most the level offset of
       2^{P-1} to be applied.  In any case, the information found in the
       `orig_prec' and `orig_signed' members is sufficient to recover the bit
       pattern that needs to be re-interpreted as a floating point value,
       taking the `float_exp_bits' exponent size into account.  After
       re-interpretation, it is expected that the following steps are
       taken to enable float-formatted values to be handled in exactly the
       same way as the usual integer-formatted values:
       [>>] If `orig_signed' is false, any -ve floating point values (those
            with non-zero sign bit) should be clipped to 0.0, after which
            0.5 should be subtracted from all values.  This converts the
            expected range of 0.0 to 1.0 to the usual nominal range of
            -0.5 to +0.5.
       [>>] If `orig_signed' is true, -ve values are preserved, but all
            values are scaled by 0.5.  This may seem a little unusual, but
            it ensures that the nominal maximum intensity of 1.0, indicated
            by the JPX file format standard (in particular, IS15444-2/AMD3),
            becomes 0.5, so that a nominal range of -0.5 to +0.5 can again
            be assumed as a starting point for interpreting the signed values.
       [//]
       The `normalized_max', `normalized_zero' and `normalized_natural_zero'
       members all describe the floating point values recovered after applying
       the above adjustments.  It follows that `normalized_max' will always
       be 0.5, while `normalized_zero' will be -0.5 for unsiged originals
       and 0.0 for signed originals, exactly as for integer-formatted data.
       The main different between float-formatted and int-formatted data,
       after applying the above conversions, is that the `normalized_max'
       value is exactly 0.5, rather than being a little bit smaller than 0.5.
       [//]
       A `JP2_CHANNEL_FORMAT_FIXPOINT' representation involving I integer
       bits and F=P-I fraction bits, represents a real-valued signed or unsiged
       value y = X / 2^F, where X is the P-bit signed or unsigned integer.
       As with float-formatted values, it is expected that some steps are
       taken to enable fixpoint-formatted values to be handled in exactly
       the same way as the usual integer-formatted values, after which the
       `normalized_max', `normalized_zero' and `normalized_natural_zero'
       members provide the appropriate interpretation.  These steps are
       as follows:
       [>>] For unsigned fixed-point values, the relative normalized and
            level offset values x should be converted by adding 0.5 (to
            remove the level offset), multiplying by 2^{P-F}, which is
            usually >= 1.0, but could be smaller, then subtracting 0.5 (to
            re-introduce the expected level offset).  In total, this can
            be expressed as 2^{P-F}(x+0.5)-0.5 = 2^{P-F}*x + 0.5*(2^{P-F}-1).
       [>>] For signed fixed-point values, we have no level offset, so the
            only step that should be taken is multiplication by 2^{P-F}.
       [//]
       As with float-formatted samples, the `normalized_max' member for
       fixpoint-formatted data will always be exactly 0.5, rather than
       being slightly smaller than 0.5.
       [//]
       It is worth noting that although the `kdu_channel_interp' structure
       does describe the output from a JP2-family file palette lookup table,
       if there is one, the palette lookup tables found in the
       `kdu_channel_mapping' object have been converted from their original
       form.  All palette LUT's in the `kdu_channel_mapping' object have
       relative values, as 16-bit fixed-point or 32-bit IEEE floating point
       quantities, and the conversions expected for `JP2_CHANNEL_FORMAT_FLOAT'
       and `JP2_CHANNEL_FORMAT_FIXPOINT' data formats have already
       been applied.  These are exactly the same conversions that are
       performed by the `jp2_palette::get_lut' functions, when invoked with
       appropriate values for their optional arguments.  Thus, for palette
       entries, there is no need to take the values of `float_exp_bits' or
       `fixpoint_int_bits' into account (it has already been done), but the
       `normalized_max', `normalized_zero', `normalized_natural_zero' and
       `zeta' members retain their interpretation exactly.
    */
  public: // Member functions
    bool init(int orig_prec, bool orig_signed, float zeta_val,
              int data_format=JP2_CHANNEL_FORMAT_DEFAULT,
              const int *format_params=NULL);
      /* Convenience function to initialize all members, taking the original
         sample precision and signed/unsigned attribute, the special `zeta'
         value that identifies the relative natural zero point for unsigned
         representations of a colour channel, plus a `data_format' identifier
         and `format_params' array that have the same interpretation as those
         described in connection with `jp2_channels::set_colour_mapping',
         `jp2_channels::get_colour_mapping' and related functions.
         [//]
         In practice, the function can accommodate data formats of
         `JP2_CHANNEL_FORMAT_DEFAULT', `JP2_CHANNEL_FORMAT_FIXPOINT'
         and `JP2_CHANNEL_FORMAT_FLOAT', and so at most one entry from the
         `format_params' array will be read.  If the format is not understood,
         the function returns false, leaving the object with a default
         initialization that is equivalent to having passed
         `JP2_CHANNEL_FORMAT_DEFAULT'.
         [//]
         The `zeta' value is recovered for colour channels by calling the
         `jp2_colour::get_natural_unsigned_zero_point' functoin.
         For opacity channels and most colour channels, `zeta'=0.  In practice,
         `zeta' is 0 for most colour spaces other than opponent colour spaces,
         where the chrominance channels usually have `zeta'=0.5.  Enumerated
         colour spaces are generally defined relative to unsigned sample values
         with a given bit-depth B.  The natural zero point for a colour
         channel in this integer representation is then `zeta'*2^B.  It should
         be apparent that `zeta' cannot be smaller than 0 and must be
         strictly less than 1.0.  In practice, though, we strictly truncate
         `zeta_val' to the interval [0.0,0.75] here.
      */
  public: // Data
    int orig_prec; // Original precision P
    bool orig_signed; // False if original integer values were unsigned ints
    float zeta; // Used in mapping to unsigned outputs with "true zero" scaling
    int float_exp_bits;
    int fixpoint_int_bits;
    float normalized_max;
    float normalized_zero;
    float normalized_natural_zero;
  };
  
/*****************************************************************************/
/*                             kdu_channel_mapping                           */
/*****************************************************************************/

struct kdu_channel_mapping {
  /* [BIND: reference]
     [SYNOPSIS]
     This object provides all information required to express the
     relationship between code-stream image components and the colour
     channels to be produced during rendering.  In the simplest case, each
     colour channel (red, green and blue, or luminance) is directly
     assigned to a single code-stream component.  More generally, component
     samples may need to be mapped through a pallete lookup table, integer
     bit patterns might need to be reinterpreted as custom floating point
     or fixed-point representations, or a colour space transformation might
     be required.
     [//]
     The purpose of this class is to capture the reproduction functions
     required for correct colour reproduction, so that they can be passed to
     the `kdu_region_decompressor::start' function.
     [//]
     `kdu_channel_mapping' objects also serve to capture any information
     concerning opacity (alpha) channels.
     [//]
     While it is possible to build the contents of the object directly, in
     most cases you should use the `configure' functions, selecting the
     particular overloaded version of this function that accepts the
     highest level construct available in your rendering task.  For example,
     if your rendering source is a JP2 or JPX file, you are recommended to
     use the third form of the `configure' function that directly accepts
     the key JP2-family interfaces `jp2_colour', `jp2_channels' and so forth.
  */
  //---------------------------------------------------------------------------
  public: // Member functions
    KDU_AUX_EXPORT kdu_channel_mapping();
      /* [SYNOPSIS]
           Constructs an empty mapping object.  Use the `configure' function
           or else fill out the data members of this structure manually before
           passing the object into `kdu_region_decompressor'.  To return to the
           empty state, use the `clear' function.
      */
    ~kdu_channel_mapping() { clear(); }
      /* [SYNOPSIS]
           Calls `clear', which will delete any lookup tables
           referenced from the `palette' array.
      */
    KDU_AUX_EXPORT void clear();
      /* [SYNOPSIS]
           Returns all data members to the empty state created by the
           constructor, deleting any lookup tables which may have been
           installed in the `palette' array.
      */
    KDU_AUX_EXPORT bool configure(int num_identical_channels,
                                  int bit_depth, bool is_signed);
      /* [SYNOPSIS]
           This is the simplest type of initialization.  It initializes the
           object with `num_identical_channels' channels, each of which
           corresponds to a single image component (index 0), having the
           indicated bit-depth and signed/unsigned characteristic.
      */
    KDU_AUX_EXPORT bool configure(kdu_codestream codestream);
      /* [SYNOPSIS]
           Configures the channel mapping information based upon the
           dimensions of the supplied raw code-stream.  Since no explicit
           channel mapping information is available from a wrapping file
           format, the function assumes that the first 3 output image
           components represent red, green and blue colour channels, unless
           they have different dimensions or there are fewer than 3 components,
           in which case the function treats the first component as a
           luminance channel and ignores the others.
         [RETURNS]
           All versions of this overloaded function return true if successful.
           This version always returns true.
      */
    KDU_AUX_EXPORT bool
      configure(jp2_colour colour, jp2_channels channels,
                int codestream_idx, jp2_palette palette,
                jp2_dimensions codestream_dimensions);
      /* [SYNOPSIS]
           Configures the channel mappings and interpretation descriptors
           based on the supplied colour, palette and channel binding
           information.  The object is configured only for colour rendering,
           regardless of whether the `channels' object identifies the existence
           of opacity channels.  However, you may augment the configuration
           with alpha information at a later time by calling
           `add_alpha_to_configuration'.
         [ARG: codestream_idx]
           A JPX source may have multiple codestreams associated with a given
           compositing layer.  This argument identifies the index of the
           codestream which is to be used with the present configuration when
           it is supplied to `kdu_region_decompressor'.  The identifier is
           compared against the codestream identifiers returned via
           `channels.get_colour_mapping' and `channels.get_opacity_mapping'.
         [ARG: palette]
           Used to supply the `jp2_palette' interface associated with the
           codestream which is identified by `codestream_idx'.  For JP2 files,
           there is only one recognized palette and one recognized codestream
           having index 0.  For JPX files, each codestream is associated with
           its own palette.  Palette information is stored here at both
           16-bit fixed-point and 32-bit floating point precision, to provide
           as many options as possible for the `kdu_region_decompressor'.
           Additionally, if the channel uses a non-default data format
           (see `kdu_channel_interp'), palette entries stored here are
           converted so that they retain the expected nominal range from
           -0.5 to +0.5, as explained in the documentation accompanying
           `kdu_channel_interp' and `jp2_palette::get_lut'.
         [ARG: codestream_dimensions]
           Used to supply the `jp2_dimensions' interface associated with the
           codestream which is identified by `codestream_idx'.  For JP2 files,
           there is only one recognized codestream having index 0.  For JPX
           files, each codestream has its own set of dimensions.  The
           present object uses the `codestream_dimensions' interface only
           to obtain default rendering precision and signed/unsigned
           information to use in the event that
           `kdu_region_decompressor::process' is invoked using a
           `precision_bits' argument of 0.
         [RETURNS]
           All versions of this overloaded function return true if successful.
           This version returns false only if the colour space cannot be
           rendered.  This gives the caller an opportunity to provide an
           alternate colour space for rendering. JPX data sources, for example,
           may provide several related colour descriptions for a single
           compositing layer.  If any other error occurs, the function
           may invoke `kdu_error' rather than returninig -- this in turn
           may throw an exception which the caller can catch.
      */
    KDU_AUX_EXPORT bool configure(jp2_source *jp2_in, bool ignore_alpha);
      /* [SYNOPSIS]
           Simplified interface to the third form of the `configure' function
           above, based on a simple JP2 data source.  Automatically invokes
           `add_alpha_to_configuration' if `ignore_alpha' is false.  Note
           that `add_alpha_to_configuration' is invoked with the
           `ignore_premultiplied_alpha' argument set to true.  If you want
           to add both regular opacity and premultiplied opacity channels,
           you should call the `add_alpha_to_configuration' function
           explicitly.
         [RETURNS]
           All versions of this overloaded function return true if successful.
           This version always returns true.  If an error occurs, or the
           colour space cannot be converted, it generates an error through
           `kdu_error' rather than returning false.  If you would like the
           function to return false when the colour space cannot be rendered,
           use the second form of the `configure' function instead.
      */
    KDU_AUX_EXPORT bool
      add_alpha_to_configuration(jp2_channels channels, int codestream_idx,
                                 jp2_palette palette,
                                 jp2_dimensions codestream_dimensions,
                                 bool ignore_premultiplied_alpha=true);
      /* [SYNOPSIS]
           Unlike the `configure' functions, this function does not call
           `clear' automatically.  Instead, it tries to add an alpha channel
           to whatever configuration already exists.  It is legal (although
           not useful) to call this function even if an alpha channel has
           already been configured, since the function strips back the current
           configuration to include only the colour channels before looking
           to add an alpha channel.  Note, however, that it is perfectly
           acceptable (and quite useful) to add an alpha channel to a
           configuration which has just been cleared, so that only alpha
           information is recovered, without any colour information.
           [//]
           If a single alpha component cannot be found in the `channels'
           object, the function returns false.  This happens if there is no
           alpha (opacity) information, or if multiple distinct alpha channels
           are identified by `channels', or if the single alpha channel does
           not use the indicated codestream.  The `palette' must be provided
           since an alpha channel may be formed by passing the relevant image
           component samples through one of the palette lookup tables.
           [//]
           JP2/JPX files may identify alpha (opacity) information as
           premultiplied or not premultiplied.  These are sometimes known
           as associated and unassociated alpha.  The
           `ignore_premultiplied_alpha' argument controls which form of
           alpha information you are interested in adding, as indicated
           in the argument description below.  For compatibility with
           earlier versions of the function, this argument defaults to true.
           [//]
           As with the second form of the `configure' function, this function
           automatically installs palette lookup tables, if appropriate,
           using `palette.get_lut' to recover both fixed- and floating-point
           precision versions of these tables, and applying all relevant
           conversions for non-default pixel formats such as
           `JP2_CHANNEL_FORMAT_FLOAT' and `JP2_CHANNEL_FORMAT_FIXPOINT'.
         [RETURNS]
           True only if there is exactly one opacity channel and it is based
           on the indicated codestream.  Otherwise, the function returns
           false, adding no channels to the configuration.
         [ARG: ignore_premultiplied_alpha]
           If true, only unassociated alpha channels are examined, as
           reported by the `jp2_channels::get_opacity_mapping' function.
           Otherwise, both regular opacity and premultiplied opacity are
           considered, as reported by the `jp2_channels::get_premult_mapping'
           function.  To discover which type of alpha is being installed,
           you can call the function first with `ignore_premultiplied_alpha'
           set to true and then, if this call returns false, again with
           `ignore_premultiplied_alpha' set to false.
      */
  //----------------------------------------------------------------------------
  public: // Data
    int num_channels;
      /* [SYNOPSIS]
           Total number of channels to render, including colour channels and
           opacity channels.  The `configure' function will set this member
           to the number of colour channels (usually 1 or 3), adding
           one extra channel if there is alpha (opacity) information.
           You can, however, manually configure however many channels you
           like -- but you are recommended to do this only by explicitly
           calling `set_num_channels'.
      */
    int get_num_channels() { return num_channels; }
      /* [SYNOPSIS] Returns the value of the public member variable,
         `num_channels'.  Useful for function-only language bindings. */
    KDU_AUX_EXPORT void set_num_channels(int num);
      /* [SYNOPSIS]
           Convenience function for allocating the `source_components' and
           `palette' arrays required to hold the channel properties.  Also
           sets the `num_channels' member.  Copies any existing channel
           data, so that you can build up a channel description progressively
           by calling this function multiple times.  Initializes all new
           `source_components' entries to -1, all new `palette' entries
           to NULL, all new `default_rendering_precision' entries to 8, all
           new `default_rendering_signed' entries to false, and all new
           `channel_interp' records to an 8-bit unsigned integer interpretation
           with a natural zero point of 0.
      */
    int num_colour_channels;
      /* [SYNOPSIS]
           Indicates the number of initial channels which are used to describe
           pixel colour.  This might be smaller than `num_channels' if, for
           example, opacity channels are to be rendered.  All channels are
           processed in the same way, except in the event that colour space
           conversion is required.
      */
    int get_num_colour_channels() { return num_colour_channels; }
      /* [SYNOPSIS] Returns the value of the public member variable,
         `num_colour_channels'.  Useful for function-only language bindings. */
    int *source_components;
      /* [SYNOPSIS]
           Array holding the indices of the codestream's output image
           components which are used to form each of the colour channels.
           There must be one entry for each channel, although multiple channels
           may reference the same component.  Also, the mapping between source
           component samples and channel sample values need not be direct.
           [//]
           This array is allocated by `set_num_channels', but the `configure'
           functions do everything for you automatically.
      */
    int get_source_component(int n)
      { return ((n>=0) && (n<num_channels))?source_components[n]:-1; }
      /* [SYNOPSIS]
           Returns entry `n' of the public `source_components' array,
           or -1 if `n' lies outside the range 0 to `num_channels'-1.
           This function is useful for function-only language bindings. */
    int *default_rendering_precision;
      /* [SYNOPSIS]
           Indicates the default precision with which to represent the sample
           values returned by the `kdu_region_decompressor::process' function
           in the event that it is invoked with a `precision_bits' argument
           of 0.  A separate precision is provided for each channel.
           [//]
           If the value found in this array is zero, the corresponding channel
           has no default precision, so the `kdu_region_decompressor::process'
           functions select the maximum precision offered by the data type
           used to return sample values as the default precision.  This
           condition is introduced by the `configure' functions only when
           dealing with JPX files whose `jp2_channels' object reports a data
           format of `JP2_CHANNEL_FORMAT_FLOAT', so the original sample bit
           depth does not provide any indication of intended precision for
           the rendered content.
           [//]
           If the `configure' functions determine that the original
           representation for a channel was to be interpreted as a
           fixed-point P-bit integer with I integer bits and F=P-I fraction
           bits, the `default_rendering_precision' is set to F, rather than P.
           This is reasonable, since F is the precision that remains after the
           fixed-point number is scaled and clipped to the expected nominal
           range.  To recover the additional headroom associated with a
           fixpoint-formatted channel or a float-formatted channel, floating
           point versions of the `kdu_region_decompressor::process' function
           must be used.
           [//]
           If any colour transformation is performed (see `colour_converter'),
           only the first entry of the `default_rendering_precision' array
           is used -- that is, all channels must have the same default
           precision after colour conversion.
           [//]
           This array is allocated by `set_num_channels', but the `configure'
           functions do everything automatically, setting the entries up with
           the bit-depth values available from the file format or code-stream
           headers, as appropriate.  Nonetheless, you can change the entries of
           this array to suit the needs of your application. */
    bool *default_rendering_signed;
      /* [SYNOPSIS]
           Similar to `default_rendering_precision', but used to identify
           whether or not each channel's samples should be rendered as signed
           quantities when the `kdu_region_decompressor::process' function
           is invoked with a zero-valued `precision_bits' argument.
           [//]
           If any colour transformation is performed (see `colour_converter'),
           this array is ignored; all transformed colour channels default to
           an unsigned interpretation.
           [//]
           Again, the array is allocated by `set_num_channels'.  The
           `configure' functions do everything automatically, setting the
           entries up with values recovered from the file format or code-stream
           headers, as appropriate, but you can change the entries yourself
           if this turns out to be appropriate to your application.
      */
    int get_default_rendering_precision(int n)
      { return ((n>=0)&&(n<num_channels))?default_rendering_precision[n]:0; }
      /* [SYNOPSIS]
           Returns entry `n' of the public `default_rendering_precision'
           array, or 0 if `n' lies outside the range 0 to `num_channels'-1.
           This function is useful for function-only language bindings. */
    bool get_default_rendering_signed(int n)
      { return ((n>=0)&&(n<num_channels))?default_rendering_signed[n]:false; }
      /* [SYNOPSIS]
           Returns entry `n' of the public `default_rendering_signed'
           array, or false if `n' lies outside the range 0 to `num_channels'-1.
           This function is useful for function-only language bindings. */
    kdu_channel_interp *channel_interp;
      /* [SYNOPSIS]
           Like all the other member arrays, this array provides one entry per
           channel.  It is allocated by `set_num_channels' and the entries
           can be manually configured if you really know what you are doing,
           but this is strongly discouraged.  The `configure' functions use
           this array's entries to record additional details concerning the
           interpretation of the sample values that are decompressed from
           the underlying codestream and (if appropriate) subjected to any
           palette lookup (see `palette').
           [//]
           The main reason for introducing this array is that some channels
           might use sample values whose bit-patterns (once expressed in the
           original integer representation advertised by codestream headers
           and file boxes) actually need to be re-interpreted as 
           floating-point or fixed-point bit patterns, rather than as integers.
           A second reason for introducing the array is that it allows the
           capabilities advdertised in connection with the
           `kdu_region_decompressor::set_true_scaling' function to be
           implemented in a way that does not depend upon the
           `default_rendering_precision' and `default_rendering_signed'
           arrays retaining the values with which they were initialized by
           `configure' -- they can be overridden by an application developer
           to customize the behaviour of the `kdu_region_decompressor::process'
           functions, without losing track of the precise interpretation of
           the decompressed sample values.
           [//]
           The `kdu_channel_interp::zeta' members affect the way in which
           `kdu_region_decompresor::process' maps decompressed and/or palette
           mapped values to output samples, when the "true-zero" mode described
           in connection with `kdu_region_decompressor::set_true_scaling' is
           in force.  The `zeta' value is recovered by the `configure' function
           from `jp2_colour::get_natural_unsigned_zero_point', where a
           `jp2_colour' interface is available.  If a colour transform is
           performed, however, (see `colour_converter') the output samples are
           always mapped based on a natural zero point of `zeta'=0.
           [//]
           In order to emphasize the delicate nature of the information in this
           array, we do not currently provide any accessors to its content for
           non-native language bindings.  Nonetheless, the documentation
           provided for the `kdu_channel_interp' object may prove instructive
           for those wanting to gain a deep knowledge of what is going on.
      */
    int palette_bits;
      /* [SYNOPSIS]
           Number of index bits used with any palette lookup tables found in
           the `palette' array.  This value has no relationship to the
           precision associated with the palette entries themselves.
      */
    kdu_sample16 **fix16_palette;
      /* [SYNOPSIS]
           Array with `num_channels' entries, each of which is either NULL or
           else a pointer to a lookup table with 2^{`palette_bits'} entries.
           [//]
           Note carefully that each lookup table must have a unique buffer,
           even if all lookup tables hold identical contents.  The buffer must
           be allocated using `new', since it will automatically be
           de-allocated using `delete' when the present object is destroyed, or
           its `clear' function is called.
           [//]
           If `palette_bits' is non-zero and one or more entries in the
           `palette' array are non-NULL, the corresponding colour channels
           are recovered by scaling the relevant code-stream image component
           samples (see `source_components') to the range 0 through
           2^{`palette_bits'}-1 and applying them (as indices) to the relevant
           lookup tables.  If the code-stream image component has an unsigned
           representation (this is common), the signed samples recovered from
           `kdu_synthesis' or `kdu_decoder' will be level adjusted to unsigned
           values before applying them as indices to a palette lookup table.
           [//]
           The entries in each `fix16_palette' lookup table are 16-bit
           fixed point values, having KDU_FIX_POINT fraction bits and
           representing normalized quantities having the nominal range
           -0.5 to +0.5.  The `channel_interp' array provides additional
           information about the interpretation of these palette entries, if
           they exist.  In particular, the original bit-depth of each palette
           entry and its signed/unsigned nature can be learned from
           `kdu_channel_interp::orig_precision', the true maximum value
           (as a normalized real-valued quantity) and the true zero and
           natural zero points can also be learned from `kdu_channel_interp'.
           [//]
           If a channel has a non-default data format, i.e. anything other
           than `JP2_CHANNEL_FORMAT_DEFAULT', the palette entries must be
           pre-converted, taking exactly the same steps that are described in
           the documentation of the `jp2_palette::get_lut' functions or,
           equivalently, in the documentation of the `kdu_channel_interp'
           structure.
      */
    float **float_palette;
      /* [SYNOPSIS]
           Same as `fix16_palette', but the entries of these tables are all
           represented at floating point precision.  As with `fix16_palette',
           if the channel has a non-default data format (i.e.,
           `JP2_CHANNEL_FORMAT_FLOAT' or `JP2_CHANNEL_FORMAT_FIXPOINT'),
           the palette entries must be pre-converted, taking exactly the
           same steps that are described in the documentation of the
           `jp2_palette::get_lut' functions or, equivalently, in the
           documentation of the `kdu_channel_interp' structure.
           [//]
           If a channel has a palette, it must have a non-NULL `fix16_palette'
           lookup table, but the corresponding `float_palette' table is allowed
           to be NULL, except where a non-trivial data format is involved.
           Specifically, the `kdu_region_decompressor' object expects both
           palette lookup tables to be available at both precisions if
           channel uses a `KDU_CHANNEL_FORMAT_FLOAT' format or a
           `KDU_CHANNEL_FORMAT_FIXPOINT' format with a non-zero number of
           integer bits.  Equivalently, if the corresponding `channel_interp'
           entry has non-zero `kdu_channel_interp::float_exp_bits' or
           `kdu_channel_interp::fixpoint_int_bits' entries, then either
           both or neither of the `fix16_palette' and `float_palette'
           entries for the channel should be non-NULL.
           [//]
           In practice, the `configure' function is almost always used to
           build these palette lookup tables; it always installs tables with
           both fix16 and float precision where there is a palette.
      */
    jp2_colour_converter colour_converter;
      /* [SYNOPSIS]
           Initialized to an empty interface (`colour_converter.exists' returns
           false), you may call `colour_converter.init' to provide colour
           transformation capabilities.  This object is used by reference
           within `kdu_region_decompressor', so you should not alter its
           state while still engaged in region processing.
           [//]
           This object is initialized by the 2'nd and 3'rd forms of the
           `configure' function to convert the JP2 or JPX colour representation
           to sRGB, if possible.
      */
    jp2_colour_converter *get_colour_converter() { return &colour_converter; }
      /* [SYNOPSIS]
            Returns a pointer to the public member variable `colour_converter';
            useful for function-only language bindings.
      */
  };

/*****************************************************************************/
/*                            kdu_region_decompressor                        */
/*****************************************************************************/

class kdu_region_decompressor {
  /* [BIND: reference]
     [SYNOPSIS]
       Objects of this class provide a powerful mechanism for interacting with
       JPEG2000 compressed imagery.  They are particularly suitable for
       applications requiring interactive decompression, such as browsers
       and image editors, although there may be many other applications for
       the object.  The object abstracts all details associated with opening
       and closing tiles, colour transformations, interpolation (possibly
       by different amounts for each code-stream component) and determining
       the elements which are required to recover a given region of interest
       within the image.
       [//]
       The object also manages all state information required
       to process any selected image region (at any given scale) incrementally
       through multiple invocations of the `process' function.  This allows
       the CPU-intensive decompression operations to be interleaved with other
       tasks (e.g., user event processing) to maintain the responsiveness
       of an interactive application.
       [//]
       The implementation here is entirely platform independent, even though
       it may often be embedded inside applications which contain platform
       dependent code to manage graphical user interfaces.
       [//]
       From Kakadu version 5.1, this object offers multi-threaded processing
       capabilities for enhanced throughput.  These capabilities are based
       upon the options for multi-threaded processing offered by the
       underlying `kdu_multi_synthesis' object and the `kdu_synthesis' and
       `kdu_decoder' objects which it, in turn, uses.  Multi-threaded
       processing provides the greatest benefit on platforms with multiple
       physical CPU's, or where CPU's offer hyperthreading capabilities.
       Interestingly, although hyper-threading is often reported as offering
       relatively little gain, Kakadu's multi-threading model is typically
       able to squeeze 30-50% speedup out of processors which offer
       hyperthreading, in addition to the benefits which can be reaped from
       true multi-processor (or multi-core) architectures.  Even on platforms
       which do not offer either multiple CPU's or a single hyperthreading
       CPU, multi-threaded processing could be beneficial, depending on other
       bottlenecks which your decompressed imagery might encounter -- this is
       because underlying decompression tasks can proceed while other steps
       might be blocked on I/O conditions, for example.
       [//]
       To take advantage of multi-threading, you need to create a
       `kdu_thread_env' object, add a suitable number of working threads to
       it (see comments appearing with the definition of `kdu_thread_env') and
       pass it into the `start' function.  You can re-use this `kdu_thread_env'
       object as often as you like -- that is, you need not tear down and
       recreate the collaborating multi-threaded environment between calls to
       `finish' and `start'.  Multi-threading could not be much simpler.  The
       only thing you do need to remember is that all calls to `start',
       `process' and `finish' should be executed from the same thread -- the
       one identified by the `kdu_thread_env' reference passed to `start'.
       This constraint represents a slight loss of flexibility with respect
       to the core processing objects such as `kdu_multi_synthesis', which
       allow calls from any thread.  In exchange, however, you get simplicity.
       In particular, you only need to pass the `kdu_thread_env' object into
       the `start' function, after which the object remembers the thread
       reference for you.
       [//]
       From KDU-7.6, this object supports the use of a `kdu_quality_limiter'
       as the preferred method to manage limited quality rendering, which
       can significantly reduce computational complexity when rendering
       very high resolution content at reduced resolutions -- the idea is
       that very high resolution content requires highly precise low
       frequency subbands, yet when the content is rendered at reduced
       resolution, this precision tends to be significantly too high,
       resulting in unnecessarily high decoding effort.  The
       `set_quality_limiting' function allows you install a
       `kdu_quality_limiter' and/or configure display resolution settings.
  */
  public: // Member functions
    KDU_AUX_EXPORT kdu_region_decompressor();
    KDU_AUX_EXPORT virtual ~kdu_region_decompressor();
      /* [SYNOPSIS]
           Deallocates any resources which might have been left behind if
           a `finish' call was still pending when the object was destroyed.
      */
    KDU_AUX_EXPORT static void
      get_safe_expansion_factors(kdu_codestream codestream,
         kdu_channel_mapping *mapping, int single_component,
         int discard_levels, double &min_prod, double &max_x, double &max_y,
         kdu_component_access_mode access_mode=KDU_WANT_OUTPUT_COMPONENTS);
      /* [SYNOPSIS]
           As explained in connection with the `start' function, the internal
           implementation is unable to handle truly massive resolution
           reductions.  This function may be used to discover a safe lower
           bound for the amount of expansion (`expand_numerator' /
           `expand_denominator') passed to `start'.  This lower bound is
           returned via `min_prod', which holds a safe minimum value for the
           product of (`expand_numerator.x' * `expand_numerator.y') /
           (`expand_denominator.x' * `expand_denominator.y') in a subsequent
           call to `start' or `get_rendered_image_dims'.  The bound is quite
           conservative, leaving a large margin for error in the approximation
           of real scaling factors by rational numbers (numerator /
           denominator).
           [//]
           It is also possible that the `expand_numerator' and
           `expand_denominator' parameters supplied to `start' or
           `get_rendered_image_dims' represent too large an expansion factor,
           so that one or both dimensions approach or exceed 2^31.  The
           internal coordinate management logic and resampling algorithm can
           fail under these circumstances.  The `max_x' and `max_y'
           parameter are used to return safe upper bounds on the rational
           expansion factor represented by the `expand_numerator' and
           `expand_denominator' parameters passed to `start'.
      */
    KDU_AUX_EXPORT static kdu_dims
      find_render_dims(kdu_dims codestream_region, kdu_coords ref_comp_subs,
                       kdu_coords ref_comp_expand_numerator,
                       kdu_coords ref_comp_expand_denominator);
      /* [SYNOPSIS]
           Find the region on the rendering grid which is associated with
           a supplied `codestream_region'.  The latter is expressed on the
           high resolution codestream canvas.  The function first identifies
           the corresponding region on the reference image component, whose
           sub-sampling factors (relative to the high resolution codestream
           canvas) are given by `ref_comp_subs'.  The function then applies
           the rational expansion factors given by `ref_comp_expand_numerator'
           and `ref_comp_expand_denominator'.
           [//]
           The region mapping conventions here are identical to those described
           for the `start' and `get_rendered_image_dims' functions.  In fact,
           this function is the single place in which dimensioning of
           rendered imagery is performed.  The function is declared static, so
           it can be used by other objects or applications without an
           instantiated instance of the `kdu_region_decompressor' class.
           [//]
           The specific coordinate transformations employed by this function
           are as follows.  Let [E,F), [E',F') and [Er,Fr) define half-open
           intervals on the high-resolution codestream canvas, the reference
           image component, and the rendering grid, respectively.  These
           intervals represent either the horizontal or vertical ordinate
           for the respective grid -- i.e., the same transformations apply
           in each direction.  Further, let N and D represent the values of
           `ref_comp_expand_numerator' and `ref_comp_expand_denominator' in
           the relevant direction, and let S denote the value of
           `ref_comp_subs' in the relevant direction.
           [>>] The function first applies the normal mapping between the high
                resolution codestream canvas and the reference image component,
                setting: E' = ceil(E/S) and F' = ceil(F/S).
           [>>] The function then applies the following the rational expansion
                factors as follows: Er=ceil(E'*N/D-H) and Fr=ceil(F'*N/D-H).
                Here, H is an approximately "half pixel" offset, given by
                H = floor((N-1)/2) / D.
           [//]
           The coordinate mapping process described above can be interpreted
           as follows.  Let x be the location of a sample on the reference
           image component.  This sample has the location x*S on the high
           resolution codestream canvas and belongs to the region [E,F) if
           and only if E <= x*S < F.  Although the rational expansion factors
           can be contractive (i.e., N can be smaller than D), we will refer
           to the transformation from [E',F') to [Er,Fr) as "expansion".
           During this "expansion" process, the sample at location x is
           considered to occupy the region [x*N/D-H,(x+1)*N/D-H) on the
           rendering grid.  The regions associated with each successive
           integer-valued x are thus disjoint and contiguous -- note that
           for large N, H is almost exactly equal to 0.5*(N/D).  In this way,
           the region on the rendering grid which is occupied by image
           component samples within the interval [E',F') is given by the
           half-open interval [E'*(N/D)-H,F'*(N/D)-H).  The determined range
           of rendering grid points [Er,Fr) is exactly the set of grid points
           whose integer locations fall within the above range.
      */
    KDU_AUX_EXPORT static kdu_coords
      find_codestream_point(kdu_coords render_point, kdu_coords ref_comp_subs,
                            kdu_coords ref_comp_expand_numerator,
                            kdu_coords ref_comp_expand_denominator,
                            bool allow_fractional_mapping=false);
      /* [SYNOPSIS]
           This function provides a uniform procedure for identifying
           a representative location on the high resolution codestream canvas
           which corresponds to a location (`render_point') on the rendering
           grid.
           [//]
           Considering the conventions implemented by the `find_render_dims'
           function, a sample on the reference component, at location x
           (consider this as either the horizontal or vertical ordinate of
           the location) is considered to cover the half-open interval
           [x*N/D-H,(x+1)*N/D-H) on the reference grid.  Here, N and D are
           taken from the relevant ordinate of `ref_comp_expand_numerator'
           and `ref_comp_expand_denominator', respectively, and
           H = floor((N-1)/2) / D.  There is exactly one location x associated
           with the `render_point' X.  This location satisfies
           x <= (X+H)*D/N < x+1, so x = floor((X+H)*D/N).
           [//]
           Assuming symmetric wavelet kernels, the location x on the reference
           image component has its centre of mass at location x*S on the
           high resolution codestream canvas, where S is the reference
           component sub-sampling factor found in the relevant ordinate of
           `ref_comp_subs'.  Regardless of wavelet kernel symmetry, it is
           appealing to adopt a policy in which locations within content
           rendered from sub-sampled data can only be associated with high
           resolution codestream canvas locations which are multiples of the
           sub-sampling factor.  For this reason, the horizontal and vertical
           components of the returned `kdu_coords' object are set to
           S * floor((X+H)*D/N), where S, X, N, D and H are obtained from
           the horizontal (resp. vertical) components of the supplied
           arguments, as described above.
           [//]
           One weakness of the above procedure is that the discovered
           points on the codestream canvas are necessarily multiples of
           the relevant sub-sampling factors S.  In some cases, we may have
           S > 1, but also N > D, so that the transformation involves both
           sub-sampling and expansion.  In such cases, the caller's intent
           may be better realized by decreasing S and increasing D by the
           same factor.  To invoke the function in this way, you should
           set the `allow_fractional_mapping' argument to true.
           [//]
           You should note that the returned point is not guaranteed to
           have coordinates that lie within the image region on the
           high resolution codestream canvas.  This may be important for
           some applications -- e.g., when working with JPX regions of
           interest whose geometry is defined in terms of points that lie
           outside the region occupied by actual imagery.
       */
    KDU_AUX_EXPORT static kdu_coords
      find_render_point(kdu_coords codestream_point, kdu_coords ref_comp_subs,
                        kdu_coords ref_comp_expand_numerator,
                        kdu_coords ref_comp_expand_denominator,
                        bool allow_fractional_mapping=false);
      /* [SYNOPSIS]
           This function returns a single representative location on the
           rendering grid, corresponding to `codestream_point' on the
           high resolution codestream canvas.
           [//]
           The function first locates the `codestream_point' on the
           coordinate system of a "reference" image component which has the
           sub-sampling factors supplied by `ref_comp_subs'.  It then
           applies the expansion factors represented by
           `ref_comp_expand_numerator' and `ref_comp_expand_denominator' to
           determine the region on the rendering grid which is associated
           with the relevant sample on the reference image component.  Finally,
           the centroid of this region is taken to be the representative
           location on the rendering grid.
           [//]
           The specific transformations employed by this function are as
           follows.  Let X denote the horizontal or vertical ordinate of
           `codestream_point' (the same transformations are applied in each
           direction).  Assuming symmetric wavelet kernels, the closest
           corresponding point x on the reference image component is
           obtained from x = ceil(X/S - 0.5) = ceil((2*X-S)/(2*S)).
           [//]
           Considering the conventions described in conjunction with the
           `find_render_dims' function, the reference component sample at
           location x covers the half-open interval [x*N/D-H,(x+1)*N/D-H) on
           the reference grid.  Here, N and D are the relevant ordinates of
           `ref_comp_expand_numerator' and `ref_comp_expand_denominator',
           respectively, and H = floor((N-1)/2) / D.  The present function
           returns the centroid of this region, rounded to the "nearest"
           integer.  Specifically, the function returns
           Xr = floor((2*x+1)*N/(2*D) - H).
           [//]
           One weakness of the above procedure is that `codestream_point' is
           always quantized to a location on the reference component grid.
           In some cases, we may have S > 1, but also N > D, so that the
           transformation involves both sub-sampling and expansion.  In such
           cases, the caller's intent may be better realized by decreasing S
           and increasing D by the same factor.  To invoke the function in
           this way, you should set the `allow_fractional_mapping' argument
           to true.
           [//]
           You should note that the returned point is not guaranteed to lie
           within the region returned by `find_render_dims'.  This may be
           important for some applications -- e.g., an application that
           maps JPX regions of interest that extend beyond the borders of
           the image region on its codestream canvas.
      */
    KDU_AUX_EXPORT static kdu_dims
      find_render_cover_dims(kdu_dims codestream_dims,
                             kdu_coords ref_comp_subs,
                             kdu_coords ref_comp_expand_numerator,
                             kdu_coords ref_comp_expand_denominator,
                             bool allow_fractional_mapping)
        {
          kdu_coords tl_point = codestream_dims.pos;
          kdu_coords br_point = tl_point+codestream_dims.size-kdu_coords(1,1);
          tl_point = find_render_point(tl_point,ref_comp_subs,
                                       ref_comp_expand_numerator,
                                       ref_comp_expand_denominator,
                                       allow_fractional_mapping);
          br_point = find_render_point(br_point,ref_comp_subs,
                                       ref_comp_expand_numerator,
                                       ref_comp_expand_denominator,
                                       allow_fractional_mapping);
          kdu_dims result;  result.pos=tl_point;
          result.size = br_point-tl_point + kdu_coords(1,1);
          return result;
        }
      /* [SYNOPSIS]
           Returns the smallest region on the rendering grid which includes
           the locations which would be returned by `find_render_point' when
           invoked with every location within `codestream_dims'.  This is
           almost, but not quite exactly identical to `find_render_dims'.  The
           difference is subtle but important.  The `find_render_dims' function
           has the property that disjoint regions on the high resolution
           codestream canvas produce disjoint regions on the rendering grid.
           Unfortunately, though, this necessarily means that some individual
           points on the high resolution canvas may occupy an empty region on
           the rendering grid (this must be the case whenever the reference
           subsampling factors given by `ref_comp_subs' are greater than 1).
           However, the `find_render_point' function necessarily returns a
           rendering grid point for every point on the high resolution
           codestream canvas.
           [//]
           Where an application needs to be sure that
           all such points are included within a region on the rendering grid,
           this function should be used in preference to `find_render_dims'.
           On the other hand, the preservation of disjoint regions by
           `find_render_dims' is considered an important attribute for
           sizing rendered content within the `start' function.
           [//]
           The present function also accepts the `allow_fractional_mapping'
           argument, whose interpretation is identical to its namesake in
           `find_render_point'.  This argument allows you to determine the
           cover region without having to first sub-sample down to the
           reference codestream's canvas.
      */
    KDU_AUX_EXPORT static kdu_dims
      find_codestream_cover_dims(kdu_dims render_dims,
                                 kdu_coords ref_comp_subs,
                                 kdu_coords ref_comp_expand_numerator,
                                 kdu_coords ref_comp_expand_denominator,
                                 bool allow_fractional_mapping=false);
      /* [SYNOPSIS]
           This function finds the tightest region on the high resolution
           codestream canvas which includes all locations which would be mapped
           into the `render_dims' region by the `find_render_point' function.
           [//]
           Let x be the horizontal or vertical ordinate of a location on the
           reference image component.  As discussed in conjunction with
           `find_render_point', this location maps to the location Xr on the
           rendering grid, where Xr = floor((2*x+1)*N/(2*D) - H), where N and D
           are the corresponding ordinates from `ref_comp_expand_numerator'
           and `ref_comp_expand_denominator' and H = floor((N-1)/2) / D.
           Now let [Er,Fr) denote the half-open interval representing the
           corresponding ordinate from `render_dims'.
           [//]
           Write x_min for the smallest x for which Xr >= Er.  That is, x_min
           is the smallest x for which (2*x+1)*N/(2*D)-H >= Er; equivalently,
           x >= ((Er+H)*2D - N) / 2N.  So x_min = ceil(((Er+H)*2D-N)/2N).
           Write x_max for the largest x for which Xr <= Fr-1.  That is, x_max
           is the largest x for which (2*x+1)*N/(2*D)-H < Fr; equivalently,
           x < ((Fr+H)*2D - N) / 2N. So x_max = ceil(((Fr+H)*2D-N)/2N)-1.
           [//]
           From the above, we see that the range of locations on the reference
           grid which map to the half-open interval [Er,Fr) can be written
           [e,f) where e=ceil(((Er+H)*2D-N)/2N) and f=ceil(((Fr+H)*2D-N)/2N).
           Now write [E,F) for the range of locations on the high resolution
           codestream canvas which map to [e,f), using the rounding conventions
           adoped by `find_render_point'.  These are the locations X, such that
           x = ceil(X/S - 0.5) lies in [e,f), where S is the relevant ordinate
           from `ref_comp_subs'.  It follows that E is the minimum X such
           that ceil(X/S - 0.5) >= e; equivalently, E is the minimum X such
           that X/S - 0.5 > (e-1) -- i.e., X > (e-0.5)*S.  So
           E = floor((e-0.5)*S) + 1 = e*S + 1 - ceil(S/2).  Similarly,
           F-1 is the maximum X such that ceil(X/S - 0.5) <= f-1; equivalently,
           F-1 is the maximum X such that X/S - 0.5 <= f-1 -- i.e.,
           X <= S*(f-0.5).  So F = 1 + floor(S*(f-0.5)) = f*S + 1 - ceil(S/2).
           [//]
           As with `find_codestream_point' and `find_render_cover_dims', this
           function can be invoked with the `allow_fractional_mapping'
           argument equal to true, in which case sub-sampling factors S
           are absorbed into expansion factors (N > D) to the extent that this
           is possible.  Also, as with those functions, the returned region
           is not confined to the image region on the high resolution
           codestream canvas, and that can be useful in some cases.
      */
    KDU_AUX_EXPORT kdu_dims
      get_rendered_image_dims(kdu_codestream codestream,
           kdu_channel_mapping *mapping, int single_component,
           int discard_levels, kdu_coords expand_numerator,
           kdu_coords expand_denominator=kdu_coords(1,1),
           kdu_component_access_mode access_mode=KDU_WANT_OUTPUT_COMPONENTS);
      /* [SYNOPSIS]
           This function may be used to determine the size of the complete
           image on the rendering canvas, based upon the supplied component
           mapping rules, number of discarded resolution levels and rational
           expansion factors.  For further explanation of the rendering
           canvas and these various parameters, see the `start' function.
           The present function is provided to assist implementators in
           ensuring that the `region' they pass to `start' will indeed lie
           within the span of the image after all appropriate level
           discarding and expansion has taken place.
           [//]
           The function may not be called between `start' and `finish' -- i.e.
           while the object is open for processing.
      */
    kdu_dims get_rendered_image_dims() { return full_render_dims; }
      /* [SYNOPSIS]
           This version of the `get_rendered_image_dims' function returns the
           location and dimensions of the complete image on the rendering
           canvas, based on the parameters which were supplied to `start'.
           If the function is called prior to `start' or after `finish', it
           will return an empty region.
      */
    void set_true_scaling(bool true_zero, bool true_max)
      { this->white_stretch_precision = 0;
        this->want_true_zero = true_zero;
        this->want_true_max = true_max; }
      /* [SYNOPSIS]
           For predictable behaviour, you should call this function only while
           the object is inactive -- i.e., before the first call to `start',
           or after the most recent call to `finish' and before any subsequent
           call to `start'.
           [//]
           The function sets up the "true scaling" mode which will become
           active when `start' is next called.  The same "true scaling" mode
           will be used in each subsequent call to `start' unless the present
           function is used to change it.  Note that calls to this function
           cancel the effect of any prior call to `set_white_stretch'; indeed
           that function is no longer recommended, since this function
           subsumes and substantially builds upon its role.
           [//]
           This function is introduced to provide the application with more
           control over the way in which sample values get scaled, and perhaps
           offset, as part of the rendering pipeline.  The function can make a
           difference to the way sample values identified as holding a signed
           representation are handled, as opposed to the vastly more common
           unsigned representation.  The function can also make a difference
           to the values returned by the `process' functions where the
           precision of the returned data differs from that originally
           identified in the source codestream.  For common cases, where
           original samples had an original unsigned representation and the
           `process' function returns samples with the same (i.e., native,
           or default) precision, the "true scaling" options discussed here
           make no difference.
           [//]
           At an elementary level, you can understand the `true_zero' and
           `true_max' modes as follows.  The objective of the `true_max' mode
           is to ensure that the maximum representable intensity of an input
           sample is mapped exactly to the maximum representable intensity of
           the output sample, regardless of their precisions.  This usually
           requires floating point conversion and multiplications, which can
           add to the computational complexity of the overall process relative
           to the default policy of using bit shifts alone.  In most cases,
           bit shifts yield very close approximations to the true-max mapping.
           The objective of the `true_zero' mode is to ensure that the natural
           representation for zero intensity (or the achromatic level for
           chrominance channels) is preserved by mappings across different
           precisions and between signed and unsigned representations.  The
           main impact of `true_zero' is that signed original sample values
           have their zero point mapped to zero (or black) of an unsigned
           output representation, rather than to a mid-level grey value,
           which is the default.  The `true_zero' mode has a subtle impact
           on the mapping of colour channels for which the natural zero point
           of an unsigned representation is something other than 0, as
           explained more carefully below.
           [//]
           The `true_zero' option determines how signed sample values are
           handled.  The JPEG2000 standards assign image components a
           representation involving a precision (or bit-depth) P, either as
           signed or unsigned integers.  This happens at the codestream and
           file format levels and applies also where palette lookup tables
           are involved. Unsigned sample values are generally offset by
           subtracting 2^{P-1} for the purpose of better utilizing the
           dynamic range afforded by numeric quantities in the compression
           and decompression processing.  Also, except where compression is
           truly reversible (required only for truly lossless compression),
           all computations are generally best performed using fixed-point or
           floating-point arithmetic, abandoning a strict connection with
           integers.  With this in mind, all sample values and palette entries
           can be thought of as taking values x in the range -0.5 to 0.5,
           where all unsigned values have been level offset and values that
           are processed as absolute integers are related to x through
           multiplication by 2^P.  The default mechanism for mapping all such
           representations to rendered intensity values is to multiply (x+0.5)
           by 2^B for unsigned outputs and x by 2^B for signed outputs, where
           B is the target precision associated with the relevant `process'
           call -- unsigned outputs are always the default.  This means that
           sample values that were originally signed prior to compression
           have their zero point mapped to a mid-level grey, allowing both
           negative and positive deviations from zero to be visualized.
           [//]
           Passing `true_zero'=true in this function modifies the default
           policy, so that the "natural zero point" always maps to the
           "natural zero point".  The natural zero point for a signed quantity
           is always taken to be 0, while the natural zero point for an
           unsigned quantity is normally also 0.  An exception occurs with
           samples that represent chominance components in an opponent
           colour space such as YCbCr.  Chrominance components are most
           commonly represented as unsigned integers with a natural zero
           point of 2^{P-1}, where P is the precision.  More generally,
           associated with every channel the natural zero point for unsigned
           representations is expressed as a relative parameter \zeta, that
           is derived from the colour space.  For opacity channels and the
           colour channels of most colour spaces, \zeta=0.  For chrominance
           channels of typical opponent colour spaces, \zeta=0.5.  An
           exception is the CIEL*a*b* colour space, for which the a* channel
           usually has \zeta=0.5 but the b* channel usually has \zeta=0.375.
           The natural zero point for an unsigned P-bit integer is then
           \zeta * 2^P.  Below we give the "true zero" conversion equations
           for the case where `true_max'=false.  The `true_max'=true case is
           considered later on.
           [>>] In the `true_zero' mode, an originally signed sample whose
                relative decompressed value x has the nominal range -0.5 to
                +0.5 is mapped to a B-bit unsigned output Y through
                Y = 2^B * [\zeta + x * (1-\zeta) / 0.5].  In the
                common case where \zeta=0, this becomes Y = x * 2^{B+1}.
                If \zeta=0.5, it becomes Y = (x+0.5) * 2^B.  In every case,
                of course, the output needs to be clipped to the range 0 to
                2^B-1.
           [>>] In the `true_zero' mode, an originally unsigned sample whose
                relative decompressed value x has the nominal range -0.5 to
                +0.5 is mapped to a B-bit signed output Y through
                Y = 2^B * (x + 0.5 - \zeta) * 0.5 / (1-\zeta).  With \zeta=0,
                this becomes Y = (x+0.5)*2^{B-1}, while when \zeta=0.5 it
                becomes Y = x * 2^B.
           [//]
           Notice that both the default and "true-zero" mappings for just about
           any situation can be accommodated using bit-shifts alone so long as
           the `true_max' option is not asserted (see below).  The only
           exceptions to this occur where zeta is neither 0 nor 0.5, for which
           the conversion is more complex.
           [//]
           As mentioned above, the `true_max' option enforces the mapping of
           the true maximum value of an input representation exactly to the
           true maximum value of the output representation.  This primarily
           affects transformations between samples of different precisions,
           but it also impacts conversions between signed and unsigned
           representations when the `true_zero' mode is asserted.
           Specifically, observe that although the level-offset relative
           decompressed sample values x lie in the range -0.5 to +0.5, the
           actual maximum value that should be expected in the absence of any
           compression distortion is x = 0.5-2^{-P}.  Meanwhile, the maximum
           value of a B-bit unsigned output is 2^B-1, while the maximum value
           of a B-bit signed output is 2^{B-1}-1.  Putting these together,
           we can describe the `true_max' conversion operations with and
           without `true_zero' as folows:
           [//]
           If `true_max'=true and `true_zero'=false, the scaling policy aims
           to stretch unsigned inputs into the full range of outputs.  For
           signed inputs, the approach is similar, but the positive part of
           the range of inputs is stretched into the corresponding part of
           the output range.  For B-bit-outputs Y and the level-offset
           relative inputs x, this means the following:
           [>>] If input and output are both unsigned,
                Y = 2^B * (x+0.5) * (1-2^{-B}) / (1-2^{-P}).
           [>>] If input is unsigned but output is signed,
                Y = 2^B * (x+0.5) * (1-2^{-B}) / (1-2^{-P}) - 2^{B-1}.
           [>>] If input is signed and output unsigned,
                Y = 2^B * x * (0.5-2^{-B}) / (0.5-2^{-P}) + 2^{B-1}.
           [>>] If input and output are both signed,
                Y = 2^B * x * (0.5-2^{-B}) / (0.5-2^{-P}).
           [//]
           If `true_max' and `true_zero' are true, the following relationships
           hold for signed or unsigned B-bit outputs Y and level-offset
           relative inputs x:
           [>>] If input and output are both unsigned,
                Y=2^B*[\zeta+(x+0.5-\zeta)*(1-2^{-B}-\zeta)/(1-2^{-P}-\zeta)].
           [>>] If input and output are both signed,
                Y = x * 2^B * (0.5 - 2^{-B}) / (0.5 - 2^{-P}).
           [>>] If inputs are signed and outputs unsigned,
                Y = 2^B * [\zeta + x * (1-2^{-B}-\zeta) / (0.5-2^{-P})].
           [>>] If inputs are unsigned and outputs are signed,
                Y = 2^B * (x+0.5-\zeta) * (0.5-2^{-B}) / (1-2^{-P}-\zeta)
           [//]
           The above, "true scaling" configuration, is defined in
           IS15444-2/AMD3 to be the correct interpretation for rendering
           of JPX images, but it has some drawbacks in regard to the
           visualisation of signed imagery and it is not necessarily suitable
           for applications that use higher bit-depth B-bit targets simply
           as a common encapsulation mechanism for decoded data.  For such
           applications, or for those which require backward compatibility
           with the Kakadu versions prior to 7.8, the default interpretation
           with `true_zero' and `true_max' both false, is more appropriate.
           [//]
           We now consider the impact of the "true scaling" options on input
           sample representations that cannot be interpreted as P-bit signed
           or unsigned integers.  Input values with non-integer representations
           occur when the `data_format' returned by
           `jp2_channels::get_colour_mapping' is not
           `JP2_CHANNEL_FORMAT_DEFAULT'.  The two non-default formats that
           are handled by this object are `JP2_CHANNEL_FORMAT_FIXPOINT'
           and `JP2_CHANNEL_FORMAT_FLOAT'.
           [>>] Fixed-point inputs behave as if P were infinite in the above
                expressions, and x (signed) or x+0.5 (unsigned) must
                additionally be scaled by 2^{-I}, where I is the number of
                integer bits in the floating point representation.
           [>>] After conversion of integer bit patterns to floats,
                `JP2_CHANNEL_FORMAT_FLOAT' formatted inputs result
                in floating point values for which P can again be considered
                infinite in the expressions above.  Unsigned floats notionally
                have a nominal range from 0 to 1.0, but we internally subtract
                0.5 and then treat them exactly as above.  Signed floats
                notionally have a nominal range from -1.0 to +1.0, but we
                internally scale by 0.5 and then treat them exactly as above.
           [//]
           We finish by identifying some special cases where the "true-max"
           scaling policy applies regardless of whether this function or
           `set_white_stretch' are ever called -- the same is not true of the
           `true_zero' scaling attribute, which is attainable only by calling
           this function.  There are three such special cases, as follows:
           [>>] `JP2_CHANNEL_FORMAT_FLOAT'-formatted input samples are always
                mapped according to the "true-max" policy.
           [>>] `JP2_CHANNEL_FIXPOINT'-formatted input samples that have
                anything other than 0 integer bits are always mapped
                according to the "true-max" policy.  If the number of integer
                bits is 0, fixpoint-formatted input samples have almost
                exactly the same interpretation as regular integer-formatted
                data.
           [>>] Some variants of the `process' function produce floating point
                outputs.  These functions also behave as if "true-max" were
                asserted, regardless of how this or any other function is
                called.
           [//]
           To extend your understanding of how the various sample value
           scaling and offset algorithms are applied, you may find it
           instructive to review the documentation for the `kdu_channel_interp'
           structure and the `jp2_channels::get_colour_mapping' function.  In
           fact correct implementation of the policies described above relies
           upon the `kdu_channel_interp' structures found in the
           `kdu_channel_mapping::channel_interp' array retaining the values
           with which they are initialized by the various
           `kdu_channel_mapping::configure' functions, even though it is
           strictly possible for an application to modify these values.
      */
    void set_white_stretch(int white_stretch_precision)
      { this->white_stretch_precision = white_stretch_precision;
        this->want_true_zero = this->want_true_max = false; }
      /* [SYNOPSIS]
           This function is provided for legacy reasons only and is not
           recommended.  Its functionality has been subsumed and extended
           by the `set_true_scaling' function, which allows correct stretching
           and offset to accommodate natural white and zero points in all
           circumstances.  If you do call this function, the effect of any
           prior call to `set_true_scaling' is lost; conversely, if you call
           `set_true_scaling', the effect of any prior call to this function
           is lost.  As with `set_true_scaling', you should call this function
           only while the object is inactive -- i.e., before the first call to
           `start', or after the most recent call to `finish' and before any
           subsequent call to `start'.
           [//]
           The function sets up the "white stretch" mode which will become
           active when `start' is next called.  The same "white stretch" mode
           will be used in each subsequent call to `start' unless the present
           function is used to change it.
           [//]
           So, what is this "white stretch" mode, and what does it have to
           do with precision?  To explain this, we begin by explaining what
           happens if you set `white_stretch_precision' to 0 -- this is the
           default, which also corresponds to the object's behaviour prior
           to Kakadu Version 5.2 for backward compatibility.  Suppose the
           original image samples which were compressed had a precision of
           P bits/sample, as recorded in the codestream and/or reported by
           the `jp2_dimensions' interface to a JP2/JPX source.  The actual
           precision may differ from component to component, of course, but
           let's keep things simple for the moment).
           [//]
           For the purpose of colour transformations and conversion to
           different rendering precisions (as requested in the relevant call
           to `process'), Kakadu applies the uniform interpretation that
           unsigned quantities range from 0 (black) to 2^P (max intensity);
           signed quantities are assumed to lie in the nominal range of
           -2^{P-1} to 2^{P-1}.  Thus, for example:
           [>>] to render P=12 bit samples into a B=8 bit buffer, the object
                simply divides by 16 (downshifts by 4=P-B);
           [>>] to render P=4 bit samples into a B=8 bit buffer, the object
                multiplies by 16 (upshifts by 4=B-P); and
           [>>] to render P=1 bit samples into a B=8 bit buffer, the object
                multiplies by 128 (upshifts by 7=B-P).
           [//]
           This last example, reveals the weakness of a pure shifting scheme
           particularly well, since the maximum attainable value in the 8-bit
           rendering buffer from a 1-bit source will be 128, as opposed to the
           expected 255, leading to images which are darker than would be
           suspected.  Nevertheless, this policy has merits.  One important
           merit is that the original sample values are preserved exactly,
           so long as the rendering buffer has precision B >= P.  The
           policy also has minimal computational complexity and produces
           visually excellent results except for very low bit-depth images.
           Moreover, very low bit-depth images are often used to index
           a colour palette, which performs the conversion from low to high
           precision in exactly the manner prescribed by the image content
           creator.
           [//]
           Nevertheless, it is possible that low bit-depth image components
           are used without a colour palette and that applications will want
           to render them to higher bit-depth displays.  The most obvious
           example of this is palette-less bi-level images, but another
           example is presented by images which have a low precision associated
           alpha (opacity) channel.  To render such images in the most
           natural way, unsigned sample values with P < B should ideally be
           stretched by (1-2^{-B})/(1-2^{-P}), prior to rendering, in
           recognition of the fact that the maximum display output value is
           (1-2^{-B}) times the assumed range of 2^B, while the maximum
           intended component sample value (during content creation) was
           probably (1-2^{-P}) times the assumed range of 2^P.
           It is not entirely clear whether the same interpretations are
           reasonable for signed sample values, but the function extends the
           policy to both signed and unsigned samples by fixing the lower
           bound of the nominal range and stretching the length of the range
           by (1-2^{-B})/(1-2^{-P}).  In fact, the internal representation of
           all component samples is signed so as to optimally exploit the
           dynamic range of the available word sizes.
           [//]
           To facilitate the stretching procedure described above, the
           present function allows you to specify the value of B that you
           would like to be applied in the stretching process.  This is the
           value of the `white_stretch_precision' argument.  Normally, B
           will coincide with the value of the `precision_bits' argument
           you intend to supply to the `process' function (often B=8).
           However, you can use a different value.  In particular, using a
           smaller value for B here will reduce the likelihood that white
           stretching is applied (also reducing the accuracy of the stretch,
           of course), which may represent a useful computational saving
           for your application.  Also, when the target rendering precision is
           greater than 8 bits, it is unclear whether your application will
           want stretching or not -- if so, stretching to B=8 bits might be
           quite accurate enough for you, differing from the optimal stretch
           value by at most 1 part in 256.  The important thing to note is
           that stretching will occur only when the component sample precision
           P is less than B, so 8-bit original samples will be completely
           unchanged if you specify B <= 8.  In particular, the default
           value of B=0 means that no stretching will ever be applied.
           [//]
           As a final note, we point out that the white stretching algorithm
           described here is ignored if you use floating point versions of
           the `process' function, or if the source data samples had the
           special `JP2_CHANNEL_FORMAT_FLOAT' data format -- integer
           bit-patterns re-interpeted as floats.  In both cases, the `true_max'
           scaling (stretching) behaviour defined by `set_true_scaling' is
           always employed, regardless of whether that function or this one
           is called.
         [ARG: white_stretch_precision]
           This argument holds the target stretching precision, B, explained
           in the discussion above.  White stretching will be applied only
           to image components (or JP2/JPX/MJ2 palette outputs) whose
           nominated precision P is less than B.
      */
    void set_interpolation_behaviour(float max_overshoot,
                                     int zero_overshoot_threshold)
      {
        if (zero_overshoot_threshold < 1) zero_overshoot_threshold = 1;
        if (max_overshoot < 0.0F) max_overshoot = 0.0F;
        this->max_interp_overshoot = max_overshoot;
        this->zero_overshoot_interp_threshold = zero_overshoot_threshold;
      }
      /* [SYNOPSIS]
           This function allows you to customize the way in which interpolation
           is performed when the `expand_denominator' and `expand_numerator'
           arguments supplied to `start' are not identical.  Interpolation is
           generally performed using 6-tap kernels which are optimized to
           approximate ideal band-limited filters whose bandwidth is
           a fraction BW of the full Nyquist bandwidth, where BW=max{ratio,1.0}
           and `ratio' is the expansion ratio determined from
           the `expand_denominator' and `expand_numerator' arguments passed to
           `start'.  Unfortunately, these interpolation operators generally
           have BIBO (Bounded-Input-Bounded-Output) gains significantly greater
           than 1, which means that they can expand the dynamic range of
           certain types of inputs.  The effect on hard step edges can be
           particularly annoying, especially under large expansion factors.
           [//]
           This function allows you to suppress such overshoot/undershoot
           artefacts based upon the amount of expansion which is occurring.
           The internal mechanism adjusts the interpolation kernels
           so that the maximum amount of undershoot or overshoot associated
           with interpolation of a step edge, is limited to at most
           `max_overshoot' (as a fraction of the dynamic range of the edge).
           Moreover, this limit on the maximum overshoot/undershoot is
           linearly decreased as a function of the expansion `ratio',
           starting from an expansion ratio of 1.0 and continuing until the
           expansion ratio reaches the `zero_overshoot_threshold' value.  At
           that point, the interpolation strategy reduces to bi-linear
           interpolation; i.e., the individual interpolation kernels used for
           interpolation in the horizontal and vertical directions are reduced
           to 2 taps with positive coefficients.  Such kernels have a BIBO
           gain which is exactly equal to their DC gain of 1.0.
           [//]
           By specifying a `zero_overshoot_threshold' which is less than
           or equal to 1, you can force the use of bi-linear interpolation for
           all expansive interpolation processes.  One side effect of this is
           that the interpolation may be substantially faster,
           depending upon the underlying machine architecture and details of
           the available SIMD accelerations which are available.
           [//]
           Changes associated with calls to this function may not have any
           effect until the next call to `start'.  If the function is never
           called, the current default values for the two parameters are
           `max_overshoot'=0.4 and `zero_overshoot_threshold'=2.0.
      */
    KDU_AUX_EXPORT void
      set_quality_limiting(const kdu_quality_limiter *limiter,
                           float hor_ppi, float vert_ppi);
      /* [SYNOPSIS]
           This function allows you to install or change a quality limiter.
           The `limiter' object is always copied here (if non-NULL), so you
           do not need to worry about preserving it.  The internal limiter
           is then configured by the next call to `start' and passed to
           `kdu_codestream::apply_input_restrictions'.  Configuration involves
           the following aspects:
           [>>] Component weights are automatically installed to reflect the
                different amounts of interpolation that might be applied to
                each component, following the algorithm described with the
                definition of the `kdu_quality_limiter' constructor.
           [>>] Additional component weights are introduced to reflect any
                colour conversion operations that are performed by this object,
                to the extent that these can be figured out.
           [>>] Chromaticity properties of the output components are configured
                to reflect the colour space from which content is being
                rendered.
           [>>] The `hor_ppi' and `vert_ppi' information supplied here,
                which describe the display resolution associated with the
                reference component (potentially resampled using the rational
                scaling factors supplied to `start'), is converted to reflect
                the resolution of a hypothetical image component that has
                not been sub-sampled (as explained in connection with the
                `kdu_quality_limiter::set_display_resolution' function.
           [//]
           The above customizations are applied each time `start' is invoked,
           so you only need to call this function again if the `limiter'
           itself changes (e.g., different quality target, or a different
           extension of the `kdu_quality_limiter' class) or if the reference
           component display resolution changes.  In most applications, the
           generated content is rendered pixel-for-pixel to the display,
           so that the display resolution remains fixed, even though
           rational scaling factors and number of discarded DWT levels may
           change.
         [ARG: hor_ppi]
           As with `kdu_quality_limiter::set_display_resolution', this
           function takes a display resolution, measured in pixels-per-inch,
           at an assumed viewing distance of 15cm.
           [//]
           Unlike `kdu_quality_limiter::set_display_resolution', the
           `hor_ppi' value supplied here refers to the resolution of the
           pixels generated by interpolating each of the image components
           by the relevant amounts.  Equivalently, it is the resolution of
           the "reference component" that `start' uses, after application of
           the rational resampling factors that may be supplied to `start'.
           This means that the PPI values supplied here correspond exactly
           to those of the rendered samples produced by the region
           decompressor.
           [//]
           We generally recommend adopting a resolution of 200 ppi if the
           rendered samples are to be displayed pixel-for-pixel on a
           "non-retina" device and 400 ppi if they are to be displayed
           pixel-for-pixel on a "retina" device.  As explained with
           `kdu_quality_limiter::set_display_resolution', the reasoning behind
           this recommendation is that most desktop retina displays offer
           about 200 dpi and are rarely viewed more closely than 30cm, while
           mobile phone retina displays may offer 300 to 400 dpi and are rarely
           viewed more closely than 20cm.  Similarly, non-retina desktop and
           mobile displays tend to offer around 100 and 150 dpi resolutions,
           respectively, both of which are equivalent to 200 ppi at 15cm.
           [//]
           This argument supplies horizontal resolution information, while
           `vert_ppi' supplies vertical display resolution, both of which are
           normally the same.  If either or both of these arguments is <= 0,
           there will be no visual weighting applied during the quality
           limiting process.
         [ARG: vert_ppi]
           Same as `hor_ppi', but in the vertical direction.  In almost all
           practical cases, the horizontal and vertical display resolutions
           are likely to be identical.
      */
    KDU_AUX_EXPORT bool
      start(kdu_codestream codestream, kdu_channel_mapping *mapping,
            int single_component, int discard_levels, int max_layers,
            kdu_dims region, kdu_coords expand_numerator,
            kdu_coords expand_denominator=kdu_coords(1,1), bool precise=false,
            kdu_component_access_mode access_mode=KDU_WANT_OUTPUT_COMPONENTS,
            bool fastest=false, kdu_thread_env *env=NULL,
            kdu_thread_queue *env_queue=NULL);
      /* [SYNOPSIS]
           Prepares to decompress a new region of interest.  The actual
           decompression work is performed incrementally through successive
           calls to `process' and terminated by a call to `finish'.  This
           incremental processing strategy allows the decompression work to
           be interleaved with other tasks, e.g. to preserve the
           responsiveness of an interactive application.  There is no need
           to process the entire region of interest established by the
           present function call; `finish' may be called at any time, and
           processing restarted with a new region of interest.  This is
           particularly helpful in interactive applications, where an
           impatient user's interests may change before processing of an
           outstanding region is complete.
           [//]
           If `mapping' is NULL, a single image component is to be
           decompressed, whose index is identified by `single_component'.
           Otherwise, one or more image components must be decompressed and
           subjected to potentially quite complex mapping rules to generate
           channels for display; the relevant components and mapping
           rules are identified by the `mapping' object.
           [//]
           The `region' argument identifies the image region which is to be
           decompressed.  This region is defined with respect to a
           `rendering canvas'.  The rendering canvas might be identical to the
           high resolution canvas associated with the code-stream, but it is
           often different -- see below.
           [//]
           The `expand_numerator' and `expand_denominator' arguments identify
           the amount by which the first channel described by `mapping' (or
           the single image component if `mapping' is NULL) should be
           expanded to obtain its representation on the rendering canvas.
           To be more precise, let Cr be the index of the reference image
           component (the first one identified by `mapping' or the
           single image component if `mapping' is NULL).  Also, let (Px,Py)
           and (Sx,Sy) denote the upper left hand corner and the dimensions,
           respectively, of the region returned by `kdu_codestream::get_dims',
           for component Cr.  Note that these dimensions take into account
           the effects of the `discard_levels' argument, as well as any
           orientation adjustments created by calls to
           `kdu_codestream::change_appearance'.  The location and dimensions
           of the image on the rendering canvas are then given by
                    ( ceil(Px*Nx/Dx-Hx), ceil(Py*Ny/Dy-Hy) )
           and
                    ( ceil((Px+Sx)*Nx/Dx-Hx)-ceil(Px*Nx/Dx-Hx),
                      ceil((Py+Sy)*Ny/Dy-Hy)-ceil(Py*Ny/Dy-Hy) )
           respectively, where (Nx,Ny) are found in `expand_numerator',
           (Dx,Dy) are found in `expand_denominator', and (Hx,Hy) are
           intended to represent approximately "half pixel" offsets.
           Specifically, Hx=floor((Nx-1)/2)/Dx and Hy=floor((Ny-1)/2)/Dy.
           Since the above formulae can be tricky to reproduce precisely,
           the `get_rendered_image_dims' function is provided to learn the
           exact location and dimensions of the image on the rendering canvas.
           Moreover, since you may wish to provide corresponding
           transformations for other purposes (e.g., finding the locations on
           the rendering grid of regions of interest expressed in codestream
           canvas coordinates, or vice-versa), the `find_render_dims'
           function is provided for general use, along with related functions
           `find_codestream_point' and `find_render_point'.
           [//]
           You are required to ensure only that the `expand_numerator'
           and `expand_denominator' coordinates are strictly positive.
           From Kakadu version 6.2.2 it is no longer necessary to ensure
           that the `expand_numerator' coordinates are at least as large as
           `expand_denominator'.  In fact, you can use these parameters to
           implement almost any amount of expansion or reduction,
           but as a result you should use `get_rendered_image_dims' first to
           verify that you are not reducing the image down to nothing.
           [//]
           In the extreme case, if you select `expand_denominator' values
           which are vastly larger than the `expand_numerator', an error may
           be generated through `kdu_error'.  For example, if the product of
           the x and y members of `expand_denominator' exceeds the product
           of the `expand_numerator' x and y values by more than 2^24, an
           error is likely to be generated.  This is because such massive
           reduction factors may exceed the dynamic range of the internal
           implementation.  The actual limit on the amount of resolution
           reduction which can be realized depends upon factors such as the
           relative sub-sampling of the various image components required
           to construct colour channels.
           [//]
           As of Kakadu version 6.2.2, expansion and reduction are implemented
           in a disciplined manner, using aliasing suppressing interpolation
           kernels, designed using rigorous criteria.  We no longer use
           pixel replication or bilinear interpolation, which produce poor
           image quality.  However, we do allow you to configure some aspects
           of the interpolation process using the `set_interpolation_behaviour'
           function -- this allows you to suppress ringing artifacts which can
           be noticeable around step edges, especially under large expansion
           factors.
           [//]
           Note carefully that this function calls the
           `kdu_codestream::apply_input_restrictions' function, which will
           destroy any restrictions you may previously have imposed.  This
           may also alter the current component access mode interpretation.
           For this reason, the function provides you with a separate
           `access_mode' argument which you can set to one of
           `KDU_WANT_CODESTREAM_COMPONENTS' or `KDU_WANT_OUTPUT_COMPONENTS',
           to control the way in which image component indices should be
           interpreted and whether or not multi-component transformations
           defined at the level of the code-stream should be performed.
         [RETURNS]
           False if a fatal error occurred and an exception (usually generated
           from within the error handler associated with `kdu_error') was
           caught.  In this case, you need not call `finish', but you should
           generally destroy the codestream interface passed in here.
           [//]
           You can avoid errors which might be generated by inappropriate
           `expand_denominator'/`expand_numerator' parameters by first
           calling `get_safe_expansion_factors'.
         [ARG: codestream]
           Interface to the internal code-stream management machinery.  Must
           have been created (see `codestream.create') for input.
         [ARG: mapping]
           Points to a structure whose contents identify the relationship
           between image components and the channels to be rendered.  The
           interpretation of these image components depends upon the
           `access_mode' argument.  Any or all of the image
           components may be involved in rendering channels; these might be
           subjected to a palette lookup operation and/or specific colour space
           transformations.  The channels need not all represent colour, but
           the initial `mapping->num_colour_channels' channels do represent
           the colour of a pixel.
           [//]
           If the `mapping' pointer is NULL, only one image component is to
           be rendered, as a monochrome image; the relevant component's
           index is supplied via the `single_component' argument.
         [ARG: single_component]
           Ignored, unless `mapping' is NULL, in which case this argument
           identifies the image component (starting from 0) which
           is to be rendered as a monochrome image.  The interpretation of
           this image component index depends upon the `access_mode'
           argument.
         [ARG: discard_levels]
           Indicates the number of highest resolution levels to be discarded
           from each image component's DWT.  Each discarded level typically
           halves the dimensions of each image component, although note that
           JPEG2000 Part-2 supports downsampling factor styles in which
           only one of the two dimensions might be halved between levels.
           Recall that the rendering canvas is determined by applying the
           expansion factors represented by `expand_numerator' and
           `expand_denominator' to the dimensions of the reference image
           component, as it appears after taking the number of discarded
           resolution levels (and any appearance changes) into account.  Thus,
           each additional discarded resolution level serves to reduce the
           dimensions of the entire image as it would appear on the rendering
           canvas.
         [ARG: max_layers]
           Maximum number of quality layers to use when decompressing
           code-stream image components for rendering.  The actual number of
           layers which are available might be smaller or larger than this
           limit and may vary from tile to tile.
         [ARG: region]
           Location and dimensions of the new region of interest, expressed
           on the rendering canvas.  This region should be located
           within the region returned by `get_rendered_image_dims'.
         [ARG: expand_numerator]
           Numerator of the rational expansion factors which are applied to
           the reference image component.
         [ARG: expand_denominator]
           Denominator of the rational expansion factors which are applied to
           the reference image component.
         [ARG: precise]
           Setting this argument to true encourages the implementation to
           use higher precision internal representations when decompressing
           image components.  The precision of the internal representation
           is not directly coupled to the particular version of the overloaded
           `process' function which is to be used.  The lower precision
           `process' functions may be used with higher precision internal
           computations, or vice-versa, although it is natural to couple
           higher precision computations with calls to a higher precision
           `process' function.
           [//]
           Note that a higher precision internal representation may be adopted
           even if `precise' is false, if it is deemed to be necessary for
           correct decompression of some particular image component.  Note
           also that higher precision can be maintained throughout the entire
           process, only for channels whose contents are taken directly from
           decompressed image components.  If any palette lookup, or colour
           space conversion operations are required, the internal precision
           for those channels will be reduced (at the point of conversion) to
           16-bit fixed-point with `KDU_FIX_POINT' fraction bits -- due to
           current limitations in the `jp2_colour_converter' object.
           [//]
           Of course, most developers may remain blissfully ignorant of such
           subtleties, since they relate only to internal representations and
           approximations.
         [ARG: fastest]
           Setting this argument to true encourages the implementation to use
           lower precision internal representations when decompressing image
           components, even if this results in the loss of image quality.
           The argument is ignored unless `precise' is false.  The argument
           is essentially passed directly to `kdu_multi_synthesis::create',
           along with the `precises' argument, as that function's
           `want_fastest' and `force_precise' arguments, respectively.
         [ARG: access_mode]
           This argument is passed directly through to the
           `kdu_codestream::apply_input_restrictions' function. It thus affects
           the way in which image components are interpreted, as found in the
           `single_component_idx' and `mapping' arguments.  For a detailed
           discussion of image component interpretations, consult the second
           form of the `kdu_codestream::apply_input_restrictions' function.
           It suffices here to mention that this argument must take one of
           the values `KDU_WANT_CODESTREAM_COMPONENTS' or
           `KDU_WANT_OUTPUT_COMPONENTS' -- for more applications the most
           appropriate value is `KDU_WANT_OUTPUT_COMPONENTS' since these
           correspond to the declared intentions of the original codestream
           creator.
         [ARG: env]
           This argument is used to establish multi-threaded processing.  For
           a discussion of the multi-threaded processing features offered
           by the present object, see the introductory comments to
           `kdu_region_decompressor'.  We remind you here, however, that
           all calls to `start', `process' and `finish' must be executed
           from the same thread, which is identified only in this function.
           For the single-threaded processing model used prior to Kakadu
           version 5.1, set this argument to NULL.
         [ARG: env_queue]
           This argument is ignored unless `env' is non-NULL, in which case
           a non-NULL `env_queue' means that all multi-threaded processing
           queues created inside the present object, by calls to `process',
           should be created as sub-queues of the identified `env_queue'.
           [//]
           One application for a non-NULL `env_queue' might be one which
           processes two frames of a video sequence in parallel.  There
           can be some benefit to doing this, since it can avoid the small
           amount of thread idle time which often appears at the end of
           the last call to the `process' function prior to `finish'.  In
           this case, each concurrent frame would have its own `env_queue', and
           its own `kdu_region_decompressor' object.  Moreover, the
           `env_queue' associated with a given `kdu_region_decompressor'
           object can be used to run a job which invokes the `start',
           `process' and `finish' member functions.  In this case, however,
           it is particularly important that the `start', `process' and
           `finish' functions all be called from within the execution of a
           single job, since otherwise there is no guarantee that they would
           all be executed from the same thread, whose importance has
           already been stated above.
           [//]
           Note that `env_queue' is not detached from the multi-threaded
           environment (identified by `env') when the current object is
           destroyed, or by `finish'.  It is, therefore, possible to have
           other `kdu_region_decompressor' objects (or indeed any other
           processing machinery) share this `env_queue'.       
      */
    KDU_AUX_EXPORT bool
      process(kdu_byte **channel_bufs, bool expand_monochrome,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              int precision_bits=8, bool measure_row_gap_in_pixels=true);
      /* [SYNOPSIS]
           This powerful function is the workhorse of a typical interactive
           image rendering application.  It is used to incrementally decompress
           an active region into identified portions of one or more
           application-supplied buffers.  Each call to the function always
           decompresses some whole number of lines of one or more horizontally
           adjacent tiles, aiming to produce roughly the number of samples
           suggested by the `suggested_increment' argument, unless that value
           is smaller than a single line of the current tile, or larger than
           the number of samples in a row of horizontally adjacent tiles.
           The newly rendered samples are guaranteed to belong to a rectangular
           region -- the function returns this region via the
           `new_region' argument.  This, and all other regions manipulated
           by the function are expressed relative to the `rendering canvas'
           (the coordinate system associated with the `region' argument
           supplied to the `start' member function).
           [//]
           Decompressed samples are automatically colour transformed,
           clipped, level adjusted, interpolated and colour appearance
           transformed, as necessary.  The result is a collection of
           rendered image pixels, each of which has the number of channels
           described by the `kdu_channel_mapping::num_channels' member of
           the `kdu_channel_mapping' object passed to `start' (or just one
           channel, if no `kdu_channel_mapping' object was passed to `start').
           The initial `kdu_channel_mapping::num_colour_channels' of these
           describe the pixel colour, while any remaining channels describe
           auxiliary properties such as opacity.  Other than these few
           constraints, the properties of the channels are entirely determined
           by the way in which the application configures the
           `kdu_channel_mapping' object.
           [//]
           The rendered channel samples are written to the buffers supplied
           via the `channel_bufs' array.  If `expand_monochrome' is false,
           this array must have exactly one entry for each of the channels
           described by the `kdu_channel_mapping' object supplied to `start'.
           The entries may all point to offset locations within a single
           channel-interleaved rendering buffer, or else they may point to
           distinct buffers for each channel; this allows the application to
           render to buffers with a variety of interleaving conventions.
           [//]
           If `expand_monochrome' is true and the number of colour channels
           (see `kdu_channel_mapping::num_colour_channels') is exactly equal
           to 1, the function automatically copies the single (monochrome)
           colour channel into 3 identical colour channels whose buffers
           appear as the first three entries in the `channel_bufs' array.
           This is a convenience feature to support direct rendering of
           monochrome images into 24- or 32-bpp display buffers, with the
           same ease as full colour images.  Your application is not obliged
           to use this feature, of course.
           [//]
           Each buffer referenced by the `channel_bufs' array has horizontally
           adjacent pixels separated by `pixel_gap'.  Regarding vertical
           organization, however, two distinct configurations are supported.
           [//]
           If `row_gap' is 0, successive rows of the region written into
           `new_region' are concatenated within each channe buffer, so that
           the row gap is effectively equal to `new_region.size.x' -- it is
           determined by the particular dimensions of the region processed
           by the function.  In this case, the `buffer_origin' argument is
           ignored.
           [//]
           If `row_gap' is non-zero, each channel buffer points to the location
           identified by `buffer_origin' (on the rendering canvas), and each
           successive row of the buffer is separated by the amount determined
           by `row_gap'.  In this case, it is the application's responsibility
           to ensure that the buffers will not be overwritten if any samples
           from the `incomplete_region' are written onto the buffer, taking
           the `buffer_origin' into account.  In particular, the
           `buffer_origin' must not lie beyond the first row or first column
           of the `incomplete_region'.  Note that the interpretation of
           `row_gap' depends upon the `measure_row_gap_in_pixels' argument.
           [//]
           On entry, the `incomplete_region' structure identifies the subset
           of the original region supplied to `start', which has not yet been
           decompressed and is still relevant to the application.  The function
           uses this information to avoid unnecessary processing of tiles
           which are no longer relevant, unless these tiles are already opened
           and being processed.
           [//]
           On exit, the upper boundary of the `incomplete_region' is updated
           to reflect any completely decompressed rows.  Once the region
           becomes empty, all processing is complete and future calls will
           return false.
           [//]
           The function may return true, with `new_region' empty.  This can
           happen, for example, when skipping over unnecessary tile or
           group of tiles.  The intent is to avoid the possibility that the
           caller might be forced to wait for an unbounded number of tiles to
           be loaded (possibly from disk) while hunting for one which has a
           non-empty intersection with the `incomplete_region'.  In general,
           the current implementation limits the number of new tiles which
           will be opened to one row of horizontally adjacent tiles.  In this
           way, a number of calls may be required before the function will
           return with `new_region' non-empty.
           [//]
           If the code-stream is found to offer insufficient DWT levels to
           support the `discard_levels' argument supplied to `start', the
           present function will return false, after which you should invoke
           `finish' and potentially start again with a different number of
           discard levels.
           [//]
           If the underlying code-stream is found to be sufficiently corrupt
           that the decompression process must be aborted, the current function
           will catch any exceptions thrown from an application supplied
           `kdu_error' handler, returning false prematurely.  This condition
           will be evident from the fact that `incomplete_region' is non-empty.
           You should still call `finish' and watch the return value from that
           function.
         [RETURNS]
           False if the `incomplete_region' becomes empty as a result of this
           call, or if a required tile is found to offer insufficient DWT
           levels to support the `discard_levels' values originally supplied to
           `start', or if an internal error occurs during code-stream data
           processing and an exception was thrown by the application-supplied
           `kdu_error' handler.  In any case, the correct response to a
           false return value is to call `finish' and check its return value.
           [//]
           If `finish' returns false, an error has occurred and
           you must close the `kdu_codestream' object (possibly re-opening it
           for subsequent processing, perhaps in a more resilient mode -- see
           `kdu_codestream::set_resilient').
           [//]
           If `finish' returns true, the incomplete region might not have
           been completed if the present function found that you were
           attempting to discard too many resolution levels or to flip an
           image which cannot be flipped, due to the use of certain packet
           wavelet decomposition structures.  To check for the former
           possibility, you should always check the value returned by
           `kdu_codestream::get_min_dwt_levels' after `finish' returns true.
           To check for the second possibility, you should also
           test the value returned by `kdu_codestream::can_flip', possibly
           adjusting the appearance of the codestream (via
           `kdu_codestream::change_appearance') before further processing.
           Note that values returned by `kdu_codestream::get_min_dwt_levels'
           and `kdu_codestream::can_flip' can become progressively more
           restrictive over time, as more tiles are visited.
           [//]
           Note carefully that this function may return true, even though it
           has decompressed no new data of interest to the application
           (`new_region' empty).  This is because a limited number of new tiles
           are opened during each call, and these tiles might not have any
           intersection with the current `incomplete_region' -- the application
           might have reduced the incomplete region to reflect changing
           interests.
         [ARG: channel_bufs]
           Array with `kdu_channel_mapping::num_channels' entries, or
           `kdu_channel_mapping::num_channels'+2 entries.  The latter applies
           only if `expand_monochrome' is true and
           `kdu_channel_mapping::num_colour_channels' is equal to 1.  If
           no `kdu_channel_mapping' object was passed to `start', the number
           of channel buffers is equal to 1 (if `expand_monochrome' is false)
           or 3 (if `expand_monochrome' is true).  The entries in the
           `channel_bufs' array are not modified by this function.
           [//]
           If any entry in the array is NULL, that channel will effectively
           be skipped.  This can be useful, for example, if the value of
           `kdu_channel_mapping::num_colour_channels' is larger than the
           number of channels produced by a colour transform supplied by
           `kdu_channel_mapping::colour_converter' -- for example, a CMYK
           colour space (`kdu_channel_mapping::num_colour_channels'=4)
           might be converted to an sRGB space, so that the 4'th colour
           channel, after conversion, becomes meaningless.
         [ARG: expand_monochrome]
           If true and the number of colour channels identified by
           `kdu_channel_mapping::num_colour_channels' is 1, or if no
           `kdu_channel_mapping' object was passed to `start', the function
           automatically copies the colour channel data into the first
           three buffers supplied via the `channel_bufs' array, and the second
           real channel, if any, corresponds to the fourth entry in the
           `channel_bufs' array.
         [ARG: pixel_gap]
           Separation between consecutive pixels, in each of the channel
           buffers supplied via the `channel_bufs' argument.
         [ARG: buffer_origin]
           Location, in rendering canvas coordinates, of the upper left hand
           corner pixel in each channel buffer supplied via the `channel_bufs'
           argument, unless `row_gap' is 0, in which case, this argument is
           ignored.
         [ARG: row_gap]
           If zero, rendered image lines will simply be concatenated into the
           channel buffers supplied via `channel_bufs', so that the line
           spacing is given by the value written into `new_region.size.x'
           upon return.  In this case, `buffer_origin' is ignored.  Otherwise,
           this argument identifies the separation between vertically adjacent
           pixels within each of the channel buffers.  If
           `measure_row_gap_in_pixels' is true, the number of samples
           between consecutive buffer lines is `row_gap'*`pixel_gap'.
           Otherwise, it is just `row_gap'.
         [ARG: suggested_increment]
           Suggested number of samples to decompress from the code-stream
           component associated with the first channel (or the single
           image component) before returning.  Of course, there will often
           be other image components which must be decompressed as well, in
           order to reconstruct colour imagery; the number of these samples
           which will be decompressed is adjusted in a commensurate fashion.
           Note that the decompressed samples may be subject to interpolation
           (if the `expand_numerator' and `expand_denominator' arguments
           supplied to the `start' member function represent expansion
           factors which are larger than 1); the `suggested_increment' value
           refers to the number of decompressed samples prior to any such
           interpolation.  Note also that the function is free to make up its
           own mind exactly how many samples it will process in the current
           call, which may vary from 0 to the entire `incomplete_region'.
           [//]
           For interactive applications, typical values for the
           `suggested_increment' argument might range from tens of thousands,
           to hundreds of thousands.
           [//]
           If `row_gap' is 0, and the present argument is 0, the only
           constraint will be that imposed by `max_region_pixels'.
         [ARG: max_region_pixels]
           Maximum number of pixels which can be written to any channel buffer
           provided via the `channel_bufs' argument.  This argument is
           ignored unless `row_gap' is 0, since only in that case is the
           number of pixels which can be written governed solely by the size
           of the buffers.  An error will be generated (through `kdu_error') if
           the supplied limit is too small to accommodate even a single line
           from the new region.  For this reason, you should be careful to
           ensure that `max_region_pixels' is at least as large as
           `incomplete_region.size.x'.
         [ARG: incomplete_region]
           Region on the rendering canvas which is still required by the
           application.  This region should be a subset of the region of
           interest originally supplied to `start'.  However, it may be much
           smaller.  The function works internally with a bank of horizontally
           adjacent tiles, which may range from a single tile to an entire
           row of tiles (or part of a row of tiles).  From within the current
           tile bank, the function decompresses lines as required
           to fill out the incomplete region, discarding any rows from already
           open tiles which do not intersect with the incomplete region.  On
           exit, the first row in the incomplete region will be moved down
           to reflect any completely decompressed image rows.  Note, however,
           that the function may decompress image data and return with
           `new_region' non-empty, without actually adjusting the incomplete
           region.  This happens when the function decompresses tile data
           which intersects with the incomplete region, but one or more tiles
           remain to the right of that region. Generally speaking, the function
           advances the top row of the incomplete region only when it
           decompresses data from right-most tiles which intersect with that
           region, or when it detects that the identity of the right-most
           tile has changed (due to the width of the incomplete region
           shrinking) and that the new right-most tile has already been
           decompressed.
         [ARG: new_region]
           Used to return the location and dimensions of the region of the
           image which was actually decompressed and written into the channel
           buffers supplied via the `channel_bufs' argument.  The region's
           size and location are all expressed relative to the same rendering
           canvas as the `incomplete_region' and `buffer_origin' arguments.
           Note that the region might be empty, even though processing is not
           yet complete.
         [ARG: precision_bits]
           Indicates the precision of the unsigned integers represented by
           each sample in each buffer supplied via the `channel_bufs' argument.
           This value need not bear any special relationship to the original
           bit-depth of the compressed image samples.  The rendered sample
           values are written into the buffer as B-bit unsigned integers,
           where B is the value of `precision_bits' and the most significant
           bits of the B-bit integer correspond to the most significant bits
           of the original image samples.  Normally, the value of this argument
           will be 8 so that the rendered data is always normalized for display
           on an 8-bit/sample device.  There may be more interest in selecting
           different precisions when using the second form of the overloaded
           `process' function that returns 16-bit results.
           [//]
           It is possible to supply a special value of 0 for this argument.
           In this case, a "default" set of precisions will be used (one for
           each channel).  If a `kdu_channel_mapping' object was supplied to
           `start', the `kdu_channel_mapping::default_rendering_precision' and
           `kdu_channel_mapping::default_rendering_signed' arrays are used
           to derive the default channel precisions, as well as per-channel
           information about whether the samples should be rendered as
           unsigned quantities or as 2's complement 8-bit integers.  That
           information, in turn, is typically initialized by one of the
           `kdu_channel_mapping::configure' functions to represent the
           native bit-depths and signed/unsigned properties of the original
           image samples (or palette indices); however, it may be explicitly
           overridden by the application.  This gives you enormous flexibility
           in choosing the way you want rendered sample bits to be
           represented.  If no `kdu_channel_mapping' object was supplied to
           `start', the default rendering precision and signed/unsigned
           characteristics are derived from the original properties of the
           image samples represented by the code-stream.
           [//]
           It is worth noting that the rendering precision, B, can actually
           exceed 8, either because `precision_bits' > 8, or because
           `precision_bits'=0 and the default rendering precisions, derived
           in the above-mentioned manner, exceed 8.  In this case, the
           function automatically truncates the rendered values to fit
           within the 8-bit representation associated with the `channel_bufs'
           array(s).  If B <= 8, the rendered values are truncated to fit
           within the B bits.  Where 2's complement output samples are
           written, they are truncated in the natural way and sign extended.
           [//]
           Finally, we note that the mapping of original compressed sample
           values to B-bit output values here, depends upon the default vs.
           "true-zero" and default vs. "true-max" mapping policies, as
           configured by `set_true_scaling' and discussed extensively in
           the accompanying documentation.  Basically, if you want samples
           whose original representation (prior to compression) is indicated
           by the codestream as having been signed to be mapped to a mid-level
           grey in the output, so that both +ve and -ve values can be
           visualized, you should use the default rather than "true-zero"
           mapping policy.  Then, if you are interested primarily in
           efficiency, keep the default rather than "true-max" policy, but if
           you are interested in the most accurate possible mapping between
           different precisions, select the "true-max" option.
         [ARG: measure_row_gap_in_pixels]
           If true, `row_gap' is interpreted as the number of whole pixels
           between consecutive rows in the buffer.  Otherwise, it is
           interpreted as the number of samples only.  The latter form can be
           useful when working with image buffers having alignment constraints
           which are not based on whole pixels (e.g., Windows bitmap buffers).
      */
    KDU_AUX_EXPORT bool
      process(kdu_uint16 **channel_bufs, bool expand_monochrome,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              int precision_bits=16, bool measure_row_gap_in_pixels=true);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `process' function, except
           that the channel buffers each hold 16-bit unsigned quantities.  As
           before, the `precision_bits' argument is used to control the
           representation written into each output sample.  If it is 0, default
           precisions are obtained, either from the `kdu_channel_mapping'
           object or from the codestream, as appropriate, and these defaults
           might also cause the 16-bit output samples to hold 2's complement
           signed quantities.  Also, as before, the precision requested
           explicitly or implicitly (via `precision_bits'=0) may exceed 16.
           In each case, the most natural truncation procedures are
           employed for out-of-range values, following the general
           strategy outlined in the comments accompanying the
           `precision_bits' argument in the first form of the `process'
           function.
      */
    KDU_AUX_EXPORT bool
      process(float **channel_buffer, bool expand_monochrome,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              bool normalize=true, bool measure_row_gap_in_pixels=true,
              bool always_clip_outputs=true);
      /* [SYNOPSIS]
           Same as the first and second forms of the overloaded `process'
           function, except that the channel buffers employ a floating point
           representation.  As with those functions, it is possible to select
           arbitrary `channel_offsets' so that the organization of `buffer'
           need not necessarily be interleaved component-by-component.
           [//]
           To get data with sufficient accuracy to deserve a floating point
           representation, you might like to set the `precise' argument to
           true in the call to `start'.
           [//]
           One important difference between this function and the
           integer-based `process' functions is that some form of white
           stretching is always employed.  Specifically, the nominal range
           of the original sample values used to create each channel is
           stretched to the full dynamic range associated with the floating
           point result.  This is equivalent to asserting the "true-max" mode
           that is described in connection with the `set_true_scaling'
           function, irrespective of whether that function is explicitly
           called or not.  You can call `set_white_stretch' if you like, but
           it is strongly discouraged, since that may result int the internal
           concatenation of two white stretching steps, one of which is
           redundant.
           [//]
           A second important difference between this function and the
           integer-based ones is that this function allows the extra head-room
           associated with non-default source pixel formats suitable for
           HDR content to be preserved, by passing `always_clip_outputs'=false.
           The integer-based `process' functions always clip their outputs to
           the expected nominal range, so that the processed content can be
           used immediately by a rendering application.  The floating-point
           `process' functions do the same thing if `always_clip_outputs'=true
           or if the original source content is identified as having
           integer-formatted samples (typical).  If a JPX pixel format (pxfm)
           box was present, the original samples can be identified as
           integer bit-patterns that represent floating point numbers, or
           fixed-point numbers with varying numbers of fraction bits
           (equivalently varying numbers of integer bits).  If
           `always_clip_outputs' is false, float-formatted source content or
           fixpoint-formatted content with a non-zero number of integer bits
           will not be clipped to the expected bounds.  This is because these
           more exotic input formats can be used to represent super-luminous
           regions of HDR content (or perhaps just scientific data with
           unknown numerical range) that should not be clipped.
           [//]
           If `normalize' is true, the output dynamic range is from 0 to 1.0,
           inclusive; thus, if original samples had an 8-bit dynamic range
           (as originally compressed), they would effectively be divided by
           255.0 to produce normalized floating point output samples.
           [//]
           If `normalize' is false, the output dynamic range depends upon the
           default rendering precision and signed/unsigned information found
           in the `kdu_channel_mapping' object supplied to `start'.  For more
           on this, see the `normalize' argument.
         [ARG: normalize]
           If true, the `buffer' samples are all normalized to lie within
           the closed interval [0,1].  Exactly what this means when the
           original samples are identified as signed, rather than unsigned,
           depends on the `true_zero' mode, as explained with the
           `set_true_scaling' function.  In the default case, the original
           signed values in the range -2^{P-1} to 2^{P-1}-1 are mapped directly
           to the interval [0,1], so that an input value of zero maps to a
           floating point output value of 0.5.  I `true_zero' is asserted,
           however, originally signed values in the range 0 to 2^{P-1} are
           mapped to [0,1], truncating all negative values to 0.  For a
           much more thorough discussion of "true-zero" mapping, read the
           extensive comments found with the `set_true_scaling' function.
           [//]
           If the `normalize' argument is false, the behaviour is similar to
           that described above, except that the nominal range of the source
           data is stretched into the range [Pmin, Pmax], where Pmin and Pmax
           depend on the `kdu_channel_mapping::default_rendering_precision' and
           `kdu_channel_mapping::default_rendering_signed' arrays found in
           the `kdu_channel_mapping' object supplied to `start'.  Specifically,
           if P is the default rendering precision for the channel, as
           determined from the `kdu_channel_mapping' object, and the default
           rendering information identifies unsigned sample values,
           [Pmin,Pmax] is equal to [0,(2^P)-1].
           [//]
           In the event that the default rendering information identifies
           signed sample values for the channel, [Pmin,Pmax] is set to
           [-2^{P-1},2^{P-1}-1], except where P=0.  In the P=0 case,
           [Pmin,Pmax]=[-0.5,0.5] for signed samples, while [Pmin,Pmax]=[0,1]
           for unsigned samples.
           [//]
           Note that the default rendering information in `kdu_channel_mapping'
           is initialized by the `kdu_channel_mapping::configure' function
           to represent the native bit-depths and signed/unsigned
           properties of the original image samples (or palette indices).
           For sample data whose identified pixel format is
           `JP2_CHANNEL_FORMAT_FLOAT' (see `jp2_channels::set_colour_mapping'
           and/or `kdu_channel_interp'), the default rendering precision is
           set to 0, which yields the nominal ranges [0,1] and [-0.5,0.5] for
           non-normalized unsigned and signed outputs, as explained above.
           For sample data whose identified as `JP2_CHANNEL_FORMAT_FIXPOINT'
           (see `jp2_channels::set_colour_mapping' and/or
           `kdu_channel_interp'), the default rendering precision is set to F,
           where F is the number of fraction bits, being P minus the number of
           integer bits.  This is consistent with the notion that values
           outside the expected range should be those having extra-ordinary
           luminosity, requiring the head-room afforded by the non-default
           pixel format.
           [//]
           Regardless of how `configure' initializes the
           `kdu_channel_mapping::default_rendering_precision' and
           `kdu_channel_mapping::default_rendering_signed' arrays,
           however, you can always explicitly set your own values so as
           to control the nominal range associated with the values returned
           by this function.  If no `kdu_channel_mapping' object was supplied
           to `start', the default rendering precision and signed/unsigned
           characteristics are derived from the original properties of the
           image samples represented by the code-stream.
         [ARG: always_clip_outputs]
           Defaults to true, so that renderers not capable of handling
           unexpected values need not worry.  However, as explained above,
           if you pass false for this argument, this function will preserve
           the extra head-room associated with the special float or fixpoint
           formats that can be signalled via the JPX pixel format (pxfm) box.
           This is mainly important for HDR rendering applications, or
           applications intended to render scientific data that might have
           been compressed as true floats, without any natural range bounds.
      */
    KDU_AUX_EXPORT bool
      process(kdu_int32 *buffer, kdu_coords buffer_origin,
              int row_gap, int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region);
      /* [SYNOPSIS]
           This function is equivalent to invoking the first form of the
           overloaded `process' function, with four 8-bit channel buffers
           and `precision_bits'=8.
           [>>] The 1st channel buffer (nominally RED) corresponds to the 2nd
                most significant byte of each integer in the `buffer'.
           [>>] The 2nd channel buffer (nominally GREEN) corresponds to the 2nd
                least significant byte of each integer in the `buffer'.
           [>>] The 3rd channel buffer (nominally BLUE) corresponds to the
                least significant byte of each integer in the `buffer'.
           [>>] The 4th channel buffer (nominally ALPHA) corresponds to the
                most significant byte of each integer in the `buffer'.
           [//]
           The above can also be summarized as follows:
           [>>] On a big-endian architecture, the bytes in each pixel have an
                ARGB order;
           [>>] On a little-endian architecture, the order is BGRA.
           [//]
           If the source has only one colour channel, the second and third
           channel buffers are copied from the first channel buffer (same as
           the `expand_monochrome' feature found in other forms of the 
           `process' function).
           [//]
           If there are more than 3 colour channels, only the first 3 will
           be transferred to the supplied `buffer' (same as setting
           the `max_colour_channels' feature found in other forms to the
           `process' function to 3).
           [//]
           If there is no alpha information, the fourth channel buffer (most
           significant byte of each integer) is set to 0xFF.
           [//]
           Apart from convenience, one reason for providing this function
           in addition to the other, more general forms of the `process'
           function, is that it is readily mapped to alternate language
           bindings, such as Java.
           [//]
           Another important reason for providing this form of the `process'
           function is that missing alpha data is always synthesized (with
           0xFF), so that a full 32-bit word is aways written for each pixel.
           This can significantly improve memory access efficiency on some
           common platforms.  It also allows for efficient internal
           implementations in which the rendered channel data is written
           in aligned 16-byte chunks wherever possible, without any need
           to mask off unused intermediate values.  To fully exploit this
           capability, you are strongly recommended to suppy a 16-byte
           aligned `buffer'.
      */
    KDU_AUX_EXPORT bool
      process(kdu_byte *buffer, int *channel_offsets,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              int precision_bits=8, bool measure_row_gap_in_pixels=true,
              int expand_monochrome=0, int fill_alpha=0,
              int max_colour_channels=0);
      /* [SYNOPSIS]
           Same as the first form of the overloaded `process' function, except
           that the channel buffers are interleaved into a single buffer.  It
           is actually possible to select arbitrary `channel_offsets'
           so that the organization of `buffer' need not necessarily be
           interleaved component-by-component.
           [//]
           One reason for providing this function in addition to the second
           form of the `process' function, is that this version is readily
           mapped to alternate language bindings, such as Java.
           [//]
           The other reason for providing this function is to provide a
           convenient method to fill in additional entries in an interleaved
           buffer with reasonable values.  This is achieved
           with the aid of the `expand_monochrome' and `fill_alpha'
           arguments.
         [ARG: channel_offsets]
           Array with at least `kdu_channel_mapping::num_channels' entries.
           Each entry specifies the offset in samples to the first sample of
           the associated channel data.  The number of entries in this array
           may need to be larger than the number of actual available channels,
           depending upon the `expand_monochrome' and `fill_alpha' arguments,
           as described below.
         [ARG: expand_monochrome]
           If the number of colour channels is 1, as determined from the
           value of `kdu_channel_mapping::num_colour_channels' in the
           `mapping' object passed to `start', or by the fact that no
           `mapping' object at all was passed to `start', this argument
           may be used to expand the single colour channel into a total of
           `expand_monochrome' copies.  Specifically, if `expand_monochrome'
           exceeds 1, an additional `expand_monochrome'-1 copies of the
           single available colour channel are created -- these are expected
           to correspond to entries 1 through `expand_monochrome'-1 in the
           `channel_offsets' array.  Any additional (non-colour) channels
           which are being decompressed are then considered to correspond to
           entries starting from index `expand_monochrome' in the
           `channel_offsets' array.
           [//]
           If, however, there are multiple colour channels in the original
           description supplied to `start', this argument is ignored.  For
           this case, it is not at all obvious how extra colour components
           ought to be synthesized -- the application should perhaps arrange
           for the `kdu_channel_mapping' object passed to `start' to
           describe the additional colour channels, along with a suitable
           colour transform.
         [ARG: fill_alpha]
           This argument may be used to synthesize "opaque" alpha channels,
           where no corresponding alpha description is available. Specifically,
           if `fill_alpha' exceeds the number of non-colour channels
           specified in the description supplied to `start' (this is
           0 if no `mapping' object is supplied to `start'; otherwise, it is
           the difference between `kdu_channel_mapping::num_channels' and
           `kdu_channel_mapping::num_colour_channels'), additional channels
           are synthesized as required and filled with the maximum
           value associated with the numeric representation (depends on
           `precision_bits').  In this case, the non-colour channels written
           by this funtion correspond to entries C through C+`fill_alpha'-1 in
           the `channel_offsets' array, where C is the number of colour
           channels being written (taking into account the effect of
           `expand_monochrome' and `max_colour_channels').
           [//]
           Note that there is no default information in the
           `kdu_channel_mapping' object supplied to `start' from which to
           determine a fill value, in the case that `precision_bits' is 0.
           For this reason, the fill value is taken to be 0xFF if
           `precision_bits' is not in the range 1 to 7.
         [ARG: max_colour_channels]
           If positive, this is the maximum number of colour channels that
           will be transferred to the elements of `buffer'.  If
           `mapping->num_colour_channels' > `max_colour_channels', channels
           `max_colour_channels' through to `mapping->num_colour_channels'-1
           are implicitly skipped and followed by any non-colour channels or
           `fill_alpha' channels.  In most applications, you should pass 3
           for this argument, so that you don't have to worry about the
           possibility of receiving an unexpected number of colour
           channels for some colour spaces (e.g., 4 for CMYK).  For reasons
           of backward compatibility, the argument defaults to 0, in which
           case there is no limit imposed on the number of colour channels.
       */
    KDU_AUX_EXPORT bool
      process(kdu_uint16 *buffer, int *channel_offsets,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              int precision_bits=16, bool measure_row_gap_in_pixels=true,
              int expand_monochrome=0, int fill_alpha=0,
              int max_colour_channels=0);
      /* [SYNOPSIS]
           Same as the second form of the overloaded `process' function, except
           that the channel buffers are interleaved into a single buffer.  It
           is actually possible to select arbitrary `channel_offsets'
           so that the organization of `buffer' need not necessarily be
           interleaved component-by-component.
           [//]
           One reason for providing this function in addition to the second
           form of the `process' function, is that this version is readily
           mapped to alternate language bindings, such as Java.
           [//]
           The other reason for providing this function is to provide a
           convenient method to fill in additional entries in an interleaved
           buffer with reasonable values.  This is achieved
           with the aid of the `expand_monochrome' and `fill_alpha'
           arguments.
         [ARG: channel_offsets]
           Array with at least `kdu_channel_mapping::num_channels' entries.
           Each entry specifies the offset in samples to the first sample of
           the associated channel data.  The number of entries in this array
           may need to be larger than the number of actual available channels,
           depending upon the `expand_monochrome' and `fill_alpha' arguments,
           as described below.
         [ARG: expand_monochrome]
           If the number of colour channels is 1, as determined from the
           value of `kdu_channel_mapping::num_colour_channels' in the
           `mapping' object passed to `start', or by the fact that no
           `mapping' object at all was passed to `start', this argument
           may be used to expand the single colour channel into a total of
           `expand_monochrome' copies.  Specifically, if `expand_monochrome'
           exceeds 1, an additional `expand_monochrome'-1 copies of the
           single available colour channel are created -- these are expected
           to correspond to entries 1 through `expand_monochrome'-1 in the
           `channel_offsets' array.  Any additional (non-colour) channels
           which are being decompressed are then considered to correspond to
           entries starting from index `expand_monochrome' in the
           `channel_offsets' array.
           [//]
           If, however, there are multiple colour channels in the original
           description supplied to `start', this argument is ignored.  For
           this case, it is not at all obvious how extra colour components
           ought to be synthesized -- the application should perhaps arrange
           for the `kdu_channel_mapping' object passed to `start' to
           describe the additional colour channels, along with a suitable
           colour transform.
         [ARG: fill_alpha]
           This argument may be used to synthesize "opaque" alpha channels,
           where no corresponding alpha description is available. Specifically,
           if `fill_alpha' exceeds the number of non-colour channels
           specified in the description supplied to `start' (this is
           0 if no `mapping' object is supplied to `start'; otherwise, it is
           the difference between `kdu_channel_mapping::num_channels' and
           `kdu_channel_mapping::num_colour_channels'), additional channels
           are synthesized as required and filled with the maximum
           value associated with the numeric representation (depends on
           `precision_bits').  In this case, the non-colour channels written
           by this funtion correspond to entries C through C+`fill_alpha'-1 in
           the `channel_offsets' array, where C is the number of colour
           channels being written (taking into account the effect of
           `expand_monochrome' and `max_colour_channels').
           [//]
           Note that there is no default information in the
           `kdu_channel_mapping' object supplied to `start' from which to
           determine a fill value, in the case that `precision_bits' is 0.
           For this reason, the fill value if taken to be 0xFFFF if
           `precision_bits' is not in the range 1 to 15.
         [ARG: max_colour_channels]
           If positive, this is the maximum number of colour channels that
           will be transferred to the elements of `buffer'.  If
           `mapping->num_colour_channels' > `max_colour_channels', channels
           `max_colour_channels' through to `mapping->num_colour_channels'-1
           are implicitly skipped and followed by any non-colour channels or
           `fill_alpha' channels.  In most applications, you should pass 3
           for this argument, so that you don't have to worry about the
           possibility of receiving an unexpected number of colour
           channels for some colour spaces (e.g., 4 for CMYK).  For reasons
           of backward compatibility, the argument defaults to 0, in which
           case there is no limit imposed on the number of colour channels.
       */
    KDU_AUX_EXPORT bool
      process(float *buffer, int *channel_offsets,
              int pixel_gap, kdu_coords buffer_origin, int row_gap,
              int suggested_increment, int max_region_pixels,
              kdu_dims &incomplete_region, kdu_dims &new_region,
              bool normalize=true, bool measure_row_gap_in_pixels=true,
              int expand_monochrome=0, int fill_alpha=0,
              int max_colour_channels=0, bool always_clip_outputs=true);
      /* [SYNOPSIS]
           Same as the third form of the overloaded `process' function, except
           that the channel buffers are interleaved into a single buffer.  It
           is actually possible to select arbitrary `channel_offsets'
           so that the organization of `buffer' need not necessarily be
           interleaved component-by-component.
           [//]
           One reason for providing this function in addition to the third
           form of the `process' function, is that this version is readily
           mapped to alternate language bindings, such as Java.
           [//]
           The other reason for providing this function is to provide a
           convenient method to fill in additional entries in an interleaved
           buffer with reasonable values.  This is achieved
           with the aid of the `expand_monochrome' and `fill_alpha'
           arguments.
         [ARG: channel_offsets]
           Array with at least `kdu_channel_mapping::num_channels' entries.
           Each entry specifies the offset in samples to the first sample of
           the associated channel data.  The number of entries in this array
           may need to be larger than the number of actual available channels,
           depending upon the `expand_monochrome' and `fill_alpha' arguments,
           as described below.
         [ARG: expand_monochrome]
           If the number of colour channels is 1, as determined from the
           value of `kdu_channel_mapping::num_colour_channels' in the
           `mapping' object passed to `start', or by the fact that no
           `mapping' object at all was passed to `start', this argument
           may be used to expand the single colour channel into a total of
           `expand_monochrome' copies.  Specifically, if `expand_monochrome'
           exceeds 1, an additional `expand_monochrome'-1 copies of the
           single available colour channel are created -- these are expected
           to correspond to entries 1 through `expand_monochrome'-1 in the
           `channel_offsets' array.  Any additional (non-colour) channels
           which are being decompressed are then considered to correspond to
           entries starting from index `expand_monochrome' in the
           `channel_offsets' array.
           [//]
           If, however, there are multiple colour channels in the original
           description supplied to `start', this argument is ignored.  For
           this case, it is not at all obvious how extra colour components
           ought to be synthesized -- the application should perhaps arrange
           for the `kdu_channel_mapping' object passed to `start' to
           describe the additional colour channels, along with a suitable
           colour transform.
         [ARG: fill_alpha]
           This argument may be used to synthesize "opaque" alpha channels,
           where no corresponding alpha description is available. Specifically,
           if `fill_alpha' exceeds the number of non-colour channels
           specified in the description supplied to `start' (this is
           0 if no `mapping' object is supplied to `start'; otherwise, it is
           the difference between `kdu_channel_mapping::num_channels' and
           `kdu_channel_mapping::num_colour_channels'), additional channels
           are synthesized as required and filled with the value 1.0.  In this
           case, the non-colour channels written by this funtion correspond to
           entries C through C+`fill_alpha'-1 in the `channel_offsets' array,
           where C is the number of colour channels being written (taking into
           account the effect of `expand_monochrome' and
           `max_colour_channels').
           [//]
           Note that there is no default information in the
           `kdu_channel_mapping' object supplied to `start' from which to
           determine a fill value, in the case that `normalize' is false.
           For this reason, the fill value is always taken to be 1.0F.
         [ARG: max_colour_channels]
           If positive, this is the maximum number of colour channels that
           will be transferred to the elements of `buffer'.  If
           `mapping->num_colour_channels' > `max_colour_channels', channels
           `max_colour_channels' through to `mapping->num_colour_channels'-1
           are implicitly skipped and followed by any non-colour channels or
           `fill_alpha' channels.  In most applications, you should pass 3
           for this argument, so that you don't have to worry about the
           possibility of receiving an unexpected number of colour
           channels for some colour spaces (e.g., 4 for CMYK).  For reasons
           of backward compatibility, the argument defaults to 0, in which
           case there is no limit imposed on the number of colour channels.
      */
    KDU_AUX_EXPORT bool
      finish(kdu_exception *failure_exception=NULL, bool do_cs_terminate=true);
      /* [SYNOPSIS]
           Every call to `start' must be matched by a call to `finish';
           however, you may call `finish' prematurely.  This allows processing
           to be terminated on a region whose intersection with a display
           window has become too small to justify the effort.
         [RETURNS]
           If the function returns false, a fatal error has occurred in the
           underlying codestream management machinery and you must destroy
           the codestream object (use `kdu_codestream::destroy').  You will
           probably also have to close the relevant compressed data source
           (e.g., using `kdu_compressed_source::close').  This should clean
           up all the resources correctly, in preparation for subsequently
           opening a new code-stream for further decompression and rendering
           work.
           [//]
           Otherwise, if the `process' function returned false leaving a
           non-empty incompletely processed region, one of two non-fatal
           errors has occurred:
           [>>] The number of `discard_levels' supplied to `start' has been
                found to exceed the number of DWT levels offered by some tile.
           [>>] The codestream was configured to flip the image geometry, but
                a Part-2 packet wavelet decomposition structure has been
                employed in some tile which is fundamentally non-flippable
                (only some of the Part-2 packet decomposition styles have
                this property).
           [//]
           The correct response to either of these events is documented in
           the description of the return value from `process'.  Note that
           the `start' function may always be re-invoked with a smaller number
           of `discard_levels' and a larger value of `expand_denominator', to
           synthesize the required resolution.
         [ARG: failure_exception]
           If non-NULL and the function returns false, this argument is used
           to return a copy of the Kakadu exception which was caught (probably
           during an earlier call to `process').  In addition to exceptions of
           type `kdu_exception', exceptions of type `std::bad_alloc' are also
           caught and converted into the special value, `KDU_MEMORY_EXCEPTION',
           so that they can be passed across this interface.  If your code
           rethrows the exception, it may be best to test for this special
           value and rethrow such exceptions as `std::bad_alloc()'.
         [ARG: do_cs_terminate]
           This argument has been added in Kakadu version 7 to simplify
           cleanup of background codestream processing.  The argument is
           relevant only if a non-NULL `kdu_thread_env' reference was passed
           to the `start' function.  In that case, the most significant
           impact of the new multi-threading architecture in Kakadu version 7
           is that you are required to call `kdu_thread_env::cs_terminate'
           before destroying a codestream.  There is no harm in invoking
           `kdu_thread_env::cs_terminate' at any point after processing has
           stopped within a codestream; if you wish to continue using the
           codestream, the internal multi-threaded context will be regenerated
           automatically, although this may involve some overhead.  To save
           you the trouble of figuring out when you might need to invoke
           `kdu_thread_env::cs_terminate' yourself, if the `do_cs_terminate'
           argument is true (default), it will be called for you.  If you
           do care about the small overhead associated with regenerating
           a codestream's multi-threaded context (assuming you may re-use
           the codestream), you can set this argument to false and then
           you must remember to call `kdu_thread_env::cs_terminate' yourself
           before `kdu_codestream::destroy'.
      */
  protected: // Implementation helpers which may be useful to extended classes
    bool process_generic(int sample_bytes, int pixel_gap,
                         kdu_coords buffer_origin, int row_gap,
                         int suggested_increment, int max_region_pixels,
                         kdu_dims &incomplete_region, kdu_dims &new_region);
      /* All `process' functions call here after initializing the
         `channel_bufs' array.  `sample_bytes' must be equal to 1 (for 8-bit
         samples), 2 (for 16-bit samples) or 4 (for floating-point samples).
         Note that `row_gap' here is measured in samples, not pixels. */
  private: // Implementation helpers
    void set_num_channels(int num);
      /* Convenience function to allocate and initialize the `channels' array
         as required to manage a total of `num' channels.  Sets `num_channels'
         and `num_colour_channels' both equal to `num' before returning.
         Also, initializes the `kdrd_channel::native_precision' value to 0
         and `kdrd_channel::native_signed' to false for each channel.  Note
         that the `fix16_palette' and `float_palette' arrays are always
         created or re-allocated, but any new entries in these arrays are
         initialized to NULL. */
    kd_supp_local::kdrd_component *add_component(int comp_idx);
      /* Adds a new component to the `components' array, if necessary,
         returning a pointer to that component. */
    bool start_tile_bank(kd_supp_local::kdrd_tile_bank *bank,
                         kdu_long suggested_tile_mem,
                         kdu_dims incomplete_region);
      /* This function uses `suggested_tile_mem' to determine the number
         of new tiles which should be opened as part of the new tile bank.  The
         value of `suggested_tile_mem' represents the suggested total memory
         associated with open tiles, where memory is estimated (very crudely)
         as the tile width multiplied by the minimum of 100 and the tile
         height.  The function opens horizontally adjacent tiles until the
         suggested tile memory is exhausted, except that it exercises some
         intelligence in avoiding a situation in which only a few small tiles
         are left on a row.
         [//]
         On entry, the supplied `bank' must have its `num_tiles' member set
         to 0, meaning that it is closed.  Tiles are opened starting from
         the tile identified by the `next_tile_idx' member, which is
         adjusted as appropriate.
         [//]
         Note that this function does not make any adjustments to the
         `render_dims' member or any of the members in the `components' or
         `channels' arrays.  To prepare the state of those objects, the
         `make_tile_bank_current' function must be invoked on a tile-bank
         which has already been started.
         [//]
         If the function is unable to open new tiles due to restrictions on
         the number of DWT levels or decomposition-structure-imposed
         restrictions on the allowable flipping modes, the function returns
         false.
         [//]
         The last argument is used to identify tiles which have no overlap
         with the current `incomplete_region'.  The supplied region is
         identical to that passed into the `process' functions on entry.  If
         some tiles that would normally be opened are found to lie outside
         this region, they are closed immediately and not included in the
         count recorded in `kdrd_tile_bank::num_tiles'.  As a result, the
         function may return with `bank->num_tiles'=0, which is not a failure
         condition, but simply means that there is nothing to decompress right
         now, at least until the application calls `process' again. */
    void close_tile_bank(kd_supp_local::kdrd_tile_bank *bank);
      /* Use this function to close all tiles and tile-processing engines
         associated with the tile-bank.  This is normally the current tile
         bank, unless processing is being terminated prematurely. */
    void make_tile_bank_current(kd_supp_local::kdrd_tile_bank *bank,
                                kdu_dims incomplete_region);
      /* Call this function any time after starting a tile-bank, to make it
         the current tile-bank and appropriately configure the `render_dims'
         and various tile-specific members of the `components' and `channels'
         arrays. */
  private: // Data
    bool precise;
    bool fastest;
    bool want_true_zero; // See `set_true_scaling'
    bool want_true_max; // See `set_true_scaling'
    int white_stretch_precision; // Persistent mode setting
    int zero_overshoot_interp_threshold; // See `set_interpolation_behaviour'
    float max_interp_overshoot; // See `set_interpolation_behaviour'
    kdu_thread_env *env; // NULL for single-threaded processing
    kdu_thread_queue local_env_queue; // Descended from `env_queue' in `start'
    kdu_long next_queue_bank_idx; // Used by `start_tile_bank'
    kd_supp_local::kdrd_tile_bank *tile_banks; // Array of 2 tile banks
    kd_supp_local::kdrd_tile_bank *current_bank; // Points to one of the
                                                 // `tile_banks'
    kd_supp_local::kdrd_tile_bank *background_bank; // Non-NULL if next
                                                    // bank already started
    kdu_dims ref_comp_dims; // Reference component is channel 0's source comp
    int min_tile_bank_width; // Measured relative in ref component samples, no
       // tile-bank should be smaller than this -- addresses the rare
       // possibility that a component might be so heavily sub-sampled and
       // tiles so small that we might be tempted to build a tile-bank that
       // produces no samples for one component.
    kdu_codestream codestream;
    bool codestream_failure; // True if an exception generated in `process'.
    kdu_exception codestream_failure_exception; // Save exception 'til `finish'
    int discard_levels; // Value supplied in last call to `start'
    kdu_dims valid_tiles;
    kdu_coords next_tile_idx; // Index of next tile, not yet opened in any bank
    kdu_sample_allocator aux_allocator; // For palette indices, interp bufs etc
    kdu_coords original_expand_numerator; // Copied from call to `start'
    kdu_coords original_expand_denominator; // Copied from call to `start'
    kdu_dims full_render_dims; // Dimensions of whole image on rendering canvas
    kdu_dims render_dims; // Dims of current tile bank on rendering canvas
    int max_channels; // So we can tell if `channels' array needs reallocating
    int num_channels;
    int num_colour_channels;
    kd_supp_local::kdrd_channel *channels;
    jp2_colour_converter *colour_converter; // For colour conversion ops
    float cc_normalized_max; // Where colour conversion occurs (see below)
    int max_components; // So we can tell if `components' needs re-allocating
    int num_components; // Num valid elements in each of the next two arrays
    kd_supp_local::kdrd_component *components;
    int *component_indices; // Used with `codestream.apply_input_restrictions'
    int max_channel_bufs; // Size of `channel_bufs' array
    int num_channel_bufs; // Set from within `process'
    kd_supp_local::kdrd_channel_buf *channel_bufs;
    kdu_quality_limiter *limiter;       // Local copies of params last passed
    float limiter_ppi_x, limiter_ppi_y; // to `set_quality_limiting'.
  };
  /* Implementation related notes:
        The object is capable of working with a single tile at a time, or
     a larger bank of concurrent tiles.  The choice between these options,
     depends on the size of the tiles, compared to the surface being rendered
     and the suggested processing increment supplied in calls to `process'.
     The `kdrd_tile_bank' structure is used to maintain a single bank of
     simultaneously open (active) tiles.  These must all be horizontally
     adjacent.  It is possible to start one bank of tiles in the background
     while another is being processed, which can be useful when a
     multi-threading environment is available, to minimize the likelihood
     that processor resources become idle.  Most of the rendering operations
     performed outside of decompression itself work on the current tile bank
     as a whole, rather than individual tiles.  For this reason, the dynamic
     state information found in `render_dims' and the various members of the
     `components' and `channels' arrays reflect the dimensions and other
     properties of the current tile bank, rather than just one tile (of
     course a tile bank may consist of only 1 tile, but it may consist of
     an entire row of tiles, or any collection of horizontally adjacent tiles).
        The `render_dims' member identifies the dimensions and location of the
     region associated with the `current_tile_bank', expressed on the
     rendering canvas coordinate system (not the code-stream canvas coordinate
     system). Whenever a new line of channel data is produced, the
     `render_dims.pos.y' field is incremented and `render_dims.size.y' is
     decremented.
        As explained with `kdu_channel_mapping', where colour conversion
     occurs the converted representation, prior to final transfer to output
     buffers, is considered always to be unsigned, with a natural zero point
     (black) of 0 (in the absence of any level adjustment) and with the
     same precision as that of the first original source channel prior to
     colour conversion.  The `cc_normalized_max' member holds the
     `kdrd_channel::interp_normalized_max' member for this first channel, if
     there is a non-NULL `colour_converter'; otherwise, `cc_normalized_max'
     holds a negative value.  The `cc_normalized_max' member is used when
     customizing channel transfer functions and preparing parameters for
     the final channel transfer to output samples.
  */

} // namespace kdu_supp

#endif // KDU_REGION_DECOMPRESSOR_H
