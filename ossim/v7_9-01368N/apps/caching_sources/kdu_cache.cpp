/*****************************************************************************/
// File: kdu_cache.cpp [scope = APPS/CACHING_SOURCES]
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
  Implements a platform independent caching compressed data source.  A
complete implementation for the client in an interactive client-server
application can be derived from this class and requires relatively little
additional effort.  The complete client must incorporate networking elements.
******************************************************************************/

#include "cache_local.h"
#include "kdu_messaging.h"

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
     kdu_error _name("E(kdu_cache.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(kdu_cache.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu JPIP Cache:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu JPIP Cache:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers


/* ========================================================================= */
/*                              Internal Functions                           */
/* ========================================================================= */


/* ========================================================================= */
/*                               kd_var_cache_seg                            */
/* ========================================================================= */

/*****************************************************************************/
/*                        kd_var_cache_seg::recycle_all                      */
/*****************************************************************************/

void kd_var_cache_seg::recycle_all(kd_cache *cache)
{
  assert(access_ctl.get() == 0);
  reclaim_next = reclaim_prev = NULL;
  if (flags & KD_CSEG_LEAF)
    { 
      for (int n=0; (n < 128) && (num_non_null > 0); n++)
        { 
          kd_cache_buf *buf_list = databins[n];
          if (buf_list == NULL)
            continue;
          databins[n] = NULL;
          assert(num_non_null > 0);
          num_non_null--;
          kdu_int32 lsbs = _addr_to_kdu_int32(buf_list) & 3;
          buf_list = (kd_cache_buf *)(((kdu_byte *)buf_list)-lsbs);
          if (buf_list == NULL)
            continue; // Must be a CEMPTY or DELETED marker
          if (lsbs == 0)
            { 
              assert(num_descendants > 0);
              num_descendants--;
            }
          else
            { 
              assert(num_erasable > 0);
              num_erasable--;
            }
          cache->buf_server->release(buf_list);
        }
    }
  else if (flags & KD_CSEG_STREAM_ROOT)
    { 
      for (int n=0; (n < KDU_NUM_DATABIN_CLASSES) && (num_non_null > 0); n++)
        { 
          kd_var_cache_seg *seg = stream.classes[n];
          if (seg == NULL)
            continue;
          stream.classes[n] = NULL;
          num_non_null--;
          kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
          seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
          if (seg == NULL)
            continue; // Must be a CEMPTY or DELETED marker
          if (lsbs == 0)
            { 
              assert(num_descendants > 0);
              num_descendants--;
            }
          else
            { 
              assert(num_erasable > 0);
              num_erasable--;
            }
          seg->recycle_all(cache);
        }
    }
  else
    { 
      for (int n=0; (n < 128) && (num_non_null > 0); n++)
        { 
          kd_var_cache_seg *seg = segs[n];
          if (seg == NULL)
            continue;
          segs[n] = NULL;
          num_non_null--;
          kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
          seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
          if (seg == NULL)
            continue; // Must be a CEMPTY or DELETED marker
          if (lsbs == 0)
            { 
              assert(num_descendants > 0);
              num_descendants--;
            }
          else
            { 
              assert(num_erasable > 0);
              num_erasable--;
            }
          seg->recycle_all(cache);
        }
    }
  assert(this->num_descendants == 0);
  this->num_erasable = 0;
  this->num_reclaimable_bins = 0;
  this->flags = 0;
  this->container = NULL;
  cache->seg_server->release(this);
}

/*****************************************************************************/
/*                   kd_var_cache_seg::adjust_reclaimability                 */
/*****************************************************************************/

void kd_var_cache_seg::adjust_reclaimability(kd_cache *cache)
{
  kdu_byte stripped_flags = flags & ~(KD_CSEG_RECLAIMABLE_DATA |
                                      KD_CSEG_RECLAIMABLE_SEG);
  kdu_byte new_flags = stripped_flags;
  if (num_reclaimable_bins > 0)
    { 
      assert(flags & KD_CSEG_LEAF);
      new_flags |= KD_CSEG_RECLAIMABLE_DATA;
    }
  else if ((num_descendants == 0) && preserve.is_empty())
    new_flags |= KD_CSEG_RECLAIMABLE_SEG;
  if ((new_flags == flags) && (new_flags == stripped_flags))
    { // Segment is not on any reclaimable list and does not need to be
      return;
    }
  
  // Remove from any reclaim list to which we already belong, unless we
  // are already in the right position on that list.
  if (flags & KD_CSEG_RECLAIMABLE_DATA)
    { // Remove from the reclaimable-data list, unless it is already at the
      // head of that list and `new_flags'=`flags'.
      if (reclaim_prev == NULL)
        { // Should already be at the head of the list; nothing more to do.
          assert(this == cache->reclaimable_data_head);
          if (new_flags == flags)
            { 
              if (access_ctl.get() == 0)
                cache->all_reclaimable_data_locked = false;
              return; // Leave everything the way it is
            }
          cache->reclaimable_data_head = this->reclaim_next;
        }
      else
        reclaim_prev->reclaim_next = this->reclaim_next;
      if (reclaim_next == NULL)
        { 
          assert(this == cache->reclaimable_data_tail);
          cache->reclaimable_data_tail = reclaim_prev;
        }
      else
        reclaim_next->reclaim_prev = this->reclaim_prev;
      reclaim_prev = reclaim_next = NULL;
    }
  else if (flags & KD_CSEG_RECLAIMABLE_SEG)
    { // Remove from the reclaimable-segs list
      flags &= ~KD_CSEG_RECLAIMABLE_SEG;
      if (reclaim_prev == NULL)
        { // Should already be at the head of the list; nothing more to do.
          assert(this == cache->reclaimable_segs_head);
          if (new_flags == flags)
            { // Leave everything the way it is
              if (access_ctl.get() == 0)
                cache->all_reclaimable_segs_locked = false;
              return;
            }
          cache->reclaimable_segs_head = this->reclaim_next;
        }
      else
        reclaim_prev->reclaim_next = this->reclaim_next;
      if (reclaim_next == NULL)
        { 
          assert(this == cache->reclaimable_segs_tail);
          cache->reclaimable_segs_tail = reclaim_prev;
        }
      else
        reclaim_next->reclaim_prev = this->reclaim_prev;
      reclaim_prev = reclaim_next = NULL;      
    }

  // Insert at the head of the relevant list
  if (new_flags & KD_CSEG_RECLAIMABLE_DATA)
    { 
      reclaim_prev = NULL;
      if ((reclaim_next = cache->reclaimable_data_head) != NULL)
        reclaim_next->reclaim_prev = this;
      else
        { 
          assert(cache->reclaimable_data_tail == NULL);
          cache->reclaimable_data_tail = this;
        }
      cache->reclaimable_data_head = this;
      if (access_ctl.get() == 0)
        cache->all_reclaimable_data_locked = false;
    }
  else if (new_flags & KD_CSEG_RECLAIMABLE_SEG)
    { 
      reclaim_prev = NULL;
      if ((reclaim_next = cache->reclaimable_segs_head) != NULL)
        reclaim_next->reclaim_prev = this;
      else
        { 
          assert(cache->reclaimable_segs_tail == NULL);
          cache->reclaimable_segs_tail = this;
        }
      cache->reclaimable_segs_head = this;
      if (access_ctl.get() > 0)
        cache->all_reclaimable_segs_locked = false;
    }
  
  this->flags = new_flags;
}

/*****************************************************************************/
/*                  kd_var_cache_seg::retract_reclaimability                 */
/*****************************************************************************/

void kd_var_cache_seg::retract_reclaimability(kd_cache *cache)
{
  if (flags & KD_CSEG_RECLAIMABLE_DATA)
    { // Remove from the reclaimable-data list
      flags &= ~KD_CSEG_RECLAIMABLE_DATA;
      if (reclaim_prev == NULL)
        { // Should already be at the head of the list; nothing more to do.
          assert(this == cache->reclaimable_data_head);
          cache->reclaimable_data_head = this->reclaim_next;
        }
      else
        reclaim_prev->reclaim_next = this->reclaim_next;
      if (reclaim_next == NULL)
        { 
          assert(this == cache->reclaimable_data_tail);
          cache->reclaimable_data_tail = reclaim_prev;
        }
      else
        reclaim_next->reclaim_prev = this->reclaim_prev;
      reclaim_prev = reclaim_next = NULL;
    }
  else if (flags & KD_CSEG_RECLAIMABLE_SEG)
    { // Remove from the reclaimable-segs list
      flags &= ~KD_CSEG_RECLAIMABLE_SEG;
      if (reclaim_prev == NULL)
        { // Should already be at the head of the list; nothing more to do.
          assert(this == cache->reclaimable_segs_head);
          cache->reclaimable_segs_head = this->reclaim_next;
        }
      else
        reclaim_prev->reclaim_next = this->reclaim_next;
      if (reclaim_next == NULL)
        { 
          assert(this == cache->reclaimable_segs_tail);
          cache->reclaimable_segs_tail = reclaim_prev;
        }
      else
        reclaim_next->reclaim_prev = this->reclaim_prev;
      reclaim_prev = reclaim_next = NULL;      
    }
}

/*****************************************************************************/
/*                       kd_var_cache_seg::lock_failed                       */
/*****************************************************************************/

void kd_var_cache_seg::lock_failed(kd_cache *cache, bool &mutex_locked)
{
  // Start out by acquiring the mutex
  if (!mutex_locked)
    { 
      cache->mutex.lock();
      mutex_locked = true;
    }
  else
    assert(0); // If mutex was locked, lock failure should have been impossible
  kd_var_stream_info *stream_container = NULL;
  if ((stream_id >= 0) && (container != NULL))
    { 
      kd_var_cache_seg *seg;
      for (seg=this; seg != NULL; seg=seg->container)
        if (seg->flags & KD_CSEG_STREAM_ROOT)
          { 
            stream_container = &(seg->stream);
            break;
          }
      assert(stream_container != NULL);
    }
  unlock(cache,mutex_locked,stream_container);
}

/*****************************************************************************/
/*                         kd_var_cache_seg::unlock                          */
/*****************************************************************************/

void kd_var_cache_seg::unlock(kd_cache *cache, bool &mutex_locked,
                              kd_var_stream_info *stream_container)
{
  if (!mutex_locked)
    { // Need to be careful to ensure that the access lock does not go to 0
      // without first acquiring a lock on the mutex.
      kdu_int32 old_val;
      do { // Enter compare-and-swap loop
        old_val = access_ctl.get();
        if (old_val <= 1)
          break;
      } while (!access_ctl.compare_and_set(old_val,old_val-1));
      if (old_val > 1)
        return;
      
      // If we get here, we have not yet reduced the `access_ctl' value, but
      // it is not safe to do so without first acquring the mutex.
      cache->mutex.lock();
      mutex_locked = true;
    }

  // If we get here, we own the mutex, so it is safe to just atomically
  // decrement `access_ctl' and check the result.
  kdu_int32 old_val = access_ctl.exchange_add(-1);
  assert(old_val > 0);
  if (old_val > 1)
    return; // Somebody else will perform the duties of an unlocker, once the
            // count actually goes to 0.
  
  // Start by erasing any erasables; if we collapse any marked databins
  // or cache-segs into deletion marks within their containers, we need to
  // adjust the relevant `mark_counts' values within the `stream_container',
  // if there is one.
  if (num_erasable)
    { 
      if (flags & KD_CSEG_LEAF)
        { 
          kd_cint delta_mark_count = 0;
          for (int b=0; b < 128; b++)
            { 
              kd_cache_buf *buf_list = databins[b];
              kdu_int32 lsbs = _addr_to_kdu_int32(buf_list) & 3;
              if (!lsbs)
                continue; // Not erasable
              buf_list = (kd_cache_buf *)(((kdu_byte *)buf_list)-lsbs);
              if (buf_list == NULL)
                continue; // Must be a CEMPTY or DELETED marker
              kdu_int32 status = buf_list->head.status.get();
              if ((status & (KD_CACHE_HD_F_BIT |
                             KD_CACHE_HD_L_MASK)) == KD_CACHE_HD_F_BIT)
                databins[b] = KD_BIN_CEMPTY; // Should have happened earlier
              else if ((status & KD_CACHE_HD_M_MASK) == KD_CACHE_HD_M_DELETED)
                databins[b] = KD_BIN_DELETED;
              else
                { // The app may have dealt with any deleted condition already
                  databins[b] = NULL;
                  assert(num_non_null > 0);
                  num_non_null--;
                  if (status & KD_CACHE_HD_M_MASK)
                    delta_mark_count--; // Losing the +ve marking conditions
                }
              kd_cache_buf *holes = buf_list->head.hole_list;
              if (holes != NULL)
                { 
                  buf_list->head.hole_list = NULL;
                  cache->buf_server->release(holes);
                }
              cache->buf_server->release(buf_list);
              num_erasable--;
              if (num_erasable == 0)
                break; // No need to keep searching
            }
          if (delta_mark_count != 0)
            { 
              assert((stream_container != NULL) &&
                     (class_id < KDU_NUM_DATABIN_CLASSES));
              stream_container->mark_counts[class_id] += delta_mark_count;
              assert(stream_container->mark_counts[class_id] >= 0);
            }
        }
      else if (flags & KD_CSEG_STREAM_ROOT)
        { 
          assert((stream_id >= 0) && (class_id == 0xFF));
          assert(stream_container == &(this->stream));
          for (int n=0; n < KDU_NUM_DATABIN_CLASSES; n++)
            { 
              kd_var_cache_seg *seg = stream.classes[n];
              kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
              if (!lsbs)
                continue; // Not erasable
              seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
              if (seg == NULL)
                continue; // Must be a DELETED marker
              assert(seg->num_descendants == 0);
              int num_collapsed_deletes=0;
              if (seg->num_non_null > 0)
                { 
                  if (seg->flags & KD_CSEG_LEAF)
                    { 
                      for (int b=0; b < 128; b++)
                        if (seg->databins[b] == KD_BIN_DELETED)
                          num_collapsed_deletes++;
                    }
                  else
                    { 
                      for (int s=0; s < 128; s++)
                        if (seg->segs[s] == KD_SEG_DELETED)
                          num_collapsed_deletes++;
                    }
                }
              if (num_collapsed_deletes > 0)
                { 
                  stream.classes[n] = KD_SEG_DELETED;
                  stream.mark_counts[n] -= num_collapsed_deletes;
                  if (seg->flags & KD_CSEG_CONTAINER_DELETED)
                    stream.mark_counts[n]--; // Losing container-deleted mark
                  stream.mark_counts[n]++; // Gaining the SEG_DELETED mark
                  assert(stream.mark_counts[n] == 1); // Just the SEG_DELETED
                                // mark remains for this whole data-bin class.
                }
              else if (seg->flags & KD_CSEG_CONTAINER_DELETED)
                { // We must be deleting a class-root which could not be
                  // prepended with a broader root (encompassing more
                  // data-bins) due to a memory allocation failure.
                  assert(this->flags & KD_CSEG_STREAM_ROOT);
                  stream.classes[n] = KD_SEG_DELETED;
                  assert(stream.mark_counts[n] == 1); // We have just lost the
                    // container-deleted mark and gained the `KD_SEG_DELETED'
                    // mark.
                }
              else
                { 
                  stream.classes[n] = NULL;
                  assert(num_non_null > 0);
                  num_non_null--;
                  assert(stream.mark_counts[n] == 0);
                }
              seg->container = NULL;
              cache->seg_server->release(seg);
              num_erasable--;
              if (num_erasable == 0)
                break; // No need to keep searching
            }
        }
      else
        { 
          kd_cint delta_mark_count = 0;
          for (int n=0; n < 128; n++)
            { 
              kd_var_cache_seg *seg = segs[n];
              kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
              if (!lsbs)
                continue; // Not erasable
              seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
              if (seg == NULL)
                continue; // Must be a DELETED marker
              assert(seg->num_descendants == 0);
              int num_collapsed_deletes=0;
              if (seg->num_non_null > 0)
                { 
                  if (seg->flags & KD_CSEG_LEAF)
                    { 
                      for (int b=0; b < 128; b++)
                        if (seg->databins[b] == KD_BIN_DELETED)
                          num_collapsed_deletes++;
                    }
                  else if (seg->flags & KD_CSEG_STREAM_ROOT)
                    { 
                      for (int c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
                        if (seg->segs[c] == KD_SEG_DELETED)
                          num_collapsed_deletes++;
                    }                    
                  else
                    { 
                      for (int s=0; s < 128; s++)
                        if (seg->segs[s] == KD_SEG_DELETED)
                          num_collapsed_deletes++;
                    }
                }
              assert(!(seg->flags & KD_CSEG_CONTAINER_DELETED)); // This
                // flag can only be set in a class root, which means that we
                // must be a stream-root, which would have been caught above.
              if (num_collapsed_deletes > 0)
                { 
                  segs[n] = KD_SEG_DELETED;
                  delta_mark_count -= num_collapsed_deletes;
                  delta_mark_count++; // Gaining the SEG_DELETED mark
                }
              else
                { 
                  segs[n] = NULL;
                  assert(num_non_null > 0);
                  num_non_null--;
                }
              seg->container = NULL;
              cache->seg_server->release(seg);
              num_erasable--;
              if (num_erasable == 0)
                break; // No need to keep searching
            }
          if ((delta_mark_count != 0) && (stream_container != NULL))
            { 
              assert(class_id < KDU_NUM_DATABIN_CLASSES);
              stream_container->mark_counts[class_id] += delta_mark_count;
              assert(stream_container->mark_counts[class_id] >= 0);
            }
        }
    }
  
  // Now see about reclaimability
  if (num_reclaimable_bins > 0)
    { // Put ourselves in the MRU position of the reclaimable-data list
      adjust_reclaimability(cache);
      return;
    }
  if ((num_descendants > 0) || (!preserve.is_empty()) || (container == NULL))
    { // Cache-seg itself neither is nor was reclaimable
      assert(!(flags & KD_CSEG_RECLAIMABLE_SEG));
      return;
    }
  
  // If we get here, the cache-seg itself is at least reclaimable
  if (num_non_null > 0)
    { // Do not make it erasable right away; just move it to the MRU position
      // on the reclaimable-segs list
      adjust_reclaimability(cache);
      return;
    }
  
  // If we get here, the cache-seg can be made erasable right away.
  this->make_erasable(cache,mutex_locked,stream_container);
}

/*****************************************************************************/
/*                     kd_var_cache_seg::make_erasable                       */
/*****************************************************************************/

void kd_var_cache_seg::make_erasable(kd_cache *cache, bool &mutex_locked,
                                     kd_var_stream_info *stream_container)
{
  retract_reclaimability(cache); // In case we were reclaimable before
  int idx = pos_in_container;
  if (container->segs[idx] != this)
    { // Must have already been made erasable (see notes in header file)
      assert((_addr_to_kdu_int32(container->segs[idx]) & 3) == 1);
      return;
    }
  container->segs[idx] = (kd_var_cache_seg *)(((kdu_byte *)this) + 1);
  assert(container->num_descendants > 0);
  container->num_descendants--;
  container->num_erasable++;
  assert(this != cache->root);
  container->access_ctl.exchange_add(1); // Temporarily make ourselves a locker
  if (container->stream_id < 0)
    stream_container = NULL;
  container->unlock(cache,mutex_locked,stream_container); // May be recursive
}

/*****************************************************************************/
/*                     kd_var_cache_seg::set_all_marks                       */
/*****************************************************************************/

void kd_var_cache_seg::set_all_marks(kd_cache *cache, bool &mutex_locked,
                                     bool leave_marked, bool was_erasable,
                                     kd_var_stream_info *stream_container)
{
  assert(mutex_locked);
  if (num_non_null == 0)
    return;
  kdu_int32 m_val = (leave_marked)?KD_CACHE_HD_M_MARKED:0;
  flags &= ~KD_CSEG_CONTAINER_DELETED;
  kd_cint delta_mark_count = 0;
  kd_cint m_inc = (leave_marked)?1:0;
  if (flags & KD_CSEG_LEAF)
    { 
      for (int b=0; b < 128; b++)
        { 
          kd_cache_buf *buf_list = databins[b];
          if (buf_list == KD_BIN_DELETED)
            { 
              databins[b] = NULL;
              assert(num_non_null > 0);
              num_non_null--;
              delta_mark_count--;
            }
          else if ((buf_list != NULL) && (buf_list != KD_BIN_CEMPTY))
            { 
              kdu_int32 lsbs = _addr_to_kdu_int32(buf_list) & 3;
              buf_list = (kd_cache_buf *)(((kdu_byte *)buf_list)-lsbs);
              kdu_int32 status = buf_list->head.status.get();
              if (status & KD_CACHE_HD_M_MASK)
                delta_mark_count--;
              status &= ~KD_CACHE_HD_M_MASK;
              status |= m_val;
              delta_mark_count += m_inc;
              buf_list->head.status.set(status);
            }
        }
    }
  else if (flags & KD_CSEG_STREAM_ROOT)
    { 
      stream_container = &(this->stream);
      assert((stream_id >= 0) && (class_id == 0xFF));
      for (int c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
        { 
          kd_var_cache_seg *seg = stream.classes[c];
          if (seg == KD_SEG_DELETED)
            { 
              stream.classes[c] = NULL;
              assert(num_non_null > 0);
              num_non_null--;
              delta_mark_count--;
            }
          else if (seg != NULL)
            { 
              kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
              bool seg_was_erasable = was_erasable;
              if (lsbs)
                { 
                  seg_was_erasable = true;
                  seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
                }
              else
                assert(!was_erasable);
              seg->set_all_marks(cache,mutex_locked,leave_marked,
                                 seg_was_erasable,stream_container);
            }
        }  
    }
  else
    { 
      for (int s=0; s < 128; s++)
        { 
          kd_var_cache_seg *seg = segs[s];
          if (seg == KD_SEG_DELETED)
            { 
              segs[s] = NULL;
              assert(num_non_null > 0);
              num_non_null--;
              delta_mark_count--;
            }
          else if (seg != NULL)
            { 
              kdu_int32 lsbs = _addr_to_kdu_int32(seg) & 3;
              bool seg_was_erasable = was_erasable;
              if (lsbs)
                { 
                  seg_was_erasable = true;
                  seg = (kd_var_cache_seg *)(((kdu_byte *)seg)-lsbs);
                }
              else
                assert(!was_erasable);
              seg->set_all_marks(cache,mutex_locked,leave_marked,
                                 seg_was_erasable,stream_container);
            }
        }
    }
  
  if ((delta_mark_count != 0) && (stream_container != NULL))
    { 
      assert(class_id < KDU_NUM_DATABIN_CLASSES);
      stream_container->mark_counts[class_id] += delta_mark_count;
      assert(stream_container->mark_counts[class_id] >= 0);
    }
  
  // See if the cache-seg can immediately become erasable
  if ((!was_erasable) &&
      (num_descendants == 0) && (num_non_null == 0) && preserve.is_empty() &&
      (container != NULL))
    this->make_erasable(cache,mutex_locked,stream_container);
}


/* ========================================================================= */
/*                           kd_cache_path_walker                            */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_cache_path_walker::make_path                      */
/*****************************************************************************/

kd_var_cache_seg *
  kd_cache_path_walker::make_path(kd_cache *cache, bool &mutex_locked,
                                  int cls, kdu_long stream_id,
                                  kdu_long bin_id, bool force_preserve)
  /* NB: While the code below seems highly complex, most branches are never
     taken.  A lot of the code exists to handle situations such as
     insufficient memory, or building a path into previously deleted segs
     or segs marked for erasure, in a totally robust manner. */
{
  assert((cls >= 0) && (cls < KDU_NUM_DATABIN_CLASSES));
  assert(cls != KDU_TILE_HEADER_DATABIN); // Caller should have converted these
                                  // to main header data-bins with `bin_id'+1
  
  // Start by backtracking to a point within the existing `path' that
  // contains the data-bin we are seeking; may leave us with an empty path.
  kd_var_cache_seg *seg = NULL;
  while (path_len > 0)
    { 
      seg = path[path_len-1];
      if (seg->stream_id < 0)
        { // `seg' is a "stream-nav" segment
          kdu_long off = stream_id - seg->base_id;
          if ((off >= 0) && ((off >> seg->shift) < 128))
            break; // The data-bin we seek is descended from `seg'
        }
      else if (seg->stream_id == stream_id)
        { // If not, we definitely need to back-track
          if (seg->class_id == (kdu_byte) cls)
            { // "class-nav" segment that belongs to the right class
              kdu_long off = bin_id - seg->base_id;
              if ((off >= 0) && ((off >> seg->shift) < 128))
                break; // The data-bin we seek is descended from `seg'              
            }
          else if (seg->class_id == 255)
            break; // "stream-root" segment for the right codestream
        }
      this->unwind(cache,mutex_locked);
      seg = NULL;
    }
  
  if (!mutex_locked)
    { // We need to lock the mutex anyway, in all contexts where this function
      // is called; doing so here makes the rest of this function easier.
      cache->mutex.lock();
      mutex_locked = true;
    }
  
  /* Note on reclaimability:
       In the code below, we only need to invoke `adjust_reclaimability' on
     cache-segs whose descendants are modified in some way, but which are not
     locked here.  When the last access lock on a segment is removed, its
     `adjust_reclaimability' function will be called anyway, and there will
     be no attempt to reclaim data or cache-segs while access locks are held,
     so we only need to make the `KD_CSEG_RECLAIMABLE_DATA' and
     `KD_CSEG_RECLAIMABLE_SEG' flags agree with the other state variables
     at that point.  This helps simplify things in the code below. */

  // Insert stream-nav segs ahead of the `cache->root' if necessary
  if (seg == NULL)
    { // `seg' is invalid; we will need to start from the root
      if ((seg = cache->root) == NULL)
        { 
          seg = cache->seg_server->get();
          if (seg == NULL)
            return NULL;
          seg->stream_id = -1;
          seg->base_id = 0;
          seg->class_id = 255;
          cache->atomic_root.barrier_set(seg);
        }
      assert((seg->stream_id<0) && (seg->class_id==255) && (seg->base_id==0));
      while ((stream_id >> seg->shift) > 127)
        { // Insert "stream-nav" segs ahead of existing root, but don't
          // acquire access locks or add to path yet.
          seg = cache->seg_server->get();
          if (seg == NULL)
            { // We have run out of memory!  Not a disaster; we just set the
              // special `KD_CSEG_CONTAINER_DELETED' flag within the root to
              // let the application know that it must consider all data-bins
              // that would have belonged to the segment we cannot create as
              // if they had been deleted.
              cache->root->flags |= KD_CSEG_CONTAINER_DELETED;
              return NULL;
            }
          seg->stream_id = -1;
          seg->base_id = 0;
          seg->class_id = 255;
          seg->shift = cache->root->shift + 7;
          seg->num_descendants = 1;
          seg->num_non_null = 1;
          if (!cache->root->preserve.is_empty())
            seg->preserve.set(0);
          kd_var_cache_seg *old_root = cache->root;
          seg->segs[0] = old_root;
          old_root->container = seg;
          old_root->pos_in_container = 0;
          if (old_root->flags & KD_CSEG_CONTAINER_DELETED)
            { // A previous attempt to insert this segment failed (no memory)
              // so we need to preserve the deletion information.
              old_root->flags &= ~KD_CSEG_CONTAINER_DELETED;
              seg->flags |= KD_CSEG_CONTAINER_DELETED;
              for (int n=1; n < 128; n++)
                seg->segs[n] = KD_SEG_DELETED;
              seg->num_non_null = 128;
            }
          cache->atomic_root.barrier_set(seg);
          old_root->adjust_reclaimability(cache); // We have no access lock
            // to `old_root' and it might not have been considered reclaimable
            // before, since the global root of the cache hierarchy is never
            // reclaimable.  Calling this function is safe, since we have the
            // mutex lock.
        }
      seg->access_ctl.exchange_add(1);
      add_to_path(seg);
    }
  
  // Build forward to the "stream-root" if not already there
  bool mark_deleted=false;
  while (seg->stream_id < 0)
    { // `seg' is a "stream-nav" segment
      assert(this->stream_info == NULL);
      assert(seg->class_id == 255);
      kdu_long off = (stream_id - seg->base_id) >> seg->shift;
      assert((off >= 0) && (off < 128));
      int idx = (int)off;
      kd_var_cache_seg *nxt_seg = seg->segs[idx];
      if (nxt_seg == KD_SEG_DELETED)
        { // Completely remove the deleted status in preparation for building
          nxt_seg = seg->segs[idx] = NULL;
          assert(seg->num_non_null > 0);
          seg->num_non_null--;
          mark_deleted = true;
        }
      else if (_addr_to_kdu_int32(nxt_seg) & 1)
        { // segment exists, but is erasure-marked; remove the erasure marking
          nxt_seg = (kd_var_cache_seg *)(((kdu_byte *) nxt_seg) - 1);
          assert(seg->num_erasable > 0);
          seg->num_erasable--;
        }
      if (nxt_seg == NULL)
        { // Create `nxt_seg'
          nxt_seg = cache->seg_server->get();
          if (nxt_seg == NULL)
            { // We ran out of memory; not a disaster.  We just return NULL
              // after making the failed segment appear to have been deleted.
              seg->segs[idx] = KD_SEG_DELETED;
              seg->num_non_null++;
              return NULL;
            }
          if (seg->shift >= 7)
            { // Creating another "stream-nav" segment
              nxt_seg->stream_id = -1;
              nxt_seg->base_id = seg->base_id + (off << seg->shift);
              nxt_seg->class_id = 255;
              nxt_seg->shift = seg->shift-7;
              if (mark_deleted)
                { // Need to mark all elements as having been deleted, since we
                  // have encountered `KD_SEG_DELETED' while building the path.
                  for (int s=0; s<128; s++)
                    nxt_seg->segs[s] = KD_SEG_DELETED;
                  nxt_seg->num_non_null = 128;
                }
            }
          else
            { // Creating the "stream-root" seg itself
              assert(seg->shift == 0);
              nxt_seg->stream_id = stream_id;
              nxt_seg->class_id = 255;
              nxt_seg->flags |= KD_CSEG_STREAM_ROOT;
              if (mark_deleted)
                { // Need to mark all elements as having been deleted, since we
                  // have encountered `KD_SEG_DELETED' while building the path.
                  for (int c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
                    { 
                      nxt_seg->stream.classes[c] = KD_SEG_DELETED;
                      nxt_seg->stream.mark_counts[c] = 1;
                    }
                  nxt_seg->num_non_null = KDU_NUM_DATABIN_CLASSES;
                }
            }
          nxt_seg->container = seg;
          nxt_seg->pos_in_container = idx;
        }
      assert(nxt_seg != cache->root);
      nxt_seg->access_ctl.exchange_add(1);
      if (nxt_seg != seg->segs[idx])
        { 
          if (seg->segs[idx] == NULL)
            seg->num_non_null++;
          seg->num_descendants++;
          seg->elts[idx].barrier_set(nxt_seg);
        }
      add_to_path(nxt_seg);
      seg = nxt_seg;
    }
  
  // Build onto the stream-root if we are there, inserting new class roots
  // ahead of existing ones if required
  assert(seg->stream_id == stream_id);
  if (seg->class_id == 255)
    { 
      assert(seg->flags & KD_CSEG_STREAM_ROOT);
      this->stream_info = &(seg->stream);
      kd_var_cache_seg *cls_root = stream_info->classes[cls];
      if (cls_root == KD_SEG_DELETED)
        { // Remove the deleted status in preparation for building
          cls_root = stream_info->classes[cls] = NULL;
          assert(stream_info->mark_counts[cls] == 1);
          stream_info->mark_counts[cls] = 0;
          assert(seg->num_non_null > 0);
          seg->num_non_null--;
          mark_deleted = true;
        }
      else if (_addr_to_kdu_int32(cls_root) & 1)
        { // segment exists, but is erasure-marked; remove erasure marking
          cls_root = (kd_var_cache_seg *)(((kdu_byte *) cls_root) - 1);
          assert(seg->num_erasable > 0);
          seg->num_erasable--;
        }
      if (cls_root == NULL)
        { // Need to create the initial class root
          assert(stream_info->mark_counts[cls] == 0);
          kd_var_cache_seg *new_root = cache->seg_server->get();
          if (new_root == NULL)
            { // We ran out of memory; not a disaster.  We just return NULL
              // after making the failed segment appear to have been deleted.
              stream_info->classes[cls] = KD_SEG_DELETED;
              seg->num_non_null++;
              stream_info->mark_counts[cls] = 1;
              return NULL;
            }
          new_root->stream_id = stream_id;
          new_root->base_id = 0;
          new_root->class_id = (kdu_byte)cls;
          new_root->flags = KD_CSEG_LEAF;
          new_root->container = seg;
          new_root->pos_in_container = (kdu_byte)cls;
          if (mark_deleted)
            { // Need to mark all elements as having been deleted, since we
              // have encountered `KD_SEG_DELETED' while building the path.
              for (int s=0; s<128; s++)
                new_root->segs[s] = KD_SEG_DELETED;
              stream_info->mark_counts[cls] = 128;
              new_root->num_non_null = 128;
            }
          cls_root = new_root;
        }
      if (cls_root != stream_info->classes[cls])
        { 
          if (stream_info->classes[cls] == NULL)
            seg->num_non_null++;
          seg->num_descendants++;
          seg->elts[cls].barrier_set(cls_root);
          assert(cls_root == stream_info->classes[cls]);
        }
      
      while ((bin_id >> cls_root->shift) > 127)
        { // Insert a new class root before the existing one
          kd_var_cache_seg *new_root = cache->seg_server->get();
          if (new_root == NULL)
            { // We have run out of memory!  Not a disaster; we just set the
              // special `KD_CSEG_CONTAINER_DELETED' flag within the root to
              // let the application know that it must consider all data-bins
              // that would have belonged to the segment we cannot create as
              // if they had been deleted.
              if (!(cls_root->flags & KD_CSEG_CONTAINER_DELETED))
                stream_info->mark_counts[cls]++; // Did not have this type of
                                                 // mark beforehand
              cls_root->flags |= KD_CSEG_CONTAINER_DELETED;
              return NULL;
            }
          new_root->stream_id = stream_id;
          new_root->base_id = 0;
          new_root->class_id = (kdu_byte)cls;
          new_root->shift = cls_root->shift+7;
          new_root->container = seg;
          new_root->pos_in_container = (kdu_byte)cls;
          new_root->num_descendants = 1;
          new_root->num_non_null = 1;
          if (!cls_root->preserve.is_empty())
            new_root->preserve.set(0);
          new_root->segs[0] = cls_root;
          cls_root->container = new_root;
          cls_root->pos_in_container = 0;
          if (cls_root->flags & KD_CSEG_CONTAINER_DELETED)
            { // A previous attempt to insert this segment failed
              // so we need to preserve the deletion information.
              cls_root->flags &= ~KD_CSEG_CONTAINER_DELETED;                
              new_root->flags |= KD_CSEG_CONTAINER_DELETED;
            }
          if (mark_deleted || (new_root->flags & KD_CSEG_CONTAINER_DELETED))
            { // Mark all the extra slots in `new_root' as deleted
              for (int s=1; s<128; s++)
                new_root->segs[s] = KD_SEG_DELETED;
              stream_info->mark_counts[cls] += 127;
              new_root->num_non_null = 128;
            }
          cls_root = new_root;
          seg->elts[cls].barrier_set(cls_root);
        }
      assert(cls_root != cache->root);
      cls_root->access_ctl.exchange_add(1);
      add_to_path(cls_root);
      seg = cls_root;
    }
  
  // Build forwards to the leaf-seg of interest, if not already there
  assert(seg->class_id == (kdu_byte)cls);
  assert(stream_info != NULL);
  while (seg->shift >= 7)
    { 
      kdu_long off = (bin_id - seg->base_id) >> seg->shift;
      assert((off >= 0) && (off < 128));
      int idx = (int)off;
      kd_var_cache_seg *nxt_seg = seg->segs[idx];
      if (nxt_seg == KD_SEG_DELETED)
        { // Remove the deleted status in preparation for building forward
          nxt_seg = seg->segs[idx] = NULL;
          assert(stream_info->mark_counts[cls] > 0);
          stream_info->mark_counts[cls]--;
          assert(seg->num_non_null > 0);
          seg->num_non_null--;
          mark_deleted = true;
        }
      else if (_addr_to_kdu_int32(nxt_seg) & 1)
        { // segment exists, but is erasure-marked; remove the erasure marking
          nxt_seg = (kd_var_cache_seg *)(((kdu_byte *) nxt_seg) - 1);
          assert(seg->num_erasable > 0);
          seg->num_erasable--;
        }
      if (nxt_seg == NULL)
        { // Create `nxt_seg'
          nxt_seg = cache->seg_server->get();
          if (nxt_seg == NULL)
            { // We ran out of memory; not a disaster.  We just return NULL
              // after making the failed segment appear to have been deleted.
              seg->segs[idx] = KD_SEG_DELETED;
              stream_info->mark_counts[cls]++;
              seg->num_non_null++;
              return NULL;
            }
          nxt_seg->stream_id = stream_id;
          nxt_seg->base_id = seg->base_id + (off << seg->shift);
          nxt_seg->class_id = (kdu_byte)cls;
          nxt_seg->shift = seg->shift-7;
          nxt_seg->container = seg;
          nxt_seg->pos_in_container = (kdu_byte)idx;
          if (mark_deleted)
            { // Need to mark all elements as having been deleted, since we
              // have encountered `KD_SEG_DELETED' while building the path.
              for (int s=0; s < 128; s++)
                nxt_seg->segs[s]=KD_SEG_DELETED; // Same addr as KD_BIN_DELETED
              stream_info->mark_counts[cls] += 128;
              nxt_seg->num_non_null = 128;
            }
          if (nxt_seg->shift == 0)
            nxt_seg->flags |= KD_CSEG_LEAF;
        }
      assert(nxt_seg != cache->root);
      nxt_seg->access_ctl.exchange_add(1);
      if (nxt_seg != seg->segs[idx])
        { 
          if (seg->segs[idx] == NULL)
            seg->num_non_null++;
          seg->num_descendants++;
          seg->elts[idx].barrier_set(nxt_seg);
        }
      add_to_path(nxt_seg);
      seg = nxt_seg;
    }
  assert(seg->shift == 0);
  
  // Make sure the databin that we seek is not marked as erasable.
  int idx = (int)(bin_id - seg->base_id);
  assert((idx >= 0) && (idx < 128));
  kd_cache_buf *buf_list = seg->databins[idx];
  kdu_int32 buf_lsbs = _addr_to_kdu_int32(buf_list) & 3;
  if (buf_lsbs)
    { 
      buf_list = (kd_cache_buf *)(((kdu_byte *) buf_list) - buf_lsbs);
      if (buf_list != NULL)
        { // Buffer list exists, but is marked for erasure
          assert(seg->num_erasable > 0);
          seg->num_erasable--;
          seg->num_descendants++;
          if (!seg->preserve.get(idx))
            seg->num_reclaimable_bins++;
          seg->elts[idx].barrier_set(buf_list);
        }
    }
  
  // Apply preservation flags if `force_preserve' is true.
  if (force_preserve && !seg->preserve.get(idx))
    { 
      seg->preserve.set(idx);
      if (buf_list != NULL)
        { // No longer a reclaimable data-bin
          assert(seg->num_reclaimable_bins > 0);
          seg->num_reclaimable_bins--;
        }
      kd_var_cache_seg *scan = seg->container;
      idx = seg->pos_in_container;
      while ((scan != NULL) && !scan->preserve.get(idx))
        { 
          scan->preserve.set(idx);
          idx = scan->pos_in_container;
          scan = scan->container;
        }
    }
  
  return seg;
}

/*****************************************************************************/
/*                     kd_cache_path_walker::make_stream                     */
/*****************************************************************************/

kd_var_stream_info *
  kd_cache_path_walker::make_stream(kd_cache *cache, bool &mutex_locked,
                                    kdu_long stream_id)
{
  // Start by backtracking to a point within the existing `path' that includes
  // the required codestream.
  kd_var_cache_seg *seg = NULL;
  while (path_len > 0)
    { 
      seg = path[path_len-1];
      if (seg->stream_id == stream_id)
        { 
          assert(this->stream_info != NULL);
          return stream_info;
        }
      if (seg->stream_id < 0)
        { // `seg' is a "stream-nav" segment
          kdu_long off = stream_id - seg->base_id;
          if ((off >= 0) && ((off >> seg->shift) < 128))
            break; // The stream we seek is descended from `seg'
        }
      this->unwind(cache,mutex_locked);
      seg = NULL;
    }
  assert(this->stream_info == NULL);
  if (!mutex_locked)
    { // We need to lock the mutex in most contexts where this function
      // is called; doing so here makes the rest of this function easier.
      cache->mutex.lock();
      mutex_locked = true;
    }

  // Insert stream-nav segs ahead of the `cache->root' if necessary
  if (seg == NULL)
    { // `seg' is invalid; we will need to start from the root
      if ((seg = cache->root) == NULL)
        { 
          seg = cache->seg_server->get();
          if (seg == NULL)
            return NULL;
          seg->stream_id = -1;
          seg->base_id = 0;
          seg->class_id = 255;
          cache->atomic_root.barrier_set(seg);
        }
      assert((seg->stream_id<0) && (seg->class_id==255) && (seg->base_id==0));
      while ((stream_id >> seg->shift) > 127)
        { // Insert "stream-nav" segs ahead of existing root, but don't
          // acquire access locks or add to path yet.
          seg = cache->seg_server->get();
          if (seg == NULL)
            { // We have run out of memory!  Not a disaster; we just set the
              // special `KD_CSEG_CONTAINER_DELETED' flag within the root to
              // let the application know that it must consider all data-bins
              // that would have belonged to the segment we cannot create as
              // if they had been deleted.
              cache->root->flags |= KD_CSEG_CONTAINER_DELETED;
              return NULL;
            }
          seg->stream_id = -1;
          seg->base_id = 0;
          seg->class_id = 255;
          seg->shift = cache->root->shift + 7;
          seg->num_descendants = 1;
          seg->num_non_null = 1;
          if (!cache->root->preserve.is_empty())
            seg->preserve.set(0);
          kd_var_cache_seg *old_root = cache->root;
          seg->segs[0] = old_root;
          old_root->container = seg;
          old_root->pos_in_container = 0;
          if (old_root->flags & KD_CSEG_CONTAINER_DELETED)
            { // A previous attempt to insert this segment failed (no memory)
              // so we need to preserve the deletion information.
              old_root->flags &= ~KD_CSEG_CONTAINER_DELETED;
              seg->flags |= KD_CSEG_CONTAINER_DELETED;
              for (int n=1; n < 128; n++)
                seg->segs[n] = KD_SEG_DELETED;
              seg->num_non_null = 128;
            }
          cache->atomic_root.barrier_set(seg);
          old_root->adjust_reclaimability(cache); // Explained in `make_path'
        }
      seg->access_ctl.exchange_add(1);
      add_to_path(seg);
    }
  
  // Build forward to the "stream-root" if not already there
  bool mark_deleted=false;
  while (seg->stream_id < 0)
    { // `seg' is a "stream-nav" segment
      assert(this->stream_info == NULL);
      assert(seg->class_id == 255);
      kdu_long off = (stream_id - seg->base_id) >> seg->shift;
      assert((off >= 0) && (off < 128));
      int idx = (int)off;
      kd_var_cache_seg *nxt_seg = seg->segs[idx];
      if (nxt_seg == KD_SEG_DELETED)
        { // Completely remove the deleted status in preparation for building
          nxt_seg = seg->segs[idx] = NULL;
          assert(seg->num_non_null > 0);
          seg->num_non_null--;
          mark_deleted = true;
        }
      else if (_addr_to_kdu_int32(nxt_seg) & 1)
        { // segment exists, but is erasure-marked; remove the erasure marking
          nxt_seg = (kd_var_cache_seg *)(((kdu_byte *) nxt_seg) - 1);
          assert(seg->num_erasable > 0);
          seg->num_erasable--;
        }
      if (nxt_seg == NULL)
        { // Create `nxt_seg'
          nxt_seg = cache->seg_server->get();
          if (nxt_seg == NULL)
            { // We ran out of memory; not a disaster.  We just return NULL
              // after making the failed segment appear to have been deleted.
              seg->segs[idx] = KD_SEG_DELETED;
              seg->num_non_null++;
              return NULL;
            }
          if (seg->shift >= 7)
            { // Creating another "stream-nav" segment
              nxt_seg->stream_id = -1;
              nxt_seg->base_id = seg->base_id + (off << seg->shift);
              nxt_seg->class_id = 255;
              nxt_seg->shift = seg->shift-7;
              if (mark_deleted)
                { // Need to mark all elements as having been deleted, since we
                  // have encountered `KD_SEG_DELETED' while building the path.
                  for (int s=0; s<128; s++)
                    nxt_seg->segs[s] = KD_SEG_DELETED;
                  nxt_seg->num_non_null = 128;
                }
            }
          else
            { // Creating the "stream-root" seg itself
              assert(seg->shift == 0);
              nxt_seg->stream_id = stream_id;
              nxt_seg->class_id = 255;
              nxt_seg->flags |= KD_CSEG_STREAM_ROOT;
              if (mark_deleted)
                { // Need to mark all elements as having been deleted, since we
                  // have encountered `KD_SEG_DELETED' while building the path.
                  for (int c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
                    { 
                      nxt_seg->stream.classes[c] = KD_SEG_DELETED;
                      nxt_seg->stream.mark_counts[c] = 1;
                    }
                  nxt_seg->num_non_null = KDU_NUM_DATABIN_CLASSES;
                }
            }
          nxt_seg->container = seg;
          nxt_seg->pos_in_container = idx;
        }
      assert(nxt_seg != cache->root);
      nxt_seg->access_ctl.exchange_add(1);
      if (nxt_seg != seg->segs[idx])
        { 
          if (seg->segs[idx] == NULL)
            seg->num_non_null++;
          seg->num_descendants++;
          seg->elts[idx].barrier_set(nxt_seg);
        }
      add_to_path(nxt_seg);
      seg = nxt_seg;
    }

  assert(seg->stream_id == stream_id);
  this->stream_info = &(seg->stream);
  return stream_info;
}

/*****************************************************************************/
/*                     kd_cache_path_walker::trace_path                      */
/*****************************************************************************/

kd_var_cache_seg *
  kd_cache_path_walker::trace_path(kd_cache *cache, bool &mutex_locked,
                                   int cls, kdu_long stream_id,
                                   kdu_long bin_id)
{
  assert((cls >= 0) && (cls < KDU_NUM_DATABIN_CLASSES));
  assert(cls != KDU_TILE_HEADER_DATABIN); // Caller should have converted these
                                  // to main header data-bins with `bin_id'+1
  
  // Start by backtracking to a point within the existing `path' that
  // contains the data-bin we are seeking.  This may leave us with an empty
  // path.
  kd_var_cache_seg *seg = NULL;
  while (path_len > 0)
    { 
      seg = path[path_len-1];
      if (seg->stream_id < 0)
        { // `seg' is a "stream-nav" segment
          kdu_long off = stream_id - seg->base_id;
          if ((off >= 0) && ((off >> seg->shift) < 128))
            break; // The data-bin we seek is descended from `seg'
        }
      else if (seg->stream_id == stream_id)
        { // If not, we definitely need to back-track
          if (seg->class_id == (kdu_byte) cls)
            { // "class-nav" segment that belongs to the right class
              kdu_long off = bin_id - seg->base_id;
              if ((off >= 0) && ((off >> seg->shift) < 128))
                break; // The data-bin we seek is descended from `seg'              
            }
          else if (seg->class_id == 255)
            break; // "stream-root" segment for the right codestream
        }
      this->unwind(cache,mutex_locked);
      seg = NULL;
    }

  // Now we need to start growing the `path', perhaps from nothing, acquiring
  // access locks and checking that we have locked the right cache-segs as
  // we go.
  while (seg == NULL)
    { // Need to gain access-lock to the cache root; it also might change.
      assert(this->stream_info == NULL);
      seg = cache->root;
      if (seg == NULL)
        return NULL;
      assert(!(_addr_to_kdu_int32(seg) & 3)); // cache root always NULL or a
                                              // valid address
      seg->access_ctl.exchange_add(1);
      if (seg == cache->root)
        { 
          add_to_path(seg);
          if (((stream_id - seg->base_id) >> seg->shift) >= 128)
            return NULL; // Cache does not yet span the desired codestream
          break;
        }
      seg->lock_failed(cache,mutex_locked);
      seg=NULL; // Go around and try again
    }
  
  // Trace forward to the "stream-root" if not already there
  while (seg->stream_id < 0)
    { // `seg' is a "stream-nav" segment
      assert(seg->class_id == 255);
      assert(this->stream_info == NULL);
      kdu_long off = (stream_id - seg->base_id) >> seg->shift;
      assert((off >= 0) && (off < 128));
      int idx = (int)off;
      kd_var_cache_seg *nxt_seg = seg->segs[idx];
      if ((nxt_seg == NULL) || _addr_to_kdu_int32(nxt_seg) & 3)
        return NULL;
      assert(nxt_seg != cache->root);
      nxt_seg->access_ctl.exchange_add(1);
      if (nxt_seg == seg->segs[idx])
        { 
          seg=nxt_seg;
          add_to_path(seg);
        }
      else
        nxt_seg->lock_failed(cache,mutex_locked); // Go around and try again
    }
  
  // Trace the relevant class hierarchy from the stream-root if we are there.
  assert(seg->stream_id == stream_id);
  while (seg->class_id == 255)
    { 
      this->stream_info = &(seg->stream);
      assert(seg->flags & KD_CSEG_STREAM_ROOT);
      assert((cls >= 0) && (cls < KDU_NUM_DATABIN_CLASSES));
      kd_var_cache_seg *cls_root = seg->stream.classes[cls];
      if ((cls_root == NULL) || _addr_to_kdu_int32(cls_root) & 3)
        return NULL;
      assert(cls_root != cache->root);
      cls_root->access_ctl.exchange_add(1);
      if (cls_root == seg->stream.classes[cls])
        { 
          seg = cls_root;
          add_to_path(seg);
          if (((bin_id - seg->base_id) >> seg->shift) >= 128)
            return NULL; // Cache does not yet span the desired data-bin
          break;
        }
      else
        cls_root->lock_failed(cache,mutex_locked); // Go around and try again
    }
  
  // Trace forwards to the leaf-seg of interest, if not already there
  assert(seg->class_id == (kdu_byte)cls);
  while (seg->shift >= 7)
    { 
      kdu_long off = (bin_id - seg->base_id) >> seg->shift;
      assert((off >= 0) && (off < 128));
      int idx = (int)off;
      kd_var_cache_seg *nxt_seg = seg->segs[idx];
      if ((nxt_seg == NULL) || _addr_to_kdu_int32(nxt_seg) & 3)
        return NULL;
      assert(nxt_seg != cache->root);
      nxt_seg->access_ctl.exchange_add(1);
      if (nxt_seg == seg->segs[idx])
        { seg=nxt_seg; add_to_path(seg); }
      else
        nxt_seg->lock_failed(cache,mutex_locked); // Go around and try again
    }

  return seg;
}

/*****************************************************************************/
/*                     kd_cache_path_walker::trace_next                      */
/*****************************************************************************/

kd_var_cache_seg *
  kd_cache_path_walker::trace_next(kd_cache *cache, bool &mutex_locked,
                                   kdu_long fixed_stream_id,
                                   int fixed_class_id, bool bin0_only,
                                   bool preserved_only, bool skip_unmarked,
                                   bool skip_meta)
{
  kd_var_cache_seg *seg = NULL;
  kdu_long stream_id=0, bin_id=0;
  int class_id=0;
  bool backtrack=true;
  assert(fixed_class_id != KDU_TILE_HEADER_DATABIN); // Caller should have
                                // converted this to the main header class.
  if ((path_len < 1) || !((seg = path[path_len-1])->flags & KD_CSEG_LEAF))
    { // Starting from scratch
      unwind_all(cache,mutex_locked);
      seg = NULL;
      class_id = (fixed_class_id >= 0)?fixed_class_id:0;
      stream_id = (fixed_stream_id >= 0)?fixed_stream_id:0;
      backtrack = false;
    }
  else
    { 
      stream_id = seg->stream_id;
      bin_id = seg->base_id;
      class_id = seg->class_id;
      if ((fixed_stream_id >= 0) && (stream_id != fixed_stream_id))
        return NULL;
      if ((fixed_class_id >= 0) && (fixed_class_id != class_id))
        return NULL;
      if (bin0_only && (bin_id != 0))
        return NULL;
      backtrack=true;
      bin_id += 128; // We will be looking for this bin during backtrack 
    }

  while ((!backtrack) || (path_len > 0))
    { 
      if (backtrack)
        { // Unwind the path one step
          assert(seg != NULL);
          unwind(cache,mutex_locked);
          if (path_len == 0)
            { // All the way back at the start, but perhaps we missed a
              // recently inserted cache-seg
              if (seg == cache->root)
                return NULL; // Already been through the entire cache hierarchy
              seg = cache->root; // Root must have been inserted after we
                                 // built the path
            }
          else
            seg = path[path_len-1];
          if (seg->flags & KD_CSEG_STREAM_ROOT)
            { // Backtracked to "stream-root"
              assert((seg->class_id == 255) && (seg->stream_id >= 0));
              bin_id = 0;
              if (class_id == fixed_class_id)
                { // About to go past the fixed class
                  if (stream_id == fixed_stream_id)
                    return NULL; // About to go past the fixed stream
                  stream_id++;
                  backtrack = true;
                  continue;
                }
              class_id++;
            }
        }
      
      kd_var_cache_seg *nxt_seg = NULL;
      int nxt_idx=0;
      backtrack = false; // Until proven otherwise
      if (seg == NULL)
        { // Starting from the very root
          assert(this->stream_info == NULL);
          nxt_seg = cache->root;
          if (nxt_seg == NULL)
            return NULL;
          nxt_seg->access_ctl.exchange_add(1);
          if (nxt_seg == cache->root)
            { seg=nxt_seg; add_to_path(seg); }
          else
            nxt_seg->lock_failed(cache,mutex_locked); // Go around & try again
          continue;
        }
      
      if (seg->stream_id < 0)
        { // "stream-nav" segment
          assert(stream_id >= fixed_stream_id);
          nxt_idx = (int)((stream_id - seg->base_id) >> seg->shift);
          while (nxt_idx < 128)
            { 
              nxt_seg = seg->segs[nxt_idx];
              if ((nxt_seg != NULL) &&
                  ((_addr_to_kdu_int32(nxt_seg) & 3) == 0) &&
                  !((seg->preserve.get(nxt_idx) ^ 1) && preserved_only))
                break;
              nxt_seg = NULL;
              if (stream_id == fixed_stream_id)
                break; // About to go past fixed stream-id
              nxt_idx++;
              stream_id = seg->base_id + (((kdu_long) nxt_idx) << seg->shift);
            }
          if (nxt_seg == NULL)
            backtrack = true;
        }
      else if (seg->class_id == 255)
        { // "stream-root" segment
          assert(seg->flags & KD_CSEG_STREAM_ROOT);
          assert(class_id >= fixed_class_id); // We set it up this way at top
          nxt_idx = class_id;
          while (nxt_idx < KDU_NUM_DATABIN_CLASSES)
            { 
              if ((!skip_meta) || (nxt_idx != KDU_META_DATABIN))
                { 
                  nxt_seg = seg->stream.classes[nxt_idx];
                  if ((nxt_seg != NULL) &&
                      ((_addr_to_kdu_int32(nxt_seg) & 3) == 0) &&
                      ((!skip_unmarked) || seg->stream.mark_counts[nxt_idx]) &&
                      !(preserved_only && !seg->preserve.get(nxt_idx)))
                    break;
                  nxt_seg = NULL;
                }
              if (class_id == fixed_class_id)
                break; // About to go past the fixed class-id
              nxt_idx++;
              class_id = nxt_idx;
            }
          if (nxt_seg == NULL)
            { // Need to backtrack
              if (stream_id == fixed_stream_id)
                return NULL; // About to go past the fixed stream
              stream_id++;
              if (class_id != fixed_class_id)
                class_id=0;
              backtrack = true;
            }
        }
      else if (!bin0_only)
        { // "class-nav" segment; looking to match/advance `bin_id'
          nxt_idx = (int)((bin_id - seg->base_id) >> seg->shift);
          while (nxt_idx < 128)
            { 
              nxt_seg = seg->segs[nxt_idx];
              if ((nxt_seg != NULL) &&
                  ((_addr_to_kdu_int32(nxt_seg) & 3) == 0) &&
                  !(preserved_only && !seg->preserve.get(nxt_idx)))
                break;
              nxt_seg = NULL; nxt_idx++;
              bin_id = seg->base_id + (((kdu_long) nxt_idx) << seg->shift);
            }
          if (nxt_seg == NULL)
            backtrack = true;
        }
      else
        { // "class-nav" segment, but match only bin_id=0
          if (bin_id != 0)
            backtrack = true;
          else
            { 
              nxt_idx = 0;
              nxt_seg = seg->segs[nxt_idx];
              if ((nxt_seg == NULL) ||
                  ((_addr_to_kdu_int32(nxt_seg) & 3) != 0) ||
                  (preserved_only && !seg->preserve.get(nxt_idx)))
                { // Cannot advance into bin-0
                  nxt_seg = NULL;
                  backtrack = true;
                }
            }
        }
      
      if (!backtrack)
        { // Advance the path to `next_seg'
          nxt_seg->access_ctl.exchange_add(1);
          
          if (nxt_seg == seg->segs[nxt_idx])
            { 
              seg = nxt_seg;
              add_to_path(seg);
              if (seg->flags & KD_CSEG_STREAM_ROOT)
                this->stream_info = &(seg->stream);
              if (seg->flags & KD_CSEG_LEAF)
                break;
            }
          else
            nxt_seg->lock_failed(cache,mutex_locked); // Go around & try again
        }      
    }
  
  return seg;
}


/* ========================================================================= */
/*                                 kd_cache                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                             kd_cache::close                               */
/*****************************************************************************/

void kd_cache::close(bool &primary_mutex_locked)
{
  if (primary == this)
    { // We are the primary cache; close all secondaries
      while (attached_head != NULL)
        attached_head->close(primary_mutex_locked);
    }
  
  // Remove all access-locks we might be holding
  marking_path.unwind_all(primary,primary_mutex_locked);
  add_path.unwind_all(primary,primary_mutex_locked);
  get_length_path.unwind_all(primary,primary_mutex_locked);
  scan_path.unwind_all(primary,primary_mutex_locked);
  last_scan_seg = NULL;
  meta_read_path.unwind_all(primary,primary_mutex_locked);
  stream_read_path.unwind_all(primary,primary_mutex_locked);
  main_read_path.unwind_all(primary,primary_mutex_locked);
  
  if (primary != this)
    { // Detach ourself
      if (!primary_mutex_locked)
        { 
          primary->mutex.lock();
          primary_mutex_locked = true;
        }
      kd_cache *tgt = primary;
      kd_cache *prev, *scan;
      for (prev=NULL, scan=tgt->attached_head;
           scan != NULL;
           prev=scan, scan=scan->attached_next)
        if (scan == this)
          { 
            if (prev == NULL)
              tgt->attached_head = this->attached_next;
            else
              prev->attached_next = this->attached_next;
            break;
          }
      assert(scan != NULL); // Otherwise we did not find ourselves on the list!
      this->primary = this;
      this->attached_next = NULL;
      assert(this->attached_head == NULL);
      this->attached_head = NULL; // Just in case
    }
  
  reset_state();
}

/*****************************************************************************/
/*                        kd_cache::attach_to_primary                        */
/*****************************************************************************/

void kd_cache::attach_to_primary(kd_cache *tgt)
{
  assert(this->primary == this);
  this->primary = tgt;
  this->attached_next = tgt->attached_head;
  tgt->attached_head = this;
}

/*****************************************************************************/
/*                          kd_cache::reset_state                            */
/*****************************************************************************/

void kd_cache::reset_state()
{
  assert((primary == this) && (attached_head==NULL) && (attached_next==NULL));
  marking_path.reset();
  add_path.reset();
  get_length_path.reset();
  scan_path.reset();
  last_scan_seg = NULL;
  last_scan_pos = 0;
  meta_read_path.reset();
  stream_read_path.reset();
  main_read_path.reset();
  last_read_codestream_id = -1;
  read_buf = read_start = NULL;
  read_buf_pos = databin_pos = 0;
  databin_status = 0;
  
  reclaimable_data_head = NULL;
  reclaimable_data_tail = NULL;
  all_reclaimable_data_locked = false;
  reclaimable_segs_head = NULL;
  reclaimable_segs_tail = NULL;
  all_reclaimable_segs_locked = false;
  
  auto_trim_buf_threshold = 0;
  auto_trim_seg_threshold = 0;
  total_reclaimed_bufs = 0;
  total_reclaimed_segs = 0;
  max_codestream_id = 0;
  int c;
  for (c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
    transferred_bytes[c] = 0;

  for (c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
    class_preserve_streams[c] = -1; // Note: wildcard if < -1
  
  if (root != NULL)
    { // Recursively delete the cache hierarchy
      assert(root->access_ctl.get() == 0);
      kd_var_cache_seg *tmp_root = root;
      root = NULL;
      tmp_root->recycle_all(this);
    }
  if (buf_server != NULL)
    { delete buf_server; buf_server = NULL; }
  if (seg_server != NULL)
    { delete seg_server; seg_server = NULL; }  
}

/*****************************************************************************/
/*                        kd_cache::reclaim_data_bufs                        */
/*****************************************************************************/

void kd_cache::reclaim_data_bufs(kd_cint num_to_reclaim, bool &mutex_locked)
{
  if (all_reclaimable_data_locked)
    return;
  if (!mutex_locked)
    { 
      mutex.lock();
      mutex_locked = true;
    }
  kd_cint target_bufs, starting_bufs = buf_server->get_allocated_bufs();
  if (num_to_reclaim > starting_bufs)
    target_bufs = 0;
  else
    target_bufs = starting_bufs - num_to_reclaim;
  kd_cint latest_bufs = starting_bufs;
  kd_var_cache_seg *seg, *locked_head=NULL, *locked_tail=NULL;
  while ((seg = reclaimable_data_tail) != NULL)
    { 
      assert(seg->reclaim_next == NULL);
      if ((reclaimable_data_tail = seg->reclaim_prev) == NULL)
        { 
          assert(reclaimable_data_head == seg);
          reclaimable_data_head = NULL;
        }
      else
        reclaimable_data_tail->reclaim_next = NULL;
      seg->reclaim_prev = NULL;
      if (seg->access_ctl.get() != 0)
        { // Put on the temporary locked list; we will put these back later
          if ((seg->reclaim_next = locked_head) == NULL)
            locked_tail = seg;
          else
            locked_head->reclaim_prev = seg;
          locked_head = seg;
          continue;
        }
      
      // If we get here, we can reclaim all reclaimable data-bins from `seg'
      assert(seg->flags & KD_CSEG_LEAF);
      seg->flags &= ~KD_CSEG_RECLAIMABLE_DATA; // We've pulled it off the list
      int cls = seg->class_id;
      kd_var_stream_info *stream_info=NULL;
      kd_var_cache_seg *sroot;
      for (sroot=seg->container; sroot != NULL; sroot=sroot->container)
        if (sroot->flags & KD_CSEG_STREAM_ROOT)
          { 
            stream_info = &(sroot->stream);
            break;
          }
      assert(stream_info != NULL);
      
      for (int idx=0; (seg->num_reclaimable_bins > 0) && (idx < 128); idx++)
        { 
          if (seg->preserve.get(idx))
            continue;
          kd_cache_buf *buf_list = seg->databins[idx];
          if ((buf_list != NULL) && ((_addr_to_kdu_int32(buf_list) & 3) == 0))
            { // else not reclaimable
              kdu_int32 status = buf_list->head.status.get();
              kdu_int32 m_val = status & KD_CACHE_HD_M_MASK;
              kdu_int32 new_m_val = m_val;
              if (m_val == KD_CACHE_HD_M_MARKED)
                new_m_val = 0; // Marked, but was empty before being marked
              else if ((m_val != 0) || ((status & KD_CACHE_HD_L_MASK) != 0))
                new_m_val = KD_CACHE_HD_M_DELETED;
              if (new_m_val != m_val)
                { 
                  if (m_val == 0)
                    stream_info->mark_counts[cls]++;
                  else if (new_m_val == 0)
                    { 
                      assert(stream_info->mark_counts[cls] > 0);
                      stream_info->mark_counts[cls]--;
                    }
                  status += (new_m_val - m_val);
                  buf_list->head.status.set(status);
                }
              seg->databins[idx] = (kd_cache_buf *)(1+((kdu_byte *)buf_list));
              assert(seg->num_descendants > 0);
              seg->num_descendants--;
              seg->num_reclaimable_bins--;
              seg->num_erasable++;
            }
        }
      assert(seg->num_reclaimable_bins == 0);

      if (seg->num_erasable != 0)
        { // Lock and unlock `seg' to erase the erasable data-bins safely;
          // with low probability they cannot be erased immediately and so
          // we may end up reclaiming some more content than was
          // originally intended -- should be no big deal.
          seg->access_ctl.exchange_add(1); // Temporarily become locker
          seg->unlock(this,mutex_locked,stream_info);
        }
      else
        { // Should not happen, but if we do not do the above, the seg may
          // be left without descendants and without preserve flags, yet not
          // on the "reclaimable-segs" list, where it should go.
          seg->adjust_reclaimability(this);
        }
      
      // See what we have achieved
      latest_bufs = buf_server->get_allocated_bufs();
      if (latest_bufs <= target_bufs)
        break;
    }
  
  this->total_reclaimed_bufs += (kdu_int64)(starting_bufs - latest_bufs);
  
  if (seg == NULL)
    { // Failed to reach the objective
      all_reclaimable_data_locked = true;
    }
  if (locked_head != NULL)
    { // Put these on the head of the list
      if ((locked_tail->reclaim_next = reclaimable_data_head) != NULL)
        reclaimable_data_head->reclaim_prev = locked_tail;
      else
        reclaimable_data_tail = locked_tail;
      reclaimable_data_head = locked_head;
    }
}


/* ========================================================================= */
/*                                kdu_cache                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                           kdu_cache::kdu_cache                            */
/*****************************************************************************/

kdu_cache::kdu_cache()
{
  state = new kd_cache();
}

/*****************************************************************************/
/*                           kdu_cache::~kdu_cache                           */
/*****************************************************************************/

kdu_cache::~kdu_cache()
{
  close();
  delete state;
}

/*****************************************************************************/
/*                           kdu_cache::attach_to                            */
/*****************************************************************************/

void
  kdu_cache::attach_to(kdu_cache *existing)
{
  close();
  kd_cache *primary = existing->state;
  assert(primary != NULL);
  primary = primary->primary; // Make sure we attach to the true primary cache
  assert(primary != NULL);
  primary->mutex.lock();
  state->attach_to_primary(primary);
  primary->mutex.unlock();
}

/*****************************************************************************/
/*                              kdu_cache::close                             */
/*****************************************************************************/

bool
  kdu_cache::close()
{
  assert(state != NULL);
  kd_cache *primary = state->primary;
  bool primary_mutex_locked = false;
  state->close(primary_mutex_locked);
  if (primary_mutex_locked)
    primary->mutex.unlock();
  return true;
}

/*****************************************************************************/
/*                         kdu_cache::add_to_databin                         */
/*****************************************************************************/

bool
  kdu_cache::add_to_databin(int cls, kdu_long stream_id, kdu_long bin_id,
                            const kdu_byte data[], int offset, int num_bytes,
                            bool is_complete, bool add_as_most_recent,
                            bool mark_if_augmented)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (bin_id < 0) || (stream_id < 0))
    return false; // We cannot store data identified in this way  
  
  if ((offset+num_bytes) > KD_CACHE_HD_L_MASK)
    { // Adding this content will result in a data-bin whose length exceeds the
      // maximum value we can record within the L field of the `status' word.
      num_bytes = KD_CACHE_HD_L_MASK - offset;
      is_complete = false;
    }
  if ((num_bytes <= 0) && !is_complete)
    return true; // Nothing to add
  
  kd_cache *tgt = state->primary;
  tgt->mutex.lock(); // Ensures thread-safety for multiple adding threads
  bool mutex_locked = true;
  if (tgt->buf_server == NULL)
    tgt->buf_server = new kd_cache_buf_server;
  if (tgt->seg_server == NULL)
    tgt->seg_server = new kd_cache_seg_server;
  tgt->transferred_bytes[cls] += num_bytes;

  kdu_long cps = tgt->class_preserve_streams[cls];
  bool force_preserve = (cps < -1) || (cps == stream_id);
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  
  if (((kd_cint)stream_id) > tgt->max_codestream_id)
    tgt->max_codestream_id = (kd_cint)stream_id;
  
  // Use our own local `add_path' path walker to manage segment access locks,
  // so that multiple adders that share a common cache can keep track of
  // their own state -- not essential (since we always lock the common mutex
  // for addition) but may be more efficient in rare circumstances when
  // different `kdu_cache' interfaces are used to add to a common cache.
  bool success = false;
  kd_cint initial_bufs = tgt->buf_server->get_allocated_bufs();
  kd_cache_path_walker *path = &(state->add_path);
  kd_var_cache_seg *seg =
    path->make_path(tgt,mutex_locked,cls,stream_id,bin_id,force_preserve);
  if (seg != NULL)
    { // else we ran out of memory!
      assert(path->stream_info != NULL);
      success = true;
      int idx = (int)(bin_id & 127);
      kd_cache_buf *old_buf_list = seg->databins[idx];
      kd_cache_buf *buf_list=old_buf_list;
      if (buf_list == KD_BIN_CEMPTY)
        buf_list = NULL; // No need to touch this complete-and-empty bin
      else if ((buf_list == NULL) || (buf_list == KD_BIN_DELETED))
        { 
          if ((num_bytes == 0) && is_complete)
            { // No need for any `buf_list'
              seg->databins[idx] = KD_BIN_CEMPTY;
              seg->num_non_null++;
              buf_list = NULL;
            }
          else
            { // Need to allocate a new cache-buf
              buf_list = tgt->buf_server->get();
              if (buf_list == NULL)
                { // Insufficient memory
                  success = false;
                  if (old_buf_list != KD_BIN_DELETED)
                    { 
                      seg->databins[idx] = KD_BIN_DELETED;
                      seg->num_non_null++;
                      path->stream_info->mark_counts[cls]++;
                    }
                }
              else
                { // Initialize new list
                  buf_list->head.init();
                  if (old_buf_list == KD_BIN_DELETED)
                    buf_list->head.status.set(KD_CACHE_HD_M_DELETED);
                  else
                    seg->num_non_null++; // We will be installing a buf-list
                  seg->num_descendants++;
                  if (!seg->preserve.get(idx))
                    seg->num_reclaimable_bins++;
                }
            }
        }
      if (buf_list != NULL)
        { // Otherwise, we are all done
          kd_cache_hd *head = &(buf_list->head);
          kdu_int32 status = head->status.get(); // Work with local copy first
          kdu_int32 initial_bytes = status & KD_CACHE_HD_L_MASK;
          kd_cache_buf_io buf_io(tgt->buf_server,buf_list,sizeof(kd_cache_hd));
          
          // First, write the data itself
          bool write_failed = false;
          if (!(buf_io.advance(offset) && buf_io.copy_from(data,num_bytes)))
            write_failed = true;

          // Modify prefix length and the hole list, as appropriate.
          kd_cache_buf_io hole_src(tgt->buf_server,head->hole_list);
          kd_cache_buf_io hole_dst(tgt->buf_server); // Use to write new list
          bool have_existing = false;
          int existing_start=0, existing_lim=0;
          
          // Merge until the new region is entirely accounted for.
          bool augmented = false;
          bool intersects_with_existing = false;
          int range_start = offset;
          int range_lim = offset + num_bytes;
          while ((have_existing =
                  hole_src.read_byte_range(existing_start,existing_lim)))
            { 
              if (existing_start > range_lim)
              break; // Existing byte range entirely follows new byte range
              if (existing_lim < range_start)
                { // Existing range entirely precedes new range
                  if (!hole_dst.write_byte_range(existing_start,existing_lim))
                    write_failed = true;
                  continue;
                }
              intersects_with_existing = true;
              if (existing_start <= range_start)
              range_start = existing_start;
            else
              augmented = true;
            if (existing_lim >= range_lim)
              range_lim = existing_lim;
            else
              augmented = true;
            }

          if ((range_lim > range_start) && (range_lim > initial_bytes))
            { // The new byte range needs to be recorded somewhere
              if (range_start <= initial_bytes)
                { // Extends initial segment
                  initial_bytes = range_lim;
                  augmented = true;
                }
              else
                { 
                  if (!hole_dst.write_byte_range(range_start,range_lim))
                    write_failed = true;
                  if (!intersects_with_existing)
                    augmented = true;
                }
            }

          // Copy any original ranges which have not yet been merged
          while (have_existing)
            { 
              if (!hole_dst.write_byte_range(existing_start,existing_lim))
                write_failed = true;
              have_existing = hole_src.read_byte_range(existing_start,
                                                       existing_lim);
            }
          
          // Write terminal 0 if necessary          
          if (!hole_dst.finish_list())
            write_failed = true;
          
          // Replace old list with new list, being careful to update the
          // `status' word last, with release semantics, and taking care of
          // write failures that might have occurred.
          if (head->hole_list != NULL)
            { state->buf_server->release(head->hole_list);
              head->hole_list = NULL; }
          head->hole_list = hole_dst.get_list();
          kdu_int32 m_val = status & KD_CACHE_HD_M_MASK;
          if (write_failed)
            { // Not safe to update F-bit or L value; moreover, we should
              // remove all holes and mark the databin as having been subject
              // to some kind of delete operation.
              success = false;
              initial_bytes = status & KD_CACHE_HD_L_MASK; // Ignore new bytes
              if (!m_val)
                path->stream_info->mark_counts[cls]++; // Data-bin newly marked
              status = initial_bytes | KD_CACHE_HD_M_DELETED;
              if (head->hole_list != NULL)
                { state->buf_server->release(head->hole_list);
                  head->hole_list = NULL; }
            }
          else
            { // Build new `status' word from `initial_byte'
              status &= KD_CACHE_HD_F_BIT; // Preserve F flag
              status |= initial_bytes;
              if (is_complete)
                status |= KD_CACHE_HD_F_BIT; // Update F flag
              if (head->hole_list != NULL)
                status |= KD_CACHE_HD_H_BIT;
              if (augmented && mark_if_augmented &&
                  (m_val != KD_CACHE_HD_M_DELETED) &&
                  (m_val != KD_CACHE_HD_M_AUGMENTED))
                { // Introduce new marks
                  if (m_val == 0)
                    path->stream_info->mark_counts[cls]++;
                  if (status & KD_CACHE_HD_L_MASK) // Was non-empty
                    m_val = KD_CACHE_HD_M_AUGMENTED;
                  else
                    m_val = KD_CACHE_HD_M_MARKED;
                }
              status |= m_val;
            }
          if (buf_list == old_buf_list)
            head->status.barrier_set(status);
          else
            { 
              head->status.set(status);
              seg->elts[idx].barrier_set(buf_list);
            }
        }
    }
  
  // See if we need to do any cache trimming before we return
  if (tgt->auto_trim_buf_threshold > 0)
    { 
      kd_cint cur_bufs = tgt->buf_server->get_allocated_bufs();
      if ((cur_bufs > tgt->auto_trim_buf_threshold) &&
          (cur_bufs > initial_bufs) && !tgt->all_reclaimable_data_locked)
        { 
          kd_cint min_reclaim = 2*(cur_bufs - initial_bufs);
          kd_cint max_reclaim = cur_bufs - tgt->auto_trim_buf_threshold;
          if (min_reclaim > max_reclaim)
            min_reclaim = max_reclaim;
          tgt->reclaim_data_bufs(min_reclaim,mutex_locked);
        }
    }
  
  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock();
  return success;
}

/*****************************************************************************/
/*                         kdu_cache::delete_databin                         */
/*****************************************************************************/

bool
  kdu_cache::delete_databin(int cls, kdu_long stream_id, kdu_long bin_id,
                            bool mark_if_non_empty)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (bin_id < 0) || (stream_id < 0))
    return false; // We cannot have stored data identified in this way  
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  
  kd_cache *tgt = state->primary;
  tgt->mutex.lock(); // Ensures thread-safety for add/delete operations
  bool mutex_locked = true;
    // Use our own local `add_path' path walker to manage segment access locks,
    // so that multiple adders that share a common cache can keep track of
    // their own state -- not essential (since we always lock the common mutex
    // for addition) but may be more efficient in rare circumstances when
    // different `kdu_cache' interfaces are used to add/delete to/from a
    // common cache.
  kd_cache_path_walker *path = &(state->add_path);
  kd_var_cache_seg *seg =
    path->trace_path(tgt,mutex_locked,cls,stream_id,bin_id);
  bool deleted_something = false;
  if (seg != NULL)
    { // Otherwise, there is nothing to delete
      assert(path->stream_info != NULL);
      int idx = (int)(bin_id & 127);
      kd_cache_buf *buf_list = seg->databins[idx];
      if ((buf_list != NULL) && ((_addr_to_kdu_int32(buf_list) & 3) == 0))
        { // else not a valid address, so there is nothing to delete
          deleted_something = true;
          kdu_int32 status = buf_list->head.status.get();
          kdu_int32 m_val = status & KD_CACHE_HD_M_MASK;
          kdu_int32 new_m_val = 0;
          if (mark_if_non_empty)
            { 
              if (m_val == KD_CACHE_HD_M_MARKED)
                new_m_val = 0; // Marked, but was empty before being marked
              else if ((m_val != 0) || ((status & KD_CACHE_HD_L_MASK) != 0))
                new_m_val = KD_CACHE_HD_M_DELETED;
              else
                new_m_val = m_val;
            }
          if (new_m_val != m_val)
            { 
              if (m_val == 0)
                path->stream_info->mark_counts[cls]++;
              else if (new_m_val == 0)
                { 
                  assert(path->stream_info->mark_counts[cls] > 0);
                  path->stream_info->mark_counts[cls]--;
                }
              status += (new_m_val - m_val);
              buf_list->head.status.set(status);
            }
          seg->databins[idx] = (kd_cache_buf *)(1+((kdu_byte *)buf_list));
          assert(seg->num_descendants > 0);
          seg->num_descendants--;
          if (!seg->preserve.get(idx))
            seg->num_reclaimable_bins--;
          seg->num_erasable++;
          
          // Now we are done; we cannot actually erase anything since the
          // access-lock count to `seg' is necessarily non-zero (we took out
          // a lock in the `trace_path' call above). This is good, because
          // it encourages efficient batch deletion.  If we come back soon to
          // delete other data-bins from this cache-seg we will find that we
          // already have the access-lock.  Once we shift our attention to
          // adding/deleting from a different cache-seg we are likely to
          // reduce the access-lock here to 0 and at that point we will erase
          // all the erasables in one hit.
        }
    }
  
  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock();
  return deleted_something;
}

/*****************************************************************************/
/*                       kdu_cache::delete_stream_class                      */
/*****************************************************************************/

int
  kdu_cache::delete_stream_class(int cls, kdu_long stream_id,
                                 bool mark_if_non_empty)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) || (stream_id < 0))
    return 0;
  if (cls == KDU_MAIN_HEADER_DATABIN)
    { // There is only one data-bin in this category; easiest to handle this
      // with `delete_databin'.
      bool did_delete = delete_databin(cls,stream_id,0,mark_if_non_empty);
      return (did_delete)?1:0;
    }
  if (cls == KDU_TILE_HEADER_DATABIN)
    cls = KDU_MAIN_HEADER_DATABIN;
  kd_cache *tgt = state->primary;
  tgt->mutex.lock(); // Ensures thread-safety for add/delete operations
  bool mutex_locked = true;
    // Use our own local `add_path' path walker to manage segment access locks,
    // so that multiple adders that share a common cache can keep track of
    // their own state -- not essential (since we always lock the common mutex
    // for addition) but may be more efficient in rare circumstances when
    // different `kdu_cache' interfaces are used to add/delete to/from a
    // common cache.
  kd_cache_path_walker *path = &(state->add_path);
  path->unwind_all(tgt,mutex_locked);
  int num_deleted = 0;
  kd_var_cache_seg *seg;
  while ((seg = path->trace_next(tgt,mutex_locked,stream_id,cls,
                                 false,false,false,false)) != NULL)
    { 
      assert(seg->stream_id == stream_id);
      assert((cls < 0) || (cls == (int)seg->class_id));
      assert((seg->class_id != (kdu_byte)KDU_META_DATABIN) ||
             (cls == KDU_META_DATABIN));
      assert(seg->flags & KD_CSEG_LEAF);
      
      // Everything below is just like `delete_databin' but executed on all
      // 128 elements in `seg'.
      int idx = 0;
      if (cls == KDU_MAIN_HEADER_DATABIN)
        { // We were actually called with `KDU_TILE_HEADER_DATABIN', but the
          // tile headers are found with the main header; however, we should
          // skip absolute bin-id 0
          if (seg->base_id == 0)
            idx = 1;
        }
      for (; idx < 128; idx++)
        { 
          kd_cache_buf *buf_list = seg->databins[idx];
          if ((buf_list != NULL) && ((_addr_to_kdu_int32(buf_list) & 3) == 0))
            { // else not a valid address, so there is nothing to delete
              num_deleted++;
              kdu_int32 status = buf_list->head.status.get();
              kdu_int32 m_val = status & KD_CACHE_HD_M_MASK;
              kdu_int32 new_m_val = 0;
              if (mark_if_non_empty)
                { 
                  if (m_val == KD_CACHE_HD_M_MARKED)
                    new_m_val = 0; // Marked, but was empty before being marked
                  else if ((m_val != 0) || ((status & KD_CACHE_HD_L_MASK) != 0))
                    new_m_val = KD_CACHE_HD_M_DELETED;
                  else
                    new_m_val = m_val;
                }
              if (new_m_val != m_val)
                { 
                  if (m_val == 0)
                    path->stream_info->mark_counts[cls]++;
                  else if (new_m_val == 0)
                    { 
                      assert(path->stream_info->mark_counts[cls] > 0);
                      path->stream_info->mark_counts[cls]--;
                    }
                  status += (new_m_val - m_val);
                  buf_list->head.status.set(status);
                }
              seg->databins[idx] = (kd_cache_buf *)(1+((kdu_byte *)buf_list));
              assert(seg->num_descendants > 0);
              seg->num_descendants--;
              if (!seg->preserve.get(idx))
                seg->num_reclaimable_bins--;
              seg->num_erasable++;
            }
        }
    }

  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock();
  return num_deleted;
}

/*****************************************************************************/
/*                   kdu_cache::set_preferred_memory_limit                   */
/*****************************************************************************/

void
  kdu_cache::set_preferred_memory_limit(kdu_long preferred_byte_limit)
{
  if (state != state->primary)
    return;
  if (preferred_byte_limit <= 0)
    state->auto_trim_buf_threshold = 0;
  else
    { 
      kdu_long num =
        (preferred_byte_limit + KD_CACHE_BUF_BYTES - 1) / KD_CACHE_BUF_BYTES;
      if (num > KD_CINT_LONG_MAX)
        num = KD_CINT_LONG_MAX;
      if (num < 1)
        num = 1;
      state->auto_trim_buf_threshold = (kd_cint) num;
    }
}

/*****************************************************************************/
/*                 kdu_cache::trim_to_preferred_memory_limit                 */
/*****************************************************************************/

void
  kdu_cache::trim_to_preferred_memory_limit()
{
  if (state != state->primary)
    return;
  state->mutex.lock();
  bool mutex_locked = true;
  kd_cint cur_allocated = state->buf_server->get_allocated_bufs();
  kd_cint threshold = state->auto_trim_buf_threshold;
  if ((threshold > 0) && (cur_allocated > threshold))
    state->reclaim_data_bufs(cur_allocated-threshold,mutex_locked);
  assert(mutex_locked);
  state->mutex.unlock();
}

/*****************************************************************************/
/*                        kdu_cache::preserve_databin                        */
/*****************************************************************************/

void
  kdu_cache::preserve_databin(int cls, kdu_long stream_id, kdu_long bin_id)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (bin_id < 0) || (stream_id < 0))
    return; // Must be preserving a valid data-bin
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  kd_cache *tgt = state->primary;
  tgt->mutex.lock(); // Ensures thread-safety for multiple marking threads
  bool mutex_locked = true;
  if (tgt->seg_server == NULL) // We may need to allocate cache-segs to hold
    tgt->seg_server = new kd_cache_seg_server; // the marking state.
  kd_cache_path_walker *path = &(state->add_path);
  path->make_path(tgt,mutex_locked,cls,stream_id,bin_id,true);
  
  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock(); 
}

/*****************************************************************************/
/*                     kdu_cache::preserve_class_stream                      */
/*****************************************************************************/

void
  kdu_cache::preserve_class_stream(int cls, kdu_long stream_id)
{
  if (cls >= KDU_NUM_DATABIN_CLASSES)
    return;
  if (stream_id < 0)
    stream_id = -2; // -1 means no preservation; -2 is internal wildcard
  kd_cache *tgt = state->primary;
  tgt->mutex.lock(); // Ensures thread-safety for multiple marking threads
  if (cls >= 0)
    tgt->class_preserve_streams[cls] = stream_id;
  else
    for (cls=0; cls < KDU_NUM_DATABIN_CLASSES; cls++)
      if (cls != KDU_META_DATABIN)
        tgt->class_preserve_streams[cls] = stream_id;
  tgt->mutex.unlock();
}

/*****************************************************************************/
/*                          kdu_cache::touch_databin                         */
/*****************************************************************************/

void
  kdu_cache::touch_databin(int cls, kdu_long stream_id, kdu_long bin_id)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (bin_id < 0) || (stream_id < 0))
    return;
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  kd_cache *tgt = state->primary;
  kd_cache_path_walker *path = &(state->marking_path);
    // Use our own local `marking_path' path walker to manage segment access
    // locks, so that multiple `kdu_cache' interfaces to a common primary
    // cache can manage their own touching process.
  bool mutex_locked = false;
  path->trace_path(tgt,mutex_locked,cls,stream_id,bin_id);
  if (mutex_locked)
    tgt->mutex.unlock();
  return;
}

/*****************************************************************************/
/*                          kdu_cache::mark_databin                          */
/*****************************************************************************/

kdu_int32
  kdu_cache::mark_databin(int cls, kdu_long stream_id, kdu_long bin_id,
                          bool mark_state, int &length, bool &is_complete)
{
  length = 0;
  is_complete = false;
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (bin_id < 0) || (stream_id < 0) || (state != state->primary))
    return 0; // We cannot store data identified in this way
  
  kd_cache *tgt = state;
  tgt->mutex.lock(); // Ensures thread-safety for multiple marking threads
  bool mutex_locked = true;
  if (tgt->seg_server == NULL) // We may need to allocate cache-segs to hold
    tgt->seg_server = new kd_cache_seg_server; // the marking state.
  
  kdu_long cps = tgt->class_preserve_streams[cls];
  bool force_preserve = (cps < -1) || (cps == stream_id);
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }

  kd_cache_path_walker *path = &(state->marking_path);
  kd_var_cache_seg *seg =
    path->make_path(tgt,mutex_locked,cls,stream_id,bin_id,force_preserve);
  kdu_int32 return_flags=0;
  if (seg == NULL)
    { // We ran out of memory; treat as deleted, but note that the cache-seg
      // we were looking for is internally marked as deleted and this marking
      // will remain until we are able to invoke this function successfully
      // on the same data-bin in the future, or until we call `clear_all_marks'
      // or `set_all_marks'.
      return_flags = KDU_CACHE_BIN_DELETED;
    }
  else
    { 
      assert(path->stream_info != NULL);
      int idx = (int)(bin_id & 127);
      kd_cache_buf *buf_list = seg->databins[idx];
      if (buf_list == KD_BIN_DELETED)
        { // Data-bin has been deleted and we have no data for it
          return_flags = KDU_CACHE_BIN_DELETED;
          seg->databins[idx] = NULL;
          assert(seg->num_non_null > 0);
          seg->num_non_null--;
          assert(path->stream_info->mark_counts[cls] > 0);
          path->stream_info->mark_counts[cls]--;
        }
      else if (buf_list == KD_BIN_CEMPTY)
        { // Data-bin is complete and empty, but cannot be marked
          is_complete = true;
        }
      else if (buf_list != NULL)
        { 
          kdu_int32 status = buf_list->head.status.get();
          length = (status & KD_CACHE_HD_L_MASK);
          is_complete = ((status & KD_CACHE_HD_F_BIT) &&
                         !(status & KD_CACHE_HD_H_BIT));
          kdu_int32 m_val = status & KD_CACHE_HD_M_MASK;
          if (m_val == KD_CACHE_HD_M_DELETED)
            return_flags = KDU_CACHE_BIN_DELETED | KDU_CACHE_BIN_MARKED;
          else if (m_val == KD_CACHE_HD_M_AUGMENTED)
            return_flags = KDU_CACHE_BIN_AUGMENTED | KDU_CACHE_BIN_MARKED;
          else if (m_val == KD_CACHE_HD_M_MARKED)
            return_flags = KDU_CACHE_BIN_MARKED;
          kdu_int32 new_m_val = 0;
          if (mark_state && ((status & KD_CACHE_HD_L_MASK) != 0))
            new_m_val = KD_CACHE_HD_M_MARKED;
          if (new_m_val != m_val)
            { 
              if (m_val == 0)
                path->stream_info->mark_counts[cls]++;
              else if (new_m_val == 0)
                { 
                  assert(path->stream_info->mark_counts[cls] > 0);
                  path->stream_info->mark_counts[cls]--;
                }
              status += new_m_val - m_val;
              buf_list->head.status.set(status);
            }
        }
    }

  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock();
  return return_flags;
}

/*****************************************************************************/
/*                       kdu_cache::stream_class_marked                      */
/*****************************************************************************/

bool
  kdu_cache::stream_class_marked(int cls, kdu_long stream_id)
{
  if ((cls < -1) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (stream_id < 0) || (state != state->primary))
    return false; // See API documentation
  if (cls == KDU_TILE_HEADER_DATABIN)
    cls = KDU_MAIN_HEADER_DATABIN;
  kd_cache *tgt = state;
  tgt->mutex.lock(); // Ensures thread-safety for multiple marking threads
  bool mutex_locked = true;
  if (tgt->seg_server == NULL) // We may need to allocate cache-segs to hold
    tgt->seg_server = new kd_cache_seg_server; // the marking state.
  kd_cache_path_walker *path = &(state->marking_path);
  kd_var_stream_info *stream_info =
    path->make_stream(tgt,mutex_locked,stream_id);
  bool result = false;
  if (stream_info != NULL)
    { 
      assert(stream_info == path->stream_info);
      if (cls < 0)
        { // Wildcard class
          for (int c=0; c < KDU_NUM_DATABIN_CLASSES; c++)
            if ((c != KDU_META_DATABIN) && (stream_info->mark_counts[c]))
              { 
                result = true;
                break;
              }
        }
      else if (stream_info->mark_counts[cls])
        result = true;
    }
  
  // Release the mutex before returning
  assert(mutex_locked);
  tgt->mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                          kdu_cache::clear_all_marks                       */
/*****************************************************************************/

void
  kdu_cache::clear_all_marks()
{
  if (state != state->primary)
    return;
  kd_cache *tgt = state;
  tgt->mutex.lock();
  bool mutex_locked = true;
  if (tgt->root != NULL)
    tgt->root->set_all_marks(tgt,mutex_locked,false,false,NULL);
  assert(mutex_locked);
  tgt->mutex.unlock();
}

/*****************************************************************************/
/*                           kdu_cache::set_all_marks                        */
/*****************************************************************************/

void
  kdu_cache::set_all_marks()
{
  if (state != state->primary)
    return;
  kd_cache *tgt = state;
  tgt->mutex.lock();
  bool mutex_locked = true;
  if (tgt->root != NULL)
    tgt->root->set_all_marks(tgt,mutex_locked,true,false,NULL);
  assert(mutex_locked);
  tgt->mutex.unlock();
}

/*****************************************************************************/
/*                       kdu_cache::get_databin_length                       */
/*****************************************************************************/

int kdu_cache::get_databin_length(int cls, kdu_long stream_id, kdu_long bin_id,
                                  bool *is_complete)
  /* NB: this function looks a lot like `set_read_scope' except that all of
     the activity must be performed while holding the mutex lock; otherwise
     another thread may try to use the same path walker object.  This is
     why we recommend using `set_read_scope' instead, wherever one can be
     sure that only one thread is querying the cache status via this
     particular `kdu_cache' interface. */
{
  if (is_complete != NULL)
    *is_complete = false; // Until proven otherwise
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (stream_id < 0) || (bin_id < 0))
    return 0;  
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  
  kd_cache *tgt = state->primary;
  tgt->mutex.lock();
  bool mutex_locked = true;
  kd_cache_path_walker *path = &(state->get_length_path);  
  kd_var_cache_seg *seg = 
    path->trace_path(tgt,mutex_locked,cls,stream_id,bin_id);
  int length = 0;
  if (seg != NULL)
    { 
      int idx = (int)(bin_id & 127);
      kd_cache_buf *buf_list = seg->databins[idx];
      if (_addr_to_kdu_int32(buf_list) & 3)
        { // Not a valid address
          if ((buf_list == KD_BIN_CEMPTY) && (is_complete != NULL))
            *is_complete = true;
        }
      else if (buf_list != NULL)
        { 
          kdu_int32 status = buf_list->head.status.get_barrier();
          length = status & KD_CACHE_HD_L_MASK;
          if ((is_complete != NULL) && (status & KD_CACHE_HD_F_BIT) &&
              !(status & KD_CACHE_HD_H_BIT))
            *is_complete = true;
        }
    }
  assert(mutex_locked);
  tgt->mutex.unlock();
  return length;
}

/*****************************************************************************/
/*                          kdu_cache::scan_databins                         */
/*****************************************************************************/

bool
  kdu_cache::scan_databins(kdu_int32 flags, int &cls, kdu_long &stream_id,
                           kdu_long &bin_id, int &bin_length,
                           bool &bin_complete, kdu_byte *buf, int buf_len)
{
  int preserve_test=0, preserve_xor=0;// Reject bin if (preserve_flag^xor)&test
  bool preserved_only=false;
  if (flags & KDU_CACHE_SCAN_PRESERVED_ONLY)
    { 
      preserved_only = true;
      if (flags & KDU_CACHE_SCAN_PRESERVED_SKIP)
        { 
          state->last_scan_seg = NULL;
          state->last_scan_pos = 0;
          return false;
        }
      preserve_test = preserve_xor = 1;
    }
  else if (flags & KDU_CACHE_SCAN_PRESERVED_SKIP)
    preserve_test = 1;
  int fixed_class=-1;
  kdu_long fixed_stream=-1;
  bool bin0_only = false;
  bool bin0_skip = false;
  if (flags & KDU_CACHE_SCAN_FIX_CODESTREAM)
    fixed_stream = stream_id;
  if (flags & KDU_CACHE_SCAN_FIX_CLASS)
    { 
      fixed_class = cls;
      if (fixed_class == KDU_MAIN_HEADER_DATABIN)
        bin0_only = true;
      else if (fixed_class == KDU_TILE_HEADER_DATABIN)
        { fixed_class = KDU_MAIN_HEADER_DATABIN; bin0_skip = true; }
    }
  bool skip_unmarked = ((flags & KDU_CACHE_SCAN_MARKED_ONLY) != 0);
  
  kd_cache *tgt = state->primary;
  kd_cache_path_walker *path = &(state->scan_path);
  bool mutex_locked = false;
  kd_var_cache_seg *seg = state->last_scan_seg;
  int pos = state->last_scan_pos;
  
  if (flags & KDU_CACHE_SCAN_START)
    { 
      pos = 0;      
      path->unwind_all(tgt,mutex_locked);
      seg = path->trace_next(tgt,mutex_locked,fixed_stream,fixed_class,
                             bin0_only,preserved_only,skip_unmarked,false);
    }
  else if (!(flags & KDU_CACHE_SCAN_NO_ADVANCE))
    pos++;
  if ((seg == NULL) ||
      ((fixed_class >= 0) && (fixed_class != (int)seg->class_id)) ||
      ((fixed_stream >= 0) && (fixed_stream != seg->stream_id)) ||
      (bin0_only && (seg->base_id != 0)))
    { // We can terminate early; if we did not start off in the right
      // class/codestream/bin-group, either they do not exist or the scan was
      // started with different conditions.
      if (mutex_locked)
        tgt->mutex.unlock();
      return false;
    }
  
  kd_cache_buf *buf_list=NULL;
  while (seg != NULL)
    { 
      assert(seg->flags & KD_CSEG_LEAF);
      if (!bin0_only)
        { 
          if (bin0_skip && (pos == 0) && (seg->base_id == 0))
            pos = 1;
          for (; pos < 128; pos++)
            { 
              if ((seg->preserve.get(pos) ^ preserve_xor) & preserve_test)
                continue; // Does not pass the preserved only/skip condition
              buf_list = seg->databins[pos];
              if ((buf_list != NULL) &&
                  ((buf_list == KD_BIN_CEMPTY) ||
                   (((_addr_to_kdu_int32(buf_list) & 3) == 0) &&
                    ((!skip_unmarked) ||
                     (buf_list->head.status.get() & KD_CACHE_HD_M_MASK)))))
                break;
              buf_list = NULL;
            }
          if (buf_list != NULL)
            break;
        }
      else if (pos == 0)
        { // Special case where we want only data-bin 0
          assert(seg->base_id == 0);
          if ((seg->preserve.get(0) ^ preserve_xor) & preserve_test)
            continue; // Does not pass the preserved only/skip condition
          buf_list = seg->databins[0];
          if ((buf_list != NULL) &&
              ((buf_list == KD_BIN_CEMPTY) ||
               (((_addr_to_kdu_int32(buf_list) & 3) == 0) &&
                ((!skip_unmarked) ||
                 (buf_list->head.status.get() & KD_CACHE_HD_M_MASK)))))
            break;
          buf_list = NULL;
        }
      pos = 0;
      seg = path->trace_next(tgt,mutex_locked,fixed_stream,fixed_class,
                             bin0_only,preserved_only,skip_unmarked,false);
    }
  if (mutex_locked)
    tgt->mutex.unlock();
  
  if (buf_list == NULL)
    { 
      assert(seg == NULL);
      state->last_scan_seg = NULL;
      state->last_scan_pos = 0;
      return false;
    }
  state->last_scan_seg = seg;
  state->last_scan_pos = pos;
  stream_id = seg->stream_id;
  cls = seg->class_id;
  bin_id = seg->base_id + pos;
  if ((cls == KDU_MAIN_HEADER_DATABIN) && (bin_id > 0))
    { 
      cls = KDU_TILE_HEADER_DATABIN;
      bin_id--;
    }
  if (buf_list == KD_BIN_CEMPTY)
    { bin_length = 0; bin_complete = true; }
  else
    { 
      kdu_int32 status = buf_list->head.status.get();
      bin_length = status & KD_CACHE_HD_L_MASK;
      bin_complete = ((status & KD_CACHE_HD_F_BIT) &&
                      !(status & KD_CACHE_HD_H_BIT));
      if (buf != NULL)
        { 
          int bytes_left = (bin_length < buf_len)?bin_length:buf_len;
          int read_pos = (int) sizeof(kd_cache_hd);
          while (bytes_left > 0)
            { 
              assert(buf_list != NULL);
              int xfer_bytes = KD_CACHE_BUF_LEN - read_pos;
              if (xfer_bytes > bytes_left)
                xfer_bytes = bytes_left;
              memcpy(buf,buf_list->bytes+read_pos,(size_t) xfer_bytes);
              bytes_left -= xfer_bytes;
              buf += xfer_bytes;
              if (bytes_left > 0)
                { 
                  assert(buf_list != NULL);
                  buf_list = buf_list->next;
                  read_pos = 0;
                }      
            }
        }
    }
  return true;
}

/*****************************************************************************/
/*                          kdu_cache::set_read_scope                        */
/*****************************************************************************/

int
  kdu_cache::set_read_scope(int cls, kdu_long stream_id, kdu_long bin_id,
                            bool *is_complete)
{
  state->read_buf = state->read_start = NULL;
  state->read_buf_pos = 0;
  state->databin_pos = 0;
  state->read_start = NULL;  state->read_buf = NULL;
  state->databin_status = 0;
  state->last_read_codestream_id = stream_id;
  if (is_complete != NULL)
    *is_complete = false; // Until proven otherwise
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES) ||
      (stream_id < 0) || (bin_id < 0))
    return 0;
  if (cls == KDU_TILE_HEADER_DATABIN)
    { 
      cls = KDU_MAIN_HEADER_DATABIN;
      bin_id++;
    }
  
  kd_cache *tgt = state->primary;
  kd_cache_path_walker *path = &(state->main_read_path);
  if (cls == KDU_META_DATABIN)
    path = &(state->meta_read_path);
  else if ((cls == KDU_MAIN_HEADER_DATABIN) ||
           (cls == KDU_TILE_HEADER_DATABIN))
    path = &(state->stream_read_path);
  bool mutex_locked = false;  
  kd_var_cache_seg *seg = 
    path->trace_path(tgt,mutex_locked,cls,stream_id,bin_id);
  if (mutex_locked)
    tgt->mutex.unlock();
    
  if (seg == NULL)
    return 0;
  int idx = (int)(bin_id & 127);
  kd_cache_buf *buf_list = seg->databins[idx];
  if (_addr_to_kdu_int32(buf_list) & 3)
    { // Not a valid address
      if ((buf_list == KD_BIN_CEMPTY) && (is_complete != NULL))
        *is_complete = true;
      return 0;
    }
  int length = 0;
  if (buf_list != NULL)
    { 
      kdu_int32 status = buf_list->head.status.get_barrier();
      length = status & KD_CACHE_HD_L_MASK;
      if ((is_complete != NULL) && (status & KD_CACHE_HD_F_BIT) &&
          !(status & KD_CACHE_HD_H_BIT))
        *is_complete = true;
      state->read_start = state->read_buf = buf_list;
      state->read_buf_pos = (int) sizeof(kd_cache_hd);
      state->databin_status = status;
    }
  return length;
}

/*****************************************************************************/
/*                              kdu_cache::seek                              */
/*****************************************************************************/

bool
  kdu_cache::seek(kdu_long offset)
{
  if (state->read_start != NULL)
    { 
      if (offset < 0)
        offset = 0;
      int initial_length = state->databin_status & KD_CACHE_HD_L_MASK;
      int off = initial_length;
      if (offset < (kdu_long) off)
        off = (int) offset;
      if (off < state->databin_pos)
        { // Reset position
          state->read_buf = state->read_start;
          state->read_buf_pos = (int)sizeof(kd_cache_hd);
          state->databin_pos = 0;
        }
      off -= state->databin_pos;
      while (off > 0)
        { 
          if (state->read_buf_pos == KD_CACHE_BUF_LEN)
            { 
              assert(state->read_buf != NULL); // If we ran out of memory in
                // `add_to_databin', `initial_length' would have been truncated
              state->read_buf = state->read_buf->next;
              state->read_buf_pos = 0;
            }
          int xfer_bytes = KD_CACHE_BUF_LEN - state->read_buf_pos;
          if (xfer_bytes > off)
            xfer_bytes = off;
          off -= xfer_bytes;
          state->read_buf_pos += xfer_bytes;
          state->databin_pos += xfer_bytes;
        }
    }
  return true;
}

/*****************************************************************************/
/*                            kdu_cache::get_pos                             */
/*****************************************************************************/

kdu_long
  kdu_cache::get_pos()
{
  return state->databin_pos;
}

/*****************************************************************************/
/*                        kdu_cache::set_tileheader_scope                    */
/*****************************************************************************/

bool
  kdu_cache::set_tileheader_scope(int tnum, int num_tiles)
{
  kdu_long bin_id = ((kdu_long) tnum);
  bool is_complete=false;
  if (state->last_read_codestream_id < 0)
    { KDU_ERROR_DEV(e,0); e <<
      KDU_TXT("Attempting to invoke `kdu_cache::set_tileheader_scope' "
              "without first calling `kdu_cache::set_read_scope' to identify "
              "the code-stream which is being accessed.");
    }
  set_read_scope(KDU_TILE_HEADER_DATABIN,state->last_read_codestream_id,
                 bin_id,&is_complete);
  return is_complete;
}

/*****************************************************************************/
/*                          kdu_cache::set_precinct_scope                    */
/*****************************************************************************/

bool
kdu_cache::set_precinct_scope(kdu_long bin_id)
{
  if (state->last_read_codestream_id < 0)
    { KDU_ERROR_DEV(e,1); e <<
      KDU_TXT("Attempting to invoke `kdu_cache::set_precinct_scope' without "
              "first calling `kdu_cache::set_read_scope' to identify the "
              "code-stream which is being accessed.");
    }
  set_read_scope(KDU_PRECINCT_DATABIN,state->last_read_codestream_id,bin_id);
  return true;
}

/*****************************************************************************/
/*                               kdu_cache::read                             */
/*****************************************************************************/

int
  kdu_cache::read(kdu_byte *data, int num_bytes)
{
  if (state->read_start == NULL)
    return 0;
  int read_lim = ((state->databin_status & KD_CACHE_HD_L_MASK) -
                  state->databin_pos);
  if (num_bytes > read_lim)
    num_bytes = read_lim;
  int bytes_left = num_bytes;
  while (bytes_left > 0)
    { 
      if (state->read_buf_pos == KD_CACHE_BUF_LEN)
        { 
          assert(state->read_buf != NULL); // If we ran out of memory in
            // `add_to_databin', `initial_length' would have been truncated
          state->read_buf = state->read_buf->next;
          state->read_buf_pos = 0;
        }
      int xfer_bytes = KD_CACHE_BUF_LEN - state->read_buf_pos;
      if (xfer_bytes > bytes_left)
        xfer_bytes = bytes_left;
      memcpy(data,state->read_buf->bytes+state->read_buf_pos,
             (size_t) xfer_bytes);
      bytes_left -= xfer_bytes;
      data += xfer_bytes;
      state->read_buf_pos += xfer_bytes;
      state->databin_pos += xfer_bytes;
    }
  return num_bytes;
}

/*****************************************************************************/
/*                       kdu_cache::get_max_codestream_id                    */
/*****************************************************************************/

kdu_long
  kdu_cache::get_max_codestream_id()
{
  kd_cache *tgt = state->primary;
  kd_cint val = tgt->max_codestream_id;
  if (val > KD_CINT_LONG_MAX)
    val = KD_CINT_LONG_MAX; // We don't really expect this limit to be reached
  return (kdu_long) val;
}

/*****************************************************************************/
/*                       kdu_cache::get_peak_cache_memory                    */
/*****************************************************************************/

kdu_long
  kdu_cache::get_peak_cache_memory()
{
  kd_cache *tgt = state->primary;
  kdu_long result = 0;
  if (tgt->buf_server != NULL)
    { 
      kd_cint val = tgt->buf_server->get_peak_allocated_bufs();
      val *= (kd_cint) sizeof(kd_cache_buf);
      result += (kdu_long) val;
    }
  if (tgt->seg_server != NULL)
    { 
      kd_cint val = tgt->seg_server->get_peak_allocated_segs();
      val *= (kd_cint) sizeof(kd_var_cache_seg);
      result += (kdu_long) val;
    }
  return result;
}

/*****************************************************************************/
/*                      kdu_cache::get_reclaimed_memory                      */
/*****************************************************************************/

kdu_int64
  kdu_cache::get_reclaimed_memory(kdu_int64 &peak, kdu_int64 &limit)
{
  /* Note: right now we only reclaim data-bufs, but everything is set up
     internally to also reclaim segments if required in the future. */
  kd_cache *tgt = state->primary;
  kdu_int64 result = 0;
  peak = 0;
  tgt->mutex.lock();
  if (tgt->buf_server != NULL)
    peak = tgt->buf_server->get_peak_allocated_bufs();
  limit = tgt->auto_trim_buf_threshold;
  result = tgt->total_reclaimed_bufs;
  tgt->mutex.unlock();
  peak *= KD_CACHE_BUF_BYTES;
  limit *= KD_CACHE_BUF_BYTES;
  result *= KD_CACHE_BUF_BYTES;
  return result;
}

/*****************************************************************************/
/*                      kdu_cache::get_transferred_bytes                     */
/*****************************************************************************/

kdu_int64
  kdu_cache::get_transferred_bytes(int cls)
{
  if ((cls < 0) || (cls >= KDU_NUM_DATABIN_CLASSES))
    return 0;
  kd_cache *tgt = state->primary;
  kdu_int64 val;
#ifdef KDU_POINTERS64
  val = tgt->transferred_bytes[cls];
#else
  tgt->mutex.lock();
  val = tgt->transferred_bytes[cls];
  tgt->mutex.unlock();
#endif
  return val;
}
