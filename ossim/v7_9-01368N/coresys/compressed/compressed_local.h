/*****************************************************************************/
// File: compressed_local.h [scope = CORESYS/COMPRESSED]
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
   Local definitions employed by the machinery behind the interfaces defined
in "kdu_compressed.h".  These definitions should not be included from any
other scope.  They are for use only in implementing the compressed data
management machinery.
   As a general rule, we use the prefix "kd_" for internal classes and
structures, for which a "kdu_" prefixed interface class exists in the
common scope header file, "kdu_compressed.h".
******************************************************************************/

#ifndef COMPRESSED_LOCAL_H
#define COMPRESSED_LOCAL_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "kdu_params.h"
#include "kdu_compressed.h"
#include "kdu_arch.h"

// The following classes and structures are all defined here:
namespace kd_core_local {
  struct kd_code_buffer;
  class kd_buf_master;
  class kd_buf_server;
  class kd_thread_buf_server;
  
  class kd_compressed_output;
  class kd_input;
  class kd_compressed_input;
  class kd_pph_input;
  
  class kd_marker;
  class kd_pp_markers;
  struct kd_tpart_pointer;
  struct kd_tile_ref;
  class kd_tlm_generator;
  class kd_tpart_pointer_server;
  class kd_precinct_pointer_server;
  
  class kd_compressed_stats;
  class kd_codestream_comment;
  class kd_packet_sequencer;
  
  class kd_header_in;
  class kd_header_out;
  class kd_block;
  
  class kd_reslength_checker;
  struct kd_global_rescomp;
  struct kd_precinct_flush_info;
  struct kd_flush_set;
  struct kd_nlt_info;
  struct kd_comp_info;
  struct kd_output_comp_info;
  struct kd_mct_ss_model;
  struct kd_mct_stage;
  struct kd_mct_block;

  class kd_cs_background_job;
  class kd_cs_thread_context;

  struct kd_codestream;
  struct kd_tile;
  struct kd_tile_comp;
  struct kd_node;
  struct kd_subband;
  struct kd_resolution;
  
  struct kd_precinct_band;
  struct kd_precinct;
  class kd_precinct_server;
  class kd_precinct_size_class;
  class kd_precinct_ref;
  
  struct kd_flush_stats;
} // namespace kd_core_local


namespace kd_core_local {
  using namespace kdu_core;
  
  
/* ========================================================================= */
/*                         Input Exception Codes                             */
/* ========================================================================= */

#define KDU_EXCEPTION_PRECISION ((kdu_uint16) 13)
    /* Thrown when a packet specifies quantities too large to represent
       within the constraints defined by the current implementation.  This
       is almost certainly due to an erroneously constructed code-stream
       and will be treated as such if the resilient mode is enabled. */
#define KDU_EXCEPTION_ILLEGAL_LAYER ((kdu_uint16) 21)
    /* Thrown when the inclusion tag tree attains an illegal state while
       decoding a packet header.  Illegal states can occur only when one
       or more packets from the same precinct have had their empty packet
       bit set to 0 (i.e. the packet was skipped).  Since no code-block can
       contribute to such packets, the index of the layer in which the
       block is first included cannot be equal to the index of a packet marked
       as empty.  If the inclusion tag tree decodes such a layer index, an
       error must have occurred and this exception is thrown. */
#define KDU_EXCEPTION_ILLEGAL_MISSING_MSBS ((kdu_uint16) 74)
    /* Thrown when the inclusion tag tree identifies a number of missing
       MSB's which is larger than 74.  The number of magnitude bit-planes in
       the representation of any subband sample can never exceed 37 in the
       JPEG2000 standard.  This may be augmented by U, the ROI up-shift
       factor; however, there is no practical value in selecting U larger
       than the number of magnitude bit-planes in any subband.  Thus, 74
       is a reasonable upper bound on the number of missing magnitude
       bit-planes which can be signalled -- the bound is mandated in profiles
       1 and 2 and would be mandated in the standard as a whole were it not
       for an oversight. */

  /* Notes:
        The above exceptions may be thrown during packet header decoding.  In
     addition, if an illegal marker code is encountered while reading any
     portion of a packet, an exception of type "kdu_uint16" will also be
     thrown, having the value of the illegal marker code.  Marker exception
     throwing may not occur unless either the "fussy" or the "resilient"
     mode is enabled, since watching for in-packet markers is a little
     time-consuming. */

/* ========================================================================= */
/*                           External Functions                              */
/* ========================================================================= */

extern void
  print_marker_code(kdu_uint16 code, kdu_message &out);


/* ========================================================================= */
/*                        Class/Struct Definitions                           */
/* ========================================================================= */

/*****************************************************************************/
/*                              kd_code_buffer                               */
/*****************************************************************************/

#define KD_CODE_BUFFER_LEN (KDU_CODE_BUFFER_ALIGN-(int) sizeof(void *))
    // Note: the codestream generation code currently relies upon the fact
    // that the code-buffer length is even.  This allows us to read/write
    // aligned short integers at the front of the variable length data
    // segment in each code-block, so as to store coding pass slope and
    // length information, while still being able to easily figure out
    // where the code-bytes actually start.  For more on this, see the
    // way in which `kd_block::body_bytes_offset' is first calculated in
    // `kd_block::write_packet_header'.

struct kd_code_buffer { // Chunks code bytes to minimize heap transactions
  private: // Functions
    friend class kd_buf_server;
    friend class kd_buf_master;
    kd_code_buffer *get_buf_link() { return buf_link; }
    void set_buf_link(kd_code_buffer *ptr) { buf_link=ptr; }
      // `kd_buf_master' and `kd_buf_server' use the above functions to build
      // links between buffer blocks that reside on a free-list, using space
      // found within the `buf' array
    kdu_int32 get_buf_val32() { return buf_ints[2]; }
    void set_buf_val32(kdu_int32 v) { buf_ints[2]=v; }
      // `kd_buf_master' alone uses the above two functions to keep track of
      // the number of blocks within each list within the `ccb_entries'
      // array.
  public: // Data
    kd_code_buffer *next;
    union { // `buf' is the fundamental buffer resource; the alternate
            // representations exist to avoid type-punned pointer warnings
            // and make the alignment explicit -- not actually required.
      kdu_byte buf[KD_CODE_BUFFER_LEN];
      kdu_uint16 wbuf[KD_CODE_BUFFER_LEN/2];
      kdu_byte *abuf[KD_CODE_BUFFER_LEN/sizeof(void *)];
      kd_code_buffer *buf_link; // Used only by `kd_buf_master'/`kd_buf_server'
      kdu_int32 buf_ints[3]; // Used only by `kd_buf_master'
    };
  };
  /* Notes:
       This structure is intended to be aligned no a memory boundary
     whose size is `KD_CODE_BUFFER_ALIGN'.  Individual buffers are
     connected via the `next' pointer. */

/*****************************************************************************/
/*                               kd_buf_master                               */
/*****************************************************************************/

#define KD_BUF_MASTER_LOG2_CCB_SPAN (KDU_LOG2_MAX_THREADS + 1)
#define KD_BUF_MASTER_CCB_SPAN (1<<KD_BUF_MASTER_LOG2_CCB_SPAN)
#define KD_CODE_BUFFERS_PER_BLOCK 31
#define KD_CODEBUF_BLOCK_BYTES (KD_CODE_BUFFERS_PER_BLOCK * \
                                KDU_CODE_BUFFER_ALIGN)

class kd_buf_master {
  public: // Lifecycle functions
    kd_buf_master();
    void attach_codestream()
      { num_codestreams.exchange_add(1); }
      /* Called when an additional codestream attaches to the object.  This
         need not be called by the codestream that constructs the object
         in the first place, since the number of attached codestreams is
         initialized to 1. */
    void detach_codestream()
      { 
        int num_left = num_codestreams.exchange_add(-1);
        assert(num_left >= 1);
        if (num_left <= 1) delete this;
      }
      /* Called when a codestream detaches from the object.  This should
         be called also when the codestream that constructed the object
         would normally delete it.  The destructor is invoked implicitly
         if the number of users reaches zero. */
    void attach_buf_server(kd_buf_server *obj)
      { num_buf_servers.exchange_add(1); }
    void detach_buf_server(kd_buf_server *obj)
      { num_buf_servers.exchange_add(-1); }
      /* The above two functions are used to keep track of the number of
         `kd_buf_server' objects that may invoke `get_blocks' and
         `release_blocks'.  Before detaching a `kd_buf_server' object, you
         must remember to return all of its memory via calls to
         `release_blocks' and `release_partial_blocks'. */
  private:  // Destructor is private
    ~kd_buf_master();
  public: // Other public member functions
    void set_multi_threaded();
      /* Call this function the first time you discover that the object is
         to be used within a multi-threaded environment.  This causes the
         internal mutex to be created and also ensures that interlocked
         operations are performed where appropriate. */
    kd_code_buffer *get_blocks(kdu_int32 &num_blocks);
      /* This function is used only by client `kd_buf_server' objects to
         obtain a list of code buffers that can be split up and distributed
         in response to their own `kd_buf_server::get' calls.  Buffers are
         not allocated one at a time by this function, but rather in
         "blocks".  A block consists of `KD_CODE_BUFFERS_PER_BLOCK' individual
         `kd_code_buffer' objects (not necessarily contiguous in memory).
         The returned list may consist of many such blocks, depending on the
         internal implementation; the actual number of blocks in the list
         is returned via the `num_blocks' argument.
         [//]
         To assist the caller in separating out individual blocks within the
         returned list, the `kd_code_buffer::get_buf_link' function may
         be used to traverse a list of code buffer blocks, starting from the
         head of the returned list.  The code buffers within each block are
         traversed using the `kd_code_buffer::next' member, but the list
         connected via these pointers terminates (with NULL) at the end of
         each block. */
    void release_blocks(kd_code_buffer *head, kd_code_buffer *tail,
                        kdu_int32 num_blocks);
      /* This function is used only by client `kd_buf_server' objects to
         return memory for shared re-allocation.  Rather than returning
         individual code-buffers, the memory is returned in "blocks".  A
         block consists of `KD_CODE_BUFFERS_PER_BLOCK' individual
         `kd_code_buffer' objects (not necessarily contiguous in memory)
         that are linked together via their `next' pointers.
         [//]
         If `num_blocks' > 1, each block must be NULL terminated via its
         `kd_code_buffer::next' pointers, while the blocks must be linked
         together via the pointers returned by `kd_code_buffer::get_buf_link'.
         [//]
         The `tail' argument refers to the head of the last block, rather
         than the last code buffer in the last block. Thus, if `num_blocks'
         is 1, `tail' should be equal to `head'.
      */
    void release_partial_blocks(kd_code_buffer *head, kd_code_buffer *tail,
                                  int num_bufs);
        /* This function is normally invoked only when a `kd_buf_server'
           object detaches.  It may happen that the object is holding onto
           some released code buffers that do not constitute a whole number
           of blocks.  In this case, the present object will have to reform
           whole blocks by combining the partial blocks released by multiple
           `kd_buf_server' objects.  This is not something you should do
           often, because it requires the internal `mutex' to be temporarily
           locked.  For this function `tail' refers to the final buffer in
           the list commencing with `head'; all buffers in the list are
           expected to be connected via their `next' pointers. */
    kdu_long augment_cache_threshold(kdu_long increment)
      { // Use to adjust the cache threshold either up or down
        mutex.lock();
        kdu_long result = (cache_threshold_bytes += increment);
        kdu_long nblocks = cache_threshold_blocks / KD_CODEBUF_BLOCK_BYTES;
        if (nblocks > (kdu_long) KDU_INT32_MAX)
          cache_threshold_blocks = KDU_INT32_MAX;
        else
          cache_threshold_blocks = (kdu_int32) nblocks;
        mutex.unlock();
        return result;
      }
    void augment_structure_blocks(kdu_int32 num_blocks)
      { /* Adjusts the number of structure bytes up/down as required, by
           multiples of the memory block size.  The number of structure
           bytes is quantized into blocks and passed along to this function
           via `kd_buf_server::augment_structure_bytes'. */
        if (!mutex.exists())
          { 
            int val = num_structure_blocks.add_get(num_blocks);
            if (val > peak_structure_blocks)
              peak_structure_blocks = val;
          }
        else
          { 
            int val = num_structure_blocks.exchange_add(num_blocks);
            if ((num_blocks > 0) &&
                ((val += num_blocks) > peak_structure_blocks))
              peak_structure_blocks = val;
          }
      }
    kdu_int32 get_current_buf_blocks()
      { // Used to implement `kd_buf_server::get_current_buf_bytes'.
        return num_allocated_blocks.get();
      }
    kdu_int32 get_peak_buf_blocks()
      { // Used to implement `kd_buf_server::get_peak_buf_bytes'.
        return peak_allocated_blocks;
      }
    kdu_int32 get_current_structure_blocks()
      { // Used to implement `kd_buf_server::get_current_structure_bytes'.
        return num_structure_blocks.get();
      }
    kdu_int32 get_peak_structure_blocks()
      { // Used to implement `kd_buf_server::get_peak_structure_bytes'.
        return peak_structure_blocks;
      }
    bool cache_threshold_exceeded(kdu_int32 known_buf_server_blocks)
      { /* Implements `kd_buf_server::cache_threshold_exceeded' using the
           quantized memory and structure size measurements available in
           the common domain represented by this object.  The value
           returned by this function might not be fully up-to-date with
           respect to what is happening on other threads, but that should
           not be a big issue in practice.  The `known_buf_server_blocks'
           argument supplies the number of free memory blocks that the
           calling `kd_buf_server' object already has in its local store.
           This value at least can be removed from the current object's
           estimate of the total amount of memory actually allocated. */
        return ((cache_threshold_blocks + known_buf_server_blocks) <
                (num_structure_blocks.get() + num_allocated_blocks.get()));
      }
  private: // Definitions
      struct kd_code_alloc {
          kd_code_alloc *next;
          kdu_byte block[1]; // Actual structure is allocated to be a much
                             // larger memory block, from which aligned
                             // code buffers are extracted.
        };
  private: // Helper functions
    void service_lists();
      /* This function is called when a call to `get_blocks' finds that
         the next entry on the internal circular buffer is empty.  The
         function assesses the total number of blocks available on the
         `release_list' (or the temporary internal `post_release_list') and
         distributes these blocks to entries within the circular buffer.
         It is careful not to distribute everything immediately, however, if
         there are more `kd_buf_server' objects than entries in the
         circular buffer.  Once free buffers are exhausted, the function
         allocates additional memory from the heap, as required.  This
         function locks the internal `mutex', whereas most other access
         functions are lock-free, relying instead on interlocked atomic
         manipulation of integer and pointer variables. */
  private: // Data
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_leadin)
    kdu_mutex mutex; // Not actually created until `set_multi_threaded' called
    kdu_interlocked_int32 ccb_get_pos;
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep1)
    kdu_interlocked_ptr release_list; // Temporarily holds released buffers
    kdu_interlocked_int32 num_release_blocks;
            // Note: the above two variables might possibly get slightly out
            // of sync, but we always update `num_release_blocks' after
            // `release_list' so that memory allocation estimates will be
            // conservative, if anything.  Also, note that `num_release_blocks'
            // keeps track of the combined number of blocks that should be
            // found in the `release_list' and the `post_release_list'.
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep2)
    kdu_interlocked_int32 num_allocated_blocks; // See below
    int peak_allocated_blocks; // Might not be 100% up-to-date, but who cares
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep3)
    kdu_interlocked_int32 num_structure_blocks;
    int peak_structure_blocks; // Might not be 100% up-to-date, but who cares
    kdu_long cache_threshold_bytes;
    int cache_threshold_blocks; // Rounded down to whole blocks
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep4)
    kdu_interlocked_ptr ccb_entries[KD_BUF_MASTER_CCB_SPAN];
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep5)
    kdu_interlocked_int32 num_buf_servers; // Total num kd_buf_server clients
    kdu_interlocked_int32 num_codestreams; // Total num attached codestreams
    //------------------------------------------------------------------------
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep6)
    int ccb_fill_pos;
    kd_code_alloc *alloc; // Linked list of all allocated memory blocks
    kd_code_buffer *post_release_list; // See below
    kd_code_buffer *partial_block_bufs; // Used by `release_partial_blocks'
    int num_partial_block_bufs; // Total code buffers in partial blocks list
    int total_alloc_blocks; // Total number of blocks allocated from the heap
    int num_blocks_per_ccb_entry; // Goes to 0 when we reconsult the free lists
    KD_PRIVATE_CACHELINE_SEPARATOR(_trailer)
  };
  /* Notes:
        From Kakadu version 7, the role of `kd_buf_server' has been modified.
     Whereas previously we had a single `kd_buf_server' per codestream,
     possibly shared with other codestreams, we now have up to 1+N
     `kd_buf_server' objects per codestream, where N is the number of
     threads that could do work, but only one `kd_buf_master' that does the
     original allocation.
        Calls into `kd_buf_server' need to either be inherently thread-safe
     (because they are contained within one thread) or else protected by
     the relevant codestream's `KD_THREADLOCK_GENERAL' mutex.  On the other
     hand, calls into `kd_buf_master' are rendered thread safe through a
     combination of interlocked atomic operations and an internal mutex
     that is locked only when a new "mass allocation" need be performed and
     at critical points in its lifecycle (e.g., when sharing it with other
     codestreams).
        Allocated memory is managed in discrete quanta known as blocks.  A
     block consists of `KD_CODE_BUFFERS_PER_BLOCK' separate code buffers.
     Blocks are initially allocated from contiguous memory, but any
     collection of `KD_CODE_BUFFERS_PER_BLOCK' is allowed to constitute a
     block.  Memory is moved between the `kd_buf_master' object and its
     client `kd_buf_server' objects in multiples of blocks and the reporting
     of memory statistics is quantized into whole-block multiples.  Calls
     to `kdu_buf_server::release' collect the returned code buffers until
     one or more whole blocks are available, returning memory to the
     present object only in whole blocks.  The `num_allocated_blocks'
     member keeps track of the total number of memory blocks are currently
     outstanding, meaning that they have been allocated to `kd_buf_server'
     objects and not returned.
        Within this object, code buffer blocks reside either in the
     `release_list' or the `KD_BUF_MASTER_CCB_SPAN' entries of the
     `ccb_entries' array.  The `release_list' collects code buffers returned
     to the object via `release_blocks' until they can be examined by
     the `service_lists' function.  These are transferred to the
     `post_release_list' as soon as it becomes empty -- that list is accessed
     only from within the `release_list' function so we don't need to use
     interlocked operations.
        The `ccb_entries' array, together with the `ccb_get_pos' and
     `ccb_fill_pos' members, are used to implement a lock-free circular
     buffer to distribute allocated memory to client `kd_buf_server' objects
     that may be entered from as many as KDU_MAX_THREADS different threads.
     We now describe the way in which memory lists are enqueued (filled by
     `service_lists') and dequeued (via `get_blocks') to/from this circular
     buffer.
        The `get_blocks' function atomically increments the interlocked
     `ccb_get_pos' member, using the old value to deduce an index R into
     the `ccb_entries' array.  In this way, the relevant thread reserves
     the right to retrieve a list of blocks from location `ccb_entries[R]'.
     It must dequeue a non-empty list from this location to ensure deadlock
     free operation of the lock-free queue, so if the current entry at that
     location is NULL, it invokes `service_lists' and tries again.  It may
     be that multiple threads serialize through calls to `service_lists',
     but this is highly unlikely, because `KD_BUF_MASTER_CCB_SPAN' is
     larger than `KDU_MAX_THREADS'.  It may also be that other threads
     race around the memory pool and grab the memory allocated for us by
     `service_lists' before our call returns.  None of this should pose
     any concern, though; at worst, we need to call `service_lists' again.
     Threads retrieve the entire list from `ccb_entries[R]' using an
     atomic exchange operation.
        The `service_lists' function iteratively increments `ccb_fill_pos'
     to obtain an index W into the `ccb_entries' array.  If `ccb_entries[W]'
     holds a non-NULL entry, the function returns (it will be called again
     when needed).  Otherwise, it prepares an appropriately sized list of
     blocks and writes this list to `ccb_entries[W]' using an atomic
     `compare-and-set' operation (for safety).  An important question is
     how many blocks should be included in each list written to the
     `ccb_entries' array.  Each time `service_lists' is called, we figure
     out the number of blocks B to write to each entry as follows.  If
     `num_blocks_per_ccb_entry' is non-zero, we use this value, consuming
     blocks from the `release_list' via the `post_release_list'.  If we
     run out of blocks, we set `num_blocks_per_ccb_entry' to zero and
     generate any remaining entries of the `ccb_entries' array by allocating
     memory from the heap.  If `num_blocks_per_ccb_entry' is zero on entry,
     we calculate a suitable value by dividing the total amount of blocks
     on the release lists into N equal portions, where N is the maximum of
     `KD_BUF_MATER_CCB_SPAN' and `num_buf_servers', rounding downwards but
     not letting the value become smaller than 1.  In this way, we ensure
     that released memory gets returned in a reasonably even way to all
     threads that require it and we also ensure that new memory is allocated
     from the heap only when this is required.
  */

/*****************************************************************************/
/*                               kd_buf_server                               */
/*****************************************************************************/

class kd_buf_server {
  public: // Member functions
    void attach_and_init(kd_buf_master *tgt);
      /* We don't use constructors/destructors here deliberately because
         we may create an array with a lot these object but only use a few. */
    void attach_and_init(const kd_buf_server *copy)
      { this->attach_and_init(copy->master); }
      /* This version of the function initializes the object in exactly the
         same way that `copy' was initialized. */
    void cleanup_and_detach();
      /* Explicitly cleanup only those objects that we actually use. */
    kd_code_buffer *get()
      { /* Returns a new code buffer, with its `next' pointer set to NULL. */
        kd_code_buffer *buf = strip_bufs;
        if (buf != NULL)
          { strip_bufs = buf->next; num_strip_bufs--; }
        else if ((buf=first_free_buf) != NULL)
          { 
            if ((first_free_buf = buf->next) == NULL) last_free_buf = NULL;
            num_free_bufs--;
          }
        else
          buf = get_from_block();
        buf->next = NULL;
        return buf;
      }
    void release(kd_code_buffer *buf)
      { /* Recycles a previously acquired code buffer. */
        assert(master != NULL);
        if ((buf->next = first_free_buf) == NULL)
          { assert(num_free_bufs == 0); last_free_buf = buf; }
        first_free_buf = buf;  num_free_bufs++;
        if (num_free_bufs == KD_CODE_BUFFERS_PER_BLOCK)
          {
            master->release_blocks(first_free_buf,first_free_buf,1);
                // Note: `first_free_buf' points to the last full "block" in
                // the list headed by `first_free_buf'.  `last_free_buf'
                // points to the last buffer, not the last block.
            first_free_buf = last_free_buf = NULL; num_free_bufs = 0;
          }
      }
    kdu_long get_current_buf_bytes()
      { /* Returns the current number of bytes allocated by the server for
           bit-stream buffers. */
        return (((kdu_long)(master->get_current_buf_blocks() -
                            num_free_blocks)) *
                KD_CODEBUF_BLOCK_BYTES) - num_free_bufs - num_strip_bufs;
      }
    kdu_long get_peak_buf_bytes()
      { /* Returns the maximum number of bytes ever actually allocated by
           the server for bit-stream buffers.  This might be considerably
           larger than the number of bytes actually allocated out of
           the `kd_buf_server' object, especially if a single master buf
           server is shared by many `kd_buf_server's in many codestreams. */
        return ((kdu_long) master->get_peak_buf_blocks()) *
                KD_CODEBUF_BLOCK_BYTES;
      }
    kdu_long get_current_structure_bytes()
      { /* Returns the current number of bytes allocated for representing
           structures.  Actually, this value might be a little higher than
           the actual number because structure bytes are quantized into
           equivalent memory blocks and rounded upwards. */
        return (((kdu_long) master->get_current_structure_blocks()) *
                KD_CODEBUF_BLOCK_BYTES) + surplus_structure_bytes;
      }
    kdu_long get_peak_structure_bytes()
      { /* Returns a conservative upper bund on the number of bytes ever
           allocated for representing structures.  This value is quantized
           (rounding up) into equivalent memory blocks. */
        return ((kdu_long) master->get_peak_structure_blocks()) *
                KD_CODEBUF_BLOCK_BYTES;
      }
    void augment_structure_bytes(kdu_long increment)
      { /* Use to adjust the number of structural bytes either up or down.
           This function must be executed from a unique thread or from within
           a critical section. */
        surplus_structure_bytes += increment;
        if (surplus_structure_bytes > 0)
          { 
            kdu_long num_blocks =
              1 + ((surplus_structure_bytes-1)/KD_CODEBUF_BLOCK_BYTES);
            surplus_structure_bytes -= num_blocks*KD_CODEBUF_BLOCK_BYTES;
            master->augment_structure_blocks((kdu_int32) num_blocks);
          }
        else if (surplus_structure_bytes <= -KD_CODEBUF_BLOCK_BYTES)
          { 
            kdu_long num_blocks =
              - ((-surplus_structure_bytes) / KD_CODEBUF_BLOCK_BYTES);
            surplus_structure_bytes -= num_blocks*KD_CODEBUF_BLOCK_BYTES;
            master->augment_structure_blocks((kdu_int32) num_blocks);
          }
      }
    bool cache_threshold_exceeded()
      { return (master->cache_threshold_exceeded(num_free_blocks)); }
      /* This function is called from the `kd_precinct_server::get' function
         before it serves up a new precinct container, whenever one or more
         precincts are currently on its inactive list.  The function returns
         true if the cache threshold has been exceeded, meaning that one or
         more inactive precincts should be closed (unloaded from
         memory, along with all their associated code buffers).  For more
         on this, consult the comments appearing below the declaration of
         the `kd_precinct_server' class.
            The function is also called whenever a new tile-part is started,
         to determine whether there are unloadable tiles which should be
         unloaded to bring memory consumption below the cache threshold. */
  private: // Helper functions
    kd_code_buffer *get_from_block();
      /* This function is called from within `get' if both the `strip_bufs'
         and `first_free_bufs' lists are empty.  The function strips the
         first free block from the `free_blocks' list, putting its contents
         on the `strip_bufs' list.  If necessary, new free blocks are
         requested from the `master' object. */
  private: // Data
    KD_PRIVATE_CACHELINE_SEPARATOR(_leadin)
    //------------------------------------------------------------------------
    kd_buf_master *master;
    kd_code_buffer *free_blocks; // List of free blocks from `master'
    int num_free_blocks; // Number of blocks in above list
    kd_code_buffer *strip_bufs; // Stripped from leading free block
    int num_strip_bufs; // Number of elements in above list
    kd_code_buffer *first_free_buf; // List of buffers returned via `release'
    kd_code_buffer *last_free_buf; // Tail of above list
    int num_free_bufs; // Number of buffers in above list
    kdu_long surplus_structure_bytes; // -KD_CODEBUF_BLOCK_BYTES < value <= 0
    //------------------------------------------------------------------------
    // KD_PRIVATE_CACHELINE_SEPARATOR(_trailer);
    /* Don't need a trailer because we allocate the `kd_buf_server'
       objects in a contiguous array, so the next object's leadin keeps
       threads separated. */
  };

/*****************************************************************************/
/*                             kd_compressed_output                          */
/*****************************************************************************/

class kd_compressed_output : public kdu_output {
  public: // Member functions
    kd_compressed_output(kdu_compressed_target *target)
      { this->target=target; flushed_bytes = 0; tile_num=-1; precinct_id=-1; }
    virtual ~kd_compressed_output()
      { flush_buf(); }
    kdu_compressed_target *access_tgt()
      { /* You may call this function to access the underlying `target'
           object, but you should never manipulate that object if you
           have written anything to the present object, without re-accessing
           it. */
           flush_buf();  return target;
      }
    kdu_long get_bytes_written()
      { return (flushed_bytes + (next_buf-buffer)); }
    void flush()
      { flush_buf(); }
    void start_mainheader()
      { 
        assert(tile_num == -1);
        target->start_mainheader();
        tile_num = -2;
      }
    void end_mainheader()
      { 
        assert(tile_num == -2);
        target->end_mainheader();
        tile_num = -1;
      }
    void start_tileheader(int tnum, int num_tiles)
      { 
        assert(target->get_capabilities() & KDU_TARGET_CAP_CACHED);
        assert((tile_num==-1) && (precinct_id < 0) && (tnum >= 0));
        target->start_tileheader(tnum,num_tiles);
        tile_num=tnum;
      }
    void end_tileheader()
      { 
        assert(tile_num >= 0);
        flush();
        target->end_tileheader(tile_num);
        tile_num = -1;
      }
    void start_precinct(kdu_long unique_id)
      { 
        assert(target->get_capabilities() & KDU_TARGET_CAP_CACHED);
        assert((tile_num==-1) && (precinct_id < 0) && (unique_id >= 0));
        target->start_precinct(unique_id);
        precinct_id = unique_id;
      }
    void end_precinct(int num_packets, const kdu_long packet_lengths[])
      { 
        assert(precinct_id >= 0);
        flush();
        target->end_precinct(precinct_id,num_packets,packet_lengths);
        precinct_id = -1;
      }
  protected: // Virtual functions which implement required services.
    virtual void flush_buf()
      {
        if (next_buf > buffer)
          target->write(buffer,(int)(next_buf-buffer));
        flushed_bytes += next_buf - buffer;
        next_buf = buffer;
      }
  private: // Data
    kdu_compressed_target *target;
    kdu_long flushed_bytes;
    int tile_num;
    kdu_long precinct_id;
  };

/*****************************************************************************/
/*                                kd_input                                   */
/*****************************************************************************/

#define KD_IBUF_SIZE 512
#define KD_IBUF_PUTBACK 6 // Maximum number of bytes you can put back.

class kd_input {
  /* This abstract base class must be derived to construct meaningful
     input devices.  Currently, we have two derived classes in mind: one
     for reading from the code-stream and one for recovering packet
     headers from PPM or PPT markers.  Since there may be many low level
     byte-oriented transactions, we emphasize efficiency for such
     transactions. */
  public: // Member functions
    kd_input()
      { first_unread = first_unwritten = buffer+KD_IBUF_PUTBACK;
        exhausted = fully_buffered = throw_markers = false; }
    virtual ~kd_input() { return; }
    bool is_fully_buffered() { return fully_buffered; }
    void enable_marker_throwing(bool reject_all=false)
      { /* If `reject_all' is false, subsequent reads will throw an exception
           if a bona fide SOP or SOT marker is found. In this case, the
           function will check to make sure that the length field is correct,
           putting the length field and marker code bytes back to the source
           before throwing the exception, so that the catch statement can read
           them again.
              If `reject_all' is true, subsequent reads will throw an
           exception if any marker code in the range FF90 to FFFF is found.
           Again, the marker code itself is put back onto the stream before
           throwing the exception.
              Marker throwing is disabled before throwing an exception for
           either of the above reasons.
              Note that code-stream reading may be slowed down
           when this marker throwing is enabled.  For this reason, the
           capability is best used only when error resilience or error
           detection capabilities are required. */
        this->reject_all = reject_all; throw_markers = true; have_FF = false;
      }
    bool disable_marker_throwing()
      { /* Disable marker exception throwing.  Returns true unless the
           last byte which was read was an FF, in which case the function
           returns false.  This allows the caller to catch markers which
           were partially read.  Any code-stream segment which is not
           permitted to contain a marker code in the range FF90 through FFFF
           is also not permitted to terminate with an FF. */
        if (!throw_markers) return true;
        throw_markers = false; // Must not touch `reject_all' here.
        if (exhausted) have_FF = false;
        return !have_FF;
      }
    void terminate_prematurely()
      { /* This function may be called in non-resilient mode if an EOC marker
           is encountered before the stream physically ends. */
        exhausted = true;
      }
    bool failed()
      { /* Returns true if any of the input functions, `get', `read' or
           `ignore' failed to completely fulfill its objective due to
           exhaustion of the relevant source. */
        return exhausted;
      }
    bool get(kdu_byte &byte) // throws (kdu_uint16 code): unexpected marker
      { /* Access a single byte, returning false if and only if the source is
           exhausted, in which case future calls to `failed' return true. */
        if (exhausted || ((first_unread==first_unwritten) && !load_buf()))
          return false; // If `load_buf' fails it sets `exhausted' for us.
        byte = *(first_unread++);
        if (throw_markers)
          {
            if (have_FF && (byte > 0x8F))
              process_unexpected_marker(byte);
            have_FF = (byte==0xFF);
          }
        return true;
      }
    void putback(kdu_byte byte)
      { /* You may put back more than 1 byte, but you may not call this
           function if a previous read has ever failed.  It is also illegal
           to call this function while marker throwing is enabled. */
        assert(!exhausted);
        assert(!throw_markers);
        first_unread--;
        if (!fully_buffered)
          {
            assert(first_unread >= buffer);
            *first_unread = byte;
          }
      }
    void putback(kdu_uint16 code)
      { /* This function is designed to improve the readability of code
           which puts marker codes back to the input object from which they
           were read. */
        assert(!exhausted);
        assert(!throw_markers);
        first_unread-=2;
        if (!fully_buffered)
          {
            assert(first_unread >= buffer);
            first_unread[0] = (kdu_byte)(code>>8);
            first_unread[1] = (kdu_byte) code;
          }
      }
    int pseudo_read(kdu_byte * &addr, int count)
      { /* This function should be called only for `fully_buffered' sources.
           It behaves like `read', except that it does not actually read
           any bytes.  Instead, it puts the address of the block of data
           that would have been read into the variable referenced by the
           `addr' argument. */
        assert(fully_buffered);
        addr = first_unread;
        if (throw_markers)
          return read(addr,count); // Slower path, looks for markers
        first_unread += count;
        if (first_unread > first_unwritten)
          {
            count -= (int)(first_unread - first_unwritten);
            first_unread = first_unwritten; exhausted = true;
          }
        return count;
      }
    int read(kdu_byte *buf, int count);
      /* More efficient than `get' when the number of bytes to be read is
         more than 2 or 3.  Returns the number of bytes actually read.  If
         less than `count', future calls to `failed' will return true.  Note
         that this function is less efficient if marker throwing is enabled. */
    int read(kd_code_buffer * &cbuf, kdu_byte &buf_pos,
             kd_buf_server *buf_server, int length);
      /* Same as the above function, except that the requested bytes are
         written to a linked list of `kd_code_buffer' buffers, headed by
         `cbuf', where the `buf_pos' indicates the next free byte in the
         `cbuf' buffer.  The function automatically grows the linked list
         of buffers, as required, using `buf_server->get()'.  Upon return,
         the `cbuf' and `buf_pos' variables are advanced to refer to the
         most recently written `kd_code_buffer' object and the already
         written prefix of that object, respectively.  The function
         returns the actual number of bytes transferred in this way, which
         is equal to `length' unless the ultimate source of data ran dry. */
    virtual kdu_long ignore(kdu_long count);
      /* Skips over the indicated number of bytes, returning the number of
         bytes actually skipped.  If less than `count', future calls to
         `failed' will return true.  The function may be overridden by
         derived classes which support seeking. */
  protected: // Data and functions used for buffer management.
    virtual bool load_buf() = 0;
      /* Returns false and sets `exhausted' to true if the source is unable
         to provide any more data.  Otherwise, augments the internal buffer
         and returns true.  Buffer manipulation must conform to the following
         conventions.  The `first_unread' pointer should be set to
         `buffer'+KD_IBUF_PUTBACK and then bytes should be loaded into the
         remaining KD_IBUF_SIZE bytes of the `buffer' array. The
         `first_unwritten' pointer should then be set to point just beyond
         the last byte actually written into the buffer.  The only exception
         to the above conventions is for fully buffered sources, in which
         the entire data source lies in a contiguous external memory buffer.
         In this case, the internal `buffer' is not used; the first call to
         `load_buf' sets `first_unread' to point to the start of the external
         memory buffer and `first_unwritten' to point just beyond the external
         memory buffer.  In this case, the `fully_buffered' flag should be
         set to true, which signals to the `putback' functions that it is
         sufficient for them to adjust the `first_unread' pointer, without
         actually writing anything. */
    kdu_byte buffer[KD_IBUF_SIZE+KD_IBUF_PUTBACK];
    kdu_byte *first_unread; // Points to next byte to retrieve from buffer.
    kdu_byte *first_unwritten; // Points beyond last available byte in buffer
    bool fully_buffered;
    bool exhausted;
    bool throw_markers; // If true, must look for unexpected markers.
  private: // Functions
    void process_unexpected_marker(kdu_byte last_byte);
      /* This function is called when marker throwing is enabled and a
         marker code in the range FF90 through FFFF has been found.  The
         least significant byte of the marker code is supplied as the
         `last_byte' argument.  The function determines whether to throw
         the exception or not.  If not, reading continues unaffected. */
  private: // Data
    bool have_FF; // Valid only with `throw_markers'. Means last byte was FF.
    bool reject_all; // If `false' marker throwing is only for SOP's and SOT's
  };
  
/*****************************************************************************/
/*                             kd_compressed_input                           */
/*****************************************************************************/

class kd_compressed_input : public kd_input {
  public: // Member functions
    kd_compressed_input(kdu_compressed_source *source);
    int get_capabilities()
      { return source->get_capabilities(); }
    bool set_tileheader_scope(int tnum, int num_tiles);
      /* Generates an error through `kdu_error' if the embedded source does
         not support the `KDU_SOURCE_CAP_CACHED' capability.  This function
         simply calls the source's own `set_tileheader_scope' function and
         clears the internal state variables (especially, the `exhausted'
         member) so that subsequent reads will recover bytes from the indicated
         tile's header.  For more information, refer to the comments appearing
         with `kdu_compressed_source::set_tileheader_scope'. */
    kdu_long get_offset()
      { /* Returns the address of the next byte which will be returned by the
           base object's `get' function.  If seeking is enabled, the source
           may be repositioned to this point by supplying this address as the
           `unique_address' argument to `seek'.  Even if seeking is not
           enabled, `get_offset' will return the correct address and this may
           be used to determine appropriate arguments for the `ignore' member
           function declared below.  May not be called if the source is in
           anything other than code-stream scope. */
        assert(!special_scope);
        return cur_offset + last_loaded_bytes - (first_unwritten-first_unread);
      }
    void seek(kdu_long unique_address);
      /* If `unique_address' is non-negative, it is treated as a code-stream
         offset and the function seeks to a point `unique_address' bytes past
         the beginning of the code-stream (SOC marker), using the embedded
         `kdu_compressed_source' object's `seek' function.  In this case,
         a terminal error is generated if the source does not
         offer the KDU_SOURCE_CAP_SEEKABLE capability.
            If `unique_address' is negative, the function treats
         -(1+`unique_address') as the unique identifier associated with a
         cached precinct, supplying this identifier to the embedded
         `kdu_compressed_source' object's `set_precinct_scope' function.  In
         this case, a terminal error is generated if the source does not
         offer the KDU_SOURCE_CAP_CACHEABLE capability.
            In either case, the function may clear or set the "exhausted"
         state, so that the base object's `failed' member function may return
         false, having previously returned true, or vice-versa. */
    kdu_long ignore(kdu_long count); // Overrides `kd_input::ignore'.
    void set_max_bytes(kdu_long limit);
      /* No effect except when reading in code-stream scope. */
    kdu_long get_bytes_read();
      /* Returns the smallest initial prefix of the source stream which
         contains all of the bytes consumed from `kd_input'.  Note that more
         bytes may have been read from the compressed data source and
         buffered internally, without having been actually used.
            For cached sources (those advertising the KDU_SOURCE_CAP_CACHED
         capability), the function's return value will be 0, unless we
         are still reading the main header. */
    kdu_long get_suspended_bytes();
      /* Returns the total number of bytes which have been read in the
         suspended state.  The returned value may be subtracted from
         the value returned by `get_bytes_read' to determine the total
         number of bytes which have been read in a non-suspended state.
         Byte limits apply only to non-suspended reads. */
    void set_suspend(bool state)
      {
        if (special_scope) return;
        if ((suspend_ptr == NULL) && state)
          { // Enter suspended mode
            suspend_ptr = first_unread;
            if (alt_first_unwritten != NULL)
              {
                assert(alt_first_unwritten > first_unwritten);
                last_loaded_bytes += alt_first_unwritten - first_unwritten;
                first_unwritten = alt_first_unwritten;
                alt_first_unwritten = NULL;
              }
          }
        else if ((suspend_ptr != NULL) && !state)
          { // Leave suspended mode
            suspended_bytes += first_unread-suspend_ptr;  suspend_ptr = NULL;
            kdu_long limit = suspended_bytes + max_bytes_allowed - cur_offset;
            if (limit < last_loaded_bytes)
              { // We have already loaded the buffer with too many bytes
                alt_first_unwritten = first_unwritten;
                first_unwritten -= (last_loaded_bytes - limit);
                last_loaded_bytes = limit;
                if (first_unwritten < first_unread)
                  { // We have already read past the end.
                    exhausted = true;
                    suspended_bytes -= (first_unread-first_unwritten);
                    first_unwritten = first_unread;
                    alt_first_unwritten = NULL;
                  }
              }
          }
      }
  protected: // Virtual functions which implement required services.
    virtual bool load_buf();
  private: // Data
    kdu_compressed_source *source;
    kdu_long cur_offset; // Offset from start of code-stream
    kdu_long max_bytes_allowed; // Source may have fewer bytes than this
    kdu_long max_address_read; // Address of last byte read from base object.
    kdu_long suspended_bytes; // Bytes read while in suspended mode.
    kdu_long last_loaded_bytes; // Num bytes added by last call to `load_buf'
    kdu_byte *suspend_ptr; // Points into the buffer
    kdu_byte *alt_first_unwritten; // See below
    bool special_scope; // True for anything other than code-stream scope.
  };
  /* Notes:
        The `cur_offset' member holds the offset from the start of the
     code-stream to the first nominal byte in the input buffer; i.e., the
     byte at kd_input::buffer + KD_IBUF_PUTBACK.  The actual number of bytes
     which have been read from `source' is larger by the amount
     `kd_input::first_unwritten' - (`kd_input::buffer'+KD_IBUF_PUTBACK).
        The object may be placed in a suspended mode while reading data
     which belongs to code-stream packets which lie outside the current
     resolution, components or layers of interest.  This allows the
     application to set a byte limit which applies only to the relevant
     data which would be left if the irrelevant data were first parsed
     out of the code-stream.  The object is in suspended mode if and only
     if `suspend_ptr' is non-NULL.  In this case, `suspend_ptr' holds the
     value of `kd_input::first_unread' immediately prior to the point at
     which the object entered the suspended mode.  The total number of
     bytes consumed while in the suspended mode is maintained by the
     `suspended_bytes' member, but use the function, `get_suspended_bytes'
     to evaluate a reliable count of the number of suspended bytes, which
     takes into account activity within the `kd_input' object of which
     the derived object might not be aware.
        The `alt_first_unwritten' member is used to keep track of bytes in
     the buffer which may be inaccessible while in the regular (non-suspended)
     mode, which may need to be made accessible while the object is in the
     suspended mode.  This member holds NULL until exiting the suspended mode
     requires `first_unwritten' to be truncated to account for a byte limit.
     At that point, it is set to the value of the `first_unwritten' member
     prior to truncation.  When the object enters the suspended mode, any
     non-NULL `alt_first_unwritten' value is copied to `first_unwritten'. */

/*****************************************************************************/
/*                               kd_pph_input                                */
/*****************************************************************************/

class kd_pph_input : public kd_input {
  /* Alternate input for packet header bytes, which is used in conjunction
     with the PPM and PPT marker segments. */
  public: // Member functions
    kd_pph_input(kd_buf_server *server)
      { buf_server = server; first_buf = read_buf = write_buf = NULL; }
    virtual ~kd_pph_input();
    void add_bytes(kdu_byte *data, int num_bytes);
      /* This function is called when unpacking a PPM or PPT marker, to
         augment the current object. */
  protected: // Virtual functions which implement required services.
    virtual bool load_buf();
  private: // Data
    kd_code_buffer *first_buf;
    kd_code_buffer *read_buf, *write_buf;
    int read_pos, write_pos;
    kd_buf_server *buf_server;
  };
  /* Notes:
        `first_buf' points to the first in a linked list of buffers used to
     temporarily store PPM or PPT marker segment bytes.
        `read_buf' points to the element in the linked list of code buffers
     which is currently being read; `read_pos' points to the next element of
     the current buffer which will be read. This may be equal to
     KD_CODE_BUFFER_LEN, in which case the next element in the code buffer
     list must be accessed when the next read attempt occurs.
        `write_buf' points to the last element in the linked list of code
     buffers and `write_pos' identifies the first unwritten byte in this
     element.  `write_pos' may equal KD_CODE_BUFFER_LEN, in which case a
     new element will need to be added before further data can be written.
        `buf_server' points to the object which is used to add and release
     code buffer elements.  This is a shared resource. */

/*****************************************************************************/
/*                                kd_marker                                  */
/*****************************************************************************/

class kd_marker {
  /* Objects of this class are used to read and store code-stream marker
     segments. The most efficient way to use the class is to construct a
     single serving object which is used to read the markers one by one and
     then copy any markers which need to be preserved into new instances of
     the class, using the copy constructor.  This concentrates wasted buffer
     space in the one server marker which actually reads from the input
     code-stream. */
  public: // Member functions
    kd_marker(kd_input *input, kd_codestream *cs)
      { this->source = input; this->codestream = cs;
        code = 0; length = 0; max_length = 0; buf = NULL;
        encountered_skip_code = false; }
    kd_marker(const kd_marker &orig);
      /* Copies any marker segment stored in `orig'. Note that the `source'
         pointer is not copied, meaning that the `read' member function may
         not be invoked on the copy. */
    ~kd_marker() { if (buf != NULL) delete[] buf; }
    kd_marker *move(kd_input *input, kd_codestream *cs)
      { this->source = input; this->codestream = cs; return this; }
        /* This function is used when a `kd_marker' object must be moved
         to a different codestream, or to work with a different source of
         data. */
    void print_current_code(kdu_message &out)
      { /* Prints a text string identifying the current marker code. */
        print_marker_code(this->code,out);
      }
    bool read(bool exclude_stuff_bytes=false, bool skip_to_marker=false);
      /* Reads a new marker (or marker segment) from the `kd_input' object
         supplied during construction.
             Returns true if successful.  Causes of failure may be: 1) the
         code-stream source is exhausted; 2) a valid marker code was not found;
         or 3) an SOP or SOT marker code was found, but followed by an invalid
         length field.  This third cause of failure is important since it
         prevents the function from attempting to consume an invalid SOP or
         SOT marker segment, in the process of which it might consume any
         number of useful packets, having their own SOP markers.
              A valid marker code must commence with an FF.  If
         `exclude_stuff_bytes' is true then the second byte of a valid marker
         code must be strictly greater than 0x8F.  Otherwise, the second byte
         is arbitrary.  If `skip_to_marker' is true, the function consumes
         bytes indefinitely, until the source is exhausted or a valid marker
         code is found.  In this case, the function returns false only if the
         source is exhausted. Otherwise, the function expects to find a valid
         marker code immediately.
             As a convenience feature, the function automatically skips over
         any EOC marker it encounters.  This has two advantages: 1) we can
         detect the end of the code-stream without having to explicitly check
         for an EOC marker; 2) we should not be overly troubled by EOC
         markers which arise due to corruption of the code-stream (this has
         quite a high likelihood of occurring if the code-stream is subject
         to corruption).
             In the event that the source is not exhausted but a valid
         marker code is not found, the function puts back any bytes it consumed
         before returning false.  This allows the caller to continue reading
         from the point where the function was first called. */
    kdu_uint16 get_code() { return code; }
      /* Returns 0 if no actual marker is available yet. */
    int get_length() { return length; }
      /* Returns length of marker segment (not including the length specifier).
         Returns 0 if no actual marker available, or if the marker is a
         delimiter (no segment). */
    kdu_byte *get_bytes() { return buf; }
      /* Returns pointer to marker segment bytes immediately following the
         length specifier. */
    void clear() { code = 0; length = 0; encountered_skip_code = false; }
      /* Erase internal state so that no marker is advertised.  This is
         useful if the marker has already been inspected and found to be
         not useful. */
  private: // Data
    kd_input *source; // If NULL, `read' may not be used.
    kd_codestream *codestream;
    kdu_uint16 code;
    int length;
    int max_length; // Number of bytes which the buffer can store.
    kdu_byte *buf;
    bool encountered_skip_code;
  };

/*****************************************************************************/
/*                                 kd_pp_markers                             */
/*****************************************************************************/

class kd_pp_markers {
  public: // Member functions
    kd_pp_markers()
      { list = NULL; }
    ~kd_pp_markers();
    void add_marker(kd_marker &copy_source); 
      /* Copies the `kd_marker' object into a new `kd_pp_marker_list'
         element, inserting it into the evolving list on the basis of its
         Zppm or Zppt index.  Markers need not be added in order. */
    void transfer_tpart(kd_pph_input *pph_input);
      /* Transfers an entire tile-part of packed packet header data from
         the internal marker list to the `pph_input' object, which may
         then be used as a source object for packet header reading.  If the
         object is managing a list of PPM markers, the function expects to
         find the total number of bytes associated with the next tile-part
         embedded in the current marker segment.  Otherwise, all marker
         segments are dumped into the `pph_input' object.  Marker segments
         whose data have been consumed are deleted automatically. */
    void ignore_tpart();
      /* Same as `transfer_tpart' except that the data are simply discarded.
         This is useful when discarding a tile for which PPM marker information
         has been provided. */
  private: // Definitions

      class kd_pp_marker_list : public kd_marker {
        public: // Member functions
          kd_pp_marker_list(kd_marker &copy_source) : kd_marker(copy_source)
            { next = NULL; }
        public: // Data
          kd_pp_marker_list *next;
          int znum;
          int bytes_read;
        };

  private: // Convenience functions
    void advance_list();
  private: // Data
    bool is_ppm; // Otherwise, list contains PPT markers.
    kd_pp_marker_list *list;
  };

/*****************************************************************************/
/*                            kd_tpart_pointer                               */
/*****************************************************************************/

struct kd_tpart_pointer {
    kdu_long address; // Offset relative to the start of the code-stream.
    kd_tpart_pointer *next; // For building linked lists.
  };

/*****************************************************************************/
/*                               kd_tile_ref                                 */
/*****************************************************************************/

#define KD_TREF_FLAGS_OPEN           0x01
#define KD_TREF_FLAGS_CLOSED         0x02
#define KD_TREF_FLAGS_OPEN_PENDING   0x10
#define KD_TREF_FLAGS_CLOSE_PENDING  0x20
#define KD_TREF_FLAGS_ENQUEUED       0x40
#define KD_TREF_FLAGS_NEED_WAKEUP    0x80

struct kd_tile_ref {
    kd_tpart_pointer *tpart_head;
    kd_tpart_pointer *tpart_tail;
    kd_tile *tile; // Pointer to the actual tile or else NULL
    kdu_interlocked_int32 flags; // See below
    kdu_thread_entity_condition *wakeup;
    kdu_interlocked_ptr bkgnd_next;
  };
  /* Notes:
       This structure is used to build an array of references to each tile
       in the code-stream.  The array is allocated when the codestream is
       constructed, but the actual `kd_tile' objects are only instantiated
       when needed and destroyed as soon as possible.
       [//]
       In multi-threaded applications, the `kd_tile' objects may be created,
       opened, closed and destroyed asynchronously, in a background processing
       job managed by the `kd_cs_thread_context' object. This is facilitated
       by the interlocked atomic `flags' member, that holds various
       combinations of flag bits defined by the macros `KD_TREF_FLAGS_OPEN',
       `KD_TREF_FLAGS_CLOSED', `KD_TREF_FLAGS_OPEN_PENDING',
       `KD_TREF_FLAGS_CLOSE_PENDING', `KD_TREF_FLAGS_ENQUEUED' and
       `KD_TREF_FLAGS_NEED_WAKEUP'.  Here is a brief description of how the
       flags are used:
       [>>] Both `tile' and `flags' start out being all-zero.
       [>>] An open tile that is accessible through a valid `kdu_tile'
            interface should have the `KD_TREF_FLAGS_OPEN' flag set, possibly
            with the `KD_TREF_FLAGS_ENQUEUED' flag also set, but nothing
            else.  Of course, the `tile' member should be non-NULL in this
            case.
       [>>] When an open tile is closed, the `KD_TREF_FLAGS_OPEN' flag is
            reset.  If the closure is permanent, in the sense that the tile
            cannot be re-opened at least until the codestream is restarted,
            the `KD_TREF_FLAGS_CLOSED' flag is set.  However, for interchange
            codestreams and input codestreams that are marked as persistent,
            tiles can be re-opened, so the `KD_TREF_FLAGS_CLOSED' flag is
            omitted.  After closure, the `tile' member may become NULL or
            `KD_EXPIRED_TILE', but it may also continue to point to the
            original `kd_tile' object; the latter is appropriate if the tile
            can be re-opened or the codestream can be restarted.
       [>>] In multi-threaded environments, the `kdu_tile::close' function
            sets the `KD_TREF_FLAGS_CLOSE_PENDING' and `KD_TREF_FLAGS_ENQUEUED'
            flags, while also resetting the `KD_TREF_FLAGS_OPEN' flag to 0,
            passing the object to `kd_cs_thread_context::append_to_tile_queue'
            (unless the `KD_TREF_FLAGS_ENQUEUED' flag was already set).  The
            `KD_TREF_FLAGS_CLOSE_PENDING' and `KD_TREF_FLAGS_OPEN' flags will
            never be found together, but `KD_TREF_FLAGS_CLOSE_PENDING' means
            that the `kd_tile::close' function has not yet been called and
            this must happen before the tile can be re-opened.  The background
            processing thread will eventually close the tile and remove the
            `KD_TREF_FLAGS_CLOSE_PENDING' flag.
               The `KD_TREF_FLAG_CLOSED' flag is not set while a tile is
            scheduled for background closure, but may be set once the closure
            operation is complete, as explained above.
               It is possible to open tiles that are scheduled for background
            closure, assuming that the tile is re-openable.  For example, the
            `kdu_codestream::open_tile' can be called while a tile is in the
            `KD_TREF_FLAGS_CLOSE_PENDING' state.  In this case, the relevant
            flags are removed and the closure is performed prior to re-opening,
            without needing to wait for the background job to get around to
            processing the tile; however, the `KD_TREF_FLAGS_ENQUEUED' flag
            must be left untouched, since only the background job itself
            can remove that flag.
       [>>] In multi-threaded environments, the `kdu_codestream::open_tiles'
            function can be used to schedule background opening of a tile that
            either does not yet exist or has previously been the subject of
            a `kdu_tile::close' call; this is done by setting the
            `KD_TREF_FLAGS_OPEN_PENDING' and `KD_TREF_FLAGS_ENQUEUED' flags
            and passing us to `kd_cs_thread_context::append_to_tile_queue'
            (unless the `KD_TREF_FLAGS_ENQUEUED' flag was already set).
            A tile can have both the `KD_TREF_FLAGS_CLOSE_PENDING' and
            `KD_TREF_FLAGS_OPEN_PENDING' flags set, in which case the tile
            is effectively closed and re-opened once the background processing
            job dequeues it from the `kd_cs_thread_context'-managed queue.
            However, neither `KD_TREF_FLAGS_CLOSE_PENDING' nor
            `KD_TREF_FLAGS_OPEN_PENDING' can be present together with
            `KD_TREF_FLAGS_OPEN'.
       [>>] The `KD_TREF_FLAGS_ENQUEUED' flag is present whenever the current
            object is on the queue managed by `kd_cs_thread_context'; the
            tile is eventually stripped from the queue by the
            `kd_cs_thread_context::do_glock_processing' function.  It can
            happen that a background opening request is satisfied by a
            different thread that calls into `kdu_codestream::open_tile' before
            the background thread can get to it -- this would be rather weird,
            however. More likely, a thread that is enqueued for background
            closing is directly opened by a non-background thread that calls
            `kdu_codestream::open_tile'.  Both cases are safe, because the
            opening operation is protected by the `KD_THREADLOCK_GENERAL'
            mutex, and in both cases the pending open/close flags are removed
            from `flags' by the immediate open request.  However, the
            `KD_TREF_FLAGS_ENQUEUED' flag can only be removed by the
            `kd_cs_thread_context::do_glock_processing' function which dequeues
            `kd_tile_ref' objects that have previously been enqueued for
            background processing.
       [>>] A thread can wait upon the completion of a background tile opening
            operation by calling `kdu_codestream::access_tile'.  If that
            function encounters the `KD_TREF_FLAGS_OPEN_PENDING' flag, it
            concurrently sets `KD_TREF_FLAGS_NEED_WAKEUP', having first
            set the `wakeup' member to point to the thread's current wait
            condition (see `kdu_thread_entity::get_condition').  When the
            `KD_TREF_FLAGS_OPEN_PENDING' flag is removed, any
            `KD_TREF_FLAGS_NEED_WAKEUP' condition is removed at the same time
            and the `wakeup' condition is signalled.
       [//]
       As mentioned above, `wakeup' is used by threads that are waiting inside
       `kdu_codestream::access_tile' for a pending tile opening operation to
       complete.
          It is worth mentioning here that while the calls to
       `kdu_codestream::open_tile', `kdu_codestream::open_tiles',
       `kdu_codestream::access_tile' and `kdu_tile::close' that manipulate
       this object are thread-safe with respect to the internal machinery
       and the manipulation of other tiles by other threads (assuming the
       calls are accompanied by the caller's `kdu_thread_env' reference), it
       cannot generally be safe for multiple threads to access a single tile
       concurrently.  In fact, this makes little sense, since if two
       asynchronous threads attempt to open and close a tile, the behaviour
       must be undefined.  Tile resources are not reference counted.  If an
       application wishes to do something like this, it must provide its own
       external synchronization methods to ensure that opening and closing
       of tile interfaces is serialized.  For this reason, we do not need to
       allow for multiple threads to wait upon the opening of a single tile,
       which means that a single `wakeup' member is always sufficient.
       [//]
       The `bkgnd_next' member is used to maintain the background tile
       processing queue within `kd_cs_thread_context'.  It is manipulated
       only by `kd_cs_thread_context' member functions.
       [//]
       The `tpart_head' and `tpart_tail' members manage a list of
       addresses to tile-parts of the tile.  The list may be constructed
       by reading TLM marker segments in the main code-stream header, or it
       may be constructed dynamically as tile-parts are encountered within
       the code-stream, enabling them to be re-opened at a later point if
       the compressed data-source supports seeking.  The list should be
       empty if the source does not support seeking.
          Once we are sure that all tile-part addresses have been found
       for the tile, the `tpart_tail' member is set to NULL.
  */

/*****************************************************************************/
/*                              kd_tlm_generator                             */
/*****************************************************************************/

class kd_tlm_generator {
  public: // Member functions
    kd_tlm_generator()
      {
        num_tiles = max_tparts = num_elts = 0; elts = NULL;
        tile_data_bytes=0;
      }
    ~kd_tlm_generator() { if (elts != NULL) delete[] elts; }
    bool init(int num_tiles, int max_tparts, int tnum_bytes, int tplen_bytes);
      /* Call this function after construction or `clear' in order to
         initialize the object in preparation for generating TLM marker
         segments.
            If the number of tiles or tile-parts identified here
         cannot be accommodated by a legal set of TLM marker segments,
         having the indicated precisions for tile-part numbers (`tnum_bytes')
         and tile-part lengths (`tplen_bytes'), the function returns false
         and `exists' will continue to return false.
            Otherwise, the object is initialized and subsequent calls to
         `exists' will return true.  Note that the number of tiles given
         here is the total number in the entire code-stream.  This might
         be more than the number of tiles being compressed in the
         current fragment, in which case the `elts' array will not be
         full by the time `write_tlms' is called. */
    bool exists() { return (num_tiles > 0); }
    bool operator!() { return (num_tiles == 0); }
    void clear()
      {
        num_tiles = max_tparts = num_elts = tnum_prec = tplen_prec = 0;
        record_bytes = 0; tile_data_bytes = 0;
        if (elts != NULL)
          { delete[] elts; elts = NULL; }
      }
    int get_max_tparts() { return max_tparts; }
      /* Returns the parameter recorded with the `ORGgen_tlm' parameter
         attribute, which triggered the creation of this object.  The
         value returned here is guaranteed to lie in the range 1 to 255. */
    int get_tlm_bytes() { return tlm_bytes; }
      /* Returns the total number of bytes occupied by TLM marker segments.
         This value is computed immediately by the object's constructor; it
         does not depend on the actual tile-part length values added later
         via `add_tpart_length'. */
    void write_dummy_tlms(kd_compressed_output *out);
      /* Writes a properly sized set of TLM marker segments to the `out'
         object, except that all tile-part lengths are set equal to 0.  It
         is most important that the TLM marker segments appear at the very
         end of the main codestream header, since a pointer to the start
         of the TLM data will later be generated on the basis of the
         amount of tile-data which has been written, plus the size of the
         TLM data. */
    void add_tpart_length(int tnum, kdu_long length);
      /* Call this function as the tile-part length information becomes
         available while generating the code-stream.  The actual marker
         TLM marker segments can be written only once all tile-part lengths
         have been added. */
    void write_tlms(kdu_compressed_target *tgt,
                    int prev_tiles_written, kdu_long prev_tile_bytes_written);
      /* This function writes the final TLM marker segments.  The `tgt'
         object's `start_rewrite' and `end_rewrite' functions are used to
         reposition the output stream over the previously written dummy
         TLM marker segments and rewrite them with valid tile-part length
         values.  The `prev_tiles_written' and `prev_tile_bytes_written'
         arguments are identical to the values originally supplied to
         `kdu_codestream::create'.  They are used to determine the
         location of the TLM marker information which must be written,
         since some of it may have previously been written when compressing
         different codestream fragments. */
  private: // Structures
      struct kd_tlm_elt {
          kdu_uint16 tnum;
          kdu_uint32 length;
        };
  private: // Data
    int num_tiles;
    int max_tparts;
    int tnum_prec; // Precision (number of bytes) used for tile numbers
    int tplen_prec; // Precision (number of bytes) used for tile-part lengths
    int record_bytes; // Bytes for each tile-part record in TLM marker segments
    int tlm_bytes; // Total number of bytes occupied by TLM marker segments
    int num_elts; // num_tiles * max_tparts
    int elt_ctr; // Pointer to next unfilled entry in `elts'
    kdu_long tile_data_bytes; // sum of all the tile-part lengths added so far
    kd_tlm_elt *elts; // One element for each tile-part, in sequence.
  };

/*****************************************************************************/
/*                         kd_tpart_pointer_server                           */
/*****************************************************************************/

class kd_tpart_pointer_server {
  public: // Member functions
    kd_tpart_pointer_server()
      {
        groups = NULL; free_list = NULL; tlm_markers = NULL;
        translated_tlm_markers = false;
      }
    ~kd_tpart_pointer_server();
    bool using_tlm_info() { return translated_tlm_markers; }
      /* Returns true if TLM data was used to construct tile-part
         address lists in the most recent call to `translate_markers'. */
    void add_tlm_marker(kd_marker &marker);
      /* Call this function whenever a TLM marker segment is encountered,
         while parsing the main header. */
    void translate_markers(kdu_long first_sot_address,
                           int num_tiles, kd_tile_ref *tile_refs);
      /* Call this function when the main header has been completely read,
         passing in the seek position associated with the first byte of
         the first SOT marker in the code-stream.  This is the location of
         the SOT marker, relative to the start of the code-stream.  It is
         used to convert tile-part lengths into absolute addresses for
         each tile-part.  If no TLM marker segments have been read, the
         function does nothing.  Otherwise, it fills in the tile-part
         address list in each of the `num_tiles' entries of the
         `tile_refs' array.  If the available TLM information is
         insufficient to fully initialize the addresses of all tile-parts
         of each tile, the function issues a warning (since is is strictly
         illegal to have only partial TLM information). */
    void add_tpart(kd_tile_ref *tile_ref, kdu_long sot_address);
      /* Call this function when a new tile-part is encountered in a
         persistent seekable compressed data source, where the tile-part
         address was not already in the list of known tile-part addresses
         for the tile.  This allows the tile to be loaded or reloaded at
         a later time without reparsing the entire code-stream. */
  private: // Definitions

#     define KD_POINTER_GROUP_SIZE 32
      struct kd_pointer_group {
          kd_tpart_pointer elements[KD_POINTER_GROUP_SIZE];
          kd_pointer_group *next;
        };
        /* The purpose of this structure is to avoid an unnecessarily large
           number of calls to `new' and `delete'. */

      class kd_tlm_marker_list : public kd_marker {
        public: // Member functions
          kd_tlm_marker_list(kd_marker &copy_source) : kd_marker(copy_source)
            { next = NULL; }
        public: // Data
          kd_tlm_marker_list *next;
          int znum; // TLM sequence number used for concatenating info.
        };
        /* Objects of this class are used to store TLM marker segments in
           a list, so that they can be correctly ordered and their contents
           extracted. */

  private: // Data
    kd_tlm_marker_list *tlm_markers; // List of TLM marker segments.
    kd_pointer_group *groups; // List of pointer record groups allocated.
    kd_tpart_pointer *free_list; // List of empty pointer records not yet used
    bool translated_tlm_markers;
  };
  /* Notes:
       This object serves two roles: 1) it collects, resequences and
       translates TLM marker segments from the code-stream's main header;
       and 2) it manages the allocation of tile-part address lists, so as
       to avoid excessive memory fragmentation.  The storage for these
       lists is deallocated only when the present object is destroyed. */

/*****************************************************************************/
/*                          kd_precinct_pointer_server                       */
/*****************************************************************************/

class kd_precinct_pointer_server {
  public: // Member functions
    kd_precinct_pointer_server()
      { buf_server = NULL; }
    ~kd_precinct_pointer_server()
      { disable(); }
    void restart()
      { disable(); } // Called if the code-stream is restarted.
    bool is_active()
      { return (buf_server != NULL); }
    void initialize(kd_buf_server *buf_server)
      /* Call this function when constructing the tile to which the
         object is attached, unless the code-stream is being read from
         a non-seekable source, in which case you should leave the object
         uninitialized and none of the other functions will have any effect. */
      { assert(this->buf_server == NULL); this->buf_server = buf_server;
        head = tail = NULL; head_pos = tail_pos = available_addresses = 0;
        next_address = 0; tpart_bytes_left = 0; num_layers = 0;
        something_served = final_tpart_with_unknown_length = false;
        next_znum = 0; packets_left_in_precinct=0; }
    void add_plt_marker(kd_marker &marker, kdu_params *cod_params,
                        kdu_params *poc_params);
      /* Call this function whenever a PLT marker segment is encountered,
         while parsing a tile-part header.  The `cod_params' and `poc_params'
         arguments must point to the relevant tile's COD and POC parameter
         objects.  The function uses these to determine whether or not the
         pointer information can be used.  In particular, it is used only
         if the current progression order sequences all packets of any
         given precinct together and if the POC object indicates that there
         will be no progression order changes.  These conditions are checked
         again after each tile-part header has been loaded (see the
         `start_tpart_body' member function).  The reason for the conditions
         is that at most one seek address can be maintained for any given
         precinct. */
    void start_tpart_body(kdu_long start_address, kdu_uint32 tpart_body_length,
                          kdu_params *cod_params, kdu_params *poc_params,
                          bool packed_headers,
                          bool final_tpart_with_unknown_length);
      /* This function must be called before addresses can be recovered for
         precincts whose packets appear in the current tile-part.  The
         `start_address' and `tpart_body_length' arguments identify the
         absolute seeking address of the first byte in the body of the
         tile-part and the total number of body bytes in the tile-part.
         These will be used to determine which packets belong to the
         tile-part.  If this is the last tile-part of the tile and its
         length is unknown, the `final_tpart_with_unknown_length' argument
         should be true, and `tpart_body_length' is ignored.
            This function may determine that precinct address information
         cannot be served up for the current tile, for one of a number of
         This is generally because the packets of each precinct are found
         not to be contiguous within the tile-part, or because the presence
         of POC information renders this a likely problem.  If the
         number of quality layers is found to have changed from the point
         when packet length information was first converted into precinct
         lengths and addresses, we must also conclude that address information
         is not available.  A further reason for discarding packet length
         information is if packet headers have been packed into marker
         segments, since this requires that the packets be processed in order
         and once one by one.  If any such determination is made before any
         precinct addresses have been served up, the object is simply
         disabled so that calls to `get_precinct_address' will return 0.
         Otherwise, a terminal error is generated.  In this event, the
         code-stream might need to be opened again with seeking disabled
         so that the present object will never be initialized. */
    kdu_long get_precinct_address()
      { return ((buf_server==NULL)?((kdu_long) 0):pop_address()); }
      /* Returns the address of the next precinct (in the sequence determined
         by the current progression order) in the tile.  This address may be
         used to seek to the relevant location when reading the precinct's
         packets.
            The function returns 0 if seeking is not possible, if
         packet length information cannot be found in the code-stream,
         or if packet length information is not usable for one of a number
         of reasons.  As explained above, packet length information is usable
         only if the packets of a precinct all appear consecutively so that
         each precinct is characterized by only one seeking address.
            The function returns -1 if seeking is possible, but the first
         packet of the precinct belongs to a tile-part which has not yet been
         opened.  In this event, the `kd_tile::read_tile_part_header' member
         function will have to be called until the information becomes
         available. */
  private: // Helper functions
    kdu_long pop_address();
      /* Does all the work of `get_precinct_address'. */
    void disable()
      { // Call to discard contents and cease recording new PLT info.
        if (buf_server == NULL) return;
        while ((tail=head) != NULL)
          { head = tail->next; buf_server->release(tail); }
        buf_server = NULL;
      }
    void initialize_recording()
      { // Call before parsing the first PLT marker segment.
        assert((buf_server != NULL) && (head == NULL));
        head = tail = buf_server->get();
      }
    void record_byte(kdu_byte val)
      { // Call to record a byte of encoded precinct length info.
        assert(tail != NULL);
        if (tail_pos == KD_CODE_BUFFER_LEN)
          { tail=tail->next=buf_server->get(); tail_pos = 0; }
        tail->buf[tail_pos++] = val;
      }
    kdu_byte retrieve_byte()
      { // Call to retrieve a byte of encoded precinct length info.
        assert((head != tail) || (head_pos < tail_pos));
        if (head_pos == KD_CODE_BUFFER_LEN)
          { kd_code_buffer *tmp = head; head=head->next; head_pos = 0;
            buf_server->release(tmp); }
        return head->buf[head_pos++];
      }
  private: // Data
    kd_buf_server *buf_server;
    kd_code_buffer *head, *tail;
    int head_pos; // First valid byte in the `head' buffer (for retrieving)
    int tail_pos; // First unused byte in the `tail' buffer (for recording)
    int available_addresses; // Precinct addresses recorded, but not retrieved
    kdu_long next_address; // Address of the next precinct in current tile-part
    kdu_uint32 tpart_bytes_left; // Bytes left in the current tile-part
    bool final_tpart_with_unknown_length; // If true, previous member ignored
    int num_layers; // Packets/precinct assumption used to parse PLT info
    bool something_served; // False until the first valid address is served
    kdu_byte next_znum; // PLT markers must appear in sequence
    kdu_long precinct_length; // Accumulator for lengths of packets
    int packets_left_in_precinct;
  };

/*****************************************************************************/
/*                             kd_compressed_stats                           */
/*****************************************************************************/

class kd_compressed_stats {
  /* An object of this class is used to monitor statistics of the compression
     process.  One application of these statistics is the provision of feedback
     to the block encoder concerning a conservative lower bound to the
     distortion-length slope threshold which will be found by the PCRD-opt
     rate allocation algorithm when the image has been fully compressed.  This
     allows the encoder to skip coding passes which are almost certain to be
     discarded.
        The current very simple implementation makes the naive assumption
     that the compressibility of all subband samples is the same, regardless
     of the subband or resolution level.  This is OK if the number of samples
     which have been processed for each subband is proportional to the
     size of the subband, since then the average compressibility is a good
     measure to use in predicting rate control properties.  In practice,
     though, delay through the wavelet transform means that the percentage
     of samples seen for higher resolution subbands is generally higher than
     that seen for lower frequency subbands, until everything has been
     compressed. Since the higher frequency subbands are almost invariably
     more compressible, the predicted compressibility of the source is
     estimated too high and so the predicted rate-distortion slope threshold
     passes considerably more bits than the final rate allocation will.  This
     renders the prediction excessively conservative for most images.  A
     good fix for this would be to keep track of the percentage of subband
     samples which have been compressed in each resolution level and use this
     information to form a more reliable predictor.  This improvement might
     be included in a future version.
        In multi-threaded applications, the system maintains one object of
     this class for each thread, in addition to a master object that is
     interpreted as the global statistics manager.  Threads update their
     local view of the compressed statistics each time a code-block is
     encoded and reflect this information to the global (or master) object
     periodically.  The object provides methods to facilitate this process,
     that we refer to as "transcription".
  */
  public: // Member functions
    kd_compressed_stats(kdu_long total_samples, kdu_long target_bytes,
                        bool enable_trimming)
      { 
        this->total_samples = total_samples;
        this->trimming_enabled = enable_trimming;
        this->next_trim = (total_samples+7)>>3;
        this->conservative_extra_samples = 4096 + (total_samples>>4);
        this->target_rate =
          (total_samples==0)?1.0:(((double) target_bytes)/total_samples);
        num_coded_samples = 0;
        min_quant_slope = 2047;  max_quant_slope = 0;
        block_slope_threshold = remaining_slope_threshold = 0;
        memset(quant_slope_rates,0,sizeof(quant_slope_rates[0])*2048);
        transcribe_counter = 0;
        next_transcribe_interval = 2;
        lock.set(0);
        last_pcrd_opt_min_threshold = -1;
        next = NULL;
      }
    kd_compressed_stats(const kd_compressed_stats *copy)
      { /* This constructor initializes the object in exactly the
           same way that `copy' was initialized.  The information drawn from
           `copy' never gets modified by any of the other member functions. */
        this->total_samples = copy->total_samples;
        this->trimming_enabled = copy->trimming_enabled;
        this->next_trim = (total_samples+7)>>3;
        this->conservative_extra_samples = 4096 + (total_samples>>4);
        this->target_rate = copy->target_rate;
        num_coded_samples = 0;
        min_quant_slope = 2047;  max_quant_slope = 0;
        block_slope_threshold = remaining_slope_threshold = 0;
        memset(quant_slope_rates,0,sizeof(quant_slope_rates[0])*2048);
        transcribe_counter = 0;
        next_transcribe_interval = 2;
        lock.set(0);
      }
    bool is_empty() const { return (num_coded_samples == 0); }
      /* Returns true if no information has been entered yet via the
         `update_stats' function. */
    bool update_stats(kdu_block *block)
      { /* Invoked by `kdu_subband::close_block'.  If the function returns
           true, it is recommended that the compressed data be trimmed back
           to a size consistent with the target compressed length at this
           point.  Remember to invoke `update_quant_slope_thresholds' once
           this function returns. */
        num_coded_samples += block->size.x*block->size.y;
        int quant_slope, length = 0;
        for (int n=0; n < block->num_passes; n++)
          {
            length += block->pass_lengths[n];
            if (block->pass_slopes[n] == 0)
              continue;
            quant_slope = (block->pass_slopes[n] >> 4) - 2048;
            if (quant_slope < min_quant_slope)
              { 
                if (quant_slope < 0)
                  min_quant_slope = quant_slope = 0;
                else
                  min_quant_slope = quant_slope;
              }
            if (quant_slope > max_quant_slope) max_quant_slope = quant_slope;
            assert((quant_slope >= 0) && (quant_slope < 2048));
            quant_slope_rates[quant_slope] += length;
            length = 0;
          }
        if (trimming_enabled && (num_coded_samples > next_trim))
          { next_trim += (total_samples+7)>>4; return true; }
        return false;
      }
    void update_quant_slope_thresholds()
      { /* This function should be called after `update_stats' to ensure that
           the most up-to-date slope threshold is available for return via
           `get_conservative_slope_threshold'.  If desired, multiple calls
           to `update_stats' may precede the call to this function, which
           may improve efficiency.  The reason for computing the slope
           threshold here, rather than in `get_conservative_slope_threshold'
           is that the latter function may be invoked asynchronously by any
           number of threads of execution, whereas the present function is
           always invoked while holding the lock granted by `try_lock'
           (in multi-threaded applications), so that other threads cannot
           modify the statistics while this function is in progress. */
        int n;  kdu_long max_bytes, cumulative_bytes;
        // Adjust `block_slope_threshold'
        max_bytes = num_coded_samples + conservative_extra_samples;
        max_bytes = 1 + (kdu_long)(max_bytes * target_rate);
        for (cumulative_bytes=0, n=max_quant_slope; n >= min_quant_slope; n--)
          if ((cumulative_bytes += quant_slope_rates[n]) >= max_bytes)
            break;
        block_slope_threshold = n;
        // Adjust `remaining_slope_threshold'
        max_bytes = total_samples;
        max_bytes = 1 + (kdu_long)(max_bytes * target_rate);
        for (cumulative_bytes=0, n=max_quant_slope; n >= min_quant_slope; n--)
          if ((cumulative_bytes += quant_slope_rates[n]) >= max_bytes)
            break;
        remaining_slope_threshold = n;
      }
    kdu_uint16
      get_conservative_slope_threshold(bool assume_all_coded=false) const
      { /* The slope threshold generated by PCRD-opt is unlikely to be
           smaller than that returned here -- remember that smaller thresholds
           mean that more compressed data are included in the code-stream.
           If `assume_all_coded' is true, the slope threshold is based on the
           assumption that no bytes will be generated for any further samples.
           Otherwise, the assumption is that future samples will have the
           same average compressibility as those already coded.  Note that
           since compressors tend to output a higher proportion of high
           frequency subband samples than low frequency subband samples at
           any point up to the end of the image, the actual compressibility
           of future samples can be expected to be lower on average, making
           our estimate more conservative. */
        int val =
          (assume_all_coded)?remaining_slope_threshold:block_slope_threshold;
        return (val <= 0)?1:((val<<4)+(2048<<4)-1);
      }
   kdu_uint16 get_pcrd_opt_min_threshold()
     { /* Returns the minimum slope threshold that is worth considering
          when the `pcrd_opt' function is invoked.  Essentially, this is
          just (2048+`min_quant_slope')<<4.  However, the information
          collected within multiple threads might not have been transcribed
          to the master object.  For this reason, the function scans the
          thread-specific stats objects connected via the `next' links,
          looking for the smallest value of `min_quant_slope'.  The
          result is stored internally, after which later calls to the function
          (within a given codestream flush cycle or a successive flush cycle)
          only examine the current object's `min_quant_slope' member to see
          if the stored minimum threshold value needs to be reduced -- i.e.,
          thread specific objects are examined only once. */
       int t_min_val, min_val=this->min_quant_slope;
       if (min_val > this->max_quant_slope)
         return 0; // `transcribe' has not yet been called
       if (last_pcrd_opt_min_threshold < 0)
         { // First call to this function; check other thread records 
           for (kd_compressed_stats *scan=next; scan != NULL; scan=scan->next)
             if ((t_min_val = scan->min_quant_slope) < min_val)
               min_val = t_min_val;
         }
       else if (last_pcrd_opt_min_threshold < min_val)
         min_val = last_pcrd_opt_min_threshold;
       last_pcrd_opt_min_threshold = min_val;
       if (min_val == 0)
         return 0;
       else
         return (kdu_uint16)((min_val+2048)<<4);
     }
    void set_next(kd_compressed_stats *nxt)
      { this->next = nxt; }
  public: // Member functions used only with multi-threaded interaction
    bool need_transcribe()
      { return ((--transcribe_counter) <= 0); }
      /* Individual threads invoke this function on their own local version
         of this object after each call to `update_stats'.
         If the function returns true, the thread should invoke the
         shared master object's `try_lock', `release_lock' and
         `transcribe' functions to transcribe its contents to the
         master object.  This protocol ensures regular copying of information
         to the shared master object, following a schedule in which copying
         can become less frequent once more statistics are available. */
    bool try_lock()
      { return (lock.exchange(1) == 0); }
      /* Before you can invoke `transcribe' or `update_quant_slope_threshold'
         on a shared master object, an individual thread must take out this
         lock.  If the attempt to acquire the lock fails, the caller does 
         not normally need to persist -- next time `need_transcribe' is
         called, it will return true and so the lock will be tried after
         coding each code-block until successful. */
    void release_lock()
      { lock.set(0); }
      /* You must release the lock after each successful call to `try_lock'. */
    bool transcribe(kd_compressed_stats *src)
      { /* Similar to the `update_stats' function, except that the information
           is copied from a thread-local `src' object to the current
           object (the shared master object), returning the thread local
           object to the empty state. */
        if (src->num_coded_samples <= 0) return false;
        num_coded_samples += src->num_coded_samples;
        src->num_coded_samples = 0;
        if (src->min_quant_slope < this->min_quant_slope)
          this->min_quant_slope = src->min_quant_slope;
        if (src->max_quant_slope > this->max_quant_slope)
          this->max_quant_slope = src->max_quant_slope;
        for (int n=src->min_quant_slope; n <= src->max_quant_slope; n++)
          { quant_slope_rates[n] += src->quant_slope_rates[n];
            src->quant_slope_rates[n] = 0; }
        src->min_quant_slope = 2047;  src->max_quant_slope = 0;
        src->transcribe_counter = src->next_transcribe_interval;
        if ((src->next_transcribe_interval <<= 1) > 16)
          src->next_transcribe_interval = 16;
        if (trimming_enabled && (num_coded_samples > next_trim))
          { next_trim += (total_samples+7)>>4; return true; }
        return false;
      }
  private: // Data
    KD_PRIVATE_CACHELINE_SEPARATOR(_leadin)
    double target_rate; // Expressed in bytes per sample, not bits per sample
    kdu_long total_samples; // Total number of subband sample in entire image
    kdu_long next_trim; // Num samples at which memory should next be trimmed
    kdu_long conservative_extra_samples; // Add to coded samples to be safe
    kdu_long num_coded_samples; // Number of samples already coded
    kdu_long quant_slope_rates[2048]; // See below
    int min_quant_slope, max_quant_slope; // See below
    int block_slope_threshold; // See below
    int remaining_slope_threshold; // See below
    bool trimming_enabled;
  private: // State information for thread-specific versions of the function
    int transcribe_counter;       // These two vars are used to implement
    int next_transcribe_interval; //  `need_transcribe' and `transcribe'.
    kdu_interlocked_int32 lock; // Used to implement `try_lock'/`release_lock'
  private: // Members used for rate control (base stats object only)
    int last_pcrd_opt_min_threshold; // -ve until first call to `get_pcrd_...'
    kd_compressed_stats *next; // Links thread-specific objs to master (head)
    KD_PRIVATE_CACHELINE_SEPARATOR(_trailer)
  };
  /* Notes:
        The `quant_slope_rates' array holds the total number of coded
     bytes which belong to each of a set of 2048 quantized distortion-length
     slope bins.  The original distortion-length slopes have a 16-bit unsigned
     logarithmic representation (see discussion of the "pass_slopes" member
     array in the "kdu_block" structure).  The index of the bin to which a
     given slope, lambda, belongs is given by
         bin = max{0, floor(lambda/16)-2048}
     Rounding down means that 16*(bin+2048) is the smallest slope consistent
     with the bin, so that summing the entries from bin to 2047 yields the
     maximum number of code-block bytes which could be contributed to the
     compressed image if the PCRD-opt algorithm selected a slope threshold of
     16*(bin+2048).  Note, however, that bin 0 is different, in that it
     represents original slopes in the range 1 to 16*2048+15 (slope 0 is
     not a valid slope threshold).
        The `block_slope_threshold' member essentially gives the current
     conservative slope threshold to be used during block coding -- this
     is the slope we want when calling `get_conservative_slope_threshold'
     with the `assume_all_coded' argument set to false.  More specifically,
     `block_slope_threshold' is the smallest index into the `quant_slope_rates'
     array such that the cumulative number of bytes represented by that
     entry and all higher entries is strictly less than the target rate
     multiplied by the number of samples coded so far (plus some
     conservative extra samples).  The corresponding slope threshold is
     obtained by adding 2048 to this value, multiplying by 16 and
     subtracting 1 -- again, though bin 0 needs to be treated specially.
        The `remaining_slope_threshold' member plays the same role as
     `block_slope_threshold', except that it is based upon the total number
     of samples in the entire image, rather than just the total number of
     samples coded so far.  In this way, it essentially provides a
     conservative distortion-length slope threshold, which can safely be
     used to trim already generated coding passes from their respective
     code-blocks.  This value is used to generate the return value from
     `get_conservative_slope_threshold' when its `assume_all_coded' argument
     is set to true. */

/*****************************************************************************/
/*                            kd_codestream_comment                          */
/*****************************************************************************/

class kd_codestream_comment {
  public: // Member functions
    kd_codestream_comment()
      { readonly=false; is_text=is_binary=false;
        max_bytes=num_bytes=0; buf=NULL; next=NULL; }
    ~kd_codestream_comment()
      { if (buf != NULL) delete[] buf; }
    void init(int length, kdu_byte *data, bool is_text);
      /* This function is used only when parsing comment markers from a
         code-stream; if `is_text' is true, the `data' buffer contains
         `length' characters, starting from the first byte in the body of the
          comment marker; the array of characters may or may not be terminated
          with a null character -- if not, a null character is automatically
          appended to the internal representation.  This function leaves
          the object in the `read-only' state.
      */
    int write_marker(kdu_output *out, int force_length=0);
      /* If `out' is NULL, the marker segment is ony simulated.  In
         any event, the function returns the total number of bytes consumed
         by the comment marker segment, from the marker code to the
         last segment byte.
            If `force_length' is not 0, the marker segment will be forced
         to occupy an exact number of bytes; this is only appropriate for
         text comments, since they can be padded beyond the end of a null
         terminator.  If necessary, the text body of the marker segment will
         be truncated, or padded to the specified length.  A non-zero
         `force_length' value may not be less than 6, which is the length
         of the comment marker header.
            This function leaves the object in the `read-only' state.
      */
  private: // Data
    friend class kdu_core::kdu_codestream_comment;
    bool readonly; // True if you are not allowed to write to the text buffer
    bool is_text; // If true, `buf' is a null-terminated UTF-8 string
    bool is_binary; // If true, `buf' holds binary data
    int max_bytes; // `buf' can hold this many chars only
    int num_bytes; // Number of bytes in `buf' -- includes null terminator
    kdu_byte *buf; // Holds comment text.
  public: // Links
    kd_codestream_comment *next;
  };

/*****************************************************************************/
/*                             kd_packet_sequencer                           */
/*****************************************************************************/

class kd_packet_sequencer {
  public: // Member functions
    kd_packet_sequencer(kd_tile *tile)
      {
        this->tile = tile;
        init();
      }
    void init();
      /* Called when the containing tile is re-initialized for processing a
         new code-stream with exactly the same structural properties as the
         previous one. */
    int get_order() const { return state.order; }
      /* Returns one of the Corder_xxx constants. */
    void save_state();
      /* Saves all quantities associated with packet sequencing, both within
         this object and in the associated tile and its constituent precincts,
         so that they can be later restored to resume sequencing from where
         we left off. */
    void restore_state();
      /* Restores the state saved by the `save_state' member function. */
    kd_precinct_ref *next_in_sequence(kd_resolution* &res, kdu_coords &idx);
      /* This function returns a pointer to the `kd_precinct_ref' object
         which manages the precinct associated with the next packet in the
         relevant tile's packet sequence (see below for the possibility of
         a NULL return value).  The function also returns a
         pointer to the `kd_resolution' object which manages the returned
         precinct and the horizontal and vertical coordinates of the
         precinct within that resolution.  The precinct itself may be
         opened, if required, by passing `res' and `idx' directly into the
         `kd_precinct_ref::open' function -- see description of that
         function for more information.
            When desequencing packets from an input code-stream, the caller
         should first invoke the returned reference's
         `kd_precinct_ref::is_desequenced' member function to determine
         whether or not the present function has done all of the required
         desequencing for the precinct itself.  This happens if the
         precinct is directly addressable via a seek address recovered from
         the relevant tile's `kd_precinct_pointer_server' object.  If
         `kd_precinct_ref::is_desequenced' returns false, the caller should
         proceed to open the precinct with `kd_precinct_ref::open' and
         parse the next packet using `kd_precinct::desequence_packet'.  This
         has the effect of updating the precinct state so that the next
         call to `next_in_sequence' can move on to the next packet in
         sequence.
            When sequencing packets for an output code-stream, the caller
         should usually open the precinct and then call
         `kd_precinct::write_packet'.  Again, this enables the next call
         to `next_in_sequence' to move on to the next packet in sequence.
            If the caller does not actually take steps to desequence the
         next input packet or to write the next output packet, subsequent
         calls to this function will return exactly with exactly the same
         precinct reference.  This allows the caller to delay its packet
         parsing or generation activities.
             For both input and output codestreams, this function may return
         NULL.   In either case, a NULL return value means that
         no further packets can be sequenced based on the information
         available for the current tile-part.  For output codestreams, a
         NULL return value means that the caller must write another tile-part
         (or simulate such an activity).  After such an activity, the
         next call to this function will have access to the packet sequencing
         specifications in a new POC marker segment (if this were not possible
         the function would have generated a terminal error instead of
         returning NULL).  For input codestreams, a NULL return value means
         that the caller must advance to a new tile-part.  There are two
         possible reasons for this: 1) a new POC marker segment may be
         required to obtain sequencing specifications for the next packet;
         or 2) the `kd_precinct_pointer_server::get_precinct_address' function
         might have returned -1, meaning that precincts are addressable, but
         the first packet of the next precinct belongs to the tile's next
         tile-part.
             The function also returns NULL if all packets have been
         transferred to or from the code-stream. This event may be detected
         by checking the tile's `sequenced_relevant_packets' field; the value
         of that field is advanced by `kd_precinct::desequence_packet' when
         it is called or from within the present function if the precinct
         is addressable (this is the condition under which the returned
         reference's `kd_precinct_ref::is_desequenced' returns true
         immediately). */
  private: // Member functions
    bool next_progression();
      /* Sets up the next progression (or the very first progression).
         Returns false, if no further progressions are available without
         starting a new tile-part. */
    kd_precinct_ref *next_in_lrcp(kd_resolution* &res, kdu_coords &idx);
    kd_precinct_ref *next_in_rlcp(kd_resolution* &res, kdu_coords &idx);
    kd_precinct_ref *next_in_rpcl(kd_resolution* &res, kdu_coords &idx);
    kd_precinct_ref *next_in_pcrl(kd_resolution* &res, kdu_coords &idx);
    kd_precinct_ref *next_in_cprl(kd_resolution* &res, kdu_coords &idx);
       // These functions do most of the work of `next_in_sequence'.  They
       // return NULL if there are no more packets to sequence within the
       // current progression order.
  private: // Declarations
      struct kd_packet_sequencer_state {
        public: // Current sequencing bounds
          int order; // One of the orders defined in the scope of `cod_params'
          int res_min, comp_min; // Lower bounds (inclusive) for loops
          int layer_lim, res_lim, comp_lim; // Exclusive upper bounds for loops
        public: // Current looping state.
          int layer_idx; // Current layer index
          int comp_idx; // Current component index
          int res_idx; // Current resolution level index
          kdu_coords pos; // Precinct position indices in current tile-comp-res
        public: // Spatial sequencing parameters.
          kdu_coords grid_min; // Common grid start for spatial loops
          kdu_coords grid_inc; // Common grid increments for spatial loops
          kdu_coords grid_loc; // Common grid location for spatial loops
        public: // POC access parameters
          kdu_params *poc; // NULL unless POC-based sequencing
          int next_poc_record; // Index of next order record in current POC
        };
  private: // Data
    kd_tile *tile;
    int max_dwt_levels; // Over all components.
    bool common_grids; // True if all components have power-of-2 sub-sampling.
    kdu_coords grid_lim; // Point just beyond tile on the canvas
    bool state_saved;
    kd_packet_sequencer_state state;
    kd_packet_sequencer_state saved_state; // Used by `save_state' member.
  };

/*****************************************************************************/
/*                          kd_reslength_checker                             */
/*****************************************************************************/

class kd_reslength_checker {
  public: // Member functions
    kd_reslength_checker() { memset(this,0,sizeof(*this)); }
    ~kd_reslength_checker() { if (specs != NULL) delete[] specs; }
    bool init(cod_params *cod, int component_idx, int num_components,
              kd_reslength_checker component_checkers[]);
      /* Initializes the contents of the object based on any `Creslengths'
         parameter attribute found within `cod'.  In practice, `cod'
         is the relevant global, tile-specific, component-specific, or
         tile-component specific object retrieved by a call to
         `kdu_params::access_unique' with the appropriate tile and component
         indices; it may well be NULL, in which case the object is initialize
         with no constraints.
            If `component_idx' >= 0 and `component_checkers' is non-NULL, the
         function also looks for `Cagglengths' parameter attributes
         which may affect the initialization of the internal `redirect' array.
         Specifically, the indices found in any `Cagglengths' attribute are
         interpreted as image component indices that represent indices into
         the `component_checkers' array to the `kd_reslength_checker' object
         that should be referenced via the `redirect' array.  If any such
         component index lies outside the range 0 to `num_components'-1,
         the corresponding `redirect' array's entry is set to NULL, which
         means that no constraint will be applied.
            The function returns true if and only if an appropriate
         `Creslengths' or `Cagglengths' attribute was found. */
    void set_layer(int layer_idx);
      /* Use this function to walk through the quality layers.  The object's
         `max_bytes' and `num_bytes' arrays maintain state for only a current
         quality layer.  If `layer_idx' is 0, the `num_bytes' and
         `prev_layer_bytes' arrays are initialized to zeroes.  Otherwise, if
         `layer_idx' is greater than the value passed in a previous call,
         the `num_bytes' array is copied to the `prev_layer_bytes' array.
         In all other cases, the function initializes the `num_bytes' array
         with the information recorded in `prev_layer_bytes'.  This behaviour
         allows the function to be used to examine progressively higher
         quality layers, where each layer is visited multiple times to
         simulate the generation of packets -- each time the layer is
         visited, it is re-initialized with the total number of bytes
         found in all previous layers.  The function can also be used to
         explore several layers ahead of the last finalized layer during
         packet simulation, which is exactly what `kd_codestream::pcrd_opt'
         does when it encounters layers that do not have an explicit size
         target.  It is fine to explore compatibility of a later quality layer
         and then retreat to an earlier one, so long as we do not retreat
         to the layer whose contents were saved in `prev_layer_bytes' or
         an earlier one -- these conditions are checked. */
    bool check_packet(kdu_long packet_bytes, int depth)
      /* Updates internal records of the number of bytes associated with
         the given resolution (highest resolution corresponds to `depth'=0)
         and all higher resolutions (smaller depths).  Checks for violation
         of constraints, returning false if any constraints is violated. */
      { 
        assert((depth >= 0) && (depth <= 32));
        if ((current_layer_idx < 0) || !is_active)
          return true;
        for (int d=0; d <= depth; d++)
          { 
            kdu_long lim;
            kd_reslength_checker *tgt = redirect[d];
            if ((tgt != NULL) && ((lim=tgt->max_bytes[d]) > 0))
              { 
                if ((tgt->num_bytes[d] += packet_bytes) > lim)
                  return false;
              }
          }
        return true;
      }
  private: // Member variables
    bool is_active; // If the object imposes direct or indirect constraints
    int num_specs;
    kdu_long *specs;
    int current_layer_idx; // -1 if no call to `set_layer' yet.
    int prev_layer_idx; // -1 if nothing saved to `prev_layer_bytes' yet.
    kd_reslength_checker *redirect[33]; // Usually points to current object
    kdu_long max_bytes[33]; // Limit for this resolution
    kdu_long num_bytes[33]; // Accumulated number of bytes; see below
    kdu_long prev_layer_bytes[33];
  };
  /* This object manages a single collection of resolution length constraints,
     as provided via the `Creslengths' coding parameter attribute.  There
     are potentially (1+T)*(1+C) of these objects in a codestream, where T
     is the number of tiles and C is the number of image components.  These
     correspond to one global form, C component-specific forms, T
     tile-specific forms and C*T tile-component-specific forms.
        `num_specs' and `specs' together hold the parameter values actually
     extracted from the relevant `Creslengths' attribute, via the `init'
     function.  These together represent a collection of limits on the
     number of bytes at one or more resolutions, potentially for more than
     one quality layer.  Quality layers are separated by 0's in the
     `specs' array.
        `max_bytes' holds one limit for each potential resolution, starting
     from the full resolution and working down.  The entries in this array
     are derived from the `specs' array, for the quality layer whose index
     is passed to `set_layer'.
        `num_bytes[d]' holds the cumulative number of bytes for all packets
     whose lengths have been passed to `check_packet' so far, for packets
     at depth d or deeper.  This value is compared with `max_bytes[d]' to
     determine whether the resolution length constraint at depth d has been
     violated.
        `prev_layer_bytes' is used to initialize the `num_bytes' entries
     at the start of each new quality layer, based on the total number of
     bytes accumulated by `check_packet' in the previous layer, if any.
        The `redirect' array's entries normally all point to the current
     object.  However, the `Cagglengths' attribute may be used to redirect
     the accumulated packet bytes from one component-specific object to
     another component specific reslength checker, so that the entries at
     the corresponding depth in that object are used to accumulate and
     check aggregated byte counts.  The pointers found in the `redirect'
     array always reference an object that lies within the same tile
     as the current one (or globally if the current object is global). */

/*****************************************************************************/
/*                           kd_global_rescomp                               */
/*****************************************************************************/

struct kd_global_rescomp {
  public: // Member functions
    kd_global_rescomp()
      { 
        codestream = NULL; depth = comp_idx = 0;
        total_area = area_used_by_tiles = area_covered_by_tiles = 0;
        remaining_area = ready_area = 0;
        expected_area = attributed_area = 0;
        ready_fraction = reciprocal_fraction = -1.0;
        first_ready = last_ready = NULL;
      }
    ~kd_global_rescomp() { close_all(); }
    void close_all();
      /* Closes all precincts currently stored on the ready list.  This
         function is called either from a single-threaded context, or with
         the `KD_THREADLOCK_GENERAL' mutex locked. */
    void initialize(kd_codestream *codestream, int depth, int comp_idx);
      /* Call this function upon creation or restart of an output
         `kdu_codestream' object.  Releases any existing precincts and
         initializes all counters. */
    void notify_tile_status(kdu_dims tile_dims, bool uses_this_resolution);
      /* Call this function when each tile is opened, to indicate whether or
         not this resolution level exists within each component of that tile.
         This updates internal counters used for incremental rate control and
         slope prediction. */
    void add_ready_precinct(kd_precinct *precinct);
      /* Enters `precinct' on the ready list and adds the `KD_PFLAG_READY'
         flag to `precinct->flags'. */
    void close_ready_precinct(kd_precinct *precinct);
      /* Extracts the precinct from the list and releases all of its resources.
         Also resets the `KD_PFLAG_READY' flag from `precinct->flags'.
         This function is called from `precinct->write_packet' or
         `kd_precinct::cache_write_packets'; the caller is always in the
         "glock" context, meaning that the call is either single-threaded
         or else the `KD_THREADLOCK_GENERAL' mutex is locked. */
  public: // Data members
    kd_codestream *codestream;
    int depth, comp_idx;
    kdu_coords size; // Dimensions of the image at this component-resolution
    kdu_long total_area;
    kdu_long area_used_by_tiles;
    kdu_long area_covered_by_tiles;
    kdu_long remaining_area;
    kd_precinct *first_ready;
    kd_precinct *last_ready;
    kdu_long ready_area;
    kdu_long expected_area;
    kdu_long attributed_area;
    double ready_fraction; // ready / (expected + attributed) or -ve
    double reciprocal_fraction; // (expected + attributed) / ready or -ve
  };
  /* Notes:
        `depth' is 0 for the highest resolution level, increasing from there.
        `total_area' is the total area of this component-resolution.
        `area_used_by_tiles' is the area associated with the portion of this
     component-resolution covered by tiles which have already been opened and
     have precincts which contribute to the component-resolution.
        `area_covered_by_tiles' is the area associated with the portion of this
     component-resolution covered by tiles which have already been opened,
     regardless of whether those tiles have precincts which contribute to the
     component-resolution or not.  The ratio between `area_used_by_tiles' and
     `area_covered_by_tiles' is used to estimate the proportion of the
     remaining area which is likely to contribute precincts.
        `remaining_area' is the maximum area for which precincts must still
     be written to the code-stream.  This is equal to `total_area' minus
     `area_unused_by_tiles' minus the total area associated with all precincts
     in this component-resolution for which at least one packet has already
     been written out to the code-stream.
        `ready_area' is the area associated with all precincts in the
     ready list headed by `first_ready'.  The ready precincts have been fully
     generated, but none of their packets have yet been written to the
     code-stream.  Whenever it is done, rate allocation is performed on these
     precincts.
        `expected_area' represents the total remaining area covered by current
     and expected future ready precincts.  A -ve value means that this
     quantity needs to be recomputed, in which case it is obtained from the
     following expression:
             expected_area = (`remaining_area'-T)
                           + T * (`area_used_by_tiles'/`area_covered_by_tiles')
             where  T = `total_area' - `area_covered_by_tiles'
     The computed value is rounded up, if necessary, to the nearest integer.
        `attributed_area' is derived by accumulating the `expected_area'
     values from all lower resolution levels for which `ready_area' is
     currently 0.  The main purpose of this member is to detect changes
     that may occur.
        `ready_fraction' holds a quantity in the range 0 to 1, representing
     the value `ready_area' / (`expected_area' + `attributed_area').
     Similarly, `reciprocal_fraction' holds the ratio
     (`expected_area' + `attributed_area') / `ready_area'.  Both quantities
     are -ve if their values need to be recomputed or if `ready_area' is 0.
  */

  
/*****************************************************************************/
/*                         kd_precinct_flush_info                            */
/*****************************************************************************/

struct kd_precinct_flush_info {
    kd_precinct *precinct;
    kdu_uint16 last_sim_threshold;
    kdu_uint16 last_mod_threshold;
    kdu_uint16 next_slope_val;
  };
  /* This structure is used to keep track of a small amount of information
     for each precinct in a flush-set (see `kd_flush_set').  Normally, flush
     sets are small, so keeping this extra information is not burdensome and
     it can significantly accelerate good PCRD-opt based rate control.
        The `last_sim_threshold' member records the last slope threshold value
     passed to this precinct's `simulate_packet_cbr' function, while
     `last_mod_threshold' and `next_slope_val' record the distortion-length
     slope information returned by that function.  All slope thresholds S
     in the range `last_sim_threshold' <= S <= `last_mod_threshold' will
     produce exactly the same number of simulated bytes, which may allow us
     to avoid unnecessary packet simulations when the threshold S is being
     adjusted only slightly.  If there has been no `simulate_packet_cbr' call,
     both `last_sim_threshold' and `last_mod_threshold' will be 0xFFFF.
        The `next_slope_val' value is the largest distortion-length slope
     threshold associated with any coding pass that was not included in the
     last simulated packet.  Accordingly, `next_slope_val'-1 is the slope
     threshold value that would make the smallest increment in the content
     included in the packet, relative to that included when using
     `last_sim_threshold'.  If `next_slope_val'=0, the `last_sim_threshold'
     value already results in all available coding pass content being
     included within the simulated packet.  If there has been no
     `simulate_packet_cbr' call, the `next_slope_val' member is 0.
  */
  
/*****************************************************************************/
/*                             kd_flush_set                                  */
/*****************************************************************************/
  
struct kd_flush_set {
  public: // Member functions
    void cleanup()
      { if (precincts != NULL) delete[] precincts;
        precincts=NULL; active_tile=NULL; }
      /* `kd_codestream::delete_and_reset_all_but_buffering_and_threading'
         calls this function as a substitute for a destructor. */
    void note_simulated_slope_and_bytes(kdu_uint16 slope, kdu_long sim_bytes)
      { 
        num_flush_sets++;
        acc_slope_threshold += slope;
        acc_sq_slope_threshold += ((kdu_uint32)slope)*((kdu_uint32)slope);
        if (slope > max_slope_threshold) max_slope_threshold = slope;
        if (slope < min_slope_threshold) min_slope_threshold = slope;
        flush_set_bytes += sim_bytes;
        bucket_fulness += flush_set_bytes;
        assert(bucket_fulness <= max_bucket_fulness);
        acc_bucket_fulness += bucket_fulness;
      }
      /* Called right before `kd_codestream::pcrd_opt_cbr' returns, this
         function updates slope threshold statistics, adds `sim_bytes' to
         `flush_set_bytes' (it might already contain some header bytes),
         updates the `bucket_fulness' to reflect the number of bytes the
         simulation tells us we should flush, and also updates any bucket
         fulness statistics we are maintaining. */
  public: // Global information
    kdu_long cs_target_bytes; // For the whole codestream
    kdu_long cs_total_samples; // Total number of codestream component samples
    double data_rate; // Measured in bytes per sample
    double max_bucket_fulness; // Measured in bytes
    double bucket_fulness; // Equals main hdr bytes prior to first flush
    kdu_long cs_flushed_bytes; // Total flushed bytes for the whole codestream
    kdu_long cs_flushed_samples; // Total flushed samples so far
    kdu_long nominal_flush_set_samples; // Same for all but last flush-set
  public: // Information about the current active tile
    kd_tile *active_tile; // Non-NULL only once we start to flush content
    kdu_long total_tile_samples;
    kdu_long planned_tile_bytes; // Intended size for a tile, incl. SOT
    kdu_long flushed_tile_samples; // Sum of all flushed samples for the tile
    kdu_long flushed_tile_bytes; // Incl. SOT marker segment but not EOC marker  
  public: // Members used to build flush sets
    kd_resolution *res_boundary;
    int y_boundary; // Relative precinct y index, not absolute
    int y_boundary_increment;
    kd_resolution *next_res;
    kdu_coords next_rel_idx; // Relative precinct index, not absolute
    kd_precinct_ref *next_pref;
  public: // Information about the current flush set
    bool last_set_in_tile;       // These attributes are set once the
    bool last_set_in_codestream; // flush set has been constructed.
    kdu_long flush_set_samples; // Number of samples in current flush set
    kdu_long flush_set_bytes; // Size produced by `pcrd_opt_cbr'
    kdu_long primary_bulk_fill_bytes; // See below
    kdu_long secondary_bulk_fill_bytes; // See below
    int fine_fill_bytes;    // See below
    int max_precincts; // Max pointers in the `precincts' array.
    int num_precincts; // Num valid entries in the `precincts' array
    int last_non_empty_precinct; // See below
    kd_precinct_flush_info *precincts;
  public: // Statistics used to see how efficient the simulation process is
    int num_flushed_precincts;
    int num_precinct_simulations;
  public: // Statistics to assess the performance of the CBR flushing process
    int num_flush_sets;
    kdu_uint16 min_slope_threshold;
    kdu_uint16 max_slope_threshold;
    kdu_long acc_slope_threshold; // Divide by `num_flush_sets' to get average
    kdu_long acc_sq_slope_threshold; // Divide to get average squared threshold
    double acc_bucket_fulness; // Divide to get avg post-flush bucket level
    kdu_long non_final_fill_bytes; // Accumulates any fill/spacer bytes used in
                              // non-final flush-sets -- the final flush-set
                              // is never filled.
  };
  /* Notes:
        This object is embedded directly within `kd_codestream' so should not
     have an explicit constructor or destructor.  It is used to progressively
     flush content belonging to a single tile in low delay applications, where
     many flushes are expected and it is valuable to avoid opening a new
     tile-part for each incremental flush. 
        The flush set is built from precincts that have already been generated
     according to a prescription that is defined by the `res_boundary',
     `y_boundary' and `y_boundary_increment' members.  The idea is to walk
     through the tile's precincts using the `kd_packet_sequencer's services
     exactly once (no backtracking is required with flush sets).  As each
     precinct is discovered, its resolution and index are recorded in the
     `next_res' and `next_idx' members.  If `next_res'=`boundary_res' and
     `next_idx.y'=`boundary_y', the precinct is considered to belong to the
     next flush set.  If the precinct has not yet been encoding, the flush set
     construction must be deferred until more precincts have been generated.
     Otherwise, the precinct is added to the `precincts' array at the location
     identified by `num_precincts', which is subsequently incremented.
        When we start building the first flush set of a tile, `boundary_res'
     will be NULL.  When this condition is detected, `boundary_res' is set
     equal to `next_res' and `boundary_y' is set to
     `next_idx.y'+`boundary_y_increment', which essentially represents the
     number of lowest resolution precinct rows from the first component that
     belong to each flush set (except possibly the last one).
        While building the flush set, we take note of whether or not each
     precinct is empty, meaning that it has no subband samples.  Such
     situations are uncommon, but possible. An empty precinct presents
     problems when introducing fill/spacer bytes, because it has no
     code-blocks to carry this content, so we record the index of the last
     non-empty precinct in the `last_non_empty_precinct' member.
        After each flush set has been constructed and flushed, the
     `num_precincts' member is reset to 0 and `boundary_y' is incremented by
     `boundary_y_increment' so that the next flush set can be constructed.
     Other members that describe the attributes of the flush set are also
     reset at this point.
        In order to do incremental flushing of a tile, without generating a new
     tile-part for each flush, the number of bytes that will be written to the
     tile must sometimes be known exactly, from the outset.  This value is
     recorded in the `planned_tile_bytes' member.  This number of bytes
     includes the tile's SOT marker segment (there will only be one tile-part
     for each tile, and usually only one tile in the image).  However, it does
     not include the codestream main header.  The first flush set's flushed
     size is considered to include the SOT marker segment that is written at
     that time.
        We consider that flushed data is transmitted to a receiver at a
     constant bit-rate between flushes.  Modelling this process in the usual
     way, via a "leaky bucket", we use D = `data_rate' to express the draining
     rate in terms of bytes per sample, considering image samples to arrive
     at a constant rate Rsample, which is of no consequence to the design of
     this object.  The bucket is initialized to hold all codestream header
     bytes (assuming this is the first tile).  Effectively, we consider all
     transmission associated with a codestream to start from the time at which
     its first flush-set has been generated.  After a flush operation
     dumps its bytes into the leaky bucket, a delay of S samples (S/Rsample
     seconds) is considered to transpire before the next flush occurs, where
     S is the `flush_set_samples'.
        Accordingly, F=S*D bytes are drained from the leaky bucket between the
     end of one flush (the one which involved S samples) and the next one,
     if any.  Accordingly, F bytes are deducted from the `bucket_fulness'
     value immediately after the flush occurs.
        This model is completely valid so long as all flush sets have
     the same size, except possibly the last one which can be smaller.  It
     is up to the application to set up the precinct dimensions and tile
     boundaries (if there are multiple tiles) to ensure that this is the case.
        A flush set cannot fill the bucket beyond Bmax = `max_bucket_fulness'.
     Also, since F = D*S = `data_bytes'*`flush_set_samples' bytes drain from
     the bucket before the next flush occurs, it is essential that flushing
     leaves `bucket_fulness' no smaller than F.  We may think of F, then, as
     Bmin.
        The `planned_tile_bytes' member records the total number of bytes
     we expect to write to the current tile, including its SOT marker segment,
     but not including the codestream main header or the final EOC marker.
     As explained below, in some cases we must write this number of bytes
     exactly, but in most cases we only need to ensure that the final
     tile writes no more than this number of bytes, since spacer bytes can
     be introduced between codestreams.
        Special considerations apply to the final flush set in a codestream
     and to the final flush set in a tile that has multiple flush sets.
     The final flush set in the codestream needs to be sized so that the
     bucket drains completely.  This means that its flush must not take
     `bucket_fulness' beyond Bmin bytes.  Unlike the other flush sets, which
     must take `bucket_fulness' to at least Bmin bytes, the final one in the
     codestream can generate less bytes, because spacer bytes can be inserted
     between codestreams to avoid buffer underflow in video applications.
     The final flush set in the codestream must also take the 2-byte EOC marker
     into account, considering it to be part of the flushed data, even though
     it is not part of the tile data.
        If there is only one tile, so that only one SOT marker segment is
     written, the tile-part length field can be 0, so it is not necessary for
     the total number of packet bytes flushed for the tile to equal
     `planned_tile_bytes' exactly.  This is the most desirable situation,
     since it usually relegates the use of spacer bytes to the gap between
     codestreams, unless `max_buffer_fulness' heavily constrains the number
     of bytes that can be flushed for any given set.
        If there are multiple tiles in the codestream, it is recommended that
     each tile consist of exactly one flush set, so there is no problem writing
     the actual tile-part length into the SOT marker segment.  If this is not
     true, it makes no sense at all to have multiple tiles across the image.
        In all other cases, `planned_tile_bytes' is used to determine and
     write the tile-part size field of the SOT marker segment ahead of time,
     and the final flush set of the tile must be prepared to introduce spacer
     bytes within the packet data itself, in order to achieve the number of
     planned tile bytes exactly.  This can be done in various ways.
     Fill bytes can be appended as FF7F pairs to the end of any regular
     JPEG2000 code-block bit-stream.  If the FAST block coding algorithm has
     been used, it is usually possible to introduce any number of fill bytes,
     except for isolated bytes.  Fill bytes that are appended to a forward
     growing FAST raw byte-stream, as used in the FAST SigProp and MagRef
     coding passes or the first internal byte-stream of the tripple-stream
     variant of the FAST Cleanup pass, should include at least one byte pair
     in the range FF80 through FF8F, and should also include such a byte pair
     within the last 3 filled bytes, so as to facilitate efficient decoding
     and simple mechanisms for discovering and removing filled bytes if
     desired.  Fill bytes introduced between forward and reverse growing
     byte streams of a FAST Cleanup pass can have any number of bytes at
     all, but they are hard to remove later.
        The number of fill bytes to be added to a flush set is determined
     during simulation, by the `pcrd_opt_cbr' function, and recorded using the
     `primary_bulk_fill_bytes', `secondary_bulk_fill_bytes' and
     `fine_fill_bytes' members.  The reason for splitting the fill information
     into three different types is that it simplifies the simulation process.
     All precincts other than the `last_non_empty_precinct' are assigned
     fill bytes in multiples of 8192, which are distributed to all their
     code-blocks, starting from the first code-block of the first precinct
     and continuing in linear fashion.  The total number of these fill bytes
     is stored in the `primary_bulk_fill_bytes' member.  The last non-empty
     precinct is assigned the remaining `secondary_bulk_fill_bytes' and
     `fine_fill_bytes', where the latter refers to bytes which are added to
     the last code-block in the precinct only, while the former refers to
     bytes that are added to the other code-blocks in the precinct, in order
     of appearance, 8192 bytes at a time.  In this way, at most two
     code-blocks of the last non-empty precinct receive a number of fill
     bytes which is not a multiple of 8192 -- the last one that receives any
     of the `secondary_bulk_fill_bytes' and the one that receives the
     `fine_fill_bytes'.  The packet simulation and writing functions know
     how to assign bulk and fine fill bytes to a precinct's code-blocks.
     The `secondary_bulk_fill_bytes' value is always a multiple of 2, while
     `fine_fill_bytes' might not be.
        It is worth finishing with a discussion of how `data_rate' and
     `planned_tile_bytes' are determined.  Let T be the total number of samples
     in a codestream, being the value of the `cs_total_samples' member, and
     let L = `cs_target_bytes' be the total planned size for the codestream.
     As mentioned, the generated size can be smaller than L, since spacer bytes
     can be introduced between codestreams.  The `data_rate' value is simply
          D = L/T,
     while the planned size for each tile is
          `planned_tile_bytes' = `data_rate'*`total_tile_samples'*(L-HDR)/L
                            = (L-HDR)*D/L*`total_tile_samples'
                            = (L-HDR) * `total_tile_samples'/`cs_total_samples'
     where HDR is the number of bytes in the main codestream header, plus the
     final EOC marker.  These planned sizes might not be integer valued, but
     care must be taken to ensure that the actual (rounded) planned tile sizes
     add to L-HDR.  To facilitate this, we keep track of the total number of
     bytes flushed to the codestream and the total number of samples associated
     with these bytes, via the `cs_flushed_bytes' and `cs_flushed_samples'
     members.  These start out equal to HDR-2 and 0, respectively, right
     after the codestream header has been written.
  */

/*****************************************************************************/
/*                              kd_nlt_info                                  */
/*****************************************************************************/
  
struct kd_nlt_info {
  public: // Member functions
    kd_nlt_info()
      { // Objects start life with a reference count of 1
        memset(this,0,sizeof(*this));
        ref_count = 1;
      }
    ~kd_nlt_info()
      { 
        assert(ref_count == 0);
        if (lut != NULL) delete[] lut;
      }
    static kd_nlt_info *create(kdu_params *params, int t_num, int c_num);
      /* If `params' is non-NULL and `params->access_unique' returns non-NULL
         when invoked with `t_num' and `c_num' as parameters, and the
         unique object retrieved in this way identifies the existence of
         an `NLType' attribute, this function allocates, initializes and
         returns a `kd_nlt_info' object that holds the same information.  It
         is expected that a non-NULL `params' object is the cluster root for
         the `NLT_params' cluster.  If `params' is NULL or
         `params->access_unique' returns false, this function simply
         returns NULL. */
    kd_nlt_info *add_ref()
      { // Use this function to install a reference (ptr) to an existing
        // object that is used elsewhere.
        assert(ref_count > 0);
        ref_count++;
        return this;
      }
    bool release() // POTENTIALLY self-efacing!
      { // Decreases the reference count.  If the count reaches 0, the
        // object deletes itself and returns true; else false is returned.
        if ((--ref_count) == 0)
          { delete this; return true; }
        return false;
      }
  public: // Data
    int ref_count;
    int t_num; // -1 for global information (not tile-specific)
    int c_num; // -1 for component-wide information (not component-specific)
    int nlt_type; // NLType_NONE, NLType_GAMMA, NLType_LUT or NLType_SMAG
    float gamma[5]; // Holds E, S, T, A and B parameters for the GAMMA type
    float dmin, dmax; // Always 0.0 and 1.0 for non-LUT types
    int num_lut_entries; // non-zero only for the LUT type
    float *lut; // non-NULL only for the LUT type
  };
  /* Notes:
        This structure is used to record all required information concerning
     a single non-linear transform, as identified by NLT markers segments
     within the codestream.
        Pointers to `kd_nlt_info' objects may appear within the
     `kd_codestream::output_comp_info' array, within the
     `kd_mct_stage::output_comp_info' array of the final MCT stage in any
     given `kd_tile', or within `kd_tile_comp' objects.  Specifically, NLT
     marker segments found in the codestream main header produce non-NULL
     `nlt_info' pointers in the `kd_codestream::output_comp_info', while
     each tile carries the applicable `nlt_info' pointers in its `kd_comp_info'
     structures if it has no Part-2 multi-component transform, else in the
     `kd_tile::output_comp_info' array of the `kd_tile::mct_tail' object.
        To keep things efficient, we allow individual objects to be referenced
     from multiple components and tiles, rather than replicating default NLT
     descriptors in all the entities that they affect.  To facilitate this, we
     keep the number of references in the `ref_count' member, deleting the
     actual object only once all references to go zero.  In most cases,
     tile-component references point to common objects derived from the main
     header, and those might also point to one common global NLT object.
  */
  
/*****************************************************************************/
/*                              kd_comp_info                                 */
/*****************************************************************************/

struct kd_comp_info {
  public: // Utility functions
    kdu_dims scale_region(const kdu_dims &region, int depth);
      /* This function converts a `region' expressed on the codestream's
         high resolution canvas coordinate system into one that is expressed
         on the component-resolution that lies at the indicated `depth' into
         the DWT hierarchy, where `depth' should lie in the range 0 to 1. */
  public: // Data members
    kdu_coords sub_sampling; // Component sub-sampling factors.
    float crg_x, crg_y; // Component registration offsets.
    int precision; // Original component bit-depths.
    bool is_signed; // Signed/unsigned nature of original components
    kdu_byte hor_depth[33];
    kdu_byte vert_depth[33];
    int apparent_idx; // See below
    kd_comp_info *from_apparent; // See below
  public: // Members used only when working with `kdu_quality_limiter'
    float qlim_sq_weight;
    kdu_int32 qlim_type_flags;
  };
  /* Notes:
       This structure holds codestream-wide information for a single image
       component.
          The `hor_depth' and `vert_depth' arrays contain information which
       might be affected by DFS marker segments in the codestream's main
       header.  The values of `hor_depth'[d] and `vert_depth'[d] identify
       the total number of horizontal and vertical low-pass filtering and
       downsampling stages which are involved in creating the resolution
       level which is `d' levels below the original full image resolution.
       Neither value may exceed d.  Thus, `hor_depth'[0] and `vert_depth'[0]
       are guaranteed to equal zero -- these entries exist only to make the
       implementation more regular.  Codestreams conforming to JPEG2000
       Part-1 must have `hor_depth'[d] = `vert_depth'[d] = d for all d.
       Part-2 codestreams, however, may have custom downsampling factor
       styles (represented by DFS marker segments in the main header).
          The `apparent_idx' and `from_apparent' members are provided
       because the `kdu_codestream' interface (and its descendants) might
       be limited to exposing only a subset of the actual image components
       which are available.  This subset is selected via
       `kdu_codestream::apply_input_restrictions'.  The `apparent_idx'
       member identifies the index (starting from 0) of the present
       codestream image component, as it appears to the external API.  This
       value will be 0 if the component is not visible across the API.  The
       `from_apparent' member is used to translate apparent component
       indices into true codestream component indices.  Specifically, the
       `kd_codestream::comp_info[c].from_apparent' member points to the
       particular entry in the `kd_codestream::comp_info' array which
       represents the true codestream image component whose apparent index is
       c.  This is NULL if c >= `kd_codestream::num_apparent_components'. */

/*****************************************************************************/
/*                           kd_output_comp_info                             */
/*****************************************************************************/

struct kd_output_comp_info {
  public: // Member functions
    kd_output_comp_info()
      { 
        in_precision=out_precision=0; in_signed=out_signed=false;
        crg_incompatible=false; subsampling_ref = NULL;  nlt_info=NULL;
        apparent_idx=-1; from_apparent=0; block=NULL; block_comp_idx=0;
        apparent_block_comp_idx=0; is_of_interest=false; ss_tmp=0.0F;
        qlim_sq_weight=1.0f; qlim_type_flags=0;
      }
    ~kd_output_comp_info()
      { 
        if (nlt_info != NULL)
          nlt_info->release();
      }
  public: // Data members
    int in_precision, out_precision;  // The out_ and in_ parameters differ
    bool in_signed, out_signed; // only in there is a non-linear point xform
    bool crg_incompatible; // If the block contributions have inconsistent CRG
    kd_comp_info *subsampling_ref;
    kd_nlt_info *nlt_info; // Non-NULL if component has non-linear point xform
    int apparent_idx;
    int from_apparent;
    kd_mct_block *block;
    int block_comp_idx;
    int apparent_block_comp_idx;
    bool is_of_interest; // See below
    float ss_tmp; // Temporary storage used in sensitivity calculations;
                  // see `kd_mct_block::analyze_sensitivity'.
  public: // Members used when working with `kdu_quality_limiter'
    float qlim_sq_weight;
    kdu_int32 qlim_type_flags;
  };
  /* Notes:
        This structure is used to describe the image components produced at
     the output of any given MCT transform (synthesis) stage.  It is
     also used to describe the image components at the output of the
     multi-component transform from a global perspective.  The `in_signed',
     `out_signed', `in_precision' and `out_precision' members are meaningful
     only where this record is found at the output of the last MCT stage in
     the inverse multi-component transform, or in the global `output_comp_info'
     array managed by `kd_codestream'.  In the former case, both the in_ and
     out_ values will be identical. In the latter case, the `in_precision' and
     `in_signed' values may differ from `out_precision' and `out_signed' only
     if there is a non-linear point transform (NLT) involved in the generation
     of final content.  In this case, `in_precision'/`in_signed' identify
     the properties of the component sample values that appear at the output
     of a multi-component transform during decompression -- if there is no
     multi-component transform, they correspond to the precision/signed
     properties of the codestream component that is produced by inverse
     wavelet transformation or inverse Part-1 decorrelating transform.
     Meanwhile `out_precision'/`out_signed' identify the properties of the
     samples produced at the output of the non-linear point transform
     during decompression.
        If the inverse multi-component transform does not produce any data for
     this output component, its `subsampling_ref' member is NULL.  Otherwise
     `subsampling_ref' points to the `kd_comp_info' structure which describes
     any of the codestream image components which is involved in
     reconstructing this output component -- it does not matter which one.
     The purpose of this reference is to facilitate the determination of the
     dimensions and sub-sampling attributes of the relevant output component.
     All codestream image components involved in the reconstruction of
     any given MCT output component must have identical dimensions, so it
     is sufficient to reference only one of them.
        The `apparent_idx' and `from_apparent' members work in exactly the
     same way as their namesakes in `kd_comp_info' to facilitate translation
     between true image component indices and apparent image component
     indices, as presented to the application.  The only difference is
     that `from_apparent' is the index (rather than the address) of the
     true output image component which appears to have the index of the
     current component.  The apparent component indices are influenced by
     calls to the second form of the `kdu_codestream::change_appearance'
     function.
        The last four members are used only when the object is found in
     the `output_comp_info' array of a `kd_mct_stage' object.  In this case,
     `block' points to the MCT transform block which is used to produce this
     component within the relevant stage, while `block_comp_idx' indicates
     which of the output components from that transform stage corresponds
     to the current component.  The `apparent_block_comp_idx' member
     identifies the apparent index of this output component within the
     referenced transform block, which may be smaller than `block_comp_idx'
     if some earlier output components from the transform block are not
     apparent.  This value is guaranteed to lie between 0 and
     `block->num_apparent_outputs'-1.  `is_of_interest' is set to false
     if and only if `apparent_idx' is negative, except in the final
     transform stage, where it is also set to false if the corresponding
     component is not of interest to the application, as identified by
     calls to `kdu_tile::set_components_of_interest'.
        If the object is found in `kd_codestream::output_comp_info',
     the `block' member will be NULL and `block_comp_idx',
     `apparent_block_idx' and `is_of_interest' have no meaning. */
  
/*****************************************************************************/
/*                            kd_mct_ss_model                                */
/*****************************************************************************/

struct kd_mct_ss_model {
  public: // Member functions
    kd_mct_ss_model()  { ss_vals = ss_handle = NULL; }
    ~kd_mct_ss_model() { if (ss_handle != NULL) delete[] ss_handle; }
  public: // Data
    kdu_int16 range_min;
    kdu_int16 range_len;
    float *ss_vals;
    float *ss_handle;
  };
  /* See the discussion of `kd_mct_block::ss_models' for an explanation of
     this structure. */

/*****************************************************************************/
/*                              kd_mct_block                                 */
/*****************************************************************************/

struct kd_mct_block {
  public: // Member functions
    kd_mct_block() { memset(this,0,sizeof(*this)); luma_input_idx=-1; }
    ~kd_mct_block()
      {
        if (input_indices != NULL) delete[] input_indices;
        if (inputs_required != NULL) delete[] inputs_required;
        if (output_indices != NULL) delete[] output_indices;
        if (dwt_step_info != NULL) delete[] dwt_step_info;
        if (dwt_coefficients != NULL) delete[] dwt_coefficients;
        if (scratch != NULL) delete[] scratch;
        if (ss_models != NULL) delete[] ss_models;
      }
    void analyze_sensitivity(int which_input, float input_weight,
                             int &min_output_idx, int &max_output_idx,
                             bool restrict_to_interest);
      /* This function plays a central role in the calculation of energy
         weights.  It determines the contribution of the block input
         component identified by `which_input' to the set of stage output
         components for the MCT stage in which the block resides,
         multiplying the contribution by `input_weight' and adding it into
         the `kd_output_comp_info::ss_tmp' members of all affected stage
         outputs.  The `which_input' argument must lie in the range 0 to
         `num_inputs'-1, where `num_inputs' is the number of inputs for
         this block.
            The `min_output_idx' and `max_output_idx' arguments
         play a key role in minimizing the complexity of sensitivity
         calculations for multi-component transforms which involve large
         numbers of image components.  On entry, these identify the range
         of stage output components whose `kd_output_comp_info::ss_tmp'
         members have already been affected by sensitivity calculations
         for other transform blocks in the same stage; if this is the first
         stage, the `min_output_idx' value supplied on entry should be
         strictly greater than the `max_output_idx' value, to indicate that
         the range of affected stage outputs is empty so far.  On exit,
         the `min_output_idx' and `max_output_idx' arguments represent the
         augmented range of affected outputs.  Whenever this range grows,
         the function first initializes the affected `ss_tmp' members to 0,
         so there is no need to explicitly initialize all `ss_tmp' members
         in the `kd_mct_stage::output_comp_info' array.  This can also save
         quite a bit of wasted effort when there are a very large number
         of components.
            If `restrict_to_interest' is true, the sensitivity analysis is
         restricted to include only those output components for which
         `kd_output_comp_info::is_of_interest' is true. */
  private: // Helper functions
    void create_matrix_ss_model();
    void create_rxform_ss_model();
    void create_old_rxform_ss_model();
    void create_dependency_ss_model();
    void create_dwt_ss_model();
  public: // Data
    kd_mct_stage *stage;
    int num_inputs;
    int num_required_inputs;
    int *input_indices;
    bool *inputs_required;
    int num_outputs;
    int num_apparent_outputs;
    int *output_indices;
    int luma_input_idx; // See below
    int apparent_input_types; // See below
    float crg_x, crg_y; // Common registration offset for all inputs
    kd_mct_ss_model *ss_models;
    bool is_reversible;
    bool is_null_transform;
    bool is_diag_transform;
    bool crg_incompatible; // If inputs have incompatible crg_x/crg_y
    kdu_params *offset_params;
    kdu_params *matrix_params;
    kdu_params *old_mat_params; // For compatibility with versions before v6.0
    kdu_params *triang_params;
    int dwt_num_steps, dwt_num_levels, dwt_canvas_origin;
    bool dwt_symmetric, dwt_symmetric_extension;
    kdu_kernel_step_info *dwt_step_info;
    float *dwt_coefficients;
    int dwt_low_synth_min, dwt_low_synth_max;
    int dwt_high_synth_min, dwt_high_synth_max;
    float dwt_synth_gains[2]; // Subband gains applied before synthesis lifting
    float *scratch; // Contains `num_inputs' entries if allocated
  };
  /* Notes:
        This structure manages a single transform block within a stage of the
     multi-component transform.  Most types of transform blocks have an
     identical number of input and output components, but irreversible
     decorrelation transforms can have more or less output components than
     input components.
        The `input_indices' array provides the indices of the stage input
     components which are used by this block.  These must lie in the range
     0 to `stage->num_input_components'-1.  Moreover, if this is not the
     first stage, these same indices may be used to address the
     `stage->prev_stage->output_comp_info' array, to discover how the input
     components are produced.
        The output_indices' array provides the indices of the stage output
     components which are produced by this block.  These indices may be
     used to address the `stage->output_comp_info' array to discover whether
     each output component is apparent or not, amongst other things.
        The `luma_input_idx' member provides an indication of whether
     this transform block appears to be of luma-chroma type, meaning that
     it has three input components and three output components, where
     one synthesis vector is a non-zero constant vector -- a scalar multiple of
     (1,1,1)^t.  If one synthesis is of this form then the other analysis
     vectors must have zero mean, meaning that they are insensitive to
     "luminance".  If this condition is found, the constant synthesis vector
     is interpreted as a luminance channel and its index is recorded here.
     Otherwise, the `luma_input_idx' member is set to -1.  The presence or
     absence of a non-negative `luma_input_idx' member does not directly
     determine whether a transform block takes chroma-type inputs.  This
     information needs to be traced through the MCT network by the
     `kd_mct_stage::apply_output_restrictions' function, which sets the
     `apparent_input_types' member.
        The `apparent_input_types' member is configured by
     `kd_mct_stage::apply_output_restrictions' when there is a
     `kdu_quality_limiter' object that has configured the `qlim_type_flags'
     members of the apparent codestream's `output_comp_info' entries.
     * If `is_null_transform' is true, `apparent_input_types' is not used
     and no component types are passed from the transform block's outputs to
     its inputs.
     * Else if `is_diag_transform' is true, `apparent_input_types' is not used
     and the component type of each input component to the block is
     transcribed directly from the corresponding output type found in
     the `qlim_type_flags' member of the relevant stage `kd_output_comp_info'
     entry.
     * Otherwise, `apparent_input_types' is formed by first taking the union
     (logical OR) of the type flags of all of the block's outputs and
     then making the following modifications if `luma_input_idx' >= 0:
       -- If all 3 LSB's of the unioned type flags word are set and neither
          the chroma flag (sign bit) nor a "chroma-blocking" flag (bit-30) are
          set, the `apparent_input_types' word is converted to -1, which
          is interpreted as meaning that input component `luma_input_idx'
          is a luma component (it will be passed on with type code 2^30) and
          the other components are of chroma type (they will be passed on with
          type code 2^31).
       -- Else, if exactly one of bits 0, 1, 2 or 31 is set, the
          `apparent_input_types' word is left unchanged, to be passed along
          as the type code for each of the block's inputs.
       -- Otherwise, the block appears to represent an opponent transform,
          whose outputs are only partially consumed, which means that it
          is dangerous to introduce chroma-based visual weighting to any
          of its inputs, so we convert `apparent_input_types' to 2^30,
          the "chroma-blocking" flag.
     It is then a simple matter to insect a block and determine the type
     codes for each of its inputs: if `is_diag_transform' is false, we
     simply pass along `apparent_input_types' to each of the block's inputs,
     except where `luma_input_idx' >= 0 and `apparent_input_types' = -1,
     in which case we pass along 2^30 or 2^31, depending on whether or not
     the input index corresponds to `luma_input_idx'.  When the type code
     is passed to a codestream component, the component is deemed to be
     of chroma-type if and only if its type code is 2^31 (sign-bit only).
        The `ss_models' array is created and manipulated by calls to the
     `analyze_sensitivity' function.  If non-NULL, the array contains
     `num_inputs' entries, each of which identifies the linearized
     contribution of a unit change in the corresponding input to the
     block's output components.  For reasons of efficiency, each input
     identifies the range of block output indices which it affects, via the
     corresponding `kd_mct_ss_model::range_min' and
     `kd_mct_ss_model::range_len' members.  The contribution of each input
     to the affected block outputs is given by its `kd_mct_ss_model::ss_vals'
     array, with length `kd_mct_ss_model::range_len'.  This array is created
     (set to a non-NULL value) only on demand.  Note also that the
     `kd_mct_ss_model::ss_vals' arrays can be allocated in a variety of
     ways and can be shared between different input models, wherever this
     allows for improved efficiency.  This is achieved by separately
     keeping track of actual allocated memory blocks via the
     `kd_mct_ss_model::ss_handle' members.
        Each transform block notionally has an offset vector, whose
     elements are to be added to the output component samples as the final
     step in the transform process.  If `offset_params' is non-NULL, the
     offsets are derived from its `Mvector' attribute.  Otherwise, the
     offsets are all 0.
        If `matrix_params', `triang_params' and `dwt_step_info' are all NULL,
     the transform block does nothing else.  We refer to blocks of this form
     as "NULL blocks".  NULL blocks map their i'th input component to their
     i'th output component, with the possible addition of an offset from
     `offset_params'.  NULL blocks are useful primarily as a tool for
     discarding certain input components, since only the first
     `num_outputs' components find their way through the block.  They are
     also useful for introducing permutations and offsets.  The JPEG2000
     standard, Part 2, refers to NULL transform blocks in this same sense,
     although the text in the standard is potentially unclear.
        For NULL transform blocks, the `is_reversible' flag will be false.
     For all other types of transform blocks, the `is_reversible' flag plays
     an important role in determining how the transformation is implemented.
        Apart from NULL transform blocks, there are four other types of
     transforms:
     [>>] Irreversible decorrelation transforms are described by a matrix,
          whose coefficients are provided by the `Mmatrix' attribute
          managed by `matrix_params'.  This type of transform is used if
          `matrix_params' is non-NULL and `is_reversible' is false.
     [>>] Reversible decorrelation transforms are described by a matrix
          of SERM factorization coefficients.  In this case, `is_reversible'
          is true and the value of `num_outputs' must equal `num_inputs'.
          Codestreams generated prior to v6.0 will have `old_mat_xforms'
          non-NULL, in which case the corresponding matrix coefficients
          have an organization with `num_outputs' rows and `num_outputs'+1
          columns, where the columns represent successive lifting steps.
          Codestreams generated from v6.0 onwards have `matrix_xforms'
          non-NULL, in which case the matrix coefficients have an organization
          with `num_outputs'+1 rows and `num_outputs' columns, where the
          rows represent successive lifting steps.  This matrix organization
          is transposed in answering calls to `kdu_tile::get_mct_rxform_info'.
     [>>] Reversible or irreversible dependency transform.  In this case,
          the `triang_params' member is non-NULL and its `Mtriang'
          attribute is used to access the transform coefficients.
     [>>] Reversible or irreversible DWT (Discrete Wavelet Transform).  In
          this case, `dwt_step_info' is non-NULL.  The DWT transform kernel
          is described by the `dwt_step_info', `dwt_num_steps',
          `dwt_symmetric', `dwt_symmetric_extension' and `dwt_coefficients'
          members.  The number of DWT levels to be applied is described by
          the `dwt_num_levels' member, and the canvas origin which defines
          the coordinate system for the DWT is given by `dwt_canvas_origin'.
          The regions of support occupied by each of the synthesis filters
          are identified by the `dwt_low_synth_min' through
          `dwt_high_synth_max' members.
     
        All transform coefficients and offsets are represented using the
     normalization conventions of the JPEG2000 marker segments from which
     they are derived.  This convention means that the supplied coefficients
     would produce the correct output image component samples, at their
     declared bit-depths, if applied to codestream image components which
     are also expressed at their declared bit-depths. */

/*****************************************************************************/
/*                              kd_mct_stage                                 */
/*****************************************************************************/

struct kd_mct_stage {
  public : // Static member function
    static void
      create_stages(kd_mct_stage * &result_head,
                    kd_mct_stage * &result_tail,
                    kdu_params *params_root, int tile_num,
                    int num_codestream_components,
                    kd_comp_info *codestream_component_info,
                    int num_output_components,
                    kd_output_comp_info *output_component_info,
                    bool is_output_codestream);
      /* This function is used to create a linked list of `kd_mct_stage'
         objects, which describe the inverse multi-component transform for
         a particular tile, identified by the `tile_num' argument.  The list
         of MCT stages is created from the descriptions found in the
         coding parameter sub-system, whose root is given by `params_root'.
         The remaining arguments describe the input and output image
         components associated with the transform.  The contents of these
         arrays are not modified, but they are used to contribute to the
         initialization of the stage contents.  Note that the
         `kd_output_comp_info::ref_subsampling' member of the
         `output_component_info' array is ignored by this function.  The
         caller may later use the corresponding member of the internal
         `output_comp_info' member array to check for dimensional
         consistency with the corresponding global value.
            The head and tail of the created list are both returned via
         `result_head' and `result_tail'.  A number of conditions could
         cause this function to fail through `kdu_error'.  In many
         applications this causes an exception to be thrown, after
         which any partially created list of transform stages should be
         cleaned up.  The easiest way to do this is for the function to
         grow the list inside an object which will persist beyond any
         such exception -- in practice, this is the `kd_codestream' or
         `kd_tile' object, both of which maintain head/tail member
         variables for MCT stage lists.
            If `tile_num' is negative, this function is being used to create
         the `global_mct_head' & `global_mct_tail' members of
         `kd_codestream' based only on information in the main codestream
         header.  Strictly speaking, it is possible that insufficient
         marker segments might be found in the main header to do this
         successfully in a legal Part 2 codestream.  However, without
         this information it is not possible to know the dimensions of
         the output components ahead of time, which is an accidental oversight
         by the committee which defined JPEG2000 Part2.  For this reason,
         we shall treat such codestreams as illegal, but we shall not
         insist that any of the transform coefficients (linear coefficients
         or offsets) be available from the main codestream header; only
         the structure is required (MCO and MCT marker segments).  In
         particular, this means that the present function leaves all but
         the first 6 member fields of each `kd_mct_block' equal to 0
         (i.e., as if each transform block was a null transform) when
         invoked with a negative `tile_num' argument.
            The caller passes true for `is_output_codestream' if the
         `kdu_codestream' object was created to generate codestream content.
         In this case, we do not perform the check for old-style MATRIX
         transformations, converting them to MAT -- an operation designed
         to support legacy codestreams that might have been created
         erroneously, but which can produce wrong results if the
         application has not yet invoked `kdu_params::finalize_all' by the
         time this function is called. */
  public: // Member functions
    kd_mct_stage() { memset(this,0,sizeof(*this)); }
    ~kd_mct_stage()
      {
        if (input_required_indices != NULL)
          delete[] input_required_indices;
        if (output_comp_info != NULL) delete[] output_comp_info;
        if (blocks != NULL) delete[] blocks;
      }
    void apply_output_restrictions(kd_output_comp_info *output_comp_info,
                                   int num_components_of_interest=0,
                                   const int *components_of_interest=NULL,
                                   bool find_type_flags=false);
      /* This function should be directly invoked on the last stage in a
         list of `kd_mct_stage' objects (i.e., on the object whose
         `next_stage' member is NULL).  It converts the restrictions on
         the set of required output components, as identified by the
         `kd_output_comp_info::apparent_idx' values in the supplied
         `output_comp_info' argument (this should be the global
         `kd_codestream::output_comp_info' array), into a set of required
         input components for each stage.  Specifically, it starts
         by reflecting the output component requirements to the final stage
         in the list.  It then works backward through the stages until it
         arrives at the first stage, at which point the set of required
         codestream components is known.  In each stage, the function sets
         the `input_required_indices' array entries, the `apparent_idx',
         `from_apparent' and `apparent_block_comp_idx' members of each
         `output_comp_info' array, the `kd_mct_stage::num_apparent_outputs'
         and the `kd_mct_block::num_apparent_outputs' values.
            The function always transcribes `qlim_type_flags' and
         `qlim_sq_weight' members from `output_comp_info' to the internal
         `output_comp_info' array if this is the last stage of the
         multi-component pipeline.
            If `find_type_flags' is true, the function also fills in the
         `apparent_input_types' member of each transform block, along with
         the `qlim_type_flags' member of each stage's `output_comp_info' array,
         by propagating `qlim_type_flags' information it finds in the supplied
         `output_comp_info' array backwards through the multi-component
         transform pipeline.  The rules used to do this are explained with
         the description of `kd_mct_block::apparent_input_types' that appears
         in the notes following the definition of `kd_mct_block'.
            The `num_components' of interest and `components_of_interest'
         arguments have the same interpretation as their namesakes in the
         `kdu_tile::set_components_of_interest' function. */
  public: // Data
    int num_inputs;
    int num_required_inputs;
    int *input_required_indices;
    int num_outputs;
    int num_apparent_outputs;
    kd_output_comp_info *output_comp_info;
    int num_blocks;
    kd_mct_block *blocks;
    kd_mct_stage *prev_stage; // Closer to the codestream image components
    kd_mct_stage *next_stage; // Closer to the output image components
  };
  /* Notes:
       Each stage of the multi-component transform has a well-defined
     contiguous set of input and output components, even if not all of the
     components in this range are fully used or defined.  The stages appear
     in the order required for inverse transformation (synthesis) during
     decompression.  For this reason, the first `num_inputs'
     member for the first MCT stage is guaranteed to be equal to the
     number of codestream image components.
        In order to satisfy the constraints described by IS 15444-2,
     the `num_inputs' value of any one stage must not be less
     than the `num_outputs' value of the previous stage.  Also,
     every one of the `num_inputs' must be "touched" by one or
     another MCT block.  However, not all of the `num_outputs'
     need to be explicitly generated by any MCT block.  Those which are
     not are known as NULL components -- they are defined to be identically
     equal to 0.
        Once the list of stages has been created, the `num_outputs' value
     from one stage is augmented (if necessary) to the `num_inputs' value
     of the next stage.  This is done only to simplify the way in which
     stages are manipulated.
        The `input_required_indices' array contains one entry for each
     of the input components.  The k'th entry is non-negative if and only
     if input component k is "required", meaning that it contributes to at
     least one of the output components which are apparent and of interest
     to the application, as identified by the
     `kd_output_comp_info::is_of_interest' values in the
     `output_comp_info' array.  Moreover, non-negative indices in the
     `input_required_indices' array identify the ordinal position
     occupied by the corresponding component, in the set of all required
     components.  For non-initial stages, this ordinal position is the same
     as the apparent index of the previous stage's output component.
        It may not always be easy (or expedient) to determine
     precisely whether an input component actually contributes to the
     output produced by a transform block, so it is possible that the
     set of input components which are marked as "required" contains
     some components which could potentially be ignored.  Nevertheless,
     reasonable efforts are made to limit the set of required input
     components, based on the set of required output components.
        The `num_required_inputs' member holds the total number of
     non-negative entries in the `input_required_indices' array.
        The `prev_stage' member is NULL if this is the first stage in
     the inverse (synthesis) multi-component transform, meaning that the
     stage's input components are the codestream image components.
        The `next_stage' member is NULL if this is the last stage in the
     inverse (synthesis) multi-component transform, meaning that the stage's
     output components are the final output image components produced by
     the decompression process. */

/*****************************************************************************/
/*                          kd_cs_background_job                             */
/*****************************************************************************/

class kd_cs_background_job : public kdu_thread_job {
  public: // Member functions
    void init(kd_cs_thread_context *owner, int which_job_type)
      { /* `which_job_type' takes the following possible values:
           0: invokes `owner->do_glock_processing' -- at most one job of this
              type may be scheduled/running at any given time.
           1: invokes `owner->do_plock_processing' -- at most one job of this
              type may be scheduled/running at any given time. */
        this->context = owner;
        this->job_type = which_job_type;
        set_job_func((kdu_thread_job_func) process);
      }
  private: // Helper functions
    static void process(kd_cs_background_job *job, kdu_thread_env *caller);
  private: // Data
    kd_cs_thread_context *context;
    int job_type; // See `init'
  };

/*****************************************************************************/
/*                          kd_cs_thread_context                             */
/*****************************************************************************/

#define KD_BKGND_FLAG_GLOCK_ACTIVE     ((kdu_int32) 0x00001)
#define KD_BKGND_FLAG_PLOCK_ACTIVE     ((kdu_int32) 0x00002)

#define KD_BKGND_FLAG_FLUSH            ((kdu_int32) 0x00010)
#define KD_BKGND_FLAG_TRIM             ((kdu_int32) 0x00020)
#define KD_BKGND_FLAG_RES_PARSE        ((kdu_int32) 0x00040)
#define KD_BKGND_FLAG_RES_PALLOC       ((kdu_int32) 0x00080)
#define KD_BKGND_FLAG_TILE_OPENCLOSE   ((kdu_int32) 0x00100)

#define KD_BKGND_FLAG_TERMINATE        ((kdu_int32) 0x01000)
#define KD_BKGND_FLAG_ALL_DONE         ((kdu_int32) 0x02000)

#define KD_BKGND_FLAG_GLOCK_PROCESSING ((kdu_int32) 0x10000)
#define KD_BKGND_FLAG_PLOCK_PROCESSING ((kdu_int32) 0x20000)
#define KD_BKGND_FLAG_CLEAR_WAITING    ((kdu_int32) 0x40000)

#define KD_BKGND_ACTIVE_MASK \
  (KD_BKGND_FLAG_GLOCK_ACTIVE | KD_BKGND_FLAG_PLOCK_ACTIVE)
#define KD_BKGND_PROCESSING_MASK \
  (KD_BKGND_FLAG_GLOCK_PROCESSING | KD_BKGND_FLAG_PLOCK_PROCESSING) 
#define KD_BKGND_GLOCK_SERVICES \
  (KD_BKGND_FLAG_FLUSH | KD_BKGND_FLAG_TRIM | KD_BKGND_FLAG_RES_PARSE | \
   KD_BKGND_FLAG_TILE_OPENCLOSE)
#define KD_BKGND_PLOCK_SERVICES \
  (KD_BKGND_FLAG_RES_PALLOC)

#define KD_BKGND_RES_TAIL_INTERRUPTED ((kd_resolution *)_kdu_long_to_addr(1))
#define KD_BKGND_TREF_TAIL_INTERRUPTED ((kd_tile_ref *)_kdu_long_to_addr(1))

class kd_cs_thread_context :
  public kdu_thread_context, public kdu_thread_queue {
  public: // Member functions
    kd_cs_thread_context(kd_codestream *codestream)
      { 
        cs=codestream;  cur_threads=0;
        thread_buf_servers=NULL;  thread_stats=NULL;
        job_state.set(0);
        glock_job.init(this,0);
        plock_job.init(this,1);
        res_head.set(NULL);
        res_tail.set(NULL);
        tref_head.set(NULL);
        tref_tail.set(NULL);
        mutex.create(1000);
        clear_wakeup = NULL;
      }
    ~kd_cs_thread_context() { mutex.destroy(); }
    void append_to_res_queue(kd_resolution *res);
      /* Appends `res' to the queue of resolution objects requiring service
         by a background processing thread.  The function must not be
         called unless the object is known not to be on the queue already.
         After calling this function, it is usually appropriate to invoke
         `schedule_resolution_parsing' or `schedule_resolution_palloc',
         unless the function is called from within the relevant background
         processing job itself. */
    void append_to_tile_queue(kd_tile_ref *tref);
      /* Appends `tref' to the queue of tiles that may need to be opened or
         closed by a background processing thread.  The function must not be
         called unless the object is known not to be on the queue already.
         After calling this function, the `schedule_tile_processing' should
         generally be called. */
    void clear_queues(kdu_thread_env *env);
      /* This function suspends all background processing, preventing any
         scheduled background jobs from accomplishing anything if they are
         not already running; once it can be sure that there is no background
         processing in progress, the function locks the `KD_THREADLOCK_GENERAL'
         mutex and empties the resolution and tile-ref queues.  It does not
         actually perform any resolution processing, but it does close any
         tiles that have been scheduled for closure.
         [//]
         The function may be called with `env'=NULL, in which case the
         object should already have detached from the thread queue, so the
         codestream is entering single-threaded processing mode.  This always
         happens inside `kdu_thread_env::cs_terminate', immediately before the
         present object is deleted.  Otherwise, the function is called with
         `env' non-NULL from a context in which the `KD_THREADLOCK_GENERAL'
         mutex is not locked.
         [//]
         The function may only be called from a context in which no resolution
         or tile-ref objects can be appended to the background queues and any
         other background processing (flush or trim processing) that may have
         been scheduled is no longer important.
         [//]
         In practice, the function is called either right before deleting
         the present object or else from a context in which pending tile
         closures must be completed before processing can continue.  The
         latter situation occurs inside `kdu_codestream::restart' and also
         when calling `kdu_codestream::apply_input_restrictions' or
         `kdu_codestream::change_appearance'. */
    void schedule_flush_processing(kdu_thread_entity *caller)
      { schedule_bkgnd_processing(KD_BKGND_FLAG_FLUSH,caller); }
    void schedule_trim_processing(kdu_thread_entity *caller)
      { schedule_bkgnd_processing(KD_BKGND_FLAG_TRIM,caller); }
    void schedule_tile_processing(kdu_thread_entity *caller)
      { schedule_bkgnd_processing(KD_BKGND_FLAG_TILE_OPENCLOSE,caller); }
    void schedule_resolution_processing(kdu_thread_entity *caller);
      /* As a `kdu_thread_queue', this object offers a limited set of jobs
         for background codestream processing.  Exactly what these jobs do
         depends upon which of these functions is called.  Currently, there
         are only two types of background jobs, and these call
         `do_glock_processing' and `do_plock_processing', respectively.
            The flush, trim, tile-openclose and res-parse operations all
         require the "glock" context, meaning that the `KD_THREADLOCK_GENERAL'
         lock must be acquired.  Accordingly, these are all serviced by a
         single job, that invokes the `do_glock_processing' function.
            The res-palloc operation (allocate precincts to resolutions)
         is relevant to output (and possibly interchange) codestreams, where
         it requires the "plock" context, meaning the `KD_THREADLOCK_PRECINCT'
         lock must be acquired.  Accordingly, this operation is serviced by
         a separate job, that invokes the `do_plock_processing' function.
            If the relevant background processing job is already scheduled
         or running at the point when any of the above functions is called,
         the function simply records the work that needs to be done rather
         than scheduling a new job -- the running job will eventually get
         around to it before terminating. */
    int manage_buf_servers(kd_buf_server buf_servers[]);
    int manage_compressed_stats(kd_compressed_stats *stats[]);
      /* The above functions transfer responsibility for managing
         thread-specific resources to/from the current object.  When
         called with a NULL argument, the relevant resource is withdrawn
         from management.  Non-NULL arguments are actually arrays consisting
         of 1+`KDU_MAX_THREADS' entries.  The first entry in the array has
         already been initialized by the caller, whereas the remaining
         objects are initialized by the thread context, using the first
         element as a template.  This is important because only the present
         object receives notification when new threads enter the thread group
         (via the `num_threads_changed' function), so only this object is
         able to initialize new members of the relevant arrays as the need
         arises.
         [//]
         It is important that the resources be withdrawn (by calling these
         functions with NULL arguments) before any resources associated with
         the arrays are deleted; otherwise, asynchronous access to a deleted
         resource might occur.  The functions both return the total number of
         threads for which objects in the relevant resource have been
         initialized, so that the caller can perform any relevant cleanup
         on the relevant elements if required.
         [//]
         A non-NULL `stats' array should not be supplied unless its first
         entry is non-NULL and all remaining `KDU_MAX_THREADS' entries are
         NULL. */
  public: // Overridden functions from `kdu_thread_context'
    virtual void enter_group(kdu_thread_entity *caller)
      { 
        kdu_thread_context::enter_group(caller);
        caller->attach_queue(this,NULL,KDU_CODESTREAM_THREAD_DOMAIN,0,
                             KDU_THREAD_QUEUE_BACKGROUND);
      }
    virtual void leave_group(kdu_thread_entity *caller=NULL)
      { // As with `kdu_thread_context::leave_group', `caller' can be NULL if
        // not known.
        if (this->is_attached())
          this->force_detach(caller);
        kdu_thread_context::leave_group(caller);
      }
  protected: // Overridden functions from `kdu_thread_context'
    virtual int get_num_locks() { return KD_THREADLOCK_COUNT; }
    virtual void num_threads_changed(int num_threads);
  protected: // Overridden functions from `kdu_thread_queue'
    virtual int get_max_jobs() { return 2; }
    virtual void notify_join(kdu_thread_entity *caller)
      { request_termination(caller); }
    virtual void request_termination(kdu_thread_entity *caller);
  private: // Helper functions
    friend class kd_cs_background_job;
    void schedule_bkgnd_processing(kdu_int32 flags, kdu_thread_entity *caller);
    void do_glock_processing(kdu_thread_env *env);
      /* Does background processing that uses the "glock" context -- i.e.,
         processing activities that require the `KD_THREADLOCK_GENERAL'
         lock to be held. */
    void do_plock_processing(kdu_thread_env *env);
      /* Does background processing that uses the "plock" context -- i.e.,
         processing activities that require the `KD_THREADLOCK_PRECINCT'
         lock to be held. */
  private: // Data
    kd_codestream *cs;
    int cur_threads;
    kd_buf_server *thread_buf_servers;
    kd_compressed_stats **thread_stats;
    kdu_mutex mutex; // Just to deal with contention between a thread that
       // might invoke `num_threads_changed' and another thread that might
       // invoke `manage_buf_servers' or related thread-specific resources
    kdu_thread_entity_condition *clear_wakeup; // see below
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep1)
    kd_cs_background_job glock_job; // Runs `do_glock_processing'
    kd_cs_background_job plock_job; // Runs `do_plock_processing'
    kdu_interlocked_int32 job_state; // See below
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep2)
    kdu_interlocked_ptr res_head;          // Used to build a list of
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep3)  // resolutions that need
    kdu_interlocked_ptr res_tail;          // servicing by background jobs
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep4)
    kdu_interlocked_ptr tref_head;         // Used to build a list of
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep5)  // tiles that need to be opened
    kdu_interlocked_ptr tref_tail;         // or closed by background jobs
    KD_PRIVATE_CACHELINE_SEPARATOR(_trailer)
  };
  /* Notes:
        The `kdu_thread_queue'-related aspects of this object are managed
     by the atomic state variable, `job_state'.  This variable has the
     following flags:
     -- `KD_BKGND_FLAG_GLOCK_ACTIVE' is set if a call to `do_glock_processing'
        is in progress or has been scheduled via `schedule_bkgnd_processing'.
     -- `KD_BKGND_FLAG_PLOCK_ACTIVE' is set if a call to `do_plock_processing'
        is in progress or has been scheduled via `schedule_bkgnd_processing'.
     -- `KD_BKGND_FLAG_FLUSH' is set if there is flush work to be done;
        proceeds in the glock context.
     -- `KD_BKGND_FLAG_TRIM' is set if there is trim work to be done;
        proceeds in the glock context.
     -- `KD_BKGND_FLAG_RES_PARSE' is set if there is pre-parsing work to be
        done (input codestreams only); proceeds in the glock context.
     -- `KD_BKGND_FLAG_RES_PALLOC' is set if there is precinct allocation
        work to be done (output and interchange codestreams only); proceeds
        in the plock context.
     -- `KD_BKGND_FLAG_TILE_OPENCLOSE' is set if there is background tile
        opening or closeing work to be done; proceeds in the glock context.
     -- `KD_BKGND_FLAG_TERMINATE' is set if `request_termination' has been
        called.
     -- `KD_BKGND_FLAG_ALL_DONE' is set if `all_done' has been called.
        In addition to the above flags, the following three are defined
     specifically to assist in the clearing of the tile and resolution
     queues via the `clear_queues' function.
     -- `KD_BKGND_FLAG_GLOCK_PROCESSING' is set if a call to
        `do_glock_processing' is currently in progress; this is similar to
        `KD_BKGND_FLAG_GLOCK_ACTIVE', except that the ACTIVE state commences
        from the point at which a glock processing job is scheduled, while the
        PROCESSING state is asserted only while the job is running.
     -- `KD_BKGND_FLAG_PLOCK_PROCESSING' is similar to the above flag, except
        that it refers to the `do_plock_processing' function, as opposed to
        `do_glock_processing'.
     -- `KD_BKGND_FLAG_CLEAR_WAITING' is set if a call to `clear_queues' finds
        that it cannot clear the resolution/tile-ref queues immediately
        because one or both of the above two flags are currently set.  Once
        both of the above flags are reset, the `KD_BKGND_FLAG_CLEAR_WAITING'
        flag will automatically be reset and the `clear_wakeup' member will
        be used to wake the waiting thread.  Note that whenever this flag
        is asserted, all flags that identify requested services are cleared --
        i.e., the flags represented by `KD_BKGND_GLOCK_SERVICES' and
        `KD_BKGND_PLOCK_SERVICES'.  This simplifies the compare-and-set loop
        executed by the `do_glock_processing' and `do_plock_processing'
        functions without introducing any adverse side effects, since calls
        to `clear_queues' can only occur in a context in which background
        processing is no longer required, regardless of which background
        jobs may have been scheduled.
  */

/*****************************************************************************/
/*                              kd_codestream                                */
/*****************************************************************************/

struct kd_codestream { // State structure for the "kdu_codestream" interface
  public: // Member functions
    kd_codestream()
      { // We will be relying on all fields starting out as 0.
        memset(this,0,sizeof(*this));
        max_unloadable_tiles = 64; // A reasonable default value
      }
    ~kd_codestream();
    void delete_and_reset_all_but_buffering_and_threading();
      /* Does all the work of the destructor, except that it does not
         touch buffer management, multi-threading contexts or `rate_stats' --
         these are all inter-dependent and have to be cleaned up carefully
         within the destructor.  The reason for creating this separate
         function is that it facilitates constructing a completely new
         `kd_codestream' object and then transferring its state across to
         an existing one, while preserving the existing multi-threading
         environment and buffer management machinery.  This is used to
         implement `kdu_codestream::restart' when a change of codestream
         parameters is detected (input codestreams only). */
    void construct_common();
      /* Called from within `kdu_codestream::create' after some initial
         construction work to set the thing up for input or output. */
    void start_multi_threading(kdu_thread_env *env)
      { /* Called when `env' is passed to a top-level API function; does
           nothing if the `thread_context' object has been created already. */
        if (thread_context != NULL) return;
        assert(env != NULL);
        buf_master->set_multi_threaded();
        thread_context = new kd_cs_thread_context(this);
        thread_context->manage_buf_servers(buf_servers);
        if (rate_stats[0] != NULL)
          thread_context->manage_compressed_stats(rate_stats);
        thread_context->enter_group(env);
      }
    void stop_multi_threading(kdu_thread_env *caller=NULL);
      /* Called from the destructor and from `kdu_thread_env::cs_terminate',
         this function deletes the `thread_context' object, but also
         deletes the thread-specific entries of the `rate_stats' and
         `buf_servers' arrays.  If called from a context in which the
         caller's thread environment is known, it should be passed in via
         the `caller' argument, since this is somewhat safer.  Nevertheless,
         it should be safe also to call the function with a NULL `caller'
         argument unless this is somehow being done accidentally from
         within a function like `kdu_thread_queue::request_termination',
         which is against all the advice given many times over within
         the interface documentation. */
    kd_buf_server *get_thread_buf_server(kdu_thread_env *env)
      { // Gets the code buffer server for this specific thread
        int t_idx = check_thread_env(env);
        return buf_servers+1+t_idx;
      }
    kd_compressed_stats *get_thread_rate_stats(kdu_thread_env *env)
      { // Gets the rate stats object for this specific thread (or NULL)
        if (rate_stats[0] == NULL) return NULL;
        int t_idx = check_thread_env(env);
        assert(rate_stats[1+t_idx] != NULL);
        return rate_stats[1+t_idx];
      }
    int check_thread_env(kdu_thread_env *env)
      { // Checks that `env' belongs to the same thread group as this object's
        // `thread_context' and returns the index of the associated thread.
        if (thread_context == NULL) gen_no_thread_context_error();
        int t_idx = thread_context->check_group(env);
        if (t_idx < 0) gen_bad_thread_context_error();
        return t_idx;
      }
    void acquire_lock(int lock_idx, kdu_thread_env *env)
      { // Simplifies invocation of `thread_context->acquire_lock'
        if (thread_context == NULL) gen_no_thread_context_error();
        thread_context->acquire_lock(lock_idx,env);
      }
    void release_lock(int lock_idx, kdu_thread_env *env)
      { // Simplifies invocation of `thread_context->release_lock'
        thread_context->release_lock(lock_idx,env);
      }
    bool check_lock(int lock_idx, kdu_thread_env *env)
      { // Simplifies invocation of `thread_context->check_lock'
        return thread_context->check_lock(lock_idx,env);
      }
    inline void add_pending_precinct(kd_precinct *precinct);
      /* This function is used only in multi-threading environments.  It
         adds to the head of a singly-linked list managed by the atomic
         interlocked member variable `pending_precincts'.  Precincts are
         passed to this function from within `kdu_subband::close_block' once
         `precinct->num_outstanding_blocks' becomes 0.  Later, the
         `process_pending_precincts' function is invoked from a context in
         which the `KD_THREADLOCK_GENERAL' mutex is locked, to move the
         pending precincts along to an appropriate destination.  For
         output codestreams, this appropriate destination is the ready
         list of a `kd_global_rescomp' object.  For input codestreams, the
         appropriate destination is the list of recycled precincts whose
         storage may be reclaimed. */
    void process_pending_precincts();
      /* As mentioned above, in multi-threaded processing precincts whose
         `kd_precinct::num_outstanding_blocks' member reaches 0 within
         `kdu_subband::close_block' are passed initially to the
         `add_pending_precinct' function; then, at an appropriate point,
         the `KD_THREADLOCK_GENERAL' mutex is locked and the present function
         is invoked to finish the processing of any pending precincts.  In
         particular, this function should be invoked at the following points:
         1) within `kdu_subband::open_block', if the general mutex needs
         to be locked to parse content; 2) within `kdu_codestream::open_tile';
         3) within `kdu_codestream::ready_for_flush', `kdu_codestream::flush'
         and `kdu_codestream::trans_out'; 4) within
         `kdu_thread_env::cs_join' and `kdu_thread_env::cs_terminate' once
         any non-codestream queues have been joined/terminated. */
    void close_pending_precincts();
      /* Empties the `pending_precincts' list, invoking
         `kd_precinct_ref::close' on all of its entries.  This is done for
         robust cleanup, in case some precincts were left behind as pending
         after an error condition or premature destruction.  The function
         is always called from the "glock" context (single threaded, or else
         `KD_THREADLOCK_GENERAL' mutex is locked). */
    void gen_no_thread_context_error();
      /* Generates a suitable error through `kdu_error'. */
    void gen_bad_thread_context_error();
      /* Generates a suitable error through `kdu_error' if the thread context
         has a different thread group to the `kdu_thread_env' object
         referenced in an interface call. */
    void construct_output_comp_info();
      /* Called if `output_comp_info' is NULL, but before (or if
         necessary inside) `finalize_construction'.  During reading, this
         function may not be called until all main header information
         has been parsed, since the information in `siz' is not
         complete until both the SIZ and CBD marker segments have been
         parsed.  For codestreams created for writing or for interchange,
         this function is called from within `construct_common'.
            Note that this function does not fill out any NLT (non-linear
         point transform) information for the output components, since this
         information cannot reliably be deduced during codestream generation
         until `kdu_params::finalize_all' has been called.  See
         `finalize_construction' for more on this. */
    void finalize_construction();
      /* For input codestreams, this function is called immediately after
         the main header has been read.  For output codestreams, the
         call is deferred so as to give the caller an opportunity to
         modify coding parameters and call `kdu_params::finalize_all'.  It
         is invoked when information from those coding parameters is
         first required, which will be no later than the first call to
         `kdu_codestream::open_tile'. */
    void restrict_to_fragment(kdu_dims fragment_region,
                              int fragment_tiles_generated,
                              kdu_long fragment_tile_bytes_generated);
      /* This function is called immediately after `construct_common'
         if the `kdu_codestream::create' function supplied a non-NULL
         `fragment_region'. */
    void restart();
      /* Called from within `kdu_codestream::restart' to perform common
         re-initialization steps for an existing code-stream. */
    kd_tile *create_tile(kdu_coords idx);
      /* Creates and completely initializes a new tile, installing it in the
         internal array of tile references and returning a pointer to the
         new tile.  The pointer returned may be equal to KD_EXPIRED_TILE if
         the tile was found not to belong to the current region of interest.
         Note that this function might reclaim previously released tiles
         from a cache of released typical tiles, managed by the
         `typical_tile_cache' member.  The `idx' argument holds the absolute
         index of the tile, which must lie within the range represented by
         the `tile_indices' region member. */
    void trim_compressed_data(kdu_thread_env *env);
      /* This function may be called periodically to trim away the storage
         associated with compressed data bytes which will never be included
         in the final compressed representation.  The function can do nothing
         unless `kdu_codestream::set_max_bytes' has been called to establish
         a target compressed length.  For single-threaded processing,
         `env' is NULL and the function is invoked directly from within
         the `kdu_subband::close_block' function.  For multi-threaded
         processing, `env' should be non-NULL and the function is actually
         invoked from within the `kd_cs_thread_context::do_job' function --
         i.e., as a background processing job. */
    void check_incremental_flush_consistency(int num_layer_specs);
      /* Called from `kdu_codestream::flush' and `kdu_codestream::auto_flush'
         to verify consistency with previous calls to either of these
         functions. */
    bool construct_cbr_flush_set();
      /* This function is invoked by `flush_if_ready' if `cbr_flush_interval'
         is non-zero.  It may need to configure `cbr_flush_set' for the first
         time, and/or install a new `active_tile' within the `cbr_flush_set'
         member.  The function returns true only if all precincts in the
         new flush set are ready to be flushed; otherwise, the function returns
         false, expecting to be called again once more precincts have been
         generated.  When this function is called for the very first time,
         `cbr_flush_set.active_tile'=NULL and `cbr_flush_tile'=0; at this
         point the function also checks other coding parameters for
         compatibility with the CBR flush process. */
    bool ready_for_flush();
      /* This function does the work of `kdu_codestream::ready_for_flush',
         except that it expects the `KD_THREADLOCK_GENERAL' mutex to have been
         locked and `process_pending_precincts' to have been invoked by the
         caller, if necessary.  The function is also invoked by
         `flush_if_ready'.
            In the special case where `cbr_flush_interval' is non-zero, there
         is no need to check whether or not we are ready to flush something,
         since we do not generate tile-parts in the CBR controlled flushing
         mode; accordingly, the function returns true immediately in this
         case. */
    void flush_if_ready(kdu_thread_env *env);
      /* This function does virtually all the actual work of
         `kdu_codestream::flush' and `kdu_codestream::trans_out'.  Delayed
         calls to this function are set up by `kdu_codestream::auto_flush'
         and `kdu_codestream::auto_trans_out'.  The behaviour of the
         function is determined by the `doing_trans_out' member, as
         well as the `layer_sizes', `target_sizes' and `target_thresholds'
         member arrays, together with `num_sized_layers'.  The function does
         the equivalent of invoking `kdu_codestream::ready_for_flush'
         before attempting a flush operation.  If `env' is non-NULL,
         the function itself must take out the `KD_THREADLOCK_GENERAL'
         lock.  Otherwise, this is unnecessary because we are either
         in single threaded mode, or the caller has already taken
         out the lock. */
    kdu_long get_main_and_tile_header_cost();
      /* This function returns the cost (number of bytes) of all main and
         tile-part header bytes that have yet to be written within the current
         fragment.  If `initial_fragment' is true and `header_generated' is
         false, the function includes the cost of the main codestream header.
         The function also includes the cost of all tile-part headers within
         the current fragment that have not yet been written.  The function
         simulates the generation of marker segments for the main header and
         tile-part headers that are relevant and includes the cost of all
         relevant SOC and SOD markers.  The function also includes the cost of
         all relevant COM marker segments.  However, it deliberately does not
         count the cost of PLT or TLM marker segments.  Also, certain
         combinations of the ORGtparts and Porder code-stream parameter
         attributes can lead to minor inaccuracies in the simulated
         lengths due to difficulties in anticipating the number of tile-parts
         which will need to be written.  Note that the cost of the 2-byte EOC
         marker that appears at the very end of a JPEG2000 codestream is
         NOT included in the returned header byte count.
            To minimize redundant recomputation of header costs, the function
         keeps a record of the most recently computed value in the
         `main_and_tile_header_cost' member variable.  Each time the
         `flush_if_ready' function enters, this member variable is reset to
         -1 so that the next call to this function will recalculate the
         cost. */
    kdu_long simulate_output(kdu_long &packet_header_bytes,
                             int layer_idx, int reslength_layer_idx,
                             kdu_uint16 slope_threshold,
                             bool finalize_layer, bool last_layer,
                             kdu_long max_bytes=KDU_LONG_HUGE,
                             kdu_long *sloppy_bytes=NULL);
      /* This function plays a critical role in rate control and preparing
         for code-stream generation.  It may be used in applications which
         generate the entire code-stream once all precinct data has been
         generated, but it may also be used in applications which generate
         the code-stream incrementally, periodically sizing and generating
         whatever portion of the code-stream can be written, given the
         code-blocks which have been encoded so far.
            To understand the operation of this function, it is helpful to
         know that whenever a precinct's code-blocks have been encoded, the
         precinct is entered onto a "ready" list.  When the present function
         is called, it simulates the process of generating final code-stream
         output for a single quality layer of all such "ready" precincts.
         If the "ready" precincts are not sufficient to generate the
         full code-stream, their size is automatically scaled, using an
         algorithm which attempts to estimate the number of bytes which
         would be occupied by all packets belonging to the indicated layer
         which have not yet been written to the code-stream.  For this reason,
         the `max_bytes' value supplied to this function should represent
         the maximum number of bytes which are allowed for all packets
         belonging to the indicated layer, minus the number of bytes which
         have already been written for packets belonging to that layer.
            The simulation process is driven by the supplied distortion-length
         slope threshold.  A slope threshold of 0 yields the largest possible
         output size and 0xFFFF yielding the smallest possible output size
         (all code-block bytes discarded).
            The number of bytes returned includes the cost of packet headers,
         which is also returned separately via the `packet_header_bytes'
         argument.  Both the return value and the `packet_header_bytes' value
         are scaled to represent the expected cost over all packets of the
         indicated layer which have not yet been written to the code-stream.
         Moreover, if `layer_idx'=0, the number of returned bytes also
         includes the value returned by the `get_main_and_tile_header_cost'
         function.
            The `layer_idx' and `reslength_layer_idx' arguments will normally
         be identical.  The former indicates the number of layers which have
         already been finalized (if 0, the tag-tree coding machinery must
         be initialized in each packet) -- this is the value passed to
         `kd_precinct::simulate_packet'.  The latter value is passed to
         `kd_reslength_checker::set_layer' if appropriate.  The two values
         may differ only when `pcrd_opt' is obtaining a preliminary
         distortion-length slope threshold for a quality layer that has a
         target size, in order to assign distortion-length slope thresholds
         to earlier unsized layers (ones that have 0 for their target length,
         meaning that they are to be implicitly sized).
            The `finalize_layer' argument should be true if this is the last
         call to this function with the current layer index.  This causes the
         packet header state information to be saved to a place from which
         it will be restored in each simulation attempt for the next layer.
         Also, it is mandatory to finalize the layers during simulation
         before actually generating packet data.
            The `last_layer' argument should be true if this is the final
         layer for which any information will be generated.  If any tile
         contains more layers, their contribution will be included at this
         point as empty packets (i.e., the empty packet bit is set to 0).
         Any such empty packet is never assigned an SOP marker.
            The `max_bytes' argument may be used to supply an upper bound on
         the total number of bytes which are allowed to be generated here.
         This allows the simulation process to stop as soon as this limit
         is exceeded.  In this case, the returned byte count may not be a
         true reflection of the full cost of generating the layer, but it
         is guaranteed to exceed the `max_bytes' limit.
            When the function is called with `finalize_layer' true, the
         `max_bytes' limit must not, under any circumstances be exceeded.
         The assumption is that the caller has already done the work of
         figuring out a slope threshold which will satisfy the relevant
         rate constraint -- if not, `max_bytes' can be set to
         KDU_LONG_HUGE (the default).  Note that KDU_LONG_HUGE is a
         very large "kdu_long" value which can be represented exactly as a
         double precision floating point quantity; it may be less than
         KDU_LONG_MAX.
            A non-NULL `sloppy_bytes' argument may be supplied only when
         finalizing the last layer, once all code-blocks in the entire
         code-stream have been generated.  In this case, the caller must be
         certain that a slope threshold of `slope_threshold'+1 can satisfy
         the limit imposed by `max_bytes'.  The function first runs the low
         level packet-oriented simulation tools with this value of
         `slope_threshold'+1 and then runs them again (this time finalizing)
         with `slope_threshold'.  On the second run, any additional coding
         passes which would be contributed due to the reduced threshold are
         trimmed away from the relevant code-blocks (they cannot be recovered
         later on) until the number of additional bytes is less than the value
         of *`sloppy_bytes'.  The *`sloppy_bytes' is progressively decremented,
         so that upon return the caller may determine the total number of
         available bytes which could not be used up.  The caller should then
         generate packet data using the supplied slope threshold.
            This `sloppy_bytes' option slows down the rate control process and
         is recommended primarily when you can expect quite a few code-block
         passes to have the same length-distortion slope value.  This happens
         during transcoding, where the only slope information available comes
         from the indices of the quality layers to which the coding passes
         originally contributed.  It may also happen if the block encoder
         does not collect a rich set of length-distortion statistics, simply
         estimating a rough prioritization instead. */
    kdu_uint16 find_slope_threshold(int layer_idx, int unsized_layers,
                                    kdu_long min_bytes, kdu_long max_bytes,
                                    kdu_uint16 max_threshold,
                                    kdu_long max_threshold_cumulative_bytes,
                                    kdu_uint16 suggested_initial_threshold,
                                    kdu_long *simulated_layer_bytes=NULL);
      /* This function does most of the work of `pcrd_opt'.  The purpose
         of the function is to find a distortion-length slope threshold
         such that the total number of bytes allocated to the quality layer
         identified by `layer_idx' and the preceding `unsized_layers' will
         lie between `min_bytes' and `max_bytes'.
            The `min_bytes' and `max_bytes' thresholds refer to the
         total number of bytes that will be occupied by the relevant packets
         of all outstanding precincts (those not flushed already).  If
         `layer_idx' plus any `unsized_layers' include layer 0, the `min_bytes'
         and `max_bytes' thresholds also include main and tile-part header
         costs, but not the EOC marker cost.
            Upon entry, it is assumed that quality layer
         `layer_idx'-`unsized_layers'-1 has been finalized -- i.e.,
         `simulate_output' has been called for that layer with
         `finalize_layer' equal to true.
            If `unsized_layers' is non-zero, the function is being used to
         form an initial estimate for the threshold to be used with quality
         layer `layer_idx' so that the earlier "unsized layers" can be assigned
         intermediate thresholds.  This estimate does not need to be highly
         accurate and indeed the `min_bytes' threshold might be violated
         if we were to actually use the returned threshold with quality layer
         `layer_idx', but we are only using the return value to assign
         thresholds for the earlier `unsized_layers'.  The challenge here is
         to avoid a situation in which the headers associated with the
         JPEG2000 packets generated for unsized layers prevent us from
         satisfying the `max_bytes' limit once all the unsized layers have
         been finalized and we call the function again with `unsized_layers'
         equal to 0.  To this end, the function conservatively assumes that
         the header sizes for each packet associated with an unsized layer
         will be identical to the header size that would be required if
         there were no unsized layers -- this is conservative because the
         header cost tends to be distributed between the layers to a
         certain extent.
            If `unsized_layers' is 0, the `min_bytes' and `max_bytes' values
         are usually chosen based on the tolerance parameter supplied to
         `pcrd_opt'.  The function is called once in this way for each
         sized quality layer -- i.e., each quality layer for which there is
         a non-zero target size in the `target_sizes' array -- after any
         unsized quality layers are finalized.
            If no quality layers have yet been finalized, the value of
         `max_threshold' is 0xFFFF and `max_threshold_cumulative_bytes' is 0.
         Otherwise,  `max_threshold' holds the slope threshold associated
         with the last finalized layer and `max_threshold_cumulative_bytes'
         represents the cumulative number of bytes occupied (or expected to be
         occupied) by all outstanding precincts (the ones being sized here)
         in all finalized quality layers.  This value is used by the internal
         threshold prediction algorithm, which expects an approximately
         log-linear relationship between the cumulative byte count and the
         slope threshold.
            The `suggested_initial_threshold' value, if non-zero, provides
         an initial guess that the internal algorithm can use to maximize
         its chance of converging to a suitable slope threshold as quickly
         as possible.
            The `simulated_layer_bytes' argument, if non-NULL, is used to
         return the number of bytes associated with a new quality layer
         that has the returned slope threshold, taken over all outstanding
         precincts.  Returning this value can avoid the need for `pcrd_opt'
         to issue a redundant call to `simulate_output'.  If, for some
         reason, the value is not known and `simulated_layer_bytes' is
         non-NULL, the value of *`simulated_layer_bytes' is set to -1.
      */
    void pcrd_opt(bool trim_to_rate, double tolerance);
      /* Runs the PCRD-opt algorithm to find the slope thresholds for each
         of the `num_sized_layers' quality layers for which an entry appears
         in the `target_sizes', `layer_sizes', `cumulative_sizes' and
         `layer_thresholds' arrays.
            Upon entry, the `layer_thresholds' array's entries are either 0
         or else they are equal to the layer thresholds that were discovered
         by a previous flush cycle.  Upon return, the `layer_thresholds' array
         holds the distortion-length slope thresholds for each quality layer,
         which are found to meet the objectives found in the `target_sizes'
         array.
            The `target_sizes' array's entries are not altered in any way
         by this function.  However, non-zero entries are copied across
         directly to the `expected_sizes' array, while the `expected_sizes'
         associated with zero-valued entries in the `target_sizes' array are
         estimated (the estimates are exact in the case where the
         codestream is being flushed all at once, rather than incrementally).
         The following principles are used to drive the rate control process
         for quality layers that do not have explicit target sizes (i.e.,
         where the `target_sizes' entry is zero):
         1) The logarithmic distortion-length slope thresholds associated with
            unsized quality layers should be distributed roughly equally
            between those found for sized quality layers.  This policy
            tends to distribute the associated quality layer sizes roughly
            logarithmically.
         2) If the last quality layer has an unknown size, all remaining
            coded data will be included in that layer and its entry in the
            `target_sizes' array will be set equal to the actual cumulative
            number of bytes allocated to all quality layers.  The second
            last quality layer, if unsized, will then be assigned a target
            size based on the logarithmic distribution rule.
         3) If one or more initial layers have an unknown target size, their
            logarithmic distortion-length slope thresholds will be separated
            from each other by 300.  This often leads to cumulative layer
            sizes that are separated from each other by roughly a factor of
            sqrt(2).
               The function is designed to work under both single flush and
         incremental flushing conditions.  When incremental flushing is used,
         the function will be called multiple times.  Each call determines a
         suitable set of slope thresholds for incremental code-stream, after
         which the caller performs the incremental code-stream generation.
         This latter activity updates entries in the `layer_sizes' array,
         which keeps track of the total size of each quality layer, excluding
         optional pointer information.  The `target_sizes' values refer to
         the cumulative size at each layer, spread over the entire code-stream.
         The present function finds slope thresholds which, if applied to
         all code-blocks not yet written to the code-stream, would be expected
         to produce a number of bytes equal to the relevant `target_sizes'
         value minus the corresponding `layer_sizes' value.
               The `trim_to_rate' argument has the same interpretation as it
         does in the `kdu_codestream::flush' function.  You should not use this
         option unless all code-blocks have already been encoded, meaning that
         this is the last call to this function prior to complete generation
         of the code-stream.
               The `tolerance' argument also has the same interpretation as it
         does in the `kdu_codestream::flush' function.  There is no restriction
         on its use.
      */
    void pcrd_trim(bool finalize_last_layer);
      /* This function is invoked if rate control is driven by fixed
         distortion-length slope targets, but the slope thresholds may need
         to be modified to accommodate an upper bound induced by `Creslengths'
         attributes and/or a lower bound established by the `min_target_sizes'
         array.  On entry, the `layer_thresholds' array contains the slope
         thresholds that are supposed to be used in the absence of
         constraint violations.  On exit, the values in this array may have
         been modified to accommodate the constraints and the `expected_sizes'
         array's entries have been filled out to reflect the expected
         cumulative number of bytes associated with each successive quality
         layer that will result from these slope thresholds.
            When incremental codestream flushing is being employed, the values
         written to the `expected_sizes' array reflect what we expect to
         happen by the time the codestream has been completely flushed,
         although of course these estimates may be quite inaccurate if there
         are significant statistical variations between what has already been
         compressed and what remains to be compressed.
            If `finalize_last_layer' is false, the function need not perform
         a finalizing call to `simulate_output' for the last quality layer;
         this will only be the case when the compressed data target is a
         structured cache, in which case the `cache_write_ready_precincts'
         function will generate all the layer sizes from scratch. */
    kdu_long simulate_output_cbr(kdu_uint16 slope_threshold,
                                 kdu_uint16 &mod_threshold,
                                 kdu_uint16 &next_slope_val);
      /* This function is used by `pcrd_opt_cbr' for most of its simulation
         work.  It is similar to `simulate_output', but works only on the
         precincts in the flush-set, and there is only one quality layer.
         The return value is the number of packet header and body bytes
         that would be produced with the indicated slope threshold, while
         `mod_threshold' is set to the largest slope threshold value that
         would produce the same result.
            `next_slope_val' is the largest distortion-length slope associated
         with any candidate coding pass whose inclusion is rejected by the
         exclusive lower bound represented by `slope_threshold'.  It is set to
         0 if no candidate coding pass (one with a non-zero distortion-length
         slope) is rejected by the supplied `slope_threshold'.  If
         `next_slope_val' > 0 calling the function again with `slope_threshold'
         equal to `next_slope_val'-1, will result in the smallest additional
         contribution that can be achieved using a global threshold.
            Considering these definitions, the following relationship must
         always hold: `mod_threshold' >= `slope_thershold' > `next_slope_val'.
            The function leaves the per-precinct values of the `mod_threshold'
         and `next_slope_val' values recorded within the
         `kd_precinct_flush_info' records of the `cbr_fset.precincts' arrray,
         so that they can be used later to adjust thresholds individually
         for each precinct, to avoid wasting unreclaimable bandwidth in
         low latency applications with a CBR communication channel. */
    void pcrd_opt_cbr();
      /* This is a much simpler version of the `pcrd_opt' function that is
         used only to determine the truncation point for precincts belonging
         to the current flush-set, as configured within `cbr_fset'.  Unlike
         `pcrd_opt', this function iterates only over the precincts that have
         already been found and recorded within the `cbr_fset' object.
         Furthermore, the function sizes the flush-set based upon the "leaky
         bucket" model, whose state and behaviour are determined by the
         `bucket_fulness', `bucket_max_fulness', `data_rate' and
         `flush_set_samples' members of the `cbr_fset' object.
            The function aims to generate approximately F bytes of data in
         total, where F=D*S with D=`cbr_fset.data_rate' and
         S=`cbr_fset.flush_set_samples'.
            Beyond this, the function must not violate the bucket constraints,
         meaning that `cbr_fset.bucket_fulness' must not grow beyond
         `cbr_fset.max_buffer_fulness', nor be smaller than F, except for the
         very last flush set of the codestream, where the lower bound need
         not apply.  The final flush set must be sized carefully to avoid
         exceeding the target codestream size.
            As we approach the end of the codestream, the function tries to
         stay further clear of the `cbr_fset.max_buffer_fulness' constraint,
         so as to avoid leaving the final flush-set with an inadequate byte
         budget.
            Properties of the simulation are recorded within the
         `kd_flush_set' object found in the `cbr_fset' member.  In particular,
         the number of simulated bytes to be flushed for the current flush set
         and the number of fill/spacer bytes are both recorded there, along
         with slope threshold to be used for each individual precinct.  For
         more information on these quantities, consult the notes following
         the `kd_flush_set' and `kd_precinct_flush_info' definitions. */
    void generate_main_header();
      /* Called from `generate_codestream' and elsewhere, if `header_generated'
         is false.  Leaves `header_generated' = true and `header_length' equal
         to the number of bytes in the main codestream header.  This function
         is used only with non-cached targets. */
    bool generate_codestream(int max_layers);
      /* Generates as much of the final code-stream as possible, given the
         packet progression order which has been selected and the precincts
         which have actually been finalized.  Returns true if the code-stream
         has been completely generated after this function finishes its
         work; in this case, the EOC marker is also written.
         Returns false otherwise.
            The contents of each quality layer are determined from the
         distortion-length slope thresholds recorded in the `layer_thresholds'
         member array.  The number of available thresholds must not be less
         than `max_layers'.  If `max_layers' is less than the number of actual
         quality layers in any tile, the additional quality layers will all
         be assigned empty packets.
            The "prcd_opt" member function may be used first to find
         distortion-length slope thresholds for the various quality layers.
         In any event, some simulation must have been performed to set up the
         packet size information required to determine tile-part header sizes,
         which are required for writing SOT marker segments.  This function
         always creates at least one new tile-part for every tile.  If
         requested, PLT marker segments are written into the tile-part headers
         to identify the lengths of packets generated in this call. */
    bool write_packets_cbr();
      /* This function is used in conjunction with `pcrd_opt_cbr'; both
         functions work with a current flush-set that is constructed using
         `construct_cbr_flush_set' and managed in the `cbr_fset' member.
         The codestream main header should already have been written, and
         `pcrd_opt_cbr' has configured the `kd_precinct_flush_info' records
         within the flush-set to identify both the precincts to be flushed
         (in order), along with the associated slope threshold, also
         identifying any precinct to which fill/spacer bytes should be added.
         All this function needs to do, then, is to walk through the precincts,
         invoking `kd_precinct::write_packet_cbr' as appropriate.
            The function may be regarded as playing a similar role to
         `generate_codestream'; like that function, the return value is true
         if the call completed the flushing of all content for the
         codestream. */
    void cache_write_ready_precincts(int max_layers);
      /* The primary purpose of this function is to invoke each ready
         precinct's `kd_precinct::cache_write_packets' function.  That function
         also updates the `layer_sizes' array and fills in final values for
         the `kd_precinct::packet_bytes' array.  Along the way, the present
         function also works out the expected number of bytes associated with
         each quality layer and writes these values into the `expected_sizes'
         array; if the codestream is being flushed incrementally, these
         expected sizes will be estimates only, but if this function completes
         the writing of all outstanding precincts, the `expected_sizes'
         should agree with the `layer_sizes' (after the writing of any
         outstanding codestream headers).  Essentially, the values written to
         `expected_sizes' are identical to those that would be determined
         by a sequence of calls to `simulate_output' (one for each quality
         layer); in some cases, `simulate_output' will have been used to
         determine the `layer_thresholds', but it is often possible to skip
         some or even all of these simulation steps when the compressed data
         target is a structured cache.
            The `max_layers' argument indicates the number of quality layers
         for which distortion-length slope thresholds are provided via
         `layer_thresholds'.  If additional quality layers must be written
         for some or all of the ready precincts, they are written as empty
         layers. */
    bool cache_write_headers();
      /* Plays a similar role to `generate_codestream' for caching compressed
         data targets.  Prior to calling this function, it is expected that
         all ready precincts have had their `kd_precinct::cache_write_packets'
         function invoked, as a result of which some in-progress tiles may
         now have their `kd_tile::sequenced_relevant_packets' and
         `kd_tile::max_relevant_packets' members equal.  This function
         writes tile headers for each of these tiles.  The function also
         writes the codestream main header element if this has not already
         been done -- note that no EOC marker is ever written to a structured
         cache. */
    void set_reserved_layer_info_bytes(int num_layers);
      /* Sets aside a certain number of bytes for writing a comment marker
         containing a text description of the size and distortion-length slope
         associated with each layer.  The actual marker must be written by
         a subsequent call to `gen_layer_info_comment'. */
    void gen_layer_info_comment(int num_layers, kdu_long *layer_bytes,
                                kdu_uint16 *thresholds);
      /* Writes the comment marker identified above in connection with
         `set_reserved_layer_info_bytes'. */
    void unload_tiles_to_cache_threshold();
      /* Releases unloadable tiles until the memory cache and/or unloadable
         tile count threshold is reached.  The function first tries to
         release those unloadable tiles which do not lie in the current
         region of interest.  If one or both thresholds are still exceeded,
         the function continues to release unloadable tiles, even if they
         do belong to the current region of interest. */
    void read_siz_auxiliary_marker_segs();
      /* Called after reading the main SIZ marker segment, to recover the
         information from any CBD marker segment that may be present, which
         contributes to the information recorded within the `siz_params' object
         referenced by the `siz' member.  This function is called prior to
         `read_main_header', which is normally invoked from within
         `construct_common'.  This way, `siz' can be properly finalized
         or tested for any changes from within `construct_common' or `restart'.
         The function generally leaves the `marker' member with a marker
         segment that has been read but not yet translated, and this marker
         segment will be the first one processed by the later call to
         `read_main_header'. */
  private: // Helper functions
    void read_main_header();
      /* Only for input objects.  Reads all main header marker segments
         (starting just after the SIZ marker segment) up to and
         including the first SOT marker.  This function is shared
         by both `construct_common' and `restart'. */
    void freeze_comments();
      /* Called right before any generation or simulation tasks for code-stream
         generation. */
  public: // Links to Other Objects
    kdu_message *textualize_out; // NULL unless params to be textualized
  //---------------------------------------------------------------------------
  public: // Owned Resources
    kd_cs_thread_context *thread_context; // NULL unless multi-threading used
    kd_compressed_input *in; // NULL unless an input codestream object.
    kd_compressed_output *out; // NULL unless an output codestream object.
    kd_buf_master *buf_master; // Might be shared between codestreams
    kd_buf_server *buf_servers; // Array with 1+`KDU_MAX_THREADS' elts
    siz_params *siz;
    kd_marker *marker; // General marker reading service. NULL if `in' is NULL
    kd_pp_markers *ppm_markers; // NULL unless PPM markers are used.
    kdu_block *block; // Shared resource, used by non-threaded applications
    kd_compressed_stats *rate_stats[1+KDU_MAX_THREADS];
    kd_tpart_pointer_server *tpart_ptr_server; // NULL if not keeping addresses
    kd_precinct_server *precinct_server; // Always active
    kd_codestream_comment *comhead, *comtail; // Linked list of comments
    kd_tlm_generator tlm_generator; // Not initialized until the first
                 // call to `generate_codestream', and then only if required.
    kd_mct_stage *global_mct_head; // Provided for robust cleanup if error
    kd_mct_stage *global_mct_tail; // occurs while constructing MCT stage list.
    kdu_quality_limiter *limiter; // Copy of last limiter passed to
                                  // `apply_input_restrictions', or else NULL.
    kd_flush_stats *flush_stats; // If `kdu_codestream::add_flush_stats' called
    kd_flush_stats *aux_flush_stats; // cf `kdu_codestream::attach_flush_stats'
  //---------------------------------------------------------------------------
  public: // Dimensions and Parameters
    int profile; // 0 thru 5.  Adjusted if a violation is detected.
    bool uses_mct; // If the `Sextensions_MCT' flag is present in SIZ.
    int num_components; // Number of codestream image components.
    int num_apparent_components; // Codestream components visible acrosss API
    int num_output_components; // Same as `num_components' unless MCT is used
    int num_apparent_output_components; // =`num_apparent_components' if no MCT
    kdu_component_access_mode component_access_mode; // See below
    kdu_dims canvas; // Holds the image origin and dimensions; this is
                     // affected by fragment regions
    kdu_dims tile_partition; // Holds the tiling origin and dimensions; this
                             // is not affected by fragment regions
    kdu_coords tile_span; // Tiles across and down the entire codestream
    kdu_dims tile_indices; // Range of tile indices in `tile_refs' array
    int discard_levels, min_dwt_levels, max_apparent_layers;
    int max_tile_layers; // Max layers in any tile opened so far.
    int num_open_tiles; // Number of tiles opened, but not yet closed.
    kdu_dims region; // Current region of interest; often identical to `canvas'
    bool cannot_flip; // If true, codestream not compatible with view flipping
    bool initial_fragment; // These are both true, except possibly when
    bool final_fragment; // generating fragmented codestreams
    double fragment_area_fraction; // Fraction of image in current fragment
    int prev_tiles_written;           // These two parameters are zero, except
    kdu_long prev_tile_bytes_written; // when `initial_fragment' is false.
  //---------------------------------------------------------------------------
  public: // Arrays
    kd_comp_info *comp_info; // Array with `num_components' members
    kd_output_comp_info *output_comp_info; // `num_output_components' members
    kd_tile_ref *tile_refs; // One for each tile, in raster order
  //---------------------------------------------------------------------------
  public: // Information used for code-stream generation and rate control
    kd_tile *tiles_in_progress_head; // Head & tail for list of tiles which
    kd_tile *tiles_in_progress_tail; // have been opened, but not yet flushed
    kd_global_rescomp *global_rescomps; // Manages precincts ready for output
    int num_incomplete_tiles; // Number of tiles for which one or more
                              // packets have yet to be generated.
    int max_depth; // Max DWT levels in any tile-comp; updated by tile creation
    int num_sized_layers; // Number of elements in each of the following arrays
    kdu_long *layer_sizes; // Bytes written to each layer; non-cumulative
    kdu_long *tmp_layer_sizes; // Used by `cache_write_ready_precincts'
    kdu_long *target_sizes; // Cumulative target sizes for each layer;
      // used to record information passed via `kdu_codestream::flush'
    kdu_long *expected_sizes; // Cumulative expected sizes for each layer;
      // manipulated by `flush_if_ready' and functions that it calls.
    kdu_long *target_min_sizes; // Cumulative minimum sizes for each layer, to
      // be used in conjunction with `target_thresholds' if `using_slopes' is
      // true.
    kdu_uint16 *layer_thresholds; // Used to store slope thresholds actually
      // used for codestream generation.
    kdu_uint16 *target_thresholds; // Used to record information passed
      // via `kdu_codestream::flush'; NULL unless layer generation is
      // threshold-driven (second mode of `kdu_codestream::flush').
    kdu_long main_and_tile_header_cost; // See `get_main_and_tile_header_cost'
    float rate_tolerance; // From first call to `kdu_codestream::flush'
    bool record_in_comseg; // From first call to `kdu_codestream::flush'
    bool trim_to_rate; // From first call to `kdu_codestream::flush'
    bool using_slopes; // From first call to `kdu_codestream::flush'
    bool using_min_sizes; // From first call to `kdu_coeestream::flush'
    int trans_out_non_empty_layers; // For `kdu_codestream::trans_out' return
    kdu_long trans_out_max_bytes; // If 0, doing `flush', not `trans_out'
    bool reslength_constraints_used; // If reslength constraints used anywhere 
    bool reslength_constraints_violated; // If any reslength constraint failed
    bool reslength_warning_issued; // If using together with incremental flush
    kd_reslength_checker *reslength_checkers; // 1+`num_components' entries;
        // first entry holds global reslength checker; then one per component.
    bool tpart_interruption_warning_issued;// See `kd_tile::generate_tile_part'
  //---------------------------------------------------------------------------
  public: // Members specific to the low delay rate control machinery
    int cbr_flush_interval; // 0 unless the Scbr attribute is present
    float cbr_max_bucket_lines; // 0 unless the Scbr attribute is present
    int cbr_flush_tile; // idx to `tile_refs'; advanced only after tile flushed
    kd_flush_set cbr_fset; // Used from first flush, if Scbr is present
  //---------------------------------------------------------------------------
  public: // Flags and other State Variables
    bool allow_restart; // True if `kdu_codestream::restart' is allowed
    bool transpose, vflip, hflip; // Geometric manipulation parameters
    bool resilient; // If true, error resilience is maximized.
    bool expect_ubiquitous_sops; // See declaration of "set_resilient" function
    bool fussy; // If true, sensitivity to correctness is maximized.
    bool interchange; // True if `in' and `out' are both NULL.
    bool simulate_parsing_while_counting_bytes; // See `set_max_bytes'
    bool persistent; // If `interchange' is true or if an input object's
                     // `kdu_codestream::set_persistent' function was called
    bool cached_source; // If `in' has `KDU_SOURCE_CAP_CACHED' capability
    bool in_memory_source; // True if `in' offers `KDU_SOURCE_CAP_IN_MEMORY'
    bool cached_target; // If `out' has `KDU_TARGET_CAP_CACHED' capability
    bool tiles_accessed; // Becomes false after first tile access
    bool construction_finalized; // True once `finalize_construction' is called
    bool comments_frozen; // True after header sizing has started
    bool header_generated; // True once main header has been written.
    kdu_int32 block_truncation_factor;// Used only by `kdu_subband::open_block'
    kdu_long header_length; // Num bytes written to main header
    kdu_long written_packet_bytes; // Total packet body & header bytes written
    kdu_long written_packet_header_bytes; // Total packet header bytes written
    int reserved_layer_info_bytes; // Bytes reserved for layer info COM segment
    kd_tile *active_tile; // NULL unless we are in the middle of reading a tile
    kdu_long next_sot_address; // For seeking to the next unread tile-part.
    kdu_long next_sot_scan_address; // Location in body of prev tpart whence we
        // can scan for the next SOT, if above address turns out to be invalid.
    int next_tnum; // Negative, except while scanning 1'st tparts in Profile-0
    int num_completed_tparts; // Number of tile-parts actually read or written.
    kdu_uint16 min_slope_threshold; // 0 until `set_min_slope_threshold' called
    kdu_clock timer; // Used by `report_cpu_time'
  //---------------------------------------------------------------------------
  public: // Tile cache management
    kd_tile *unloadable_tiles_head;
    kd_tile *unloadable_tiles_tail; // Tiles enter list from the tail
    kd_tile *unloadable_tile_scan; // Points to next tile in the list, to be
        // checked by `unload_tiles_to_cache_threshold' to see if it belongs
        // to the current region of interest.  Becomes NULL if all tiles in
        // the unloadable list belong to the region of interest.
    int num_unloadable_tiles; // Number of tiles in unloadable list
    int max_unloadable_tiles; // Limits the length of the unloadable tiles
        // list to avoid long searches for tiles which belong to the current
        // region of interest.
    kd_tile *typical_tile_cache; // List of released typical tiles, all of
                                 // which have `kd_tile::tile_ref'=NULL.
  //---------------------------------------------------------------------------
  public: // Members used with atomic interlocked operations and for auto-flush
    KD_PUBLIC_CACHELINE_SEPARATOR(_leadin)
    kdu_interlocked_ptr pending_precincts; // See `add_pending_precinct'
    kdu_interlocked_int32 tc_flush_counter;   // These are used for auto-flush
    kdu_interlocked_int32 incr_flush_counter; // with output codestreams. 
    int tc_flush_interval;   // These hold the values to add to above
    int incr_flush_interval; // counters after transition to -ve triggers flush
    kdu_interlocked_int32 tc_flush_pending; // If non-zero, next generated
                                            // block row will schedule a flush
    KD_PUBLIC_CACHELINE_SEPARATOR(_tailer)
  };
  /* Notes:
         The first entry of the `buf_servers' array points to a global
     code buffer server that is used in single-threaded deployment or
     from within a `KD_THREADLOCK_GENERAL' protected critical section in
     multi-threaded applications.  There are `KDU_MAX_THREADS' additional
     elements, providing individual code buffer servers for each thread which
     is associated with the object.  The number of threads is monitored by
     the `thread_context' object, which receives notification of new
     threads from any thread group to which we may belong.  Code buffer
     servers for these extra threads are initialized and attached to the
     `buf_master' object only as required.
         The `rate_stats' array also has 1+`KDU_NUM_THREADS' objects; this
     allows each thread to separately update code-block statistics,
     occasionally passing this information along to the first object which
     holds global code-block R-D statistics.  The `rate_stats[0]' entry is
     NULL unless rate allocation is being used.
         The `num_tparts_used' count does not include tiles which we skipped
     over since they had no intersection with the region of interest.
         The `active_tile' member is used during code-stream parsing to keep
     track of the tile whose tile-part is being actively parsed.  If
     `cached_source' is true, the compressed data is managed by a cache and
     there is no parsing as such.  In this case, `active_tile' is always NULL
     and `tpart_ptr_server' should remain NULL.
         The `global_rescomps' array contains 33*num_components entries.
     The entry at location d*num_components+c is used to manage precincts
     which are ready for rate-control simulation, belonging to component c
     at resolution depth d.  Depth 0 corresponds to the highest resolution
     level of each tile of component c.
         Information is maintained for two different types of image
     components here.  Most of the machinery is concerned with so-called
     "codestream image components".  These are the ones which are subjected
     to spatial DWT, quantization and block coding processes.  Consequently,
     we commonly refer to these simply as the components.  The image
     components, however, are often subjected to subsequent inverse
     colour or multi-component transformation processes in order to
     reconstruct a final image.  We refer to the components (or planes) of
     the final reconstructed image as "output image components".  The
     output components are described by the `num_output_components',
     `num_apparent_output_components' and `output_comp_info' members.
         As discussed extensively in the comments appearing with the second
     form of the `kdu_codestream::apply_input_restrictions' function, the
     set of visible (or apparent) output components may be different to
     the set of visible (or apparent) codestream components.  In fact, only
     one of these two sets of components may be restricted at any given
     time, depending on the state of the `component_access_mode' member.
        If `component_access_mode' is set to `KDU_WANT_CODESTREAM_COMPONENTS',
     the set of codestream components may be restricted or permuted via the
     `num_apparent_components' member, working in conjunction with
     `kd_comp_info::apparent_idx' and `kd_comp_info::from_apparent'.  In this
     case, however, information about output components will not be made
     available over the API -- output components will be treated as synonymous
     with codestream components.
        If `component_access_mode' is set to `KDU_WANT_OUTPUT_COMPONENTS', the
     set of output components may be restricted or permuted via the
     `num_apparent_output_components' member, working in conjunction with
     `kd_output_comp_info::apparent_idx' and
     `kd_output_comp_info::from_apparent'.  In this case, all codestream
     components are visible across the API in their original form, except
     that applications will not be able to access a non-empty
     `kdu_tile_comp' interface for any tile-component which is not
     involved in the reconstruction of any visible output components within
     the relevant tile (this may vary from tile to tile). */
  
/*****************************************************************************/
/*                                kd_tile                                    */
/*****************************************************************************/

#define KD_EXPIRED_TILE ((kd_tile *)(-1))

struct kd_tile { // State structure for the "kdu_tile" interface
  public: // Member functions
    kd_tile(kd_codestream *codestream, kd_tile_ref *tref,
            kdu_coords idx, kdu_dims tile_dims);
      /* Sets up only enough information for the tile to locate itself
         within the codestream canvas.  `idx' holds the absolute index of
         the tile, but this must lie within the range identified by
         `codestream->tile_indices'. */
    ~kd_tile();
      /* Note that this function also detaches the tile object from the
         codestream object which references it, replacing the pointer with
         the special illegal value, KD_EXPIRED_TILE. */
    void release();
      /* This function serves a similar role to the destructor, except that
         it reserves the option to move the object onto a list of cached
         "typical" tiles managed by the `kd_codestream' object.  This is
         done if the tile has its `is_typical' member set to true and if the
         codestream is open for reading.  The cached typical tile's structure
         can be reused in the future, without rebuilding it from scratch,
         subject to appropriate conditions.  If the function chooses not to
         move the tile onto the typical list, the destructor will be invoked,
         so this is a self-efacing function.  Tiles that are only empty
         interfaces, created to deal with insufficient header information in
         a dynamic cache, are always deleted by this function. */
    void initialize();
      /* Called immediately after a successful construction.  Builds all
         subordinate structures and fills in all fields.   If the codestream
         object has the persistent mode (applies to persistent input objects
         and all interchange objects), the region of interest members
         in the tile and all of its subordinate structures will be set to
         empty regions. Otherwise, the `set_elements_of_interest' function
         will be called immediately. */
    void recycle(kd_tile_ref *tref, kdu_coords idx, kdu_dims tile_dims);
      /* This function is similar to `initialize', but works on a previously
         constructed (typical) tile, rather than a tile which has been
         freshly created using the constructor.  If possible, the function
         will reuse the previously constructed structures (this is generally
         possible if the new tile is also a typical one).  Otherwise, it
         will delete those structures and invoke `initialize' to start over
         again.  The function is called only from `kd_codestream::create_tile'
         and only on tiles which were previously added to the typical tile
         cache by `kd_tile::release'. */
    void restart();
      /* Called from within `kdu_codestream::restart', so as to recycle all
         resources as early as possible and reset state variables in
         preparation for re-use.  Sets the `needs_reinit' flag, since
         the `reinitialize' function must be called to complete the restart
         operation -- this is deferred until the tile is first accessed
         (opened, or accessed for parsing). */
    void reinitialize();
      /* Called when a tile is first accessed in any way, after a previous
         call to `restart' (i.e., after a code-stream restart). */
    void count_non_empty_tile_comp_subbands();
      /* This function is used only with output codestreams; it is called
         from within `initialize', `reinitialize' and `recycle' -- i.e.,
         whenever a tile is newly prepared for use.  The function counts
         the number of non-empty subbands in each tile-component and stores
         this value in the `kd_tile_comp::completed_subband_counter' member
         for use in auto-flush operations. */
    void open(kdu_thread_env *env);
      /* Called by `kdu_codestream::open_tile' or `kdu_codestream::open_tiles'
         after the tile has been constructed and initialized (if necessary),
         but may also be called by a background processing job that is
         responding to a tile opening request scheduled by
         `kdu_codestream::open_tiles'.  If the tile is already marked as
         open, the function does nothing at all.
            The function marks the tile as open until it is subsequently
         closed.  If the codestream object has the persistent mode (applies to
         persistent and all interchange objects), the tile's region of interest
         fields will all be set up here.
            The function also updates the `tile_ref->flags' member (atomically
         if `env' is non-NULL), clearing any pending open/close/wakeup
         conditions and setting the `KD_TREF_FLAGS_OPEN' flag.  If the
         `KD_TREF_FLAGS_NEED_WAKEUP' flag was set, the function wakes up the
         thread that is waiting upon the `tile_ref->wakeup' object. */
    kd_tile *close(kdu_thread_env *env);
      /* May be called directly from `kdu_tile::close' or from a background
         processing job that is responding to an earlier call to
         `kdu_tile::close'.  This function either marks the tile as closed,
         adding it to a list of tiles whose resources can be reclaimed and
         recycled if a cache threshold is exceeded during subsequent tile
         opening operations, or else the function permanently closes the tile,
         leaving its `tile_ref->tile' member with the `KD_EXPIRED_TILE' value.
            The function also updates the `tile_ref->flags' member (atomically
         if `env' is non-NULL) clearing the `KD_TREF_FLAGS_OPEN' and
         `KD_TREF_FLAGS_CLOSE_PENDING' flags and (if the closure is permanent,
         at least until the codestream is restarted) setting the
         `KD_TREF_FLAGS_CLOSED' flag.
            The function returns a copy of the pointer written to
         `tile_ref->tile' which may be NULL, `KD_EXPIRED_TILE' or a pointer
         to the current object, if it has not been deleted or recycled. */
    bool read_tile_part_header();
      /* Returns false, if the code-stream source was exhausted before a
         new tile-part header could be completely read (up to and including
         the SOD marker).  Except in this event, the current tile becomes
         the codestream object's active tile upon return. */
    bool finished_reading();
      /* This function may be called as soon as we can be sure that no more
         information can be read for the tile.  When this happens, the tile
         is marked as `exhausted' and a search is conducted to see if we can
         destroy any precincts whose existence serves only to allow the parsing
         of packet headers.
            It can happen that the tile deletes itself inside this function,
         if the tile's resources are no longer needed by the application.  In
         this case, the function returns true, indicating to the caller that
         no further attempt should be made to reference the tile.  In all
         other circumstances, the function returns false. */
    kdu_long generate_tile_part(int max_layers, kdu_uint16 slope_thresholds[]);
      /* If all packets for the tile have already been transferred, the
         function does nothing and returns 0.  Otherwise, it generates the
         next tile-part header and all packets for that tile-part, using the
         packet sequencer to determine both the order of packets and the
         number of packets which belong to this tile-part.  The number
         of packets generated may be limited by the number of precincts
         for which code-block data is currently available.  The function
         returns the total number of bytes in the tile-part, including all
         marker codes and segments.
            The `max_layers' argument indicates the number of quality layers
         for which rate allocation information is available in the
         `slope_thresholds' array.  If the tile contains additional layers,
         the corresponding packets are assigned the special empty packet code,
         occupying only a single byte.
            If this function completes the generation of all the tile data,
         it may release the tile itself, unless its continued existence is
         otherwise required (e.g., it may be open for access by the
         application).  Also, if the maximum number of tile-parts advertised
         by a `codestream->tlm_generator' object exceeds the number of
         tile-parts actually generated when the tile data has all been
         written, the function writes extra empty tile-parts as required.
         The empty tile-parts are each 14 bytes long, but the function
         returns only the size of the last non-empty tile-part which was
         generated. */
    void cache_write_tileheader();
      /* Plays a similar role to `generate_tile_header', but for cached
         compressed data targets -- these support structured writing.
         The function is called from `kd_codestream::cache_write_headers'
         if it detects an in-progress tile whose header has not yet been
         written -- this is true if `next_tpart' is 0, because the function
         increments the `next_tpart' member to 1.  The function writes all
         required coding parameter marker segments, except that no SOT
         marker segment or SOD marker is written to a structured cache; nor
         are PPT marker segments written to cached compressed data targets. */
    void remove_from_in_progress_list();
      /* This function is called after the last tile-part of a tile has been
         generated.  It is also called when destroying or restarting a tile,
         just to be on the safe side, although it should have been removed
         from the in-progress list previously.  See the notes below for a
         discussion of "in-progress" tiles. */
    void adjust_unloadability()
      { // Called when any of the conditions described in the notes below
        // associated with tile unloadability might have changed.
        if ((!codestream->persistent) ||
            ((codestream->tpart_ptr_server == NULL) &&
             !codestream->cached_source))
          { assert(!is_unloadable); return; }
        if (is_open || (this==codestream->active_tile))
          { // Should not be on the unloadable list
            if (is_unloadable)
              withdraw_from_unloadable_list();
          }
        else
          { // Should be on the unloadable list
            if (!is_unloadable)
              add_to_unloadable_list();
          }
      }
  private: // Helper functions
    friend class kdu_core::kdu_tile;
    void set_elements_of_interest();
      /* Called from within `kd_tile::open' and/or `kd_tile::initialize'
         to mark each component, resolution, subband, precinct and
         precinct-band with the current region of interest, resolution of
         interest, components of interest and layers of interest.  If the
         codestream mode is "persistent" (applies to input objects and all
         interchange objects), this function is delayed until the tile is
         opened (an event which may occur any number of times, with different
         regions, resolutions, components and layers of interest).  Otherwise,
         the function is called only once, when the tile is initialized.  This
         is important, since non-persistent input objects' resources will be
         cleaned up as soon as they are known not to be
         required.  To do this, region of interest information must be made
         available during parsing, which may occur prior to the point at which
         a tile is actually opened. */
    void withdraw_from_unloadable_list();
      /* Removes a tile, whose `is_unloadable' member is true, from the
         unloadable tiles list managed by `kd_codestream', leaving
         `is_unloadable' equal to false. */
    void add_to_unloadable_list();
      /* Adds a tile, whose `is_unloadable' member is false, to the
         unloadable tiles list managed by `kd_codestream', leaving
         `is_unloadable' equal to true. */
    float find_multicomponent_energy_gain(int comp_idx,
                                          bool restrict_to_interest,
                                          bool incorporate_qlim_weights);
      /* Finds the energy gain factor associated with the contribution of
         the indicated codestream image component to the output image
         components.  For this purpose, an impulsive distortion in codestream
         component `comp_idx' is considered, having magnitude equal to the
         component's nominal range.  The impact of this impulsive distortion
         is evaluated at the outputs of any multi-component transform, or
         Part-1 colour transform.  The resulting output distortion is
         expressed relative to each output component's nominal range and
         the sum of these squared normalized quantities is taken and returned.
         If `restrict_to_interest' is true, output distortion contributions
         are ignored if they are not currently of interest to the
         application.
            If `incorporate_qlim_weights' is true, the function incorporates
         the squares of the `qlim_sq_weight' values found in the relevant
         `kd_output_comp_info' entries into the energy gain term.  In this
         case, `restrict_to_interest' should alway be true, since only
         those components that are of interest are sure to have valid
         `qlim_sq_weight' entries when a `kdu_quality_limiter' is in force.
         The, in this case, is that the function computes the expression
         [\sum_c (W_c)^2 (M_cs)^2], as found in the detailed description of
         `kdu_quality_limiter' mathematics that accompanies the declaration
         of its constructor, where `comp_idx' is the s value here.  The
         function is called in this way only from within
         `set_elements_of_interest'.
      */
  public: // Links and Identification
    kd_codestream *codestream;
    int t_num; // Absolute tile-number, as used by parameter sub-system
    kdu_coords t_idx; // Absolute 2D tile-index
    kd_tile_ref *tile_ref; // Points to entry in `kd_codestream::tile_refs'
                           // which points to us, or NULL if we are not there.
    kd_tpart_pointer *tpart_ptrs; // Pointers for seekable sources
    kd_tile *in_progress_next, *in_progress_prev; // See below
    kd_tile *unloadable_next, *unloadable_prev; // See below
    kd_tile *typical_next; // Used to build a list of released typical tiles.
                           // Tiles on this list have `tile_ref'=NULL

  public: // Owned Resources
    kd_pp_markers *ppt_markers; // NULL except possibly in tpart header reading
    kd_pph_input *packed_headers; // NULL unless tile has packed packet headers
    kd_packet_sequencer *sequencer;
    kd_precinct_pointer_server precinct_pointer_server;
    kd_reslength_checker *reslength_checkers; // 1+`num_components' entries;
      // same structure as `kd_codestream::reslength_checkers' but for
      // tile-specific reslength constraints.
  
  public: // Dimensions and Parameters
    bool is_typical; // All typical tiles have the same structure & transform
    bool fully_typical; // All fully typical tiles have identical coding params
    int num_components; // Number of tile-components
    int num_layers;
    int num_apparent_layers;
    int max_relevant_layers; // See below
    kdu_long total_precincts; // Used only during code-stream generation
    kdu_long max_relevant_packets; // See below
    kdu_dims dims; // Region occupied by the tile on the canvas
    kdu_dims region; // Region of interest within tile.
    kdu_coords coding_origin;

  public: // Arrays and Lists
    kd_mct_stage *mct_head; // Linked list of MCT stages, starting from the one
    kd_mct_stage *mct_tail; // whose input components are the tile-components
    kd_tile_comp *comps;
    kdu_long structure_bytes; // Total bytes contained in all embedded
        // structures, excepting only precincts and code-block bit-stream
        // buffers.  Whenever this value changes, the changes should also be
        // reflected in the `codestream->buf_server' object, so that the total
        // memory load associated with tile data structures can be included in
        // cache threshold calculations.  This enables precinct and tile
        // unloading decisions to be made in a wholistic way, based on
        // total memory consumption.

  public: // Flags and other State Variables
    bool use_sop, use_eph, use_ycc;
    bool empty_shell; // True if tile header was not available when opened.
    bool is_in_progress; // See `in_progress_next' and `in_progress_prev'
    bool is_open; // A tile that is not open may be opened in the future,
                  // except where it is marked as `closed'.
    bool is_unloadable; // See below
    bool is_addressable; // See below
    bool closed; // Once closed, the tile may no longer be accessed.
                 // Persistent tiles are never marked as closed.  A closed tile
                 // will be passed to `release', but this might have to wait
                 // if it is currently the active tile for codestream parsing.
    bool initialized; // Fields below here are valid only once initialized
    bool needs_reinit; // Set inside `restart' to indicate the need for
                       // reinitialization when the tile is first accessed.
    bool insert_plt_segments; // If true, PLT marker segs introduced
    bool resolution_plts; // Start new PLT marker seg at resolution boundaries
    bool component_plts; // Start new PLT marker seg at component boundaries
    bool layer_plts; // Start new PLT marker seg at layer boundaries
    bool resolution_tparts; // Start new tile-part at resolution boundaries
    bool component_tparts; // Start new tile-part at component boundaries
    bool layer_tparts; // Start new tile-part at layer boundaries
    int max_tpart_interruptions; // From the `ORGtpart_interrupts' attribute
    int num_tparts; // Zero if number of tile-parts unknown.
    int next_tpart; // Index of next tile-part to be read or written.
    kdu_long sequenced_relevant_packets; // See below
    kdu_long saved_sequenced_packets; // See `kdu_packet_sequencer::save_state'
    int next_input_packet_num;
    int next_sop_sequence_num; // Valid only if `skipping_to_sop' is true.
    bool skipping_to_sop; // See notes below.
    bool exhausted; // True if no more tile-parts are available for this tile
  };
  /* Notes:
        The `max_relevant_layers' member indicates the maximum number of
     layers from the current tile which can ever be relevant to the
     application.  It holds the same value as `num_layers', unless the
     code-stream was created for reading and is non-persistent, in which
     case it holds the value of `num_apparent_layers'.  This member is used
     to avoid parsing any more packets than we have to when reading
     non-persistent code-streams.
        The `max_relevant_packets' member is equal to the total number of
     packets in the tile, unless the code-stream was created for reading,
     is non-persistent, and `kdu_codestream::apply_input_restrictions' has
     been used to reduce the spatial region of interest, or to identify one
     or more resolutions, components or layers as irrelevant to the
     application.  In that case it holds the product of the number of the
     `max_relevant_layers' value and the number of precincts which are
     actually relevant to the identify spatial region, resolution and
     components.
        Similarly, `sequenced_relevant_packets' counts the number of packets
     which have been sequenced (either written or read, as appropriate) so
     far, skipping only those packets which are deemed to be irrelevant to
     the application.  As for `max_relevant_packets', all packets are
     considered relevant if the code-stream was not created for reading, or
     if the code-stream is persistent, regardless of any calls to
     `kdu_codestream::apply_input_restrictions'.  When this value reaches
     the value of `max_relevant_packets', there is no need to parse any
     further contents from the tile.
        If `skipping_to_sop' is true, packets are automatically discarded
     (this is a pseudo-transfer which leaves the packet empty) until one is
     reached whose packet number agrees with the value of
     `next_sop_sequence_num'.  To assist this operation, the
     `next_input_packet_num' counter keeps track of the zero-based index
     of the next packet to be parsed.  Note that we need a separate counter,
     in addition to `sequenced_relevant_packets', since not all packets
     which are parsed need necessarily be relevant to the application.
        A tile is said to be "unloadable" once the following conditions have
     been satisfied:
     1) the codestream is in persistent mode;
     2) the compressed data source is seekable or a cache;
     3) this tile is not currently open;
     4) this tile is not the one referenced by `kd_codestream::active';
     Such tiles are enterered on a list managed by the `unloadable_next'
     and `unloadable_prev' members, and have their `is_unloadable' flag set
     to true.  When an unloadable tile is deleted, the corresponding
     `kd_tile_ref::tile' entry is set to NULL rather than `KD_EXPIRED_TILE'.
     This ensures that the tile can be re-parsed from the compressed data
     source if required again at a later point.  Tiles on the unloadable
     list may be deleted automatically, according to a tile memory management
     policy.
        A tile is said to be "addressable" if the following conditions hold:
     1) the codestream is in input (reading) mode;
     2) the compressed data source is not a cache;
     3) the compressed data source is seekable; and
     4) the current tile-part contains usable precinct pointer information.
     Such tiles are identified by the `is_addressable' flag.  This flag is
     set on each call to `read_tile_part_header'.  All that actually
     matters is that we correctly identify when the codestream has an
     active tile which is not addressable; in that case, the tile's
     packets must be desequenced up until the end of the tile-part before
     any seeking on the compressed data source can be allowed.
        A tile is said to be "in-progress" if the following conditions
     apply:
     1) the codestream was created for output;
     2) the tile has already been opened -- it may or may not have been closed;
     3) the tile's contents have not yet been fully flushed to the codestream.
     In-progress tiles are entered onto a doubly-linked list managed by the
     `in_progress_next' and `in_progress_prev' pointers.  The head and
     tail of this list are managed by `kd_codestream::tiles_in_progress_head'
     and `kd_codestream::tiles_in_progress_tail'. */ 

/*****************************************************************************/
/*                              kd_tile_comp                                 */
/*****************************************************************************/

struct kd_tile_comp { // State structure for the "kdu_tile_comp" interface
  public: // Member functions
    kd_tile_comp()
      { // We will be relying on all fields starting out as 0.
        memset(this,0,sizeof(*this));
        G_tc = G_tc_restricted = -1.0F;
      }
    ~kd_tile_comp();
    void initialize_kernel_parameters(int atk_idx, kdu_kernels &kernels);
      /* On entry, the `kernel_id' member is valid.  On exit, the
         other `kernel_xxx' parameters are valid.  The `atk_idx' value is
         used only if `kernel_id' is equal to `Ckernels_ATK'.  The
         function leaves `kernels' initialized with the relevant transform
         kernel, for further use by the caller if desired. */
    void reset_layer_stats()
      {
        if (layer_stats != NULL)
          {
            int num_entries = ((1+dwt_levels)*tile->num_layers)<<1;
            memset(layer_stats,0,(size_t)(sizeof(kdu_long)*num_entries));
          }
      }
  public: // Links and Identification
    kd_codestream *codestream;
    kd_tile *tile;
    kd_comp_info *comp_info; // Ptr to relevant `codestream->comp_info' entry
    kd_nlt_info *nlt_info; // non-NULL if tile has no MCT list and this comp
                           // has an associated non-linear point transform.
    int cnum;

  public: // Dimensions and Parameters
    kdu_coords sub_sampling;
    kdu_dims dims;
    kdu_dims region;
    int dwt_levels, apparent_dwt_levels;
    bool reversible;
    int kernel_id; // Ckernels_W5X3, Ckernels_W9X7 or Ckernels_ATK
    bool kernel_symmetric, kernel_symmetric_extension;
    int kernel_num_steps;
    kdu_kernel_step_info *kernel_step_info, *kernel_step_info_flipped;
    float *kernel_coefficients, *kernel_coefficients_flipped;
    float kernel_low_scale; // Amount to scale low-pass lifting analysis output
    float kernel_high_scale; // Amount to scale hi-pass lifting analysis output
    int low_support_min, low_support_max; // low-pass synthesis impulse support
    int high_support_min, high_support_max; // high-pass synth impulse support
    float G_tc; // Global multi-component energy gain term; -ve if not known
    float G_tc_restricted; // Multi-component energy gain term based only on
            // those output components currently of interest; -ve if not known
    int recommended_extra_bits;
    kdu_coords blk; // Nominal code-block dimensions.
    int modes; // Block coder modes.  Flags defined in scope `cod_params'

  public: // Arrays
    kd_resolution *resolutions; // Contains 1+`dwt_levels' entries.
    kdu_long *layer_stats; // Contains `num_layers' pairs per resolution,
      // starting from the lowest resolution (`res_level'=0); each pair holds
      // number of parsed packets for the layer-resolution, followed by
      // number of parsed packet bytes for the layer-resolution.  Available
      // only for input codestreams; populated by `kd_precinct::read_packet'.

  public: // Flags and Other State Variables
    bool enabled; // See below
    bool is_of_interest; // See below
    kdu_int32 qlim_type_flags; // Set by `kd_tile::set_elements_of_interest' if
                               // `kdu_codestream::limiter' is non-NULL.
    kdu_coords grid_min; // These fields are used by "kd_packet_sequencer"
    kdu_coords grid_inc; // for spatially oriented progressions.
    kdu_coords saved_grid_min; // For "kd_packet_sequencer::save_state"
    kdu_coords saved_grid_inc; // For "kd_packet_sequencer::save_state"
  
  public: // State variables for auto-flush processing
    kdu_interlocked_int32 completed_subband_counter; // This is a down-counter
  };
  /* Notes:
        `recommended_extra_bits' may be added to the sample bit-depth
     (precision) for this component to determine an appropriate number
     of bits for internal representations associated with the data
     processing path.
        In the reversible path, the value is set to the maximum bit-depth
     expansion factor, as reported in Table 17.4 of the book by Taubman and
     Marcellin, adding an extra 1 bit if the RCT has been used -- note that it
     is not desirable for luminance and chrominance components to have
     different numeric representations, since that would significantly
     complicate the colour transform procedure.
        In the irreversible path, the value is set to 7, which means
     that 16-bit representations are judged sufficient for 9 bit
     imagery or less.  Of course, the impact of selecting a reduced
     representation precision is only reduced accuracy in the
     irreversible processing path.
        The `enabled' flag is used to efficiently identify the tile-components
     which are enabled.  Remember that a tile-component may be disabled,
     either because the corresponding codestream image component is not
     apparent (only when the `kd_codestream::component_access_mode' is
     `KDU_WANT_CODESTREAM_COMPONENTS'), or because the tile-component
     is not involved in the construction of an apparent output image
     component (when the `kd_codestream::component_access_mode' is
     `KDU_WANT_OUTPUT_COMPONENTS').  In the latter case, all codestream
     image components are apparent, but disabled ones cannot be
     accessed -- this is important, since in the latter case the set
     of enabled components may vary from tile to tile.
        The `is_of_interest' flag is used to store the identify of components
     which are marked as being of interest, by calls to
     `kdu_tile::set_components_of_interest'.  This is done only when the
     component access mode is `KDU_WANT_CODESTREAM_COMPONENTS' or the
     `mct_head' member is NULL.  Otherwise, the components of interest are
     passed directly to the `kd_mct_stage::apply_output_restrictions'
     function.  Note that the `is_of_interest' flags do not generally
     belong to the component in which they are stored.  They are just
     copies of the information supplied to
     `kdu_tile::set_components_of_interest', without any attempt to apply
     any component index permutations which might be relevant. */

/*****************************************************************************/
/*                            kd_precinct_ref                                */
/*****************************************************************************/

  /* Objects of this class provide the vehicle through which precinct
     references are instantiated and removed from the array of precinct
     references maintained by the `kd_resolution' object.  It is important
     that the state information be small.  In particular, it has the same
     size as `kdu_long', which should never be smaller than the size of
     a memory pointer, but may be larger -- see the declaration of
     `kdu_long' in "elementary.h".  For precincts whose seek addresses are
     deduced from PLT marker segments in the code-stream, or precincts which
     are maintained by a caching data source, this object provides facilities
     for recycling a disused precinct's resources while keeping its
     unique address around for rapid reloading in the event that it
     is required at a later point.
        In multi-threaded environments, it is worth noting that for
     input codestreams, precincts are created, deactivated and reactivated
     under the protection of the `KD_THREADLOCK_GENERAL' mutex, while ouput
     and interchange codestreams create precincts under the protection of the
     `KD_THREADLOCK_PRECINCT' mutex; precincts can be closed/released under
     the protection of either lock or even without taking out any locks.  For
     more on how this is managed, consult the `kd_precinct_size_class' object
     as well as the implementation of `kd_precinct::closing'.
   */

class kd_precinct_ref {
  public: // Member functions
    kd_precinct_ref()
      { state = 0; }
    ~kd_precinct_ref()
      { if (!((state == 0) || (state & 1))) close(NULL); }
    bool parsed_and_unloaded()
      { return ((state & 3) == 3) && (state != 3); }
      /* Returns true if the precinct is addressable (i.e., dynamically
         loadable) and one or more packets have previously been read, after
         which it was unloaded from memory (via `kd_precinct::close'). */
    kd_precinct *deref()
      { /* Returns NULL unless the reference idenifies a valid `kd_precinct'
           object, which is already loaded in memory. Note that this function
           is often invoked without mutex protection.  It is used to determine
           whether a valid precinct reference exists already or one must
           be created using `kd_precinct_ref::open', which should be protected
           in a multi-threading environment.  To avoid possible race
           conditions, we inform the compiler that the `state' variable should
           be treated as volatile here, so that it is only read once. */
        volatile kdu_long *state_ref = &state;
        register kdu_long state_val = *state_ref;
        if (state_val & 1) return NULL;
        return (kd_precinct *) _kdu_long_to_addr(state_val);
          // Note: on a 32-bit machine, only the 32 LSB's of `state_val' are
          // relevant to the return value.
      }
    kd_precinct *active_deref();
      /* Returns NULL unless the reference identifies a valid `kd_precinct'
         object, which is already loaded in memory and is in the active
         state. */
    kd_precinct *open(kd_resolution *res, kdu_coords pos_idx,
                      bool need_activate, kdu_thread_env *env);
      /* Attempts to open the precinct at the indicated position within the
         array of precincts maintained by the indicated resolution object.
         `pos_idx' holds the horizontal and vertical position of the
         precinct (starting from 0) within the array of precincts for
         the given resolution.  That is, `pos_idx' holds a relative, not
         an absolute precinct index.
            If the precinct is known to be unloadable (this happens if the
         precinct has previously been opened and has then been permanently
         closed), the function returns NULL.
            If the precinct currently exists in memory (the `state' variable
         contains a valid pointer to the precinct), the function returns a
         pointer to that precinct; however, if `need_activate' is true
         and the precincts's `released' flag is true, `kd_precinct::activate'
         is called before returning the `kd_precinct' pointer.  When the
         codestream object was created for input or for interchange, the
         `need_activate' argument should be set to true only when the
         precinct is being opened in order to access code-block data.  In
         that case, the precinct must belong to an open tile and lie within
         the application's current region of interest, meaning that it will
         be released again when the tile is closed.  For codestream objects
         created for output, the argument should have no effect, since
         output precincts are not actually released.
            Otherwise, the function obtains a new `kd_precinct' object from
         the code-stream's precinct server and calls its `initialize'
         function.  If the `state' variable held the precinct's unique
         address value on entry, that address is written into the newly
         instantiated `kd_precinct' object's `unique_address' field, and
         the P bit (see notes on `state' below) is copied to the precinct's
         KD_PFLAG_WAS_READ flag. If compressed data comes from a cached source,
         or the codestream object was created for interchange (no compressed
         data sources or targets), the newly created precinct is automatically
         marked as addressable, with its `unique_address' set to the
         precinct's unique identifier, as described in the comments appearing
         with the declaration of `kd_precinct::unique_address'.  For
         precincts which can be addressed using a seek address (for seekable
         compressed data sources), the seek address is installed using
         `set_address' from within the `kd_packet_sequencer::next_in_sequence'
         function.
            It is worth noting that this function may cause other precincts
         to be unloaded from memory, in order to conserve memory resources.
         If this happens, only precincts which have been released and
         which do not intersect with the current region of interest will be
         unloaded. */
    void close(kdu_thread_env *env);
      /* Closes the active `kd_precinct' object whose pointer is in
         `state', calling its `closing' member function and recycling its
         storage.  If the precinct is marked as addressable, its
         `unique_address' member and KD_PFLAG_READ flag are recorded in
         `state' using the convention outlined in the notes below, allowing
         the precinct to be re-opened at a later point.
            In many cases, you are better off calling the `release' member
         function instead of `close' (see below).
            Note that this function may be safely invoked even when the
         precinct is not currently active -- it does nothing unless `state'
         holds a valid precinct address. */
    void close_and_reset();
      /* This function is used only by `kdu_tile::parse_all_relevant_packets'
         and then only if it is asked to parse everything from scratch.  The
         behaviour is similar to calling `close', except that the internal
         `state' record is left with its P flag reset, so that when the
         precinct is reloaded, its packet statistics will be entered into
         the `kd_tile_comp::layer_stats' array -- the caller invariably
         resets this array. */
    void release(kdu_thread_env *env);
      /* This function should be called once a precinct is no longer
         required, in preference to the `close' function.  If the system is
         running low on resources, the function may unload the precinct from
         memory to recycle its resources.  Alternatively, the function is at
         liberty to leave the precinct in memory, moving it to a list of
         inactive precincts which are subject to unloading at some point in
         the future, when resources may be running low.  Non-addressable or
         cached precincts are unloaded immediately, since they are
         guaranteed never to be re-opened once `release' has been called. */
    void clear();
      /* This function recycles the storage associated with any existing
         precinct and sets the internal `state' variable to 0.  It is called
         when the code-stream management machinery is restarted. */
    bool is_desequenced();
      /* This function is intended for use within
         `kd_packet_sequencer::next_in_sequence', for checking whether
         or not the precinct which is returned by that function needs
         to have its contents read from a compressed data source in
         order to desequence a new packet.  Opening precincts can be a time
         consuming task, especially when there are an enormous number of
         precincts (this can happen when working with huge untiled images,
         especially in geospatial applications).  This function takes
         advantage of the fact that there is no need to actually open a
         precinct which is not already loaded into memory.  The fact that this
         function is called first by the packet desequencer, allows a precinct
         which has already been fully generated to be closed immediately
         during packet generation mode.
            The function works as follows.  If the precinct is already loaded
         in memory, its `sequenced' flag is accessed directly.   Otherwise,
         if the reference `state' variable identifies a valid unique
         address for the precinct, it is known to be addressable and
         therefore must have already been fully desequenced.  Also, if the
         `state' variable indicates that the precinct is unloadable, or that
         it has been closed in packet generation mode, it is interpreted as
         sequenced, in the sense that the application cannot sequence further
         packets for the precinct.  Otherwise, the `state' variable must hold
         0 (see below), from which we deduce that the precinct has not yet
         been sequenced. */
    bool set_address(kd_resolution *res, kdu_coords pos_idx,
                     kdu_long seek_address);
      /* Called from within `kd_packet_sequencer::next_in_sequence', this
         function installs a seek address for a precinct whose packets are
         contiguous within the code-stream and have valid pointer information
         stored in PLT marker segments.  Subsequent calls to `is_sequenced'
         will return true.  The precinct itself may either be open or closed
         when this function is called.  The `pos_idx' argument is provided
         for computing the relevance of this precinct to the application.  It
         holds a relative precinct index, having the same interpretation as
         that passed to `kd_precinct::initialize' and `kdu_precinct_ref::open'.
            It can happen that this function causes the tile, and hence the
         precinct reference, to be destroyed.  This happens if the call to
         this function renders the resolution or tile completely parsed,
         and it is known that the application has no need for any further
         access to the tile.  In this case, and only in this case, the
         function returns false, meaning that the caller must not make
         any further attempt to access the tile or any of its resources. */
  private: // Utility functions
    kd_precinct *instantiate_precinct(kd_resolution *res, kdu_coords pos_idx,
                                      kdu_thread_env *env);
      /* Called from `open' if the `kd_precinct' object is not in memory at
         the time and can legitimately be created at this point.  This
         function needs to be careful to set the `state' member only once
         all members of the precinct have been fully initialized. */
  private: // Data
    kdu_long state;
  };
  /* Notes:
        `state' holds 0 if no information is yet available concerning the
     precinct associated with this reference.
        If the precinct is currently active in memory, `state' holds the
     address in memory (pointer) of the `kd_precinct' structure.
        If the precinct is known to be addressable, `state' holds an odd
     integer, equal to 4*A + 2*P + 1, where A is the precinct's
     `unique_address' value and P is a flag, equal to 1 if the precinct
     has been loaded and parsed previously.
        The `unique_address' value has the same definition as the
     corresponding data member in `kd_precinct'.
        This P flag plays the same role as the KD_PFLAG_WAS_READ bit in
     `kd_precinct::flags'.  It is used to avoid collecting layer size
     statistics twice for the same precinct which could otherwise happen
     when precincts get swapped in and out of memory on demand.
        A value of `state'=1 means that the precinct can be re-instantiated,
     but that it is known to contain no packets whatsoever.
        If a previously active precinct's resources have been recycled and
     re-loading is not possible, `state' is assigned the special value of 3. */

/*****************************************************************************/
/*                               kd_leaf_node                                */
/*****************************************************************************/

struct kd_leaf_node {
    public: // Data
      kd_node *parent; // Every leaf node has a non-leaf parent node
      kd_resolution *resolution; // Always points to the owning resolution
      kdu_dims dims; // Dimensions & location of the image at this node
      kdu_dims region; // See below
      kdu_byte branch_x, branch_y; // See below
      bool is_leaf; // False if this object is the base of `kd_node'
  };
  /* Notes:
        This structure is the base of the richer `kd_node' structure.  The
     key difference is that leaf nodes have no children, so we do not waste
     storage recording pointers to them.
        The `branch_x' and `branch_y' members describe how this node is
     derived from its parent node, if any.  `branch_x' holds 0 if horizontal
     low-pass filtering and decimation were used to obtain the node from its
     parent; it holds 1 if horizontal high-pass filtering and decimation were
     used; it holds 2 if no filtering or decimation were involved in the
     horizontal direction.  `branch_y' plays a similar role in describing the
     vertical filtering and decimation processes.  If `parent' is NULL,
     the `branch_x' and `branch_y' members have no meaning.
        This structure manages two different, yet related regions
     (location, plus dimensions) which deserve some explanation here.
     * The `dims' member serves the usual role of identifying the location and
       dimensions of the image, as it appears on entry into this node,
       during forward transformation (analysis).  For primary nodes,
       these are the dimensions and location of the tile-component-resolution
       within its canvas.
     * `region' identifies a subset of the `dims' region, containing
       all of the node's samples which are involved in the synthesis of the
       current region of interest, as specified in the most recent call to
       `kdu_codestream::apply_input_restrictions'. */

/*****************************************************************************/
/*                                 kd_node                                   */
/*****************************************************************************/

struct kd_node : public kd_leaf_node {
    public: // Member functions
      void adjust_cover(kdu_dims child_cover,
                        int child_branch_x, int child_branch_y);
        /* This function is used to find the `region_cover' members.  In
           a downward sweep through the decomposition branches associated
           with a given resolution level, the `region_cover' members are all
           set to empty regions.  We then assign each leaf node (subband)
           a region cover (not actually recorded in the leaf node) equal to
           its region of interest and work back up the tree from the
           leaves to the primary node of the resolution in question, calling
           this function to grow the region cover so as to encompass the
           smallest region which covers each of its children. */
    public: // Data
      kdu_dims region_cover; // See below
      kdu_dims prec_dims; // Temp storage for calculating precinct dimensions
      kd_leaf_node *children[4]; // Indexed by `LL_BAND' through `HH_BAND'
      kdu_byte num_hor_steps, num_vert_steps; // See below
      kdu_uint16 num_descendant_nodes;
      kdu_uint16 num_descendant_leaves;
      float *bibo_gains; // See below
  };
  /* Notes:
        This object is used to construct descriptions of all non-leaf
     nodes in the DWT decomposition structure.  Each node in the structure
     may be decomposed horizontally, vertically or both.  If it is decomposed
     in the horizontal direction, `children[HL_BAND]' will be non-NULL.  If it
     is decomposed in the vertical direction, `children[LH_BAND]' will be
     non-NULL.  If it is decomposed in both directions, `children[HH_BAND]'
     will also be non-NULL.  If it is decomposed at all, `children[LL_BAND]'
     will always be non-NULL.
        The decomposition structure consists of a collection of primary
     nodes, each of which is embedded within a `kd_resolution' object.
     In the case of the simple Mallat structure, each primary node has
     4 children, with the LL child embedded within the next lower
     resolution object and the remaining children embedded within
     `kd_subband' objects.  In wavelet packet decomposition structures,
     detail subbands produced by a primary node may, themselves, be
     decomposed further into subbands.
        The actual object pointed to by each non-NULL member of the `children'
     array may need to be cast to type `kd_node', rather than `kd_leaf_node'
     if the `kd_leaf_node::is_leaf' member is false.
        The `children[LL_BAND]' entry of a primary node always points to
     the node which is embedded within the next lower resolution
     `kd_resolution' object, except at the lowest resolution of all.
     The lowest `kd_resolution' object's embedded node has only one
     child, with `children[LL_BAND]' pointing to the single (LL) subband
     belonging to that resolution.
        The `num_hor_steps' and `num_vert_steps' members identify the number
     of horizontal and vertical lifting steps associated with the horizontal
     and vertical transform operations applied at this node to produce its
     children.  One or both of these may be 0, either because the transform
     is trivial (lazy wavelet) or because the node is split only in the
     horizontal direction, only in the vertical direction, or is not split
     at all.
        The `num_descendant_nodes' member holds a count of the total number
     of nodes (leaf nodes and non-leaf nodes) which may be reached via the
     `children' array.  The `num_descendant_leaves' member holds a count of
     the number of these descendant nodes which are leaves.  Neither of these
     counts includes the current node.  Clearly there is no need to
     maintain this information with leaf nodes, since the counts would both
     be 0 in this case.
        The `bibo_gains' member point to an array with
     2+`num_hor_steps'+`num_vert_steps' entries.  The first 1+`num_hor_steps'
     entries describe BIBO gains for the horizontal transform performed
     at this node.  The remaining `num_vert_steps' entries describe BIBO
     gains for the vertical transform performed at this node.  Within each
     of these 2 sets of BIBO gains the first entry holds the BIBO gain from
     the original input image to the image which is presented at the input
     to this node.  Subsequent entries hold the BIBO gains from the original
     input image to the image which is presented at the output of each
     successive analysis lifting step.  All BIBO gains are assessed relative to
     the nominal transform normalization, in which reversible lifting steps
     are applied as-is, while irreversible lifting steps are followed by the
     relevant `kd_tile_comp::low_scale' and `kd_tile_comp::high_scale' values.
        The interpretation of `region_cover' is similar to that of
     `kd_leaf_node::region', with a subtle, yet important twist.  In order to
     reconstruct each sample in `region', it is necessary to decode a
     particular set of samples from the subbands which are associated with
     this node.  Each of these subband samples has a mapping onto the
     coordinate system associated with the node, following the usual rules of
     the canvas coordinate system.  `res_cover' holds the smallest region
     which contains all of these mapped subband sample locations, except that
     the LL subband branch is included only if this is not a primary node.
     This ensures that the primary node embedded inside each `kd_resolution'
     object will have a `region_cover' which spans the nominal locations of
     all subband samples of interest, which are represented by codestream
     packets associated with that resolution. */

/*****************************************************************************/
/*                                kd_subband                                 */
/*****************************************************************************/

struct kd_subband : public kd_leaf_node {
    // State structure for the "kdu_subband" interface
  public: // Identification
    kdu_int16 descriptor; // See `cod_params::expand_decomp_bands'
    kdu_byte orientation; // One of LL_BAND, HL_BAND, LH_BAND or HH_BAND.
                     // This is the orientation of the primary subband from
                     // which we are derived within the same resolution level.
    kdu_byte sequence_idx; // Index within the `kd_resolution::subbands' array
                     // This is also the order in which subbands contribute
                     // code-blocks to codestream packets.
    kdu_byte transpose_sequence_idx; // Index of the real subband which appears
                      // to have this `sequence_idx' value when
                      // `kdu_codestream::change_appearance' has been used to
                      // transpose the codestream's appearance.
  public: // Dimensions and Parameters
    kdu_byte epsilon; // Ranging parameter.
    kdu_byte K_max; // Maximum magnitude bit-planes, not including ROI upshift
    kdu_byte K_max_prime; // Max magnitude bit-planes, including ROI upshift
    kdu_byte qlim_max_passes; // Max passes to transfer based on quality limits
    float delta; // Step size, normalized for image data range of -0.5 to 0.5
    float G_b; // Subband energy gain factor.
    float W_b; // Subband amplitude weight; needs squaring to get energy weight
    float vis_floor; // Non-zero if visual masking models to be used
    float vis_scale; // 1/(W_b*R_b). R_b = 1 (irrev); 2^(B+h) (rev), where
                     // B=bit-depth, h=#high-pass analysis stages.
    float roi_weight; // Extra amplitude weight for ROI foreground blocks.
    kdu_dims block_partition; // Holds the coding origin and dimensions.
    kdu_dims block_indices; // Holds the range of valid block indices.
    kdu_dims region_indices;  // Same as `block_indices', but restricted to
                              // blocks which overlap the region of interest.
    kdu_coords blocks_per_precinct; // precinct size / block size
    kdu_coords log2_blocks_per_precinct; // log2 of precinct size/block size
  public: // Background processing state
    kdu_thread_queue *notify_queue; // See `kdu_subband::attach_block_notifier'
    int notify_quantum_bits; // From `kdu_subband::attach_block_notifier'
    kdu_interlocked_int32 bkgnd_state; // See below
    kdu_int32 pending_bkgnd_state; // See below
  };
  /* Notes:
        It may come as a surprise to notice that this object has no
     direct connection to code-blocks.  To access a code-block, the object
     first determines the precinct to which it belongs, creating that
     precinct if necessary; this, in turn, creates its precinct-bands and
     the associated code-blocks.  In this way, no memory whatsoever need be
     dedicated to code-blocks which lie in unaccessed precincts.
        The `notify_queue', `bkgnd_state' and `pending_bkgnd_state' members
     are used to implement `kdu_subband::attach_block_notifier' and
     `kdu_subband::advance_rows_needed'.  The `pending_bkgnd_state' is
     adjusted only from within the background processing job, apart from
     being reset during tile initialization and close events.  Each time
     `kd_resolution::advance_available_precinct_rows' is called, it
     propagates the impact of newly available precinct "rows" to the
     individual subbands, incrementing `pending_bkgnd_state' by the number
     of newly available code-block "rows" and setting the MSB of this
     member variable if all code-block rows are available.
        The `bkgnd_state' member has the form S+4*R, where S is a 2-bit state
     variable and R is the "required rows" accumulator.  Specifically,
     -- S=0 means that the `notify_queue' is either not valid or not
        "live" in the sense discussed in the comments following
        `kd_resolution'.  Not being live means that the final notification
        message has been delivered to the queue or is about to be delivered
        once the background thread gets around to it.
     -- S=1 means that the `notify_queue' is live; this state is set up by
        `kdu_subband::attach_block_notifier' and remains until the
        `kdu_subband::detach_block_notifier' function is called, or the
        final notification message is delivered to the `notify_queue'.
     -- S=2 means that the `kdu_subband::detach_block_notifier' function
        has been called, requesting premature detachment of a `notify_queue'
        that was live at the time.  The actual detachment will not be
        completed until a special notification message is delivered from
        within the `kd_resolution::do_background_processing' function.
     -- S=3 is a reserved value that currently has no meaning and should
        never occur.
     -- The value of R is equal to `max_Rp' - `Rp', where `max_Rp' is the sum
        of the `delta_rows_needed' values passed to
        `kdu_subband::advance_block_rows_needed' and `Rp' is the number of
        code-block "rows" that the `notify_queue' should be allowed to
        "consider" available.  Those code-block "rows" that are available but
        cannot yet be considered available are stored in `pending_bkgnd_state',
        as mentioned above.  Currently, our policy is to decrement R
        (increment Rp) by 1 and decrement `pending_bkgnd_state' by 1, so long
        as this leaves R >= 0 and (`pending_bkgnd_state' & 0x7FFFFFFF) >= 0,
        each time a subband is serviced, scheduling a future service event
        if R and `pending_bkgnd_state' & 0x7FFFFFFF both still have non-zero
        values.
     Note that in the above, by "rows" we actually mean whole rows of
     code-blocks as seen from the `kdu_subband' interface.  If the
     `kdu_codestream::change_appearance' function has been used to set a
     transposed geometry, these are actually columns.  Similarly, if the
     relevant dimension has been flipped, these rows are measured from the
     bottom (resp. right) rather than the top (resp. left) of the subband. */

/*****************************************************************************/
/*                              kd_resolution                                */
/*****************************************************************************/

#define KD_RESOLUTION_BKGND_SCHEDULED           ((kdu_int32) 1)
#define KD_RESOLUTION_BKGND_PROGRESS            ((kdu_int32) 2)
#define KD_RESOLUTION_BKGND_LIVE_QUEUES_1       ((kdu_int32) 4)
#define KD_RESOLUTION_BKGND_LIVE_QUEUES_MASK    ((kdu_int32)(63<<2))
#define KD_RESOLUTION_BKGND_BLOCKING_1          ((kdu_int32) 256)
#define KD_RESOLUTION_BKGND_BLOCKING_MASK       ((kdu_int32)(-256))

struct kd_resolution { // State structure for the "kdu_resolution" interface
  public: // Member functions
    kd_resolution()
      { 
        codestream = NULL; tile_comp = NULL; rescomp = NULL;
        precinct_refs = NULL; subbands = subband_handle = NULL; num_subbands=0;
        num_intermediate_nodes = 0; intermediate_nodes = NULL;
        can_flip = true;  node.bibo_gains = NULL;
        precinct_rows_available=0; bkgnd_state.set(0); bkgnd_next.set(NULL);
        pending_notify_queue = NULL;  pending_p_delta = 0;
      }
    ~kd_resolution()
      {
        if (node.bibo_gains != NULL) delete[] node.bibo_gains;
        for (int n=0; n < (int) num_intermediate_nodes; n++)
          if (intermediate_nodes[n].bibo_gains != NULL)
            delete[] intermediate_nodes[n].bibo_gains;
        if (precinct_refs != NULL) delete[] precinct_refs;
        if (subband_handle != NULL) delete[] subband_handle;
        if (intermediate_nodes != NULL) delete[] intermediate_nodes;
      }
    void build_decomposition_structure(kdu_params *coc, kdu_kernels &kernels);
      /* This function creates the `subbands' and `intermediate_nodes'
         arrays and fills in all the relevant pointers.  It uses `kernels'
         to create and fill in the `kdu_node::hor_bibo_gains' and
         `kdu_node::vert_bibo_gains' arrays, where required. */
    void complete_initialization();
      /* Called from within `kd_tile::initialize' to finish the initialization
         process, after all of the key parameters have been set.  Currently,
         the only task performed here is the initialization of the
         `max_blocks_per_precinct' member. */
    void reserve_background_detach()
      { bkgnd_state.exchange_add(KD_RESOLUTION_BKGND_BLOCKING_1); }
      /* Called only from `kdu_subband::detach_block_notifier', this function
         increments an internal count of the number of threads that are
         blocking new calls to `do_background_processing' from doing anything.
         On entry, if a `do_background_processing' call finds that there are
         one or more blockers, it returns immediately.  Moreover, if there
         are one or more blockers, `schedule_background_progress' calls
         do not actually schedule any new calls to `do_background_processing',
         although they do update the internal record of the fact that
         background processing is required.  The number of blockers
         is reduced only when `schedule_background_detach' is called.
            The idea is that a thread invokes this function if it is about to
         record a request within some subband for a deferred detachment
         notification, after which it would normally need at the very least
         to check whether or not the object needs to be appended to the
         background service queue in order to ensure that the request gets
         processed and the detachment notification message gets delivered to
         the relevant notify queue.  However, this sequence of operations
         runs the risk that a call to `do_background_processing'
         picks up the detachment notification request and processes it,
         before the thread that recorded it has yet checked whether or not
         the resolution object needs to be appended to the background service
         queue.  This is only a problem if the detachment notification belongs
         to the last live notification queue for the resolution, because it
         could cause a chain of events to be set in progress that will result
         in another thread calling `kdu_tile::close' before the thread that
         requested the notification gets a chance to check whether or not
         the resolution object needs to be appended to the background service
         queue -- testing for such a need alone is dangerous since a call to
         `kdu_tile::close' may have deleted or recycled the memory associated
         with the present object!
            The solution to the above problem is as follows.  The final
         notification message for the last live subband notify queue in the
         resolution is always deferred until a subsequent call to
         `do_background_processing', if another such call has been scheduled
         or if there are any blockers (created by this function).  This
         ensures that this notification call cannot take place at least until
         after the thread that requested the detachment notification invokes
         `schedule_background_detach', which is the last thing that it will
         do before returning. */
    bool schedule_background_detach()
      /* As explained above, this function decrements the count of background
         servicing "blockers" that was incremented by a prior call to
         `reserve_background_detach'.  If appropriate, this causes the
         current object to be appended to the background serving queue so
         that processing of the detachment request can be completed.
         Specifically, if the number of blockers reduces to 0 and the object
         is not currently on the background service queue, it is added to
         the queue so that at least one more call to `do_background_processing'
         can occur. The function returns false if the necessary processing
         will eventually occur all by themselves (no further action required
         by the caller).  The function returns true if the caller needs to
         invoke `kd_cs_thread_context::schedule_resolution_processing'
         to be sure of this happening. */
      { 
        kdu_int32 old_state, new_state;
        do { // Enter compare-and-set loop
          old_state = bkgnd_state.get();
          assert(old_state & KD_RESOLUTION_BKGND_BLOCKING_MASK);
          new_state = old_state - KD_RESOLUTION_BKGND_BLOCKING_1;
          if (!(new_state & KD_RESOLUTION_BKGND_BLOCKING_MASK))
            new_state |= KD_RESOLUTION_BKGND_SCHEDULED;
        } while (!bkgnd_state.compare_and_set(old_state,new_state));
        if (!((old_state ^ new_state) & KD_RESOLUTION_BKGND_SCHEDULED))
          return false; // No need to schedule resolution processing
        codestream->thread_context->append_to_res_queue(this);
        return true;        
      }
    bool schedule_background_progress()
      /* This function is used to schedule a background servicing call for
         the present object.  If the object is already scheduled for
         servicing, or if the number of live notify queues associated with
         the object has gone to 0, the function returns false immediately
         (no further action required by the caller).  Also, if the function
         finds that the count of blockers created by other threads calling
         `reserve_background_detach' is non-zero, it also returns false,
         because there is guaranteed to be a later call to
         `schedule_background_detach' that will ensure a background servicing
         call to `do_background_processing' eventually occurs.  Otherwise,
         the function returns true, meaning that this function has appended
         the object to the background servicing queue, but the caller may
         need to invoke `kd_cs_thread_context::schedule_resolution_processing'
         to ensure that the servicing actually takes place, unless the
         function is being invoked from within the background job itself. */
      { 
        kdu_int32 old_state, new_state;
        do { // Enter compare-and-set loop
          old_state = new_state = bkgnd_state.get();
          if ((old_state & KD_RESOLUTION_BKGND_LIVE_QUEUES_MASK) != 0)
            { 
              new_state |= KD_RESOLUTION_BKGND_PROGRESS;
              if (!(old_state & KD_RESOLUTION_BKGND_BLOCKING_MASK))
                new_state |= KD_RESOLUTION_BKGND_SCHEDULED;
            }
        } while (!bkgnd_state.compare_and_set(old_state,new_state));
        if (!((old_state ^ new_state) & KD_RESOLUTION_BKGND_SCHEDULED))
          return false; // No need to schedule more resolution processing
        codestream->thread_context->append_to_res_queue(this);
        return true;
      }
    void reset_background_processing()
      { /* Called from `kd_tile::close' wherever necessary. */
        bkgnd_state.set(0);  precinct_rows_available = 0;
        pending_notify_queue = NULL;  pending_p_delta = 0;
        for (int b=0; b < num_subbands; b++)
          { subbands[b].notify_queue = NULL;
            subbands[b].bkgnd_state.set(0);
            subbands[b].pending_bkgnd_state = 0;
            subbands[b].notify_quantum_bits = 0; }
      }
    void do_background_processing(kdu_thread_env *env);
      /* Services this object from within the background processing job,
         some time after the `schedule_background_processing' function
         was used to schedule the operation.  The first thing this
         function does is to reset the scheduling flags D, P and I in the
         `bkgnd_state' variable (see notes below), using the previous
         values of these flags to determine what processing is required.  The
         function is effectively called in a single threaded environment,
         since there is exactly one background processing job that can call
         this function -- for input codestreams, the function is invoked
         from the `kd_cs_thread_context::glock_job' function, while for
         output and interchange codestreams, the function is invoked from
         within `kd_cs_thread_context::plock_job'.  The function may need
         to call `advance_precinct_rows_available, in which case the
         `KD_THREADLOCK_GENERAL' or `KD_THREADLOCK_PRECINCT' mutex is locked
         and released, as required. */
     void advance_precinct_rows_available(kdu_thread_env *env);
       /* This function is only called from within `do_background_processing'.
          It increments `precinct_rows_available' after allocating/parsing
          a new "row" of precincts and reflecting the impact of this change
          to the subbands' `kd_subband::pending_bkgnd_state' members. */
  public: // Links and Identification
    kd_codestream *codestream;
    kd_tile_comp *tile_comp;
    kd_global_rescomp *rescomp;
    kdu_byte res_level; // Runs from 0 (LL band) to `num_dwt_levels'
    kdu_byte dwt_level; // Runs from `num_dwt_levels' (res level 0 and 1) to 1
    kdu_byte hor_depth; // From relevant entry of `kd_comp_info::hor_depth'
    kdu_byte vert_depth; // From relevant entry of `kd_comp_info::vert_depth'

  public: // Embedded node
    kd_node node; // Each resolution is a primary node in the decomposition

  public: // Dimensions and Parameters
    kdu_dims precinct_partition; // Holds the coding origin and dimensions.
    kdu_dims precinct_indices; // Holds the range of valid precinct indices.
    kdu_dims region_indices; // Holds range of indices for precincts whose
                             // code-blocks contribute to region of interest.
  public: // Derived quantities
    int max_blocks_per_precinct; // See discussion under `kd_precinct_band'
    bool propagate_roi; // True if ROI masks to be propagated to descendants

    bool can_flip; // Set to false if this level contains subband decomposition
                   // styles which are incompatible with view flipping.

  public: // Arrays
    kdu_byte num_subbands; // Excludes the LL band unless `res_level'=0
    kdu_byte num_intermediate_nodes; // Enough for all non-leaf nodes descended
                                 // from the primary node in this object.
    kd_node *intermediate_nodes; // Array with `num_intermediate_nodes' entries
    int num_precincts; // Number of entries in `precinct_refs' array
    kd_precinct_ref *precinct_refs;
    kd_subband *subbands; // Points to `subband_store' or `subband_handle'.
    kd_subband *subband_handle; // For dynamically allocated subbands
    kd_subband subband_store[3]; // Avoid dynamic allocation for common case

  public: // Flags and Other State Variables
    kdu_coords current_sequencer_pos; // Used by `kd_packet_sequencer'.
    kdu_coords saved_current_sequencer_pos; // For saving the sequencer state
  
  public: // Members used for background parsing
    int precinct_rows_available; // Interpretation depends on geometry
    kdu_interlocked_int32 bkgnd_state; // See below
    kdu_interlocked_ptr bkgnd_next; // See below
    kdu_thread_queue *pending_notify_queue; // See below
    int pending_p_delta; // See below
  };
  /* Notes:
        The `current_sequencer_pos' coordinates are used to sequence
     spatially oriented packet progressions.  They maintain the
     indices (starting from [0,0], rather than `precinct_indices.pos')
     of the precinct which is currently being sequenced.
        The `region_indices' member holds the smallest range of precinct
     indices, such that the code-blocks contained within these precincts
     are sufficient to reconstruct all samples inside `node.region'.
     Equivalently, the range in `precinct_indices' yields the set of
     precincts which intersect with `node.region_cover'.
        It is worth noting that the area of intersection between each
     precinct's region on the resolution and the `node.region_cover' region
     is used to compute the information returned by
     `kdu_resolution::get_precinct_relevance'.
        The last five members are used to implement background parsing and
     resource allocation processing.  Firstly, `precinct_rows_available'
     keeps track of the number of precinct "rows" from the range identified
     by `region_indices' that have already been parsed (decompression) or
     allocated (compression) within a background job scheduled from within
     `kd_cs_thread_context'.  The meaning of "rows" here depends upon the
     geometry set by `kdu_codestream::change_appearance'.  In particular, if
     the appearance has been flipped vertically, rows start from the bottom
     and work upwards, if the appearance has been transposed,
     `precinct_rows_available' actually counts whole precinct columns, and
     so forth.  This member variable is used exclusively in the implementation
     of `advance_precinct_rows_available' that is called, as required, from
     within `do_background_processing'.
        The remaining four member variables play a critical role in the
     robust and lock-free interaction between a background processing thread
     that enters `do_background_processing' and the threads that enter
     the `attach_block_notifier', `detach_block_notifier' and
     `advance_block_rows_needed' functions of the resolution's subbands.
     The most critical consideration that underlies the implementation is
     protection against a highly unlikely yet highly insidious race condition
     that could arise through the delivery of notification messages to the
     `kd_subband::notify_queue' objects from within `do_background_processing'.
     Specifically, the concern is that these notification calls will eventually
     leave all the subband encoding/decoding engines in a state where they
     can invoke their `kdu_thread_queue::all_done' functions (usually through
     the scheduling and execution of block encoding/decoding jobs), which
     will eventually allow an application to successfully return from a call
     to `kdu_thread_queue::join' or `kdu_thread_queue::terminate', after
     which it will eventually invoke `kdu_tile::close'; that, in turn, may
     result in the cleaning up (or recycling) of the present `kd_resolution'
     object.  If we are extraorinarily unlucky, all of this could happen
     while the call to `do_background_processing' that set all these events
     in motion is still in progress, potentially accessing member variables
     or setting synchronization state values that are no longer accessible.
        The key to avoiding any such possibility, without having to actually
     create mutexes and lock critical sections, is to keep track of the number
     of "live notify queues" within the interlocked `bkgnd_state' variable.
     Each time a subband's `attach_block_notifier' function is called, the
     number of live queues is incremented.  Each time the last notification
     call is delivered to a notify queue, the count is decremented.  If the
     count is about to go to zero through such a decrementing process, we
     are in danger of creating the above-mentioned race condition.  To avoid
     this, the `bkgnd_state' member contains other fields that keep track of
     whether or not this object has been re-scheduled for background processing
     via a call one of its subband's `advance_block_rows_needed' or
     `detach_block_notifier' functions -- this could happen asynchronously
     while we are visiting the subbands within `do_background_processing'.  If
     we are reduce the number of "live notify queues" to 0 only to discover
     that the `bkgnd_state' value has been modified to reflect
     rescheduling of the current object for background processing, we do
     not immediately invoke the relevant notify queue's `update_dependencies'
     function; instead, we store details of the notification message that
     needs to be posted within the `pending_notify_queue' and
     `pending_p_delta' members of this object; the first thing we do when
     we return to `do_background_processing', after being rescheduled for
     background processing, is check these members to see if there is a
     pending notification message that needs to be sent.  That way, we
     can be guaranteed that the dangerous final notification call can be
     performed at a point when the `kd_resolution' object is not enqueued
     for further background processing; moreover we can return immediately
     from `do_background_processing' immediately after issuing this call,
     so that there is no possibility that we touch any member variables or
     virtual functions within the tile that might henceforth be cleaned up
     or recycled.
        The `bkgnd_next' member is used to build a linked list of
     `kd_resolution' objects that must be visited during background
      processing.
        The `bkgnd_state' member has the following structure, reflected in
     defined macros of the form `KD_RESOLUTION_BKGND_SCHEDULED_...':
     -- S (bit-0) is set when the object is appended to the background
        servicing queue and reset when the `do_background_processing'
        function enters.  Relevant macro: `KD_RESOLUTION_BKGND_SCHEDULED'.
     -- P (bit-1) is set if regular background processing has been requested
        via a call to `append_to_background_service_queue'.  If the
        `do_background_processing' function finds this bit to be reset, it
        need not attempt to advance the set of available precinct rows,
        since only detachment processing is of interest.  Relevant macro:
        `KD_RESOLUTION_BGND_PROGRESS'.
     -- L (bits 2-7) holds a count of the total number of
        "live notify queues", as explained above -- this value will never
        exceed 48 in practice, since that is the maximum number of subbands
        in any given resolution level.  Relevant macros:
        `KD_RESOLUTION_BGND_LIVE_xxx'.
     -- B (all remaining bits) holds count of the number of threads that are
        currently "blocking" calls to `do_background_processing' from being
        scheduled or doing anything.  This count is incremented by calls to
        `reserve_background_detachment' and decremented by calls to
        `schedule_background_detachment'.  Relevant macros:
        `KD_RESOLUTION_BKGND_BLOCKING_xxx'.
  */

/*****************************************************************************/
/*                             kd_header_in                                  */
/*****************************************************************************/

class kd_header_in {
  /* Manages the reading of packet header bytes from the code-stream, along
     with bit stuffing and unpacking. */
  public: // Member functions
    kd_header_in(kd_input *source)
      { this->source = source; bits_left=0; byte=0; header_bytes=0; }
    int get_bit() // throws its own "this" pointer if the source is exhausted.
      {
        if (bits_left==0)
          {
            bits_left = (byte==0xFF)?7:8;
            if (!source->get(byte))
              { bits_left = 0; throw this; }
            header_bytes++;
          }
        bits_left--;
        return (byte >> bits_left) & 1;
      }
    kdu_uint32 get_bits(int num_bits) // thorws(kd_header_in *)
      { kdu_uint32 result = 0;
        while (num_bits > 0)
          {
            if (bits_left==0)
              {
                bits_left = (byte==0xFF)?7:8;
                if (!source->get(byte))
                  { bits_left = 0; throw this; }
                header_bytes++;
              }
            int xfer_bits = (num_bits<bits_left)?num_bits:bits_left;
            bits_left -= xfer_bits;
            num_bits -= xfer_bits;
            result <<= xfer_bits;
            result |= ((byte >> bits_left) & ~(0xFF << xfer_bits));
          }
        return result;
      }
    int finish()
      { // Call this when the header is all read, to consume any stuffing byte
        // Returns total header length
        if ((bits_left == 0) && (byte == 0xFF))
          {
            bits_left = 7;
            if (!source->get(byte))
              throw this;
            header_bytes++;
          }
        return header_bytes;
      }
  private: // Data
    kd_input *source; // Provides the input mechanism.
    kdu_byte byte;
    int bits_left;
    int header_bytes;
  };

/*****************************************************************************/
/*                                kd_header_out                              */
/*****************************************************************************/

class kd_header_out {
  /* Manages the simulated and real output of packet header bytes. */
  public: // Member functions
    kd_header_out(kdu_output *out=NULL) // Null for simulating header
      { byte = 0; bits_left = 8; completed_bytes = 0; this->out = out; }
    void put_bit(int bit)
      { // Output a header bit
        assert(bit == (bit & 1));
        if (bits_left == 0)
          { if (out != NULL) out->put(byte);
            completed_bytes++;
            bits_left = (byte==0xFF)?7:8;
            byte = 0; }
        byte += byte + bit;
        bits_left--;
      }
    void put_bits(kdu_int32 val, int num_bits)
      { // Output the least significant `num_bits' of `val'
        while (num_bits > 0)
          put_bit((val >> (--num_bits)) & 1);
      }
    int finish()
      { // Returns the total number of bytes consumed by the packet header.
        if (bits_left < 8)
          {
            byte <<= bits_left;
            if (out != NULL)
              out->put(byte);
            completed_bytes++;
            if (byte == 0xFF)
              { // Need the stuffing byte.
                if (out != NULL)
                  out->put((kdu_byte) 0);
                completed_bytes++;
              }
          }
        return completed_bytes;
      }
  private: // Data
    kdu_byte byte;
    int bits_left;
    int completed_bytes;
    kdu_output *out;
  };

/*****************************************************************************/
/*                                 kd_block                                  */
/*****************************************************************************/

class kd_block {
  /* For a very small size (only 24 bytes on 32-bit machines), this class
     defines a lot of member functions!. */
  // --------------------------------------------------------------------------
  public: // Lifecycle member functions
    void cleanup(kd_buf_server *buf_server)
      {
        while ((current_buf=first_buf) != NULL)
          { first_buf=current_buf->next;  buf_server->release(current_buf); }
        set_discard(); // Discard from any packets parsed in the future
      }
  // --------------------------------------------------------------------------
  public: // Packet manipulation member functions
    int parse_packet_header(kd_header_in &head, kd_buf_server *buf_server,
                            int layer_idx); // Throws (kdu_uint16 code)
      /* Parses bits in the packet header corresponding to the current
         block for the current layer (this must be the next layer for which
         information has not yet been parsed).  The function returns the
         number of new body bytes contributed by the code-block in this layer
         and also stores this value internally to be used in subsequent calls
         to the `read_body_bytes' member function.
            The `kd_header_in' object may throw an exception if the packet
         data terminates unexpectedly.  Moreover, to assist in locating and
         possibly recovering from errors, the present function may throw a
         16-bit unsigned integer exception code, which is either the value of
         an illegal marker code encountered in the packet header, or one of
         the special exception codes, KDU_EXCEPTION_PRECISION or
         KDU_EXCEPTION_ILLEGAL_LAYER. */
    void read_body_bytes(kd_input *source, kd_buf_server *buf_server,
                         bool in_memory_source)
      { /* This function should be called after parsing the header of a
           packet.  It reads the code-bytes contributed by the code-block to
           this packet.  If `in_memory_source' is true, only the address of
           the code-bytes is recorded, and then only if the length of the
           code-byte segment is non-zero. */
        if (this->temp_length > 0)
          {
            if (num_passes == 255)
              { // Read and discard the code-bytes.
                assert(first_buf == NULL);  source->ignore(this->temp_length);
              }
            else if (in_memory_source)
              {
                kdu_byte *addr;
                this->num_bytes += source->pseudo_read(addr,this->temp_length);
                put_address(addr,buf_server);
              }
            else
              this->num_bytes +=
                source->read(current_buf,buf_pos,buf_server,this->temp_length);
            this->temp_length = 0;
          }
      }
    void retrieve_data(kdu_block *block, int max_layers, int discard_passes,
                       int qlim_max_passes, bool in_memory_source);
      /* Retrieves compressed code-bytes, number of coding passes and codeword
         segment lengths from the current code-block and stores them in the
         `block' object.  All fields which are common for compression and
         decompression have already been filled out by the caller.  The
         byte buffer is allocated so as to be large enough to allow access to
         at least one aligned 32-bit word beyond the last actual code byte
         within the 32-bit aligned `block->byte_buffer' array.  This allows
         marker codes to be appended to the compressed data, if desired,
         with properties that support efficient buffer parsing.  See the
         declaration of `kdu_subband::open_block' for more on this.
            The `max_layers' argument identifies the maximum number of
         quality layers for which code-block contributions are to be
         retrieved.
            The `discard_passes' argument indicates the number of additional
         coding passes to be removed, if any.
            The `qlim_max_passes' argument indicates the maximum number of
         coding passes to transfer, regardless of `discard_passes' or
         `max_layers' -- this value is derived from a `kdu_quality_limiter'
         within `kd_tile::set_elements_of_interest'.
            If `in_memory_source' is true, the layer code bytes are not
         stored in the `kd_code_buffer' list embedded inside `block'.  Instead,
         the `kd_code_buffer' list stores only the header information for
         each layer, along with the address of the code bytes, which reside
         in the memory block managed by an in-memory compressed data source
         (one which offers the `KDU_SOURCE_CAP_IN_MEMORY') capability.  In
         this case, we retrieve the address and then perform a simple memory
         block copy operation to efficiently retrieve the code bytes for
         each layer. */
    void store_data(kdu_block *block, kd_buf_server *buf_server);
      /* Stores compressed code-bytes, pass length and pass slope information
         from the `block' object into the current code-block.  This function
         is invoked by "kdu_subband::close_block" when the containing
         codestream object was not constructed for input.  Also reflects
         the `msbs_w' value into higher nodes in the tag tree. */
    void transfer_data(kd_block &src)
      { /* Like `store_data', but gets results from another block. */
        current_buf = first_buf = src.first_buf;
        // *((kdu_int32 *) &buf_pos) = *((kdu_int32 *) &src.buf_pos);
        buf_pos = src.buf_pos;         msbs_w = src.msbs_w;
        num_passes = src.num_passes;   pass_idx = src.pass_idx;        
        src.current_buf = src.first_buf = NULL;
      }
    bool trim_data(kdu_uint16 slope_threshold, kd_buf_server *buf_server);
      /* This function serves two purposes: it helps reduce the amount of
         compressed data which must be buffered in memory while compressing
         an image; and it allows individual block contributions to be
         discarded during packet construction in order to tightly fit the
         generated bit-rate to the target compressed bit-rate.
            The function sets all coding pass slopes which are less than or
         equal to the supplied slope threshold to 0 and then discards all
         code bytes which could not possibly be included in the final
         code-stream, as a result of this modification.
            If anything was discarded, the function returns true.  Otherwise,
         it returns false. */
    int start_packet(int layer_idx, kdu_uint16 slope_threshold);
      /* Call this function only during rate allocation or packet generation.
         Before the first call to this function, you must reset the tag tree
         (see the "reset_output_tree" member function).  If you are calling
         the function multiple times with the same layer for rate allocation
         purposes, you must use "save_output_tree" and "restore_output_tree"
         as described below.
            The function leaves behind information about the number of new
         coding passes and the number of new code bytes which would be
         contributed by this code-block if the supplied threshold were actually
         used.  The final contributing coding pass must have a non-zero
         distortion-length slope which is strictly greater than the slope
         threshold.  It also updates the tag tree structure to reflect any
         new knowledge about the first layer to which this code-block would
         contribute.  This information is picked up by "write_packet_header".
            The function's return value is the total number of new code-bytes
         contributed by the block.  This allows the PCRD-opt algorithm to
         determine roughly the right slope threshold before committing to the
         work of simulating packet header generation. */
    void write_packet_header(kd_header_out &head, int layer_idx,
                             bool simulate);
      /* Call this function only after a `start_packet' call.  It writes
         (or simulates the writing of) packet header information corresponding
         to the code-block inclusion information set up by `start_packet'.
         If `simulate' is true, some internal state adjustments are delayed
         until the next call to "save_output_tree", so multiple calls to
         "start_packet" and "write_packet_header" may be issued with the same
         layer index to determine an optimal slope threshold.  Otherwise,
         if `simulate' is false, the internal state information is fully
         updated and the "save_output_tree" and "restore_output_tree" functions
         need not and should not be called. */
    void write_body_bytes(kdu_output *dest);
      /* This function should be called after a non-simulated
         "write_packet_header" call to output the newly included body bytes.
         The actual number of bytes to be written was left behind by
         the preceding "start_packet" call in the `temp_length' field. */
    int start_packet_cbr(kdu_uint16 slope_threshold, kdu_uint16 &mod_threshold,
                         kdu_uint16 &next_slope_val, bool want_fill);
      /* Like `start_packet', but this function is used by
         `kd_precinct::simulate_packet_cbr', rather than
         `kd_precinct::simulate_packet', which uses the regular
         `start_packet' function.  Both functions include coding passes
         whose distortion-length slope is strictly greater than the
         `slope_threshold', but this function only ever processes quality
         layer 0 and returns important additional distortion-length slope
         information via the `mod_threshold' and `next_slope_val' arguments.
            `mod_threshold' is set to the largest slope threshold such that
         the simulated result would be exactly the same as that obtained
         using `slope_threshold'.  This means that all thresholds S in the
         range `slope_threshold <= S <= `mod_threshold' will produce the same
         result.  Note that S is an exclusive lower bound on the D-L slope
         value for coding passes that are included in the packet.
            `next_slope_val' returns the largest distortion-length slope
         value that fails the inclusion test, meaning that it is less than
         or equal to `slope_threshold'.  If `slope_threshold' is small enough
         to include all coding passes of all code-blocks, `next_slope_val' is
         set to 0.  It follows, that the following relationship always holds:
            `next_slope_val' <= `slope_threshold' <= `mod_threshold'.
         If `next_slope_val' is > 0, then `next_slope_val'-1 is the largest
         value of `slope_threshold' that would cause this function to include
         more code-block data in the packet.
            Additionally, this function allows for the possibility that
         fill/spacer bytes may need to be added to this code-block within the
         JPEG2000 packet that is being started.  The actual number of fill
         bytes is not important to this particular function, except if the
         code-block would not make any contribution at all.  In this case, if
         `want_fill' is true, the function arranges for the code-block to make
         a contribution, installing a convenient value for the `missing_msbs'
         field so as to avoid incurring additional coding costs for this
         unnecessary information.  To make this effective, fill bytes should
         only be added to the last code-block within a precinct.
            Note that the `temp_length' member does not actually include any
         fill bytes that we may add, but it includes everything else.  This
         is because `temp_bytes' is only a 16-bit field and we may (unusually)
         need more fill bytes than this to meet certain CBR objectives, when
         the content is extremely compressible. */
    int write_packet_header_cbr(kd_header_out &head, int fill_bytes,
                                bool simulate);
      /* Like `write_packet_header', but this function is used by
         `kd_precinct::simulate_packet_cbr', rather than
         `kd_precinct::simulate_packet', which uses the regular
         `write_packet_header' function.
            This function always processes only quality layer 0, so the
         quality layer index need not be one of its arguments.
            More importantly, this function is able to accommodate fill/spacer
         bytes being added to the packet.  When `fill_bytes' is non-zero,
         `start_packet_cbr' must have been called with `want_fill' true, and
         this must be the last code-block in its precinct.  The number of
         fill bytes is not recorded in the `temp_length' member left behind
         by `start_packet_cbr', because `temp_length' is only a 16-bit field
         and we may need a larger number of fill bytes.  If the code-block
         is identified as making a contribution (`num_pending_passes' != 0),
         the `fill_bytes' will be inserted into the code-block bytes stream
         itself, but this is not done here -- it is done within the actual
         call to `write_body_bytes_cbr'.
            If the code-block had no passes, or none that met the slope
         threshold supplied to `start_packet', `num_pending_passes' will be 0,
         yet `fill_bytes' can still be non-zero.  In that case, this function
         understands that the code-block is to be treated as contributing one
         coding pass, whose content will be generated artificially within the
         `write_body_bytes_cbr' function.
            The function returns the actual number of `fill_bytes' that are
         to be introduced during the `write_body_bytes_cbr' call, which is
         also the amount by which the actual number of written body bytes will
         exceed the value recorded in the `temp_length' member.  This return
         value will be rounded up from `fill_bytes' to an even number in the
         event that the standard block coder has been used, or if the
         code-block would not otherwise make any contribution to the packet,
         since in these cases, fill bytes necessarily consist of FF7F pairs
         that are appended to the code-block byte stream (even an empty one).
         If the FAST block coding algorithm has been used, and the code-block
         is already making a contribution to the packet, any integer number
         of fill bytes can usually be introduced, so the function usually
         returns `fill_bytes' directly; note, however, that if `fill_bytes' is
         1 on entry, the return value will often need to be 2, since it is
         not possible to terminate any codeword segment with FF. */
    void write_body_bytes_cbr(kdu_output *dest, int fill_bytes);
      /* This function should be called after a non-simulated
         `write_packet_header_cbr' call to output the newly included body
         bytes, introducing any `fill_bytes' that were identified in the
         earlier call.  The actual number of bytes to be written, not including
         any `fill_bytes', was left behind by the `start_packet_cbr' call
         in the `temp_length' field. */
  // --------------------------------------------------------------------------
  public: // Static functions used to operate on tag-trees built from kd_block
    static kd_block *build_tree(kdu_coords size, kdu_byte * &mem_block);
      /* Builds a full tag tree returning a pointer to the upper left hand
         corner leaf node.  Apart from the `up_down' navigation pointers,
         all fields are initialized to 0.  This is appropriate for tag tree
         decoding, but encoders will need to call "reset_output_tree" and
         use "start_packet" to install appropriate node values.
            The tag tree is not allocated from the heap.  Instead, it is
         allocated from an existing memory block, identified by the `mem_block'
         argument.  This memory block is assumed to be large enough to hold
         the array of `kd_block' structures.  Upon return, the `mem_block'
         value is advanced by an amount equal to the size of the tag tree
         array. */
    static void reset_output_tree(kd_block *tree, kdu_coords size);
      /* Call this function immediately before simulating or actually
         generating any packet data for the relevant code-blocks.  Once
         the PCRD-opt algorithm has completed, you should call this function
         a second time to prepare for actual packet output.  The function
         initializes the threshold state variables, `layer_w_bar' and
         `msbs_w_bar' to 0, and the value state variables, `layer_w' and
         `msbs_w' to their maximum representable values.  The succession of
         "start_packet" calls will update the value variables `layer_w'
         and `msbs_w' in such a way that the tag tree coding process runs
         correctly in each subsequent call to "write_packet_header". */
    static void save_output_tree(kd_block *tree, kdu_coords size);
      /* Call this only during rate allocation, before moving on to a new
         layer.  The function saves the current state (`msbs_wbar' and
         `layer_wbar') of each node in the tag tree.  For leaf nodes, it also
         commits the number of new coding passes found in the last call to
         "start_packet". */
    static void restore_output_tree(kd_block *tree, kdu_coords size);
      /* Call this during rate allocation before supplying a new slope
         threshold for the same layer index, in a new "start_packet" call.
         The function restores the `msbs_wbar' and `layer_wbar' values
         previously saved by "save_output_tree". */
    static void restart_parsing(kd_block *tree, kdu_coords size,
                                kd_buf_server *buf_server);
      /* This function may be called after one or more packets have been
         parsed to reset the tag-tree state information in preparation for
         parsing the first packet again.  The function leaves all modes and
         tag tree structural properties intact.  Storage allocated by behind
         by prior parsing acivities is recycled to the supplied
         `buf_server' -- just like `cleanup' but without invoking
         `set_discard'. */
  // --------------------------------------------------------------------------
  public: // State identification functions
    void set_discard()
      { /* Use this function only with input-oriented operations.  It informs
           the object that none of the code-bytes associated with this
           block will ever be accessed, so that it is free to skip over them
           when reading a packet.  This can save a lot of memory if you have
           a defined region of interest for decompression. */
        num_passes = 255;
      }
    bool empty()
      { // Returns true if the block has no code-bytes yet.
        return first_buf == NULL;
      }
    void set_modes(int modes)
      {
        assert(modes == (modes & 0xFF));
        this->modes = (kdu_byte) modes;
      }
  // --------------------------------------------------------------------------
  private: // Convenience functions for transferring data to/from code buffers.
    void start_buffering(kd_buf_server *buf_server) // Call before `put_byte'
      { assert(first_buf == NULL);
        first_buf = current_buf = buf_server->get(); buf_pos = 0; }
    void put_byte(kdu_byte val, kd_buf_server *buf_server)
      {
        assert(current_buf != NULL);
        if (buf_pos==KD_CODE_BUFFER_LEN)
          { buf_pos=0; current_buf=current_buf->next=buf_server->get(); }
        current_buf->buf[buf_pos++] = val;
      }
    void put_word(int val, kd_buf_server *buf_server)
      { 
        int i = (buf_pos+1)>>1; buf_pos = (kdu_byte)(2*i+2); // 2-byte aligned
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { i=0; buf_pos=2;
            current_buf=current_buf->next=buf_server->get(); }
        current_buf->wbuf[i] = (kdu_uint16) val;
      }
    void put_address(kdu_byte *addr, kd_buf_server *buf_server)
      { 
        int i = (buf_pos+KDU_POINTER_BYTES-1)>>KDU_LOG2_POINTER_BYTES;
        buf_pos = (kdu_byte)(KDU_POINTER_BYTES*i+KDU_POINTER_BYTES);
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { i=0; buf_pos=KDU_POINTER_BYTES;
            current_buf=current_buf->next=buf_server->get(); }
        current_buf->abuf[i] = addr;
      }
    kdu_byte get_byte()
      { 
        if (buf_pos==KD_CODE_BUFFER_LEN)
          { buf_pos=0; current_buf=current_buf->next;
            assert(current_buf != NULL); }
        return current_buf->buf[buf_pos++];
      }
    int get_word()
      { 
        int i = (buf_pos+1)>>1; buf_pos = (kdu_byte)(2*i+2); // 2-byte aligned
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { i=0; buf_pos=2; current_buf=current_buf->next;
            assert(current_buf != NULL); }
        return (int) current_buf->wbuf[i];
      }
    kdu_byte *get_address()
      { 
        int i = (buf_pos+KDU_POINTER_BYTES-1)>>KDU_LOG2_POINTER_BYTES;
        buf_pos = (kdu_byte)(KDU_POINTER_BYTES*i+KDU_POINTER_BYTES);
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { i=0; buf_pos=KDU_POINTER_BYTES; current_buf=current_buf->next;
            assert(current_buf != NULL); }
        return current_buf->abuf[i];
      }
    void skip_word()
      {
        buf_pos += 2 + (buf_pos & 1); // Align and advance the buffer pointer
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { buf_pos=2; current_buf=current_buf->next;
            assert(current_buf != NULL); }
      }
    void skip_address()
      {
        buf_pos += (-buf_pos)&(KDU_POINTER_BYTES-1); // Align the buf pointer
        buf_pos += KDU_POINTER_BYTES; // Advance the buffer pointer
        if (buf_pos > KD_CODE_BUFFER_LEN)
          { buf_pos=KDU_POINTER_BYTES; current_buf=current_buf->next;
            assert(current_buf != NULL); }
      }
  // --------------------------------------------------------------------------
  private: // Data (lots of unions to make it as small as possible!)
    union {
      kd_code_buffer *first_buf;
      kdu_uint16 save_layer_w; // Used for saving non-leaf node state
      };
    union {
      kd_code_buffer *current_buf;
      kdu_uint16 save_layer_wbar; // Used for saving_non-leaf node state
      };
    union {
      kdu_byte buf_pos; // Location of next read/write byte in `current_buf'.
      kdu_byte save_msbs_wbar; // Used for saving non-leaf node state
      };
    kdu_byte msbs_w;
    kdu_byte num_passes;
    kdu_byte pass_idx;
    union {
      kdu_uint16 layer_w; // Used only when `pass_idx'=0
      kdu_uint16 body_bytes_offset; // Used in packet output if `pass_idx' > 0
      kdu_byte save_beta; // Used during output simulation if `pass_idx' > 0
      };
    union {
      kdu_uint16 layer_wbar;
      kdu_uint16 num_bytes; // Used only for input operations
      kdu_byte pending_new_passes; // Used only for output operations
      };
    union {
      kdu_byte msbs_wbar;
      kdu_byte beta;
      };
    kdu_byte modes;
    kdu_uint16 temp_length; // Temporary storage of length quantities.
    kd_block *up_down;
  };
  /* Notes:
        This structure has been designed to provide sufficient state
     information for each code-block to perform packet header parsing as
     well as block encoding and decoding.  For coding purposes, the information
     represented by the structure is generally transferred to or from a
     much less tightly packed representation for speed of execution of the
     block coding routines.  As it stands, the total size of this structure
     should be 24 bytes on a 32-bit architecture. An array of code-blocks is
     maintained by each precinct-band.  The array contains one entry for
     every block in the relevant precinct-band, but also contains additional
     entries to be used as tag-tree nodes.  The total number of such extra
     nodes is about 1/3 the number of code-blocks and should not be a
     significant cause of concern for overall memory consumption. There is
     also substantial benefit to be had by locating the tag tree nodes in
     the same contiguous memory block.  Notice that the structure contains
     no reference back to the precinct-band to which it belongs.  When
     necessary, this reference must be supplied separately to interested
     functions. One consequence of this memory saving step is that the
     object's destructor cannot do anything useful.  It only checks to make
     sure that the explicit `cleanup' function has already been called.
        If a block is to be discarded (since it does not intersect with a
     user-specified region of interest), the `num_passes' field will hold
     the otherwise impossible value, 255.
        The `first_buf', `current_buf' and `buf_pos' fields constitute the
     compressed data buffer management mechanism, which is used to store all
     quantities whose length is unpredictable a priori. In particular, a
     linked list of `kd_code_buffer' structures is provided, which grows on
     demand through access to the codestream object's embedded `buffering'
     object.  Data is stored in these buffers in the following formats:
           * Input Format -- [<layer #> (<#bytes> <#passes> ...)] data [<...
             That is, for each layer to which the code-block makes a non-empty
             contribution, we record first the layer number (2 bytes) and
             then one or more #bytes/#passes pairs.  #bytes is represented
             using 2 bytes, while #passes is represented using only 1 byte.
             There is one such 3 byte pair for each layer and one additional
             3 byte pair for each codeword segment terminating pass which
             is not the final pass contributed in the layer.  In this way,
             sufficient information is provided to recover all termination
             lengths, as well as to build layer associations.  The most
             significant bit of the #bytes field is used as a marker to
             indicate whether or not this is the last such field prior to
             the appearance of the code bytes contributed by that layer.  A
             1 means that there are more fields to go before the code bytes
             appear.
           * In-memory Format --[<layer #>(<#bytes> <#passes>...)] <addr> [<...
             This format is used as an alternative to the Input Format above,
             in the special case when the compressed data source supports the
             `KDU_SOURCE_CAP_IN_MEMORY' capability, so that
             `kd_codestream::in_memory_source' is true.  In this case, the
             "data" field is replaced by the address where the data can be
             found in the memory block managed by the compressed data source.
             Moreover, if the number of code-bytes associated with a layer
             is zero, the <addr> field is missing.
           * Output Format -- [<RD slope> <#bytes>] ... [<RD ...] data
             That is, all of the header information for all coding passes
             appears up front, followed by all of the code bytes associated
             with those coding passes.  The reason for putting all the
             header information up front is that it facilitates the
             identification of appropriate layer contributions by the
             rate control algorithm.  Each coding pass has a 4 byte header,
             with 2 bytes for the distortion-length slope value and 2 bytes
             for the number of code-bytes for the pass.
     The particular format which is used to store data depends upon the
     member functions which are used, since these implicitly determine
     whether the codestream object is being used for input or output.
        The `modes' field holds the block coder mode fields.  Some of these
     are required even for correct packet header parsing, regardless of whether
     the block is to be actually decoded.
        `msbs_w' and `msbs_wbar' are the state information required
     for tag-tree coding of the number of missing MSB's for a code-block
     which is about to make its first non-empty contribution to the
     code-stream. Similarly, `layer_w' and `layer_wbar' hold the state
     information required for tag-tree coding of the layer index in which a
     code-block first contributes to the code-stream.  The operation of
     tag-tree coding and the `w' and `wbar' terminology are explained in
     Section 8.4 of the book by Taubman and Marcellin.
        The `beta' field shares storage with `msbs_wbar'.  This is not
     required until the number of missing MSB's has been encoded or decoded,
     at which point we initialize `beta' to 3 (see the JPEG2000 standard or
     Section 12.5.4 of the book by Taubman and Marcellin).  A useful
     consequence of this behaviour is that `beta' may be tested at any
     point of the packet header encoding or decoding process to determine
     whether or not the code-block has yet contributed to the code-stream
     -- the value will be non-zero if and only if it has done so.
        The `num_bytes' field shares storage with `layer_wbar'.  It
     identifies the total number of bytes included by the code-block in
     all packets read or written so far.  The value is taken to be 0 if
     the code-block has not yet contributed to any packets (`beta'=0),
     during which time the `layer_wbar' state variable is in use.
        The `up_down' field is used to efficiently navigate the tag-tree
     structure.  When the tag tree is not actually being navigated, the
     field points up to the current node's parent in the tag tree.  The
     root mode is readily identified by the fact that it has a NULL `up_down'
     pointer. During navigation, the tag-tree coder first walks from a
     leaf node up to the root, modifying the `up_down' pointers as it goes
     so that each successive parent's `up_down' field points bacl down to the
     child from which the parent was reached.  The coder then walks back down
     from the root to the original leaf node, restoring the `up_down' fields to
     their original `up' state. */

/*****************************************************************************/
/*                             kd_precinct_band                              */
/*****************************************************************************/

struct kd_precinct_band {
  /* Used to store state information for an element of the precinct partition
     within a single subband.  Once we get down to precincts and ultimately
     code-blocks, we have to be careful not to waste too much memory, since
     an image may have a very large number of these.  Moreover, it may be
     necessary to maintain tag-trees and other state information even for
     precincts which do not lie within our region of interest, so that the
     relevant packet headers can be parsed -- otherwise, we may have no way
     of knowing how to skip over packets which are not interesting to us. */
    kd_subband *subband;
    kdu_dims block_indices; /* Range of code-block indices for this precinct.
        This is a subset of the `block_indices' specified by the `subband'. */
    kd_block *blocks;
  };
  /* Notes:
     In order to make the representation of this object as efficient as
     possible, the two tag trees and all additional information required to
     correctly parse packet headers is maintained within the code-block
     array itself.  The `blocks' array consists of one element for each
     code-block, organized in raster fashion, plus additional elements to
     represent higher nodes in the tag-tree structure.  See the comments
     associated with the definition of `kd_block' for more information on
     this.
        In practice, the `blocks' array is not allocated separately on the
     heap.  Instead, the block of memory allocated to hold the `kd_precinct'
     structure is augmented by an amount sufficient to hold all of the
     code-blocks for all of the subbands in the precinct.  This is
     accomplished by the custom `kd_precinct::new' operator, which is
     informed of the maximum number of code-blocks required by any precinct
     within the containing `kd_resolution' object.  This value is maintained
     by the `kd_resolution::max_blocks_per_precinct" data member.  The
     `blocks' fields are initialized to the NULL state by the
     `kd_precinct::initialize' member function and are later set to point
     into appropriate locations in the precinct's memory block during
     tag-tree construction. */

/*****************************************************************************/
/*                                kd_precinct                                */
/*****************************************************************************/

  /* Precincts and all of their subordinate entities (precinct-bands and
     code-blocks) are maintained by a code-stream wide precinct server
     object, which allows for efficient allocation and recycling of the
     memory resources. The constructor is invoked from within the server,
     while the `initialize' and `release' functions are invoked from
     within the `kd_precinct_ref' class which manages references to
     precincts from within the `kd_resolution' object. */

// Interpretation of the following flags is given with the `flags' member:
#define KD_PFLAG_GENERATING         0x0001
#define KD_PFLAG_CORRUPTED          0x0002
#define KD_PFLAG_DESEQUENCED        0x0004
#define KD_PFLAG_ADDRESSABLE        0x0008
#define KD_PFLAG_RELEASED           0x0010
#define KD_PFLAG_INACTIVE           0x0020
#define KD_PFLAG_RELEVANT           0x0040
#define KD_PFLAG_SIGNIFICANT        0x0080
#define KD_PFLAG_WAS_READ           0x0100
#define KD_PFLAG_LOADED_LOCKED      0x0200
#define KD_PFLAG_LOAD_TRUNCATED     0x0400
#define KD_PFLAG_READY              0x0800
#define KD_PFLAG_NO_CONTRIB         0x1000

struct kd_precinct {
  public: // Member functions
    void initialize(kd_resolution *resolution, kdu_coords pos_idx);
      /* Called from within `kd_precinct_ref::open' to configure a newly
         allocated (or recycled) object.  `pos_idx' holds the horizontal and
         vertical position of the precinct (starting from 0) within the array
         of precincts for the relevant resolution level.  Thus, `pos_idx'
         holds a relative index, rather than an absolute index. */
    void activate();
      /* This function is called from within `kd_precinct_ref::open' to
         re-activate a previously released precinct, which has not actually
         been unloaded from memory.  Substantially less work is required in
         this case, and the function must be careful to respect the current
         contents of the precinct's code-blocks.  The function sets the
         `num_required_layers' field for this activation (may change between
         successive `kd_tile::open' calls) and also sets the
         `num_outstanding_blocks' field to the total number of code-blocks
         which lie within the current region of interest. */
    void closing(kdu_thread_env *env);
      /* Called from within `kd_precinct_ref::close' immediately prior to
         recycling the storage associated with the precinct, its precinct-bands
         and their code-blocks.  This function releases the code-byte buffer
         resources that are currently in use by the precinct.
         Note that the `kd_precinct_ref::close' function may be called
         indirectly while opening a different precinct, if the current
         precinct has already been released.  This enables open calls to
         deal with resource limitations by unloading precincts which are not
         currently active. */
    void reset_packet_reading();
      /* This function may be called from `load_required_packets' if one of
         more packets are found to have already been parsed, but not all
         required packets have been parsed.  In that case, that function needs
         to restore the precinct to an unparsed state before seeking to the
         start of the precinct's packet stream and parsing in all required
         precincts from scratch. */
    void release()
      /* Used only for input codestream objects, this function is called once
         all code-blocks within the current region of interest have been
         consumed.  The function sets `num_outstanding_blocks' to 0, although
         in most cases this function will be called because
         `num_outstanding_blocks' is already 0 (only exception is when
         a tile is being closed).  Also sets `KD_PFLAG_RELEASED' and resets
         `KD_PFLAG_LOADED_LOCKED'.  If the `KD_PFLAG_DESEQUENCED' flag is
         false, the function returns without modifying the precinct in any
         other way.
            The `kd_precinct_ref::release' function is called to formally
         release the precinct's resources (this might not unload them from
         memory) if the codestream mode is non-persistent or if the precinct
         is addressable.  In the latter case, the precinct's contents can
         be reloaded from the compressed source if necessary.
            This function shall not be used with codestream objects
         opened for output or for interchange (no compressed data source and
         no compressed data target). */
      { 
        assert(resolution->codestream->in != NULL);
        num_outstanding_blocks.set(0);
        flags = (flags | KD_PFLAG_RELEASED) & ~KD_PFLAG_LOADED_LOCKED;
        if ((flags & KD_PFLAG_ADDRESSABLE) ||
            ((flags & KD_PFLAG_DESEQUENCED) &&
             !resolution->codestream->persistent))
          ref->release(NULL); // May unload us from memory!
      }
    void finished_desequencing()
      /* Called once we can be sure that no more packets will be available
         for this precinct.  In practice, the function is called when the
         last packet has been desequenced or when the containing tile has
         been completely read.  If no packets at all have been desequenced for
         the precinct when this function is called (must have been invoked
         from within `kd_tile::finished_reading'), the precinct is marked
         as addressable, with an address of 0.  This is because it is safe
         to unload such a precinct and then reload it again regardless of
         the services offered by the compressed data source -- there is no
         data actually available from the compressed source for the
         precinct anyway, so reloading is a no-op.
            It should be noted that this function may unload the precinct
         from memory, but only if it is marked with `KD_PFLAG_NO_CONTRIB' flag,
         in which case it cannot possibly be accessed by functions such as
         `kdu_subband::load_block' or `kdu_subband::close_block', and it can
         no wind up on the `kd_codestream::pending_precincts' list, at least
         until a new region of interest is defined (if persistent). */
      { 
        if ((next_layer_idx == 0) && (flags & KD_PFLAG_DESEQUENCED))
          { 
            flags |= KD_PFLAG_ADDRESSABLE;
            unique_address = 0;
          }
        flags |= KD_PFLAG_DESEQUENCED;
        if (flags & KD_PFLAG_NO_CONTRIB)
          release(); // Early release is safe (and desirable) only if precinct
                     // has no relevant code-blocks; these cannot wind up on
                     // the `kd_codestream::pending_precincts' list, so would
                     // not otherwise be released until the tile is closed.
      }
    bool desequence_packet()
      /* This function should be called when a non-NULL `kd_precinct_ref'
         pointer is returned by `kd_packet_sequencer::next_in_sequence' and
         its `kd_precinct_ref::is_sequenced' function returns false.
         The function invokes `read_packet' and, if successful, increments
         the precinct's `next_layer_idx' field and the containing tile's
         `sequenced_relevant_packets' field.
            This function must not be called if the precinct is marked as
         ADDRESSABLE.
            The function returns false if `read_packet' returns false,
         in which case, more tile-parts must be read (if there are any) to
         recover the information required to desequence the packet.
            Before returning true, the function checks to see whether all
         packets for the tile have been sequenced.  If so, the tile's
         `finished_reading' function is called. */
      {
        kd_tile *tile = resolution->tile_comp->tile;
        assert(!(flags & (KD_PFLAG_DESEQUENCED | KD_PFLAG_ADDRESSABLE)));
        if (!read_packet(0))
          return false;
        next_layer_idx++;
        if (next_layer_idx == tile->num_layers)
          finished_desequencing();
        tile->next_input_packet_num++;
        if ((flags & KD_PFLAG_RELEVANT) &&
            (next_layer_idx <= tile->max_relevant_layers))
          {
            tile->sequenced_relevant_packets++;
            if (tile->sequenced_relevant_packets==tile->max_relevant_packets)
              tile->finished_reading();
          }
        return true;
      }
    void load_required_packets();
      /* This function is invoked when the application requires access to the
         precinct, with all required packets parsed and loaded from the
         compressed data source.  The function does nothing if the
         `KD_PFLAG_LOADED_LOCKED' flag is present in the `flags' member;
         moreover, before returning the function always sets this flag, which
         is only reset by the `release' (unless of course the precinct is
         completely unloaded).
            If the `KD_PFLAG_LOAD_TRUNCATED' flag is found in the `flags'
         member, or if the `num_packets_read' member is greater than or
         equal to `num_required_packets', the function does nothing other
         than to set the `KD_PFLAG_LOADED_LOCKED' flag.  Otherwise, the
         function proceeds in the manner described below.
            As a first step, the function parses through tiles and packets as
         required to recover the location of the precinct and also to make sure
         that any non-addressable tile that is currently being actively parsed
         is not inappropriately interrupted by seeking to the precinct to load
         packets.  During this first step, the precinct will actually be fully
         parsed from the source via calls to `desequence_packets', if it turns
         out to be non-addressable; in this case, we finish up by setting
         the `KD_PFLAG_LOADED_LOCKED' flag and we are done.
            In the event that the precinct is addressable and not all of
         the required packets have been parsed, they are parsed here before
         the `KD_PFLAG_LOADED_LOCKED' flag is set.  To do this, the function
         needs to seek to the start of the precinct's packet stream and parse
         all required packets in from scratch.  In most cases the function
         is working with a precinct whose `num_packets_read' member is zero
         on entry, but if this is not the case, the function first restores
         the precinct to a state in which nothing has been read and the
         `num_read_packets' member is 0, being careful to later invoke
         `read_packet' in such a way that the already parsed packets do not
         contribute to the relevant tile-component's `layer_stats' record.
            Finally, for addressable precincts for which `num_packets_read'
         is 0 (or has been made zero, as described above), the function uses
         the precinct's `unique_address' to position the compressed data
         source at the beginning of the precinct's first packet and then
         load all required packets from the data source.
            In multi-threading environments, this function should not be
         called unless the `KD_THREADLOCK_GENERAL' mutex is locked. */
      kdu_long simulate_packet(kdu_long &header_bytes,
                               int layer_idx, kdu_uint16 slope_threshold,
                               bool finalize_layer, bool last_layer,
                               kdu_long max_bytes=KDU_LONG_HUGE,
                               bool trim_to_limit=false);
      /* Implements the functionality of `kd_codestream::simulate_output'
         for a single precinct.  Note that the total number of packet bytes
         is left behind in the internal `layer_bytes' array.  This allows the
         determination of tile-part length fields later on when we come to
         construct the code-stream.  The function returns this total number
         of bytes.  It also returns the number of packet header bytes via
         `header_bytes' (the function return value includes the cost of these
         header bytes).
            If `last_layer' is true, the function includes the cost of any
         empty packet headers that may be required for layers beyond the
         one identified by `layer_idx', as indicated by `kd_tile::num_layers'.
            If the `max_bytes' limit is exceeded, the function may return
         prematurely, without having completed the simulation.  This is
         unacceptable when finalizing a layer, so you must be sure that
         `max_bytes' is sufficient in that case.
            If `trim_to_limit' is true, `finalize_layer' and `last_layer'
         must also both be true.  The function then trims away all coding
         passes from the precinct's code-blocks which are associated with
         slope values of `slope_threshold'+1 or less, until the limit
         associated with `max_bytes' is satisfied.  This functionality is
         used to implement the `sloppy_bytes' capabilities advertised by the
         `kd_codestream::simulate_output' function. */
    kdu_long simulate_packet_cbr(kdu_long &header_bytes,
                                 kdu_uint16 slope_threshold,
                                 kdu_uint16 &mod_threshold,
                                 kdu_uint16 &next_slope_val,
                                 kdu_long &bulk_fill_bytes,
                                 int &fine_fill_bytes);
      /* This function plays a similar role to `simulate_packet', but is used
         in the low latency CBR "leaky bucket" rate control process associated
         with `kd_codestream::pcrd_opt_cbr', in which relatively small groups
         of precincts (flush-sets) are flushed one by one subject to
         typically quite tight constraints.
            In the interest of high throughput, we do not consider trimming
         code-blocks within precincts, but individual precincts are small,
         being expected to span only one or two code-blocks from each
         constituent subband.  On the other hand, we do not require all
         precincts in the flush-set to use exactly the same slope threshold,
         and this provides a mechanism for hitting a size target quite closely
         in the first instance.  Finally, we have only one sized quality layer
         in the low latency rate control case, since it is less obvious how
         we should add extra layers.  As a result, this function takes no
         quality layer index; the sized quality layer is always layer 0 and
         provision is automatically made to include empty packets for any
         extra layers that might exist.  Moreover, this function has no need
         for the concept of "finalization", since simulation is always done
         for just quality layer 0.
            Like `simulate_packet', this function simulates construction of
         the precinct's packets, subject to a distortion-length slope threshold
         supplied via `slope_threshold', which is an exclusive lower bound on
         the distortion-length slope values associated with included coding
         passes from any code-block.  The function returns the total number of
         bytes, and also reports the associated number of `header_bytes'.
            Unlike `simulate_packet', this function returns additional
         information that helps determine useful slope thresholds to use in
         the future.  `mod_threshold' is set to a value no smaller than
         `slope_threshold' that produces exactly the same results, being the
         lower bound of the corresponding `mod_threshold' values determined
         in calls to `start_packet_cbr'.  `next_slope_val' is set to the
         largest distortion-length slope value of any candidate coding
         pass that is rejected by the inclusion test, or 0 if `slope_threshold'
         is too small to reject anything.  A candidate coding pass is one whose
         distortion-length slope is non-zero, and all candidate passes whose
         slope is strictly greater than `slope_threshold' are included, so
         `next_slope_val' is necessarily <= `slope_threshold'.  When
         `next_slope_val' is non-zero, changing the slope threshold to
         `next_slope_val'-1 will provide a minimal increment in included
         content, which usually consists of just one additional coding pass
         from one code-block.
            The function can also add fill/spacer bytes to code-blocks, or
         at least simulate their addition.  To facilitate efficient simulation,
         with good control over the filled size, we treat the last code-block
         separately, since adding a small number of fill bytes to the last
         code-block almost certainly augments the total packet size by this
         number of bytes, but perhaps 1 less or 1 more.  The `fine_fill_bytes'
         are added only to the last code-block, and there are normally not
         too many of them, if any.  The value may be rounded up to an even
         integer by the function, if necessary.  The `bulk_fill_bytes' may be
         distributed to any code-block in the precinct, except if
         `fine_fill_bytes' is non-zero, in which case they may not be added
         to the last code-block.  At most 8192 of the `bulk_fill_bytes' are
         added to any code-block.  The actual number of bulk fill bytes that
         are added to code-blocks in the precinct is returned to the caller
         by modifying `bulk_fill_bytes'.  The `bulk_fill_bytes' value is
         required to be an even integer on entry, and it will always be
         an even integer on exit. */
    kdu_long write_packet(kdu_uint16 slope_threshold, bool empty_packet,
                          kdu_long &header_bytes);
      /* Writes out the next packet for this precinct, using the supplied
         distortion-length slope threshold.  Increments the `next_layer_idx'
         field as well as the containing tile's `sequenced_relevant_packets'
         field when done.  If `empty_packet' is true, an empty packet should
         be issued, having a length of 1 byte, plus the length of any required
         EPH marker (no SOP marker will be attached to empty packets).  The
         function returns the total number of bytes output for the packet and
         also sets `header_bytes' to the total number of header bytes in the
         written packet, including SOP/EPH markers where appropriate. */
    kdu_long write_packet_cbr(kdu_uint16 slope_threshold,
                              kdu_long &header_bytes,
                              kdu_long &bulk_fill_bytes,
                              int fine_fill_bytes);
      /* Similar to `write_packet', except that this one uses
         `kd_block::start_packet_cbr', `kd_block::write_packet_header_cbr'
         and `kd_block::write_body_bytes_cbr', which allows fill bytes to
         be introduced, as simulated using `simulate_packet_cbr'.  This
         function does not write empty packets, since `write_packet' can
         always be used to do that, to there is no `empty_packet' argument. */
    void cache_write_packets(int max_layers, kdu_uint16 layer_thresholds[]);
      /* This function is used only with compressed data targets that support
         the `KDU_TARGET_CAP_CACHED' capability.  It writes a complete set
         of packets for the precinct, the first `max_layers' of which are
         determined by the distortion-length slopes recorded in the
         `layer_thresholds' array, while any additional packets required
         for the precinct are written as empty packets.  The function
         invokes `kd_compressed_output::start_precinct' and
         `kd_compressed_output::end_precinct'; it also increments the
         relevant `kd_tile::sequenced_relevant_packets' member.  If, as a
         result, the tile becomes complete, and the tile header has already
         been written (`next_tpart' > 0), the function also removes the
         tile from the in-progress list and performs whatever additional
         cleanup may be required. */
  private: // Member functions
    bool read_packet(int num_prior_packets);
      /* Called either directly by `load_required_packets' or from within
         `desequence_packet', which itself is called by
         `load_required_packets'.
            Reads a new layer of this precinct sequentially from the
         compressed data source, incrementing the `num_packets_read' member.
         Note that packet reading may be interleaved with acesses to the
         precinct's code-blocks for various reasons.  For example, we
         may initially open a tile with only a limited number of required
         layers, using its code-block data to decompress an approximate
         representation of the image.  We may then find that the additional
         packets need to be loaded in order to advance to other precincts
         in the code-stream.
            The function returns false if an SOT marker (or an EOC marker) is
         encountered.  Note that when an SOT marker is encountered, its
         segment is fully read into the codestream marker service and the
         precinct's tile must be active (otherwise an error message will be
         generated).  Before returning false in this way, the function sets
         the `active_tile' pointer in the codestream object to NULL.  The
         caller may determine the cause of failure by invoking the codestream
         marker service's `get_code' function, which will return one of SOT,
         EOC or 0, where 0 occurs only if the code-stream has been
         exhausted.
            The `num_prior_packets' member is normally 0.  However, in random
         access applications, if `num_packets_read' was non-zero but more
         packets can potentially be parsed after seeking to the start of the
         precinct's packet data stream, the `reset_packet_reading' function is
         called first which resets the `num_packets_read' member to 0, but
         then `read_packet' is supplied with the previous value of
         `num_packets_read' via its `num_prior_packets' argument.  This value
         is used to ensure that `kd_tile_comp::layer_stats' information is
         only updated for newly parsed packets, whose layer indices are
         greater than or equal to `num_prior_packets'. */
    bool handle_corrupt_packet();
      /* This function implements alternate processing for the `read_packet'
         function in the event that a packet is found to be corrupt.  It is
         called whenever packet corruption is detected, including when the
         precinct is known to have had previously corrupt packets. The function
         manipulates the containing tile's `skipping_to_sop' and
         `next_sop_sequence_num' fields, advancing to the next SOP or SOT
         marker if necessary (the function may be called after detecting an
         SOP marker already, whose sequence number is not as expected).  If
         an SOT marker is encountered, the function renders the current tile
         inactive and returns false (this is the behaviour expected of
         `read_packet').  Otherwise, it updates the precinct and tile state to
         identify the packet as having been read. */
  public: // Public Links and Identification
    kd_resolution *resolution;
    kd_precinct_ref *ref; // Element of `resolution->precinct_refs' array

  public: // Mode flags used for decompression and/or interchange
    int flags; // The following flags are defined.
       // KD_PFLAG_GENERATING -- after 1'st call to `kd_precinct::get_packets'
       // KD_PFLAG_CORRUPTED -- if error occurred during packet reading
       // KD_PFLAG_DESEQUENCED -- if all packets have been desequenced
       // KD_PFLAG_ADDRESSABLE -- if seekable, cached or used for interchange
       // KD_PFLAG_RELEASED -- if `activate' must be called to revive state
       // KD_PFLAG_INACTIVE -- if resides on `kd_precict_server's inactive list
       // KD_PFLAG_RELEVANT -- always true if codestream is for non-persistent
       //     input; otherwise, true only if precinct belongs to the relevant
       //     set of precincts of a relevant tile-component-resolution
       // KD_PFLAG_SIGNIFICANT -- if any code-block bytes have been generated
       //     by `kdu_precinct::size_packets' or `kdu_precinct::get_packets'
       // KD_PFLAG_WAS_READ -- if one or more packets were previously read,
       //     after which the precinct was closed; in this case, the packet
       //     length statistics stored with the tile already hold information
       //     for the parsed content (possibly not all of it) and so should
       //     not be updated when the precinct is re-instantiated and its
       //     packets read again.  This is the one flag whose state is
       //     preserved inside `kdu_precinct_ref' after a precinct is
       //     closed -- the state is destroyed only if the tile is unloaded
       //     from memory or completely reparsed from scratch.
       // KD_PFLAG_LOADED_LOCKED -- if a call to `load_required_packets' has
       //     been issued since the precinct was last released (or its tile
       //     was last opened).  This condition acts as a lock against further
       //     manipulation of the precinct's contents, until `release' is
       //     called; otherwise race conditions might arise inside
       //     `kdu_subband::open_block'.
       // KD_PFLAG_LOAD_TRUNCATED -- if packet parsing has failed due to
       //     lack of sufficient compressed source data; this flag is NOT set
       //     if the data source is a dynamic cache, which might later hold
       //     more data.  The main purpose of this flag is to avoid the need
       //     for truncated precincts to be parsed over and over again when
       //     repeatedly accessed within a persistent codestream.
       // KD_PFLAG_READY  -- if the precinct is found on the ready list of
       //     a `kd_global_rescomp' object.
       // KD_PFLAG_NO_CONTRIB -- if the precinct is found by `initialize' or
       //     `activate' to have no code-blocks that are relevant to the
       //     current region of interest (it might have no code-blocks at all).
       //     The flag is used by `finished_desequencing' to distinguish
       //     between precincts that will eventually be moved to the
       //     `kd_codestream::pending_precincts' list, from whcih they will
       //     be released, and precincts that can never wind up on that list;
       //     the latter need to be released as soon as desequencing has
       //     occurred, and it is safe to do so because they will never be
       //     accessed from outside the "glock" context.
  public: // State variables
    int required_layers; /* Actual number of layers for output codestream;
                max apparent layers for input or interchange codestream. */
    int next_layer_idx; // Next layer to be sequenced or generated.
    union { // First field used for input; second interchange; third for output
        int num_packets_read; // Incremented when packets read or interchanged
        int cumulative_bytes; // Size of all packets generated so far.
        int saved_next_layer_idx; // Used by "kd_packet_sequencer::save_state"
      };

  public: // Activation state variables
    kdu_interlocked_int32 num_outstanding_blocks; // See notes below.
    union { // First field used only in reading; second only in writing.
        kdu_long unique_address; // See below
        kdu_long *packet_bytes; // Lengths generated during simulated output;
          // in practice, this member points into the same block of memory
          // which is allocated to hold the precinct so as to encourage
          // memory locality and allow for manipulation of all aspects of
          // a precinct within a contiguous block of memory.
      };
  public: // code-block containers
    kd_precinct_band *subbands; // In practice, this member points into the
       // same block of memory which is allocated to hold the precinct so
       // as to encourage memory locality and allow for manipulation of all
       // aspects of a precinct, including all of its code-blocks, within a
       // contiguous block of memory.  Note that the number of entries in
       // this array is given by `resolution->num_subbands'.  In the special
       // case of precincts that have no code-blocks at all, this member
       // is NULL.

  public: // See below for an explanation of these links
    kd_precinct *next; // Next in free, inactive or generated list
    kd_precinct *prev; // Previous in inactive or generated list
    kd_precinct_size_class *size_class; // For correctly releasing storage
  };
  /* Notes:
        `unique_address' is valid if and only if the precinct is marked as
     addressable, meaning that the precinct has a unique address from which it
     may be loaded (for input codestream objects) or identified (for
     interchange codestream objects).  If addressable, the `unique_address'
     will be 0 if and only if the precinct is known to be empty, meaning that
     reloading the precinct does not require any actual data transfers from a
     compressed data source.
        Except as described above, when used with seekable compressed data
     sources, the `unique_address' member holds a positive quantity which
     identifies an offset into the code-stream at which the precinct's
     packets may be found.  In the case of cached sources (those advertising
     the KDU_SOURCE_CAP_CACHED capability) or codestream objects created for
     interchange (no compressed data source or target), the `unique_address'
     will be negative and -(1+`unique_address') is the precinct's unique
     identifier, as passed to the `kdu_compressed_source::set_precinct_scope'
     function, or returned by the `kdu_precinct::get_unique_id' function.
        The `num_outstanding_blocks' member is used to monitor the number of
     code-blocks from the application's region of interest which have still
     to be consumed (input codestreams) or the number of code-blocks which
     have still to be written in order to complete the precinct (output
     and interchange codestreams).  This value might be 0 for any of the
     following reasons: a) all code-blocks have been consumed; b) there are
     no code-blocks for the precinct that are relevant to the current region
     of interest; or c) the precinct has no code-blocks whatsoever.  In cases
     (b) and (c), the `KD_PFLAG_NO_CONTRIB' flag is set, which allows the
     `finished_desequencing' function to distinguish between precincts that
     can and cannot be accessed via `kdu_subband::open_block',
     `kdu_subband::close_block', and related functions.  If the precinct
     can be accessed, then it will eventually be moved to the
     `kd_codestream::pending_precincts' list, from which it will be released,
     unless the tile is permanently closed first.  If the precinct cannot
     be accessed, it will never be released via that mechanism, so it should
     be released as soon as it has been desequenced.  Of course, release
     might not mean closure of the precinct; this depends on the mode of
     operation.
        The `next' and `prev' links may be used to manage the precinct's
     location in one of three different lists.  The first list is the
     singly-linked free list maintained by `kd_precinct_size_class', from
     which the `kd_precinct_server' object re-issues previously released
     resources.
        The second list on which this structure may find itself is the
     doubly-linked list of currently inactive precincts, which is managed by
     the `kd_precinct_server' object.  Precincts which find themselves on this
     list have been released, but their resources have not yet been recycled.
     They may be successfully re-activated, if their resources have not been
     re-issued to another precinct.  Precincts on this list may be identified
     by the `inactive' flag.  Every inactive precinct necessarily also has
     its `released' member set to true, but the converse is not necessarily
     true.  This is because precincts which have been partially parsed
     in a context for which they were not directly relevant, will be marked
     as released, even though they are not inactive.
        The third list on which this structure may find itself is the
     doubly-linked list of generated precincts managed by the
     `kd_global_rescomp' structure.  This list contains all precincts
     from a given resolution of a given image component, across all tiles,
     which have been fully generated, but not yet flushed out to the
     code-stream.   The list is used to manage incremental rate allocation
     when processing large images.  Elements on this list have not yet
     been closed. */

/*****************************************************************************/
/*                           kd_precinct_server                              */
/*****************************************************************************/

class kd_precinct_server {
  public: // Member functions
    kd_precinct_server(kd_codestream *cs, bool need_packet_bytes_arrays);
    ~kd_precinct_server();
    kd_precinct *get(int max_blocks, int num_subbands, int max_layers,
                     kdu_thread_env *env);
      /* Note that the `max_layers' argument is ignored (effectively forced
         to zero) if the object was constructed with `need_packet_bytes_arrays'
         equal to false. */
    kdu_long get_total_allocated_bytes()
      { return glock_allocated_bytes + plock_allocated_bytes; }
  private: // Helper functions
    void augment_glock_structure_bytes(int increment)
      { codestream->buf_servers->augment_structure_bytes(increment); }
    void augment_plock_structure_bytes(int increment)
      { /* Same as `kd_buf_server::augment_structure_bytes' but uses
           `plock_surplus_structure_bytes' for its temporary accumulator. */
        plock_surplus_structure_bytes += increment;
        if (plock_surplus_structure_bytes > 0)
          { 
            int num_blocks =
              1 + ((plock_surplus_structure_bytes-1)/KD_CODEBUF_BLOCK_BYTES);
            plock_surplus_structure_bytes -= num_blocks*KD_CODEBUF_BLOCK_BYTES;
            codestream->buf_master->augment_structure_blocks(num_blocks);
          }
        else if (plock_surplus_structure_bytes <= -KD_CODEBUF_BLOCK_BYTES)
          { 
            int num_blocks =
              - ((-plock_surplus_structure_bytes) / KD_CODEBUF_BLOCK_BYTES);
            plock_surplus_structure_bytes -= num_blocks*KD_CODEBUF_BLOCK_BYTES;
            codestream->buf_master->augment_structure_blocks(num_blocks);
          }
      }
  private: // Data
    friend class kd_precinct_size_class;
    kd_precinct *inactive_head, *inactive_tail;
    kd_codestream *codestream;
    bool allocate_packet_bytes;
  private: // State information for the "glock" context
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep1)
    kd_precinct_size_class *glock_size_classes;
    kdu_long glock_allocated_bytes;
  private: // State information for the "plock" context
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep2)
    kd_precinct_size_class *plock_size_classes;
    int plock_surplus_structure_bytes;
    kdu_long plock_allocated_bytes;
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep3)
  };
  /* Notes:
        This object is used to serve up empty precincts and to recycle their
     storage when they are no longer required.  Precinct memory blocks are
     organized into size classes, based on the maximum number of code-blocks
     and precinct-bands in the precinct, where these quantities are consistent
     for all precincts associated with any given `kd_resolution' object.
     In the event that the `need_packet_bytes_arrays' argument to the
     object's constructor was true, precinct memory blocks also include
     space for the array that holds the number of bytes in each packet of
     the precinct, and the length of this array must also be consistent
     across each size class.
        In many cases, it can happen that all precincts from the entire image
     have exactly the same size class.  This maximizes the potential for
     recycling precinct storage, since precincts may only be allocated from
     and recycled back to their own size class.  The reason for this dependence
     on size classes is that the block of memory associated with each precinct
     is large enough to accommodate all `max_blocks' code-blocks which might
     be required to populate the `kd_precinct_band::blocks' arrays, all
     precinct-bands which are required to populate the `kd_precinct::subbands'
     array, and any storage required for the `kd_precinct::packet_bytes'
     array.  This gets us out of having to maintain separate mechanisms
     to serve up code-block storage as a recyclable resource.  It also helps
     with memory localization.
        The `get' function is called in one of two contexts that we identify
     as the "glock" and "plock" contexts.  A "glock" call is either single
     threaded (`env' argument is NULL) or else one in which the
     `KD_THREADLOCK_GENERAL' mutex is locked.  A "plock" call is one in which
     the `KD_THREADLOCK_PRECINCT' mutex is locked.  Each of these contexts
     allocates precincts from a separate list of `kd_precinct_size_class'
     objects, so as to avoid race conditions if the two types of calls are
     mixed.  Note, however, that precincts allocated within one context
     ("glock" or "plock") may be released back to their size class via any
     of three different contexts, known as "glock", "plock" and "nolock",
     as explained in the notes that accompany the `kd_precinct_size_class'
     object.
        The object also manages a cacheing service for precincts and their
     data, which is relevant only to input codestreams (i.e., codestream
     machinery which reads and parses precincts from a compressed data
     source).  In such contexts, access to both `get' and
     `kd_precinct_size_class::release' all use the "glock" context.
     When previously loaded/parsed precincts are released using
     `kd_precinct_ref::release', they are appended to a list of inactive
     precincts headed by `inactive_head' and concluded by `inactive_tail'.
     The list is doubly linked via the `kd_precinct::next' and
     `kd_precinct::prev' members and, in multi-threading contexts, this is
     protected by the `KD_THREADLOCK_GENERAL' mutext.
        When a previously released precinct is opened using
     `kd_precinct_ref::open', it is removed from the inactive list and its
     `kd_precinct::activate' function is called.  Note, however, that the
     KD_PFLAG_RELEASED flag is not a reliable indicator of whether
     or not a precinct is on the inactive list, since precincts which have
     been released using `kd_precinct::release', but have not yet been
     completely desequenced (all packets have not yet been parsed from or
     located in the code-stream), will also have this flag set, but will not
     be on an inactive list.  The KD_PFLAG_INACTIVE flag should be used to
     determine whether or not a precinct is on the inactive list.
        When `kd_precinct_server::get' function is called and the list headed
     by `inactive_head' is found to be non-empty (can happen only for input
     codestreams), the `get' function, which must be using the "glock" context,
     queries the `codestream->buf_servers[0]' object to determine whether or
     not a cache threshold has been exceeded.  If so, it closes down precincts
     on the inactive list until the `kd_buf_server' object reports that
     sufficient resources are in hand.  As mentioned, in multi-threaded
     environments, this all happens under the protection of the
     `KD_THREADLOCK_GENERAL' mutex.
  */

/*****************************************************************************/
/*                         kd_precinct_size_class                            */
/*****************************************************************************/

class kd_precinct_size_class {
  public: // Member functions
    kd_precinct_size_class(int max_blocks, int num_subbands,
                           kd_precinct_server *server,
                           int max_layers, bool using_plock)
      { 
        this->max_blocks = max_blocks;
        this->num_subbands = num_subbands;
        this->max_layers = max_layers;
        this->server = server;
        this->get_uses_plock = using_plock;
        next = NULL;
        alloc_bytes = (int) sizeof(kd_precinct);
        alloc_bytes += (-alloc_bytes) & 7; // Round up to 8-byte boundary
        alloc_bytes += max_layers << 3; // Allows 8-bytes per length value
        alloc_bytes += num_subbands * (int) sizeof(kd_precinct_band);
        alloc_bytes += (-alloc_bytes) & 7; // Round up to 8-byte boundary
        alloc_bytes += 4 + max_blocks * (int) sizeof(kd_block);
        if (max_blocks == 0)
          nolock_xfer_thresh = INT_MAX; // Special case; no transfers
        else
          nolock_xfer_thresh = 8192 / alloc_bytes; // Exclusive lower bound
        glock_free_list = glock_free_tail = NULL;
        glock_free_count = glock_allocated_precincts = 0;
        plock_free_list = plock_free_tail = NULL;
        plock_free_count = plock_allocated_precincts = 0;
        nolock_free_list.set(NULL);
      }
    ~kd_precinct_size_class()
      { 
        kdu_long total_precincts =
          glock_allocated_precincts + plock_allocated_precincts;
        while ((glock_free_tail=glock_free_list) != NULL)
          { glock_free_list = glock_free_tail->next; free(glock_free_tail);
            total_precincts--; glock_free_count--; }
        while ((plock_free_tail=plock_free_list) != NULL)
          { plock_free_list = plock_free_tail->next; free(plock_free_tail);
            total_precincts--; plock_free_count--; }
        while (nolock_free_list.get() != NULL)
          { kd_precinct *tmp = (kd_precinct *) nolock_free_list.get();
            nolock_free_list.set(tmp->next); free(tmp); total_precincts--; }
        assert(total_precincts == 0);
        assert(glock_free_count == 0);
        assert(plock_free_count == 0);
      }
    kd_precinct *glock_get()
      { 
        assert(!get_uses_plock);
        if (glock_free_list == NULL)
          augment_glock_free_list();
        glock_free_count--;
        kd_precinct *result = glock_free_list;
        if ((glock_free_list = result->next) == NULL)
          { 
            assert(glock_free_count == 0);
            glock_free_tail = NULL;
          }
        result->next = result->prev = NULL;
        server->augment_glock_structure_bytes(alloc_bytes);
        return result;
      }
    kd_precinct *plock_get()
      { 
        assert(get_uses_plock);
        if (plock_free_list == NULL)
          augment_plock_free_list();
        plock_free_count--;
        kd_precinct *result = plock_free_list;
        if ((plock_free_list = result->next) == NULL)
          { 
            assert(plock_free_count == 0);
            plock_free_tail = NULL;
          }
        result->next = result->prev = NULL;
        server->augment_plock_structure_bytes(alloc_bytes);
        return result;
      }
    void release(kd_precinct *prec, kdu_thread_env *env)
      { 
        bool glock=((env==NULL) ||
                    server->codestream->check_lock(KD_THREADLOCK_GENERAL,env));
        if (prec->flags & KD_PFLAG_INACTIVE)
          { 
            assert(glock);
            withdraw_from_inactive_list(prec);
          }
        if (glock)
          { // Single-threaded, or release guarded by the general mutex
            if ((prec->next = glock_free_list) == NULL) glock_free_tail=prec;
            glock_free_list = prec;  glock_free_count++;
            if (get_uses_plock && (glock_free_count > nolock_xfer_thresh))
              export_glock_free_list();
            server->augment_glock_structure_bytes(-alloc_bytes);
          }
        else if (server->codestream->check_lock(KD_THREADLOCK_PRECINCT,env))
          { // Release guarded by the precinct mutex
            if ((prec->next = plock_free_list) == NULL) plock_free_tail=prec;
            plock_free_list = prec;  plock_free_count++;
            if ((!get_uses_plock) && (plock_free_count > nolock_xfer_thresh))
              export_plock_free_list();
            server->augment_plock_structure_bytes(-alloc_bytes);
          }
        else
          { // Lock-less multi-threaded release
            kd_buf_server *buf_server =
              server->codestream->get_thread_buf_server(env);
            do {
              prec->next = (kd_precinct *) nolock_free_list.get();
            } while (!nolock_free_list.compare_and_set(prec->next,prec));
            buf_server->augment_structure_bytes(-alloc_bytes);
          }
      }
    void move_to_inactive_list(kd_precinct *precinct);
    void withdraw_from_inactive_list(kd_precinct *precinct);
      /* Used to move precincts onto or off the inactive list maintained
         by the `kd_precinct_server' object to which the present object
         belongs.  These functions may only be called in the "glock"
         context, explained in the notes following `kd_precinct_server'. */
  private: // Helper function
    void augment_glock_free_list();
    void augment_plock_free_list();
      /* The above functions first try to move spare precincts from the
         `nolock_free_list' to the relevant locked spare precinct list
         (`glock_free_list' or `plock_free_list').  If this fails, they
         allocate a new precinct on the heap and place it on the relevant
         free list. */
    void export_glock_free_list()
      { // Move the `glock_free_list' across to the `nolock_free_list'
        kd_precinct *old_head;
        do {
          old_head = (kd_precinct *) nolock_free_list.get();
          glock_free_tail->next = old_head;
        } while (!nolock_free_list.compare_and_set(old_head,glock_free_list));
        glock_free_list = glock_free_tail = NULL;  glock_free_count = 0;
      }
    void export_plock_free_list()
      { // Move the `plock_free_list' across to the `nolock_free_list'
        kd_precinct *old_head;
        do {
          old_head = (kd_precinct *) nolock_free_list.get();
          plock_free_tail->next = old_head;
        } while (!nolock_free_list.compare_and_set(old_head,plock_free_list));
        plock_free_list = plock_free_tail = NULL;  plock_free_count = 0;
      }
  private: // Data
    friend class kd_precinct_server;
    kd_precinct_server *server;
    int max_blocks;
    int num_subbands;
    int max_layers; // 0 if no `packet_bytes' array is to be allocated
    int alloc_bytes; // Num bytes to allocate per precinct.
    int nolock_xfer_thresh; // See below
    bool get_uses_plock; // True if `get' calls use the "glock" context
    kd_precinct_size_class *next; // Next size class
  private: // Data members used in the "glock" context
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep1)
    kd_precinct *glock_free_list;
    kd_precinct *glock_free_tail;
    kdu_long glock_free_count;
    kdu_long glock_allocated_precincts;
  private: // Data members used in the "plock" context
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep2)
    kd_precinct *plock_free_list;
    kd_precinct *plock_free_tail;
    kdu_long plock_free_count;
    kdu_long plock_allocated_precincts;
  private: // Data members used in the "nolock" context
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep3)
    kdu_interlocked_ptr nolock_free_list; // Precincts released with no lock
    KD_PRIVATE_CACHELINE_SEPARATOR(_sep4)
  };
  /* Notes:
        Each `kd_precinct_size_class' object keeps three lists of free
     precincts, corresponding to the three different contexts in which
     precinct resources can be released.  These contexts are: a) the "glock"
     context, which is either single-threaded or protected by the
     `KD_THREADLOCK_GENERAL' mutex; b) the "plock" context, which is
     protected by the `KD_THREADLOCK_PRECINCT' mutex; and c) the "nolock"
     context, which is multi-threaded but not protected by any mutex.  In
     practice, almost all precincts are released in the "glock" context, even
     though they may have been allocated in the "plock" context.
        Precincts that are released in the "glock" or "plock" context are
     counted.  If allocation of new precincts is proceeding in the other
     type of context ("plock" instead of "glock" or "glock" instead of
     "plock"), as indicated by the `get_uses_plock' flag, the `release'
     function periodically moves the released list of precincts across to the
     `nolock_free_list', from which it may be picked up by a `server->get'
     call.  The `nolock_xfer_thresh' argument is compared with the count of
     precincts that are available to move, in order to determine whether
     the move is worthwhile.  The idea is to minimize the number of atomic
     operations introduced onto the system bus. */

/*****************************************************************************/
/*                                kd_flush_stats                             */
/*****************************************************************************/

struct kd_flush_stats {
  public: // Member functions
    void add_ref()
      { assert(ref_count > 0); ref_count++; }
      /* Called when we are attached to another codestream. */
    void remove_ref()
      { assert(ref_count > 0); ref_count--;
        if (ref_count == 0) delete this; }
      /* Called when a codestream no longer needs to reference the object.
         This function is likely to be self-efacing. */
    int advance(int frame_sep);
      /* Does the work of `kdu_flush_stats::advance', so that it can also
         be done when auto-advance is configured. */
    bool check_compatible(const kd_flush_stats *ref) const
      { 
        return ((num_components == ref->num_components) &&
                (num_tiles == ref->num_tiles));
      }
  private: // Destructor is private; invoked only by `remove_ref'.
    ~kd_flush_stats();
  public: // Lifecycle information
    int ref_count; // Number of codestreams referencing this object
  public: // Set-related information
    int active_set; // 0 or 1 -- the other set is an archive
    int aux_set_parity; // Filled out by `kdu_codestream::attach_flush_set'
    kdu_uint32 summary_slope[2]; // See below
    kdu_interlocked_int32 frame_valid[2]; // See below
    int auto_advance_frame_sep; // Zero if auto-advance is not configured
  public: // Resources and dimensions
    int num_components;
    kdu_long num_tiles;
    kdu_long num_tile_comps;
  };
  /* Notes:
        The purpose of this object is to preserve flushing statistics
     from one codestream to the next, so that they can be used to assist both
     encoding and rate control in a subsequent codestream.  To facilitate this,
     some information is managed in "sets", of which there are two.  The
     active set corresponds to the frame that is currently being encoded or
     flushed, the change being made only after a previous set's content has
     been completely flushed.  The other set is either empty or else it
     corresonds to the last codestream generated by the same entity
     (usually same controlling thread).  Multi-threaded video compression
     implementations often keep two `kdu_codestream' machines alive at once,
     each of which processes half the frames.  The second machine can start
     processing a new frame before the first one completes the processing of
     the previous frame, but only to the extent that there is insufficient
     work available in the first codestream to keep all available threads
     working.  In this model, each `kdu_codestream' will have its own
     `kdu_flush_stats' interface, with associated an `kd_flush_stats' internal
     representation, and each codestream processing machine would then share
     its `kdu_flush_stats' interface with the other codestream so that they
     can each update the most up-to-date flushing information available.  In
     other applications, a single `kdu_codestream' machine may do all the
     work so there is no need for sharing.
        The `aux_set_parity' member is 0 unless two codestream generation
     machines are collaborating in the manner mentioned above.  In that case,
     `kd_codestream::aux_flush_set' and `kd_codestream::flush_set' will both
     be non-NULL.  Relative to this flush-stats object, the auxiliary one is
     that created by the other codestream's `kdu_codestream::add_flush_set'
     function and attached via our `kdu_codestream::attach_flush_set'
     function.  The parity value reflects the order in which the frames
     associated with the initial active set in each of the two codestreams
     will be flushed.  An `aux_set_parity' value of 0 means that the
     auxiliary codestream's first frame is flushed before the current one,
     while an `aux_set_parity' value of 1 means that the current one is
     flushed first.  When encoding a given frame within the current
     object's `kdu_codestream', the availability of information for a prior
     codestream in the auxiliary flush-stats object is found by looking
     at the auxiliary `kd_flush_stats' object's state variables that belong
     to set S = (this->active_set ^ this->aux_set_parity).
        The `summary_slope' array holds the final summary (perhaps average)
     log-slope threshold associated with the flushing of each set.  This
     value is updated only once all codestream content for the relevant set
     has been flushed.  To help separate threads determine whether or not
     this has happened, care is taken in the update and reading of the
     `frame_valid' array's entries; there is one entry for each set.  The
     most significant bit of each `frame_valid' entry is 0 if the
     corresponding `summary_slope' value has not yet been updated; it is
     set to 1 with release semantics (`kdu_interlocked_int32::barrier_set')
     after the summary slope information has been written to the corresponding
     entry in the `summary_slope' array.  The least significant 31 bits in
     each `frame_valid' entry hold the frame that is currently represented by
     that set.  Thus, from the point at which the encoding of one set
     commences until the point at which all of its content has been flushed,
     the MSB of its `frame_valid' entry will be 0, informing other threads that
     the summary slope information for that frame might not be valid.  Threads
     which need the summary slope information should use acquire semantics
     (`kdu_interlocked_int32::get_barrier') to first determine that the MSB
     of the relevant `frame_valid' entry is 1, before reading and interpreting
     the `summary_slope' value for the set.  If the summary slope is not
     valid, the thread that is working within a separate `kdu_codestream'
     context may choose to use older summary slope information that has
     been recorded within its own local `kd_flush_stats' object -- i.e. in
     the non-active set.  From the perspective of the thread which actually
     does flushing within a codestream, the non-active set's `frame_valid'
     entry should always have its MSB set, unless there was no previous
     frame, and the corresponding `summary_slope' value holds summary
     log-slope information for the previous codestream processed in the
     same context.
        In the future, other information will be added to the statistics
     managed by flush-sets.  For the moment, however, we content ourselves
     with just keeping the summary slope information.
  */
  

/* ========================================================================= */
/*         Inlined Member Functions Delayed to Satisfy Dependencies          */
/* ========================================================================= */

inline void
  kd_cs_thread_context::schedule_resolution_processing(kdu_thread_entity *ent)
{
  if (cs->in != NULL)
    schedule_bkgnd_processing(KD_BKGND_FLAG_RES_PARSE,ent);
  else
    schedule_bkgnd_processing(KD_BKGND_FLAG_RES_PALLOC,ent);
}

inline void
  kd_codestream::add_pending_precinct(kd_precinct *precinct)
{ 
  assert(precinct->num_outstanding_blocks.get() == 0);
  kd_precinct *old_head;
  do {
    old_head = (kd_precinct *) pending_precincts.get();
    precinct->next = old_head;
  } while (!pending_precincts.compare_and_set(old_head,precinct));
}

inline bool
  kd_precinct_ref::is_desequenced()
{
  register kdu_long state_val = state;
  return ((state_val != 0) &&
          ((state_val & 1) ||
           (((kd_precinct *) _kdu_long_to_addr(state_val))->flags &
            KD_PFLAG_DESEQUENCED)));
}

inline void
  kd_precinct_ref::clear()
{ // We can assume this function is always called in the "glock" context
  if (state && !(state & 1))
    {
      kd_precinct *precinct = (kd_precinct *) _kdu_long_to_addr(state);
      precinct->ref = NULL; // Satisfy the test in `kd_precinct::closing'
      precinct->closing(NULL);
      precinct->size_class->release(precinct,NULL);
    }
  state = 0;
}

inline void
  kd_precinct_ref::close_and_reset()
{ // We can assume this function is always called in the "glock" context
  if (state & 1)
    {
      if (state != 3)
        state &= ~((kdu_long) 2);
    }
  else if (state != 0)
    {
      kd_precinct *prec = (kd_precinct *) _kdu_long_to_addr(state);
      if ((prec->flags & KD_PFLAG_WAS_READ) || (prec->num_packets_read > 0))
        {
          close(NULL);
          state &= ~((kdu_long) 2);
        }
    }
}

inline void
  kd_precinct_ref::release(kdu_thread_env *env)
{
  kd_precinct *precinct = (kd_precinct *) _kdu_long_to_addr(state);
  assert((state != 0) && !(state & 1));
  if (precinct->flags & KD_PFLAG_INACTIVE)
    return; // Already on the inactive list.
  precinct->flags =
    (precinct->flags | KD_PFLAG_RELEASED) & ~(KD_PFLAG_LOADED_LOCKED);
  if ((precinct->flags & KD_PFLAG_ADDRESSABLE) &&
      (!precinct->resolution->codestream->cached_source) &&
      ((precinct->num_packets_read == 0) ||
       (precinct->num_packets_read == precinct->next_layer_idx)))
    precinct->size_class->move_to_inactive_list(precinct);
  else
    close(env);
}

inline kd_precinct *
  kd_precinct_ref::open(kd_resolution *res, kdu_coords pos_idx,
                        bool need_activate, kdu_thread_env *env)
{
  if (state == 3)
    return NULL; // Precinct is not currently loaded
  if ((state == 0) || (state & 1))
    return instantiate_precinct(res,pos_idx,env);
  kd_precinct *result = (kd_precinct *) _kdu_long_to_addr(state);
  if (result->flags & KD_PFLAG_INACTIVE)
    { // Must be on the inactive list; in this case call must use "glock" ctxt
      result->size_class->withdraw_from_inactive_list(result);
      result->activate();
    }
  else if (need_activate && (result->flags & KD_PFLAG_RELEASED))
    result->activate();
  return result;
}

inline kd_precinct *
  kd_precinct_ref::active_deref()
{ // This function is often invoked without mutex protection.  It is used to
  // determine whether a valid precinct reference exists already or one must
  // be created using `kd_precinct_ref::open', which should be protected in
  // a multi-threading environment.  To avoid possible race conditions, we
  // inform the compiler that the `state' variable should be treated as
  // volatile here.
  volatile kdu_long *state_ref = &state;
  register kdu_long state_val = *state_ref;
  kd_precinct *result = (kd_precinct *) _kdu_long_to_addr(state_val);
  if ((state_val & 1) || (result == NULL)) return NULL;
      // Note: if we directly test (state == 0) instead of (result == NULL)
      // on the above line, we might occasionally suffer from a race condition
      // on 32-bit architectures, due to the fact that 64-bit loads of
      // `state' are not atomic; as a result, it may happen that another
      // thread is manipulating `state' (e.g., inside `instantiate_precinct')
      // but has modified only one of the DWORDS by the time we read `state'
      // here.  Fortunately, we only really need to test the part of `state'
      // that should represent an address.
  if (result->flags & (KD_PFLAG_INACTIVE|KD_PFLAG_RELEASED)) return NULL;
  return ((result->ref == this) &&
          (*state_ref == _addr_to_kdu_long(result)))?result:NULL;
      // Need to be sure that another thread did not activate the precinct
      // object for use with another `kd_precinct_ref' reference, causing
      // the flags test above to succeed, even though the precinct had been
      // in the released state when we accessed the value of `state' on
      // the second line.  The fact that `state_ref' is volatile, forces the
      // compiler to generate code which reloads it to properly confirm that
      // `result' is the precinct which belongs to us.
}


/* ========================================================================= */
/*                              External Functions                           */
/* ========================================================================= */

extern void
  kd_create_dwt_description(int kernel_id, int atk_idx, kdu_params *root,
                            int tnum, bool &reversible, bool &symmetric,
                            bool &symmetric_extension, int &num_steps,
                            kdu_kernel_step_info * &step_info,
                            float * &coefficients);
  /* This function is used in two places to expand the description of an
     arbitrary DWT kernel, based on information recovered from the codestream
     parameter sub-system.  Inputs to the function are the `kernel_id'
     (one of `Ckernels_W5X3', `Ckernels_W9X7' or `Ckernels_ATK'), the
     `atk_idx' (for non-Part1 kernel's identified as `Ckernels_ATK'), and
     the `root' of the codestream parameter sub-system.  The function
     determines whether or not the kernel describes a reversible transform,
     whether the kernel filters are whole-sample-symmetric, whether the
     boundary extension policy is symmetric extension or replication, the
     description of each lifting step, and the set of all lifting step
     coefficients, returning them via the last 6 arguments.  The function
     creates two arrays, returning them via the `step_info' and
     `coefficients' arguments.  In the event that the function fails through
     `kdu_error', most applications will define the behaviour to be that of
     throwing an exception.  In this case, it is important that these
     arguments reference members of a persistent structure which can be
     cleaned up when the exception is handled.  As for the structure of the
     arrays, it is sufficient to note that they can be passed directly to
     the `kdu_kernels::init' function. */
  
} // namespace kd_core_local

#endif // COMPRESSED_LOCAL_H
