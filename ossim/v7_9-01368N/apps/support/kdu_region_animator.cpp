/*****************************************************************************/
// File: kdu_region_animator.cpp [scope = APPS/SUPPORT]
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
   Implements the `kdu_region_animator' object.
******************************************************************************/

#include <math.h>
#include "kdu_utils.h"
#include "kdu_region_animator.h"
#include "region_animator_local.h"
using namespace kd_supp_local;


/*===========================================================================*/
/*                             Internal Functions                            */
/*===========================================================================*/

/*****************************************************************************/
/* STATIC                     measure_roi_distance                           */
/*****************************************************************************/

static double
  measure_roi_distance(const kdu_dims &roi1, const kdu_dims &roi2,
                       double &max_delta_x, double &max_delta_y)
  /* Returns a measure of the distance between two regions of interest
     on an image surface.  The assumption is that each corner of `roi1'
     needs to be mapped to/from the corresponding corner of `roi2'.
     Euclidean distances are calculated for each of these four mapping
     vectors and the maximum of these four distances is the function's
     return value.  `max_delta_x' and `max_delta_y' are used to return
     the maximum distance travelled in each of the horizontal and vertical
     directions by any of the four region vertices when moving from
     `roi1' to `roi2' or vice-versa. */
{
  kdu_coords min1=roi1.pos, lim1=min1+roi1.size;
  kdu_coords min2=roi2.pos, lim2=min2+roi2.size;
  int delta_x1=(min1.x-min2.x), delta_x2=(lim1.x-lim2.x);
  int delta_y1=(min1.y-min2.y), delta_y2=(lim1.y-lim2.y);
  delta_x1 = (delta_x1 < 0)?(-delta_x1):delta_x1;
  delta_x2 = (delta_x2 < 0)?(-delta_x2):delta_x2;
  delta_y1 = (delta_y1 < 0)?(-delta_y1):delta_y1;
  delta_y2 = (delta_y2 < 0)?(-delta_y2):delta_y2;
  max_delta_x = (delta_x1 > delta_x2)?delta_x1:delta_x2;
  max_delta_y = (delta_y1 > delta_y2)?delta_y1:delta_y2;
  double dist_sq = max_delta_x*max_delta_x + max_delta_y*max_delta_y;
  assert(dist_sq > 0.0);
  return sqrt(dist_sq);
}

/*****************************************************************************/
/* STATIC                   get_link_numlist_and_roi                         */
/*****************************************************************************/

static jpx_metanode
  get_link_numlist_and_roi(jpx_metanode node, kdu_dims &roi)
{
  jpx_metanode numlist;
  jpx_metanode_link_type link_type=JPX_METANODE_LINK_NONE;
  jpx_metanode link_target = node.get_link(link_type);
  if (link_target.exists())
    { // See if the link target has a region of interest or number list
      numlist = link_target.get_numlist_container();
      if (numlist.exists())
        { 
          while (link_target.exists() && (link_target != numlist) &&
                 (roi=link_target.get_bounding_box()).is_empty())
            link_target = link_target.get_parent();
        }
    }
  return numlist;
}

/*****************************************************************************/
/* STATIC              check_numlist_fully_used_by_frame                     */
/*****************************************************************************/

static bool
  check_numlist_fully_used_by_frame(jpx_metanode numlist, jpx_frame frame)
  /* Returns true if all compositing layers defined by `numlist' are
     used by one or more instructions in `frame'.  The function ignores
     the instructions belonging to any persistent preceding frames that
     are included with `frame'.  The function takes into account the
     fact that if `numlist' is embedded within a JPX container with
     multiple (or potentially multiple) repetitions, and any of the
     compositing layers belong to the JPX container, the `frame' cannot
     generally use all of the possible compositing layers obtained by
     such replication. */
{
  int n, base_idx=INT_MAX;
  if (numlist.get_container_lmap(&base_idx) == 1)
    base_idx = INT_MAX; // No repetitions
  int numlist_streams=0, numlist_layers=0; bool rres;
  if (!numlist.get_numlist_info(numlist_streams,numlist_layers,rres))
    return true;
  if (numlist_layers < 2)
    { // At most one compositing layer; this is the easy case, because the
      // frame must be using this layer in one of its instructions.
      if (numlist.get_numlist_layer(0) < base_idx)
        return true; // <= 1 layer, not replicated; must be using it
      else
        return false; // 1 replicated container-defined base layer; cannot use
                      // all repetitions.
    }
  const int *indices = numlist.get_numlist_layers();
  int min_inst = frame.get_num_persistent_instructions();
  for (n=0; n < numlist_layers; n++)
    { 
      if (indices[n] >= base_idx)
        break; // Cannot use all repetitions of container-defined base layer
      if (frame.find_last_instruction_for_layer(indices[n],0) < min_inst)
        break;
    }
  return (n == numlist_layers);
}

/*****************************************************************************/
/* STATIC                  check_novel_frame_match                           */
/*****************************************************************************/

static bool
  check_novel_frame_match(jpx_frame tgt, jpx_frame ref, jpx_metanode numlist)
  /* Returns true if `tgt' uses at least one compositing layer referenced
     by `numlist' that is not also used by `ref'.  For simplicity, we
     examine only the non-persistent instructions provided by `tgt'. */
{
  kdu_dims src_dims, tgt_dims;  jpx_composited_orientation orient;
  int layer_idx, n = tgt.get_num_persistent_instructions();
  while (tgt.get_instruction(n++,layer_idx,src_dims,tgt_dims,orient))
    { 
      if (!numlist.test_numlist_layer(layer_idx))
        continue;
      if (ref.find_last_instruction_for_layer(layer_idx) < 0)
        return true;
    }
  return false;
}

/*****************************************************************************/
/* STATIC                 map_codestream_rgn_to_frame                        */
/*****************************************************************************/

static kdu_dims
  map_codestream_rgn_to_frame(kdu_dims src_rgn, kdu_coords align,
                              kdu_coords sampling, kdu_coords denominator,
                              kdu_dims src_dims, kdu_dims tgt_dims,
                              jpx_composited_orientation orientation)
  /* This function is intended to replicate the geometric transformations
     performed within `kdu_region_compositor' when mapping a region defined
     relative to the image origin on a codestream to a corresponding region
     on a composited frame.  `src_rgn' argument expresses the source region
     relative to the upper left hand corner of the image on the codestream.
     The `align', `sampling' and `denominator' arguments are the values
     retrieved from `jpx_layer_source::get_codestream_registration' that
     define the mapping between the codestream and the compositing layer's
     registration grid.  The `src_dims' argument identifies the cropped
     region of the source compositing layer that is composited.  The
     `orientation' argument identifies any re-orientation that must be
     applied to the cropped source compositing layer before scaling and
     translating it to the `tgt_dims' region on the composited frame.
     Note that `src_dims' and `tgt_dims' are non-empty regions when this
     function is invoked.
        The function may produce a very slightly larger region that one might
     expect, due to rounding out to whole pixel locations during the
     transformation first to the compositing layer grid and then to the
     composited frame. */
{
  // Start by mapping `src_rgn' to the compositing layer's registration grid.
  kdu_coords min, lim;
  kdu_long num, den, val;
  num = sampling.x; den = denominator.x;
  val = align.x + num * src_rgn.pos.x;  min.x = long_floor_ratio(val,den);
  val += num*src_rgn.size.x;  lim.x = long_ceil_ratio(val,den);
  num = sampling.y; den = denominator.y;
  val = align.y + num * src_rgn.pos.y;  min.y = long_floor_ratio(val,den);
  val += num*src_rgn.size.y;  lim.y = long_ceil_ratio(val,den);
  kdu_dims layer_rgn; layer_rgn.pos = min; layer_rgn.size = lim-min;
  
  // Now make adjustments to account for orientation changes and then
  // express `layer_rgn' relative to the `src_dims' region after re-orientation
  layer_rgn.to_apparent(orientation.transpose,orientation.vflip,
                        orientation.hflip);
  src_dims.to_apparent(orientation.transpose,orientation.vflip,
                       orientation.hflip);
  layer_rgn &= src_dims;
  layer_rgn.pos -= src_dims.pos;
  
  // Finally scale and translate `layer_rgn' according to `tgt_dims'
  num = tgt_dims.size.x; den = src_dims.size.x;
  val = layer_rgn.pos.x*num;  min.x = long_floor_ratio(val,den);
  val += layer_rgn.size.x*num;  lim.x = long_ceil_ratio(val,den);
  num = tgt_dims.size.y; den = src_dims.size.y;
  val = layer_rgn.pos.y*num;  min.y = long_floor_ratio(val,den);
  val += layer_rgn.size.y*num;  lim.y = long_ceil_ratio(val,den);
  
  kdu_dims result;  result.pos = min+tgt_dims.pos;  result.size = lim-min;
  return result;
}


/*===========================================================================*/
/*                          kdu_region_animator_roi                          */
/*===========================================================================*/

/*****************************************************************************/
/*                      kdu_region_animator_roi::prepare                     */
/*****************************************************************************/

double kdu_region_animator_roi::init(double acceleration, double max_ds_dt)
{
  if ((acceleration <= 0.0) || (max_ds_dt <= 0.0))
    { 
      this->static_roi = true;
      this->acceleration = this->max_ds_dt = -1.0;
      this->trans_pos = this->time_to_trans_pos = 0.0;
      this->total_transition_time = 0.0;
      this->start_pos = this->end_pos = 1.0;
    }
  else
    { 
      this->static_roi = false;
      this->acceleration = acceleration;
      this->max_ds_dt = max_ds_dt;
      this->time_to_trans_pos = max_ds_dt / acceleration;
      this->trans_pos = 0.5 * max_ds_dt * time_to_trans_pos;
      if (trans_pos > 0.5)
        { 
          trans_pos = 0.5;
          time_to_trans_pos = sqrt(1.0/acceleration);
          this->total_transition_time = 2.0*time_to_trans_pos;
        }
      else
        this->total_transition_time = (2.0*time_to_trans_pos +
                                       (1.0-2.0*trans_pos) / max_ds_dt);
      this->start_pos = 0.0;
      this->end_pos = 1.0;
    }
  this->initial_roi = this->final_roi = kdu_dims();
  this->is_valid = false;
  return total_transition_time;
}

/*****************************************************************************/
/*                kdu_region_animator_roi::set_end_points                    */
/*****************************************************************************/

double
  kdu_region_animator_roi::set_end_points(double start_pos, double end_pos,
                                          kdu_dims initial_roi,
                                          kdu_dims final_roi)
{
  if (this->static_roi)
    { 
      this->start_pos = this->end_pos = 1.0;
      this->initial_roi = this->final_roi = final_roi;
      this->is_valid = true;
      return 0.0;
    }
  else
    { 
      if (start_pos < 0.0)
        start_pos = 0.0;
      else if (start_pos > 1.0)
        start_pos = 1.0;
      if (end_pos < start_pos)
        end_pos = start_pos;
      else if (end_pos > 1.0)
        end_pos = 1.0;
      this->start_pos = start_pos;
      this->end_pos = end_pos;
      this->initial_roi = initial_roi;
      this->final_roi = final_roi;
      this->is_valid = true;
      return (get_time_for_pos(end_pos) - get_time_for_pos(start_pos));
    }
}

/*****************************************************************************/
/*                 kdu_region_animator_roi::get_roi_for_pos                  */
/*****************************************************************************/

bool kdu_region_animator_roi::get_roi_for_pos(double s, kdu_dims &roi)
{
  if (!is_valid)
    return false;
  if (static_roi || (s >= 1.0))
    { roi = final_roi; return true; }
  if (s <= 0.0)
    { roi = initial_roi; return true; }
  roi.pos.x = (int) kdu_round(initial_roi.pos.x*(1-s)+final_roi.pos.x*s);
  roi.pos.y = (int) kdu_round(initial_roi.pos.y*(1-s)+final_roi.pos.y*s);
  kdu_coords initial_lim = initial_roi.pos + initial_roi.size;
  kdu_coords final_lim = final_roi.pos + final_roi.size;
  roi.size.x = (int) kdu_round(initial_lim.x*(1-s)+final_lim.x*s);
  roi.size.y = (int) kdu_round(initial_lim.y*(1-s)+final_lim.y*s);
  roi.size -= roi.pos;
  return true;
}

/*****************************************************************************/
/*                kdu_region_animator_roi::get_time_for_pos                  */
/*****************************************************************************/

double kdu_region_animator_roi::get_time_for_pos(double s)
{
  if (static_roi || (s <= 0.0))
    return 0.0;
  
  // For 0 <= s <= trans_pos, we have s = 0.5*A*t^2
  if (s <= trans_pos)
    return sqrt(2.0*s/acceleration);
  
  // For trans_pos <= s <= 1-trans_pos, s increases linearly with t
  if (s < (1.0-trans_pos))
    return time_to_trans_pos + (s-trans_pos) / max_ds_dt;
  
  // For s >= 1-trans_pos, s decreases quadratically
  if (s >= 1.0)
    return total_transition_time;
  
  return total_transition_time - sqrt(2.0*(1.0-s)/acceleration);
}

/*****************************************************************************/
/*                 kdu_region_animator_roi::get_pos_for_time                 */
/*****************************************************************************/

double kdu_region_animator_roi::get_pos_for_time(double t)
{
  if (static_roi || (t <= 0.0))
    return 0.0;
  if (t <= time_to_trans_pos)
    return 0.5*acceleration*t*t;
  if (t < (total_transition_time-time_to_trans_pos))
    return trans_pos + (t-time_to_trans_pos) * max_ds_dt;
  if (t >= total_transition_time)
    return 1.0;
  t = total_transition_time - t;
  return 1.0 - 0.5*acceleration*t*t;
}


/*===========================================================================*/
/*                                kdra_layer                                 */
/*===========================================================================*/

/*****************************************************************************/
/*                          kdra_layer::configure                            */
/*****************************************************************************/

void
  kdra_layer::configure(jpx_source *src, kdu_dims source_dims,
                        kdu_dims target_dims, double scale,
                        kdu_coords ref_comp_subsampling[])
{
  // Start by obtaining the compositing layer and primary codestream interfaces
  jpx_layer_source layer = src->access_layer(this->layer_idx,false);
  assert(layer.exists());
  int ref_comp_idx=0, lut_idx, main_stream_idx=0, fmt;
  jp2_channels channels = layer.access_channels();
  channels.get_colour_mapping(0,ref_comp_idx,lut_idx,main_stream_idx,fmt);
  jpx_codestream_source stream = src->access_codestream(main_stream_idx,false);
  assert(stream.exists());
  
  kdu_coords align, stream_sampling, stream_denominator;
  int str_idx, c=0;
  while ((str_idx=layer.get_codestream_registration(c++,align,stream_sampling,
                                                    stream_denominator)) >= 0)
    if (str_idx == main_stream_idx)
      break;
  assert(str_idx >= 0); // Must have encountered the break condition above
  
  // Find/estimate codestream canvas dimensions and sub-sampling factors
  // for each available resolution level of the reference image component.
  int d, max_discard_levels = 32;
  kdu_dims canvas_dims;
  if (stream.stream_ready())
    { 
      jpx_input_box cs_box;
      stream.open_stream(&cs_box);
      kdu_codestream ifc;
      try {
        ifc.create(&cs_box);
        ifc.get_dims(-1,canvas_dims);
        max_discard_levels = ifc.get_min_dwt_levels();
        assert(max_discard_levels <= 32); // Constrained by the standard
        for (d=max_discard_levels; d >= 0; d--)
          { 
            ifc.apply_input_restrictions(0,0,d,0,NULL,
                                         KDU_WANT_OUTPUT_COMPONENTS);
            ifc.get_subsampling(ref_comp_idx,ref_comp_subsampling[d],true);
          }
      } catch (...) {
        ifc.destroy();
      }
      ifc.destroy();
      cs_box.close();
    }
  else
    { // We will have to synthesize sub-sampling factors 
      canvas_dims.size = stream.access_dimensions().get_size();
      kdu_coords subs = kdu_coords(1,1);
      if ((ref_comp_subsampling[0].x > 0) && (ref_comp_subsampling[0].y > 0))
        subs = ref_comp_subsampling[0]; // Re-use previous codestream values
      for (d=0; d <= max_discard_levels; d++, subs.x+=subs.x, subs.y+=subs.y)
        ref_comp_subsampling[d] = subs;
    }
 
  // Next, fill in any missing values for `source_dims' and `target_dims'.
  bool synthesized_source_dims = false;
  if (!source_dims)
    { 
      source_dims.pos = kdu_coords(0,0);
      source_dims.size.x =
        long_ceil_ratio(((kdu_long) canvas_dims.size.x) *
                        stream_sampling.x,stream_denominator.x);
      source_dims.size.y =
        long_ceil_ratio(((kdu_long) canvas_dims.size.y) *
                        stream_sampling.y,stream_denominator.y);
      synthesized_source_dims = true;
    }
  if (!target_dims)
    target_dims = source_dims;
  
  // Convert `source_dims' to dimensions on the source canvas
  if (synthesized_source_dims)
    source_dims = canvas_dims;
  else
    { 
      kdu_coords min = source_dims.pos;
      kdu_coords lim = min + source_dims.size;
      min.x = long_ceil_ratio(((kdu_long) min.x)*stream_denominator.x,
                              stream_sampling.x);
      min.y = long_ceil_ratio(((kdu_long) min.y)*stream_denominator.y,
                              stream_sampling.y);
      lim.x = long_ceil_ratio(((kdu_long) lim.x)*stream_denominator.x,
                              stream_sampling.x);
      lim.y = long_ceil_ratio(((kdu_long) lim.y)*stream_denominator.y,
                              stream_sampling.y);
      source_dims.pos = min; source_dims.size = lim-min;
      source_dims.pos += canvas_dims.pos;
    }
  
  // Now find scaling factors relative to the high resolution codestream canvas
  double scale_x, scale_y;
  scale_x = target_dims.size.x / (double) source_dims.size.x;
  scale_y = target_dims.size.y / (double) source_dims.size.y;
  scale_x *= scale;
  scale_y *= scale;
  
  // Work out tolerance to be used by `find_min_max_jpip_woi_scales'
  double cs_x=scale_x*canvas_dims.size.x, cs_y=scale_y*canvas_dims.size.y;
  scale_tolerance = 2.0 / ((cs_x < cs_y)?cs_x:cs_y);
  if (scale_tolerance > 0.5)
    scale_tolerance = 0.5;
  
  // Now we are ready to determine the number of discard levels which we
  // should use to approximate the required scaling factors; the logic here
  // precisely mirrors that in `kdrc_stream::set_scale' in the implementation
  // of `kdu_region_compositor'.
  active_discard_levels = 0;
  while ((active_discard_levels < max_discard_levels) &&
         (((scale_x*ref_comp_subsampling[active_discard_levels].x) < 0.6) ||
          ((scale_y*ref_comp_subsampling[active_discard_levels].y) < 0.6)))
    active_discard_levels++;
  
  // Convert `scale_x' and `scale_y' to measure scaling factors relative to
  // the active image component, after applying `discard_levels'.
  scale_x *= ref_comp_subsampling[active_discard_levels].x;
  scale_y *= ref_comp_subsampling[active_discard_levels].y;
  
  // Work out correction factors to be applied to the composited image
  // dimensions (at the current `scale') such that the the corrected dimensions
  // correspond exacly to `active_discard_levels' from a JPIP server's
  // point of view.
  this->jpip_correction_scale_x = ref_comp_subsampling[0].x / scale_x;
  this->jpip_correction_scale_y = ref_comp_subsampling[0].y / scale_y;
  
  // Record relevant sub-sampling for `find_min_max_jpip_woi_scales' to use.
  this->active_subs = ref_comp_subsampling[active_discard_levels];
  if (active_discard_levels < max_discard_levels)
    this->low_subs = ref_comp_subsampling[active_discard_levels+1];
  else
    this->low_subs = active_subs;
  if (active_discard_levels > 0)
    this->high_subs = ref_comp_subsampling[active_discard_levels-1];
}

/*****************************************************************************/
/*                 kdra_layer::find_min_max_jpip_woi_scales                  */
/*****************************************************************************/

void
  kdra_layer::find_min_max_jpip_woi_scales(double min_scale[],
                                           double max_scale[])
{
  // Find scaling factors which would match `active_discard_levels' exactly
  // (as far as a JPIP server is concerned), if applied to the scaled
  // composition dimensions (using the `scale' value that was passed to
  // `configure').
  double active_scale_x = this->jpip_correction_scale_x;
  double active_scale_y = this->jpip_correction_scale_y;
  double active_area = active_scale_x * active_scale_y;
  
  // Find a single minimum scaling factor for each rounding case
  if (low_subs != active_subs)
    { // At least one more discard level was found by `configure'
      double low_scale_x = active_scale_x *
        ((double) active_subs.x) / ((double) low_subs.x);
      double low_scale_y = active_scale_y *
        ((double) active_subs.y) / ((double) low_subs.y);
      double low_area = low_scale_x * low_scale_y;
      double min_area = (active_area + low_area) * 0.5 * (1.0+scale_tolerance);
      double tmp_scale = sqrt(min_area);
      if (tmp_scale > min_scale[0])
        min_scale[0] = tmp_scale;
      tmp_scale = (low_scale_x < low_scale_y)?low_scale_x:low_scale_y;
      tmp_scale *= (1.0+scale_tolerance);
      if (tmp_scale > min_scale[1])
        min_scale[1] = tmp_scale;
    }
  else
    { // Don't have information about a larger number of discard levels, but
      // the server might discover one for some image components.  Assume worst
      // case in which only one of the dimensions is halved if we go to more
      // discard levels -- can happen in Part-2.
      double min_area = active_area * 0.75 * (1.0+scale_tolerance);
      double tmp_scale = sqrt(min_area);
      if (tmp_scale > min_scale[0])
        min_scale[0] = tmp_scale;
      double min1 = active_scale_x*0.5;
      min1 = (min1 < active_scale_y)?min1:active_scale_y;
      double min2 = active_scale_y*0.5;
      min2 = (min2 < active_scale_x)?min2:active_scale_x;
      tmp_scale = (min1 > min2)?min1:min2;
      tmp_scale *= (1.0+scale_tolerance);
      if (tmp_scale > min_scale[1])
        min_scale[1] = tmp_scale;
    }
  
  // Find a single maximum scale for each rounding case
  if (active_discard_levels > 0)
    { 
      double high_scale_x = active_scale_x *
        ((double) active_subs.x) / ((double) high_subs.x);
      double high_scale_y = active_scale_y *
        ((double) active_subs.y) / ((double) high_subs.y);
      double high_area = high_scale_x * high_scale_y;
      double max_area = (active_area + high_area) * 0.5*(1.0-scale_tolerance);
      double tmp_scale = sqrt(max_area);
      if (tmp_scale < max_scale[0])
        max_scale[0] = tmp_scale;
      tmp_scale=(high_scale_x < high_scale_y)?high_scale_x:high_scale_y;
      tmp_scale *= (1.0-scale_tolerance);
      if (tmp_scale < max_scale[1])
        max_scale[1] = tmp_scale;
    }
}


/*===========================================================================*/
/*                                kdra_frame                                 */
/*===========================================================================*/

/*****************************************************************************/
/*                            kdra_frame::init                               */
/*****************************************************************************/

void kdra_frame::init()
{
  metanode = incomplete_prev = jpx_metanode();
  incomplete_meta_next = NULL;
  incomplete_meta_prev = NULL;
  incomplete_link_target = false;
  incomplete_descendants = false;
  incomplete_top_level = false;
  incomplete_numlist_above = false;
  
  metanode_roi = kdu_dims();
  numlist = jpx_metanode();
  single_numlist_layer=0;
  metadata_frame_id = -1;
  first_match = false;
  last_match = false;
  may_extend_match = false;
  match_track_or_above = false;
  match_track_idx = 0;
  
  frame_idx = -1;
  jpx_frm = jpx_frame();
  source_duration = -1.0;
  frame_size = kdu_coords();
  frame_roi = kdu_dims();
  have_all_source_info = false;
  
  frame_reqs = NULL;
  
  generated = false;
  reset_pan_params();
  display_time = -1.0;
  activation_time = -1.0;
  cur_display_time = -1.0;
  remove_conditional_frame_step();
  
  next = prev = NULL;
}

/*****************************************************************************/
/*                       kdra_frame::calculate_duration                      */
/*****************************************************************************/

double kdra_frame::calculate_duration(bool ignore_roi_pan_duration)
{
  if (in_auto_pan_preamble() && !ignore_roi_pan_duration)
    return roi_pan_duration;
  double duration = owner->custom_frame_interval;
  if (duration < 0.0)
    duration = source_duration * owner->native_frame_interval_multiplier;
  if (owner->metadata_driven && (frame_idx >= 0) &&
      (owner->intra_frame_interval > 0.0))
    { // See if we need to use `intra_frame_interval'
      if (source_duration == 0.0)
        duration = owner->intra_frame_interval;
      else
        { 
          kdra_frame *p_frm = (prev==this)?NULL:prev;
          kdra_frame *n_frm = (next==this)?NULL:next;
          if (((p_frm != NULL) && (p_frm->frame_idx == frame_idx)) ||
              ((n_frm != NULL) && (n_frm->frame_idx == frame_idx)))
            duration = owner->intra_frame_interval;
          else if (((p_frm != NULL) && (p_frm->frame_idx < 0) &&
                    (p_frm->find_source_info() >= 0) &&
                    (p_frm->frame_idx >= 0) &&
                    (p_frm->frame_idx == frame_idx)) ||
                   ((n_frm != NULL) && (n_frm->frame_idx < 0) &&
                    (n_frm->find_source_info() >= 0) &&
                    (n_frm->frame_idx >= 0) &&
                    (n_frm->frame_idx == frame_idx)))
            duration = owner->intra_frame_interval;
        }
    }
  return (duration < 0.0)?0.0:duration;
}

/*****************************************************************************/
/*               kdra_frame::calculate_expected_total_duration               */
/*****************************************************************************/

double
  kdra_frame::calculate_expected_total_duration(float scale)
{
  assert(have_all_source_info);
  double duration = calculate_duration(true);
  if (duration <= 0.0)
    duration = 1.0; // Assign pause frames a duration here
  if ((!frame_roi.is_empty()) && (owner->max_pan_speed > 0.0) &&
      (owner->pan_acceleration > 0.0) && (prev != NULL) &&
      (prev->frame_idx == this->frame_idx) &&
      (!prev->frame_roi.is_empty()))
    { // May need to account for an auto-pan preamble
      assert(owner->metadata_driven && this->numlist.exists());
      double dist, max_delta_x, max_delta_y;
      dist = measure_roi_distance(frame_roi,prev->frame_roi,
                                  max_delta_x,max_delta_y);
      dist *= scale;
      double inv_dist = 1.0 / dist;
      kdu_region_animator_roi dyn_roi;
      duration += dyn_roi.init(owner->pan_acceleration*inv_dist,
                               owner->max_pan_speed*inv_dist);
    }
  return duration;
}

/*****************************************************************************/
/*                  kdra_frame::expand_incomplete_metadata                   */
/*****************************************************************************/

int kdra_frame::expand_incomplete_metadata()
{
  kdu_dims roi;
  jpx_metanode nl; // Temporary number list
  if (incomplete_link_target)
    { 
      jpx_metanode_link_type link_type=JPX_METANODE_LINK_NONE;
      jpx_metanode link_target = metanode.get_link(link_type,true);
      if (link_target.exists())
        nl = get_link_numlist_and_roi(metanode,roi);
      else if (link_type != JPX_METANODE_LINK_NONE)
        return 1; // Still need more data to resolve the link target
      incomplete_link_target = false;
    }
  
  if (nl.exists())
    set_metadata_complete();
  else if (incomplete_descendants)
    { // Search at or below the current node for appropriate nodes to
      // drive the animation
      nl = metanode.get_numlist_container();
      bool node_is_numlist = (metanode == nl);
      if (node_is_numlist && incomplete_numlist_above)
        { // Number list inside a number list -- don't go further down
          owner->remove_frame(this);
          return 0;
        }
      roi = metanode.get_bounding_box();
      if (!roi.is_empty())
        set_metadata_complete();
      else
        { // Current node is not an ROI node; look at descendants
          int num_added = 0;
          jpx_metanode scan=incomplete_prev;
          while ((scan = metanode.get_next_descendant(scan,0)).exists())
            { 
              incomplete_prev = scan;
              kdra_frame *elt = owner->insert_new_frame(this,
                                                        !owner->reverse_mode);
              elt->metanode = scan;
              elt->set_metadata_incomplete(false,node_is_numlist ||
                                           incomplete_numlist_above);
              num_added += elt->expand_incomplete_metadata();
            }
          
          if (!metanode.check_descendants_complete(0,NULL))
            return num_added+1; // Need to come back later
          
          if (incomplete_numlist_above || (num_added > 0) ||
              !(incomplete_top_level || node_is_numlist))
            { // Not appropriate to add the current node
              owner->remove_frame(this);
              return num_added;
            }
          
          set_metadata_complete();
          
          if (!node_is_numlist)
            { 
              scan = metanode.get_parent();
              while (scan.exists() && (scan != nl) &&
                     (roi = scan.get_bounding_box()).is_empty())
                scan = scan.get_parent();
              if (!scan)
                { // Found nothing
                  owner->remove_frame(this);
                  return 0;
                }
            }
        }
      if (!nl)
        { // We are not associated with any number lists
          owner->remove_frame(this);
          return 0;
        }
    }
  else
    return 0; // The current frame was not incomplete to start with!!
  
  // If we get here, we are no longer incomplete and appear to represent
  // an animation frame.  However, we need to check that the number list
  // has sufficient information.
  assert(!(incomplete_descendants || incomplete_link_target));
  this->numlist = nl;
  int numlist_layers=0, numlist_streams=0; bool rres;
  nl.get_numlist_info(numlist_streams,numlist_layers,rres);
  if (numlist_layers == 0)
    { /* There are no referenced compositing layers.  We will allow this
         if there are codestream references, so long as either: a) we are
         not in single-layer mode; or b) at least one of the codestream
         references is used by compositing layer 0 and we have a region of
         interest. */
      bool acceptable = (numlist_streams > 0);
      if (acceptable && owner->single_layer_mode && !roi.is_empty())
        { // Check compatibility with layer 0
          int stream_idx, w=0;
          while ((stream_idx =
                  owner->jpx_src->get_layer_codestream_id(0,w++)) >= 0)
            if (nl.test_numlist_stream(stream_idx))
              break;
          acceptable = (stream_idx >= 0);
        }
      if (!acceptable)
        { 
          owner->remove_frame(this);
          return 0;
        }
    }
  
  // Finally, we have a completed frame
  this->metanode_roi = roi;
  this->metadata_frame_id = owner->metadata_frame_count++;
  this->first_match = this->last_match = true;
  this->may_extend_match = true; // Until we know any better
  if (this->extend_numlist_match_span() < 0)
    { // Node was removed -- matching frames do not exist!
      return 0;
    }
  return 1;
}

/*****************************************************************************/
/*                  kdra_frame::calculate_metadata_id_in_span                */
/*****************************************************************************/

int kdra_frame::calculate_metadata_id_in_span(kdra_frame *tgt)
{
  assert(tgt->numlist == this->numlist);
  int gap = 0;
  if (owner->single_layer_mode)
    { 
      gap = tgt->single_numlist_layer - this->single_numlist_layer;
    }
  else
    { // Frame composition mode; metadata-id's measured relative to frame idx
      assert((tgt->frame_idx >= 0) && (this->frame_idx >= 0));
      gap = (tgt->frame_idx - this->frame_idx);
    }
  return this->metadata_frame_id + gap;
}

/*****************************************************************************/
/*                  kdra_frame::reconcile_match_track_info                   */
/*****************************************************************************/

void
  kdra_frame::reconcile_match_track_info()
{
  assert(owner->metadata_driven && !owner->single_layer_mode);
  kdra_frame *scan=this;
  while ((scan->next != NULL) &&
         !((owner->reverse_mode)?(scan->first_match):(scan->last_match)))
    { 
      scan = scan->next;
      assert(scan->numlist == this->numlist);
      assert(scan->match_track_idx > 0);
      assert((scan->match_track_idx <= this->match_track_idx) &&
             (scan->match_track_or_above || !this->match_track_or_above));
      scan->match_track_idx = this->match_track_idx;
      scan->match_track_or_above = this->match_track_or_above;
    }
  scan = this;
  while ((scan->prev != NULL) &&
         !((owner->reverse_mode)?(scan->last_match):(scan->first_match)))
    { 
      scan = scan->prev;
      assert(scan->numlist == this->numlist); 
      assert(scan->match_track_idx > 0);
      assert((scan->match_track_idx <= this->match_track_idx) &&
             (scan->match_track_or_above || !this->match_track_or_above));
      scan->match_track_idx = this->match_track_idx;
      scan->match_track_or_above = this->match_track_or_above;
    }      
}

/*****************************************************************************/
/*                  kdra_frame::extend_numlist_match_span                    */
/*****************************************************************************/

int kdra_frame::extend_numlist_match_span()
{
  if (!(last_match && may_extend_match && numlist.exists()))
    { // We should not have called here
      may_extend_match = false;
      return 0;
    }
  kdra_frame *new_elt = NULL;
  if (owner->single_layer_mode)
    { 
      int count=0;
      may_extend_match = !numlist.count_numlist_layers(count);
      if (count <= (this->single_numlist_layer+1))
        return 0; // Cannot extend

      // Build a new frame to hold `last_frm'
      new_elt = owner->insert_new_frame(this,owner->reverse_mode);
      new_elt->metanode = this->metanode;
      new_elt->metanode_roi = this->metanode_roi;
      new_elt->numlist = this->numlist;
      new_elt->single_numlist_layer = count-1;
      new_elt->first_match = false;
      new_elt->last_match = true;
      new_elt->may_extend_match = this->may_extend_match;
      this->last_match = this->may_extend_match = false;      
    }
  else
    { // Frame composition mode
      jpx_composition comp = owner->composition_rules;
      int match_inst, match_result;
      if (!jpx_frm.exists())
        { // Need to instantiate the current frame before doing anything else
          assert(this->first_match);
          if ((match_result =
               comp.find_numlist_match(jpx_frm,match_inst,0,numlist,1,true,
                                       JPX_FRAME_MATCH_LATER_TRACKS)) <= 0)
            { // No match found
              if (match_result < 0)
                owner->remove_frame(this); // Failure is permanent
              return match_result;
            }
          match_track_idx = jpx_frm.get_track_idx(match_track_or_above);
          match_track_idx = (match_track_idx == 0)?1:match_track_idx;
          frame_idx = jpx_frm.get_frame_idx();
        }
      if (check_numlist_fully_used_by_frame(numlist,jpx_frm))
        { 
          may_extend_match = false;
          return 0;
        }

      assert(match_track_idx > 0);
      jpx_frame next_frm = jpx_frm.access_next(match_track_idx,true);
      if (!next_frm)
        return 0;
      int match_flags = (match_track_or_above)?JPX_FRAME_MATCH_LATER_TRACKS:0;
      match_result =
        comp.find_numlist_match(next_frm,match_inst,match_track_idx,
                                numlist,1,true,match_flags);
      if (match_result < 0)
        may_extend_match = false; // There can be no more matches
      if (match_result <= 0)
        return 0;
      
      // If we get here, there is at least one more matching frame; let's
      // look for the last one.
      bool last_in_context=false, reconcile_track_changes=false;
      kdu_uint32 trk_idx=next_frm.get_track_idx(last_in_context);
      if (trk_idx > match_track_idx)
        { reconcile_track_changes=true;  match_track_idx = trk_idx; }
      if (match_track_or_above && !last_in_context)
        { reconcile_track_changes=true;  match_track_or_above = false; }
      match_flags = (match_track_or_above)?JPX_FRAME_MATCH_LATER_TRACKS:0;
      match_flags |= JPX_FRAME_MATCH_REVERSE;
      jpx_frame last_frm;
      match_result =
        comp.find_numlist_match(last_frm,match_inst,match_track_idx,
                                numlist,1,true,match_flags);
      assert(match_result > 0);
      assert(last_frm.get_frame_idx() >= next_frm.get_frame_idx());
      trk_idx = last_frm.get_track_idx(last_in_context);
      if (trk_idx > match_track_idx)
        { reconcile_track_changes=true;  match_track_idx = trk_idx; }
      if (match_track_or_above && !last_in_context)
        { reconcile_track_changes=true;  match_track_or_above = false; }
      if (reconcile_track_changes)
        reconcile_match_track_info();
      
      // Build a new frame to hold `last_frm'
      new_elt = owner->insert_new_frame(this,owner->reverse_mode);
      new_elt->metanode = this->metanode;
      new_elt->metanode_roi = this->metanode_roi;
      new_elt->numlist = this->numlist;
      new_elt->first_match = false;
      new_elt->last_match = new_elt->may_extend_match = true;
      this->last_match = this->may_extend_match = false;
      new_elt->match_track_or_above = this->match_track_or_above;
      new_elt->match_track_idx = this->match_track_idx;
      new_elt->frame_idx = last_frm.get_frame_idx();
      new_elt->jpx_frm = last_frm;
    }
  
  if (new_elt != NULL)
    { // Make the metadata_frame_id changes
      new_elt->metadata_frame_id = this->metadata_frame_id; // Temporary value
      int mfid = this->calculate_metadata_id_in_span(new_elt);
      int mfid_inc = mfid - this->metadata_frame_id;
      assert(mfid_inc > 0);
      owner->metadata_frame_count += mfid_inc;
      if (owner->reverse_mode)
        { 
          for (kdra_frame *scan=new_elt; scan != NULL; scan=scan->prev)
            { 
              scan->metadata_frame_id += mfid_inc;
              if (scan == owner->head)
                break;
            }
        }
      else
        { 
          for (kdra_frame *scan=new_elt; scan != NULL; scan=scan->next)
            { 
              scan->metadata_frame_id += mfid_inc;
              if (scan == owner->tail)
                break;
            }
        }
      assert(new_elt->metadata_frame_id ==
             (this->metadata_frame_id + mfid_inc));
      return 1;
    }
  return 0;
}

/*****************************************************************************/
/*               kdra_frame::instantiate_next_numlist_match                  */
/*****************************************************************************/

void kdra_frame::instantiate_next_numlist_match()
{
  assert(owner->metadata_driven);
  if ((first_match && owner->reverse_mode) ||
      (last_match && !owner->reverse_mode))
    return; // We are already at the end
  if ((next == NULL) || (next->numlist != numlist))
    { // Not part of a discovered span
      assert(0);
      return;
    }
  kdra_frame *new_elt = NULL;
  if (owner->single_layer_mode)
    { 
      int new_idx = this->single_numlist_layer + ((owner->reverse_mode)?-1:1);
      if (new_idx == next->single_numlist_layer)
        return; // We already have the required frame
      // Build a new frame to hold `match_frm'
      new_elt = owner->insert_new_frame(this,false);
      new_elt->metanode = this->metanode;
      new_elt->metanode_roi = this->metanode_roi;
      new_elt->numlist = this->numlist;
      new_elt->single_numlist_layer = new_idx;
    }
  else
    { // frame composition mode
      jpx_composition comp = owner->composition_rules;
      if (!jpx_frm.exists())
        return; // Should not really happen
      assert(match_track_idx > 0);
      jpx_frame match_frm = jpx_frm;
      bool last_in_context=false, reconcile_track_changes=false;
      do { 
        if (owner->reverse_mode)
          match_frm = match_frm.access_prev(match_track_idx,true);
        else
          match_frm = match_frm.access_next(match_track_idx,true);
        if (!match_frm.exists())
          break;
        if (match_frm == next->jpx_frm)
          { // We already have the next frame
            match_frm = jpx_frame();
            break;
          }
        int match_inst, match_result, match_flags;
        match_flags = (match_track_or_above)?JPX_FRAME_MATCH_LATER_TRACKS:0;
        if (owner->reverse_mode)
          match_flags |= JPX_FRAME_MATCH_REVERSE;
        match_result =
          comp.find_numlist_match(match_frm,match_inst,match_track_idx,
                                  numlist,1,true,match_flags);
        assert(match_result >= 0);
        if ((match_result == 0) || (match_frm == next->jpx_frm))
          { 
            match_frm = jpx_frame();
            break;
          }
        kdu_uint32 trk_idx=match_frm.get_track_idx(last_in_context);
        if (trk_idx > match_track_idx)
          { reconcile_track_changes=true;  match_track_idx = trk_idx; }
        if (match_track_or_above && !last_in_context)
          { reconcile_track_changes=true;  match_track_or_above = false; }
      } while (!check_novel_frame_match(match_frm,jpx_frm,numlist));
      if (reconcile_track_changes)
        this->reconcile_match_track_info();
      if (!match_frm)
        return;
      
      // Build a new frame to hold `match_frm'
      new_elt = owner->insert_new_frame(this,false);
      new_elt->metanode = this->metanode;
      new_elt->metanode_roi = this->metanode_roi;
      new_elt->numlist = this->numlist;
      new_elt->match_track_or_above = this->match_track_or_above;
      new_elt->match_track_idx = this->match_track_idx;
      new_elt->frame_idx = match_frm.get_frame_idx();
      new_elt->jpx_frm = match_frm;
    }

  if (new_elt != NULL)
    { // Set the metadata_frame_id 
      new_elt->metadata_frame_id =
        this->calculate_metadata_id_in_span(new_elt);
      if (owner->reverse_mode)
        assert(new_elt->metadata_frame_id < this->metadata_frame_id);
      else
        assert(new_elt->metadata_frame_id > this->metadata_frame_id);
    }
}

/*****************************************************************************/
/*                        kdra_frame::find_source_info                       */
/*****************************************************************************/

int kdra_frame::find_source_info()
{
  if (is_metadata_incomplete())
    { 
      assert(!have_all_source_info);
      return 0;
    }
  if (have_all_source_info)
    return 1;
  
  if (owner->mj2_src != NULL)
    { // Find frame size and duration from the MJ2 video track
      assert(frame_idx >= 0);
      if (owner->current != NULL)
        frame_size = owner->current->frame_size;
      else
        { // Get frame dimensions from the `mj2_src' object
          jp2_dimensions dims = owner->mj2_track->access_dimensions();
          frame_size = dims.get_size();
        }
      assert((frame_size.x > 0) && (frame_size.y > 0));
      source_duration =
        (((double) owner->mj2_track->get_frame_period(frame_idx)) /
         ((double) owner->mj2_track->get_timescale()));
      have_all_source_info = true;
      owner->note_new_frame(this);
      return 1;
    }
  
  if (owner->single_layer_mode)
    { // Find frame index, geometry and duration from the JPX compositing layer 
      int max_compositing_layers; // -ve if not yet known
      if (!owner->jpx_src->count_compositing_layers(max_compositing_layers))
        max_compositing_layers = -1;
      source_duration = 1.0;
      if (frame_idx < 0)
        { // Interpret compositing layer as frame index
          frame_idx = numlist.get_numlist_layer(single_numlist_layer,-1);
          if (frame_idx < 0)
            { // Special match with compositing layer 0 only 
              assert(single_numlist_layer == 0);
              frame_idx = 0;
            }
        }
      if ((frame_idx < 0) ||
          ((frame_idx >= owner->max_source_frames) &&
           (owner->max_source_frames >= 0)))
        { 
          owner->remove_frame(this);
          return -1;
        }
      if ((max_compositing_layers >= 0) &&
          ((max_compositing_layers < owner->max_source_frames) ||
           (owner->max_source_frames < 0)))
        { 
          bool frm_survives = (frame_idx < max_compositing_layers);
          owner->set_max_frames(max_compositing_layers);
          if (!frm_survives)
            return -1; // Must have been removed by above call
        }
      jpx_layer_source layer = owner->jpx_src->access_layer(frame_idx,false);
      if (!layer)
        return 0; // We don't yet have the required header boxes.
      frame_size = layer.get_layer_size();
      assert((frame_size.x > 0) && (frame_size.y > 0));
      if (!metanode_roi.is_empty())
        { 
          int w=0, stream_idx;
          kdu_coords align, sampling, denominator;
          while ((stream_idx =
                  layer.get_codestream_registration(w++,align,sampling,
                                                    denominator)) >= 0)
            if (numlist.test_numlist_stream(stream_idx))
              { // Found a matching codestream
                kdu_dims src_dims, tgt_dims;
                src_dims.size = tgt_dims.size = frame_size;
                jpx_composited_orientation orientation; // normal orientation
                frame_roi =
                  map_codestream_rgn_to_frame(metanode_roi,
                                              align,sampling,denominator,
                                              src_dims,tgt_dims,orientation);
                break;
              }
        }
      have_all_source_info = true;
      owner->note_new_frame(this);
      return 1;
    }
  
  // If we get here, we are looking for a JPX animation frame.  This is the
  // most complex case.
  assert(owner->composition_rules.exists()); // else should not be here
  if (!jpx_frm.exists())
    { // Find the composition frame
      if (frame_idx >= 0)
        { // We know the frame index; just have to look it up
          jpx_frm =
            owner->composition_rules.access_frame(owner->track_idx,
                                                  frame_idx,false);
          if (!jpx_frm)
            { // Count frames to see if we are waiting for more data
              int count=0, result=0;
              if (owner->composition_rules.count_track_frames(owner->track_idx,
                                                              count))
                { 
                  if (frame_idx >= count)
                    result = -1;
                  owner->set_max_frames(count); // Removes us if `result' < 0
                }
              return result;
            }
        }
      else
        { // Need to use `numlist'
          assert(this->first_match);
          int match_inst, match_result, flags=JPX_FRAME_MATCH_LATER_TRACKS;
          if ((match_result =
               owner->composition_rules.find_numlist_match(jpx_frm,match_inst,
                                                           0,numlist,1,true,
                                                           flags)) <= 0)
            { // No match found
              if (match_result < 0)
                owner->remove_frame(this); // Failure is permanent
              return match_result;
            }
          match_track_idx = jpx_frm.get_track_idx(match_track_or_above);
          match_track_idx = (match_track_idx == 0)?1:match_track_idx;
          frame_idx = jpx_frm.get_frame_idx();
        }
    }
  owner->composition_rules.get_global_info(frame_size);
  assert(jpx_frm.exists());
  kdu_long start_point, frm_duration;
  jpx_frm.get_info(start_point,frm_duration);
  double inv_timescale = 1.0 / owner->composition_rules.get_timescale();
  source_duration = frm_duration * inv_timescale;
  
  // Now that we are here, we just need to verify that all compositing layer
  // and codestream headers are available and figure out the `frame_roi'
  // member, if required.
  int numlist_layers=0, numlist_streams=0; bool rres;
  numlist.get_numlist_info(numlist_streams,numlist_layers,rres);
  kdu_dims src_dims, tgt_dims;
  jpx_composited_orientation orientation;
  int layer_idx, inst_idx=0;
  frame_roi = kdu_dims();
  while (jpx_frm.get_instruction(inst_idx++,layer_idx,src_dims,tgt_dims,
                                 orientation))
    { 
      jpx_layer_source layer = owner->jpx_src->access_layer(layer_idx,false);
      if (!layer)
        return 0; // Not all source info available yet.
      if (!numlist.exists())
        continue; // No `frame_roi'
      if ((numlist_layers > 0) && !numlist.test_numlist_layer(layer_idx))
        continue; // This layer does not contribute to `frame_roi'
      int stream_idx=-1;
      kdu_coords align, sampling, denominator;
      if ((numlist_layers == 0) || !metanode_roi.is_empty())
        { // Need a codestream match
          int w=0;
          while ((stream_idx =
                  layer.get_codestream_registration(w++,align,sampling,
                                                    denominator)) >= 0)
            if (numlist.test_numlist_stream(stream_idx))
              break;
          if (stream_idx < 0)
            continue; // This layer does not contribute to `frame_roi'
        }
      if (src_dims.is_empty())
        { src_dims.pos = kdu_coords();
          src_dims.size = layer.get_layer_size(); }
      if (tgt_dims.is_empty())
        { 
          tgt_dims.size = src_dims.size;
          if (orientation.transpose)
            tgt_dims.size.transpose();
        }
      kdu_dims layer_roi;
      if (metanode_roi.is_empty())
        layer_roi = tgt_dims;
      else
        layer_roi = map_codestream_rgn_to_frame(metanode_roi,align,sampling,
                                                denominator,src_dims,tgt_dims,
                                                orientation);
      if (!layer_roi.is_empty())
        frame_roi.augment(layer_roi);
    }
  
  have_all_source_info = true;
  owner->note_new_frame(this);
  return 1;
}

/*****************************************************************************/
/*                    kdra_frame::request_unknown_layers                     */
/*****************************************************************************/

bool
  kdra_frame::request_unknown_layers(kdu_window *wnd)
{
  if (have_all_source_info || (owner->jpx_src == NULL) || (frame_idx < 0))
    return false;
  if (owner->single_layer_mode)
    { 
      if (owner->jpx_src->access_layer(frame_idx,false).exists())
        return false;
      kdu_sampled_range rg(frame_idx);
      rg.set_context_type(KDU_JPIP_CONTEXT_JPXL);
      wnd->contexts.add(rg);
      return true;
    }
  
  // If we get here, we are dealing with a JPX animation frame
  assert(jpx_frm.exists());
  kdu_dims src_dims, tgt_dims;
  jpx_composited_orientation orient;
  int layer_idx, m=0;
  bool added_something=false;
  while (jpx_frm.get_instruction(m++,layer_idx,src_dims,tgt_dims,orient))
    { 
      if (!owner->jpx_src->access_layer(layer_idx,false))
        { 
          kdu_sampled_range rg(layer_idx);
          rg.set_context_type(KDU_JPIP_CONTEXT_JPXL);
          wnd->contexts.add(rg);
          added_something = true;
        }
    }
  return added_something;
}

/*****************************************************************************/
/*                     kdra_frame::construct_window_request                  */
/*****************************************************************************/

void
  kdra_frame::construct_window_request(const kdu_dims &viewport,
                                       const kdu_dims &user_roi,
                                       double scale, kdu_window &window)
{
  // Start by finding the region of interest, expressed at the frame's
  // full resolution (i.e., for `scale'=1).
  kdu_dims full_res_roi;
  if (!frame_roi.is_empty())
    { // Use `frame_roi' as the full resolution region of interest, but
      // we need to intersect it with a scaled and translated version
      // `viewport' (if non-empty); remember that `viewport' is expressed
      // in the scaled domain, whereas `frame_roi' is not.
      full_res_roi = frame_roi;
      if ((!viewport.is_empty()) && (scale > 0.0))
        { 
          kdu_dims view_dims;
          double inv_scale = 1.0 / scale;
          kdu_coords min=viewport.pos, lim=min+viewport.size;
          min.x = (int) floor(min.x*inv_scale);
          min.y = (int) floor(min.y*inv_scale);
          view_dims.size.x = ((int) ceil(lim.x*inv_scale)) - min.x;
          view_dims.size.y = ((int) ceil(lim.y*inv_scale)) - min.y;
          view_dims.pos.x = full_res_roi.pos.x +
            ((full_res_roi.size.x - view_dims.size.x) >> 1);
          view_dims.pos.y = full_res_roi.pos.y +
            ((full_res_roi.size.y - view_dims.size.y) >> 1);
          view_dims.pos.x--;   view_dims.pos.y--;   // Allow a little extra
          view_dims.size.x+=2; view_dims.size.y+=2; // around the viewport
          full_res_roi &= view_dims;
        }
    }
  else if (!user_roi.is_empty() && (scale > 0.0))
    { // We need to scale `user_roi' by 1/scale in order to find the full
      // resolution region of interest.
      double inv_scale = 1.0 / scale;
      kdu_coords min=user_roi.pos, lim=min+user_roi.size;
      full_res_roi.pos.x = (int) floor(min.x*inv_scale);
      full_res_roi.pos.y = (int) floor(min.y*inv_scale);
      full_res_roi.size.x = ((int) ceil(lim.x*inv_scale)) - full_res_roi.pos.x;
      full_res_roi.size.y = ((int) ceil(lim.y*inv_scale)) - full_res_roi.pos.y;
    }
  
  // Build a list of all the compositing layers that are involved,
  // together with all relevant information for these layers.
  kdra_layer *lyr, *layers=create_layer_descriptions(full_res_roi,scale);
  
  // Next, find minimum and maximum additional scaling factors (over and above
  // `scale') that are required to account for differences between the way
  // `kdu_region_compositor' determines its optimal rendering scale and the
  // way in which a JPIP server is supposed to do this for each codestream.
  // The `kdra_layer::active_subs' member represents the sub-sampling factors
  // associated with the reference image component that we expect
  // `kdu_region_compositor' to arrive at for the `scale' in question.
  double min_scale[2]={0.001,0.001};
  double max_scale[2]={2000000.0,2000000.0};
  for (lyr=layers; lyr != NULL; lyr=lyr->next)
    lyr->find_min_max_jpip_woi_scales(min_scale,max_scale);

  // Now determine the full image size and region of interest components
  // of the window-of-interest.
  int round_direction = 0;
  if (min_scale[0] > max_scale[0])
    round_direction = 1;
  if (min_scale[round_direction] > max_scale[round_direction])
    scale *= min_scale[round_direction];
  else if ((min_scale[round_direction] >= 1.0) ||
           (max_scale[round_direction] <= 1.0))
    { 
      if (max_scale[round_direction] > 1000000.0) // No upper bound
        scale *= 2.0 * min_scale[round_direction];
      else
        scale *= 0.5*(min_scale[round_direction]+max_scale[round_direction]);
    }
  
  kdu_dims full_dims;  full_dims.size = this->frame_size;
  if ((full_dims.size.x * scale) > (double) INT_MAX)
    scale = ((double) INT_MAX) / full_dims.size.x;
  if ((full_dims.size.y * scale) > (double) INT_MAX)
    scale = ((double) INT_MAX) / full_dims.size.y;  
  window.resolution.x = (int) floor(0.5 + full_dims.size.x * scale);
  window.resolution.y = (int) floor(0.5 + full_dims.size.y * scale);
  if (!(full_dims.is_empty() || full_res_roi.is_empty()))
    { 
      full_res_roi &= full_dims;
      kdu_coords min=full_res_roi.pos, lim=min+full_res_roi.size;
      scale = ((double) window.resolution.x) / ((double) full_dims.size.x);
      min.x = (int) floor(min.x * scale);
      lim.x = window.resolution.x -
        (int) floor((full_dims.size.x-lim.x) * scale);
      scale = ((double) window.resolution.y) / ((double) full_dims.size.y);
      min.y = (int) floor(min.y * scale);
      lim.y = window.resolution.y -
        (int) floor((full_dims.size.y-lim.y) * scale);
      window.region.pos = min;
      window.region.size = lim - min;
    }
  window.round_direction = round_direction;
  
  // Finally record the compositing layers of interest, along with
  // remapping id's.
  kdu_sampled_range rg;
  for (lyr=layers; lyr != NULL; lyr=lyr->next)
    { 
      rg.from = rg.to = lyr->layer_idx;
      rg.context_type = KDU_JPIP_CONTEXT_JPXL;
      rg.remapping_ids[0] = lyr->remapping_ids[0];
      rg.remapping_ids[1] = lyr->remapping_ids[1];
      window.contexts.add(rg);
    }

  // Cleanup
  while ((lyr = layers) != NULL)
    { layers = lyr->next; delete lyr; }
}

/*****************************************************************************/
/*                   kdra_frame::create_layer_descriptions                   */
/*****************************************************************************/
kdra_layer *
  kdra_frame::create_layer_descriptions(const kdu_dims &region, double scale)
{
  kdra_layer *elt, *head=NULL, *tail=NULL;
  assert(have_all_source_info);
  try { // In case something goes wrong
    if (owner->single_layer_mode)
      { 
        head = tail = elt = new kdra_layer;
        elt->layer_idx = frame_idx;
        elt->configure(owner->jpx_src,kdu_dims(),kdu_dims(),scale,
                       owner->ref_comp_subsampling);
      }
    else
      { 
        assert(jpx_frm.exists());
        jpx_frame_expander expander;
        if (!expander.construct(owner->jpx_src,jpx_frm,region))
          return NULL;
        int m, num_members=expander.get_num_members();
        for (m=0; m < num_members; m++)
          { 
            elt = new kdra_layer;
            if (tail == NULL)
              head = tail = elt;
            else
              tail = tail->next = elt;
            bool covers_composition=true;
            kdu_dims source_dims, target_dims;
            jpx_composited_orientation orientation;
            int instruction_idx =
              expander.get_member(m,elt->layer_idx,covers_composition,
                                  source_dims,target_dims,orientation);
            if ((instruction_idx >= 0) && !covers_composition)
              jpx_frm.get_original_iset(instruction_idx,elt->remapping_ids[0],
                                        elt->remapping_ids[1]);
            elt->configure(owner->jpx_src,source_dims,target_dims,scale,
                           owner->ref_comp_subsampling);
          }
      }
  } catch (...) {
    while ((tail=head) != NULL)
      { head = tail->next; delete tail; }
    throw;
  }
  return head;
}


/*===========================================================================*/
/*                               kdra_frame_req                              */
/*===========================================================================*/

/*****************************************************************************/
/*                          kdra_frame_req::detach                           */
/*****************************************************************************/

void kdra_frame_req::detach(bool from_tail)
{
  if (frm != NULL)
    { // Must be detaching either the first or the last frame-req associated
      // with `frm'.
      if (!from_tail)
        { // Usual case
          assert(frm->frame_reqs == this);
          frm->frame_reqs = this->next_in_frame;
        }
      else
        { 
          assert(this->next_in_frame == NULL);
          kdra_frame_req *scan=frm->frame_reqs, *prev=NULL;
          for (; scan != this; prev=scan, scan=scan->next_in_frame);
          if (prev == NULL)
            frm->frame_reqs = NULL;
          else
            prev->next_in_frame = NULL;
        }
      frm = NULL;
      next_in_frame = NULL;
    }
  for (int r=0; r < 2; r++)
    { 
      kdra_client_req *req = client_reqs[r];
      if (req == NULL)
        continue;
      client_reqs[r] = NULL;
      if (this == req->first_frame)
        { 
          if (this == req->last_frame)
            req->first_frame = req->last_frame = NULL;
          else
            { 
              req->first_frame = req->first_frame->next;
              assert((req->first_frame->client_reqs[0] == req) ||
                     (req->first_frame->client_reqs[1] == req));
            }
        }
      else if (this == req->last_frame)
        { 
          req->last_frame = req->last_frame->prev;
          assert((req->last_frame->client_reqs[0] == req) ||
                 (req->last_frame->client_reqs[1] == req));
        }
    }
}

/*****************************************************************************/
/*            kdra_frame_req::note_newly_initialized_display_time            */
/*****************************************************************************/

void
  kdra_frame_req::note_newly_initialized_display_time()
{
  assert(frm != NULL);
  double start_time = frm->display_time;
  for (kdra_frame_req *scan=this; scan != NULL; scan=scan->next)
    { 
      double interval =
        scan->expected_display_end - scan->expected_display_start;
      scan->expected_display_start = start_time;
      start_time += interval;
      scan->expected_display_end = start_time;
    }
}


/*===========================================================================*/
/*                            kdu_region_animator                            */
/*===========================================================================*/

/*****************************************************************************/
/*                  kdu_region_animator::kdu_region_animator                 */
/*****************************************************************************/

kdu_region_animator::kdu_region_animator()
{
  repeat_mode = reverse_mode = false;
  custom_frame_interval = -1.0;
  native_frame_interval_multiplier = 1.0;
  intra_frame_interval = 1.0;
  max_pan_speed = 1000.0;
  pan_acceleration = 500.0;
  
  jpx_src = NULL;
  mj2_src = NULL;
  mj2_track = NULL;
  track_idx = 0;
  metadata_driven = single_layer_mode = false;
  incomplete_meta_head = incomplete_meta_tail = NULL;

  video_range_start = video_range_end = next_video_frame_idx = -1;
  max_source_frames = -1;
  metadata_frame_count = 0;
  head = tail = current = free_list = NULL;
  cleanup = NULL;
  next_frame_changed = false;
  accumulate_display_times = false; // Becomes true after first frame generated
  
  min_outstanding_client_requests = 2;
  max_outstanding_client_requests = 20;
  assert(max_outstanding_client_requests > min_outstanding_client_requests);
  client_request_min_t_agg = client_request_t_agg = 0.13;
  min_auto_refresh_interval = 0.5;
  earliest_client_refresh = -1.0;
  metadata_request_time_offset = 0.5; // Reasonable lower bound to get started
  client_refresh_state = 0;
  client_prefs.set_pref(KDU_WINDOW_PREF_FULL);
  frame_reqs = last_frame_req = NULL;
  num_client_requests = 0;
  ref_display_clock_base = -1.0; // Until initialized
  client_requests = last_client_request = NULL;
  next_request_custom_id = 1;
  last_client_request_scale = -1.0F;
  
  delay_accumulator = 0.0;
  display_event_reference = 0.0;
  display_event_interval = 0.01; // Any positive value for the moment
  
  last_original_display_time = 0.0;
  mean_frame_interval = max_rendering_time = 0.0;
  num_frame_interval_updates = 0;
  
  reset_quality_layer_recommender();
  target_rendering_frac = 0.95;
}

/*****************************************************************************/
/*                         kdu_region_animator::stop                         */
/*****************************************************************************/

void kdu_region_animator::stop()
{
  jpx_src = NULL;
  mj2_src = NULL;
  mj2_track = NULL;
  track_idx = 0;
  metadata_driven = single_layer_mode = false;
  composition_rules = jpx_composition();
  meta_manager = jpx_meta_manager();
  last_original_display_time = -1.0;
  mean_frame_interval = max_rendering_time = 0.0;
  num_frame_interval_updates = 0;
  next_frame_changed = false;
  accumulate_display_times = false;
  abandon_all_client_requests();
  if (head != NULL)
    tail->next = head->prev = NULL;
  while ((tail=head) != NULL)
    { 
      head = tail->next;
      delete tail;
    }
  while ((current=free_list) != NULL)
    { 
      free_list = current->next;
      delete current;
    }
  earliest_client_refresh = -1.0;
  client_refresh_state = 0;
  for (int d=0; d <= 32; d++)
    ref_comp_subsampling[d] = kdu_coords(0,0);
  cleanup = NULL;
  incomplete_meta_head = incomplete_meta_tail = NULL;
  video_range_start = video_range_end = next_video_frame_idx = -1;
  max_source_frames = -1;
  metadata_frame_count = 0;
  
  reset_quality_layer_recommender();
  target_rendering_frac = 0.95;
}

/*****************************************************************************/
/*                         kdu_region_animator::start                        */
/*****************************************************************************/

bool kdu_region_animator::start(int video_start, int video_end,
                                int video_first_idx, jpx_source *jpx_in,
                                mj2_source *mj2_in, kdu_uint32 video_track,
                                bool jpx_single_layer_mode)
{
  stop(); // Just in case
  this->delay_accumulator = 0.0;
  this->jpx_src = jpx_in;
  this->mj2_src = mj2_in;
  if (mj2_in != NULL)
    { 
      this->track_idx = video_track;
      if ((track_idx == 0) ||
          ((mj2_track = mj2_src->access_video_track(track_idx)) == NULL) ||
          ((max_source_frames = mj2_track->get_num_frames()) == 0))
        { 
          stop();
          return false;
        }
      if (max_source_frames <= video_end)
        video_end = max_source_frames-1;
      jpx_src = NULL; // Just in case both args were non-NULL
    }
  else if (jpx_in != NULL)
    { 
      single_layer_mode = jpx_single_layer_mode;
      composition_rules = jpx_in->access_composition();
      meta_manager = jpx_in->access_meta_manager();
      if (single_layer_mode)
        this->track_idx = video_track = 0;
      else
        { 
          this->track_idx = video_track;
          int count=0;
          if (composition_rules.exists())
            composition_rules.count_track_frames(track_idx,count);
          if (count < 1)
            { 
              stop();
              return false;
            }
        }
    }
  else
    return false;
  if (video_start >= 0)
    { // Not metadata-driven
      assert((jpx_in != NULL) || (mj2_in != NULL));
      assert((video_end >= video_first_idx) &&
             (video_first_idx >= video_start));
      this->video_range_start = video_start;
      this->video_range_end = video_end;
      this->next_video_frame_idx = video_first_idx;
      this->metadata_driven = false;
    }
  else
    { // Metadata-driven
      assert(jpx_in != NULL);
      this->video_range_start = this->video_range_end = -1;
      this->next_video_frame_idx = -1;
      this->metadata_driven = true;
    }
  return true;
}

/*****************************************************************************/
/*                    kdu_region_animator::add_metanode                      */
/*****************************************************************************/

int kdu_region_animator::add_metanode(jpx_metanode node)
{
  assert(metadata_driven);
  if ((jpx_src == NULL) || (!meta_manager.exists()) ||
      (!node.exists()) || node.is_deleted())
    return 0;
  
  // First create a frame to hold the as-yet unexplored state
  kdra_frame *elt = insert_new_frame(NULL,this->reverse_mode);
  elt->metanode = node;
  elt->set_metadata_incomplete(true,false);
  
  // Now attempt to derive actual animation frames from `elt'
  return elt->expand_incomplete_metadata();
}

/*****************************************************************************/
/*                     kdu_region_animator::set_repeat                       */
/*****************************************************************************/

void kdu_region_animator::set_repeat(bool repeat)
{
  if (repeat == repeat_mode)
    return;
  this->repeat_mode = repeat;
  if ((!metadata_driven) && (current != NULL) && !repeat)
    { // Remove all future and past animation frames that involve wrap-around
      kdra_frame *scan;
      for (scan=current; scan->next != NULL; scan=scan->next)
        if (scan->next->frame_idx < scan->frame_idx)
          { // Found wrap-around
            while (scan->next != NULL)
              remove_frame(scan->next,true);
            break;
          }
      for (scan=current; scan->prev != NULL; scan=scan->prev)
        if (scan->prev->frame_idx > scan->frame_idx)
          { // Found wrap-around
            while (scan->prev != NULL)
              remove_frame(scan->prev,true);
            break;
          }
    }
  
  if ((head == tail) || !metadata_driven)
    assert((head == NULL) || ((head->prev == NULL) && (tail->next == NULL)));
  else if (repeat)
    { 
      head->prev = tail;
      tail->next = head;
    }
  else
    head->prev = tail->next = NULL;
}

/*****************************************************************************/
/*                     kdu_region_animator::set_reverse                      */
/*****************************************************************************/

void kdu_region_animator::set_reverse(bool reverse,
                                      double next_display_event_time)
{
  if (reverse == reverse_mode)
    return;
  this->reverse_mode = reverse;
  this->delay_accumulator = 0.0;
  this->next_frame_changed = false;
  
  // Remove all outstanding client requests, forcing a new set of (pre-emptive)
  // requests to be issued from scratch.
  abandon_all_client_requests();

  if ((current != NULL) && (next_display_event_time > 0.0))
    { // Try to adjust the state of `current' so that the change in playback
      // direction will appear to be as seamless as possible.
      backup_to_display_event_time(next_display_event_time);
      if (next_display_event_time < current->display_time)
        next_display_event_time = current->display_time; // Just in case
      double delta_t = next_display_event_time - current->display_time;
      if ((current->roi_pan_acceleration > 0.0) && (current->prev != NULL))
        { 
          kdu_region_animator_roi roi_info;
          roi_info.init(current->roi_pan_acceleration,
                        current->roi_pan_max_ds_dt);
          double pos = roi_info.get_pos_for_time(delta_t);
          kdra_frame *old = current;
          current = current->prev;
          current->roi_pan_acceleration = old->roi_pan_acceleration;
          current->roi_pan_max_ds_dt = old->roi_pan_max_ds_dt;
          current->roi_pan_duration = 0.0;
          current->cur_roi_pan_pos = current->next_roi_pan_pos = 1.0 - pos;
          current->display_time = next_display_event_time;
        }
      else
        { // Regular frame; adjust display time so we work backward through
          // the same timeline we have been moving along.
          double duration = current->calculate_duration();
          double rev_delta_t = duration - delta_t;
          if (rev_delta_t < 0.0)
            rev_delta_t = 0.0;
          current->display_time = next_display_event_time - rev_delta_t;
        }
      current->cur_display_time = current->display_time;
    }
  else
    { // Start timing from scratch
      accumulate_display_times = false;
      if (current != NULL)
        { 
          current->remove_conditional_frame_step();
          current->reset_pan_params();
          current->cur_display_time = current->display_time;          
        }
    }
  
  if (!metadata_driven)
    { // Regular playback mode; remove all non-current animation frames
      assert(video_range_start >= 0);
      if (current == NULL)
        return;
      while (current->prev != NULL)
        remove_frame(current->prev);
      while (current->next != NULL)
        remove_frame(current->next);
      assert((current == head) && (current == tail));
    }
  else
    { // Metadata-driven animation; need to reconnect the animation frame list
      // and also mark all non-current frames as not yet generated.
      kdra_frame *elt, *next;
      if (current != NULL)
        for (elt=current->prev;
             (elt != NULL) && (elt != current) && (elt->display_time >= 0.0);
             elt=elt->prev)
          { 
            elt->display_time = elt->cur_display_time = -1.0;
            elt->reset_pan_params();
            elt->generated = false;
          }
      for (elt=head; elt != NULL; elt=next)
        { 
          next = elt->next;  elt->next = elt->prev;  elt->prev = next;
          if (next == head)
            next = NULL;
        }
      next = head; head = tail; tail = next;
    }
  cleanup = head; // Might as well do a fresh scan for frames to clean up
}

/*****************************************************************************/
/*                      kdu_region_animator::set_timing                      */
/*****************************************************************************/

void kdu_region_animator::set_timing(double custom_frame_rate,
                                     double native_frame_rate_multiplier,
                                     double intra_frame_rate)
{
  double new_custom_interval =
    (custom_frame_rate <= 0.0)?-1.0:(1.0/custom_frame_rate);
  double new_native_multiplier = 1.0 / native_frame_rate_multiplier;
  double new_intra_interval=-1.0, new_pan_speed=-1.0, new_acceleration=-1.0;
  if (intra_frame_rate > 0.0)
    { 
      new_intra_interval = 1.0/intra_frame_rate;
      new_pan_speed = intra_frame_rate * 1000.0;
      new_acceleration = intra_frame_rate * 500.0;
    }
  if ((this->custom_frame_interval == new_custom_interval) &&
      (this->native_frame_interval_multiplier == new_native_multiplier) &&
      (this->intra_frame_interval == new_intra_interval) &&
      (this->max_pan_speed == new_pan_speed) &&
      (this->pan_acceleration == new_acceleration))
    return; // No change
  
  // Start by adjusting the quality layer recommender's parameters to account
  // for any change in frame period implied by this call.
  if (last_noted_display_period > 0.0)
    { 
      if (new_custom_interval > 0.0)
        last_noted_display_period = new_custom_interval;
      else
        last_noted_display_period *= (new_native_multiplier /
                                      this->native_frame_interval_multiplier);
      this->cum_display_period = last_noted_display_period;
      this->cum_rendering_period = last_noted_rendering_period;
    }
  else
    reset_quality_layer_recommender();
    
  // Now install the new timing parameters
  this->custom_frame_interval = new_custom_interval;
  this->native_frame_interval_multiplier = new_native_multiplier;
  this->intra_frame_interval = new_intra_interval;
  this->max_pan_speed = new_pan_speed;
  this->pan_acceleration = new_acceleration;
  num_frame_interval_updates = 0;
  mean_frame_interval = 0.0;
  last_client_request_scale = -1.0F; // Force regeneration of expected display
                                     // times in `generate_imagery_requests'  
}

/*****************************************************************************/
/*                    kdu_region_animator::set_max_frames                    */
/*****************************************************************************/

void kdu_region_animator::set_max_frames(int max_frames)
{
  if (metadata_driven ||
      ((max_source_frames >= 0) && (max_source_frames <= max_frames)))
    return; // Nothing to do

  if (max_frames < 1)
    { // Remove all frames
      current = NULL;
      abandon_all_client_requests();
      while (head != NULL)
        remove_frame(head);
      assert(tail == NULL);
      accumulate_display_times = false;
      max_source_frames = 0;
      video_range_end = -1;
      return;
    }

  max_source_frames = max_frames;
  if (video_range_end >= max_frames)
    video_range_end = max_frames-1;
  
  // Remove all frames whose frame indices equal or exceed `max_frames'
  kdra_frame *elt, *next=NULL;
  for (elt=head; elt != NULL; elt=next)
    { 
      next = elt->next;
      bool cycled_around = (next == head);
      if (elt->frame_idx >= max_frames)
        remove_frame(elt,true);
      if (cycled_around)
        break;
    }
  if ((current == NULL) || (current->frame_idx < 0))
    { // Start from scratch
      current = NULL;
      accumulate_display_times = false;
    }
}

/*****************************************************************************/
/*                 kdu_region_animator::retard_animation                     */
/*****************************************************************************/

void kdu_region_animator::retard_animation(double display_time_delay)
{ 
  if ((current == NULL) || (display_time_delay <= 0.0))
    return;
  delay_accumulator += display_time_delay;
  conditionally_apply_accumulated_delay();
}

/*****************************************************************************/
/*           kdu_region_animator::insert_conditional_frame_step              */
/*****************************************************************************/

bool kdu_region_animator::insert_conditional_frame_step(
                                             double next_display_event_time,
                                             double min_gap)
{
  if ((current == NULL) ||
      (current->cur_display_time >= next_display_event_time))
    return false; // Cases 1 and 2 from the function's description
  assert(current->frame_idx >= 0);
  if (current->in_auto_pan_preamble())
    return false; // Case 3 from the function's description
  if ((current->conditional_step_display_time >= 0.0) &&
      (!current->conditional_step_is_auto_refresh) &&
      (current->conditional_step_display_time < next_display_event_time))
    return true; // We already have an earlier conditional step
  
  current->remove_conditional_frame_step(); // Start from scratch
  if (current->is_final_frame())
    return false;
  else
    { 
      current->conditional_step_display_time = next_display_event_time;
      current->min_conditional_step_gap = min_gap;
      this->next_frame_changed = true;
      return true;
    }
}

/*****************************************************************************/
/*              kdu_region_animator::generate_imagery_requests               */
/*****************************************************************************/

int
  kdu_region_animator::generate_imagery_requests(kdu_client *client,
                                                 int queue_id,
                                                 double next_disp_event_time,
                                                 kdu_dims viewport,
                                                 kdu_dims roi, float scale,
                                                 int max_quality_layers)
{
  if (roi.is_empty())
    roi = viewport;

  // Start by seeing if we have to regenerate the expected timing information
  // associated with all frames from `current' through to the last one
  // to have been associated with a request, or `next_client_request_frame',
  // whichever comes later.  Also determine whether we need to trim any
  // existing request queue to maximize responsiveness to changes in
  // window of interest.
  if ((last_client_request_scale != scale) && (frame_reqs != NULL))
    { // There has been a change in scale or in timing parameters.
      this->trim_imagery_requests(client,queue_id);
      kdra_frame_req *frq = frame_reqs;
      if (frq != NULL)
        { 
          double start_time = frq->expected_display_start;
          for (; frq != NULL; frq=frq->next)
            { 
              assert(start_time > 0.0);
              frq->expected_display_start = start_time;
              start_time += frq->frm->calculate_expected_total_duration(scale);
              frq->expected_display_end = start_time;
            }
        }
    }
  else if ((last_client_request != NULL) &&
           ((max_quality_layers != last_client_request->user_layers) ||
            (roi != last_client_request->user_roi) ||
            (viewport != last_client_request->user_viewport)))
    this->trim_imagery_requests(client,queue_id);

  bool was_at_last_frame = (((current == NULL) && (head == NULL)) ||
                            ((current != NULL) && current->is_final_frame()));
  
  // Obtain important timing information in the reference microsecond base
  kdu_long last_cumulative_display_usecs = 0; // These are for the last posted
  kdu_long last_cumulative_request_usecs = 0; // request, if any.
  if (last_client_request != NULL)
    { 
      last_cumulative_display_usecs =
        last_client_request->cumulative_display_usecs;
      last_cumulative_request_usecs =
        last_client_request->cumulative_request_usecs;
      if (current != NULL)
        { // Let's quickly check to see whether we should abandon all
          // requests and start from scratch with a pre-emptive one.
          kdu_long current_start_time =
            this->display_time_to_ref_usecs(current->display_time);
          if (current_start_time > last_cumulative_display_usecs)
            { 
              abandon_all_client_requests();
              last_cumulative_display_usecs = 0;
              last_cumulative_request_usecs = 0;
            }
        }
    }
  
  bool will_preempt = (last_client_request == NULL);
  if (will_preempt)
    initialize_ref_display_clock(next_disp_event_time);
  kdu_long next_disp_event_usecs =
    this->display_time_to_ref_usecs(next_disp_event_time);
  kdu_long client_lag_usecs =
    client->sync_timing(queue_id,next_disp_event_usecs,will_preempt);
  kdu_long request_horizon_usecs =
    client->get_timed_request_horizon(queue_id,will_preempt);
    // This is a limit on the cumulative preferred service times associated
    // with new requests that should be made at this point in time.  Asking
    // further ahead is unlikely to have any value.
  if (request_horizon_usecs <= 0)
    return 0; // No need to do anything more at this point in time

  // Find out how many microseconds need to be added to the cumulative
  // request time in order to arrive at the display clock time at which we
  // expect the requested service time to have been satisfied, based on
  // what the client's `sync_timing' function just told us.
  kdu_long request_to_display_offset_usecs =
    client_lag_usecs + next_disp_event_usecs - last_cumulative_request_usecs;
  
  // Find out how much time we need to allow for rendering
  kdu_long cpu_allowance_usecs = (kdu_long)
    (0.5 + 1000000.0*calculate_cpu_allowance());
  
  // See if we need to adjust the `client_request_t_agg' parameter based on
  // client-server channel characteristics.
  double suggested_t_agg=client_request_t_agg;
  if (!client->get_timing_info(queue_id,NULL,&suggested_t_agg))
    return 0; // Queue is not alive
  if (suggested_t_agg < client_request_min_t_agg)
    suggested_t_agg = client_request_min_t_agg;
  if (suggested_t_agg > client_request_t_agg)
    client_request_t_agg = suggested_t_agg;
  else
    { 
      double min_val = 0.875*client_request_t_agg; // Don't decrease too fast
      if (suggested_t_agg < min_val)
        suggested_t_agg = min_val;
      client_request_t_agg = suggested_t_agg;
    }
  
  // Prepare to examine future frames
  double agg_time_left = client_request_t_agg;
  int old_outstanding_requests = num_client_requests;
  int new_outstanding_requests = num_client_requests;
    // Note: the `num_client_requests' member is incremented inside the
    // loop below, whenever a new entry is added to the `client_requests' list,
    // but we don't want to terminate the loop as soon as this happens, because
    // there may be later frames that can be aggregated into the same
    // request.  For this reason, we keep the separate local variable
    // `new_outstanding_requests', that increments only once a request is
    // posted.  On entry to and exit from this function, all requests
    // on the `client_requests' list have been posted, so the variable is
    // initialized to `num_client_requests' here.
  
  kdu_window woi;
  while (new_outstanding_requests < max_outstanding_client_requests)
    { 
      kdra_frame *frm = NULL;
      kdra_frame_req *frq = last_frame_req;
      double earliest_new_start =
        next_disp_event_time + get_estimated_rendering_time();
      if ((frq == NULL) || (frq->requested_fraction >= 1.0))
        { // Need to append a new element to the `frame_reqs' list
          double start_time;
          if (frq == NULL)
            { 
              if ((frm = instantiate_next_frame(NULL)) == NULL)
                { // We cannot go any further here but we can encourage the
                  // `generate_metadata_requests' function to compensate by
                  // asking further ahead for frame metadata.
                  set_metadata_req_offset(request_to_display_offset_usecs,
                                          cpu_allowance_usecs,NULL);
                  break;
                }
              if ((start_time = frm->display_time) <= 0.0)
                start_time = earliest_new_start;
            }
          else
            { 
              if ((frm = instantiate_next_frame(frq->frm)) == NULL)
                { // We cannot go any further here but we can encourage the
                  // `generate_metadata_requests' function to compensate by
                  // asking further ahead for frame metadata.
                  set_metadata_req_offset(request_to_display_offset_usecs,
                                          cpu_allowance_usecs,frq->frm);
                  break;
                }
              start_time = frq->expected_display_end;
            }
          frq = append_frame_req(frm);
          frq->expected_display_start = start_time;
          frq->expected_display_end = start_time +
            frm->calculate_expected_total_duration(scale);
        }
      else
        frm = frq->frm;
          
      assert(frm->have_all_source_info);
      assert(frq->requested_fraction < 1.0);
      
      // Find timing information for the next frame (or fraction of a frame)
      double new_start = frq->expected_display_start;
      double new_period = frq->expected_display_end - new_start;
      new_start += new_period*frq->requested_fraction;
      new_period *= (1.0 - frq->requested_fraction);
      if (new_start < earliest_new_start)
        { // Display process is already beyond the point at which we are
          // preparing to allocate requested service time.
          double delta = earliest_new_start - new_start;
          new_start += delta;
          new_period -= delta;
          if (new_period <= 0.0)
            { // Skip over this frame
              frq->requested_fraction = 1.0;
              continue;
            }
        }
      kdra_client_req *req = last_client_request;
      if (req == NULL)
        { // 0 was not the best initializer for `last_cumulative_display_usecs'
          last_cumulative_display_usecs =
            this->display_time_to_ref_usecs(new_start);
        }
      
      // See if aggregation might be possible before building the WOI
      bool can_aggregate = false;
      if (req != NULL)
        { 
          if (req->custom_id != 0)
            req = NULL; // Cannot augment this request; already posted
          else if ((frq->client_reqs[0] == NULL) &&
                   ((agg_time_left >= (0.9*new_period)) ||
                    (agg_time_left >= (0.25*client_request_t_agg))))
            can_aggregate = true;
            // Note: the above conditions mean that we can only aggregate a
            // frame (fully or partially) into an existing request if the
            // frame has not yet been aggregated into any request and if
            // the frame can fit entirely into the new request (without
            // exceeding the nominal aggregation limit) or if the
            // aggregation window is not yet too close to the aggregation
            // limit (defined here as 75% of the limit).
        }
      
      woi.init();
      bool ignore_request_horizon =
        (new_outstanding_requests < min_outstanding_client_requests);
      if (can_aggregate || ignore_request_horizon ||
          (request_horizon_usecs > 0))
        { // Build the window of interest and check again for aggregation
          frm->construct_window_request(viewport,roi,scale,woi);
          if (can_aggregate &&
              (woi.resolution == req->window.resolution) &&
              (woi.round_direction == req->window.round_direction) &&
              (woi.region == req->window.region))
            { // Aggregate into existing request
              kdu_sampled_range rg;  int i=0;
              while (!(rg = woi.contexts.get_range(i++)).is_empty())
                req->window.contexts.add(rg,true);
              req->attach_frame_req(frq);
              assert(frq->client_reqs[0] == req);
              if (agg_time_left >= (0.9*new_period))
                { // No point in splitting frame across two requests;
                  // aggregate fully and set `frq'=NULL to make this clear.
                  frq->requested_fraction = 1.0;
                  agg_time_left -= new_period;
                  new_start += new_period;
                  new_period = 0.0;
                  can_aggregate = (agg_time_left > 0.0);
                  frq = NULL;
                }
              else
                { // Partially aggregated
                  frq->requested_fraction = agg_time_left / new_period;
                  new_start += agg_time_left;
                  new_period -= agg_time_left;
                  agg_time_left = 0.0;
                  can_aggregate = false; // Cannot keep aggregating
                }
              req->cumulative_display_usecs =
                this->display_time_to_ref_usecs(new_start);
            }
          else
            can_aggregate = false;
        }

      if ((req != NULL) && !can_aggregate)
        { // Cannot aggregate further; dispatch request for `req'
          assert(req->custom_id == 0);
          kdu_long aggregate_usecs = (kdu_long)
            (0.5 + 1000000.0*(client_request_t_agg-agg_time_left));
          post_last_client_request(client,queue_id,aggregate_usecs,
                                   last_cumulative_display_usecs,
                                   last_cumulative_request_usecs,
                                   request_to_display_offset_usecs,
                                   cpu_allowance_usecs);
          last_cumulative_display_usecs = req->cumulative_display_usecs;
          last_cumulative_request_usecs = req->cumulative_request_usecs;
          request_horizon_usecs -= req->requested_usecs;
          assert(req->custom_id != 0); // Proof that it was posted
          new_outstanding_requests++;
          req = NULL;
        }
      if (frq == NULL)
        continue;
      assert(req == NULL);
      if (((request_horizon_usecs <= 0) && !ignore_request_horizon) ||
          (new_outstanding_requests >= max_outstanding_client_requests))
        break; // We will have to come back later
      
      // Create new request record, but don't post it immediately; aggregation
      // with later frames may be possible.
      req = append_client_request(max_quality_layers,roi,viewport);
      req->attach_frame_req(frq);
      req->window.copy_from(woi);
      req->cumulative_display_usecs =
        this->display_time_to_ref_usecs(new_start+new_period);
      frq->requested_fraction = 1.0;
      agg_time_left = client_request_t_agg - new_period;
         // This may leave `agg_time_left' negative, but that's fine.
         // The first frame-req in a request is always requested in full.
         // Frame-req's are split into two requests only where the first
         // request contains earlier frame-reqs and the split frame-req has
         // significantly longer duration than the earlier frame-reqs in the
         // request.  That is why `requested_fraction' is always set to 1.0
         // here.
      if ((max_quality_layers > 0) && (max_quality_layers < (1<<16)))
        req->window.max_layers = max_quality_layers;
      
      // Finish up by adding any image window dependent metadata requests.
      req->window.add_metareq(jp2_roi_description_4cc,KDU_MRQ_WINDOW);
      req->window.add_metareq(jp2_number_list_4cc,KDU_MRQ_STREAM,true);
      if (meta_manager.exists())
        { 
          jpx_metanode root_node = meta_manager.access_root();
          kdu_uint32 box_types[2]={ jp2_label_4cc, jp2_cross_reference_4cc };
          root_node.generate_metareq(&(req->window),2,box_types,0,NULL,true,
                                     0,KDU_MRQ_GLOBAL);
        }
    }
  
  if ((last_client_request != NULL) && (last_client_request->custom_id == 0))
    { // Don't leave any unposted requests behind -- if future frames are
      // not yet available to be considered for aggregation into the last
      // request, we post the request anyway.
      kdu_long aggregate_usecs = (kdu_long)
        (0.5 + 1000000.0*(client_request_t_agg-agg_time_left));
      post_last_client_request(client,queue_id,aggregate_usecs,
                               last_cumulative_display_usecs,
                               last_cumulative_request_usecs,
                               request_to_display_offset_usecs,
                               cpu_allowance_usecs);
      request_horizon_usecs -= last_client_request->requested_usecs;
      assert(last_client_request->custom_id != 0); // Proof that it was posted
      new_outstanding_requests++;
    }
  
  bool is_at_last_frame = (((current == NULL) && (head == NULL)) ||
                           ((current != NULL) && current->is_final_frame()));
  if (is_at_last_frame && !was_at_last_frame)
    this->next_frame_changed = true; // Force application to discover the
           // fact that it is now known to be at the end of the animation.
  
  assert(new_outstanding_requests == num_client_requests);
  this->last_client_request_scale = scale;
  return (new_outstanding_requests - old_outstanding_requests);
}

/*****************************************************************************/
/*               kdu_region_animator::notify_client_progress                 */
/*****************************************************************************/

void
  kdu_region_animator::notify_client_progress(kdu_client *client, int queue_id,
                                              double next_disp_event_time)
{
  kdra_client_req *req = client_requests;
  kdu_long service_usecs=0, custom_id=0;
  int status_flags=KDU_CLIENT_WINDOW_RESPONSE_STARTED;
  if ((req == NULL) ||
      (!client->get_window_info(queue_id,status_flags,custom_id,NULL,
                                &service_usecs)) ||
      (custom_id == 0) || (service_usecs <= 0))
    return;
  while ((req != NULL) && (req->custom_id != custom_id))
    { // Look for the request corresponding to `custom_id', deleting any
      // requests which have earlier custom id's as we go.
      kdu_long id_diff = req->custom_id - custom_id;
      if (id_diff > 0)
        return; // Request being serviced no longer on the list for some reason
      remove_client_request(req);
      req = client_requests;
    }
  for (; req != NULL; req=req->next)
    if (req->custom_id == custom_id)
      break;
  if (req == NULL)
    return;
  assert(req->service_usecs <= service_usecs);
  if (req->service_usecs < service_usecs)
    { 
      req->service_usecs = service_usecs;

      // First we update the machinery that manages suggested refresh
      // behaviour, which applies even when individual frames are not
      // divided into auto-refresh steps -- even if a frame is rendered only
      // once, a `kdu_region_compositor::refresh' call may be recommended
      // first, to force regeneration of any compositing layers that are
      // being re-used from earlier animation frames.
      if ((client_refresh_state <= 0) && (earliest_client_refresh > 0.0))
        client_refresh_state = -1;
      
      // Next, we see if a special auto-refresh frame step needs to be
      // inserted to sub-divide the current animation frame -- this may
      // be required if the frame has an excessive duration so that
      // regular re-rendering of the frame allows updates in the state of
      // the cache to be reflected in progressively improving image quality.
      if ((current != NULL) && current->generated &&
          (current->frame_reqs != NULL) &&
          ((current->frame_reqs->client_reqs[0] == req) ||
           (current->frame_reqs->client_reqs[1] == req)) &&
          (current->conditional_step_display_time < 0.0) &&
          !(current->in_auto_pan_preamble() || current->is_final_frame()))
        { // Set up an auto-refresh time
          current->conditional_step_display_time =
            current->cur_display_time + this->min_auto_refresh_interval;
          current->conditional_step_is_auto_refresh = true;
          double cpu_allowance = calculate_cpu_allowance();
          double min_refresh_time = next_disp_event_time + cpu_allowance;
          if (current->conditional_step_display_time < min_refresh_time)
            current->conditional_step_display_time = min_refresh_time;
          current->min_conditional_step_gap = 0.5*min_auto_refresh_interval;
          if (current->min_conditional_step_gap < cpu_allowance)
            current->min_conditional_step_gap = cpu_allowance;
          double next_frame_time =
            current->display_time + current->calculate_duration();
          if ((current->conditional_step_display_time <
               (next_frame_time-current->min_conditional_step_gap)) ||
              (current->next == NULL) || !current->next->have_all_source_info)
            this->next_frame_changed = true;
        }
    }
}

/*****************************************************************************/
/*             kdu_region_animator::generate_metadata_requests               */
/*****************************************************************************/

int
  kdu_region_animator::generate_metadata_requests(kdu_window *client_window,
                                                  double display_time_limit,
                                                  double next_disp_event_time)
{
  int num_metareqs = 0;
  
  // Start by trying to expand any incomplete metadata
  kdra_frame *scan = this->incomplete_meta_head;
  while (scan != NULL)
    { 
      assert(scan->is_metadata_incomplete());
      scan->expand_incomplete_metadata();
      if (scan == incomplete_meta_head)
        break;
      scan = incomplete_meta_head;
    }

  // Now generate requests for the metadata required to expand
  // incomplete animation frames -- i.e., frames whose `metanode' member may
  // expand into 0, 1 or more actual animation frames once it becomes
  // available.
  int num_incomplete_links=0, num_incomplete_descendants=0;
  for (scan=incomplete_meta_head; scan != NULL;
       scan=scan->incomplete_meta_next)
    { 
      assert(scan->is_metadata_incomplete());
      num_incomplete_links += scan->incomplete_link_target?1:0;
      num_incomplete_descendants += scan->incomplete_descendants?1:0;
      if (scan->incomplete_link_target)
        num_metareqs +=
          scan->metanode.generate_link_metareq(client_window,0,NULL,0,NULL,
                                               true,0);
      if (scan->incomplete_descendants)
        num_metareqs +=
          scan->metanode.generate_metareq(client_window,0,NULL,0,NULL,true,0);
    }
  
  // Finally, generate requests for compositing layer and/or codestream
  // header boxes that we are going to need in the near future.
  assert(metadata_request_time_offset >= 0.0);
	display_time_limit += metadata_request_time_offset;
  int num_incomplete_frames = 0, num_extra_frames=0;
  double display_time;
  if (current != NULL)
    display_time = current->display_time;
  else
    display_time = next_disp_event_time;
  scan = (current == NULL)?head:current;
  kdra_frame *next, *wrap_check=NULL;
  for (; scan != NULL; scan=next)
    { 
      if (wrap_check == NULL)
        wrap_check = scan;
      else if ((scan == wrap_check) ||
               ((scan->frame_idx == wrap_check->frame_idx) &&
                !metadata_driven))
        { // Wrapped around.  If metadata driven, a repeated animation will
          // be connected circularly, so we will eventually get back to
          // `wrap_check'.  If not metadata driven, a repeated animation
          // continuously spawns new frames, but the frame index is sufficient
          // to check for wrap around.
          break;
        }
      if (scan->is_metadata_incomplete())
        break; // No point in searching any further
      if ((next = scan->next) == NULL)
        { // So we don't run out of frames to consider, just because they
          // have not yet been created.
          instantiate_next_frame(scan);
          next = scan->next;
        }
      if ((!scan->have_all_source_info) && (scan->find_source_info() < 0))
        continue; // Frame has been removed by `find_source_info'.
      if (scan->frame_idx < 0)
        break; // Don't even know frame index; no point in continuing
      if ((!scan->have_all_source_info) &&
          scan->request_unknown_layers(client_window))
        num_incomplete_frames++;
      display_time += scan->calculate_duration(true);
      if (display_time > display_time_limit)
        { 
          if (num_extra_frames > 0)
            break;
          num_extra_frames++; // Consider up to one extra frame
        }
    }
  if (num_incomplete_frames > 0)
    { // Request the actual metadata
      num_metareqs += 2;
      client_window->add_metareq(jp2_compositing_layer_hdr_4cc,KDU_MRQ_STREAM);
      client_window->add_metareq(jp2_codestream_header_4cc,KDU_MRQ_STREAM);
    }
  
  // Add in any additional requests for mandatory headers that may be
  // required to discover the existence of frames.
  num_metareqs += jpx_src->generate_metareq(client_window);

  return num_metareqs;
}

/*****************************************************************************/
/*             kdu_region_animator::get_suggested_advance_delay              */
/*****************************************************************************/

double
  kdu_region_animator::get_suggested_advance_delay(double next_disp_event_time)
{
  double next_display_time = -1.0;
  if (current != NULL)
    { 
      double next_frm_time =
        current->display_time + current->calculate_duration();
      double step = current->conditional_step_display_time;
      if ((step >= 0.0) &&
          ((step + current->min_conditional_step_gap) < next_frm_time))
        next_display_time = step; // No need to check next frame exists
      else if (instantiate_next_frame(current) != NULL)
        next_display_time = next_frm_time; // Use next actual frame time
      else if (current->is_final_frame())
        return -1.0; // Encourage immediate call to `advance_animation' to
                     // discover the fact that we are at the end
      else
        next_display_time = step; // Use frame step, if there is one
    }
  else if (instantiate_next_frame(NULL) != NULL)
    return -1.0; // Encourage immediate call to `advance_animation' to advance
                 // to the first animation frame, which is now available.
  else if (head == NULL)
    return -1.0; // Encourage immediate call to `advance_animation' to discover
                 // the fact that the animation is empty.    
  
  if (next_display_time < 0.0)
    return 10.0; // No information yet about the next frame, but we cannot
                 // determine that the animation is over; encourage long sleep
                 // during which external events might cause us to come back
                 // here and re-assess.
  
  double cpu_allowance = calculate_cpu_allowance();
  double delay = next_display_time - next_disp_event_time;
  return delay - cpu_allowance;
}

/*****************************************************************************/
/*                  kdu_region_animator::advance_animation                   */
/*****************************************************************************/

int kdu_region_animator::advance_animation(double cur_system_time,
                                           double last_display_event_time,
                                           double next_display_event_time,
                                           bool need_refresh,
                                           bool skip_undisplayables)
{
  assert((cur_system_time >= 0.0) && // -ve values have special meaning
         (next_display_event_time >= 0.0));
  this->next_frame_changed = false;
  if ((max_source_frames == 0) || !is_active())
    return KDU_ANIMATION_DONE;
  if ((current != NULL) && (current->source_duration == 0.0) &&
      !metadata_driven)
    return KDU_ANIMATION_PAUSE;
  
  // Update preferred display times
  display_event_reference = next_display_event_time;
  if (next_display_event_time > last_display_event_time)
    display_event_interval = next_display_event_time - last_display_event_time;
  
  // Determine the new `current' frame
  kdra_frame *new_current = NULL;
  int num_undisplayables_skipped = 0;
  bool display_time_newly_initialized = false;
  bool was_in_auto_pan_preamble = false;
  if (need_refresh)
    { // The `new_current' frame is drawn from one of the existing ones
      backup_to_display_event_time(next_display_event_time);
      if (current == NULL)
        return KDU_ANIMATION_PENDING; // We don't want to instantiate a new
           // frame during refresh, so safest thing to do is report "pending"
           // and let the application discover whether "done" is more
           // appropriate when it calls this function again without the
           // `need_refresh' option.
      new_current = current;
      if (current->roi_pan_acceleration > 0.0)
        { // This frame has been or is being subjected to an auto-pan preamble.
          // Need to adjust the pan position according to the next display
          // event time.
          double delta_t = next_display_event_time - current->display_time;
          if (delta_t >= 0.0)
            { 
              kdu_region_animator_roi roi_info;
              double trans_t = roi_info.init(current->roi_pan_acceleration,
                                             current->roi_pan_max_ds_dt);
              if (delta_t > trans_t)
                { 
                  current->display_time += trans_t;
                  current->cur_roi_pan_pos = 1.0;
                }
              else
                { 
                  current->display_time += delta_t;
                  current->cur_roi_pan_pos=roi_info.get_pos_for_time(delta_t);
                }
              current->next_roi_pan_pos = current->cur_roi_pan_pos;
            }
        }
      if (next_display_event_time > current->display_time)
        current->cur_display_time = next_display_event_time;
      else
        current->cur_display_time = current->display_time;
      num_frame_interval_updates = 0;
    }
  else
    { 
      // Find the next display time based upon current timing parameters
      double next_display_time;
      if (accumulate_display_times && (current != NULL))
        next_display_time = (current->display_time +
                             current->calculate_duration());
      else
        { 
          accumulate_display_times = false; // Should be false already
          next_display_time = next_display_event_time;
        }
      
      // Find the `new_current' frame
      if ((current != NULL) && current->in_auto_pan_preamble())
        { // We were in the auto-pan pre-amble of the `current' frame.
          // Continue using the current frame after updating its
          // auto-pan status
          was_in_auto_pan_preamble = true;
          new_current = current;
          current->cur_roi_pan_pos = current->next_roi_pan_pos;
          current->roi_pan_duration = 0.0;
          current->remove_conditional_frame_step(); // Just in case
        }
      else if ((current != NULL) &&
               (current->conditional_step_display_time >= 0.0))
        { // See if we should advance to the conditional frame step or not
          new_current = current;
          if (current->conditional_step_display_time >=
              (next_display_time - current->min_conditional_step_gap))
            { // If the next frame is available now, we should get rid of
              // the conditional frame step.
              try {
                new_current = instantiate_next_frame(current);
              } catch (kdu_exception) {
                return KDU_ANIMATION_DONE;
              }
              if (new_current == NULL)
                new_current = current;
              else
                { 
                  current->remove_conditional_frame_step();
                  display_time_newly_initialized = true;
                }
            }
          if (new_current == current)
            next_display_time = current->conditional_step_display_time;
        }
      else
        { 
          try {
            new_current = instantiate_next_frame(current);
          } catch (kdu_exception) {
            return KDU_ANIMATION_DONE;
          }
          if (new_current != NULL)
            display_time_newly_initialized = true;
          if (skip_undisplayables && (current != NULL) &&
              (display_event_interval > 0.0))
            { // May need to skip over multiple frames that are spaced
              // too close together for all of them to be rendered.
              double thresh = next_display_event_time;
              double test_time=0.0;
              while ((new_current != NULL) &&
                     (next_display_time < thresh) &&
                     ((test_time = next_display_time +
                       new_current->calculate_duration()) < thresh))
                { 
                  kdra_frame *test_frame;
                  try {
                    test_frame = instantiate_next_frame(new_current);
                  } catch (kdu_exception) {
                    return KDU_ANIMATION_DONE;
                  }
                  if (test_frame == NULL)
                    break;
                  new_current = test_frame;
                  next_display_time = test_time;
                  num_undisplayables_skipped++;
                }
            }
        }
      if (new_current == NULL)
        { 
          if ((head == NULL) ||
              ((current != NULL) && (current->next == NULL)))
            return KDU_ANIMATION_DONE;
          else
            { // Must still be waiting for metadata to discover frames
              return KDU_ANIMATION_PENDING;
            }
        }
      if (display_time_newly_initialized || was_in_auto_pan_preamble)
        new_current->display_time = next_display_time;
      else
        assert((new_current->display_time >= 0.0) &&
               (next_display_time > new_current->display_time));
      if (new_current->conditional_step_is_auto_refresh)
        client_refresh_state = 2; // Means "compositor refresh required"
      else if ((client_refresh_state < 0) &&
               (next_display_time >= earliest_client_refresh))
        client_refresh_state = 1; // Means "compositor refresh recommended"
      new_current->remove_conditional_frame_step();
      new_current->cur_display_time = next_display_time;
    }

  // Update `current' state
  assert(new_current->frame_idx >= 0);
  new_current->generated = false;
  new_current->activation_time = cur_system_time;
  bool need_frame_interval_update = (new_current != current);
  if (frame_reqs != NULL)
    { 
      for (kdra_frame *frm=current;
           (frm != NULL) && (frm != new_current) && (frame_reqs != NULL);
           frm=frm->next)
        if (frm->frame_reqs == frame_reqs)
          remove_frame_req(frame_reqs);
    }
  current = new_current;
  if (display_time_newly_initialized && (current->frame_reqs != NULL))
    current->frame_reqs->note_newly_initialized_display_time();
  conditionally_apply_accumulated_delay();

  // Update mean frame interval statistics
  if (need_frame_interval_update)
    { 
      double interval = current->display_time - last_original_display_time;
      last_original_display_time = current->display_time;
      if (num_undisplayables_skipped > 0)
        interval = interval / (1+num_undisplayables_skipped);
      num_frame_interval_updates++;
      if (num_frame_interval_updates <= 3)
        mean_frame_interval = interval;
      else if (num_frame_interval_updates != 0)
        { 
          double update_weight =
            0.5*(1+num_undisplayables_skipped)*(mean_frame_interval+interval);
          if (update_weight > 1.0)
            update_weight = 1.0;
          else if (update_weight < 0.1)
            update_weight = 0.1;
          if ((update_weight*num_frame_interval_updates) < 1.0)
            update_weight = 1.0 / (double) num_frame_interval_updates;
          mean_frame_interval += (interval-mean_frame_interval)*update_weight;
        }
    }
  
  // Finally remove any frames from the animation list that we no longer need
  while ((cleanup != NULL) && (cleanup != current) &&
         (cleanup->next->display_time <= next_display_event_time))
    { 
      kdra_frame *tmp = cleanup;
      cleanup = tmp->next;
      if (!(tmp->first_match || tmp->last_match))
        remove_frame(tmp); // Either regular or temporary metadata-driven frame
    }

  next_frame_changed = false; // Only need this flag to be set if a new
                              // frame becomes available after this function
                              // returns false.
  return current->frame_idx;
}

/*****************************************************************************/
/*                kdu_region_animator::note_frame_generated                  */
/*****************************************************************************/

void kdu_region_animator::note_frame_generated(double cur_system_time,
                                               double next_display_event_time,
                                               int max_available_layers,
                                               int quality_layer_limit)
{
  assert(cur_system_time >= 0); // We rely on -ve values having special meaning
  if ((current == NULL) || current->generated)
    return; // Should not be calling this function
  
  current->generated = true;
  if (current->cur_display_time == current->display_time)
    { // Update processing time statistics based on the first attempt to
      // render content from a frame.
      double rendering_time = cur_system_time - current->activation_time;
      if (rendering_time > 0.0)
        { // Otherwise don't update statistics
          if (rendering_time > max_rendering_time)
            max_rendering_time = rendering_time;
          else
            max_rendering_time += (rendering_time-max_rendering_time)*0.1;
        }
      
      // Update quality layer recommender machinery
      if (max_available_layers > 0)
        { 
          this->last_noted_available_quality_layers = max_available_layers;
          double display_period=0.0;
          if ((quality_layer_limit >= 0) && (rendering_time > 0.0) &&
              ((display_period = current->calculate_duration()) > 0.0))
            { 
              if ((quality_layer_limit == 0) ||
                  (quality_layer_limit > max_available_layers))
                quality_layer_limit = max_available_layers;
              this->last_noted_quality_layer_limit = quality_layer_limit;
              this->last_noted_rendering_period = rendering_time;
              this->last_noted_display_period = display_period;
              if (rendering_time > (display_period*target_rendering_frac))
                { // Start accumulator from scratch
                  cum_rendering_period = last_noted_rendering_period;
                  cum_display_period = last_noted_display_period;
                }
              else
                { 
                  cum_rendering_period += last_noted_rendering_period;
                  cum_display_period += last_noted_display_period;
                }
            }
        }
    }
  if (!accumulate_display_times)
    { 
      double delta_t = next_display_event_time - current->cur_display_time;
      current->display_time += delta_t;
      current->cur_display_time += delta_t;
      if (current->conditional_step_display_time >= 0.0)
        current->conditional_step_display_time += delta_t;
      if (current->frame_reqs != NULL)
        current->frame_reqs->note_newly_initialized_display_time();
      accumulate_display_times = true;
    }
  if (earliest_client_refresh < 0.0)
    { 
      earliest_client_refresh =
        current->cur_display_time + min_auto_refresh_interval;
        // Note: this alone is not enough to prompt the
        // `get_client_refresh_suggestions' to return non-zero; we need to
        // wait until `notify_client_progress' detects the arrival of new
        // service data, at which point `client_refresh_status' can take on
        // the special value -1 (unless it is already non-zero), after which
        // `advance_animation' can convert this value to 1 (refresh
        // recommended) if it advances to a frame whose display time is
        // equal to or greater than the `earliest_client_refresh'.
    }
}

/*****************************************************************************/
/*                 kdu_region_animator::get_current_frame                    */
/*****************************************************************************/

int
  kdu_region_animator::get_current_frame(jpx_frame &jpx_frm,
                                         mj2_video_source * &mj2_trk)
{
  mj2_trk = NULL; jpx_frm = jpx_frame();
  if ((current == NULL) || (current->frame_idx < 0))
    return -1;
  if (mj2_src != NULL)
    mj2_trk = mj2_track;
  else if (!single_layer_mode)
    jpx_frm = current->jpx_frm;
  return current->frame_idx;
}

/*****************************************************************************/
/*                 kdu_region_animator::get_display_time                     */
/*****************************************************************************/

double kdu_region_animator::get_display_time()
{
  if (current == NULL)
    return -1.0;
  assert(current->cur_display_time >= current->display_time);
  return current->cur_display_time;
}

/*****************************************************************************/
/*               kdu_region_animator::get_first_display_time                 */
/*****************************************************************************/

double kdu_region_animator::get_first_display_time()
{
  if (current == NULL)
    return -1.0;
  return current->display_time;
}

/*****************************************************************************/
/*            kdu_region_animator::get_recommended_quality_layers            */
/*****************************************************************************/

int kdu_region_animator::get_recommended_quality_layers(float target_frac)
{
  if (target_frac < 0.05f)
    target_frac = 0.05f;
  else if (target_frac > 0.95f)
    target_frac = 0.95f;
  this->target_rendering_frac = target_frac;
  if ((last_noted_display_period <= 0.0) ||
      (last_noted_rendering_period <= 0.0) ||
      (last_noted_quality_layer_limit <= 0))
    return -1; // We cannot generate any specific recommendation
  double last_ratio = last_noted_rendering_period/last_noted_display_period;
  double cum_ratio = cum_rendering_period / cum_display_period;
  if (last_ratio < target_frac)
    { // We can use the last quality limit again, or else increase it by 1
      if (cum_ratio < target_frac)
        return last_noted_quality_layer_limit+1;
      else
        return last_noted_quality_layer_limit;
    }
  else
    { // We should return a value 1 less than the last quality layer limit,
      // if possible.
      if (last_noted_quality_layer_limit > 1)
        return last_noted_quality_layer_limit-1;
      else
        return 1;
    }
}

/*****************************************************************************/
/*                 kdu_region_animator::get_current_metanode                 */
/*****************************************************************************/

jpx_metanode kdu_region_animator::get_current_metanode()
{
  return (current == NULL)?jpx_metanode():current->metanode;
}

/*****************************************************************************/
/*           kdu_region_animator::get_metadata_driven_frame_id               */
/*****************************************************************************/

int kdu_region_animator::get_metadata_driven_frame_id()
{
  return ((current == NULL)?-1:current->metadata_frame_id);
}

/*****************************************************************************/
/*           kdu_region_animator::count_metadata_driven_frames               */
/*****************************************************************************/

bool kdu_region_animator::count_metadata_driven_frames(int &count)
{
  count = metadata_frame_count;
  return (incomplete_meta_head == NULL);
}

/*****************************************************************************/
/*                 kdu_region_animator::get_current_geometry                 */
/*****************************************************************************/

bool
  kdu_region_animator::get_current_geometry(kdu_coords &frame_size,
                                            kdu_dims &frame_roi)
{
  if (current == NULL)
    return false;
  assert(current->have_all_source_info);
  frame_size = current->frame_size;
  frame_roi = current->frame_roi;
  return true;
}

/*****************************************************************************/
/*              kdu_region_animator::get_roi_and_mod_viewport                */
/*****************************************************************************/

void
  kdu_region_animator::get_roi_and_mod_viewport(
                                          kdu_region_compositor *compositor,
                                          kdu_region_animator_roi &roi_info,
                                          kdu_dims &mod_viewport)
{ 
  roi_info.init();
  
  if ((current == NULL) || (compositor == NULL) || !current->numlist.exists())
    return;
  
  kdu_dims image_dims;
  if ((!compositor->get_total_composition_dims(image_dims)) ||
      (image_dims.size.x < 1) || (image_dims.size.y < 1))
    return; // Compositor not initialized -- should not have called function
  
  kdu_dims initial_roi, final_roi;
  final_roi = compute_mapped_roi(compositor,current->numlist,
                                 current->metanode_roi);
  // See if we have a region of interest that is worth displaying as such
  if (final_roi.is_empty())
    return; // No ROI
  kdu_dims tmp_roi = final_roi;
  tmp_roi.pos.x -= 5;  tmp_roi.pos.y -= 5;
  tmp_roi.size.x += 10; tmp_roi.size.y += 10;
  if (tmp_roi.contains(image_dims))
    return; // ROI occupies all or virtually all of the image
  
  kdra_frame *prev = current->prev;
  if ((max_pan_speed <= 0.0) || (pan_acceleration <= 0.0) ||
      (current->cur_roi_pan_pos >= 1.0) || (prev == NULL) ||
      (prev->frame_idx != current->frame_idx) ||
      (!prev->numlist.exists()) ||
      (initial_roi =
       compute_mapped_roi(compositor,prev->numlist,
                          prev->metanode_roi)).is_empty() ||
      (initial_roi == final_roi))
    { // We have a single region of interest (may have finished a transition)
      current->roi_pan_duration = 0.0;
      roi_info.set_end_points(1.0,1.0,final_roi,final_roi);
      adjust_viewport_for_roi(final_roi,image_dims,mod_viewport);
    }
  else
    { // We have a transition between two regions of interest
      
      // Start by assessing the total amount of movement in a way that
      // recognizes the importance of both translation and scaling of the
      // region of interest.  We do this by averaging the squared euclidean
      // distance between the corresponding top-left corners and bottom-right
      // corners of the initial and final mapped regions of interest.
      double dist, inv_dist, max_delta_x, max_delta_y;
      dist = measure_roi_distance(final_roi,initial_roi,
                                  max_delta_x,max_delta_y);
      inv_dist = 1.0 / dist;
      current->roi_pan_acceleration = this->pan_acceleration * inv_dist;
      current->roi_pan_max_ds_dt = this->max_pan_speed * inv_dist;
      roi_info.init(current->roi_pan_acceleration,
                    current->roi_pan_max_ds_dt);
      
      // Now find the `start_pos' and `end_pos' values based on the
      // criterion that we don't want the buffer region to expand
      // excessively.
      int max_pan_border = 128;

      double max_delta_s = 1.0;
      if (max_delta_x > 0.0)
        max_delta_s = (mod_viewport.size.x + max_pan_border) / max_delta_x;
      if ((max_delta_y*max_delta_s) > (mod_viewport.size.y + max_pan_border))
        max_delta_s = (mod_viewport.size.y + max_pan_border) / max_delta_y;
      double s0 = current->cur_roi_pan_pos;
      double s1 = s0 + max_delta_s;
      if (s1 >= 1.0)
        s1 = 1.0;
      else if (display_event_interval > 0.0)
        { // Align `max_delta_s' with one of the preferred display event times
          double rel_t0 = current->display_time - display_event_reference;
          double rel_tstart = rel_t0 - roi_info.get_time_for_pos(s0);
          double rel_t1 = rel_tstart + roi_info.get_time_for_pos(s1);
          rel_t1 = display_event_interval*floor(rel_t1/display_event_interval);
          double quant_s1 = roi_info.get_pos_for_time(rel_t1-rel_tstart);
          if (quant_s1 > s1)
            quant_s1 = s1;
          if (quant_s1 > s0)
            s1 = quant_s1; // Just in case the increments are too small
        }
      current->next_roi_pan_pos = s1;
      current->roi_pan_duration =
        roi_info.set_end_points(s0,s1,initial_roi,final_roi);
      current->next_roi_pan_pos = roi_info.get_end_pos();
      current->remove_conditional_frame_step(); // Don't allow conditional
                        // frame steps during the frame's panning preamble
      
      // Find the modified viewport
      kdu_dims roi1, roi2, mod_vp1=mod_viewport;
      roi_info.get_roi_for_pos(s0,roi1);
      roi_info.get_roi_for_pos(s1,roi2);
      adjust_viewport_for_roi(roi1,image_dims,mod_vp1);
      adjust_viewport_for_roi(roi2,image_dims,mod_viewport);
      mod_viewport.augment(mod_vp1);
    }
}

/*****************************************************************************/
/* STATIC       kdu_region_animator::adjust_viewport_for_roi                 */
/*****************************************************************************/

kdu_dims
  kdu_region_animator::adjust_viewport_for_roi(
                                         kdu_region_compositor *compositor,
                                         kdu_dims &mod_viewport,
                                         jpx_metanode node)
{
  kdu_dims source_roi;
  jpx_metanode numlist;
  int numlist_layers=0, numlist_streams=0; bool rres;
  if (node.exists() && (compositor != NULL))
    { 
      numlist = get_link_numlist_and_roi(node,source_roi);
      if (!numlist)
        { 
          numlist = node.get_numlist_container();
          if (numlist.exists())
            while (node.exists() &&
                   (source_roi = node.get_bounding_box()).is_empty())
              node = node.get_parent();
        }
      if (numlist.exists())
        numlist.get_numlist_info(numlist_streams,numlist_layers,rres);
    }

  kdu_dims mapped_roi;
  if ((numlist_layers > 0) || (numlist_streams > 0))
    mapped_roi = compute_mapped_roi(compositor,numlist,source_roi);
  if (!mapped_roi.is_empty())
    { 
      kdu_dims image_dims;
      if ((!compositor->get_total_composition_dims(image_dims)) ||
          (image_dims.size.x < 1) || (image_dims.size.y < 1))
        return mapped_roi; // Should not be possible
      if (mapped_roi.contains(image_dims))
        mapped_roi = kdu_dims();
      else
        adjust_viewport_for_roi(mapped_roi,image_dims,mod_viewport);
    }
  
  return mapped_roi;
}


//-----------------------------------------------------------------------------
// Private member functions
//-----------------------------------------------------------------------------

/*****************************************************************************/
/*                     kdu_region_animator::get_frame                        */
/*****************************************************************************/

kdra_frame *kdu_region_animator::get_frame()
{ 
  kdra_frame *result = free_list;
  if (result == NULL)
    result = new kdra_frame(this);
  else
    free_list = result->next;
  result->init();
  return result;
}

/*****************************************************************************/
/*                   kdu_region_animator::recycle_frame                      */
/*****************************************************************************/

void kdu_region_animator::recycle_frame(kdra_frame *frm)
{ 
  frm->next = free_list;
  free_list = frm;
}

/*****************************************************************************/
/*                 kdu_region_animator::insert_new_frame                     */
/*****************************************************************************/

kdra_frame *
  kdu_region_animator::insert_new_frame(kdra_frame *ref,
                                        bool insert_before_ref)
{
  kdra_frame *elt = get_frame();
  if (head == NULL)
    { 
      assert(ref == NULL);
      head = tail = elt;
    }
  else
    { 
      // First break any loop from `tail' back to `head'.
      tail->next = head->prev = NULL;
      if (insert_before_ref)
        { 
          if (ref == NULL)
            { // Insert at the head
              if (cleanup == head)
                cleanup = NULL;
              elt->next = head;
              head = head->prev = elt;
            }
          else
            { 
              if (cleanup == ref)
                cleanup = NULL;
              elt->next = ref;
              if ((elt->prev = ref->prev) == NULL)
                head = elt;
              else
                elt->prev->next = elt;
              ref->prev = elt;
            }
        }
      else
        { // Insert after `ref'
          if (ref == NULL)
            { // Insert at the tail
              elt->prev = tail;
              tail = tail->next = elt;
            }
          else
            { 
              elt->prev = ref;
              if ((elt->next = ref->next) == NULL)
                tail = elt;
              else
                elt->next->prev = elt;
              ref->next = elt;
            }
        }
      if (repeat_mode && metadata_driven)
        { 
          tail->next = head;
          head->prev = tail;
        }
    }
  if (cleanup == NULL)
    cleanup = elt;
  return elt;
}

/*****************************************************************************/
/*           kdu_region_animator::relabel_metadata_driven_frames             */
/*****************************************************************************/

void kdu_region_animator::relabel_metadata_driven_frames()
{
  kdra_frame *scan, *start_of_span=NULL;
  int idx = 0;
  for (scan=(reverse_mode)?tail:head;
       scan != NULL; scan=(reverse_mode)?(scan->prev):(scan->next))
    { 
      if (scan->is_metadata_incomplete())
        break;
      if (start_of_span != NULL)
        { // We are in a span of related numlist matched animation frames;
          // their metadata frame ID's may have intentional gaps.
          assert(!scan->first_match);
          idx = start_of_span->calculate_metadata_id_in_span(scan);
        }
      scan->metadata_frame_id = idx++;
      if (scan->last_match)
        start_of_span = NULL;
      else if (scan->first_match)
        start_of_span = scan;
      if (scan == ((reverse_mode)?head:tail))
        break; // Avoid accidental wrap around in repeat mode
    }
  assert(idx <= metadata_frame_count);
}

/*****************************************************************************/
/*                    kdu_region_animator::remove_frame                      */
/*****************************************************************************/

bool kdu_region_animator::remove_frame(kdra_frame *frm, bool force_remove)
{
  while (frm->frame_reqs != NULL)
    { 
      if (!force_remove)
        return false;
      remove_frame_req(frm->frame_reqs); // Advances us to `frm->next_in_frame'
    }
  frm->set_metadata_complete(); // Remove from any list of incomplete frames
  if (frm == cleanup)
    { 
      if (frm == tail)
        cleanup = NULL;
      else
        cleanup = cleanup->next;
    }
  if (frm->prev != NULL)
    frm->prev->next = frm->next;
  if (frm->next != NULL)
    frm->next->prev = frm->prev;
  if (frm == head)
    { 
      if (frm == tail)
        head = tail = current = NULL;
      else if (frm->next != NULL)
        head = frm->next;
      else
        assert(0);
    }
  if (frm == tail)
    { 
      assert(frm->prev != NULL);
      tail = frm->prev;
    }
  if ((head == tail) && (head != NULL))
    { // Make sure we don't have a cyclic loop involving one frame only
      head->prev = NULL;
      tail->next = NULL;
    }
  if (frm == current)
    { 
      if (current == head)
        { 
          if (!metadata_driven)
            next_video_frame_idx = frm->frame_idx;
          current = NULL;
        }
      else
        current = frm->prev;
    }
  recycle_frame(frm);
  return true;
}

/*****************************************************************************/
/*                  kdu_region_animator::append_frame_req                    */
/*****************************************************************************/

kdra_frame_req *
  kdu_region_animator::append_frame_req(kdra_frame *frm)
{
  kdra_frame_req *elt = new kdra_frame_req;
  elt->frm = frm;
  if ((elt->prev = last_frame_req) == NULL)
    { 
      assert(frame_reqs == NULL);
      frame_reqs = elt;
    }
  else
    elt->prev->next = elt;
  last_frame_req = elt;
  assert(elt->next == NULL);
  kdra_frame_req *prq=frm->frame_reqs;
  if (prq != NULL)
    { 
      while (prq->next_in_frame != NULL)
        prq = prq->next_in_frame;
      prq->next_in_frame = elt;
    }
  else
    frm->frame_reqs = elt;
  return elt;
}

/*****************************************************************************/
/*                  kdu_region_animator::remove_frame_req                    */
/*****************************************************************************/

void
  kdu_region_animator::remove_frame_req(kdra_frame_req *elt, bool from_tail)
{
  if (from_tail)
    assert(elt == last_frame_req);
  else
    assert(elt == frame_reqs); // Although the code below is mostly general,
    // the `elt->detach' assumes that the removed frame request is either the
    // first or last frame request for its underlying frame, depending on
    // the `from_tail' argument.
  
  if (elt->prev == NULL)
    { 
      assert(elt == frame_reqs);
      frame_reqs = elt->next;
    }
  else
    elt->prev->next = elt->next;
  if (elt->next == NULL)
    { 
      assert(elt == last_frame_req);
      last_frame_req = elt->prev;
    }
  else
    elt->next->prev = elt->prev;
  elt->detach(from_tail);
  delete elt;
}

/*****************************************************************************/
/*                kdu_region_animator::append_client_request                 */
/*****************************************************************************/

kdra_client_req *
  kdu_region_animator::append_client_request(int user_layers,
                                             kdu_dims user_roi,
                                             kdu_dims user_viewport)
{
  kdra_client_req *elt = new kdra_client_req;
  if ((elt->prev = last_client_request) == NULL)
    client_requests = elt;
  else
    elt->prev->next = elt;
  last_client_request = elt;
  num_client_requests++;
  elt->user_layers = user_layers;
  elt->user_roi = user_roi;
  elt->user_viewport = user_viewport;
  return elt;
}

/*****************************************************************************/
/*                kdu_region_animator::remove_client_request                 */
/*****************************************************************************/

void
  kdu_region_animator::remove_client_request(kdra_client_req *elt)
{
  assert(num_client_requests > 0);
  if (elt->prev == NULL)
    { 
      assert(elt == client_requests);
      client_requests = elt->next;
    }
  else
    elt->prev->next = elt->next;
  if (elt->next == NULL)
    { 
      assert(elt == last_client_request);
      last_client_request = elt->prev;
    }
  else
    elt->next->prev = elt->prev;
  num_client_requests--;
  elt->detach_frame_reqs();
  delete elt;
}

/*****************************************************************************/
/*             kdu_region_animator::abandon_all_client_requests              */
/*****************************************************************************/

void
  kdu_region_animator::abandon_all_client_requests()
{
  while (frame_reqs != NULL)
    remove_frame_req(frame_reqs);
  while (client_requests != NULL)
    remove_client_request(client_requests);
  num_client_requests = 0;
  earliest_client_refresh = -1.0;
  client_refresh_state = 0;
}

/*****************************************************************************/
/*              kdu_region_animator::post_last_client_request                */
/*****************************************************************************/

void
  kdu_region_animator::post_last_client_request(kdu_client *client,
                                                int queue_id,
                                                kdu_long nominal_display_usecs,
                                                kdu_long last_cum_disp_usecs,
                                                kdu_long last_cum_req_usecs,
                                                kdu_long req_to_disp_offset,
                                                kdu_long cpu_allowance_usecs)
{
  kdra_client_req *req = last_client_request;
  assert((req != NULL) && (req->custom_id == 0));
  if ((req->custom_id = next_request_custom_id++) == 0)
    req->custom_id = next_request_custom_id++; // Skip over the 0 value
  kdu_long request_usecs = req->cumulative_display_usecs - last_cum_disp_usecs;
  kdu_long min_request_usecs = 1 + (nominal_display_usecs >> 1);
  if (request_usecs < min_request_usecs)
    request_usecs = min_request_usecs;
  
  // Work out how much of the request is required to get us
  // `min_auto_refresh_interval' seconds past the start of the first
  // frame referenced by the request.
  kdu_long min_auto_refresh_usecs = 
    1+(kdu_long)(1000000.0*min_auto_refresh_interval);
  kdu_long usecs_needed = min_auto_refresh_usecs;
  if (usecs_needed > request_usecs)
    usecs_needed = request_usecs;
  double display_start = req->first_frame->expected_display_start;
  kdu_long slack = (this->display_time_to_ref_usecs(display_start) -
                    (last_cum_req_usecs + usecs_needed + req_to_disp_offset));
    // This is the number of microseconds ahead of the first frame's
    // display time that we expect to reach the point when we have received
    // either the full `request_usecs' microseconds of service or
    // `min_auto_refresh_interval' seconds of service, whichever is less.
  slack -= cpu_allowance_usecs;
  if (slack < 0)
    { // Adjust requested service time
      kdu_long delta = request_usecs >> 1; // Reduce request by up to 50%
      if ((slack += delta) > 0)
        delta -= slack; // Don't need to reduce request length by so much
      request_usecs -= delta;
    }
  req->cumulative_request_usecs = request_usecs + last_cum_req_usecs;
  req->requested_usecs = request_usecs;
  bool preemptive = (req == client_requests); // First request
  client->post_window(&(req->window),queue_id,preemptive,
                      &client_prefs,req->custom_id,request_usecs);
}

/*****************************************************************************/
/*                kdu_region_animator::set_metadata_req_offset               */
/*****************************************************************************/

void
  kdu_region_animator::set_metadata_req_offset(kdu_long req_to_disp_offset,
                                               kdu_long cpu_allowance_usecs,
                                               kdra_frame *last_frame)
{
  double duration = min_auto_refresh_interval;
  if ((last_frame != NULL) && (last_frame->next != NULL))
    duration = last_frame->next->calculate_duration();
  else if (head != NULL)
    duration = head->calculate_duration();
  if (duration > min_auto_refresh_interval)
    duration = min_auto_refresh_interval;
  double offset = duration +
	  0.000001*(double)(req_to_disp_offset + cpu_allowance_usecs);
  if (offset > metadata_request_time_offset)
    metadata_request_time_offset = offset;
}

/*****************************************************************************/
/*                kdu_region_animator::trim_imagery_requests                 */
/*****************************************************************************/

void
  kdu_region_animator::trim_imagery_requests(kdu_client *client, int queue_id)
{
  kdra_client_req *req = last_client_request;
  if (req == NULL)
    return; // Nothing to trim
  assert(req->custom_id != 0);
  kdu_long cumulative_display_usecs = req->cumulative_display_usecs;
  kdu_long cumulative_request_usecs = req->cumulative_request_usecs;
  
  bool first_req_partially_sent=false;
  kdu_long custom_id=0, discard_usecs;
  discard_usecs = client->trim_timed_requests(queue_id,custom_id,
                                              first_req_partially_sent);
  if (discard_usecs <= 0)
    return; // Nothing trimmed
  assert(custom_id != 0);
  cumulative_display_usecs -= discard_usecs;
  cumulative_request_usecs -= discard_usecs;
  
  // Remove client requests, as required
  kdu_long delta_id;
  kdra_frame_req *last_freq = NULL;
  int removed_reqs=0;
  while ((delta_id = (req->custom_id-custom_id)) >= 0)
    { 
      if ((req == client_requests) && !first_req_partially_sent)
        { // We should start a new timed request sequence from scratch.
          abandon_all_client_requests();
          return;
        }
      last_freq = req->first_frame;
      if ((req == client_requests) ||
          ((delta_id == 0) && (last_freq != NULL) &&
           (req == last_freq->client_reqs[0])))
        break; // Don't render the client request list empty
      remove_client_request(req);
      req = last_client_request;
      removed_reqs++;
    }
  req->cumulative_display_usecs = cumulative_display_usecs;
  req->cumulative_request_usecs = cumulative_request_usecs;
  
  // Remove elements from the tail of the `frame_reqs' list until
  // `last_frame_req' becomes equal to `last_freq' -- could leave the
  // list empty.  We cannot do this with `remove_frame_req', because
  // that function expects to remove elements from the head of the list,
  // not the tail, and this makes a difference if the frame request list
  // snakes around the complete set of animation frames more than once.
  int removed_freqs=0;
  while (last_frame_req != last_freq)
    { 
      kdra_frame_req *freq = last_frame_req;
      assert(freq != NULL);
      remove_frame_req(freq,true);      
      removed_freqs++;
    }
  
  if (last_freq != NULL)
    { 
      if ((req = last_freq->client_reqs[1]) != NULL)
        { 
          if (last_freq->client_reqs[0] != NULL)
            remove_client_request(last_freq->client_reqs[0]);
          assert(last_freq->client_reqs[0] == NULL);
          last_freq->client_reqs[0] = req;
          last_freq->client_reqs[1] = NULL;
        }
      assert(last_freq->client_reqs[0] != NULL);
      last_freq->requested_fraction = 0.01F;
    }
}

/*****************************************************************************/
/*             kdu_region_animator::backup_to_display_event_time             */
/*****************************************************************************/

void
  kdu_region_animator::backup_to_display_event_time(double display_event_time)
{
  double hysteresis = 0.1 + 2.0*get_estimated_rendering_time();
    /* We want to avoid backing up too far only to compute a frame that is
       about to be replaced by the next one anyway -- if we do, it can
       happen that the application renders the refresh frame after the
       frame that follows it (previously already renderered) is displayed,
       so that the display flickers back and forth between the next frame,
       the refreshed old frame, and a new version of the next frame that
       is rendered after this one.  To minimize the likelihood of this,
       we introduce hysteresis of twice the expected rendering time, plus
       an extra 0.1s -- again to mitigate against flicker. */
  while (current != NULL)
    { // Walk back through the frames until we come to the most
      // appropriate one.  Along the way, we may need to adjust
      // display times and auto-panning parameters to get back to the
      // true starting point of each frame we encounter.
      current->remove_conditional_frame_step();
      if (current->roi_pan_acceleration > 0.0)
        { // Work back to the start of the auto-pan preamble
          kdu_region_animator_roi roi_info;
          double trans_time = roi_info.init(current->roi_pan_acceleration,
                                            current->roi_pan_max_ds_dt);
          if (current->cur_roi_pan_pos >= 1.0)
            current->display_time -= trans_time;
          else
            current->display_time -=
              roi_info.get_time_for_pos(current->cur_roi_pan_pos);
          current->cur_roi_pan_pos = current->next_roi_pan_pos = 0.0;
          current->cur_display_time = current->display_time;
          current->roi_pan_duration = 0.0;
          if (display_event_time >= current->display_time)
            break;
        }
      else if ((display_event_time + hysteresis) >= current->display_time)
        break;
      if (current->prev == NULL)
        break; // Should not happen; but can't go back any furher anyway

      // If we get here, we need to skip back past the current frame
      kdra_frame *elt = current;
      current = elt->prev;
      elt->display_time = elt->cur_display_time = -1.0;
      elt->reset_pan_params();
      elt->generated = false;
      
      if (!metadata_driven)
        { // We want to remove `elt' -- not absolutely necessary, but this will
          // save memory.  However, we should not be removing any frames unless
          // any later frames are also removed; removal of some frames might
          // not be desirable if there are outstanding JPIP client requests
          // associated with the frame.
          bool force_remove = false;
          bool can_remove = true;
          while ((elt->next != NULL) && can_remove)
            if (remove_frame(elt->next,force_remove))
              force_remove = true; // Must remove all later ones now
            else
              can_remove = false; // Should not be removing anything now
          if (can_remove)
            remove_frame(elt);
        }
    }
}

/*****************************************************************************/
/* STATIC          kdu_region_animator::compute_mapped_roi                   */
/*****************************************************************************/

kdu_dims
  kdu_region_animator::compute_mapped_roi(kdu_region_compositor *compositor,
                                          jpx_metanode numlist,
                                          kdu_dims source_roi)
{
  int numlist_streams=0, numlist_layers=0; bool rres;
  if ((!numlist.get_numlist_info(numlist_streams,numlist_layers,rres)) ||
      ((numlist_streams == 0) && (numlist_layers == 0)))
    return kdu_dims();
  
  kdu_dims mapped_roi;
  kdu_ilayer_ref frm_ilayer;
  while ((frm_ilayer = compositor->get_next_ilayer(frm_ilayer)).exists())
    { 
      int dsi; bool opq; // Dummy variables
      int layer_idx=-1;
      if (compositor->get_ilayer_info(frm_ilayer,layer_idx,dsi,opq) < 1)
        continue;
      if ((numlist_layers != 0) && !numlist.test_numlist_layer(layer_idx))
        continue;
      kdu_istream_ref frm_istream;
      if ((numlist_layers == 0) || !source_roi.is_empty())
        { // Find first `frm_istream' that matches a numlist codestream
          int w=0;
          while ((frm_istream =
                  compositor->get_ilayer_stream(frm_ilayer,w++)).exists())
            { 
              int cs_idx;
              compositor->get_istream_info(frm_istream,cs_idx);
              if (numlist.test_numlist_stream(cs_idx))
                break; // Found a matching codestream
            }
          if (!frm_istream.exists())
            continue; // Cannot use this ilayer
        }
                
      kdu_dims layer_roi;
      if (source_roi.is_empty())
        layer_roi = compositor->find_ilayer_region(frm_ilayer,true);
      else
        layer_roi = compositor->inverse_map_region(source_roi,frm_istream);
      if (!layer_roi.is_empty())
        mapped_roi.augment(layer_roi);
    }
  return mapped_roi;
}

/*****************************************************************************/
/* STATIC        kdu_region_animator::adjust_viewport_for_roi                */
/*****************************************************************************/

void kdu_region_animator::adjust_viewport_for_roi(kdu_dims mapped_roi,
                                                  kdu_dims image_dims,
                                                  kdu_dims &viewport)
{
  if (mapped_roi.is_empty() || image_dims.is_empty())
    return;
  kdu_coords view_pos = mapped_roi.pos;
  view_pos.x += mapped_roi.size.x >> 1;
  view_pos.y += mapped_roi.size.y >> 1;
  view_pos.x -= viewport.size.x >> 1;
  view_pos.y -= viewport.size.y >> 1;
  view_pos.x -= 2;  view_pos.y -= 2;           // Leave a little extra space to
  viewport.size.x += 4;  viewport.size.y += 4; // help with smooth panning
  if (view_pos.x < image_dims.pos.x)
    view_pos.x = image_dims.pos.x;
  if (view_pos.y < image_dims.pos.y)
    view_pos.y = image_dims.pos.y;
  kdu_coords image_lim = image_dims.pos + image_dims.size;
  if (view_pos.x > (image_lim.x - viewport.size.x))
    view_pos.x = image_lim.x - viewport.size.x;
  if (view_pos.y > (image_lim.y - viewport.size.y))
    view_pos.y = image_lim.y - viewport.size.y;
  viewport.pos = view_pos;
}

/*****************************************************************************/
/*                kdu_region_animator::instantiate_next_frame                */
/*****************************************************************************/

kdra_frame *
  kdu_region_animator::instantiate_next_frame(kdra_frame *base)
{
  if (base == NULL)
    base = current;
  kdra_frame *frm = NULL;
  bool skipping_leading_pause_frames = false;
  while (frm == NULL)
    { // May need to loop until we discover a frame that exists
      kdra_frame **frm_ref = NULL;
      if (base != NULL)
        { 
          if (base->may_extend_match)
            { 
              int extend_result = base->extend_numlist_match_span(); 
              if (extend_result < 0)
                { // Verify `base' not removed
                  assert(0);
                }
            }
          if (base->numlist.exists() &&
              !(base->first_match && base->last_match))
            base->instantiate_next_numlist_match(); // May do nothing
          frm_ref = &(base->next);
        }
      if (frm_ref == NULL)
        frm_ref = &head;
      frm = *frm_ref;
      if ((frm != NULL) && frm->is_metadata_incomplete())
        { // Try to expand into metadata-driven animation frames
          kdra_frame *frm_next = frm->next;
          frm->expand_incomplete_metadata(); // May have removed `frm'
          frm = *frm_ref;
          if (frm == NULL)
            return NULL; // No frames remain
          else if (frm->is_metadata_incomplete())
            { // We still have incomplete metadata, but this may be because
              // we have removed the old `frm' and have not yet had a chance
              // to inspect its successor, if any.
              if (frm != frm_next)
                return NULL; // We have attempted to expand `frm' already
              frm = NULL;
              continue; // Worth having another attempt at expansion
            }
          else if (reverse_mode)
            { // We have just discovered one or more new complete frames, but
              // we are parsing them recursively in a backwards order so we
              // do not necessarily discover the frames in linear sequence.
              relabel_metadata_driven_frames();
            }
        }
      if ((frm == NULL) && !metadata_driven)
        { // Create a new record for the animation frame list
          assert(video_range_start >= 0);
          if (video_range_start > video_range_end)
            return NULL; // Empty range
          if (reverse_mode)
            { 
              if ((base != NULL) && !skipping_leading_pause_frames)
                next_video_frame_idx = base->frame_idx - 1;
              if (next_video_frame_idx < video_range_start)
                { 
                  if (!repeat_mode)
                    return NULL; // No more frames in the animation
                  next_video_frame_idx = video_range_end;
                }
              else if (next_video_frame_idx > video_range_end)
                { // Might happen if the range-end has been reduced
                  next_video_frame_idx = video_range_end;
                }
            }
          else
            { 
              if ((base != NULL) && !skipping_leading_pause_frames)
                { 
                  if ((base->frame_idx == INT_MAX) && !repeat_mode)
                    return NULL; // Unlikely, but possible numeric wrap-around
                  next_video_frame_idx = base->frame_idx + 1;
                }
              if (next_video_frame_idx < video_range_start)
                next_video_frame_idx = video_range_start; // Catch up to start
              else if (next_video_frame_idx > video_range_end)
                { 
                  if (base == NULL)
                    next_video_frame_idx = video_range_end; // Range truncated?
                  else if (!repeat_mode)
                    return NULL; // No more frames in the animation
                  else
                    next_video_frame_idx = video_range_start;
                }
            }
          frm = insert_new_frame(NULL,false);
          frm->frame_idx = next_video_frame_idx;
        }
      if (frm == NULL)
        return NULL;
      if (!frm->have_all_source_info)
        { 
          if (frm->find_source_info() < 0)
            frm = NULL; // Above function may have deleted `frm'
          else if ((frm->source_duration == 0.0) && (!metadata_driven))
            { // Encountered a "pause" frame, but we may be wanting to
              // skip over the pause frame.
              if (reverse_mode)
                { // In reverse playback, we skip all pause frames except for
                  // the first frame in the sequence.
                  if (frm->frame_idx != video_range_start)
                    { 
                      if (next_video_frame_idx > video_range_start)
                        next_video_frame_idx--;
                      else if (repeat_mode)
                        next_video_frame_idx = video_range_end;
                      else
                        return NULL;                      
                    }
                }
              else
                { // In forward playback, we skip only leading "pause" frames
                  if ((frm->frame_idx != video_range_end) &&
                      ((base == NULL) || (frm->frame_idx < base->frame_idx)))
                    { 
                      remove_frame(frm);
                      frm = NULL;
                      skipping_leading_pause_frames = true;
                      if (next_video_frame_idx < video_range_end)
                        next_video_frame_idx++;
                      else if (repeat_mode)
                        next_video_frame_idx = 0;
                      else
                        return NULL;
                    }
                }
            }          
        }
    }
  if (!frm->have_all_source_info)
    return NULL;

  // At this point, we have a frame to return.  If it is not the current
  // frame, we will do well to make certain that it has no intermediate
  // frame steps and no partially processed pan preamble -- possibly these
  // were left behind from some earlier part of the animation and we
  // are looping back to the same frame.
  if (frm != current)
    { 
      frm->remove_conditional_frame_step();
      frm->reset_pan_params();
    }
  return frm;
}

/*****************************************************************************/
/*                    kdu_region_animator::note_new_frame                    */
/*****************************************************************************/

void kdu_region_animator::note_new_frame(kdra_frame *frm)
{
  if (((current == NULL) && (frm == head)) ||
      ((current != NULL) && (frm == current->next)))
    next_frame_changed = true;
}

/*****************************************************************************/
/*         kdu_region_animator::conditionally_apply_accumulated_delay        */
/*****************************************************************************/

void kdu_region_animator::conditionally_apply_accumulated_delay()
{ 
  if ((delay_accumulator > 0.0) && (current != NULL) &&
      (current->cur_display_time == current->display_time) &&
      ((current->roi_pan_acceleration <= 0.0) ||
       ((current->cur_roi_pan_pos == 0.0) &&
        (current->next_roi_pan_pos == 1.0))))
    { 
      for (kdra_frame_req *frq=frame_reqs; frq != NULL; frq=frq->next)
        if (frq->expected_display_start >= 0.0)
          { 
            frq->expected_display_start += delay_accumulator;
            frq->expected_display_end += delay_accumulator;
          }
      current->display_time += delay_accumulator;
      current->cur_display_time += delay_accumulator;
      if (current->conditional_step_display_time >= 0.0)
        current->conditional_step_display_time += delay_accumulator;
      delay_accumulator = 0.0;
    }
}
