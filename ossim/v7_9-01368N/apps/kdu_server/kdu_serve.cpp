/*****************************************************************************/
// File: kdu_serve.cpp [scope = APPS/SERVER]
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
   Implementation of the `kdu_serve' class, representing the platform
independent core services of the Kakadu server application.  A complete
server application requires network services and, in the case of the
"kdu_server" application, requires support for multi-threading.
******************************************************************************/

#include <math.h>
#include "kdu_params.h"
#include "kdu_utils.h"
#include "serve_local.h"
#include "jp2.h"

using namespace kd_serve_local; // Also includes `kdu_supp' and `kdu_core'

/* ========================================================================= */
/*                               Static Objects                              */
/* ========================================================================= */

#define KD_NUM_RELEVANCE_THRESHOLDS 26
class kd_log_relevance {
  public: // Functions
    kd_log_relevance();
    int lookup(double relevance)
      { /* Returns roughly 256*log_2(`relevance'), assuming that
           `relevance' lies in the range 0.01 to 1.0. */
        for (int i=0; i < KD_NUM_RELEVANCE_THRESHOLDS; i++)
          if (relevance > thresholds[i])
            return relevance_vals[i];
        return relevance_vals[KD_NUM_RELEVANCE_THRESHOLDS-1];
      }
  private: // Data
    double thresholds[KD_NUM_RELEVANCE_THRESHOLDS];
    int relevance_vals[KD_NUM_RELEVANCE_THRESHOLDS];
  };

static kd_log_relevance kd_log_rel;

kd_log_relevance::kd_log_relevance()
{
  double val = 1.0, gap = exp(log(0.5)/4.0);
  for (int i=0; i < KD_NUM_RELEVANCE_THRESHOLDS; i++)
    {
      relevance_vals[i] = (int)(256.0*(log(val)/log(2.0)));
      val *= gap;
      if (i == (KD_NUM_RELEVANCE_THRESHOLDS-1))
        val = -1.0; // Ensure everything is caught here.
      thresholds[i] = val;
    }
}


/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/*                 find_metagroup_with_tightest_container                    */
/*****************************************************************************/

static kd_meta *
  find_metagroup_with_tightest_container(kdu_long pos, kd_meta *groups)
  /* Recursively scans the list of metagroups headed by `groups' for the
     tightest one which contains the absolute file location `pos'.  A
     containing metagroup is one which contains the box header (or
     placeholder) of a box whose contents contain `pos'. */
{
  kd_meta *scan, *container=NULL;
  for (scan=groups; scan != NULL; container=scan, scan=scan->next_in_bin)
    if (scan->metagroup->fpos > pos)
      break; // `scan' is the first group which lies beyond `pos'
  if ((container == NULL) || (pos < container->metagroup->fpos))
    return NULL;
  if ((pos == container->metagroup->fpos) ||
      (container->metagroup->num_box_types == 0))
    return NULL; // It is not possible that `pos' lies within the contents of
                 // a box whose header is found in this metagroup.
  if (container->phld != NULL)
    { // See if we can go deeper
      kd_meta *tighter_container =
        find_metagroup_with_tightest_container(pos,container->phld);
      if (tighter_container != NULL)
        return tighter_container;
    }
  return container;
}

/*****************************************************************************/
/* INLINE                     output_var_length                              */
/*****************************************************************************/

static inline void
  output_var_length(kds_chunk *chunk, int val, int min_len=1)
  /* Encodes the supplied value using a byte-oriented extension code,
     storing the results in the supplied data chunk, augmenting its
     `num_bytes' field as appropriate. */
{
  int shift=7*(min_len-1);
  kdu_byte byte;

  if (val < 0)
    { 
      assert(0);
      val = 0; // Avoid endless loops at least
    }
  for (; (shift < 28) && ((val >> shift) >= 128); shift += 7);
  for (; shift >= 0; shift -= 7)
    {
      byte = (kdu_byte)(val>>shift) & 0x7F;
      if (shift > 0)
        byte |= 0x80;
      assert(chunk->num_bytes < chunk->max_bytes);
      chunk->data[chunk->num_bytes++] = byte;
    }
}

/*****************************************************************************/
/* INLINE                     simulate_var_length                            */
/*****************************************************************************/

static inline int
  simulate_var_length(int val)
{
  int result = 1;
  while (val >= 128)
    { val >>= 7; result++; }
  return result;
}

/*****************************************************************************/
/* STATIC                    set_precinct_dimensions                         */
/*****************************************************************************/

static void
  set_precinct_dimensions(kdu_params *cod)
  /* This function retrieves the code-block dimensions from the supplied
     COD parameter object and uses these, in conjunction with the current
     precinct dimensions, to determine suitable precinct dimensions for
     the interchange object.  The function may be called at the main header
     level, the tile level or the tile-component level, which allows it
     to ensure that the most suitable parameters are chosen, even if
     code-block dimensions vary from tile-component to tile-component. */
{
  // Determine the number of precinct specifications we need to write
  bool use_precincts=false;
  if (!cod->get(Cuse_precincts,0,0,use_precincts,false))
    return;
  kdu_coords block_size, min_size;
  cod->get(Cblk,0,0,block_size.y);
  cod->get(Cblk,0,1,block_size.x);

  int r, num_specs = 1;
  kdu_coords precinct_size[17];
  if (use_precincts)
    {
      for (r=0; r < 17; r++)
        if (!(cod->get(Cprecincts,r,0,precinct_size[r].y,false,false) &&
              cod->get(Cprecincts,r,1,precinct_size[r].x,false,false)))
          break;
      num_specs = r;
      if (num_specs == 0)
        use_precincts = false;
      else
        for (; r < 17; r++)
          precinct_size[r] = precinct_size[num_specs-1];
    }
  int decomp_val=3;
  for (r=0;
       (r < 17) &&
       (cod->get(Cdecomp,r,0,decomp_val,true,false) || (r < num_specs));
       r++)
    {
      cod_params::get_max_decomp_levels(decomp_val,min_size.x,min_size.y);
      min_size.x = block_size.x << min_size.x;
      min_size.y = block_size.y << min_size.y;
      if (!use_precincts)
        precinct_size[r] = min_size;
      else
        {
          if (precinct_size[r].x > min_size.x)
            precinct_size[r].x = min_size.x;
          if (precinct_size[r].y > min_size.y)
            precinct_size[r].y = min_size.y;
        }
      cod->set(Cprecincts,r,0,precinct_size[r].y);
      cod->set(Cprecincts,r,1,precinct_size[r].x);
    }
  cod->set(Cuse_precincts,0,0,true);
}

/*****************************************************************************/
/* STATIC                           load_blocks                              */
/*****************************************************************************/

static void
  load_blocks(kdu_precinct dest, kdu_resolution src)
{
  int b, min_band;
  int num_bands = src.get_valid_band_indices(min_band);
  for (b=min_band; num_bands > 0; num_bands--, b++)
    {
      kdu_dims blk_indices;
      if (!dest.get_valid_blocks(b,blk_indices))
        continue;
      kdu_subband src_band = src.access_subband(b);
      kdu_coords idx;
      for (idx.y=0; idx.y < blk_indices.size.y; idx.y++)
        for (idx.x=0; idx.x < blk_indices.size.x; idx.x++)
          {
            kdu_block *src_block = src_band.open_block(idx+blk_indices.pos);
            kdu_block *dst_block = dest.open_block(b,idx+blk_indices.pos);
            assert((src_block->modes == dst_block->modes) &&
                   (src_block->size == dst_block->size));
            dst_block->missing_msbs = src_block->missing_msbs;
            if (dst_block->max_passes < src_block->num_passes)
              dst_block->set_max_passes(src_block->num_passes);
            dst_block->num_passes = src_block->num_passes;
            int num_bytes = 0;
            for (int z=0; z < src_block->num_passes; z++)
              {
                num_bytes +=
                  (dst_block->pass_lengths[z] = src_block->pass_lengths[z]);
                dst_block->pass_slopes[z] = src_block->pass_slopes[z];
              }
            if (dst_block->max_bytes < num_bytes)
              dst_block->set_max_bytes(num_bytes,false);
            memcpy(dst_block->byte_buffer,src_block->byte_buffer,
                   (size_t) num_bytes);
            dest.close_block(dst_block);
            src_band.close_block(src_block);
          }
    }
}

/*****************************************************************************/
/* INLINE                         init_res_tail                              */
/*****************************************************************************/

static inline void
  init_res_tail(kd_active_binref *elt, kd_active_binref **rtail,
                kd_active_binref **res_tails, kd_active_binref * &head)
  /* This function is used by `kd_codestream_window::sequence_active_bins',
     whenever a new active data-bin reference (`elt') needs to be added to
     the list headed by `head', but it belongs to a resolution entry
     (`rtail') which is currently empty (NULL) within the `res_tails' array.
     The function scans preceding entries in the `res_tails' array to
     determine how `elt' should be linked into the overall list.  In the
     process, it is possible that the `head' of the entire list must be
     changed. */
{
  assert((*rtail == NULL) && (rtail >= res_tails));
  kd_active_binref **ptail;
  for (ptail=rtail-1; ptail >= res_tails; ptail--)
    if (*ptail != NULL)
      break;
  if (ptail < res_tails)
    { 
      elt->next = head;
      head = *rtail = elt;
    }
  else
    { 
      elt->next = (*ptail)->next; 
      (*ptail)->next = *rtail = elt;
    }
}


/* ========================================================================= */
/*                               kd_chunk_server                             */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_chunk_server::~kd_chunk_server                    */
/*****************************************************************************/

kd_chunk_server::~kd_chunk_server()
{
  kd_chunk_group *grp;
  while ((grp=chunk_groups) != NULL)
    {
      chunk_groups = grp->next;
      free(grp);
    }
}

/*****************************************************************************/
/*                         kd_chunk_server::get_chunk                        */
/*****************************************************************************/

kds_chunk *
  kd_chunk_server::get_chunk()
{
  kds_chunk *chunk;
  if (free_chunks == NULL)
    {
      size_t header_size = sizeof(kds_chunk);
      header_size += ((8-header_size) & 7); // Round up to multiple of 8.
      size_t size_one = header_size + chunk_size;
      size_one += ((8-size_one) & 7); // Round up to multiple of 8.
      size_t num_in_grp = 1 + (16384 / size_one);
      size_t grp_head_size = sizeof(kd_chunk_group);
      grp_head_size += ((8-grp_head_size) & 7); // Round up to multiple of 8.
      kd_chunk_group *grp = (kd_chunk_group *)
        malloc(size_one * num_in_grp + grp_head_size);
      grp->next = chunk_groups;
      chunk_groups = grp;
      chunk = (kds_chunk *)(((kdu_byte *) grp) + grp_head_size);
      for (size_t n=0; n < num_in_grp; n++)
        {
          chunk->data = ((kdu_byte *) chunk) + header_size;
          chunk->max_bytes = chunk_size;
          chunk->next = free_chunks;
          free_chunks = chunk;
          chunk = (kds_chunk *)(((kdu_byte *) chunk) + size_one);
        }
    }
  chunk = free_chunks;
  free_chunks = chunk->next;
  chunk->prefix_bytes = chunk_prefix_bytes;
  chunk->num_bytes = chunk->prefix_bytes;
  chunk->max_bytes = chunk_size; // Value might have been changed by user
  chunk->abandoned = false;
  chunk->next = NULL;
  chunk->bins = NULL;
  return chunk;
}

/*****************************************************************************/
/*                       kd_chunk_server::release_chunk                      */
/*****************************************************************************/

void
  kd_chunk_server::release_chunk(kds_chunk *chunk)
{
  chunk->next = free_chunks;
  free_chunks = chunk;
  assert(chunk->bins == NULL);
     // The caller is responsible for releasing the above lists first.
}


/* ========================================================================= */
/*                               kd_chunk_output                             */
/* ========================================================================= */

/*****************************************************************************/
/*                          kd_chunk_output::flush_buf                       */
/*****************************************************************************/

void
  kd_chunk_output::flush_buf()
{
  kdu_byte *ptr = buffer;
  int xfer_bytes;

  if (skip_bytes > 0)
    {
      xfer_bytes = (int)(next_buf-ptr);
      if (xfer_bytes > skip_bytes)
        xfer_bytes = skip_bytes;
      skip_bytes -= xfer_bytes;
      ptr += xfer_bytes;
    }
  while (ptr < next_buf)
    {
      assert(chunk->num_bytes >= chunk->prefix_bytes);
      xfer_bytes = chunk->max_bytes - chunk->num_bytes;
      if (xfer_bytes == 0)
        {
          if (chunk->next != NULL)
            { chunk = chunk->next; continue; }
          break;
        }
      if (xfer_bytes > (next_buf-ptr))
        xfer_bytes = (int)(next_buf-ptr);
      memcpy(chunk->data+chunk->num_bytes,ptr,(size_t) xfer_bytes);
      ptr += xfer_bytes;
      chunk->num_bytes += xfer_bytes;
    }
  next_buf = buffer;
}

/* ========================================================================= */
/*                         kd_active_precinct_server                         */
/* ========================================================================= */

/*****************************************************************************/
/*            kd_active_precinct_server::~kd_active_precinct_server          */
/*****************************************************************************/

kd_active_precinct_server::~kd_active_precinct_server()
{
  kd_active_precinct_group *grp;
  while ((grp = groups) != NULL)
    {
      groups = grp->next;
      delete grp;
    }
}

/*****************************************************************************/
/*                  kd_active_precinct_server::get_precinct                  */
/*****************************************************************************/

kd_active_precinct *
  kd_active_precinct_server::get_precinct()
{
  kd_active_precinct *elt;
  if (free_list == NULL)
    {
      kd_active_precinct_group *grp = new kd_active_precinct_group;
      grp->next = groups;
      groups = grp;
      for (int n=0; n < 32; n++)
        {
          elt = grp->precincts + n;
          elt->next = free_list;
          free_list = elt;
        }
    }
  elt = free_list;
  free_list = elt->next;
  memset(elt,0,sizeof(kd_active_precinct));
  return elt;
}


/* ========================================================================= */
/*                               kd_hole_server                              */
/* ========================================================================= */

/*****************************************************************************/
/*                 kd_model_hole_server::trim_for_max_bytes                  */
/*****************************************************************************/

kd_model_hole *
  kd_model_hole_server::trim_for_max_bytes(kd_model_hole *list, int max_bytes)
{
  kd_model_hole *prev=NULL, *scan;
  for (scan=list; scan != NULL; prev=scan, scan=scan->next)
    if (((int) scan->start) >= max_bytes)
      { // Remove the rest of the list, starting from `scan'
        release_holes(scan);
        if (prev == NULL)
          list = NULL;
        else
          prev->next = NULL;
      }
    else if (((int) scan->lim) > max_bytes)
      scan->lim = (kdu_uint16) max_bytes;
  return list;
}

/*****************************************************************************/
/*                 kd_model_hole_server::trim_for_fill_bytes                 */
/*****************************************************************************/

kd_model_hole *
  kd_model_hole_server::trim_for_fill_bytes(kd_model_hole *list,
                                            int fill_bytes)
{
  while ((list != NULL) && (((int) list->start) < fill_bytes))
    { 
      if (fill_bytes >= (int) list->lim)
        { 
          kd_model_hole *elt = list;
          list = elt->next;
          elt->next = free_holes;
          free_holes = elt;
        }
      else
        list->start = (kdu_uint16) fill_bytes;
    }
  return list;
}

/*****************************************************************************/
/*                       kd_model_hole_server::add_hole                      */
/*****************************************************************************/

kd_model_hole *
  kd_model_hole_server::add_hole(kd_model_hole *list,
                                 kdu_uint16 hole_start, kdu_uint16 hole_lim)
{
  kd_model_hole *prev=NULL, *scan;
  for (scan=list; scan != NULL; prev=scan, scan=scan->next)
    if (scan->lim >= hole_start)
      break;
  if ((scan == NULL) || (scan->start > hole_lim))
    { // Insert a new hole immediately after `prev'
      if (free_holes == NULL)
        { 
          kd_model_hole_group *grp = new kd_model_hole_group;
          grp->next = hole_groups; hole_groups = grp;
          for (int i=0; i < 32; i++)
            { grp->holes[i].next = free_holes; free_holes = grp->holes+i; }
        }
      kd_model_hole *elt = free_holes;
      free_holes = elt->next;
      elt->start = hole_start;
      elt->lim = hole_lim;
      elt->next = scan;
      if (prev == NULL)
        list = elt;
      else
        prev->next = elt;
    }
  else
    { // `scan' overlaps the new hole
      if (hole_start < scan->start)
        scan->start = hole_start;
      if (hole_lim > scan->lim)
        scan->lim = hole_lim;
      while ((scan->next != NULL) && (scan->next->start <= scan->lim))
        { // Join two holes
          kd_model_hole *elt = scan->next;
          if (elt->lim > scan->lim)
            scan->lim = elt->lim;
          scan->next = elt->next;
          elt->next = free_holes;
          free_holes = elt;
        }
    }
  return list;
}


/* ========================================================================= */
/*                              kd_binref_server                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    kd_binref_server::~kd_binref_server                    */
/*****************************************************************************/

kd_binref_server::~kd_binref_server()
{
  kd_active_binref_group *agrp;
  while ((agrp = active_groups) != NULL)
    { 
      active_groups = agrp->next;
      delete agrp;
    }
  kd_chunk_binref_group *cgrp;
  while ((cgrp = chunk_groups) != NULL)
    { 
      chunk_groups = cgrp->next;
      delete cgrp;
    }  
}

/*****************************************************************************/
/*                   kd_binref_server::get_active_binref                     */
/*****************************************************************************/

kd_active_binref *
  kd_binref_server::get_active_binref()
{
  kd_active_binref *elt;
  if (active_free == NULL)
    {
      kd_active_binref_group *grp = new kd_active_binref_group;
      grp->next = active_groups;
      active_groups = grp;
      for (int n=0; n < 32; n++)
        {
          elt = grp->binrefs + n;
          elt->next = active_free;
          active_free = elt;
        }
    }
  elt = active_free;
  active_free = elt->next;
  return elt;
}

/*****************************************************************************/
/*                    kd_binref_server::get_chunk_binref                     */
/*****************************************************************************/

kd_chunk_binref *
  kd_binref_server::get_chunk_binref()
{
  kd_chunk_binref *elt;
  if (chunk_free == NULL)
    { 
      kd_chunk_binref_group *grp = new kd_chunk_binref_group;
      grp->next = chunk_groups;
      chunk_groups = grp;
      for (int n=0; n < 32; n++)
        { 
          elt = grp->binrefs + n;
          elt->next = chunk_free;
          chunk_free = elt;
        }
    }
  elt = chunk_free;
  chunk_free = elt->next;
  return elt;
}


/* ========================================================================= */
/*                             kd_pblock_server                              */
/* ========================================================================= */

/*****************************************************************************/
/*                    kd_pblock_server::~kd_pblock_server                    */
/*****************************************************************************/

kd_pblock_server::~kd_pblock_server()
{  
  for (int s=0; s < lim_log_pblock_size; s++)
    {
      kd_precinct_block *pb;
      while ((pb = free_lists[s]) != NULL)
        { 
          free_lists[s] = pb->free_next;
          free(pb);
        }
    }
  if (free_lists != NULL)
    { delete[] free_lists; free_lists = NULL; }
}

/*****************************************************************************/
/*                        kd_pblock_server::get_pblock                       */
/*****************************************************************************/

kd_precinct_block *kd_pblock_server::get_pblock(int s)
{
  if (s >= lim_log_pblock_size)
    {
      kd_precinct_block **new_lists = new kd_precinct_block *[s+1];
      memset(new_lists,0,sizeof(kd_precinct_block *)*(size_t)(s+1));      
      kd_precinct_block **old_lists = free_lists;
      for (int t=0; t < lim_log_pblock_size; t++)
        { new_lists[t] = old_lists[t]; old_lists[t] = NULL; }
      lim_log_pblock_size = s + 1;
      free_lists = new_lists;
      delete[] old_lists;
    }
  size_t pblk_bytes =
    sizeof(kd_precinct_block) + (sizeof(kd_precinct_model)<<(s+s));
  kd_precinct_block *pb = free_lists[s];
  if (pb == NULL)
    pb = (kd_precinct_block *) malloc(pblk_bytes);
  else
    free_lists[s] = pb->free_next;
  memset(pb,0,pblk_bytes);
  return pb;
}

/*****************************************************************************/
/*                      kd_pblock_server::release_pblock                     */
/*****************************************************************************/

void kd_pblock_server::release_pblock(kd_precinct_block *pb, int s)
{
  if ((s < 0) || (s >= lim_log_pblock_size))
    { // Non-existent size class
      assert(0);
      return;
    }
  pb->free_next = free_lists[s];
  free_lists[s] = pb;
  int n, pblk_elts = 1<<(s+s);
  for (n=0; n < pblk_elts; n++)
    if (pb->precincts[n].get_active() != NULL)
      assert(0);
}


/* ========================================================================= */
/*                               kds_id_encoder                              */
/* ========================================================================= */

/*****************************************************************************/
/*                          kds_id_encoder::encode_id                        */
/*****************************************************************************/

int
  kds_id_encoder::encode_id(kdu_byte *dest, int cls, int stream_id,
                            kdu_long bin_id, bool is_complete,
                            bool extended)
{
  assert((stream_id >= 0) && (bin_id >= 0));
  bool repeat_class =
    exploit_last_message && (last_class == cls) && (last_extended==extended);
  bool repeat_stream =
    exploit_last_message && (last_stream_id == stream_id);
  if (dest != NULL)
    {
      exploit_last_message = true;
      last_stream_id = stream_id;
      last_class = cls; last_extended = extended;
    }
  
  kdu_byte byte = (is_complete)?0x10:0;
  if (!repeat_stream)
    { repeat_class = false; byte |= 0x60; }
  else if (!repeat_class)
    byte |= 0x40;
  else
    byte |= 0x20;
  int bytes_out = 1;
  int bits_left = 4;

  // Write the in-class bin identifier.
  while ((bin_id >> bits_left) != 0)
    {
      bits_left += 7;
      bytes_out++;
    }
  if (dest != NULL)
    {
      bits_left -= 4;
      byte += (kdu_byte)((bin_id>>bits_left) & 0x0F);
      while (bits_left > 0)
        {
          byte |= 0x80;
          *(dest++) = byte;
          bits_left -= 7;
          byte = ((kdu_byte)(bin_id >> bits_left)) & 0x7F;
        }
      *(dest++) = byte;
    }

  int shift;

  if (!repeat_class)
    { // Write the class VBAS
      cls += cls + ((extended)?1:0);
      for (shift=0, bytes_out++; (cls>>shift) >= 128; shift+=7)
        bytes_out++;
      if (dest != NULL)
        {
          for (; shift >= 0; shift -= 7)
            {
              byte = (kdu_byte)(cls>>shift) & 0x7F;
              if (shift > 0)
                byte |= 0x80;
              *(dest++) = byte;
            }
        }
    }

  if (!repeat_stream)
    { // Write the stream-ID VBAS
      for (shift=0, bytes_out++; (stream_id>>shift) >= 128; shift+=7)
        bytes_out++;
      if (dest != NULL)
        {
          for (; shift >= 0; shift -= 7)
            {
              byte = (kdu_byte)(stream_id>>shift) & 0x7F;
              if (shift > 0)
                byte |= 0x80;
              *(dest++) = byte;
            }
        }
    }

  return bytes_out;
}


/* ========================================================================= */
/*                                  kd_meta                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                               kd_meta::init                               */
/*****************************************************************************/

int
  kd_meta::init(const kds_metagroup *metagroup, kd_meta *parent,
                kdu_long bin_id, int bin_offset, int tree_depth)
{
  this->bin_id = bin_id;
  this->tree_depth = tree_depth;
  this->metagroup = metagroup;
  this->parent = parent;
  this->link_target = NULL;
  this->bin_offset = bin_offset;
  next_in_bin = prev_in_bin = phld = active_next = next_modeled = NULL;
  num_bytes = metagroup->length;
  dispatched_span = sim_span = 0;
  span_complete = sim_span_complete = 0;
  dispatched_holes = NULL;
  is_modeled = 0;
  in_scope = false;
  max_content_bytes = INT_MAX;
  sequence = KDS_METASCOPE_MAX_SEQUENCE;
  active_length = 0;
  int num_completed_bins = (metagroup->is_last_in_bin)?1:0;
  if (metagroup->phld != NULL)
    {
      kd_meta *mnew, *tail = NULL;
      const kds_metagroup *scan;
      int off=0;
      for (scan=metagroup->phld; scan != NULL; scan=scan->next)
        {
          int phld_tree_depth = tree_depth;
          if (scan->num_box_types > 0)
            phld_tree_depth++;
          mnew = new kd_meta;
          num_completed_bins +=
            mnew->init(scan,this,metagroup->phld_bin_id,off,phld_tree_depth);
          off += mnew->num_bytes;
          mnew->prev_in_bin = tail;
          if (tail == NULL)
            phld = tail = mnew;
          else
            tail = tail->next_in_bin = mnew;
        }
    }
  return num_completed_bins;
}

/*****************************************************************************/
/*                            kd_meta::resolve_links                         */
/*****************************************************************************/

void
  kd_meta::resolve_links(kd_meta *metatree)
{
  if (metagroup->link_fpos > 0)
    link_target =
      find_metagroup_with_tightest_container(metagroup->link_fpos,metatree);
  kd_meta *child;
  for (child=phld; child != NULL; child=child->next_in_bin)
    child->resolve_links(metatree);
}

/*****************************************************************************/
/*                    kd_meta::find_active_scope_and_sequence                */
/*****************************************************************************/

bool
  kd_meta::find_active_scope_and_sequence(kdu_window &window,
                                          kd_codestream_window *first_window,
                                          kd_codestream_window *lim_window,
                                          int max_sequence,
                                          int *accumulating_byte_limit,
                                          int recursion_depth)
{
  kd_codestream_window *wp;
  kd_meta *mscan;
  kdu_metareq *req;
  assert(!in_scope); // `include_active_groups' was supposed to reset scope!!
  max_content_bytes = 0; // For the moment
  int original_content_byte_limit=0; // Max bytes of orig box contents to send
  
  if (accumulating_byte_limit != NULL)
    {
      max_content_bytes = *accumulating_byte_limit;
      if (max_content_bytes < 0)
        {
          max_content_bytes = 0;
          accumulating_byte_limit = NULL;
        }
      else
        { 
          in_scope = true;
          original_content_byte_limit = max_content_bytes;
          if (phld == NULL)
            {
              *accumulating_byte_limit -= num_bytes;
              accumulating_byte_limit = NULL; // Don't need it anymore here
            }
        }
    }
  if (recursion_depth >= tree_depth)
    { 
      in_scope = true;
    }
  sequence = (in_scope)?max_sequence:KDS_METASCOPE_MAX_SEQUENCE;
  
  // Start by setting the dynamic depth of each `kdu_metareq' entry
  if ((bin_id == 0) && (bin_offset == 0))
    { // Right at the start of the metadata scan, install initial configuration
      for (req=window.metareq; req != NULL; req=req->next)
        if (req->root_bin_id == 0)
          req->dynamic_depth = req->max_depth;
        else
          req->dynamic_depth = -1; // These elements don't apply yet
    }

  // Find out if this group's scope matches any of the stream windows or
  // any of the originally requested codestream contexts; if
  // it is not a leaf, the match will be a possible match, rather than a
  // guaranteed one.
  kds_metascope *scope = metagroup->scope;
  assert(scope != NULL);
  
  bool have_scope_match = true; // True until proven otherwise
  bool have_stream_match = false;
  bool have_region_match = false; // If any descendant has a true region match
  bool have_true_region_match = false;
  
  if (scope->flags & KDS_METASCOPE_HAS_IMAGE_SPECIFIC_DATA)
    { 
      if (metagroup->last_box_type == jp2_layer_extensions_4cc)
        have_scope_match = true; // Just for a debug breakpoint
      int cn, cne;
      kdu_sampled_range *rg, *rge;
      for (cn=0; ((rg=window.codestreams.access_range(cn)) != NULL) &&
           !have_stream_match; cn++)
        if (((rg->context_type != KDU_JPIP_CONTEXT_TRANSLATED) ||
             !(scope->flags & KDS_METASCOPE_IS_CONTEXT_SPECIFIC_DATA)) &&
            scope->entities->test_codestream_range(*rg))
          have_stream_match = true;
      for (cn=0; ((rg=window.contexts.access_range(cn)) != NULL) &&
           !have_stream_match; cn++)
        {
          if ((rg->context_type == KDU_JPIP_CONTEXT_JPXL) &&
              scope->entities->test_compositing_layer_range(*rg))
            have_stream_match = true;
          else if ((rg->expansion != NULL) &&
                   !(scope->flags & KDS_METASCOPE_IS_CONTEXT_SPECIFIC_DATA))
            for (cne=0; ((rge=rg->expansion->access_range(cne)) != NULL) &&
                 !have_stream_match; cne++)
              have_stream_match = scope->entities->test_codestream_range(*rge);
        }
      if ((!have_stream_match) &&
          !(scope->flags & KDS_METASCOPE_HAS_GLOBAL_DATA))
        have_scope_match = false;
    }
  
  if (have_stream_match &&
      (scope->flags & KDS_METASCOPE_HAS_REGION_SPECIFIC_DATA))
    {
      for (wp=first_window; wp != lim_window; wp=wp->next)
        if (scope->region.intersects(wp->window_region))
          { 
            have_region_match = true;
            break;
          }
      if ((!have_region_match) &&
          !(scope->flags & (KDS_METASCOPE_HAS_IMAGE_WIDE_DATA |
                            KDS_METASCOPE_HAS_GLOBAL_DATA)))
        have_scope_match = false;
      have_true_region_match = have_region_match &&
        (scope->flags & KDS_METASCOPE_IS_REGION_SPECIFIC_DATA);
    }
  
  // Now see if this group or any of its sub-groups, have a possible match
  // with any metareq element.  We will need to know this later to determine
  // whether or not there is any value in examining matches with descendants.
  // It is also a simple test which can save us from having to examine all
  // metareq entries.
  bool have_possible_metareq_match = window.have_metareq_all ||
    (window.have_metareq_global &&
     (scope->flags & KDS_METASCOPE_HAS_GLOBAL_DATA)) ||
    (window.have_metareq_window && have_true_region_match) ||
    (window.have_metareq_stream && have_stream_match &&
     (scope->flags & KDS_METASCOPE_HAS_IMAGE_WIDE_DATA));

  // Now check to see if there are any actual metareq matches.
  if (have_possible_metareq_match && (metagroup->num_box_types > 0))
    for (req=window.metareq; req != NULL; req=req->next)
      {
        int b;

        if (req->dynamic_depth < tree_depth)
          continue; // Metareq entry does not apply so deep in the tree
        if (req->box_type != 0)
          {
            for (b=0; b < metagroup->num_box_types; b++)
              if (metagroup->box_types[b] == req->box_type)
                break;
            if (b == metagroup->num_box_types)
              continue; // No matching box types
          }
        if ((req->qualifier & KDU_MRQ_ALL) ||
            ((req->qualifier & KDU_MRQ_GLOBAL) &&
             (scope->flags & KDS_METASCOPE_HAS_GLOBAL_DATA)) ||
            ((req->qualifier & KDU_MRQ_STREAM) && have_stream_match &&
             (scope->flags & KDS_METASCOPE_HAS_IMAGE_WIDE_DATA)) ||
            ((req->qualifier & KDU_MRQ_WINDOW) && have_true_region_match))
          { 
            in_scope = true;
            if (scope->sequence < sequence)
              sequence = scope->sequence;
            if (req->priority && (sequence > 0))
              sequence = 0;
            if (req->byte_limit > original_content_byte_limit)
              original_content_byte_limit = req->byte_limit;
            if (req->recurse && (req->dynamic_depth > recursion_depth))
              recursion_depth = req->dynamic_depth;
          }
      }

  if (original_content_byte_limit > max_content_bytes)
    max_content_bytes = original_content_byte_limit;

  // Now check for direct matches with the view window
  if ((scope->flags & KDS_METASCOPE_LEAF) && // Matches count only at leaves
      have_scope_match &&
      ((scope->flags & KDS_METASCOPE_MANDATORY) ||
       ((scope->flags & KDS_METASCOPE_IMAGE_MANDATORY) &&
        !window.metadata_only)))
    { 
      in_scope = true;
      max_content_bytes = INT_MAX;
      original_content_byte_limit = INT_MAX; // Include all descendants
      if (scope->sequence < sequence)
        sequence = scope->sequence;
    }

  bool have_active_link_targets = false;
  if ((phld != NULL) &&
      (in_scope || have_scope_match || have_possible_metareq_match))
    { // Examine the descendants, allowing them to affect our own scope and
      // keeping track of the number of bytes from such descendants which
      // are forced into scope by the `original_content_byte_limit' value.
      int max_bytes_left = original_content_byte_limit - 8;
              // We assume an 8-byte original box header; if it was 16 bytes
              // we are being a little generous here.
      
      // Prepare dynamic depth values for all metareq's which are rooted
      // at the data-bin whose contents are spanned by the `phld' list.  Note
      // that this was done incorrectly prior to version 6.2.  According to
      // the JPIP standard, metareq's apply to the boxes found within the
      // contents of any box whose contents have been replaced by the relevant
      // data-bin; these metareq's should not be tested against the header
      // of the box whose contents have been replaced.
      for (req=window.metareq; req != NULL; req=req->next)
        if (req->root_bin_id == phld->bin_id)
          req->dynamic_depth = phld->tree_depth + req->max_depth;

      for (mscan=phld; mscan != NULL; mscan=mscan->next_in_bin)
        { 
          if (mscan->metagroup->num_box_types == 0)
            { // Not a box; just the contents of `this' box.
              assert((mscan->phld == NULL) && (mscan->next_in_bin == NULL) &&
                     (mscan==phld)); // Must be the contents of this box
              if (max_bytes_left <= 0)
                mscan->in_scope = false;
              else
                {
                  mscan->in_scope = in_scope;
                  mscan->sequence = sequence;
                  mscan->max_content_bytes = max_bytes_left;
                  max_bytes_left -= mscan->num_bytes;
                  if (mscan->link_target != NULL)
                    have_active_link_targets = true;
                }
            }
          else
            {
              kdu_int32 *max_bytes_ref=(max_bytes_left>0)?&max_bytes_left:NULL;
              if (mscan->find_active_scope_and_sequence(window,first_window,
                                                        lim_window,sequence,
                                                        max_bytes_ref))
                have_active_link_targets = true;
            }
        }
      if (max_bytes_left < 0)
        max_bytes_left = 0;
      if (accumulating_byte_limit != NULL)
        accumulating_byte_limit-=(original_content_byte_limit-max_bytes_left);
      
      if (phld->in_scope)
        {
          this->in_scope = true;
          this->max_content_bytes = INT_MAX;
          if (phld->sequence < sequence)
            sequence = phld->sequence;
        }

      // Reset all dynamic depths that we set prior to visiting the members
      // of the `phld' list.
      for (req=window.metareq; req != NULL; req=req->next)
        if (req->root_bin_id == phld->bin_id)
          req->dynamic_depth = -1;
    }

  // Finally, make scope & sequence adjustments based on sibling metagroups
  // and required initial sub-boxes, and finalize the determination of the
  // `have_active_link_targets' return result.
  if (in_scope)
    { 
      if (scope->flags & KDS_METASCOPE_INCLUDE_FIRST_SUBBOX)
        { 
          assert((phld != NULL) && !phld->metagroup->is_placeholder);
          phld->sequence = sequence;
          phld->in_scope = true;
          phld->max_content_bytes = INT_MAX;
        }
      
      for (mscan=prev_in_bin; mscan != NULL; mscan=mscan->prev_in_bin)
        { // Walk backwards, bringing prior siblings into scope as necessary
          if (mscan->in_scope && (mscan->sequence <= sequence))
            {
              if (mscan->max_content_bytes == INT_MAX)
                break; // All previous elements in scope with lower sequence
            }
          else
            { 
              mscan->sequence = sequence;
              mscan->in_scope = true;
              if (mscan->link_target != NULL)
                have_active_link_targets = true;
              if (mscan->metagroup->scope->flags &
                  KDS_METASCOPE_INCLUDE_FIRST_SUBBOX)
                { 
                  assert((mscan->phld != NULL) &&
                         !mscan->phld->metagroup->is_placeholder);
                  mscan->phld->sequence = sequence;
                  mscan->phld->in_scope = true;
                  mscan->phld->max_content_bytes = INT_MAX;
                }
            }
          mscan->max_content_bytes = INT_MAX;
        }
    }

  if ((prev_in_bin != NULL) && prev_in_bin->in_scope &&
      (prev_in_bin->metagroup->scope->flags &
       KDS_METASCOPE_INCLUDE_NEXT_SIBLING))
    { // Need to at least send the placeholder box to allow the client to
      // receive a complete data-bin and hence determine whether or not
      // there are any further boxes of interest at the top level of the
      // current data-bin.
      if (!in_scope)
        { 
          in_scope = true;
          max_content_bytes = num_bytes; // Send whole box or placeholder
          sequence = prev_in_bin->sequence;
        }
      else if (prev_in_bin->sequence < sequence)
        sequence = prev_in_bin->sequence; // Sequence box headers at same time
                 // as prev group, but might end up sequencing box contents for
                 // this group later, or not at all.
    }
  if (bin_id == 0)
    { // Force all of metadata-bin 0 into scope
      in_scope = true;
      max_content_bytes = INT_MAX;
    }
  if (in_scope && (link_target != NULL))
    have_active_link_targets = true; // Can't finally test `in_scope' until now
  return have_active_link_targets;
}

/*****************************************************************************/
/*                 kd_meta::add_active_link_targets_to_scope                 */
/*****************************************************************************/

void
  kd_meta::add_active_link_targets_to_scope()
{
  if (!in_scope)
    return;
  
  kd_meta *scan, *grp;
  if (link_target != NULL)
    { // Force the link target and all its parents and preceding siblings
      // into scope.
      for (scan=link_target; scan != NULL; scan=scan->parent)
        for (grp=scan; grp != NULL; grp=grp->prev_in_bin)
          {
            if (!grp->in_scope)
              {
                grp->sequence = this->sequence;
                grp->in_scope = true;
                if (grp == scan)
                  grp->max_content_bytes =
                    grp->metagroup->last_box_header_prefix;
                else
                  grp->max_content_bytes = INT_MAX;
              }
            else
              { // `grp' is already in scope, but we may need to change
                // the number of bytes or the sequence
                if (grp->sequence > this->sequence)
                  grp->sequence = this->sequence;
                if (grp->max_content_bytes < grp->num_bytes)
                  {
                    if (grp == scan)
                      grp->max_content_bytes =
                        grp->metagroup->last_box_header_prefix;
                    else
                      grp->max_content_bytes = INT_MAX;
                  }
              }
        }
    }
  
  for (scan=phld; scan != NULL; scan=scan->next_in_bin)
    scan->add_active_link_targets_to_scope();
}

/*****************************************************************************/
/*                       kd_meta::include_active_groups                      */
/*****************************************************************************/

void
  kd_meta::include_active_groups(kd_meta *start)
{
  if (!in_scope)
    return;
  
  active_length = 0;
  kd_meta *scan, *prev=start;
  for (scan=prev->active_next; scan != NULL; prev=scan, scan=scan->active_next)
    if (scan->sequence > sequence)
      break;
  active_next = scan;
  if (prev != this)
    prev->active_next = this;

  assert((max_content_bytes >= 0) &&
         (metagroup->length >= metagroup->last_box_header_prefix));
  active_length = metagroup->length;
  if (max_content_bytes < (active_length - metagroup->last_box_header_prefix))
    active_length = max_content_bytes + metagroup->last_box_header_prefix;
  
  if (phld != NULL)
    for (prev=this, scan=phld; (scan != NULL) && scan->in_scope;
         prev=scan, scan=scan->next_in_bin)
      scan->include_active_groups(prev);
  
  in_scope = false; // Very important to clean the tree so that nothing is
                    // left in scope for next time.  Otherwise, we can have
                    // all kinds of problems when we need to force specific
                    // elements into scope in a non-hierarchical fashion.
}


/* ========================================================================= */
/*                                 kd_stream                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                            kd_stream::kd_stream                           */
/*****************************************************************************/

kd_stream::kd_stream(kd_serve *owner)
{
  this->serve = owner;
  stream_id = -1; // Not yet known
  num_components = num_output_components = total_tiles = 0;
  pid_gap = 0;
  component_subs = output_component_subs = NULL;
  max_discard_levels = max_quality_layers = 0;
  num_layer_slopes = 0;
  layer_log_slopes = NULL;
  active_layer_log_slopes = NULL;
  dispatched_header_span = sim_header_span = 0;
  header_span_complete = sim_header_span_complete = 0;
  dispatched_header_holes = NULL;
  completed_tiles = 0;
  is_locked = false;
  is_modeled = false;
  windows = NULL;
  num_active_contexts = num_chunk_binrefs = 0;
  detached_interface_mem = 0;
  main_header_bytes = 0;
  tiles = NULL;
  attached_tiles = modeled_tiles = NULL;
  next = prev = NULL;
}

/*****************************************************************************/
/*                           kd_stream::~kd_stream                           */
/*****************************************************************************/

kd_stream::~kd_stream()
{
  deconstruct_interfaces(false);
  assert((tiles == NULL) && (completed_tiles == 0) &&
         !(source.exists() || interchange.exists() || structure.exists()));
  if (dispatched_header_holes != NULL)
    { 
      kd_model_hole *holes = dispatched_header_holes;
      dispatched_header_holes = NULL;
      serve->hole_server->release_holes(holes);
    }
  if (layer_log_slopes != NULL)
    { delete[] layer_log_slopes; layer_log_slopes = NULL; }
  if (component_subs != NULL)
    { delete[] component_subs; component_subs = NULL; }
  if (output_component_subs != NULL)
    { delete[] output_component_subs; output_component_subs = NULL; }
}

/*****************************************************************************/
/*                           kd_stream::initialize                           */
/*****************************************************************************/

void kd_stream::initialize(kdu_serve_target *target, int codestream_id,
                           bool ignore_relevance_info)
{
  assert((this->stream_id < 0) && (tiles == NULL) &&
         (component_subs == NULL) && (output_component_subs == NULL) &&
         (layer_log_slopes == NULL) && (attached_tiles == NULL) &&
         (modeled_tiles == NULL));
  this->stream_id = codestream_id;
  if (!target->get_codestream_siz_info(codestream_id,image_dims,tile_partition,
                                       tile_indices,num_components,
                                       num_output_components,
                                       max_discard_levels,max_quality_layers))
    { kdu_error e; e <<
      "Serve target failed to provide information for codestream "
      << stream_id << ".  Weird!";
    }
  
  assert((num_components <= 16384) && (num_output_components <= 16384));
  component_subs = new kdu_coords[num_components];
  output_component_subs = new kdu_coords[num_output_components];
  total_tiles = (int) tile_indices.area();
  pid_gap = ((kdu_long) num_components) * total_tiles;
  target->get_codestream_siz_info(codestream_id,image_dims,tile_partition,
                                  tile_indices,num_components,
                                  num_output_components,
                                  max_discard_levels,max_quality_layers,
                                  component_subs,output_component_subs);
  num_layer_slopes = 0;
  int num_layer_lengths=0;
  if (target->get_codestream_rd_info(codestream_id,num_layer_slopes,
                                     num_layer_lengths) &&
      (num_layer_slopes > 0) && (num_layer_slopes < 16384))
    { 
      layer_log_slopes = new int[num_layer_slopes];
      target->get_codestream_rd_info(codestream_id,num_layer_slopes,
                                     num_layer_lengths,layer_log_slopes);
      while ((num_layer_slopes > 1) &&
             (layer_log_slopes[num_layer_slopes-1] ==
              layer_log_slopes[num_layer_slopes-2]))
        num_layer_slopes--; // Remove degenerate duplicate values, just in case
    }
}

/*****************************************************************************/
/*                      kd_stream::construct_interfaces                      */
/*****************************************************************************/

void kd_stream::construct_interfaces()
{
  // Note that all the operations here should be safe to invoke on a
  // `source' codestream which is not locked, because none of them should
  // modify the state of the `source' codestream in any way.  The main header
  // of the codestream should have been fully parsed already.
  
  // Start by running some consistency checks
  int c;
  bool is_consistent = true;
  kdu_dims tmp_dims;
  source.get_dims(-1,tmp_dims);
  if (tmp_dims != image_dims)
    is_consistent = false;
  source.get_tile_partition(tmp_dims);
  if (tmp_dims != tile_partition)
    is_consistent = false;
  source.get_valid_tiles(tmp_dims);
  if (tmp_dims != tile_indices)
    is_consistent = false;
  if (num_components != source.get_num_components(false))
    is_consistent = false;
  if (num_output_components != source.get_num_components(true))
    is_consistent = false;
  if (is_consistent)
    { 
      kdu_coords subs;
      for (c=0; c < num_components; c++)
        { 
          source.get_subsampling(c,subs);
          if (subs != component_subs[c])
            is_consistent = false;
        }
      for (c=0; c < num_output_components; c++)
        {
          source.get_subsampling(c,subs,true);
          if (subs != output_component_subs[c])
            is_consistent = false;
        }
    }
  if (!is_consistent)
    { kdu_error e; e <<
      "Codestream " << stream_id << " is found to be inconsistent with "
      "summary information held in the server's cache.  This may have "
      "occurred if the codestream is symbolically linked from a JPX/MJ2 "
      "file on the server and the linked file have been changed unexpectedly.";
    }

  // Construct a blank set of tiles, if required
  if (tiles == NULL)
    tiles = new kd_tile[total_tiles];
  
  // Start building the interchange codestreams.  We will transfer tile
  // headers and build the structures for each tile only on demand.
  siz_params *src_params = source.access_siz();
  siz_params *ichg_params = NULL;
  if (!interchange)
    { 
      interchange.create(src_params);
      ichg_params = interchange.access_siz();
      ichg_params->copy_from(src_params,-1,-1); // Copy main header parameters
      kdu_params *ichg_cod = ichg_params->access_cluster(COD_params);
      set_precinct_dimensions(ichg_cod);
      kdu_params *ichg_poc = ichg_params->access_cluster(POC_params);
      ichg_poc->delete_unparsed_attribute(Porder); // Remove global POC
      for (c=0; c < num_components; c++)
        {
          kdu_params *comp_cod = ichg_cod->access_unique(-1,c);
          if (comp_cod != NULL)
            set_precinct_dimensions(comp_cod);
        }
      ichg_params->finalize_all(-1,false);
      main_header_bytes = 2 + ichg_params->generate_marker_segments(NULL,-1,0);
    }
  if (!structure)
    { 
      ichg_params = interchange.access_siz();
      structure.create(ichg_params);
      siz_params *struc_params = structure.access_siz();
      struc_params->copy_from(ichg_params,-1,-1); // Copy main header params
      struc_params->finalize_all(-1,false);
    }
}

/*****************************************************************************/
/*                     kd_stream::deconstruct_interfaces                     */
/*****************************************************************************/

void kd_stream::deconstruct_interfaces(bool leave_expanded)
{
  assert(!(is_locked || source.exists())); // Must be detached from source
  bool was_complete = this->check_complete();
  kd_model_hole_server *hole_server = serve->hole_server;
  if (tiles != NULL)
    {
      kdu_coords idx;
      kd_tile *tp = tiles;
      for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
        for (idx.x=0; idx.x < tile_indices.size.x; idx.x++, tp++)
          { 
            if (tp->interchange.exists())
              tp->interchange.close();
            if (tp->structure.exists())
              tp->structure.close();
            tp->have_params = false;
            assert((tp->num_active_precincts == 0) &&
                   (tp->num_tile_binrefs == 0));
            if (leave_expanded)
              continue;
          
            if (tp->dispatched_header_holes != NULL)
              { 
                kd_model_hole *holes = tp->dispatched_header_holes;
                tp->dispatched_header_holes = NULL;
                hole_server->release_holes(holes);
              }
            tp->dispatched_header_span = tp->sim_header_span = 0;
            tp->header_span_complete = tp->sim_header_span_complete = 0;
            int c, r;
            if (tp->comps != NULL)
              {
                for (c=0; c < num_components; c++)
                  {
                    kd_tile_comp *tc = tp->comps + c;
                    if (tc->res != NULL)
                      {
                        for (r=0; r < tc->num_resolutions; r++)
                          {
                            kd_resolution *rp = tc->res + r;
                            rp->free_active_pblocks();
                            if (rp->pblocks != NULL)
                              {
                                delete[] rp->pblocks;
                                rp->pblocks = NULL;
                              }
                          }
                        delete[] tc->res;
                      }
                  }
                delete[] tp->comps;
              }
            tp->completed_precincts = 0;
          }      
      if (!leave_expanded)
        { 
          delete[] tiles;
          tiles = NULL;
          completed_tiles = 0;
          attached_tiles = modeled_tiles = NULL;
          serve->adjust_completed_streams(this,was_complete);
        }
    }
  if (interchange.exists())
    interchange.destroy();
  if (structure.exists())
    structure.destroy();
  last_structure_region = kdu_dims();
  serve->total_detached_interface_mem -= this->detached_interface_mem;
  this->detached_interface_mem = 0;
}

/*****************************************************************************/
/*                        kd_stream::ensure_expanded                         */
/*****************************************************************************/
void kd_stream::ensure_expanded()
{
  if (tiles == NULL)
    serve->attach_stream(this); // Attaching a collapsed stream expands it
}

/*****************************************************************************/
/*                          kd_stream::access_tile                           */
/*****************************************************************************/

kd_tile *kd_stream::access_tile(kdu_coords t_idx)
{
  ensure_expanded();
  kdu_coords rel_idx = t_idx - tile_indices.pos;
  assert((rel_idx.x >= 0) && (rel_idx.x < tile_indices.size.x) &&
         (rel_idx.y >= 0) && (rel_idx.y < tile_indices.size.y));
  int tnum = rel_idx.x + (rel_idx.y * tile_indices.size.x);
  assert(tiles != NULL);
  kd_tile *tp = tiles + tnum;
  if (!tp->have_params)
    { 
      if (!source)
        serve->attach_stream(this);
      init_tile(tp,t_idx); // Leaves `tp->interchange' open
    }
  assert(tp->have_params && (tp->tnum == tnum) && (tp->t_idx == t_idx));
  return tp;  
}

/*****************************************************************************/
/*                           kd_stream::open_tile                            */
/*****************************************************************************/

void kd_stream::open_tile(kd_tile *tp, bool attach_to_source,
                          kdu_dims *structure_region)
{
  assert((tp->stream == this) && (tp == (tiles + tp->tnum)));
  if (structure_region != NULL)
    { // Looking for access to the `structure' interface only
      if (last_structure_region != *structure_region)
        { // Apply input restrictions first
          last_structure_region = *structure_region;
          structure.apply_input_restrictions(0,0,0,0,&last_structure_region,
                                             KDU_WANT_OUTPUT_COMPONENTS);
        }
      if (!tp->structure)
        tp->structure = structure.open_tile(tp->t_idx);
      return;
    }
  if (attach_to_source)
    { 
      assert(is_locked && source.exists());
      assert(!tp->source); // Source tiles should never be left open!
      tp->source = source.open_tile(tp->t_idx);
      tp->prev_attached = NULL;
      if ((tp->next_attached = attached_tiles) != NULL)
        attached_tiles->prev_attached = tp;
      attached_tiles = tp;
    }
  if (!tp->interchange)
    tp->interchange = interchange.open_tile(tp->t_idx);
}

/*****************************************************************************/
/*                          kd_stream::close_tile                            */
/*****************************************************************************/

void kd_stream::close_tile(kd_tile *tp)
{
  if (tp->source.exists())
    {
      assert(is_locked);
      tp->source.close();
      if (tp->prev_attached == NULL)
        { 
          assert(tp == attached_tiles);
          attached_tiles = tp->next_attached;
        }
      else
        tp->prev_attached->next_attached = tp->next_attached;
      if (tp->next_attached != NULL)
        tp->next_attached->prev_attached = tp->prev_attached;
      tp->next_attached = tp->prev_attached = NULL;
    }
  if (tp->structure.exists())
    tp->structure.close();
  if (tp->interchange.exists() &&
      ((tp->num_active_precincts | tp->num_tile_binrefs) == 0))
    tp->interchange.close();
}

/*****************************************************************************/
/*                      kd_stream::close_attached_tiles                      */
/*****************************************************************************/

void kd_stream::close_attached_tiles()
{
  kd_tile *tp;
  while ((tp=attached_tiles) != NULL)
    { 
      if ((attached_tiles = tp->next_attached) != NULL)
        attached_tiles->prev_attached = NULL;
      tp->next_attached = NULL;
      assert((tp->prev_attached == NULL) && tp->source.exists());
      tp->source.close();
      if (tp->structure.exists())
        tp->structure.close();
      if (tp->interchange.exists())
        tp->interchange.close();
    }
}

/*****************************************************************************/
/*                     kd_stream::adjust_completed_tiles                     */
/*****************************************************************************/

void kd_stream::adjust_completed_tiles(kd_tile *tp, bool was_complete)
{
  assert((tp->completed_precincts >= 0) &&
         (tp->completed_precincts <= tp->total_precincts));
  bool is_complete = tp->check_complete();
  if (is_complete && !was_complete)
    {
      completed_tiles++;
      serve->adjust_completed_streams(this,false);
    }
  else if (was_complete && !is_complete)
    { 
      bool str_was_complete = this->check_complete();
      completed_tiles--;
      serve->adjust_completed_streams(this,str_was_complete);
    }
}

/*****************************************************************************/
/*                        kd_stream::erase_cache_model                       */
/*****************************************************************************/

void kd_stream::erase_cache_model()
{
  if (!is_modeled)
    return;
  is_modeled = false;
  bool was_complete = this->check_complete();
  dispatched_header_span = sim_header_span = 0;
  header_span_complete = sim_header_span_complete = 0;
  kd_model_hole_server *hole_server = serve->hole_server;
  if (dispatched_header_holes != NULL)
    { 
      kd_model_hole *holes = dispatched_header_holes;
      dispatched_header_holes = NULL;
      hole_server->release_holes(holes);
    }
  completed_tiles = 0;
  serve->adjust_completed_streams(this,was_complete);
  kd_tile *tp;
  while ((tp=modeled_tiles) != NULL)
    { 
      modeled_tiles = tp->next_modeled;
      assert(tp->is_modeled != 0);
      tp->dispatched_header_span = tp->sim_header_span = 0;
      tp->header_span_complete = tp->sim_header_span_complete = 0;
      tp->is_modeled = 0;  tp->next_modeled = NULL;
      if (tp->dispatched_header_holes != NULL)
        { 
          kd_model_hole *holes = tp->dispatched_header_holes;
          tp->dispatched_header_holes = NULL;
          hole_server->release_holes(holes);
        }
      kd_resolution *rp;
      while ((rp=tp->modeled_resolutions) != NULL)
        { 
          tp->modeled_resolutions = rp->next_modeled;
          assert(rp->is_modeled);
          rp->is_modeled = false;  rp->next_modeled = NULL;
          rp->free_active_pblocks();
        }
      tp->completed_precincts = 0;
    }
}

/*****************************************************************************/
/*               kd_stream::process_header_model_instructions                */
/*****************************************************************************/

void kd_stream::process_header_model_instructions(int tnum, int i_buf[],
                                                  int i_cnt)
{
  assert(i_cnt <= 2);
  int val;
  bool additive = (i_cnt > 0) && (i_buf[0] != 0);
  bool subtractive = (i_cnt == 2) && (i_buf[1] > 0);
  if (!(additive || subtractive))
    return; // Nothing to do
  
  if (additive)
    this->is_modeled = true; // Just in case this was not set already
  if (tnum < 0)
    { 
      bool was_complete = this->check_complete();
      if ((i_cnt > 0) && ((val = i_buf[0]) != 0))
        { // Process additive instruction
          if (val < 0)
            { // Header data-bin fully cached
              if (dispatched_header_holes != NULL)
                { 
                  serve->hole_server->release_holes(dispatched_header_holes);
                  dispatched_header_holes = NULL;
                }
              dispatched_header_span = sim_header_span = main_header_bytes;
              header_span_complete = sim_header_span_complete = 1;
            }
          else
            { // Header data-bin has at least val bytes
              if (dispatched_header_holes != NULL)
                dispatched_header_holes =
                  serve->hole_server->trim_for_fill_bytes(
                                           dispatched_header_holes,val);
              if (dispatched_header_span < val)
                dispatched_header_span = val;
              if (sim_header_span < val)
                sim_header_span = val;
            }
        }
      if ((i_cnt > 1) && ((val = i_buf[1]) > 0))
        { // Header data-bin has less than val bytes
          val--; // Convert to inclusive upper bound
          if (dispatched_header_holes != NULL)
            dispatched_header_holes =
              serve->hole_server->trim_for_max_bytes(dispatched_header_holes,
                                                     val);
          if (dispatched_header_span > val)
            { dispatched_header_span = val;
              header_span_complete = sim_header_span_complete = 0; }
          if (sim_header_span > val)
            sim_header_span = val;
        }
      serve->adjust_completed_streams(this,was_complete);
      return;
    }
  
  if (tiles == NULL)
    { // Stream is collapsed
      if (!additive)
        { // No need to expand the stream just to apply subtractive cache
          // model statements
          return;
        }
      ensure_expanded();
    }
  if (tnum < total_tiles)
    { 
      kd_tile *tp = tiles + tnum;
      if (!tp->have_params)
        { // Need to instantiate the tile
          if (!additive)
            { // No need to instantiate the tile just to apply subtractive
              // cache model statements
              tp->dispatched_header_span = tp->sim_header_span = 0;
              tp->header_span_complete = tp->sim_header_span_complete = 0;
                // The above statements are not really required, since a
                // non-instantiated tile should not have any meaningful cache
                // model information at all, but there is no harm in being
                // quite sure.
              return;
            }
          if (!source)
            serve->attach_stream(this);
          kdu_coords t_idx;
          t_idx.y = tnum / tile_indices.size.x;
          t_idx.x = tnum - t_idx.y*tile_indices.size.x;
          init_tile(tp,t_idx);
          close_tile(tp); // Close all resources opened to instantiate tile
        }
      if (!tp->is_modeled)
        { // Mark this tile as modeled
          tp->is_modeled = true;
          tp->next_modeled = this->modeled_tiles;
          this->modeled_tiles = tp;
        }
      bool was_complete = tp->check_complete();
      if ((i_cnt > 0) && ((val = i_buf[0]) != 0))
        { // Process additive instruction
          if (val < 0)
            { // Header data-bin fully cached
              if (tp->dispatched_header_holes != NULL)
                { 
                  kd_model_hole *holes = tp->dispatched_header_holes;
                  tp->dispatched_header_holes = NULL;
                  serve->hole_server->release_holes(holes);
                }
              tp->dispatched_header_span =
                tp->sim_header_span = tp->header_bytes;
              tp->header_span_complete = tp->sim_header_span_complete = 1;
            }
          else
            { // Header data-bin has at least val bytes
              if (tp->dispatched_header_holes != NULL)
                tp->dispatched_header_holes =
                  serve->hole_server->trim_for_fill_bytes(
                                           tp->dispatched_header_holes,val);
              if (tp->dispatched_header_span < val)
                tp->dispatched_header_span = val;
              if (tp->sim_header_span < val)
                tp->sim_header_span = val;
            }
        }
      if ((i_cnt > 1) && ((val = i_buf[1]) > 0))
        { // Header data-bin has less than val bytes
          val--; // Convert to inclusive upper bound
          if (tp->dispatched_header_holes != NULL)
            tp->dispatched_header_holes =
              serve->hole_server->trim_for_max_bytes(
                                       tp->dispatched_header_holes,val);
          if (tp->dispatched_header_span > val)
            { 
              tp->dispatched_header_span = val;
              tp->header_span_complete = tp->sim_header_span_complete = 0;
            }
          if (tp->sim_header_span > val)
            tp->sim_header_span = val;
        }
      adjust_completed_tiles(tp,was_complete);
    }
  else
    assert(0);
}

/*****************************************************************************/
/*              kd_stream::process_precinct_model_instructions               */
/*****************************************************************************/

void kd_stream::process_precinct_model_instructions(int tnum, int cnum,
                                                    int rnum, kdu_long pnum,
                                                    int i_buf[], int i_cnt,
                                                    bool additive)
{
  if (tiles == NULL)
    { 
      if (!additive)
        return;
      serve->attach_stream(this);
    }
  if (additive)
    this->is_modeled = true; // In case not set already
  if (tnum < 0)
    { // Compute `tnum' and `cnum' and factor these out from `pnum'
      kdu_long s = pnum / pid_gap;
      kdu_long tc = pnum - s*pid_gap;
      cnum = (int)(tc / num_components);
      tnum = (int)(tc - cnum*num_components);
      pnum = s;
    }
  if ((tnum >= total_tiles) || (cnum < 0) || (cnum >= num_components))
    return;

  kd_tile *tp = tiles + tnum;
  if (!tp->have_params)
    { // Need to instantiate the tile
      if (!additive)
        { // No need to instantiate the tile just to apply subractive cache
          // model statements
          return;
        }
      if (!source)
        serve->attach_stream(this);
      kdu_coords t_idx;
      t_idx.y = tnum / tile_indices.size.x;
      t_idx.x = tnum - t_idx.y*tile_indices.size.x;
      init_tile(tp,t_idx);
      close_tile(tp); // Close all resources opened to instantiate tile
    }
  if (additive && !tp->is_modeled)
    { // Mark this tile as modeled
      tp->is_modeled = true;
      tp->next_modeled = this->modeled_tiles;
      this->modeled_tiles = tp;      
    }

  kd_tile_comp *tc = tp->comps + cnum;
  kd_resolution *rp;
  if (rnum < 0)
    { // Find resolution and factor it out of `pnum'
      for (rnum=0, rp=tc->res; rnum < tc->num_resolutions; rnum++, rp++)
        { 
          kdu_long nprecs = rp->precinct_indices.area();
          if (pnum < nprecs)
            break;
          pnum -= nprecs;
        }
    }
  else
    rp = tc->res + rnum;
  if ((rnum >= tc->num_resolutions) || (pnum < 0))
    return; // Illegal resolution
  if (additive && !rp->is_modeled)
    { // Add to list of modeled resolutions
      rp->is_modeled = true;
      rp->next_modeled = tp->modeled_resolutions;
      tp->modeled_resolutions = rp;
    }
  kdu_coords p_idx;
  kdu_long py = pnum / rp->precinct_indices.size.x;
  p_idx.x = (int)(pnum - py*rp->precinct_indices.size.x);
  p_idx.y = (int) py;
  if (py >= rp->precinct_indices.size.y)
    return; // Illegal precinct index
  kd_precinct_model *prec = rp->access_precinct(p_idx,additive);
  if (prec != NULL)
    rp->process_model_instructions(prec,i_buf,i_cnt,serve->hole_server);
}

/*****************************************************************************/
/*                           kd_stream::init_tile                            */
/*****************************************************************************/

void kd_stream::init_tile(kd_tile *tp, kdu_coords t_idx)
{
  if (tp->have_params)
    {
      assert(tp->comps != NULL);
      return;
    }
  
  tp->stream = this;
  tp->t_idx = t_idx;
  tp->tnum = (int)(tp - tiles);
  
  // Copy parameters
  kdu_params *src_params = source.access_siz();
  kdu_params *ichg_params = interchange.access_siz();
  if (is_locked)
    { // Create tile to ensure all tile header parameters are loaded
      source.create_tile(t_idx);
      ichg_params->copy_from(src_params,tp->tnum,tp->tnum); // Get tile params
    }
  else
    { // Same as above, but lock the codestream first and then unlock it
      kd_stream *me = this;
      try {
        serve->lock_codestreams(1,&me);
        source.create_tile(t_idx);
        ichg_params->copy_from(src_params,tp->tnum,
                               tp->tnum); // Get tile params
        serve->unlock_codestreams(1,&me);
      } catch (...) {
        fprintf(stderr,
                "Received unusual exception during \"init_tile\".\n");
        if (is_locked)
          serve->unlock_codestreams(1,&me);
        throw;
      }
    }
  
  // Fix up precinct dimensions and eliminate any POC marker segments
  kdu_params *ichg_cod = ichg_params->access_cluster(COD_params);
  kdu_params *ichg_poc = ichg_params->access_cluster(POC_params);
  kdu_params *tile_poc = ichg_poc->access_unique(tp->tnum,-1);
  if (tile_poc != NULL)
    tile_poc->delete_unparsed_attribute(Porder);
  int c;
  for (c=-1; c < num_components; c++)
    {
      kdu_params *tcomp_cod = ichg_cod->access_unique(tp->tnum,c);
      if (tcomp_cod != NULL)
        set_precinct_dimensions(tcomp_cod);
    }
  ichg_params->finalize_all(tp->tnum,false);
  tp->header_bytes = ichg_params->generate_marker_segments(NULL,tp->tnum,0);
  
  kdu_params *struc_params = structure.access_siz();
  struc_params->copy_from(ichg_params,tp->tnum,tp->tnum);
  struc_params->finalize_all(tp->tnum,false);
  tp->have_params = true;
  
  if (tp->comps != NULL)
    return; // All done; cache model existed already
  
  // Create the rest of the tile structure here
  tp->interchange = interchange.open_tile(tp->t_idx);
  tp->uses_ycc = (tp->interchange.get_ycc())?1:0;
  tp->num_layers = tp->interchange.get_num_layers();
  if (tp->num_layers >= 0xFFFF)
    tp->num_layers = 0xFFFE; // Reserve 0xFFFF for special signalling
  tp->comps = new kd_tile_comp[num_components];
  tp->total_precincts = 0;
  for (c=0; c < num_components; c++)
    {
      kd_tile_comp *tc = tp->comps + c;
      tc->c_idx = c;
      tc->tile = tp;
      kdu_tile_comp tci = tp->interchange.access_component(c);
      tc->num_resolutions = tci.get_num_resolutions();
      tc->res = new kd_resolution[tc->num_resolutions];
      int r;
      for (r=0; r < tc->num_resolutions; r++)
        { 
          kd_resolution *rp = tc->res + r;
          rp->r_idx = r;
          rp->tile_comp = tc;
          kdu_resolution rpi = tci.access_resolution(r);
          rpi.get_valid_precincts(rp->precinct_indices);
          rp->pid_base = rpi.get_precinct_id(rp->precinct_indices.pos);
          kdu_long res_precincts = rp->precinct_indices.area();
          tp->total_precincts += (int) res_precincts;
          rp->log_pblock_size = 4;
          while ((rp->log_pblock_size > 0) &&
                 (rp->precinct_indices.size.x<=(1<<(rp->log_pblock_size-1))) &&
                 (rp->precinct_indices.size.y<=(1<<(rp->log_pblock_size-1))))
            rp->log_pblock_size--;
          rp->pblock_size = (1 << rp->log_pblock_size);
          rp->num_pblocks.x =
            1 + ((rp->precinct_indices.size.x-1)>>rp->log_pblock_size);
          rp->num_pblocks.y =
            1 + ((rp->precinct_indices.size.y-1)>>rp->log_pblock_size);
          int nblks = rp->num_pblocks.x*rp->num_pblocks.y;
          rp->pblocks = new kd_precinct_block *[nblks];
          memset(rp->pblocks,0,sizeof(kd_precinct_block *)*nblks);
          
          // Discover the number of samples in each type of precinct
          rp->tl_samples = rp->top_samples = rp->tr_samples = 0;
          rp->left_samples = rp->centre_samples = rp->right_samples = 0;
          rp->bl_samples = rp->bottom_samples = rp->br_samples = 0;
          if (res_precincts < 1)
            continue;
          kdu_coords idx_l, idx_c, idx_r;
          idx_l = idx_c = idx_r = rp->precinct_indices.pos;
          rp->tl_samples = (int) rpi.get_precinct_samples(idx_l);
          rp->tl_samples = (rp->tl_samples > 1)?rp->tl_samples:1;
          if (rp->precinct_indices.size.x > 1)
            { 
              idx_c.x++;
              idx_r.x += rp->precinct_indices.size.x - 1;
              rp->top_samples = (int) rpi.get_precinct_samples(idx_c);
              rp->tr_samples = (int) rpi.get_precinct_samples(idx_r);
              rp->top_samples = (rp->top_samples > 1)?rp->top_samples:1;
              rp->tr_samples = (rp->tr_samples > 1)?rp->tr_samples:1;
            }
          else
            rp->tr_samples = rp->top_samples = rp->tl_samples;
          if (rp->precinct_indices.size.y > 1)
            { 
              idx_l.y = idx_c.y = idx_r.y = rp->precinct_indices.pos.y+1;
              rp->left_samples = (int) rpi.get_precinct_samples(idx_l);
              rp->centre_samples = (int) rpi.get_precinct_samples(idx_c);
              rp->right_samples = (int) rpi.get_precinct_samples(idx_r);
              rp->left_samples = (rp->left_samples > 1)?rp->left_samples:1;
              rp->centre_samples=(rp->centre_samples > 1)?rp->centre_samples:1;
              rp->right_samples = (rp->right_samples > 1)?rp->right_samples:1;
            }
          else
            { rp->left_samples = rp->tl_samples;
              rp->centre_samples = rp->top_samples;
              rp->right_samples = rp->tr_samples; }
          if (rp->precinct_indices.size.y > 2)
            { 
              idx_l.y = idx_c.y = idx_r.y = 
                rp->precinct_indices.pos.y+rp->precinct_indices.size.y-1;
              rp->bl_samples = (int) rpi.get_precinct_samples(idx_l);
              rp->bottom_samples = (int) rpi.get_precinct_samples(idx_c);
              rp->br_samples = (int) rpi.get_precinct_samples(idx_r);
              rp->bl_samples = (rp->bl_samples > 1)?rp->bl_samples:1;
              rp->bottom_samples=(rp->bottom_samples > 1)?rp->bottom_samples:1;
              rp->br_samples=(rp->br_samples > 1)?rp->br_samples:1;
            }
          else
            { rp->bl_samples = rp->left_samples;
              rp->bottom_samples = rp->centre_samples;
              rp->br_samples = rp->right_samples; }
        }
    }
  tp->interchange.close();
}


/* ========================================================================= */
/*                             kd_precinct_model                             */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_precinct_model::make_active                      */
/*****************************************************************************/

void kd_precinct_model::make_active(kd_active_precinct *active,
                                    kd_model_hole_server *hole_server)
{ 
  assert(!(state & 1));
  if (active->dispatched_holes != NULL)
    { 
      hole_server->release_holes(active->dispatched_holes);
      active->dispatched_holes = NULL;
    }
  kdu_uint32 hole = (kdu_uint32)(state >> 32);
  if (hole != 0)
    { 
      kdu_uint16 start=(kdu_uint16)hole, lim=(kdu_uint16)(hole>>16);
      assert(start < lim);
      active->dispatched_holes = hole_server->add_hole(NULL,start,lim);
    }
  active->span_complete = (state & 2)?1:0;
  active->dispatched_span = ((int) state) >> 2;
  state = 1 + _addr_to_kdu_long(active);
  if (active->dispatched_span < 0)
    active->num_packets = (kdu_uint16)(-active->dispatched_span);
  if (active->dispatched_span != 0)
    active->num_packet_bytes = -1; // So `load_active_precincts' will work
                                   // everything out from `dispatched_span'
}

/*****************************************************************************/
/*                     kd_precinct_model::make_inactive                      */
/*****************************************************************************/

kd_active_precinct *kd_precinct_model::make_inactive()
{
  if (!(state & 1))
    return NULL; // Already inactive
  kd_active_precinct *active=(kd_active_precinct *)_kdu_long_to_addr(state-1);
  assert(active->cache == this);
  int span=active->dispatched_span, span_complete=active->span_complete;
  int hs=0, hl=0; // Used to capture a single hole
  kd_model_hole *holes = active->dispatched_holes;
  if (span < 0)
    { // We have not had a chance to invoke `load_active_precincts' since the
      // `make_active' function was called
      span = -((int) active->num_packets);
      if (holes != NULL)
        { // Merge any holes into a single big one
          span_complete = 0; // Should be 0 already
          hs = holes->start;
          hl = holes->lim;
          while (holes->next != NULL)
            { 
              holes = holes->next;
              assert(holes->lim > hl);
              hl = holes->lim;
            }
        }
    }
  else
    { // Regular case, in which `state' will record the current value of `span'
      if (holes != NULL)
        { 
          hs = holes->start;
          hl = holes->lim;
          while (holes->next != NULL)
            { 
              holes = holes->next;
              int pre_waste = ((int) holes->start) - hl; // Bytes we waste if
                                     // merge the two holes into a bigger one
              int post_waste = span - (int) holes->start; // Bytes we waste if
                       // discard everything from the start of this extra hole
              assert((pre_waste > 0) && (post_waste > 0));
              if (post_waste < pre_waste)
                { span -= post_waste; span_complete=0; break; }
              hl = holes->lim;
            }
        }
    }
  state = ((span_complete)?2:0) | (kdu_uint32)(span<<2);
  state |= ((kdu_long)(hs + (hl<<16))) << 32;
  return active;
}

/*****************************************************************************/
/*                       kd_precinct_model::add_hole                         */
/*****************************************************************************/

void
  kd_precinct_model::add_hole(kdu_uint16 start, kdu_uint16 lim,
                               kd_model_hole_server *hole_server)
{ 
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      if (active->num_packet_bytes > (int) start)
        { // Have to go back and work out number of packets from scratch
          if (active->interchange.exists())
              active->interchange.restart();
          active->num_packets = active->sim_packets = 0;
          active->num_packet_bytes = -1;
          active->sim_span = active->sim_packet_bytes = 0;
        }
      else if (active->sim_span > (int) start)
        { 
          active->sim_span = (int) start;
          if (active->sim_packets != active->num_packets)
            { // Num simulated packets has been getting ahead so we can save
              // the cost of recalculating all the time, until there are
              // enough bytes to send a message; but the introduction of
              // new holes could mess up the `simulate_increments' function.
              assert(active->sim_packets > active->num_packets);
              active->sim_packets = active->num_packets;
              active->sim_packet_bytes = active->num_packet_bytes;
              if (active->interchange.exists())
                active->interchange.restart();
            }
        }
      active->dispatched_holes =
        hole_server->add_hole(active->dispatched_holes,start,lim);
      return;
    }
  
  kdu_uint32 aux_val = (kdu_uint32)(state>>32); // Get `hs' and `hl' parameters
  if (aux_val != 0)
    { // Adjust `start' and `lim' to include the existing hole
      kdu_uint32 hs=(aux_val & 0xFFFF), hl=(aux_val>>16); 
      if (hs < start)
        start = hs;
      if (hl > lim)
        lim = hl;
    }
  aux_val = (((kdu_uint32) lim) << 16) | (kdu_uint32) start;
  state = (kdu_uint32) state; // Keep just the 32 LSB's
  state |= ((kdu_long) aux_val) << 32;
}

/*****************************************************************************/
/*                     kd_precinct_model::set_max_bytes                      */
/*****************************************************************************/

void
  kd_precinct_model::set_max_bytes(int max_bytes,
                                   kd_model_hole_server *hole_server)
{ 
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      if ((max_bytes <= 0) ||
          ((active->dispatched_span < 0) && !active->span_complete))
        { // May have a `num_packets' value without any associated number of
          // dispatched bytes yet determined; only safe thing is to
          // empty the cache model here.
          if (active->interchange.exists())
            active->interchange.restart();
          active->dispatched_span = active->sim_span = 0;
          active->num_packet_bytes = active->sim_packet_bytes = 0;
          active->num_packets = active->sim_packets = 0;
        }
      else if ((max_bytes < active->dispatched_span) ||
               (active->span_complete && (active->dispatched_span < 0)))
        { // Safe to set number of dispatched bytes to `max_bytes
          active->dispatched_span = max_bytes;
          if (active->sim_span > max_bytes)
            active->sim_span = max_bytes;
          if (active->num_packet_bytes > max_bytes)
            { // Can't know the number of packets until `load_active_precinct'
              active->num_packet_bytes = -1;
              active->num_packets = active->sim_packets = 0;
              active->sim_packet_bytes = 0;
            }
          else if (active->num_packet_bytes < 0)
            { // In case we had a completed precinct with a known number of
              // packets but unknown number of bytes
              active->num_packets = active->sim_packets = 0;
              active->sim_packet_bytes = 0;
            }
          if (active->sim_packets != active->num_packets)
            { // Num simulated packets has been getting ahead so we can save
              // the cost of recalculating all the time, until there are
              // enough bytes to send a message; but the introduction of
              // new holes could mess up the `simulate_increments' function.
              assert(active->sim_packets > active->num_packets);
              active->sim_packets = active->num_packets;
              active->sim_packet_bytes = active->num_packet_bytes;
              if (active->interchange.exists())
                active->interchange.restart();
            }
        }
      active->span_complete = active->sim_span_complete = 0; // Safest
      if (active->dispatched_holes != NULL)
        active->dispatched_holes =
          hole_server->trim_for_max_bytes(active->dispatched_holes,max_bytes);
      return;
    }

  int val = ((int) state) >> 1; // Keep `span' value and `span_complete' flag
  kdu_uint32 aux_val=(kdu_uint32)(state>>32); // Keep `hs' and `hl' parameters
  if ((max_bytes <= 0) || ((val < 0) && !(val & 1)))
    { // May have an incomplete packet count, but only safe thing is to empty
      // the cache model
      state = 0;
    }
  else if ((val < 0) || (val > (max_bytes<<1)))
    { // need to change recorded byte count to `max_bytes'
      val = (max_bytes<<1);  state = ((kdu_uint32) val) << 1;
      if (aux_val != 0)
        { 
          kdu_uint32 hs=(aux_val & 0xFFFF), hl=(aux_val>>16); 
          if (((kdu_uint32) max_bytes) > hs)
            { // Otherwise there is no hole left
              if (((kdu_uint32) max_bytes) < hl)
                hl = max_bytes;
              state |= ((kdu_long)(hs + (hl<<16))) << 32;
            }
        }
    }
  else
    state &= ~((kdu_long) 2); // Safest to clear the completeness flag
}

/*****************************************************************************/
/*                   kd_precinct_model::set_max_packets                      */
/*****************************************************************************/

void
  kd_precinct_model::set_max_packets(int max_packets,
                                     kd_model_hole_server *hole_server)
{
  if (max_packets < 0)
    max_packets = 0;
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      assert(max_packets <= (int) active->max_packets);
      if (max_packets == 0)
        { // Discard everything
          if (active->interchange.exists())
            active->interchange.restart();
          active->num_packets = active->sim_packets = 0;
          active->num_packet_bytes = active->sim_packet_bytes = 0;
          active->dispatched_span = active->sim_span = 0;
          if (active->dispatched_holes != NULL)
            { 
              hole_server->release_holes(active->dispatched_holes);
              active->dispatched_holes = NULL;
            }
        }
      else if ((max_packets < (int) active->num_packets) ||
               ((active->num_packet_bytes < 0) &&
                (active->dispatched_span > 0) && active->span_complete))
        { // We can safely set `active->num_packets' to `max_packets'.  We
          // will have to convert packets to byte counts later during
          // `load_active_precinct', so for now just leave any
          // `dispatched_holes' list as-is.
          active->num_packets = active->sim_packets = (kdu_uint16) max_packets;
          active->num_packet_bytes = -1;
          active->dispatched_span = -((int) active->num_packets);
          active->sim_span = active->sim_packet_bytes = 0;
        }
      else if ((max_packets == (int) active->num_packets) &&
               (active->num_packet_bytes >= 0) &&
               (active->dispatched_span > active->num_packet_bytes))
        { // Trimming away partial packets; OK to set `sim_span' to
          // `dispatched_span' here because there cannot be any holes
          // prior to `num_packet_bytes'. No need to restart the `kdu_precinct'
          // interchange interface because we are not trimming away any whole
          // packets that have previously been dispatched.
          active->dispatched_span =
            active->sim_span = active->num_packet_bytes;
        }
      if (active->sim_packets != active->num_packets)
        { // Num simulated packets has been getting ahead so we can save
          // the cost of recalculating all the time, until there are
          // enough bytes to send a message; but the introduction of
          // new holes could mess up the `simulate_increments' function.
          assert(active->sim_packets > active->num_packets);
          active->sim_packets = active->num_packets;
          active->sim_packet_bytes = active->num_packet_bytes;
          if (active->interchange.exists())
            active->interchange.restart();
        }      
      active->span_complete = active->sim_span_complete = 0; // Safest
      return;
    }

  int val = ((int) state) >> 1; // Keep `span' value and `span_complete' flag
  kdu_uint32 aux_val=(kdu_uint32)(state>>32); // Keep `hs' and `hl' parameters
  if ((max_packets <= 0) || ((val >= 0) && !(val & 1)))  
    { // Discard everything; situation too ambiguous otherwise
      state = 0;
    }
  else if ((val < -(max_packets<<1)) || ((val & 1) && (val > 1)))
    { // Have known number of packets (larger than max value) or have a
      // complete precinct with only the byte count known.
      val = -(max_packets<<1);  state = ((kdu_uint32) val) << 1;
      state |= ((kdu_long) aux_val) << 32; // Leave holes alone for now
    }
  else
    state &= ~((kdu_long) 2); // Safest to clear the completeness flag
}

/*****************************************************************************/
/*                     kd_precinct_model::set_min_bytes                      */
/*****************************************************************************/

void
  kd_precinct_model::set_min_bytes(int min_bytes,
                                   kd_model_hole_server *hole_server)
{
  assert(min_bytes >= 0);
  if (min_bytes == 0)
    return; // Call has no impact
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      active->span_complete = active->sim_span_complete = 0; // Safest
      if (active->dispatched_span < min_bytes)
        { // Make a change only in this case; changes below will cause
          // `load_active_precinct' to be called, which will update the number
          // of packets and full packet bytes, as required.
          active->dispatched_span = active->sim_span = min_bytes;
          active->num_packet_bytes = -1;
          active->num_packets = active->sim_packets = 0;
          active->sim_packet_bytes = 0;
        }
      if (active->dispatched_holes != NULL)
        active->dispatched_holes =
          hole_server->trim_for_fill_bytes(active->dispatched_holes,min_bytes);
      return;
    }
  
  int val = ((int) state) >> 1; // Keep `span' value and `span_complete' flag
  kdu_uint32 aux_val=(kdu_uint32)(state>>32); // Keep `hs' and `hl' parameters
  if ((val >= 0) && (val <= (min_bytes<<1)))
    { // State records a byte span of `min_bytes' and any holes disappear
      val = (min_bytes<<1); state = ((kdu_uint32) val) << 1;
    }
  else
    { // Erase completeness flag (safest) and fill any holes below `min_bytes'
      val &= ~1;  state = ((kdu_uint32) val) << 1;
      if (aux_val != 0)
        { 
          kdu_uint32 hs=(aux_val & 0xFFFF), hl=(aux_val>>16);
          if (min_bytes <= ((int) hs))
            state |= ((kdu_long) aux_val) << 32; // Keep hole info as-is
          else if (min_bytes < ((int) hl))
            { // Modify start of existing hole
              hs = (kdu_uint16) min_bytes;
              state |= ((kdu_long)(hs + (hl<<16))) << 32;
            }
        }
    }
}

/*****************************************************************************/
/*                    kd_precinct_model::set_min_packets                     */
/*****************************************************************************/

void
  kd_precinct_model::set_min_packets(int min_packets,
                                     kd_model_hole_server *hole_server)
{
  assert(min_packets >= 0);
  if (min_packets == 0)
    return; // Call has no impact
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      assert(min_packets <= (int) active->max_packets);
      active->span_complete = active->sim_span_complete = 0; // Safest
      if ((min_packets > (int) active->num_packets) &&
          ((active->num_packet_bytes >= 0) || (active->dispatched_span <= 0)))
        { // We have a known number of packets which is less than `min_packets'
          // The code below will cause `load_active_precinct' to be called to
          // regenerate the missing information
          active->num_packets = active->sim_packets = (kdu_uint16) min_packets;
          active->dispatched_span = -((int) active->num_packets);
          active->num_packet_bytes = -1;
          active->sim_span = active->sim_packet_bytes = 0;
        }
      return;
    }
  
  int val = ((int) state) >> 1; // Keep `span' value and `span_complete' flag
  if ((val <= 0) && (val > -(min_packets<<1)))
    { 
      val = -(min_packets<<1);  val <<= 1;
      state = (state & (((kdu_uint64) -1) << 32)) | (kdu_uint32) val;
    }
  else
    state &= ~((kdu_long) 2); // Safest to clear the completion flag
}

/*****************************************************************************/
/*                     kd_precinct_model::set_complete                       */
/*****************************************************************************/

void kd_precinct_model::set_complete(int tile_layers,
                                     kd_model_hole_server *hole_server)
{
  if (state & 1)
    { // Have an active representation
      kd_active_precinct *active =
        (kd_active_precinct *) _kdu_long_to_addr(state-1);
      assert(tile_layers == (int) active->max_packets);
      if (!active->span_complete)
        { // We can mark as complete, but we cannot know the number of bytes,
          // so we set `num_packet_bytes' to -1, indicating that
          // `load_active_precinct' would need to be called to determine the
          // number of bytes -- actually, we will never need to do this.
          active->span_complete = active->sim_span_complete = 1;
          active->num_packets = active->sim_packets = (kdu_uint16) tile_layers;
          active->dispatched_span = -((int) active->num_packets);
          active->num_packet_bytes = (active->dispatched_span==0)?0:-1;
          active->sim_packet_bytes = 0;
          active->sim_span = active->sim_packet_bytes = 0;
        }
      if (active->dispatched_holes != NULL)
        { 
          hole_server->release_holes(active->dispatched_holes);
          active->dispatched_holes = NULL;
        }
      return;
    }
  
  if (!(state & 2))
    { // We can mark as complete, remove holes and record number of packets
      // as `tile_layers'
      state = (kdu_uint32)(2 - (tile_layers<<2));
    }
}


/* ========================================================================= */
/*                               kd_resolution                               */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_resolution::get_new_pblock                        */
/*****************************************************************************/

kd_precinct_block *kd_resolution::get_new_pblock()
{ 
  kd_serve *serve = tile_comp->tile->stream->serve;
  kd_precinct_block *result=serve->pblock_server->get_pblock(log_pblock_size);
  return result;
}

/*****************************************************************************/
/*                    kd_resolution::free_active_pblocks                     */
/*****************************************************************************/

void kd_resolution::free_active_pblocks()
{
  kd_serve *serve = tile_comp->tile->stream->serve;
  kd_precinct_block **pb;
  while ((pb=active_pblocks) != NULL)
    { 
      kd_precinct_block *pblk = *pb;   *pb = NULL;
      active_pblocks = pblk->active_next;
      serve->pblock_server->release_pblock(pblk,log_pblock_size);
    }
}

/*****************************************************************************/
/*             kd_resolution::process_pblock_model_instructions              */
/*****************************************************************************/

void kd_resolution::process_pblock_model_instructions(kdu_dims region,
                                                      int i_buf[])
{
  assert((((region.pos.x | region.pos.y) & (pblock_size-1)) == 0) &&
         (region.size.x <= pblock_size) && (region.size.y <= pblock_size));
  int b_x = region.pos.x >> log_pblock_size;
  int b_y = region.pos.y >> log_pblock_size;
  kd_precinct_block *pb = pblocks[b_x + b_y*num_pblocks.x];
  assert(pb != NULL);
  int r, c;
  kd_model_hole_server *hole_server =
    tile_comp->tile->stream->serve->hole_server;
  kd_precinct_model *prec, *prec_p=pb->precincts;
  for (r=region.size.y; r > 0; r--, prec_p += pblock_size)
    for (prec=prec_p, c=region.size.x; c > 0; c--, prec++, i_buf+=2)
      { // Apply the 2 instructions at `i_buf' to the cache model at `prec'
        if (i_buf[0] | i_buf[1])
          process_model_instructions(prec,i_buf,2,hole_server);
      }
}
        
/*****************************************************************************/
/*                 kd_resolution::process_model_instructions                 */
/*****************************************************************************/

void
  kd_resolution::process_model_instructions(kd_precinct_model *prec,
                                            int i_buf[], int i_cnt,
                                            kd_model_hole_server *hole_server)
{
  assert(i_cnt <= 2);
  bool additive = ((i_cnt > 0) && (i_buf[0] != 0));
  bool subtractive = ((i_cnt > 1) && (i_buf[1] != 0));
  if (!(additive || subtractive))
    return;
  kd_tile *tp = tile_comp->tile;
  assert(tp->num_layers < 0xFFFF);
  int val, tile_layers = tp->num_layers;
  bool is_complete = prec->check_complete();
  bool was_complete = is_complete;
  if (additive)
    { 
      if ((val = i_buf[0]) < 0)
        { 
          if (!is_complete)
            prec->set_complete(tile_layers,hole_server);
          is_complete = true;
        }
      else if (!(val & 1))
        { // Precinct data-bin has at least val/2 bytes
          val >>= 1;
          prec->set_min_bytes(val,hole_server);
          is_complete = false; // Above call also marks precinct as incomplete
        }
      else
        { // Precinct data-bin has at least val/2 quality layers
          val >>= 1;
          if (val > tile_layers)
            val = tile_layers;
          prec->set_min_packets(val,hole_server);
          is_complete = false; // Above call also marks precinct as incomplete
        }
    }
  if (subtractive)
    { // Process subtractive instruction
      if (!((val = i_buf[1]) & 1))
        { // Precinct data-bin has less than val/2 bytes
          val = (val >> 1) - 1; // Now `val' is inclusive upper bound
          prec->set_max_bytes(val,hole_server);
          is_complete = false; // Above call also marks precinct as incomplete
        }
      else
        { // Precinct data-bin has less than val/2 quality layers
          val = (val >> 1) - 1; // Now `val' is inclusive upper bound
          prec->set_max_packets(val,hole_server);
          is_complete = false; // Above call also marks precinct as incomplete
        }
    }
  
  kd_stream *str = tp->stream;
  bool tp_was_complete = tp->check_complete();
  if (was_complete && !is_complete)
    { 
      tp->completed_precincts--;
      str->adjust_completed_tiles(tp,tp_was_complete);
    }
  else if (is_complete && !was_complete)
    { 
      tp->completed_precincts++;
      str->adjust_completed_tiles(tp,tp_was_complete);
    }
}


/* ========================================================================= */
/*                             kd_window_context                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    kd_window_context::kd_window_context                   */
/*****************************************************************************/

kd_window_context::kd_window_context(kd_serve *owner, int context_id)
{
  this->context_id = context_id;
  this->serve = owner;
  stream_windows = new_stream_windows = old_stream_windows = NULL;
  first_active_stream_window = last_active_stream_window = NULL;
  active_streams_locked = false;
  num_active_streams = 0;
  scanned_precinct_bytes = scanned_precinct_samples = 0;
  scanned_incomplete_metabins = scanned_incomplete_codestream_bins = 0;
  int s;
  for (s=0; s < KD_MAX_ACTIVE_CODESTREAMS; s++)
    { 
      active_streams[s] = NULL;
      num_active_stream_refs[s] = 0;
    }
  num_streams = 0;
  for (s=0; s < KD_MAX_WINDOW_CODESTREAMS; s++)
    { 
      stream_indices[s] = 0;
      first_stream_windows[s] = last_stream_windows[s] = NULL;
    }
  window_changed = window_imagery_changed = false;
  update_window_streams = update_window_metadata = false;
  current_model_instructions = NULL;
  codestream_seq_pref = false;
  active_bins = last_active_metabin = NULL;
  old_active_bins = new_active_bins = NULL;
  sweep_extra_discard_levels = 0;
  sweep_start = sweep_next = meta_sweep_next = NULL;
  active_bin_ptr = NULL;
  active_threshold = next_active_threshold = INT_MIN;
  scan_first_layer = true;
  dummy_layer_log_slopes = NULL;
  num_dummy_layer_slopes = 0;
  active_bins_completed = active_metabins_completed = false;
  active_bin_rate_reached = false;
  active_bin_rate_threshold = -1.0; // Unlimited
  extra_data_head = extra_data_tail = NULL;
  extra_data_bytes = 0;
  num_msg_boundaries = max_msg_boundaries = 0;
  msg_boundaries = NULL;
  next = NULL;
}

/*****************************************************************************/
/*                    kd_window_context::~kd_window_context                  */
/*****************************************************************************/

kd_window_context::~kd_window_context()
{
  remove_active_stream_ref(NULL); // Remove all active stream references
  serve->release_codestream_windows(stream_windows);
  stream_windows = NULL;
  serve->release_codestream_windows(new_stream_windows);
  new_stream_windows = NULL;
  serve->release_codestream_windows(old_stream_windows);
  old_stream_windows = NULL;
  serve->release_active_binrefs(active_bins);
  active_bins = last_active_metabin = NULL;
  serve->release_active_binrefs(old_active_bins);
  old_active_bins = NULL;
  serve->release_active_binrefs(new_active_bins);
  new_active_bins = NULL;
  if (dummy_layer_log_slopes != NULL)
    delete[] dummy_layer_log_slopes;
  dummy_layer_log_slopes = NULL;
  while ((extra_data_tail = extra_data_head) != NULL)
    {
      extra_data_head = extra_data_tail->next;
      serve->chunk_server->release_chunk(extra_data_tail);
    }
  extra_data_bytes = 0;
  if (msg_boundaries != NULL)
    delete[] msg_boundaries;
}

/*****************************************************************************/
/*                 kd_window_context::process_window_changes                 */
/*****************************************************************************/

void kd_window_context::process_window_changes()
{
  if (!window_changed)
    return;
  
  if ((stream_windows != NULL) && !window_imagery_changed)
    { 
      assert(!serve->is_stateless);
      last_translated_window.copy_metareq_from(current_window);
      window_changed = window_imagery_changed = false;
      update_window_metadata = true;
      return;
    }
  
  // Save a copy of the window, as requested, prior to translation
  last_translated_window.copy_from(current_window);
  
  // Make sure we are not holding onto any active references in stateless mode
  if (serve->is_stateless)
    { // For the moment, we need to take the steps below in order to safely
      // erase the cache model.
      if (active_bins != NULL)
        { 
          serve->release_active_binrefs(active_bins);
          active_bins = last_active_metabin = NULL;
        }
      for (kd_codestream_window *wp=stream_windows; wp != NULL; wp=wp->next)
        wp->stream->erase_cache_model();
      serve->erase_metadata_cache_model();
      serve->stateless_image_done = false;
    }
  
  // Ensure consistency amongst window dimensions
  if ((current_window.resolution.x <= 0) || (current_window.resolution.y <= 0))
    current_window.resolution = kdu_coords(0,0);
  if ((current_window.region.size.x<=0) || (current_window.region.size.y<=0))
    current_window.region.size = current_window.region.pos = kdu_coords(0,0);
  
  // Make copies of the requested codestreams and codestream contexts so that
  // we can limit them when building the actual window to be served.
  context_set.copy_from(current_window.contexts);
  current_window.contexts.init();
  stream_set.copy_from(current_window.codestreams);
  current_window.codestreams.init();
 
  // Prepare to build a new set of codestream windows
  if (new_stream_windows != NULL)
    { 
      serve->release_codestream_windows(new_stream_windows);
      new_stream_windows = NULL;
    }
  this->num_streams = 0;
  
  // Build an initial set of codestream windows based on the codestreams
  // which are explicitly requested  
  kd_codestream_window *win;
  int c, csn, range_idx, num_stream_ranges, stream_entry;
  const int *stream_ranges =
    serve->target->get_codestream_ranges(num_stream_ranges);
  if (stream_set.is_empty() && context_set.is_empty())
    stream_set.add(stream_ranges[0]);
  kdu_sampled_range csrg, rg;
  for (csn=0; !(csrg=stream_set.get_range(csn)).is_empty(); csn++)
    { 
      if (csrg.context_type == KDU_JPIP_CONTEXT_TRANSLATED)
        continue;
      for (range_idx=0; range_idx < num_stream_ranges; range_idx++)
        { 
          int first_id = stream_ranges[2*range_idx];
          int last_id = stream_ranges[2*range_idx+1];
          if ((first_id > csrg.to) || (last_id < csrg.from))
            continue; // No intersection
          rg = csrg;
          if (first_id > rg.from)
            { 
              c = 1 + (first_id-rg.from-1) / rg.step;
              rg.from += c * rg.step;
              assert(rg.from >= first_id);
            }
          if (last_id < rg.to)
            rg.to = last_id;
          if (rg.is_empty())
            continue;
          for (c=rg.from; c <= rg.to; c += rg.step)
            { // Add a new codestream window
              if ((stream_entry = add_stream(c)) < 0)
                continue; // Cannot add any more codestreams
              current_window.codestreams.add(c);
              win =
                serve->get_codestream_window(c,this,current_window.resolution,
                                             current_window.region,
                                             current_window.round_direction,
                                             current_window.max_layers,
                                             current_window.components);
              insert_new_stream_window(win,stream_entry);
            }
        }
    }
  
  // Now process any requested codestream contexts
  range_idx = 0; // Here it stands for the current context range
  for (csn=0; !(csrg=context_set.get_range(csn)).is_empty(); csn++)
    { 
      // Copy all contexts in the range, for which an expansion exists
      rg = csrg;
      rg.to = rg.from - 1;
      for (c=csrg.from; c <= csrg.to; c+=csrg.step)
        { 
          if (!serve->target->access_context(rg.context_type,c,
                                             rg.remapping_ids))
            break;
          rg.to = c;
        }
      if (rg.is_empty())
        continue; // Try the next context range
    
      // Add this context range to the current window, applying its expansion
      current_window.contexts.add(rg,false); // Do not allow merging
      kdu_sampled_range *rg_ref =
        current_window.contexts.access_range(range_idx);
      kdu_range_set *expansion = 
        current_window.create_context_expansion(range_idx);
      if (expansion != rg_ref->expansion)
        assert(0);
      kdu_sampled_range aux_range;
      aux_range.context_type = KDU_JPIP_CONTEXT_TRANSLATED;
      aux_range.remapping_ids[0] = range_idx;
      aux_range.remapping_ids[1] = 0;
      for (c=rg.from; c <= rg.to; c+=rg.step, aux_range.remapping_ids[1]++)
        { 
          kdu_window_context ctxt =
            serve->target->access_context(rg.context_type,c,rg.remapping_ids);
          assert(ctxt.exists());
          int num_members = ctxt.get_num_members(rg.remapping_ids);
          int stream_idx, member_idx;
          for (member_idx=0; member_idx < num_members; member_idx++)
            { 
              stream_idx = ctxt.get_codestream(rg.remapping_ids,member_idx);
              expansion->add(stream_idx);
              
              // Now see if we should add a codestream window
              kdu_coords res = current_window.resolution;
              kdu_dims reg = current_window.region;
              if (!ctxt.perform_remapping(rg.remapping_ids,member_idx,res,reg))
                continue; // Cannot translate the view window
              if ((stream_entry = add_stream(stream_idx)) < 0)
                continue; // Cannot add any more codestreams
              aux_range.from = aux_range.to = stream_idx;
              current_window.codestreams.add(aux_range);
              if ((first_stream_windows[stream_entry] != NULL) && !reg)
                continue;  // This window has zero size and codestream already
                          // included; no need to create the stream window
              int num_comps = 0;
              const int *comps =
                ctxt.get_components(rg.remapping_ids,member_idx,num_comps);
              win=serve->get_codestream_window(stream_idx,this,res,reg,
                                               current_window.round_direction,
                                               current_window.max_layers,
                                               current_window.components,
                                               num_comps,comps);
              insert_new_stream_window(win,stream_entry);
              win->context_type = rg.context_type;
              win->context_idx = c;
              win->window_ctxt = ctxt;
              win->member_idx = member_idx;
              win->remapping_ids[0] = rg.remapping_ids[0];
              win->remapping_ids[1] = rg.remapping_ids[1];
            }
        }
      range_idx++;
    }
  
  // Now see if any of the codestream windows are contained within others.
  // If this happens, it is not a disaster, but it does unnecessarily consume
  // time and resources making multiple active references to the same data-bins
  // for the purpose of generating increments.
  int s;
  for (s=0; s < this->num_streams; s++)
    { 
      kd_codestream_window *next, *scan=first_stream_windows[s];
      kd_codestream_window *prev=(s==0)?NULL:last_stream_windows[s-1];
      kd_stream *stream = scan->stream;
      for (; (scan != NULL) && (scan->stream == stream); prev=scan, scan=next)
        { 
          next = scan->next;
          for (win=first_stream_windows[s];
               (win != NULL) && (win->stream == stream); win=win->next)
            if ((win != scan) && win->contains(scan))
              {
                assert(last_stream_windows[s] != first_stream_windows[s]);
                if (prev == NULL)
                  { 
                    assert(scan == new_stream_windows);
                    new_stream_windows = next;
                  }
                else
                  prev->next = next;
                if (scan == first_stream_windows[s])
                  first_stream_windows[s] = next;
                else if (scan == last_stream_windows[s])
                  last_stream_windows[s] = prev;
                scan->next = NULL; // So we release just this one
                serve->release_codestream_windows(scan);
                scan = prev; // So we don't change `prev'
                break;
              }
        }
    }
  
  // Now see if we should adjust the size of the view window to accommodate
  // complexity constraints in one hit
  int prefs = window_prefs.preferred | window_prefs.required;
  bool want_full_window = ((prefs & KDU_WINDOW_PREF_FULL) != 0);
  while ((!want_full_window) &&
         ((win=new_stream_windows) != NULL) && (win->next == NULL) &&
         (win->get_window_samples(0) > KD_MAX_WINDOW_SAMPLES) &&
         !current_window.metadata_only)
    { // Just serving a single stream window, no nasty implications if we
      // adjust the window region here.
      double scale = sqrt((0.8*KD_MAX_WINDOW_SAMPLES) /
                          win->get_window_samples(0));
      double centre_x =
        current_window.region.pos.x + 0.5*current_window.region.size.x;
      double centre_y =
        current_window.region.pos.y + 0.5*current_window.region.size.y;
      current_window.region.pos.x = (int)
        (0.5 + centre_x - 0.5*scale*current_window.region.size.x);
      current_window.region.size.x = (int)
        (0.5 + scale*current_window.region.size.x);
      current_window.region.pos.y = (int)
        (0.5 + centre_y - 0.5*scale*current_window.region.size.y);
      current_window.region.size.y = (int)
        (0.5 + scale*current_window.region.size.y);
      kdu_coords res = current_window.resolution;
      kdu_dims reg = current_window.region;
      int num_comps=0;
      const int *comps=NULL;
      if (win->context_type != 0)
        { 
          win->window_ctxt.perform_remapping(win->remapping_ids,
                                             win->member_idx,res,reg);
          comps = win->window_ctxt.get_components(win->remapping_ids,
                                                  win->member_idx,num_comps);
        }
      win->initialize(win->stream,this,res,reg,current_window.round_direction,
                      current_window.max_layers,current_window.components,
                      num_comps,comps);
    }
  
  // Finish up
  window_changed = window_imagery_changed = false;
  update_window_streams = true;
  codestream_seq_pref = ((window_prefs.preferred | window_prefs.required) &
                         (KDU_CODESEQ_PREF_FWD | KDU_CODESEQ_PREF_BWD)) != 0;
}

/*****************************************************************************/
/*                  kd_window_context::sequence_active_bins                  */
/*****************************************************************************/

void kd_window_context::sequence_active_bins()
{ 
  // Step 1: initialize the sweep machinery from scratch, if necessary
  assert(old_stream_windows == NULL);
  assert(!active_streams_locked);
  if (update_window_streams)
    { // Initialize the sweep machinery.
      update_window_streams = update_window_metadata = false;
      old_stream_windows = stream_windows;
      stream_windows = new_stream_windows;
      new_stream_windows = NULL;
      sweep_start = sweep_next = meta_sweep_next = stream_windows;
      kd_codestream_window *old_active_window = first_active_stream_window;
      first_active_stream_window = last_active_stream_window = NULL;
      
      // Find a good value for `sweep_extra_discard_levels'
      kd_codestream_window *wp;
      sweep_extra_discard_levels = 0;
      int max_extra_discard_levels = INT_MAX; // Until we know better
      if (codestream_seq_pref)
        max_extra_discard_levels = 0; // Don't want interleaved codestreams
      while ((!current_window.metadata_only) && (stream_windows != NULL) &&
             (sweep_extra_discard_levels < max_extra_discard_levels))
        { 
          int num_stream_windows = 0;
          kdu_long win_samples = 0;
          for (wp=stream_windows; wp != NULL; wp=wp->next)
            {
              int d = wp->get_max_extra_discard_levels();
              if (d < max_extra_discard_levels)
                {
                  max_extra_discard_levels = d;
                  assert(sweep_extra_discard_levels <= d);
                  if (sweep_extra_discard_levels == d)
                    break; // No need to go any further
                }
              num_stream_windows++;
              win_samples+=wp->get_window_samples(sweep_extra_discard_levels);
            }
          if (win_samples <= (((kdu_long) num_stream_windows)*160*160))
            break; // Average window resolution getting too small; enough
          if ((win_samples <= KD_MAX_WINDOW_SAMPLES) &&
              (num_streams <= KD_MAX_ACTIVE_CODESTREAMS))
            break; // Everything can fit into one collection of active bins
          if (sweep_extra_discard_levels < max_extra_discard_levels)
            sweep_extra_discard_levels++;
        }

      if (codestream_seq_pref)
        active_bin_rate_threshold = -1.0; // Don't want interleaved codestreams
      else if (sweep_extra_discard_levels == 0) 
        active_bin_rate_threshold = 2.0;
      else
        active_bin_rate_threshold = 1.0;
      
      if ((old_active_window != NULL) && !codestream_seq_pref)
        { 
          for (wp=stream_windows; wp != NULL; wp=wp->next)
            if (wp->contains(old_active_window) && !wp->fully_dispatched)
              break;
          if (wp != NULL)
            { 
              sweep_start = wp;
              wp->sync_sequencer(old_active_window->sequencer_start,
                                 sweep_extra_discard_levels);
            }
        }
    }
  else if (update_window_metadata)
    { 
      meta_sweep_next = stream_windows;
    }
  
  // Step 2: put the existing set of codestream data-bin references to one side
  // for the moment so that we don't immediately release resources we may
  // need to re-use once more data-bins are sequenced.
  assert((old_active_bins == NULL) && (new_active_bins == NULL));
  if (last_active_metabin == NULL)
    { 
      old_active_bins = active_bins;
      active_bins = NULL;
    }
  else
    { // Only put the codestream data-bins to one side for now 
      old_active_bins = last_active_metabin->next;
      last_active_metabin->next = NULL;
    }
  if (update_window_metadata)
    { // Make these `old_active_bins' the `new_active_bins'
      new_active_bins = old_active_bins;
      old_active_bins = NULL;
    }
  
  // Step 3: Except in the case where we are only resequencing the metadata
  // bins, we now return all codestream windows and all streams to the inactive
  // state and see if we can set the `fully_dispatched' flag for any
  // codestream windows which have been fully sequenced.   Once we have done
  // all this, we can release any `old_stream_windows'.
  if (!update_window_metadata)
    { 
      while (first_active_stream_window != NULL)
        { 
          if (first_active_stream_window->is_active &&
              !(first_active_stream_window->sequencing_active ||
                first_active_stream_window->content_incomplete))
            first_active_stream_window->fully_dispatched = true;
          first_active_stream_window->is_active = false;
          if (first_active_stream_window == last_active_stream_window)
            { first_active_stream_window = last_active_stream_window = NULL; }
          else
            {
              first_active_stream_window = first_active_stream_window->next;
              if (first_active_stream_window == NULL)
                first_active_stream_window = stream_windows;
            }
        }
      remove_active_stream_ref(NULL); // Removes all active codestreams
      if (old_stream_windows != NULL)
        { 
          serve->release_codestream_windows(old_stream_windows);
          old_stream_windows = NULL;
        }
    }
  assert((old_stream_windows == NULL) && (new_stream_windows == NULL));

  // Step 3: See if we need to generate metadata-bin references again.  Note
  // that at this point, `active_bins' points to a list of existing
  // metadata-bin references only.
  if ((meta_sweep_next != NULL) || update_window_metadata)
    { // Generate all the metadata-bin references in one hit for now
      serve->release_active_binrefs(active_bins);
      active_bins = last_active_metabin = NULL;
      serve->create_active_meta_binrefs(active_bins,last_active_metabin,
                                        current_window,meta_sweep_next,
                                        NULL,current_model_instructions);
      meta_sweep_next = NULL;
      active_metabins_completed = false;
    }
  else if (active_metabins_completed)
    { 
      serve->release_active_binrefs(active_bins);
      active_bins = last_active_metabin = NULL;
    }
  restart_increment_generation(); // Reset all the other state variables
  
  // Step 4: Now go through the codestream windows, starting from `sweep_next'
  // and continuing until we have sequenced all the data-bins that we can.
  // During this process, we accumulate information about the new
  // `first_active_stream_window' and `last_active_stream_window'.
  if (current_window.metadata_only)
    sweep_next = NULL;
  else if (!update_window_metadata)
    { // Temporarily accumulate codestream data-bins on `new_active_bins' list
      assert(new_active_bins == NULL);
      kd_active_binref *active_res_tails[33];
      memset(active_res_tails,0,sizeof(kd_active_binref *)*33);
      kdu_long available_samples = KD_MAX_WINDOW_SAMPLES;
      bool final_sweep_started=false;
      while (sweep_next != first_active_stream_window)
        {
          assert(!sweep_next->is_active);
          if ((sweep_next == sweep_start) &&
              (sweep_extra_discard_levels == 0) &&
              !sweep_next->sequencing_active)
            final_sweep_started = true;
          if (sweep_next->fully_dispatched)
            assert(!sweep_next->sequencing_active);
          else
            {
              if (!add_active_stream_ref(sweep_next->stream))
                break; // Reached the active stream limit already
              kdu_long win_samples =
                sweep_next->get_window_samples(sweep_extra_discard_levels);
              bool partial_sequencing = false;
              if ((available_samples < win_samples) ||
                  sweep_next->sequencing_active)
                {
                  if (first_active_stream_window != NULL)
                    break; // Can't fit any more complete stream windows
                  partial_sequencing = true;
                }
              kdu_long seq_limit=(partial_sequencing)?KD_MAX_WINDOW_SAMPLES:0;
              if (!sweep_next->sequence_active_bins(new_active_bins,
                               active_res_tails,sweep_extra_discard_levels,
                               seq_limit,current_model_instructions))
                { // Failed to sequence any content from this stream
                  remove_active_stream_ref(sweep_next->stream);
                  assert(!(sweep_next->sequencing_active ||
                           sweep_next->is_active));
                }
              else
                { 
                  available_samples -= win_samples;
                  assert(sweep_next->is_active);
                  if (first_active_stream_window == NULL)
                    first_active_stream_window = sweep_next;
                  last_active_stream_window = sweep_next;
                  if (sweep_next->sequencing_active)
                    break;// Don't aggregate partially sequenced window streams
                }
            }
        
          // Advance the `sweep_stream' now
          sweep_next = sweep_next->next;
          if (sweep_next == NULL)
            sweep_next = stream_windows;
          if (sweep_next == sweep_start)
            {
              if (final_sweep_started)
                { // We have been right around with 0 extra discard levels
                  sweep_next = NULL; // Indicates that we are all done
                  break;
                }
              if (sweep_extra_discard_levels > 0)
                { 
                  sweep_extra_discard_levels--;
                  if ((sweep_extra_discard_levels == 0) &&
                      (active_bin_rate_threshold > 0.0))
                    active_bin_rate_threshold = 2.0;
                }
              else if (active_bin_rate_threshold > 0.0)
                active_bin_rate_threshold += 2.0; // Add 2 bits/sample
            }
        }
    }
  update_window_metadata = false; // No longer need this flag
  
  // Step 5: remove old codestream data-bin references and append new ones
  // to the metadata section of the `active_bins' list; this process may
  // release some or all of the associated `kd_active_precinct' objects if
  // they are no longer referenced and have far reaching resource cleanup
  // implications.
  serve->release_active_binrefs(old_active_bins);
  old_active_bins = NULL;  
  if (last_active_metabin == NULL)
    { 
      assert(active_bins == NULL);
      active_bins = new_active_bins;
    }
  else
    { 
      assert(last_active_metabin->next == NULL);
      last_active_metabin->next = new_active_bins;
    }
  new_active_bins = NULL;
  
  // Step 6: See if we have dispatched all the cache model instructions already
  if ((current_model_instructions != NULL) && model_instructions.is_empty())
    current_model_instructions = NULL; // Saves processing effort later
  
  // Step 7: See if all content has been fully generated.  If so, this is
  // a reasonable point to process any outstanding cache model instructions.
  if ((active_bins == NULL) && (sweep_next == NULL) &&
      (meta_sweep_next == NULL))
    { 
      if (serve->is_stateless)
        { 
          if ((serve->num_completed_codestreams == serve->total_codestreams) &&
              serve->check_metadata_complete())
            serve->stateless_image_done = true; // Unlikely, but possible
        }
      else
        process_outstanding_model_instructions(NULL);
      current_model_instructions = NULL;
      
      /* We used to release resources here, but this is no longer a good idea.
         In stateless mode, we might as well wait until the next request comes
         along, since it might use many of the same resources.  For a
         stateful session, we cannot necessarily release the window context's
         resources here, because some UDP transported data chunks might be
         abandoned from an earlier window of interest, after which the
         current window of interest should at least be able to regenerate
         the relevant content from such abandoned chunks.  From Kakadu v7.0,
         there is a new function, `kdu_serve::window_finished', which may
         be used to explicitly clean up all the window's resources. */
      /*
      // serve->release_codestream_windows(stream_windows);
      // stream_windows = NULL;
      // num_streams = 0;
      */
    }
  
  // Step 8: Now that the set of streams that are of interest to us is stable,
  // we should consider trimming excessive memory-consuming resources.
  serve->trim_resources();
}

/*****************************************************************************/
/*                  kd_window_context::simulate_increments                   */
/*****************************************************************************/

int kd_window_context::simulate_increments(int suggested_total_bytes,
                                           int max_total_bytes, bool align,
                                           bool extended_headers,
                                           int min_msg_increment,
                                           int suggested_chunk_body_bytes)
{
  if (suggested_total_bytes > max_total_bytes)
    suggested_total_bytes = max_total_bytes;
  
  // Set up state variables to keep track of open interchange interfaces
  kd_tile_comp *tc=NULL;
  kdu_tile_comp tci; // Open interface for `tc' if non-NULL
  kdu_tile_comp source_tci; // Open if `tci' is open and `tp->source' is open
  kd_resolution *rp=NULL;
  kdu_resolution rpi; // Open interface for `rp' if non-NULL
  kdu_resolution source_rpi; // Open if `rpi' is open and `tp->source' is open
  
  // Now set up looping variables and tallies and start the simulation
  assert(!active_streams_locked);
  install_active_layer_log_slopes();
  int simulated_bytes = 0;
  bool byte_limit_reached = false;
  kds_id_encoder *id_encoder = serve->id_encoder;
  try {
    while ((active_threshold != INT_MIN) && !byte_limit_reached)
      { 
        kd_active_binref *scan_next, *scan;
        for (scan=active_bin_ptr;
             (scan != NULL) && !byte_limit_reached;
             scan=scan_next)
          { 
            scan_next = scan->next;
            kd_stream *stream = scan->stream;
            kd_tile *tp = scan->tile;
            kd_meta *meta = scan->meta;
            if (scan->precinct == NULL)
              { 
                int max_span=0, cur_span=0, dispatched_span=0, max_id_length=0;
                kdu_byte cur_span_complete=0, max_span_complete=0;
                kd_model_hole *holes = NULL;
                if (meta != NULL)
                  { 
                    max_span = scan->num_active_bytes;
                    cur_span = meta->sim_span;
                    dispatched_span = meta->dispatched_span;
                    holes = meta->dispatched_holes;
                    if (meta->next_in_bin == NULL)
                      { 
                        max_span_complete = (max_span == meta->num_bytes)?1:0;
                        cur_span_complete = meta->sim_span_complete;
                      }
                    if ((max_span <= cur_span) &&
                        (max_span_complete <= cur_span_complete))
                      continue; // No need to send anything
                    
                    // Adjust next active threshold as appropriate
                    int thresh=scan->log_relevance-256;
                    if (thresh > next_active_threshold)
                      next_active_threshold = thresh;
                    
                    // Need `active_threshold' <= `scan->log_relevance'
                    if (active_threshold > scan->log_relevance)
                      { // Not yet a candidate
                        scanned_incomplete_metabins++;
                        continue;
                      }
                    
                    id_encoder->decouple(); // Force conservative estimation
                    max_id_length = id_encoder->encode_id(NULL,
                                                          KDU_META_DATABIN,
                                                          0,meta->bin_id,true);
                  }
                else if (tp != NULL)
                  { 
                    max_span = tp->header_bytes;
                    cur_span = tp->sim_header_span;
                    dispatched_span = tp->dispatched_header_span;
                    holes = tp->dispatched_header_holes;
                    cur_span_complete = tp->sim_header_span_complete;
                    max_span_complete = 1; // Header data-bins can be completed
                    if ((max_span <= cur_span) &&
                        (max_span_complete <= cur_span_complete))
                      continue; // No need to send anything
                    id_encoder->decouple();
                    max_id_length =
                      id_encoder->encode_id(NULL,KDU_TILE_HEADER_DATABIN,
                                            stream->stream_id,tp->tnum,true);
                  }
                else if (stream != NULL)
                  { 
                    max_span = stream->main_header_bytes;
                    cur_span = stream->sim_header_span;
                    dispatched_span = stream->dispatched_header_span;
                    holes = stream->dispatched_header_holes;
                    cur_span_complete = stream->sim_header_span_complete;
                    max_span_complete = 1; // Header data-bins can be completed
                    if ((max_span <= cur_span) &&
                        (max_span_complete <= cur_span_complete))
                      continue; // No need to send anything                  
                    id_encoder->decouple();
                    max_id_length =
                      id_encoder->encode_id(NULL,KDU_MAIN_HEADER_DATABIN,
                                            stream->stream_id,0,true);
                  }
                else
                  { 
                    assert(0);
                    continue;
                  }
                
                int increment = 0;
                for (; holes != NULL; holes=holes->next)
                  { // Simulate increments from the `holes' list, if required
                    if (cur_span < (int) holes->start)
                      cur_span = (int) holes->start;
                    increment = ((int) holes->lim) - cur_span;
                    if (increment <= 0)
                      continue;
                    int hdr_threshold=-1;
                    int hdr_len =
                      estimate_msg_hdr_cost(hdr_threshold,cur_span,
                                            (int)holes->lim,0,max_id_length,
                                            suggested_chunk_body_bytes);
                    simulated_bytes += increment + hdr_len;
                    if (simulated_bytes >= suggested_total_bytes)
                      { // Need to decide how much to actually send
                        byte_limit_reached = true;
                        if (simulated_bytes > max_total_bytes)
                          { 
                            increment -= (simulated_bytes-max_total_bytes);
                            simulated_bytes=max_total_bytes; // So caller knows
                            // that the simulation was terminated by hard limit
                            if (increment < min_msg_increment)
                              increment = 0; // Not worth signalling anything
                          }
                      }
                    cur_span += increment;
                    if (byte_limit_reached)
                      break; // Make sure `holes' stays non-NULL in this case
                  }
                if (holes == NULL)
                  { // See about adding an increment at the end of the span
                    if (cur_span < dispatched_span)
                      cur_span = dispatched_span;
                    increment = max_span - cur_span;
                    kdu_byte complete_inc=max_span_complete-cur_span_complete;
                    assert(complete_inc == (complete_inc & 1));
                    if ((increment > 0) || complete_inc)
                      { // Simulate an increment at the end of the span
                        int hdr_threshold=-1;
                        int hdr_len =
                          estimate_msg_hdr_cost(hdr_threshold,
                                                cur_span,max_span,0,
                                                max_id_length,
                                                suggested_chunk_body_bytes);
                        simulated_bytes += increment + hdr_len;
                        if (simulated_bytes >= suggested_total_bytes)
                          { // Need to decide how much to actually send
                            byte_limit_reached=true;
                            if (simulated_bytes > max_total_bytes)
                              { 
                                increment -= (simulated_bytes-max_total_bytes);
                                complete_inc = 0; // Do not alter completeness
                                simulated_bytes=max_total_bytes; // Simulation
                                                 // terminated by a hard limit
                                if (increment < min_msg_increment)
                                  increment = 0;
                              }
                          }
                        cur_span += increment;
                        cur_span_complete += complete_inc;
                      }
                  }
                if (meta != NULL)
                  { 
                    meta->sim_span = cur_span;
                    meta->sim_span_complete = cur_span_complete;
                    if ((cur_span < max_span) ||
                        (cur_span_complete < max_span_complete))
                      scanned_incomplete_metabins++;
                  }
                else if (tp != NULL)
                  { 
                    tp->sim_header_span = cur_span;
                    tp->sim_header_span_complete = cur_span_complete;
                    if ((cur_span < max_span) || !cur_span_complete)
                      scanned_incomplete_codestream_bins++;
                  }
                else
                  { 
                    stream->sim_header_span = cur_span;
                    stream->sim_header_span_complete = cur_span_complete;
                    if ((cur_span < max_span) || !cur_span_complete)
                      scanned_incomplete_codestream_bins++;
                  }
                continue; // Finished handling non-precinct data-bin
              }
          
            //----------------
          
            // If we get here, we are processing a precinct data-bin.  First
            // see if we need to fill out the various members of the active
            // precinct structure based on incomplete data found in the
            // `num_packets' and `dispatched_span' members.
            kd_active_precinct *prec = scan->precinct;
            assert(prec != NULL);
            if (prec->span_complete && (prec->dispatched_holes == NULL))
              { // Precinct is complete already
                if (prec->interchange.exists() && (prec->dispatched_span >= 0))
                  { 
                    scanned_precinct_bytes += prec->dispatched_span;
                    scanned_precinct_samples += prec->num_samples;
                  }
                continue;
              }
            if ((prec->num_packet_bytes < 0) || !prec->interchange.exists())
              { // At least some of the members of `prec' cannot be trusted
                // and need to be derived by examining layer boundaries.
                load_active_precinct(prec,tc,tci,source_tci,rp,rpi,source_rpi);
              }
            
            // See if we can immediately pass over this precinct
            if ((scan->num_active_layers <= (int) prec->sim_packets) &&
                (prec->sim_span_complete ||
                 (scan->num_active_layers < (int) prec->max_packets)))
              { // We have already simulated enough packets to satisfy the
                // needs of this binref.
                if (prec->sim_span < prec->sim_packet_bytes)
                  { // It is possible that we previously advanced `sim_packets'
                    // without deciding to dispatch any bytes, because the
                    // increment would have been too small to be worthwhile and
                    // the `sim_packets' value had not yet reached a binref's
                    // target `num_active_layers' threshold, but the
                    // `num_active_layers' threshold has now changed (e.g.,
                    // because of a modified window of interest or because
                    // there are multiple windows of interest being served
                    // concurrently in different window contexts which have
                    // this precinct in common).  The simplest solution is
                    // just to dispatch sufficient increments to let `sim_span'
                    // catch up with the whole packet boundary associated with
                    // `sim_packets'.
                    prec->sim_span = prec->sim_packet_bytes;
                  }
                scanned_precinct_bytes += prec->sim_span;
                scanned_precinct_samples += prec->num_samples;
                continue;
              }
          
            if (prec->max_id_length == 0)
              { 
                id_encoder->decouple(); // Force conservative estimation
                kdu_long unique_id = prec->interchange.get_unique_id();
                prec->max_id_length =
                  id_encoder->encode_id(NULL,KDU_PRECINCT_DATABIN,
                                        stream->stream_id,unique_id,true,
                                        extended_headers);
              }
          
            int t_layer = prec->sim_packets;
            if (t_layer == prec->max_packets)
              t_layer--; // Deal with special case in which we just need to
                         // send the data-bin completion message but we want
                         // to sequence it with the final completed layer
            int thresh = stream->active_layer_log_slopes[t_layer+1]
                       + scan->log_relevance + 1;
            if (thresh > next_active_threshold)
              next_active_threshold = thresh;

            // See if the precinct is a candidate for including more data yet
            if ((active_threshold == INT_MAX) ||
                (scan_first_layer && (t_layer != 0)) ||
                ((!scan_first_layer) &&
                 (active_threshold >
                  (stream->active_layer_log_slopes[t_layer] +
                   scan->log_relevance))))
              { // All done for now
                scanned_precinct_bytes += prec->sim_span;
                scanned_precinct_samples += prec->num_samples;
                scanned_incomplete_codestream_bins++;
                continue; // Move on to another active databin reference
              }
            
            // Find out the range of bytes for which the data increment is
            // to be generated.  The range of bytes starts at `start_cur_span'
            // and finishes at `lim_sim_span' (set to 0 if there is no limit).
            // The current value of `prec->sim_span' should wind up somewhere
            // in this range -- may be part way through the range if we already
            // considered some but not all quality layers which are interior
            // to the range.
            kd_model_hole *holes=prec->dispatched_holes;
            int lim_sim_span = 0; // No limit so far
            int start_cur_span = prec->dispatched_span; // Default if no holes
            for (; (holes != NULL) && (lim_sim_span == 0); holes=holes->next)
              { // Adjust `prec->sim_span' and `lim_sim_span' to account for
                // the presence of any holes, noting that generated data
                // increments will fill in any holes up to whatever the
                // `prec->sim_span' value winds up at.  This loop leaves
                // `holes' pointing immediately beyond the hole for which data
                // is currently being generated, if any.
                lim_sim_span = (int) holes->lim;
                if (prec->sim_span < lim_sim_span)
                  start_cur_span = (int) holes->start;
                else
                  lim_sim_span = 0;
              }

            // Run the simulation for the precinct
            int cum_packets, cum_bytes;
            bool packet_truncated = false;
            if (prec->sim_packets == prec->max_packets)
              { // Special case in which just need to send completion message
                assert((lim_sim_span == 0) &&
                       (start_cur_span == prec->dispatched_span) &&
                       (prec->sim_packets == prec->num_packets) &&
                       (prec->sim_span == prec->dispatched_span) &&
                       (prec->sim_packet_bytes == prec->sim_span));
                cum_packets = prec->max_packets;
                cum_bytes = prec->sim_span;
              }
            else
              { // Simulate to see if this precinct can contribute an increment
                bool sig=false;
                cum_bytes=0; cum_packets=prec->sim_packets+1;
                prec->interchange.size_packets(cum_packets,cum_bytes,sig);
                assert(cum_packets == (int)(prec->sim_packets+1));
                if ((cum_packets < scan->num_active_layers) &&
                    ((lim_sim_span == 0) || (cum_bytes < lim_sim_span)) &&
                    (((cum_bytes-start_cur_span) < min_msg_increment) || !sig))
                  { // Need more quality layers before generating this
                    // increment; it does not cross over a hole boundary,
                    // does not take us to the quality layer limit, if any,
                    // and does not contain at least `min_msg_increment'
                    // bytes (or else the precinct is still completely
                    // insignificant).
                    prec->sim_packets = (kdu_uint16) cum_packets;
                    prec->sim_packet_bytes = cum_bytes;
                    scan_next=scan; // Continue with same precinct in nxt layer
                    continue;
                  }
                if ((lim_sim_span != 0) && (cum_bytes > lim_sim_span))
                  { // Normally, we will truncate the increment to
                    // `lim_sim_span' but if any of the truncated bytes fall
                    // within a subsequent hole or beyond `dispatched_span',
                    // we should keep them so that we don't have to generate
                    // multiple increments from the same packet later on; this
                    // is a very unlikely situation to arise anyway.
                    for (; (holes != NULL) && (cum_bytes > (int) holes->start);
                         holes=holes->next)
                      { 
                        assert(holes->lim > holes->start);
                        lim_sim_span = (int) holes->lim;
                      }
                    if (cum_bytes >= prec->dispatched_span)
                      lim_sim_span = 0;
                    else if (cum_bytes > lim_sim_span)
                      { cum_bytes = lim_sim_span; packet_truncated = true; }
                  }
              }
          
            int increment = cum_bytes - prec->sim_span;
            int new_sim_span = cum_bytes;
            assert(increment >= 0);
            int hdr_threshold = -1;
            if (prec->sim_span > start_cur_span)
              hdr_threshold=prec->hdr_threshold; // No need for new msg header
            if ((hdr_threshold == 0) || (cum_bytes > hdr_threshold))
              increment +=
                estimate_msg_hdr_cost(hdr_threshold,start_cur_span,cum_bytes,
                                      (extended_headers)?prec->max_packets:0,
                                      prec->max_id_length,
                                      suggested_chunk_body_bytes);
            simulated_bytes += increment;
            if (simulated_bytes >= suggested_total_bytes)
              { // Need to decide how much of packet to send and finish here
                byte_limit_reached = true;
                if (simulated_bytes > max_total_bytes)
                  { 
                    new_sim_span -= (simulated_bytes-max_total_bytes);
                    simulated_bytes = max_total_bytes; // So caller knows that
                         // the simulation was terminated by a hard byte limit
                    if ((new_sim_span <= prec->sim_span) || align ||
                        ((new_sim_span-start_cur_span) < min_msg_increment))
                      break; // Will start from same data-bin again next time
                    packet_truncated = true;
                  }
                else if ((increment < simulated_bytes) &&
                         (increment >= (suggested_total_bytes >> 2)))
                  { // Abandon the increment for now
                    simulated_bytes -= increment;
                    break; // We will start from same data-bin again next time
                  }
              }
            if ((new_sim_span == cum_bytes) && !packet_truncated)
              { 
                prec->sim_packets = (kdu_uint16) cum_packets;
                prec->sim_packet_bytes = cum_bytes;
                if (prec->sim_packets == prec->max_packets)
                  prec->sim_span_complete = 1;
              }
              
            prec->sim_span = new_sim_span;
            prec->hdr_threshold = hdr_threshold;
            if (prec->sim_span == lim_sim_span)
              { // We have just filled in a hole.  We need to advance
                // `sim_span' to the point where the next hole begins, or to
                // `dispatched_span' if there are no more holes, and then we
                // need to go back and process the same precinct again.
                // However, before we do go back, since `sim_span' is being
                // adjusted, we also need to adjust the `sim_packets' and
                // `sim_packet_bytes' members to the correct values.
                if (holes == NULL)
                  { 
                    if (prec->sim_span < prec->dispatched_span)
                      prec->sim_span = prec->dispatched_span;
                  }
                else
                  prec->sim_span = (int) holes->start;
                assert(prec->sim_span >= lim_sim_span);
                do {
                  bool sig=false;
                  cum_bytes=0; cum_packets=prec->sim_packets+1;
                  prec->interchange.size_packets(cum_packets,cum_bytes,sig);
                  assert(cum_packets == (int)(prec->sim_packets+1));
                  if (cum_bytes <= prec->sim_span)
                    { 
                      prec->sim_packets = (kdu_uint16) cum_packets;
                      prec->sim_packet_bytes = cum_bytes;
                    }
                } while (cum_bytes < prec->sim_span);
                if (prec->sim_packets == prec->max_packets)
                  prec->sim_span_complete = 1;
                else
                  { 
                    scan_next=scan; // Continue with same precinct in nxt layer
                    continue;
                  }
              }
            if (scan->num_active_layers > (int) prec->sim_packets)
              scanned_incomplete_codestream_bins++;
            scanned_precinct_bytes += prec->sim_span;
            scanned_precinct_samples += prec->num_samples;
          }
      
        active_bin_ptr = scan; // Remember where we were up to
        if (active_bin_ptr == NULL)
          { // We have been right around once; advance the active threshold and
            // start again.
            active_bin_ptr = active_bins;
            if (active_threshold < INT_MAX) // If done with threshold eval scan
              scan_first_layer = false;
            active_threshold = next_active_threshold;
            next_active_threshold = INT_MIN;

            // Here is where we test the ratio between `scanned_precinct_bytes'
            // and `scanned_precinct_samples' to see if we need to return and
            // let the new active bins be sequenced.
            if ((sweep_next != NULL) && (active_bin_rate_threshold > 0.0) &&
                ((scanned_precinct_samples * active_bin_rate_threshold) <=
                 (8.0 * scanned_precinct_bytes)))
              active_bin_rate_reached = true;
            if (scanned_incomplete_metabins == 0)
              { 
                active_metabins_completed = true;
                if (scanned_incomplete_codestream_bins == 0)
                  active_bins_completed = true;
              }
            scanned_precinct_bytes = scanned_precinct_samples = 0;
            scanned_incomplete_metabins =
              scanned_incomplete_codestream_bins = 0;
            if (active_bins_completed || active_bin_rate_reached)
              break;
          }
      }
    unlock_active_streams(); // Also closes any open source tiles automatically
  } catch (...) {
    unlock_active_streams();
    fprintf(stderr,
            "Received unusual exception during \"simulate_increments\".\n");
    throw;
  }
    
  return simulated_bytes;
}

/*****************************************************************************/
/*                 kd_window_context::generate_increments                    */
/*****************************************************************************/

kds_chunk *
  kd_window_context::generate_increments(int suggested_message_bytes,
                                         int &max_message_bytes, bool align,
                                         bool use_extended_message_headers,
                                         bool decouple_chunks,
                                         int suggested_chunk_body_bytes)
{
  kd_chunk_server *chunk_server = serve->chunk_server;
  kds_id_encoder *id_encoder = serve->id_encoder;
  int min_msg_increment = suggested_chunk_body_bytes;
  if (min_msg_increment > 16)
    min_msg_increment = 16;

  // Start by moving extra data chunks onto the output list.
  kds_chunk *chunk_head, *chunk_tail;
  chunk_head = chunk_tail = NULL;
  while (extra_data_head != NULL)
    {
      int chunk_bytes = extra_data_head->num_bytes
                      - extra_data_head->prefix_bytes;
      if (chunk_tail == NULL)
        chunk_head = chunk_tail = extra_data_head; // Must send >= 1 chunk
      else
        chunk_tail = chunk_tail->next = extra_data_head;
      extra_data_bytes -= chunk_bytes;
      assert(extra_data_bytes >= 0);
      if ((extra_data_head = extra_data_head->next) == NULL)
        extra_data_tail = NULL;
      chunk_tail->next = NULL;
    }
  
  // Prepare for the transfer of all other data
  id_encoder->decouple();
  if (chunk_tail == NULL)
    chunk_head = chunk_tail = chunk_server->get_chunk();  

  // Apply any required changes in the window of interest
  if (window_changed)
    process_window_changes();
  if (update_window_streams || update_window_metadata)
    {
      sequence_active_bins();
      restart_increment_generation();
    }
  assert(!(update_window_streams || update_window_metadata || window_changed));
  
  // Set up message limits
  if (max_message_bytes <= 0)
    return chunk_head;
  if (suggested_message_bytes >= max_message_bytes)
    suggested_message_bytes = max_message_bytes;

  // Loop around, alternately sequencing active data-bins and dispatching them
  // until there are no remaining active data-bins.
  bool truncated_by_byte_limit = false;
  bool truncated_by_hard_byte_limit = false;
  while (((active_bins != NULL) ||
          (sweep_next != NULL) || (meta_sweep_next != NULL)) &&
         (suggested_message_bytes > 0) && !truncated_by_byte_limit)
    { 
      if ((active_bins == NULL) || active_bins_completed ||
          active_bin_rate_reached)
        { // We can advance the collection of active data-bins
          if (!active_bins_completed)
            { // Set active codestream windows's `content_incomplete' flags
              kd_codestream_window *wp = first_active_stream_window;
              while (wp != NULL)
                { 
                  if (wp->is_active)
                    wp->content_incomplete = true;
                  if (wp == last_active_stream_window)
                    wp = NULL; // Loop finishes
                  else if ((wp = wp->next) == NULL)
                    wp = stream_windows;
                }
            }
          assert((sweep_next != NULL) || !active_bin_rate_reached);
             // The logic inside `simulate_increments' ensures that the
             // `active_bin_rate_reached' flag will never be set if
             // there are no new precinct data-bins that could
             // be sequenced (i.e., if `sweep_next' is NULL).
          sequence_active_bins();
          restart_increment_generation();
          continue; // Go back and check `active_bins' again
        }
      
      int simulated_bytes =
        simulate_increments(suggested_message_bytes,max_message_bytes,
                            align,use_extended_message_headers,
                            min_msg_increment,suggested_chunk_body_bytes);
      truncated_by_hard_byte_limit = (simulated_bytes == max_message_bytes);
      truncated_by_byte_limit = (simulated_bytes >= suggested_message_bytes);
      if (simulated_bytes == 0)
        continue; // Do some re-sequencing, possibly leaving the loop
      
      // Now process the simulated binref increments, determined as above.
      kd_active_binref *scan;
      for (scan=active_bins; scan != NULL; scan=scan->next)
        { 
          int bin_bytes = 0;
          if (scan->meta != NULL)
            { 
              kd_meta *meta = scan->meta;
              while ((meta->sim_span > meta->dispatched_span) ||
                     (meta->sim_span_complete > meta->span_complete) ||
                     ((meta->dispatched_holes != NULL) &&
                      (meta->sim_span > (int) meta->dispatched_holes->start)))
                { 
                  assert(meta->check_complete() == NULL);
                  bin_bytes +=
                    generate_meta_increment(meta,chunk_tail,decouple_chunks,
                                            suggested_chunk_body_bytes);
                }
              if ((bin_bytes > 0) && ((meta = meta->check_complete()) != NULL))
                serve->adjust_completed_metabins(meta,false);
            }
          else if (scan->precinct != NULL)
            { 
              kd_active_precinct *prec = scan->precinct;
              while ((prec->sim_span > prec->dispatched_span) ||
                     (prec->sim_span_complete > prec->span_complete) ||
                     ((prec->dispatched_holes != NULL) &&
                      (prec->sim_span > (int) prec->dispatched_holes->start)))
                bin_bytes +=
                  generate_precinct_increment(prec,scan->stream,scan->tile,
                                              chunk_tail,decouple_chunks,
                                              use_extended_message_headers,
                                              suggested_chunk_body_bytes);
              if ((bin_bytes > 0) && prec->span_complete &&
                  (prec->dispatched_holes == NULL))
                { 
                  assert(scan->tile == prec->res->tile_comp->tile);
                  scan->tile->completed_precincts++;
                  scan->stream->adjust_completed_tiles(scan->tile,false);
                }
            }
          else if (scan->tile != NULL)
            { 
              kd_tile *tp = scan->tile;
              kd_stream *str = tp->stream;
              while ((tp->sim_header_span > tp->dispatched_header_span) ||
                     (tp->sim_header_span_complete >
                      tp->header_span_complete) ||
                     ((tp->dispatched_header_holes != NULL) &&
                      (tp->sim_header_span >
                       (int) tp->dispatched_header_holes->start)))
                bin_bytes +=
                  generate_header_increment(str,tp,chunk_tail,decouple_chunks,
                                            suggested_chunk_body_bytes);
              if (bin_bytes > 0)
                str->adjust_completed_tiles(tp,false);
            }
          else if (scan->stream != NULL)
            { 
              kd_stream *str = scan->stream;
              while ((str->sim_header_span > str->dispatched_header_span) ||
                     (str->sim_header_span_complete >
                      str->header_span_complete) ||
                     ((str->dispatched_header_holes != NULL) &&
                      (str->sim_header_span >
                       (int) str->dispatched_header_holes->start)))
                bin_bytes +=
                  generate_header_increment(str,NULL,chunk_tail,
                                            decouple_chunks,
                                            suggested_chunk_body_bytes);
              if (bin_bytes > 0)
                serve->adjust_completed_streams(str,false);
            }
          else
            assert(0);
          suggested_message_bytes -= bin_bytes;
          max_message_bytes -= bin_bytes;
        }
      if (truncated_by_hard_byte_limit)
        max_message_bytes = suggested_message_bytes = 0;
    }
  
  if ((chunk_tail != chunk_head) &&
      (chunk_tail->num_bytes <= chunk_tail->prefix_bytes))
    { // We allocated one too many chunks.  Let's not return the empty
      // chunk to the caller.
      kds_chunk *chunk_prev;
      for (chunk_prev=chunk_head; chunk_prev->next != chunk_tail;
           chunk_prev = chunk_prev->next);
      chunk_prev->next = NULL;
      chunk_server->release_chunk(chunk_tail);
    }
  
  return chunk_head;
}

/*****************************************************************************/
/*                 kd_window_context::add_active_stream_ref                  */
/*****************************************************************************/

bool kd_window_context::add_active_stream_ref(kd_stream *stream)
{
  int s;
  for (s=0; s < num_active_streams; s++)
    if (active_streams[s] == stream)
      break;
  if (s == num_active_streams)
    { // This is a new one
      if ((num_active_streams >= KD_MAX_ACTIVE_CODESTREAMS) ||
          (codestream_seq_pref && (num_active_streams > 0)))
        return false;
      serve->add_active_context_for_stream(stream);
      num_active_streams++;
      active_streams[s] = stream;
      num_active_stream_refs[s] = 0;
    }
  num_active_stream_refs[s]++;
  return true;
}

/*****************************************************************************/
/*                kd_window_context::remove_active_stream_ref                */
/*****************************************************************************/

void kd_window_context::remove_active_stream_ref(kd_stream *stream)
{
  int s, t;
  for (s=0; s < num_active_streams; s++)
    { 
      if (stream == NULL)
        num_active_stream_refs[s] = 0;
      else if (stream == active_streams[s])
        num_active_stream_refs[s]--;
      if (num_active_stream_refs[s] == 0)
        { 
          kd_stream *str = active_streams[s];
          active_streams[s] = NULL;
          if (str != NULL)
            serve->remove_active_context_for_stream(str);
        }
    }
  for (s=0; s < num_active_streams; s++)
    if (active_streams[s] == NULL)
      { 
        num_active_streams--;
        for (t=s; t < num_active_streams; t++)
          { 
            active_streams[t] = active_streams[t+1];
            num_active_stream_refs[t] = num_active_stream_refs[t+1];
          }
        s--; // So loop does not advance `s'
      }
}

/*****************************************************************************/
/*                      kd_window_context::add_stream                        */
/*****************************************************************************/

int kd_window_context::add_stream(int stream_idx)
{
  int s, t;
  bool reverse_order = ((window_prefs.preferred | window_prefs.required) &
                        KDU_CODESEQ_PREF_BWD) != 0;
  if (reverse_order)
    { 
      for (s=0; s < num_streams; s++)
        if (stream_indices[s] <= stream_idx)
          break;
    }
  else
    { 
      for (s=0; s < num_streams; s++)
        if (stream_indices[s] >= stream_idx)
          break;
    }
  if ((s == num_streams) || (stream_indices[s] != stream_idx))
    { // Need to insert a new entry
      if (num_streams >= KD_MAX_WINDOW_CODESTREAMS)
        return -1;
      for (t=num_streams++; t > s; t--)
        { 
          stream_indices[t] = stream_indices[t-1];
          first_stream_windows[t] = first_stream_windows[t-1];
          last_stream_windows[t] = last_stream_windows[t-1];
        }
      stream_indices[s] = stream_idx;
      first_stream_windows[s] = last_stream_windows[s] = NULL;
    }
  return s;
}

/*****************************************************************************/
/*               kd_window_context::insert_new_stream_window                 */
/*****************************************************************************/

void
  kd_window_context::insert_new_stream_window(kd_codestream_window *win, int s)
{
  assert(stream_indices[s] == win->stream->stream_id);
  if (last_stream_windows[s] != NULL)
    { // Insert right after existing last window for this stream
      win->next = last_stream_windows[s]->next;
      last_stream_windows[s]->next = win;
      last_stream_windows[s] = win;
    }
  else
    { 
      if ((s+1) < num_streams)
        { 
          assert(first_stream_windows[s+1] != NULL);
          win->next = first_stream_windows[s+1];
        }
      else
        win->next = NULL;
      if (s > 0)
        last_stream_windows[s-1]->next = win;
      else
        new_stream_windows = win;
      first_stream_windows[s] = last_stream_windows[s] = win;
    }
}

/*****************************************************************************/
/*         kd_window_context::process_outstanding_model_instructions         */
/*****************************************************************************/

void
  kd_window_context::process_outstanding_model_instructions(
                                               kd_stream *single_stream)
{
  if (serve->is_stateless)
    { 
      if (current_model_instructions != NULL)
        current_model_instructions->clear();
      current_model_instructions = NULL;
    }
  if (current_model_instructions == NULL)
    return;
  kdu_window_model *instructions = current_model_instructions;
  int i_cnt, i_buf[2];
  
  kdu_long meta_bin_id=-1;
  if (single_stream == NULL)
    while ((i_cnt =
            instructions->get_meta_instructions(meta_bin_id,i_buf)) > 0)
    {
      kd_meta *meta = serve->find_metabin(meta_bin_id);
      if (meta != NULL)
        serve->process_metabin_model_instructions(meta,i_buf,i_cnt);
      meta_bin_id = -1; // Prepare for next call
    }
  
  int which_stream = 0; // Index to look up in `stream_indices' array
  do {
      int stream_idx = -1; // Stream index not yet known
      kd_stream *stream = NULL; // Bind to an actual stream as late as needed
      if (single_stream != NULL)
        { 
          stream = single_stream;
          stream_idx = single_stream->stream_id;
        }
      else if (which_stream < this->num_streams)
        stream_idx = this->stream_indices[which_stream++];
      else if ((stream_idx = instructions->get_first_atomic_stream()) < 0)
        break;
      
      int tnum, cnum, rnum;
      kdu_long pnum;
      while ((i_cnt =
              instructions->get_precinct_instructions(stream_idx,tnum,cnum,
                                                      rnum,pnum,i_buf)) > 0)
        { 
          bool additive = (i_buf[0] != 0);
          if (stream == NULL)
            stream = serve->get_stream(stream_idx,additive);
          if (stream != NULL)
            stream->process_precinct_model_instructions(tnum,cnum,rnum,pnum,
                                                        i_buf,i_cnt,additive);
        }
      while ((i_cnt =
              instructions->get_header_instructions(stream_idx,&tnum,
                                                    i_buf)) > 0)
        {
          bool additive = (i_buf[0] != 0);
          if (stream == NULL)
            stream = serve->get_stream(stream_idx,additive);
          if (stream != NULL)
            stream->process_header_model_instructions(tnum,i_buf,i_cnt);
        }
    } while (single_stream == NULL);
  
  if (single_stream != NULL)
    { 
      current_model_instructions->clear();
      current_model_instructions = NULL;
    }
}

/*****************************************************************************/
/*             kd_window_context::install_active_layer_log_slopes            */
/*****************************************************************************/

void kd_window_context::install_active_layer_log_slopes()
{
  bool use_dummy_slopes = false;
  int s, num_slopes_needed, max_slopes_needed = 1;
  for (s=0; s < num_active_streams; s++)
    { 
      kd_stream *stream = active_streams[s];
      num_slopes_needed = stream->max_quality_layers+1;
      if (num_slopes_needed > max_slopes_needed)
        max_slopes_needed = num_slopes_needed;
      stream->active_layer_log_slopes = NULL;
      if ((stream->layer_log_slopes == NULL) ||
          (stream->num_layer_slopes < 1))
        use_dummy_slopes = true;
    }
  
  if (use_dummy_slopes && (max_slopes_needed > num_dummy_layer_slopes))
    { // Need to augment dummy slopes array
      if (dummy_layer_log_slopes != NULL)
        delete[] dummy_layer_log_slopes;
      dummy_layer_log_slopes = NULL;
      num_dummy_layer_slopes = max_slopes_needed;
      dummy_layer_log_slopes = new int[num_dummy_layer_slopes];
      int i, val = 49000; // Typical for high rate visual lossless compression
      int gap = (65000 - val) / num_dummy_layer_slopes;
      if (gap < 1)
        gap = 1;
      if (gap > 256)
        gap = 256;
      for (i=num_dummy_layer_slopes-1; i >= 0; i--, val += gap)
        dummy_layer_log_slopes[i] = val;
    }
  
  for (s=0; s < num_active_streams; s++)
    { 
      kd_stream *stream = active_streams[s];
      int num_slopes_needed = stream->max_quality_layers + 1;
      if (use_dummy_slopes)
        { // Use the most significant dummy log slope values
          stream->active_layer_log_slopes =
            dummy_layer_log_slopes+(num_dummy_layer_slopes-num_slopes_needed);
          continue;
        }
      if (num_slopes_needed > stream->num_layer_slopes)
        { // Need to augment stream's `layer_log_slopes' array
          int i, *new_slopes = new int[num_slopes_needed+1];
          for (i=0; i < stream->num_layer_slopes; i++)
            new_slopes[i] = stream->layer_log_slopes[i];
          int val = new_slopes[i-1];
          stream->num_layer_slopes = num_slopes_needed;
          delete[] stream->layer_log_slopes;
          stream->layer_log_slopes = new_slopes;
          for (; i < stream->num_layer_slopes; i++)
            { 
              val -= 256;
              stream->layer_log_slopes[i] = val;
            }
        }
      stream->active_layer_log_slopes = stream->layer_log_slopes;
    }
}

/*****************************************************************************/
/*                  kd_window_context::lock_active_streams                   */
/*****************************************************************************/

void kd_window_context::lock_active_streams()
{ 
  if (active_streams_locked)
    return;
  int s;
  for (s=0; s < num_active_streams; s++)
    { 
      kd_stream *stream = active_streams[s];
      assert((stream->num_active_contexts > 0) &&
             stream->interchange.exists() && stream->structure.exists());
      if (!stream->source)
        serve->attach_stream(stream);
      assert(stream->source.exists() && !stream->is_locked);
    }
  serve->lock_codestreams(num_active_streams,active_streams);
  active_streams_locked = true;
}

/*****************************************************************************/
/*                 kd_window_context::unlock_active_streams                  */
/*****************************************************************************/

void kd_window_context::unlock_active_streams()
{ 
  if (!active_streams_locked)
    return;
  for (int s=0; s < num_active_streams; s++)
    active_streams[s]->close_attached_tiles();
  serve->unlock_codestreams(num_active_streams,active_streams);
  active_streams_locked = false;
}

/*****************************************************************************/
/*                 kd_window_context::load_active_precinct                   */
/*****************************************************************************/

void kd_window_context::load_active_precinct(kd_active_precinct *active,
                                             kd_tile_comp * &tc,
                                             kdu_tile_comp &tci,
                                             kdu_tile_comp &source_tci,
                                             kd_resolution * &rp,
                                             kdu_resolution &rpi,
                                             kdu_resolution &source_rpi)
{ 
  if (!active->interchange)
    { // Need to open the precinct's interchange interface, and load all
      // code-block data into it so we are ready for simulation.
      kd_tile *tp=NULL;
      if (rp != active->res)
        { // Need to open resolution interface
          rp = active->res; source_rpi = kdu_resolution();
          if (tc != rp->tile_comp)
            { 
              tc = rp->tile_comp; source_tci = kdu_tile_comp();
              tp = tc->tile;
              if (!tp->interchange)
                tp->stream->open_tile(tp,false,NULL);
              tci = tp->interchange.access_component(tc->c_idx);
            }
          rpi = tci.access_resolution(rp->r_idx);
        }
      tp = tc->tile;
      active->interchange = rpi.open_precinct(active->p_idx);
      if (!active->interchange.check_loaded())
        { // Need to load content from source codestream
          if (!active_streams_locked)
            lock_active_streams();
          if (!source_rpi)
            { 
              if (!source_tci)
                { 
                  if (!tp->source)
                    tp->stream->open_tile(tp,true,NULL);
                  source_tci = tp->source.access_component(tc->c_idx);
                }
              source_rpi = source_tci.access_resolution(rp->r_idx);
            }
          load_blocks(active->interchange,source_rpi);
        }
    }
  else
    active->interchange.restart();
  
  // Now find the missing active precinct parameters
  kd_model_hole *holes = active->dispatched_holes;
  active->sim_span = active->dispatched_span;
  if ((holes != NULL) && (active->sim_span > (int) holes->start))
    active->sim_span = (int) holes->start;  
  if ((active->num_packet_bytes < 0) && (active->dispatched_span > 0))
    { // Don't know the current number of packets; must deduce it from bytes
      active->num_packet_bytes = 0;
      active->num_packets = 0;
      bool significant;
      int cum_packets, cum_bytes, max_packets=(int)active->max_packets;
      for (cum_packets=1; cum_packets <= max_packets; cum_packets++)
        { 
          cum_bytes = 0;
          active->interchange.size_packets(cum_packets,cum_bytes,significant);
          if (cum_bytes <= active->sim_span)
            { 
              active->num_packets = (kdu_uint16) cum_packets;
              active->num_packet_bytes = cum_bytes;
            }
          if (cum_bytes >= active->sim_span)
            break;
        }
      if (cum_bytes > active->sim_span)
        active->interchange.restart(); // Start simulating from scratch
    }
  else if (active->num_packet_bytes < 0)
    { // Deduce the number of bytes from the number of packets
      assert(active->num_packets > 0);
      if (active->num_packets > active->max_packets)
        active->num_packets = active->max_packets;
      bool significant;
      int cum_packets, cum_bytes, max_packets=(int)active->num_packets;
      for (cum_packets=1; cum_packets <= max_packets; cum_packets++)
        { 
          cum_bytes = 0;
          active->interchange.size_packets(cum_packets,cum_bytes,significant);
          active->sim_span = active->dispatched_span = cum_bytes;
          if ((holes != NULL) && (active->sim_span > (int) holes->start))
            { 
              active->sim_span = (int) holes->start;
              active->interchange.restart();
              break;
            }
          active->num_packets = (kdu_uint16) cum_packets;
          active->num_packet_bytes = cum_bytes;
        }
      if (holes != NULL)
        { 
          active->dispatched_holes = NULL;
          serve->hole_server->release_holes(holes);
        }
    }
  active->sim_span_complete = active->span_complete;
  active->sim_packets = active->num_packets;
  active->sim_packet_bytes = active->num_packet_bytes;
}

/*****************************************************************************/
/*                kd_window_context::estimate_msg_hdr_cost                   */
/*****************************************************************************/

int kd_window_context::estimate_msg_hdr_cost(int &hdr_threshold,
                                             int range_start, int range_lim,
                                             int extended_num_packets,
                                             int max_id_length,
                                             int suggested_chunk_body_bytes)
{
  int result = 0;
  int mandatory_splitting_threshold = suggested_chunk_body_bytes >> 2;
  while (hdr_threshold < range_lim)
    { 
      int extra_header_bytes = max_id_length;
      if (extended_num_packets > 0)
        extra_header_bytes += simulate_var_length(extended_num_packets);
      if (hdr_threshold < 0)
        { // First message header for byte range
          hdr_threshold = range_start + mandatory_splitting_threshold;
          extra_header_bytes += simulate_var_length(range_start);
          extra_header_bytes += simulate_var_length(hdr_threshold-range_start);
        }
      else
        { 
          extra_header_bytes +=
            simulate_var_length(hdr_threshold+suggested_chunk_body_bytes);
          extra_header_bytes += 
            simulate_var_length(suggested_chunk_body_bytes);
          int inc = suggested_chunk_body_bytes - extra_header_bytes;
          hdr_threshold += inc;
          if (inc < mandatory_splitting_threshold)
            { kdu_error e;
              e << "You must use larger chunks or smaller chunk "
              "prefixes for `kdu_serve::generate_increments' "
              "to be able to create a legal set of data chunks.";
            }
          hdr_threshold += suggested_chunk_body_bytes-extra_header_bytes;
        }
      result += extra_header_bytes;
    }
  return result;
}

/*****************************************************************************/
/*               kd_window_context::generate_meta_increment                  */
/*****************************************************************************/

int kd_window_context::generate_meta_increment(kd_meta *meta,
                                               kds_chunk * &chunk,
                                               bool decouple_chunks,
                                               int suggested_chunk_body_bytes)
{
  if (meta->metagroup->is_rubber_length)
    { kdu_error e; e << "The present implementation of `kdu_serve' cannot "
      "dispatch meta data-bins whose length cannot be determined up front "
      "(e.g., rubber length JP2 box in the original file).";
    }
  
  kds_id_encoder *id_encoder = serve->id_encoder;
  int mandatory_splitting_threshold = (suggested_chunk_body_bytes >> 2);
      // For consistency with the simulator
  int suggested_chunk_bytes =
    suggested_chunk_body_bytes + serve->chunk_prefix_bytes;
  int range_start=0, range_length=0;
  bool is_final = false;
  while ((meta->dispatched_holes != NULL) && (range_length <= 0))
    { 
      kd_model_hole *hole = meta->dispatched_holes;
      range_start = (int) hole->start;
      int range_lim = meta->sim_span;
      assert(range_lim > range_start); // Otherwise should not have been called
      if (range_lim >= (int) hole->lim)
        { 
          range_lim = (int) hole->lim;
          meta->dispatched_holes = hole->next;
          hole->next = NULL;
          serve->hole_server->release_holes(hole);
        }
      else
        hole->start = (kdu_uint16) range_lim;
      range_length = range_lim - range_start;
    }
  if (range_length <= 0)
    { 
      range_start = meta->dispatched_span;
      range_length = meta->sim_span - range_start;
      is_final = (meta->sim_span_complete != 0);
      meta->dispatched_span = meta->sim_span;
      meta->span_complete = meta->sim_span_complete;
      assert((meta->next_in_bin == NULL) || !is_final);
      assert((range_length > 0) || ((range_length == 0) && is_final));
    }
  
  assert((chunk != NULL) && (chunk->next == NULL));
  int written_bytes = 0;
  while (1)
    { 
      int hdr_bytes =
        id_encoder->encode_id(NULL,KDU_META_DATABIN,0,meta->bin_id,true);
      hdr_bytes += simulate_var_length(range_start+meta->bin_offset);
      hdr_bytes += simulate_var_length(range_length);
      assert((suggested_chunk_bytes <= chunk->max_bytes) &&
             (suggested_chunk_bytes >= (chunk->prefix_bytes+8)));
      int xfer_bytes = suggested_chunk_bytes - chunk->num_bytes;
      xfer_bytes -= hdr_bytes;
      if (xfer_bytes >= range_length)
        xfer_bytes = range_length; // Can put whole thing in one message
      else if ((xfer_bytes < mandatory_splitting_threshold) ||
               ((xfer_bytes < (suggested_chunk_body_bytes >> 1)) &&
                (range_length <= suggested_chunk_body_bytes)))
        { // Start packing data at the next chunk
          if (chunk->num_bytes == chunk->prefix_bytes)
            { kdu_error e;
              e << "You must use larger chunks or smaller chunk "
              "prefixes for `kdu_serve::generate_increments' "
              "to be able to create a legal set of data chunks.";
            }
          chunk->max_bytes = chunk->num_bytes; // Freeze chunk
          chunk = chunk->next = serve->chunk_server->get_chunk();
          if (decouple_chunks)
            id_encoder->decouple();
          continue;
        }
      
      kd_chunk_binref *bref =
        serve->create_chunk_binref(NULL,NULL,NULL,meta,chunk);
      bref->prev_bytes = range_start;
      bref->num_bytes = xfer_bytes;
      int starting_chunk_bytes = chunk->num_bytes;
      
      chunk->num_bytes +=
        id_encoder->encode_id(chunk->data + chunk->num_bytes,
                              KDU_META_DATABIN,0,meta->bin_id,
                              is_final && (range_length==xfer_bytes));
      output_var_length(chunk,range_start+meta->bin_offset);
      output_var_length(chunk,xfer_bytes);
      assert(chunk->max_bytes >= (chunk->num_bytes+xfer_bytes));
      if (serve->target->read_metagroup(meta->metagroup,
                                        chunk->data + chunk->num_bytes,
                                        range_start,xfer_bytes) != xfer_bytes)
        { kdu_error e; e << "`kdu_serve_target' derived object failed to "
          "return all requested bytes for a group of meta-data boxes.  This "
          "advanced feature (intended to support proxy servers with "
          "partial data) is not supported by the current \"kdu_serve\" "
          "object's implementation.  Alternatively, it may be that the "
          "file has been corrupted or truncated.";
        }
      range_start += xfer_bytes;
      range_length -= xfer_bytes;
      chunk->num_bytes += xfer_bytes;
      written_bytes += (chunk->num_bytes - starting_chunk_bytes);
      if (range_length == 0)
        break;
    }

  return written_bytes;
}

/*****************************************************************************/
/*              kd_window_context::generate_header_increment                 */
/*****************************************************************************/

int
  kd_window_context::generate_header_increment(kd_stream *str, kd_tile *tp,
                                               kds_chunk * &chunk,
                                               bool decouple_chunks,
                                               int suggested_chunk_body_bytes)
{
  assert((chunk != NULL) && (chunk->next == NULL));
  kds_id_encoder *id_encoder = serve->id_encoder;
  int mandatory_splitting_threshold = (suggested_chunk_body_bytes >> 2);
      // For consistency with the simulator
  int suggested_chunk_bytes =
    suggested_chunk_body_bytes + serve->chunk_prefix_bytes;

  int range_start=0, range_length=0, tnum;
  bool is_final=false;
  if (tp != NULL)
    { 
      tnum = tp->tnum;
      while ((tp->dispatched_header_holes != NULL) && (range_length <= 0))
        { 
          kd_model_hole *hole = tp->dispatched_header_holes;
          range_start = (int) hole->start;
          int range_lim = tp->sim_header_span;
          assert(range_lim > range_start); // Otherwise chould not have called
          if (range_lim >= (int) hole->lim)
            { 
              range_lim = (int) hole->lim;
              tp->dispatched_header_holes = hole->next;
              hole->next = NULL;
              serve->hole_server->release_holes(hole);
            }
          else
            hole->start = (kdu_uint16) range_lim;
          range_length = range_lim - range_start;
        }
      if (range_length <= 0)
        { 
          range_start = tp->dispatched_header_span;
          range_length = tp->sim_header_span - range_start;
          is_final = (tp->sim_header_span_complete != 0);
          tp->dispatched_header_span = tp->sim_header_span;
          tp->header_span_complete = tp->sim_header_span_complete;
        }
    }
  else
    { 
      tnum = -1;
      while ((str->dispatched_header_holes != NULL) && (range_length <= 0))
        { 
          kd_model_hole *hole = str->dispatched_header_holes;
          range_start = (int) hole->start;
          int range_lim = str->sim_header_span;
          assert(range_lim > range_start); // Otherwise chould not have called
          if (range_lim >= (int) hole->lim)
            { 
              range_lim = (int) hole->lim;
              str->dispatched_header_holes = hole->next;
              hole->next = NULL;
              serve->hole_server->release_holes(hole);
            }
          else
            hole->start = (kdu_uint16) range_lim;
          range_length = range_lim - range_start;
        }
      if (range_length <= 0)
        { 
          range_start = str->dispatched_header_span;
          range_length = str->sim_header_span - range_start;
          is_final = (str->sim_header_span_complete != 0);
          str->dispatched_header_span = str->sim_header_span;
          str->header_span_complete = str->sim_header_span_complete;
        }
    }
  
  out.open(chunk,range_start);
  kds_chunk *starting_chunk = chunk;
  kds_chunk *last_written_chunk = NULL;
  int restore_max_bytes=0, written_bytes=0;  
  int original_range_length = range_length;
  while ((range_length > 0) || (last_written_chunk == NULL))
    { 
      if ((chunk != starting_chunk) && decouple_chunks)
        id_encoder->decouple();
      int hdr_bytes = 0;
      if (tp == NULL)
        hdr_bytes = id_encoder->encode_id(NULL,KDU_MAIN_HEADER_DATABIN,
                                          str->stream_id,0,true);
      else
        hdr_bytes = id_encoder->encode_id(NULL,KDU_TILE_HEADER_DATABIN,
                                          str->stream_id,tnum,true);
      hdr_bytes += simulate_var_length(range_start);
      hdr_bytes += simulate_var_length(range_length);
      assert((suggested_chunk_bytes <= chunk->max_bytes) &&
             (suggested_chunk_bytes >= (chunk->prefix_bytes+8)));
      int xfer_bytes = suggested_chunk_bytes - chunk->num_bytes;
      xfer_bytes -= hdr_bytes;
      if (xfer_bytes >= range_length)
        xfer_bytes = range_length; // Can put whole thing in one message
      else if ((xfer_bytes < mandatory_splitting_threshold) ||
               ((xfer_bytes < (suggested_chunk_body_bytes >> 1)) &&
                (range_length <= suggested_chunk_body_bytes)))
        { // Start packing data at the next chunk
          if (chunk->num_bytes == chunk->prefix_bytes)
            { kdu_error e;
              e << "You must use larger chunks or smaller chunk "
              "prefixes for `kdu_serve::generate_increments' "
              "to be able to create a legal set of data chunks.";
            }
          assert(last_written_chunk == NULL);
          chunk->max_bytes = chunk->num_bytes; // Freeze chunk
          chunk = chunk->next = serve->chunk_server->get_chunk();
          continue;              
        }
      
      kd_chunk_binref *bref=serve->create_chunk_binref(str,tp,NULL,NULL,chunk);
      bref->prev_bytes = range_start;
      bref->num_bytes = xfer_bytes;
      
      int starting_chunk_bytes = chunk->num_bytes;
      if (tp == NULL)
        chunk->num_bytes +=
          id_encoder->encode_id(chunk->data+chunk->num_bytes,
                                KDU_MAIN_HEADER_DATABIN,str->stream_id,0,
                                (xfer_bytes == range_length) && is_final);
      else
        chunk->num_bytes +=
          id_encoder->encode_id(chunk->data+chunk->num_bytes,
                                KDU_TILE_HEADER_DATABIN,str->stream_id,tnum,
                                (xfer_bytes == range_length) && is_final);
      output_var_length(chunk,range_start);
      output_var_length(chunk,xfer_bytes);
      last_written_chunk = chunk;
      restore_max_bytes = chunk->max_bytes;
      assert(chunk->max_bytes >= (chunk->num_bytes+xfer_bytes));
      chunk->max_bytes = chunk->num_bytes + xfer_bytes;
      range_start += xfer_bytes;
      range_length -= xfer_bytes;
      written_bytes += (chunk->max_bytes - starting_chunk_bytes);
      if (range_length > 0)
        chunk = chunk->next = serve->chunk_server->get_chunk();
    }
  
  assert(last_written_chunk == chunk);
  kdu_params *ichg_params = str->interchange.access_siz();
  if (tnum < 0)
    out.put(KDU_SOC);
  ichg_params->generate_marker_segments(&out,tnum,0);
  if ((out.close() != last_written_chunk) && (original_range_length > 0))
    assert(0); // Could happen if message headers only
  assert(last_written_chunk->max_bytes == last_written_chunk->num_bytes);
  last_written_chunk->max_bytes = restore_max_bytes;

  return written_bytes;  
}

/*****************************************************************************/
/*             kd_window_context::generate_precinct_increment                */
/*****************************************************************************/

int
  kd_window_context::generate_precinct_increment(kd_active_precinct *prec,
                                               kd_stream *str, kd_tile *tp,
                                               kds_chunk * &chunk,
                                               bool decouple_chunks,
                                               bool use_extended_headers,
                                               int suggested_chunk_body_bytes)
{
  kds_id_encoder *id_encoder = serve->id_encoder;
  int mandatory_splitting_threshold = (suggested_chunk_body_bytes >> 2);
      // For consistency with the simulator
  int suggested_chunk_bytes =
    suggested_chunk_body_bytes + serve->chunk_prefix_bytes;
  
  int range_start=0, range_length=0;
  bool is_final = false;
  while ((prec->dispatched_holes != NULL) && (range_length <= 0))
    { 
      kd_model_hole *hole = prec->dispatched_holes;
      range_start = (int) hole->start;
      int range_lim = prec->sim_span;
      assert(range_lim > range_start); // Otherwise could not have called here
      if (range_lim >= (int) hole->lim)
        { 
          range_lim = (int) hole->lim;
          prec->dispatched_holes = hole->next;
          hole->next = NULL;
          serve->hole_server->release_holes(hole);
        }
      else
        hole->start = (kdu_uint16) range_lim;
      range_length = range_lim - range_start;
    }
  if (range_length <= 0)
    { 
      range_start = prec->dispatched_span;
      range_length = prec->sim_span - range_start;
      assert(range_length >= 0);
      is_final = (prec->sim_span_complete != 0);
      prec->dispatched_span = prec->sim_span;
      prec->span_complete = prec->sim_span_complete;
    }

  assert(range_start >= prec->num_packet_bytes);
  int original_range_start = range_start;
  int original_range_length = range_length;
  assert(((range_length > 0) || ((range_length == 0) && is_final)) &&
         prec->interchange.exists());
  kdu_long unique_id = prec->interchange.get_unique_id();

  kds_chunk *starting_chunk = chunk;
  kds_chunk *last_written_chunk = NULL;
  int restore_max_bytes=0, written_bytes=0;
  num_msg_boundaries = 0;
  while ((range_length > 0) || (last_written_chunk == NULL))
    { 
      if ((chunk != starting_chunk) && decouple_chunks)
        id_encoder->decouple();
      int hdr_bytes =
        id_encoder->encode_id(NULL,KDU_PRECINCT_DATABIN,str->stream_id,
                              unique_id,is_final,use_extended_headers);
      hdr_bytes += simulate_var_length(range_start);
      hdr_bytes += simulate_var_length(range_length);
      if (use_extended_headers)
        hdr_bytes += simulate_var_length(prec->sim_packets);
      assert((suggested_chunk_bytes <= chunk->max_bytes) &&
             (suggested_chunk_bytes >= (chunk->prefix_bytes+8)));
      int xfer_bytes = suggested_chunk_bytes - chunk->num_bytes;
      xfer_bytes -= hdr_bytes;
      if (xfer_bytes >= range_length)
        xfer_bytes = range_length; // Can put whole thing in one message
      else if ((xfer_bytes < mandatory_splitting_threshold) ||
               ((xfer_bytes < (suggested_chunk_body_bytes >> 1)) &&
                (range_length <= suggested_chunk_body_bytes)))
        { // Start packing data at the next chunk.
          if (chunk->num_bytes == chunk->prefix_bytes)
            { kdu_error e;
              e << "You must use larger chunks or smaller chunk "
              "prefixes for `kdu_serve::generate_increments' "
              "to be able to create a legal set of data chunks.";
            }
          assert(last_written_chunk == NULL);
          chunk->max_bytes = chunk->num_bytes; // Freeze chunk
          chunk = chunk->next = serve->chunk_server->get_chunk();
          continue;
        }
      
      kd_chunk_binref *bref =
        serve->create_chunk_binref(str,tp,prec,NULL,chunk);
      bref->prev_bytes = range_start;
      bref->num_bytes = xfer_bytes;
      
      int starting_chunk_bytes = chunk->num_bytes;
      chunk->num_bytes +=
        id_encoder->encode_id(chunk->data + chunk->num_bytes,
                              KDU_PRECINCT_DATABIN,str->stream_id,unique_id,
                              (xfer_bytes == range_length) && is_final,
                              use_extended_headers);
      output_var_length(chunk,range_start);
      output_var_length(chunk,xfer_bytes);      
      if (use_extended_headers)
        { 
          int var_start = chunk->num_bytes;
          output_var_length(chunk,prec->sim_packets);
          save_msg_boundary(chunk,var_start,chunk->num_bytes-var_start,
                            range_start+xfer_bytes);
        }
      last_written_chunk = chunk;
      restore_max_bytes = chunk->max_bytes;
      assert(chunk->max_bytes >= (chunk->num_bytes + xfer_bytes));
      chunk->max_bytes = chunk->num_bytes + xfer_bytes;
      range_start += xfer_bytes;
      range_length -= xfer_bytes;
      written_bytes += (chunk->max_bytes - starting_chunk_bytes);
      if (range_length > 0)
        chunk = chunk->next = serve->chunk_server->get_chunk();
    }
  assert(last_written_chunk == chunk);
  out.open(starting_chunk,original_range_start-prec->num_packet_bytes);
  int cum_packets=0, cum_bytes=range_start;
  prec->interchange.get_packets(prec->num_packets,0,
                                cum_packets,cum_bytes,&out);
  if ((out.close() != last_written_chunk) && (original_range_length > 0))
    assert(0);
  assert(last_written_chunk->max_bytes == last_written_chunk->num_bytes);
  last_written_chunk->max_bytes = restore_max_bytes;

  if (use_extended_headers)
    { // Need to walk through the message boundaries, figuring out the
      // packet boundaries for each one and rewriting the associated VBAS's
      int b;
      int cpackets = prec->num_packets;
      int cbytes = prec->num_packet_bytes;
      int accepted_cpackets = cpackets;
      int accepted_cbytes = cbytes;
      bool did_restart = false;
      if (cum_bytes != range_start)
        { prec->interchange.restart(); did_restart = true; }
      for (b=0; b < num_msg_boundaries; b++)
        { 
          kd_msg_boundary *bnd = msg_boundaries + b;
          if (bnd->range_lim > cbytes)
            { 
              if (cpackets == (cum_packets-1))
                { // We already have the next layer boundary.  Don't do
                  // simulation, because that would require us to reset again
                  cpackets = cum_packets;
                  cbytes = cum_bytes;
                }
              else
                { 
                  if (!did_restart)
                    { prec->interchange.restart(); did_restart = true; }
                  bool significant; // Dummy variable for `size_packets'
                  cbytes = bnd->range_lim;
                  prec->interchange.size_packets(cpackets,cbytes,significant);
                }
              assert((cbytes >= bnd->range_lim) && (cbytes <= cum_bytes) &&
                     (cpackets <= cum_packets));
            }
          if (bnd->range_lim <= cbytes)
            { 
              accepted_cbytes = cbytes;
              accepted_cpackets = cpackets;
            }

          // Write the `accepted_cpackets' into the VBAS.
          int save_num_bytes = bnd->chunk->num_bytes;
          bnd->chunk->num_bytes = bnd->vbas_start;
          output_var_length(bnd->chunk,accepted_cpackets,bnd->vbas_len);
          assert(bnd->chunk->num_bytes == (bnd->vbas_start+bnd->vbas_len));
          bnd->chunk->num_bytes = save_num_bytes;
        }
      assert((accepted_cpackets <= cum_packets) &&
             (accepted_cpackets <= range_start));
      prec->num_packets = (kdu_uint16) accepted_cpackets;
      prec->num_packet_bytes = accepted_cbytes;
    }
  else if (cum_bytes == range_start)
    { // Wrote a whole number of packets
      prec->num_packets = (kdu_uint16) cum_packets;
      prec->num_packet_bytes = range_start;
    }
  else
    { // Partial packet transfer
      assert((cum_bytes > range_start) && (cum_packets > 0));
      prec->num_packets = (kdu_uint16)(cum_packets-1);
      prec->interchange.restart(); // Partial transfers always require a restart
      if (prec->num_packets == prec->sim_packets)
        prec->num_packet_bytes = prec->sim_packet_bytes;
      else
        { // Need to explicitly compute the value of `prec->num_packet_bytes'
          cum_packets = prec->num_packets;
          cum_bytes = 0;
          bool significant; // Dummy variable for our purpose of `size_packets'
          prec->interchange.size_packets(cum_packets,cum_bytes,significant);
          assert((cum_packets == (int) prec->num_packets) &&
                 (cum_bytes < range_start));
          prec->num_packet_bytes = cum_bytes;
        }
    }

  return written_bytes;
}


/* ========================================================================= */
/*                           kd_codestream_window                            */
/* ========================================================================= */

/*****************************************************************************/
/*               kd_codestream_window::kd_codestream_window                  */
/*****************************************************************************/

kd_codestream_window::kd_codestream_window(kd_serve *owner)
{
  this->serve = owner;
  stream = NULL;
  context = NULL;
  codestream_comps_unrestricted = false;
  max_codestream_components = num_codestream_components = 0;
  codestream_components = NULL;
  max_context_components = num_context_components = 0;
  context_components = NULL;
  window_discard_levels = window_max_layers = 0;
  context_type = 0;
  context_idx = -1;
  member_idx = -1;
  remapping_ids[0] = remapping_ids[1] = -1;
  sequencing_active = is_active = false;
  fully_dispatched = content_incomplete = false;
  next = stream_next = stream_prev = NULL;
  max_scratch_components = 0;
  scratch_components = NULL;
  max_model_pblock_area = 0;
  model_pblock_buf = NULL;
}

/*****************************************************************************/
/*               kd_codestream_window::~kd_codestream_window                 */
/*****************************************************************************/

kd_codestream_window::~kd_codestream_window()
{
  assert((stream == NULL) && (context == NULL)); // No-one referencing us
  if (codestream_components != NULL)
    delete[] codestream_components;
  if (context_components != NULL)
    delete[] context_components;
  if (scratch_components != NULL)
    delete[] scratch_components;
  if (model_pblock_buf != NULL)
    delete[] model_pblock_buf;
}

/*****************************************************************************/
/*                     kd_codestream_window::initialize                      */
/*****************************************************************************/

void
  kd_codestream_window::initialize(kd_stream *stream,
                                   kd_window_context *context,
                                   kdu_coords resolution,
                                   kdu_dims region, int round_direction,
                                   int max_required_layers,
                                   kdu_range_set &component_ranges,
                                   int num_context_components,
                                   const int *context_component_indices)
{
  if (this->stream == NULL)
    { // Attach to `stream' for the first time
      assert(this->context == NULL);
      this->stream = stream;
      this->context = context;
      this->next = this->stream_next = this->stream_prev = NULL;
      if ((stream_next = stream->windows) != NULL)
        stream_next->stream_prev = this;
      stream->windows = this;
    }
  assert((this->stream == stream) && (this->context == context));
  
  this->window_discard_levels = 0; // Until proven otherwise
  this->window_max_layers = stream->max_quality_layers;
  if ((max_required_layers > 0) &&
      (max_required_layers < this->window_max_layers))
    this->window_max_layers = max_required_layers;
  this->window_region = stream->image_dims;
  this->num_context_components = 0;
  this->codestream_comps_unrestricted = false;
  this->component_ranges.init();
  this->sequencing_active = this->is_active = false;
  this->sequencer.reset();
  this->sequencer_start.reset();
  this->fully_dispatched = this->content_incomplete = false;
  if (stream->check_complete())
    this->fully_dispatched = true;
  if ((resolution.x < 1) || (resolution.y < 1) ||
      (region.size.x < 1) || (region.size.y < 1))
    { 
      this->window_tiles.size = kdu_coords(0,0);
      this->window_region.size = kdu_coords(0,0); // so we send only headers
      this->window_discard_levels = stream->max_discard_levels; // Just in case
      return;
    }
  
  // Start by figuring out the number of discard levels
  kdu_coords min = stream->image_dims.pos;
  kdu_coords size = stream->image_dims.size;
  kdu_coords lim = min + size;
  kdu_dims active_res; active_res.pos = min; active_res.size = size;
  kdu_long target_area = ((kdu_long) resolution.x) * resolution.y;
  kdu_long best_area_diff = 0;
  for (int d=0; d <= stream->max_discard_levels; d++)
    { 
      if (round_direction < 0)
        { // Round down
          this->window_discard_levels = d;
          active_res.size = size; active_res.pos = min;
          if ((size.x <= resolution.x) && (size.y <= resolution.y))
            break;
        }
      else if (round_direction > 0)
        { // Round up
          if ((size.x >= resolution.x) && (size.y >= resolution.y))
            { 
              this->window_discard_levels = d;
              active_res.size = size; active_res.pos = min;
            }
          else
            break;
        }
      else
        { // Round to closest in area
          kdu_long area = ((kdu_long) size.x) * ((kdu_long) size.y);
          kdu_long area_diff =
            (area < target_area)?(target_area-area):(area-target_area);
          if ((d == 0) || (area_diff < best_area_diff))
            { 
              this->window_discard_levels = d;
              active_res.size = size; active_res.pos = min;
              best_area_diff = area_diff;
            }
          if (area <= target_area)
            break;
        }
      min.x = (min.x+1)>>1;   min.y = (min.y+1)>>1;
      lim.x = (lim.x+1)>>1;   lim.y = (lim.y+1)>>1;
      size = lim - min;
    }
  
  // Now scale the image region to match the selected image resolution
  min = region.pos;
  lim = min + region.size;
  window_region.pos.x = (int)
    ((((kdu_long) min.x) * ((kdu_long) active_res.size.x)) /
     ((kdu_long) resolution.x));
  window_region.pos.y = (int)
    ((((kdu_long) min.y) * ((kdu_long) active_res.size.y)) /
     ((kdu_long) resolution.y));
  window_region.size.x = 1 + (int)
    ((((kdu_long)(lim.x-1)) * ((kdu_long) active_res.size.x)) /
     ((kdu_long) resolution.x)) - window_region.pos.x;
  window_region.size.y = 1 + (int)
    ((((kdu_long)(lim.y-1)) * ((kdu_long) active_res.size.y)) /
     ((kdu_long) resolution.y)) - window_region.pos.y;
  window_region.pos += active_res.pos;
  window_region &= active_res;
  
  if ((window_region.size.x < 1) || (window_region.size.y < 1))
    {
      this->window_tiles.size = kdu_coords(0,0);
      this->window_region.size = kdu_coords(0,0); // so we send only headers
    }

  // Now adjust the image region up onto the full image canvas
  window_region.pos.x <<= window_discard_levels;
  window_region.pos.y <<= window_discard_levels;
  window_region.size.x <<= window_discard_levels;
  window_region.size.y <<= window_discard_levels;
  window_region &= stream->image_dims;
  
  // Calculate the set of relevant tiles
  if (!window_region)
    window_tiles.size = kdu_coords(0,0);
  else
    { 
      min = window_region.pos - stream->tile_partition.pos;
      lim = min + window_region.size;
      min.x = floor_ratio(min.x,stream->tile_partition.size.x);
      min.y = floor_ratio(min.y,stream->tile_partition.size.y);
      lim.x = ceil_ratio(lim.x,stream->tile_partition.size.x);
      lim.y = ceil_ratio(lim.y,stream->tile_partition.size.y);
      window_tiles.pos = min;
      window_tiles.size = lim - min;
    }
  
  // Install the image components which belong to the window.
  if (component_ranges.is_empty())
    { 
      this->component_ranges.add(0,stream->num_components-1);
      this->codestream_comps_unrestricted = true;
    }
  else
    { 
      this->component_ranges.copy_from(component_ranges);
      this->codestream_comps_unrestricted = false;
    }
  
  if (stream->num_components > max_codestream_components)
    {
      max_codestream_components = stream->num_components;
      if (codestream_components != NULL)
        delete[] codestream_components;
      codestream_components = new int[max_codestream_components];
    }
  
  num_codestream_components =
    this->component_ranges.expand(codestream_components,0,
                                  stream->num_components-1);
  assert(num_codestream_components <= max_codestream_components);
  this->num_context_components = num_context_components;
  if (num_context_components > this->max_context_components)
    { 
      this->max_context_components += num_context_components;
      if (this->context_components != NULL)
        delete[] this->context_components;
      this->context_components = new int[this->max_context_components];
    }
  for (int n=0; n < num_context_components; n++)
    this->context_components[n] = context_component_indices[n];
}

/*****************************************************************************/
/*            kd_codestream_window::get_max_extra_discard_levels             */
/*****************************************************************************/

int kd_codestream_window::get_max_extra_discard_levels()
{
  int result = stream->max_discard_levels - window_discard_levels;
  if (result < 0)
    { 
      assert(0); // Should not be possible
      result = 0;
    }
  return result;
}

/*****************************************************************************/
/*                 kd_codestream_window::get_window_samples                  */
/*****************************************************************************/

kdu_long
  kd_codestream_window::get_window_samples(int extra_discard_levels)
{
  int actual_discard_levels = window_discard_levels + extra_discard_levels;
  if (actual_discard_levels > stream->max_discard_levels)
    actual_discard_levels = stream->max_discard_levels;
  
  kdu_coords size = window_region.size;
  size.x = 1 + (size.x >> actual_discard_levels);
  size.y = 1 + (size.y >> actual_discard_levels);
  
  int n;
  kdu_long result = 0;
  for (n=0; n < num_context_components; n++)
    {
      int idx = this->context_components[n];
      if (idx >= stream->num_output_components)
        continue;
      kdu_long area = 1 + size.x / stream->output_component_subs[idx].x;
      area *= 1 + size.y / stream->output_component_subs[idx].y;
      result += area;
    }
  if ((result > 0) && codestream_comps_unrestricted)
    return result;
  
  kdu_long alt_result = 0;
  for (n=0; n < num_codestream_components; n++)
    { 
      int idx = this->codestream_components[n];
      kdu_long area = 1 + size.x / stream->component_subs[idx].x;
      area *= 1 + size.y / stream->component_subs[idx].y;
      alt_result += area;
    }
  if ((num_context_components == 0) || (alt_result < result))
    result = alt_result;
  
  return result;
}

/*****************************************************************************/
/*                       kd_codestream_window::contains                      */
/*****************************************************************************/

bool
  kd_codestream_window::contains(kd_codestream_window *rhs)
{
  if ((rhs->stream != this->stream) || (rhs->context != this->context) ||
      (rhs->window_discard_levels < this->window_discard_levels) ||
      (rhs->window_max_layers > this->window_max_layers) ||
      (!this->window_region.contains(rhs->window_region)))
    return false;
  int c;
  if (!this->codestream_comps_unrestricted)
    { // See if all `rhs' components are contained within us
      if (rhs->codestream_comps_unrestricted)
        return false;
      for (c=0; c < rhs->num_codestream_components; c++)
        if (!this->component_ranges.test(rhs->codestream_components[c]))
          return false;
    }
  if (this->num_context_components > 0)
    { // Image components are restricted by output components.  In this case,
      // RHS must have restrictions which are at least as tight
      if (rhs->num_context_components == 0)
        return false; // No output-component-based restrictions on RHS
      for (c=0; c < rhs->num_context_components; c++)
        {
          int d;
          for (d = 0; d < this->num_context_components; d++)
            if (this->context_components[d] == rhs->context_components[c])
              break;
          if (d == this->num_context_components)
            return false; // No match found
        }
    }
  return true;
}

/*****************************************************************************/
/*                 kd_codestream_window::sequence_active_bins                */
/*****************************************************************************/

bool
  kd_codestream_window::sequence_active_bins(kd_active_binref * &head,
                                             kd_active_binref *res_tails[],
                                             int extra_discard_levels,
                                             kdu_long max_samples,
                                             kdu_window_model *model)
{
  if (this->fully_dispatched)
    return false;
  if (extra_discard_levels < 0)
    extra_discard_levels = 0; // Just in case
  int active_discard_levels = window_discard_levels + extra_discard_levels;
  if (active_discard_levels > stream->max_discard_levels)
    active_discard_levels = stream->max_discard_levels;
  if (max_samples <= 0)
    max_samples = KDU_LONG_HUGE; // So we don't have any limits
  bool ignore_relevance_info = serve->ignore_relevance_info;
  bool is_stateless = serve->is_stateless;
  kdu_long pid_gap = stream->pid_gap;
  
  kd_active_binref *elt, **rtail=NULL;
  bool added_something = false;
  if (!sequencing_active)
    { // Initialize the `sequencer' state variables and add a codestream
      // main header data-bin reference if required.
      sequencing_active = true;
      content_incomplete = false; // Until proven otherwise
      sequencer.reset();
      sequencer_start.reset();
    }
  bool force_tile_header_bin = false;
  if (sequencer_start == sequencer)
    { 
      force_tile_header_bin = true; // Might be following `sync_sequencer'
      stream->ensure_expanded();
      if ((model != NULL) && !(is_stateless && stream->is_modeled))
        { // Above test ensures we don't repeat non-atomic instructions
          int mi_str=stream->stream_id, mi_buf[2], cnt;
          if ((cnt = model->get_header_instructions(mi_str,-1,mi_buf)) > 0)
            stream->process_header_model_instructions(-1,mi_buf,cnt);
        }
      stream->is_modeled = true;
      if (!stream->check_header_complete())
        { 
          elt = serve->create_active_binref(stream,NULL);
          rtail = res_tails;
          if (*rtail == NULL)
            init_res_tail(elt,rtail,res_tails,head);
          else
            {
              elt->next = (*rtail)->next;
              (*rtail)->next = elt;
              *rtail = elt;
            }
          this->is_active = added_something = true;
        }
    }
  if (extra_discard_levels > 0)
    content_incomplete = true;
  sequencer_start = sequencer;
  
  kdu_long new_samples = 0;
  for (; sequencer.t_idx.y < window_tiles.size.y;
       sequencer.t_idx.x = 0, sequencer.t_idx.y++)
    for (; sequencer.t_idx.x < window_tiles.size.x;
         sequencer.t_idx.x++, sequencer.cn=0)
      { 
        kdu_coords abs_t_idx = sequencer.t_idx + window_tiles.pos;
        kd_tile *tp = stream->access_tile(abs_t_idx);
        if ((model != NULL) && !(is_stateless && tp->is_modeled))
          { // Above test ensures we don't repeat non-atomic instructions
            int mi_str=stream->stream_id, mi_buf[2], cnt;
            if ((cnt =
                 model->get_header_instructions(mi_str,tp->tnum,mi_buf)) > 0)
              stream->process_header_model_instructions(tp->tnum,mi_buf,cnt);
          }
        if (!tp->is_modeled)
          { // Mark this tile as modeled
            tp->is_modeled = true;
            tp->next_modeled = stream->modeled_tiles;
            stream->modeled_tiles = tp;
          }
        if (tp->check_complete())
          { // Nothing more to do here
            stream->close_tile(tp); // Good practice to do this after
            continue;               // `access_tile' even without `open_tile'.
          }
        stream->open_tile(tp,false,&window_region);
        int tiles_across = stream->tile_indices.size.x;
        if ((force_tile_header_bin ||
            ((sequencer.cn == 0) && (sequencer.r_idx == 0) &&
             (sequencer.p_idx == kdu_coords(0,0)))) &&
            !tp->check_header_complete())
          { // Add tile header data-bin reference
            elt = serve->create_active_binref(stream,tp);
            rtail = res_tails;
            if (*rtail == NULL)
              init_res_tail(elt,rtail,res_tails,head);
            else
              { 
                elt->next = (*rtail)->next;
                (*rtail)->next = elt;
                *rtail = elt;
              }
            if (!added_something)
              { 
                sequencer_start = sequencer;
                this->is_active = added_something = true;
              }
          }
        force_tile_header_bin = false; // At most the first needs to be forced
        int num_scan_comps = this->num_codestream_components;
        int *scan_comps = this->codestream_components;
        if (this->num_context_components > 0)
          { 
            int nsi, nso, nbi, nbo;
            tp->structure.set_components_of_interest(num_context_components,
                                                     context_components);
            tp->structure.get_mct_block_info(0,0,nsi,nso,nbi,nbo);
            num_scan_comps = nsi;
            scan_comps = get_scratch_components(num_scan_comps);
            tp->structure.get_mct_block_info(0,0,nsi,nso,nbi,nbo,
                                             NULL,NULL,NULL,NULL,scan_comps);
          }
        for (; sequencer.cn < num_scan_comps;
             sequencer.cn++, sequencer.r_idx=0)
          { 
            int comp_idx = scan_comps[sequencer.cn];
            assert((comp_idx>=0) && (comp_idx<stream->num_components));
            if (!component_ranges.test(comp_idx))
              continue;
            
            double component_relevance = 1.0;
            if (!ignore_relevance_info)
              component_relevance =
                (tp->structure.find_component_gain_info(comp_idx,true) /
                 tp->structure.find_component_gain_info(comp_idx,false));
            kd_tile_comp *tc = tp->comps + comp_idx;
            assert(tc->c_idx == comp_idx);
            kdu_tile_comp tci=tp->structure.access_component(comp_idx);
            int r_lim = tc->num_resolutions-active_discard_levels;
            if (r_lim < 1)
              { assert(0); r_lim = 1; }
            for (; sequencer.r_idx < r_lim;
                 sequencer.r_idx++, sequencer.p_idx.y = 0)
              { 
                rtail = res_tails;
                if (sequencer.r_idx > 0)
                  { 
                    int res_off = sequencer.r_idx+33-tc->num_resolutions;
                    if (res_off > 0)
                      rtail += (res_off < 32)?res_off:32;
                  }
                kd_resolution *rp = tc->res + sequencer.r_idx;
                if (!rp->is_modeled)
                  { // Add to list of modeled resolutions
                    rp->is_modeled = true;
                    rp->next_modeled = tp->modeled_resolutions;
                    tp->modeled_resolutions = rp;
                  }
                kdu_resolution rpi =
                  tci.access_resolution(sequencer.r_idx);
                kdu_dims p_region;
                rpi.get_valid_precincts(p_region);
                int res_precincts_across = rp->precinct_indices.size.x;
                bool need_pblk_top=false, need_pblk_left;
                for (; sequencer.p_idx.y < p_region.size.y; need_pblk_top=true,
                     sequencer.p_idx.y++, sequencer.p_idx.x=0)
                  for (need_pblk_left=false;
                       sequencer.p_idx.x < p_region.size.x;
                       need_pblk_left=true, sequencer.p_idx.x++)
                    { 
                      kdu_coords p_idx = sequencer.p_idx+p_region.pos;
                      kdu_dims pblk_dims;
                      if ((model != NULL) &&
                          rp->access_pblock(p_idx,pblk_dims,need_pblk_top,
                                            need_pblk_left,is_stateless))
                        {
                          int *pblk_buf =
                            get_scratch_pblock_model_buf(pblk_dims.size);
                          if (model->get_precinct_block(stream->stream_id,
                                     tp->tnum,comp_idx,rp->r_idx,
                                     tiles_across,res_precincts_across,
                                     rp->pid_base,pid_gap,pblk_dims,pblk_buf))
                            rp->process_pblock_model_instructions(pblk_dims,
                                                                  pblk_buf);
                        }
                      kd_precinct_model *prec = rp->access_precinct(p_idx);
                      if (prec->check_complete())
                        continue;
                      int need_packets = window_max_layers;
                      if (need_packets < tp->num_layers)
                        { // May be sufficiently complete already
                          kd_active_precinct *active = prec->get_active();
                          if (active != NULL)
                            { 
                              if (need_packets >= (int)active->num_packets)
                                continue;
                            }
                          else if (prec->get_span() <= -need_packets)
                            continue;
                        }
                      
                      // If we get here, we need to deliver something, or
                      // at least figure out how many packets are in the cache
                      elt=serve->create_active_binref(tp,rp,prec,p_idx);
                      if (*rtail == NULL)
                        init_res_tail(elt,rtail,res_tails,head);
                      else
                        { 
                          elt->next = (*rtail)->next;
                          (*rtail)->next = elt;
                          *rtail = elt;
                        }
                      elt->num_active_layers = need_packets;
                      if (!added_something)
                        { 
                          this->is_active = added_something = true;
                          sequencer_start = sequencer;
                        }
                      if (!ignore_relevance_info)
                        { 
                          double relfrac = component_relevance *
                          rpi.get_precinct_relevance(p_idx);
                          elt->log_relevance = kd_log_rel.lookup(relfrac);
                        }
                      new_samples += elt->precinct->num_samples;     
                      if (new_samples >= max_samples)
                        { // Return early leaving sequencing active
                          stream->close_tile(tp);
                          return true;
                        }
                    }
              }
          }
        stream->close_tile(tp);
      }

  sequencing_active = false;
  if (!(content_incomplete || added_something))
    fully_dispatched = true;

  return added_something;
}

/*****************************************************************************/
/*                    kd_codestream_window::sync_sequencer                   */
/*****************************************************************************/

void kd_codestream_window::sync_sequencer(kd_window_sequencer &seq,
                                          int extra_discard_levels)
{
  if ((seq.t_idx != kdu_coords(0,0)) || (seq.cn != 0) ||
      (seq.r_idx != 0) || (seq.p_idx != kdu_coords(0,0)))
    { // Just go ahead and assume that `seq' is compatible with the current
      // codestream window.  If it is not, we will find out inside the
      // `sequence_active_bins' function and make necessary adjustments.
      this->content_incomplete = true; // Have to assume this for now
      this->sequencer_start = this->sequencer = seq;
      this->sequencing_active = true;
    }
}


/* ========================================================================= */
/*                                 kd_serve                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                            kd_serve::kd_serve                             */
/*****************************************************************************/

kd_serve::kd_serve(kdu_serve *owner)
{
  this->owner = owner;
  target = NULL;
  contexts = NULL;
  chunk_server = NULL;
  active_precinct_server = NULL;
  binref_server = NULL;
  hole_server = NULL;
  pblock_server = NULL;
  id_encoder = NULL;
  max_chunk_body_bytes = 0;
  ignore_relevance_info = false;
  is_stateless = is_stateful = false;
  metatree = NULL;
  total_metabins = num_completed_metabins = 0;
  modeled_metabins = NULL;
  total_codestreams = num_completed_codestreams = 0;
  stream_refs = NULL;
  collapsed_unused_streams = collapsed_unused_tail = NULL;
  lightweight_unused_streams = lightweight_unused_tail = NULL;
  mediumweight_unused_streams = mediumweight_unused_tail = NULL;
  attached_unused_streams = attached_unused_tail = NULL;
  inactive_streams = inactive_tail = NULL;
  active_streams = active_tail = NULL;
  num_attached_streams = num_expanded_streams = num_collapsed_streams = 0;
  total_detached_interface_mem = 0;
  free_codestream_windows = NULL;
  active_precincts = NULL;
  num_locked_stream_indices = 0;
  memset(locked_stream_indices,0,sizeof(int)*KD_MAX_ACTIVE_CODESTREAMS);
}

/*****************************************************************************/
/*                            kd_serve::~kd_serve                            */
/*****************************************************************************/

kd_serve::~kd_serve()
{
  kd_window_context *wc;
  while ((wc=contexts) != NULL)
    { 
      contexts = wc->next;
      delete wc;
    }
  
  if (stream_refs != NULL)
    { 
      for (int s=0; s < total_codestreams; s++)
        { 
          kd_stream *stream = stream_refs[s];
          if (stream != NULL)
            { 
              detach_stream(stream,true); // No harm, even if not attached
              delete stream;
              stream_refs[s] = NULL;
            }
        }
      delete[] stream_refs;
      stream_refs = NULL;
    }
  
  kd_meta *mp;
  while ((mp=metatree) != NULL)
    {
      metatree = mp->next_in_bin;
      delete mp; // Recursively deletes the `phld'-linked sub-trees.
    }  
  
  kd_codestream_window *wp;
  while ((wp = free_codestream_windows) != NULL)
    { 
      free_codestream_windows = wp->next;
      delete wp;
    }

  if (chunk_server != NULL)
    { delete chunk_server; chunk_server = NULL; }
  if (active_precinct_server != NULL)
    { delete active_precinct_server; active_precinct_server = NULL; }
  if (binref_server != NULL)
    { delete binref_server; binref_server = NULL; }
  if (hole_server != NULL)
    { delete hole_server; hole_server = NULL; }
  if (pblock_server != NULL)
    { delete pblock_server; pblock_server = NULL; }
  id_encoder = NULL;
}

/*****************************************************************************/
/*                            kd_serve::initialize                           */
/*****************************************************************************/

void
  kd_serve::initialize(kdu_serve_target *target, int max_chunk_size,
                       int chunk_prefix_bytes, bool ignore_relevance_info,
                       kds_id_encoder *custom_id_encoder)
{
  chunk_server = new kd_chunk_server(max_chunk_size);
  chunk_server->set_chunk_prefix_bytes(chunk_prefix_bytes);
  this->chunk_prefix_bytes = chunk_prefix_bytes;
  this->max_chunk_body_bytes = max_chunk_size - chunk_prefix_bytes;
  
  active_precinct_server = new kd_active_precinct_server;
  binref_server = new kd_binref_server;
  hole_server = new kd_model_hole_server;
  pblock_server = new kd_pblock_server;
  this->target = target;
  this->ignore_relevance_info = ignore_relevance_info;
  this->id_encoder = &default_id_encoder;
  if (custom_id_encoder != NULL)
    this->id_encoder = custom_id_encoder;
  
  // Configure stream refs and an initial window context
  contexts = new kd_window_context(this,0);
  contexts->next = NULL; // Just to be sure
  int num_stream_ranges=0;
  int *stream_ranges = target->get_codestream_ranges(num_stream_ranges);
  total_codestreams = 0;
  for (; num_stream_ranges > 0; num_stream_ranges--, stream_ranges+=2)
    total_codestreams += (stream_ranges[1]-stream_ranges[0])+1;
  stream_refs = new kd_stream *[total_codestreams];
  memset(stream_refs,0,sizeof(kd_stream *)*(size_t)total_codestreams);
  
  // Now build the meta-data tree
  const kds_metagroup *mgroup=target->get_metatree();
  assert(mgroup != NULL); // Even for raw streams, need one group with 0 length
  kd_meta *mnew, *mtail=NULL;
  metatree=NULL;
  total_metabins = num_completed_metabins = 0;
  int bin_off=0;
  for (; mgroup != NULL; mgroup=mgroup->next)
    {
      mnew = new kd_meta;
      total_metabins += mnew->init(mgroup,NULL,0,bin_off,0);
      bin_off += mnew->num_bytes;
      mnew->prev_in_bin = mtail;
      if (mtail == NULL)
        metatree = mtail = mnew;
      else
        mtail = mtail->next_in_bin = mnew;
    }
  
  // Before we can finish up here, we need to find the `link_target'
  // members which belong to any `kds_metagroup::link_fpos' addresses, so
  // that whenever a cross-reference box is served, the header (at least)
  // of any box into whose contents it points will also be served.  Without
  // this, there is no general way that a remote client can follow links
  // and dereferenced cross-referenced boxes, because a file position in the
  // original file cannot be converted to a JPIP data-bin index unless the
  // headers of all boxes which contain the location are available.
  for (mnew=metatree; mnew != NULL; mnew=mnew->next_in_bin)
    mnew->resolve_links(metatree);
}

/*****************************************************************************/
/*                  kd_serve::create_active_binref (header)                  */
/*****************************************************************************/

kd_active_binref *
  kd_serve::create_active_binref(kd_stream *stream, kd_tile *tp)
{
  assert(stream != NULL);
  kd_active_binref *elt = binref_server->get_active_binref();
  elt->meta=NULL; elt->stream=stream; elt->tile=tp; elt->precinct=NULL;
  elt->log_relevance = 65535;  elt->num_active_layers = 0;  elt->next = NULL;
  if (tp != NULL)
    { 
      tp->num_tile_binrefs++;
      tp->sim_header_span_complete = tp->header_span_complete;
      tp->sim_header_span = tp->dispatched_header_span;
      if ((tp->dispatched_header_holes != NULL) &&
          (tp->sim_header_span > (int) tp->dispatched_header_holes->start))
        tp->sim_header_span = (int) tp->dispatched_header_holes->start;
    }
  else
    { 
      stream->sim_header_span_complete = stream->header_span_complete;
      stream->sim_header_span = stream->dispatched_header_span;
      if ((stream->dispatched_header_holes != NULL) &&
          (stream->sim_header_span >
           (int) stream->dispatched_header_holes->start))
        stream->sim_header_span = (int) stream->dispatched_header_holes->start;
    }
  return elt;
}

/*****************************************************************************/
/*                 kd_serve::create_active_binref (precinct)                 */
/*****************************************************************************/

kd_active_binref *
  kd_serve::create_active_binref(kd_tile *tp, kd_resolution *rp,
                                 kd_precinct_model *prec, kdu_coords p_idx)
{
  assert(rp->tile_comp->tile == tp); // Consistency check
  kd_active_precinct *active = prec->get_active();
  if (active == NULL)
    { 
      active = active_precinct_server->get_precinct();
      if ((active->next = this->active_precincts) != NULL)
        active->next->prev = active;
      this->active_precincts = active;
      prec->make_active(active,hole_server);
      // Note: `active' had its members all initialized to 0, so we only have
      // to set the non-zero values here.
      assert((active->res == NULL) && (active->cache == NULL));
      active->cache = prec;
      active->p_idx = p_idx;
      active->max_packets = (kdu_uint16) tp->num_layers;
      active->sim_span_complete = active->span_complete;
      active->sim_packets = active->num_packets;
      active->res = rp;
      kdu_coords off = p_idx - rp->precinct_indices.pos;
      assert((off.x >= 0) && (off.x < rp->precinct_indices.size.x) &&
             (off.y >= 0) && (off.y < rp->precinct_indices.size.y));
      if (off.y <= 0)
        { // precinct is on top row
          if (off.x <= 0)
            active->num_samples = rp->tl_samples;
          else if (off.x < (rp->precinct_indices.size.x-1))
            active->num_samples = rp->top_samples;
          else
            active->num_samples = rp->tr_samples;
        }
      else if (off.y < (rp->precinct_indices.size.y-1))
        { // precinct is in a middle row
          if (off.x <= 0)
            active->num_samples = rp->left_samples;
          else if (off.x < (rp->precinct_indices.size.x-1))
            active->num_samples = rp->centre_samples;
          else
            active->num_samples = rp->right_samples;
        }
      else
        { // precinct is on bottom row
          if (off.x <= 0)
            active->num_samples = rp->bl_samples;
          else if (off.x < (rp->precinct_indices.size.x-1))
            active->num_samples = rp->bottom_samples;
          else
            active->num_samples = rp->br_samples;
        }
      tp->num_active_precincts++;
    }
  
  kd_active_binref *elt = binref_server->get_active_binref();
  elt->meta=NULL; elt->stream=tp->stream; elt->tile=tp; elt->precinct=active;
  elt->log_relevance = 0; elt->num_active_layers = 0; elt->next = NULL;
  active->num_active_binrefs++;
  
  assert((rp == active->res) && (tp->num_active_precincts > 0));
  
  return elt;
}

/*****************************************************************************/
/*                  kd_serve::create_active_meta_binrefs                     */
/*****************************************************************************/

bool kd_serve::create_active_meta_binrefs(kd_active_binref * &head,
                                          kd_active_binref * &tail,
                                          kdu_window &current_window,
                                          kd_codestream_window *first_window,
                                          kd_codestream_window *lim_window,
                                          kdu_window_model *model)
{
  bool have_active_link_targets = false;
  kd_meta *mscan;
  for (mscan=metatree; mscan != NULL; mscan=mscan->next_in_bin)
    if (mscan->find_active_scope_and_sequence(current_window,
                                              first_window,lim_window))
      have_active_link_targets = true;
  if (have_active_link_targets)
    for (mscan=metatree; mscan != NULL; mscan=mscan->next_in_bin)
      mscan->add_active_link_targets_to_scope();
  
  kd_meta *mstart = metatree;
  mstart->active_next = NULL;
  for (mscan=metatree; mscan != NULL;
       mstart=mscan, mscan=mscan->next_in_bin)
    {
      assert(mscan->in_scope); // All top-level boxes are necessarily in scope!
      mscan->include_active_groups(mstart); // Recursive function
    }
  
  bool added_something = false;
  for (mscan=metatree; mscan != NULL; mscan=mscan->active_next)
    { 
      if ((model != NULL) && !(is_stateless && mscan->is_modeled))
        { 
          kdu_long meta_bin_id = mscan->bin_id;
          int i_cnt, i_buf[2];
          if ((i_cnt = model->get_meta_instructions(meta_bin_id,i_buf)) > 0)
            process_metabin_model_instructions(mscan,i_buf,i_cnt);
        }
      if (!mscan->is_modeled)
        { 
          assert(mscan->prev_in_bin==NULL); // Should have already visited it
          kd_meta *mp, *last_mp=NULL;
          for (mp=mscan; mp != NULL; last_mp=mp, mp=mp->next_in_bin)
            mp->is_modeled = 1;
          last_mp->next_modeled = modeled_metabins;
          modeled_metabins = last_mp;
        }
      if ((mscan->dispatched_holes == NULL) &&
          (mscan->active_length <= mscan->dispatched_span) &&
          (mscan->span_complete || (mscan->next_in_bin != NULL) ||
           (mscan->active_length < mscan->num_bytes)))
        continue; // Don't need to send anything
      kd_active_binref *elt = binref_server->get_active_binref();
      elt->meta=mscan; elt->stream=NULL; elt->tile=NULL; elt->precinct=NULL;
      elt->num_active_bytes = mscan->active_length;  elt->next = NULL;
      if (tail == NULL)
        head = tail = elt;
      else
        tail = (kd_active_binref *)(tail->next = elt);
      int tmp=mscan->sequence; // Holds 0 or 64+log_2(region-cost/region-area)
      if (tmp <= 0)
        elt->log_relevance = 65535; // Maximum possible relevance
      else
        {
          tmp = 64 - tmp; // Yields log_2(region-area / region-cost (in bytes))
          tmp -= 6; // Comparable to log_2(Delta_D/Delta_L) with MSE of (1/8)^2
          tmp = 256-64 + tmp;
          if (tmp < 0)
            tmp = 0;
          elt->log_relevance = 256*tmp; // This value is directly comparable
                                        // with the layer-log-slope thresholds
        }
      mscan->sim_span_complete = mscan->span_complete;
      mscan->sim_span = mscan->dispatched_span;
      if ((mscan->dispatched_holes != NULL) &&
          (mscan->sim_span > (int) mscan->dispatched_holes->start))
        mscan->sim_span = (int) mscan->dispatched_holes->start;
      added_something = true;
    }
  return added_something;
}

/*****************************************************************************/
/*                     kd_serve::release_active_binrefs                      */
/*****************************************************************************/

void kd_serve::release_active_binrefs(kd_active_binref *list)
{
  kd_active_binref *scan;
  for (scan=list; scan != NULL; scan=scan->next)
    { // Modify reference counters and take action as appropriate
      if (scan->precinct != NULL)
        { 
          kd_active_precinct *active = scan->precinct;
          kd_tile *tp = scan->tile;
          scan->precinct = NULL; scan->tile = NULL; // Just in case
          assert((active != NULL) && (active->num_active_binrefs > 0) &&
                 (active->res->tile_comp->tile == tp) &&
                 (tp->num_active_precincts > 0));
          if ((--active->num_active_binrefs) == 0)
            { // Remove the active precinct
              kd_precinct_model *prec = active->cache;
              prec->make_inactive();
              if (active->interchange.exists())
                active->interchange.close();
              if (active->prev == NULL)
                { 
                  assert(active == this->active_precincts);
                  this->active_precincts = active->next;
                }
              else
                active->prev->next = active->next;
              if (active->next != NULL)
                active->next->prev = active->prev;
              if (active->dispatched_holes != NULL)
                { 
                  hole_server->release_holes(active->dispatched_holes);
                  active->dispatched_holes = NULL;
                }
              active_precinct_server->release_precinct(active);
              if (((--tp->num_active_precincts) == 0) &&
                  (tp->num_tile_binrefs == 0))
                tp->stream->close_tile(tp);
            }
        }
      else if (scan->tile != NULL)
        { 
          kd_tile *tp = scan->tile;
          scan->tile = NULL; // Just in case
          assert(tp->num_tile_binrefs > 0);
          if (((--tp->num_tile_binrefs) == 0) &&
              (tp->num_active_precincts == 0))
            tp->stream->close_tile(tp);
        }
    }
  binref_server->release_active_binrefs(list);
}

/*****************************************************************************/
/*                      kd_serve::create_chunk_binref                        */
/*****************************************************************************/

kd_chunk_binref *
  kd_serve::create_chunk_binref(kd_stream *str, kd_tile *tp,
                                kd_active_precinct *prec, kd_meta *meta,
                                kds_chunk *chunk)
{
  kd_chunk_binref *elt = binref_server->get_chunk_binref();
  elt->stream = str;
  elt->tnum = (tp==NULL)?-1:tp->tnum;
  elt->meta = meta;
  if (prec == NULL)
    elt->cnum = elt->rnum = -1;
  else
    { 
      kd_resolution *res = prec->res;
      elt->rnum = (kdu_int16) res->r_idx;  
      elt->cnum = (kdu_int16) res->tile_comp->c_idx;
      elt->p_idx.x = prec->p_idx.x;  elt->p_idx.y = prec->p_idx.y;
    }
  elt->prev_bytes = 0;
  elt->next = chunk->bins;
  chunk->bins = elt;
  if (str != NULL)
    str->num_chunk_binrefs++;
  return elt;
}

/*****************************************************************************/
/*                          kd_serve::get_stream                             */
/*****************************************************************************/

kd_stream *kd_serve::get_stream(int stream_id, bool create_if_missing)
{ 
  if ((stream_refs == NULL) ||
      (stream_id < 0) || (stream_id >= total_codestreams))
    return NULL;
  kd_stream *stream = stream_refs[stream_id];
  if ((stream != NULL) || !create_if_missing)
    return stream;
  stream = stream_refs[stream_id] = new kd_stream(this);
  stream->initialize(target,stream_id,ignore_relevance_info);
  stream->prev = NULL;
  if ((stream->next = collapsed_unused_streams) != NULL)
    stream->next->prev = stream;
  else
    collapsed_unused_tail = stream;
  collapsed_unused_streams = stream;
  num_collapsed_streams++;
  return stream;
}

/*****************************************************************************/
/*                      kd_serve::move_stream_to_list                        */
/*****************************************************************************/

void kd_serve::move_stream_to_list(kd_stream *stream, kd_stream * &list)
{ 
  if (stream->prev == NULL)
    { // Need to find out what list we're on
      if (stream == collapsed_unused_streams)
        collapsed_unused_streams = stream->next;
      else if (stream == lightweight_unused_streams)
        lightweight_unused_streams = stream->next;
      else if (stream == mediumweight_unused_streams)
        mediumweight_unused_streams = stream->next;
      else if (stream == attached_unused_streams)
        attached_unused_streams = stream->next;
      else if (stream == inactive_streams)
        inactive_streams = stream->next;
      else if (stream == active_streams)
        active_streams = stream->next;
      else
        { 
          assert(0);
          abort();
        }
    }
  else
    stream->prev->next = stream->next;
  if (stream->next == NULL)
    { // Need to find out what list we're on
      if (stream == collapsed_unused_tail)
        collapsed_unused_tail = stream->prev;
      else if (stream == lightweight_unused_tail)
        lightweight_unused_tail = stream->prev;
      else if (stream == mediumweight_unused_tail)
        mediumweight_unused_tail = stream->prev;
      else if (stream == attached_unused_tail)
        attached_unused_tail = stream->prev;
      else if (stream == inactive_tail)
        inactive_tail = stream->prev;
      else if (stream == active_tail)
        active_tail = stream->prev;
      else
        { 
          assert(0);
          abort();
        }
    }
  else
    stream->next->prev = stream->prev;
  
  stream->prev = NULL;
  if ((stream->next = list) != NULL)
    list->prev = stream;
  else
    { // Need to update the tail of the list also
      if (&list == &collapsed_unused_streams)
        collapsed_unused_tail = stream;
      else if (&list == &lightweight_unused_streams)
        lightweight_unused_tail = stream;
      else if (&list == &mediumweight_unused_streams)
        mediumweight_unused_tail = stream;
      else if (&list == &attached_unused_streams)
        attached_unused_tail = stream;
      else if (&list == &inactive_streams)
        inactive_tail = stream;
      else if (&list == &active_streams)
        active_tail = stream;
      else
        { 
          assert(0);
          abort();
        }
    }
  list = stream;
}

/*****************************************************************************/
/*                     kd_serve::get_codestream_window                       */
/*****************************************************************************/

kd_codestream_window *
  kd_serve::get_codestream_window(int stream_id, kd_window_context *context,
                                  kdu_coords resolution, kdu_dims region,
                                  int round_direction, int max_required_layers,
                                  kdu_range_set &component_ranges,
                                  int num_context_components,
                                  const int *context_component_indices)
{
  kd_stream *stream = get_stream(stream_id,true); // Create if necessary
  if (stream->windows == NULL)
    move_stream_to_list(stream,inactive_streams);
  kd_codestream_window *wp = free_codestream_windows;
  if (wp == NULL)
    wp = new kd_codestream_window(this);
  else
    free_codestream_windows = wp->next;
  wp->initialize(stream,context,resolution,region,round_direction,
                 max_required_layers,component_ranges,num_context_components,
                 context_component_indices);
  assert((wp->context == context) &&
         (stream->windows == wp)); // Above function should have linked us in
  return wp;
}

/*****************************************************************************/
/*                  kd_serve::release_codestream_windows                     */
/*****************************************************************************/

void kd_serve::release_codestream_windows(kd_codestream_window *list)
{
  kd_codestream_window *wp;
  while ((wp = list) != NULL)
    { 
      list = wp->next;
      wp->next = free_codestream_windows;
      free_codestream_windows = wp;
      kd_stream *stream = wp->stream;
      wp->stream = NULL;
      wp->context = NULL;
      wp->num_codestream_components = 0;
      assert(stream->windows != NULL);
      if (wp->stream_prev == NULL)
        { 
          assert(stream->windows == wp);
          stream->windows = wp->stream_next;
        }
      else
        wp->stream_prev->stream_next = wp->stream_next;
      if (wp->stream_next != NULL)
        wp->stream_next->stream_prev = wp->stream_prev;
      wp->stream_next = wp->stream_prev = NULL;
      if (stream->windows == NULL)
        { // Move stream to one of the unused lists
          assert(stream->num_active_contexts == 0); // Must be inactive
          if (stream->tiles == NULL)
            move_stream_to_list(stream,collapsed_unused_streams);
          else if (!stream->interchange)
            move_stream_to_list(stream,lightweight_unused_streams);
          else if (!stream->source)
            move_stream_to_list(stream,mediumweight_unused_streams);
          else
            move_stream_to_list(stream,attached_unused_streams);
        }
    }
}

/*****************************************************************************/
/*                  kd_serve::add_active_context_for_stream                  */
/*****************************************************************************/

void kd_serve::add_active_context_for_stream(kd_stream *stream)
{
  assert(stream->windows != NULL); // Must at least be on inactive list
  if (stream->num_active_contexts == 0)
    move_stream_to_list(stream,active_streams);
  stream->num_active_contexts++;
}

/*****************************************************************************/
/*                kd_serve::remove_active_context_for_stream                 */
/*****************************************************************************/

void kd_serve::remove_active_context_for_stream(kd_stream *stream)
{
  assert(stream->num_active_contexts > 0);
  if ((--stream->num_active_contexts) > 0)
    return;
  
  // Move from active to inactive list
  assert(!stream->is_locked); // Must protect against any exceptions that can
                              // occur within a section in which codestreams
                              // are locked.  
  assert(stream->windows != NULL);
  move_stream_to_list(stream,inactive_streams);
}

/*****************************************************************************/
/*                          kd_serve::trim_resources                         */
/*****************************************************************************/

void kd_serve::trim_resources()
{
  while ((total_detached_interface_mem > KD_MAX_DETACHED_INTERFACE_MEM) &&
         (mediumweight_unused_tail != NULL))
    { 
      kd_stream *scan = mediumweight_unused_tail;
      assert(scan->windows == NULL);
      scan->deconstruct_interfaces(true);
      move_stream_to_list(scan,lightweight_unused_streams);
      assert(scan != mediumweight_unused_tail);
    }
}

/*****************************************************************************/
/*                          kd_serve::attach_stream                          */
/*****************************************************************************/

void kd_serve::attach_stream(kd_stream *stream)
{
  if (stream->source.exists())
    return; // Already attached
  stream->source = target->attach_to_codestream(stream->stream_id,this);
  if (!stream->source)
    { kdu_error e; e << "Server attempt to attach to underlying codestream "
      "has failed -- may be low on resources."; }
  while ((num_attached_streams >= KD_MAX_ATTACHED_CODESTREAMS) &&
         (attached_unused_tail != NULL))
    { // Detach unused attached codestream (from the tail)
      kd_stream *scan = attached_unused_tail;
      assert(scan->source.exists());
      detach_stream(scan);
      assert(scan != attached_unused_tail);
    }
  if (num_attached_streams >= KD_MAX_ATTACHED_CODESTREAMS)
    { // Detach inactive streams (from the head)
      kd_stream *scan;
      for (scan=inactive_streams; scan != NULL; scan=scan->next)
        if (scan->source.exists())
          { 
            detach_stream(scan);
            if (num_attached_streams < KD_MAX_ATTACHED_CODESTREAMS)
              break;
          }
    }
  total_detached_interface_mem -= stream->detached_interface_mem;
  assert(total_detached_interface_mem >= 0);
  num_attached_streams++;
  if (stream->windows == NULL)
    move_stream_to_list(stream,attached_unused_streams);
  if (!(stream->structure.exists() && stream->interchange.exists()))
    { // Create the interchange codestreams now, creating the `tiles' array
      // (and hence the cache model) as we go, if required.
      bool was_collapsed = (stream->tiles == NULL);
      stream->construct_interfaces();
      if (was_collapsed)
        { 
          num_expanded_streams++;
          num_collapsed_streams--;
          assert(num_collapsed_streams >= 0);
        }
    }
  assert(stream->tiles != NULL);
}

/*****************************************************************************/
/*                          kd_serve::detach_stream                          */
/*****************************************************************************/

void kd_serve::detach_stream(kd_stream *stream, bool deleting)
{
  if (!stream->source)
    return;
  assert(!stream->is_locked); // Must protect against any exceptions that can
                              // occur within a section in which codestreams
                              // are locked.
  target->detach_from_codestream(stream->stream_id,this);
  stream->source = kdu_codestream();
  num_attached_streams--;
  kdu_long mem = 0;
  if (stream->interchange.exists())
    mem += (stream->interchange.get_compressed_data_memory() +
            stream->interchange.get_compressed_state_memory());
  if (stream->structure.exists())
    mem += stream->structure.get_compressed_state_memory();
  stream->detached_interface_mem = mem;
  this->total_detached_interface_mem += mem;
  if ((stream->windows == NULL) && !deleting)
    move_stream_to_list(stream,mediumweight_unused_streams);
}

/*****************************************************************************/
/*                        kd_serve::lock_codestreams                         */
/*****************************************************************************/

void kd_serve::lock_codestreams(int num_streams, kd_stream *streams[])
{
  assert((num_locked_stream_indices == 0) &&
         (num_streams <= KD_MAX_ACTIVE_CODESTREAMS));
  int n;
  for (n=0; n < num_streams; n++)
    { 
      if (n == KD_MAX_ACTIVE_CODESTREAMS)
        break; // Just in case -- should never happen
      locked_stream_indices[n] = streams[n]->stream_id;
      assert(!streams[n]->is_locked);
    }
  num_streams = n;
  target->lock_codestreams(num_streams,locked_stream_indices,this);
  num_locked_stream_indices = num_streams;
  for (n=0; n < num_streams; n++)
    streams[n]->is_locked = true;
}

/*****************************************************************************/
/*                       kd_serve::unlock_codestreams                        */
/*****************************************************************************/

void kd_serve::unlock_codestreams(int num_streams, kd_stream *streams[])
{
  assert(num_locked_stream_indices == num_streams);
  int n;
  for (n=0; n < num_streams; n++)
    { 
      if (n == KD_MAX_ACTIVE_CODESTREAMS)
        break; // Just in case -- should never happen
      assert((locked_stream_indices[n] == streams[n]->stream_id) &&
             (streams[n]->is_locked));
      locked_stream_indices[n] = streams[n]->stream_id; // Just in case
    }
  num_streams = n;
  target->release_codestreams(num_streams,locked_stream_indices,this);
  num_locked_stream_indices = 0;
  for (n=0; n < num_streams; n++)
    streams[n]->is_locked = false;
}

/*****************************************************************************/
/*                    kd_serve::adjust_completed_streams                     */
/*****************************************************************************/

void kd_serve::adjust_completed_streams(kd_stream *stream, bool was_complete)
{ 
  assert((stream->completed_tiles >= 0) &&
         (stream->completed_tiles <= stream->total_tiles));
  bool is_complete = stream->check_complete();
  if (was_complete && !is_complete)
    { 
      assert(num_completed_codestreams > 0);
      num_completed_codestreams--;
    }
  else if (is_complete && !was_complete)
    { 
      num_completed_codestreams++;
      assert(num_completed_codestreams <= total_codestreams);
    }
}

/*****************************************************************************/
/*                   kd_serve::adjust_completed_metabins                     */
/*****************************************************************************/

void kd_serve::adjust_completed_metabins(kd_meta *meta, bool was_complete)
{
  if (meta->next_in_bin != NULL)
    return;
  bool is_complete = meta->span_complete && (meta->dispatched_holes == NULL);
  while (is_complete && (meta->prev_in_bin != NULL))
    { 
      meta = meta->prev_in_bin;
      if ((meta->dispatched_holes != NULL) ||
          (meta->dispatched_span < meta->num_bytes))
        is_complete = false;
    }
  if (was_complete && !is_complete)
    { 
      assert(num_completed_metabins > 0);
      num_completed_metabins--;
    }
  else if (is_complete && !was_complete)
    { 
      num_completed_metabins++;
      assert(num_completed_metabins <= total_metabins);
    }
}

/*****************************************************************************/
/*                         kd_serve::find_metabin                            */
/*****************************************************************************/

kd_meta *kd_serve::find_metabin(kdu_long bin_id, kd_meta *root)
{
  if (root == NULL)
    root = this->metatree;
  if (root->bin_id == bin_id)
    return root;
  kd_meta *scan, *possible_phld=NULL;
  for (scan=root; scan != NULL; scan=scan->next_in_bin)
    if (scan->phld != NULL)
      { 
        if (scan->phld->bin_id <= bin_id)
          possible_phld = scan->phld;
        else if (possible_phld != NULL)
          return find_metabin(bin_id,possible_phld);
        else
          return NULL;
      }
  return NULL;
}

/*****************************************************************************/
/*               kd_serve::process_metabin_model_instructions                */
/*****************************************************************************/

void kd_serve::process_metabin_model_instructions(kd_meta *meta, int i_buf[],
                                                  int i_cnt)
{
  int val;
  assert(i_cnt <= 2);
  bool additive = ((i_cnt > 0) && (i_buf[0] != 0));
  if (!(additive || meta->is_modeled))
    return; // Cache model is empty and nothing to add

  while (meta->prev_in_bin != NULL)
    meta = meta->prev_in_bin; // Make `meta' point to start of bin
  kd_meta *mp, *last_mp=NULL;
  kdu_byte modeled = meta->is_modeled;
  bool was_complete = true; // Until proven otherwise
  for (mp=meta; mp != NULL; last_mp=mp, mp=mp->next_in_bin)
    { 
      assert(modeled == mp->is_modeled);
      mp->is_modeled = 1;
      if ((mp->dispatched_holes != NULL) ||
          (mp->dispatched_span < mp->num_bytes))
        was_complete = false;
    }
  if (!modeled)
    {
      last_mp->next_modeled = modeled_metabins;
      modeled_metabins = last_mp;
    }
  if (!last_mp->span_complete)
    was_complete = false;
  if (additive)
    { // Process additive instruction
      if ((val = i_buf[0]) < 0)
        { // Meta data-bin fully cached
          for (mp=meta; mp != NULL; mp=mp->next_in_bin)
            { 
              mp->dispatched_span = mp->sim_span = mp->num_bytes;
              if (mp->dispatched_holes != NULL)
                { 
                  hole_server->release_holes(mp->dispatched_holes);
                  mp->dispatched_holes = NULL;
                }
            }
          last_mp->span_complete = last_mp->sim_span_complete = 1;
        }
      else
        { // Meta data-bin has at least `val' bytes
          for (mp=meta; (mp != NULL) && (val > 0); mp=mp->next_in_bin)
            { 
              int min_bytes=(val < mp->num_bytes)?val:mp->num_bytes;
              if (min_bytes > mp->dispatched_span)
                mp->dispatched_span = min_bytes;
              if (min_bytes > mp->sim_span)
                mp->sim_span = min_bytes;
              if (mp->dispatched_holes != NULL)
                mp->dispatched_holes =
                  hole_server->trim_for_fill_bytes(mp->dispatched_holes,
                                                   min_bytes);
              val -= mp->num_bytes;
            }
          last_mp->span_complete = last_mp->sim_span_complete = 0; // Safest
        }
    }
  if ((i_cnt > 1) && ((val=i_buf[1]) > 0))
    { // Meta data-bin has less than `val' bytes
      val--; // Convert to inclusive upper bound
      for (mp=meta; mp != NULL; mp=mp->next_in_bin)
        { 
          if (val <= 0)
            { 
              mp->dispatched_span = mp->sim_span = 0;
              mp->span_complete = mp->sim_span_complete = 0;
              if (mp->dispatched_holes != NULL)
                { 
                  hole_server->release_holes(mp->dispatched_holes);
                  mp->dispatched_holes = NULL;
                }
            }
          else if (val < mp->dispatched_span)
            { 
              mp->dispatched_span = val;
              if (val < mp->sim_span)
                mp->sim_span = val;
              mp->span_complete = mp->sim_span_complete = 0;
              if (mp->dispatched_holes != NULL)
                mp->dispatched_holes =
                  hole_server->trim_for_max_bytes(mp->dispatched_holes,val);
            }
          val -= mp->num_bytes;
        }
    }
  
  adjust_completed_metabins(last_mp,was_complete);
}

/*****************************************************************************/
/*                    kd_serve::erase_metadata_cache_model                   */
/*****************************************************************************/

void kd_serve::erase_metadata_cache_model()
{
  kd_meta *scan;
  while ((scan=modeled_metabins) != NULL)
    {
      modeled_metabins = scan->next_modeled;
      assert(scan->is_modeled && (scan->next_in_bin == NULL));
      bool was_complete = (scan->check_complete() != NULL);
      kd_meta *mp;
      for (mp=scan; mp != NULL; mp=mp->prev_in_bin)
        { 
          mp->is_modeled = 0;
          mp->span_complete = mp->sim_span_complete = 0;
          mp->dispatched_span = mp->sim_span = 0;
          if (mp->dispatched_holes != NULL)
            { // Should not usually be required, since we don't get holes in
              // the stateless mode, where we expect to use this function
              hole_server->release_holes(mp->dispatched_holes);
              mp->dispatched_holes = NULL;
            }
        }
      adjust_completed_metabins(scan,was_complete);
    }
}


/* ========================================================================= */
/*                                 kdu_serve                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                           kdu_serve::initialize                           */
/*****************************************************************************/

void
  kdu_serve::initialize(kdu_serve_target *target, int max_chunk_size,
                        int chunk_prefix_bytes, bool ignore_relevance_info,
                        kds_id_encoder *custom_id_encoder)
{
  if (state != NULL)
    { kdu_error e; e << "Attempting to initialize a \"kdu_serve\" object "
      "which has already been initialized and has not yet been destroyed."; }
  state = new kd_serve(this);
  state->initialize(target,max_chunk_size,chunk_prefix_bytes,
                    ignore_relevance_info,custom_id_encoder);
}

/*****************************************************************************/
/*                             kdu_serve::destroy                            */
/*****************************************************************************/

void kdu_serve::destroy()
{
  if (state != NULL)
    delete state;
  state = NULL;
}

/*****************************************************************************/
/*                           kdu_serve::set_window                           */
/*****************************************************************************/

void
  kdu_serve::set_window(const kdu_window &window,
                        const kdu_window_prefs *pref_updates,
                        const kdu_window_model *model_instructions,
                        bool is_stateless, int context_id)
{
  if (!(state->is_stateless || state->is_stateful))
    { // First ever call to `set_window'
      state->is_stateless = is_stateless;
      state->is_stateful = !is_stateless;
    }
  if ((state->is_stateless != is_stateless) ||
      (is_stateless && (context_id != 0)))
    { kdu_error e; e << "Illegal call to `kdu_serve::set_window'.  You "
      "may not mix stateless window-of-interest requests with stateful "
      "(session-based) requests.  Either all calls must be for stateless "
      "processing, with a window context of 0, or all calls must be for "
      "session-based processing -- in that case, you may use multiple "
      "window contexts.";
    }
  kd_window_context *cp, *last_cp=NULL;
  for (cp=state->contexts; cp != NULL; last_cp=cp, cp=cp->next)
    if (cp->context_id == context_id)
      break;
  if (cp == NULL)
    { // Create a new window context
      cp = new kd_window_context(state,context_id);
      if (last_cp == NULL)
        state->contexts = cp;
      else
        last_cp->next = cp;
      cp->next = NULL;
    }

  // Manage cache model instructions
  bool have_new_model_instructions = false;
  if (is_stateless)
    cp->model_instructions.clear();
  if (model_instructions != NULL)
    { 
      if (is_stateless != model_instructions->is_stateless())
        { kdu_error e; e << "Either the `model_instructions' supplied in a "
          "call to `kdu_serve::set_window' have been prepared for a stateless "
          "request, yet the call to `kdu_serve::set_window' itself specifies "
          "stateful (session-based) processing, or else the "
          "`model_instructions' are for stateful processing and the call "
          "specifies stateless processing.  You may not inter-mix stateless "
          "and stateful window-of-interest processing.";
        }
      have_new_model_instructions = !model_instructions->is_empty();
      if (is_stateless)
        cp->model_instructions.copy_from(*model_instructions);
      else
        cp->model_instructions.append(*model_instructions);
    }
  if (cp->model_instructions.is_empty())
    cp->current_model_instructions = NULL;
  else
    cp->current_model_instructions = &(cp->model_instructions);
  
  // Manage preferences
  bool have_new_prefs = false;
  if (is_stateless)
    { cp->window_prefs.init(); have_new_prefs=true; }
  if ((pref_updates != NULL) &&
      (cp->window_prefs.update(*pref_updates) != 0))
    have_new_prefs = true;
  
  // Now see if we need to change the window of interest
  bool imagery_change = (is_stateless || have_new_model_instructions ||
                         have_new_prefs || cp->window_imagery_changed ||
                         !(cp->last_translated_window.imagery_equals(window) ||
                           cp->current_window.imagery_equals(window)));
  bool window_change = (imagery_change || cp->window_changed ||
                        !(cp->last_translated_window.equals(window) ||
                          cp->current_window.equals(window)));
  if (!window_change)
    return;

  cp->window_changed = true;
  if (!imagery_change)
    { // We only want `cp->process_window_changes' to update the metadata-bins
      cp->current_window.copy_metareq_from(window);
        // Note that `cp->process_window_changes' will not parse the imagery
        // aspects of the new window, so it will not translate or modify any
        // codestream contexts (normally required).  For this reason, we have
        // to be careful to keep the results from previous modifications, so
        // we only copy the metadata aspects of the new `window'.
    }
  else
    {
      cp->current_window.copy_from(window);
      cp->window_imagery_changed = true;
    }
}

/*****************************************************************************/
/*                           kdu_serve::get_window                           */
/*****************************************************************************/

bool
  kdu_serve::get_window(kdu_window &window, int context_id) const
{
  if (state == NULL)
    {
      window.init();
      return false;
    }
  kd_window_context *cp;
  for (cp=state->contexts; cp != NULL; cp=cp->next)
    if (cp->context_id == context_id)
      break;
  if (cp == NULL)
    { 
      window.init();
      return false;
    }
  
  window.copy_from(cp->current_window,true);
  return ((cp->active_bins != NULL) || (cp->sweep_next != NULL) ||
          (cp->meta_sweep_next != NULL) || cp->window_changed ||
          (cp->new_stream_windows != NULL));
}

/*****************************************************************************/
/*                       kdu_serve::generate_increments                      */
/*****************************************************************************/

kds_chunk *
  kdu_serve::generate_increments(int suggested_message_bytes, 
                                 int &max_message_bytes, bool align,
                                 bool use_extended_message_headers,
                                 bool decouple_chunks, int context_id,
                                 int suggested_chunk_size)
{
  if (state == NULL)
    {
      assert(0);
      return NULL;
    }
  kd_window_context *cp;
  for (cp=state->contexts; cp != NULL; cp=cp->next)
    if (cp->context_id == context_id)
      break;
  if (cp == NULL)
    return NULL;
  kd_window_context *ctxt = cp;
  if (ctxt->window_changed)
    { 
      ctxt->process_window_changes();
      for (cp=state->contexts; cp != NULL; cp=cp->next)
        if ((cp != ctxt) && (cp->current_model_instructions != NULL))
          cp->process_outstanding_model_instructions(NULL); // sync cache model
    }
  int suggested_chunk_body_bytes = state->max_chunk_body_bytes;
  if (suggested_chunk_size > 0)
    { 
      suggested_chunk_body_bytes =
        suggested_chunk_size - state->chunk_prefix_bytes;
      if (suggested_chunk_body_bytes < 24)
        suggested_chunk_body_bytes = 24; // A rather arbitrary lower limit
      if (suggested_chunk_body_bytes > state->max_chunk_body_bytes)
        suggested_chunk_body_bytes = state->max_chunk_body_bytes;
    }
  return ctxt->generate_increments(suggested_message_bytes,max_message_bytes,
                                   align,use_extended_message_headers,
                                   decouple_chunks,suggested_chunk_body_bytes);
}

/*****************************************************************************/
/*                         kdu_serve::window_finished                        */
/*****************************************************************************/

void kdu_serve::window_finished(int context_id)
{
  if (state == NULL)
    { 
      assert(0);
      return;
    }
  kd_window_context *cp;
  for (cp=state->contexts; cp != NULL; cp=cp->next)
    if (cp->context_id == context_id)
      break;
  if (cp == NULL)
    return;
  if (cp->stream_windows != NULL)
    { 
      if (!state->is_stateless)
        cp->process_outstanding_model_instructions(NULL);
      cp->current_model_instructions = NULL;
      state->release_codestream_windows(cp->stream_windows);
      cp->stream_windows = NULL;
      cp->num_streams = 0;
    }
}

/*****************************************************************************/
/*                          kdu_serve::get_image_done                        */
/*****************************************************************************/

bool kdu_serve::get_image_done() const
{
  if (state == NULL)
    return false;
  if (state->is_stateless)
    return state->stateless_image_done;
  else
    return ((state->num_completed_codestreams == state->total_codestreams) &&
            state->check_metadata_complete());
}

/*****************************************************************************/
/*                          kdu_serve::push_extra_data                       */
/*****************************************************************************/

int
  kdu_serve::push_extra_data(kdu_byte *data, int num_bytes,
                             kds_chunk *chunk_list, int context_id)
{
  if (state == NULL)
    { 
      assert(0);
      return 0;
    }
  kd_window_context *cp=NULL;
  
  kds_chunk *scan;
  if (chunk_list != NULL)
    for (scan=chunk_list; scan->next != NULL; scan=scan->next);
  else
    { 
      kd_window_context *last_cp=NULL;
      for (cp=state->contexts; cp != NULL; last_cp=cp, cp=cp->next)
        if (cp->context_id == context_id)
          break;
      if (cp == NULL)
        { // Create a new window context
          cp = new kd_window_context(state,context_id);
          if (last_cp == NULL)
            state->contexts = cp;
          else
            last_cp->next = cp;
          cp->next = NULL;
        }
      scan = cp->extra_data_tail;
    }
  
  if ((data != NULL) && (num_bytes != 0))
    {
      if (scan == NULL)
        { 
          assert(cp != NULL);
          scan = cp->extra_data_head = state->chunk_server->get_chunk();
        }
      else if (scan->max_bytes < (scan->num_bytes + num_bytes))
        scan = scan->next = state->chunk_server->get_chunk();
      if (chunk_list == NULL)
        {
          cp->extra_data_tail = scan;
          cp->extra_data_bytes += num_bytes;
        }
      if (scan->max_bytes < (scan->num_bytes + num_bytes))
        { kdu_error e; e << "Attempting to push too much data in a single "
          "call to `kdu_serve::push_extra_data'.  You should be more "
          "careful to push the data incrementally."; }
      memcpy(scan->data+scan->num_bytes,data,(size_t) num_bytes);
      scan->num_bytes += num_bytes;
    }
  return ((scan==NULL)?0:(scan->max_bytes-scan->num_bytes));
}

/*****************************************************************************/
/*                   kdu_serve::retrieve_extra_data_chunks                   */
/*****************************************************************************/

kds_chunk *
  kdu_serve::retrieve_extra_data_chunks(int context_id)
{
  if (state == NULL)
    {
      assert(0);
      return NULL;
    }
  kd_window_context *cp;
  for (cp=state->contexts; cp != NULL; cp=cp->next)
    if (cp->context_id == context_id)
      break;
  if (cp == NULL)
    return NULL;
  kds_chunk *result = cp->extra_data_head;
  cp->extra_data_head = cp->extra_data_tail = NULL;
  return result;
}

/*****************************************************************************/
/*                          kdu_serve::release_chunks                        */
/*****************************************************************************/

void
  kdu_serve::release_chunks(kds_chunk *chunks, bool check_abandoned)
{
  if (state == NULL)
    { // Must have been already destroyed -- should not happen
      assert(0);
      return;
    }
  kd_model_hole_server *hole_server = state->hole_server;
  
  kds_chunk *chk;
  while ((chk=chunks) != NULL)
    { 
      chunks = chk->next;
      bool have_abandoned_meta_binrefs = false;
      kd_chunk_binref *ref;
      while ((ref = chk->bins) != NULL)
        { 
          chk->bins = ref->next;
          kd_stream *str = ref->stream;
          if (str != NULL)
            { 
              assert(str->num_chunk_binrefs > 0);
              str->num_chunk_binrefs--;
            }
          if (state->is_stateless || (ref->num_bytes <= 0) ||
              !(check_abandoned && chk->abandoned))
            continue;

          // If we get here, we have an abandoned chunk binref
          int range_start = ref->prev_bytes;
          int range_lim = range_start + ref->num_bytes;
          if (str == NULL)
            { // Reference is to a metadata-bin
              kd_meta *meta = ref->meta;
              assert(meta != NULL);
              have_abandoned_meta_binrefs = true;
              kd_meta *last_in_complete_bin = meta->check_complete();
              if ((range_lim <= 0xFFFF) &&
                  ((range_lim < meta->dispatched_span) ||
                   ((meta->next_in_bin != NULL) &&
                    (meta->next_in_bin->dispatched_span > 0))))
                { // We have a non-terminal hole
                  meta->dispatched_holes =
                    hole_server->add_hole(meta->dispatched_holes,
                                          (kdu_uint16) range_start,
                                          (kdu_uint16) range_lim);
                }
              else
                { // Abandoned everything following `range_start'
                  for (; meta != NULL; range_start=0, meta=meta->next_in_bin)
                    { 
                      meta->span_complete = meta->sim_span_complete = 0;
                      if (range_start < meta->dispatched_span)
                        meta->dispatched_span = range_start;
                      if (range_start < meta->sim_span)
                        meta->sim_span = range_start;
                      if (meta->dispatched_holes != NULL)
                        meta->dispatched_holes =
                        hole_server->trim_for_max_bytes(meta->dispatched_holes,
                                                        range_start);
                    }
                }
              if (last_in_complete_bin != NULL)
                state->adjust_completed_metabins(last_in_complete_bin,true);
              continue;
            }

          kd_codestream_window *wp;
          for (wp=str->windows; wp != NULL; wp=wp->next)
            { // Scan through the codestream windows which could be
              // affected by the abandoned codestream data increment
              wp->fully_dispatched = false;
              wp->content_incomplete = true; // Have to assume this
              if (wp->context->sweep_next == NULL)
                wp->context->sweep_next = wp; // Force sweep to start
                                              // again from here
            }

          if (ref->tnum < 0)
            { // Reference is to a main codestream header data-bin
              bool was_complete = str->check_complete();
              if ((range_lim <= 0xFFFF) &&
                  (range_lim < str->dispatched_header_span))
                { // We have a non-terminal hole
                  str->dispatched_header_holes =
                    hole_server->add_hole(str->dispatched_header_holes,
                                          (kdu_uint16) range_start,
                                          (kdu_uint16) range_lim);
                }
              else
                { // Abandon everything following `range_start'
                  str->header_span_complete = 0;
                  str->sim_header_span_complete = 0;
                  if (range_start < str->dispatched_header_span)
                    str->dispatched_header_span = range_start;
                  if (range_start < str->sim_header_span)
                    str->sim_header_span = range_start;
                  if (str->dispatched_header_holes != NULL)
                    str->dispatched_header_holes =
                      hole_server->trim_for_max_bytes(
                                   str->dispatched_header_holes,range_start);
                }
              if (was_complete)
                state->adjust_completed_streams(str,was_complete);
              continue;
            }
              
          if ((ref->tnum >= str->total_tiles) || (str->tiles == NULL))
            continue; // No cache model for this tile anymore
                
          kd_tile *tp = str->tiles + ref->tnum;
          bool tp_was_complete = tp->check_complete();
          if (ref->cnum < 0)
            { // Reference is to a tile header data-bin
              if ((range_lim <= 0xFFFF ) &&
                  (range_lim < tp->dispatched_header_span))
                { // We have a non-terminal hole
                  tp->dispatched_header_holes =
                    hole_server->add_hole(tp->dispatched_header_holes,
                                          (kdu_uint16) range_start,
                                          (kdu_uint16) range_lim);
                }
              else
                { // Abandon everything following `range_start'
                  tp->header_span_complete = 0;
                  tp->sim_header_span_complete = 0;
                  if (range_start < tp->dispatched_header_span)
                    tp->dispatched_header_span = range_start;
                  if (range_start < tp->sim_header_span)
                    tp->sim_header_span = range_start;
                  if (tp->dispatched_header_holes != NULL)
                    tp->dispatched_header_holes =
                      hole_server->trim_for_max_bytes(
                                   tp->dispatched_header_holes,range_start);
                }
              if (tp_was_complete)
                str->adjust_completed_tiles(tp,tp_was_complete);
              continue;
            }
          
          if (str->num_components <= (int) ref->cnum)
            continue;
          kd_tile_comp *comp = tp->comps + ref->cnum;
          if ((ref->rnum < 0) || (comp->num_resolutions <= (int) ref->rnum))
            continue;
          kd_resolution *rp = comp->res + ref->rnum;
          if (!rp->is_modeled)
            continue;
          kd_precinct_model *prec=rp->access_precinct(ref->p_idx,false);
          if (prec == NULL)
            continue;
          
          int span = prec->get_span();
          if (span <= 0)
            continue; // If the precinct's cache model involves only a known
                      // number of packets, something must have happened to
                      // change the representation since it was last activated
                      // (the abandoned data chunk must have been generated
                      // from a `kd_active_precinct' with a known number of
                      // bytes).  So the only reasonable conclusion is that the
                      // client has since altered the cache model explicitly.
                      // In particular, the client must have added and
                      // subtracted from the cache model or declared it to
                      // be complete; otherwise we would still have the number
                      // of bytes recorded there from when `kd_active_precinct'
                      // was last available.  In any of these cases, we do not
                      // need to worry about the fact that the previously
                      // generated data increment was not received, so we
                      // can ignore it.
          bool was_complete = prec->check_complete();
          if ((range_lim <= 0xFFFF) && (range_lim < span))
            { // We have a non-terminal hole
              prec->add_hole((kdu_uint16) range_start,(kdu_uint16) range_lim,
                             hole_server);
            }
          else
            { // Abandon everything following `range_start'
              prec->set_max_bytes(range_start,hole_server);
              assert(!prec->check_complete()); // Above call always marks us
                                               // as incomplete
            }
          if (was_complete)
            { 
              assert(tp->completed_precincts > 0);
              tp->completed_precincts--;
              if (tp_was_complete)
                str->adjust_completed_tiles(tp,tp_was_complete);
            }
        }
      
      if (have_abandoned_meta_binrefs)
        { 
          kd_window_context *cp;
          for (cp=state->contexts; cp != NULL; cp=cp->next)
            cp->meta_sweep_next = cp->stream_windows;
        }
      state->binref_server->release_chunk_binrefs(chk->bins);
      state->chunk_server->release_chunk(chk);
    }
}
