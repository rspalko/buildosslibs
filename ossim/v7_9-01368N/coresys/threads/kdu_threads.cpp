/*****************************************************************************/
// File: kdu_threads.cpp [scope = CORESYS/THREADS]
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
   Implements the multi-threaded architecture described in "kdu_threads.h".
******************************************************************************/

#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include "kdu_messaging.h"
#include "threads_local.h"

using namespace kd_core_local;

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
   kdu_error _name("E(threads.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
   kdu_warning _name("W(threads.cpp)",_id);
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

/* ========================================================================= */
/*                             kdu_thread_object                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    kdu_thread_object::kdu_thread_object                   */
/*****************************************************************************/

kdu_thread_object::kdu_thread_object(const char *name_string)
{
  name = NULL; next = NULL;
  if (name_string == NULL) return;
  name = new char[strlen(name_string)+1];
  strcpy(name,name_string);
}

/*****************************************************************************/
/*                   kdu_thread_object::~kdu_thread_object                   */
/*****************************************************************************/

kdu_thread_object::~kdu_thread_object()
{
  if (name != NULL)
    delete[] name;
}


/* ========================================================================= */
/*                                kdu_thread                                 */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     kd_thread_tls_slot                             */
/*****************************************************************************/

namespace kd_core_local {
  
class kd_thread_tls {
  public: // Member functions
    kd_thread_tls()
      { // Acquire the slot
        slot_key_valid = false;
#if (defined KDU_WIN_THREADS)
        slot_key = TlsAlloc();
        slot_key_valid = (slot_key != TLS_OUT_OF_INDEXES);
#elif (defined KDU_PTHREADS)
        if (pthread_key_create(&slot_key,NULL) == 0)
          slot_key_valid = true;
#endif
      }
    ~kd_thread_tls()
      { // Release the slot
#if (defined KDU_WIN_THREADS)
        if (slot_key_valid)
          TlsFree(slot_key);
#elif (defined KDU_PTHREADS)
        if (slot_key_valid)
          pthread_key_delete(slot_key);
#endif
      }
    void *get_val()
      { // Get the value for current thread stored in TLS slot
#if (defined KDU_WIN_THREADS)
        return (slot_key_valid)?TlsGetValue(slot_key):NULL;
#elif (defined KDU_PTHREADS)
        return (slot_key_valid)?pthread_getspecific(slot_key):NULL;
#else // KDU_PTHREADS
        return NULL;
#endif
      }
    bool set_val(void *val)
      { // Store `val' in current thread's version of TLS slot
#if (defined KDU_WIN_THREADS)
        return (slot_key_valid && (TlsSetValue(slot_key,val) != 0));
#elif (defined KDU_PTHREADS)
        return (slot_key_valid && (pthread_setspecific(slot_key,val) == 0));
#else
        return false;
#endif
      }
  private: // Data
    bool slot_key_valid;
#if (defined KDU_WIN_THREADS)
    DWORD slot_key;
#elif (defined KDU_PTHREADS)
    pthread_key_t slot_key;
#endif
  };
  
} // namespace kd_core_local

static kd_core_local::kd_thread_tls kd_thread_tls_slot; // Grabs us a TLS slot
                                                        // at startup.

/*****************************************************************************/
/* EXTERN                kd_thread_create_entry_point                        */
/*****************************************************************************/
namespace kdu_core {

kdu_thread_startproc_result KDU_THREAD_STARTPROC_CALL_CONVENTION
  kd_thread_create_entry_point(void *param)
{
  kdu_thread *obj = (kdu_thread *) param;
  kdu_thread_startproc_result result;
  kd_thread_tls_slot.set_val(obj);
  try {
    kdu_thread_startproc start_proc = obj->run_start_proc;
    void *start_arg = obj->run_start_arg;
    result = start_proc(start_arg);
  }
  catch (...) {
    kd_thread_tls_slot.set_val(NULL);
    obj->cleanup_thread_objects();
    throw;
  }
  kd_thread_tls_slot.set_val(NULL);
  obj->cleanup_thread_objects();
  return result;
}
  
} // namespace kdu_core

/*****************************************************************************/
/* STATIC                 kdu_thread::get_thread_ref                         */
/*****************************************************************************/

kdu_thread *kdu_thread::get_thread_ref()
{
  return (kdu_thread *) kd_thread_tls_slot.get_val();
}

/*****************************************************************************/
/*                            kdu_thread::create                             */
/*****************************************************************************/

bool
  kdu_thread::create(kdu_thread_startproc start_proc, void *start_arg)
{
  if (can_destroy)
    return false;
  if (start_proc == NULL)
    set_to_self();
  else
    { 
#if defined KDU_WIN_THREADS
      run_start_proc = start_proc;  run_start_arg = start_arg;
      if ((thread = CreateThread(NULL,0,kd_thread_create_entry_point,this,
                                 0,&thread_id)) != NULL)
        can_destroy = true;
      else
        { run_start_proc = NULL;  run_start_arg = NULL; }
#elif defined KDU_PTHREADS
#  if (defined __APPLE__)
      run_start_proc = start_proc;  run_start_arg = start_arg;
      thread_valid = (pthread_create(&thread,NULL,
                                     kd_thread_create_entry_point,this) == 0);
      if (thread_valid)
        mach_thread_id = pthread_mach_thread_np(thread);
#  else // Use POSIX threads
      run_start_proc = start_proc;  run_start_arg = start_arg;
      thread_valid = (pthread_create(&thread,NULL,
                                     kd_thread_create_entry_point,this) == 0);
#  endif // !__APPLE
      can_destroy = thread_valid;
      if (!thread_valid)
        { run_start_proc = NULL;  run_start_arg = NULL; }
#endif
    }
  return exists();
}

/*****************************************************************************/
/*                            kdu_thread::destroy                            */
/*****************************************************************************/

bool kdu_thread::destroy()
{
  if (!can_destroy) return false;
  bool result = true;
#if defined KDU_WIN_THREADS
  if ((thread != NULL) && (!check_self()) &&
      ((WaitForSingleObject(thread,INFINITE)!=WAIT_OBJECT_0) ||
       !CloseHandle(thread)))
    result = false;
  thread = NULL;
#elif defined KDU_PTHREADS
  if (thread_valid && (!check_self()) &&
      (pthread_join(thread,NULL) != 0))
    result = false;
  thread_valid = false;
#endif
  cleanup_thread_objects(); // In case destroying own `kdu_thread'
  run_start_proc = NULL; run_start_arg = NULL;
  can_destroy = false;
  return result;
}

/*****************************************************************************/
/*                      kdu_thread::add_thread_object                        */
/*****************************************************************************/

bool
  kdu_thread::add_thread_object(kdu_thread_object *obj)
{
  if (!(can_destroy && check_self()))
    return false;
  kdu_thread_object *scan=thread_objects;
  while ((scan != NULL) && (scan != obj))
    scan = scan->next;
  if (scan == obj) return true;
  obj->next = thread_objects; thread_objects = obj;
  return true;
}

/*****************************************************************************/
/*                     kdu_thread::find_thread_object                        */
/*****************************************************************************/

kdu_thread_object *
  kdu_thread::find_thread_object(const char *name)
{
  kdu_thread_object *scan=thread_objects;
  while ((scan != NULL) && !scan->check_name(name))
    scan = scan->next;
  return scan;
}

/*****************************************************************************/
/*                    kdu_thread::cleanup_thread_objects                     */
/*****************************************************************************/

void kdu_thread::cleanup_thread_objects()
{ 
  while (thread_objects != NULL)
    { 
      kdu_thread_object *obj = thread_objects;
      thread_objects = obj->next;
      delete obj;
    }
}

/*****************************************************************************/
/*                       kdu_thread::set_cpu_affinity                        */
/*****************************************************************************/

bool kdu_thread::set_cpu_affinity(kdu_int64 affinity_mask,
                                  int affinity_context)
{
#if (defined KDU_WIN_THREADS)
  bool success=false;
  { 
#  if (_WIN32_WINNT >= 0x0601)
    HINSTANCE hinst = LoadLibrary("Kernel32.dll");
    if (hinst != NULL)
      { 
        typedef BOOL (WINAPI *SetThreadGroupAffinity_FUNC)
          (HANDLE, const GROUP_AFFINITY *, PGROUP_AFFINITY);
        SetThreadGroupAffinity_FUNC SetThreadGroupAffinity_RT =
          (SetThreadGroupAffinity_FUNC)
            GetProcAddress(hinst,"SetThreadGroupAffinity");
        if (SetThreadGroupAffinity_RT != NULL)
          { 
            GROUP_AFFINITY grp;
            memset(&grp, 0, sizeof(GROUP_AFFINITY));
            grp.Group = (WORD)affinity_context;
            grp.Mask = (KAFFINITY)affinity_mask;
            if (SetThreadGroupAffinity_RT(thread, &grp, NULL))
              success = true;
          }
        FreeLibrary(hinst);
      }
#  endif // Windows 7 or above
#  if (defined _WIN64)
    if ((!success) &&
        SetThreadAffinityMask(thread,(ULONG_PTR) affinity_mask))
      success = true;
#  else // 32-bit system
    if ((!success) &&
        SetThreadAffinityMask(thread,(DWORD) affinity_mask))
      success = true;
#  endif // 32-bit system
  }
  return success;
#elif (defined __ANDROID__)
  return false;
#elif (defined KDU_PTHREADS)
  {
#  if (defined _GNU_SOURCE)
    int t;  kdu_long test_mask;  cpu_set_t cpuset;  CPU_ZERO(&cpuset);
    for (t=0, test_mask=1; test_mask != 0; t++, test_mask<<=1)
      if (affinity_mask & test_mask)
        CPU_SET(affinity_context+t,&cpuset);
    return (pthread_setaffinity_np(thread,sizeof(cpuset),&cpuset)==0);
#  elif (defined __APPLE__)
    thread_affinity_policy policy;
    policy.affinity_tag = THREAD_AFFINITY_TAG_NULL;
    int t;
    kdu_long test_mask=1;
    for (t=1; test_mask != 0; t++, test_mask<<=1)
      if (affinity_mask & test_mask)
        { 
          policy.affinity_tag = t; // Install 1st logical CPU index
          break;
        }
    for (test_mask<<=1, t++; test_mask != 0; t++, test_mask<<=1)
      if (affinity_mask & test_mask)
        { 
          policy.affinity_tag &= 0xFF00; // Remove any existing 2nd CPU
          policy.affinity_tag |= (t << 8); // index and install new one
        }
    policy.affinity_tag |= (affinity_context << 16);
    return (thread_policy_set(mach_thread_id,THREAD_AFFINITY_POLICY,
                              (thread_policy_t) &policy,
                              THREAD_AFFINITY_POLICY_COUNT) ==
            KERN_SUCCESS);
#  else
    return false;
#  endif
  }
#else
  return false;
#endif
}

/*****************************************************************************/
/*                         kdu_thread::get_priority                          */
/*****************************************************************************/

int kdu_thread::get_priority(int &min_priority, int &max_priority)
{
#if (defined KDU_WIN_THREADS)
  min_priority = -2;  max_priority = 2;
  return GetThreadPriority(thread);
#elif (defined KDU_NO_SCHED_SUPPORT)
  return (min_priority = max_priority = 0);
#elif (defined KDU_PTHREADS)
  sched_param param;  int policy;
  if (pthread_getschedparam(thread,&policy,&param) != 0)
    return (min_priority = max_priority = 0);
  min_priority = sched_get_priority_min(policy);
  max_priority = sched_get_priority_max(policy);
  return param.sched_priority;
#else
  return (min_priority = max_priority = 0);
#endif
}

/*****************************************************************************/
/*                         kdu_thread::set_priority                          */
/*****************************************************************************/

bool kdu_thread::set_priority(int priority)
{
#if (defined KDU_WIN_THREADS)
  if (priority < -2) priority = -2;
  if (priority > 2) priority = 2;
  return (SetThreadPriority(thread,priority)!=FALSE);
#elif (defined KDU_NO_SCHED_SUPPORT)
  return false;
#elif (defined KDU_PTHREADS)
  sched_param param;  int policy;
  if (pthread_getschedparam(thread,&policy,&param) != 0)
    return false;
  param.sched_priority = priority;  
  return (pthread_setschedparam(thread,policy,&param) == 0);
#else
  return false;
#endif
}


/* ========================================================================= */
/*                            INTERNAL FUNCTIONS                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                      kd_set_threadname                             */
/*****************************************************************************/

#if (defined _WIN32 && defined _DEBUG && defined _MSC_VER && (_MSC_VER>=1300))
static void
  kd_set_threadname(LPCSTR thread_name)
{
  struct {
      DWORD dwType; // must be 0x1000
      LPCSTR szName; // pointer to name (in user addr space)
      DWORD dwThreadID; // thread ID (-1=caller thread)
      DWORD dwFlags; // reserved for future use, must be zero
    } info;
  info.dwType = 0x1000;
  info.szName = thread_name;
  info.dwThreadID = -1;
  info.dwFlags = 0;
  __try {
      RaiseException(0x406D1388,0,sizeof(info)/sizeof(DWORD),
                     (ULONG_PTR *) &info );
    }
  __except(EXCEPTION_CONTINUE_EXECUTION) { }
}
#endif // .NET debug compilation

/*****************************************************************************/
/*                             worker_startproc                              */
/*****************************************************************************/

/* The following definition allocates 36K of the thread's stack for use as
   a general purpose memory block, intended primarily for use by the block
   encoding/decoding machinery. */
#define KD_DONATED_STACK_BLOCK_BYTES (36*1024)

namespace kdu_core {
  
kdu_thread_startproc_result
  KDU_THREAD_STARTPROC_CALL_CONVENTION worker_startproc(void *param)
{
  kdu_thread_entity *ent = (kdu_thread_entity *) param;
#if (defined _WIN32 && defined _DEBUG && defined _MSC_VER && (_MSC_VER>=1300))
  const char *domain_name = ent->thread_domain->name;
  if ((domain_name == NULL) || (*domain_name == '\0'))
    domain_name = "Kakadu default thread domain";
  kd_set_threadname(domain_name);
#endif // .NET debug compilation
  int t = ent->thread_idx;
  assert(t < KDU_MAX_THREADS);
  kd_thread_group *group = ent->group;
  if (group->cpu_affinity_masks[t] != 0)
    ent->thread.set_cpu_affinity(group->cpu_affinity_masks[t],
                                 group->cpu_affinity_contexts[t]);
  ent->pre_launch();
  kdu_byte stack_block[KD_DONATED_STACK_BLOCK_BYTES];
  ent->donate_stack_block(stack_block,KD_DONATED_STACK_BLOCK_BYTES);
  ent->process_jobs(NULL);
  ent->pre_destroy();
  return KDU_THREAD_STARTPROC_ZERO_RESULT;
}
  
} // namespace kdu_core


/* ========================================================================= */
/*                            kd_thread_idle_pool                            */
/* ========================================================================= */

/*****************************************************************************/
/*                      kd_thread_idle_pool::remove_any                      */
/*****************************************************************************/

int kd_thread_idle_pool::remove_any(kd_thread_flags mask,
                                    int num_wanted, int indices[])
{
  kd_thread_flags old_val, new_val;
  int n, count;
  do { // Need to loop if an intervening thread wakes some threads up
    old_val = idle_flags.get();
    kd_thread_flags scan_mask = mask & old_val; // Threads we might wake up
    kd_thread_flags wake_mask=0, wake_flag=1;
    for (count=0, n=0;
         (scan_mask != 0) && (n < KDU_MAX_THREADS);
         n+=8, scan_mask>>=8, wake_flag<<=8)
      { 
        int byte = (int)(scan_mask & 0xFF);
        if (byte == 0)
          continue;
        for (int k=0; k < 8; k++, byte>>=1)
          if (byte & 1)
            { 
              indices[count++] = n+k;
              wake_mask |= (wake_flag<<k);
              if (count == num_wanted)
                { // Force outer loop to exit
                  scan_mask = 0;
                  break;
                }
            }
      }
    if (wake_mask == 0)
      break;
    new_val = old_val ^ wake_mask;
    assert(new_val == (new_val & old_val));
  } while (!idle_flags.compare_and_set(old_val,new_val));
  return count;
}


/* ========================================================================= */
/*                         kd_thread_domain_sequence                         */
/* ========================================================================= */

/*****************************************************************************/
/*                   kd_thread_domain_sequence::terminate                    */
/*****************************************************************************/

void kd_thread_domain_sequence::terminate(kd_thread_job_hzp *hzp)
{ 
  kd_thread_palette *plt = terminator;
  assert(plt->slot[0].get() == (void *)KD_JOB_TERMINATOR);
  plt->nxt.set(plt); // Points back to itself so the terminator is not removed
  append_jobs(plt,plt,1,hzp);
  domain->group->wake_idle_threads_for_domain(KDU_MAX_THREADS,NULL);
}


/* ========================================================================= */
/*                             kd_thread_domain                              */
/* ========================================================================= */

/*****************************************************************************/
/*                   kd_thread_domain::add_domain_sequence                   */
/*****************************************************************************/

kd_thread_domain_sequence *
  kd_thread_domain::add_domain_sequence(kdu_long seq_idx,
                                        kdu_thread_entity *caller)
{ // Called with group mutex locked
  kd_thread_domain_sequence *seq = free_sequences;
  if (seq == NULL)
    seq = new kd_thread_domain_sequence(this,seq_idx);
  else
    { 
      free_sequences = seq->next;
      seq->init(this,seq_idx);
    }
  seq->active_state.set(1); // Set the `L' bit -- this one is the last element
  if (sequence_tail == NULL)
    sequence_head = sequence_tail = seq;
  else
    { 
      kd_thread_domain_sequence *old_tail = sequence_tail;
      sequence_tail = sequence_tail->next = seq;
      kdu_int32 old_active_state = old_tail->active_state.exchange_add(-1);
      assert(old_active_state & 1); // L-bit must have been non-zero
      assert(caller != NULL);
      if (old_active_state == 1)
        old_tail->terminate(caller->hzp);
    }
  return seq;
}

/*****************************************************************************/
/*              kd_thread_domain::get_initial_domain_sequence                */
/*****************************************************************************/

kd_thread_domain_sequence *
  kd_thread_domain::get_initial_domain_sequence()
{ // Called with group mutex locked
  if (this->check_match(NULL))
    { 
      assert(0); // You cannot schedule jobs in the default domain
      return NULL;
    }
  kd_thread_domain_sequence *seq = sequence_head;
  if (seq == NULL)
    { // Create initial domain sequence
      assert(group->num_threads > 0);
      kdu_thread_entity *thrd = group->threads[0];
      if (thrd->num_work_domains >= KDU_MAX_DOMAINS)
        return NULL;
      seq = this->add_domain_sequence(0,NULL);
      for (int n=0; n < group->num_threads; n++)
        { 
          kdu_thread_entity *thrd = group->threads[n];
          assert(thrd->num_work_domains < KDU_MAX_DOMAINS);
          thrd->work_domains[thrd->num_work_domains++] = seq->add_consumer();
            /* Note: it does not matter what order `thrd->num_work_domains'
               and the `thrd->work_domains' array entries are modified in,
               because `thrd->num_work_domains' is not accessed while a
               thread looks for work; instead, the entries of the
               `work_domains' array are tested one by one until one
               is NULL (or `KDU_MAX_DOMAINS' is reached) and all entries of
               the array were set to NULL when the thread was created. */
        }
    }
  return seq;
}

/*****************************************************************************/
/*               kd_thread_domain::get_active_domain_sequence                */
/*****************************************************************************/

kd_thread_domain_sequence *
  kd_thread_domain::get_active_domain_sequence(kdu_long seq_idx,
                                               bool &newly_added,
                                               kdu_thread_entity *caller)
{ // Called with group mutex locked
  newly_added = false;
  kd_thread_domain_sequence *seq = sequence_head;
  if ((seq == NULL) && ((seq = get_initial_domain_sequence()) == NULL))
    return NULL;
  
  // Start by exploring non-terminal elements of the sequence list
  bool have_removable_sequences = ((seq->sequence_idx < seq_idx) &&
                                   (seq->num_consumers.get() == 0) &&
                                   (seq->num_queues.get() == 0) &&
                                   (seq->next != NULL));
  for (; seq->next != NULL; seq=seq->next)
    { 
      if (seq->sequence_idx != seq_idx)
        continue;
      kdu_int32 old_state, new_state;
      do { // Enter compare-and-set loop
        old_state = new_state = seq->active_state.get();
        if (old_state <= 0)
          break; // Not active
        new_state += 2;
      } while (!seq->active_state.compare_and_set(old_state,new_state));
      assert((old_state >= 0) && !(old_state & 1));
      if (old_state > 0)
        { // Active -- and we have already added to the active count
          assert(new_state > 2);
          seq->num_queues.exchange_add(1);
          return seq;
        }
    }
  
  // If we get here, we are looking at the last element of the list
  assert(seq == sequence_tail);
  kdu_int32 old_state, new_state;
  bool can_use_tail = false;
  do { // Enter compare-and-set loop
    old_state = new_state = seq->active_state.get();
    can_use_tail = ((old_state == 1) || (seq->sequence_idx == seq_idx));
    if (!can_use_tail)
      break;
    new_state = old_state + 2;
  } while (!seq->active_state.compare_and_set(old_state,new_state));
  assert(old_state & 1); // L bit marks `seq' as tail of sequence list
  if (can_use_tail)
    { 
      assert(new_state >= 3);
      seq->num_queues.exchange_add(1);
      seq->sequence_idx = seq_idx; // May or may not change it
    }
  else
    { // We need to append a new element to the sequence list
      seq = add_domain_sequence(seq_idx,caller);
      seq->num_queues.set(1);   // No other thread can manipulate these
      seq->active_state.set(3); // member variables yet, so we can avoid the
                                // overhead of atomic synchronized operations.
      newly_added = true;
    }
  if (have_removable_sequences)
    this->remove_unused_sequences(); // Clears any initial elements from the
      // sequence list that were not removed when the number of consumers went
      // to 0 because there were still queues attached; these are not removed
      // when the queues detach because that operation does not normally need
      // to lock the group mutex, so it is reasonable to just stripe away any
      // strays here, at the point when we need to add extras.
  return seq;
}

/*****************************************************************************/
/*                 kd_thread_domain::remove_unused_sequences                 */
/*****************************************************************************/

void kd_thread_domain::remove_unused_sequences()
{ // Called with group mutex locked
  kd_thread_domain_sequence *seq;
  while (((seq=sequence_head) != NULL) &&
         (seq->num_consumers.get() == 0) &&
         (seq->num_queues.get() == 0) &&
         seq->check_queue_terminated())
    { 
      assert((seq->next != NULL) && (seq->active_state.get() == 0));
      sequence_head = seq->next;
      seq->next = free_sequences;
      free_sequences = seq;
    }
}


/* ========================================================================= */
/*                              kd_thread_group                              */
/* ========================================================================= */

/*****************************************************************************/
/*                       kd_thread_group::kd_thread_group                    */
/*****************************************************************************/

kd_thread_group::kd_thread_group()
{
  memset(cpu_affinity_masks,0,sizeof(kdu_int64)*KDU_MAX_THREADS);
  memset(cpu_affinity_contexts,0,sizeof(kdu_int32)*KDU_MAX_THREADS);
  min_thread_concurrency = 0;
  num_threads = 0;
  worker_thread_yield_freq = 100; // Default value
  memset(saved_job_counts,0,sizeof(int)*KDU_MAX_THREADS);
  memset(threads,0,sizeof(void *)*KDU_MAX_THREADS);
  memset(thread_job_hzps,0,sizeof(kd_thread_job_hzp)*KDU_MAX_THREADS);
  num_domains = 0;
  domain_list = NULL;
  top_queues = NULL;
  contexts = NULL;
#ifdef KDU_THREAD_TRACE_RECORDS
  thread_trace.init(KDU_THREAD_TRACE_RECORDS);
#endif // KDU_THREAD_TRACE_RECORDS
  palette_blocks = NULL; free_palettes = NULL;
  returned_palettes.set(NULL);
  num_non_waiting_workers.set(0);
  non_waiting_worker_flags.set(0);
  exceptional_join_dependencies.set(0);
  grouperr.failed = false;
  grouperr.failure_code=KDU_NULL_EXCEPTION;
  destruction_requested = false;
}

/*****************************************************************************/
/*                    kd_thread_group::~kd_thread_group                      */
/*****************************************************************************/

kd_thread_group::~kd_thread_group()
{
  kd_thread_domain *domain;
  while ((domain=this->domain_list) != NULL)
    { 
      this->domain_list=domain->next;
      delete domain;
    }
  kd_palette_block *blk;
  while ((blk = palette_blocks) != NULL)
    { 
      palette_blocks = blk->next;
      delete[] blk->handle;
    }
  this->mutex.destroy();
}

/*****************************************************************************/
/*                       kd_thread_group::get_domain                         */
/*****************************************************************************/

kd_thread_domain *
  kd_thread_group::get_domain(const char *domain_name)
{
  kd_thread_domain *last_domain=NULL, *domain=this->domain_list;
  for (; domain != NULL; last_domain=domain, domain=domain->next)
    if (domain->check_match(domain_name))
      break;
  if (domain == NULL)
    { // Create new domain
      domain = new kd_thread_domain(domain_name,this);
      if (last_domain == NULL)
        this->domain_list = domain;
      else
        last_domain->next = domain;
      this->num_domains++;
    }
  return domain;
}

/*****************************************************************************/
/*                   kd_thread_group::clean_dirty_palettes                   */
/*****************************************************************************/

void kd_thread_group::clean_dirty_palettes(kdu_thread_entity *caller)
{
  kd_thread_palette *hazards[KDU_MAX_THREADS];
  int t, n, num_hazards = 0;
  for (t=0; t < num_threads; t++)
    { 
      kd_thread_palette *hp = this->thread_job_hzps[t].ref;
      if (hp != NULL)
        hazards[num_hazards++] = hp;
    }
  kd_thread_palette **plt_heap = caller->palette_heap;
  for (n=0; n < KD_THREAD_PALETTES; n++)
    { 
      kd_thread_palette *plt = plt_heap[n];
      assert(plt->slot[0].get() == KD_PLT_HAZARD_MKR); // We only clean when
                                                       // whole heap is dirty
      if (plt->nxt.get() != KD_PLT_DEQUEUE_MKR)
        continue; // Still on the head of a job queue
      for (t=0; t < num_hazards; t++)
        if (hazards[t] == plt)
          break;
      if (t == num_hazards)
        plt->slot[0].set(NULL); // Palette is clean :)
    }
}

/*****************************************************************************/
/*                     kd_thread_group::allocate_palettes                    */
/*****************************************************************************/

kd_thread_palette_ref *
  kd_thread_group::allocate_palettes(int num_plts)
{
  kd_thread_palette_ref *ref, *result=NULL;
  for (; num_plts > 0; num_plts--)
    { 
      if ((ref = free_palettes) == NULL)
        { 
          ref = free_palettes = (kd_thread_palette_ref *)
            returned_palettes.exchange(NULL);
          if (ref == NULL)
            ref = augment_free_palettes();
        }
      free_palettes = ref->next;
      ref->next = result;
      result = ref;
    }
  return result;
}

/*****************************************************************************/
/*                   kd_thread_group::augment_free_palettes                  */
/*****************************************************************************/

kd_thread_palette_ref *kd_thread_group::augment_free_palettes()
{
  assert(free_palettes == NULL);
  size_t blk_size = sizeof(kd_palette_block) + KD_PLT_ALIGN;
  kdu_byte *handle = new kdu_byte[blk_size];
  memset(handle,0,blk_size);
  kdu_byte *first_plt = handle + 2*sizeof(void *);
  int offset = (- _addr_to_kdu_int32(first_plt)) & (KD_PLT_ALIGN-1);
  kd_palette_block *blk = (kd_palette_block *)(handle + offset);
    // Above statements ensure that first palette is aligned on a
    // `KD_PLT_ALIGN'-byte boundary.
  blk->handle = handle;
  blk->next = palette_blocks;
  palette_blocks = blk;
  kd_thread_palette *plt = blk->palettes;
  kd_thread_palette_ref *ref = blk->palette_refs;
  for (int n=0; n < 255; n++, plt++, ref++)
    { 
      ref->next = ref+1;
      ref->palette = plt;
      assert((_addr_to_kdu_int32(plt) & (KD_PLT_ALIGN-1)) == 0);
    }
  ref->palette = plt;
  assert(ref->next==NULL); // No need to set last element's link to NULL
  free_palettes = blk->palette_refs;
  return free_palettes;
}


/* ========================================================================= */
/*                             kdu_thread_queue                              */
/* ========================================================================= */

/*****************************************************************************/
/*                     kdu_thread_queue::kdu_thread_queue                    */
/*****************************************************************************/

kdu_thread_queue::kdu_thread_queue()
{
  group = NULL;
  belongs_to_group = false;
  flags = 0;
  next_sibling = prev_sibling = NULL;
  parent = descendants = NULL;
  dependency_monitor = NULL;
  skip_dependency_propagation = false;
  sequence_idx = 0;
  domain_sequence = NULL;
  last_domain_name = NULL;
  registered_max_jobs = 0;
  palette_refs = NULL;
  completion_state.set(0); 
  completion_waiter = NULL;
  auto_bind_count.set(0);
  dependency_count.set(0);
  max_dependency_count.set(0);
}

/*****************************************************************************/
/*                    kdu_thread_queue::~kdu_thread_queue                    */
/*****************************************************************************/

kdu_thread_queue::~kdu_thread_queue()
{
  if (belongs_to_group)
    { KDU_ERROR_DEV(e,2); e <<
      KDU_TXT("You should not explicitly delete a thread queue that was "
              "created using `kdu_thread_entity::add_queue'.");
    }
  if (group != NULL)
    { KDU_WARNING_DEV(w,0); w <<
      KDU_TXT("Attempting to destroy a `kdu_thread_queue' object before "
              "waiting for its removal from the thread group to which it "
              "is attached -- see `kdu_thread_entity::join' or "
              "`kdu_thread_entity::terminate'.");
      force_detach();
    }
}

/*****************************************************************************/
/*                       kdu_thread_queue::force_detach                      */
/*****************************************************************************/

void
  kdu_thread_queue::force_detach(kdu_thread_entity *caller)
{ // Remember that `caller' may be NULL
  if (group == NULL)
    return;
  assert(!belongs_to_group);
  kdu_mutex *unsafe_mutex_ref = NULL;
  if (caller != NULL)
    caller->lock_group_mutex();
  else
    { 
      unsafe_mutex_ref = &(group->mutex);
      unsafe_mutex_ref->lock();
    }
  if (group != NULL) // Just in case
    { 
      kd_thread_domain_sequence *seq = domain_sequence;
      if (seq != NULL)
        { 
          this->domain_sequence = NULL;
          group->release_palettes(this->palette_refs);
          this->palette_refs = NULL;
        }
      kdu_thread_entity *fake_caller = group->threads[0];
      unlink_from_thread_group(fake_caller,false,true);
        // The `unlink_from_thread_group' function can safely be invoked with
        // a `fake_caller' but only with its `foreign_caller' argument set
        // to true.
    }
  if (caller != NULL)
    caller->unlock_group_mutex();
  else
    unsafe_mutex_ref->unlock();
}

/*****************************************************************************/
/*                        kdu_thread_queue::all_done                         */
/*****************************************************************************/

void
  kdu_thread_queue::all_done(kdu_thread_entity *caller)
{
  assert((caller != NULL) && caller->exists());

  kd_thread_domain_sequence *seq = domain_sequence;
  
  kdu_int32 old_state, new_state;
  kdu_int32 flags_to_remove =
    (KD_THREAD_QUEUE_CSTATE_D | KD_THREAD_QUEUE_CSTATE_T |
     KD_THREAD_QUEUE_CSTATE_S);
  do { // Enter compare-and-set loop
    old_state = completion_state.get();
    new_state = old_state & ~flags_to_remove;
  } while (!completion_state.compare_and_set(old_state,new_state));
  
  if ((new_state ^ old_state) & KD_THREAD_QUEUE_CSTATE_S)
    { // The `all_scheduled' argument was not used in calls to `schedule_jobs'
      kdu_int32 old_active_state = seq->active_state.exchange_add(-2);
      assert(old_active_state >= 2);
      if (old_active_state == 2)
        seq->terminate(caller->hzp);
    }

  if (old_state & KD_THREAD_QUEUE_CSTATE_T)
    { 
      assert(old_state & KD_THREAD_QUEUE_CSTATE_D);
      caller->send_termination_requests(this,true);
    }
  
  if ((old_state & KD_THREAD_QUEUE_CSTATE_S_D) &&
      !(new_state & KD_THREAD_QUEUE_CSTATE_S_D))
    all_complete(caller);
}

/*****************************************************************************/
/*                      kdu_thread_queue::all_complete                       */
/*****************************************************************************/

void
  kdu_thread_queue::all_complete(kdu_thread_entity *caller)
{
  kd_thread_domain_sequence *seq = this->domain_sequence;
  caller->group->release_palettes(this->palette_refs);
  this->palette_refs = NULL;
  this->domain_sequence = NULL;
  this->registered_max_jobs = 0;
  kdu_int32 old_queues = seq->num_queues.exchange_add(-1);
  if (old_queues < 1)
    assert(0);

  kdu_thread_entity_condition *deferred_wake_cond=NULL;
  bool mutex_locked=false;
  kdu_thread_queue *scan, *parent;
  for (scan=this; scan != NULL; scan=parent)
    { 
      parent = scan->parent; // In case `scan' gets cleaned up after we
                             // reduce its completion counter.
      kdu_int32 new_state = -KD_THREAD_QUEUE_CSTATE_C_BIT +
        scan->completion_state.exchange_add(-KD_THREAD_QUEUE_CSTATE_C_BIT);
      assert(new_state >= 0);
      if (new_state & KD_THREAD_QUEUE_CSTATE_C_MASK)
        break;
      if (new_state & KD_THREAD_QUEUE_CSTATE_W)
        { // A thread appears to be waiting to join with this queue
          if (!mutex_locked)
            { // `completion_waiter' may be manipulated only with lock held
              caller->lock_group_mutex();
              mutex_locked = true;
            }
          kdu_thread_entity_condition *cond = scan->completion_waiter;
          if (cond != NULL)
            { 
              scan->completion_waiter = NULL;
              // Try to signal the condition outside the mutex lock
              if (deferred_wake_cond != NULL)
                caller->signal_condition(deferred_wake_cond);
              deferred_wake_cond = cond;
            }
        }
    }

  if (mutex_locked)
    caller->unlock_group_mutex();
  if (deferred_wake_cond != NULL)
    caller->signal_condition(deferred_wake_cond);
}
  
/*****************************************************************************/
/*                   kdu_thread_queue::link_to_thread_group                  */
/*****************************************************************************/

void
  kdu_thread_queue::link_to_thread_group(kdu_thread_entity *caller)
{ // Called while `group->mutex' is locked
  assert(this->group == NULL);
  this->group = caller->group;
  if (parent == NULL)
    { // Link as a top-level queue
      kdu_thread_queue *scan = group->top_queues;
      if ((scan == NULL) || !(flags & KDU_THREAD_QUEUE_BACKGROUND))
        { // Place at head of sibling list
          prev_sibling = NULL;
          if ((next_sibling = scan) != NULL)
            scan->prev_sibling = this;
          group->top_queues = this;
        }
      else
        { // Place at tail of sibling list
          while (scan->next_sibling != NULL)
            scan = scan->next_sibling;
          this->prev_sibling = scan;
          this->next_sibling = NULL;
          scan->next_sibling = this;
        }
    }
  else
    { // Link as a descendant
      kdu_thread_queue *scan = parent->descendants;
      if ((scan == NULL) || !(flags & KDU_THREAD_QUEUE_BACKGROUND))
        { // Place at head of sibling list 
          prev_sibling = NULL;
          if ((next_sibling = scan) != NULL)
            scan->prev_sibling = this;
          parent->descendants = this;
        }
      else
        { // Place at tail of sibling list
          while (scan->next_sibling != NULL)
            scan = scan->next_sibling;
          this->prev_sibling = scan;
          this->next_sibling = NULL;
          scan->next_sibling = this;          
        }
    }
}

/*****************************************************************************/
/*                 kdu_thread_queue::unlink_from_thread_group                */
/*****************************************************************************/

void
  kdu_thread_queue::unlink_from_thread_group(kdu_thread_entity *caller,
                                             bool unless_belongs_to_group,
                                             bool foreign_caller)
{ // Called while `group->mutex' is locked and only if the queue is still
  // participating in the thread group.
  assert(this->group == caller->group);
  kdu_thread_queue *scan, *next;
  for (scan=descendants; scan != NULL; scan=next)
    { 
      next = scan->next_sibling;
      scan->unlink_from_thread_group(caller,unless_belongs_to_group,
                                     foreign_caller);
    }
  kdu_thread_entity_condition *cond = completion_waiter;
  if (cond != NULL)
    { // Make sure any joining thread awakes;
      // there is no harm in signalling conditions at any time, even when
      // the group mutex is locked, because condition signalling is
      // lock-free and requires access to nothing other than the
      // condition object itself, which always lives somewhere in
      // memory that is guaranteed not to be deallocated until the
      // entire thread group is destroyed.
      completion_waiter = NULL;
      caller->signal_condition(cond,foreign_caller);
    }
  if (belongs_to_group && unless_belongs_to_group)
    return;

  // Finish by unlinking ourself
  if (prev_sibling == NULL)
    {
      if (parent != NULL)
        { // Unlinking from `parent->descendants' list
          assert(this == parent->descendants);
          parent->descendants = next_sibling;
        }
      else
        { // Unlinking from `group->top_queues' list
          assert(this == group->top_queues);
          group->top_queues = next_sibling;
        }
    }
  else
    { 
      assert(this == prev_sibling->next_sibling);
      prev_sibling->next_sibling = next_sibling;
    }
  if (next_sibling != NULL)
    next_sibling->prev_sibling = prev_sibling;
  group = NULL; next_sibling = prev_sibling = NULL;
  parent = NULL;
  dependency_monitor = NULL;
  skip_dependency_propagation = false;

  if (belongs_to_group)
    { 
      belongs_to_group = false; // Avoid asserts in the destructor
      delete this;
    }
}

/*****************************************************************************/
/*                        kdu_thread_queue::bind_jobs                        */
/*****************************************************************************/

void
  kdu_thread_queue::bind_jobs(kdu_thread_job * const jobs[], int num_jobs,
                              kdu_uint32 range_start)
{
  if (registered_max_jobs < (num_jobs + (int) range_start))
    { KDU_ERROR_DEV(e,0x11021201); e <<
      KDU_TXT("The `kdu_thread_queue::bind_jobs' function may be used "
              "only on working queues -- i.e., those that have been passed to "
              "`kdu_thread_entity::attach_queue' and whose `get_max_jobs' "
              "function returned a value at least as large as the number "
              "of jobs you are trying to bind when the queue was attached to "
              "the thread group.  Perhaps you forgot to override "
              "`kdu_thread_queue::get_max_jobs' in a derived class??");
    }
  if (!(completion_state.get() & KD_THREAD_QUEUE_CSTATE_S))
    { KDU_ERROR_DEV(e,0x16021201); e <<
      KDU_TXT("Attempting to invoke `kdu_thread_queue::bind_jobs' after "
              "the final job for a thread queue has already been "
              "scheduled -- as identified by the `all_scheduled' argument "
              "in calls to `kdu_thread_queue::schedule_jobs' or "
              "`kdu_thread_queue::schedule_job' -- or after the "
              "`kdu_thread_queue::all_done' function has been called!");
    }
  kd_thread_palette_ref *scan=palette_refs;
  for (; range_start > 0; range_start--)
    { 
      assert(scan != NULL);
      scan = scan->next;
    }
  for (int n=0; n < num_jobs; n++, scan=scan->next)
    { 
      assert(scan != NULL);
      assert(jobs[n] != NULL);
      jobs[n]->palette_ref = scan;
    }
  auto_bind_count.set(registered_max_jobs); // Prevents auto-binding
}

/*****************************************************************************/
/*                      kdu_thread_queue::schedule_jobs                      */
/*****************************************************************************/

void
  kdu_thread_queue::schedule_jobs(kdu_thread_job * const jobs[], int num_jobs,
                                  kdu_thread_entity *caller,
                                  bool all_scheduled)
{
  if (num_jobs < 1)
    return;
  assert((caller != NULL) && caller->exists());
  if (caller->grouperr->failed)
    { 
      caller->lock_group_mutex();   // Failure code written inside critical
      caller->unlock_group_mutex(); // section, so make sure we get it
      kdu_rethrow(caller->grouperr->failure_code); 
    }
  if (this->group != caller->group)
    { KDU_ERROR_DEV(e,0x11021202); e <<
      KDU_TXT("The `kdu_thread_queue::schedule_jobs' function may be called "
              "only from a thread that is participating in the same group "
              "to which the queue has been attached.");
    }
  if ((registered_max_jobs <= 0) ||
      !(completion_state.get() & KD_THREAD_QUEUE_CSTATE_S))
    { KDU_ERROR_DEV(e,0x16021202); e <<
      KDU_TXT("You appear to be invoking `kdu_thread_queue::schedule_jobs' "
              "after the queue's final job has already been scheduled!");
    }
  
  if (all_scheduled)
    { // Update the `completion_state' variable first to reflect the fact
      // that later calls to `all_done' will not need to manipulate our
      // domain sequence's `active_state' variable.
      kdu_int32 old_state, new_state;
      kdu_int32 flags_to_remove = KD_THREAD_QUEUE_CSTATE_S;
      do { // Enter compare-and-set loop
        old_state = completion_state.get();
        new_state = old_state & ~flags_to_remove;
      } while (!completion_state.compare_and_set(old_state,new_state));
      if (old_state == new_state)
        { KDU_WARNING_DEV(w,0x17051201); w <<
          KDU_TXT("You appear to be calling `kdu_thread_queue::schedule_jobs' "
                  "with the `all_scheduled' argument set to true when this "
                  "has been done before, or else the (even worse) the "
                  "`kdu_thread_queue::all_done' function has already been "
                  "invoked!!  This suggests a serious flaw in the "
                  "implementation, which may result in dangerous race "
                  "conditions.");
          all_scheduled = false;
        }
    }

  // Get a local reference to the `domain_sequence' member variable now so
  // that we will not have to access the present object again -- this ensures
  // that a scheduled job which completes, calls `all_done' and results in
  // the queue being cleaned up will not be the cause of any exceptoin
  // faults.
  kd_thread_domain_sequence *seq = domain_sequence;
  assert(seq != NULL);
  kd_thread_palette *plt_head, *plt_tail;
  plt_head = plt_tail = group->get_palette_to_schedule(jobs[0],caller);
  int n, s;
  for (s=1, n=1; n < num_jobs; n++, s++)
    { 
      if (s == KD_PLT_SLOTS)
        { 
          kd_thread_palette *plt =
            group->get_palette_to_schedule(jobs[n],caller);
          plt_tail->nxt.set(plt);
          plt_tail = plt;
          s = 0;
        }
      else
        plt_tail->slot[s].set(jobs[n]);
    }
  seq->append_jobs(plt_head,plt_tail,s,caller->hzp);
  caller->group->wake_idle_threads_for_domain(num_jobs,seq->domain);
  
#ifdef KDU_THREAD_TRACE_RECORDS
  kdu_int32 old_cnt = seq->trace_add.exchange_add(num_jobs);
  group->thread_trace.add_entry(caller->get_thread_id(),seq->domain,
                                -1-seq->sequence_idx,old_cnt,old_cnt+num_jobs);
#endif // KDU_THREAD_TRACE_RECORDS
    
  if (all_scheduled)
    { // Update `seq->active_state'
      kdu_int32 old_active_state = seq->active_state.exchange_add(-2);
      assert(old_active_state >= 2);
      if (old_active_state == 2)
        seq->terminate(caller->hzp);
    }  
}

/*****************************************************************************/
/*                       kdu_thread_queue::schedule_job                      */
/*****************************************************************************/

void
  kdu_thread_queue::schedule_job(kdu_thread_job *job,
                                 kdu_thread_entity *caller,
                                 bool all_scheduled, int bind_options)
{
  assert((caller != NULL) && caller->exists());
  if (caller->grouperr->failed)
    { 
      caller->lock_group_mutex();   // Failure code written inside critical
      caller->unlock_group_mutex(); // section, so make sure we get it
      kdu_rethrow(caller->grouperr->failure_code); 
    }
  if ((this->group != caller->group) || (registered_max_jobs < 1))
    { KDU_ERROR_DEV(e,0x12021201); e <<
      KDU_TXT("The `kdu_thread_queue::schedule_job' function may be called "
              "only from a thread that is participating in the same group "
              "to which the queue has been attached, and whose "
              "`get_max_jobs' function returned a non-zero value when the "
              "queue was attached to the group.  Perhaps you forgot to "
              "override `kdu_thread_queue::get_max_jobs' in a derived "
              "class??");
    }
  if (!(completion_state.get() & KD_THREAD_QUEUE_CSTATE_S))
    { KDU_ERROR_DEV(e,0x16021203); e <<
      KDU_TXT("Attempting to invoke `kdu_thread_queue::schedule_job' after "
              "the queue's final job has already been scheduled!");
    }
  kd_thread_palette_ref *ref = job->palette_ref;
  if (bind_options == KDU_THREAD_JOB_REBIND_0)
    { 
      if (auto_bind_count.get() > 0)
        { KDU_ERROR_DEV(e,0x12021202); e <<
          KDU_TXT("The `kdu_thread_queue::schedule_job' function may "
                  "not be called with the `KDU_THREAD_JOB_REBIND_0' "
                  "option if any other job binding operation has "
                  "previously been performed on the same thread queue.");
        }
      auto_bind_count.set(-1);
      ref = job->palette_ref = this->palette_refs;
    }
  else if ((ref == NULL) && (bind_options == KDU_THREAD_JOB_AUTO_BIND_ONCE))
    { 
      kdu_int32 idx = auto_bind_count.exchange_add(1);
      if ((idx < 0) || (idx >= registered_max_jobs))
        { KDU_ERROR_DEV(e,0x12021203); e <<
          KDU_TXT("The `kdu_thread_queue::schedule_job' function is being "
                  "called with the `KDU_THREAD_AUTO_BIND_ONCE' option; "
                  "however, either another binding operation has previously "
                  "been performed, or else the number of auto-bind "
                  "operations requested exceeds the value returned by "
                  "`kdu_thread_queue::get_max_jobs'.");
        }
      ref = this->palette_refs;
      for (; idx > 0; idx--, ref=ref->next);
      assert(ref != NULL);
      job->palette_ref = ref;
    }
  else if (ref == NULL)
    { KDU_ERROR_DEV(e,0x12021204); e <<
      KDU_TXT("The `kdu_thread_queue::schedule_job' function is being "
              "called with a job that has not yet been bound, yet "
              "none of the automatic binding options were specified in "
              "the call.");
    }
  
  if (all_scheduled)
    { // Update the `completion_state' variable first to reflect the fact
      // that later calls to `all_done' will not need to manipulate our
      // domain sequence's `active_state' variable.
      kdu_int32 old_state, new_state;
      kdu_int32 flags_to_remove = KD_THREAD_QUEUE_CSTATE_S;
      do { // Enter compare-and-set loop
        old_state = completion_state.get();
        new_state = old_state & ~flags_to_remove;
      } while (!completion_state.compare_and_set(old_state,new_state));
      if (old_state == new_state)
        { KDU_WARNING_DEV(w,0x17051202); w <<
          KDU_TXT("You appear to be calling `kdu_thread_queue::schedule_job' "
                  "with the `all_scheduled' argument set to true when this "
                  "has been done before, or else the (even worse) the "
                  "`kdu_thread_queue::all_done' function has already been "
                  "invoked!!  This suggests a serious flaw in the "
                  "implementation, which may result in dangerous race "
                  "conditions.");
          all_scheduled = false;
        }
    }
  
  // Get a local reference to the `domain_sequence' member variable now so
  // that we will not have to access the present object again -- this ensures
  // that a scheduled job which completes, calls `all_done' and results in
  // the queue being cleaned up will not be the cause of any exceptoin
  // faults.
  kd_thread_domain_sequence *seq = domain_sequence;
  assert(seq != NULL);

  kd_thread_palette *plt = group->get_palette_to_schedule(job,caller);
  seq->append_jobs(plt,plt,1,caller->hzp);
  caller->group->wake_idle_threads_for_domain(1,seq->domain);

#ifdef KDU_THREAD_TRACE_RECORDS
  kdu_int32 old_cnt = seq->trace_add.exchange_add(1);
  group->thread_trace.add_entry(caller->get_thread_id(),seq->domain,
                                -1-seq->sequence_idx,old_cnt,old_cnt+1);
#endif // KDU_THREAD_TRACE_RECORDS  

  if (all_scheduled)
    { // Update `seq->active_state'
      kdu_int32 old_active_state = seq->active_state.exchange_add(-2);
      assert(old_active_state >= 2);
      if (old_active_state == 2)
        seq->terminate(caller->hzp);
    }  
}

/*****************************************************************************/
/*                   kdu_thread_queue::note_all_scheduled                    */
/*****************************************************************************/

void kdu_thread_queue::note_all_scheduled(kdu_thread_entity *caller)
{
  assert((caller != NULL) && caller->exists());

  // Update the `completion_state' variable first to reflect the fact
  // that later calls to `all_done' will not need to manipulate our
  // domain sequence's `active_state' variable.
  kdu_int32 old_state, new_state;
  kdu_int32 flags_to_remove = KD_THREAD_QUEUE_CSTATE_S;
  do { // Enter compare-and-set loop
    old_state = completion_state.get();
    new_state = old_state & ~flags_to_remove;
  } while (!completion_state.compare_and_set(old_state,new_state));
  if (old_state == new_state)
    return; // Let multiple calls go unpunished here -- no real harm done.
  kd_thread_domain_sequence *seq = domain_sequence;
  assert(seq != NULL);
  kdu_int32 old_active_state = seq->active_state.exchange_add(-2);
  assert(old_active_state >= 2);
  if (old_active_state == 2)
    seq->terminate(caller->hzp);  
}



/* ========================================================================= */
/*                             kdu_thread_entity                             */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdu_thread_entity::kdu_thread_entity                    */
/*****************************************************************************/

kdu_thread_entity::kdu_thread_entity()
{
  thread_idx=0;
  group=NULL;
  grouperr=NULL;
  hzp=NULL;
  thread_domain = NULL;
  num_work_domains = 0;
  memset(work_domains,0,sizeof(void *)*KDU_MAX_DOMAINS);
  alt_work_idx = 0; job_counter = yield_counter = 0;  yield_freq = 0;
  yield_outstanding = false;
  first_wait_safe = false;
  in_process_jobs = in_process_jobs_with_cond = false;
  group_mutex_lock_count = 0;
  next_palette_idx = 0;
  memset(palette_heap,0,sizeof(void *)*KD_THREAD_PALETTES);
  free_conditions = cur_condition = NULL;
  for (int c=KDU_INITIAL_THREAD_CONDITIONS-1; c >= 0; c--)
    { 
      kdu_thread_entity_condition *cond = condition_store+c;
      cond->link = free_conditions;
      free_conditions = cond;
      cond->signalled = cond->is_dynamic = false;
      cond->thread_idx = -1; // Just a safety precaution
    }
}

/*****************************************************************************/
/*                   kdu_thread_entity::~kdu_thread_entity                   */
/*****************************************************************************/

kdu_thread_entity::~kdu_thread_entity()
{
  if (is_group_owner())
    destroy();
  assert(group == NULL);
  while (cur_condition != NULL)
    pop_condition();
  kdu_thread_entity_condition *cond;
  while ((cond=free_conditions) != NULL)
    { 
      free_conditions = cond->link;
      if (cond->is_dynamic)
        delete cond;
    }
}

/*****************************************************************************/
/*                     kdu_thread_entity::operator new                       */
/*****************************************************************************/

void *
  kdu_thread_entity::operator new(size_t size)
{
  size += sizeof(kdu_byte *); // Leave room to store deallocation pointer
  int mask = KDU_MAX_L2_CACHE_LINE-1;
  assert(!(mask & (mask+1))); // KDU_MAX_L2_CACHE_LINE must be power of 2!
  int offset = (KDU_MAX_L2_CACHE_LINE - (int) size) & mask;
  size += offset; // Pad to nearest multiple of max L2 cache line size.
  size += KDU_MAX_L2_CACHE_LINE; // Allow for incorrectly aligned result
  kdu_byte *handle = (kdu_byte *) malloc(size);
  if (handle == NULL)
    throw std::bad_alloc();
  kdu_byte *base = handle + sizeof(kdu_byte *);
  offset = (- _addr_to_kdu_int32(base)) & mask;
  base += offset; // Aligned base of object
  ((kdu_byte **) base)[-1] = handle;
  return (void *) base;
}

/*****************************************************************************/
/*                   kdu_thread_entity::operator delete                      */
/*****************************************************************************/

void
  kdu_thread_entity::operator delete(void *base)
{
  kdu_byte *handle = ((kdu_byte **) base)[-1];
  free(handle);
}

/*****************************************************************************/
/*                       kdu_thread_entity::pre_launch                       */
/*****************************************************************************/

void
  kdu_thread_entity::pre_launch()
{
  assert(exists());
  group->mutex.lock();
  kdu_thread_context *ctxt;
  for (ctxt=group->contexts; ctxt != NULL; ctxt=ctxt->next)
    ctxt->num_threads_changed(group->num_threads);
  group->mutex.unlock();
}

/*****************************************************************************/
/*                         kdu_thread_entity::create                         */
/*****************************************************************************/

void
  kdu_thread_entity::create(kdu_int64 cpu_affinity,
                            bool also_set_owner_affinity)
{
  assert(!exists());
  this->thread_idx = 0;
  this->thread.create(NULL,NULL); // Use self
  this->group = new kd_thread_group;
  if (cpu_affinity != 0)
    { 
      for (int t=1; t < KDU_MAX_THREADS; t++)
        group->cpu_affinity_masks[t] = cpu_affinity;
      if (also_set_owner_affinity)
        group->cpu_affinity_masks[0] = cpu_affinity;
    }
  this->thread_domain = group->get_domain(NULL);
  thread_domain->num_member_threads++;
  thread_domain->member_threads |= (kd_thread_flags) 1;

  group->num_threads = 1;
  group->threads[0] = this;
  group->thread_semaphores[0].create(0); // Semaphore blocks by default
  group->mutex.create(4000); // Prefer to spin rather than sleep
  this->grouperr = &(group->grouperr);
  this->hzp = &(group->thread_job_hzps[0]);
  if ((cpu_affinity != 0) && also_set_owner_affinity) 
    thread.set_cpu_affinity(cpu_affinity);
  this->num_work_domains = 0;
  memset(work_domains,0,sizeof(void *)*KDU_MAX_DOMAINS);

  // Allocate the thread palette heap.
  kd_thread_palette_ref *ref = group->allocate_palettes(KD_THREAD_PALETTES);
  for (this->next_palette_idx=KD_THREAD_PALETTES;
       this->next_palette_idx > 0; ref=ref->next)
    this->palette_heap[--this->next_palette_idx] = ref->palette;

  // Initialize the condition stack
  while (cur_condition != NULL)
    pop_condition();
  push_condition();
}

/*****************************************************************************/
/*                        kdu_thread_entity::destroy                         */
/*****************************************************************************/

bool kdu_thread_entity::destroy()
{
  if (!exists())
    return true;
  assert(is_group_owner() && check_current_thread());
  assert(group_mutex_lock_count == 0);
  int n;
  bool result = !grouperr->failed;
  handle_exception(-1); // Forces threads to terminate as soon as possible and
                   // also releases any locks the group owner might still
                   // be holding -- see `kdu_thread_context::handle_exception'
  while (group->top_queues != NULL)
    group->top_queues->unlink_from_thread_group(this); // Deletes any
        // internally allocated queues that remain -- `handle_exception'
        // did not unlink these, although `join' and/or `terminate' do.
  group->destruction_requested = true;
  for (n=1; n < group->num_threads; n++)
    group->thread_semaphores[n].signal();
  for (n=1; n < group->num_threads; n++)
    group->threads[n]->thread.destroy(); // Waits for the thread to exit
  for (n=0; n < group->num_threads; n++)
    group->thread_semaphores[n].destroy();
  while (group->contexts != NULL)
    group->contexts->leave_group();
  
#ifdef KDU_THREAD_TRACE_RECORDS
  const char *job_domain_names[KDU_MAX_THREADS];
  int job_seq_indices[KDU_MAX_THREADS];
  int job_indices[KDU_MAX_THREADS];
  int job_counts[KDU_MAX_THREADS];
  fprintf(stdout,"Thread activity trace:\n");
  fprintf(stdout," Event");
  for (n=0; n < group->num_threads; n++)
    { 
      fprintf(stdout,"  Thrd-%02d         ",n);
      job_domain_names[n] = NULL;
      job_seq_indices[n] = 1;
      job_indices[n] = 1;
      job_counts[n] = 0;
    }
  fprintf(stdout,"\n");
  fprintf(stdout," -----");
  for (n=0; n < group->num_threads; n++)
    fprintf(stdout,"  -------         ");
  fprintf(stdout,"\n");
  int trace_entry, t, t_idx, j_seq_idx, j_idx, j_cnt;
  kd_thread_domain *domain=NULL;
  for (trace_entry=0;
       group->thread_trace.get_entry(trace_entry,t_idx,
                                     domain,j_seq_idx,j_idx,j_cnt);
       trace_entry++)
    { 
      const char *j_name = NULL;
      if (domain != NULL)
        j_name = (domain->name==NULL)?"Default":domain->name;
      if (j_seq_idx >= 0)
        { // Entry corresponds to a state change, as opposed to an event
          job_domain_names[t_idx] = j_name;
          job_seq_indices[t_idx] = j_seq_idx;
          job_indices[t_idx] = j_idx;
          job_counts[t_idx] = j_cnt;
        }
      
      fprintf(stdout,"%6d ",trace_entry);
      for (t=0; t < group->num_threads; t++)
        { 
          const char *cp = (t==t_idx)?j_name:job_domain_names[t];
          int seq_idx = (t==t_idx)?j_seq_idx:job_seq_indices[t];
          int idx = (t==t_idx)?j_idx:job_indices[t];
          int cnt = (t==t_idx)?j_cnt:job_counts[t];
          if (cp != NULL)
            { 
              putc(' ',stdout);
              for (n=0; (n < 4) && (*cp != '\0'); n++, cp++)
                putc(*cp,stdout);
              char tbuf[40];
              if (seq_idx < 0)
                sprintf(tbuf,":%d:%d->%d",-1-seq_idx,idx,cnt);
              else if (t == t_idx)
                sprintf(tbuf,":%d:%d/%d",seq_idx,idx,cnt);
              else
                sprintf(tbuf,":%d:%d----",seq_idx,idx);
              for (cp=tbuf; (n < 17) && (*cp != '\0'); n++, cp++)
                putc(*cp,stdout);
              for (; n < 17; n++)
                putc(' ',stdout);
            }
          else if (idx == 0)
            fprintf(stdout," Idle             ");
          else if (idx == 1)
            fprintf(stdout," Wake             ");
          else if (idx == 2)
            fprintf(stdout," Revive           ");
          else
            fprintf(stdout," <Unknown>        ");
        }
      putc('\n',stdout);
    }
#endif // KDU_THREAD_TRACE_RECORDS

  for (n=0; n < group->num_threads; n++)
    { 
      if (n > 0)
        delete group->threads[n];
      group->threads[n] = NULL;
    }
  group->num_threads = 0;
  
  delete group;
  group = NULL;
  grouperr = NULL;
  hzp = NULL;
  this->thread_domain = NULL;
  this->num_work_domains = 0;
  this->alt_work_idx = 0;
  this->job_counter = 0;
  this->in_process_jobs = false;
  this->in_process_jobs_with_cond = false;
  this->group_mutex_lock_count = 0;
  thread.destroy();
  
  next_palette_idx = 0;
  memset(palette_heap,0,sizeof(void *)*KD_THREAD_PALETTES);
  
  while (cur_condition != NULL)
    pop_condition();
  return result;
}

/*****************************************************************************/
/*                    kdu_thread_entity::set_cpu_affinity                    */
/*****************************************************************************/

bool
  kdu_thread_entity::set_cpu_affinity(const kdu_thread_entity_affinity &desc)
{
  if ((group == NULL) || !is_group_owner())
    return false;
  int b=0, count=0;
  kdu_int64 mask=0;
  kdu_int32 context=0;
  for (int t=0; t < KDU_MAX_THREADS; t++, count--)
    { 
      if (count == 0)
        { 
          count = desc.get_bundle_affinity(b++,mask,context);
          if (count == 0)
            count = KDU_MAX_THREADS; // No more bundle info available
        }
      group->cpu_affinity_masks[t] = mask;
      group->cpu_affinity_contexts[t] = context;
    }
  thread.set_cpu_affinity(group->cpu_affinity_masks[0],
                          group->cpu_affinity_contexts[0]);
  return true;
}

/*****************************************************************************/
/*               kdu_thread_entity::set_min_thread_concurrency               */
/*****************************************************************************/

void kdu_thread_entity::set_min_thread_concurrency(int min_concurrency)
{
  if (group != NULL)
    group->min_thread_concurrency = min_concurrency;
}

/*****************************************************************************/
/*                    kdu_thread_entity::get_num_threads                     */
/*****************************************************************************/

int
  kdu_thread_entity::get_num_threads(const char *domain_name)
{
  if (!exists())
    return 0;
  if (domain_name == NULL)
    return group->num_threads;
  else
    { // No need to lock the mutex here, because we never remove or reorder
      // domains -- safest to do no locking.
      kd_thread_domain *domain=group->domain_list;
      for (; domain != NULL; domain=domain->next)
        if (domain->check_match(domain_name))
          return domain->num_member_threads;
    }
  return 0;
}

/*****************************************************************************/
/*                       kdu_thread_entity::add_thread                       */
/*****************************************************************************/

bool
  kdu_thread_entity::add_thread(const char *domain_name)
{
  if (!exists())
    return false;

  lock_group_mutex();
  if (grouperr->failed)
    { 
      unlock_group_mutex();
      return false; // This is safest
    }

  kd_thread_domain *domain = group->get_domain(domain_name);
  kd_thread_domain_sequence *seq = NULL;
  if ((domain_name != NULL) && (*domain_name !='\0'))
    { 
      seq = domain->get_initial_domain_sequence();
      if (seq == NULL)
        { // Too many working domains
          unlock_group_mutex();
          return false;
        }
    }

  int thrd_idx = group->num_threads;
  bool success = ((thrd_idx < KDU_MAX_THREADS) &&
                  group->thread_semaphores[thrd_idx].create(0));
  if (success)
    {
      if ((group->threads[thrd_idx] = this->new_instance()) == NULL)
        success = false;
      if (success)
        { 
          kdu_thread_entity *thrd = group->threads[thrd_idx];
          group->num_threads = thrd_idx+1;
          thrd->thread_idx = thrd_idx;
          thrd->group = this->group;
          thrd->grouperr = this->grouperr;
          thrd->hzp = &(group->thread_job_hzps[thrd_idx]);
          thrd->thread_domain = domain;
          thrd->num_work_domains = 0;
          thrd->alt_work_idx = 0;
          thrd->job_counter = thrd->yield_counter = 0;
          thrd->yield_outstanding = false;
          thrd->yield_freq = group->worker_thread_yield_freq;
          memset(thrd->work_domains,0,sizeof(void *)*KDU_MAX_DOMAINS);
          assert(thrd->cur_condition == NULL);
          
          // Allocate the thread palette heap.
          kd_thread_palette_ref *ref =
            group->allocate_palettes(KD_THREAD_PALETTES);
          for (thrd->next_palette_idx=KD_THREAD_PALETTES;
               thrd->next_palette_idx > 0; ref=ref->next)
            thrd->palette_heap[--thrd->next_palette_idx] = ref->palette;

          // Add domain sequence references for each existing work domain
          kd_thread_flags thrd_flag = ((kd_thread_flags) 1) << thrd_idx;
          thrd->thread_domain->num_member_threads++;
          thrd->thread_domain->member_threads |= thrd_flag;
          if (seq != NULL)
            {
              thrd->work_domains[thrd->num_work_domains++]=seq->add_consumer();
              thrd->alt_work_idx = 1; // The thread has its own preferred work
                // domain so alternate work domains, if any, start from the
                // second position in the `work_domains' array.
            }
          for (domain=group->domain_list; domain != NULL; domain=domain->next)
            if ((domain != thrd->thread_domain) &&
                (domain->sequence_head != NULL))
              thrd->work_domains[thrd->num_work_domains++] =
                domain->sequence_head->add_consumer();

          success = thrd->thread.create(worker_startproc,(void *) thrd);
          if (!success)
            { // Unable to create the new thread
              thrd->group = NULL; // So `delete' passes verification test
              group->threads[thrd_idx] = NULL;
              group->num_threads--;
              thrd->thread_domain->num_member_threads--;
              thrd->thread_domain->member_threads &= ~thrd_flag;

              // Remove domain sequence references from the thread
              while (thrd->num_work_domains > 0)
                { 
                  thrd->num_work_domains--;
                  seq = thrd->work_domains[thrd->num_work_domains];
                  thrd->work_domains[thrd->num_work_domains] = NULL;
                  if (seq->remove_consumer())
                    seq->domain->remove_unused_sequences();
                }
              delete thrd;
            }
        }
    }

  if (!success)
    group->thread_semaphores[thrd_idx].destroy();

#if (defined KDU_PTHREADS) && !defined __ANDROID__
  if (success)
    { // Adjust number of kernel threads (affects Solaris, but not Linux/OSX)
      int concurrency = group->min_thread_concurrency;
      if (group->num_threads > concurrency)
        concurrency = group->num_threads;
      pthread_setconcurrency(concurrency);
    }
#endif // KDU_PTHREADS

  unlock_group_mutex();

  return success;
}

/*****************************************************************************/
/*             kdu_thread_entity::declare_first_owner_wait_safe              */
/*****************************************************************************/

bool kdu_thread_entity::declare_first_owner_wait_safe(bool is_safe)
{
  if (!(exists() && is_group_owner() && check_current_thread()))
    { 
      assert(0);
      return false;
    }
  bool old_val = first_wait_safe;
  first_wait_safe = is_safe;
  return old_val;
}  

/*****************************************************************************/
/*                  kdu_thread_entity::set_yield_frequency                   */
/*****************************************************************************/

void
  kdu_thread_entity::set_yield_frequency(int worker_yield_freq)
{
  if (!exists())
    return;
  if (worker_yield_freq < 0)
    worker_yield_freq = 0;
  group->worker_thread_yield_freq = worker_yield_freq;
  for (int t=1; t < group->num_threads; t++)
    group->threads[t]->yield_freq = worker_yield_freq;
}

/*****************************************************************************/
/*                kdu_thread_entity::get_job_count_stats                     */
/*****************************************************************************/

kdu_long
  kdu_thread_entity::get_job_count_stats(kdu_long &group_owner_count)
{
  group_owner_count = 0;
  if (!exists())
    return 0;
  kdu_long total_count = 0;
  for (int t=0; t < group->num_threads; t++)
    { 
      int new_count = group->threads[t]->job_counter;
      int old_count = group->saved_job_counts[t];
      group->saved_job_counts[t] = new_count;
      int diff = new_count - old_count; // Automatically fixed wrap-around
      total_count += diff;
      if (t == 0)
        group_owner_count = diff;
    }
  return total_count;
}

/*****************************************************************************/
/*                      kdu_thread_entity::attach_queue                      */
/*****************************************************************************/

bool
  kdu_thread_entity::attach_queue(kdu_thread_queue *queue,
                                  kdu_thread_queue *super_queue,
                                  const char *domain_name,
                                  kdu_long min_sequencing_idx,
                                  int queue_flags)
{
  if ((!exists()) || (queue==NULL) || (queue->group != NULL) ||
      ((super_queue != NULL) && (super_queue->group != this->group)))
    return false;
  
  kdu_int32 max_jobs = queue->get_max_jobs(); // Call in the safest context
  if ((max_jobs > 0) && ((domain_name==NULL) || (*domain_name=='\0')))
    return false;
  
  if (queue_flags & KDU_THREAD_QUEUE_SAFE_CONTEXT)
    { // Give safe-context queues all the same attributes as background
      // queues, but with extra constraints.
      queue_flags |= KDU_THREAD_QUEUE_BACKGROUND;
      if (group->num_threads < 2)
        { KDU_ERROR_DEV(e,0x14011201); e <<
          KDU_TXT("Trying to attach a queue with the "
                  "`KDU_THREAD_QUEUE_SAFE_CONTEXT' attribute to a thread "
                  "group that does not have any worker threads.  Safe-context "
                  "jobs can only be executed by worker threads, so this is "
                  "not a meaningful thing to do.");
        }
    }

  assert(queue->palette_refs == NULL);
  queue->auto_bind_count.set(0);

  lock_group_mutex();
  if (grouperr->failed)
    { 
      unlock_group_mutex();
      kdu_rethrow(grouperr->failure_code);
    }
  queue->flags = queue_flags;
  queue->prev_sibling = queue->next_sibling = NULL; // Change these later
  queue->parent = queue->descendants = NULL; // Change these later
  queue->skip_dependency_propagation = false;
  queue->sequence_idx = min_sequencing_idx;
  if ((super_queue != NULL) &&
      (super_queue->sequence_idx > min_sequencing_idx))
    queue->sequence_idx = super_queue->sequence_idx;
  queue->domain_sequence = NULL;  // These values may be changed later
  queue->last_domain_name = domain_name;
  queue->registered_max_jobs = 0; // if all goes according to plan.
  queue->completion_state.set(0);
  queue->completion_waiter = NULL;

  bool success = true;
  bool added_new_domain_sequence = false;
  kd_thread_domain *domain=NULL;
  if (max_jobs > 0)
    { 
      domain = group->get_domain(domain_name);
      queue->last_domain_name = domain->name;
      if (queue_flags & KDU_THREAD_QUEUE_BACKGROUND)
        domain->set_background_domain(queue_flags);
      kd_thread_domain_sequence *seq =
        domain->get_active_domain_sequence(queue->sequence_idx,
                                           added_new_domain_sequence,this);
      if (seq == NULL)
        success = false;
      else
        { 
          queue->domain_sequence = seq;
          queue->sequence_idx = seq->sequence_idx;
        }
    }
  if (success)
    { 
      queue->parent = super_queue;
      queue->registered_max_jobs = max_jobs;
      if (max_jobs > 0)
        { 
          queue->completion_state.set(KD_THREAD_QUEUE_CSTATE_S_D +
                                      KD_THREAD_QUEUE_CSTATE_C_BIT);
          while (super_queue != NULL)
            {
              kdu_int32 old_val =
                super_queue->completion_state.exchange_add(
                                              KD_THREAD_QUEUE_CSTATE_C_BIT);
              if (old_val & KD_THREAD_QUEUE_CSTATE_C_MASK)
                break; // `super_queue' already had non-zero completion count.
              super_queue = super_queue->parent;
            }
          queue->palette_refs = group->allocate_palettes(max_jobs);
        }
      queue->link_to_thread_group(this);
    }
  if (added_new_domain_sequence)
    advance_work_domains(); // Best to do this here, just in case the attaching
                            // thread never does any jobs, so that the domain
                            // sequences might never get a chance to advance.
  unlock_group_mutex();
  return success;
}

/*****************************************************************************/
/*                       kdu_thread_entity::add_queue                        */
/*****************************************************************************/

kdu_thread_queue *
  kdu_thread_entity::add_queue(kdu_thread_dependency_monitor *monitor,
                               kdu_thread_queue *super_queue,
                               const char *domain_name,
                               kdu_long min_sequencing_idx)
{
  if ((!exists()) ||
      ((super_queue != NULL) &&
       ((super_queue->group != this->group) ||
        !super_queue->belongs_to_group)))
    return NULL;

  kdu_thread_queue *queue = new kdu_thread_queue;
  queue->belongs_to_group = true;
  queue->set_dependency_monitor(monitor);
  bool success = false;
  try {
    success = attach_queue(queue,super_queue,domain_name,min_sequencing_idx,0);
  } catch (...) {
    delete queue;
    throw;
  }
  if (!success)
    { 
      delete queue;
      queue = NULL;
    }
  return queue;
}

/*****************************************************************************/
/*                   kdu_thread_entity::wait_for_condition                   */
/*****************************************************************************/

void
  kdu_thread_entity::wait_for_condition(const char *debug_text)
{
  if (!exists())
    return;
  assert(check_current_thread());
  kdu_thread_entity_condition *cond = cur_condition; // Top of condition stack
  if (grouperr->failed)
    { 
      lock_group_mutex();   // Failure code is always written inside critical
      unlock_group_mutex(); // section, so we do this to make sure we get it
      kdu_rethrow(grouperr->failure_code); 
    }
  assert(cond != NULL);
  if (!cond->signalled)
    { 
      assert(cond->thread_idx == this->thread_idx);
      cond->debug_text = debug_text;
      process_jobs(cond);
      if (grouperr->failed)
        {
          lock_group_mutex();
          unlock_group_mutex();
          kdu_rethrow(grouperr->failure_code);
        }
    }
  cond->signalled = false; // Reset to non-signalled state
}

/*****************************************************************************/
/*                    kdu_thread_entity::signal_condition                    */
/*****************************************************************************/

void
  kdu_thread_entity::signal_condition(kdu_thread_entity_condition *cond,
                                      bool foreign_caller)
{
  if ((cond == NULL) || (!exists()) || cond->signalled)
    return;
  int idx = cond->thread_idx;
  if ((idx < 0) || (idx >= group->num_threads))
    { // Should never happen 
      assert(0);
      return;
    }
  cond->signalled = true;
  if (group->idle_pool.remove(idx) &&
      (foreign_caller || (group->threads[idx] != this)))
    { 
      if (!group->wake_thread(idx))
        { KDU_ERROR_DEV(e,5); e <<
          KDU_TXT("Internal error encountered while trying to access "
                  "consistent multi-threaded support services from the "
                  "operating system.  Attempt to signal semaphore failed "
                  "while other state information suggests that a thread "
                  "might be blocked on the semaphore!!  Deadlock may "
                  "ensue.");
        }
    }
}

/*****************************************************************************/
/*          kdu_thread_entity::generate_deadlock_error_and_report            */
/*****************************************************************************/

void
  kdu_thread_entity::generate_deadlock_error_and_report()
{
  KDU_ERROR_DEV(e,0x21021201); e <<
  KDU_TXT("System is entering deadlock!!");
  for (int t=0; t < group->num_threads; t++)
    { 
      kdu_thread_entity *thrd = group->threads[t];
      e << "\n   Thread " << t << ":";
      kdu_thread_entity_condition *cond = thrd->cur_condition->link;
      if (cond == NULL)
        e << " <idle>";
      else
        { 
          for (; cond != NULL; cond=cond->link)
            { 
              const char *string = cond->debug_text;
              string = (string==NULL)?"":string;
              e << "\n      Waiting for \"" << string << "\"";
            }
        }
    }
}

/*****************************************************************************/
/*                       kdu_thread_entity::terminate                        */
/*****************************************************************************/

bool
  kdu_thread_entity::terminate(kdu_thread_queue *root_queue,
                               bool descendants_only, kdu_exception *exc_code)
{
  if (exc_code != NULL)
    *exc_code = KDU_NULL_EXCEPTION;
  if (!exists())
    return true; // Because false means an exception occurred
  assert(check_current_thread());
  send_termination_requests(root_queue,descendants_only);
  return join(root_queue,descendants_only,exc_code);
}
  
/*****************************************************************************/
/*               kdu_thread_entity::send_termination_requests                */
/*****************************************************************************/

void
  kdu_thread_entity::send_termination_requests(kdu_thread_queue *queue,
                                               bool descendants_only)
{
  if ((queue != NULL) && (queue->group == NULL))
    return; // No point in locking the mutex to terminate queue that has
            // already been terminated or joined.
  lock_group_mutex();
  try {
    bool queue_is_done = true;
    if ((queue == NULL) || (queue->group != NULL))
      {
        if ((queue != NULL) && !descendants_only)
          { // Perform the operation on `queue'
            kdu_int32 old_state, new_state;
            while ((new_state = old_state = queue->completion_state.get()) &
                   KD_THREAD_QUEUE_CSTATE_D)
              { 
                new_state |= KD_THREAD_QUEUE_CSTATE_T;
                if (queue->completion_state.compare_and_set(old_state,
                                                            new_state))
                  break;
              }
            if (new_state & KD_THREAD_QUEUE_CSTATE_D)
              { 
                queue_is_done = false;
                if (!(old_state & KD_THREAD_QUEUE_CSTATE_T))
                  queue->request_termination(this);
              }
          }
        if (queue_is_done)
          { // Propagate the termination request immediately
            kdu_thread_queue *qp =
              (queue==NULL)?group->top_queues:queue->descendants;
            for (; qp != NULL; qp=qp->next_sibling)
              send_termination_requests(qp,false);
          }
      }
  }
  catch (...) {
    unlock_group_mutex();
    throw;
  }
  unlock_group_mutex();
}

/*****************************************************************************/
/*               kdu_thread_entity::wait_for_exceptional_join                */
/*****************************************************************************/

void
  kdu_thread_entity::wait_for_exceptional_join()
{
  kdu_int32 delta_ejd = (in_process_jobs)?-1:0;
  kdu_int32 old_ejd =
    group->exceptional_join_dependencies.exchange_add(delta_ejd);
  assert((old_ejd & 0xFFFF) >= delta_ejd);
  kdu_int32 new_ejd = old_ejd + delta_ejd;
  while (new_ejd & 0xFFFF)
    { // We need to wait for at least one other thread
      kdu_int32 old_waiter = old_ejd & 0xFFFF0000;
      old_ejd = new_ejd;
      new_ejd += ((1+this->thread_idx) << 16) - old_waiter;
      if (!group->exceptional_join_dependencies.compare_and_set(old_ejd,
                                                                new_ejd))
        { 
          new_ejd = group->exceptional_join_dependencies.get();
          continue;
        }
      
      // If we get here, we have at least one dependency, we have installed
      // ourselves as the waiting thread, and `old_waiter' saves the
      // identify of any previous waiting thread.  We can safely wait to be
      // woken up.
      old_ejd = new_ejd;
      do { 
        group->wait(this);
        new_ejd = group->exceptional_join_dependencies.get();
      } while (new_ejd == old_ejd); // If no change, semaphore might have been
                                    // spuriously signalled.
      if (old_waiter != 0)
        { // Simplest to just wake up the old waiting thread here, so we can
          // go around the loop again without having to remember multiple
          // waiting threads.  This may cause some contention overhead because
          // the waiting thread will most likely wake up to find that the
          // `exceptional_join_dependencies' value has increased (we are
          // about to increase it before leaving this function), after which
          // it will have to wait again at least until the current thread
          // exits from `process_jobs'.  However, this saves us the need for
          // complex conditional manipulations of the join dependencies value,
          // considering that there may be other waiters that are installing
          // themselves concurrently.
          group->wake_thread((old_waiter >> 16) - 1);
        }
    }
  if (delta_ejd != 0)
    group->exceptional_join_dependencies.exchange_add(-delta_ejd);
}

/*****************************************************************************/
/*                         kdu_thread_entity::join                           */
/*****************************************************************************/

bool
  kdu_thread_entity::join(kdu_thread_queue *root_queue,
                          bool descendants_only, kdu_exception *exc_code)
{
  bool no_exceptions = true;
  if (exc_code != NULL)
    *exc_code = KDU_NULL_EXCEPTION;
  if ((!exists()) ||
      ((root_queue != NULL) && (root_queue->group == NULL) &&
       !grouperr->failed))
    return true; // Because false means an exception occurred
  assert(check_current_thread());

  kdu_thread_queue *queue;
  do {
    lock_group_mutex();
    queue = root_queue;
    if (queue == NULL)
      queue = group->top_queues;
    else if (descendants_only)
      queue = queue->descendants;
    if ((queue != NULL) && (queue->group != NULL))
      { // Otherwise the queue has already been unlinked
        kdu_int32 cstate = queue->completion_state.get();
        if ((root_queue == NULL) &&
            (cstate & KD_THREAD_QUEUE_CSTATE_D) &&
            !(cstate & KD_THREAD_QUEUE_CSTATE_T))
          { // Top-level queue, termination not requested; give such queues
            // an opportunity to automatically call `all_done' if they have
            // no other way of knowing when to do so.
            assert(queue == group->top_queues);
            queue->notify_join(this);
            cstate = queue->completion_state.get();
          }
        kdu_thread_entity_condition *old_cond = NULL;
        while (cstate & KD_THREAD_QUEUE_CSTATE_C_MASK)
          { // Waiting for C=0
            kdu_int32 cstate_or_w = cstate | KD_THREAD_QUEUE_CSTATE_W;
            if (queue->completion_state.compare_and_set(cstate,cstate_or_w))
              break;
            cstate = queue->completion_state.get();
          }
        if ((cstate & KD_THREAD_QUEUE_CSTATE_C_MASK) && !grouperr->failed)
          { // We have installed the W flag; now we need to wait
            kdu_thread_entity_condition *local_cond = push_condition();
              // Best to use a totally fresh condition to wait upon here, also
              // group owner might not have a current condition yet.
            old_cond = queue->completion_waiter;
            queue->completion_waiter = local_cond;
            unlock_group_mutex();
            if (!local_cond->signalled)
              { 
                local_cond->debug_text = "join/terminate";
                process_jobs(local_cond);
              }
            lock_group_mutex();
            assert(queue->completion_waiter == NULL);
            queue->completion_waiter = NULL; // Just in case
            pop_condition();
          }
        if (queue->group != NULL)
          { // Otherwise someone has already unlinked the queue
            queue->unlink_from_thread_group(this);
          }
        if (old_cond != NULL)
          signal_condition(old_cond); // Pass the wakeup call along; there
             // is no harm in doing this even if `handle_exception' has been
             // called.  It is also safe to call `signal_condition' while
             // the group mutex is locked.
      }
    if (no_exceptions && grouperr->failed)
      { 
        no_exceptions = false;
        if (exc_code != NULL)
          *exc_code = grouperr->failure_code;
      }
    unlock_group_mutex();
  } while ((queue != NULL) && (queue != root_queue));
  
  if (!no_exceptions)
    wait_for_exceptional_join();
  return no_exceptions;
}

/*****************************************************************************/
/*                   kdu_thread_entity::handle_exception                     */
/*****************************************************************************/

void
  kdu_thread_entity::handle_exception(kdu_exception exc_code)
{
  if (!exists())
    return;
  lock_group_mutex();
  if (!grouperr->failed)
    { 
      grouperr->failure_code = exc_code;
      grouperr->failed = true;
      kdu_thread_queue *scan, *next;
      for (scan=group->top_queues; scan != NULL; scan=next)
        { 
          next = scan->next_sibling;
          scan->unlink_from_thread_group(this,true);
        }
      for (int n=0; n < group->num_threads; n++)
        { // Make sure all threads wake up
          group->thread_semaphores[n].signal();
        }
    }
  kdu_thread_context *ctxt;
  for (ctxt=group->contexts; ctxt != NULL; ctxt=ctxt->next)
    { 
      ctxt->handle_exception(this);
      assert(ctxt->group == group); // Above call must not remove ctxt from grp
    }
  unlock_group_mutex();
}

/*****************************************************************************/
/*                  kdu_thread_entity::advance_work_domains                  */
/*****************************************************************************/

void
  kdu_thread_entity::advance_work_domains()
{
  if ((group == NULL) || (grouperr == NULL) || grouperr->failed)
    return;
  for (int d=0; d < KDU_MAX_DOMAINS; d++)
    { 
      kd_thread_domain_sequence *seq = work_domains[d];
      if (seq == NULL)
        break;
      kd_thread_domain *domain = seq->domain;
      bool something_to_remove = false;
      while ((seq->active_state.get() == 0) &&
             seq->check_queue_terminated())
        { // Advance to the next domain sequence
          assert(seq->next != NULL);
          work_domains[d] = seq->next->add_consumer();
          if (seq->remove_consumer())
            something_to_remove = true;
          seq = work_domains[d];
        }
      if (something_to_remove)
        { 
          lock_group_mutex();
          domain->remove_unused_sequences();
          unlock_group_mutex();
        }
    }
}

/*****************************************************************************/
/*                      kdu_thread_entity::process_jobs                      */
/*****************************************************************************/

void
  kdu_thread_entity::process_jobs(kdu_thread_entity_condition *cond)
{
  assert(group_mutex_lock_count == 0);
  bool was_in_process_jobs = this->in_process_jobs;
  bool was_in_process_jobs_with_cond = this->in_process_jobs_with_cond;
  this->in_process_jobs = true;
  this->in_process_jobs_with_cond = (cond != NULL);
  kd_thread_flags this_thread_flag = ((kd_thread_flags) 1) << thread_idx;
  kdu_int32 delta_nww=0; // Change in `group->num_non_waiting_workers'
  kdu_int32 delta_ejd=0; // Change in `group->exceptional_join_dependencies'
  if (thread_idx != 0)
    { // Worker thread -- i.e., not the group owner
      if (!in_process_jobs_with_cond)
        { 
          delta_nww = delta_ejd = 1; // New worker thread just started
          group->non_waiting_worker_flags.exchange_or(this_thread_flag);
        }
      else if (!was_in_process_jobs_with_cond)
        { // Outermost `wait_for_condition' or similar call
          // by worker thread has temporarily donated its
          // resources to processing other jobs
          delta_nww = -1;
          group->non_waiting_worker_flags.exchange_and(~this_thread_flag);
        }
    }
  else if (!was_in_process_jobs_with_cond)
    { 
      assert(!was_in_process_jobs);
      delta_ejd=1; // Group owner just entered context in which it can do jobs
    }
  if (delta_nww != 0)
    group->num_non_waiting_workers.exchange_add(delta_nww);
  if (delta_ejd != 0)
    group->exceptional_join_dependencies.exchange_add(delta_ejd);
  push_condition();
  bool added_to_idle_pool=false; // True if we have added ourself to the pool.
            // If this flag is false, we should be guaranteed that we are
            // not on the thread group's idle pool.  However, if the flag is
            // true, we cannot be sure whether or not another thread has
            // removed us from the idle pool.
  bool is_default = thread_domain->is_default();
  while ((!grouperr->failed) && ((cond == NULL) || !cond->signalled))
    { 
      kd_thread_domain_sequence *seq=NULL;
      kdu_thread_job *job = NULL;
      
      // Walk through each domain in which we can do work, beginning with
      // our own primary domain, if any, and then cycling through alternate
      // domains (based upon `alt_work_idx') to see if there is any job
      // available.  If we find that a domain sequence has been completely
      // consumed, we can simply move on to the next domain sequence,
      // marking the existing one as consumed.  If we find that there is no
      // work to do at all within currently active domain sequences that
      // have not yet been terminated, we pass through a second time looking
      // for jobs that might exist on the next job sequence after the
      // current one.
      int next_iteration=0;
      int d_next, d=(is_default)?alt_work_idx:0;
      for (; (next_iteration < 2) && (job == NULL); d=d_next)
        { 
          bool consider_next_seq = (next_iteration > 0);
          if ((d >= KDU_MAX_DOMAINS) ||
              ((seq = this->work_domains[d]) == NULL))
            { 
              if (is_default)
                { 
                  d_next = 0;
                  if (d_next == alt_work_idx)
                    next_iteration++;
                }
              else
                { 
                  d_next = 1;
                  if (d_next == alt_work_idx)
                    { d = 0; next_iteration++; }
                }
              continue;
            }
          if (is_default)
            { 
              d_next = d+1;
              if (d_next == alt_work_idx)
                next_iteration++;
            }
          else
            { 
              if (d == 0)
                d_next = alt_work_idx;
              else if ((d_next=d+1) == alt_work_idx)
                { d_next = 0; next_iteration++; }
            }

          if ((cond != NULL) && seq->domain->background)
            { 
              if (seq->domain->safe_context)
                { 
                  if (was_in_process_jobs || !first_wait_safe)
                    continue; // This is not a safe context from which to
                              // schedule jobs within this domain.
                }
              else if (group->num_non_waiting_workers.get() > 0)
                continue; // We should not do background jobs so long as
                          // other threads exist that are not in the
                          // working wait state.
            }
          
          job = seq->get_job(this->hzp);
          if (job != NULL)
            { 
              if (job == KD_JOB_TERMINATOR)
                { // Advance to next domain sequence
                  job = NULL;
                  kd_thread_domain *domain = seq->domain;
                  assert(seq->next != NULL);
                  work_domains[d] = seq->next->add_consumer();
                  if (seq->remove_consumer())
                    { 
                      lock_group_mutex();
                      domain->remove_unused_sequences();
                      unlock_group_mutex();
                    }
                }
             }
          else if (consider_next_seq && (seq->next != NULL) &&
                   ((job = seq->next->get_job(this->hzp)) != NULL) &&
                   (job == KD_JOB_TERMINATOR))
            job = NULL;
        }
      
      if (job == NULL)
        { // We should enter the idle pool.  However, we need to be a bit
          // careful here, because it could happen that all threads go idle
          // at once without anyone to wake the others up.  This can happen
          // if the most recently scheduled jobs arrived after we examined
          // the job queues but before we had a chance to register ourselves
          // as idle.  For this reason, we need to make at least one pass
          // through the domains looking for work after we first register
          // ourself as an idle thread.
          added_to_idle_pool = true;
          if (!group->idle_pool.add(this->thread_idx))
            continue; // Seems that we were not previously on the idle pool
          /* // The following commented code fragment is useful for discovering
             // possible sources of deadlock.  However, for production code, we
             // eliminate this because it is possible that the `all_idle'
             // function returns true when in fact not all threads have
             // actually gone idle -- this is because a thread first announces
             // its intention to become idle and then checks one more time for
             // a job -- if one is found, the idle condition is cleared, but
             // another thread might race around, find no jobs, think all
             // threads are idle and generate an error report unnecessarily.
             // In the future, we could re-activate this mechanism but
             // include extra checks to verify that everything is indeed
             // idle.           
          if (group->idle_pool.all_idle(group->num_threads))
            { 
              try {
                this->generate_deadlock_error_and_report();
              }
              catch (kdu_exception exc) {
                handle_exception(exc);
                continue;
              }
              catch (std::bad_alloc) {
                handle_exception(KDU_MEMORY_EXCEPTION);
                continue;
              }
              catch (...) {
                handle_exception(KDU_CONVERTED_EXCEPTION);
                continue;
              }
            }
          */
          group->wait(this);
          added_to_idle_pool = false;
          continue; // Go back now and look for work all over again
        }
      else if (added_to_idle_pool)
        { // It is best for us to remove ourselves explicitly, even though
          // another thread may have done it already for us.
          group->idle_pool.remove(this->thread_idx);
          added_to_idle_pool = false;
        }
      
      // If we get here, we have a job to perform
      if ((d != 0) || !is_default)
        { // The job does not come from the thread's preferred work domain
          alt_work_idx = d+1;
          if ((alt_work_idx >= KDU_MAX_DOMAINS) ||
              (work_domains[alt_work_idx] == NULL))
            alt_work_idx = (is_default)?0:1;
        }
      try {
        assert((this->group != NULL) &&
               (this->grouperr == &group->grouperr) &&
               (this == group->threads[thread_idx]) &&
               (this->group_mutex_lock_count == 0));
#ifdef KDU_THREAD_TRACE_RECORDS
        group->thread_trace.add_entry(this->thread_idx,
                                      seq->domain,seq->sequence_idx,
                                      seq->trace_get.exchange_add(1),
                                      seq->trace_add.get());
#endif // KDU_THREAD_TRACE_RECORDS
        job->do_job(this);
        assert((this->group != NULL) &&
               (this->grouperr == &group->grouperr) &&
               (this == group->threads[thread_idx]) &&
               (this->group_mutex_lock_count == 0));
           /* Note: the asserts placed before and after `do_job' above are
              just there to catch any serious programming errors. */
        job_counter++;
        if (yield_freq > 0)
          {
            yield_counter++;
            while (yield_counter >= yield_freq)
              {
                yield_counter -= yield_freq;
                if (yield_counter <= 0)
                  { // Could become -ve if `yield_freq' updated from different
                    // thread asynchronously -- not a big issue.
                    yield_counter = 0;
                    yield_outstanding = true;
                  }
              }
          }
        if (yield_outstanding)
          { // Allows other threads/tasks to do work; it
            // is better that we get interrupted here, rather than
            // getting pre-empted for a potentially long time, while
            // in the middle of something important.
            yield_outstanding = false;
              thread.yield();
          }
      }
      catch (kdu_exception exc) {
        handle_exception(exc);
      }
      catch (std::bad_alloc) {
        handle_exception(KDU_MEMORY_EXCEPTION);
      }
      catch (...) {
        handle_exception(KDU_CONVERTED_EXCEPTION);
      }
    }

  if (added_to_idle_pool)
    group->idle_pool.remove(this->thread_idx); // Make absolutely sure
        // we do not appear to be idle -- this could happen if we added
        // ourselves to the idle pool and then subsequently detected the
        // signalling of the condition (`cond') we were waiting upon.
  if (delta_nww != 0)
    {
      group->num_non_waiting_workers.exchange_add(-delta_nww);
      if (delta_nww > 0)
        group->non_waiting_worker_flags.exchange_and(~this_thread_flag);
      else
        group->non_waiting_worker_flags.exchange_or(this_thread_flag);
    }
  if (delta_ejd != 0)
    { 
      assert(delta_ejd == 1);
      kdu_int32 old_ejd, new_ejd;
      do { // Enter compare-and-set loop
        old_ejd = group->exceptional_join_dependencies.get();
        new_ejd = old_ejd - delta_ejd;
        if ((new_ejd & 0xFFFF) == 0)
          new_ejd = 0; // Removes off any waiting thread's identity
      } while (!group->exceptional_join_dependencies.compare_and_set(old_ejd,
                                                                     new_ejd));
      assert((old_ejd & 0xFFFF) > 0);
      if ((new_ejd == 0) && ((old_ejd >>= 16) > 0))
        group->wake_thread(old_ejd-1);
    }
  pop_condition();
  this->in_process_jobs_with_cond = was_in_process_jobs_with_cond;
  this->in_process_jobs = was_in_process_jobs;
}


/* ========================================================================= */
/*                            kdu_thread_context                             */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdu_thread_context::attach                          */
/*****************************************************************************/

void
  kdu_thread_context::enter_group(kdu_thread_entity *caller)
{ // Called with the thread group mutex locked
  assert((group == NULL) && caller->exists());
  num_locks = this->get_num_locks();
  locks = NULL;
  lock_handle = NULL;
  if (num_locks > 0)
    { 
      int locks_per_line = 1 +
        (int)((KDU_MAX_L2_CACHE_LINE-1)/sizeof(kd_thread_lock));
      lock_handle = new kd_thread_lock[num_locks + (2*locks_per_line-1)];
      int align_off =
        (- _addr_to_kdu_int32(lock_handle)) & (KDU_MAX_L2_CACHE_LINE-1);
      locks = lock_handle;
      for (; align_off > 0; align_off -= (int)sizeof(kd_thread_lock))
        locks++;
      for (int n=0; n < num_locks; n++)
        {
          locks[n].holder = NULL;
          locks[n].mutex.create();
        }
    }

  caller->lock_group_mutex();
  group = caller->group;
  grouperr = caller->grouperr;
  prev = NULL;
  if ((next = group->contexts) != NULL)
    next->prev = this;
  group->contexts = this;
  this->num_threads_changed(group->num_threads); // See interface documentation
  caller->unlock_group_mutex();
}

/*****************************************************************************/
/*                     kdu_thread_context::leave_group                       */
/*****************************************************************************/

void
  kdu_thread_context::leave_group(kdu_thread_entity *caller)
{
  kd_thread_group *grp = this->group; // Because `group' may become NULL
  if (grp != NULL)
    { 
      assert((caller == NULL) || (caller->group == grp));
      if (caller != NULL)
        caller->lock_group_mutex();
      else
        grp->mutex.lock();
      if (group != NULL)
        {
          if (prev == NULL)
            { 
              assert(group->contexts == this);
              group->contexts = next;
            }
          else
            { 
              assert(prev->next == this);
              prev->next = next;
            }
          if (next != NULL)
            next->prev = prev;
          this->group = NULL;
          this->grouperr = NULL;
        }
      if (caller != NULL)
        caller->unlock_group_mutex();
      else
        grp->mutex.unlock();
    }

  if (locks != NULL)
    { 
      for (int n=0; n < num_locks; n++)
        {
          assert(locks[n].holder == NULL); // You should not be releasing a
          locks[n].mutex.destroy();        // thread context while holding one
        }                                  // of its locks!
      delete[] lock_handle;
    }
  num_locks = 0;
  locks = NULL;
  lock_handle = NULL;
}

/*****************************************************************************/
/*                    kdu_thread_context::handle_exception                   */
/*****************************************************************************/

void
  kdu_thread_context::handle_exception(kdu_thread_entity *caller)
{
  assert(this->group == caller->group);
  for (int n=0; n < num_locks; n++)
    if (locks[n].holder == caller)
      release_lock(n,caller);
}
