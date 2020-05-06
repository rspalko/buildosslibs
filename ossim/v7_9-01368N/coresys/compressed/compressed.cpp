/*****************************************************************************/
// File: compressed.cpp [scope = CORESYS/COMPRESSED]
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
   Implements most of the compressed data management machinery which fits
logically between actual code-stream I/O (see "codestream.cpp") and individual
code-block processing (see "blocks.cpp").  Includes the machinery for
generating, tearing down and re-entering tiles, tile-components, resolutions,
subbands and precincts.
******************************************************************************/

#include <string.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include "kdu_elementary.h"
#include "kdu_messaging.h"
#include "kdu_utils.h"
#include "kdu_kernels.h"
#include "kdu_compressed.h"
#include "compressed_local.h"

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
   kdu_error _name("E(compressed.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(compressed.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) kdu_error _name("Kakadu Core Error:\n");
#  define KDU_WARNING(_name,_id) kdu_warning _name("Kakadu Core Warning:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers

using namespace kd_core_local;

/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC     gen_missing_or_invalid_parameter_attributes_error              */
/*****************************************************************************/

static void
  gen_missing_or_invalid_parameter_attributes_error()
{
  KDU_ERROR(e,0x16021604); e <<
  KDU_TXT("Missing or invalid coding parameter attribute.  You are probably "
          "receiving this error when trying to parse or decode a codestream "
          "with missing or invalid marker segments in the main or tile-part "
          "headers.");
}

/*****************************************************************************/
/* STATIC              gen_tile_interface_invalid_error                      */
/*****************************************************************************/

static void
  gen_tile_interface_invalid_error(const char *func_name)
{
  KDU_ERROR_DEV(e,0x11051201); e << "`" << func_name << "' " <<
  KDU_TXT("invoked on an invalid `kdu_tile' interface.  It looks like "
          "the underlying tile has already been closed.");
}

/*****************************************************************************/
/* STATIC                       get_child_dims                               */
/*****************************************************************************/

static inline kdu_dims
  get_child_dims(kdu_dims parent_dims, int branch_x, int branch_y,
                 int low_support_min=0, int low_support_max=0,
                 int high_support_min=0, int high_support_max=0)
  /* Converts a region in the parent node into a region for one of
     its children, given the branch indices.  Each individual branch
     index takes a value of 0 for low-pass, 1 for high-pass and 2 for
     no splitting at all.  The last five arguments are used to extend
     the region in the parent node before reducing it to a subband region,
     where `low_support_min' and `low_support_max' are used for low-pass
     subbands while `high_support_min' and `high_support_max' are used for
     high-pass subbands.  These min/max values are the lower and upper
     bounds in the spatial support of the synthesis filter kernels.
     Non-zero values for these arguments should be used when mapping a
     region of interest from a parent node into its children. */
{
  kdu_coords min = parent_dims.pos;
  kdu_coords lim = min + parent_dims.size;

  if ((branch_x & ~1) == 0)
    { // Parent node is split horizontally
      min.x -= (branch_x)?high_support_max:low_support_max;
      lim.x -= (branch_x)?high_support_min:low_support_min;
      min.x = (min.x + 1 - branch_x) >> 1;
      lim.x = (lim.x + 1 - branch_x) >> 1;
    }

  if ((branch_y & ~1) == 0)
    { // Parent node is split vertically
      min.y -= (branch_y)?high_support_max:low_support_max;
      lim.y -= (branch_y)?high_support_min:low_support_min;
      min.y = (min.y + 1 - branch_y) >> 1;
      lim.y = (lim.y + 1 - branch_y) >> 1;
    }

  kdu_dims result;
  result.pos = min;
  result.size = lim-min;
  return result;
}

/*****************************************************************************/
/* STATIC                   get_partition_indices                            */
/*****************************************************************************/

static inline kdu_dims
  get_partition_indices(kdu_dims partition, kdu_dims region)
  /* Returns the range of indices for elements in the supplied partition,
     which intersect with the supplied region. The `pos' field of the
     partition identifies the coordinates of the upper left hand corner
     of the first element in the partition, having indices (0,0), while the
     `size' field indicates the dimensions of the partition elements.
     Intersecting partitions with regions is a common function in JPEG2000. */
{
  kdu_coords min = region.pos - partition.pos;
  kdu_coords lim = min + region.size;
  min.x = floor_ratio(min.x,partition.size.x);
  lim.x = ceil_ratio(lim.x,partition.size.x);
  min.y = floor_ratio(min.y,partition.size.y);
  lim.y = ceil_ratio(lim.y,partition.size.y);
  if (region.size.x == 0) lim.x = min.x;
  if (region.size.y == 0) lim.y = min.y;

  kdu_dims indices;
  indices.pos = min;
  indices.size = lim-min;

  return indices;
}

/*****************************************************************************/
/* STATIC                        is_power_2                                  */
/*****************************************************************************/

static bool
  is_power_2(int val)
{
  for (; val > 1; val >>= 1)
    if (val & 1)
      return false;
  return (val==1);
}

/*****************************************************************************/
/* STATIC                   check_coding_partition                           */
/*****************************************************************************/

static void
  check_coding_partition(kdu_dims partition)
  /* Coding partitions (namely, code-block and precinct partitions) must have
     exact power-of-2 dimensions and origins equal to 0 or 1. */
{
  if ((partition.pos.x != (partition.pos.x & 1)) ||
      (partition.pos.y != (partition.pos.y & 1)))
    { KDU_ERROR(e,0); e <<
        KDU_TXT("Coding partitions (code-blocks and precinct "
        "partitions) must have origin coordinates equal to 1 or 0 only!");
    }
  if (!(is_power_2(partition.size.x) && is_power_2(partition.size.y)))
    { KDU_ERROR(e,1); e <<
        KDU_TXT("Coding partitions (namely, code-block and precinct "
        "partitions) must have exact power-of-2 dimensions!");
    }
}

/*****************************************************************************/
/* STATIC                      create_child_node                             */
/*****************************************************************************/

static kd_leaf_node *
  create_child_node(kd_node *parent, int child_idx, int branch_mask,
                    kd_node *intermediate_nodes, int &next_inode_idx,
                    kd_subband *subbands, int &next_band_idx,
                    int decomp_val, int sub_level,
                    int orientation, bool hor_high, bool vert_high,
                    int num_hor_extra_stages, bool hor_extra_stage_high[],
                    int num_vert_extra_stages, bool vert_extra_stage_high[],
                    kdu_kernels &kernels)
  /* This recursive function is used to build the decomposition node
     structure associated with any given `kd_resolution' object.  If
     the new child node is a leaf, it is allocated from the `subbands'
     array, using the next entry in sequence.  Otherwise, it is allocated
     from the `intermediate_nodes' array, again taking the next entry
     in sequence.  These entries are identified by the `next_band_idx'
     and `next_inode_idx' arguments, respectively.  The recursive
     creation process should create subbands in the correct order -- this
     is checked by comparing the `kd_subband::descriptor' value (already
     filled in) with its expected value whenever a leaf node is created.
        The `child_idx' argument is the index of the child within its
     parent's `children' array.  The `branch_mask' value holds 1 in the
     least significant bit if horizontal decomposition is applied to
     generate children and 1 in the second least significant bit if vertical
     decomposition is applied to generate the children.  This is used
     to correctly fill in the new child's `kd_node::branch_idx' member
     and thence to generate appropriate dimensions.
        The `decomp_val' and `sub_level' entries work together to control
     further recursive invocation of this function to generate descendants
     for the new child node.  The least significant 2 bits of `decomp_val'
     become the `branch_mask' for any children of this node.  If these
     bits are both 0, the current node is a leaf.  If `sub_level' is 3
     `decomp_val' is ignored and the node must be a leaf.  If `sub_level'
     is 1, the 8 bits beyond the least significant 2 bits in `decomp_val'
     contain the branch mask values to use for each of the current node's
     children, in turn.
        The `orientation' argument holds the index of the branch within
     the primary node's `children' array from which this node derives.  This
     value is written as the `kd_subband::orientation' value for leaf
     nodes.
        The `hor_high' and `vert_high' arguments are true if a horizontal
     (resp. vertical) high-pass filtering and decimation operation has
     already been involved in producing the parent node.  This information
     is used to detect whether or not the subband decomposition is
     compatible with splitting.  If a high-pass subband is subsequently
     split, the owning `resolution' object has its `can_flip' member
     set to false.
        The `num_hor_extra_stages' and `hor_extra_stage_high' arguments
     are used to assemble the decomposition path from the primary node for
     this resolution level, for the purpose of finding BIBO gain information
     in the horizontal direction.  Similarly, the `num_vert_extra_stages'
     and `vert_extra_stage_high' arguments are used to derive BIBO gain
     information for the vertical transform stages to this point.  This
     information is supplied to `kernels.get_bibo_gains', when
     appropriate. */
{
  assert((sub_level >= 1) && (sub_level <= 3));
  int next_branch_mask = decomp_val & 3;
  decomp_val >>= 2;
  if (sub_level == 2)
    decomp_val = 0; // No more decomposition beyond our own children, if any

  kd_leaf_node *result=NULL;
  kd_node *inode=NULL;
  if (next_branch_mask == 0)
    result = &(subbands[next_band_idx++]);
  else
    result = inode = &(intermediate_nodes[next_inode_idx++]);

  result->parent = parent;
  if (branch_mask & 1)
    {
      result->branch_x = (kdu_byte)(child_idx & 1);
      assert(num_hor_extra_stages < 3);
      hor_extra_stage_high[num_hor_extra_stages++] =
        (result->branch_x)?true:false;
      if (result->branch_x)
        {
          if (hor_high)
            result->resolution->can_flip = false;
          hor_high = true;
        }
    }
  else
    {
      result->branch_x = 2;
      assert(!(child_idx & 1));
    }
  if (branch_mask & 2)
    {
      result->branch_y = (kdu_byte)((child_idx>>1) & 1);
      assert(num_vert_extra_stages < 3);
      vert_extra_stage_high[num_vert_extra_stages++] =
        (result->branch_y)?true:false;
      if (result->branch_y)
        {
          if (vert_high)
            result->resolution->can_flip = false;
          vert_high = true;
        }
    }
  else
    {
      result->branch_y = 2;
      assert(!(child_idx & 2));
    }

  // Do the BIBO gain stuff here
  if (inode != NULL)
    {
      assert(next_branch_mask != 0);
      inode->num_hor_steps = (kdu_byte)
        ((next_branch_mask & 1)?
         (result->resolution->tile_comp->kernel_num_steps):0);
      inode->num_vert_steps = (kdu_byte)
        ((next_branch_mask & 2)?
         (result->resolution->tile_comp->kernel_num_steps):0);
      inode->bibo_gains =
        new float[inode->num_hor_steps+inode->num_vert_steps+2];
      float *hor_bibo_gains = inode->bibo_gains;
      float *vert_bibo_gains = inode->bibo_gains + (1+inode->num_hor_steps);
      hor_bibo_gains[0] =
        parent->bibo_gains[parent->num_hor_steps & 254];
      vert_bibo_gains[0] =
        parent->bibo_gains[(1+parent->num_hor_steps) +
                           (parent->num_vert_steps & 254)];

      double lval, hval, *gains;
      kdu_byte primary_hor_depth = parent->resolution->hor_depth;
      kdu_byte primary_vert_depth = parent->resolution->vert_depth;
      if (inode->num_hor_steps > 0)
        {
          gains = kernels.get_bibo_gains(primary_hor_depth,
                                         num_hor_extra_stages,
                                         hor_extra_stage_high,lval,hval);
          for (kdu_byte n=0; n < inode->num_hor_steps; n++)
            hor_bibo_gains[n+1] = (float) gains[n];
        }
      if (inode->num_vert_steps > 0)
        {
          gains = kernels.get_bibo_gains(primary_vert_depth,
                                         num_vert_extra_stages,
                                         vert_extra_stage_high,lval,hval);
          for (kdu_byte n=0; n < inode->num_vert_steps; n++)
            vert_bibo_gains[n+1] = (float) gains[n];
        }
    }

  // Now for dimensions and further splitting

  result->dims =
    get_child_dims(parent->dims,result->branch_x,result->branch_y);

  if (next_branch_mask == 0)
    { // This is a leaf node; check the subband descriptor
      assert(result->is_leaf);
      kd_subband *band = (kd_subband *) result;
      band->orientation = (kdu_byte) orientation;
#ifdef _DEBUG
      kdu_int16 descriptor = band->descriptor;
      int hor_count = descriptor & 3;
      int vert_count = (descriptor >> 8) & 3;
      kd_leaf_node *scan;
      for (scan=result; scan != &(scan->resolution->node); scan=scan->parent)
        {
          if ((scan->branch_x > 1) == 0)
            { // Branch is decomposed horizontally
              hor_count -= (scan->branch_x & ~1)?0:1;
              assert(((descriptor>>(2+hor_count))&1) == (int)scan->branch_x);
            }
          if ((scan->branch_y > 1) == 0)
            { // Branch is decomposed vertically
              vert_count -= (scan->branch_y & ~1)?0:1;
              assert(((descriptor>>(10+vert_count))&1) == (int)scan->branch_y);
            }
        }
      assert((hor_count == 0) && (vert_count == 0));
#endif // _DEBUG

      parent->num_descendant_nodes++;
      parent->num_descendant_leaves++;
      return result; // No more children
    }

  int b;
  for (b=0; b < 4; b++)
    if (b == (b & next_branch_mask))
      {
        inode->children[b] =
          create_child_node(inode,b,next_branch_mask,
                            intermediate_nodes,next_inode_idx,
                            subbands,next_band_idx,(decomp_val & 3),
                            sub_level+1,orientation,hor_high,vert_high,
                            num_hor_extra_stages,hor_extra_stage_high,
                            num_vert_extra_stages,vert_extra_stage_high,
                            kernels);
        decomp_val >>= 2;
      }

  parent->num_descendant_leaves += inode->num_descendant_leaves;
  parent->num_descendant_nodes += inode->num_descendant_nodes + 1;

  return result;
}

/*****************************************************************************/
/* INLINE                        compare_sop_num                             */
/*****************************************************************************/

static inline int
  compare_sop_num(int sop_num, int packet_num)
  /* Compares a true packet sequence number with the 16-bit sequence
     number found in an SOP marker segment.  The SOP number is the least
     significant 16 bits of the real packet sequence number, so the function
     returns 0 (equal) if and only if the least significant 16 bits of
     `packet_num' are identical to the value in `sop_num'.  Otherwise, the
     function returns the expected number of packets between the one
     identified by `packet_num' and that identified by `sop_num'.  The
     return value is positive if `sop_num' is deemed to refer to a packet
     following that identified by `packet_num', taking into account the
     fact that `sop_num' contains only the least significant 16 bits of
     the sequence number. */
{
  assert((sop_num >= 0) && (sop_num < (1<<16)));
  int diff = sop_num - packet_num;

  if ((diff & 0x0000FFFF) == 0)
    return 0;
  if ((diff > 0) || ((diff & 0x0000FFFF) <= (1<<15)))
    return diff; // `sop_num' deemed to be greater than `packet_num'.
  else
    return diff-(1<<16); // `sop_num' deemed to be less than `packet_num'.
}

/*****************************************************************************/
/* INLINE           desequence_packets_until_tile_inactive                   */
/*****************************************************************************/

static inline void
  desequence_packets_until_tile_inactive(kd_tile *active,
                                         kd_codestream *codestream)
  /* Read data and/or assign seekable precinct addresses into `active' tile's
     precincts until we come to an SOT marker.  This function exists to
     satisfy the requirement that we never leave the desequencing state in
     the middle of an active tile.  We do not allow random access to
     precincts while a non-addressable active tile exists.
       In multi-threading environments, this function should not be called
     unless the `KD_THREADLOCK_GENERAL' mutex is locked -- i.e., the
     caller must be in the "glock" context, so `kd_precinct_ref::open' calls
     can safely pass NULL for the `env' argument. */
{
  kd_precinct_ref *pref;
  kd_resolution *res;
  kdu_coords p_idx;
  while ((active == codestream->active_tile) && // In case destroyed
         ((pref = active->sequencer->next_in_sequence(res,p_idx)) != NULL))
    if (!(pref->is_desequenced() ||
          pref->open(res,p_idx,false,NULL)->desequence_packet()))
      break;
  if (active == codestream->active_tile)
    {
      codestream->active_tile = NULL;
      active->adjust_unloadability();
    }
}


/* ========================================================================= */
/*                                    kd_tile                                */
/* ========================================================================= */

/*****************************************************************************/
/*                              kd_tile::kd_tile                             */
/*****************************************************************************/

kd_tile::kd_tile(kd_codestream *codestream, kd_tile_ref *tref,
                 kdu_coords idx, kdu_dims dims)
{
  this->structure_bytes = 0; // Set the actual value in `initialize'
  this->codestream = codestream;
  this->tile_ref = tref;  assert(tref->tile == NULL);
  this->t_idx = idx;
  this->t_num = idx.x + idx.y*codestream->tile_span.x;
  this->is_typical = false; // Cannot be typical until initialized
  this->fully_typical = false; // As above
  this->is_in_progress = false;
  this->is_addressable = false;
  tpart_ptrs = NULL; // Start pointers in `initialize' or `reinitialize'
  this->dims = dims;

  region.size = kdu_coords(0,0);
  ppt_markers = NULL;
  packed_headers = NULL;
  sequencer = NULL;
  reslength_checkers = NULL;
  mct_head = mct_tail = NULL;
  comps = NULL;

  typical_next = NULL;
  in_progress_next = in_progress_prev = NULL;
  unloadable_next = unloadable_prev = NULL;
  initialized = is_open = is_unloadable = closed = false;
  needs_reinit = empty_shell = false;
  insert_plt_segments = false;
  resolution_plts = component_plts = layer_plts = false;
  resolution_tparts = component_tparts = layer_tparts = false;
  max_tpart_interruptions = 200;
  num_tparts = next_tpart = 0;
  sequenced_relevant_packets = saved_sequenced_packets = 0;
  next_input_packet_num = next_sop_sequence_num = 0;
  skipping_to_sop = exhausted = false;
}

/*****************************************************************************/
/*                             kd_tile::~kd_tile                             */
/*****************************************************************************/

kd_tile::~kd_tile()
{
  assert(this != codestream->active_tile);

  if (is_in_progress)
    remove_from_in_progress_list(); // Should not happen; just for safety

  if (ppt_markers != NULL)
    delete ppt_markers;

  if (packed_headers != NULL)
    delete packed_headers;
  
  if (reslength_checkers != NULL)
    delete[] reslength_checkers;

  if ((tile_ref != NULL) &&
      (codestream->textualize_out != NULL) && !empty_shell)
    {
      kdu_message &out = *codestream->textualize_out;
      out << "\n>> New attributes for tile " << t_num << ":\n";
      codestream->siz->textualize_attributes(out,t_num,t_num);
      out.flush();
    }

  if ((tile_ref != NULL) && !(empty_shell || is_unloadable))
    {
      int cluster = 1;
      kdu_params *csp;

      while ((csp=codestream->siz->access_cluster(cluster++)) != NULL)
        if ((csp=csp->access_unique(t_num,-1)) != NULL)
          delete csp;
    }

  if (sequencer != NULL)
    delete sequencer;
  if (comps != NULL)
    delete[] comps;
  while ((mct_tail=mct_head) != NULL)
    {
      mct_head = mct_tail->next_stage;
      delete mct_tail;
    }

  if (tile_ref != NULL)
    assert(tile_ref->tile == this);
  if (is_unloadable)
    {
      assert(tile_ref != NULL); // Released tiles not on the unloadable list
      withdraw_from_unloadable_list();
      tile_ref->tile = NULL;
    }
  else if (empty_shell)
    {
      assert(tile_ref != NULL);
      tile_ref->tile = NULL;
    }
  else if (tile_ref != NULL)
    tile_ref->tile = KD_EXPIRED_TILE;

  if (tile_ref != NULL)
    codestream->buf_servers->augment_structure_bytes(-structure_bytes);
}

/*****************************************************************************/
/*                              kd_tile::release                             */
/*****************************************************************************/

void
  kd_tile::release()
{
  assert((tile_ref != NULL) && (this != codestream->active_tile));
  if ((codestream->in == NULL) || empty_shell || !is_typical)
    {
      delete this;
      return;
    }

  // If we get here, we are committed to entering the tile on the released
  // typical tile cache.  To do this, we first need to delete all the
  // elements which are specific to an individual tile.
  tpart_ptrs = NULL;

  if (ppt_markers != NULL)
    {
      delete ppt_markers;
      ppt_markers = NULL;
    }
  if (packed_headers != NULL)
    {
      delete packed_headers;
      packed_headers = NULL;
    }
  if (sequencer != NULL)
    {
      delete sequencer;
      sequencer = NULL;
    }
  precinct_pointer_server.restart();
  codestream->process_pending_precincts(); // Can't have any of this tile's
                                     // precinct's left on the pending list
  for (int c=0; c < num_components; c++)
    { 
      kd_tile_comp *tc = comps + c;
      tc->reset_layer_stats();
      for (int r=0; r <= tc->dwt_levels; r++)
        {
          kd_resolution *res = tc->resolutions + r;
          int num_precincts = res->num_precincts;
          for (int p=0; p < num_precincts; p++)
            res->precinct_refs[p].clear();
        }
    }

  if ((codestream->textualize_out != NULL) && !empty_shell)
    {
      kdu_message &out = *codestream->textualize_out;
      out << "\n>> New attributes for tile " << t_num << ":\n";
      codestream->siz->textualize_attributes(out,t_num,t_num);
      out.flush();
    }

  if (!(empty_shell || is_unloadable))
    {
      int cluster = 1;
      kdu_params *csp;

      while ((csp=codestream->siz->access_cluster(cluster++)) != NULL)
        if ((csp=csp->access_unique(t_num,-1)) != NULL)
          delete csp;
    }

  assert(tile_ref->tile == this);
  if (is_unloadable)
    {
      withdraw_from_unloadable_list();
      tile_ref->tile = NULL;
    }
  else if (empty_shell)
    tile_ref->tile = NULL;
  else
    tile_ref->tile = KD_EXPIRED_TILE;

  codestream->buf_servers->augment_structure_bytes(-structure_bytes);
  structure_bytes = 0;

  // Finally, we assign the tile a NULL `tile_ref' and negative `t_num'
  // so that it will not be mistaken for a real tile if subsequently deleted,
  // and add the tile to the codestream's typical cache list.
  tile_ref = NULL;
  t_num = -1;
  typical_next = codestream->typical_tile_cache;
  codestream->typical_tile_cache = this;
}

/*****************************************************************************/
/*                            kd_tile::initialize                            */
/*****************************************************************************/

void
  kd_tile::initialize()
{
  bool read_failure = false;

  assert(!needs_reinit);
  kdu_long new_structure_bytes = (int) sizeof(*this);

  if (!initialized)
    {
      num_components = codestream->num_components;
      next_tpart = num_tparts = 0;
      if (codestream->in != NULL)
        {
          if (codestream->in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE)
            precinct_pointer_server.initialize(codestream->buf_servers);
          tpart_ptrs = tile_ref->tpart_head;
          read_failure = !read_tile_part_header();
        }
    }

  kdu_params *cod = codestream->siz->access_cluster(COD_params);
  assert(cod != NULL); cod = cod->access_relation(t_num,-1,0,true);
  kdu_params *qcd = codestream->siz->access_cluster(QCD_params);
  assert(qcd != NULL); qcd = qcd->access_relation(t_num,-1,0,true);
  kdu_params *rgn = codestream->siz->access_cluster(RGN_params);
  assert(rgn != NULL); rgn = rgn->access_relation(t_num,-1,0,true);
  kdu_params *org = codestream->siz->access_cluster(ORG_params);
  assert(org != NULL); org = org->access_relation(t_num,-1,0,true);

  // Get tile-wide COD parameters.
  if (!(cod->get(Cuse_sop,0,0,use_sop) &&
        cod->get(Cuse_eph,0,0,use_eph) &&
        cod->get(Cycc,0,0,use_ycc) &&
        cod->get(Calign_blk_last,0,0,coding_origin.y) &&
        cod->get(Calign_blk_last,0,1,coding_origin.x) &&
        cod->get(Clayers,0,0,num_layers)))
    { 
      gen_missing_or_invalid_parameter_attributes_error();
      abort(); // There is no safe choice that would allow continuing if
               // the above function returns.
    }
    
  if (num_layers > codestream->max_tile_layers)
    codestream->max_tile_layers = num_layers;
  
  // Collect any reslength specifiers
  int c;
  if (reslength_checkers == NULL)
    { // See if we need a `reslength_checkers' array
      for (c=-1; c < num_components; c++)
        {
          int max_bytes;
          cod_params *coc = (cod_params *) cod->access_unique(t_num,c);
          if ((coc != NULL) &&
              coc->get(Creslengths,0,0,max_bytes,false,false))
            break;
        }
      if (c < num_components)
        reslength_checkers = new kd_reslength_checker[1+num_components];
    }
  if (reslength_checkers != NULL)
    for (c=-1; c < num_components; c++)
      {
        cod_params *coc = (cod_params *) cod->access_unique(t_num,c);
        if (reslength_checkers[c+1].init(coc,c,num_components,
                                         reslength_checkers+1))
          codestream->reslength_constraints_used = true;
      }

  // Get tile-wide ORG parameters.
  if (codestream->out != NULL)
    { 
      int tpart_flags=0, plt_part_flags=0;
      if (!org->get(ORGtparts,0,0,tpart_flags))
        tpart_flags = 0;
      if (!org->get(ORGgen_plt,0,0,insert_plt_segments))
        insert_plt_segments = false;
      if (!(insert_plt_segments && org->get(ORGplt_parts,0,0,plt_part_flags)))
        plt_part_flags = 0;
      resolution_plts = ((plt_part_flags & ORGplt_parts_R) != 0);
      component_plts = ((plt_part_flags & ORGplt_parts_C) != 0);
      layer_plts = ((plt_part_flags & ORGplt_parts_L) != 0);
      resolution_tparts = ((tpart_flags & ORGtparts_R) != 0);
      component_tparts = ((tpart_flags & ORGtparts_C) != 0);
      layer_tparts = ((tpart_flags & ORGtparts_L) != 0);
      int max_interrupts;
      if (org->get(ORGtpart_interrupts,0,0,max_interrupts))
        { 
          if ((max_interrupts < 0) || (max_interrupts > 254))
            { KDU_ERROR_DEV(e,0x01051402); e <<
              KDU_TXT("The `ORGtpart_interrupts' attribute expects "
                      "tile-part interruption bounds in the range 0 to "
                      "254 -- other values are not meaningful, given that "
                      "no J2K tile may have more than 255 parts.");
            }
          max_tpart_interruptions = max_interrupts;
        }
      else
        max_tpart_interruptions = 200; // Should be enough for most cases
    }

  // Create description of any multi-component transform
  assert(mct_head == NULL);
  if (codestream->uses_mct)
    kd_mct_stage::create_stages(mct_head,mct_tail,codestream->siz,t_num,
                                num_components,codestream->comp_info,
                                codestream->num_output_components,
                                codestream->output_comp_info,
                                (codestream->out != NULL));

  // Initialize appearance parameters
  num_apparent_layers = num_layers;

  // Build tile-components.
  kd_tile_comp *tc = comps = new kd_tile_comp[num_components];
  this->total_precincts = 0;
  for (c=0; c < num_components; c++, tc++)
    { 
      kdu_coords subs, min, lim;

      new_structure_bytes += (int) sizeof(*tc);

      tc->enabled = true;
      tc->is_of_interest = true;
      tc->G_tc = tc->G_tc_restricted = -1.0F;
      tc->codestream = codestream;
      tc->tile = this;
      tc->comp_info = codestream->comp_info + c;
      tc->cnum = c;
      tc->sub_sampling = subs = tc->comp_info->sub_sampling;
      min = dims.pos; lim = min + dims.size;
      min.x = ceil_ratio(min.x,subs.x); lim.x = ceil_ratio(lim.x,subs.x);
      min.y = ceil_ratio(min.y,subs.y); lim.y = ceil_ratio(lim.y,subs.y);
      tc->dims.pos = min; tc->dims.size = lim - min;
      
      kdu_params *coc = cod->access_relation(t_num,c,0,true);
      kdu_params *qcc = qcd->access_relation(t_num,c,0,true);
      kdu_params *rgc = rgn->access_relation(t_num,c,0,true);
      assert((coc != NULL) && (qcc != NULL) && (rgc != NULL));

      bool use_precincts=false;
      bool derived_quant=false;
      float base_delta = 0.0F;
      int atk_idx=0;
      if (!(coc->get(Clevels,0,0,tc->dwt_levels) &&
            coc->get(Creversible,0,0,tc->reversible) &&
            coc->get(Ckernels,0,0,tc->kernel_id) &&
            coc->get(Cuse_precincts,0,0,use_precincts) &&
            coc->get(Cblk,0,0,tc->blk.y) &&
            coc->get(Cblk,0,1,tc->blk.x) &&
            coc->get(Cmodes,0,0,tc->modes) &&
            coc->get(Catk,0,0,atk_idx)))
        { KDU_ERROR(e,0x23091304); e <<
          KDU_TXT("Failed to find all relevant coding parameter attributes: "
                  "Clevels, Creversible, Ckernels, Cuse_precincts, Cblk, "
                  "Cmodes and Catk.  Perhaps there are no COD marker "
                  "segments??");
        }
      if (tc->dwt_levels > 32)
        { KDU_ERROR(e,0x21101302); e <<
          KDU_TXT("Invalid number of DWT levels for tile-component!  "
                  "The JPEG2000 standard permits no more than 32 DWT levels, "
                  "which is already enormous!  Codestream is likely "
                  "corrupt.");
        }
      if (tc->dwt_levels > codestream->max_depth)
        codestream->max_depth = tc->dwt_levels;

      kdu_kernels kernels;
      tc->initialize_kernel_parameters(atk_idx,kernels);

      if ((!tc->reversible) &&
          !(qcc->get(Qderived,0,0,derived_quant) &&
            ((!derived_quant) || qcc->get(Qabs_steps,0,0,base_delta))))
        { KDU_ERROR(e,2); e <<
            KDU_TXT("Tile-components which are compressed "
            "using the irreversible processing path must have quantization "
            "parameters specified in the QCD/QCC marker segments, either "
            "explicitly, or through implicit derivation from the "
            "quantization parameters for the LL subband, as explained in the "
            "JPEG2000 standard, ISO/IEC 15444-1.  The present set of "
            "code-stream parameters is not legal.");
        }
      int roi_levels=0;
      if ((codestream->in != NULL) || !rgc->get(Rlevels,0,0,roi_levels))
        roi_levels = 0;
      float comp_weight;
      if (!coc->get(Cweight,0,0,comp_weight))
        comp_weight = 1.0F;
      tc->apparent_dwt_levels = tc->dwt_levels;
      if (tc->dwt_levels < codestream->min_dwt_levels)
        codestream->min_dwt_levels = tc->dwt_levels;
      if (tc->reversible)
        tc->recommended_extra_bits = 4 + ((use_ycc)?1:0);
      else
        tc->recommended_extra_bits = 7;

      // Run some profile consistency checks.
      if ((tc->dwt_levels < 0)  || (tc->dwt_levels > 32) ||
          (tc->blk.x < 0) || (tc->blk.y < 0) ||
          (tc->blk.x >= 32768) || (tc->blk.y >= 32768))
        abort(); // This condition should not ever be possible, since bounds
                 // on all these parameters are checked within "kdu_params.cpp"
                 // for both reading and writing.
      if (codestream->profile == 0)
        {
          if ((tc->blk.x != tc->blk.y) ||
              ((tc->blk.x != 32) && (tc->blk.x != 64)))
            { KDU_WARNING(w,0); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 code-streams must have "
                "nominally square code-block dimensions, measuring 32x32 or "
                "64x64.  You should set \"Sprofile\" to 1 or 2.");
              codestream->profile = 2; // Prevent further profile warnings.
            }
          else if (tc->modes & (Cmodes_BYPASS|Cmodes_RESET|Cmodes_CAUSAL))
            { KDU_WARNING(w,1); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 code-streams may not use "
                "the BYPASS, RESET or CAUSAL block coder mode switches.  "
                "You should set \"Sprofile\" to 1 or 2.");
              codestream->profile = 2; // Prevent further profile warnings.
            }
        }
      else if (codestream->profile == 1)
        {
          if ((tc->blk.x > 64) || (tc->blk.y > 64))
            { KDU_WARNING(w,2); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-1 code-streams may not have "
                "code-block dimensions larger than 64.  You should set "
                "\"Sprofile\" to 2.");
              codestream->profile = 2; // Prevent further profile warnings.
            }
        }
      if ((codestream->profile != 3) && (coding_origin.x || coding_origin.y))
        { KDU_WARNING(w,3); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  Part-1 code-streams must have "
            "their coding origin (anchor point) set to 0.  A non-zero "
            "coding origin is legal only in JPEG2000 Part 2; set "
            "`Sprofile=PART2' to avoid this warning message.");
          codestream->profile = 3;
        }

      // Find multi-component energy gain terms, if we are a compressor
      if (codestream->out != NULL)
        tc->G_tc = find_multicomponent_energy_gain(c,false,false);

      // Build the layer_stats array
      if (codestream->in != NULL)
        {
          tc->layer_stats = new kdu_long[((1+tc->dwt_levels)*num_layers)<<1];
          tc->reset_layer_stats();
        }
      
      // Now build the resolution level structure.
      int r, b;
      tc->resolutions = new kd_resolution[tc->dwt_levels+1];
      for (r=tc->dwt_levels; r >= 0; r--)
        { 
          kd_resolution *res = tc->resolutions + r;

          new_structure_bytes += (int) sizeof(*res);

          res->codestream = codestream;
          res->tile_comp = tc;
          res->res_level = (kdu_byte) r;
          res->dwt_level = (kdu_byte)(tc->dwt_levels - ((r==0)?0:(r-1)));
          res->hor_depth = tc->comp_info->hor_depth[tc->dwt_levels-r];
          res->vert_depth = tc->comp_info->vert_depth[tc->dwt_levels-r];
          res->propagate_roi = (res->dwt_level <= roi_levels);

          if (r == tc->dwt_levels)
            {
              res->node.parent = NULL;
              res->node.dims = tc->dims;
            }
          else
            {
              res->node.parent = &(res[1].node);
              res->node.parent->children[LL_BAND] = &(res->node);
              res->node.branch_x =
                (res->hor_depth ==
                 tc->comp_info->hor_depth[tc->dwt_levels-r-1])?2:0;
              res->node.branch_y =
                (res->vert_depth ==
                 tc->comp_info->vert_depth[tc->dwt_levels-r-1])?2:0;
              res->node.dims =
                get_child_dims(res->node.parent->dims,
                               res->node.branch_x,res->node.branch_y);
            }
          res->build_decomposition_structure(coc,kernels);

          // Set up precincts.
          res->precinct_partition.pos = coding_origin;
          if (!use_precincts)
            {
              res->precinct_partition.size.x = 1<<15;
              res->precinct_partition.size.y = 1<<15;
            }
          else if (!(coc->get(Cprecincts,tc->dwt_levels-r,0,
                              res->precinct_partition.size.y) &&
                     coc->get(Cprecincts,tc->dwt_levels-r,1,
                              res->precinct_partition.size.x) &&
                     (res->precinct_partition.size.x > 0) &&
                     (res->precinct_partition.size.y > 0)))
            { // The "kdu_params" system should not allow this
              gen_missing_or_invalid_parameter_attributes_error();
              res->precinct_partition.size.x = 1<<15; // Probably safe choices
              res->precinct_partition.size.y = 1<<15; // if above func returns
            }
          check_coding_partition(res->precinct_partition);
          res->precinct_indices = res->region_indices =
            get_partition_indices(res->precinct_partition,
                                  res->node.dims);
          kdu_long num_precincts = res->precinct_indices.area();
          if (num_precincts > (1<<30))
            { KDU_ERROR(e,0x07110802); e <<
              KDU_TXT("Tile-component-resolution encountered in the "
                      "codestream contains way too many precincts!!!  "
                      "The value calculated from codestream parameters "
                      "exceeds (2^30) which means that even the "
                      "storage required to keep a status pointer for "
                      "each precinct will exceed the memory on most "
                      "machines.");
            }
          res->precinct_refs = new kd_precinct_ref[(size_t)num_precincts];
          res->num_precincts = (int)num_precincts;
          this->total_precincts += num_precincts;
          new_structure_bytes += num_precincts * sizeof(kd_precinct_ref);

          // Run profile checks.
          if ((r == 0) && (c < 4) && (codestream->profile < 2) &&
              (((res->node.dims.size.x *
                 res->tile_comp->sub_sampling.x) > 128) ||
               ((res->node.dims.size.y *
                 res->tile_comp->sub_sampling.y) > 128)))
            { KDU_WARNING(w,4); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 and Profile-1 code-streams "
                "must have sufficient DWT levels to permit extraction of a "
                "low resolution image which is no larger than 128x128.   "
                "Try setting a larger value for \"Clevels\" or else set "
                "\"Sprofile\" to 2.");
              codestream->profile = 2;
            }
          if ((codestream->profile == 0) &&
              (res->node.dims.size.x <= 128) &&
              (res->node.dims.size.y <= 128) &&
              (res->num_precincts > 1))
            { KDU_WARNING(w,5); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 code-streams may have "
                "multiple precincts only in those tile-component resolutions "
                "whose dimensions are greater than 128x128.");
              codestream->profile = 2;
            }
        } // End of resolution loop.

      // Next, walk up from the lowest resolution, initializing the
      // parameters for each subband and propagating `kd_resolution::can_flip'
      // information, node and leaf counts.  The subband weights were already
      // installed above, since they appear in order from highest to lowest
      // resolution, rather than lowest to highest.
      int cumulative_subbands = 0;
      bool can_flip = true;
      for (r=0; r <= tc->dwt_levels; r++)
        { 
          kd_resolution *res = tc->resolutions + r;
          if (!can_flip)
            res->can_flip = false;
          else if (!res->can_flip)
            {
              can_flip = false; // Higher resolutions also cannot be flipped
              codestream->cannot_flip = true;
            }
          if (r > 0)
            {
              res->node.num_descendant_leaves +=
                tc->resolutions[r-1].node.num_descendant_leaves;
              res->node.num_descendant_nodes += 1 +
                tc->resolutions[r-1].node.num_descendant_nodes;
            }

          // Get energy weighting parameters.
          float level_weight;
          if (!coc->get(Clev_weights,tc->dwt_levels-r,0,level_weight))
            level_weight = 1.0;
          level_weight *= comp_weight;

          // Scan through the subbands
          for (b=0; b < res->num_subbands; b++)
            { 
              kd_subband *band = res->subbands + b;
              assert(band->resolution == res);

              // Find quantization parameters for the subband
              int kmax, eps, abs_band_idx = cumulative_subbands + b;
              if (tc->reversible)
                {
                  if (!qcc->get(Qabs_ranges,abs_band_idx,0,eps))
                    { // The "kdu_params" system should not have allowed this 
                      gen_missing_or_invalid_parameter_attributes_error();
                      eps = 0; // Allow safe continue if above func returns
                    }
                  band->epsilon = (kdu_byte) eps;
                  band->delta = 1.0F / ((float)(1<<tc->comp_info->precision));
                         // Fake delta value created here ensures that the
                         // weights returned by `kdu_subband::get_msb_wmse'
                         // have the same significance for reversibly and
                         // irreversibly generated subband samples.
                }
              else
                {
                  float delta;
                  if (derived_quant)
                    {
                      int sum_depths =
                        res->hor_depth + (band->descriptor & 3) +
                        res->vert_depth + ((band->descriptor >> 8) & 3);
                      sum_depths -= tc->comp_info->hor_depth[tc->dwt_levels];
                      sum_depths -= tc->comp_info->vert_depth[tc->dwt_levels];

                      // Set `delta' = `base_delta' / 2^{sum_depths/2}
                      delta = base_delta;
                      if (sum_depths & 1)
                        {
                          sum_depths++;
                          delta *= (float) sqrt(2.0);
                        }
                      sum_depths >>= 1;
                      if (sum_depths > 0)
                        delta /= (float)(1<<sum_depths);
                      else
                        delta *= (float)(1<<(-sum_depths));
                    }
                  else if ((!qcc->get(Qabs_steps,abs_band_idx,0,delta)) ||
                           (delta <= 0.0F))
                    { // The "kdu_params" system should not allow this
                      gen_missing_or_invalid_parameter_attributes_error();
                      delta = 1.0F; // Allow safe continue in release mode
                    }
                  assert(delta > 0.0F);
                  band->delta = delta;
                  for (band->epsilon=0; delta < 1.0F; delta*=2.0F)
                    band->epsilon++;
                  assert(delta < 2.0F);
                }

              if (!qcc->get(Qguard,0,0,kmax))
                { // The "kdu_params" system should not have allowed this
                  gen_missing_or_invalid_parameter_attributes_error();
                  kmax = 0; // Allow safe continue in release mode
                }
              band->K_max = (kdu_byte) kmax;

              if (!rgc->get(Rweight,0,0,band->roi_weight))
                band->roi_weight = -1.0F; // Indicates no ROI weights.
              band->K_max += band->epsilon;
              band->K_max -= 1;
              if (!rgc->get(Rshift,0,0,kmax))
                kmax = 0;
              else if ((kmax > 37) && (codestream->profile < 2))
                { KDU_WARNING(w,6); w <<
                    KDU_TXT("Profile violation detected (code-stream is "
                    "technically illegal).  The \"Rshift\" attribute may "
                    "not exceed 37, except in Profile-2 (the unrestricted "
                    "profile).");
                  codestream->profile = 2;
                }
              band->K_max_prime = (kdu_byte)(kmax + band->K_max);
              band->qlim_max_passes = 255;
              if (codestream->in != NULL)
                band->W_b = 0.0f;
              else
                { 
                  int weights_idx = 3*res->dwt_level - band->orientation;

                  if (!coc->get(Cband_weights,weights_idx,0,band->W_b))
                    band->W_b = 1.0F;
                  band->W_b *= level_weight;
                  if (res->res_level == 0)
                    band->W_b = comp_weight; // Don't tamper with DC band.
                }
              bool extra_stage_high_horz[3];
              extra_stage_high_horz[0] = ((band->descriptor>>2)&1)?true:false;
              extra_stage_high_horz[1] = ((band->descriptor>>3)&1)?true:false;
              extra_stage_high_horz[2] = ((band->descriptor>>4)&1)?true:false;
              band->G_b = (float)
                kernels.get_energy_gain(res->hor_depth,
                                        band->descriptor&3,
                                        extra_stage_high_horz);
              bool extra_stage_high_vert[3];
              extra_stage_high_vert[0] = ((band->descriptor>>10)&1)?true:false;
              extra_stage_high_vert[1] = ((band->descriptor>>11)&1)?true:false;
              extra_stage_high_vert[2] = ((band->descriptor>>12)&1)?true:false;
              band->G_b *= (float)
                kernels.get_energy_gain(res->vert_depth,
                                        (band->descriptor>>8)&3,
                                        extra_stage_high_vert);
              band->vis_scale = 1.0f;
              if ((codestream->in != NULL) ||
                  !coc->get(Cvis,0,0,band->vis_floor))
                band->vis_floor = 0.0f;
              else if (band->W_b > 0.0f)
                { 
                  band->vis_scale = 1.0f;
                  if (tc->reversible)
                    { 
                      int rdx = tc->comp_info->precision;
                      rdx += (extra_stage_high_horz[0])?1:0;
                      rdx += (extra_stage_high_horz[1])?1:0;
                      rdx += (extra_stage_high_horz[2])?1:0;
                      rdx += (extra_stage_high_vert[0])?1:0;
                      rdx += (extra_stage_high_vert[1])?1:0;
                      rdx += (extra_stage_high_vert[2])?1:0;
                      if (rdx > 30)
                        { band->vis_scale *= 1.0f/(float)(1<<30); rdx-=30; }
                      band->vis_scale *= 1.0f/(float)(1<<rdx);
                    }
                }

              // Now determine code-block partition parameters for the subband.
              band->block_partition.pos = res->precinct_partition.pos;
              band->block_partition.size = tc->blk;
              int hor_splits = (band->descriptor&3);
              int vert_splits = ((band->descriptor>>8)&3);
              if (res->res_level > 0)
                {
                  band->block_partition.size.x <<= hor_splits;
                  band->block_partition.size.y <<= vert_splits;
                }
              band->block_partition &= res->precinct_partition; // Intersect
              band->blocks_per_precinct.x =
                res->precinct_partition.size.x / band->block_partition.size.x;
              band->blocks_per_precinct.y =
                res->precinct_partition.size.y / band->block_partition.size.y;
              if (res->res_level > 0)
                {
                  if (band->descriptor & (7<<2))
                    band->block_partition.pos.x = 0;
                  if (band->descriptor & (7<<10))
                    band->block_partition.pos.y = 0;
                  band->block_partition.size.x >>= hor_splits;
                  band->block_partition.size.y >>= vert_splits;
                  if (!band->block_partition)
                    { KDU_ERROR(e,0x25050501); e <<
                        KDU_TXT("Precinct partition dimensions too small!  "
                        "Must not be so small that the induced code-block "
                        "partition becomes smaller than 1 sample wide or "
                        "1 sample high within any subband.");
                    }
                }
              check_coding_partition(band->block_partition);
              band->block_indices = 
                get_partition_indices(band->block_partition,band->dims);
              band->log2_blocks_per_precinct = kdu_coords(0,0);
              while ((1<<band->log2_blocks_per_precinct.x) <
                     band->blocks_per_precinct.x)
                band->log2_blocks_per_precinct.x++;
              while ((1<<band->log2_blocks_per_precinct.y) <
                     band->blocks_per_precinct.y)
                band->log2_blocks_per_precinct.y++;
              band->notify_queue = NULL;
              band->notify_quantum_bits = 0;
              band->bkgnd_state.set(0);
              band->pending_bkgnd_state = 0;
            }
          cumulative_subbands += res->num_subbands;
          res->precinct_rows_available = 0;
          res->bkgnd_state.set(0);
          res->bkgnd_next.set(NULL);
          res->complete_initialization();
        }
    } // End of tile-component loop.
  
  if (codestream->out != NULL)
    count_non_empty_tile_comp_subbands();

  // Perform any parameter consistency checks.

  if (use_ycc)
    {
     if ((num_components < 3) ||
         (comps[0].reversible != comps[1].reversible) ||
         (comps[1].reversible != comps[2].reversible) ||
         (comps[0].sub_sampling != comps[1].sub_sampling) ||
         (comps[1].sub_sampling != comps[2].sub_sampling))
       { KDU_ERROR(e,4); e <<
           KDU_TXT("Illegal colour transform specified when "
           "image has insufficient or incompatible colour components.");
       }
    }
  
  // Collect any non-linear point transform parameters required for the tile
  kdu_params *nlt = codestream->siz->access_cluster(NLT_params);
  assert(nlt != NULL);
  kd_output_comp_info *oci = codestream->output_comp_info;
  if (mct_tail == NULL)
    { // `kd_comp_info' records carry the nlt_info pointers
      assert(codestream->num_output_components == num_components);
      comps[0].nlt_info = kd_nlt_info::create(nlt,t_num,-1);
      for (tc=comps, c=0; c < num_components; c++, oci++, tc++)
        { 
          if ((c < (num_components-1)) && (tc->nlt_info != NULL))
            tc[1].nlt_info = tc->nlt_info->add_ref();
          kd_nlt_info *local_nlt_info = kd_nlt_info::create(nlt,t_num,c);
          if ((local_nlt_info != NULL) && (tc->nlt_info != NULL))
            tc->nlt_info->release();
          tc->nlt_info = local_nlt_info;
          if ((tc->nlt_info == NULL) && (oci->nlt_info != NULL))
            tc->nlt_info = oci->nlt_info->add_ref();
        }
    }
  else
    { // `mct_tail->output_comp_info' records carry the nlt_info pointers
      assert(mct_tail->num_outputs == codestream->num_output_components);
      kd_output_comp_info *tile_oci = mct_tail->output_comp_info;
      tile_oci[0].nlt_info = kd_nlt_info::create(nlt,t_num,-1);
      for (c=0; c < mct_tail->num_outputs; c++, tile_oci++, oci++)
        { 
          if ((c < (num_components-1)) && (tile_oci->nlt_info != NULL))
            tile_oci[1].nlt_info = tile_oci->nlt_info->add_ref();
          kd_nlt_info *local_nlt_info = kd_nlt_info::create(nlt,t_num,c);
          if ((local_nlt_info != NULL) && (tile_oci->nlt_info != NULL))
            tile_oci->nlt_info->release();
          tile_oci->nlt_info = local_nlt_info;
          if ((tile_oci->nlt_info == NULL) && (oci->nlt_info != NULL))
            tile_oci->nlt_info = oci->nlt_info->add_ref();
        }
    }

  // Now set up the packet sequencing machinery. Note that packet
  // sequencing is performed incrementally, rather than up front.

  max_relevant_layers = num_layers; // May be reduced later
  max_relevant_packets = total_precincts * num_layers; // May be reduced later
  initialized = true;
  sequenced_relevant_packets = saved_sequenced_packets = 0;
  next_input_packet_num = 0;
  skipping_to_sop = false;
  sequencer = new kd_packet_sequencer(this);

  new_structure_bytes += (int) sizeof(*sequencer);
  assert(structure_bytes == 0);
  structure_bytes += new_structure_bytes;
  codestream->buf_servers->augment_structure_bytes(new_structure_bytes);

  if (!codestream->persistent)
    set_elements_of_interest(); // May change `max_relevant_packets/layers'
  if (read_failure)
    finished_reading();

  fully_typical = is_typical = codestream->siz->check_typical_tile(t_num);
  if (!fully_typical)
    is_typical =
      codestream->siz->check_typical_tile(t_num,(QCD_params ":" RGN_params));
}

/*****************************************************************************/
/*                              kd_tile::recycle                             */
/*****************************************************************************/

void
  kd_tile::recycle(kd_tile_ref *tref, kdu_coords idx, kdu_dims dims)
{
  assert(structure_bytes == 0);

  // Start by reproducing initialization steps from the constructor, which
  // would have been done for new tiles, but do not damage the existing
  // structures.
  assert((tile_ref == NULL) && (t_num < 0) && is_typical);
  this->tile_ref = tref;
  this->t_idx = idx;
  this->t_num = idx.x + idx.y*codestream->tile_span.x;
  this->dims = dims;

  region.size = kdu_coords(0,0);
  typical_next = NULL;
  assert((ppt_markers == NULL) && (packed_headers == NULL) &&
         (sequencer == NULL) && (unloadable_next == NULL) &&
         (unloadable_prev == NULL));
  initialized = is_open = is_unloadable = closed = exhausted = false;
  needs_reinit = empty_shell = false;
  num_tparts = next_tpart = 0;
  sequenced_relevant_packets = saved_sequenced_packets = 0;
  skipping_to_sop = false;
  next_input_packet_num = next_sop_sequence_num = 0;

  // Now reproduce the relevant steps from `initialize', determining as
  // soon as possible whether or not we need to destroy the structure and
  // start from scratch.
  assert(num_components == codestream->num_components);
  bool read_failure = false;
  if (codestream->in != NULL)
    {
      if (codestream->in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE)
        precinct_pointer_server.initialize(codestream->buf_servers);
      tpart_ptrs = tile_ref->tpart_head;
      read_failure = !read_tile_part_header();
    }

  if (!read_failure)
    {
      if (fully_typical && !codestream->siz->check_typical_tile(t_num))
        fully_typical = false;
      if (is_typical && !fully_typical)
        is_typical =
          codestream->siz->check_typical_tile(t_num,
                                              (QCD_params ":" RGN_params));
      if (!is_typical)
        { // Need to initialize from scratch
          if (comps != NULL)
            delete[] comps;
          comps = NULL;
          while ((mct_tail = mct_head) != NULL)
            {
              mct_head = mct_tail->next_stage;
              delete mct_tail;
            }
          initialized = true; // Make sure `initialize' does not call
                              // `read_tile_part_header' again
          insert_plt_segments = false;
          resolution_plts = component_plts = layer_plts = false;
          resolution_tparts = component_tparts = layer_tparts = false;
          initialize();
          return;
        }
    }

  // If we get here, we are able to recycle a typical tile
  num_apparent_layers = num_layers;

  // Visit tile-components
  int c;
  kd_tile_comp *tc = comps;
  this->total_precincts = 0;
  kdu_long new_structure_bytes = (int) sizeof(*this);
  for (c=0; c < num_components; c++, tc++)
    { 
      kdu_coords subs, min, lim;

      new_structure_bytes += (int) sizeof(*tc);

      tc->enabled = true;
      tc->is_of_interest = true;
      tc->G_tc_restricted = -1.0F; // Need to regenerate this if needed
      subs = tc->sub_sampling;
      min = dims.pos; lim = min + dims.size;
      min.x = ceil_ratio(min.x,subs.x); lim.x = ceil_ratio(lim.x,subs.x);
      min.y = ceil_ratio(min.y,subs.y); lim.y = ceil_ratio(lim.y,subs.y);
      tc->dims.pos = min; tc->dims.size = lim - min;
      tc->apparent_dwt_levels = tc->dwt_levels;

      // Now visit the resolution level structure
      int r;
      for (r=tc->dwt_levels; r >= 0; r--)
        {
          kd_resolution *res = tc->resolutions + r;
          new_structure_bytes += (int) sizeof(*res);

          if (r == tc->dwt_levels)
            res->node.dims = tc->dims;
          else
            res->node.dims =
              get_child_dims(res->node.parent->dims,
                             res->node.branch_x,res->node.branch_y);

          res->rescomp = NULL;

          // Check precinct allocation
          res->precinct_indices = res->region_indices =
            get_partition_indices(res->precinct_partition,res->node.dims);
          kdu_long num_precincts = res->precinct_indices.area();
          if (num_precincts != (kdu_long)res->num_precincts)
            { // Reallocate precinct references array
              if (res->precinct_refs != NULL)
                { delete[] res->precinct_refs;
                  res->precinct_refs = NULL; res->num_precincts = 0; }
              if (num_precincts > (1<<30))
                { KDU_ERROR(e,0x07110801); e <<
                  KDU_TXT("Tile-component-resolution encountered in the "
                          "codestream contains way too many precincts!!!  "
                          "The value calculated from codestream parameters "
                          "exceeds (2^30) which means that even the "
                          "storage required to keep a status pointer for "
                          "each precinct will exceed the memory on most "
                          "machines.");
                }
              res->precinct_refs = new kd_precinct_ref[(size_t) num_precincts];
              res->num_precincts = (int)num_precincts;
            }
          this->total_precincts += num_precincts;
          new_structure_bytes += num_precincts * sizeof(kd_precinct_ref);

          // Run profile checks.
          if ((r == 0) && (c < 4) && (codestream->profile < 2) &&
              (((res->node.dims.size.x *
                 res->tile_comp->sub_sampling.x) > 128) ||
               ((res->node.dims.size.y *
                 res->tile_comp->sub_sampling.y) > 128)))
            { KDU_WARNING(w,7); w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 and Profile-1 code-streams "
                "must have sufficient DWT levels to permit extraction of a "
                "low resolution image which is no larger than 128x128.   Try "
                "setting a larger value for \"Clevels\" or else set "
                "\"Sprofile\" to 2.");
              codestream->profile = 2;
            }
          if ((codestream->profile == 0) &&
              (res->node.dims.size.x <= 128) &&
              (res->node.dims.size.y <= 128) &&
              (res->precinct_indices.area() > 1))
            { KDU_WARNING(w,8);  w <<
                KDU_TXT("Profile violation detected (code-stream is "
                "technically illegal).  Profile-0 code-streams may have "
                "multiple precincts only in those tile-component "
                "resolutions whose dimensions are greater than 128x128.");
              codestream->profile = 2;
            }

          // Now visit all intermediate and subband nodes, filling in their
          // dimensions, along with the code-block partition parameters
          kdu_byte b;
          kd_node *node;
          for (b=0; b < res->num_intermediate_nodes; b++)
            {
              node = res->intermediate_nodes + b;
              node->dims = get_child_dims(node->parent->dims,
                                          node->branch_x,node->branch_y);
            }
          for (b=0; b < res->num_subbands; b++)
            {
              kd_subband *band = res->subbands + b;
              band->dims = get_child_dims(band->parent->dims,
                                          band->branch_x,band->branch_y);
              band->block_indices =
                get_partition_indices(band->block_partition,band->dims);
            } // End of subband loop.
          res->complete_initialization();
        } // End of resolution loop.
    } // End of tile-component loop.

  if (!fully_typical)
    { // Need to re-initialize the quantization and ROI parameters
      kdu_params *qcd = codestream->siz->access_cluster(QCD_params);
      assert(qcd != NULL); qcd = qcd->access_relation(t_num,-1,0,true);
      kdu_params *rgn = codestream->siz->access_cluster(RGN_params);
      assert(rgn != NULL); rgn = rgn->access_relation(t_num,-1,0,true);
      for (tc=comps, c=0; c < num_components; c++, tc++)
        {
          kdu_params *qcc = qcd->access_relation(t_num,c,0,true);
          kdu_params *rgc = rgn->access_relation(t_num,c,0,true);
          assert((qcc != NULL) && (rgc != NULL));

          bool derived_quant=false;
          float base_delta = 0.0F;
          if ((!tc->reversible) && !(qcc->get(Qderived,0,0,derived_quant) &&
              ((!derived_quant) || qcc->get(Qabs_steps,0,0,base_delta))))
            { KDU_ERROR(e,0x05010701); e <<
              KDU_TXT("Tile-components which are compressed "
              "using the irreversible processing path must have quantization "
              "parameters specified in the QCD/QCC marker segments, either "
              "explicitly, or through implicit derivation from the "
              "quantization parameters for the LL subband, as explained in "
              "the JPEG2000 standard, ISO/IEC 15444-1.  The present set of "
              "code-stream parameters is not legal.");
            }
          int roi_levels;
          if ((codestream->in != NULL) || !rgc->get(Rlevels,0,0,roi_levels))
            roi_levels = 0;

          // Next walk up from the lowest resolution, modifying quantization
          // and ROI parameters for each subband.
          int b, r, cumulative_subbands=0;
          for (r=0; r <= tc->dwt_levels; r++)
            {
              kd_resolution *res = tc->resolutions + r;
              res->propagate_roi = (res->dwt_level <= roi_levels);
              for (b=0; b < res->num_subbands; b++)
                {
                  kd_subband *band = res->subbands + b;

                  // Find quantization parameters for the subband
                  int kmax, eps, abs_band_idx = cumulative_subbands + b;
                  if (tc->reversible)
                    {
                      if (!qcc->get(Qabs_ranges,abs_band_idx,0,eps))
                        { // The "kdu_params" system should not allow this
                          gen_missing_or_invalid_parameter_attributes_error();
                          eps = 0; // Allow safe continue if above func returns
                        }
                      band->epsilon = (kdu_byte) eps;
                    }
                  else
                    {
                      float delta;
                      if (derived_quant)
                        {
                          int sum_depths =
                            res->hor_depth + (band->descriptor & 3) +
                            res->vert_depth + ((band->descriptor >> 8) & 3);
                          sum_depths -=
                            tc->comp_info->hor_depth[tc->dwt_levels];
                          sum_depths -=
                            tc->comp_info->vert_depth[tc->dwt_levels];

                          // Set `delta' = `base_delta' / 2^{sum_depths/2}
                          delta = base_delta;
                          if (sum_depths & 1)
                            {
                              sum_depths++;
                              delta *= (float) sqrt(2.0);
                            }
                          sum_depths >>= 1;
                          if (sum_depths > 0)
                            delta /= (float)(1<<sum_depths);
                          else
                            delta *= (float)(1<<(-sum_depths));
                        }
                      else if ((!qcc->get(Qabs_steps,abs_band_idx,0,delta)) ||
                               (delta <= 0.0F))
                        { // The "kdu_params" system should not allow this
                          gen_missing_or_invalid_parameter_attributes_error();
                          delta = 1.0F; // Allow safe continue if above returns
                        }
                      assert(delta > 0.0F);
                      band->delta = delta;
                      for (band->epsilon=0; delta < 1.0F; delta*=2.0F)
                        band->epsilon++;
                      assert(delta < 2.0F);
                    }

                  if (!qcc->get(Qguard,0,0,kmax))
                    { // The "kdu_params" system should not allow this
                      gen_missing_or_invalid_parameter_attributes_error();
                      kmax = 0; // Allow safe continue if above func returns
                    }
                  band->K_max = (kdu_byte) kmax;

                  if (!rgc->get(Rweight,0,0,band->roi_weight))
                    band->roi_weight = -1.0F; // Indicates no ROI weights.
                  band->K_max += band->epsilon;
                  band->K_max -= 1;
                  if (!rgc->get(Rshift,0,0,kmax))
                    kmax = 0;
                  else if ((kmax > 37) && (codestream->profile < 2))
                    { KDU_WARNING(w,0x05010702); w <<
                        KDU_TXT("Profile violation detected (code-stream is "
                        "technically illegal).  The \"Rshift\" attribute may "
                        "not exceed 37, except in Profile-2 (the unrestricted "
                        "profile).");
                      codestream->profile = 2;
                    }
                  band->K_max_prime = (kdu_byte)(kmax + band->K_max);
                } // End of subband loop
              cumulative_subbands += res->num_subbands;
            } // End of resolution loop
        } // End of tile-component loop
    } // End of (!fully_typical) case

  // Now set up the packet sequencing machinery.
  max_relevant_layers = num_layers; // May be reduced later
  max_relevant_packets = total_precincts * num_layers; // May be reduced later
  initialized = true;
  sequenced_relevant_packets = saved_sequenced_packets = 0;
  next_input_packet_num = 0;
  skipping_to_sop = false;
  assert(sequencer == NULL);
  sequencer = new kd_packet_sequencer(this);

  new_structure_bytes += (int) sizeof(*sequencer);
  assert(structure_bytes == 0);
  structure_bytes += new_structure_bytes;
  codestream->buf_servers->augment_structure_bytes(new_structure_bytes);

  if (!codestream->persistent)
    set_elements_of_interest(); // May change `max_relevant_packets/layers'

  if (read_failure)
    finished_reading();
  
  if (codestream->out != NULL)
    count_non_empty_tile_comp_subbands();
}

/*****************************************************************************/
/*                              kd_tile::restart                             */
/*****************************************************************************/

void
  kd_tile::restart()
{ // Only called from `kd_codestream::restart'
  if (codestream->textualize_out != NULL)
    {
      if (is_in_progress)
        remove_from_in_progress_list(); // Should not happen; just for safety
      kdu_message &out = *codestream->textualize_out;
      out << "\n>> New attributes for tile " << t_num << ":\n";
      codestream->siz->textualize_attributes(out,t_num,t_num);
      out.flush();
    }

  tpart_ptrs = NULL;
  if (packed_headers != NULL)
    delete packed_headers;
  packed_headers = NULL;
  precinct_pointer_server.restart();

  region.size = kdu_coords(0,0);
  next_tpart = num_tparts = 0;
  closed = exhausted = initialized = false;
  needs_reinit = true;
  sequenced_relevant_packets = saved_sequenced_packets = 0;
  max_relevant_layers = num_layers; // May be reduced later
  max_relevant_packets = total_precincts * num_layers; // May be reduced later
  skipping_to_sop = false;
  next_input_packet_num = next_sop_sequence_num = 0;

  for (int c=0; c < num_components; c++)
    {
      kd_tile_comp *comp = comps+c;
      comp->enabled = true;
      comp->is_of_interest = true;
      comp->G_tc_restricted = -1.0F; // need to regenerate this if needed
      comp->apparent_dwt_levels = comp->dwt_levels;
      comp->region = comp->dims;
      comp->reset_layer_stats();
      for (int r=0; r <= comp->dwt_levels; r++)
        { 
          kd_resolution *res = comp->resolutions + r;
          res->reset_background_processing();
          res->rescomp = NULL;
          res->node.region = res->node.region_cover = res->node.dims;
          res->region_indices = res->precinct_indices;

          int b;
          for (b=0; b < res->num_intermediate_nodes; b++)
            {
              kd_node *node = res->intermediate_nodes + b;
              node->region = node->region_cover = node->dims;
            }
          for (b=0; b < res->num_subbands; b++)
            {
              kd_subband *band = res->subbands + b;
              band->region = band->dims;
              band->region_indices = band->block_indices;
            }

          kdu_coords idx;
          for (idx.y=0; idx.y < res->precinct_indices.size.y; idx.y++)
            for (idx.x=0; idx.x < res->precinct_indices.size.x; idx.x++)
              {
                kd_precinct_ref *ref = res->precinct_refs + idx.x +
                  idx.y*res->precinct_indices.size.x;
                ref->clear();
              }
        }
    }
}

/*****************************************************************************/
/*                           kd_tile::reinitialize                           */
/*****************************************************************************/

void
  kd_tile::reinitialize()
{
  assert(needs_reinit && !is_open);
  needs_reinit = false;

  assert(tile_ref->tile == this);

  // Read code-stream headers as required (input only) and check for changes
  bool read_failure = false;
  if (codestream->in != NULL)
    {
      if (codestream->in->get_capabilities() & KDU_SOURCE_CAP_SEEKABLE)
        precinct_pointer_server.initialize(codestream->buf_servers);
      tpart_ptrs = tile_ref->tpart_head;
      if (!read_tile_part_header())
        read_failure = true;
    }
  if (read_failure || !codestream->siz->any_changes())
    { // We can fully re-use the existing structure
      initialized = true;
      sequencer->init();
      if (!codestream->persistent)
        set_elements_of_interest(); // May change `max_relevant_packets/layers'
      if (read_failure)
        finished_reading();
      if (codestream->out != NULL)
        count_non_empty_tile_comp_subbands();
    }
  else
    { /* We need to delete the tile's contents and start again
         with a call to `initialize'. */
      if (sequencer != NULL)
        delete sequencer;
      sequencer = NULL;
      if (comps != NULL)
        delete[] comps;
      comps = NULL;
      while ((mct_tail=mct_head) != NULL)
        {
          mct_head = mct_tail->next_stage;
          delete mct_tail;
        }
      is_typical = fully_typical = false;
      insert_plt_segments = false;
      resolution_plts = component_plts = layer_plts = false;
      resolution_tparts = component_tparts = layer_tparts = false;
      initialized = true; // Make sure `initialize' does not call
                          // `read_tile_part_header' again
      codestream->buf_servers->augment_structure_bytes(-structure_bytes);
      structure_bytes = 0;
      initialize();
    }
}

/*****************************************************************************/
/*               kd_tile::count_non_empty_tile_comp_subbands                 */
/*****************************************************************************/

void
  kd_tile::count_non_empty_tile_comp_subbands()
{ /* Used to prepare the `kd_tile_comp::completed_subband_counter' values for
     auto-flush processing. */
  for (int c=0; c < num_components; c++)
    { 
      kd_tile_comp *tc = comps+c;
      tc->completed_subband_counter.set(0);
      for (int r=0; r <= tc->dwt_levels; r++)
        { 
          kd_resolution *res = tc->resolutions + r;
          for (int b=0; b < res->num_subbands; b++)
            { 
              kd_subband *band = res->subbands + b;
              if (!band->block_indices.is_empty())
                tc->completed_subband_counter.add_get(1);
            }
        }
    }
}

/*****************************************************************************/
/*                              kd_tile::open                                */
/*****************************************************************************/

void
  kd_tile::open(kdu_thread_env *env)
{
  if (is_open)
    return;
  if (codestream->persistent)
    set_elements_of_interest();
  if (codestream->out != NULL)
    { 
      assert((!is_in_progress) && (in_progress_next == NULL));
      if ((in_progress_prev = codestream->tiles_in_progress_tail) == NULL)
        codestream->tiles_in_progress_head = this;
      else
        in_progress_prev->in_progress_next = this;
      codestream->tiles_in_progress_tail = this;
      is_in_progress = true;

      for (int c=0; c < num_components; c++)
        { 
          kd_tile_comp *comp = comps+c;
          kd_global_rescomp *rc = codestream->global_rescomps + c;
          bool reopening = false;
          int r;
          for (r=comp->dwt_levels; r >= 0; r--, rc+=num_components)
            { 
              kd_resolution *res = comp->resolutions + r;
              if (reopening || (res->rescomp != NULL))
                {
                  assert(res->rescomp == rc);
                  reopening = true;
                }
              else
                {
                  res->rescomp = rc;
                  rc->notify_tile_status(dims,true);

                  // Check for precincts, which contain no code-blocks.  These
                  // should be placed on the ready list immediately, since
                  // they will not be placed there by code-block generation.
                  if ((res->res_level > 0) && !(!res->precinct_indices))
                    {
                      bool hor_split = (res->node.children[HL_BAND] != NULL);
                      bool vert_split = (res->node.children[LH_BAND] != NULL);
                      kdu_coords ps, p_idx = res->precinct_indices.pos;
                      kdu_dims check_dims, p_dims = res->precinct_partition;
                      p_dims.pos.x += p_idx.x*p_dims.size.x;
                      p_dims.pos.y += p_idx.y*p_dims.size.y;
                      for (int corner=0; corner < 4; corner++)
                        { // Check the four corner precincts
                          p_idx.x = p_idx.y = 0;
                          if (corner & 1)
                            {
                              p_idx.x = res->precinct_indices.size.x - 1;
                              if ((p_idx.x < 1) || !hor_split)
                                continue;
                            }
                          if (corner & 2)
                            {
                              p_idx.y = res->precinct_indices.size.y - 1;
                              if ((p_idx.y < 1) || !vert_split)
                                continue;
                            }
                          check_dims = p_dims;
                          check_dims.pos.x += p_idx.x*check_dims.size.x;
                          check_dims.pos.y += p_idx.y*check_dims.size.y;
                          check_dims &= res->node.dims;
                          if (hor_split && ((check_dims.size.x != 1) ||
                                            (check_dims.pos.x & 1)))
                            continue; // Precinct has horiz high-pass blocks
                          if (vert_split && ((check_dims.size.y != 1) ||
                                             (check_dims.pos.y & 1)))
                            continue; // Precinct has vertical high-pass blocks

                          // If we get here, we have found a corner precinct
                          // which contains no code-blocks.  However, if this
                          // decomposition stage is not split in one of the
                          // horizontal or vertical directions, we will need
                          // to deal with a whole row or column (or even an
                          // entire region) of precincts which do not contain
                          // any code-blocks.
                          int h, v, h_span=1, v_span=1;
                          if (!hor_split)
                            {
                              assert(p_idx.x == 0);
                              h_span = res->precinct_indices.size.x;
                            }
                          if (!vert_split)
                            {
                              assert(p_idx.y == 0);
                              v_span = res->precinct_indices.size.y;
                            }

                          for (ps.y=p_idx.y, v=v_span; v > 0; v--, ps.y++)
                            for (ps.x=p_idx.x, h=h_span; h > 0; h--, ps.x++)
                              { // We are in the "glock" context here, which is
                                // different from the "plock" context in which
                                // most precincts get allocated for output
                                // codestreams, in the multi-threaded case.  In
                                // any case, this is OK, because the precinct
                                // server keeps separate resources for glock
                                // and plock based allocation requests.
                                int pnum = ps.x +
                                  ps.y*res->precinct_indices.size.x;
                                kd_precinct *precinct =
                                  res->precinct_refs[pnum].open(res,ps,true,
                                                                NULL);
                                rc->add_ready_precinct(precinct);
                              }
                        }
                    }
                }
            }
          if (!reopening)
            for (r=32-comp->dwt_levels; r > 0; r--, rc+=num_components)
              rc->notify_tile_status(dims,false);
        }
    }
  is_open = true;
  adjust_unloadability();
  codestream->num_open_tiles++;
  if (env != NULL)
    { // Atomically adjust the `tile_ref->flags' word and wake any waiters
      kdu_int32 old_flags, new_flags;
      do { // Enter compare-and-set loop
        old_flags = tile_ref->flags.get();
        new_flags = (old_flags | KD_TREF_FLAGS_OPEN) &
          ~(KD_TREF_FLAGS_OPEN_PENDING | KD_TREF_FLAGS_NEED_WAKEUP);
      } while (!tile_ref->flags.compare_and_set(old_flags,new_flags));
      if (old_flags & KD_TREF_FLAGS_NEED_WAKEUP)
        env->signal_condition(tile_ref->wakeup); // Save even if `wakeup'==NULL
    }
  else
    { // Adjust `tile_ref->flags' non-atomically
      kdu_int32 old_flags = tile_ref->flags.get();
      if (old_flags & (KD_TREF_FLAGS_OPEN_PENDING | KD_TREF_FLAGS_NEED_WAKEUP))
        { KDU_ERROR_DEV(e,0x22041401); e <<
          KDU_TXT("Attempting to open a tile that has been scheduled for "
                  "multi-threaded background opening wihtout providing a "
                  "`kdu_thread_env' reference to make the open call "
                  "thread-safe!");
        }
      kdu_int32 new_flags = old_flags | KD_TREF_FLAGS_OPEN;
      tile_ref->flags.set(new_flags);
    }
}

/*****************************************************************************/
/*                               kd_tile::close                              */
/*****************************************************************************/

kd_tile *
  kd_tile::close(kdu_thread_env *env)
{
  kd_codestream *cs = this->codestream;
  cs->process_pending_precincts(); // Can't have any of this tile's precincts
                                   // left on the pending list.
  kd_tile_ref *tref = this->tile_ref;
  bool self_released = false;
  kdu_int32 closed_flag = 0;
  if (is_open)
    { // Should always be the case
      cs->num_open_tiles--;
      
      is_open = false;
      assert(!closed);
      if (cs->in != NULL)
        { // Release all open precincts which lie in the current region
          // of interest
          for (int c=0; c < num_components; c++)
            { 
              kd_tile_comp *tc = comps + c;
              for (int r=0; r <= tc->dwt_levels; r++)
                { 
                  kd_resolution *res = tc->resolutions + r;
                  res->reset_background_processing();
                  kd_precinct_ref *ref;
                  kd_precinct *precinct;
                  kdu_coords idx, offset_idx;
                  kdu_coords offset =
                  res->region_indices.pos - res->precinct_indices.pos;
                  for (idx.y=0; idx.y < res->region_indices.size.y; idx.y++)
                    for (idx.x=0; idx.x < res->region_indices.size.x; idx.x++)
                      { 
                        offset_idx = idx + offset;
                        ref = res->precinct_refs + offset_idx.x +
                          offset_idx.y*res->precinct_indices.size.x;
                        precinct = ref->deref();
                        if (precinct != NULL)
                          precinct->release();
                      }
                }
            }
        }
  
      // Reset background processing
      for (int c=0; c < num_components; c++)
        { 
          kd_tile_comp *tc = comps + c;
          for (int r=0; r <= tc->dwt_levels; r++)
            tc->resolutions[r].reset_background_processing();
        }
  
      if (!cs->persistent)
        { // NB: codestreams created for interchange are considered persistent
          closed = true;
          closed_flag = KD_TREF_FLAGS_CLOSED;
          if ((((cs->in != NULL) && exhausted) ||
               ((cs->out != NULL) &&
                (sequenced_relevant_packets == max_relevant_packets))) &&
              !cs->allow_restart)
            { 
              release(); // May be self-efacing
              self_released = true; // Must not access `this' again!
            }
        }
      else if (empty_shell)
        { 
          closed = true;
          release(); // Always self-efacing
          self_released = true;
        }
      if (!self_released)
        adjust_unloadability();
    }
  
  if (env != NULL)
    { // Manipulate `tref->flags' atomically
      kdu_int32 old_flags, new_flags;
      do { // Enter compare-and-set loop;
        old_flags = tref->flags.get();
        new_flags = old_flags | closed_flag;
        new_flags &= ~(KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSE_PENDING);
      } while (!tref->flags.compare_and_set(old_flags,new_flags));
    }
  else
    { // Non-atomic manipulation of `tref->flags' is OK 
      kdu_int32 flags = tref->flags.get();
      flags |= closed_flag;
      flags &= ~(KD_TREF_FLAGS_OPEN | KD_TREF_FLAGS_CLOSE_PENDING);
      tref->flags.set(flags);
    }

  return tref->tile;
}

/*****************************************************************************/
/*                    kd_tile::set_elements_of_interest                      */
/*****************************************************************************/

void
  kd_tile::set_elements_of_interest()
{
  int c;
  
  kdu_quality_limiter *limiter = codestream->limiter;
  
  // Inherit appearance parameters from parent object
  if ((mct_head != NULL) && (codestream->out == NULL) &&
      (codestream->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS))
    mct_tail->apply_output_restrictions(codestream->output_comp_info,0,NULL,
                                        (limiter != NULL));
  num_apparent_layers = codestream->max_apparent_layers;
  if (num_apparent_layers > num_layers)
    num_apparent_layers = num_layers;
  region = dims & codestream->region;

  bool parse_only_relevant_packets =
    ((codestream->in != NULL) && !codestream->persistent);
  if (parse_only_relevant_packets)
    {
      max_relevant_layers = num_apparent_layers;
      max_relevant_packets = 0; // Accumulate relevant packets in code below
    }
  
  // Make preliminary pass through the components, leaving the resolution
  // levels to be processed later.
  if (comps == NULL)
    { // This condition should never happen, unless somebody is trying to
      // continue using a codestream which has already generated a fatal error
      // through `kdu_error', which may have thrown a caught exception.
      // Nevertheless, we might as well at least mark the tile as empty so that
      // fatal crashes are less likely (at least here).
      num_components = 0;
      return;
    }
  for (c=0; c < num_components; c++)
    { 
      kd_tile_comp *tc = comps + c;
      tc->is_of_interest = true;
      tc->qlim_type_flags = 0;
      tc->G_tc_restricted = -1.0F; // Need to regenerate this if needed
      if (codestream->out != NULL)
        tc->enabled = true;
      else if (codestream->component_access_mode ==
               KDU_WANT_CODESTREAM_COMPONENTS)
        tc->enabled = (codestream->comp_info[c].apparent_idx >= 0);
      else
        { 
          if (mct_head != NULL)
            tc->enabled = (mct_head->input_required_indices[c] >= 0);
          else
            { // In this case, output components are in one-to-one
              // correspondence with codestream components
              if (use_ycc && (c < 3))
                { 
                  tc->enabled = false;
                  for (int d=0; d < 3; d++)
                    if ((d < codestream->num_output_components) &&
                        (codestream->output_comp_info[d].apparent_idx >= 0))
                      { tc->enabled = true; break; }
                }
              else
                tc->enabled =
                  (c < codestream->num_output_components) &&
                  (codestream->output_comp_info[c].apparent_idx >= 0);
            }
        }
    
      kdu_coords subs = tc->sub_sampling;
      kdu_coords min, lim;
    
      min = region.pos; lim = min + region.size;
      min.x = ceil_ratio(min.x,subs.x); lim.x = ceil_ratio(lim.x,subs.x);
      min.y = ceil_ratio(min.y,subs.y); lim.y = ceil_ratio(lim.y,subs.y);
      tc->region.pos = min; tc->region.size = lim - min;
    }
  
  // Prepare to apply quality limits, if any
  float qlim_Qval = 0.0f; // Will be `Q', described with `kdu_quality_limiter'
  if (limiter != NULL)
    { 
      qlim_Qval = limiter->get_weighted_rmse();
      qlim_Qval *= qlim_Qval; // Convert from RMSE to MSE
      if (codestream->component_access_mode != KDU_WANT_CODESTREAM_COMPONENTS)
        { // Need to scale Q by ratio between the sum of reciprocal
          // sub-sampling factors of output and codestream components.
          double num=0.0, den=0.0;
          for (c=0; c < num_components; c++)
            { 
              kd_tile_comp *tc = comps + c;
              if (tc->enabled)
                den += 1.0 / (double)(tc->sub_sampling.x*tc->sub_sampling.y);
            }
          for (c=0; c < codestream->num_apparent_output_components; c++)
            { 
              kd_output_comp_info *oci = codestream->output_comp_info + c;
              oci = codestream->output_comp_info + oci->from_apparent;
              assert(oci->apparent_idx == c);
              kd_comp_info *ci = oci->subsampling_ref;
              num += 1.0 / (double)(ci->sub_sampling.x*ci->sub_sampling.y);
            }
          if ((num > 0.0) && (den > 0.0))
            qlim_Qval *= (float)(num / den);
          
          if (mct_head != NULL)
            { // Derive `qlim_type_flags' by analyzing `mct_head' blocks
              for (int b=0; b < mct_head->num_blocks; b++)
                { 
                  kd_mct_block *block = mct_head->blocks + b;
                  if (block->is_null_transform)
                    continue;
                  if (block->is_diag_transform)
                    { 
                      assert(block->num_inputs == block->num_outputs);
                      for (c=0; c < block->num_inputs; c++)
                        { 
                          kd_output_comp_info *oci_src =
                          mct_head->output_comp_info+block->output_indices[c];
                          comps[block->input_indices[c]].qlim_type_flags |=
                            oci_src->qlim_type_flags;
                        }
                    }
                  else if ((block->luma_input_idx >= 0) &&
                           (block->apparent_input_types == -1))
                    for (c=0; c < block->num_inputs; c++)
                      comps[block->input_indices[c]].qlim_type_flags |=
                        (c==block->luma_input_idx)?(1<<30):KDU_INT32_MIN;
                  else
                    for (c=0; c < block->num_inputs; c++)
                      comps[block->input_indices[c]].qlim_type_flags |=
                        block->apparent_input_types;
                }
            }
          else if (use_ycc && (num_components >= 3) &&
                   comps[0].enabled && comps[1].enabled && comps[2].enabled)
            { // Second and third components get chroma status
              comps[1].qlim_type_flags = KDU_INT32_MIN;
              comps[2].qlim_type_flags = KDU_INT32_MIN;
            }
        }
      else
        { // Copy `qlim_type_flags' from `codestream->comp_info'
          for (c=0; c < num_components; c++)
            comps[c].qlim_type_flags=codestream->comp_info[c].qlim_type_flags;
        }
      
      // Adjust `qlim_Qval' to absorb the fact that all the G_b gain terms
      // stored with the subbands don't account for discarded resolution levels
      int d;
      float discard_factor = 1.0f;
      for (d=codestream->discard_levels; d > 16; d-=16)
        discard_factor *= (1 << 16);
      discard_factor *= (1 << d);
      qlim_Qval *= discard_factor*discard_factor;
    }

  // Walk through the components again, visiting resolutions and subbands.
  for (c=0; c < num_components; c++)
    { 
      kd_tile_comp *tc = comps + c;
      tc->apparent_dwt_levels = tc->dwt_levels - codestream->discard_levels;
      if (tc->apparent_dwt_levels < 0)
        continue; // Any attempt to access resolution levels will generate an
                  // error, but it is not helpful to generate an error here.

      float qlim_Gcomp=1.0f; // component-part `g_sbn' described in the notes
                             // accompanying `kdu_quality_limiter' constructor
      if ((limiter != NULL) && tc->enabled)
        { 
          if (codestream->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS)
            qlim_Gcomp = find_multicomponent_energy_gain(c,true,true);
          else
            qlim_Gcomp = tc->comp_info->qlim_sq_weight;
        }
    
      // Now work through the resolution levels.
      int r, b;
      for (r=tc->dwt_levels; r >= 0; r--)
        {
          kd_resolution *res = tc->resolutions + r;
          if (res->node.parent == NULL)
            res->node.region = tc->region;
          else if (r >= tc->apparent_dwt_levels)
            res->node.region =
              get_child_dims(res->node.parent->region,
                             res->node.branch_x,res->node.branch_y);
          else
            res->node.region =
              get_child_dims(res->node.parent->region,
                             res->node.branch_x,res->node.branch_y,
                             tc->low_support_min,tc->low_support_max,
                             tc->high_support_min,tc->high_support_max);
          res->node.region &= res->node.dims;
          res->node.region_cover.pos =
            res->node.region_cover.size = kdu_coords(0,0);
          if ((r > tc->apparent_dwt_levels) || !tc->enabled)
            { // This resolution is not of interest
              res->region_indices = res->node.region_cover; // Empty
              continue;
            }

          for (b=0; b < (int) res->num_intermediate_nodes; b++)
            {
              kd_node *node = res->intermediate_nodes + b;
              node->region =
                get_child_dims(node->parent->region,
                               node->branch_x,node->branch_y,
                               tc->low_support_min,tc->low_support_max,
                               tc->high_support_min,tc->high_support_max);
              node->region &= node->dims;
              node->region_cover.pos =
                node->region_cover.size = kdu_coords(0,0);
            }
          for (b=0; b < (int) res->num_subbands; b++)
            { 
              kd_subband *band = res->subbands + b;
              band->region =
                get_child_dims(band->parent->region,
                               band->branch_x,band->branch_y,
                               tc->low_support_min,tc->low_support_max,
                               tc->high_support_min,tc->high_support_max);
              band->region &= band->dims;
              band->region_indices =
                get_partition_indices(band->block_partition,band->region);
              if (!band->region.is_empty())
                band->parent->adjust_cover(band->region,
                                           band->branch_x,band->branch_y);
              band->qlim_max_passes = 255;
              if (limiter != NULL)
                { // Evaluate quality limit
                  int depth = ((int)res->dwt_level)-codestream->discard_levels;
                  kdu_coords subs = tc->comp_info->sub_sampling;
                  if (codestream->transpose)
                    subs.transpose();
                  bool is_chroma = (tc->qlim_type_flags == KDU_INT32_MIN);
                  bool is_full_reversible = tc->reversible &&
                    (codestream->discard_levels==0);
                  float g_sbn = qlim_Gcomp * band->G_b *
                    limiter->get_square_visual_weight((int)band->orientation,
                                                      depth,subs,is_chroma,
                                                      is_full_reversible);
                  if (g_sbn > 0.0f)
                    { // Else we do not limit this subband
                      float delta = (float)sqrt(12.0f * qlim_Qval / g_sbn);
                      int skip_passes = 2; // Temporarily assume we can skip
                         // the final mag-ref + cleanup passes, then see how
                         // many sig-prop passes we can skip.
                      while (band->delta <= (0.35f*delta))
                        { // Can skip sig-prop pass
                          skip_passes += 3; // Advance to the next bit-plane
                          delta *= 0.5f;    // to see if we can skip any more
                        }
                      if (band->delta > (0.5f*delta))
                        { 
                          skip_passes--; // Put the mag-ref pass back
                          if (band->delta > (0.625*delta))
                            skip_passes--; // Put the cleanup pass back
                        }
                      int max_passes = 3*(int)band->K_max_prime - skip_passes;
                      if (max_passes < 0) max_passes = 0;
                      band->qlim_max_passes = (kdu_byte)max_passes;
                    }
                }
            }
          for (b=((int) res->num_intermediate_nodes)-1; b >= 0; b--)
            {
              kd_node *node = res->intermediate_nodes + b;
              if (!node->region.is_empty())
                node->parent->adjust_cover(node->region_cover,
                                           node->branch_x,node->branch_y);
            }
          res->region_indices =
            get_partition_indices(res->precinct_partition,
                                  res->node.region_cover);
          res->region_indices &= res->precinct_indices;

          if (parse_only_relevant_packets)
            max_relevant_packets +=
              max_relevant_layers * (int) res->region_indices.area();
        }
    }
}

/*****************************************************************************/
/*                    kd_tile::withdraw_from_unloadable_list                 */
/*****************************************************************************/

void
  kd_tile::withdraw_from_unloadable_list()
{
  assert(is_unloadable);
  if (unloadable_prev == NULL)
    {
      assert(codestream->unloadable_tiles_head == this);
      codestream->unloadable_tiles_head = unloadable_next;
    }
  else
    unloadable_prev->unloadable_next = unloadable_next;

  if (unloadable_next == NULL)
    {
      assert(codestream->unloadable_tiles_tail == this);
      codestream->unloadable_tiles_tail = unloadable_prev;
    }
  else
    unloadable_next->unloadable_prev = unloadable_prev;
  if (codestream->unloadable_tile_scan == this)
    codestream->unloadable_tile_scan = unloadable_next;
  unloadable_next = unloadable_prev = NULL;
  codestream->num_unloadable_tiles--;
  assert(codestream->num_unloadable_tiles >= 0);
  is_unloadable = false;
}

/*****************************************************************************/
/*                       kd_tile::add_to_unloadable_list                     */
/*****************************************************************************/

void
  kd_tile::add_to_unloadable_list()
{
  assert(!is_unloadable);
  unloadable_prev = codestream->unloadable_tiles_tail;
  unloadable_next = NULL;
  if (unloadable_prev == NULL)
    {
      assert(codestream->unloadable_tiles_head == NULL);
      codestream->unloadable_tiles_head = this;
    }
  else
    unloadable_prev->unloadable_next = this;
  codestream->unloadable_tiles_tail = this;
  codestream->num_unloadable_tiles++;
  is_unloadable = true;
  if ((codestream->unloadable_tile_scan == NULL) &&
      !dims.intersects(codestream->region))
    codestream->unloadable_tile_scan = this;
}

/*****************************************************************************/
/*                      kd_tile::read_tile_part_header                       */
/*****************************************************************************/

bool
  kd_tile::read_tile_part_header()
{
  assert(codestream->in != NULL);
  if (codestream->cached_source)
    { // Reading of cached tile headers is quite different.
      assert(next_tpart == 0);
      if (is_unloadable)
        withdraw_from_unloadable_list();
      codestream->unload_tiles_to_cache_threshold();
      if (codestream->in->set_tileheader_scope(t_num,codestream->tile_span.x *
                                               codestream->tile_span.y))
        {
          kdu_params *root = codestream->siz;
          while (codestream->marker->read())
            if (codestream->marker->get_code() == KDU_PPT)
              { KDU_ERROR(e,6); e <<
                  KDU_TXT("You cannot use PPM or PPT marker segments (packed "
                  "packet headers) with cached compressed data sources.");
              }
            else
              root->translate_marker_segment(codestream->marker->get_code(),
                                             codestream->marker->get_length(),
                                             codestream->marker->get_bytes(),
                                             t_num,0);
          if (!codestream->in->failed())
            { KDU_ERROR(e,7); e <<
                KDU_TXT("Found non-marker code while parsing "
                "tile header marker segments.  Chances are that a marker "
                "segment length field is incorrect!");
            }

          root->finalize_all(t_num,true); // Finalize tile-header params
        }
      else
        empty_shell = true; // Try loading header again if tile is re-opened
      next_tpart = num_tparts = 1;
      codestream->num_completed_tparts++;
      exhausted = true;
      assert(!closed);
      adjust_unloadability();
      return true;
    }

  if (exhausted) // || ((num_tparts > 0) && (next_tpart >= num_tparts)))
    {
      // if (!exhausted)
      //  finished_reading();
           /* All checking of tile part counts has been disabled since
              Adobe's JPEG2000 encoder writes invalid tile-part counts,
              causing diligent decoders to fail on some codestreams
              generated with Adobe software. */
      assert(this != codestream->active_tile);
      return false;
    }

  do {
      kd_tile *active = codestream->active_tile;
      if (active != NULL)
        {
          desequence_packets_until_tile_inactive(active,codestream);
          active = NULL;
        }
      assert(tile_ref->tile == this);
      if (codestream->tpart_ptr_server != NULL)
        { // Seeking is permitted
          if (tpart_ptrs != NULL)
            { // Can seek immediately to the relevant tile-part.
              codestream->in->seek(tpart_ptrs->address);
              tpart_ptrs = tpart_ptrs->next;
              codestream->marker->read(); // Read the SOT marker segment
            }
          else if (codestream->tpart_ptr_server->using_tlm_info() ||
                   ((tile_ref->tpart_head != NULL) &&
                    (tile_ref->tpart_tail == NULL)))
            { // No more tile-parts for this tile.
              num_tparts = next_tpart;
              finished_reading();
              return false;
            }
          else if ((codestream->marker->get_code() == KDU_SOT) &&
                   (codestream->tile_span.x == 1) &&
                   (codestream->tile_span.y == 1))
            { // Save seeking to the next sot_address and reading the SOT
              // marker again.  We don't need this special condition, since
              // the next statement should handle it; however, some
              // codestreams with only one tile might have been written with
              // incorrect tile-part lengths.
              codestream->next_sot_address = 0;
            }
          else if ((!codestream->in->failed()) &&
                   (codestream->next_sot_address > 0))
            { // Seek to next unread SOT address in code-stream
              codestream->in->seek(codestream->next_sot_address);
              codestream->marker->read();
              if ((codestream->marker->get_code() != KDU_SOT) &&
                  !codestream->in->failed())
                { // Something seems to be wrong with the tile-length field
                  // read from the last tile-part so that we have landed at
                  // an invalid location.  Rather than dying here, however,
                  // we will directly search for the SOT marker segment.
                  codestream->in->seek(codestream->next_sot_scan_address);
                  while (codestream->marker->read(true,true) &&
                         (codestream->marker->get_code() != KDU_SOT))
                    { 
                      int len = codestream->marker->get_length();
                      if (len > 0)
                        codestream->in->ignore(len);
                    }
                }
              codestream->next_sot_address = 0; // So we know to change it
            }
          else if (codestream->next_sot_address < 0)
           return false;
        }
      else
        { // Reading code-stream in sequence
          if ((codestream->marker->get_code() != KDU_SOT) &&
               !codestream->in->failed())
            { // Reading in sequence; need to seek over rest of current tile.
              assert(codestream->next_sot_address > 0);
              codestream->in->ignore(codestream->next_sot_address -
                                     codestream->in->get_offset());
              codestream->marker->read();
            }
          codestream->next_sot_address = 0; // So we know to change it
        }

      if (codestream->in->failed())
        {
          if (codestream->next_sot_address == 0)
            codestream->next_sot_address = -1;
          return false;
        }
      if (codestream->marker->get_code() != KDU_SOT)
        { KDU_ERROR(e,8); e <<
            KDU_TXT("Invalid marker code found in code-stream!\n");
          e << KDU_TXT("\tExpected SOT marker and got ");
          codestream->marker->print_current_code(e); e << ".";
        }

      // Now process the SOT marker.
      int seg_length = codestream->marker->get_length();
      assert(seg_length == 8); // Should already have been checked in `read'.
      kdu_byte *bp = codestream->marker->get_bytes();
      kdu_byte *end = bp+seg_length;
      int sot_tnum = kdu_read(bp,end,2);
      kdu_uint32 sot_tpart_length32 = (kdu_uint32) kdu_read(bp,end,4);
      if (sot_tpart_length32 == 12)
        sot_tpart_length32 = 14; // Introduced to fix potentially erroneous
          // tile-part lengths introduced for empty tile-parts when
          // generating TLM info, in versions 6.0 and earlier.
      kdu_long sot_tpart_length = (kdu_long) sot_tpart_length32;
      int sot_tpart = kdu_read(bp,end,1);
      int sot_num_tparts = kdu_read(bp,end,1);

      if ((sot_tnum < 0) ||
          (sot_tnum >= (codestream->tile_span.x*codestream->tile_span.y)))
        { KDU_ERROR(e,9); e <<
            KDU_TXT("Corrupt SOT marker segment found in "
            "codestream: tile-number lies outside the range of available "
            "tiles derived from the SIZ marker segment.");
        }
     
      kdu_coords sot_idx, rel_sot_idx;
      sot_idx.y = sot_tnum / codestream->tile_span.x;
      sot_idx.x = sot_tnum - sot_idx.y*codestream->tile_span.x;
      rel_sot_idx = sot_idx - codestream->tile_indices.pos;
      assert((rel_sot_idx.x >= 0) && (rel_sot_idx.y >= 0) &&
             (rel_sot_idx.x < codestream->tile_indices.size.x) &&
             (rel_sot_idx.y < codestream->tile_indices.size.y));
      kd_tile_ref *tref = codestream->tile_refs +
        rel_sot_idx.x + rel_sot_idx.y*codestream->tile_indices.size.x;
      codestream->next_sot_scan_address = codestream->in->get_offset();
      kdu_long sot_address = codestream->next_sot_scan_address -
        (codestream->marker->get_length()+4);

      if (codestream->next_sot_address == 0)
        { // Advance location of first unparsed SOT marker segment.
          codestream->next_sot_address = sot_address + sot_tpart_length;
          if ((codestream->tpart_ptr_server != NULL) &&
              (!codestream->tpart_ptr_server->using_tlm_info()) &&
              ((tref->tpart_head == NULL) || (tref->tpart_tail != NULL)))
            {
              codestream->tpart_ptr_server->add_tpart(tref,sot_address);
              if (sot_tpart_length == 0)
                {
                  tref->tpart_tail = NULL; // There can be no more t-parts
                  codestream->next_sot_address = -1;
                }
            }
        }

      active = tref->tile;
      if ((active == KD_EXPIRED_TILE) ||
          ((active != NULL) && active->exhausted))
        { /* There is no more relevant information to be parsed from this tile.
             Skip to next SOT marker and discard any PPM/PLT info which may
             be available for this tile-part. */
          if (codestream->ppm_markers != NULL)
            codestream->ppm_markers->ignore_tpart();
          codestream->marker->clear(); // Forces seeking to next tile-part
          continue;
        }

      if ((active != NULL) && active->needs_reinit)
        { // Reading this tile for first time since a codestream restart
          assert(codestream->allow_restart);
          active->reinitialize(); // May recursively call here
          continue;
        }

      if (active != this)
        {
          if (codestream->tpart_ptr_server != NULL)
            { // No need to actually parse the tile-part right now, since we
              // can always come back later.  But we should make sure that
              // `active->tpart_ptrs' correctly indexes the next unread
              // tile-part first.
              if ((active != NULL) && (active->tpart_ptrs == NULL))
                {
                  if ((active->tpart_ptrs = tref->tpart_tail) == NULL)
                    { // May happen, since the tail can be set to NULL to
                      // indicate the end of a tile-part
                      active->tpart_ptrs = tref->tpart_head;
                      for (int tpctr=1; tpctr < active->next_tpart; tpctr++)
                        {
                          active->tpart_ptrs = active->tpart_ptrs->next;
                          assert(active->tpart_ptrs != NULL);
                        }
                    }
                }
              continue;
            }
          else if (sot_tpart_length == 0)
            { // At the last tile-part and it belongs to a different tile
              finished_reading();
              return false;
            }
        }

      // If we get here, we are committed to parsing this tile-part header
      if (active == NULL)
        {
          active = codestream->create_tile(sot_idx);
          continue; // Above call should invoke present function recursively,
                    // so tile-part header will have already been parsed.
        }

      // Read a new tile-part header for the `active' tile.
      assert(active->t_num == sot_tnum);
      if (active->next_tpart != sot_tpart)
        { KDU_ERROR(e,10); e <<
            KDU_TXT("Missing or out-of-sequence tile-parts for "
            "tile number ") << sot_tnum << KDU_TXT(" in code-stream!");
        }
      if (sot_num_tparts != 0)
        {
          if (active->num_tparts == 0)
            active->num_tparts = sot_num_tparts;
          else if (active->num_tparts != sot_num_tparts)
            { KDU_ERROR(e,11); e <<
                KDU_TXT("The number of tile-parts for tile number ")
                << sot_tnum <<
                KDU_TXT(" is identified by different non-zero values "
                "in different SOT markers for the tile!");
            }
        }

      // Release unloadable tiles in accordance with caching thresholds
      if (active->is_unloadable)
        active->withdraw_from_unloadable_list();
      codestream->unload_tiles_to_cache_threshold();

      kdu_params *root = codestream->siz;
      kdu_params *cod = root->access_cluster(COD_params);
      cod = cod->access_relation(sot_tnum,-1,0,true); assert(cod != NULL);
      kdu_params *poc = root->access_cluster(POC_params);
      poc = poc->access_relation(sot_tnum,-1,0,true); assert(poc != NULL);
      assert(active->ppt_markers == NULL);
      kdu_uint16 code=0;
      while (codestream->marker->read() &&
             ((code = codestream->marker->get_code()) != KDU_SOD))
        {
          if (code == KDU_PPT)
            {
              if (codestream->profile == 0)
                { KDU_WARNING(w,9); w <<
                    KDU_TXT("Profile violation detected (code-stream is "
                    "technically illegal).  PPT marker segments may "
                    "not appear within a Profile-0 code-stream.  You "
                    "should set \"Sprofile\" to 1 or 2.");
                  codestream->profile = 2; // Prevent further warnings
                }
              if (active->ppt_markers == NULL)
                active->ppt_markers = new kd_pp_markers;
              active->ppt_markers->add_marker(*(codestream->marker));
            }
          else if (code == KDU_PLT)
            active->precinct_pointer_server.add_plt_marker(
                                              *(codestream->marker),cod,poc);
          else
            root->translate_marker_segment(code,
                                           codestream->marker->get_length(),
                                           codestream->marker->get_bytes(),
                                           sot_tnum,sot_tpart);
        }
      if (code == 0)
        {
          if (!codestream->in->failed())
            { KDU_ERROR(e,12); e <<
                KDU_TXT("Found non-marker code while looking "
                        "for SOD marker to terminate a tile-part header.  "
                        "Chances are that a marker segment length field is "
                        "incorrect!");
            }
          return false;
        }

      root->finalize_all(sot_tnum,true); // Finalize tile-header params
      kdu_long cur_offset = codestream->in->get_offset();

      // Transfer packed packet header data.
      if (active->ppt_markers != NULL)
        {
          if (codestream->ppm_markers != NULL)
            { KDU_ERROR(e,13); e <<
                KDU_TXT("Use of both PPM and PPT marker segments "
                "is illegal!");
            }
          if (active->packed_headers == NULL)
            active->packed_headers = new kd_pph_input(codestream->buf_servers);
          active->ppt_markers->transfer_tpart(active->packed_headers);
          delete active->ppt_markers;
          active->ppt_markers = NULL;
        }
      else if (codestream->ppm_markers != NULL)
        {
          if (active->packed_headers == NULL)
            active->packed_headers = new kd_pph_input(codestream->buf_servers);
          codestream->ppm_markers->transfer_tpart(active->packed_headers);
        }

      // Compute precinct pointers from any available packet length info.
      if (sot_tpart_length == 0)
        active->precinct_pointer_server.start_tpart_body(cur_offset,0,cod,poc,
                                        (active->packed_headers!=NULL),true);
      else
        {
          kdu_long tpart_body_length =
            sot_address + sot_tpart_length - cur_offset;
          assert(tpart_body_length >= 0);
          active->precinct_pointer_server.start_tpart_body(cur_offset,
                                      ((kdu_uint32) tpart_body_length),
                                      cod,poc,(active->packed_headers!=NULL),
                                      false);
        }

      // Make `active' the active tile.
      active->next_tpart++;
      active->is_addressable = active->precinct_pointer_server.is_active();
      codestream->active_tile = active;
      active->adjust_unloadability();
      codestream->num_completed_tparts++;

    } while (this != codestream->active_tile);

  return true;
}

/*****************************************************************************/
/*                        kd_tile::finished_reading                          */
/*****************************************************************************/

bool
  kd_tile::finished_reading()
{
  if (!initialized)
    return false; // Prevent calls here while trying to initialize a tile.
  if (codestream->active_tile == this)
    {
      assert(!exhausted);
      codestream->active_tile = NULL;
      adjust_unloadability();
    }
  else
    {
      adjust_unloadability(); // Just in case
      if (exhausted)
        return false; // True only if this function has been called before
    }
  exhausted = true;
  if (closed)
    { // Should never happen if the codestream object is persistent.
      if (!codestream->allow_restart)
        {
          release(); // Could be self-efacing!
          return true;
        }
      return false;
    }
  for (int c=0; c < num_components; c++)
    {
      kd_tile_comp *tc = comps + c;
      for (int r=0; r <= tc->dwt_levels; r++)
        {
          kd_resolution *res = tc->resolutions + r;
          int num_precincts = res->num_precincts;
          for (int p=0; p < num_precincts; p++)
            {
              kd_precinct *precinct = res->precinct_refs[p].deref();
              if (precinct != NULL)
                precinct->finished_desequencing();
            }
        }
    }
  return false;
}

/*****************************************************************************/
/*                       kd_tile::generate_tile_part                         */
/*****************************************************************************/

kdu_long
  kd_tile::generate_tile_part(int max_layers, kdu_uint16 slope_thresholds[])
{
  if (sequenced_relevant_packets == max_relevant_packets)
    return 0;
  assert(is_in_progress);
  if (next_tpart >= 255)
    { KDU_ERROR(e,14); e <<
        KDU_TXT("Too many tile-parts for tile ") << t_num <<
        KDU_TXT(".  No tile may have more than 255 parts.  This problem "
                "can usually be managed via the `ORGtparts' and/or "
                "`ORGtpart_interrupts' parameter attributes.");
    }
  else if ((codestream->tlm_generator.exists()) &&
           (codestream->tlm_generator.get_max_tparts() <= next_tpart))
    { KDU_ERROR(e,15); e <<
      KDU_TXT("Too many tile-parts for tile ") << t_num <<
      KDU_TXT(".  The maximum number of tile-parts per tile has been "
              "fixed by the `ORGgen_tlm' parameter attribute to ") <<
      codestream->tlm_generator.get_max_tparts() <<
      KDU_TXT(".  It may be that extra tile-parts are being generated by "
              "incremental flushing procedures, which can be hard to "
              "predict; however, you can bound the impact of incremental "
              "flushing via the `ORGtpart_interrupts' parameter attribute.");
    }
  assert(max_layers <= codestream->num_sized_layers);
  next_tpart++; // Makes sure packet sequencer does the right thing.
  kdu_long tpart_bytes = 12 + 2 +
    codestream->siz->generate_marker_segments(NULL,t_num,next_tpart-1);
  int plt_seg_lengths[256]; // Lengths of each PLT segment, including marker
  int current_plt_seg = -1; // No PLT segments generated by default
  int last_plt_resolution = -1; // These hold the resolution level, component
  int last_plt_component = -1;  // index and layer index of the last packet
  int last_plt_layer = -1;      // recorded in the current PLT segment, if any.
  int first_resolution = -1; // Resolution level of first packet in tile-part
  int first_component = -1; // Component index of first packet in tile-part
  int first_layer = -1; // Layer number of first packet in tile-part

  if ((codestream->profile == 0) && (codestream->next_tnum >= 0))
    { // Check for a valid tile-part sequence.
      if (codestream->next_tnum != t_num)
        { KDU_WARNING(w,10); w <<
            KDU_TXT("Profile violation detected (code-stream is technically "
            "illegal).  In a Profile-0 code-stream, all first "
            "tile-parts of all tiles must appear first, in exactly "
            "the same order as their respective tile numbers.");
          codestream->profile = 2;
        }
      codestream->next_tnum++;
      if (codestream->next_tnum ==
          (codestream->tile_span.x * codestream->tile_span.y))
        codestream->next_tnum = -1;
    }

  // Simulate packet sequencing to determine the tile-part length and the
  // number of packets in the tile-part.
  int n, num_tpart_packets = 0;
  bool precinct_not_ready=false; // If we get interrupted by not ready precinct
  kd_precinct *precinct;
  kd_precinct_ref *p_ref;
  kd_resolution *p_res;
  kdu_coords p_idx;
  sequencer->save_state();
  while ((p_ref = sequencer->next_in_sequence(p_res,p_idx)) != NULL)
    { 
      //precinct = p_ref->open(p_res,p_idx,true); -- must not allocate here!
      precinct = p_ref->deref();
      if ((precinct == NULL) || !(precinct->flags & KD_PFLAG_READY))
        { // This precinct has not yet been marked as "ready" for writing
          precinct_not_ready = true;
          break;
        }
      assert(precinct->num_outstanding_blocks.get() == 0);
      int layer_idx = precinct->next_layer_idx;
      assert (layer_idx < num_layers);
      int res_idx = precinct->resolution->res_level;
      int comp_idx = precinct->resolution->tile_comp->cnum;
      if (first_resolution < 0)
        {
          first_resolution = res_idx;
          first_component = comp_idx;
          first_layer = layer_idx;
        }
      if ((resolution_tparts && (first_resolution != res_idx)) ||
          (component_tparts && (first_component != comp_idx)) ||
          (layer_tparts && (first_layer != layer_idx)))
        break; // This packet should be sequenced into the next tile-part
      
      // Include this packet in the current tile-part
      if ((precinct->packet_bytes == NULL) ||
          (precinct->packet_bytes[layer_idx] == 0))
        { KDU_ERROR(e,16); e <<
            KDU_TXT("Attempting to generate tile-part data without "
            "first determining packet lengths.  This may be a consequence of "
            "incomplete simulation of the packet construction process.");
        }
      kdu_long pbytes;
      if (layer_idx < max_layers)
        pbytes = precinct->packet_bytes[layer_idx];
      else
        pbytes = (use_eph)?3:1;
      tpart_bytes += pbytes;
      if (insert_plt_segments)
        { // Include the cost of PLT marker segments in the tile-part length.
          int iplt_bytes;
          for (iplt_bytes=1; pbytes >= 128; pbytes>>=7, iplt_bytes++);
          if (current_plt_seg < 0)
            { current_plt_seg = 0; plt_seg_lengths[0] = iplt_bytes + 5; }
          else if (((plt_seg_lengths[current_plt_seg]+iplt_bytes) > 65537) ||
                   (resolution_plts && (last_plt_resolution != res_idx)) ||
                   (component_plts && (last_plt_component != comp_idx)) ||
                   (layer_plts && (last_plt_layer != layer_idx)))
            { // Need to start a new PLT segment
              tpart_bytes += plt_seg_lengths[current_plt_seg];
              current_plt_seg++;
              if (current_plt_seg > 255)
                { KDU_ERROR(e,17); e <<
                  KDU_TXT("Cannot satisfy the request to generate PLT marker "
                          "segments!  There are so many packets in one "
                          "tile-part that it is beyond the capacity of the "
                          "maximum 256 marker segments to represent length "
                          "information for all tile-parts!!");
                }
              plt_seg_lengths[current_plt_seg] = iplt_bytes + 5;
            }
          else
            plt_seg_lengths[current_plt_seg] += iplt_bytes;
          last_plt_resolution = res_idx;
          last_plt_component = comp_idx;
          last_plt_layer = layer_idx;
        }
      num_tpart_packets++;
      precinct->next_layer_idx++; // This will be restored later.
      sequenced_relevant_packets++; // This will also be restored later.
    }
  if (num_tpart_packets == 0)
    { // Nothing to write
      next_tpart--;
      sequencer->restore_state();
      return 0;
    }
  if (precinct_not_ready)
    { // Generation of this tile-part has been interrupted by a precinct that
      // is not yet ready.  This can happen during incremental codestream
      // generation.  The normal plan is to write the packets we can, leaving
      // the rest for a future tile-part, but this could be a problem if:
      // a) `next_tpart'=255, so we cannot legally add any more tile-parts; or
      // b) we are writing TLM marker segments and space has been reserved
      // for only one more tile-part length within the main header.  Since
      // we don't want to try to figure out exactly how many tile-parts are
      // required to implement the boundaries requested via an `ORGtparts'
      // attribute (if any), we simply provide for a limited number of
      // these tile-part interruptions, which can be configured by providing
      // a second argument to `ORGtparts', but otherwise defaults to 255.
      // This limit is stored in the `kd_tile::max_tpart_interruptions'
      // member.
      if (max_tpart_interruptions > 0)
        max_tpart_interruptions--; // This interruption is OK
      else
        { // Abandon this tile-part for now; later, when all precincts
          // for the tile are available, we can come back and generate it.
          if (!codestream->tpart_interruption_warning_issued)
            { 
              codestream->tpart_interruption_warning_issued = true;
              KDU_WARNING_DEV(w,0x01051401); w <<
              KDU_TXT("Unable to introduce sufficient tile-part interruptions "
                      "to flush codestream data in the optimal manner.  You "
                      "are receiving this warning to inform you that you "
                      "may be able to improve performance by increasing the "
                      "tile-part interruption limit supplied via the "
                      "`ORGtpart_interrupts' parameter attribute.");
            }
          next_tpart--;
          sequencer->restore_state();
          return 0;
        }
    }
  
  if (current_plt_seg >= 0)
    tpart_bytes += plt_seg_lengths[current_plt_seg];
  
  // Now generate the tile-part header
  if ((tpart_bytes>>30) >= 4)
    { KDU_ERROR(e,18); e <<
        KDU_TXT("Length of current tile-part exceeds the maximum "
        "value which can be represented by the 32-bit length field in the "
        "SOT marker!  You will have to split the code-stream into smaller "
        "tile-parts -- see the \"ORGtparts\" parameter attribute.");
    }
  kd_compressed_output *out = codestream->out;
#ifndef NDEBUG
  kdu_long start_bytes = out->get_bytes_written(); // Used in assert below
#endif // !NDEBUG
  
  out->put(KDU_SOT);
  out->put((kdu_uint16) 10);
  out->put((kdu_uint16) t_num);
  out->put((kdu_uint32) tpart_bytes);
  out->put((kdu_byte)(next_tpart-1));
  if (codestream->tlm_generator.exists())
    out->put((kdu_byte) codestream->tlm_generator.get_max_tparts());
  else if (sequenced_relevant_packets == max_relevant_packets)
    out->put((kdu_byte) next_tpart); // This is the last tile-part
  else
    out->put((kdu_byte) 0); // We don't know how many tile-parts might follow
  codestream->layer_sizes[0] += 12 + // 12 is for the SOT marker segment
    codestream->siz->generate_marker_segments(out,t_num,next_tpart-1);
  if (current_plt_seg >= 0)
    { // Generate the PLT marker segments.  To do this, we need to go through
      // the packet sequence all over again.
      assert(insert_plt_segments);
      
      sequencer->restore_state();
      current_plt_seg = -1;
      for (n=0; n < num_tpart_packets; n++)
        { 
          p_ref = sequencer->next_in_sequence(p_res,p_idx);
          assert(p_ref != NULL);
          precinct = p_ref->deref();
          assert(precinct != NULL); // We already tested this during simulation
          if ((current_plt_seg < 0) ||
              (plt_seg_lengths[current_plt_seg] == 0))
            { // Write new PLT segment header
              current_plt_seg++;
              out->put(KDU_PLT);
              out->put((kdu_uint16)(plt_seg_lengths[current_plt_seg]-2));
              out->put((kdu_byte) current_plt_seg);
              plt_seg_lengths[current_plt_seg] -= 5;
            }
        
          kdu_long pbytes;
          int layer_idx = precinct->next_layer_idx;
          if (layer_idx < max_layers)
            pbytes = precinct->packet_bytes[layer_idx];
          else
            pbytes = (use_eph)?3:1;
          int shift;
          for (shift=0; (pbytes>>shift) >= 128; shift += 7);
          for (; shift >= 0; shift -= 7)
            {
              out->put((kdu_byte)(((pbytes>>shift)&0x7F)+((shift>0)?0x80:0)));
              plt_seg_lengths[current_plt_seg]--;
            }
          assert(plt_seg_lengths[current_plt_seg] >= 0);
          precinct->next_layer_idx++; // This will be restored later.
          sequenced_relevant_packets++; // This will also be restored later.
        }
      assert(plt_seg_lengths[current_plt_seg] == 0);
    }
  codestream->layer_sizes[0] += out->put(KDU_SOD);
  
  // Finally, output the packet data
  sequencer->restore_state();
  for (n=0; n < num_tpart_packets; n++)
    { 
      p_ref = sequencer->next_in_sequence(p_res,p_idx);
      assert(p_ref != NULL);
      precinct = p_ref->deref();
      assert(precinct != NULL); // Already tested this during simulation
      int layer_idx = precinct->next_layer_idx;
      kdu_long pbytes, hbytes=0;
      if (layer_idx < max_layers)
        pbytes = precinct->write_packet(slope_thresholds[layer_idx],
                                        false,hbytes);
      else
        { // Write an empty packet
          pbytes = precinct->write_packet(0,true,hbytes);
        }
      codestream->written_packet_header_bytes += hbytes;
      codestream->written_packet_bytes += pbytes;
      if (layer_idx < codestream->num_sized_layers)
        codestream->layer_sizes[layer_idx] += pbytes;
      else
        codestream->layer_sizes[codestream->num_sized_layers-1] += pbytes;
    }
  
  // Finish up
  assert(tpart_bytes == (out->get_bytes_written() - start_bytes));
  codestream->num_completed_tparts++;
  if (codestream->tlm_generator.exists())
    codestream->tlm_generator.add_tpart_length(t_num,tpart_bytes);
  if (sequenced_relevant_packets == max_relevant_packets)
    { // We have finished generating all data for this tile.  See if we
      // have to generate some empty tile-parts to make things legal.  Also
      // see if we can free up the tile's resources.
      if (codestream->tlm_generator.exists())
        { 
          while (codestream->tlm_generator.get_max_tparts() > next_tpart)
            { // Add an empty tile-part
              out->put(KDU_SOT);
              out->put((kdu_uint16) 10);
              out->put((kdu_uint16) t_num);
              out->put((kdu_uint32) 14); // Was 12 in v6.0 and prior releases
              out->put((kdu_byte) next_tpart);
              out->put((kdu_byte) codestream->tlm_generator.get_max_tparts());
              out->put(KDU_SOD);
              codestream->layer_sizes[0] += 14;
              codestream->tlm_generator.add_tpart_length(t_num,14);
              next_tpart++;
            }
        }
      
      remove_from_in_progress_list();
      
      // See if we can free the tile's resources at this point
      if (closed && !codestream->allow_restart)
        release(); // Typically self-efacing
    }
  return tpart_bytes;
}

/*****************************************************************************/
/*                     kd_tile::cache_write_tileheader                       */
/*****************************************************************************/

void
  kd_tile::cache_write_tileheader()
{
  assert(is_in_progress);
  assert(next_tpart == 0);
  next_tpart++;
  int total_tiles = codestream->tile_span.x * codestream->tile_span.y;
  if ((codestream->profile == 0) && (codestream->next_tnum >= 0))
    { // Check for a valid tile-part sequence.
      if (codestream->next_tnum != t_num)
        { KDU_WARNING(w,0x20121201); w <<
          KDU_TXT("Profile violation detected (code-stream is technically "
                  "illegal).  In a Profile-0 code-stream, all first "
                  "tile-parts of all tiles must appear first, in exactly "
                  "the same order as their respective tile numbers.");
          codestream->profile = 2;
        }
      codestream->next_tnum++;
      if (codestream->next_tnum == total_tiles)
        codestream->next_tnum = -1;
    }
  
  kd_compressed_output *out = codestream->out;
  out->start_tileheader(t_num,total_tiles);
  codestream->layer_sizes[0] += 14 + // 14 is for SOT/SOD that we don't write
    codestream->siz->generate_marker_segments(out,t_num,0);
  out->end_tileheader();

  if (sequenced_relevant_packets == max_relevant_packets)
    { // We have finished generating all data for this tile
      remove_from_in_progress_list();
      if (closed && !codestream->allow_restart)
        release(); // Typically self-efacing
    }
}

/*****************************************************************************/
/*                  kd_tile::remove_from_in_progress_list                    */
/*****************************************************************************/

void
  kd_tile::remove_from_in_progress_list()
{
  if (!is_in_progress)
    return;
  assert(codestream->num_incomplete_tiles > 0);
  codestream->num_incomplete_tiles--;
  if (in_progress_prev == NULL)
    {
      assert(this == codestream->tiles_in_progress_head);
      codestream->tiles_in_progress_head = in_progress_next;
    }
  else
    in_progress_prev->in_progress_next = in_progress_next;
  if (in_progress_next == NULL)
    {
      assert(this == codestream->tiles_in_progress_tail);
      codestream->tiles_in_progress_tail = in_progress_prev;
    }
  else
    in_progress_next->in_progress_prev = in_progress_prev;
  in_progress_next = in_progress_prev = NULL;
  is_in_progress = false;
}

/*****************************************************************************/
/*                 kd_tile::find_multicomponent_energy_gain                  */
/*****************************************************************************/

float
  kd_tile::find_multicomponent_energy_gain(int comp_idx,
                                           bool restrict_to_interest,
                                           bool incorporate_qlim_weights)
{
  int n;
  double result = 0.0;
  
  assert(restrict_to_interest || !incorporate_qlim_weights);
    // Calling with `incorporate_qlim_weights' only makes sense when also
    // restricting to the components that are of interest, since the
    // `qlim_sq_weight' member of other components' `kd_output_comp_info'
    // entries might not be valid.

  if (restrict_to_interest)
    assert(codestream->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS);
          // Otherwise, it makes no sense to be trying to compute the
          // impact of multi-component transforms

  if (mct_head != NULL)
    {
      kd_mct_stage *stage;
      int range_min_in=comp_idx,  range_max_in=comp_idx;
      float input_weight = 1.0F;
      for (stage=mct_head; stage != NULL; stage=stage->next_stage)
        {
          int block_idx;
          int range_min_out=0,  range_max_out=-1; // Empty range to start
          for (block_idx=0; block_idx < stage->num_blocks; block_idx++)
            {
              kd_mct_block *block = stage->blocks + block_idx;
              if (restrict_to_interest && (block->num_apparent_outputs == 0))
                continue;
              for (n=0; n < block->num_inputs; n++)
                {
                  if (restrict_to_interest && !block->inputs_required[n])
                    continue;
                  int idx = block->input_indices[n];
                  if ((idx >= range_min_in) && (idx <= range_max_in))
                    {
                      if (stage->prev_stage != NULL)
                        input_weight =
                          stage->prev_stage->output_comp_info[idx].ss_tmp;
                      block->analyze_sensitivity(n,input_weight,
                                                 range_min_out,range_max_out,
                                                 restrict_to_interest);
                    }
                }
            }

          range_min_in = range_min_out;
          range_max_in = range_max_out;
        }
      for (n=range_min_in; n <= range_max_in; n++)
        {
          kd_output_comp_info *oci = mct_tail->output_comp_info + n;
          if (oci->is_of_interest || !restrict_to_interest)
            { 
              double val = oci->ss_tmp / (float)(1<<oci->in_precision);
              val *= val;
              if (incorporate_qlim_weights)
                val *= oci->qlim_sq_weight;
              result += val;
            }
        }
    }
  else if (use_ycc && (comp_idx < 3) && (num_components >= 3))
    { // Calculate energy gain for the Part-1 RCT or ICT transform
      double rgb_gains[3]; // Squared energy contributions
      if (comps[comp_idx].reversible)
        { // All components must be reversible: consider YDbDr to RGB transform
          if (comp_idx == 0)
            rgb_gains[0] = rgb_gains[1] = rgb_gains[2] = 1.0;
          else if (comp_idx == 1)
            { // Db component contributes 0.75 to B, 0.25 to R, G
              rgb_gains[0] = rgb_gains[1] = 0.25*0.25;
              rgb_gains[2] = 0.75*0.75;
            }
          else
            { // Dr component contributes 0.75 to R, 0.25 to G, B
              rgb_gains[0] = 0.75*0.75;
              rgb_gains[1] = rgb_gains[2] = 0.25*0.25;
            }
        }
      else
        { // All components irreversible: consider YCbCr to RGB transform
          double alpha_R=0.299, alpha_G=0.587, alpha_B=0.114;
          if (comp_idx == 0)
            rgb_gains[0] = rgb_gains[1] = rgb_gains[2] = 1.0;
          else if (comp_idx == 1)
            { double f1 = 2.0*(1-alpha_B);
              double f2 = 2.0*alpha_B*(1-alpha_B)/alpha_G;
              rgb_gains[0] = 0.0;  rgb_gains[1] = f2*f2;  rgb_gains[2] = f1*f1;
            }
          else
            { double f1 = 2.0*(1-alpha_R);
              double f2 = 2.0*alpha_R*(1-alpha_R)/alpha_G;
              rgb_gains[0] = f1*f1;  rgb_gains[1] = f2*f2;  rgb_gains[2] = 0.0;
            }
        }
      for (n=0; n < 3; n++)
        { 
          int apparent_idx;
          kd_output_comp_info *oci = codestream->output_comp_info + n;
          if (restrict_to_interest &&
              (((apparent_idx = oci->apparent_idx) < 0) ||
               !comps[apparent_idx].is_of_interest))
            continue;
          double scale = 1.0 / (float)(1<<oci->in_precision);
          scale *= scale;
          if (incorporate_qlim_weights)
            scale *= oci->qlim_sq_weight;
          result += rgb_gains[n] * scale;
        }
    }
  else
    { // Component is just passed straight through
      int apparent_idx;
      kd_output_comp_info *oci = codestream->output_comp_info + comp_idx;
      if (restrict_to_interest &&
          (((apparent_idx = oci->apparent_idx) < 0) ||
           !comps[apparent_idx].is_of_interest))
        result = 0.0; // This really should not happen, since we would not be
                      // asking for the component's energy gain factor if it
                      // was not of interest.
      else
        { 
          result = 1.0 / (float)(1<<oci->in_precision);
          result *= result;
          if (incorporate_qlim_weights)
            result *= oci->qlim_sq_weight;
        }
    }

  double comp_range = (double)(1<<codestream->comp_info[comp_idx].precision);
  result *= comp_range*comp_range;

  if (result < 0.0001)
    result = 0.0001; // Avoid excessively small weights, which could cause
                     // numerical problems in certain parts of the system; we
                     // really should not be asking for the energy gain of
                     // components which have no impact on the outputs of
                     // interest anyway -- during decompression this means that
                     // the algorithm which tells the application what
                     // codestream components are required has failed to do its
                     // job properly; during compression, this means that we
                     // have no path from available image data to this
                     // codestream component, which can be used to deduce its
                     // values.
  return (float) result;
}


/* ========================================================================= */
/*                                  kdu_tile                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                              kdu_tile::close                              */
/*****************************************************************************/

void
  kdu_tile::close(kdu_thread_env *env, bool close_in_background)
{
  if (state == NULL)
    return; // Must have already been closed
  kd_tile_ref *tref = state;
  kdu_int32 flags = tref->flags.get();
  if (!(flags & KD_TREF_FLAGS_OPEN))
    { 
      KDU_WARNING_DEV(w,0x11051402); w <<
      KDU_TXT("Attempting to invoke `kdu_tile::close' on a tile "
              "interface that is already closed (or has at least been "
              "scheduled for background closure).  It seems that you are "
              "keeping invalid tile references around."); // Note: we are only
                  // issuing a warning here so as to avoid killing cleanup
                  // attempts that may have resulted from errors elsewhere.
      state = NULL;
      return;
    }
  if (env != NULL)
    { 
      kd_codestream *cs = tref->tile->codestream;
      cs->start_multi_threading(env); // No harm; just in case
      if (close_in_background)
        { // Schedule for background closure
          kdu_int32 old_flags, new_flags;
          do { // Enter compare-and-set loop
            old_flags = tref->flags.get();
            new_flags = old_flags & ~KD_TREF_FLAGS_OPEN;
            new_flags |= KD_TREF_FLAGS_CLOSE_PENDING | KD_TREF_FLAGS_ENQUEUED;
          } while (!tref->flags.compare_and_set(old_flags,new_flags));
          if ((new_flags ^ old_flags) & KD_TREF_FLAGS_ENQUEUED)
            { // Otherwise it is already scheduled for background processing
              cs->thread_context->append_to_tile_queue(tref);
              cs->thread_context->schedule_tile_processing(env);
            }
        }
      else
        { 
          cs->acquire_lock(KD_THREADLOCK_GENERAL,env);
          if ((tref->tile != NULL) && (tref->tile != KD_EXPIRED_TILE))
            tref->tile->close(env);
          cs->release_lock(KD_THREADLOCK_GENERAL,env);
        }
    }
  else if ((tref->tile != NULL) && (tref->tile != KD_EXPIRED_TILE))
    tref->tile->close(NULL);
  state = NULL; // Renders the interface impotent.
}

/*****************************************************************************/
/*                             kdu_tile::get_tnum                            */
/*****************************************************************************/

int
  kdu_tile::get_tnum()
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_tnum");
    }
  return tp->t_num;
}

/*****************************************************************************/
/*                           kdu_tile::get_tile_idx                          */
/*****************************************************************************/

kdu_coords
  kdu_tile::get_tile_idx()
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_tile_idx");
    }
  kdu_coords idx = tp->t_idx;
  kd_codestream *cs = tp->codestream;
  idx.to_apparent(cs->transpose,cs->vflip,cs->hflip);
  return idx;
}

/*****************************************************************************/
/*                             kdu_tile::get_ycc                             */
/*****************************************************************************/

bool
  kdu_tile::get_ycc()
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_ycc");
    }

  if ((!tp->use_ycc) || (tp->num_components < 3))
    return false;

  int c;
  for (c=0; c < 3; c++)
    if (!tp->comps[c].enabled)
      return false;

  kd_codestream *cs = tp->codestream;
  if (cs->component_access_mode == KDU_WANT_CODESTREAM_COMPONENTS)
    for (c=0; c < 3; c++)
      {
        int apparent_idx = cs->comp_info[c].apparent_idx;
        assert(apparent_idx >= 0);
        if (!tp->comps[apparent_idx].is_of_interest)
          return false;
      }

  return true;
}

/*****************************************************************************/
/*                        kdu_tile::get_nlt_descriptors                      */
/*****************************************************************************/

bool
  kdu_tile::get_nlt_descriptors(int num_comps, int *descriptors)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::set_components_of_interest");
    }
  
  int c;
  kd_codestream *cs = tp->codestream;
  if (cs->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    { 
      if (descriptors != NULL)
        for (c=0; c < num_comps; c++)
          descriptors[c] = -1;
      return false;
    }
  
  // First go through all the NLT records filling out descriptor entries with
  // a special code in bits 4-31 that uniquely identifies the NLT descriptor
  // so that we can come back for a second pass to figure out which ones
  // are unique.
  bool have_nlt=false;
  int valid_comps=0;
  if (tp->mct_tail != NULL)
    { 
      kd_output_comp_info *oci = tp->mct_tail->output_comp_info;
      valid_comps = cs->num_apparent_output_components;
      if (valid_comps > num_comps)
        valid_comps = num_comps;
      for (c=0; c < valid_comps; c++)
        { 
          int true_idx = cs->output_comp_info[c].from_apparent;
          kd_nlt_info *nlt = oci[true_idx].nlt_info;
          if (nlt == NULL)
            { 
              if (descriptors != NULL)
                descriptors[c] = -1;
              continue;
            }
          have_nlt = true;
          if (descriptors == NULL)
            return true; // No need to go further
          descriptors[c] = ((nlt->nlt_type & 15) | (nlt->c_num << 5) |
                            ((nlt->t_num >= 0)?16:0)) & KDU_INT32_MAX;
        }
    }
  else
    { 
      kd_tile_comp *tc = tp->comps;
      valid_comps = cs->num_apparent_components;
      if (valid_comps > num_comps)
        valid_comps = num_comps;
      for (c=0; c < valid_comps; c++)
        { 
          int true_idx = (int)(cs->comp_info[c].from_apparent - cs->comp_info);
          kd_nlt_info *nlt = tc[true_idx].nlt_info;
          if (nlt == NULL)
            { 
              if (descriptors != NULL)
                descriptors[c] = -1;
              continue;
            }
          have_nlt = true;
          if (descriptors == NULL)
            return true; // No need to go further
          descriptors[c] = ((nlt->nlt_type & 15) | (nlt->c_num << 5) |
                            ((nlt->t_num >= 0)?16:0)) & KDU_INT32_MAX;
        }
    }
  if (!have_nlt)
    return false;
  
  // Go back and figure out uniqueness
  assert(descriptors != NULL); // Else we would have returned above
  for (c=valid_comps-1; c >= 0; c--)
    { 
      int k, code = descriptors[c];
      if (code < 0)
        continue;
      for (k=0; descriptors[k] != code; k++);
      assert(k <= c);
      descriptors[c] = (code & 15) | (k << 4); // Identify idx of 1st match
    }
  
  for (c=valid_comps; c < num_comps; c++)
    descriptors[c] = -1; // Fill in any extra descriptors with -1 = no-NLT
  return true;
}

/*****************************************************************************/
/*                          kdu_tile::make_nlt_table                         */
/*****************************************************************************/

bool
  kdu_tile::make_nlt_table(int comp_idx, bool for_analysis,
                           float &dmin, float &dmax,
                           int num_entries, float *lut,
                           float nominal_range_in,
                           float nominal_range_out)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::set_components_of_interest");
    }
  kd_codestream *cs = tp->codestream;
  if (cs->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return false;
  kd_nlt_info *nlt=NULL;
  if (tp->mct_tail != NULL)
    { 
      if (comp_idx < cs->num_apparent_output_components)
        { 
          comp_idx = cs->output_comp_info[comp_idx].from_apparent;
          nlt = tp->mct_tail->output_comp_info[comp_idx].nlt_info;
        }
    }
  else if (comp_idx < cs->num_apparent_components)
    { 
      comp_idx = (int)(cs->comp_info[comp_idx].from_apparent - cs->comp_info);
      nlt = tp->comps[comp_idx].nlt_info;
    }
  if ((nlt == NULL) || (num_entries < 2))
    return false;
  
  int in_precision, out_precision;
  bool in_signed, out_signed;
  if (!for_analysis)
    { 
      in_precision = cs->output_comp_info[comp_idx].in_precision;
      in_signed = cs->output_comp_info[comp_idx].in_signed;
      out_precision = cs->output_comp_info[comp_idx].out_precision;
      out_signed = cs->output_comp_info[comp_idx].out_signed;
    }
  else
    { 
      in_precision = cs->output_comp_info[comp_idx].out_precision;
      in_signed = cs->output_comp_info[comp_idx].out_signed;
      out_precision = cs->output_comp_info[comp_idx].in_precision;
      out_signed = cs->output_comp_info[comp_idx].in_signed;
    }
    
  int n;
  if ((nlt->nlt_type == NLType_GAMMA) || (nlt->nlt_type == NLType_NONE))
    { // These both have full-range inputs and outputs
  
      // Start by generating a normalized mapping in the LUT where signed and
      // unsigned values are all mapped to the range [-1,1], ensuring that 0
      // always means true 0.  Following the prescription for gamma mappings
      // (which we also adopt for the NONE mapping), signed values are mapped
      // to/from this range by dividing the original intergers by 2^{1-P},
      // where P is the precision, so an input value of 1 cannot quite be
      // achieved, but we will not unnecessarily clip to the achievable range
      // here.  Meanwhile, unsigned values are mapped to/from the [-1,1] range
      // by mapping 0 to 0 and (2^P-1) to 1.
      
      // First build the normalized inputs
      float y, y_gap, y_min, y_max;
      if (in_signed)
        { // Map inputs to the interval [-1,1], pinning 0 and min integer in
          y_min = -1.0f;  y_max = 1.0f;
          dmin = -0.5f*nominal_range_in;
          dmax = 0.5f*nominal_range_in;
        }
      else
        { // Map inputs to the interval [0,1], pinning 0 and max integer in
          y_min = 0.0f;  y_max = 1.0f - powf(2.0f,(float)(-in_precision));
          dmin = -0.5f*nominal_range_in;
          dmax = 0.5f*y_max*nominal_range_in;
        }
      y_gap = (y_max - y_min) / (num_entries-1);
      for (y=y_min, n=0; n < num_entries; n++, y+=y_gap)
        lut[n] = y;

      if (nlt->nlt_type == NLType_GAMMA)
        { 
          if (for_analysis)
            nlt_params::apply_fwd_gamma(lut,num_entries,nlt->gamma);
          else
            nlt_params::apply_rev_gamma(lut,num_entries,nlt->gamma);
        }

      // Finish by mapping normalized outputs back to the right range
      if (out_signed)
        { // Stretch values in the interval [-1,1] out to [-range/2,range/2],
          // pinning 0 and the minimum integer output.
          float scale = 0.5f * nominal_range_out;
          for (n=0; n < num_entries; n++)
            lut[n] *= scale;
        }
      else
        { // Stretch values in the interval [0,1] out to [-range/2,range/2],
          // pinning 0 to -range/2 and 1 to the maximum achievable integer out.
          float scale = nominal_range_out - powf(2.0f,(float)(-out_precision));
          float offset = -0.5f*nominal_range_out;
          for (n=0; n < num_entries; n++)
            { // Stretch values in the interval [0,1] out to a scaled and
              // level shifted version of [0,2^P-1].
              float val = lut[n];
              if (val < 0.0f) val = 0.0f;
              lut[n] = val*scale + offset;
            }
        }
      
      return true;
    }
  else if ((nlt->nlt_type == NLType_LUT) && !for_analysis)
    { // In this case, the lookup table input may range over a reduced domain
      // from the one used above.  We need to take care setting dmin and dmax
      // appropriately.
      assert((nlt->dmin >= 0.0f) && (nlt->dmax <= 1.0f));
      float in_range = (1.0f-powf(2.0f,(float)(-in_precision))) *
        nominal_range_in;
      dmin = nlt->dmin * in_range - 0.5f*nominal_range_in;
      dmax = nlt->dmax * in_range - 0.5f*nominal_range_in;
      float out_scale = (1.0f-powf(2.0f,(float)(-out_precision))) *
        nominal_range_out;
      float out_off = -0.5f * nominal_range_out;
      int b, b_max = nlt->num_lut_entries-1;
      float x, delta_x = ((float) b_max) / ((float)(num_entries-1));
      for (x=0.0f, b=0, n=0; n < num_entries; n++, x+=delta_x)
        { 
          while (x > 1.0f)
            { x -= 1.0f; b++; }
          float val;
          if (b >= b_max)
            val = nlt->lut[b_max];
          else
            val = nlt->lut[b] + x * (nlt->lut[b+1] - nlt->lut[b]);
          if (val < 0.0f) val = 0.0f; // Just in case; should never happen
          if (val > 1.0f) val = 1.0f; // Just in case; should never happen
          lut[n] = val * out_scale + out_off;
        }
      return true;
    }
  else if ((nlt->nlt_type == NLType_LUT) && for_analysis)
    { // This is the reverse of the above; it is a little tricker, since we
      // may need to search a non-monotonic LUT.  Also, it is helpful to
      // confine the input domain to the range of outputs that the LUT can
      // actually generate.
      float lut_val, lut_min=nlt->lut[0], lut_max=nlt->lut[0];
      int b, b_min=0, b_max=0; // LUT indices of the min/max values
      for (b=1; b < nlt->num_lut_entries; b++)
        { 
          lut_val = nlt->lut[b];
          if (lut_val < lut_min)
            { lut_min = lut_val;  b_min = b; }
          if (lut_val > lut_max)
            { lut_max = lut_val;  b_max = b; }
        }
      if (lut_min < 0.0f) lut_min = 0.0f;
      if (lut_max > 1.0f) lut_max = 1.0f;
      float in_scale = (1.0f-powf(2.0f,(float)(-in_precision))) *
        nominal_range_in;
      float in_off = -0.5f * nominal_range_in;
      dmin = lut_min * in_scale + in_off;
      dmax = lut_max * in_scale + in_off;

      // Figure out the mapping from nlt->lut idx in [0,nlt->num_lut_entires-1]
      // to output values.  This mapping needs to take into account the
      // relative domain bounds of the original mapping being inverted, as
      // found in nlt->dmin and nlt->dmax.
      assert((nlt->dmin >= 0.0f) && (nlt->dmax <= 1.0f));
      float out_range = (1.0f-powf(2.0f,(float)(-out_precision))) *
        nominal_range_out;
      float out_min = nlt->dmin * out_range - 0.5f*nominal_range_out;
      float out_max = nlt->dmax * out_range - 0.5f*nominal_range_out;
      float out_scale = (out_max - out_min) / (nlt->num_lut_entries - 1);
           // Mapping is:  out = idx*out_scale + out_min

      // Now build the lut whose indices span the lut_min to lut_max range
      // of normalized input values.
      float z=lut_min, z_delta=(lut_max-lut_min)/(num_entries-1);
      for (n=0; n < num_entries; n++, z+=z_delta)
        { 
          // First find z within the original lookup table and work out the
          // corresponding normalized input y.  Since z is increasing, we
          // will progressively increase b_min so as to encourage a continuous
          // mapping, in case the LUT is not one-to-one.
          int b_top = b_max; // Temporary upper bound
          float lut_top = lut_max;
          while ((b = (b_min + b_top)>>1) > b_min)
            { 
              lut_val = nlt->lut[b];
              if (lut_val <= z)
                { b_min = b;  lut_min = lut_val; }
              else
                { b_top = b;  lut_top = lut_val; }
            }
          assert((b_top == (b_min+1)) || (b_top == b_min));
          float y=(float) b;
          if (lut_top > lut_min)
            y += (z-lut_min) / (lut_top - lut_min);
          lut[n] = y * out_scale + out_min;
        }

      return true;
    }
  else
    return false;
}

/*****************************************************************************/
/*                     kdu_tile::set_components_of_interest                  */
/*****************************************************************************/

void
  kdu_tile::set_components_of_interest(int num_components_of_interest,
                                       const int *components_of_interest)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::set_components_of_interest");
    }
  if ((tp->codestream->component_access_mode == KDU_WANT_OUTPUT_COMPONENTS) &&
      (tp->mct_head != NULL))
    tp->mct_tail->apply_output_restrictions(tp->codestream->output_comp_info,
                                            num_components_of_interest,
                                            components_of_interest);
  else
    { // Record the components which are of interest in the `is_of_interest'
      // members of the `kd_tile::comps' array.
      kd_codestream *cs = tp->codestream;
      kd_tile_comp *comps = tp->comps;
      int n, num_output_comps;
      if (cs->component_access_mode == KDU_WANT_CODESTREAM_COMPONENTS)
        num_output_comps = cs->num_apparent_components;
      else
        num_output_comps = cs->num_apparent_output_components;
      if (num_components_of_interest == 0)
        { // All apparent components are of interest
          for (n=0; n < num_output_comps; n++)
            comps[n].is_of_interest = true;
        }
      else if (components_of_interest == NULL)
        { // First `num_components_of_interest' components are of interest
          for (n=0; n < num_output_comps; n++)
            comps[n].is_of_interest = (n < num_components_of_interest);
        }
      else
        {
          for (n=0; n < num_output_comps; n++)
            comps[n].is_of_interest = false;
          for (n=0; n < num_components_of_interest; n++)
            {
              int idx = components_of_interest[n];
              if ((idx >= 0) && (idx < num_output_comps))
                comps[idx].is_of_interest = true;
            }
        }
    }

  for (int n=0; n < tp->num_components; n++)
    tp->comps[n].G_tc_restricted = -1.0F; // Have to regenerate if needed
}

/*****************************************************************************/
/*                        kdu_tile::get_mct_block_info                       */
/*****************************************************************************/

bool
  kdu_tile::get_mct_block_info(int stage_idx, int block_idx,
                               int &num_stage_inputs, int &num_stage_outputs,
                               int &num_block_inputs, int &num_block_outputs,
                               int *block_input_indices,
                               int *block_output_indices,
                               float *irrev_block_offsets,
                               int *rev_block_offsets,
                               int *stage_input_indices)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_mct_block_info");
    }
  
  int n, m, k;

  kd_codestream *cs = tp->codestream;
  bool want_codestream =
    (cs->component_access_mode==KDU_WANT_CODESTREAM_COMPONENTS);
  if (want_codestream || (tp->mct_head == NULL))
    { // Report the existence of one dummy transform stage
      if ((stage_idx != 0) || (block_idx != 0))
        return false;
      if (want_codestream)
        num_stage_outputs = cs->num_apparent_components;
      else
        num_stage_outputs = cs->num_apparent_output_components;
      num_stage_inputs = num_block_inputs =
        num_block_outputs = num_stage_outputs;

      bool need_ycc = get_ycc();
               // If this is true and `want_codestream', all of the YCC
               // components are necessarily of interest.
      if (need_ycc && !want_codestream)
        {
          for (n=0; n < 3; n++)
            if (((k=cs->output_comp_info[n].apparent_idx) >= 0) &&
                tp->comps[k].is_of_interest)
              break;
          if (n == 3)
            need_ycc = false; // None of the first 3 codestream components are
                              // of interest
        }
      int ycc_perm[3] = {-1,-1,-1};
      if (need_ycc)
        { // Find the apparent indices of the components which are involved in
          // the YCC transform.
          for (n=0; n < 3; n++)
            {
              ycc_perm[n] = tp->codestream->comp_info[n].apparent_idx;
              assert(ycc_perm[n] >= 0);
              if ((!want_codestream) &&
                  (((k=cs->output_comp_info[n].apparent_idx) < 0) ||
                   !tp->comps[k].is_of_interest))
                num_stage_inputs++; // Need more codestream components than
                                    // output components
            }
        }

      // See if there are any stage outputs which are not of interest.
      int span_stage_inputs = num_stage_inputs; // Remember this value
      for (n=0; n < num_stage_outputs; n++)
        if (!tp->comps[n].is_of_interest)
          {
            num_stage_inputs--;
            num_block_inputs--;
            num_block_outputs--;
              // Note that if the YCC transform is to be used, all of its
              // components are necessarily of interest.
          }

      if (block_output_indices != NULL)
        {
          for (n=k=0; n < num_stage_outputs; n++)
            if (tp->comps[n].is_of_interest)
              block_output_indices[k++] = n;
          assert(k == num_block_outputs);
        }

      if ((irrev_block_offsets != NULL) || (rev_block_offsets != NULL))
        {
          for (n=k=0; n < num_stage_outputs; n++)
            {
              if (!tp->comps[n].is_of_interest)
                continue;
              kd_comp_info *ci = NULL;
              if (want_codestream)
                ci = cs->comp_info[n].from_apparent;
              else
                {
                  m = cs->output_comp_info[n].from_apparent;
                  ci = cs->output_comp_info[m].subsampling_ref;
                }
              int off = (ci->is_signed)?0:(1<<(ci->precision-1));
              if (rev_block_offsets != NULL)
                rev_block_offsets[k] = off;
              if (irrev_block_offsets != NULL)
                irrev_block_offsets[k] = (float) off;
              k++;
            }
          assert(k == num_block_outputs);
        }

      if ((stage_input_indices == NULL) && (block_input_indices == NULL))
        return true;

      if (want_codestream)
        { // Codestream indices may be permuted; arrange for the first three
          // entries of the `stage_input_indices' array to reference the
          // first 3 original components (via their modified indices if
          // necessary), and for `block_input_indices' to undo any permutation
          // required to make this happen.
          int non_ycc_idx = (need_ycc)?3:0;
          for (n=m=k=0; n < span_stage_inputs; n++)
            {
              if (n == ycc_perm[0])
                m = 0;
              else if (n == ycc_perm[1])
                m = 1;
              else if (n == ycc_perm[2])
                m = 2;
              else
                {
                  if (!tp->comps[n].is_of_interest)
                    continue;
                  m = non_ycc_idx++;
                }
              if (stage_input_indices != NULL)
                stage_input_indices[m] = n;
              if (block_input_indices != NULL)
                block_input_indices[k] = m;
              k++;
            }
          assert(k == num_block_inputs);
        }
      else
        { // In this case, the codestream components are in order already, but
          // if there is a YCC transform, it is possible that only some of
          // its outputs are used.
          if (need_ycc && (stage_input_indices != NULL))
            for (n=0; n < 3; n++)
              stage_input_indices[n] = n;
          int si_idx, non_ycc_idx = (need_ycc)?3:0;
          for (n=k=0; n < num_stage_outputs; n++)
            {
              if (!tp->comps[n].is_of_interest)
                continue;
              m = cs->output_comp_info[n].from_apparent;
                 // m holds the true codestream input index
              if ((need_ycc) && (m < 3))
                si_idx = m;
              else
                si_idx = non_ycc_idx++;

              if (stage_input_indices != NULL)
                stage_input_indices[si_idx] = m;
              if (block_input_indices != NULL)
                block_input_indices[k] = si_idx;

              k++;
            }
          assert(non_ycc_idx == num_stage_inputs);
          assert(k == num_block_inputs);
        }

      return true;
    }

  // If we get here, we have a `kd_mct_stage' list from which to derive the
  // requested information.
  kd_mct_stage *stage = tp->mct_head;
  for (; (stage_idx > 0) && (stage != NULL); stage_idx--)
    stage = stage->next_stage;
  if (stage == NULL)
    return false;
  num_stage_inputs = stage->num_required_inputs;
  num_stage_outputs = stage->num_apparent_outputs;
  if (stage_input_indices != NULL)
    {
      assert(stage_idx == 0);
      for (n=m=0; (n<stage->num_inputs) && (m<stage->num_required_inputs); n++)
        if (stage->input_required_indices[n] >= 0)
          {
            assert(stage->input_required_indices[n] == m);
            stage_input_indices[m++] = n;
          }
      assert(m == stage->num_required_inputs);
    }
  if (stage->num_blocks <= block_idx)
    return false;

  int b;
  kd_mct_block *block = stage->blocks;
  for (b=0; b < stage->num_blocks; b++, block++)
    if (block->num_apparent_outputs > 0)
      {
        if (block_idx == 0)
          break;
        block_idx--;
      }
  if (b == stage->num_blocks)
    return false;
  num_block_inputs = block->num_required_inputs;
  num_block_outputs = block->num_apparent_outputs;
  if (block_input_indices != NULL)
    {
      for (n=m=0; (n<block->num_inputs) && (m<block->num_required_inputs); n++)
        if (block->inputs_required[n])
          {
            int idx = block->input_indices[n];
            assert(stage->input_required_indices[idx] >= 0);
            block_input_indices[m++] = stage->input_required_indices[idx];
            /*
            if (stage_idx == 0)
              idx = cs->comp_info[idx].apparent_idx;
            else
              idx = stage->prev_stage->output_comp_info[idx].apparent_idx;
            block_input_indices[m++] = idx;
            */
          }
      assert(m == block->num_required_inputs);
    }
  if (block_output_indices != NULL)
    {
      for (n=m=0; (n<block->num_outputs)&&(m<block->num_apparent_outputs); n++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[n];
          if (oci->is_of_interest)
            block_output_indices[m++] = oci->apparent_idx;
        }
      assert(m == block->num_apparent_outputs);
    }

  if ((block->offset_params == NULL) || (block->triang_params != NULL))
    { // Note that dependency transforms always report having 0 offsets here,
      // since their offsets have a different interpretation and should be
      // returned via the `get_mct_dependency_info' function.
      if (irrev_block_offsets != NULL)
        for (n=0; n < block->num_apparent_outputs; n++)
          irrev_block_offsets[n] = 0.0F;
      if (rev_block_offsets != NULL)
        for (n=0; n < block->num_apparent_outputs; n++)
          rev_block_offsets[n] = 0;
    }
  else if ((irrev_block_offsets != NULL) || (rev_block_offsets != NULL))
    {
      for (n=m=0; (n<block->num_outputs)&&(m<block->num_apparent_outputs); n++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[n];
          if (oci->is_of_interest)
            {
              float coeff=0.0F;
              block->offset_params->get(Mvector_coeffs,n,0,coeff);
              if (irrev_block_offsets != NULL)
                irrev_block_offsets[m] = coeff;
              if (rev_block_offsets != NULL)
                rev_block_offsets[m] = (int) floor(coeff + 0.5);
              m++;
            }
        }
      assert(m == block->num_apparent_outputs);
    }
  return true;
}

/*****************************************************************************/
/*                       kdu_tile::get_mct_matrix_info                       */
/*****************************************************************************/

bool
  kdu_tile::get_mct_matrix_info(int stage_idx, int block_idx,
                                float *coefficients)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_mct_matrix_info");
    }

  
  if (tp->codestream->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return false;
  kd_mct_stage *stage = tp->mct_head;
  for (; (stage_idx > 0) && (stage != NULL); stage_idx--)
    stage = stage->next_stage;
  if (stage == NULL)
    return false;
  if (stage->num_blocks <= block_idx)
    return false;

  int b;
  kd_mct_block *block = stage->blocks;
  for (b=0; b < stage->num_blocks; b++, block++)
    if (block->num_apparent_outputs > 0)
      {
        if (block_idx == 0)
          break;
        block_idx--;
      }
  if (b == stage->num_blocks)
    return false;

  if ((block->matrix_params == NULL) ||
      block->is_null_transform || block->is_reversible)
    return false;

  if (coefficients != NULL)
    {
      int m, n, c_in=0, c_out=0;
      for (m=0; m < block->num_outputs; m++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[m];
          if (!oci->is_of_interest)
            { // Skip this row
              c_in += block->num_inputs;
              continue;
            }
          for (n=0; n < block->num_inputs; n++, c_in++)
            {
              if (!block->inputs_required[n])
                continue; // Skip this column
              float coeff=0.0F;
              block->matrix_params->get(Mmatrix_coeffs,c_in,0,coeff);
              coefficients[c_out++] = coeff;
            }
        }
      assert(c_out==(block->num_apparent_outputs*block->num_required_inputs));
    }
  return true;
}

/*****************************************************************************/
/*                       kdu_tile::get_mct_rxform_info                       */
/*****************************************************************************/

bool
  kdu_tile::get_mct_rxform_info(int stage_idx, int block_idx,
                                int *coefficients, int *active_outputs)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_mct_rxform_info");
    }
 
  if (tp->codestream->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return false;
  kd_mct_stage *stage = tp->mct_head;
  for (; (stage_idx > 0) && (stage != NULL); stage_idx--)
    stage = stage->next_stage;
  if (stage == NULL)
    return false;
  if (stage->num_blocks <= block_idx)
    return false;

  int b;
  kd_mct_block *block = stage->blocks;
  for (b=0; b < stage->num_blocks; b++, block++)
    if (block->num_apparent_outputs > 0)
      {
        if (block_idx == 0)
          break;
        block_idx--;
      }
  if (b == stage->num_blocks)
    return false;

  if (((block->matrix_params == NULL) && (block->old_mat_params == NULL)) ||
      block->is_null_transform || !block->is_reversible)
    return false;
  assert(block->num_required_inputs == block->num_inputs);

  int n, m;
  if (coefficients != NULL)
    {
      int N = block->num_required_inputs;
      if (block->old_mat_params != NULL)
        {
          for (m=0; m < N; m++)
            for (n=0; n <= N; n++)
              {
                float coeff = 0.0F;
                block->old_mat_params->get(Mmatrix_coeffs,m*(N+1)+n,0,coeff);
                coefficients[m*(N+1)+n] = (int) floor(coeff + 0.5);
              }
        }
      else
        {
          for (m=0; m < N; m++)
            for (n=0; n <= N; n++)
              {
                float coeff = 0.0F;
                block->matrix_params->get(Mmatrix_coeffs,n*N+m,0,coeff);
                coefficients[m*(N+1)+n] = (int) floor(coeff + 0.5);
              }
        }
    }
  if (active_outputs != NULL)
    {
      for (n=m=0; (n<block->num_outputs)&&(m<block->num_apparent_outputs); n++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[n];
          if (oci->is_of_interest)
            active_outputs[m++] = n;
        }
      assert(m == block->num_apparent_outputs);
    }

  return true;
}

/*****************************************************************************/
/*                     kdu_tile::get_mct_dependency_info                     */
/*****************************************************************************/

bool
  kdu_tile::get_mct_dependency_info(int stage_idx, int block_idx,
                                    bool &is_reversible,
                                    float *irrev_coefficients,
                                    float *irrev_offsets,
                                    int *rev_coefficients,
                                    int *rev_offsets,
                                    int *active_outputs)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_dependency_info");
    }
  
  if (tp->codestream->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return false;
  kd_mct_stage *stage = tp->mct_head;
  for (; (stage_idx > 0) && (stage != NULL); stage_idx--)
    stage = stage->next_stage;
  if (stage == NULL)
    return false;
  if (stage->num_blocks <= block_idx)
    return false;

  int b;
  kd_mct_block *block = stage->blocks;
  for (b=0; b < stage->num_blocks; b++, block++)
    if (block->num_apparent_outputs > 0)
      {
        if (block_idx == 0)
          break;
        block_idx--;
      }
  if (b == stage->num_blocks)
    return false;

  if ((block->triang_params == NULL) || block->is_null_transform)
    return false;

  int n, m;
  is_reversible = block->is_reversible;
  if (block->is_reversible)
    {
      assert((irrev_coefficients == NULL) && (irrev_offsets == NULL));
      if (rev_coefficients != NULL)
        {
          int num_coeffs =
            (block->num_required_inputs*(block->num_required_inputs+1))/2 - 1;
          for (n=0; n < num_coeffs; n++)
            {
              float coeff=0.0F;
              block->triang_params->get(Mtriang_coeffs,n,0,coeff);
              rev_coefficients[n] = (int) floor(coeff + 0.5);
            }
        }
      if (rev_offsets != NULL)
        for (n=0; n < block->num_required_inputs; n++)
          {
            float off=0.0F;
            block->offset_params->get(Mvector_coeffs,n,0,off);
            rev_offsets[n] = (int) floor(off + 0.5);
          }
    }
  else
    {
      assert((rev_coefficients == NULL) && (rev_offsets == NULL));
      if (irrev_coefficients != NULL)
        {
          int num_coeffs =
            (block->num_required_inputs*(block->num_required_inputs-1))/2;
          for (n=0; n < num_coeffs; n++)
            {
              float coeff=0.0F;
              block->triang_params->get(Mtriang_coeffs,n,0,coeff);
              irrev_coefficients[n] = coeff;
            }
        }
      if (irrev_offsets != NULL)
        for (n=0; n < block->num_required_inputs; n++)
          {
            float off=0.0F;
            block->offset_params->get(Mvector_coeffs,n,0,off);
            irrev_offsets[n] = off;
          }
    }

  if (active_outputs != NULL)
    {
      for (n=m=0; (n<block->num_outputs)&&(m<block->num_apparent_outputs); n++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[n];
          if (oci->is_of_interest)
            active_outputs[m++] = n;
        }
      assert(m == block->num_apparent_outputs);
    }

  return true;
}

/*****************************************************************************/
/*                        kdu_tile::get_mct_dwt_info                         */
/*****************************************************************************/

const kdu_kernel_step_info *
  kdu_tile::get_mct_dwt_info(int stage_idx, int block_idx,
                             bool &is_reversible, int &num_levels,
                             int &canvas_min, int &canvas_lim,
                             int &num_steps, bool &symmetric,
                             bool &symmetric_extension,
                             const float * &coefficients,
                             int *active_inputs, int *active_outputs)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_mct_dwt_info");
    }
  
  if (tp->codestream->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return NULL;
  kd_mct_stage *stage = tp->mct_head;
  for (; (stage_idx > 0) && (stage != NULL); stage_idx--)
    stage = stage->next_stage;
  if (stage == NULL)
    return NULL;
  if (stage->num_blocks <= block_idx)
    return NULL;

  int b;
  kd_mct_block *block = stage->blocks;
  for (b=0; b < stage->num_blocks; b++, block++)
    if (block->num_apparent_outputs > 0)
      {
        if (block_idx == 0)
          break;
        block_idx--;
      }
  if (b == stage->num_blocks)
    return NULL;

  if ((block->dwt_step_info == NULL) || (block->dwt_num_levels < 1) ||
      block->is_null_transform)
    return NULL;
  is_reversible = block->is_reversible;
  num_levels = block->dwt_num_levels;
  canvas_min = block->dwt_canvas_origin;
  canvas_lim = canvas_min + block->num_inputs;
  num_steps = block->dwt_num_steps;
  symmetric = block->dwt_symmetric;
  symmetric_extension = block->dwt_symmetric_extension;
  coefficients = block->dwt_coefficients;

  int m, n;
  if (active_inputs != NULL)
    {
      for (n=m=0; (n<block->num_inputs) && (m<block->num_required_inputs); n++)
        if (block->inputs_required[n])
          active_inputs[m++] = n;
      assert(m == block->num_required_inputs);
    }
  if (active_outputs != NULL)
    {
      for (n=m=0; (n<block->num_outputs)&&(m<block->num_apparent_outputs); n++)
        {
          kd_output_comp_info *oci =
            stage->output_comp_info + block->output_indices[n];
          if (oci->is_of_interest)
            active_outputs[m++] = n;
        }
      assert(m == block->num_apparent_outputs);
    }

  return block->dwt_step_info;
}

/*****************************************************************************/
/*                        kdu_tile::get_num_components                       */
/*****************************************************************************/

int
  kdu_tile::get_num_components()
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_num_components");
    }
  return tp->codestream->num_apparent_components;
}

/*****************************************************************************/
/*                          kdu_tile::get_num_layers                         */
/*****************************************************************************/

int
  kdu_tile::get_num_layers()
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_num_layers");
    }

  return tp->num_apparent_layers;
}

/*****************************************************************************/
/*                   kdu_tile::parse_all_relevant_packets                    */
/*****************************************************************************/

bool
  kdu_tile::parse_all_relevant_packets(bool start_from_scratch_if_possible,
                                       kdu_thread_env *env)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      return false;
    }
  if (!tp->is_open)
    return false;
  kd_codestream *codestream = tp->codestream;
  if (codestream->in == NULL)
    return false;
  
  int r, c;
  if (env != NULL)
    codestream->acquire_lock(KD_THREADLOCK_GENERAL,env);
  
  if (start_from_scratch_if_possible)
    { // We may need to unload a lot of stuff.  Need to check first whether
      // this is necessary and also whether it is possible.
      bool need_unload = false;
      bool all_addressable = true;
      bool any_locked = false;
      for (c=0; c < tp->num_components; c++)
        {
          kd_tile_comp *comp = tp->comps + c;
          if (!comp->enabled)
            continue;
          for (r=0; r <= comp->apparent_dwt_levels; r++)
            {
              kd_resolution *res = comp->resolutions + r;
              kd_precinct_ref *ref = res->precinct_refs;
              kdu_coords idx, min, lim;
              min = res->region_indices.pos - res->precinct_indices.pos;
              lim = min + res->region_indices.size;
              for (idx.y=0; idx.y < res->precinct_indices.size.y; idx.y++)
                for (idx.x=0; idx.x < res->precinct_indices.size.x;
                     idx.x++, ref++)
                  {
                    if (ref->parsed_and_unloaded())
                      need_unload = true;
                    else
                      {
                        kd_precinct *precinct = ref->deref();
                        if (precinct == NULL)
                          continue;
                        if (((precinct->flags & KD_PFLAG_WAS_READ) ||
                             (precinct->num_packets_read > 0)) &&
                            ((idx.x < min.x) || (idx.x >= lim.x) ||
                             (idx.y < min.y) || (idx.y >= lim.y)))
                          need_unload = true;
                        if (!(precinct->flags & KD_PFLAG_ADDRESSABLE))
                          all_addressable = false;
                        if (precinct->flags & KD_PFLAG_LOADED_LOCKED)
                          any_locked = true;
                      }
                  }
            }
        }
      
      if (need_unload)
        {
          if ((!all_addressable) || ((env != NULL) && any_locked))
            return false; // Impossible or too dangerous to unload precinct's
                          // contents here.
          for (c=0; c < tp->num_components; c++)
            {
              kd_tile_comp *comp = tp->comps + c;
              if (!comp->enabled)
                continue;
              kdu_long *stats = comp->layer_stats;
              for (r=0; r <= comp->apparent_dwt_levels; r++)
                {
                  for (int n=tp->num_layers; n > 0; n--, stats+=2)
                    stats[0] = stats[1] = 0;
                  kd_resolution *res = comp->resolutions + r;
                  kd_precinct_ref *ref = res->precinct_refs;
                  kdu_coords idx;
                  for (idx.y=0; idx.y < res->precinct_indices.size.y; idx.y++)
                    for (idx.x=0; idx.x<res->precinct_indices.size.x; idx.x++)
                      (ref++)->close_and_reset();
                }
            }
        }
    }
  
  for (c=0; c < tp->num_components; c++)
    {
      kd_tile_comp *comp = tp->comps + c;
      if (!comp->enabled)
        continue;
      for (r=0; r <= comp->apparent_dwt_levels; r++)
        {
          kd_resolution *res = comp->resolutions + r;
          kdu_coords idx;
          kdu_coords idx_offset =
            res->region_indices.pos-res->precinct_indices.pos;
          for (idx.y=0; idx.y < res->region_indices.size.y; idx.y++)
            for (idx.x=0; idx.x < res->region_indices.size.x; idx.x++)
              { // Note: we are in the "glock" context here
                kdu_coords pos_idx = idx+idx_offset;
                int p_num = pos_idx.x+pos_idx.y*res->precinct_indices.size.x;
                kd_precinct_ref *ref = res->precinct_refs+p_num;
                kd_precinct *precinct = ref->open(res,pos_idx,true,NULL);
                if (!(precinct->flags & KD_PFLAG_LOADED_LOCKED))
                  precinct->load_required_packets();
              }
        }
    }
  if (env != NULL)
    codestream->release_lock(KD_THREADLOCK_GENERAL,env);
  return true;
}

/*****************************************************************************/
/*                     kdu_tile::get_parsed_packet_stats                     */
/*****************************************************************************/

kdu_long
  kdu_tile::get_parsed_packet_stats(int component_idx, int discard_levels,
                                    int num_layers, kdu_long *layer_bytes,
                                    kdu_long *layer_packets)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::get_parsed_packet_stats");
    }

  if ((tp->codestream->in == NULL) || (num_layers < 1))
    return 0;

  if (discard_levels < 0)
    discard_levels = 0;
  int c=component_idx, lim_comp_idx=component_idx+1;
  if (component_idx < 0)
    { c = 0;  lim_comp_idx = tp->num_components; }
  kdu_long max_packets = 0;
  int tile_layers = tp->num_layers;
  int xfer_layers = (tile_layers < num_layers)?tile_layers:num_layers;
  for (; c < lim_comp_idx; c++)
    {
      kd_tile_comp *comp = tp->comps + c;
      if (comp->layer_stats == NULL)
        continue;
      int n, r, lim_res_idx = comp->dwt_levels+1-discard_levels;
      kdu_long *stats = comp->layer_stats;
      for (r=0; r < lim_res_idx; r++, stats += (tile_layers<<1))
        {
          max_packets += comp->resolutions[r].precinct_indices.area();
          if (layer_bytes != NULL)
            for (n=0; n < xfer_layers; n++)
              layer_bytes[n] += stats[2*n+1];
          if (layer_packets != NULL)
            for (n=0; n < xfer_layers; n++)
              layer_packets[n] += stats[2*n];
        }
    }
  return max_packets;
}

/*****************************************************************************/
/*                         kdu_tile::access_component                        */
/*****************************************************************************/

kdu_tile_comp
  kdu_tile::access_component(int comp_idx)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::access_component");
    }

  if ((comp_idx < 0) ||
      (comp_idx >= tp->codestream->num_apparent_components))
    return kdu_tile_comp(NULL); // Return an empty interface.
  int true_idx = (int)(tp->codestream->comp_info[comp_idx].from_apparent -
                       tp->codestream->comp_info);
  assert((true_idx >= 0) && (true_idx < tp->num_components));
  if (!tp->comps[true_idx].enabled)
    return kdu_tile_comp(NULL); // Return an empty interface
  return kdu_tile_comp(tp->comps + true_idx);
}

/*****************************************************************************/
/*                     kdu_tile::find_component_gain_info                    */
/*****************************************************************************/

float
  kdu_tile::find_component_gain_info(int comp_idx, bool restrict_to_interest)
{
  kd_tile *tp=NULL;
  if ((state == NULL) || (!(state->flags.get() & KD_TREF_FLAGS_OPEN)) ||
      ((tp=state->tile) == NULL) || (tp == KD_EXPIRED_TILE))
    { 
      state = NULL;
      gen_tile_interface_invalid_error("kdu_tile::find_component_gain_info");
    }

  if ((comp_idx < 0) || (comp_idx >= tp->num_components))
    return 0.0F;
  if (tp->codestream->component_access_mode != KDU_WANT_OUTPUT_COMPONENTS)
    return 1.0F;
  if (restrict_to_interest)
    {
      if (tp->comps[comp_idx].G_tc_restricted < 0.0F)
        tp->comps[comp_idx].G_tc_restricted =
          tp->find_multicomponent_energy_gain(comp_idx,true,false);
      assert(tp->comps[comp_idx].G_tc_restricted > 0.0F);
      return tp->comps[comp_idx].G_tc_restricted;
    }
  else
    {
      if (tp->comps[comp_idx].G_tc < 0.0F)
        tp->comps[comp_idx].G_tc =
          tp->find_multicomponent_energy_gain(comp_idx,false,false);
      assert(tp->comps[comp_idx].G_tc > 0.0F);
      return tp->comps[comp_idx].G_tc;
    }
}


/* ========================================================================= */
/*                                 kd_tile_comp                              */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_tile_comp::~kd_tile_comp                         */
/*****************************************************************************/

kd_tile_comp::~kd_tile_comp()
{
  if (nlt_info != NULL)
    nlt_info->release();
  if (kernel_step_info != NULL)
    delete[] kernel_step_info; // Also deletes storage for flipped step info
  if ((kernel_step_info_flipped != NULL) &&
      (kernel_step_info_flipped != kernel_step_info))
    delete[] kernel_step_info_flipped;
  if (kernel_coefficients != NULL)
    delete[] kernel_coefficients; // Also deletes storage for flipped coeffs
  if ((kernel_coefficients_flipped != NULL) &&
      (kernel_coefficients_flipped != kernel_coefficients))
    delete[] kernel_coefficients_flipped;
  if (resolutions != NULL)
    delete[] resolutions;
  if (layer_stats != NULL)
    delete[] layer_stats;
}

/*****************************************************************************/
/*               kd_tile_comp::initialize_kernel_parameters                  */
/*****************************************************************************/

void
  kd_tile_comp::initialize_kernel_parameters(int atk_idx, kdu_kernels &kernels)
{
  bool kernel_rev = reversible;
  kd_create_dwt_description(kernel_id,atk_idx,codestream->siz,tile->t_num,
                            kernel_rev,kernel_symmetric,
                            kernel_symmetric_extension,kernel_num_steps,
                            kernel_step_info,kernel_coefficients);
  assert(reversible == kernel_rev);
  if (kernel_symmetric)
    {
      kernel_step_info_flipped = kernel_step_info;
      kernel_coefficients_flipped = kernel_coefficients;
    }
  else
    {
      int c, s, n;
      kernel_step_info_flipped = new kdu_kernel_step_info[kernel_num_steps];
      for (c=s=0; s < kernel_num_steps; s++)
        c += kernel_step_info[s].support_length;
      kernel_coefficients_flipped = new float[c];
      for (c=s=0; s < kernel_num_steps; s++)
        {
          kdu_kernel_step_info *sp = kernel_step_info + s;
          kdu_kernel_step_info *dp = kernel_step_info_flipped + s;
          int Ls = dp->support_length = sp->support_length;
          dp->support_min = -(sp->support_min + Ls - 1) + 1-2*(s&1);
          dp->downshift = sp->downshift;
          dp->rounding_offset = sp->rounding_offset;
          for (n=0; n < Ls; n++)
            kernel_coefficients_flipped[c+n] = kernel_coefficients[c+Ls-1-n];
          c += Ls;
        }
    }
  kernels.init(kernel_num_steps,kernel_step_info,
               kernel_coefficients,kernel_symmetric,
               kernel_symmetric_extension,reversible);
  int low_hlen, high_hlen;
  kernels.get_impulse_response(KDU_SYNTHESIS_LOW,low_hlen,
                               &low_support_min,&low_support_max);
  kernels.get_impulse_response(KDU_SYNTHESIS_HIGH,high_hlen,
                               &high_support_min,&high_support_max);
  assert((low_hlen >= low_support_max) &&
         (low_hlen >= -low_support_min) &&
         (high_hlen >= high_support_max) &&
         (high_hlen >= -high_support_min));
  int nsteps;
  kernels.get_lifting_factors(nsteps,kernel_low_scale,kernel_high_scale);
  assert(nsteps == kernel_num_steps);
}


/* ========================================================================= */
/*                                kdu_tile_comp                              */
/* ========================================================================= */

/*****************************************************************************/
/*                         kdu_tile_comp::get_reversible                     */
/*****************************************************************************/

bool
  kdu_tile_comp::get_reversible()
{
  return state->reversible;
}

/*****************************************************************************/
/*                        kdu_tile_comp::get_subsampling                     */
/*****************************************************************************/

void
  kdu_tile_comp::get_subsampling(kdu_coords &sub_sampling)
{
  sub_sampling = state->sub_sampling;
  int shift = state->dwt_levels - state->apparent_dwt_levels;
  sub_sampling.x <<= state->comp_info->hor_depth[shift];
  sub_sampling.y <<= state->comp_info->vert_depth[shift];
  if (state->codestream->transpose)
    sub_sampling.transpose();
}

/*****************************************************************************/
/*                          kdu_tile_comp::get_bit_depth                     */
/*****************************************************************************/

int
  kdu_tile_comp::get_bit_depth(bool internal)
{
  int bit_depth = state->comp_info->precision;
  if (internal)
    bit_depth += state->recommended_extra_bits;
  return bit_depth;
}

/*****************************************************************************/
/*                           kdu_tile_comp::get_signed                       */
/*****************************************************************************/

bool
  kdu_tile_comp::get_signed()
{
  return state->comp_info->is_signed;
}

/*****************************************************************************/
/*                      kdu_tile_comp::get_num_resolutions                   */
/*****************************************************************************/

int
  kdu_tile_comp::get_num_resolutions()
{
  if (state->apparent_dwt_levels < 0)
    return 0;
  return state->apparent_dwt_levels+1;
}

/*****************************************************************************/
/*                      kdu_tile_comp::access_resolution                     */
/*****************************************************************************/

kdu_resolution
  kdu_tile_comp::access_resolution(int res_level)
{
  if ((res_level < 0) || (res_level > state->apparent_dwt_levels))
    { KDU_ERROR_DEV(e,19); e <<
        KDU_TXT("Attempting to access a non-existent resolution level "
        "within some tile-component.  Problem almost certainly caused by "
        "trying to discard more resolution levels than the number of DWT "
        "levels used to compress a tile-component.");
    }
  kd_resolution *result = state->resolutions + res_level;
  if ((!result->can_flip) &&
      (state->codestream->vflip || state->codestream->hflip))
    { KDU_ERROR_DEV(e,0x17050500); e <<
        KDU_TXT("Attempting to access a resolution level within some "
                "tile-component, while the codestream is in a geometrically "
                "flipped viewing condition, where a packet wavelet "
                "transform has been found to be incompatible with flipping.  "
                "This condition can be identified by calling "
                "`kdu_codestream::can_flip' first.");
    }
  return kdu_resolution(result);
}

/*****************************************************************************/
/*                 kdu_tile_comp::access_resolution (no args)                */
/*****************************************************************************/

kdu_resolution
  kdu_tile_comp::access_resolution()
{
  return access_resolution(state->apparent_dwt_levels);
}


/* ========================================================================= */
/*                                kd_resolution                              */
/* ========================================================================= */

/*****************************************************************************/
/*                kd_resolution::build_decomposition_structure               */
/*****************************************************************************/

void
  kd_resolution::build_decomposition_structure(kdu_params *coc,
                                               kdu_kernels &kernels)
{
  int decomp = 3;
  int transpose_decomp = 3;
  kdu_int16 band_descriptors[49];
  kdu_int16 transpose_band_descriptors[49];
  num_subbands = 1;
  if (res_level > 0)
    {
      if (coc != NULL)
        coc->get(Cdecomp,dwt_level-1,0,decomp);
      transpose_decomp = cod_params::transpose_decomp(decomp);
      num_subbands = (kdu_byte)
        cod_params::expand_decomp_bands(decomp,band_descriptors) - 1;
      cod_params::expand_decomp_bands(transpose_decomp,
                                      transpose_band_descriptors);
    }
  else
    band_descriptors[0] = transpose_band_descriptors[0] = 0;

  // Initialize the subbands
  kdu_byte b, k;
  assert(subbands == NULL);
  if (num_subbands <= 3)
    subbands = subband_store;
  else
    subbands = subband_handle = new kd_subband[num_subbands];
  for (b=0; b < num_subbands; b++)
    {
      kd_subband *band = subbands + b;
      band->parent = NULL; // Until we find the true parent
      band->resolution = this;
      band->is_leaf = true;

      if (res_level == 0)
        band->descriptor = 0;
      else
        band->descriptor = band_descriptors[b+1];
      band->sequence_idx = b;
    }

  // Find transpose subband sequence indices
  kdu_int16 tdesc;
  for (b=0; b < num_subbands; b++)
    {
      if (res_level == 0)
        tdesc = 0;
      else
        tdesc = transpose_band_descriptors[b+1];
      tdesc = ((tdesc & 0x00FF)<<8) | ((tdesc>>8) & 0x00FF);
      for (k=0; k < num_subbands; k++)
        if (subbands[k].descriptor == tdesc)
          break;
      assert(k < num_subbands);
      subbands[b].transpose_sequence_idx = k;
    }

  // Find out how many intermediate nodes we need
  int n;
  assert(intermediate_nodes == NULL);
  num_intermediate_nodes = 0;
  for (n=2; n <= 30; n+=2)
    if ((decomp >> n) & 3)
      num_intermediate_nodes++;
  if (num_intermediate_nodes != 0)
    intermediate_nodes = new kd_node[num_intermediate_nodes];

  node.resolution = this;
  node.is_leaf = false;
  node.num_descendant_nodes = 0;
  node.num_descendant_leaves = 0;
  for (b=0; b < num_intermediate_nodes; b++)
    {
      assert(res_level > 0);
      intermediate_nodes[b].parent = NULL; // Until we find the true parent
      intermediate_nodes[b].resolution = this;
      intermediate_nodes[b].is_leaf = false;
      intermediate_nodes[b].num_descendant_nodes = 0;
      intermediate_nodes[b].num_descendant_leaves = 0;
      intermediate_nodes[b].bibo_gains = NULL;
      for (n=0; n < 4; n++)
        intermediate_nodes[b].children[n] = NULL;
    }

  // Create parent-child pointers
  for (n=0; n < 4; n++)
    node.children[n] = NULL;
  this->can_flip = true; // Until proven otherwise inside `create_child_node'
  if (res_level == 0)
    {
      node.children[LL_BAND] = subbands;
      node.num_descendant_leaves = 1;
      subbands[0].parent = &node;
      subbands[0].dims = node.dims;
      subbands[0].branch_x = subbands[0].branch_y = 2;
      subbands[0].orientation = (kdu_byte) LL_BAND;
      return;
    }

  kd_comp_info *ci = codestream->comp_info + tile_comp->cnum;
  int next_band_idx=0, next_inode_idx=0;
  int dfs_horz = ((int) ci->hor_depth[dwt_level]) - hor_depth;
  int dfs_vert = ((int) ci->vert_depth[dwt_level]) - vert_depth;
  if ((dfs_horz | dfs_vert) & ~1)
    { KDU_ERROR(e,0x21101301); e <<
      KDU_TXT("Invalid downsampling factor structure; codestream appears "
              "to be seriously corrupted.");
    }
  int dfs_mask = (dfs_horz + (dfs_vert<<1));

  node.num_hor_steps = (kdu_byte)
    ((dfs_mask & 1)?(tile_comp->kernel_num_steps):0);
  node.num_vert_steps = (kdu_byte)
    ((dfs_mask & 2)?(tile_comp->kernel_num_steps):0);
  node.bibo_gains = new float[node.num_hor_steps+node.num_vert_steps+2];
  float *hor_bibo_gains = node.bibo_gains;
  float *vert_bibo_gains = node.bibo_gains + (1+node.num_hor_steps);
  hor_bibo_gains[0] = (float) kernels.get_bibo_gain(hor_depth,0,NULL);
  if (node.num_hor_steps > 0)
    {
      double lval, hval, *gains =
        kernels.get_bibo_gains(hor_depth,0,NULL,lval,hval);
      for (b=0; b < node.num_hor_steps; b++)
        hor_bibo_gains[b+1] = (float) gains[b];
    }
  vert_bibo_gains[0] = (float) kernels.get_bibo_gain(vert_depth,0,NULL);
  if (node.num_vert_steps > 0)
    {
      double lval, hval, *gains =
        kernels.get_bibo_gains(vert_depth,0,NULL,lval,hval);
      for (b=0; b < node.num_vert_steps; b++)
        vert_bibo_gains[b+1] = (float) gains[b];
    }

  bool hor_extra_stage_high[3], vert_extra_stage_high[3];
  assert(dfs_mask == (decomp & 3));
  decomp >>= 2;
  for (n=1; n < 4; n++)
    if (n == (n & dfs_mask))
      {
        node.children[n] =
          create_child_node(&node,n,dfs_mask,intermediate_nodes,next_inode_idx,
                            subbands,next_band_idx,(decomp & 0x3FF),1,
                            n,false,false,0,hor_extra_stage_high,
                            0,vert_extra_stage_high,kernels);
        decomp >>= 10;
        assert((next_inode_idx <= (int) num_intermediate_nodes) &&
               (next_band_idx <= (int) num_subbands));
      }
  assert((next_inode_idx == (int) num_intermediate_nodes) &&
         (next_band_idx == (int) num_subbands));
}

/*****************************************************************************/
/*                  kd_resolution::complete_initialization                   */
/*****************************************************************************/

void
  kd_resolution::complete_initialization()
{
  /* The purpose of this function is to configure the `max_blocks_per_precinct'
     member.  The implementation is based on the observation that it is
     sufficient to consider the upper left hand 2x2 block of precincts in the
     current tile-component-resolution.  One of these must have the maximum
     size, either because it is a full sized precinct, having the nominal
     precinct dimensions for the resolution, or because the resolution has no
     more than these 4 precincts. */
  max_blocks_per_precinct = 0; // Initial value

  kdu_coords p_idx;
  for (p_idx.y=0; p_idx.y < 2; p_idx.y++)
    for (p_idx.x=0; p_idx.x < 2; p_idx.x++)
      {
        kdu_coords pos_idx = precinct_indices.pos + p_idx;
        node.prec_dims = precinct_partition;
        node.prec_dims.pos.x += pos_idx.x * node.prec_dims.size.x;
        node.prec_dims.pos.y += pos_idx.y * node.prec_dims.size.y;
        node.prec_dims &= node.dims;
        if (!node.prec_dims)
          continue;

        kdu_byte b;
        int precinct_blocks = 0;
        for (b=0; b < num_intermediate_nodes; b++)
          {
            kd_node *node = intermediate_nodes + b;
            node->prec_dims = get_child_dims(node->parent->prec_dims,
                                             node->branch_x,node->branch_y);
          }
        for (b=0; b < num_subbands; b++)
          {
            kd_subband *band = subbands + b;
            kdu_dims prec_dims = get_child_dims(band->parent->prec_dims,
                                                band->branch_x,band->branch_y);
            kdu_dims blocks =
              get_partition_indices(band->block_partition,prec_dims);

            // Convert the dimensions of the block array into a tag tree size
            int level_nodes = blocks.size.x * blocks.size.y;
            precinct_blocks += level_nodes;
            while (level_nodes > 1)
              {
                blocks.size.x = (blocks.size.x+1)>>1;
                blocks.size.y = (blocks.size.y+1)>>1;
                level_nodes = blocks.size.x * blocks.size.y;
                precinct_blocks += level_nodes;
              }
          }
        if (precinct_blocks > max_blocks_per_precinct)
          max_blocks_per_precinct = precinct_blocks;
      }
}

/*****************************************************************************/
/*                  kd_resolution::do_background_processing                  */
/*****************************************************************************/

void
  kd_resolution::do_background_processing(kdu_thread_env *env)
{
  kdu_int32 old_sched, new_sched;
  do { // Enter compare-and-set loop
    old_sched = bkgnd_state.get();
    new_sched = old_sched & ~(KD_RESOLUTION_BKGND_SCHEDULED);
    if (!(old_sched & KD_RESOLUTION_BKGND_BLOCKING_MASK))
      new_sched &= ~(KD_RESOLUTION_BKGND_PROGRESS);
  } while (!bkgnd_state.compare_and_set(old_sched,new_sched));
  if (old_sched & KD_RESOLUTION_BKGND_BLOCKING_MASK)
    return; // This function is blocked from doing anything until the blocking
            // thread(s) invoke `schedule_background_detachment', which
            // will eventually cause us to come back here.

  if (pending_notify_queue != NULL)
    { // Deliver notification that could not safely be delivered the last
      // time we entered this function.
      pending_notify_queue->update_dependencies(pending_p_delta,1,env);
      pending_notify_queue = NULL;
      pending_p_delta = 0;
    }
  if ((old_sched & KD_RESOLUTION_BKGND_LIVE_QUEUES_MASK) == 0)
    { 
      return; // Processing completed for all live queues; dead queues cannot
              // schedule progress requests!
    }
  
  int b;
  if (old_sched & KD_RESOLUTION_BKGND_PROGRESS)
    { // We may need to parse a new "row" of precincts
      for (b=0; b < num_subbands; b++)
        if (subbands[b].pending_bkgnd_state == 0)
          { // At least one subband has no code-block "rows" left
            advance_precinct_rows_available(env);
            break;
          }
    }

  bool reschedule=false;
  for (b=0; b < num_subbands; b++)
    { 
      kd_subband *band = subbands + b;
      kdu_thread_queue *queue = band->notify_queue;
      kdu_int32 old_state, old_S, old_R, new_R;
      kdu_int32 old_pending = band->pending_bkgnd_state & 0x7FFFFFFF;
      kdu_int32 new_state, new_pending;
      do { // Enter compare-and-set loop
        old_state = band->bkgnd_state.get();
        old_S = old_state & 3;
        old_R = new_R = old_state >> 2;
        new_state = old_state;
        new_pending = old_pending;
        if (old_S == 1)
          { 
            if ((old_R > 0) && (old_pending > 0))
              { new_R--;  new_state -= 4;  new_pending--; }
            if ((new_pending == 0) && (band->pending_bkgnd_state < 0))
              new_state = 0; // All resources have been made available
          }
        else if (old_S == 2)
          new_state = new_R = 0; // Respond to detachment request
      } while (!band->bkgnd_state.compare_and_set(old_state,new_state));
      band->pending_bkgnd_state += (new_pending - old_pending);
      if (new_state == old_state)
        { // Nothing to do, but may need to reschedule
          if (new_R > 0)
            reschedule = true;
          continue;
        }
      assert((old_S == 1) || (old_S == 2));
      int p_delta = 0;
      if (old_S == 1)
        { 
          assert((new_R >= 0) && (old_R >= 0));
          assert(old_R > new_R);
          p_delta = (old_R - new_R) << band->notify_quantum_bits;
          if (new_R > 0)
            { 
              assert(new_state != 0);
              reschedule = true;
            }
        }
      if (new_state == 0)
        { // Notify queue becoming non-live; need to check whether it is
          // safe to send the notification message immediately; otherwise,
          // we need to defer it.
          old_sched =
            bkgnd_state.exchange_add(-KD_RESOLUTION_BKGND_LIVE_QUEUES_1);
          kdu_int32 old_live = old_sched&KD_RESOLUTION_BKGND_LIVE_QUEUES_MASK;
          assert(old_live > 0);
          if (old_live == KD_RESOLUTION_BKGND_LIVE_QUEUES_1)
            { // This is the last live queue
              if (old_sched & (KD_RESOLUTION_BKGND_SCHEDULED |
                               KD_RESOLUTION_BKGND_BLOCKING_MASK))
                { // We have either been rescheduled or else we are going to
                  // be rescheduled by a call to `schedule_background_detach';
                  // it is dangerous to send the closure notification
                  // immediately, so defer it until we come back to this
                  // function without any blocking threads.
                  pending_notify_queue = band->notify_queue;
                  pending_p_delta = p_delta;
                }
              else
                queue->update_dependencies(p_delta,1,env); // Closure call
              assert(!reschedule);
              return; // No need to scan for any other queues.
            }
          queue->update_dependencies(p_delta,1,env); // Closure call
        }
      else
        { 
          assert(p_delta > 0);
          queue->update_dependencies(p_delta,0,env); // Not a closure call
        }
    }

  if (reschedule)
    this->schedule_background_progress();
}

/*****************************************************************************/
/*               kd_resolution::advance_precinct_rows_available              */
/*****************************************************************************/

void
  kd_resolution::advance_precinct_rows_available(kdu_thread_env *env)
{
  bool transpose = codestream->transpose;
  bool flip = codestream->vflip;
  int p_across; // Number of precincts to make available
  kdu_coords p_inc, p_idx=region_indices.pos;
  if (!transpose)
    { // Make precincts available by rows
      p_inc.x = 1;
      p_across = region_indices.size.x;
      if (!flip)
        p_idx.y += precinct_rows_available;
      else
        p_idx.y += region_indices.size.y-1-precinct_rows_available;
      assert(precinct_rows_available < region_indices.size.y);
    }
  else
    { // Make precincts available by columns
      p_inc.y = 1;
      p_across = region_indices.size.y;
      if (!flip)
        p_idx.x += precinct_rows_available;
      else
        p_idx.x += region_indices.size.x-1-precinct_rows_available;
      assert(precinct_rows_available < region_indices.size.x);
    }
     
  bool have_lock = false;
  kdu_coords rel_p_idx = p_idx - precinct_indices.pos;
  if (codestream->in == NULL)
    { // For output or interchange codestreams, use the "plock" context
      for (; p_across > 0; p_across--, rel_p_idx+=p_inc)
        { 
          int p_num = rel_p_idx.x + rel_p_idx.y*precinct_indices.size.x;
          kd_precinct *precinct = precinct_refs[p_num].active_deref();
          if (precinct != NULL)
            continue;
          if (!have_lock)
            { 
              have_lock = true;
              codestream->acquire_lock(KD_THREADLOCK_PRECINCT,env);
            }
          precinct = precinct_refs[p_num].open(this,rel_p_idx,true,env);
        }
      if (have_lock)
        codestream->release_lock(KD_THREADLOCK_PRECINCT,env);
    }
  else
    { // For input codestreams, use the "glock" context
      for (; p_across > 0; p_across--, rel_p_idx+=p_inc)
        { 
          int p_num = rel_p_idx.x + rel_p_idx.y*precinct_indices.size.x;
          kd_precinct *precinct = precinct_refs[p_num].active_deref();
          if ((precinct != NULL) && (precinct->flags & KD_PFLAG_LOADED_LOCKED))
            continue;
          if (!have_lock)
            { 
              have_lock = true;
              codestream->acquire_lock(KD_THREADLOCK_GENERAL,env);
              codestream->process_pending_precincts();
            }          
          precinct = precinct_refs[p_num].open(this,rel_p_idx,true,env);
          if ((precinct == NULL) ||
              (precinct->num_outstanding_blocks.get() == 0) ||
              (precinct->flags & KD_PFLAG_LOADED_LOCKED))
            continue; // `precinct' can be NULL if `kd_precinct::initialize'
                      // detected that the precinct has no code-blocks, after
                      // which the precinct may have been released from within
                      // the `finished_desequencing' function.
          precinct->load_required_packets();
        }
      if (have_lock)
        codestream->release_lock(KD_THREADLOCK_GENERAL,env);
    }

  precinct_rows_available++;

  // Now convert precinct availability to block availability and reflect
  // the information to the individual subbands.
  int b;
  for (b=0; b < num_subbands; b++)
    { 
      kd_subband *band = subbands + b;
      int region_size, log2_blocks;
      int r_old, r_new; // Figure out block "rows" available before and now
      if (!transpose)
        { 
          region_size = band->region_indices.size.y;
          log2_blocks = band->log2_blocks_per_precinct.y;
          r_old = p_idx.y << log2_blocks;
          r_old -= band->region_indices.pos.y;
        }
      else
        { 
          region_size = band->region_indices.size.x;
          log2_blocks = band->log2_blocks_per_precinct.y;
          r_old = p_idx.x << log2_blocks;
          r_old -= band->region_indices.pos.x;
        }
      if (flip)
        r_old = region_size - r_old - (1<<log2_blocks); 
      r_new = r_old + (1 << log2_blocks);
      if (r_new >= region_size)
        { // Reached the end of the subband
          band->pending_bkgnd_state |= 0x80000000;
          r_new = region_size;
        }
      if (r_old < 0)
        r_old = 0;
      if (r_new > r_old)
        band->pending_bkgnd_state += r_new-r_old;
    }
}


/* ========================================================================= */
/*                             kdu_resolution                                */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdu_resolution::access_next                         */
/*****************************************************************************/

kdu_resolution
  kdu_resolution::access_next()
{
  assert(state != NULL);
  return kdu_resolution((state->res_level==0)?NULL:(state-1));
}

/*****************************************************************************/
/*                         kdu_resolution::which                             */
/*****************************************************************************/

int
  kdu_resolution::which()
{
  assert(state != NULL);
  return state->res_level;
}

/*****************************************************************************/
/*                       kdu_resolution::get_dwt_level                       */
/*****************************************************************************/

int
  kdu_resolution::get_dwt_level()
{
  return state->dwt_level;
}

/*****************************************************************************/
/*                         kdu_resolution::get_dims                          */
/*****************************************************************************/

void
  kdu_resolution::get_dims(kdu_dims &result)
{
  assert(state != NULL);
  result = state->node.region;
  result.to_apparent(state->codestream->transpose,
                     state->codestream->vflip,
                     state->codestream->hflip);
}

/*****************************************************************************/
/*                   kdu_resolution::get_valid_precincts                     */
/*****************************************************************************/

void
  kdu_resolution::get_valid_precincts(kdu_dims &indices)
{
  indices = state->region_indices;
  indices.to_apparent(state->codestream->transpose,
                      state->codestream->vflip,
                      state->codestream->hflip);
}

/*****************************************************************************/
/*                      kdu_resolution::open_precinct                        */
/*****************************************************************************/

kdu_precinct
  kdu_resolution::open_precinct(kdu_coords idx, kdu_thread_env *env)
{
  if ((state->codestream->in != NULL) ||
      (state->codestream->out != NULL))
    { KDU_ERROR_DEV(e,21); e <<
        KDU_TXT("Calls to `kdu_resolution::open_precinct' are "
        "permitted only with interchange codestream objects (i.e., those "
        "which have neither a compressed data source nor a compressed data "
        "target).");
    }
  idx.from_apparent(state->codestream->transpose,
                    state->codestream->vflip,
                    state->codestream->hflip);
  idx -= state->region_indices.pos;
  assert((idx.x >= 0) && (idx.x < state->region_indices.size.x) &&
         (idx.y >= 0) && (idx.y < state->region_indices.size.y));
  idx += state->region_indices.pos; // Back to absolute indices.
  idx -= state->precinct_indices.pos;
  int p = idx.y*state->precinct_indices.size.x + idx.x;
  kd_precinct *precinct;
  if (env != NULL)
    { 
      state->codestream->acquire_lock(KD_THREADLOCK_PRECINCT,env);
      precinct = state->precinct_refs[p].open(state,idx,true,env);
      state->codestream->release_lock(KD_THREADLOCK_PRECINCT,env);
    }
  else
    precinct = state->precinct_refs[p].open(state,idx,true,NULL);
  return kdu_precinct(precinct);
}

/*****************************************************************************/
/*                     kdu_resolution::get_precinct_id                       */
/*****************************************************************************/

kdu_long
  kdu_resolution::get_precinct_id(kdu_coords idx)
{
  idx.from_apparent(state->codestream->transpose,
                    state->codestream->vflip,
                    state->codestream->hflip);
  idx -= state->precinct_indices.pos;
  assert((idx.x >= 0) && (idx.x < state->precinct_indices.size.x) &&
         (idx.y >= 0) && (idx.y < state->precinct_indices.size.y));
  kd_tile_comp *tc = state->tile_comp;
  kd_tile *tile = tc->tile;
  kdu_long id = idx.y*state->precinct_indices.size.x + idx.x;
  for (kd_resolution *rp=state-state->res_level; rp != state; rp++)
    id += rp->num_precincts;
  id = id*tile->num_components + tc->cnum;
  id = id*tile->codestream->tile_span.x*tile->codestream->tile_span.y;
  id += tile->t_num;
  return id;
}

/*****************************************************************************/
/*                  kdu_resolution::get_precinct_relevance                   */
/*****************************************************************************/

double
  kdu_resolution::get_precinct_relevance(kdu_coords idx)
{
  idx.from_apparent(state->codestream->transpose,
                    state->codestream->vflip,
                    state->codestream->hflip);
  kdu_dims precinct_region = state->precinct_partition;
  precinct_region.pos.x += idx.x * precinct_region.size.x;
  precinct_region.pos.y += idx.y * precinct_region.size.y;
  precinct_region &= state->node.dims;
  kdu_long area = precinct_region.area();
  if (area <= 0)
    return 0.0;
  precinct_region &= state->node.region_cover;
  double ratio = ((double) precinct_region.area()) / ((double) area);
  return ratio;
}

/*****************************************************************************/
/*                   kdu_resolution::get_precinct_packets                    */
/*****************************************************************************/

int
  kdu_resolution::get_precinct_packets(kdu_coords idx, kdu_thread_env *env,
                                       bool parse_if_necessary)
{
  kd_tile *tile = state->tile_comp->tile;
  kd_codestream *codestream = state->codestream;
  if (codestream->out != NULL)
    return tile->num_layers;

  idx.from_apparent(state->codestream->transpose,
                    state->codestream->vflip,
                    state->codestream->hflip);
  idx -= state->region_indices.pos;
  assert((idx.x >= 0) && (idx.x < state->region_indices.size.x) &&
         (idx.y >= 0) && (idx.y < state->region_indices.size.y));
  idx += state->region_indices.pos; // Back to absolute indices.
  idx -= state->precinct_indices.pos;
  int p = idx.y*state->precinct_indices.size.x + idx.x;
  kd_precinct *precinct = state->precinct_refs[p].active_deref();
  if (parse_if_necessary && (codestream->in != NULL) &&
      ((precinct == NULL) || !(precinct->flags & KD_PFLAG_LOADED_LOCKED)))
    { 
      if (env != NULL)
        codestream->acquire_lock(KD_THREADLOCK_GENERAL,env);
      if (precinct == NULL)
        precinct = state->precinct_refs[p].open(state,idx,true,env);
      if (precinct == NULL)
        { 
          KDU_ERROR_DEV(e,22); e <<
            KDU_TXT("The precinct you are trying to access via "
                    "`kdu_resolution::get_precinct_packets' is no longer "
                    "available, probably because you already fully accessed "
                    "its visible contents, causing it to be recycled.");
        }
      precinct->load_required_packets();
      if (env != NULL)
        codestream->release_lock(KD_THREADLOCK_GENERAL,env);
    }
  int result = 0;
  if (precinct != NULL)
    { 
      result = precinct->num_packets_read;
      if (result < 0)
        result = 0;
      if (result > tile->num_apparent_layers)
        result = tile->num_apparent_layers;
    }
  return result;
}

/*****************************************************************************/
/*                   kdu_resolution::get_precinct_samples                    */
/*****************************************************************************/

kdu_long
  kdu_resolution::get_precinct_samples(kdu_coords idx)
{
  idx.from_apparent(state->codestream->transpose,
                    state->codestream->vflip,
                    state->codestream->hflip);
  kdu_dims precinct_region = state->precinct_partition;
  precinct_region.pos.x += idx.x * precinct_region.size.x;
  precinct_region.pos.y += idx.y * precinct_region.size.y;
  precinct_region &= state->node.dims;
  kdu_long area = precinct_region.area();
  if (state->res_level > 0)
    { // Remove the area associated with the LL-band
      kdu_coords low_min = precinct_region.pos;
      kdu_coords low_lim = low_min + precinct_region.size;
      if (state->node.children[HL_BAND] != NULL)
        { // Primary node is split horizontally
          low_min.x = (low_min.x+1) >> 1;
          low_lim.x = (low_lim.x+1) >> 1;
        }
      if (state->node.children[LH_BAND] != NULL)
        { // Primary node is split vertically
          low_lim.y = (low_lim.y+1) >> 1;
          low_min.y = (low_min.y+1) >> 1;
        }
      area -= (low_lim.y-low_min.y) * (low_lim.x-low_min.x);
      assert(area >= 0);
    }
  return area;
}

/*****************************************************************************/
/*                      kdu_resolution::get_reversible                       */
/*****************************************************************************/

bool
  kdu_resolution::get_reversible()
{
  return state->tile_comp->reversible;
}

/*****************************************************************************/
/*                      kdu_resolution::propagate_roi                        */
/*****************************************************************************/

bool
  kdu_resolution::propagate_roi()
{
  return state->propagate_roi;
}

/*****************************************************************************/
/*                       kdu_resolution::access_node                         */
/*****************************************************************************/

kdu_node
  kdu_resolution::access_node()
{
  assert(state != NULL);
  return kdu_node(&state->node);
}

/*****************************************************************************/
/*                  kdu_resolution::get_valid_band_indices                   */
/*****************************************************************************/

int
  kdu_resolution::get_valid_band_indices(int &min_idx)
{
  min_idx = (state->res_level==0)?0:1;
  return state->num_subbands;
}

/*****************************************************************************/
/*                      kdu_resolution::access_subband                       */
/*****************************************************************************/

kdu_subband
  kdu_resolution::access_subband(int band_idx)
{
  if (state->res_level > 0)
    band_idx--;
  assert((band_idx >= 0) && (band_idx < state->num_subbands));
  kd_subband *band = state->subbands + band_idx;
  if (state->codestream->transpose)
    band = state->subbands + band->transpose_sequence_idx;
  return kdu_subband(band);
}


/* ========================================================================= */
/*                                  kd_node                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                            kd_node::adjust_cover                          */
/*****************************************************************************/

void
  kd_node::adjust_cover(kdu_dims child_cover,
                        int child_branch_x, int child_branch_y)
{
  if ((child_cover.size.x < 0) || (child_cover.size.y < 0))
    return;
  kdu_coords node_min = child_cover.pos;
  kdu_coords node_lim = node_min + child_cover.size;
  if ((child_branch_x & ~1) == 0)
    { // Child was obtained after a horizontal split
      node_min.x += node_min.x + child_branch_x;
      node_lim.x += node_lim.x + child_branch_x - 1;
    }
  if ((child_branch_y & ~1) == 0)
    { // Child was obtained after a vertical split
      node_min.y += node_min.y + child_branch_y;
      node_lim.y += node_lim.y + child_branch_y - 1;
    }
  if (!region_cover)
    {
      region_cover.pos = node_min;
      region_cover.size = node_lim - node_min;
    }
  else
    {
      int delta;
      if ((delta = region_cover.pos.x - node_min.x) > 0)
        {
          region_cover.pos.x -= delta;
          region_cover.size.x += delta;
        }
      if ((delta = node_lim.x - region_cover.pos.x - region_cover.size.x) > 0)
        region_cover.size.x += delta;
      if ((delta = region_cover.pos.y - node_min.y) > 0)
        {
          region_cover.pos.y -= delta;
          region_cover.size.y += delta;
        }
      if ((delta = node_lim.y - region_cover.pos.y - region_cover.size.y) > 0)
        region_cover.size.y += delta;
    }
}


/* ========================================================================= */
/*                                 kdu_node                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                           kdu_node::access_child                          */
/*****************************************************************************/

kdu_node
  kdu_node::access_child(int child_idx)
{
  assert((child_idx >= 0) && (child_idx <= 3));
  if (state->is_leaf)
    return kdu_node(NULL);

  if (state->resolution->codestream->transpose)
    child_idx = ((child_idx & 1) << 1) + ((child_idx >> 1) & 1);
  kd_leaf_node *result = ((kd_node *) state)->children[child_idx];
  if ((result != NULL) && (result->resolution->res_level == 0))
    { // Special case in which the LL child node should actually be the LL
      // subband, rather than the next lower resolution primary node
      assert((child_idx==LL_BAND) && (result==&(result->resolution->node)));
      result = ((kd_node *) result)->children[LL_BAND];
      assert(result->is_leaf);
    }
  return kdu_node(result);
}

/*****************************************************************************/
/*                         kdu_node::get_directions                          */
/*****************************************************************************/

int
  kdu_node::get_directions()
{
  int result = 0;
  if (!state->is_leaf)
    { 
      if (((kd_node *) state)->children[HL_BAND] != NULL)
        result |= KDU_NODE_DECOMP_HORZ;
      if (((kd_node *) state)->children[LH_BAND] != NULL)
        result |= KDU_NODE_DECOMP_VERT;
    }
  if (state->resolution->codestream->transpose)
    result |= KDU_NODE_TRANSPOSED;
  return result;
}

/*****************************************************************************/
/*                       kdu_node::get_num_descendants                       */
/*****************************************************************************/

int
  kdu_node::get_num_descendants(int &num_leaf_descendants)
{
  if (state->is_leaf)
    return (num_leaf_descendants = 0);
  kd_node *node = (kd_node *) state;
  if (node->resolution->res_level == 0)
    { // Special case: the lowest resolution level formally can be used to
      // access one child node, which is the LL subband, so we should return
      // 1 here, even though we cannot access this node via calls to
      // `access_child', starting from higher up in the subband hierarchy.
      return (num_leaf_descendants = 1);
    }
  num_leaf_descendants = node->num_descendant_leaves;
  return node->num_descendant_nodes;
}

/*****************************************************************************/
/*                          kdu_node::access_subband                         */
/*****************************************************************************/

kdu_subband
  kdu_node::access_subband()
{
  return kdu_subband((state->is_leaf)?((kd_subband *) state):NULL);
}

/*****************************************************************************/
/*                        kdu_node::access_resolution                        */
/*****************************************************************************/

kdu_resolution
  kdu_node::access_resolution()
{
  return kdu_resolution(state->resolution);
}

/*****************************************************************************/
/*                            kdu_node::get_dims                             */
/*****************************************************************************/

void
  kdu_node::get_dims(kdu_dims &result)
  /* We need to be particularly careful about mapping apparent node
     dimensions through geometric transformations.  The reason is that
     high-pass branches produce coordinate systems which are notionally
     based on an origin which is spaced +1/2 a subband sample beyond
     the origin of the real coordinate system.  To flip these branches, it
     is not sufficient to negate the coordinates of each constituent sample;
     we must also subtract 1.
        Importantly, where flipping is required, the path which produces
     any given node must contain no non-trivial subband splitting operations
     beyond the first high-pass operation (evaluated separately in each
     direction).  If this condition is violated, `kdu_codestream::can_flip'
     should return false. */
{
  kd_codestream *cs = state->resolution->codestream;
  result = state->region;
  result.to_apparent(cs->transpose,cs->vflip,cs->hflip);
  if (!(cs->vflip || cs->hflip))
    return;

  kdu_coords offset, branch_idx;
  kd_leaf_node *scan;
  for (scan=state; scan != &(scan->resolution->node); scan=scan->parent)
    {
      branch_idx.x = scan->branch_x;  branch_idx.y = scan->branch_y;
      if (cs->transpose)
        branch_idx.transpose();
      if ((branch_idx.x == 1) && cs->hflip)
        { assert(!offset.x); offset.x = 1; }
      if ((branch_idx.y == 1) && cs->vflip)
        { assert(!offset.y); offset.y = 1; }
    }
  result.pos -= offset;
}

/*****************************************************************************/
/*                         kdu_node::get_kernel_id                           */
/*****************************************************************************/

int
  kdu_node::get_kernel_id()
{
  return state->resolution->tile_comp->kernel_id;
}

/*****************************************************************************/
/*                       kdu_node::get_kernel_info                           */
/*****************************************************************************/

const kdu_kernel_step_info *
  kdu_node::get_kernel_info(int &num_steps, float &low_scale,
                            float &high_scale, bool &symmetric,
                            bool &symmetric_extension,
                            int &low_support_min, int &low_support_max,
                            int &high_support_min, int &high_support_max,
                            bool vertical)
{
  kd_tile_comp *tc = state->resolution->tile_comp;
  bool flip = (vertical)?(tc->codestream->vflip):(tc->codestream->hflip);
  num_steps = tc->kernel_num_steps;
  low_scale = tc->kernel_low_scale;
  high_scale = tc->kernel_high_scale;
  symmetric = tc->kernel_symmetric;
  symmetric_extension = tc->kernel_symmetric_extension;
  if (flip)
    {
      low_support_min = -tc->low_support_max;
      low_support_max = -tc->low_support_min;
      high_support_min = -tc->high_support_max;
      high_support_max = -tc->high_support_min;
      return tc->kernel_step_info_flipped;
    }
  else
    {
      low_support_min = tc->low_support_min;
      low_support_max = tc->low_support_max;
      high_support_min = tc->high_support_min;
      high_support_max = tc->high_support_max;
      return tc->kernel_step_info;
    }
}

/*****************************************************************************/
/*                   kdu_node::get_kernel_coefficients                       */
/*****************************************************************************/

const float *
  kdu_node::get_kernel_coefficients(bool vertical)
{
  kd_tile_comp *tc = state->resolution->tile_comp;
  bool flip = (vertical)?(tc->codestream->vflip):(tc->codestream->hflip);
  return (flip)?(tc->kernel_coefficients_flipped):(tc->kernel_coefficients);
}

/*****************************************************************************/
/*                         kdu_node::get_bibo_gains                          */
/*****************************************************************************/

const float *
  kdu_node::get_bibo_gains(int &num_steps, bool vertical)
{
  if (state->resolution->codestream->transpose)
    vertical = !vertical;
  if (state->is_leaf)
    {
      num_steps = 0;
      kd_node *parent = state->parent;
      if (vertical)
        return parent->bibo_gains + (parent->num_vert_steps & 254);
      else
        return parent->bibo_gains + (parent->num_hor_steps & 254);
    }
  else
    {
      kd_node *node = (kd_node *) state;
      if (vertical)
        {
          num_steps = node->num_vert_steps;
          return node->bibo_gains + (node->num_hor_steps+1);
        }
      else
        {
          num_steps = node->num_hor_steps;
          return node->bibo_gains;
        }
    }
}


/* ========================================================================= */
/*                               kdu_subband                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdu_subband::get_band_idx                          */
/*****************************************************************************/

int
  kdu_subband::get_band_idx()
{
  int band_idx = state->sequence_idx;
  if (state->resolution->res_level > 0)
    band_idx++;
  return band_idx;
}

/*****************************************************************************/
/*                      kdu_subband::access_resolution                       */
/*****************************************************************************/

kdu_resolution
  kdu_subband::access_resolution()
{
  return kdu_resolution(state->resolution);
}

/*****************************************************************************/
/*                      kdu_subband::is_top_level_band                       */
/*****************************************************************************/

bool
  kdu_subband::is_top_level_band()
{
  kd_resolution *res = state->resolution;
  kd_tile_comp *tc = res->tile_comp;
  int max_res_level = tc->apparent_dwt_levels;
  return (res->res_level == max_res_level);
}

/*****************************************************************************/
/*                      kdu_subband::get_thread_context                      */
/*****************************************************************************/

kdu_thread_context *
  kdu_subband::get_thread_context(kdu_thread_env *env)
{
  if (env == NULL)
    return NULL;
  kd_codestream *cs = state->resolution->codestream;
  if (cs->thread_context == NULL)
    cs->gen_no_thread_context_error();
  int t_idx = cs->thread_context->check_group(env);
  if (t_idx < 0)
    cs->gen_bad_thread_context_error();
  return cs->thread_context;
}

/*****************************************************************************/
/*                          kdu_subband::get_K_max                           */
/*****************************************************************************/

int
  kdu_subband::get_K_max() const
{
  return state->K_max;
}

/*****************************************************************************/
/*                       kdu_subband::get_K_max_prime                        */
/*****************************************************************************/

int
  kdu_subband::get_K_max_prime() const
{
  return state->K_max_prime;
}

/*****************************************************************************/
/*                        kdu_subband::get_reversible                        */
/*****************************************************************************/

bool
  kdu_subband::get_reversible() const
{
  return state->resolution->tile_comp->reversible;
}

/*****************************************************************************/
/*                           kdu_subband::get_delta                          */
/*****************************************************************************/

float
  kdu_subband::get_delta() const
{
  return (state->resolution->tile_comp->reversible)?0.0F:state->delta;
}

/*****************************************************************************/
/*                          kdu_subband::get_msb_wmse                        */
/*****************************************************************************/

float
  kdu_subband::get_msb_wmse() const
{
  kd_resolution *res = state->resolution;
  if (res->codestream->in != NULL)
    return 1.0F;
  double result = state->delta;
  int i;
  for (i=state->K_max_prime; i > 30; i-=30)
    result *= (double)(1<<30);
  result *= (double)(1<<(i-1));
  result *= result;
  result *= state->G_b;
  if (res->tile_comp->G_tc > 0.0F)
    result *= res->tile_comp->G_tc; // Should always be the case
  result *= state->W_b;
  result *= state->W_b; // Squares the W_b weight.
  return (float) result;
}

/*****************************************************************************/
/*                         kdu_subband::get_roi_weight                       */
/*****************************************************************************/

bool
  kdu_subband::get_roi_weight(float &energy_weight) const
{
  if (state->roi_weight < 0.0F)
    return false;
  energy_weight = state->roi_weight * state->roi_weight;
  return true;
}

/*****************************************************************************/
/*                       kdu_subband::get_masking_params                     */
/*****************************************************************************/

bool
  kdu_subband::get_masking_params(float &visibility_floor,
                                  float &masking_exponent,
                                  float &visual_scale) const
{
  if ((state == NULL) || (state->vis_floor <= 0.0f))
    return false;
  visibility_floor = state->vis_floor;
  masking_exponent = 0.5;
  visual_scale = state->vis_scale;
  
  return true;
}

/*****************************************************************************/
/*                           kdu_subband::get_dims                           */
/*****************************************************************************/

void
  kdu_subband::get_dims(kdu_dims &result) const
  /* We need to be particularly careful about mapping apparent subband
     dimensions through geometric transformations.  The reason is that
     high-pass subband coordinates are notionally based on an origin which
     is spaced +1/2 a subband sample beyond the origin of the real
     coordinate system.  To flip these subbands, it is not sufficient
     to negate the coordinates of each constituent sample; we must also
     subtract 1.
        Perhaps most interesting of all is the fact that flipping the
     image cannot be achieved by appropriately flipping and offsetting the
     subband regions if a packet wavelet transform has been used in which
     horizontally (resp. vertically) high-pass subbands are decomposed
     again in the horizontal (resp. vertical) direction.  Under these
     conditions, `kdu_codestream::can_flip' will return false. */
{
  kd_codestream *cs = state->resolution->codestream;
  result = state->region;
  result.to_apparent(cs->transpose,cs->vflip,cs->hflip);
  if (!(cs->vflip || cs->hflip))
    return;

  kdu_coords offset, branch_idx;
  kd_leaf_node *scan;
  for (scan=state; scan!=&(scan->resolution->node); scan=scan->parent)
    {
      branch_idx.x = scan->branch_x;  branch_idx.y = scan->branch_y;
      if (cs->transpose)
        branch_idx.transpose();
      if ((branch_idx.x == 1) && cs->hflip)
        { assert(!offset.x); offset.x = 1; }
      if ((branch_idx.y == 1) && cs->vflip)
        { assert(!offset.y); offset.y = 1; }
    }
  result.pos -= offset;
}

/*****************************************************************************/
/*                       kdu_subband::get_valid_blocks                       */
/*****************************************************************************/

void
  kdu_subband::get_valid_blocks(kdu_dims &indices) const
{
  kd_codestream *cs = state->resolution->codestream;
  indices = state->region_indices;
  indices.to_apparent(cs->transpose,cs->vflip,cs->hflip);
}

/*****************************************************************************/
/*                        kdu_subband::get_block_size                        */
/*****************************************************************************/

void
  kdu_subband::get_block_size(kdu_coords &nominal_size,
                              kdu_coords &first_size) const
{
  kdu_dims indices;
  kdu_coords first_idx;
  kdu_dims first_dims;
  kd_codestream *cs = state->resolution->codestream;
  
  nominal_size = state->block_partition.size;
  get_valid_blocks(indices);
  first_idx = indices.pos;
  first_idx.from_apparent(cs->transpose,cs->vflip,cs->hflip);
  first_dims = state->block_partition;
  first_dims.pos.x += first_idx.x*first_dims.size.x;
  first_dims.pos.y += first_idx.y*first_dims.size.y;
  first_dims &= state->region;
  assert((!first_dims) || (first_dims.area() > 0));
  first_size = first_dims.size;
  if (cs->transpose)
    {
      nominal_size.transpose();
      first_size.transpose();
    }
}

/*****************************************************************************/
/*                     kdu_subband::get_block_geometry                       */
/*****************************************************************************/

int
  kdu_subband::get_block_geometry(bool &transpose,
                                  bool &vflip, bool &hflip) const
{
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  transpose = codestream->transpose;
  vflip = codestream->vflip;
  hflip = codestream->hflip;
  return state->orientation;
}

/*****************************************************************************/
/*                     kdu_subband::block_row_generated                      */
/*****************************************************************************/

void
  kdu_subband::block_row_generated(int block_height, bool subband_finished,
                                   kdu_thread_env *env)
{
  kd_resolution *res = state->resolution;
  kd_codestream *cs = res->codestream;
  if ((env != NULL) && cs->tc_flush_pending.get())
    { // Seems we have a pending tile-component triggered flush to schedule
      if (cs->tc_flush_pending.exchange(0) > 0)
        cs->thread_context->schedule_flush_processing(env);
    }
  if (subband_finished)
    { // May affect completed tile-component trigger, but not the incremental
      // flush trigger
      kd_tile_comp *tc = res->tile_comp;
      if (env == NULL)
        { // Can avoid the overhead of interlocked atomics
          kdu_int32 new_cnt = tc->completed_subband_counter.add_get(-1);
          assert(new_cnt >= 0);
          if ((new_cnt == 0) && (cs->tc_flush_interval > 0))
            { 
              new_cnt = cs->tc_flush_counter.add_get(-1);
              if (new_cnt == 0)
                { 
                  cs->flush_if_ready(NULL);
                  cs->tc_flush_counter.set(cs->tc_flush_interval);
                }
            }
        }
      else
        { // Need interlocked atomics
          kdu_int32 new_cnt = tc->completed_subband_counter.exchange_add(-1)-1;
          assert(new_cnt >= 0);
          if ((new_cnt == 0) && (cs->tc_flush_interval > 0))
            { // Otherwise, there are no more tile-comp flush trigger points
              new_cnt = cs->tc_flush_counter.exchange_add(-1)-1;
              if (new_cnt == 0)
                { 
                  cs->tc_flush_pending.set(1); // schedule flush later
                  kdu_int32 interval = cs->tc_flush_interval;
                  while ((interval > 0) && (new_cnt <= 0))
                    new_cnt = cs->tc_flush_counter.exchange_add(interval)
                            + interval;
                }
            }
        }
    }
  else if (cs->cached_target)
    { // May affect the incremental flush trigger (structured target policy)
      kdu_int16 descr_mask;
      int delta = block_height;
      if (!cs->transpose)
        { 
          descr_mask = 7<<2; // Bits 2-4 must be 0 (horizontal low-pass)
          delta *= res->tile_comp->sub_sampling.y;
        }
      else
        { 
          descr_mask = 7<<10; // Bits 10-12 must be 0 (vertical low-pass)
          delta *= res->tile_comp->sub_sampling.x;
        }
      if (((state->descriptor & descr_mask) == 0) &&
          ((cs->incr_flush_interval > 0) ||
           (cs->incr_flush_counter.get() > 0)))
        { 
          if (env == NULL)
            { // Can avoid the overhead of interlocked atomics
              kdu_int32 new_cnt = cs->incr_flush_counter.add_get(-delta);
              if (new_cnt <= 0)
                { 
                  cs->flush_if_ready(NULL);
                  kdu_int32 interval = cs->incr_flush_interval;
                  while ((interval > 0) && (new_cnt <= 0))
                    new_cnt = cs->incr_flush_counter.add_get(interval);
                }
            }
          else
            { // Need interlocked atomics
              kdu_int32 old_cnt = cs->incr_flush_counter.exchange_add(-delta);
              kdu_int32 new_cnt = old_cnt - delta;
              if ((old_cnt > 0) && (new_cnt <= 0))
                { 
                  cs->thread_context->schedule_flush_processing(env);
                  kdu_int32 interval = cs->incr_flush_interval;
                  while ((interval > 0) && (new_cnt <= 0))
                    new_cnt = cs->incr_flush_counter.exchange_add(interval)
                            + interval;
                }
            }
        }
    }
  else if ((!cs->cached_target) && (res->res_level == 0))
    { // May affect the incremental flush trigger (linear target policy)
      if ((cs->incr_flush_interval > 0) || (cs->incr_flush_counter.get() > 0))
        { // Otherwise, there are no more incremental flush trigger points
          int delta = block_height; // This will be amount to decrement counter
          if (!cs->transpose)
            { // Normal orientation
              delta <<= res->vert_depth;
              delta *= res->tile_comp->sub_sampling.y;
            }
          else
            { // Transposed orientation
              delta <<= res->hor_depth;
              delta *= res->tile_comp->sub_sampling.x;
            }
          if (env == NULL)
            { // Can avoid the overhead of interlocked atomics
              kdu_int32 new_cnt = cs->incr_flush_counter.add_get(-delta);
              if (new_cnt <= 0)
                { 
                  cs->flush_if_ready(NULL);
                  kdu_int32 interval = cs->incr_flush_interval;
                  while ((interval > 0) && (new_cnt <= 0))
                    new_cnt = cs->incr_flush_counter.add_get(interval);
                }
            }
          else
            { // Need interlocked atomics
              kdu_int32 old_cnt = cs->incr_flush_counter.exchange_add(-delta);
              kdu_int32 new_cnt = old_cnt - delta;
              if ((old_cnt > 0) && (new_cnt <= 0))
                { 
                  cs->thread_context->schedule_flush_processing(env);
                  kdu_int32 interval = cs->incr_flush_interval;
                  while ((interval > 0) && (new_cnt <= 0))
                    new_cnt = cs->incr_flush_counter.exchange_add(interval)
                            + interval;
                }
            }
        }
    }
}

/*****************************************************************************/
/*                    kdu_subband::attach_block_notifier                     */
/*****************************************************************************/

bool
  kdu_subband::attach_block_notifier(kdu_thread_queue *client_queue,
                                     kdu_thread_env *env)
{
  if ((state == NULL) || (state->notify_queue != NULL) ||
      (state->region_indices.size.y <= 0))
    return false;
  
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  if (codestream->thread_context == NULL)
    return false;
  if (state->bkgnd_state.get() != 0)
    { 
      assert(0);
      return false;
    }
  state->notify_queue = client_queue;
  res->bkgnd_state.exchange_add(KD_RESOLUTION_BKGND_LIVE_QUEUES_1);
  state->bkgnd_state.set(1); // S=1 (live notifier attached)
  return true;
}

/*****************************************************************************/
/*                    kdu_subband::detach_block_notifier                     */
/*****************************************************************************/

bool
  kdu_subband::detach_block_notifier(kdu_thread_queue *client_queue,
                                     kdu_thread_env *env)
{
  if ((state == NULL) || (state->notify_queue != client_queue))
    return true;
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  if (codestream->thread_context == NULL)
    { 
      assert(0);
      return true;
    }
  res->reserve_background_detach(); // Enter safe context to avoid possible
                                    // race conditions created by the delivery
                                    // of closure notification messages before
                                    // we finish manipulating the tile's member
                                    // variables.
  kdu_int32 old_val, new_val;
  do {
    old_val = state->bkgnd_state.get();
    new_val = (old_val == 0)?0:2;
  } while (!state->bkgnd_state.compare_and_set(old_val,new_val));
  if (old_val == 0)
    { // We have already become non-live; however, it is not safe to return
      // true here, because the final notification call might still be in
      // progress and we do not want to run the risk of allowing the
      // caller to issue its `all_done' call before the final notification
      // message actually arrives.  Normally, this state of affairs should
      // not be encountered, because the final notification message
      // identifies itself as such via the `closure' argument, so that
      // `detach_block_notifier' should not be called.  However, to deal
      // with the current predicament, our solution is actually to
      // re-attach ourself as a block notifier so that another notification
      // message can be received.
      state->notify_queue = client_queue; // Just in case
      res->bkgnd_state.exchange_add(KD_RESOLUTION_BKGND_LIVE_QUEUES_1);
      state->bkgnd_state.set(2); // S=2 means forced detach requested
    }
  if (res->schedule_background_detach())
    codestream->thread_context->schedule_resolution_processing(env);
  return false;
}

/*****************************************************************************/
/*                  kdu_subband::advance_block_rows_needed                   */
/*****************************************************************************/

void
  kdu_subband::advance_block_rows_needed(kdu_thread_queue *client_queue,
                                         kdu_uint32 delta_rows_needed,
                                         kdu_uint32 q_bits, kdu_uint32 q_size,
                                         kdu_thread_env *env)
{
  if (delta_rows_needed == 0)
    return;
  assert(q_bits < 30);
  if ((state == NULL) || (state->notify_queue != client_queue))
    { // Entice the `client_queue' into believing that all requested
      // code-blocks have already been made available.  This is the right
      // thing to do when the `client_queue' is not attached for background
      // processing and notification.
      kdu_int32 p_delta = (kdu_int32)(delta_rows_needed << q_bits);
      client_queue->update_dependencies(p_delta,0,env);
    }
  else
    { 
      state->notify_quantum_bits = q_bits;
      kdu_int32 old_state, new_state;
      do { // Enter compare-and-set loop
        old_state = new_state = state->bkgnd_state.get();
        if ((old_state & 3) != 0)
          new_state += (kdu_int32)(delta_rows_needed<<2);
      } while (!state->bkgnd_state.compare_and_set(old_state,new_state));        
      if ((old_state & 3) == 0)
        { // All code-blocks should be considered available; this is a sign
          // of a lazy caller who has not noticed that all possible block
          // rows have already been requested -- we have gone non-live
          // for that very reason.  Still no harm in telling the caller
          // that everything required is already available.
          assert(old_state == 0); // R value should be 0 also
          kdu_int32 p_delta = (kdu_int32)(delta_rows_needed << q_bits);
          client_queue->update_dependencies(p_delta,0,env);
          return;
        }
      assert((old_state & 3) == 1);
      kdu_int32 old_R = old_state >> 2;
      assert(old_R >= 0);
      assert(!state->region_indices.is_empty());
      if (old_R == 0)
        { 
          kd_resolution *res = state->resolution;
          kd_codestream *cs = res->codestream;
          if (res->schedule_background_progress())
            cs->thread_context->schedule_resolution_processing(env);
        }
    }
}

/*****************************************************************************/
/*                          kdu_subband::open_block                          */
/*****************************************************************************/

kdu_block *
  kdu_subband::open_block(kdu_coords block_idx, int *return_tpart,
                          kdu_thread_env *env, int hscan_length,
                          bool hscan_start)
{
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  block_idx.from_apparent(codestream->transpose,
                          codestream->vflip,codestream->hflip);
  block_idx -= state->region_indices.pos;
  assert((block_idx.x >= 0) && (block_idx.x < state->region_indices.size.x) &&
         (block_idx.y >= 0) && (block_idx.y < state->region_indices.size.y));
  block_idx += state->region_indices.pos; // Back to absolute indices.

  // First find the precinct to which this block belongs and the block index
  // within that precinct.

  kdu_coords precinct_idx = block_idx;
  precinct_idx.x >>= state->log2_blocks_per_precinct.x;
  precinct_idx.y >>= state->log2_blocks_per_precinct.y;

  // Create the precinct if necessary.
  assert(res->tile_comp->tile->is_open);
  precinct_idx -= res->precinct_indices.pos;
  int precinct_num =
    precinct_idx.x + precinct_idx.y*res->precinct_indices.size.x;
  kd_precinct *precinct;
  if (codestream->in == NULL)
    { // Output and interchange codestreams use the "plock" context
      precinct = res->precinct_refs[precinct_num].deref();
      if (precinct == NULL)
        { // Need to lock the `KD_THREADLOCK_PRECINCT' mutex
          if (env != NULL)
            codestream->acquire_lock(KD_THREADLOCK_PRECINCT,env);
          precinct =
            res->precinct_refs[precinct_num].open(res,precinct_idx,true,env);
          if (precinct == NULL)
            { KDU_ERROR_DEV(e,0x14091301); e <<
              KDU_TXT("You are permitted to open each code-block only once "
                      "from an open tile before closing that tile.");
            }
          if (env != NULL)
            codestream->release_lock(KD_THREADLOCK_PRECINCT,env);
        }
    }
  else
    { // Input codestreams use the "glock" context
      bool have_lock = false;
      precinct = res->precinct_refs[precinct_num].active_deref();
      if (precinct == NULL)
        { // Need to lock general mutex and open the precinct.
          if (env != NULL)
            { 
              have_lock = true;
              codestream->acquire_lock(KD_THREADLOCK_GENERAL,env);
              codestream->process_pending_precincts();
            }
          precinct =
            res->precinct_refs[precinct_num].open(res,precinct_idx,true,env);
          if (precinct == NULL)
            { KDU_ERROR_DEV(e,23); e <<
              KDU_TXT("You are permitted to open each code-block only once "
                      "from an open tile before closing that tile.  If the "
                      "codestream object is marked as persistent, you may "
                      "re-open code-blocks only after re-opening their "
                      "containing tiles.");
            }
        }

      // Load the precinct if necessary.
      if (!(precinct->flags & KD_PFLAG_LOADED_LOCKED))
        { 
          if ((env != NULL) && !have_lock)
            { 
              have_lock = true;
              codestream->acquire_lock(KD_THREADLOCK_GENERAL,env);
              codestream->process_pending_precincts();
            }
          precinct->load_required_packets();
          assert(precinct->flags & KD_PFLAG_LOADED_LOCKED);
        }

      if (have_lock)
        codestream->release_lock(KD_THREADLOCK_GENERAL,env);
    }
  
  // Initialize the block structure.
  kdu_dims band_dims = state->block_partition;
  band_dims.pos.x += block_idx.x*band_dims.size.x;
  band_dims.pos.y += block_idx.y*band_dims.size.y;
  band_dims &= state->dims;
  assert(band_dims.area() > 0);

  kd_precinct_band *pband = precinct->subbands + state->sequence_idx;
  block_idx -= pband->block_indices.pos;
  assert((block_idx.x >= 0) && (block_idx.y >= 0) &&
         (block_idx.x < pband->block_indices.size.x) &&
         (block_idx.y < pband->block_indices.size.y));

  
  // Configure the `result' object
  kdu_block *result;
  if (env == NULL)
    result = codestream->block;
  else
    result = env->get_block();
  assert(result->precinct == NULL);
  result->precinct = precinct;
  result->block = pband->blocks +
    block_idx.x + block_idx.y*pband->block_indices.size.x;
  if (hscan_length <= 0)
    result->cur_hspan = result->hspan_counter = 0;
  else
    { 
      int hspan_left;
      if (!codestream->transpose)
        { 
          hspan_left = pband->block_indices.size.x - block_idx.x;
          if (codestream->hflip)
            hspan_left = 1+block_idx.x; 
        }
      else
        { 
          hspan_left = pband->block_indices.size.y - block_idx.y;
          if (codestream->hflip) // `from_apparent' flips before transpose
            hspan_left = 1+block_idx.y; 
        }
      if (hspan_left > hscan_length)
        hspan_left = hscan_length;
      if ((hscan_start) || (result->cur_hspan == 0))
        result->cur_hspan = result->hspan_counter = hspan_left;
      else if (result->hspan_counter != hspan_left)
        { KDU_ERROR_DEV(e,0x18071401); e <<
          KDU_TXT("Incorrect use of the `hscan_length' and/or `hscan_start' "
                  "arguments to `kdu_subband::open_block'.  After supplying "
                  "a positive value for `hscan_length' each subsequent call "
                  "must increment the `block_idx.x' argument and decrement "
                  "the `hscan_length'.");
        }
    }
  
  result->size = band_dims.size;
  result->region = band_dims & state->region;
  result->region.pos -= band_dims.pos;
  result->modes = state->resolution->tile_comp->modes;
  result->orientation = state->orientation;
  result->K_max_prime = state->K_max_prime;
  result->vflip = codestream->vflip;
  result->hflip = codestream->hflip;
  result->transpose = codestream->transpose;
  result->resilient = codestream->resilient;
  result->fussy = codestream->fussy;

  // Retrieve compressed data, if necessary.
  if (codestream->in != NULL)
    {
      int trunc=codestream->block_truncation_factor;
      int disc_passes = trunc>>8;
      if ((trunc > 0) &&
          (((trunc & 255)*(res->dwt_level+res->res_level)) >
           (((int) res->dwt_level)<<8)))
        disc_passes++;
      result->block->retrieve_data(result,precinct->required_layers,
                                   disc_passes,state->qlim_max_passes,
                                   codestream->in_memory_source);
    }
  else if (!result->block->empty())
    { KDU_ERROR_DEV(e,24); e <<
        KDU_TXT("Attempting to open the same code-block more than "
        "once for writing!");
    }

  if (return_tpart != NULL)
    *return_tpart = precinct->resolution->tile_comp->tile->next_tpart-1;

  return result;
}

/*****************************************************************************/
/*                          kdu_subband::close_block                         */
/*****************************************************************************/

void
  kdu_subband::close_block(kdu_block *result, kdu_thread_env *env)
{
  kd_precinct *precinct = result->precinct;
  kd_block *block = result->block;
  kd_codestream *cs = state->resolution->codestream;
  assert(precinct != NULL);
  assert(((env == NULL) && (result == cs->block)) ||
         ((env != NULL) && (result == env->get_block())));
  result->precinct = NULL;
  int delta_blocks = 1;
  if (result->hspan_counter > 0)
    { 
      delta_blocks = 0;
      result->hspan_counter--;
      if (result->hspan_counter == 0)
        { 
          delta_blocks = result->cur_hspan;
          result->cur_hspan = 0;
          assert(delta_blocks > 0);
        }
    }
  if (env != NULL)
    { // Work with resources that belong exclusively to this thread

      if (cs->in != NULL)
        { // Release as many resources as we can here
          if (!cs->persistent)
            block->cleanup(cs->get_thread_buf_server(env));
              // This moves the block resources to our own local buf server
              // from which they will eventually be released back to the
              // global `kd_buf_master' for recycling.  All of this is
              // lock free, in any event.
          if (delta_blocks > 0)
            { 
              kdu_int32 old_cnt =
                precinct->num_outstanding_blocks.exchange_add(-delta_blocks);
              assert(old_cnt >= delta_blocks);
              if (old_cnt == delta_blocks)
                cs->add_pending_precinct(precinct);
            }
        }
      else if (cs->out != NULL)
        { // Update stats and store data, all temporarily
          assert(block->empty());
          block->store_data(result,cs->get_thread_buf_server(env));
          bool trim_storage = false;
          kd_compressed_stats *master_stats=cs->rate_stats[0];
          if (master_stats != NULL)
            {
              kd_compressed_stats *local_stats=cs->get_thread_rate_stats(env);
              local_stats->update_stats(result);
              if (local_stats->need_transcribe() && master_stats->try_lock())
                { 
                  trim_storage = (master_stats->transcribe(local_stats) &&
                                  !cs->header_generated);
                  master_stats->update_quant_slope_thresholds();
                  master_stats->release_lock();
                }
            }
          if (delta_blocks > 0)
            { 
              kdu_int32 old_cnt =
                precinct->num_outstanding_blocks.exchange_add(-delta_blocks);
              assert(old_cnt >= delta_blocks);
              if (old_cnt == delta_blocks)
                cs->add_pending_precinct(precinct);
            }
          if (trim_storage && (cs->tc_flush_interval == 0))
            cs->thread_context->schedule_trim_processing(env);
        }
      else
        { 
          block->store_data(result,cs->get_thread_buf_server(env));
          if (delta_blocks > 0)
            precinct->num_outstanding_blocks.exchange_add(-delta_blocks);
        }
    }
  else
    { // Single-threaded operation; work with codestream-wide resources
      if (cs->in != NULL)
        { // Release as many resources as we can here.
          if (!cs->persistent)
            block->cleanup(cs->buf_servers);
          if (delta_blocks > 0)
            { 
              kdu_int32 old_cnt =
                precinct->num_outstanding_blocks.get_add(-delta_blocks);
              assert(old_cnt >= 1);
              if (old_cnt == 1)
                precinct->release(); // Might not actually release anything yet
            }
        }
      else if (cs->out != NULL)
        { // If we get here, we have an output block.
          assert(block->empty());
          block->store_data(result,cs->buf_servers);
          bool trim_storage = false;
          kd_compressed_stats *stats = cs->rate_stats[0];
          if (stats != NULL)
            {
              trim_storage = (stats->update_stats(result) &&
                              !cs->header_generated);
              stats->update_quant_slope_thresholds();
            }
          if (delta_blocks > 0)
            { 
              kdu_int32 old_cnt =
                precinct->num_outstanding_blocks.get_add(-delta_blocks);
              assert(old_cnt >= delta_blocks);
              if (old_cnt == delta_blocks)
                precinct->resolution->rescomp->add_ready_precinct(precinct);
            }
          if (trim_storage)
            cs->trim_compressed_data(NULL);
        }
      else
        { 
          block->store_data(result,cs->buf_servers);
          if (delta_blocks > 0)
            precinct->num_outstanding_blocks.get_add(-delta_blocks);
        }
    }
}

/*****************************************************************************/
/*               kdu_subband::get_conservative_slope_threshold               */
/*****************************************************************************/

kdu_uint16
  kdu_subband::get_conservative_slope_threshold()
{
  kd_codestream *codestream = state->resolution->codestream;
  kdu_uint16 result = 1;
  if (codestream->rate_stats[0] != NULL)
    result = codestream->rate_stats[0]->get_conservative_slope_threshold();
  if (codestream->min_slope_threshold > result)
    result = codestream->min_slope_threshold;
  return result;
}


/* ========================================================================= */
/*                                kd_precinct                                */
/* ========================================================================= */

/*****************************************************************************/
/*                          kd_precinct::initialize                          */
/*****************************************************************************/

void
  kd_precinct::initialize(kd_resolution *resolution, kdu_coords pos_idx)
{
  kd_codestream *codestream = resolution->codestream;
  kd_tile_comp *comp = resolution->tile_comp;
  kd_tile *tile = comp->tile;

  pos_idx += resolution->precinct_indices.pos;

  this->resolution = resolution;
  ref = NULL; // Value will be set later by `kd_precinct_ref::open'.
  flags = KD_PFLAG_RELEVANT;
  // addressable = corrupted = desequenced = released = inactive = false;
  // generating = is_significant = false; is_relevant = true;
  if ((codestream->in != NULL) && (!codestream->persistent) &&
      ((resolution->res_level > comp->apparent_dwt_levels) ||
       (!comp->enabled) ||
       (pos_idx.x < resolution->region_indices.pos.x) ||
       (pos_idx.y < resolution->region_indices.pos.y) ||
       (pos_idx.x >= (resolution->region_indices.pos.x +
                      resolution->region_indices.size.x)) ||
       (pos_idx.y >= (resolution->region_indices.pos.y +
                      resolution->region_indices.size.y))))
    flags &= ~KD_PFLAG_RELEVANT;

  required_layers = tile->num_apparent_layers;
  next_layer_idx = num_packets_read = cumulative_bytes = 0;
  num_outstanding_blocks.set(0);
  packet_bytes = NULL;
  unique_address = 0;

  resolution->node.prec_dims = resolution->precinct_partition;
  resolution->node.prec_dims.pos.x +=
    pos_idx.x * resolution->node.prec_dims.size.x;
  resolution->node.prec_dims.pos.y +=
    pos_idx.y * resolution->node.prec_dims.size.y;
  resolution->node.prec_dims &= resolution->node.dims;
  assert((resolution->node.prec_dims.size.x > 0) &&
         (resolution->node.prec_dims.size.y > 0));

  bool nothing_visible = // True if current view excludes this resolution
    (codestream->persistent && !tile->is_open) ||
    (resolution->res_level > comp->apparent_dwt_levels) ||
    (!comp->enabled);
  
  // Initialize the precinct-bands and, if necessary, `packet_bytes' array
  int mem_offset = sizeof(kd_precinct);
  mem_offset += (-mem_offset) & 7; // Align on 8-byte boundary
  kdu_byte *mem_block = ((kdu_byte *) this) + mem_offset;
  if (codestream->out != NULL)
    { // Note: the precinct server leaves sufficient space for one 8-byte
      // packet length value for each layer in the tile.  It is assumed that
      // `kdu_long' is no longer than 8 bytes.  If it is smaller than 8 bytes
      // some small amount of space might be wasted, but this is no big deal.
      packet_bytes = (kdu_long *) mem_block;
      mem_block += tile->num_layers << 3;
    }
  subbands = (kd_precinct_band *) mem_block;
  mem_offset = resolution->num_subbands * sizeof(kd_precinct_band);
  mem_offset += (-mem_offset) & 7;
  mem_block += mem_offset;

  int b;
  for (b=0; b < resolution->num_intermediate_nodes; b++)
    {
      kd_node *node = resolution->intermediate_nodes + b;
      node->prec_dims =
        get_child_dims(node->parent->prec_dims,node->branch_x,node->branch_y);
    }

  int nocontrib_flag=KD_PFLAG_NO_CONTRIB; // -> 0 iff any outstanding blocks
  for (b=0; b < resolution->num_subbands; b++)
    {
      kd_precinct_band *pb = subbands + b;
      kd_subband *subband = resolution->subbands + b;
      pb->subband = subband;
      kdu_dims prec_dims =
        get_child_dims(subband->parent->prec_dims,
                       subband->branch_x,subband->branch_y);
      pb->block_indices =
        get_partition_indices(subband->block_partition,prec_dims);
      pb->blocks = kd_block::build_tree(pb->block_indices.size,mem_block);

      /* Finally, scan through the leaf nodes of the `blocks' array setting
         up the coding modes and determining whether or not each block
         actually belongs to the current region of interest. */

      // Get location and size of first code-block in the partition which
      // intersects with current precinct-band.
      kdu_dims block_dims = subband->block_partition;
      block_dims.pos.x += block_dims.size.x * pb->block_indices.pos.x;
      block_dims.pos.y += block_dims.size.y * pb->block_indices.pos.y;
      kdu_coords min = block_dims.pos; // So we can keep coming back here.
      kd_block *block = pb->blocks;
      int x, y;
      if (codestream->in == NULL) // Used for output or interchange
        { // All blocks marked as outstanding; none can be discarded
          for (y=0, block_dims.pos.y=min.y;
               y < pb->block_indices.size.y;
               y++, block_dims.pos.y += block_dims.size.y)
            for (x=0, block_dims.pos.x=min.x;
                 x < pb->block_indices.size.x;
                 x++, block_dims.pos.x += block_dims.size.x, block++)
              { 
                block->set_modes(resolution->tile_comp->modes);
                num_outstanding_blocks.get_add(1);
                nocontrib_flag = 0;
              }
        }
      else
        { // For input codestreams only (not interchange or output codestreams)
          for (y=0, block_dims.pos.y=min.y;
               y < pb->block_indices.size.y;
               y++, block_dims.pos.y += block_dims.size.y)
            for (x=0, block_dims.pos.x=min.x;
                 x < pb->block_indices.size.x;
                 x++, block_dims.pos.x += block_dims.size.x, block++)
              {
                block->set_modes(resolution->tile_comp->modes);
                if (nothing_visible || !block_dims.intersects(subband->region))
                  {
                    if (!codestream->persistent)
                      block->set_discard();
                  }
                else
                  { 
                    num_outstanding_blocks.get_add(1);
                    nocontrib_flag = 0;
                  }
              }
        }
    }
  flags |= nocontrib_flag; // Marks blocks that make no contribution
  if ((num_outstanding_blocks.get() == 0) && (codestream->in != NULL))
    flags |= KD_PFLAG_RELEASED;
                     // Can only happen when the precinct is opened for parsing
                     // and does not lie within the current region of interest.
                     // For this reason, it will not be automatically released
                     // when the containing tile is closed (the tile may not
                     // be open yet) or correctly activated when the tile is
                     // opened.  By marking it as released here, we ensure that
                     // `kd_precinct_ref::open' calls `kd_precinct::activate'
                     // when the precinct is first opened for actually
                     // retrieving code-block data.
  if (tile->empty_shell)
    num_packets_read = -1; // Prevents any attempt to read compressed data
}

/*****************************************************************************/
/*                           kd_precinct::closing                            */
/*****************************************************************************/

void
  kd_precinct::closing(kdu_thread_env *env)
{
  assert(ref == NULL); // Safety check to ensure that this function is
                       // called only from within `kd_precinct_ref::close'.
  kd_codestream *cs = resolution->codestream;
  kd_buf_server *buf_server =
    (env == NULL)?cs->buf_servers:cs->get_thread_buf_server(env);
  for (int b=0; b < resolution->num_subbands; b++)
    {
      kd_precinct_band *pb = subbands+b;
      if (pb->blocks != NULL)
        {
          int num_blocks = (int) pb->block_indices.area();
          for (int n=0; n < num_blocks; n++)
            pb->blocks[n].cleanup(buf_server);
          pb->blocks = NULL; // Note that there is no deletion since the
            // `blocks' array is part of the same memory block as `kd_precinct'
        }
    }
}

/*****************************************************************************/
/*                      kd_precinct::reset_packet_reading                    */
/*****************************************************************************/

void
  kd_precinct::reset_packet_reading()
{
  assert((flags & KD_PFLAG_ADDRESSABLE) && (ref != NULL) &&
         (num_packets_read > 0));
  kd_buf_server *buf_server = resolution->codestream->buf_servers;
  for (int b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pb = subbands+b;
      if (pb->blocks != NULL)
        kd_block::restart_parsing(pb->blocks,pb->block_indices.size,
                                  buf_server);
    }
  this->num_packets_read = 0;
}
  
/*****************************************************************************/
/*                            kd_precinct::activate                          */
/*****************************************************************************/

void
  kd_precinct::activate()
{ // Always called in the "glock" context
  kd_tile_comp *comp = resolution->tile_comp;
  kd_tile *tile = comp->tile;

  assert((flags & KD_PFLAG_RELEASED) &&
         (num_outstanding_blocks.get() == 0) && tile->is_open);
  required_layers = tile->num_apparent_layers;
  
  if ((resolution->res_level > comp->apparent_dwt_levels) || !comp->enabled)
    { // No code-blocks from the precinct lie in the region of interest
      flags |= KD_PFLAG_NO_CONTRIB;
      return;
    }
  
  int n_blocks = 0;
  for (int b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pb = subbands+b;
      kd_subband *subband = resolution->subbands+b;

      // Get location and size of first code-block in the partition which
      // intersects with current precinct-band.
      kdu_dims block_dims = subband->block_partition;
      block_dims.pos.x += block_dims.size.x * pb->block_indices.pos.x;
      block_dims.pos.y += block_dims.size.y * pb->block_indices.pos.y;
      kdu_coords min = block_dims.pos; // So we can keep coming back here.
      kd_block *block = pb->blocks;
      int x, y;
      for (y=0, block_dims.pos.y=min.y;
           y < pb->block_indices.size.y;
           y++, block_dims.pos.y += block_dims.size.y)
        for (x=0, block_dims.pos.x=min.x;
             x < pb->block_indices.size.x;
             x++, block_dims.pos.x += block_dims.size.x, block++)
          if (block_dims.intersects(subband->region))
            n_blocks++;
    }
  if (tile->codestream->thread_context == NULL)
    num_outstanding_blocks.set(n_blocks);
  else
    num_outstanding_blocks.exchange(n_blocks); // Forces this operation to
               // take place prior to resetting the KD_PFLAG_RELEASED
               // flag, so that `kd_precinct_ref::active_deref' will not
               // succeed before we have had a chance to set the number of
               // outstanding code-blocks.
  flags &= ~(KD_PFLAG_RELEASED | KD_PFLAG_NO_CONTRIB);
  if (n_blocks == 0)
    flags |= KD_PFLAG_NO_CONTRIB; // Unusual here
}

/*****************************************************************************/
/*                      kd_precinct::load_required_packets                   */
/*****************************************************************************/

void kd_precinct::load_required_packets()
{ // Always called in the "glock" context
  if (flags & KD_PFLAG_LOADED_LOCKED)
    return; // Either all required packets have been read or the data source
            // has become exhausted.
  
  kd_tile *tile = resolution->tile_comp->tile;
  kd_codestream *codestream = tile->codestream;
  if (!codestream->cached_source)
    { 
      while ((!tile->exhausted) && (next_layer_idx < required_layers))
        { 
          if ((tile != codestream->active_tile) &&
              !tile->read_tile_part_header())
            { 
              assert(!tile->closed); // Otherwise, we could delete ourself.
              tile->finished_reading();
              break; // Can't read any more information for this tile.
            }
          kd_resolution *seq_res;
          kdu_coords seq_idx;
          kd_precinct_ref *seq_ref =
            tile->sequencer->next_in_sequence(seq_res,seq_idx);
          if ((seq_ref == NULL) ||
              !(seq_ref->is_desequenced() ||
                seq_ref->open(seq_res,seq_idx,
                              false,NULL)->desequence_packet()))
            tile->read_tile_part_header();
        }
      if ((num_packets_read == 0) && (codestream->active_tile != NULL) &&
          !codestream->active_tile->is_addressable)
        { // Strange situation in which `precinct' has a seek address, but
          // the `active_tile' does not have seek addresses; must fully
          // desequence it before doing any seeking.
          kd_tile *active=codestream->active_tile;  assert(active != tile);
          desequence_packets_until_tile_inactive(active,codestream);
        }
    }
  
  if ((flags & KD_PFLAG_ADDRESSABLE) && (unique_address != 0) &&
      (num_packets_read < required_layers) &&
      !(flags & KD_PFLAG_LOAD_TRUNCATED))
    { 
      int old_packets_read = num_packets_read;
      if (old_packets_read >= 0)
        { 
          if (old_packets_read > 0)
            reset_packet_reading();
          codestream->in->seek(unique_address);
          while (num_packets_read < required_layers)
            if (!read_packet(old_packets_read))
              break;
        }
    }

  flags |= KD_PFLAG_LOADED_LOCKED;
}

/*****************************************************************************/
/*                           kd_precinct::read_packet                        */
/*****************************************************************************/

bool
  kd_precinct::read_packet(int num_prior_packets)
  /* For the purpose of error resilience, we can focus on just this one
     function, since an error resilient code-stream should probably not
     contain any more than one tile-part and one tile.  Otherwise, we
     would have to deal with the problem of specially protecting marker
     codes which may appear in tile-part headers. */
{
  assert((num_packets_read >= required_layers) ||
         !(flags & KD_PFLAG_LOADED_LOCKED));
  if (flags & KD_PFLAG_LOAD_TRUNCATED)
    return false;
  if (num_packets_read < 0) 
    { 
      assert(flags & KD_PFLAG_ADDRESSABLE);
      return false;
    }

  kd_codestream *codestream = resolution->codestream;
  kd_tile_comp *comp = resolution->tile_comp;
  kd_tile *tile = comp->tile;
  assert(num_packets_read < tile->num_layers);
  assert((flags & KD_PFLAG_ADDRESSABLE) || (tile == codestream->active_tile));

  bool use_sop = resolution->tile_comp->tile->use_sop;
  bool use_eph = resolution->tile_comp->tile->use_eph;
  bool tpart_ends = false;
  bool seek_marker =
    use_sop && codestream->expect_ubiquitous_sops && codestream->resilient;
  int header_bytes = 0;
  
  if ((flags & KD_PFLAG_CORRUPTED) ||
      (tile->skipping_to_sop &&
       (compare_sop_num(tile->next_sop_sequence_num,
                        tile->next_input_packet_num) != 0)))
    return handle_corrupt_packet();
  if (tile->skipping_to_sop)
    { // We have already read the SOP marker for this packet.
      assert(compare_sop_num(tile->next_sop_sequence_num,
                             tile->next_input_packet_num) == 0);
      tile->skipping_to_sop = false;
      header_bytes += 6; // Count the SOP marker
    }
  else
    while (codestream->marker->read(true,seek_marker))
      {
        if (codestream->marker->get_code() == KDU_SOT)
          { tpart_ends = true; break; }
        else if (use_sop &&
                 (codestream->marker->get_code() == KDU_SOP))
          { // Found the required SOP segment
            if (!(flags & KD_PFLAG_ADDRESSABLE))
              { // Verify SOP number (possible only when reading sequentially)
                header_bytes += 6; // Count the SOP marker
                int sequence_num = codestream->marker->get_bytes()[0];
                sequence_num <<= 8;
                sequence_num += codestream->marker->get_bytes()[1];
                if (compare_sop_num(sequence_num,
                                    tile->next_input_packet_num) != 0)
                  {
                    if (!codestream->resilient)
                      { KDU_ERROR(e,25);
                        e << KDU_TXT("Out-of-sequence SOP marker found while "
                             "attempting to read a packet from the "
                             "code-stream!\n");
                        e << KDU_TXT("\tFound sequence number ")
                          << sequence_num
                          << KDU_TXT(", but expected ")
                          << (tile->next_input_packet_num & 0x0000FFFF)
                          << ".\n";
                        e << KDU_TXT("Use the resilient option if you would "
                             "like to try to recover from this error.");
                      }
                    else
                      {
                        tile->skipping_to_sop = true;
                        tile->next_sop_sequence_num = sequence_num;
                        return handle_corrupt_packet();
                      }
                  }
              }
            break;
          }
        else if (!seek_marker) // If `seek_marker' is true we will loop back
          { 
            if (!codestream->resilient)
              { KDU_ERROR(e,26);
                e << KDU_TXT("Illegal marker code found while attempting to "
                     "read a packet from the code-stream!\n");
                e << KDU_TXT("\tIllegal marker code is ");
                codestream->marker->print_current_code(e); e << ".\n";
                e << KDU_TXT("Use the resilient option if you would like to "
                     "try to recover from this error.");
              }
            return handle_corrupt_packet();
          }
      } // End of marker seeking loop.

  if (codestream->in->failed())
    { 
      if (!codestream->cached_source)
        { 
          flags |= KD_PFLAG_LOAD_TRUNCATED;
          tile->finished_reading();
        }
      if ((num_packets_read == 0) && (flags & KD_PFLAG_ADDRESSABLE))
        num_packets_read = -1; // Avoid coming back here time and time again
                       // for addressable precincts wtih all packets missing
      return false;
    }

  if (tpart_ends)
    { 
      codestream->active_tile = NULL;
      tile->adjust_unloadability();
      return false;
    }

  // Now read the packet header.
  bool suspend = codestream->simulate_parsing_while_counting_bytes &&
    ((num_packets_read >= required_layers) || !(flags & KD_PFLAG_RELEVANT));
  if (suspend)
    codestream->in->set_suspend(true);

  kd_input *header_source = tile->packed_headers;
  if (header_source == NULL)
    header_source = codestream->in;
  kd_header_in header(header_source);
  kdu_long body_bytes = 0;
  if (codestream->resilient)
    header_source->enable_marker_throwing();
  else if (codestream->fussy)
    header_source->enable_marker_throwing(true);
  try {
      if (header.get_bit() != 0)
        for (int b=0; b < resolution->num_subbands; b++)
          {
            kd_precinct_band *pband = subbands + b;
            kd_block *block = pband->blocks;
            for (int m=pband->block_indices.size.y; m > 0; m--)
              for (int n=pband->block_indices.size.x; n > 0; n--, block++)
                body_bytes +=
                  block->parse_packet_header(header,codestream->buf_servers,
                                             num_packets_read);
          }
      header_bytes += header.finish(); // Clears any terminal FF
    }
  catch (kd_header_in *)
    { // Insufficient header bytes in source.
      if (header_source == tile->packed_headers)
        {
          assert(tile->packed_headers->failed());
          KDU_ERROR(e,27); e <<
            KDU_TXT("Exhausted PPM/PPT marker segment data while "
            "attempting to parse a packet header!");
        }
      assert(codestream->in->failed());
      if (!codestream->cached_source)
        { 
          flags |= KD_PFLAG_LOAD_TRUNCATED;
          tile->finished_reading();
        }
      if ((num_packets_read == 0) && (flags & KD_PFLAG_ADDRESSABLE))
        num_packets_read = -1; // Saves coming back here again for addressable
                               // precincts with all packets missing
      if (suspend)
        codestream->in->set_suspend(false);
      return false;
    }
  catch (kdu_uint16 code)
    {
      if (codestream->resilient && (header_source == codestream->in))
        { // Assume that all exceptions arise as a result of corruption.
          if (suspend)
            codestream->in->set_suspend(false);
          return handle_corrupt_packet();
        }
      else if (code == KDU_EXCEPTION_PRECISION)
        { KDU_ERROR(e,28); e <<
            KDU_TXT("Packet header contains a representation which is "
            "not strictly illegal, but unreasonably large so that it exceeds "
            "the dynamic range available for our internal representation!  "
            "The problem is most likely due to a corrupt or incorrectly "
            "constructed code-stream.  Try re-opening the image with the "
            "resilient mode enabled.");
        }
      else if (code == KDU_EXCEPTION_ILLEGAL_LAYER)
        { KDU_ERROR(e,29); e <<
            KDU_TXT("Illegal inclusion tag tree encountered while decoding "
            "a packet header.  This problem can arise if empty packets are "
            "used (i.e., packets whose first header bit is 0) and the "
            "value coded by the inclusion tag tree in a subsequent packet "
            "is not exactly equal to the index of the quality layer in which "
            "each code-block makes its first contribution.  Such an "
            "error may arise from a mis-interpretation of the standard.  "
            "The problem may also occur as a result of a corrupted "
            "code-stream.  Try re-opening the image with the resilient mode "
            "enabled.");
        }
      else if (code == KDU_EXCEPTION_ILLEGAL_MISSING_MSBS)
        { KDU_ERROR(e,30); e <<
            KDU_TXT("Illegal number of missing MSB's signalled in corrupted "
            "tag tree.  The value may not exceed 74 in any practical "
            "code-stream or any legal code-stream which is "
            "consistent with profile 0 or profile 1.  Try re-opening the "
            "image with the resilient mode enabled.");
        }
      else
        { KDU_ERROR(e,31); e << KDU_TXT("Illegal marker code, ");
          print_marker_code(code,e); e <<
            KDU_TXT(", found while reading packet header.  Try re-opening the "
            "image with the resilient mode enabled.");
        }
    }
  if (!header_source->disable_marker_throwing())
    {
      assert(0); // If this happens, `header.finish()' did not execute properly
    }

  // Next, read any required EPH marker.

  if (use_eph)
    {
      kdu_byte byte;
      kdu_uint16 code;

      code = 0;
      if (header_source->get(byte))
        code = byte;
      if (header_source->get(byte))
        code = (code<<8) + byte;
      if (header_source->failed())
        { 
          if (header_source == tile->packed_headers)
            {
              assert(tile->packed_headers->failed());
              KDU_ERROR(e,32); e <<
                KDU_TXT("Exhausted PPM/PPT marker segment data while "
                "attempting to parse a packet header!");
            }
          assert(codestream->in->failed());
          if (!codestream->cached_source)
            { 
              flags |= KD_PFLAG_LOAD_TRUNCATED;
              tile->finished_reading();
            }
          if ((num_packets_read == 0) && (flags & KD_PFLAG_ADDRESSABLE))
            num_packets_read = -1; // Saves coming back here again for
                      // addressable precincts with all packets missing
          if (suspend)
            codestream->in->set_suspend(false);
          return false;
        }
      if (code != KDU_EPH)
        {
          if (codestream->resilient && (header_source == codestream->in))
            { // Missing EPH is a clear indicator of corruption.
              if (suspend)
                codestream->in->set_suspend(false);
              return handle_corrupt_packet();
            }
          else
            { KDU_ERROR(e,33); e <<
                KDU_TXT("Expected to find EPH marker following packet "
                "header.  Found ");
              print_marker_code(code,e);
              e << KDU_TXT(" instead.");
            }
        }
      header_bytes += 2; // Count the EPH marker
    }

  // Finally, read the body bytes.

  if (body_bytes > 0)
    {
      if (codestream->resilient)
        codestream->in->enable_marker_throwing();
      else if (codestream->fussy)
        codestream->in->enable_marker_throwing(true);
      try {
          for (int b=0; b < resolution->num_subbands; b++)
            {
              kd_precinct_band *pband = subbands + b;
              kd_block *block = pband->blocks;
              for (int m=pband->block_indices.size.y; m > 0; m--)
                for (int n=pband->block_indices.size.x; n > 0; n--, block++)
                  block->read_body_bytes(codestream->in,
                                         codestream->buf_servers,
                                         codestream->in_memory_source);
            }
        }
      catch (kdu_uint16 code)
        {
          if (codestream->resilient)
            { // We have run into the next SOP or SOT marker.
              if (suspend)
                codestream->in->set_suspend(false);
              return handle_corrupt_packet();
            }
          else
            { KDU_ERROR(e,34); e <<
              KDU_TXT("Illegal marker code, ");
              print_marker_code(code,e); e <<
              KDU_TXT(", found while reading packet body.  Try re-opening "
                "the image with the resilient mode enabled.");
            }
        }
    }
  if (!codestream->in->disable_marker_throwing())
    {
      if (codestream->resilient)
        { // We probably just ran into the 1'st byte of the next SOP/SOT marker
          codestream->in->putback((kdu_byte) 0xFF);
          if (suspend)
            codestream->in->set_suspend(false);
          return handle_corrupt_packet();
        }
      else
        { KDU_ERROR(e,35); e <<
            KDU_TXT("Packet body terminated with an FF!");
        }
    }
  
  if ((!(flags & KD_PFLAG_WAS_READ)) && (comp->layer_stats != NULL) &&
      (num_packets_read >= num_prior_packets))
    { // Accumulate packet length information in layer statistics
      int entry_idx = (resolution->res_level * tile->num_layers +
                       num_packets_read) << 1;
      comp->layer_stats[entry_idx]++;
      comp->layer_stats[entry_idx+1] += body_bytes+header_bytes;
    }
  num_packets_read++;
  if (suspend)
    codestream->in->set_suspend(false);
  return true;
}

/*****************************************************************************/
/*                   kd_precinct::handle_corrupt_packet                      */
/*****************************************************************************/

bool
  kd_precinct::handle_corrupt_packet()
{
  if (flags & KD_PFLAG_ADDRESSABLE)
    { KDU_ERROR(e,36); e <<
        KDU_TXT("Encountered a corrupted packet while using "
        "packet length information to access the compressed data source in a "
        "random access fashion.  To process corrupted code-streams in an "
        "error resilient manner, you must disable seeking on the compressed "
        "data source (i.e., force sequential access) as well as enabling the "
        "resilient parsing mode.");
    }

  kd_tile *tile = resolution->tile_comp->tile;
  kd_codestream *codestream = tile->codestream;
  bool expect_large_gap = !codestream->expect_ubiquitous_sops;
      // It is reasonable to expect large (unbounded) gaps between valid
      // SOP marker sequence numbers if SOP markers are not known to be
      // in front of every packet.
  bool confirm_large_gap = expect_large_gap;

  flags |= KD_PFLAG_CORRUPTED;
    // corrupted = true;
  do { // Read up to next valid SOP marker, if this has not already been done.
      if (!tile->skipping_to_sop)
        { // Need to read next SOP marker.
          do {
              if (!codestream->marker->read(true,true))
                { // Must have exhausted the code-stream.
                  assert(codestream->in->failed());
                  tile->finished_reading();
                  return false;
                }
              if (codestream->marker->get_code() == KDU_SOT)
                { // We have encountered the end of the current tile-part.
                  codestream->active_tile = NULL;
                  tile->adjust_unloadability();
                  return false;
                }
            } while (codestream->marker->get_code() != KDU_SOP);
          tile->next_sop_sequence_num = codestream->marker->get_bytes()[0];
          tile->next_sop_sequence_num<<=8;
          tile->next_sop_sequence_num += codestream->marker->get_bytes()[1];
          tile->skipping_to_sop = true;
        }
      assert(tile->skipping_to_sop);
      if (compare_sop_num(tile->next_sop_sequence_num,
                          tile->next_input_packet_num) <= 0)
        { // If equal, the SOP marker must be for a previously corrupt precinct
          tile->skipping_to_sop = false; // Force hunt for another SOP marker.
          confirm_large_gap = expect_large_gap;
        }
      else if ((compare_sop_num(tile->next_sop_sequence_num,
                                tile->next_input_packet_num) > 3) &&
               !confirm_large_gap)
        { // Unwilling to skip so many packets without confirmation that
          // the SOP sequence number itself has not been corrupted.
          tile->skipping_to_sop = false; // Skip another one to make sure.
          confirm_large_gap = true;
        }
      else if (compare_sop_num(tile->next_sop_sequence_num,
                               (int)(tile->total_precincts*tile->num_layers))
                               >= 0)
        { // Assume that the SOP marker itself has a corrupt sequence number
          tile->skipping_to_sop = false; // Force hunt for another SOP marker.
          confirm_large_gap = expect_large_gap;
        }
    } while (!tile->skipping_to_sop);

  // Update state to indicate a transferred packet.
  assert(num_packets_read < tile->num_layers);
  num_packets_read++;
  return true;
}

/*****************************************************************************/
/*                       kd_precinct::simulate_packet                        */
/*****************************************************************************/

kdu_long
  kd_precinct::simulate_packet(kdu_long &header_bytes, int layer_idx,
                               kdu_uint16 slope_threshold, bool finalize_layer,
                               bool last_layer, kdu_long max_bytes,
                               bool trim_to_limit)
{
  kd_tile *tile = resolution->tile_comp->tile;
  assert(required_layers == tile->num_layers);
  kd_buf_server *buf_server = resolution->codestream->buf_servers;
  assert(layer_idx < required_layers);
  if (this->num_outstanding_blocks.get() != 0)
    { KDU_ERROR_DEV(e,37); e <<
        KDU_TXT("You may not currently flush compressed "
        "code-stream data without completing the compression "
        "of all code-blocks in all precincts of all tiles."); }
  assert(packet_bytes != NULL);
  if (layer_idx == 0)
    { // Assign each packet the smallest legal empty packet size by default.
      for (int n=0; n < required_layers; n++)
        packet_bytes[n] = (tile->use_eph)?3:1;
    }
  packet_bytes[layer_idx] = 0; // Mark packet so we can catch any failure later

  if (trim_to_limit)
    { /* To make the code below work efficiently, we need to first trim away
         all coding passes which have slopes <= `slope_threshold'.  This
         only leaves coding passes which have slopes equal to `slope_threshold'
         to consider for further discarding. */
      assert(last_layer && finalize_layer);
      for (int b=0; b < resolution->num_subbands; b++)
        {
          kd_precinct_band *pband = subbands + b;
          int num_blocks = (int) pband->block_indices.area();
          for (int n=0; n < num_blocks; n++)
            pband->blocks[n].trim_data(slope_threshold,buf_server);
        }
    }

  kdu_long empty_packet_bytes = 0;
  if (last_layer)
    for (int i=layer_idx+1; i < required_layers; i++)
      empty_packet_bytes += packet_bytes[i];
  
  int last_trimmed_subband = resolution->num_subbands;
  int last_trimmed_block = 0;
  kdu_long body_bytes;
  do { // Iterates only if we are trimming blocks to meet a `max_bytes' target
      body_bytes = 0;
      header_bytes = empty_packet_bytes+1;
      if (tile->use_sop)
        header_bytes += 6;
      if (tile->use_eph)
        header_bytes += 2;
      // Run the packet start functions for this packet
      int b;
      for (b=0; b < resolution->num_subbands; b++)
        {
          kd_precinct_band *pband = subbands + b;
          if (layer_idx == 0)
            kd_block::reset_output_tree(pband->blocks,
                                        pband->block_indices.size);
          else
            kd_block::restore_output_tree(pband->blocks,
                                          pband->block_indices.size);
          int num_blocks = (int) pband->block_indices.area();
          for (int n=0; n < num_blocks; n++)
            body_bytes +=
              pband->blocks[n].start_packet(layer_idx,slope_threshold);
          if ((body_bytes+header_bytes) > max_bytes)
            {
              if (!finalize_layer)
                return body_bytes+header_bytes;
              else
                assert(trim_to_limit);
            }
        }

      // Simulate header construction for this packet
      kd_header_out head(NULL);
      head.put_bit(1); // Packet not empty
      for (b=0; b < resolution->num_subbands; b++)
        {
          kd_precinct_band *pband = subbands + b;
          int num_blocks = (int) pband->block_indices.area();
          for (int n=0; n < num_blocks; n++)
            pband->blocks[n].write_packet_header(head,layer_idx,true);
        }
      header_bytes += head.finish() - 1; // Already counted the 1'st byte above

      if ((body_bytes+header_bytes) > max_bytes)
        {
          if (!finalize_layer)
            return body_bytes+header_bytes;
          assert(trim_to_limit);

          // If we get here, we need to trim away some code-block contributions

          bool something_discarded = false;
          while (!something_discarded)
            {
              kd_precinct_band *pband = subbands + last_trimmed_subband;
              if (last_trimmed_block == 0)
                {
                  last_trimmed_subband--; pband--;
                  assert(last_trimmed_subband >= 0);
                    // Otherwise, the function is being used incorrectly.
                  last_trimmed_block = (int) pband->block_indices.area();
                  continue; // In case precinct-band contains no code-blocks
                }
              last_trimmed_block--;
              kd_block *block = pband->blocks + last_trimmed_block;
              something_discarded =
                block->trim_data(slope_threshold+1,buf_server);
            }
        }
      else if (finalize_layer)
        { // Save the state information in preparation for output
          for (b=0; b < resolution->num_subbands; b++)
            {
              kd_precinct_band *pband = subbands + b;
              kd_block::save_output_tree(pband->blocks,
                                         pband->block_indices.size);
            }
        }
    } while ((body_bytes+header_bytes) > max_bytes);

  // Record packet length information.
  packet_bytes[layer_idx] = body_bytes + header_bytes - empty_packet_bytes;
  return body_bytes + header_bytes;
}

/*****************************************************************************/
/*                      kd_precinct::simulate_packet_cbr                     */
/*****************************************************************************/

kdu_long
  kd_precinct::simulate_packet_cbr(kdu_long &header_bytes,
                                   kdu_uint16 slope_threshold,
                                   kdu_uint16 &mod_threshold,
                                   kdu_uint16 &next_slope_val,
                                   kdu_long &bulk_fill_bytes,
                                   int &fine_fill_bytes)
{
  resolution->codestream->cbr_fset.num_precinct_simulations++;
  
  kd_tile *tile = resolution->tile_comp->tile;
  assert((required_layers == 1) && (tile->num_layers == 1));
  if (this->num_outstanding_blocks.get() != 0)
    { KDU_ERROR_DEV(e,0x01081601); e <<
        KDU_TXT("You may not currently flush compressed "
        "code-stream data without completing the compression "
        "of all code-blocks in all precincts of all tiles."); }
  assert(packet_bytes != NULL);

  int n;
  packet_bytes[0] = 0;  
  kdu_long body_bytes = 0;
  header_bytes = 1; // We need at least 1 packet header byte
  if (tile->use_sop)
    header_bytes += 6;
  if (tile->use_eph)
    header_bytes += 2;

  // Figure out which code-blocks should receive any fill bytes
  int b;
  assert((bulk_fill_bytes >= 0) && (fine_fill_bytes >= 0) &&
         !(bulk_fill_bytes & 1));
  kd_block *fine_fill_block = NULL;
  bool any_fill = false;
  if (fine_fill_bytes > 0)
    { 
      any_fill = true;
      for (b=resolution->num_subbands-1; b >= 0; b--)
        { 
          kd_precinct_band *pband = subbands + b;
          int num_blocks=(int)pband->block_indices.area();
          if (num_blocks > 0)
            { 
              fine_fill_block = pband->blocks + (num_blocks-1);
              break;
            }
        }
    }
  else if (bulk_fill_bytes > 0)
    any_fill = true;
  
  // Run the packet start functions
  kdu_long temp_fill_bytes = bulk_fill_bytes;
  kdu_uint16 max_nxt_slope=0, min_mod_thresh=0xFFFF;
  for (b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pband = subbands + b;
      kd_block::reset_output_tree(pband->blocks,pband->block_indices.size);
      int num_blocks = (int) pband->block_indices.area();
      for (n=0; n < num_blocks; n++)
        { 
          kd_block *block = pband->blocks + n;
          kdu_uint16 mod_thresh, nxt_slope;
          bool block_wants_fill = false;
          if (any_fill)
            { 
              if (block == fine_fill_block)
                block_wants_fill = true;
              else if (temp_fill_bytes > 0)
                { block_wants_fill = true; temp_fill_bytes -= 8192; }
            }
          body_bytes +=
            block->start_packet_cbr(slope_threshold,mod_thresh,nxt_slope,
                                    block_wants_fill);
          if (nxt_slope > max_nxt_slope)
            max_nxt_slope = nxt_slope;
          if (mod_thresh < min_mod_thresh)
            min_mod_thresh = mod_thresh;
        }
    }

  // Simulate header construction for this packet
  temp_fill_bytes = bulk_fill_bytes;
  kd_header_out head(NULL);
  head.put_bit(1); // Packet not empty
  kdu_long total_fill_bytes = 0;
  for (b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pband = subbands + b;
      int num_blocks = (int) pband->block_indices.area();
      for (n=0; n < num_blocks; n++)
        { 
          kd_block *block = pband->blocks + n;
          if (!any_fill)
            block->write_packet_header_cbr(head,0,true);
          else if (block == fine_fill_block)
            { 
              fine_fill_bytes =
                block->write_packet_header_cbr(head,fine_fill_bytes,true);
              total_fill_bytes += fine_fill_bytes;
            }
          else
            { 
              int fb = (int)temp_fill_bytes;
              if (temp_fill_bytes > 8192)
                fb = 8192;
              temp_fill_bytes -= fb;
              if (block->write_packet_header_cbr(head,fb,true) != fb)
                assert(0);
              total_fill_bytes += fb;
            }
        }
    }
  header_bytes += head.finish() - 1; // Already counted the 1'st byte above
  bulk_fill_bytes -= temp_fill_bytes; // Leaves number actually used

  // Return threshold information
  assert(min_mod_thresh >= slope_threshold);
  mod_threshold = min_mod_thresh;
  next_slope_val = max_nxt_slope;
  
  // Record packet length information.
  kdu_long result = body_bytes + header_bytes + total_fill_bytes;
  packet_bytes[0] = result;
  return result;
}

/*****************************************************************************/
/*                         kd_precinct::write_packet                         */
/*****************************************************************************/

kdu_long
  kd_precinct::write_packet(kdu_uint16 threshold, bool empty_packet,
                            kdu_long &header_bytes)
{
  int b, n, num_blocks;
  kd_tile *tile = resolution->tile_comp->tile;
  kdu_output *out = resolution->codestream->out;

  header_bytes = 0;
  kdu_long body_bytes = 0;
  if (!empty_packet)
    { // Start the packet
      for (b=0; b < resolution->num_subbands; b++)
        {
          kd_precinct_band *pband = subbands + b;
          if (next_layer_idx == 0)
            kd_block::reset_output_tree(pband->blocks,
                                        pband->block_indices.size);
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            body_bytes +=
              pband->blocks[n].start_packet(next_layer_idx,threshold);
        }
      if (resolution->tile_comp->tile->use_sop)
        { // Generate an SOP marker.
          header_bytes += out->put(KDU_SOP);
          header_bytes += out->put((kdu_uint16) 4);
          header_bytes += out->put((kdu_uint16)
                                   tile->sequenced_relevant_packets);
        }
    }

  kd_header_out head(out);
  if (empty_packet)
    head.put_bit(0); // Empty packet bit
  else
    {
      head.put_bit(1);
      for (b=0; b < resolution->num_subbands; b++)
        {
          kd_precinct_band *pband = subbands + b;
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            pband->blocks[n].write_packet_header(head,next_layer_idx,false);
        }
    }
  header_bytes += head.finish();
  if (resolution->tile_comp->tile->use_eph)
    header_bytes += out->put(KDU_EPH);
  if (!empty_packet)
    for (b=0; b < resolution->num_subbands; b++)
      {
        kd_precinct_band *pband = subbands + b;
        num_blocks = (int) pband->block_indices.area();
        for (n=0; n < num_blocks; n++)
          pband->blocks[n].write_body_bytes(out);
      }

  kdu_long check_bytes = body_bytes + header_bytes;
  assert(check_bytes == packet_bytes[next_layer_idx]); // Simulation correct?
  next_layer_idx++;
  tile->sequenced_relevant_packets++;
  if (next_layer_idx == tile->num_layers)
    resolution->rescomp->close_ready_precinct(this);
  return check_bytes;
}

/*****************************************************************************/
/*                       kd_precinct::write_packet_cbr                       */
/*****************************************************************************/

kdu_long
  kd_precinct::write_packet_cbr(kdu_uint16 threshold, kdu_long &header_bytes,
                                kdu_long &bulk_fill_bytes,
                                int fine_fill_bytes)
{
  int b, n, num_blocks;
  kd_tile *tile = resolution->tile_comp->tile;
  kdu_output *out = resolution->codestream->out;  
  header_bytes = 0;
  kdu_long body_bytes = 0;

  // Figure out which, if any code-block should receive any required fill bytes
  assert((bulk_fill_bytes >= 0) && (fine_fill_bytes >= 0) &&
         !(bulk_fill_bytes & 1));
  kd_block *fine_fill_block = NULL;
  bool any_fill = false;
  if (fine_fill_bytes > 0)
    { 
      any_fill = true;
      for (b=resolution->num_subbands-1; b >= 0; b--)
        { 
          kd_precinct_band *pband = subbands + b;
          int num_blocks=(int)pband->block_indices.area();
          if (num_blocks > 0)
            { 
              fine_fill_block = pband->blocks + (num_blocks-1);
              break;
            }
        }
    }
  else if (bulk_fill_bytes > 0)
    any_fill = true;

  // Start the packet
  assert(next_layer_idx == 0);
  kdu_long temp_fill_bytes = bulk_fill_bytes;
  for (b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pband = subbands + b;
      kd_block::reset_output_tree(pband->blocks,pband->block_indices.size);
      num_blocks = (int) pband->block_indices.area();
      for (n=0; n < num_blocks; n++)
        { 
          kd_block *block = pband->blocks + n;
          kdu_uint16 mod_thresh, nxt_slope; // Don't need these values
          bool block_wants_fill = false;
          if (any_fill)
            { 
              if (block == fine_fill_block)
                block_wants_fill = true;
              else if (temp_fill_bytes > 0)
                { block_wants_fill = true; temp_fill_bytes -= 8192; }
            }
          body_bytes +=
            block->start_packet_cbr(threshold,mod_thresh,nxt_slope,
                                    block_wants_fill);
        }
    }
  if (resolution->tile_comp->tile->use_sop)
    { // Generate an SOP marker.
      header_bytes += out->put(KDU_SOP);
      header_bytes += out->put((kdu_uint16) 4);
      header_bytes += out->put((kdu_uint16)tile->sequenced_relevant_packets);
    }

  // Write the packet header
  temp_fill_bytes = bulk_fill_bytes;
  kd_header_out head(out);
  head.put_bit(1);
  for (b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pband = subbands + b;
      num_blocks = (int) pband->block_indices.area();
      for (n=0; n < num_blocks; n++)
        { 
          kd_block *block = pband->blocks + n;
          if (!any_fill)
            block->write_packet_header_cbr(head,0,false);
          else
            { 
              int fb = 0;
              if (block == fine_fill_block)
                fb = fine_fill_bytes;
              else if (temp_fill_bytes > 0)
                { 
                  fb = (int)temp_fill_bytes;
                  if (temp_fill_bytes > 8192)
                    fb = 8192;
                  temp_fill_bytes -= fb;
                }
              int actual_fill_bytes =
                block->write_packet_header_cbr(head,fb,false);
              if (actual_fill_bytes != fb)
                assert(0);
            }
        }
    }
  header_bytes += head.finish();
  if (resolution->tile_comp->tile->use_eph)
    header_bytes += out->put(KDU_EPH);
  
  // Write the body bytes
  temp_fill_bytes = bulk_fill_bytes;
  kdu_long total_fill_bytes = 0;
  for (b=0; b < resolution->num_subbands; b++)
    { 
      kd_precinct_band *pband = subbands + b;
      num_blocks = (int) pband->block_indices.area();
      for (n=0; n < num_blocks; n++)
        { 
          kd_block *block = pband->blocks + n;
          if (!any_fill)
            block->write_body_bytes_cbr(out,0);
          else
            { 
              int fb = 0;
              if (block == fine_fill_block)
                fb = fine_fill_bytes;
              else
                { 
                  fb = (int)temp_fill_bytes;
                  if (temp_fill_bytes > 8192)
                    fb = 8192;
                  temp_fill_bytes -= fb;
                }
              block->write_body_bytes_cbr(out,fb);
              total_fill_bytes += fb;
            }
        }
    }
  bulk_fill_bytes -= temp_fill_bytes; // Leaves number actually added

  kdu_long check_bytes = body_bytes + header_bytes + total_fill_bytes;
  // The check below is part of the non-cbr version of this function, since
  // the packet_bytes entries may have been used to write PLT information; for
  // this reason, the regular version of pcrd_opt and related functions is
  // careful to ensure that all precincts are simulated one last time, with
  // the final selected slope threshold value.  In the CBR variant, we do not
  // write PLT marker segments and we are also very interested in speed, so
  // we do not bother ensuring that `packet_bytes' information is up-to-date
  // coming out of the simulation phase in `pcrd_opt_cbr'.
  //assert(check_bytes == packet_bytes[0]);
  next_layer_idx = 1;

  // Must not increment `tile->sequenced_relevant_packets' here because we
  // only exercise the packet sequencer once, during `construct_cbr_flush_set';
  // we do not save and restore the packet sequencer between simulating and
  // writing, since we keep a record of the individual precincts in each
  // flush set, generated inside `construct_cbr_flush_set'.
  //tile->sequenced_relevant_packets++;
  assert(tile->num_layers == 1);
  resolution->rescomp->close_ready_precinct(this);
  return check_bytes;
}

/*****************************************************************************/
/*                     kd_precinct::cache_write_packets                      */
/*****************************************************************************/

void
  kd_precinct::cache_write_packets(int max_layers,
                                   kdu_uint16 layer_thresholds[])
{
  assert((flags & KD_PFLAG_READY) != 0);
  kd_resolution *res = this->resolution;
  kd_tile_comp *tc = res->tile_comp;
  kd_tile *tile = tc->tile;
  kd_codestream *codestream = res->codestream;
  kd_compressed_output *out = codestream->out;
  kdu_long unique_id = (kdu_long)(this->ref-res->precinct_refs);
  assert((unique_id >= 0) && (unique_id < (kdu_long)res->num_precincts));
  for (kd_resolution *rp=res-res->res_level; rp != resolution; rp++)
    unique_id += rp->num_precincts;
  unique_id = unique_id*tile->num_components + tc->cnum;
  unique_id = unique_id*codestream->tile_span.x*codestream->tile_span.y;
  unique_id += tile->t_num;
  out->start_precinct(unique_id);
  
  for (next_layer_idx=0; next_layer_idx < tile->num_layers; next_layer_idx++)
    { 
      kdu_long pbytes=0, hbytes=0;
      int b, n, num_blocks;
      bool empty_packet = (next_layer_idx >= max_layers);
      if (!empty_packet)
        { // Start the packet
          kdu_uint16 slope = layer_thresholds[next_layer_idx];
          for (b=0; b < res->num_subbands; b++)
            { 
              kd_precinct_band *pband = subbands + b;
              if (next_layer_idx == 0)
                kd_block::reset_output_tree(pband->blocks,
                                            pband->block_indices.size);
              num_blocks = (int) pband->block_indices.area();
              for (n=0; n < num_blocks; n++)
                pbytes += pband->blocks[n].start_packet(next_layer_idx,slope);
            }
          if (tile->use_sop)
            { // Generate an SOP marker.
              hbytes += out->put(KDU_SOP);
              hbytes += out->put((kdu_uint16) 4);
              hbytes += out->put((kdu_uint16)tile->sequenced_relevant_packets);
            }
        }
      kd_header_out head(out);
      if (empty_packet)
        head.put_bit(0); // Empty packet bit
      else
        { 
          head.put_bit(1);
          for (b=0; b < res->num_subbands; b++)
            { 
              kd_precinct_band *pband = subbands + b;
              num_blocks = (int) pband->block_indices.area();
              for (n=0; n < num_blocks; n++)
                pband->blocks[n].write_packet_header(head,next_layer_idx,
                                                     false);
            }
        }
      hbytes += head.finish();
      if (tile->use_eph)
        hbytes += out->put(KDU_EPH);
      codestream->written_packet_header_bytes += hbytes;
      pbytes += hbytes;
      if (!empty_packet)
        for (b=0; b < res->num_subbands; b++)
          { 
            kd_precinct_band *pband = subbands + b;
            num_blocks = (int) pband->block_indices.area();
            for (n=0; n < num_blocks; n++)
              pband->blocks[n].write_body_bytes(out);
          }
      packet_bytes[next_layer_idx] = pbytes;
      codestream->written_packet_bytes += pbytes;
      if (next_layer_idx < codestream->num_sized_layers)
        codestream->layer_sizes[next_layer_idx] += pbytes;
      else
        codestream->layer_sizes[codestream->num_sized_layers-1] += pbytes;
      tile->sequenced_relevant_packets++;
    }
  
  out->end_precinct(next_layer_idx,packet_bytes);
  res->rescomp->close_ready_precinct(this);
  
  if ((tile->sequenced_relevant_packets == tile->max_relevant_packets) &&
      (tile->next_tpart > 0))
    { // We have finished generating all data for this tile
      assert(tile->next_tpart == 1); // No multi-part tiles for cached targets
      tile->remove_from_in_progress_list();
      if (tile->closed && !codestream->allow_restart)
        tile->release();
    }
}


/* ========================================================================= */
/*                               kdu_precinct                                */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdu_precinct::get_unique_id                         */
/*****************************************************************************/

kdu_long
  kdu_precinct::get_unique_id()
{
  kdu_long result = -(1+state->unique_address);
  assert(result >= 0);
  return result;
}

/*****************************************************************************/
/*                        kdu_precinct::check_loaded                         */
/*****************************************************************************/

bool
  kdu_precinct::check_loaded()
{
  return (state->num_outstanding_blocks.get() == 0);
}

/*****************************************************************************/
/*                      kdu_precinct::get_valid_blocks                       */
/*****************************************************************************/

bool
  kdu_precinct::get_valid_blocks(int band_idx, kdu_dims &indices)
{
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  if (res->res_level > 0)
    band_idx--;
  if ((band_idx < 0) || (band_idx >= res->num_subbands))
    return false;
  if (codestream->transpose)
    band_idx = res->subbands[band_idx].transpose_sequence_idx;
  indices = state->subbands[band_idx].block_indices;
  indices.to_apparent(codestream->transpose,
                      codestream->vflip,codestream->hflip);
  if (!indices)
    return false;
  return true;
}

/*****************************************************************************/
/*                        kdu_precinct::open_block                           */
/*****************************************************************************/

kdu_block *
  kdu_precinct::open_block(int band_idx, kdu_coords block_idx,
                           kdu_thread_env *env)
{ // Note that this function can only be called with codestreams created
  // for interchange.
  kd_resolution *res = state->resolution;
  kd_codestream *codestream = res->codestream;
  if (res->res_level > 0)
    band_idx--;
  assert((band_idx >= 0) && (band_idx < res->num_subbands));
  if (codestream->transpose)
    band_idx = res->subbands[band_idx].transpose_sequence_idx;
  block_idx.from_apparent(codestream->transpose,
                          codestream->vflip,codestream->hflip);

  // Initialize the block structure.
  kd_subband *subband = res->subbands + band_idx;
  kdu_dims band_dims = subband->block_partition;
  band_dims.pos.x += block_idx.x*band_dims.size.x;
  band_dims.pos.y += block_idx.y*band_dims.size.y;
  band_dims &= subband->dims;
  assert(band_dims.area() > 0);

  kd_precinct_band *pband = state->subbands + band_idx;
  block_idx -= pband->block_indices.pos;
  assert((block_idx.x >= 0) && (block_idx.x < pband->block_indices.size.x) &&
         (block_idx.y >= 0) && (block_idx.y < pband->block_indices.size.y));

  kdu_block *result;
  if (env == NULL)
    result = codestream->block;
  else
    result = env->get_block();
  assert(result->precinct == NULL);
  result->precinct = state;
  result->block = pband->blocks +
    block_idx.x + block_idx.y*pband->block_indices.size.x;

  // Set up the common fields (common to input and output codestream objects).

  result->size = band_dims.size;
  result->region.pos = kdu_coords(0,0);
  result->region.size = band_dims.size;
  result->modes = res->tile_comp->modes;
  result->orientation = subband->orientation;
  result->K_max_prime = subband->K_max_prime;

  if (!result->block->empty())
    { KDU_ERROR_DEV(e,38); e <<
        KDU_TXT("Attempting to open the same code-block more than "
        "once for writing!");
    }

  return result;
}

/*****************************************************************************/
/*                       kdu_precinct::close_block                           */
/*****************************************************************************/

void
  kdu_precinct::close_block(kdu_block *result, kdu_thread_env *env)
{ // Note that this function can only be called with codestreams created
  // for interchange.
  kd_codestream *cs = state->resolution->codestream;
  kd_block *block = result->block;
  assert((result->precinct == state) && (block != NULL) && block->empty());
  assert(((env == NULL) && (result == cs->block)) ||
         ((env != NULL) && (result == env->get_block())));
  result->precinct = NULL;

  if (env != NULL)
    { 
      block->store_data(result,cs->get_thread_buf_server(env));
      state->num_outstanding_blocks.exchange_add(-1);
    }
  else
    {
      block->store_data(result,cs->buf_servers);
      state->num_outstanding_blocks.get_add(-1);
    }
}

/*****************************************************************************/
/*                       kdu_precinct::size_packets                          */
/*****************************************************************************/

bool
  kdu_precinct::size_packets(int &cumulative_packets, int &cumulative_bytes,
                             bool &is_significant)
{
  is_significant = false;
  if (state->num_outstanding_blocks.get() > 0)
    return false;
  if (state->flags & KD_PFLAG_GENERATING)
    { // Need to go back to the sizing mode.
      state->cumulative_bytes = 0;
      state->next_layer_idx = 0;
      state->flags &= ~(KD_PFLAG_GENERATING | KD_PFLAG_SIGNIFICANT);
         // state->generating = false;
         // state->is_significant = false;
    }
  assert(state->next_layer_idx <= state->required_layers);
  /* Not necessary; best to include test for `required_layers' in the loop
  if (cumulative_packets > state->required_layers)
    cumulative_packets = state->required_layers;
   */

  kd_resolution *res = state->resolution;
  int layer_bytes, block_bytes;
  for (; (state->next_layer_idx < state->required_layers) &&
         ((state->next_layer_idx < cumulative_packets) ||
          (state->cumulative_bytes < cumulative_bytes));
         state->next_layer_idx++, state->cumulative_bytes += layer_bytes)
    {
      int b, n, num_blocks, layer_idx = state->next_layer_idx;
      kdu_uint16 threshold = 0xFFFF - 1 - (kdu_uint16) layer_idx;

      layer_bytes = (res->tile_comp->tile->use_eph)?2:0;
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          if (layer_idx == 0)
            kd_block::reset_output_tree(pband->blocks,
                                        pband->block_indices.size);
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            {
              block_bytes = pband->blocks[n].start_packet(layer_idx,threshold);
              layer_bytes += block_bytes;
              if (block_bytes > 0)
                state->flags |= KD_PFLAG_SIGNIFICANT;
                  // state->is_significant = true;
            }
        }
      // Simulate header construction for this packet
      kd_header_out head(NULL);
      head.put_bit(1); // Packet not empty.
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            pband->blocks[n].write_packet_header(head,layer_idx,true);
        }
      layer_bytes += head.finish();
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          kd_block::save_output_tree(pband->blocks,pband->block_indices.size);
        }
    }
  cumulative_bytes = state->cumulative_bytes;
  cumulative_packets = state->next_layer_idx;
  is_significant = ((state->flags & KD_PFLAG_SIGNIFICANT) != 0);
  return true;
}

/*****************************************************************************/
/*                       kdu_precinct::get_packets                           */
/*****************************************************************************/

  // Define `kd_dummy_target' class to simply discard the data pushed to
  // its base `kdu_output' object.  This is the simplest way to handle
  // discarding of packets prior to `first_layer_idx'.
  class kd_dummy_target : public kdu_output {
    protected: // Virtual functions which implement required services.
      virtual void flush_buf() { next_buf = buffer; }
    };

bool
  kdu_precinct::get_packets(int leading_skip_packets, int leading_skip_bytes,
                            int &cumulative_packets, int &cumulative_bytes,
                            kdu_output *out)
{
  // Now for the implementation of this function.
  if (state->num_outstanding_blocks.get() > 0)
    return false;
  if (!(state->flags & KD_PFLAG_GENERATING))
    {
      state->cumulative_bytes = 0;
      state->next_layer_idx = 0;
      state->flags |= KD_PFLAG_GENERATING;
      state->flags &= ~KD_PFLAG_SIGNIFICANT;
        // state->generating = true;
        // state->is_significant = false;
    }
  /* Not necessary; best to include the test for `required_layers' in the loop
  if (cumulative_packets > state->required_layers)
    cumulative_packets = state->required_layers;
  */

  kd_resolution *res = state->resolution;
  kd_dummy_target dummy_target; // For discarding layers.
  if (out == NULL)
    out = &dummy_target;
  int layer_bytes, block_bytes;
  for (; (state->next_layer_idx < state->required_layers) &&
         ((state->next_layer_idx < cumulative_packets) ||
          (state->cumulative_bytes < cumulative_bytes));
         state->next_layer_idx++, state->cumulative_bytes += layer_bytes)
    {
      int b, n, num_blocks, layer_idx = state->next_layer_idx;
      kdu_uint16 threshold = 0xFFFF - 1 - (kdu_uint16) layer_idx;
      kdu_output *target = out;
      if ((layer_idx < leading_skip_packets) ||
          (state->cumulative_bytes  < leading_skip_bytes))
        target = &dummy_target; // Discard this packet.

      layer_bytes = 0;
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          if (layer_idx == 0)
            kd_block::reset_output_tree(pband->blocks,
                                        pband->block_indices.size);
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            {
              block_bytes = pband->blocks[n].start_packet(layer_idx,threshold);
              layer_bytes += block_bytes;
              if (block_bytes > 0)
                state->flags |= KD_PFLAG_SIGNIFICANT;
                 // state->is_significant = true;
            }
        }
      // Note that SOP marker segments are never generated by codestream
      // objects created for interchange.  There is nothing illegal about this.

      kd_header_out head(target);
      head.put_bit(1);
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            pband->blocks[n].write_packet_header(head,layer_idx,false);
        }
      layer_bytes += head.finish();
      if (res->tile_comp->tile->use_eph)
        layer_bytes += target->put(KDU_EPH);
      for (b=0; b < res->num_subbands; b++)
        {
          kd_precinct_band *pband = state->subbands + b;
          num_blocks = (int) pband->block_indices.area();
          for (n=0; n < num_blocks; n++)
            pband->blocks[n].write_body_bytes(target);
        }
    }
  cumulative_bytes = state->cumulative_bytes;
  cumulative_packets = state->next_layer_idx;
  return true;
}

/*****************************************************************************/
/*                          kdu_precinct::restart                            */
/*****************************************************************************/

void
  kdu_precinct::restart()
{
  if (state->num_outstanding_blocks.get() > 0)
    return;
  state->flags &= ~(KD_PFLAG_GENERATING | KD_PFLAG_SIGNIFICANT);
    // state->generating = false;
    // state->is_significant = false;
  state->cumulative_bytes = 0;
  state->next_layer_idx = 0;
}

/*****************************************************************************/
/*                          kdu_precinct::close                              */
/*****************************************************************************/

void
  kdu_precinct::close(kdu_thread_env *env)
{
  state->ref->close(env);
  state = NULL; // A safety measure.
}


/* ========================================================================= */
/*                            kd_precinct_server                             */
/* ========================================================================= */

/*****************************************************************************/
/*                   kd_precinct_server::kd_precinct_server                  */
/*****************************************************************************/

kd_precinct_server::kd_precinct_server(kd_codestream *cs,
                                       bool need_packet_bytes_arrays)
{ 
  inactive_head = inactive_tail = NULL;
  this->codestream = cs;
  this->allocate_packet_bytes = need_packet_bytes_arrays;
  glock_size_classes = NULL;
  glock_allocated_bytes = 0;
  plock_size_classes = NULL;
  plock_surplus_structure_bytes = 0;
  plock_allocated_bytes = 0;
}

/*****************************************************************************/
/*                  kd_precinct_server::~kd_precinct_server                  */
/*****************************************************************************/

kd_precinct_server::~kd_precinct_server()
{
  kd_precinct_size_class *tmp;
  while ((tmp=glock_size_classes) != NULL)
    { 
      glock_size_classes = tmp->next;
      delete tmp;
    }
  while ((tmp=plock_size_classes) != NULL)
    { 
      plock_size_classes = tmp->next;
      delete tmp;
    }
}

/*****************************************************************************/
/*                         kd_precinct_server::get                           */
/*****************************************************************************/

kd_precinct *
  kd_precinct_server::get(int max_blocks, int num_subbands, int max_layers,
                          kdu_thread_env *env)
{
  if (!allocate_packet_bytes)
    max_layers = 0;
  if ((env == NULL) || codestream->check_lock(KD_THREADLOCK_GENERAL,env))
    { // Using the "glock" context
      kd_precinct_size_class *scan=glock_size_classes;
      for (; scan != NULL; scan=scan->next)
        if ((scan->max_blocks == max_blocks) &&
            (scan->num_subbands == num_subbands) &&
            (scan->max_layers == max_layers))
          break;
      if (scan == NULL)
        { 
          scan = new kd_precinct_size_class(max_blocks,num_subbands,
                                            this,max_layers,false);
          scan->next = glock_size_classes;
          glock_size_classes = scan;
        }
      kd_precinct *tmp;
      while (((tmp = inactive_head) != NULL) &&
             codestream->buf_servers[0].cache_threshold_exceeded())
        { 
          assert((tmp->flags & KD_PFLAG_RELEASED) &&
                 (tmp->flags & KD_PFLAG_INACTIVE));
          tmp->ref->close(env);
        }
      return scan->glock_get();
    }
  else if (codestream->check_lock(KD_THREADLOCK_PRECINCT,env))
    { // Using the "plock" context
      kd_precinct_size_class *scan=plock_size_classes;
      for (; scan != NULL; scan=scan->next)
        if ((scan->max_blocks == max_blocks) &&
            (scan->num_subbands == num_subbands) &&
            (scan->max_layers == max_layers))
          break;
      if (scan == NULL)
        { 
          scan = new kd_precinct_size_class(max_blocks,num_subbands,
                                            this,max_layers,true);
          scan->next = plock_size_classes;
          plock_size_classes = scan;
        }
      return scan->plock_get();
    }
  else
    assert(0);
  return NULL;
}


/* ========================================================================= */
/*                         kd_precinct_size_class                            */
/* ========================================================================= */

/*****************************************************************************/
/*             kd_precinct_size_class::augment_glock_free_list               */
/*****************************************************************************/

void
  kd_precinct_size_class::augment_glock_free_list()
{
  assert(!get_uses_plock);
  assert(glock_free_count == 0);
  assert(glock_free_tail == NULL);
  kd_precinct *elt;
  if ((nolock_free_list.get() != NULL) &&
      ((elt = (kd_precinct *)nolock_free_list.exchange(NULL)) != NULL))
    { 
      glock_free_list = glock_free_tail = elt;
      glock_free_count = 1;
      while ((elt = elt->next) != NULL)
        { 
          glock_free_tail = elt;
          glock_free_count++;
        }
    }
  else
    { 
      elt = (kd_precinct *) malloc((size_t) alloc_bytes);
      if (elt == NULL)
        throw std::bad_alloc();
      elt->size_class = this;
      elt->next = NULL;
      glock_free_list = glock_free_tail = elt;
      glock_free_count = 1;
      glock_allocated_precincts++;
      server->glock_allocated_bytes += (kdu_long) alloc_bytes;
    }
}

/*****************************************************************************/
/*             kd_precinct_size_class::augment_plock_free_list               */
/*****************************************************************************/

void
  kd_precinct_size_class::augment_plock_free_list()
{
  assert(get_uses_plock);
  assert(plock_free_count == 0);
  assert(plock_free_tail == NULL);
  kd_precinct *elt;
  if ((nolock_free_list.get() != NULL) &&
      ((elt = (kd_precinct *) nolock_free_list.exchange(NULL)) != NULL))
    { 
      plock_free_list = plock_free_tail = elt;
      plock_free_count = 1;
      while ((elt = elt->next) != NULL)
        { 
          plock_free_tail = elt;
          plock_free_count++;
        }
    }
  else
    { 
      elt = (kd_precinct *) malloc((size_t) alloc_bytes);
      if (elt == NULL)
        throw std::bad_alloc();
      elt->size_class = this;
      elt->next = NULL;
      plock_free_list = plock_free_tail = elt;
      plock_free_count = 1;
      plock_allocated_precincts++;
      server->plock_allocated_bytes += (kdu_long) alloc_bytes;
    }
}

/*****************************************************************************/
/*              kd_precinct_size_class::move_to_inactive_list                */
/*****************************************************************************/

void
  kd_precinct_size_class::move_to_inactive_list(kd_precinct *precinct)
{
  assert((precinct->prev == NULL) && (precinct->next == NULL) &&
         !(precinct->flags & KD_PFLAG_INACTIVE));
  assert(!get_uses_plock);
  precinct->flags |= KD_PFLAG_INACTIVE;
  if ((precinct->prev = server->inactive_tail) == NULL)
    server->inactive_head = server->inactive_tail = precinct;
  else
    server->inactive_tail = server->inactive_tail->next = precinct;
}

/*****************************************************************************/
/*            kd_precinct_size_class::withdraw_from_inactive_list            */
/*****************************************************************************/

void
  kd_precinct_size_class::withdraw_from_inactive_list(kd_precinct *precinct)
{
  assert(precinct->flags & KD_PFLAG_INACTIVE);
  assert(!get_uses_plock);
  if (precinct->prev == NULL)
    {
      assert(precinct == server->inactive_head);
      server->inactive_head = precinct->next;
    }
  else
    precinct->prev->next = precinct->next;
  if (precinct->next == NULL)
    {
      assert(precinct == server->inactive_tail);
      server->inactive_tail = precinct->prev;
    }
  else
    precinct->next->prev = precinct->prev;
  precinct->flags &= ~KD_PFLAG_INACTIVE;
  precinct->prev = precinct->next = NULL;
}


/* ========================================================================= */
/*                              kd_precinct_ref                              */
/* ========================================================================= */

/*****************************************************************************/
/*                   kd_precinct_ref::instantiate_precinct                   */
/*****************************************************************************/

kd_precinct *
  kd_precinct_ref::instantiate_precinct(kd_resolution *res,
                                        kdu_coords pos_idx,
                                        kdu_thread_env *env)
{
  int num_layers = res->tile_comp->tile->num_layers;
  kd_precinct *result =
    res->codestream->precinct_server->get(res->max_blocks_per_precinct,
                                          res->num_subbands,num_layers,env);
  result->initialize(res,pos_idx);
  result->ref = this;
  if (state & 1)
    {
      if (state & 2)
        result->flags |= (KD_PFLAG_ADDRESSABLE | KD_PFLAG_WAS_READ);
      else
        result->flags |= KD_PFLAG_ADDRESSABLE;
      result->unique_address = state >> 2;
      if (!res->codestream->interchange)
        {
          assert(result->num_outstanding_blocks.get() > 0);
            // Otherwise, the client is opening a precinct which covers no
            // blocks in the region of interest, which makes no sense.
          result->flags |= KD_PFLAG_DESEQUENCED;
          result->next_layer_idx = num_layers;
        }
      state = _addr_to_kdu_long(result); assert(!(state & 1));
      return result;
    }

  // If we get here, we are creating the precinct for the first time.
  assert(state == 0);
  if (res->codestream->cached_source || res->codestream->interchange)
    { /* Cached source receives a negative seek address equal to -(1+id),
         where `id' is the precinct's unique identifier. */
      kd_tile_comp *tc = res->tile_comp;
      kd_tile *tile = tc->tile;
      kdu_long id = pos_idx.y*res->precinct_indices.size.x + pos_idx.x;
      for (kd_resolution *rp=res-res->res_level; rp != res; rp++)
        id += rp->num_precincts;
      id = id*tile->num_components + tc->cnum;
      id = id*res->codestream->tile_span.x*res->codestream->tile_span.y;
      id += tile->t_num; // id now holds the precinct's unique identifier.
      result->flags |= KD_PFLAG_ADDRESSABLE;
      result->unique_address = -(1+id);
      if (!res->codestream->interchange)
        { 
          result->flags |= KD_PFLAG_DESEQUENCED;
          result->next_layer_idx = num_layers;
        }
    }
  state = _addr_to_kdu_long(result); assert(!(state & 1));
  return result;
}

/*****************************************************************************/
/*                           kd_precinct_ref::close                          */
/*****************************************************************************/

void
  kd_precinct_ref::close(kdu_thread_env *env)
{
  if ((state == 0) || (state & 1))
    return; // Nothing to do.
  kd_precinct *precinct = (kd_precinct *) _kdu_long_to_addr(state);
  assert(precinct->ref == this);
  precinct->ref = NULL; // To satisfy the test in `kd_precinct::closing'.
  precinct->closing(env);
  if (precinct->flags & KD_PFLAG_ADDRESSABLE)
    {
      state = (precinct->unique_address << 2) + 1;
      if ((precinct->flags & KD_PFLAG_WAS_READ) ||
          (precinct->num_packets_read != 0))
        state += 2;
    }
  else
    state = 3; // Mark precinct as permanently unloaded.
  precinct->size_class->release(precinct,env);
}

/*****************************************************************************/
/*                        kd_precinct_ref::set_address                       */
/*****************************************************************************/

bool
  kd_precinct_ref::set_address(kd_resolution *res, kdu_coords pos_idx,
                               kdu_long seek_address)
{ 
  assert(seek_address > 0);
  kd_tile_comp *comp = res->tile_comp;
  kd_tile *tile = comp->tile;
  kd_codestream *codestream = tile->codestream;
  kd_precinct *precinct = deref();
  if (precinct != NULL)
    {
      assert(precinct->next_layer_idx == 0);
      precinct->next_layer_idx = tile->num_layers;
      precinct->flags |= KD_PFLAG_ADDRESSABLE;
      precinct->unique_address = seek_address;
      if (precinct->flags & KD_PFLAG_RELEVANT)
        tile->sequenced_relevant_packets += tile->max_relevant_layers;
      precinct->finished_desequencing();
    }
  else
    {
      state = (seek_address<<2) + 1;
      bool is_relevant = true;
      pos_idx += res->precinct_indices.pos;
      if ((!codestream->persistent) &&
          ((res->res_level > comp->apparent_dwt_levels) || (!comp->enabled) ||
           (pos_idx.x < res->region_indices.pos.x) ||
           (pos_idx.y < res->region_indices.pos.y) ||
           (pos_idx.x >= (res->region_indices.pos.x +
                          res->region_indices.size.x)) ||
           (pos_idx.y >= (res->region_indices.pos.y +
                          res->region_indices.size.y))))
        is_relevant = false;
      if (is_relevant)
        tile->sequenced_relevant_packets += tile->max_relevant_layers;
    }
  if (tile->sequenced_relevant_packets == tile->max_relevant_packets)
    {
      if (tile->finished_reading())
        return false; // tile was destroyed inside `finished_reading'
    }
  return true;
}
