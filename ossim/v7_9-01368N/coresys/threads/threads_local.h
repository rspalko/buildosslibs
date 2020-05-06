/*****************************************************************************/
// File: threads_local.h [scope = CORESYS/THREADS]
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
   Provides local definitions for the core multi-threading implementation
found in "kdu_threads.cpp".
******************************************************************************/

#ifndef THREADS_LOCAL_H
#define THREADS_LOCAL_H

#include <assert.h>
#include "kdu_threads.h"
#include "kdu_arch.h"

// Objects defined here:
namespace kd_core_local {
  struct kd_thread_job_hzp;
  struct kd_thread_palette;
  struct kd_thread_idle_pool;
  struct kd_thread_domain_sequence;
  struct kd_thread_domain;
  struct kd_thread_group;
  class kd_thread_trace; // Used only if thread tracing is enabled
}

// The following macros identify the structure of the
// `kdu_thread_queue::completion_state' member.
#define KD_THREAD_QUEUE_CSTATE_S          ((kdu_int32) 1)
#define KD_THREAD_QUEUE_CSTATE_D          ((kdu_int32) 2)
#define KD_THREAD_QUEUE_CSTATE_T          ((kdu_int32) 4)
#define KD_THREAD_QUEUE_CSTATE_W          ((kdu_int32) 8)
#define KD_THREAD_QUEUE_CSTATE_S_D        ((kdu_int32) 3)
#define KD_THREAD_QUEUE_CSTATE_C_BIT      ((kdu_int32) 16)
#define KD_THREAD_QUEUE_CSTATE_C_MASK     ((kdu_int32) ~15)

// Defining the following macro enables recording of thread activity, a record
// of which is printed to stdout when `kdu_thread_entity::destroy' is invoked
// on a created multi-processing environment.  The macro supplies the
// maximum number of entries to record.
//#define KDU_THREAD_TRACE_RECORDS 10000000

// The following definitions facilitate the implementation of Kakadu's
// highly efficient wait-free lock-free job queue.
#define KD_PLT_ALIGN     64
#define KD_PLT_CNT_MASK  56
#define KD_PLT_CNT_INC    8
#define KD_PLT_POS_MASK   7
#define KD_PLT_SLOTS      7

#define KD_PLT_HAZARD_MKR ((void *) _kdu_long_to_addr(1))
#define KD_PLT_DEQUEUE_MKR ((void *) _kdu_long_to_addr(1))
#define KD_JOB_TERMINATOR ((kdu_thread_job *) _kdu_long_to_addr(1))

namespace kd_core_local {
  using namespace kdu_core;
  
/*****************************************************************************/
/*                             kd_thread_job_hzp                             */
/*****************************************************************************/

struct kd_thread_job_hzp {
    kd_thread_palette *ref; // We use hazard pointers only to point to palettes
    void *rsvd1[7]; // Ensures size of object is at least 8 pointers
#ifndef KDU_POINTERS64
    void *rsvd2[8]; // Ensures object is 64 bytes on 32-bit and 64-bit systems
#endif // !KDU_POINTERS64
  };

/*****************************************************************************/
/*                             kd_thread_palette                             */
/*****************************************************************************/

struct kd_thread_palette {
  public: // Member functions
    void init(kdu_thread_job *job)
      { 
        nxt.set(NULL); slot[0].set(job);
        for (int j=1; j < KD_PLT_SLOTS; j++)
          slot[j].set(NULL);
      }
  public: // Data
    kdu_interlocked_ptr nxt;
    kdu_interlocked_ptr slot[KD_PLT_SLOTS];
#ifndef KDU_POINTERS64
    void *resvd[8]; // Ensures that the object occupies exactly 64 bytes
#endif // !KDU_POINTERS64
  };
  /* Notes:
     Each thread job is bound to a palette that it owns.  While enqueued,
     the owning job is referenced by slot[0].  Once the palette moves off
     the head of the job queue, `nxt' is set to the special value
     `KD_PLT_DEQUEUE_MKR' by the thread that moves it off.  Once the
     palette comes up for re-enqueue, if slot[0] is NULL, the palette is
     neither on the head of a queue nor is it the subject of hazardous
     references, so it can be re-used immediately.  Otherwise, slot[0]
     is set to the special value `KD_PLT_HAZARD_MKR' and the palette is
     swapped for a clean one within the relevant thread's private palette
     heap.  Palettes become clean once their `nxt' member becomes
     `KD_PLT_DEQUEUE_MKR' and there are no hazardous references to them,
     at which point their slot[0] entry is converted to NULL. */

/*****************************************************************************/
/*                          kd_thread_palette_ref                            */
/*****************************************************************************/

struct kd_thread_palette_ref {
    kd_thread_palette *palette;
    kd_thread_palette_ref *next;
  };
  /* Notes:
       This object allows us to build linked lists of references to palettes
     that can be readily allocated to thread queues by the thread group and
     recycled back to the thread group for re-allocation.  Importantly, the
     palette associated with a palette reference can be swapped with one
     that is found in a thread's private palette store. */

/*****************************************************************************/
/*                           kd_thread_idle_pool                             */
/*****************************************************************************/

#  if (KDU_MAX_THREADS <= 32)
  typedef kdu_int32 kd_thread_flags;
  typedef kdu_interlocked_int32 kd_interlocked_thread_flags;
  struct kd_thread_idle_pool {
     public: // Member functions
       kd_thread_idle_pool() { idle_flags.set(0); }
       bool add(int idx)
         { // Adds the thread with index `idx' to the idle pool, returning
           // true if the thread was already been on the idle pool.
           assert((idx >= 0) && (idx < 32));
           kdu_int32 mask = ((kdu_int32) 1) << idx;
           kdu_int32 old_val = idle_flags.exchange_or(mask);
           return ((old_val & mask) != 0);
         }
       bool remove(int idx)
         { // Removes the thread with index `idx' from the idle pool, returning
           // true if the thread was previously on the idle pool.
           assert((idx >= 0) && (idx < 32));
           kdu_int32 mask = ((kdu_int32) 1) << idx;
           kdu_int32 old_val = idle_flags.exchange_and(~mask);
           return ((old_val & mask) != 0);
         }
       bool all_idle(int nthreads)
         { return (idle_flags.get() == ~(((kd_thread_flags) -1)<<nthreads)); }
       bool test(kd_thread_flags mask)
         { return ((mask & idle_flags.get()) != 0); } 
       int remove_any(kd_thread_flags mask, int num_wanted, int indices[]);
         /* This function attempts to remove up to `num_wanted' threads from
            the pool of idle thread, writing the indices of the removed
            threads into the `indices' array and returning the number
            actually removed.  Only those threads for which a bit is set in
            the `mask' argument are potentially removed and identified via
            the `indices' array. */
     public: // Data
       kd_interlocked_thread_flags idle_flags;
  };
#  elif (KDU_MAX_THREADS <= 64)
  typedef kdu_int64 kd_thread_flags;
  typedef kdu_interlocked_int64 kd_interlocked_thread_flags;
  struct kd_thread_idle_pool {
     public: // Member functions
       kd_thread_idle_pool() { idle_flags.set(0); }
       bool add(int idx)
         { // Adds the thread with index `idx' to the idle pool, returning
           // true if the thread was already been on the idle pool.
           assert((idx >= 0) && (idx < 64));
           kdu_int64 mask = ((kdu_int64) 1) << idx;
           kdu_int64 old_val = idle_flags.exchange_or(mask);
           return ((old_val & mask) != 0);
         }
       bool remove(int idx)
         { // Removes the thread with index `idx' from the idle pool, returning
           // true if the thread was previously on the idle pool.
           assert((idx >= 0) && (idx < 64));
           kdu_int64 mask = ((kdu_int64) 1) << idx;
           kdu_int64 old_val = idle_flags.exchange_and(~mask);
           return ((old_val & mask) != 0);
         }
       bool all_idle(int nthreads)
         { return (idle_flags.get() == ~(((kd_thread_flags) -1)<<nthreads)); }
       bool test(kd_thread_flags mask)
         { return ((mask & idle_flags.get()) != 0); } 
       int remove_any(kd_thread_flags mask, int num_wanted, int indices[]);
         /* This function attempts to remove up to `num_wanted' threads from
            the pool of idle thread, writing the indices of the removed
            threads into the `indices' array and returning the number
            actually removed.  Only those threads for which a bit is set in
            the `mask' argument are potentially removed and identified via
            the `indices' array. */
     public: // Data
       kd_interlocked_thread_flags idle_flags;
  };
#  else
#    error "KDU_MAX_THREADS is too big for this architecture!!"
#  endif

/*****************************************************************************/
/*                         kd_thread_domain_sequence                         */
/*****************************************************************************/

struct kd_thread_domain_sequence {
  public: // Member functions
    kd_thread_domain_sequence(kd_thread_domain *owner, kdu_long seq_idx)
      { init(owner,seq_idx); }
    void init(kd_thread_domain *owner, kdu_long seq_idx)
      { 
        memset(this,0,sizeof(*this));
        this->domain = owner;  this->sequence_idx = seq_idx;
        int align_off = ((- _addr_to_kdu_int32(palette_block)) &
                         (KD_PLT_ALIGN-1));
        terminator = (kd_thread_palette *)(palette_block + align_off);
        terminator->slot[0].set(KD_JOB_TERMINATOR);
        terminator->nxt.set(terminator); // Remains forever once enqueued
        kd_thread_palette *dummy_plt = terminator+1;
        queue_head.set(dummy_plt); // Installed with CNT=0 and POS=0
        queue_tail.set(dummy_plt);
      }
    kd_thread_domain_sequence *add_consumer()
      { /* Convenience function called when a thread needs to add a reference
           to this domain sequence to its `work_domains' array.  Increments
           the `num_consumers' reference counter and returns a reference to
           itself, to be placed in the thread's `work_domains' array. */
        num_consumers.exchange_add(1);
        return this;
      }
    bool remove_consumer()
      { /* Convenience function called when a thread removes the reference to
           this object from its `work_domains' array.  If the function
           returns true, the caller has just removed the last reference to
           the object, so the group mutex should be locked and the
           `kd_thread_domain::remove_unused_sequences' function should be
           invoked. */
        bool seq_removable = (num_queues.get() == 0) && (next != NULL);
        kdu_int32 old_val = num_consumers.exchange_add(-1);
        assert(old_val > 0); // Not safe to reference `this' from now on
        return ((old_val == 1) && seq_removable);
      }
    void terminate(kd_thread_job_hzp *hzp);
      /* This function is called once the `active_state' member transitions
         to 0, which necessarily means that there are no longer any queues
         that can schedule jobs to this domain sequence and there is at
         least one more element in the sequence list that threads can move
         to.  The function appends a special `terminator' to the job queue
         and then wakes all idle threads, encouraging them to
         move on to the next sequence object within the domain.  Note that
         the group mutex need not be locked when this function is invoked. */
  //---------------------------------------------------------------------------
  public: // Job enqueueing procedure
    void append_jobs(kd_thread_palette *plt_head, kd_thread_palette *plt_tail,
                     int tail_jobs, kd_thread_job_hzp *hzp)
      { /* Appends one or more job palettes to the end of the job queue,
           using `hzp' to store the hazard pointer required to avoid ABA
           race conditions.  Note that `hzp' is specific to the calling
           thread.  All palettes on the list must be free from hazards.  All
           but the last palette in the list must have a full complement of
           `KD_PLT_SLOTS' valid job pointers installed within its job
           slots, while the one referenced by `plt_tail' must have `tail_jobs'
           valid job pointers installed in its initial `tail_jobs' slots.
           The first slot in each palette must point to the palette's
           owner -- i.e., the job which indirectly points to `palette'
           via its `palette_ref' object. */
        // Start by installing CNT and POS=1 values in all relevant pointers
        kd_thread_palette *plt, *nxt;
        int lsbs = 1 + (KD_PLT_SLOTS-1)*KD_PLT_CNT_INC;
        kdu_byte *plt_h = ((kdu_byte *) plt_head) + lsbs;
        for (plt=plt_head; plt != plt_tail; plt=nxt)
          { 
            nxt = (kd_thread_palette *) plt->nxt.get();
            if (nxt == plt_tail)
              lsbs += (tail_jobs-KD_PLT_SLOTS)*KD_PLT_CNT_INC;
            plt->nxt.set(((kdu_byte *) nxt) + lsbs);
          }
        if (plt == plt_head)
          plt_h += (tail_jobs-KD_PLT_SLOTS)*KD_PLT_CNT_INC;
        // Now link the palettes into the queue
        while (true)
          { 
            kd_thread_palette *t = (kd_thread_palette *) queue_tail.get();
            hzp->ref = t; // Make sure tail pointer is hazard free
            if (!queue_tail.validate(t)) // Includes memory barrier
              continue;
            kdu_byte *t_nxt = (kdu_byte *) t->nxt.get();
            if (t_nxt != NULL)
              { // One or more new palettes added by a concurrent enqueuer
                t_nxt -= (_addr_to_kdu_int32(t_nxt) & (KD_PLT_ALIGN-1));
                if (queue_tail.compare_and_set(t,t_nxt))
                  assert(t_nxt != NULL);
                continue;
              }
            if (t->nxt.compare_and_set(NULL,plt_h))
              { 
                queue_tail.compare_and_set(t,plt_tail);
                break;
              }
          }
        hzp->ref = NULL; // Remove the hazard pointer
      }
  //---------------------------------------------------------------------------
  public: // Job dequeueing procedure
    kdu_thread_job *get_job(kd_thread_job_hzp *hzp)
      { /* Strips first job from queue, using `hzp' to store the hazard pointer
           required to avoid ABA race conditions.  Note that `hzp' is specific
           to the calling thread.  If the queue is empty, the function
           returns NULL, unless the queue has been permanently terminated,
           in which case it returns the special `KD_JOB_TERMINATOR' value.
           If this thread moves a palette off the queue head, it must set
           its first slot to the special `KD_PLT_HAZARD_MKR' value. */
        kdu_thread_job *job = NULL;
        while (job == NULL)
          { 
            kdu_byte *h = (kdu_byte *) queue_head.get();  assert(h != NULL);
            int h_lsbs = _addr_to_kdu_int32(h) & (KD_PLT_ALIGN-1);
            kd_thread_palette *h_plt = (kd_thread_palette *)(h-h_lsbs);
            hzp->ref = h_plt; // Make head element hazard free
            if (h_lsbs & KD_PLT_CNT_MASK)
              { // Head palette has at least one element left
                if (!queue_head.compare_and_set(h,h-KD_PLT_CNT_INC+1))
                  continue;
                job = (kdu_thread_job *)
                  h_plt->slot[h_lsbs & KD_PLT_POS_MASK].get();
                assert(job != NULL);
              }
            else
              { // Head palette is empty; need to advance
                kdu_byte *n = (kdu_byte *) h_plt->nxt.get();
                if (n == NULL)
                  break; // Queue is empty; get to this case without barriers
                if (!queue_head.validate(h)) // Includes memory barrier
                  continue;
                n = (kdu_byte *) h_plt->nxt.get(); // Reload `n' to be sure
                if (n == NULL)
                  break; // Queue was empty after all
                int n_lsbs = _addr_to_kdu_int32(n) & (KD_PLT_ALIGN-1);
                kd_thread_palette *n_plt = (kd_thread_palette *)(n-n_lsbs);
                kd_thread_palette *t = (kd_thread_palette *) queue_tail.get();
                if (t == h_plt) // Make sure tail does not get behind head
                  queue_tail.compare_and_set(t,n_plt);
                if (queue_head.compare_and_set(h,n))
                  { 
                    job = (kdu_thread_job *) n_plt->slot[0].get();
                    if (job != KD_JOB_TERMINATOR)
                      n_plt->slot[0].set(KD_PLT_HAZARD_MKR);
                    if (h_plt != n_plt)
                      h_plt->nxt.set(KD_PLT_DEQUEUE_MKR);
                  }
              }
          }
        hzp->ref = NULL; // Remove the hazard pointer
        return job;
      }
  //---------------------------------------------------------------------------
  public: // Termination confirmation procedure
    bool check_queue_terminated()
      { // If the queue has been terminated, the head element must reference
        // the special `terminator' palette.  There will be no other condition
        // under which this can be the case.
        kdu_byte *h = (kdu_byte *) queue_head.get();  assert(h != NULL);
        int h_lsbs = _addr_to_kdu_int32(h) & (KD_PLT_ALIGN-1);
        kd_thread_palette *h_plt = (kd_thread_palette *)(h-h_lsbs);
        return (h_plt == terminator);
      }
  //---------------------------------------------------------------------------
  public: // Links and identification members
    kd_thread_domain *domain;
    kdu_long sequence_idx;
    kd_thread_domain_sequence *next;
    kdu_interlocked_int32 num_consumers; // Threads that reference us
    kdu_interlocked_int32 num_queues; // Thread queues that reference us
    kdu_interlocked_int32 active_state; // See below
  public: // Job queue management members
    KD_PUBLIC_CACHELINE_SEPARATOR(_sep1)
    kdu_byte palette_block[KD_PLT_ALIGN*3]; // Holds dummy head + terminator
    kd_thread_palette *terminator; // Points into `terminator_block'
    kdu_interlocked_ptr queue_head; // Head of scheduled jobs queue
    kdu_interlocked_int32 trace_get; // These count retrieved jobs and added
    KD_PUBLIC_CACHELINE_SEPARATOR(_sep2)
    kdu_interlocked_int32 trace_add; // jobs if thread tracing is enabled.
    kdu_interlocked_ptr queue_tail; // Tail of scheduled jobs queue
    KD_PUBLIC_CACHELINE_SEPARATOR(_sep3)
  };
  /* Notes:
        Thread sequence domains are the place where we enqueue jobs for
     processing.  This needs to be efficient.  Each `kdu_thread_queue'
     object that is (or was) able to schedule jobs keeps a pointer to a
     `kd_thread_domain_sequence' where its jobs will be (or have been)
     scheduled, until the `kdu_thread_queue::all_complete' function gets
     called.
        Each `kdu_thread_entity' object keeps a pointer to the
     `kd_thread_domain_sequence' where it will look to process jobs,
     within each domain in which it can potentially do work.
        Each domain keeps a list of `kd_thread_domain_sequence' objects
     corresponding to increasing sequence indices.  This list is never empty,
     except in the special case of the default domain, to which jobs cannot
     be scheduled.
        Each `kd_thread_domain_sequence' object maintains a `num_consumers'
     counter, which keeps track of the number of threads that are currently
     holding references to it.  Once this counter reaches zero, an attempt
     is made to delete leading elements of the domain's `sequence_list'
     that are no longer in use -- this attempt is made from within a
     critical section, locked by the thread group's `mutex'.
        Each `kd_thread_domain_sequence' object also maintains a `num_queues'
     counter, which counts the number of queues that are currently
     referencing the domain sequence -- the reference is removed only once
     `kdu_thread_queue::all_done' has been called (it is actually removed
     from within `kdu_thread_queue::all_complete').  A domain sequence
     cannot be recycled at least until its `active_state' and `num_queues'
     members have both become 0.
        The `active_state' member may be considered to hold two bit-fields:
     -- L (bit-0) is a 1-bit flag that holds 1 if this is the last
        element of its domain's sequence list -- i.e., if `next' is NULL.
     -- A (bits 1,2,...) counts the number of queues that can still schedule
        jobs via this domain sequence.  This value is decremented when a
        call to `kdu_thread_queue::schedule_jobs' receives an `all_scheduled'
        argument that is true or, failing this, when the queue's
        `kdu_thread_queue::all_done' function is called.
     Once the `active_state' member transitions to 0, the `terminate'
     function is invoked to append the special `terminator' element to the
     end of the job queue.  Once threads detect this special marker, they
     are able to move on to new domain sequences, and once all threads have
     done this, the disused domain sequences can be cleaned up via
     `kd_thread_domain::remove_unused_sequences'.
        Jobs themselves are enqueued to and dequeued from a highly efficient
     lock-free and wait-free FIFO queue that is managed via the `queue_head'
     and `queue_tail' arguments.
  */

/*****************************************************************************/
/*                              kd_thread_domain                             */
/*****************************************************************************/

struct kd_thread_domain {
  public: // Member functions
    kd_thread_domain(const char *domain_name, kd_thread_group *grp)
      { 
        this->group=grp;  next = NULL; name = NULL;
        background=false; safe_context=false;
        num_member_threads=0; member_threads = (kd_thread_flags) 0;
        sequence_head = sequence_tail = free_sequences = NULL;
        if ((domain_name != NULL) && (*domain_name != '\0'))
          { 
            this->name = new char[strlen(domain_name)+1];
            strcpy(this->name,domain_name);
          }
      }
    ~kd_thread_domain()
      { 
        while ((sequence_tail = sequence_head) != NULL)
          { sequence_head = sequence_tail->next; delete sequence_tail; }
        while ((sequence_tail = free_sequences) != NULL)
          { free_sequences = sequence_tail->next; delete sequence_tail; }
        if (name != NULL) delete[] name;
      }
    void set_background_domain(int queue_flags)
      { // Called when a background/safe-context queue that can schedule
        // jobs is attached to the domain.
        this->background = true;
        if (queue_flags & KDU_THREAD_QUEUE_SAFE_CONTEXT)
          this->safe_context = true;
      }
    bool check_match(const char *ref_name)
      { 
        if ((ref_name == NULL) || (*ref_name == '\0'))
          return (name == NULL);
        else
          return ((name != NULL) && (strcmp(name,ref_name) == 0));
      }
    bool is_default() { return (name == NULL); }
    kd_thread_domain_sequence *add_domain_sequence(kdu_long sequence_idx,
                                                   kdu_thread_entity *caller);
      /* This function is always called from a context in which the group
         mutex is locked.  It appends a new `kd_thread_domain_sequence' object
         to the end of the `sequence_list'.  In the process, any previous
         element of the list has the L bit of its `active_state' member
         reset to 0 (no longer the last element in the list) which may cause
         `kd_thread_domain_sequence::terminate' to be invoked and idle
         threads to be woken up.
         [//]
         The `caller' argument must be non-NULL if this function needs
         to terminate a previous domain sequence for the object.  In practice,
         this means that the function should always be called with a non-NULL
         caller argument, except when invoked from within the
         `get_initial_domain_sequence' function. */
    kd_thread_domain_sequence *get_initial_domain_sequence();
      /* This function is used to obtain the first element of the
         domain's sequence-list.  If the sequence list is currently empty,
         this function initializes it with an element that has sequence
         index 0, adding it to the set of `work_domains' managed by all
         relevant threads and adding them as consumers for the sequence.
         If there are already too many work domains, the function returns
         NULL without creating a domain sequence for the `domain' object.
         This function is always called from a context in which the group
         mutex is locked. */
    kd_thread_domain_sequence *
      get_active_domain_sequence(kdu_long seq_idx, bool &added_new_sequence,
                                 kdu_thread_entity *caller);
      /* As with the other domain sequence manipulation functions here,
         this function may only be called when the thread group's mutex is
         locked.  If the domain currently has no domain sequences at all,
         the `get_initial_domain_sequence' function is called; if this fails,
         the function returns NULL, meaning that there are too many work
         domains in the thread group -- a highly unlikely event.  More
         generally, the function scans through the list of existing domain
         sequences, looking for one that has the supplied sequence index and
         is still active (i.e., `active_state' must be non-zero).  If such
         an element is found, the `active_state' value is atomically
         incremented by 2 -- atomicity allows us to verify that the
         `active_state' never transitions to 0.  If a match is not found,
         the function either repurposes the tail of the sequence list
         to work with the new sequence index (this is possible if the
         tail has `active_state'=1, meaning that no queues are currently
         able to use it to schedule jobs), or else a new element is appended
         to the sequence list using `add_domain_sequence'.
            In any event, the function ensures that the `active_state' member
         of the returned object reflects the fact that a new thread queue is
         able to schedule jobs to the domain sequence (the function is called
         only from within `kdu_thread_entity::attach_queue') and the function
         also adds 1 to the returned object's `num_queues' member before
         returning.
            The `added_new_sequence' argument is set to false unless the
         function invokes `add_domain_sequence', in which case it is set
         to true.  The caller uses this information to determine whether it
         may be helpful to invoke `kdu_thread_entity::advance_work_domains' --
         see the explanation provided within the implementation of
         `kdu_thread_entity::attach_queue'. */
    void remove_unused_sequences();
      /* As with the other domain sequence manipulation functions here,
         this function must be called while the thread group's mutex is
         locked.  The function removes any leading elements from the
         domain's sequence list that have `num_consumers'=0,
         `num_queues'=0 and have been terminated.  This means, in
         particular that the function will never remove the tail of the
         sequence list and cannot remove any object that is being
         accessed from elsewhere.  It does not matter overly much that
         we remove every possible element from the list, since the purpose
         of this function is just to keep the sequence list from growing
         indefinitely. */
  public: // Data
    kd_thread_group *group; // Points to the thread group to which we belong
    kd_thread_domain *next;
    char *name; // NULL for the default domain
    bool background; // For thread domains that perform background jobs
    bool safe_context; // For thread domains that perform safe-context jobs
    int num_member_threads; // Num threads that prefer to work in this domain
    kd_thread_flags member_threads; // Set bits represent the threads
    kd_thread_domain_sequence *sequence_head; // See below
    kd_thread_domain_sequence *sequence_tail;
    kd_thread_domain_sequence *free_sequences; // Recycled domain sequences
  };
  /* Notes:
       The `sequence_head' may be NULL only if: a) no thread belongs to the
     domain; and b) no thread queue that can schedule jobs has yet been
     added to the domain.  When `sequence_head' becomes non-NULL for the
     first time, it is added to every thread's `work_domains' array.  The
     `sequence_tail' member points to the last element of the sequence list
     headed by `sequence_head'.
  */

/*****************************************************************************/
/*                              kd_thread_trace                              */
/*****************************************************************************/

class kd_thread_trace {
  public: // Member functions
    kd_thread_trace()
      { this->max_entries=0; this->num_entries.set(0); this->entries=NULL; }
    ~kd_thread_trace()
      { 
        if (entries != NULL)
          delete[] entries;
      }
    void init(int max_trace_entries)
      { 
        this->max_entries = max_trace_entries;
        this->num_entries.set(0);
        if (entries != NULL)
          { delete[] entries; entries = NULL; }
        if (max_trace_entries > 0)
          entries = new kd_thread_trace_entry[max_trace_entries];
      }
    void add_entry(int thread_idx, kd_thread_domain *job_domain,
                   int job_seq_idx, int job_idx, int job_count)
      { 
        kdu_int32 idx = num_entries.exchange_add(1);
        if (idx >= max_entries)
          { num_entries.set(max_entries); return; }
        entries[idx].thread_idx = thread_idx;
        entries[idx].job_domain = job_domain;
        entries[idx].job_seq_idx = job_seq_idx;
        entries[idx].job_idx = job_idx;
        entries[idx].job_count = job_count;
      }
      /* This function is called from within `kdu_thread_entity::process_jobs'
         when thread tracing is enabled, each time a thread does one of the
         following things:
         1) Goes idle:
            `job_domain'=NULL, `job_seq_idx'=1 (state), `job_idx'=0 (idle)
         2) Wakes from idle:
            `job_domain'=NULL, `job_seq_idx'=1 (state), `job_idx'=1 (wake)
         3) Receives wakeup signal:
            `job_domain'=NULL, `job_seq_idx'=-1 (event), `job_idx'=2 (revive)
         4) Schedules a job:
            `job_domain' != NULL, `job_seq_idx' = -1-domain sequence index,
            `job_idx'=previous job count, `job_count'=new job count
         5) Executes a job:
            `job_domain' != NULL, `job_seq_idx' is domain sequence index,
            `job_idx' is job index within its domain sequence,
            `jobs_scheduled' is total number of jobs scheduled within the
            domain sequence so far.
      */
    bool get_entry(int which, int &thread_idx, kd_thread_domain * &job_domain,
                   int &job_seq_idx, int &job_idx, int &job_count)
      { 
        if ((which < 0) || (which >= num_entries.get()) ||
            (which >= max_entries))
          return false;
        thread_idx = entries[which].thread_idx;
        job_domain = entries[which].job_domain;
        job_seq_idx = entries[which].job_seq_idx;
        job_idx = entries[which].job_idx;
        job_count = entries[which].job_count;
        return true;
      }
  private: // Declarations
    struct kd_thread_trace_entry {
      int thread_idx;
      kd_thread_domain *job_domain;
      int job_seq_idx, job_idx, job_count;
    };
  private: // Data
    kdu_int32 max_entries;
    kdu_interlocked_int32 num_entries;
    kd_thread_trace_entry *entries;
  };

/*****************************************************************************/
/*                              kd_thread_group                              */
/*****************************************************************************/

struct kd_thread_group {
  public: // Data
    kd_thread_group();
    ~kd_thread_group();
    kd_thread_domain *get_domain(const char *domain_name);
      /* This function must be called from a context in which the group
         mutex is locked (unless the thread group is just being created).
         The function searches for a domain whose name matches `domain_name'
         (may be NULL or an empty string for the default domain).  If the
         domain does not exist, it is created.  Newly created domains are
         not initially assigned any `kd_thread_domain_sequence' object --
         that is done by `kd_thread_domain::get_initial_domain_sequence' or
         `kd_thread_domain::get_active_domain_sequence'.  The
         present function should not fail or return NULL. */
    void wake_idle_threads_for_domain(int num, kd_thread_domain *domain)
      { // Wake up to `num' threads from the idle pool, based upon the
        // preferences associated with `domain' (if non-NULL).
        if (!idle_pool.test((kd_thread_flags)(-1))) return;
        kd_thread_flags members = (domain!=NULL)?(domain->member_threads):0;
        kd_thread_flags non_waiting = non_waiting_worker_flags.get();
        kd_thread_flags non_waiting_members = members & non_waiting;
        kd_thread_flags all = (kd_thread_flags) -1;
        int nleft, nfound=0, indices[KDU_MAX_THREADS];
        if (idle_pool.test(non_waiting_members))
          nfound = idle_pool.remove_any(non_waiting_members,num,indices);
        if (((nleft = num-nfound) > 0) && idle_pool.test(non_waiting))
          nfound += idle_pool.remove_any(non_waiting,nleft,indices+nfound);
        if (((nleft = num-nfound) > 0) && idle_pool.test(members))
          nfound += idle_pool.remove_any(members,nleft,indices+nfound);
        if (((nleft = num-nfound) > 0) && idle_pool.test(all))
          nfound += idle_pool.remove_any(all,nleft,indices+nfound);
        for (int n=0; n < nfound; n++) wake_thread(indices[n]);
      }
    bool wake_thread(int idx)
      { // Just signals the semaphore associated with index `idx'
        if ((idx < 0) || (idx >= num_threads))
          { abort(); return false; }
#ifdef KDU_THREAD_TRACE_RECORDS
        thread_trace.add_entry(idx,NULL,-1,2,0); // Event=-1, revive=2
#endif        
        return thread_semaphores[idx].signal();
      }
    void wait(kdu_thread_entity *thrd)
      { // Waits upon the semaphore associated with thread `thrd'
        int idx = thrd->thread_idx;
#ifdef KDU_THREAD_TRACE_RECORDS
        thread_trace.add_entry(idx,NULL,1,0,0); // State=1, idle=0
#endif
        thread_semaphores[idx].wait();
#ifdef KDU_THREAD_TRACE_RECORDS
        thread_trace.add_entry(idx,NULL,1,1,0); // State=1, wake=1
#endif
      }
  //---------------------------------------------------------------------------
  public: // Machinery for working with job palettes
    kd_thread_palette *
      get_palette_to_schedule(kdu_thread_job *job, kdu_thread_entity *caller)
      { /* This function swaps the palette referenced from within
           `job->palette_ref' with a palette that has been cleaned of hazards
           within the `caller' thread's internal palette heap, unless it is
           already clear of hazards.  A palette is deemed to be clear of
           hazards if its first slot holds NULL.  Otherwise, the value in
           the first slot must be one of `KD_PLT_HAZARD_MKR' or a valid
           pointer to a `kd_thread_domain_sequence' object at whose head the
           palette was last known to reside.
           [//]
           If the function swaps the palette referenced from within
           `job->palette_ref', the new palette is written to the
           `job->palette_ref->palette' member.  Whether swapped or not, the
           function sets the referenced palette's first slot to point to
           `job', it sets all other slots within the palette to NULL, and it
           sets the `kd_thread_palette::nxt' member to NULL, before returning
           a pointer to the palette.
           [//]
           NOTE CAREFULLY: This function must be called prior to passing
           a palette to `kd_thread_domain_sequence::append_jobs', since that
           function requires `job->palette_ref->palette->slot[0]' to point to
           `job'.  Moreover, the function must not be called again until after
           `job' has been dequeued via `kd_thread_domain_sequence::get_job',
           since that function sets `job->palette_ref->palette->slot[0]' to
           point to the `kd_thread_domain_sequence' object from which the job
           was dequeued.  Obviously, slot 0 of a `kd_thread_palette' object
           is dangerously aliased, having an interpretation which depends upon
           the context in which the palette is found, so failing to follow
           these guidelines may lead to access faults or something worse.  We
           do this deliberately because palette object alignment
           considerations mean that extra fields cannot be created within the
           object without reducing the already limited number of available job
           slots; these constraints are only significant on 64-bit
           platforms. */
        kd_thread_palette_ref *pref = job->palette_ref;
        kd_thread_palette *plt=pref->palette, **swp_heap=caller->palette_heap;
        while (plt->slot[0].get() != NULL)
          { // Loop until we find a clean palette
            assert(plt->slot[0].get() == KD_PLT_HAZARD_MKR);
            int swp_idx = caller->next_palette_idx;
            if (swp_idx >= KD_THREAD_PALETTES)
              { clean_dirty_palettes(caller); swp_idx = 0; }
            caller->next_palette_idx = swp_idx+1;
            pref->palette = swp_heap[swp_idx];
            swp_heap[swp_idx] = plt;
            plt = pref->palette;
          }
        plt->init(job);
        return plt;
      }
    void clean_dirty_palettes(kdu_thread_entity *caller);
      /* This function is called once all of the palettes in the `caller's
         private palette heap are "dirty".  A dirty palette is one whose
         first slot is non-NULL. */
    kd_thread_palette_ref *allocate_palettes(int num_plts);
      /* Called while the group mutex is locked.  Note that the palettes
         allocated by this function will have their first slot equal to NULL
         if and only if they are known to be hazard free.  Otherwise, the
         first slot will hold `KD_PLT_HAZARD_MKR', meaning that the palette
         may either be at the head of a domain sequence object's job queue
         or else the subject of hazardous references from other threads.
         You should bear in mind that the `nxt' member of any palette
         allocated by this function is sensitive.  If the palette is
         hazardous, the `nxt' field may or may not equal `KD_PLT_DEQUEUE_MKR'
         and you must not reset or otherwise meddle with the `nxt' field.
         These special marker values are read and manipulated only by
         `kd_thread_domain_sequence::get_job' and
         `kd_thread_queue::get_palette_to_schedule'. */
    void release_palettes(kd_thread_palette_ref *first_plt)
      { /* Note: this function may be called without locking the group mutex.
           As suggested by the discussion following `allocate_palettes', the
           palettes released here may be the subject of hazardous references
           that might not be cleaned until the palettes are re-allocated and
           passed to `get_palette_to_schedule' during job scheduling. */
        if (first_plt == NULL) return;
        kd_thread_palette_ref *nxt_plt, *last_plt=first_plt;
        for (; last_plt->next != NULL; last_plt=last_plt->next);
        do { // Enter compare-and-set loop
          nxt_plt = (kd_thread_palette_ref *) returned_palettes.get();
          last_plt->next = nxt_plt;
        } while (!returned_palettes.compare_and_set(nxt_plt,first_plt));
      }
  //---------------------------------------------------------------------------
  private: // Helper functions
    kd_thread_palette_ref *augment_free_palettes();
    /* Called while group mutex locked when `free_palettes' is NULL.
       Augments the free list and returns the new `free_palettes' head. */
  //---------------------------------------------------------------------------
  private: // Internal definitions
    struct kd_palette_block {
      kdu_byte *handle; // Used to free the memory block
      kd_palette_block *next;
      kd_thread_palette palettes[256];
      kd_thread_palette_ref palette_refs[256]; // Refs to carry the palettes
    };
  //---------------------------------------------------------------------------
  public: // Threads and their properties
    KD_PUBLIC_CACHELINE_SEPARATOR(_leadin)
    kdu_int64 cpu_affinity_masks[KDU_MAX_THREADS]; // Records info passed to
    kdu_int32 cpu_affinity_contexts[KDU_MAX_THREADS]; // `set_cpu_affinity'.
    int min_thread_concurrency;
    int num_threads; // All threads, regardless of domain
    int worker_thread_yield_freq; // Used by `add_thread'
    int saved_job_counts[KDU_MAX_THREADS]; // Used to improve reliability
       // of the `kdu_thread_entity::get_job_count_stats' function.
    kdu_thread_entity *threads[KDU_MAX_THREADS]; // 1'st is the group owner
    kdu_semaphore thread_semaphores[KDU_MAX_THREADS]; // 1 per thread
    kd_thread_job_hzp thread_job_hzps[KDU_MAX_THREADS]; // 1 per thread
  public: // Structural information
    int num_domains; // Must not exceed `KDU_MAX_DOMAINS'
    kd_thread_domain *domain_list; // All domains within the thread group
    kdu_thread_queue *top_queues; // Maintained to facilitate safe cleanup
    kdu_thread_context *contexts;
    kd_thread_trace thread_trace; // Initialized only if thread tracing enabled
  //---------------------------------------------------------------------------
  public: // Job palette resource management
    kd_palette_block *palette_blocks; // List holds all the job palette memory
    kd_thread_palette_ref *free_palettes; // Recycled palettes
  //---------------------------------------------------------------------------
  // Synchronization objects and atomically accessed variables
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep1)
    kdu_interlocked_ptr returned_palettes; // See below
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep2)
    kdu_mutex mutex; // Guards access to all thread and queue members
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep3)
    kd_thread_idle_pool idle_pool;
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep4)
    kd_interlocked_thread_flags non_waiting_worker_flags; // See below
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep5)
    kdu_interlocked_int32 num_non_waiting_workers; // See below
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep6)
    kdu_interlocked_int32 exceptional_join_dependencies; // See below
    KD_PUBLIC_CACHELINE_SEPARATOR(_synch_sep7)
  //---------------------------------------------------------------------------
  public: // Other state information
    kd_thread_grouperr grouperr;
    bool destruction_requested;
    KD_PUBLIC_CACHELINE_SEPARATOR(_trailer);
  };
  /* Notes:
        The object manages the `kd_thread_palette' and `kd_thread_palette_ref'
     objects used by all of its domain sequence objects.  Job palettes are
     allocated to queues during calls to `kdu_thread_entity::attach_queue',
     and this is done from within a critical section locked by the group
     `mutex'.  Within this critical section, the `palette_blocks' and
     `free_palettes' members may be freely manipulated.  Lists of palette
     references are returned to the group when a thread invokes
     `kdu_thread_queue::all_complete', but that function does not
     execute within a critical section -- this is deliberate, since many
     threads might otherwise contend for access to the critical section.
     For this reason, job palettes are returned via the interlocked
     `returned_palettes' member.  When new job palettes are required, an
     atomic exchange is used to move any list anchored at `returned_palettes'
     across to the `free_palettes' list, before pulling off individual
     palettes.
        `num_non_waiting_workers' keeps track of the number of theads that
     are currently executing within the `kdu_thread_entity::process_jobs'
     function that have been invoked with a NULL `cond' argument -- these are
     necessarily not the group owner thread (i.e., worker threads) and not
     threads that are in the process of executing
     `kdu_thread_entity::wait_for_condition' or a related function, such
     as `kdu_thread_entity::join' or `kdu_thread_entity::terminate'.  The
     value is increased when a new worker thread is added to the group or
     when a thread returns from a waiting context to a non-waiting one.  The
     value is decreased when a worker thread that was not waiting on a
     condition starts to do so.
        `non_waiting_worker_flags' contains one single-bit flag for each
     thread.  The flags have the same association with threads as those used
     by the `idle_pool'.  If a flag is set in this variable, the corresponding
     thread is one of the non-waiting worker threads counted in the
     `num_non_waiting_workers' count.
        `exceptional_join_dependencies' is used to correctly implement the
     `kdu_thread_entity::wait_for_exceptional_join' function.  This variable
     is divided into two 16-bit words.  The least significant word identifies
     the number of threads that are inside `kdu_thread_entity::process_jobs',
     but not executing the `kdu_thread_entity::wait_for_exceptional_join'
     function.  The most significant word holds either 0 or 1+T where T is
     the index of a thread that is waiting inside the
     `kdu_thread_entity::wait_for_exceptional_join'.  When a thread's return
     from `kdu_thread_entity::process_jobs' causes the least significant word
     to become 0, any thread identified by the most significant word is
     woken up (by signalling its semaphore).  If multiple threads need to be
     woken up, one of them is identified by the most significant word of the
     `exceptional_join_dependencies' variable, while that thread temporarily
     holds the identity of the next thread to be woken up, and so forth.  Note
     that threads waiting inside `kdu_thread_entity::wait_for_exceptional_join'
     do not add themselves to the `idle_pool'.
  */

} // namespace kd_core_local

/*****************************************************************************/
/*                         Inline Implementations                            */
/*****************************************************************************/

void kdu_core::kdu_thread_entity::lock_group_mutex()
{
  assert(group != NULL);
  assert(check_current_thread()); // In case a function was called with
                                  // another's `kdu_thread_entity' reference
  if ((group_mutex_lock_count == 0) && (group != NULL))
    group->mutex.lock();
  group_mutex_lock_count++;
}

void kdu_core::kdu_thread_entity::unlock_group_mutex()
{
  assert((group_mutex_lock_count > 0) && (group != NULL));
  if (((--group_mutex_lock_count) == 0) && (group != NULL))
    group->mutex.unlock();
}

#endif // THREADS_LOCAL_H
