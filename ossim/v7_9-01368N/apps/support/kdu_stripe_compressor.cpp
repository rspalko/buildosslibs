/*****************************************************************************/
// File: kdu_stripe_compressor.cpp [scope = APPS/SUPPORT]
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
   Implements the `kdu_stripe_compressor' object.
******************************************************************************/

#include <assert.h>
#include <math.h>
#include "kdu_messaging.h"
#include "stripe_compressor_local.h"
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
     kdu_error _name("E(kdu_stripe_compressor.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_stripe_compressor.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu Stripe Compressor:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu Stripe Compressor:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* INLINE                       transfer_bytes                               */
/*****************************************************************************/

static inline void
  transfer_bytes(kdu_line_buf &dest, kdu_byte *src, int num_samples,
                 int sample_gap, int src_bits, int original_bits)
{
  if (dest.get_buf16() != NULL)
    {
      kdu_sample16 *dp = dest.get_buf16();
      kdu_int16 off = ((kdu_int16)(1<<src_bits))>>1;
      kdu_int16 mask = ~((kdu_int16)((-1)<<src_bits));
      if (!dest.is_absolute())
        {
          int shift = KDU_FIX_POINT - src_bits; assert(shift >= 0);
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = ((((kdu_int16) *src) & mask) - off) << shift;
        }
      else if (src_bits < original_bits)
        { // Reversible processing; source buffer has too few bits
          int shift = original_bits - src_bits;
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = ((((kdu_int16) *src) & mask) - off) << shift;
        }
      else if (src_bits > original_bits)
        { // Reversible processing; source buffer has too many bits
          int shift = src_bits - original_bits;
          off -= (1<<shift)>>1; // For rounded down-shifting
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = ((((kdu_int16) *src) & mask) - off) >> shift;
        }
      else
        { // Reversible processing, `src_bits'=`original_bits'
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = (((kdu_int16) *src) & mask) - off;
        }
    }
  else
    {
      kdu_sample32 *dp = dest.get_buf32();
      kdu_int32 off = ((kdu_int32)(1<<src_bits))>>1;
      kdu_int32 mask = ~((kdu_int32)((-1)<<src_bits));
      if (!dest.is_absolute())
        {
          float scale = 1.0F / (float)(1<<src_bits);
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->fval = scale * (float)((((kdu_int32) *src) & mask) - off);
        }
      else if (src_bits < original_bits)
        { // Reversible processing; source buffer has too few bits
          int shift = original_bits - src_bits;
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = ((((kdu_int32) *src) & mask) - off) << shift;
        }
      else if (src_bits > original_bits)
        { // Reversible processing; source buffer has too many bits
          int shift = src_bits - original_bits;
          off -= (1<<shift)>>1; // For rounded down-shifting
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = ((((kdu_int32) *src) & mask) - off) >> shift;
        }
      else
        { // Reversible processing, `src_bits'=`original_bits'
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = (((kdu_int32) *src) & mask) - off;
        }
    }
}

/*****************************************************************************/
/* INLINE                       transfer_words                               */
/*****************************************************************************/

static inline void
  transfer_words(kdu_line_buf &dest, kdu_int16 *src, int num_samples,
                 int sample_gap, int src_bits, int original_bits,
                 bool is_signed)
{
  if (dest.get_buf16() != NULL)
    {
      kdu_sample16 *dp = dest.get_buf16();
      int upshift = 16-src_bits; assert(upshift >= 0);
      if (!dest.is_absolute())
        {
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((kdu_int16)((*src)<<upshift)) >> (16-KDU_FIX_POINT);
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((kdu_int16)(((*src) << upshift) - (1<<15))) >>
                         (16-KDU_FIX_POINT);
        }
      else
        { // Reversible processing
          int downshift = 16-original_bits; assert(downshift >= 0);
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((kdu_int16)((*src) << upshift)) >> downshift;
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((kdu_int16)(((*src) << upshift) - (1<<15))) >>
                         downshift;
        }
    }
  else
    {
      kdu_sample32 *dp = dest.get_buf32();
      int upshift = 32-src_bits; assert(upshift >= 0);
      if (!dest.is_absolute())
        {
          float scale = 1.0F / (((float)(1<<16)) * ((float)(1<<16)));
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->fval = scale * (float)(((kdu_int32) *src)<<upshift);
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->fval = scale*(float)((((kdu_int32) *src)<<upshift)-(1<<31));
        }
      else
        {
          int downshift = 32-original_bits; assert(downshift >= 0);
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (((kdu_int32) *src)<<upshift) >> downshift;
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((((kdu_int32) *src)<<upshift)-(1<<31)) >> downshift;
        }
    }
}

/*****************************************************************************/
/* INLINE                      transfer_dwords                               */
/*****************************************************************************/

static inline void
  transfer_dwords(kdu_line_buf &dest, kdu_int32 *src, int num_samples,
                  int sample_gap, int src_bits, int original_bits,
                  bool is_signed)
{
  if (dest.get_buf16() != NULL)
    {
      kdu_sample16 *dp = dest.get_buf16();
      int upshift = 32-src_bits; assert(upshift >= 0);
      if (!dest.is_absolute())
        {
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (kdu_int16)
                (((*src) << upshift) >> (32-KDU_FIX_POINT));
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (kdu_int16)
                ((((*src) << upshift) - (1<<31)) >> (32-KDU_FIX_POINT));
        }
      else
        { // Reversible processing
          int downshift = 32-original_bits; assert(downshift >= 0);
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (kdu_int16)
                (((*src) << upshift) >> downshift);
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (kdu_int16)
                ((((*src) << upshift) - (1<<31)) >> downshift);
        }
    }
  else
    {
      kdu_sample32 *dp = dest.get_buf32();
      int upshift = 32-src_bits; assert(upshift >= 0);
      if (!dest.is_absolute())
        {
          float scale = 1.0F / (((float)(1<<16)) * ((float)(1<<16)));
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->fval = scale * (float)((*src)<<upshift);
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->fval = scale * (float)(((*src)<<upshift)-(1<<31));
        }
      else
        {
          int downshift = 32-original_bits; assert(downshift >= 0);
          if (is_signed)
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = ((*src)<<upshift) >> downshift;
          else
            for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
              dp->ival = (((*src)<<upshift)-(1<<31)) >> downshift;
        }
    }
}

/*****************************************************************************/
/* INLINE                      transfer_floats                               */
/*****************************************************************************/

static inline void
  transfer_floats(kdu_line_buf &dest, float *src, int num_samples,
                  int sample_gap, int src_bits, int original_bits,
                  bool is_signed)
{
  float src_scale = 1.0F; // Amount required to scale src to unit dynamic range
  while (src_bits < -16)
    { src_bits += 16;  src_scale *= (float)(1<<16); }
  while (src_bits > 0)
    { src_bits -= 16;  src_scale *= 1.0F / (float)(1<<16); }
  src_scale *= (float)(1<<(-src_bits));

  int dst_bits = (dest.is_absolute())?original_bits:KDU_FIX_POINT;
  float dst_scale = 1.0F; // Amount to scale from unit range to dst
  while (dst_bits > 16)
    { dst_bits -= 16;  dst_scale *= (float)(1<<16); }
  dst_scale *= (float)(1<<dst_bits);

  if (dest.get_buf16() != NULL)
    {
      float scale = dst_scale * src_scale;
      float offset = 0.5F - ((!is_signed)?(0.5F*dst_scale):0.0F);
      kdu_sample16 *dp = dest.get_buf16();
      for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
        dp->ival = (kdu_int16) floor(((*src) * scale) + offset);
    }
  else
    {
      kdu_sample32 *dp = dest.get_buf32();
      if (dest.is_absolute())
        {
          float scale = dst_scale * src_scale;
          float offset = 0.5F - ((!is_signed)?(0.5F*dst_scale):0.0F);
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->ival = (kdu_int32) floor(((*src) * scale) + offset);
        }
      else
        {
          float offset = (!is_signed)?(-0.5F):0.0F;
          for (; num_samples > 0; num_samples--, src+=sample_gap, dp++)
            dp->fval = ((*src) * src_scale) + offset;
        }
    }
}


/* ========================================================================= */
/*                           kdsc_component_state                            */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdsc_component_state::update                        */
/*****************************************************************************/

void
  kdsc_component_state::update(kdu_coords next_tile_idx,
                               kdu_codestream codestream)
{
  int increment = stripe_height;
  if (increment > remaining_tile_height)
    increment = remaining_tile_height;
  stripe_height -= increment;
  remaining_tile_height -= increment;
  int adj = increment*row_gap;
  int log2_bps = buf_type & 3; // 2 LSB's hold log_2(bytes/sample)
  assert(log2_bps <= 2);
  buf_ptr += adj << log2_bps;
  if (remaining_tile_height > 0)
    return;
  remaining_tile_height = next_tile_height;
  next_tile_height = 0;
  remaining_tile_rows--;
  if (remaining_tile_rows == 0)
    return;
  if (remaining_tile_rows > 1)
    { // Find new value for `next_tile_height'
      next_tile_idx.y++;
      kdu_dims dims;
      codestream.get_tile_dims(next_tile_idx,comp_idx,dims,true);
      next_tile_height = dims.size.y;
    }
}


/* ========================================================================= */
/*                                kdsc_tile                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                           kdsc_tile::configure                            */
/*****************************************************************************/

void
  kdsc_tile::configure(int num_comps, const kdsc_component_state *comp_states)
{
  if ((num_comps != this->num_components) || (components == NULL))
    { // Allocate/reallocate the `components' array
      if (components != NULL)
        { delete[] components; components = NULL; }
      this->num_components = num_comps;
      this->components = new kdsc_component[num_comps];
    }
  memset(components,0,sizeof(kdsc_component)*(size_t)num_comps);
  int c, min_subsampling=1;
  for (c=0; c < num_comps; c++)
    { 
      components[c].original_precision = comp_states[c].original_precision;
      kdu_coords subs = comp_states[c].sub_sampling;
      components[c].vert_subsampling = subs.y;
      if ((c == 0) || (subs.y < min_subsampling))
        min_subsampling = subs.y;
    }
  for (c=0; c < num_comps; c++)
    components[c].count_delta = min_subsampling;
}

/*****************************************************************************/
/*                             kdsc_tile::init                               */
/*****************************************************************************/

void
  kdsc_tile::init(kdu_coords idx, kdu_codestream codestream,
                  kdsc_component_state *comp_states, bool force_precise,
                  bool want_fastest, kdu_thread_env *env,
                  int env_dbuf_height, kdsc_queue *env_queue,
                  const kdu_push_pull_params *pp_params, int tiles_wide)
{
  int c;
  if (!tile.exists())
    { 
      if (env == NULL)
        tile = codestream.open_tile(idx,env);
      else
        { // `idx' should already have been passed to `codestream.open_tiles'
          // for background tile opening, so all we have to do is to access
          // the (hopefully) already open tile, waiting (if necessary) for
          // the background open operation to complete.
          tile = codestream.access_tile(idx,true,env);
          if (!tile)
            { KDU_ERROR_DEV(e,0x28041401); e <<
              KDU_TXT("Attempt to open tile via `kdu_codestream::access_tile' "
                      "has failed, even though the call involved a blocking "
                      "wait.  Something seems to have gone wrong internally.");
            }
        }
      
      tile.set_components_of_interest(num_components);
      assert(this->queue == NULL);
      kdu_thread_queue *thread_queue = NULL;
      if (env != NULL)
        { 
          assert(env_queue != NULL);
          this->queue = env_queue;
          if (env_queue->first_tile == NULL)
            env_queue->first_tile = this;
          env_queue->last_tile = this;
          env_queue->num_tiles++;
          thread_queue = &(env_queue->thread_queue);
        }
      bool double_buffering = (env != NULL) && (env_dbuf_height != 0);
      if (double_buffering && (env_dbuf_height < 0) && (tiles_wide > 1))
        { // See if we should be automatically selecting the `env_dbuf_height'
          // so that the tile engine is able to buffer up all samples in the
          // tile at the front end -- this is a good idea when the stripes
          // being pushed in are large enough to allow tiles to be processed
          // one by one and there are multiple horizontally adjacent tiles.
          int max_remaining_tile_height = -2;
          for (c=0; c < num_components; c++)
            { 
              kdsc_component_state *cs = comp_states + c;
              if (cs->remaining_tile_height > cs->stripe_height)
                break; // Pushed stripes not large enough
              if (cs->remaining_tile_height > max_remaining_tile_height)
                max_remaining_tile_height = cs->remaining_tile_height;
            }
          if (c == num_components)
            env_dbuf_height = (max_remaining_tile_height+1) >> 1;
        }
      
      int flags = KDU_MULTI_XFORM_DEFAULT_FLAGS;
      if (force_precise)
        flags |= KDU_MULTI_XFORM_PRECISE;
      if (want_fastest)
        flags |= KDU_MULTI_XFORM_FAST;
      if (double_buffering)
        flags |= KDU_MULTI_XFORM_MT_DWT;
      else
        env_dbuf_height = 1;
      engine.create(codestream,tile,env,thread_queue,flags,NULL,
                    env_dbuf_height,&sample_allocator,pp_params);
      for (c=0; c < num_components; c++)
        { 
          kdsc_component *comp = components + c;
          kdsc_component_state *cs = comp_states + c;
          comp->size = engine.get_size(c);
          comp->using_shorts = !engine.is_line_precise(c);
          comp->is_absolute = engine.is_line_absolute(c);
          kdu_dims dims;  codestream.get_tile_dims(idx,c,dims,true);
          comp->horizontal_offset = dims.pos.x - cs->pos_x;
          assert((comp->size == dims.size) && (comp->horizontal_offset >= 0));
          comp->ratio_counter = 0;
          comp->stripe_rows_left = 0;
        }
    }
  else
    assert(env_queue == this->queue);
  
  // Now go through the components, assigning buffers and counters
  for (c=0; c < num_components; c++)
    { 
      kdsc_component *comp = components + c;
      kdsc_component_state *cs = comp_states + c;
      assert(comp->stripe_rows_left == 0);
      assert(cs->remaining_tile_height == comp->size.y);
      comp->stripe_rows_left = cs->stripe_height;
      if (comp->stripe_rows_left > comp->size.y)
        comp->stripe_rows_left = comp->size.y;
      comp->sample_gap = cs->sample_gap;
      comp->row_gap = cs->row_gap;
      comp->precision = cs->precision;
      comp->is_signed = cs->is_signed;
      comp->buf_type = cs->buf_type;
      comp->buf_ptr = cs->buf_ptr;
      comp->line = NULL;
      int adj = comp->horizontal_offset * comp->sample_gap;
      int log2_bps = comp->buf_type & 3; // 2 LSB's hold log_2(bytes/sample)
      assert(log2_bps <= 2);
      comp->buf_ptr += adj << log2_bps;
    }
  
#ifdef KDU_SIMD_OPTIMIZATIONS
  // Finally, let's see if there are fast transfer functions that can be
  // used for the current configuration.  This may depend upon interleaving
  // patterns.
  kdu_check_sample_alignment();
  int ilv_count=0; // Num elts in potential interleave group, so far
  kdu_byte *ilv_ptrs[4]; // Buf_ptr for each elt in potential ilv group so far
  kdsc_component *ilv_comps[4]; // Components contributing each elt in group
  for (c=0; c < num_components; c++)
    { 
      kdsc_component *comp = components + c;
      int log2_bps = comp->buf_type & 3; // 2 LSB's hold log_2(bytes/sample)
      comp->simd_transfer = NULL;
      comp->simd_grp = NULL;
      KDSC_FIND_SIMD_TRANSFER_FUNC(comp->simd_transfer,comp->buf_type,
                                   comp->using_shorts,comp->sample_gap,
                                   comp->size.x,comp->precision,
                                   comp->original_precision,comp->is_absolute);
      if (comp->simd_transfer == NULL)
        { // No point in looking for an interleave group
          ilv_count = 0;
          continue;
        }
      if (comp->sample_gap <= 1)
        { // Component is not interleaved
          ilv_count = 0;
          comp->simd_grp = comp;
          comp->simd_ilv = 0;
          continue;
        }
      // Try to start or finish building an interleave group
      if ((ilv_count > 0) && (comp->sample_gap <= 4) &&
          (comp->sample_gap >= ilv_count) &&
          (comp->size == comp[-1].size) &&
          (comp->using_shorts == comp[-1].using_shorts) &&
          (comp->is_absolute == comp[-1].is_absolute) &&
          (comp->sample_gap == comp[-1].sample_gap) &&
          (comp->row_gap == comp[-1].row_gap)  &&
          (comp->buf_type == comp[-1].buf_type))
        { // Augment existing potential interleave group
          ilv_ptrs[ilv_count] = comp->buf_ptr;
          ilv_comps[ilv_count] = comp;
          ilv_count++;;
          if (ilv_count < comp->sample_gap)
            continue; // Need to keep building the group

          // See if `ilv_ptrs' is compatible with a true interleave group
          int j, k;
          kdu_byte *base_ptr = ilv_ptrs[0];
          for (j=0; j < ilv_count; j++)
            { // Find base interleaving address and prepare to generate
              // `simd_ilv' and `simd_padded_ilv' indices in the next step.
              if (base_ptr > ilv_ptrs[j])
                base_ptr = ilv_ptrs[j];
            }
          for (j=0; j < ilv_count; j++)
            { 
              int ilv_off = (int)((ilv_ptrs[j] - base_ptr) >> log2_bps);
              for (k=0; k < j; k++)
                if (ilv_ptrs[k] == ilv_ptrs[j])
                  { // Not fully interleaved
                    ilv_off = comp->sample_gap; // Forces test below to fail
                    break;
                  }
              if (ilv_off >= comp->sample_gap)
                { // Not actually interleaved
                  ilv_count = 0; break;
                }
              ilv_comps[j]->simd_ilv = ilv_off;
            }
          if (ilv_count > 0)
            { // Finish configuring state information for SIMD interleaving
              comp->simd_dst[0] = comp->simd_dst[1] =
                comp->simd_dst[2] = comp->simd_dst[3] = NULL;
              for (j=0; j < ilv_count; j++)
                ilv_comps[j]->simd_grp = comp;
              continue;
            }
        }

      // If we get here, we are not part of an existing interleave group
      // Start building a new potential interleave group.
      assert(comp->sample_gap > 1); // See test up above
      ilv_count = 1;
      ilv_comps[0] = comp;
      ilv_ptrs[0] = comp->buf_ptr;
    }
#endif // KDU_SIMD_OPTIMIZATIONS
}

/*****************************************************************************/
/*                            kdsc_tile::process                             */
/*****************************************************************************/

bool
  kdsc_tile::process(kdu_thread_env *env)
{
  int c;
  bool tile_complete = false;
  bool done = false;
  while (!done)
    {
      done = true;
      tile_complete = true;
      for (c=0; c < num_components; c++)
        { 
          kdsc_component *comp = components + c;
          if (comp->size.y > 0)
            tile_complete = false;
          if (comp->stripe_rows_left == 0)
            continue;
#if 1 // Use ratio counter to push stripe line in as uniformly as possible
          done = false;
          comp->ratio_counter -= comp->count_delta;
          if (comp->ratio_counter >= 0)
            continue;

          comp->size.y--;
          comp->stripe_rows_left--;
          comp->ratio_counter += comp->vert_subsampling;

          int log2_bps = comp->buf_type & 3;
          assert(comp->line == NULL);
          comp->line = engine.exchange_line(c,NULL,env);
          assert(comp->line != NULL);
#else // Allow more subsampled components to be pushed in as fast as possible
          assert(comp->line == NULL);
          if ((comp->line = engine.exchange_line(c,NULL,env)) == NULL)
            continue; // This component is not yet ready for writing
          done = false;
          comp->size.y--;
          comp->stripe_rows_left--;
          int log2_bps = comp->buf_type & 3;
#endif // Flow control strategies
          
#ifdef KDU_SIMD_OPTIMIZATIONS
          kdsc_component *grp = comp->simd_grp;
          if (grp != NULL)
            { 
              assert(comp->simd_transfer != NULL);
              int ilv = comp->simd_ilv;
              assert((ilv >= 0) && (ilv < 4));
              if (comp->using_shorts)
                grp->simd_dst[ilv] = comp->line->get_buf16();
              else 
                grp->simd_dst[ilv] = comp->line->get_buf32();
              if (grp == comp)
                comp->simd_transfer(comp->simd_dst,
                                    comp->buf_ptr-(comp->simd_ilv<<log2_bps),
                                    comp->size.x,comp->precision,
                                    comp->original_precision,
                                    comp->is_absolute,comp->is_signed);
            }
          else
#endif // KDU_SIMD_OPTIMIZATIONS
          if (comp->buf_type == KDSC_BUF8)
            transfer_bytes(*(comp->line),comp->buf8,comp->size.x,
                           comp->sample_gap,comp->precision,
                           comp->original_precision);
          else if (comp->buf_type == KDSC_BUF16)
            transfer_words(*(comp->line),comp->buf16,comp->size.x,
                           comp->sample_gap,comp->precision,
                           comp->original_precision,comp->is_signed);
          else if (comp->buf_type == KDSC_BUF32)
            transfer_dwords(*(comp->line),comp->buf32,comp->size.x,
                            comp->sample_gap,comp->precision,
                            comp->original_precision,comp->is_signed);
          else if (comp->buf_type == KDSC_BUF_FLOAT)
            transfer_floats(*(comp->line),comp->buf_float,comp->size.x,
                            comp->sample_gap,comp->precision,
                            comp->original_precision,comp->is_signed);
          else
            assert(0);
          comp->buf_ptr += comp->row_gap << log2_bps;
        }
      for (c=0; c < num_components; c++)
        { 
          kdsc_component *comp = components + c;
          if (comp->line == NULL)
            continue;
          engine.exchange_line(c,comp->line,env);
          comp->line = NULL;
        }
    }
  
  return tile_complete;
}


/* ========================================================================= */
/*                           kdu_stripe_compressor                           */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdu_stripe_compressor::get_new_tile                     */
/*****************************************************************************/

kdsc_tile *
  kdu_stripe_compressor::get_new_tile()
{
  kdsc_tile *tp = inactive_tiles;
  if (tp != NULL)
    { 
      if ((inactive_tiles = tp->next) == NULL)
        last_inactive_tile = NULL;
      tp->cleanup();
      tp->next = free_tiles;
      free_tiles = tp;
    }
  
  tp = free_tiles;
  if (tp == NULL)
    tp = new kdsc_tile;
  else
    free_tiles = tp->next;
  tp->next = NULL;
  tp->configure(num_components,comp_states);
  return tp;
}

/*****************************************************************************/
/*                kdu_stripe_compressor::note_inactive_tile                  */
/*****************************************************************************/

void
  kdu_stripe_compressor::note_inactive_tile(kdsc_tile *tp,
                                            kdu_thread_env *caller,
                                            bool all_pushed)
{
  tp->next = NULL;
  tp->queue = NULL;
  if (last_inactive_tile == NULL)
    last_inactive_tile = inactive_tiles = tp;
  else
    last_inactive_tile = last_inactive_tile->next = tp;
  if (caller == NULL)
    tp->tile.close();
  else if (all_pushed)
    tp->tile.close(caller);
  else
    tp->tile.close(caller,true); // Best to close tile in the background
  tp->tile = kdu_tile(NULL); // Not really required; `close' does this anyway
}

/*****************************************************************************/
/*                   kdu_stripe_compressor::get_new_queue                    */
/*****************************************************************************/

kdsc_queue *
  kdu_stripe_compressor::get_new_queue()
{
  kdsc_queue *qp = free_queues;
  if (qp == NULL)
    qp = new kdsc_queue;
  else
    free_queues = qp->next;
  qp->next = NULL;
  assert((qp->first_tile == NULL) && (qp->last_tile == NULL) &&
         (qp->num_tiles == 0));
  assert(env != NULL);
  if (next_queue_idx < 0)
    next_queue_idx = 0; // In case of wrap-around
  env->attach_queue(&(qp->thread_queue),&local_env_queue,NULL,next_queue_idx);
  next_queue_idx++;
  return qp;
}

/*****************************************************************************/
/*                   kdu_stripe_compressor::release_queue                    */
/*****************************************************************************/

void
  kdu_stripe_compressor::release_queue(kdsc_queue *qp, bool all_pushed,
                                       kdu_thread_env *caller)
{
  qp->next = free_queues;
  free_queues = qp;

  assert(caller != NULL);
  if (qp->thread_queue.is_attached())
    caller->join(&(qp->thread_queue),false);
  
  kdsc_tile *tp;
  while ((tp = qp->first_tile) != NULL)
    { 
      qp->first_tile = (tp==qp->last_tile)?NULL:tp->next;
      assert(qp->num_tiles > 0);
      qp->num_tiles--;
	    note_inactive_tile(tp,caller,all_pushed);
    }
  qp->last_tile = NULL;
  assert(qp->num_tiles == 0);
  qp->num_tiles = 0; // Just in case
}

/*****************************************************************************/
/*                   kdu_stripe_compressor::cleanup_queue                    */
/*****************************************************************************/

void
  kdu_stripe_compressor::cleanup_queue(kdsc_queue *qp)
{
  qp->next = free_queues;
  free_queues = qp;
  assert(!qp->thread_queue.is_attached());
  
  kdsc_tile *tp;
  while ((tp = qp->first_tile) != NULL)
    { 
      qp->first_tile = (tp==qp->last_tile)?NULL:tp->next;
      assert(qp->num_tiles > 0);
      qp->num_tiles--;
      note_inactive_tile(tp,NULL,true);      
    }
  qp->last_tile = NULL;
  assert(qp->num_tiles == 0);
  qp->num_tiles = 0; // Just in case
}

/*****************************************************************************/
/*                kdu_stripe_compressor::kdu_stripe_compressor               */
/*****************************************************************************/

kdu_stripe_compressor::kdu_stripe_compressor()
{
  flush_layer_specs = 0;
  flush_sizes = NULL;
  flush_slopes = NULL;
  flush_flags = 0;
  size_tolerance = 0.0;
  trim_to_rate = false;
  record_layer_info_in_comment = false;
  force_precise = false;
  want_fastest = false;
  all_done = true;
  num_components = 0;
  left_tile_idx = num_tiles = kdu_coords(0,0);
  partial_tiles = free_tiles = NULL;
  inactive_tiles = NULL;
  last_inactive_tile = NULL;
  lines_since_flush = 0;
  flush_on_tile_boundary = false;
  auto_flush_started = false;
  comp_states = NULL;
  env = NULL; env_dbuf_height = 0;
  active_queue = NULL;
  finished_queues = last_finished_queue = NULL;
  free_queues = NULL;
  num_finished_tiles = 0;
  max_finished_tiles = 1; // Modified by `start' anyway
  next_queue_idx = 0;
}

/*****************************************************************************/
/*                        kdu_stripe_compressor::start                       */
/*****************************************************************************/

void
  kdu_stripe_compressor::start(kdu_codestream codestream, int num_layer_specs,
                               const kdu_long *layer_sizes,
                               const kdu_uint16 *layer_slopes,
                               kdu_uint16 min_slope_threshold,
                               bool no_prediction, bool force_precise,
                               bool record_layer_info_in_comment,
                               double size_tolerance, int num_components,
                               bool want_fastest, kdu_thread_env *env,
                               kdu_thread_queue *env_queue,
                               int env_dbuf_height, int env_tile_concurrency,
                               bool trim_to_rate, int flush_flags,
                               const kdu_push_pull_params *extra_params)
{
  assert((partial_tiles == NULL) &&
         (inactive_tiles == NULL) && (finished_queues == NULL) &&
         (last_finished_queue == NULL) && (last_inactive_tile == NULL) &&
         (active_queue == NULL) && (free_queues == NULL) &&
         (this->flush_sizes == NULL) && (this->flush_slopes == NULL) &&
         (this->comp_states == NULL) && !this->codestream.exists() &&
         (this->env == NULL));

  // Start by getting some preliminary parameters
  this->codestream = codestream;
  this->pp_params = kdu_push_pull_params();
  if (extra_params != NULL)
    this->pp_params = *extra_params;
  this->record_layer_info_in_comment = record_layer_info_in_comment;
  this->size_tolerance = size_tolerance;
  this->trim_to_rate = trim_to_rate;
  this->flush_flags = flush_flags;
  this->force_precise = force_precise;
  this->want_fastest = want_fastest;
  this->num_components = codestream.get_num_components(true);
  kdu_dims tile_indices; codestream.get_valid_tiles(tile_indices);
  this->num_tiles = tile_indices.size;
  this->left_tile_idx = tile_indices.pos;
  if (env == NULL)
    max_finished_tiles = 0;
  else
    { 
      if (env_tile_concurrency <= 0)
        env_tile_concurrency = (2 + env->get_num_threads()) >> 1;
      if (env_tile_concurrency > num_tiles.x)
        max_finished_tiles = num_tiles.x;
      else
        max_finished_tiles = env_tile_concurrency - 1;
    }
  
  // Next, determine the number of components we are actually going to process
  if (num_components > 0)
    {
      if (num_components > this->num_components)
        { KDU_ERROR_DEV(e,0x06090500); e <<
            KDU_TXT("The optional `num_components' argument supplied to "
            "`kdu_stripe_compressor::start' may not exceed the number of "
            "output components declared by the codestream header.");
        }
      this->num_components = num_components;
    }
  num_components = this->num_components;

  // Now process the rate and slope specifications
  int n;
  kdu_params *cod = codestream.access_siz()->access_cluster(COD_params);
  assert(cod != NULL);
  if (!cod->get(Clayers,0,0,flush_layer_specs))
    flush_layer_specs = 0;
  if (num_layer_specs > 0)
    { 
      if (flush_layer_specs == 0)
        {
          flush_layer_specs = num_layer_specs;
          cod->set(Clayers,0,0,num_layer_specs);
        }
      if (flush_layer_specs > num_layer_specs)
        flush_layer_specs = num_layer_specs;
      flush_sizes = new kdu_long[flush_layer_specs];
      flush_slopes = new kdu_uint16[flush_layer_specs];
      for (n=0; n < flush_layer_specs; n++)
        { flush_sizes[n] = 0; flush_slopes[n] = 0; }
      if (layer_sizes != NULL)
        for (n=0; n < flush_layer_specs; n++)
          flush_sizes[n] = layer_sizes[n];
      else
        flush_flags &= ~(KDU_FLUSH_USES_THRESHOLDS_AND_SIZES |
                         KDU_FLUSH_THRESHOLDS_ARE_HINTS);
      if ((layer_slopes != NULL) && (layer_slopes[0] != 0) &&
          ((layer_sizes == NULL) ||
           (flush_flags & (KDU_FLUSH_THRESHOLDS_ARE_HINTS |
                           KDU_FLUSH_USES_THRESHOLDS_AND_SIZES))))
        for (n=0; n < flush_layer_specs; n++)
          flush_slopes[n] = layer_slopes[n];
    }
  else
    {
      if (flush_layer_specs == 0)
        {
          flush_layer_specs = 1;
          cod->set(Clayers,0,0,flush_layer_specs);
        }
      flush_sizes = new kdu_long[flush_layer_specs];
      flush_slopes = new kdu_uint16[flush_layer_specs];
      for (n=0; n < flush_layer_specs; n++)
        { flush_sizes[n] = 0; flush_slopes[n] = 0; }
    }

  // Install block truncation prediction mechanisms, as appropriate
  if (!no_prediction)
    { 
      if (min_slope_threshold != 0)
        codestream.set_min_slope_threshold(min_slope_threshold);
      else if ((layer_sizes != NULL) && (num_layer_specs > 0) &&
               (layer_sizes[num_layer_specs-1] > 0))
        codestream.set_max_bytes(layer_sizes[num_layer_specs-1]);
      else if ((layer_slopes != NULL) && (num_layer_specs > 0))
        codestream.set_min_slope_threshold(layer_slopes[num_layer_specs-1]);
    }

  // Finalize preparation for compression
  codestream.access_siz()->finalize_all();
  all_done = false;
  lines_since_flush = 0;
  flush_on_tile_boundary = false;
  auto_flush_started = false;
  comp_states = new kdsc_component_state[num_components];
  for (n=0; n < num_components; n++)
    { 
      kdsc_component_state *cs = comp_states + n;
      cs->comp_idx = n;
      kdu_dims dims; codestream.get_dims(n,dims,true);
      cs->pos_x = dims.pos.x; // Values used by `kdsc_tile::init'
      cs->width = dims.size.x;
      cs->original_precision = codestream.get_bit_depth(n,true);
      if (cs->original_precision < 0)
        cs->original_precision = -cs->original_precision;
      codestream.get_subsampling(n,cs->sub_sampling,true);
      cs->row_gap = cs->sample_gap = cs->precision = 0;
      cs->buf_ptr = NULL;  cs->buf_type = -1;
      cs->stripe_height = 0;
      kdu_coords idx = tile_indices.pos;
      codestream.get_tile_dims(idx,n,dims,true);
      cs->remaining_tile_height = dims.size.y;
      cs->remaining_tile_rows = num_tiles.y;
      cs->next_tile_height = 0; // Updated below if next row of tiles exists
      cs->max_tile_height = dims.size.y;
      if (num_tiles.y > 1)
        { 
          idx.y++;
          codestream.get_tile_dims(idx,n,dims,true);
          cs->next_tile_height = dims.size.y;
          if (dims.size.y > cs->max_tile_height)
            cs->max_tile_height = dims.size.y;
        }
      cs->max_recommended_stripe_height = 0; // Until we assign one
    }

  // Configure multi-threaded processing
  if ((this->env = env) != NULL)
    env->attach_queue(&local_env_queue,env_queue,NULL);
  this->env_dbuf_height = env_dbuf_height;
  this->tiles_to_open = tile_indices;
  this->last_tile_accessed = tile_indices.pos; // No harm done even though
            // the first tile has not actually been scheduled yet.  This member
            // is used only to facilitate closure of tiles that may have been
            // scheduled for opening but have not been accessed by the time
            // `finish' is called.  We find the indices of all such tiles by
            // using `first_scheduled_tile' together with `tiles_to_open'.
}

/*****************************************************************************/
/*                kdu_stripe_compressor::configure_auto_flush                */
/*****************************************************************************/

void
  kdu_stripe_compressor::configure_auto_flush(int flush_period)
{
  assert((env != NULL) && !auto_flush_started);
  int c, min_sub_y=0; // Will be min component vertical sub-sampling
  for (c=0; c < num_components; c++)
    { 
      kdu_coords subs = comp_states[c].sub_sampling;
      if ((min_sub_y == 0) || (min_sub_y > subs.y))
        min_sub_y = subs.y;
    }
  kdu_dims t_dims; codestream.get_tile_partition(t_dims);
  int max_tile_lines = 1 + ((t_dims.size.y-1) / min_sub_y);
  
  kdu_long tc_trigger_interval=1; 
  if (flush_period > max_tile_lines)
    tc_trigger_interval = flush_period / max_tile_lines;
  tc_trigger_interval *= num_components;
  tc_trigger_interval *= num_tiles.x;
  if (tc_trigger_interval > (1<<30))
    tc_trigger_interval = 1<<30; // Just in case
  
  kdu_long incr_trigger_interval = 0;
  if ((flush_period+(flush_period>>1)) < max_tile_lines)
    { // Otherwise, don't bother with incremental flush within tile
      incr_trigger_interval = flush_period * min_sub_y;
      incr_trigger_interval *= num_components;
      incr_trigger_interval *= num_tiles.x;
      if (incr_trigger_interval > (1<<30))
        incr_trigger_interval = 1<<30; // Just in case
    }
  
  codestream.auto_flush((int)tc_trigger_interval,
                        (int)tc_trigger_interval,
                        (int)incr_trigger_interval,
                        (int)incr_trigger_interval,
                        flush_sizes,flush_layer_specs,flush_slopes,
                        trim_to_rate,record_layer_info_in_comment,
                        size_tolerance,env,flush_flags);
  auto_flush_started = true;
}

/*****************************************************************************/
/*             kdu_stripe_compressor::get_set_next_queue_sequence            */
/*****************************************************************************/

kdu_long
  kdu_stripe_compressor::get_set_next_queue_sequence(kdu_long min_val)
{
  kdu_long old_val = this->next_queue_idx;
  volatile kdu_long diff = min_val - old_val; // Insist on evaluation of `diff'
  if (diff > 0)
    this->next_queue_idx = min_val;
  return old_val;
}

/*****************************************************************************/
/*                       kdu_stripe_compressor::reset                        */
/*****************************************************************************/

void
  kdu_stripe_compressor::reset(bool free_memory)
{
  if (env != NULL)
    { 
      env = NULL;
      env_dbuf_height = 0;
      // In case we did not finish all processing before calling here, there
      // may be some tiles that were scheduled for opening but have not been
      // accessed.  These will not be closed properly unless we close them
      // here.  The `env' reference itself must not be used from this
      // function, since the multi-threaded environment may have been
      // destroyed already; its existence is just an indication that there
      // may be outstanding open tiles.
      
      assert(left_tile_idx.x == tiles_to_open.pos.x);
      assert(num_tiles.x == tiles_to_open.size.x);
      kdu_coords scheduled_lim = tiles_to_open.pos + tiles_to_open.size;
      kdu_dims trange;
      trange.pos.y = last_tile_accessed.y;
      trange.pos.x = last_tile_accessed.x + 1;
      if (trange.pos.x < scheduled_lim.x)
        { // Partial row of tiles to close
          trange.size.y = 1;
          trange.size.x = scheduled_lim.x - trange.pos.x;
          assert(trange.size.x < num_tiles.x);
          if (trange.pos.y < scheduled_lim.y)
            codestream.close_tiles(trange,NULL);
        }
      trange.pos.x = left_tile_idx.x;
      trange.pos.y++;
      trange.size.x = num_tiles.x;
      trange.size.y = scheduled_lim.y - trange.pos.y;
      if (!trange.is_empty())
        codestream.close_tiles(trange,NULL);
      
      // Terminate the multi-threaded processing queues
      while ((last_finished_queue=finished_queues) != NULL)
        { // These ones have already been finished.
          finished_queues = last_finished_queue->next;
          cleanup_queue(last_finished_queue);
        }
      num_finished_tiles = 0;
    }
  assert(num_finished_tiles == 0);

  flush_layer_specs = 0;
  if (flush_sizes != NULL)
    delete[] flush_sizes;
  flush_sizes = NULL;
  if (flush_slopes != NULL)
    delete[] flush_slopes;
  flush_slopes = NULL;
  if (comp_states != NULL)
    delete[] comp_states;
  comp_states = NULL;
  codestream = kdu_codestream(); // Make the interface empty
  
  kdsc_queue *qp;
  if ((qp = active_queue) != NULL)
    { 
      active_queue = NULL;
      delete qp;
    }
  last_finished_queue = NULL;
  while ((qp = free_queues) != NULL)
    { 
      free_queues = qp->next;
      delete qp;
    }
  
  kdsc_tile *tp;
  while ((tp=partial_tiles) != NULL)
    { 
      partial_tiles = tp->next;
      tp->cleanup();
      tp->next = free_tiles;
      free_tiles = tp;
    }
  while ((tp=inactive_tiles) != NULL)
    { 
      inactive_tiles = tp->next;
      tp->cleanup();
      tp->next = free_tiles;
      free_tiles = tp;
    }
  last_inactive_tile = NULL;
  if (!free_memory)
    return;
  
  while ((tp=free_tiles) != NULL)
    { 
      free_tiles = tp->next;
      delete tp;
    }
}

/*****************************************************************************/
/*                       kdu_stripe_compressor::finish                       */
/*****************************************************************************/

bool
  kdu_stripe_compressor::finish(int num_layer_specs,
                                kdu_long *layer_sizes,
                                kdu_uint16 *layer_slopes,
                                kdu_thread_env *local_env)
{
  if (env == NULL)
    local_env = NULL;
  if (local_env == NULL)
    local_env = env;
  if (local_env != NULL)
    { 
      // In case we did not finish all processing before calling here, there
      // may be some tiles that were scheduled for opening but have not been
      // accessed.  Closing these forcefully here at least saves unnecessary
      // tile opening operations being performed if they have not already
      // been performed.
      assert(left_tile_idx.x == tiles_to_open.pos.x);
      assert(num_tiles.x == tiles_to_open.size.x);
      kdu_coords scheduled_lim = tiles_to_open.pos + tiles_to_open.size;
      kdu_dims trange;
      trange.pos.y = last_tile_accessed.y;
      trange.pos.x = last_tile_accessed.x + 1;
      if (trange.pos.x < scheduled_lim.x)
        { // Partial row of tiles to close
          trange.size.y = 1;
          trange.size.x = scheduled_lim.x - trange.pos.x;
          assert(trange.size.x < num_tiles.x);
          if (trange.pos.y < scheduled_lim.y)
            codestream.close_tiles(trange,local_env);
        }
      trange.pos.x = left_tile_idx.x;
      trange.pos.y++;
      trange.size.x = num_tiles.x;
      trange.size.y = scheduled_lim.y - trange.pos.y;
      if (!trange.is_empty())
        codestream.close_tiles(trange,local_env);
      
      // Terminate the multi-threaded processing queues
      while ((last_finished_queue=finished_queues) != NULL)
        { // These ones have already been finished so wait for them to complete
          // properly.
          finished_queues = last_finished_queue->next;
          release_queue(last_finished_queue,true,local_env);
        }
      num_finished_tiles = 0;
      
      // Terminate all processing associated with the codestream
      local_env->terminate(&local_env_queue,false);
      local_env->cs_terminate(codestream); // Terminates background processing
      env = NULL; // Unprotected access to the codestream is fine from here
      env_dbuf_height = 0;
    }
  assert(num_finished_tiles == 0);

  if (!codestream.exists())
    {
      assert((layer_sizes == NULL) && (layer_slopes == NULL));
      return false;
    }
  if (all_done)
    { // Always issue a final `flush' call -- this is now safe even if
      // incremental flushing has done everything.
      codestream.flush(flush_sizes,flush_layer_specs,flush_slopes,
                       trim_to_rate,record_layer_info_in_comment,
                       size_tolerance,NULL,flush_flags);
    }
  
  int n;
  for (n=0; (n < num_layer_specs) && (n < flush_layer_specs); n++)
    {
      if (layer_sizes != NULL)
        layer_sizes[n] = flush_sizes[n];
      if (layer_slopes != NULL)
        layer_slopes[n] = flush_slopes[n];
    }
  for (; n < num_layer_specs; n++)
    {
      if (layer_sizes != NULL)
        layer_sizes[n] = 0;
      if (layer_slopes != NULL)
        layer_slopes[n] = 0;
    }

  flush_layer_specs = 0;
  if (flush_sizes != NULL)
    delete[] flush_sizes;
  flush_sizes = NULL;
  if (flush_slopes != NULL)
    delete[] flush_slopes;
  flush_slopes = NULL;
  if (comp_states != NULL)
    delete[] comp_states;
  comp_states = NULL;
  codestream = kdu_codestream(); // Make the interface empty
  
  kdsc_queue *qp;
  if ((qp = active_queue) != NULL)
    { 
      active_queue = NULL;
      delete qp;
    }
  last_finished_queue = NULL;
  while ((qp = free_queues) != NULL)
    { 
      free_queues = qp->next;
      delete qp;
    }
  
  kdsc_tile *tp;
  while ((tp=partial_tiles) != NULL)
    { 
      partial_tiles = tp->next;
      tp->cleanup();
      tp->next = free_tiles;
      free_tiles = tp;
    }
  while ((tp=inactive_tiles) != NULL)
    { 
      inactive_tiles = tp->next;
      tp->cleanup();
      tp->next = free_tiles;
      free_tiles = tp;
    }
  last_inactive_tile = NULL;
  
  return all_done;
}

/*****************************************************************************/
/*           kdu_stripe_compressor::get_recommended_stripe_heights           */
/*****************************************************************************/

bool
  kdu_stripe_compressor::get_recommended_stripe_heights(int preferred_min,
                                                        int absolute_max,
                                                        int rec_heights[],
                                                        int *max_heights)
{
  if (preferred_min < 1)
    preferred_min = 1;
  if (absolute_max < preferred_min)
    absolute_max = preferred_min;
  if (!codestream.exists())
    { KDU_ERROR_DEV(e,1); e <<
        KDU_TXT("You may not call `kdu_stripe_compressor's "
        "`get_recommended_stripe_heights' function without first calling the "
        "`start' function.");
    }
  int c, max_val;

  if (comp_states[0].max_recommended_stripe_height==0)
    { // Need to assign max recommended stripe heights, based on max tile size
      for (max_val=0, c=0; c < num_components; c++)
        {
          comp_states[c].max_recommended_stripe_height =
            comp_states[c].max_tile_height;
          if (comp_states[c].max_tile_height > max_val)
            max_val = comp_states[c].max_tile_height;
        }
      int limit = (num_tiles.x==1)?preferred_min:absolute_max;
      if (limit < max_val)
        {
          int scale = 1 + ((max_val-1) / limit);
          for (c=0; c < num_components; c++)
            {
              comp_states[c].max_recommended_stripe_height =
                1 + (comp_states[c].max_tile_height / scale);
              if (comp_states[c].max_recommended_stripe_height > limit)
                comp_states[c].max_recommended_stripe_height = limit;
            }
        }
    }

  for (max_val=0, c=0; c < num_components; c++)
    {
      kdsc_component_state *cs = comp_states + c;
      rec_heights[c] = cs->remaining_tile_height;
      if (rec_heights[c] > max_val)
        max_val = rec_heights[c];
      if (max_heights != NULL)
        max_heights[c] = cs->max_recommended_stripe_height;
    }
  int limit = (num_tiles.x==1)?preferred_min:absolute_max;
  if (limit < max_val)
    {
      int scale = 1 + ((max_val-1) / limit);
      for (c=0; c < num_components; c++)
        rec_heights[c] = 1 + (rec_heights[c] / scale);
    }
  for (c=0; c < num_components; c++)
    {
      if (rec_heights[c] > comp_states[c].max_recommended_stripe_height)
        rec_heights[c] = comp_states[c].max_recommended_stripe_height;
      if (rec_heights[c] > comp_states[c].remaining_tile_height)
        rec_heights[c] = comp_states[c].remaining_tile_height;
    }
  return (num_tiles.x > 1);
}

/*****************************************************************************/
/*              kdu_stripe_compressor::get_next_stripe_heights               */
/*****************************************************************************/

bool
  kdu_stripe_compressor::get_next_stripe_heights(int preferred_min,
                                                 int absolute_max,
                                                 int cur_heights[],
                                                 int rec_heights[])
{
  if (preferred_min < 1)
    preferred_min = 1;
  if (absolute_max < preferred_min)
    absolute_max = preferred_min;
  if (!codestream.exists())
    return false;

  int c, max_val;
  
  if (comp_states[0].max_recommended_stripe_height==0)
    { // Need to assign max recommended stripe heights, based on max tile size
      for (max_val=0, c=0; c < num_components; c++)
        { 
          comp_states[c].max_recommended_stripe_height =
          comp_states[c].max_tile_height;
          if (comp_states[c].max_tile_height > max_val)
            max_val = comp_states[c].max_tile_height;
        }
      int limit = (num_tiles.x==1)?preferred_min:absolute_max;
      if (limit < max_val)
        { 
          int scale = 1 + ((max_val-1) / limit);
          for (c=0; c < num_components; c++)
            { 
              comp_states[c].max_recommended_stripe_height =
                1 + (comp_states[c].max_tile_height / scale);
              if (comp_states[c].max_recommended_stripe_height > limit)
                comp_states[c].max_recommended_stripe_height = limit;
            }
        }
    }
  
  bool all_empty = true;
  for (max_val=0, c=0; c < num_components; c++)
    { 
      kdsc_component_state *cs = comp_states + c;
      int remaining = cs->remaining_tile_height - cur_heights[c];
      if (remaining <= 0)
        remaining = cs->next_tile_height;
      rec_heights[c] = remaining;
      if (rec_heights[c] > max_val)
        max_val = rec_heights[c];
      if (rec_heights[c] > 0)
        all_empty = false;
    }
  if (all_empty)
    return false;
  
  int limit = (num_tiles.x==1)?preferred_min:absolute_max;
  if (limit < max_val)
    { 
      int scale = 1 + ((max_val-1) / limit);
      for (c=0; c < num_components; c++)
        rec_heights[c] = 1 + (rec_heights[c] / scale);
    }
  for (c=0; c < num_components; c++)
    { 
      kdsc_component_state *cs = comp_states + c;
      if (rec_heights[c] > cs->max_recommended_stripe_height)
        rec_heights[c] = cs->max_recommended_stripe_height;
      int remaining = cs->remaining_tile_height - cur_heights[c];
      if (remaining <= 0)
        remaining = cs->next_tile_height;
      if (rec_heights[c] > remaining)
        rec_heights[c] = remaining;
    }
  
  return true;
}

/*****************************************************************************/
/*                   kdu_stripe_compressor::push_stripe (bytes)              */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_byte *stripe_bufs[],
                                     const int heights[],
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF8;
      cs->buf8 = stripe_bufs[c];
      cs->stripe_height = heights[c];
      cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?8:(precisions[c]);
      cs->is_signed = false;
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 8) cs->precision = 8;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*                kdu_stripe_compressor::push_stripe (bytes, single buf)     */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_byte *buffer, const int heights[],
                                     const int *sample_offsets,
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF8;
      cs->buf8 = buffer + ((sample_offsets==NULL)?c:(sample_offsets[c]));
      cs->stripe_height = heights[c];
      if ((sample_offsets == NULL) && (sample_gaps == NULL))
        cs->sample_gap = num_components;
      else
        cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?8:(precisions[c]);
      cs->is_signed = false;
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 8) cs->precision = 8;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*                kdu_stripe_compressor::push_stripe (words)                 */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_int16 *stripe_bufs[],
                                     const int heights[],
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF16;
      cs->buf16 = stripe_bufs[c];
      cs->stripe_height = heights[c];
      cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?16:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 16) cs->precision = 16;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*             kdu_stripe_compressor::push_stripe (words, single buf)        */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_int16 *buffer, const int heights[],
                                     const int *sample_offsets,
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF16;
      cs->buf16 = buffer + ((sample_offsets==NULL)?c:(sample_offsets[c]));
      cs->stripe_height = heights[c];
      if ((sample_offsets == NULL) && (sample_gaps == NULL))
        cs->sample_gap = num_components;
      else
        cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?16:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 16) cs->precision = 16;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*                kdu_stripe_compressor::push_stripe (dwords)                */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_int32 *stripe_bufs[],
                                     const int heights[],
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF32;
      cs->buf32 = stripe_bufs[c];
      cs->stripe_height = heights[c];
      cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?32:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 32) cs->precision = 32;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*             kdu_stripe_compressor::push_stripe (dwords, single buf)       */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(kdu_int32 *buffer, const int heights[],
                                     const int *sample_offsets,
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF32;
      cs->buf32 = buffer + ((sample_offsets==NULL)?c:(sample_offsets[c]));
      cs->stripe_height = heights[c];
      if ((sample_offsets == NULL) && (sample_gaps == NULL))
        cs->sample_gap = num_components;
      else
        cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?32:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < 1) cs->precision = 1;
      if (cs->precision > 32) cs->precision = 32;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*                kdu_stripe_compressor::push_stripe (floats)                */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(float *stripe_bufs[],
                                     const int heights[],
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF_FLOAT;
      cs->buf_float = stripe_bufs[c];
      cs->stripe_height = heights[c];
      cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?0:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < -64) cs->precision = -64;
      if (cs->precision > 64) cs->precision = 64;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*             kdu_stripe_compressor::push_stripe (floats, single buf)       */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_stripe(float *buffer, const int heights[],
                                     const int *sample_offsets,
                                     const int *sample_gaps,
                                     const int *row_gaps,
                                     const int *precisions,
                                     const bool *is_signed,
                                     int flush_period)
{
  int c;
  assert(codestream.exists());
  for (c=0; c < num_components; c++)
    { // Copy pointers so they can be internally updated during processing
      kdsc_component_state *cs=comp_states+c;
      assert(cs->stripe_height == 0);
      cs->buf_type = KDSC_BUF_FLOAT;
      cs->buf_float = buffer + ((sample_offsets==NULL)?c:(sample_offsets[c]));
      cs->stripe_height = heights[c];
      if ((sample_offsets == NULL) && (sample_gaps == NULL))
        cs->sample_gap = num_components;
      else
        cs->sample_gap = (sample_gaps==NULL)?1:(sample_gaps[c]);
      cs->row_gap = (row_gaps==NULL)?(cs->width*cs->sample_gap):(row_gaps[c]);
      cs->precision = (precisions==NULL)?0:(precisions[c]);
      cs->is_signed = (is_signed==NULL)?true:(is_signed[c]);
      if (cs->precision < -64) cs->precision = -64;
      if (cs->precision > 64) cs->precision = 64;
    }
  return push_common(flush_period);
}

/*****************************************************************************/
/*                    kdu_stripe_compressor::push_common                     */
/*****************************************************************************/

bool
  kdu_stripe_compressor::push_common(int flush_period)
{
  if (num_tiles.y <= 0)
    return false; // The application probably ignored a previous false return
  
  // Configure auto-flush, if required, as soon as possible, since this
  // requires the `KD_THREADLOCK_GENERAL' mutex to be acquired within the
  // core codestream machinery which might need to contend with background
  // tile opening operations if we wait until after we have already called
  // `codestream.open_tiles' -- see below.
  if ((flush_period > 0) && (flush_period < INT_MAX) &&
      (env != NULL) && !auto_flush_started)
    configure_auto_flush(flush_period);
  
  int c;
  bool push_complete = false;
  lines_since_flush += comp_states[0].stripe_height;
  while (!push_complete)
    {
      int t;
      kdu_coords tile_idx=left_tile_idx;
      kdsc_tile *next_tp=NULL, *tp=partial_tiles;
      for (t=num_tiles.x; t > 0; t--, tile_idx.x++, tp=next_tp)
        { 
          if (tp == NULL)
            { // Need to create new tile
              tp = partial_tiles = get_new_tile();
              assert(tp->queue == NULL); // Queue added by `init' if required
              if ((env != NULL) && (t == num_tiles.x) &&
                  (tiles_to_open.size.y > 0))
                { // Schedule a whole row of tiles to be opened in advance
                  // rather than opening them in-line within `tp->init'.
                  kdu_dims tile_range = tiles_to_open;
                  if (tile_range.pos.y == tile_idx.y)
                    { // No tiles opened yet at all, so schedule two rows for
                      // opening in the background -- the very first tile will
                      // actually be opened immediately inside the call to
                      // `codestream.open_tiles' below.
                      if (tile_range.size.y > 2)
                        tile_range.size.y = 2;
                    }
                  else
                    tile_range.size.y = 1;
                  codestream.open_tiles(tile_range,true,env);
                  tiles_to_open.pos.y += tile_range.size.y;
                  tiles_to_open.size.y -= tile_range.size.y;
                }
            }
          if ((env != NULL) && (active_queue == NULL))
            active_queue = get_new_queue();
          tp->init(tile_idx,codestream,comp_states,force_precise,
                   want_fastest,env,env_dbuf_height,active_queue,
                   &pp_params,num_tiles.x);
          if ((last_tile_accessed.y < tile_idx.y) ||
              ((last_tile_accessed.y == tile_idx.y) &&
               (last_tile_accessed.x < tile_idx.x)))
            last_tile_accessed = tile_idx;
          
          if (tp->process(env))
            { // Tile is completed
              assert(tp == partial_tiles);
              next_tp = partial_tiles = tp->next;
              bool all_pushed = (t == 1) && (num_tiles.y == 1);
              if (tp->queue == NULL)
	              note_inactive_tile(tp,env,all_pushed);
              else
                { 
                  assert(tp->queue == active_queue);
                  if (tp == active_queue->last_tile)
                    { 
                      if (last_finished_queue == NULL)
                        finished_queues = active_queue;
                      else
                        last_finished_queue->next = active_queue;
                      last_finished_queue = active_queue;
                      num_finished_tiles += active_queue->num_tiles;
                      active_queue = NULL;
                      while ((finished_queues != NULL) &&
                             (num_finished_tiles > max_finished_tiles) &&
                             ((max_finished_tiles < 1) ||
                              (last_finished_queue != finished_queues)))
                        { // Remove first finished queue; note that the entire
                          // list of finished queues can be removed only in
                          // the event that `max_finished_tiles' < 1
                          kdsc_queue *qp = finished_queues;
                          num_finished_tiles -= qp->num_tiles;
                          if ((finished_queues = qp->next) == NULL)
                            { // Removing the entire finished tile queue
                              assert(max_finished_tiles < 1);
                              last_finished_queue = NULL;
                            }
                          else
                            assert(num_finished_tiles >=
                                   finished_queues->num_tiles);
                          release_queue(qp,all_pushed,env);
                        }
                    }
                }
            }
          else
            { // Not enough data to complete tile
              if ((t > 1) && ((next_tp = tp->next) == NULL))
                { // Build on the list
                  next_tp = tp->next = get_new_tile();
                }
            }
        }

      // See if the entire row of tiles is complete or not
      if (partial_tiles == NULL)
        {
          left_tile_idx.y++; num_tiles.y--;
          all_done = (num_tiles.y == 0);
        }
      push_complete = true;
      for (c=0; c < num_components; c++)
        {
          comp_states[c].update(left_tile_idx,codestream);
          if (comp_states[c].stripe_height > 0)
            push_complete = false;
        }
      if ((partial_tiles != NULL) && !push_complete)
        { KDU_ERROR_DEV(e,2); e <<
            KDU_TXT("Inappropriate use of the `kdu_stripe_compressor' "
            "object.  Image component samples must not be pushed into this "
            "object in such disproportionate fashion as to require the object "
            "to maintain multiple rows of open tile pointers!  See "
            "description of the `kdu_stripe_compressor::push_line' interface "
            "function for more details on how to use it correctly.");
        }
    }

  if (all_done)
    return false;

  if ((env != NULL) || (flush_period <= 0))
    return true;

  // If we get here, single-threaded incremental flushing may be required
  if (partial_tiles != NULL)
    { // See if it is worth waiting until we reach the tile boundary
      int rem = partial_tiles->components->size.y;
      if ((rem < (flush_period>>2)) || flush_on_tile_boundary)
        {
          flush_on_tile_boundary = true;
          return true; // Wait until the tile boundary before flushing
        }
    }

  if ((lines_since_flush >= flush_period) || flush_on_tile_boundary)
    { 
      if (codestream.ready_for_flush())
        {
          codestream.flush(flush_sizes,flush_layer_specs,flush_slopes,
                           trim_to_rate,record_layer_info_in_comment,
                           size_tolerance,NULL,flush_flags);
          lines_since_flush -= flush_period;
        }
      else
        { // Not ready yet; try again in a little while
          lines_since_flush -= (flush_period >> 3);
        }
      flush_on_tile_boundary = false;
    }
  
  return true;
}
