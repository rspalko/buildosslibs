/*****************************************************************************/
// File: kdu_clientx.cpp [scope = APPS/KDU_CLIENT]
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
  Implements the `kdu_clientx' client context translator object
******************************************************************************/

#include <assert.h>
#include <math.h>
#include "kdu_utils.h"
#include "clientx_local.h"
#include "jp2_shared.h"

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
     kdu_error _name("E(kdu_clientx.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_clientx.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu Client:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu Client:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


/* ========================================================================= */
/*                             kdcx_stream_mapping                           */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdcx_stream_mapping::parse_ihdr_box                  */
/*****************************************************************************/

void
  kdcx_stream_mapping::parse_ihdr_box(jp2_input_box *ihdr)
{
  if (num_components > 0)
    return; // Already parsed one
  kdu_uint32 height, width;
  kdu_uint16 nc;
  if (ihdr->read(height) && ihdr->read(width) && ihdr->read(nc))
    {
      num_components = (int) nc;
      size.x = (int) width;
      size.y = (int) height;
    }
}

/*****************************************************************************/
/*                      kdcx_stream_mapping::parse_cmap_box                  */
/*****************************************************************************/

void
  kdcx_stream_mapping::parse_cmap_box(jp2_input_box *cmap)
{
  if (component_indices != NULL)
    return; // Already parsed one
  num_channels = ((int) cmap->get_remaining_bytes()) >> 2;
  component_indices = new int[num_channels];
  for (int n=0; n < num_channels; n++)
    {
      kdu_uint16 cmp;
      kdu_byte mtyp, pcol;
      if (!(cmap->read(cmp) && cmap->read(mtyp) && cmap->read(pcol)))
        component_indices[n] = 0; // Easy way to defer error condition
      else
        component_indices[n] = (int) cmp;
    }
}

/*****************************************************************************/
/*                          kdcx_stream_mapping::finish                      */
/*****************************************************************************/

bool
  kdcx_stream_mapping::finish(kdcx_stream_mapping *defaults)
{
  if (is_complete)
    return false;
  if (jpch_box.exists())
    { 
      if (!jpch_box.is_complete())
        return false;
      while ((jpch_sub.exists() || jpch_sub.open(&jpch_box)) &&
             jpch_sub.is_complete())
        { 
          if (jpch_sub.get_box_type() == jp2_image_header_4cc)
            parse_ihdr_box(&jpch_sub);
          else if (jpch_sub.get_box_type() == jp2_component_mapping_4cc)
            parse_cmap_box(&jpch_sub);
          jpch_sub.close();
        }
      if (!jpch_sub)
        { 
          jpch_box.close();
          jpch_closed = true;
        }
    }
  
  if (num_components == 0)
    { 
      if (!defaults->is_complete)
        return false;
      num_components = defaults->num_components;
      size = defaults->size;
    }
  if (component_indices == NULL)
    { 
      if (!defaults->is_complete)
        return false;
      if (defaults->component_indices != NULL)
        { // Copy defaults
          num_channels = defaults->num_channels;
          component_indices = new int[num_channels];
          for (int n=0; n < num_channels; n++)
            component_indices[n] = defaults->component_indices[n];
        }
      else
        { // Components in 1-1 correspondence with channels
          num_channels = num_components;
          component_indices = new int[num_channels];
          for (int n=0; n < num_channels; n++)
            component_indices[n] = n;
        }
    }
  
  is_complete = true;
  return true;
}


/* ========================================================================= */
/*                             kdcx_layer_mapping                            */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdcx_layer_mapping::parse_cdef_box                   */
/*****************************************************************************/

void
  kdcx_layer_mapping::parse_cdef_box(jp2_input_box *cdef)
{
  if (channel_indices != NULL)
    return; // Already parsed one
  kdu_uint16 num_descriptions;
  if (!cdef->read(num_descriptions))
    return;
  num_channels = (int) num_descriptions;
  channel_indices = new int[num_channels];

  int n, k;
  for (n=0; n < num_channels; n++)
    {
      kdu_uint16 channel_idx, typ, assoc;
      if (!(cdef->read(channel_idx) && cdef->read(typ) && cdef->read(assoc)))
        channel_indices[n] = 0; // Easiest way to defer error condition
      else
        channel_indices[n] = (int) channel_idx;
    }

  // Sort the channel indices into increasing order
  bool done = false;
  while (!done)
    { // Use simple bubble sort
      done = true;
      for (n=1; n < num_channels; n++)
        if (channel_indices[n] < channel_indices[n-1])
          { // Swap
            done = false;
            k = channel_indices[n];
            channel_indices[n] = channel_indices[n-1];
            channel_indices[n-1] = k;
          }
    }

  // Remove redundant elements
  for (n=1; n < num_channels; n++)
    if (channel_indices[n] == channel_indices[n-1])
      { // Found redundant channel
        num_channels--;
        for (k=n; k < num_channels; k++)
          channel_indices[k] = channel_indices[k+1];
      }
}

/*****************************************************************************/
/*                      kdcx_layer_mapping::parse_creg_box                   */
/*****************************************************************************/

void
  kdcx_layer_mapping::parse_creg_box(jp2_input_box *creg)
{
  if (members != NULL)
    return;
  kdu_uint16 xs=0, ys=0;
  if (!(creg->read(xs) && creg->read(ys) && (xs > 0) && (ys > 0)))
    { KDU_ERROR(e,0); e <<
        KDU_TXT("Malformed codestream registration box encountered "
        "in JPX compositing layer header box.");
    }
  reg_precision.x = (int) xs;
  reg_precision.y = (int) ys;
  num_members = ((int) creg->get_remaining_bytes()) / 6;
  members = new kdcx_layer_member[num_members];
  for (int n=0; n < num_members; n++)
    {
      kdcx_layer_member *mem = members + n;
      kdu_uint16 cdn=0;
      kdu_byte xr=0, yr=0, xo=0, yo=0;
      if (!(creg->read(cdn) && creg->read(xr) && creg->read(yr) &&
            creg->read(xo) && creg->read(yo) && (xr > 0) && (yr > 0)))
        { KDU_ERROR(e,1); e <<
            KDU_TXT("Malformed codestream registration box "
            "encountered in JPX compositing layer header box.");
        }
      mem->codestream_idx = (int) cdn;
      mem->reg_offset.x = (int) xo;
      mem->reg_offset.y = (int) yo;
      mem->reg_subsampling.x = (int) xr;
      mem->reg_subsampling.y = (int) yr;
    }
}

/*****************************************************************************/
/*                      kdcx_layer_mapping::parse_opct_box                   */
/*****************************************************************************/

void
  kdcx_layer_mapping::parse_opct_box(jp2_input_box *opct)
{
  have_opct_box = true;
  kdu_byte otyp;
  if (!opct->read(otyp))
    return;
  if (otyp < 2)
    have_opacity_channel = true;
}

/*****************************************************************************/
/*                      kdcx_layer_mapping::parse_colr_box                   */
/*****************************************************************************/

void
  kdcx_layer_mapping::parse_colr_box(jp2_input_box *colr)
{
  if (num_colours > 0)
    return; // Already have a value.
  assert(colr->get_box_type() == jp2_colour_4cc);
  kdu_byte meth, prec_val, approx;
  if (!(colr->read(meth) && colr->read(prec_val) && colr->read(approx) &&
        (approx <= 4) && (meth >= 1) && (meth <= 4)))
    return; // Malformed colour description box.
  kdu_uint32 enum_cs;
  if ((meth == 1) && colr->read(enum_cs))
    switch (enum_cs) {
      case 0:  num_colours=1; break; // JP2_bilevel1_SPACE
      case 1:  num_colours=3; break; // space=JP2_YCbCr1_SPACE
      case 3:  num_colours=3; break; // space=JP2_YCbCr2_SPACE
      case 4:  num_colours=3; break; // space=JP2_YCbCr3_SPACE
      case 9:  num_colours=3; break; // space=JP2_PhotoYCC_SPACE
      case 11: num_colours=3; break; // space=JP2_CMY_SPACE
      case 12: num_colours=4; break; // space=JP2_CMYK_SPACE
      case 13: num_colours=4; break; // space=JP2_YCCK_SPACE
      case 14: num_colours=3; break; // space=JP2_CIELab_SPACE
      case 15: num_colours=1; break; // space=JP2_bilevel2_SPACE
      case 16: num_colours=3; break; // space=JP2_sRGB_SPACE
      case 17: num_colours=1; break; // space=JP2_sLUM_SPACE
      case 18: num_colours=3; break; // space=JP2_sYCC_SPACE
      case 19: num_colours=3; break; // space=JP2_CIEJab_SPACE
      case 20: num_colours=3; break; // space=JP2_esRGB_SPACE
      case 21: num_colours=3; break; // space=JP2_ROMMRGB_SPACE
      case 22: num_colours=3; break; // space=JP2_YPbPr60_SPACE
      case 23: num_colours=3; break; // space=JP2_YPbPr50_SPACE
      case 24: num_colours=3; break; // space=JP2_esYCC_SPACE
      default: // Unrecognized colour space.
        return;
      }
}

/*****************************************************************************/
/*                        kdcx_layer_mapping::finish                         */
/*****************************************************************************/

bool
  kdcx_layer_mapping::finish(kdcx_layer_mapping *defaults,
                             kdcx_context_mappings *owner)
{
  if (is_complete)
    return false;
  
  if (jplh_box.exists())
    { 
      if (!jplh_box.is_complete())
        return false;
      while ((jplh_sub.exists() || jplh_sub.open(&jplh_box)) &&
             jplh_sub.is_complete())
        { 
          if (!jplh_sub.is_complete())
              break;
          if (jplh_sub.get_box_type() == jp2_channel_definition_4cc)
            parse_cdef_box(&jplh_sub);
          else if (jplh_sub.get_box_type() == jp2_registration_4cc)
            parse_creg_box(&jplh_sub);
          else if (jplh_sub.get_box_type() == jp2_opacity_4cc)
            parse_opct_box(&jplh_sub);
          else if ((jplh_sub.get_box_type() == jp2_colour_group_4cc) &&
                   !cgrp_box)
            { 
              cgrp_box.transplant(jplh_sub);
              assert(!jplh_sub);
            }
          jplh_sub.close();
        }
      if (!jplh_sub)
        { 
          jplh_box.close();
          jplh_closed = true;
        }
    }
  
  if (cgrp_box.exists())
    { 
      if (!cgrp_box.is_complete())
        return false;
      while ((cgrp_sub.exists() || cgrp_sub.open(&cgrp_box)) &&
             cgrp_sub.is_complete())
        { 
          if (cgrp_sub.get_box_type() == jp2_colour_4cc)
            parse_colr_box(&cgrp_sub);
          cgrp_sub.close();
          if (num_colours > 0)
            break; // No need to parse any more colour sub-boxes
        }
      if (!cgrp_sub)
        cgrp_box.close();
    }
  
  if (members == NULL)
    { // One codestream whose index is equal to that of the compositing layer
      assert(rel_layer_idx >= 0);
      int stream_idx = rel_layer_idx;
      kdcx_entity_container *container = owner->container;
      if (container != NULL)
        { 
          stream_idx = rel_layer_idx + container->first_base_layer;
          // This is the absolute codestream index
          if (stream_idx >= container->num_top_codestreams)
            { 
              if ((stream_idx -= container->first_base_codestream) < 0)
                { KDU_ERROR(e,0x15011301); e <<
                  KDU_TXT("Invalid Compositing Layer Header box found "
                          "within a Compositing Layer Extensions box.  Since "
                          "no Codestream Registration box is supplied, the "
                          "compositing layer must use a codestream with the "
                          "same absolute index; however, the associated "
                          "codestream is neither a top-level codestream, nor "
                          "one of the codestreams defined within the "
                          "Compositing Layer Extensions box.");
                }
              stream_idx += container->num_top_codestreams;
            }
        }
      num_members = 1;
      members = new kdcx_layer_member[1];
      members->codestream_idx = stream_idx;
      reg_precision = kdu_coords(1,1);
      members->reg_subsampling = reg_precision;
    }

  layer_size = kdu_coords(0,0); // Find layer size by intersecting its members
  int n, m, total_channels=0;
  for (m=0; m < num_members; m++)
    { 
      kdcx_layer_member *mem = members + m;
      kdcx_stream_mapping *stream = owner->stream_lookup(mem->codestream_idx);
      if ((stream == NULL) || !stream->is_complete)
        return false; // We will have to come back here
      kdu_coords stream_size = stream->size;
      stream_size.x *= mem->reg_subsampling.x;
      stream_size.y *= mem->reg_subsampling.y;
      stream_size += mem->reg_offset;
      if ((m == 0) || (stream_size.x < layer_size.x))
        layer_size.x = stream_size.x;
      if ((m == 0) || (stream_size.y < layer_size.y))
        layer_size.y = stream_size.y;
      total_channels += stream->num_channels;
    }
  layer_size.x = ceil_ratio(layer_size.x,reg_precision.x);
  layer_size.y = ceil_ratio(layer_size.y,reg_precision.y);

  if (num_colours == 0)
    { 
      if (!defaults->is_complete)
        return false;
      num_colours = defaults->num_colours; // Inherit any colour box info
    }
  if (num_colours <= 0)
    { // No information about number of colours could be deduced; let's have
      // a reasonably conservative guess
      num_colours = total_channels;
      if (num_colours > 3)
        num_colours = 3;
      if (num_colours < 2)
        num_colours = 1;
    }

  if (channel_indices == NULL)
    { 
      if (!defaults->is_complete)
        return false;
      if ((defaults->channel_indices != NULL) && !have_opct_box)
        { // Copy default cdef info
          num_channels = defaults->num_channels;
          channel_indices = new int[num_channels];
          for (n=0; n < num_channels; n++)
            channel_indices[n] = defaults->channel_indices[n];
        }
      else
        { // Must be using initial set of `num_colours' channels
          num_channels = num_colours + ((have_opacity_channel)?1:0);
          if (num_channels > total_channels)
            num_channels = total_channels;
          channel_indices = new int[num_channels];
          for (n=0; n < num_channels; n++)
            channel_indices[n] = n;
        }
    }
  
  // Determine the image components in each codestream member
  total_channels = 0;
  int chan = 0; // Indexes first unused entry in `channel_indices' array
  for (m=0; m < num_members; m++)
    { 
      kdcx_layer_member *mem = members + m;
      kdcx_stream_mapping *stream = owner->stream_lookup(mem->codestream_idx);
      assert((stream != NULL) && stream->is_complete);
      if (chan == num_channels)
        break; // No more channels, and hence no more member components
      mem->component_indices = new int[stream->num_channels]; // Max value
      for (n=0; n < stream->num_channels; n++, total_channels++)
        {
          if (chan == num_channels)
            break;
          if (channel_indices[chan] == total_channels)
            { // Add corresponding image component
              mem->component_indices[mem->num_components++] =
                stream->component_indices[n];
              chan++;
            }
          else
            assert(channel_indices[chan] > total_channels);
        }
    }

  is_complete = true;
  return true;
}


/* ========================================================================= */
/*                             kdcx_context_mappings                         */
/* ========================================================================= */

/*****************************************************************************/
/*                  kdcx_context_mappings::get_num_members                   */
/*****************************************************************************/

int kdcx_context_mappings::get_num_members(int base_context_idx, int rep_idx,
                                           const int remapping_ids[])
{
  assert((base_context_idx >= 0) &&
         (base_context_idx < num_compositing_layers));
  kdcx_layer_mapping *layer = layer_refs[base_context_idx];
  assert((layer != NULL) && layer->is_complete);
  return layer->num_members;
}

/*****************************************************************************/
/*                   kdcx_context_mappings::get_codestream                   */
/*****************************************************************************/

int
  kdcx_context_mappings::get_codestream(int base_context_idx, int rep_idx,
                                        const int remapping_ids[],
                                        int member_idx)
{
  assert((base_context_idx >= 0) &&
         (base_context_idx < num_compositing_layers));
  kdcx_layer_mapping *layer = layer_refs[base_context_idx];
  assert((layer != NULL) && layer->is_complete);
  if ((member_idx < 0) || (member_idx >= layer->num_members))
    return -1;
  int cs_idx = layer->members[member_idx].codestream_idx;
  if ((container == NULL) || (cs_idx < container->num_top_codestreams))
    return cs_idx;
  cs_idx += container->first_base_codestream - container->num_top_codestreams;
  cs_idx += rep_idx*container->num_base_codestreams;
  return cs_idx;
}

/*****************************************************************************/
/*                  kdcx_context_mappings::get_components                    */
/*****************************************************************************/

const int *
  kdcx_context_mappings::get_components(int base_context_idx, int rep_idx,
                                        const int remapping_ids[],
                                        int member_idx, int &num_components)
{
  assert((base_context_idx >= 0) &&
         (base_context_idx < num_compositing_layers));
  num_components = 0;
  kdcx_layer_mapping *layer = layer_refs[base_context_idx];
  assert((layer != NULL) && layer->is_complete);
  if ((member_idx < 0) || (member_idx >= layer->num_members))
    return NULL;
  num_components = layer->members[member_idx].num_components;
  return layer->members[member_idx].component_indices;
}

/*****************************************************************************/
/*                  kdcx_context_mappings::perform_remapping                 */
/*****************************************************************************/

bool
  kdcx_context_mappings::perform_remapping(int base_context_idx, int rep_idx,
                                           const int remapping_ids[],
                                           int member_idx,
                                           kdu_coords &resolution,
                                           kdu_dims &region)
{
  assert((base_context_idx >= 0) &&
         (base_context_idx < num_compositing_layers));
  kdcx_layer_mapping *layer = layer_refs[base_context_idx];
  assert((layer != NULL) && layer->is_complete);
  if ((member_idx < 0) || (member_idx >= layer->num_members))
    return false;
  kdcx_layer_member *member = layer->members + member_idx;
  kdcx_stream_mapping *stream = stream_lookup(member->codestream_idx);
  assert((stream != NULL) && stream->is_complete);
  if ((stream->size.x <= 0) || (stream->size.y <= 0))
    return false; // Should not happen, but we don't want to crash
  
  if ((layer->layer_size.x <= 0) || (layer->layer_size.y <= 0))
    { region.size = region.pos = kdu_coords(0,0); }
  else if ((remapping_ids[0] < 0) || (remapping_ids[1] < 0) ||
           (num_comp_instructions < 1) ||
           (composited_size.x <= 0) || (composited_size.y <= 0))
    { // View window expressed relative to compositing layer registration grid
      double region_off, res_val, res_factor;
      kdu_coords region_lim = region.pos+region.size;
      
      region_off =
        ((double) member->reg_offset.x * (double) resolution.x /
         ((double) layer->reg_precision.x * (double) layer->layer_size.x));
      region.pos.x -= (int) ceil(region_off);
      region_lim.x -= (int) floor(region_off);
      res_val = ((double) resolution.x) *
        (((double) stream->size.x * (double) member->reg_subsampling.x) /
         ((double) layer->reg_precision.x * (double) layer->layer_size.x));
      if (res_val < 1.0) res_val = 1.0; // Just in case
      resolution.x = (int) ceil(res_val);
      res_factor = resolution.x / res_val; // Capture impact of res rounding
      region.pos.x = (int) floor(region.pos.x*res_factor);
      region_lim.x = (int) ceil(region_lim.x*res_factor);
      region.size.x = region_lim.x - region.pos.x;
      
      region_off =
        ((double) member->reg_offset.y * (double) resolution.y /
         ((double) layer->reg_precision.y * (double) layer->layer_size.y));
      region.pos.y -= (int) ceil(region_off);
      region_lim.y -= (int) floor(region_off);      
      res_val = ((double) resolution.y) *
        (((double) stream->size.y * (double) member->reg_subsampling.y) /
         ((double) layer->reg_precision.y * (double) layer->layer_size.y));
      if (res_val < 1.0) res_val = 1.0; // Just in case
      resolution.y = (int) ceil(res_val);
      res_factor = resolution.y / res_val; // Capture impact of res rounding
      region.pos.y = (int) floor(region.pos.y*res_factor);
      region_lim.y = (int) ceil(region_lim.y*res_factor);
      region.size.y = region_lim.y - region.pos.y;
    }
  else
    { // View window expressed relative to composition grid
      int iset = remapping_ids[0];
      int inum = remapping_ids[1];
      if (iset < 0)
        iset = 0;
      else if (iset >= this->num_comp_sets)
        iset = this->num_comp_sets-1;
      inum += this->comp_set_starts[iset];
      if (inum < 0)
        inum = 0; // Just in case
      else if (inum >= this->num_comp_instructions)
        inum = this->num_comp_instructions-1;
      kdcx_comp_instruction *inst = this->comp_instructions + inum;
      kdu_coords comp_size = this->composited_size;
      kdu_dims source_dims = inst->source_dims;
      kdu_dims target_dims = inst->target_dims;
      if (!source_dims)
        source_dims.size = layer->layer_size;
      if (!target_dims)
        { 
          target_dims.size = source_dims.size;
          if (inst->transpose)
            target_dims.size.transpose();
        }
      
      // Before going any further, we may need to modify `target_dims',
      // `comp_size', `resolution' and `region' to accommodate any appearance
      // changes.  Conceptually, these are applied to the cropped `source_dims'
      // region before scaling and translating to the `target_dims' region,
      // but we can get the same effect by considering that the appearance
      // transformations are applied after composition, with appropriate
      // modifications to `target_dims' and `comp_size' so that they reflect
      // what is going on before appearance transformations.  Then all we need
      // to do is map the window of interest (`resolution' and `region')
      // back through these appearance transformations to the modified
      // output composited geometry.
      if (inst->vflip)
        { 
          region.pos.y = resolution.y - region.pos.y - region.size.y;
          target_dims.pos.y = comp_size.y-target_dims.pos.y-target_dims.size.y;
        }
      if (inst->hflip)
        { 
          region.pos.x = resolution.x - region.pos.x - region.size.x;
          target_dims.pos.x = comp_size.x-target_dims.pos.x-target_dims.size.x;
        }
      if (inst->transpose)
        { 
          region.transpose(); resolution.transpose();
          target_dims.transpose(); comp_size.transpose();
        }
      
      // Now we can continue to process in the original orientation of the
      // underlying compositing layer
      double alpha, beta, region_off, res_val, res_factor;
      kdu_coords region_lim = region.pos+region.size;
      int min_val, lim_val;
      
      alpha = ((double) resolution.x) / ((double) comp_size.x);
      beta =  ((double) target_dims.size.x) / ((double) source_dims.size.x);
      min_val = (int) floor(alpha * target_dims.pos.x);
      lim_val = (int) ceil(alpha * (target_dims.pos.x + target_dims.size.x));
      if (min_val > region.pos.x)
        region.pos.x = min_val;
      if (lim_val < region_lim.x)
        region_lim.x = lim_val;
      region_off = alpha * (target_dims.pos.x -
                            beta * (source_dims.pos.x -
                                    ((double) member->reg_offset.x) /
                                    ((double) layer->reg_precision.x)));      
      region.pos.x -= (int) ceil(region_off);
      region_lim.x -= (int) floor(region_off);
      res_val = (alpha * beta * ((double) stream->size.x) *
                 ((double) member->reg_subsampling.x) /
                 ((double) layer->reg_precision.x));
      if (res_val < 1.0) res_val = 1.0; // Just in case
      resolution.x = (int) ceil(res_val);
      res_factor = resolution.x / res_val; // Capture impact of res rounding
      region.pos.x = (int) floor(region.pos.x*res_factor);
      region_lim.x = (int) ceil(region_lim.x*res_factor);
      region.size.x = region_lim.x - region.pos.x;
      
      alpha = ((double) resolution.y) / ((double) comp_size.y);
      beta =  ((double) target_dims.size.y) / ((double) source_dims.size.y);
      min_val = (int) floor(alpha * target_dims.pos.y);
      lim_val = (int) ceil(alpha * (target_dims.pos.y + target_dims.size.y));
      if (min_val > region.pos.y)
        region.pos.y = min_val;
      if (lim_val < region_lim.y)
        region_lim.y = lim_val;
      region_off = alpha * (target_dims.pos.y -
                            beta * (source_dims.pos.y -
                                    ((double) member->reg_offset.y) /
                                    ((double) layer->reg_precision.y)));
      region.pos.y -= (int) ceil(region_off);
      region_lim.y -= (int) floor(region_off);
      res_val = (alpha * beta * ((double) stream->size.y) *
                 ((double) member->reg_subsampling.y) /
                 ((double) layer->reg_precision.y));
      if (res_val < 1.0) res_val = 1.0; // Just in case
      resolution.y = (int) ceil(res_val);
      res_factor = resolution.y / res_val; // Capture impact of res rounding
      region.pos.y = (int) floor(region.pos.y*res_factor);
      region_lim.y = (int) ceil(region_lim.y*res_factor);
      region.size.y = region_lim.y - region.pos.y;
    }
  
  // Adjust region to ensure that it fits inside the codestream resolution
  kdu_coords lim = region.pos + region.size;
  if (region.pos.x < 0)
    region.pos.x = 0;
  if (region.pos.y < 0)
    region.pos.y = 0;
  if (lim.x > resolution.x)
    lim.x = resolution.x;
  if (lim.y > resolution.y)
    lim.y = resolution.y;
  region.size = lim - region.pos;
  if (region.size.x < 0)
    region.size.x = 0;
  if (region.size.y < 0)
    region.size.y = 0;
  
  return true;
}

/*****************************************************************************/
/*                      kdcx_context_mappings::add_stream                    */
/*****************************************************************************/

kdcx_stream_mapping *
  kdcx_context_mappings::add_stream(int idx, bool container_relative)
{
  if (container != NULL)
    { 
      if (!container_relative)
        idx -= container->num_top_codestreams;
      if (idx < 0)
        { 
          assert(0);
          return NULL;
        }
    }
  if (idx >= num_codestreams)
    { // Need to allocate new stream
      if (idx >= max_codestreams)
        { // Need to reallocate references array
          max_codestreams += idx+8;
          kdcx_stream_mapping **refs =
            new kdcx_stream_mapping *[max_codestreams];
          for (int n=0; n < num_codestreams; n++)
            refs[n] = stream_refs[n];
          if (stream_refs != NULL)
            delete[] stream_refs;
          stream_refs = refs;
        }
      while (num_codestreams <= idx)
        stream_refs[num_codestreams++] = new kdcx_stream_mapping;
    }
  return stream_refs[idx];
}

/*****************************************************************************/
/*                      kdcx_context_mappings::add_layer                     */
/*****************************************************************************/

kdcx_layer_mapping *
  kdcx_context_mappings::add_layer(int idx, bool container_relative)
{
  if (container != NULL)
    { 
      if (!container_relative)
        idx -= container->num_top_layers;
      if (idx < 0)
        { 
          assert(0);
          return NULL;
        }
    }
  if (idx >= num_compositing_layers)
    { // Need to allocate new layer
      if (idx >= max_compositing_layers)
        { // Need to reallocate references array
          max_compositing_layers += idx+8;
          kdcx_layer_mapping **refs =
            new kdcx_layer_mapping *[max_compositing_layers];
          for (int n=0; n < num_compositing_layers; n++)
            refs[n] = layer_refs[n];
          if (layer_refs != NULL)
            delete[] layer_refs;
          layer_refs = refs;
        }
      while (num_compositing_layers <= idx)
        { 
          layer_refs[num_compositing_layers] =
          new kdcx_layer_mapping(num_compositing_layers);
          num_compositing_layers++;
        }
    }
  return layer_refs[idx];
}

/*****************************************************************************/
/*               kdcx_context_mappings::parse_all_donated_isets              */
/*****************************************************************************/

bool
  kdcx_context_mappings::parse_all_donated_isets()
{
  while (donated_isets != NULL)
    { 
      if (!donated_isets->iset_box.is_complete())
        return false;
      parse_iset_box(&(donated_isets->iset_box));
      if ((donated_isets = donated_isets->next) == NULL)
        last_donated_iset = NULL;
    }
  return true;
}

/*****************************************************************************/
/*                    kdcx_context_mappings::parse_copt_box                  */
/*****************************************************************************/

void
  kdcx_context_mappings::parse_copt_box(jp2_input_box *box)
{
  composited_size = kdu_coords(0,0);
  kdu_uint32 height=0, width=0;
  if (!(box->read(height) && box->read(width)))
    return; // Leaves `composited_size' 0, so rest of comp box is ignored
  composited_size.x = (int) width;
  composited_size.y = (int) height;
}

/*****************************************************************************/
/*                   kdcx_context_mappings::parse_iset_box                   */
/*****************************************************************************/

void
  kdcx_context_mappings::parse_iset_box(jp2_input_box *box)
{
  int n;
  if ((composited_size.x <= 0) || (composited_size.y <= 0))
    return; // Failed to parse a valid `copt' box previously

  kdu_uint16 flags, rept;
  kdu_uint32 tick;
  if (!(box->read(flags) && box->read(rept) && box->read(tick)))
    { KDU_ERROR(e,3); e <<
        KDU_TXT("Malformed Instruction Set box found in JPX data source.");
    }

  if (num_comp_sets == max_comp_sets)
    { // Augment the array
      int new_max_sets = max_comp_sets + num_comp_sets + 8;
      int *tmp_starts = new int[new_max_sets];
      for (n=0; n < num_comp_sets; n++)
        tmp_starts[n] = comp_set_starts[n];
      if (comp_set_starts != NULL)
        delete[] comp_set_starts;
      comp_set_starts = tmp_starts;
      max_comp_sets = new_max_sets;
    }
  comp_set_starts[num_comp_sets++] = num_comp_instructions;
  
  kdu_dims source_dims, target_dims;
  bool transpose=false, vflip=false, hflip=false;
  bool have_target_pos = ((flags & 1) != 0);
  bool have_target_size = ((flags & 2) != 0);
  bool have_life_persist = ((flags & 4) != 0);
  bool have_source_region = ((flags & 32) != 0);
  bool have_orientation = ((flags & 64) != 0);
  if (!(have_target_pos || have_target_size ||
        have_life_persist || have_source_region || have_orientation))
    return; // No new instructions in this set

  while (1)
    { 
      if (have_target_pos)
        {
          kdu_uint32 X0=0, Y0=0;
          if (!(box->read(X0) && box->read(Y0)))
            return; // No more instructions
          target_dims.pos.x = (int) X0;
          target_dims.pos.y = (int) Y0;
        }
      else
        target_dims.pos = kdu_coords(0,0);

      if (have_target_size)
        {
          kdu_uint32 XS=0, YS=0;
          if (!(box->read(XS) && box->read(YS)))
            {
              if (!have_target_pos)
                return; // No more instructions
              KDU_ERROR(e,4); e <<
                KDU_TXT("Malformed Instruction Set box found in JPX data "
                "source.");
            }
          target_dims.size.x = (int) XS;
          target_dims.size.y = (int) YS;
        }
      else
        target_dims.size = kdu_coords(0,0);

      if (have_life_persist)
        {
          kdu_uint32 life, next_reuse;
          if (!(box->read(life) && box->read(next_reuse)))
            {
              if (!(have_target_pos || have_target_size))
                return; // No more instructions
              KDU_ERROR(e,5); e <<
                KDU_TXT("Malformed Instruction Set box found in JPX data "
                "source.");
            }
        }
      
      if (have_source_region)
        {
          kdu_uint32 XC=0, YC=0, WC=0, HC=0;
          if (!(box->read(XC) && box->read(YC) &&
                box->read(WC) && box->read(HC)))
            {
              if (!(have_life_persist || have_target_pos || have_target_size))
                return; // No more instructions
              KDU_ERROR(e,6); e <<
                KDU_TXT("Malformed Instruction Set box found in JPX "
                "data source.");
            }
          source_dims.pos.x = (int) XC;
          source_dims.pos.y = (int) YC;
          source_dims.size.x = (int) WC;
          source_dims.size.y = (int) HC;
        }
      else
        source_dims.size = source_dims.pos = kdu_coords(0,0);

    if (have_orientation)
      { 
        kdu_uint32 rot;
        if (!box->read(rot))
          { 
            if (!(have_life_persist || have_target_pos || have_target_size ||
                  have_source_region))
              return; // No more instructions
            KDU_ERROR(e,0x29041201); e <<
              KDU_TXT("Malformed Instruction Set box found in JPX "
                      "data source.");
          }
        int r_val = ((int) (rot & ~((kdu_uint32) 16))) - 1;
        if ((r_val < 0) || (r_val > 3))
          { KDU_ERROR(e,0x29041202); e <<
              KDU_TXT("Malformed Instruction Set box found in JPX "
                      "data source.");
          }
        hflip = ((rot & 16) != 0);
        switch (r_val & 3) {
          case 0: transpose=false; vflip=false; break;
          case 1: transpose=true; vflip=false; hflip=!hflip; break;
          case 2: transpose=false; vflip=true; hflip=!hflip; break;
          case 3: transpose=true; vflip=true; break;
        }
      }

    if (num_comp_instructions == max_comp_instructions)
        { // Augment array
          int new_max_insts = max_comp_instructions + num_comp_instructions+8;
          kdcx_comp_instruction *tmp_inst =
            new kdcx_comp_instruction[new_max_insts];
          for (n=0; n < num_comp_instructions; n++)
            tmp_inst[n] = comp_instructions[n];
          if (comp_instructions != NULL)
            delete[] comp_instructions;
          comp_instructions = tmp_inst;
          max_comp_instructions = new_max_insts;
        }
      comp_instructions[num_comp_instructions].source_dims = source_dims;
      comp_instructions[num_comp_instructions].target_dims = target_dims;
      comp_instructions[num_comp_instructions].transpose = transpose;
      comp_instructions[num_comp_instructions].vflip = vflip;
      comp_instructions[num_comp_instructions].hflip = hflip;
      num_comp_instructions++;
    }
}

/*****************************************************************************/
/*              kdcx_context_mappings::finish_layers_and_streams             */
/*****************************************************************************/

bool
  kdcx_context_mappings::finish_layers_and_streams(bool context_complete)
{
  int n;
  bool any_change = false;
  
  // See if we can complete any codestream header boxes
  for (n=first_unfinished_stream; n < num_codestreams; n++)
    { 
      kdcx_stream_mapping *str_defaults = &stream_defaults;
      if (top_mappings != NULL)
        str_defaults = top_mappings->get_stream_defaults();
      kdcx_stream_mapping *str = stream_refs[n];
      if ((!str->is_complete) && 
          (context_complete ||
           (str->jpch_closed && str_defaults->is_complete)) &&
          str->finish(str_defaults))
        { 
          any_change = true;
          if (n == first_unfinished_stream)
            first_unfinished_stream++;
        }
    }

  // See if we can complete any codestream header boxes
  for (n=first_unfinished_layer; n < num_compositing_layers; n++)
    { 
      kdcx_layer_mapping *lyr_defaults = &layer_defaults;
      if (top_mappings != NULL)
        lyr_defaults = top_mappings->get_layer_defaults();
      kdcx_layer_mapping *lyr = layer_refs[n];
      if ((!lyr->is_complete) && 
          (context_complete ||
           (lyr->jplh_closed && lyr_defaults->is_complete)) &&
          lyr->finish(lyr_defaults,this))
        { 
          any_change = true;
          if (n == first_unfinished_layer)
            first_unfinished_layer++;
        }
    }

  return any_change;
}


/* ========================================================================= */
/*                             kdcx_entity_container                         */
/* ========================================================================= */

/*****************************************************************************/
/*                  kdcx_entity_container::kdcx_entity_container             */
/*****************************************************************************/

kdcx_entity_container::kdcx_entity_container(kdcx_context_mappings *top_maps)
{
  next = NULL;
  info_complete = all_complete = false;
  num_jpch = num_jplh = 0;
  num_top_codestreams = top_maps->num_codestreams;
  num_top_layers = top_maps->num_compositing_layers;
  first_base_codestream = num_top_codestreams;
  first_base_layer = num_top_layers;
  num_base_codestreams = num_base_layers = 0;
  max_codestream = num_top_codestreams-1;
  max_layer = num_top_layers-1;
  this->context_mappings = new kdcx_context_mappings(this,top_maps);
}

/*****************************************************************************/
/*                 kdcx_entity_container::~kdcx_entity_container             */
/*****************************************************************************/

kdcx_entity_container::~kdcx_entity_container()
{
  if (context_mappings != NULL)
    delete context_mappings; 
}

/*****************************************************************************/
/*                         kdcx_entity_container::init                       */
/*****************************************************************************/

bool
  kdcx_entity_container::init(kdcx_entity_container *last_initialized)
{
  assert(!info_complete);
  assert(jclx_box.exists());
  if (!jclx_box.is_complete())
    return false;
  if (!((jclx_sub.exists() || jclx_sub.open(&jclx_box)) &&
        jclx_sub.is_complete()))
    return false;
  if (jclx_sub.get_box_type() != jp2_layer_extensions_info_4cc)
    { KDU_ERROR(e,0x16011304); e <<
      KDU_TXT("Error in Compositing Layer Extensions box: first sub-box "
              "must be a Compositing Layer Extensions Info box.");
    }
  if (last_initialized == NULL)
    assert((first_base_codestream == num_top_codestreams) &&
           (first_base_layer == num_top_layers));
  else
    { 
      first_base_codestream = last_initialized->max_codestream+1;
      first_base_layer = last_initialized->max_layer+1;
    }
  assert((first_base_codestream >= num_top_codestreams) &&
         (first_base_layer >= num_top_layers));
  kdu_uint32 Mjclx=0, Cjclx=1, Ljclx=1, Tjclx=0, Fjclx=0, Sjclx=0;
  if (!(jclx_sub.read(Mjclx) && jclx_sub.read(Cjclx) &&
        jclx_sub.read(Ljclx) && jclx_sub.read(Tjclx) && jclx_sub.read(Fjclx) &&
        ((Tjclx==0) || jclx_sub.read(Sjclx))))
    { KDU_ERROR(e,0x16011305); e <<
      KDU_TXT("Error in Compositing Layer Extensions Info box: "
              "box appears to be prematurely truncated.");
    }
  Mjclx &= 0x7FFFFFFF;  Cjclx &= 0x7FFFFFFF;  Ljclx &= 0x7FFFFFFF;
  num_base_codestreams = (int) Cjclx;
  num_base_layers = (int) Ljclx;
  if (Mjclx == 0)
    { // Indefinite reps
      if (num_base_layers > 0)
        max_layer = INT_MAX;
      if (num_base_codestreams > 0)
        max_codestream = INT_MAX;
    }
  else
    { 
      if (num_base_layers > 0)
        max_layer = ((int) Mjclx) * num_base_layers + first_base_layer - 1;
      if (num_base_codestreams > 0)
        max_codestream =
        ((int) Mjclx) * num_base_codestreams + first_base_codestream - 1;
    }
  jclx_sub.close();
  info_complete = true;
  
  if (num_base_codestreams > 0)
    context_mappings->add_stream(num_base_codestreams-1,true);
  if (num_base_layers > 0)
    context_mappings->add_layer(num_base_layers-1,true);
  
  return true;
}

/*****************************************************************************/
/*                        kdcx_entity_container::finish                      */
/*****************************************************************************/

bool
  kdcx_entity_container::finish()
{
  assert(info_complete && !all_complete);
  bool any_change = false;
  if (context_mappings->composited_size !=
      context_mappings->top_mappings->composited_size)
    { 
      context_mappings->composited_size =
      context_mappings->top_mappings->composited_size;
      any_change = true;
    }
  if (jclx_box.exists())
    { 
      if (!jclx_box.is_complete())
        return false;
      while (jclx_sub.exists() || jclx_sub.open(&jclx_box))
        { 
          kdu_uint32 box_type = jclx_sub.get_box_type();
          if (box_type == jp2_codestream_header_4cc)
            { 
              kdcx_stream_mapping *str =
                context_mappings->add_stream(num_jpch++,true);
              str->jpch_box.transplant(jclx_sub);
              assert(!jclx_sub);
              if (num_jpch > num_base_codestreams)
                { KDU_ERROR(e,0x16011306); e <<
                  KDU_TXT("Too many Codestream Header sub-boxes found within "
                          "Compositing Layer Extensions box, compared with "
                          "value supplied by the Info sub-box.");
                }
            }
          else if (box_type == jp2_compositing_layer_hdr_4cc)
            { 
              kdcx_layer_mapping *lyr =
                context_mappings->add_layer(num_jplh++,true);
              lyr->jplh_box.transplant(jclx_sub);
              assert(!jclx_sub);
              if (num_jplh > num_base_layers)
                { KDU_ERROR(e,0x16011307); e <<
                  KDU_TXT("Too many Compositing Layer Header sub-boxes found "
                          "within Compositing Layer Extensions box, compared "
                          "with value supplied by the Info sub-box.");
                }
            }
          else if (box_type == jp2_comp_instruction_set_4cc)
            { 
              context_mappings->donate_iset(jclx_sub);
              jclx_sub.close();
            }
          else
            jclx_sub.close();
        }
      if (!jclx_sub)
        { 
          jclx_box.close();
          if ((num_jplh != num_base_layers) ||
              (num_jpch != num_base_codestreams))
            { KDU_ERROR(e,0x16011308); e <<
              KDU_TXT("Too few Compositing Layer Header or Codestream Header "
                      "sub-boxes found within Compositing Layer Extensions "
                      "box, compared with value supplied by the Info "
                      "sub-box.");
            }
        }
    }
  if ((!context_mappings->comp_info_complete) &&
      context_mappings->parse_all_donated_isets() && (!jclx_box) &&
      context_mappings->top_mappings->comp_info_complete)
    { 
      context_mappings->comp_info_complete = true;
      any_change = true;
    }
  bool context_complete = ((num_jplh == num_base_layers) &&
                           (num_jpch == num_base_codestreams));
  if (context_mappings->finish_layers_and_streams(context_complete))
    any_change = true;
  if (context_mappings->comp_info_complete &&
      (context_mappings->first_unfinished_layer == num_base_layers) &&
      (context_mappings->first_unfinished_stream == num_base_codestreams))
    { 
      all_complete = true;
      any_change = true;
    }
  return any_change;
}


/* ========================================================================= */
/*                                 kdu_clientx                               */
/* ========================================================================= */

/*****************************************************************************/
/*                           kdu_clientx::kdu_clientx                        */
/*****************************************************************************/

kdu_clientx::kdu_clientx()
{
  started = false;
  not_compatible = false;
  is_jp2 = false;
  top_level_complete = false;
  have_multi_codestream_box = false;
  top_context_mappings = NULL;
  num_top_jp2c_or_frag = num_top_jpch = num_top_jplh = 0;
  containers = last_container = NULL;
  first_unfinished_container = last_initialized_container = NULL;
}

/*****************************************************************************/
/*                          kdu_clientx::~kdu_clientx                        */
/*****************************************************************************/

kdu_clientx::~kdu_clientx()
{
  close();
}

/*****************************************************************************/
/*                              kdu_clientx::close                           */
/*****************************************************************************/

void
  kdu_clientx::close()
{
  if (top_context_mappings != NULL)
    { 
      delete top_context_mappings;
      top_context_mappings = NULL;
    }
  while ((last_container=containers) != NULL)
    { 
      containers = last_container->next;
      delete last_container;
    }
  first_unfinished_container = last_initialized_container = NULL;
  jp2h_sub.close();
  top_box.close();
  comp_sub.close();
  comp_box.close();
  src.close();
  started = false;
  not_compatible = false;
  is_jp2 = false;
  have_multi_codestream_box = false;
  top_level_complete = false;
  num_top_jp2c_or_frag = num_top_jpch = num_top_jplh = 0;
}

/*****************************************************************************/
/*                        kdu_clientx::access_context                        */
/*****************************************************************************/

kdu_window_context
  kdu_clientx::access_context(int context_type, int context_idx,
                              int remapping_ids[])
{
  if ((context_type != KDU_JPIP_CONTEXT_JPXL) || (context_idx < 0))
    return kdu_window_context();
  int rep_idx = 0;
  int base_context_idx = context_idx;
  kdcx_context_mappings *mappings = top_context_mappings;
  if ((containers != NULL) &&
      (context_idx >= containers->first_base_layer))
    { 
      kdcx_entity_container *scan=containers;
      while (scan->info_complete && (scan->next != NULL) &&
             (context_idx >= scan->next->first_base_layer))
        scan = scan->next;
      if (context_idx >= scan->max_layer)
        return kdu_window_context();
      mappings = scan->context_mappings;
      base_context_idx = context_idx - scan->first_base_layer;
      rep_idx = base_context_idx / scan->num_base_layers;
      base_context_idx -= rep_idx*scan->num_base_layers;
    }
  if ((mappings == NULL) ||
      (base_context_idx >= mappings->num_compositing_layers))
    return kdu_window_context(); // Context might belong to a later container
  kdcx_layer_mapping *layer = mappings->layer_refs[base_context_idx];
  if ((layer == NULL) || !layer->is_complete)
    return kdu_window_context(); // Might be waiting to finish parsing layer
  if ((remapping_ids[0] >= 0) || (remapping_ids[1] >= 0))
    { // See if the instruction reference is valid
      if (!top_context_mappings->comp_info_complete)
        return kdu_window_context(); // Still waiting for composition info
      int iset = remapping_ids[0];
      int inum = remapping_ids[1];
      if ((iset < 0) || (iset >= mappings->num_comp_sets) ||
          (inum < 0) || ((inum + mappings->comp_set_starts[iset]) >=
                         mappings->num_comp_instructions) ||
          (mappings->composited_size.x < 1) ||
          (mappings->composited_size.y < 1))
        remapping_ids[0] = remapping_ids[1] = -1; // Invalid instruction set
    }
  return kdu_window_context(mappings,base_context_idx,rep_idx);
}

/*****************************************************************************/
/*                             kdu_clientx::update                           */
/*****************************************************************************/

bool
  kdu_clientx::update()
{
  if (not_compatible)
    return false;

  bool any_change = false;
  if (!started)
    { // Opening for the first time
      if (!src.exists())
        src.open(&aux_cache);
      if (!top_box.open(&src))
        return false; // Need to come back later
      if (top_box.get_box_type() != jp2_signature_4cc)
        { not_compatible = true; return false; }
      top_box.close();
      if (top_context_mappings == NULL)
        top_context_mappings = new kdcx_context_mappings(NULL,NULL);
      top_context_mappings->add_stream(0,false); // Always create at least one
      top_context_mappings->add_layer(0,false); // codestream and layer
      started = true;
    }
  
  // Parse as many top-level boxes as possible
  src.synch_with_cache();
  while (!top_level_complete)
    { 
      if ((!top_box) && !top_box.open_next())
        { 
          if (src.is_top_level_complete())
            { 
              kdcx_context_mappings *mappings = top_context_mappings;
              top_level_complete = true;
              if ((num_top_jpch == 0) && (num_top_jp2c_or_frag > 0))
                mappings->add_stream(num_top_jp2c_or_frag-1,false);
                  // We have at least one codestream for each top-level
                  // JP2C of FRAG box.  If there is a multi-codestream box,
                  // we may need to add more, but this condition is handled
                  // outside of this loop -- see below.
              mappings->get_stream_defaults()->is_complete = true;
              mappings->get_layer_defaults()->is_complete = true;
              if (!comp_box.exists())
                top_context_mappings->comp_info_complete = true;
              any_change = true;
            }
        }
      if (!top_box)
        break; // Have to come back and continue parsing later
      kdu_uint32 box_type = top_box.get_box_type();
      bool box_complete = top_box.is_complete();
      if (box_type == jp2_file_type_4cc)
        { 
          if (!box_complete)
            return any_change; // No point in continuing
          kdu_uint32 brand, minor_version, compat;
          top_box.read(brand); top_box.read(minor_version);
          is_jp2 = not_compatible = true; // Until proven otherwise
          while (top_box.read(compat))
            if (compat == jp2_brand)
              not_compatible = false;
            else if ((compat == jpx_brand) || (compat == jpx_baseline_brand))
              not_compatible = is_jp2 = false;
          top_box.close();
          if (not_compatible)
            { is_jp2 = false; return false; }
          any_change = true;
        }
      else if (box_type == jp2_header_4cc)
        { 
          if (!box_complete)
            return any_change; // Simplify things by insisting JP2H is complete
          kdcx_context_mappings *mappings = top_context_mappings;
          while (jp2h_sub.exists() || jp2h_sub.open(&top_box))
            { 
              if (!jp2h_sub.is_complete())
                return any_change; // Don't parse any further until header done
              if (jp2h_sub.get_box_type() == jp2_image_header_4cc)
                mappings->get_stream_defaults()->parse_ihdr_box(&jp2h_sub);
              else if (jp2h_sub.get_box_type() == jp2_component_mapping_4cc)
                mappings->get_stream_defaults()->parse_cmap_box(&jp2h_sub);
              else if (jp2h_sub.get_box_type() == jp2_channel_definition_4cc)
                mappings->get_layer_defaults()->parse_cdef_box(&jp2h_sub);
              else if (jp2h_sub.get_box_type() == jp2_colour_4cc)
                mappings->get_layer_defaults()->parse_colr_box(&jp2h_sub);
              jp2h_sub.close();
            }
          top_box.close();
          mappings->get_stream_defaults()->is_complete = true;
          mappings->get_layer_defaults()->is_complete = true;
          any_change = true;
        }
      else if ((box_type == jp2_codestream_4cc) ||
               (box_type == jp2_fragment_table_4cc))
        { // Need to be able to skip over incomplete codestream boxes
          num_top_jp2c_or_frag++;
          top_box.close();
          continue;
        }
      else if (box_type == jp2_multi_codestream_4cc)
        { 
          have_multi_codestream_box = true;
          top_box.close();
          continue;
        }
      else if (box_type == jp2_composition_4cc)
        { 
          if (!(top_context_mappings->comp_info_complete || comp_box.exists()))
            { 
              comp_box.transplant(top_box);
              assert(!top_box);
            }
        }
      else if (box_type == jp2_codestream_header_4cc)
        { 
          if (containers != NULL)
            { KDU_ERROR(e,0x15011302); e <<
              KDU_TXT("Top-level Codestream Header boxes must all precede any "
                      "Compositing Layer Extensions boxes in a JPX file.");
            }
          kdcx_stream_mapping *str =
            top_context_mappings->add_stream(num_top_jpch++,false);
          str->jpch_box.transplant(top_box);
          assert(!top_box);
        }
      else if (box_type == jp2_compositing_layer_hdr_4cc)
        { 
          if (containers != NULL)
            { KDU_ERROR(e,0x15011303); e <<
              KDU_TXT("Top-level Compositing Layer boxes must all precede any "
                    "Compositing Layer Extensions boxes in a JPX file.");
            }
          kdcx_layer_mapping *lyr =
            top_context_mappings->add_layer(num_top_jplh++,false);
          lyr->jplh_box.transplant(top_box);
          assert(!top_box);
        }
      else if ((box_type == jp2_layer_extensions_4cc) && !is_jp2)
        { 
          if ((num_top_jplh == 0) || (num_top_jpch == 0) ||
              !(comp_box.exists() || top_context_mappings->comp_info_complete))
            { KDU_ERROR(e,0x16011301); e <<
              KDU_TXT("Compositing Layer Extensions box must be preceded by "
                      "a Composition box, at least one Codestream Header box "
                      "and at least one Compositing Layer Header box at "
                      "the top level of the file.");
            }
          assert((num_top_jpch==top_context_mappings->num_codestreams) &&
                 (num_top_jplh==top_context_mappings->num_compositing_layers));
          kdcx_entity_container *elt =
            new kdcx_entity_container(top_context_mappings);
          if (last_container == NULL)
            containers = elt;
          else
            last_container->next = elt;
          last_container = elt;
          if (first_unfinished_container == NULL)
            first_unfinished_container = elt;
          elt->jclx_box.transplant(top_box);
          assert(!top_box);
        }
      else 
        top_box.close();
    }
  
  if (top_level_complete && (num_top_jpch == 0) && have_multi_codestream_box)
    { // Rather than trying to parse the multiple codestream box here, we
      // can just update the number of top-level layers and codestreams
      // according to the number of codestreams for which non-meta data-bins
      // have been received so far.  This is for the rather pathalogical
      // case in which a file has multiple codestreams but no codestream
      // header boxes, which is allowed (not with JCLX boxes).
      assert(containers == NULL);
      kdu_long last_stream_id = aux_cache.get_max_codestream_id();
      if (last_stream_id > 1000000)
        last_stream_id = 1000000; // Ridiculous number of top level codestreams
      if (last_stream_id >= (kdu_long) top_context_mappings->num_codestreams)
        { 
          top_context_mappings->add_stream((int)last_stream_id,false);
          any_change = true;
        }
    }
  
  // See if we can parse any top-level composition instructions
  if (comp_box.exists() && comp_box.is_complete())
    { 
      while ((comp_sub.exists() || comp_sub.open(&comp_box)) &&
             comp_sub.is_complete())
        { 
          if (comp_sub.get_box_type() == jp2_comp_options_4cc)
            top_context_mappings->parse_copt_box(&comp_sub);
          else if (comp_sub.get_box_type() == jp2_comp_instruction_set_4cc)
            top_context_mappings->parse_iset_box(&comp_sub);
          any_change = true;
          comp_sub.close();
        }
      if (!comp_sub)
        { 
          comp_box.close();
          any_change = true;
          top_context_mappings->comp_info_complete = true;
        }
    }
  
  // See if we can parse further into the discovered top-level boxes
  if (top_context_mappings->finish_layers_and_streams(top_level_complete))
    any_change = true;
  kdcx_entity_container *cscan=last_initialized_container;
  cscan=(cscan==NULL)?containers:(cscan->next);
  while ((cscan != NULL) &&
         cscan->init(last_initialized_container))
    { 
      any_change = true;
      last_initialized_container = cscan;
      cscan = cscan->next;
    }
  for (cscan=first_unfinished_container; cscan != NULL; cscan=cscan->next)
    { 
      if (!cscan->info_complete)
        break;
      if (cscan->finish())
        { 
          any_change = true;
          if (cscan->all_complete && (cscan==first_unfinished_container))
            first_unfinished_container = cscan->next;
        }
    }
  
  return any_change;
}
