/*****************************************************************************/
// File: cache_local.h [scope = APPS/CACHING_SOURCES]
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
  Contains local definitions used within "kdu_cache.h"
******************************************************************************/

#ifndef CACHE_LOCAL_H
#define CACHE_LOCAL_H

#include "kdu_cache.h"

// Defined here:
namespace kd_supp_local {
  struct kd_cache_hd;
  struct kd_cache_buf;
  struct kd_cache_buf_group;
  class kd_cache_buf_server;
  class kd_cache_buf_io;
  struct kd_var_cache_flags;
  struct kd_var_stream_info;
  struct kd_var_cache_seg;
  class kd_cache_seg_server;
  class kd_cache_path_walker;
  struct kd_cache;
}

namespace kd_supp_local {
  using namespace kdu_supp;
  
/*****************************************************************************/
/*                                  kd_cint                                  */
/*****************************************************************************/

  // The `kd_cint' data type is an integer type that is large enough to
  // count things that can be stored in the cache.  On a 32-bit platform
  // we make this a 32-bit integer so that it can be read and written without
  // having to worry about synchronizing multiple threads; this will never be
  // a problem, because the `kdu_cache' stores everything in memory, so the
  // need to count more than 2^32 things can never arise.
  //   We also define KD_CINT_LONG_MAX as the maximum value of `kd_cint'
  // that can be converted to `kdu_long' without wrap-around.
#ifdef KDU_POINTERS64
  typedef kdu_int64 kd_cint;
# define KD_CINT_LONG_MAX ((kd_cint) KDU_LONG_MAX)
#else // !KDU_POINTERS64
  typedef kdu_uint32 kd_cint;
# ifdef KDU_LONG64
#   define KD_CINT_LONG_MAX ((kd_cint) 0xFFFFFFFF)
# else
#   define KD_CINT_LONG_MAX ((kd_cint) KDU_LONG_MAX)
# endif
#endif // !KDU_POINTERS64
  
/*****************************************************************************/
/*                                kd_cache_hd                                */
/*****************************************************************************/
  
#define KD_CACHE_HD_L_MASK ((kdu_int32) 0x0FFFFFFF) /* 28 LSB's of status */
#define KD_CACHE_HD_M_POS  28  /* Location of 2-bit M field in status word */
#define KD_CACHE_HD_M_MASK ((kdu_int32)(3<<KD_CACHE_HD_M_POS))
#define KD_CACHE_HD_F_BIT  ((kdu_int32) 0x40000000)
#define KD_CACHE_HD_H_BIT  ((kdu_int32) 0x80000000)
  
#define KD_CACHE_HD_M_DELETED   ((kdu_int32)(1<<KD_CACHE_HD_M_POS))
#define KD_CACHE_HD_M_AUGMENTED ((kdu_int32)(2<<KD_CACHE_HD_M_POS))
#define KD_CACHE_HD_M_MARKED    ((kdu_int32)(3<<KD_CACHE_HD_M_POS))

struct kd_cache_hd {
    void init() { hole_list=NULL; status.set(0); }
    kd_cache_buf *hole_list; // NULL unless there are holes.
    kdu_interlocked_int32 status; // See below
  };
  /* Notes:
     The first sizeof(kd_cache_hd) bytes of each non-empty data-bin in the
     cache hold the contents of this structure.  Specifically, they hold the
     information required to determine the range and/or ranges of
     bytes which are currently available for this data-bin, whether or
     not the total length of the original data-bin is known and also
     to determine any marking flags that have been deposited with the
     data-bin.
     [//]
     The `hole_list' member points to a linked list of `kd_cache_buf'
     objects which are used to store information about holes in the
     data-bin's contents.  This member will hold NULL if and only if all
     available bytes form a contiguous prefix of the data-bin's contents.
     Otherwise, the list of buffers to which this member points hold a
     sequence of 2K-1 4-byte integers, where K is the number of disjoint
     contiguous segments of data in the cache for this data-bin.  The last of
     these integers holds 0.  The remaining integers form K-1 pairs, which
     represent the start of each non-initial segment (relative to the start
     of the data-bin) and the location immediately beyond the end of the
     same segment.
     [//]
     The `status' member is a collection of bit-fields with the following
     interpretation:
     * Bits 0 to 27 hold L, the number of initial bytes from the data-bin which
       have already been loaded into the cache.  This L-byte prefix may
       possibly be followed by other ranges of bytes which are separated
       from it by holes represented by the `hole_list'.
     * Bits 28-29 hold M, the data-bin's marking flags, coded as follows:
       -- M=0 means all marking flag bits are 0
       -- M=1 means `KDU_CACHE_BIN_DELETED' and `KDU_CACHE_MARKED' are set
       -- M=2 means `KDU_CACHE_BIN_AUGMENTED' and `KDU_CACHE_MARKED' are set
       -- M=3 means `KDU_CACHE_MARKED' alone is set
     * Bit 30 hold the F flag, which is set if the final byte of this
       data-bin has already been loaded into the cache -- that does not
       necessarily mean that all earlier bytes of the data-bin have already
       been loaded, however.
     * Bit 31 holds the H flag, which is set if `hole_list' is non-NULL;
       while this might seem redundant, it allows the `status' word to
       encapsulate all information required to understand the meaning of
       the data-bin prefix that is available, and 32-bit words can always
       be read and written atomically.  In particular, if F=1 and H=0, then
       the data-bin is complete and consists of exactly L bytes, all of
       which are available.
     [//]
     When updating a data-bin, the contents of the cache-buf list should be
     updated first, after which `status' can be updated using the
     `status.barrier_set' atomic function, which represents a store with
     release semantics.  When reading from the cache, the `status' word can
     be read first using `status.get_barrier, 
  */
  
/*****************************************************************************/
/*                               kd_cache_buf                                */
/*****************************************************************************/

#define KD_CACHE_BUF_BYTES 64
  // This is the total size of a `kd_cache_buf' object; must be a multiple of
  // the length of a pointer on the current platform and also greater than
  // sizeof(kd_cache_hd).
  
#define KD_CACHE_BUF_LEN (KD_CACHE_BUF_BYTES-sizeof(void *))
  // This is the number of data bytes in a `kd_cache_buf' object; must be
  // a multiple of 4 and no less than sizeof(kd_cache_hd).

struct kd_cache_buf {
    union {
      kdu_byte bytes[KD_CACHE_BUF_LEN];
      kd_cache_hd head; // Valid only for a data-bin's first buffer
    };
    kd_cache_buf *next;
  };
  /* Variable length buffers in the cache are created as linked lists of
     these buffers.  When used in this way, the first buffer in the list
     always commences with a `kd_cache_hd' structure. */

/*****************************************************************************/
/*                            kd_cache_buf_group                             */
/*****************************************************************************/

#define KD_CACHE_BUF_GROUP_LEN 32

struct kd_cache_buf_group {
    kd_cache_buf_group *next;
    kd_cache_buf bufs[KD_CACHE_BUF_GROUP_LEN]; // Each `buf' is 8-byte aligned
  };
  /* Rather than allocating `kd_cache_buf' structures on an individual basis,
     they are allocated in multiples (groups) of 32 at a time.  This reduces
     the number of heap allocation requests and the risk of excessive
     heap fragmentation. */

/*****************************************************************************/
/*                            kd_cache_buf_server                            */
/*****************************************************************************/

class kd_cache_buf_server {
  public: // Member functions
    kd_cache_buf_server()
      { groups = NULL; free_bufs = NULL;
        allocated_bufs=peak_allocated_bufs=0; }
    ~kd_cache_buf_server()
      {
        kd_cache_buf_group *tmp;
        while ((tmp=groups) != NULL)
          { groups = tmp->next; delete tmp; }
      }
    kd_cache_buf *get()
      { /* Does no initialization of the returned buffer except to set its
           `next' member to NULL.  May return NULL if there is insufficient
           memory to allocate any more buffers. */
        if (free_bufs == NULL)
          { 
            kd_cache_buf_group *grp = new(std::nothrow) kd_cache_buf_group;
            if (grp == NULL) return NULL;
            grp->next = groups; groups = grp;
            for (int n=KD_CACHE_BUF_GROUP_LEN-1; n >= 0; n--)
              { grp->bufs[n].next = free_bufs; free_bufs = grp->bufs + n; }
          }
        kd_cache_buf *result = free_bufs;
        free_bufs = result->next;
        result->next = NULL;
        allocated_bufs++;
        if (allocated_bufs > peak_allocated_bufs)
          peak_allocated_bufs = allocated_bufs;
        return result;
      }
    void release(kd_cache_buf *head)
      { // Releases a list of buffers headed by `head'.
        kd_cache_buf *tmp;
        while ((tmp=head) != NULL)
          { head = tmp->next; tmp->next = free_bufs;
            free_bufs = tmp; allocated_bufs--; }
      }
    kd_cint get_allocated_bufs() const { return allocated_bufs; }
    kd_cint get_peak_allocated_bufs() const { return peak_allocated_bufs; }
  private: // Data
    kd_cache_buf_group *groups; // List of buffer resources for deletion
    kd_cache_buf *free_bufs; // Buffers are allocated from a free list
    kd_cint allocated_bufs, peak_allocated_bufs;
  };

/*****************************************************************************/
/*                                kd_cache_buf_io                            */
/*****************************************************************************/

  /* This class allows convenient updating of the data in a list of cache
     buffers. */
class kd_cache_buf_io {
  public: // Member functions
    kd_cache_buf_io()
      { buf_server=NULL; buf=list=NULL; buf_pos=0; }
    kd_cache_buf_io(kd_cache_buf_server *server, kd_cache_buf *list=NULL,
                    int initial_offset=0)
      { init(server,list,initial_offset); }
    void init(kd_cache_buf_server *server, kd_cache_buf *list=NULL,
              int initial_offset=0)
      /* If `list' is NULL, a new list of buffers is created when the first
         attempt is made to write data. */
      {
        assert((initial_offset >= 0) && (initial_offset <= KD_CACHE_BUF_LEN));
        buf_server = server;
        this->buf = this->list = list;
        this->buf_pos = (list==NULL)?KD_CACHE_BUF_LEN:initial_offset;
      }
    kd_cache_buf *get_list() { return list; }
    bool finish_list()
      { /* Writes a terminal 0 (32-bit word) if necessary, returning false
           if a memory failure prevented the 0 from being written. */
        if (list == NULL) return true; // No terminal 0 required
        return write_length(0);
      }
    bool advance(int num_bytes)
      { /* Advances the current location `num_bytes' into the cached
           representation, adding new cache buffers to the end of the list
           if necessary, but not writing any data at all into old or new
           cache buffers.  Returns false if there is insufficient memory
           to complete the operation. */
        while (num_bytes > 0)
          {
            if (buf_pos == KD_CACHE_BUF_LEN)
              {
                if (buf == NULL)
                  { 
                    buf = buf_server->get();
                    if (buf == NULL) return false;
                    list = buf;
                  }
                else if (buf->next == NULL)
                  { 
                    kd_cache_buf *new_buf = buf_server->get();
                    if (new_buf == NULL) return false;
                    buf = buf->next = new_buf;
                  }
                else
                  buf = buf->next;
                buf_pos = 0;
              }
            buf_pos += num_bytes; num_bytes = 0;
            if (buf_pos > KD_CACHE_BUF_LEN)
              { num_bytes = buf_pos-KD_CACHE_BUF_LEN; buf_pos -= num_bytes; }
          }
        return true;
      }
    kdu_int32 read_length()
      { /* Reads a 4-byte length value in native byte order and returns it.
           If the buffer list finishes unexpectedly, this function simply
           returns 0. */
        assert((buf_pos & 3) == 0);
        if (buf_pos == KD_CACHE_BUF_LEN)
          { 
            if ((buf == NULL) || (buf->next == NULL)) return 0;
            buf = buf->next;
            buf_pos = 0;
          }
        kdu_int32 *ptr = (kdu_int32 *)(buf->bytes + buf_pos);
        buf_pos += 4;
        return *ptr;
      }
    bool read_byte_range(kdu_int32 &start, kdu_int32 &lim)
      { /* Reads a new byte range consisting of `start' and `limt'>`start' if
           it can, returning false if `start' turns out to be zero, or if
           the buffer list terminates unexpectedly. */
        if ((buf == NULL) || ((start = read_length()) == 0) ||
            ((buf_pos == KD_CACHE_BUF_LEN) &&
             ((buf==NULL) || (buf->next==NULL))))
          { lim = 0; return false; }
        lim = read_length();
        assert(lim > start);
        return true;
      }
    bool write_length(kdu_int32 length)
      { /* Writes a 4 byte big-endian length value, augmenting the cache buffer
           list if necessary.  It is an error to write to an unaligned address
           within the buffer.  Returns false if we ran out of memory. */
        assert((buf_pos & 3) == 0);
        if (buf_pos == KD_CACHE_BUF_LEN)
          {
            if (buf == NULL)
              { 
                buf = buf_server->get();
                if (buf == NULL) return false;
                list = buf;
              }
            else if (buf->next == NULL)
              { 
                kd_cache_buf *new_buf = buf_server->get();
                if (new_buf == NULL) return false;
                buf = buf->next = new_buf;
              }
            else
              buf = buf->next;
            buf_pos = 0;
          }
        kdu_int32 *ptr = (kdu_int32 *)(buf->bytes + buf_pos);
        buf_pos += 4;
        *ptr = length;
        return true;
      }
    bool write_byte_range(kdu_int32 start, kdu_int32 lim)
      { /* Returns false if either write failed due to insufficient memory, but
           this may leave one of the two integers written. */
        assert(lim > start);
        return (write_length(start) && write_length(lim));
      }
    bool copy_from(const kdu_byte data[], int num_bytes)
      { /* Copies `num_bytes' from the `data' buffer into the cached
           representation, starting from the current location and extending
           the buffer list as necessary to accommodate the demand.  Note
           that this function may return false if insufficient memory
           could be allocated, leaving some but not all of the data written. */
        while (num_bytes > 0)
          {
            if (buf_pos == KD_CACHE_BUF_LEN)
              {
                if (buf == NULL)
                  { 
                    buf = buf_server->get();
                    if (buf == NULL) return false;
                    list = buf;
                  }
                else if (buf->next == NULL)
                  { 
                    kd_cache_buf *new_buf = buf_server->get();
                    if (new_buf == NULL) return false;
                    buf = buf->next = new_buf;
                  }
                else
                  buf = buf->next;
                buf_pos = 0;
              }
            int xfer_bytes = KD_CACHE_BUF_LEN-buf_pos;
            xfer_bytes = (xfer_bytes < num_bytes)?xfer_bytes:num_bytes;
            memcpy(buf->bytes+buf_pos,data,(size_t) xfer_bytes);
            data += xfer_bytes; num_bytes -= xfer_bytes; buf_pos += xfer_bytes;
          }
        return true;
      }
    bool operator==(kd_cache_buf_io &rhs)
      { return (buf == rhs.buf) && (buf_pos == rhs.buf_pos); }
  public:
    kd_cache_buf_server *buf_server;
    kd_cache_buf *list; // Points to head of list
    kd_cache_buf *buf; // Points to current buffer in list
    int buf_pos;
  };
  
/*****************************************************************************/
/*                            kd_var_cache_flags                             */
/*****************************************************************************/
  
struct kd_var_cache_flags {
  public: // Member functions
    bool is_empty() const { return ((f0 | f1) == 0); }
    int get(int idx) const
      { 
        int n = idx & 63; kdu_int64 f=(idx & 64)?f1:f0;
        return ((int)(f >> n)) & 1;
      }
    void set(int idx)
      { 
        kdu_int64 bit = ((kdu_int64) 1) << (idx & 63);
        if (idx & 64) f1 |= bit; else f0 |= bit;
      }
  public: // Data
    kdu_int64 f0; // Together, these store 128 1-bit flags, one for each
    kdu_int64 f1; // element referenced from a `kd_var_cache_seg'
  };
  
/*****************************************************************************/
/*                            kd_var_stream_info                             */
/*****************************************************************************/
  
struct kd_var_stream_info {
    kd_var_cache_seg *classes[KDU_NUM_DATABIN_CLASSES];
    kd_cint mark_counts[KDU_NUM_DATABIN_CLASSES];
  };
  /* Notes:
        This structure is aliased (unioned) with the `segs' and `databins'
     arrays within `kd_var_cache_seg' so that accessing the first
     `KDU_NUM_DATABIN_CLASSES' entries of `kd_var_cache_seg::segs' is
     identical to accessing the corresponding entries of this object's
     `classes' member.  The main reason for providing this structure is to
     keep track of extra codestream-wide information in the otherwise
     unused space associated with the remaining 128-`KDU_NUM_DATABIN_CLASSES'
     entries of the `kd_var_cache_seg::segs' array.
        The `mark_counts' array contains one entry for each data-bin class,
     whose purpose is to keep track of the number of elements wihtin that
     class that have marks that can be returned via `kdu_cache::mark_databin'.
     In particular, any data-bin that is carrying a deletion, augmentation or
     plain mark is included in these counts.  Each occurrence of the
     `KD_SEG_DELETED' special address within a `kd_var_cache_seg::segs' array
     is also included in the count, since these stand for deletion marks from
     data-bins whose cache-segs have been collapsed.  Note that
     `KD_SEG_DELETED' values found within this object's `classes' array are
     also counted, and occurrences of the `KD_CSEG_CONTAINER_DELETED' flag
     also contribute to the counts.  The counts apply only to elements
     that are managed by the "stream-root" cache-seg in which this structure
     is found.
  */
  
/*****************************************************************************/
/*                             kd_var_cache_seg                              */
/*****************************************************************************/

// The following are special addresses for the `databins'/`segs' member arrays
#define KD_BIN_DELETED          ((kd_cache_buf *)_kdu_int32_to_addr(1))
#define KD_BIN_CEMPTY           ((kd_cache_buf *)_kdu_int32_to_addr(2))
#define KD_SEG_DELETED          ((kd_var_cache_seg *)_kdu_int32_to_addr(1))

// The following are flags for the `flags' member
#define KD_CSEG_LEAF               ((kdu_byte) 0x01)
#define KD_CSEG_STREAM_ROOT        ((kdu_byte) 0x02)
#define KD_CSEG_RECLAIMABLE_SEG    ((kdu_byte) 0x10)
#define KD_CSEG_RECLAIMABLE_DATA   ((kdu_byte) 0x20)
#define KD_CSEG_CONTAINER_DELETED  ((kdu_byte) 0x40)
  
struct kd_var_cache_seg {
  public: // Member functions
    kd_var_cache_seg() { memset(this,0,sizeof(*this)); }
    void recycle_all(kd_cache *cache);
      /* Recursive function moves all storage back to the relevant buf/seg
         servers within `cache', including the present object itself.
         The function is called only from `kdu_cache::close', where it is
         assumed (or perhaps validated) that there are no interfaces attached
         to the cache that might hold or use access locks.  For this reason,
         erasable-descendants are considered valid recycling targets, as
         well as valid descendants. */
    void init()
      { // Resets all members except `access_ctl' to 0
        memset(&container,0,
               ((kdu_byte *)(databins+128))-((kdu_byte *)&container));
      }
    void adjust_reclaimability(kd_cache *cache);
      /* This function takes care of all the complexities of managing
         insertion, removal and re-insertion of cache-segs from the
         two global reclaimable lists, headed by `cache->reclaimable_segs_head'
         and `cache->reclaimable_data_head'.  The `flags' member tells us
         which of these lists we are already on, if any, so that removal
         and re-insertion can be handled correctly.  The function recomputes
         the `KD_CSEG_RECLAIMABLE_SEG' and `KD_CSEG_RECLAIMABLE_DATA' flags
         and inserts the cache-seg at the head (MRU position) of the relevant
         list.  The function need only be called when the segment's locking
         count is reduced to 0 by `unlock', except in a few unusual cases
         that are documented in "kdu_cache.cpp". */
    void retract_reclaimability(kd_cache *cache);
      /* Removes the cache-seg from any reclaimables list to which it might
         belong.  This is done only right before the cache-seg is made
         erasable.  To just modify the reclaimability of a segment that is
         not being made erasable, use `adjust_reclaimability'. */
    void unlock(kd_cache *cache, bool &mutex_locked,
                kd_var_stream_info *stream_container);
      /* This function implements the responsibilities of a thread that
         reduces the access-lock count in `access_ctl'.  On entry,
         `mutex_locked' indicates whether `cache->mutex' is already locked;
         on exist, this value may have changed to true if the function needed
         to lock the mutex.  The `stream_container' member is non-NULL if and
         only if the element being unlocked belongs to one of the class
         hierarchies of the associated "stream-root" seg.  This allows for
         updates in the mark counts. */
    void lock_failed(kd_cache *cache, bool &mutex_locked);
      /* Very similar to `unlock', but called if it turns out after
         incrementing the access lock in `access_ctl' that this object is
         not the one the locking thread was expecting to lock.  This can
         happen only from within `kd_cache_path_walker::trace_path' or
         `kd_cache_path_walker::trace_next' and then only if `mutex_locked'
         was false.  This function invokes `unlock' but it first figures out
         what the `stream_container' argument to `unlock' should be, since
         this might have no relationship to the caller's
         `kd_cache_path_walker::stream_info' member.  The reason for this is
         that lock failure means that the object being locked was unprotected
         and may thus have been asynchronously recycled or re-inserted into a
         different location in the cache hierarchy before the locking count
         was incremented. */
    void make_erasable(kd_cache *cache, bool &mutex_locked,
                       kd_var_stream_info *stream_container);
      /* Called from `unlock' or from any other context where it is determined
         that the current cache-seg can be made erasable.  If the cache-seg
         has already been marked as erasable (could happen if it became
         so-marked after we entered, but before we unlocked it), the function
         does nothing at all.
            As explained in the expansive notes at the end of this object
         definition, the erasable condition requires `num_non_null' and
         `num_descendants' both to be 0, all `preserve' flags to be 0, and the
         `container' member to be non-NULL (the root of the cache hierarchy
         cannot be erased).  In practice, erasability can only be determined
         and set when the `cache->mutex' is locked, so the `mutex_locked'
         argument is provided only as a formality; it will always be true on
         entry and exit from this function.  The function manipulates
         `container' to identify the present cache-seg as erasable; it then
         proceeds to acquire and release an access lock on the `container',
         whereupon `container->unlock' may recursively call back into this
         function.
            The `stream_container' argument to this function plays exactly
         the same role as it does for `unlock'. */
    void set_all_marks(kd_cache *cache, bool &mutex_locked,
                       bool leave_marked, bool was_erasable,
                       kd_var_stream_info *stream_container);
      /* Recursive function walks through the cache hierarchy removing
         `KDU_CACHE_BIN_DELETED' and `KDU_CACHE_BIN_AUGMENTED' flags and
         adding or clearing the `KDU_CACH_BIN_MARKED' flag, dependung on
         whether `leave_marked' is true or false.  The function also removes
         the `KD_BIN_DELETED' and `KD_SEG_DELETED' special addresses from the
         `databins'/`segs' arrays of all `kd_var_cache_seg' objects that it
         visits, since these special addresses are place holders for
         one or more databins which are considered to have the
         `KDU_CACHE_BIN_DELETED' mark.
            The `mutex_locked' argument is provided as a formality; it should
         be true on entry and it will remain true on exit.
            This function does walk into erasable segments and data-bins to
         modify the marks that are found inside -- later, erasable databins
         or cache-segs may be restored to regular databin or cache-segs if
         `add_to_databin' updates their contents, so it is important that
         marks are correctly updated.  The `was_erasable' argument is true
         if the current object is itself a cache-seg that has been marked
         as erasable within its container.  This is important only because
         the current function may adjust `num_non_null' to 0 which may leave
         a cache-seg in a state where it can immediately be rendered erasable,
         but there is nothing to do if the cache-seg has already been made
         erasable.  The function is always invoked on the root of the cache
         hierarchy with `was_erasable'=false, since the root node is never
         erased.
            This function generally modifies the `mark_counts' entries found
         in `kd_var_stream_info' members of "stream-root" cache-segs that
         it encounters in its recursive traversal of the cache hierarchy.
         To facilitate this, the `stream_container' argument points to the
         `kd_var_stream_info' member of the most recently encountered
         "stream-root" seg; this argument will be NULL if the current object
         is a "stream-nav" or itself a "stream-root" cache-seg. */
  public: // Data
    kdu_interlocked_int32 access_ctl; // Contains the access lock L
    kd_var_cache_seg *container; // Should not be accessed without mutex lock
    kdu_long stream_id; // -1 for "stream-nav" segs, else the codestream ID
    kdu_long base_id; // 1st contained stream or data-bin;
    kdu_byte class_id; // 255 for "stream-nav' and "stream-root" segs
    kdu_byte shift; // See below; always a non-negative multiple of 7
    kdu_byte pos_in_container; // Our position in `container->segs' array
    kdu_byte num_descendants; // Valid non-NULL pointers in `segs'/`databins'
    kdu_byte num_non_null; // Num `databins'/`segs' entries that are not NULL
    kdu_byte num_erasable; // Num `databins'/`segs' entries marked for erasure
    kdu_byte num_reclaimable_bins; // Num non-empty databins with preserve=0
    kdu_byte flags; // `KD_CSEG_RECLAIMABLE_SEG', `KD_CSEG_RECLAIMABLE_DATA',
      // `KD_CSEG_LEAF', `KD_CSEG_STREAM_ROOT' &/or `KD_CSEG_CONTAINER_DELETED'
    kd_var_cache_flags preserve; // Elements not to be auto-trimmed
    kd_var_cache_seg *reclaim_prev; // used to build "reclaimable-data" and
    union {                        // "reclaimable-segs" lists.
      kd_var_cache_seg *reclaim_next; 
      kd_var_cache_seg *free_next; // Only when on a list of recycled segs
      };
    union {
      kd_var_cache_seg *segs[128];
      kd_cache_buf *databins[128];
      kdu_interlocked_ptr elts[128]; // Used to make atomic updates
      kd_var_stream_info stream; // `classes' member maps to `segs' array
      };
  };
  /* This structure is the basis for a dynamically expandable set of cache
     entries.  The entire cache hierarchy is a tree structure, whose leaf
     nodes contain the `databins', each of which is represented by a linked
     list of `kd_cache_buf' objects.  Associated with every data-bin is a
     path that leads from the root of the hierarchy down to the data-bin
     itself, following links found in the `segs' array.
     --------------------------------------------------------------------------
     Basic properties of the cache hierarchy:
     ----------
       1) Each path to a data-bin involves three different types of cache-segs:
          a) "stream-nav" segments are used to navigate from the root of the
             hierarchy to a particular codestream.  These have `stream_id' < 0
             because they do not represent or belong to just one codestream.
             For "stream-nav" segs, the `base_id' identifies the first
             codestream that can be represented by the segment; the last
             codestream that can be represented by the segment is found by
             adding `base_id' to 2^{`shift'+7}, as explained below.  The leaves
             of the "stream-nav" part of the hierarchy have `shift'=0; their
             `segs' array contains pointers to "stream-root" segments.
          b) Each path to a data-bin contains one "stream-root" segment,
             which serves to represent a specific codestream -- the one whose
             identifier is found in `stream_id'.  Stream-root segments are
             special in that their `segs' array has a different organization
             to that found elsewhere and only `KDU_NUM_DATABIN_CLASSES'
             elements of this array can be used; they are aliased with
             the `head.classes' array.  Stream-root segments may be identified
             either through the presence of a non-negative `stream_id' member
             with 255 for the `class_id' member, or else via the presence of
             the `KD_CSEG_STREAM_ROOT' flag within the `flags' member.  Both
             conditions should always hold together or fail together.
                In particular, `segs[c]' holds the root of the data-bin
             hierarchy associated with data-bin class c, for each c in the
             range 0 to `KDU_NUM_DATABIN_CLASSES'-1.  For simplicity, tile
             header and codestream main header data-bins are all collapsed
             under the `KDU_MAIN_HEADER_DATABIN' class, adding 1 to all
             tile header data-bin identifiers and using the identifier 0 for
             the main header.
                 Note that the `KDU_META_DATABIN' class can have a non-NULL
             root only in code-stream 0.  Besides this, we expect to have
             non-NULL class hierarchy roots only for the classes
             `KDU_PRECINCT_DATABIN' and `KDU_MAIN_HEADER_DATABIN'.
          c) Each path to a data-bin contains one or more "class-nav"
             segments that navigate within the data-bin class hierarchy that
             hangs off one of the `segs' entries in the relevant "stream-root".
             For "class-nav" segments, the `base_id' identifies the first
             in-class data-bin identifier associated with databins that can
             be contained within the segment, while the last such identifier
             is found by adding `base_id' to 2^{`shift'+7}, as explained below.
             The leaves of the "class-nav" part of the hierarchy have
             `shift'=0 and their `databins' array contains pointers to linked
             lists of `kd_cache_buf' objects.
       2) Since each path to a data-bin always terminates at a "class-nav"
          segment with `shift'=0, we identify these as "leaf-seg"s.  Each
          "leaf-seg" has the `KD_CSEG_LEAF' flag in its `flags' member and
          belongs to a doubly-linked list of leaf-segs managed via the
          `kd_cache::leaf_segs_head' and `kd_cache::leaf_segs_tail' members.
          Leaf-segs use the `databins' array, while all other cache-segs use
          the `segs' array.
       3) The cache can grow by adding cache-bufs to the lists managed by
          the `databins' array of any leaf-seg.  The cache may also grow by
          inserting cache-segs into the hierarchy.  Importantly, this may
          result in an existing path from the root to a data-bin becoming
          longer, due to insertion in the middle (between the "stream-root"
          and the class roots that are found in its `segs' array) or at
          the start (growing the breadth of the codestream hierarchy).
       4) The cache can also shrink by deleting cache-buf lists from leaf-segs
          or by removing cache-segs themselves.  Deletion is delicate, so
          it is generally managed in two phases, as follows:
          a) Cache-bufs can always be deleted from a leaf segment with
             `num_reclaimable_bins' > 0, but this is done in two steps:
             first the address(es) found in the relevant `databins' entries
             are marked with a special erasure code; then, if or when there
             are no threads holding an access lock (see `access_ctl'), the
             cache-bufs are actually removed from the leaf-seg and recycled.
          b) Cache-segs themselves can sometimes be recycled, but only if
             marked with the `KD_CSEG_RECLAIMABLE_SEG' flag.  A reclaimable-seg
             can be a leaf or non-leaf cache-seg.  It must have
             `num_descendants'=0 and all `preserve' flags 0.  It is not
             actually reclaimable until any erasable elements have been erased,
             but this happens when there are no locks on the cache-seg, which
             is the only point at which the cache-seg can be considered truly
             reclaimable anyway.  Again, recycling of reclaimable cache-segs
             proceeds in two steps: first the address found in its `container's
             `segs' array is marked with a special erasure code; then, if or
             when there are no threads with access locks (see `access_ctl') to
             its `container', the reclaimable-seg is actually removed from its
             `container' and recycled.
       5) A key design principle is that it should be possible for threads to
          navigate the hierarchy, from its root to a data-bin of interest,
          without having to lock the global cache mutex, even though
          elements may be asynchronously added to and deleted from the
          hierarchy.  Such additions and deletions, however, only occur while
          the global cache mutex is locked.
             In order to traverse the cache hierarchy, a path walker
          progressively acquires access locks on `kd_var_cache' objects by
          atomically incrementing `access_ctl' (see below).  While this may
          seem safe at first, a key question is what happens if a new cache seg
          is inserted into the path asynchronously.  The path walker may
          miss this inserted segment and hence not acquire any lock on it.
          This is not in itself a problem, except that the cache segment that
          follows the insertion may then potentially be removed from the
          hierarchy (if it becomes a "reclaimable-seg") before the path walker
          has a chance to increment the locking count in the `access_ctl'
          member.  In this way, threads that traverse the hierarchy without
          holding a lock on the global cache mutex need to be prepared for
          the fact that they may acquire a lock on a cache-seg that is either
          in the recycled state or may in fact have been inserted back into the
          hierarchy at a completely unrelated location.  This condition is
          readily detected while the lock is held, simply by re-evaluating
          the address that was followed from the preceding (locked) cache-seg,
          right after taking out a new lock.  If a mismatch is detected,
          the cache seg can be unlocked and the navigation can begin again.
          Note carefully, however, that any thread that decrements a cache
          segment's `access_ctl' counter to 0 has special responsibilities
          that must be carried out, regardless of whether or not that segment
          has been moved to a different part of the hierarchy.
     --------------------------------------------------------------------------
     Navigating with `shift' and `base_id':
     ----------
        To navigate "stream-segs" with a codestream ID, one starts at the root
     of the hierarchy, which is always a "stream-seg".  It follows that all
     available codestreams must lie in the range `base_id' (always 0 for the
     root) to `base_id'+2^{shift+7}-1.  If ID lies outside this range, it is
     not available.  Otherwise, one follows the address found at `segs[s]'
     where s = (ID-base_id) >> shift; if the address points to a valid
     descendant the search can continue.  If `shift'=0, any valid descendant
     must be the relevant codestream's "stream-root".  Evidently, `shift' must
     always be a multiple of 7.  If the range of codestreams that can be
     represented within the hierarchy is found to be too small, a writer
     thread may insert new segments into the hierarch, as explained above,
     which does not invalidate any segments that are already being used.  New
     segments, however, are only inserted while the global cache mutex is held.
        Exactly the same procedure is followed to navigate "class-segs" with
     an in-class data-bin ID.
     --------------------------------------------------------------------------
     Special addresses
     ----------
     A valid descendant of a cache-seg corresponds to a non-NULL address that
     is at least 4-byte aligned, found in the aliased `databins' or `segs'
     arrays.  The number of such valid addresses is counted by the
     `num_descendants' member.
        Apart from valid descendants, we also introduce "erasable-descendants"
     that are not counted by the `num_descendants' or `num_reclaimable_bins'
     members.  An erasable-descendant is a cache-buf list in the `databins'
     array or a cache-seg in the `segs' array whose raw address has been
     artificially marked by adding 1, so that it is no longer a muliple of 4.
     The object referenced by such addresses still exists and may already be
     in use by one or more reading threads, but any thread that encounters
     an erasable-descendant while navigating the cache hierarchy and does not
     hold a lock on the global cache mutex must not follow the address, since
     the associated objects are liable to be removed from the cache and
     recycled -- the protocol for doing this is safe for threads
     that gained access to the erasable-descendant before it became erasable,
     noting that these threads had already taken out a lock on the containing
     cache-seg by incrementing L.
        Erasable-descendants are counted by the `num_erasable' member and are
     also included in the `num_non_null' count.
        In addition to valid and erasable descendants, we use the following
     special addresses, each occurrence of which is included in the
     `num_non_null' count.
     * `KD_BIN_DELETED' may be found in the `databins' array of a leaf-seg if
       the associated data-bin was removed from the cache but its deletion has
       not yet been noted externally via a call to `kdu_cache::mark_databin'.
       This special address preserves this information until it can be
       discovered, whereupon the address becomes NULL.
     * `KD_SEG_DELETED' may be found in the `segs' array if a childless cache
       segment was removed before the `kdu_cache::mark_databin' could be used
       to discover all of the databins that had been deleted.  If
       `kdu_cache::mark_databin' encounters this special address while
       attempting to discover and/or modify marks on a data-bin, it actually
       recreates all relevant cache-segs, so that it can retain marking
       changes -- these may be destroyed again if cache trimming operations
       necessitate the release of more memory.  The `mark_databin' function
       can always recreate cache-segs because it holds the global cache
       mutex while executing.
     * `KD_BIN_CEMPTY' may be found in the `databins' array of a leaf-seg if
       the associated data-bin is known to be both "complete and empty".  The
       CEMPTY status refers to data-bins whose representation in the original
       source never had any bytes whatsoever.  This is common for tile-header
       data-bins and may also occur for many precinct data-bins, if the content
       was originally compressed quite heavily.  Rather than allocate a
       separate `kd_cache_buf' object to retain the CEMPTY status, it is
       simplest to mark such data-bins with this special address.  CEMPTY
       databins do not contribute to the cache-seg's `num_descendants' or
       `num_reclaimable_bins' values, but they do contribute to its
       `num_non_nulls' count.
     --------------------------------------------------------------------------
     Preserve flags
     ----------
     The `preserve' member contains one flag bit for each of the 128 elements
     found in the `databins' or `segs' array, as appropriate.  These flags may
     be used to identify elements that should be preserved against
     auto-trimming operations.  While explicit `kdu_cache::delete_databin'
     calls can delete a databin marked for preservation, auto-trimming
     operations will not do this.  Also, no cache-seg that contains any
     non-zero `preserve' flags can be reclaimed for re-use of its storage.
     --------------------------------------------------------------------------
     Structure and interpretation of `access_ctl'
     ----------
     `access_ctl' holds a locking count L, that is used to protect continued
     access to the `cache-seg' and any of its immediate descendants that have
     already been discovered by reading a valid address from the `databins' or
     `segs' array.  Locks are acquired and released by path-walkers,
     implemented via the `kd_cache_path_walker' class, that build, remove and
     modify a single path from the root of the cache hierarchy to a data-bin
     of interest, if possible.  Each `kdu_cache' has a collection of path
     walkers that can be used to provide hard retention of particular access
     contexts.  Moreover, `kdu_cache' objects may share the use of one common
     cache via the `kdu_cache::attach_to' function, contributing their
     path walkers to the set that can acquire access locks on individual
     cache-segs.
        Reading threads do not need to lock the cache's global mutex, which
     means that many readers can co-exist with relatively little access
     contention.  Instead, they use path walkers to take out or maintain a lock
     on the cache-seg by atomically incrementing the L count in `access_ctl'.
     In order to do this reliably, the path walker must already hold a lock on
     the `kd_cache_seg' or `kd_cache_stream' that points to this object.
        So long as L is non-zero, none of the elements referenced by this
     cache-seg can be recycled; but they can be marked as erasable.
     When a thread is about to decrement the lock count L to 0, it first
     acquires the global cache mutex, if it has not already done so.  Only
     then is the lock count decremented.  If this does indeed leave L
     equal to 0, the thread that has just locked the global cache mutex is
     responsible for actually erasing any elements that were previously
     marked as erasable (`num_erasable' > 0), which is always
     safe at this point, and carrying out additional duties, as explained
     below.
        It can happen that a cache-seg is asynchronously inserted into the
     path that a path walker has traversed to a data-bin of interest,
     resulting in holes in a path walker's sequence of access locks.  This
     does not itself cause any problems, since the path walker only unlocks
     cache-segs it has locked, as it unwinds the path and walks back down to
     a new data-bin of interest.  Even though such unlocked holes may exist,
     the unlocked cache-segs cannot be removed from the cache so long as they
     have descendants and those descendants will not be removed so long as
     they are locked. by any reading thread.
        What can happen, however, is that a new cache-seg is inserted between
     a cache-seg that has been locked by the path walker and one that it
     has accessed but has not yet locked.  The path walker needs to be
     prepared for the possibility that by the time it gets around to locking
     the new segment, it has already been removed from the cache and recycled,
     or perhaps even re-inserted into a completely unrelated part of the cache
     hierarchy.  Cache-segs are not actually deleted from memory, so taking the
     lock is safe, but immediately following the acquisition of a lock,
     the path walker must re-read the address that it followed to the segment
     that has just been locked, to make sure that the path was not broken by
     insertion of a new cache-seg before the lock was taken.  If this has
     happened, the path walker must unlock the cache-seg, perform all the
     duties of an unlocker (see below) and then repeat its attempt to walk
     towards the data-bin that is ultimately of interest.
     --------------------------------------------------------------------------
     MRU Lists
     ----------
     The `kd_cache' object maintains two doubly-linked lists of cache-segs
     that help an auto-trimming mechanism to make sensible decisions regarding
     the reclamation of both cache buffers and cache-segs based on memory usage
     guidelines.  These lists are both connected via the `reclaim_prev' and
     `reclaim_next' pointers, so a cache-seg may belong to at most one of them.
     The lists are as follows:
        a) `kd_cache::reclaimable_data' is a list that includes all
           leaf-segs with `num_reclaimable_bins' > 0.  A leaf-seg that
           belongs to this list has the `KD_CSEG_RECLAIMABLE_DATA' flag set.
           Some/all of the data buffers associated with these segs can be
           reclaimed if storage runs low, so long as the access lock count
           in `access_ctl' is 0.
        b) `kd_cache::reclaimable_segs' is a list that includes all
           cache-segs (both leaf and non-leaf) with `num_descendants'=0, and
           all `preserve' flags 0.  These are cache-segs that can be recycled
           (as opposed to cache buffers that can be recycled) so long as the
           access lock counts in `access_ctl' and `container->access_ctl' are
           both 0.  Once a reclaimable-seg is marked as erasable within its
           `container' (can only happen if the reclaimable-seg has no access
           locks), it is removed from the "reclaimable-segs" list; then, once
           the `container' has no access locks, the erasable segment is
           actually recycled.  The `KD_CSEG_RECLAIMABLE_SEG' flag is set
           if and only if the cache-seg belongs to the "reclaimable-segs"
           list.
     Both lists are sorted based on an MRU (most-recently-used) principle,
     which aims to keep the most recent entries at the head of the list and the
     least recent ones at the tail.  In practice, the position occupied by a
     cache-seg on this list is not updated when the cache-seg is used, but when
     its access lock counter (in `access_ctl') goes to 0.  This is expected to
     be much more efficient, since updating the lists requires acquisition of
     the global cache mutex.
        In addition to the above, a cache-seg may live on a list of recycled
     segments; these are singly-linked via the `free_next' pointer, which is
     aliased with `reclaim_next'.
     --------------------------------------------------------------------------
     Responsibilities of an unlocking thread
     ----------
     When a thread is about to reduce the locking count L in `access_ctl' to 0,
     it must first acquire the global cache mutex (if it has not already done
     so) and then decrement L.  If this does not leave L=0, the thread can
     release the mutex and continue (unless it had other business to do while
     holding the mutex).  If L is left equal to 0, the thread can be sure that
     no other thread will access any erasable-descendant, since any subsequent
     thread that asynchronously increments the locking count L is certain to
     see erasable descendants marked as such, with invalid addresses, and not
     follow them.  With this in mind, the thread's duties are as follows:
     1) Erase all erasable-descendants, recycling them for future re-use and
        returning their addresses to NULL, or one of the special values,
        `KD_BIN_DELETED', `KD_SEG_DELETED' or `KD_BIN_CEMPTY'.  Along the way,
        the thread must update the `num_non_null' and `num_erasable'
        counters.  None of the other counters or flags are affected by the
        erasure of erasable-descendants, since `num_descendants',
        `num_reclaimable_bins' are all insensitive to the existence of
        erasable-descendants (as if they had been erased already).
     2) If the cache-seg had the `KD_CSEG_RECLAIMABLE_SEG' flag, the unlocking
        thread may be able to immediately make the cache-seg erasable, without
        waiting for this to be done based on a cache trimming memory threshold.
        In particular, the cache-seg can be removed from the
        `kd_cache::reclaimable_segs' list and made erasable right away if
        `num_non_null' is now 0.  In this case, the thread removes the
        cache-seg from the reclaimable-segs list, marks it as erasable within
        its `container' and atomically checks the locking count L in
        `container->access_ctl' (a memory barrier is generally required in
        conjunction with this check).  If L=0, the erasable segment can itself
        be immediately recycled, which is done by recursively applying steps
        1 and 2 to the `container'.
     3) Except where the cache-seg became erasable (step 2 above), the
        unlocking thread must finish up by moving it to the head of the
        relevant MRU list -- i.e., to the most-recently-used position on that
        list.  Note that leaf-segs may belong to two such lists: they always
        belong to the `kd_cache::leaf_segs' list; and they may also belong to
        either the `kd_cache::reclaimable_data' or the
        `kd_cache::reclaimable_segs' list.
     Typically, an unlocking thread that took out a lock on the global cache
     mutex to accomplish the above steps will retain its mutex lock until it
     has finished all required unlocking operations, so as to avoid having
     to re-acquire it.  The thread may need to remove multiple locks as it
     walks back along a path through the cache hierarchy to a point at which
     it can walk forward again to a new data-bin of interest.
     --------------------------------------------------------------------------
     Allocation failures and the `KD_CSEG_CONTAINER_DELETED' flag
     ----------
     It can happen that a call to `kdu_cache::add_to_databin' cannot add new
     content because it is unable to allocate new cache buffers, or new
     cache-segs to record the data.  If this happens, marks are left behind
     that are consistent with the data having been added and then subsequently
     deleted.  In most cases, this is easy to achieve.  However, if the
     addition of new data required the insertion of a `kd_var_cache_seg'
     ahead of a current root in the stream or in-class portions of the cache
     hierarchy, and that cache-seg could not be allocated, the existing root
     is marked with the special `KD_CSEG_CONTAINER_DELETED' flag.  Later, if
     we do manage to create the parent `kd_var_cache_seg' and insert it
     ahead of the current root, the `KD_CSEG_CONTAINER_DELETED' flag is moved
     to the new root and all of its descendants (other than the one at
     element 0, which already exists) are marked with `KD_SEG_DELETED'.
     This policy is the only robust way to manage interaction with a JPIP
     server that is modelling the client cache based on the assumption that
     all delivered content is cached -- the deletion information is used
     in subsequent requests to correct the server's cache model.  All of
     these deletion hints are cleared by calls to `kdu_cache::clear_all_marks'
     and `kdu_cache::set_all_marks'.
   */
  
/*****************************************************************************/
/*                            kd_cache_seg_server                            */
/*****************************************************************************/
  
class kd_cache_seg_server {
  public: // Member functions
    kd_cache_seg_server()
      { free_segs=NULL; allocated_segs=peak_allocated_segs=0; }
    ~kd_cache_seg_server()
      { 
        while (free_segs != NULL)
          { kd_var_cache_seg *seg=free_segs;
            free_segs=seg->free_next; delete seg; }
      }
  kd_var_cache_seg *get()
    { /* NB: This function returns NULL if it cannot recycle or allocate any
         new segments.  The caller needs to be prepared for this
         possibility. */
      kd_var_cache_seg *seg = free_segs;
      if (seg == NULL)
        { seg = new(std::nothrow) kd_var_cache_seg;
          if (seg == NULL) return NULL; }
      else
        { free_segs = seg->free_next;  seg->free_next = NULL; }
      allocated_segs++;
      if (allocated_segs > peak_allocated_segs)
        peak_allocated_segs = allocated_segs;
      return seg;
    }
  void release(kd_var_cache_seg *seg)
    { // Releases a cache-seg
      assert(allocated_segs > 0);
      assert(seg->container == NULL);
      assert(!(seg->flags & // We should not belong to any list
               (KD_CSEG_RECLAIMABLE_DATA | KD_CSEG_RECLAIMABLE_SEG)));
      seg->init();
      seg->free_next = free_segs;
      free_segs = seg;
      allocated_segs--;
    }
  kd_cint get_allocated_segs() const { return allocated_segs; }
  kd_cint get_peak_allocated_segs() const { return peak_allocated_segs; }
  private: // Data
    kd_var_cache_seg *free_segs;
    kd_cint allocated_segs, peak_allocated_segs;
  };
  
/*****************************************************************************/
/*                             kd_cache_path_walker                          */
/*****************************************************************************/

class kd_cache_path_walker {
  public: // Member functions
    kd_cache_path_walker()
      { path_len = max_path_len = 0;  path = NULL; stream_info = NULL; }
    ~kd_cache_path_walker()
      { if (path != NULL) delete[] path; }
    void reset()
      { while (path_len > 0) path[--path_len]=NULL; stream_info = NULL; }
      /* The above function should only be called from `kdu_cache::close' when
         there was no attached cache, in which case it is safe just to reset
         the `path' to the empty state without unlocking anything. */
    void unwind_all(kd_cache *cache, bool &mutex_locked)
      { while (path_len > 0) unwind(cache,mutex_locked); }
      /* `mutex_locked' keeps track of whether or not this thread has
         acquired a lock on the `cache->mutex' object.  If true on entry,
         the value will be true on return, but if false on entry, the
         value may either be true or false on return, depending on what
         we found we had to do as we removed our access lock on each
         segment in the path. */
    kd_var_cache_seg *
      make_path(kd_cache *cache, bool &mutex_locked,
                int class_id, kdu_long stream_id, kdu_long databin_id,
                bool force_preserve);
      /* If `cache->mutex' is not already locked on entry, it is will be
         locked inside this function which will return with it locked.
         Whether or not this thread has a lock on `cache->mutex' is recorded
         on entry and exit from this function by the `mutex_locked' variable,
         so for this function, `mutex_locked' will always be true on exit.
         For `trace_path', though, this is often not the case.
            This function modifies the path as required, adding cache-segs if
         required, in order to return a leaf-seg that contains the data-bin
         of interest.  The actual location of the data-bin within the
         returned object's `databins' array is easily identified by subtracting
         `databin_id' from `kd_var_cache_seg::base_id', which is guaranteed
         to lie in the range 0 to 127.
            The function ensures that no cache-seg along the path to the
         desired data-bin is marked as erasable, and also makes sure that the
         data-bin itself is not marked as erasable, removing erasure marks
         wherever necessary.  However, it is possible that the address found
         in the relevant entry of the returned object's
         `kd_var_cache_seg::databins' array is one of the special values
         `KD_BIN_DELETED' or `KD_BIN_CEMPTY'.  The caller may choose to
         replace a data-bin marked using the `KD_BIN_DELETED' special address
         with an actual list of cache-bufs, retaining the information that
         the contents of the bin were at some point deleted -- that information
         needs to be recovered later by calls to `kdu_cache::mark_databin'.
            If `force_preserve' is true, the function also makes sure that
         the entire path from the root of the cache hierarchy to the data-bin
         of interest is marked with preservation flags via the
         `kd_var_cache_seg::preserve' member.  This may require some
         backtracking through the path to add preservation flags to elements
         that precede the current one.
            This function can return NULL only if it was unable to allocate
         sufficient memory to accommodate new `kd_var_cache_seg's that needed
         to be inserted into the cache hierarchy.  If this happens, all
         relevant adjustments have been made to the cache to ensure that
         data-bins that we cannot add will show up as deleted in calls to
         `kdu_cache::mark_databins' so that if we are able to allocate the
         memory in the future, a JPIP server's cache model will be kept up
         to date. */
    kd_var_stream_info *
      make_stream(kd_cache *cache, bool &mutex_locked, kdu_long stream_id);
      /* Similar to `make_path' but this function is content to have, extend
         or create a path that includes or even concludes with the
         "stream-root" cache-seg for the indicated codestream.  Creates the
         "stream-root" cache-seg if necessary, returning NULL only if we
         run out of memory unexpectedly.  As with `make_path', this function
         is usually called with `mutex_locked'=true on entry, but if anything
         needs to be created the mutex will be locked and `mutex_locked' will
         be true on exit. */
    kd_var_cache_seg *
      trace_path(kd_cache *cache, bool &mutex_locked,
                 int class_id, kdu_long stream_id, kdu_long databin_id);
      /* Similar to the above function, but this one does not create any
         cache-segs.  If the path to the desired data-bin cannot be
         completed, the function returns NULL.  However, a non-NULL return
         does not mean that the data-bin exists in the cache.  For that
         you will need to inspect the relevant `databins' entry in the
         returned object.  Usually, this function is called with
         `mutex_locked'=false, but it may also be called while holding
         a lock on the mutex.  Upon return, `mutex_locked' often remains
         false (if it was false on entry), but it may have become true, so
         you must be prepared to unlock `cache->mutex' later. */
    kd_var_cache_seg *
      trace_next(kd_cache *cache, bool &mutex_locked,
                 kdu_long fixed_stream_id, int fixed_class_id, bool bin0_only,
                 bool preserved_only, bool skip_unmarked, bool skip_meta);
      /* This function is used to implement `kdu_cache::scan_databins'.  It
         behaves in a similar manner to `trace_path', always returning a
         leaf-seg, or else NULL.  If the path-walker is already at a leaf-seg,
         it advances to the next leaf-seg in the cache which satisfies the
         specifications associated with the last four arguments (see below).
         Otherwise, the function walks down from the cache root to the first
         leaf-seg it can find that matches these specification.
            If `fixed_stream_id' is non-negative, the function skips over
         all cache-segs that do not belong to the indicated codestream.  In
         practice, this means that as soon as the scan enters a codestream
         whose stream-id is larger than `fixed_stream_id', it can immediately
         return NULL.
            If `fixed_class_id' is non-negative, the function skips over
         all cache-segs that do not belong to the indicated databin class.
         Note that `fixed_class_id' will never be `KDU_TILE_HEADER_DATABIN'; if
         the intention is to tile header data-bins only, the caller
         should pass `KDU_MAIN_HEADER_DATABIN' for the `fixed_class_id', since
         main and tile header data-bins are collapsed into a single class
         in the cache hierarchy.
            If `bin0_only' is true, the function skips over all cache-segs
         that do not contain data-bin 0 for the relevant databin-classes and
         streams.  This is useful when scanning for only the main header
         data-bins of each codestream, since tile-header databins appear in
         the main header data-bin class with non-zero bin-id's.
            When advancing into a new cache-seg, if `preserved_only' is true,
         the function checks the `preserve' flag associated with that cache-seg
         within its container, skipping the cache-seg if it is 0.
            If `skip_unmarked' is true, the function completely skips over
         class hierarchies (within stream-roots) whose mark count is 0,
         as recorded in the `kd_var_stream_info::mark_counts' array.
            Similarly, if `skip_meta' is true, the function completely skips
         over class hierarchies (within stream-roots) whose class-id is
         `KDU_META_DATABIN'. */
  private: // Helper functions
    void add_to_path(kd_var_cache_seg *seg)
      { 
        if (path_len == max_path_len)
          { // Grow the `path' array
            max_path_len += max_path_len + 4;
            kd_var_cache_seg **new_path = new kd_var_cache_seg *[max_path_len];
            for (int n=0; n < path_len; n++)
              new_path[n] = path[n];
            if (path != NULL)
              { kd_var_cache_seg **tmp=path; path=new_path; delete[] tmp; }
            else
              path = new_path;
          }
        path[path_len++] = seg;
      }
      /* Augments the path by adding `seg' to the end; all relevant locks
         have already been taken. */
    bool unwind(kd_cache *cache, bool &mutex_locked)
      { 
        if (path_len < 1) return false;
        kd_var_cache_seg *seg=path[path_len-1];  path[--path_len] = NULL;
        kd_var_stream_info *stream_container = this->stream_info;
        if (seg->flags & KD_CSEG_STREAM_ROOT)
          this->stream_info = NULL;
        seg->unlock(cache,mutex_locked,stream_container);
        return true;
      }
      /* If the path is empty, this function returns false; otherwise it pops
         the most recently added path segment and unlocks it, returning
         true. */
  private: // Data
    int path_len; // Number of elements in `path' that are access locked
    int max_path_len; // Max pointers that `path' can accommodate
    kd_var_cache_seg **path;
  public:
    kd_var_stream_info *stream_info; // Points to locked "stream-root" if any
  };
  
/*****************************************************************************/
/*                                  kd_cache                                 */
/*****************************************************************************/

struct kd_cache {
  public: // Member functions
    kd_cache()
      { 
        buf_server = NULL; seg_server = NULL; root = NULL;
        primary = this; attached_head = attached_next = NULL;
        reset_state();
        mutex.create();
      }
    ~kd_cache()
      { 
        assert((root == NULL) && (buf_server == NULL) && (seg_server == NULL));
        assert((primary == this) && (attached_head == NULL) &&
               (attached_next == NULL));
        mutex.destroy();
      }
    void close(bool &primary_mutex_locked);
      /* Does all the work of `kdu_cache::close', but takes an extra argument
         that keeps track of with the `primary' cache's mutex is locked by
         the caller, modifying the locking state as appropriate.  If
         `primary' differs from `this' on entry, the function detaches
         the object from the `primary' cache to which it is attached and
         then resets `this->primary' to `this'.  If `primary' is identical to
         `this' on entry, the function closes (and hence detaches) all
         objects that are attached to us. */
    void attach_to_primary(kd_cache *tgt);
      /* This function must be called while `tgt->mutex' is locked. */
    void reset_state();
      /* Reset all state variables to their natural initial values, deleting
         the `root' of the hierarchy and any buf/seg servers as well.  The
         only member that this function does not touch is the `mutex'.  Note
         that the `reset' function is invoked on all path walker members; if
         you are detaching from another (primary cache) you should first use
         the `unwind_all' function on these path walkers to remove access
         locks within the primary cache. */
    void reclaim_data_bufs(kd_cint num_bufs, bool &mutex_locked);
      /* This function aims to reclaim at least `num_bufs' `kd_cache_buf'
         buffers.  To do this, it removes cache-segs from the tail of the
         `reclaimable_data_tail' list, moving locked buffers back to the head
         of the list, and reclaiming all reclaimable buffers from each
         unlocked buffer removed, until the threshold is reached.  The function
         may terminate early if it cannot find enough reclaimable data segs
         that are not currently locked, in which case it leaves the
         `all_reclaimable_data_locked' flag true.
            The function updates the `total_reclaimed_bufs' member to reflect
         the total number reclaimed by this function since the object was
         created or reset.
            For consistency with most other functions in the internal
         `kdu_cache' implementation, this one takes a `mutex_locked' in-out
         argument.  If `mutex_locked' is false on entry, the `mutex' is
         locked and `mutex_locked' is set to true, but the function never
         returns it to false.  The caller is expected to eventually unlock
         the `mutex'. */
  public: // Owned resources and cache state
    kdu_mutex mutex;
    kd_cache *primary; // Either `this' or else cache to which we are attached
    kd_cache *attached_head; // Non-NULL (perhaps) only in `primary' cache
    kd_cache *attached_next; // Links non-primary objects attached to `primary'
    kd_cache_buf_server *buf_server; // Created on demand
    kd_cache_seg_server *seg_server; // Created on demand
    union {
      kd_var_cache_seg *root;
      kdu_interlocked_ptr atomic_root; // For atomic updates
    };
  public: // Statistics and auto-trim thresholds and counters
    kd_cint max_codestream_id; // Saturates rather than wrapping around
    kd_cint auto_trim_buf_threshold; // 0 means that no threshold applies
    kd_cint auto_trim_seg_threshold; // 0 means that no threshold applies
    kdu_int64 total_reclaimed_bufs;
    kdu_int64 total_reclaimed_segs;
    kdu_int64 transferred_bytes[KDU_NUM_DATABIN_CLASSES];
  public: // MRU lists (head is the MRU position, tail is the LRU position)
    kd_var_cache_seg *reclaimable_data_head;
    kd_var_cache_seg *reclaimable_data_tail;
    bool all_reclaimable_data_locked; // Reduces pointless auto-trim searches
    kd_var_cache_seg *reclaimable_segs_head;
    kd_var_cache_seg *reclaimable_segs_tail;
    bool all_reclaimable_segs_locked; // Reduces pointless auto-trim searches
  public: // Auto-preservation conditions
    kdu_long class_preserve_streams[KDU_NUM_DATABIN_CLASSES]; // -1 means no
                               // preservation condition; -2 means wildcard.
  public: // Path walkers associated with activities that update the cache
    kd_cache_path_walker add_path; // Used by `kdu_cache::add_to_databin' and
                                   // `kdu_cache::delete_databin'.
    kd_cache_path_walker marking_path; // Used by `kdu_cache::mark_databin'
  public: // Path walkers for data-bin query functions
    kd_cache_path_walker get_length_path; // Just for `get_databin_length'
    kd_cache_path_walker scan_path; // Just for `scan_databins'
    kd_var_cache_seg *last_scan_seg; // NULL if scan has ended (or not started)
    int last_scan_pos; // 0 to 127 = last databin scanned from `last_scan_seg'
  public: // Read/scope state management (local to an attached cache)
    kd_cache_path_walker meta_read_path; // To access a meta-databin
    kd_cache_path_walker stream_read_path; // To access main/tile header bins
    kd_cache_path_walker main_read_path; // To access all other data-bin types
    kdu_long last_read_codestream_id; // -ve if `set_read_scope' not yet called
    kd_cache_buf *read_start; // Points to first buffer in active data-bin
    kd_cache_buf *read_buf; // Points to current buffer for the active data-bin
    int read_buf_pos; // Position of next byte to be read from `read_buf'
    int databin_pos; // Position of next byte to be read, within data-bin
    kdu_int32 databin_status; // Copied from actual databin header; use this to
      // get length of current read context; valid if `read_start' non-NULL
  };
  
} // namespace kd_supp_local

#endif // CACHE_LOCAL
