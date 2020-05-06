/*****************************************************************************/
// File: kdcs_comms.cpp [scope = APPS/CLIENT-SERVER]
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
  Implements the communications objects and functions defined in
"kdcs_comms.h".
******************************************************************************/

#include <math.h>
#include "comms_local.h"
#include "kdu_messaging.h"
#include "kdu_utils.h"

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
kdu_error _name("E(client_server_comms.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
kdu_warning _name("W(client_server_comms.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
kdu_error _name("Error in Client/Server Comms:\n");
#  define KDU_WARNING(_name,_id) \
kdu_warning _name("Warning in Client/Server Comms:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
// Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
// Use the above version for warnings which are of interest only to developers


#ifdef KDCS_WIN_SOCKETS
class kdcs_network_services {
  public: // Functions
    kdcs_network_services() { started=false; mutex.create(); }
    ~kdcs_network_services() { stop(); mutex.destroy(); }
    bool start()
      {
        if (started) return true;
        mutex.lock();
        if (!started)
          { 
            WORD wsa_version = MAKEWORD(2,2);
            WSADATA wsa_data;
            WSAStartup(wsa_version,&wsa_data);
            started = true;
          }
        mutex.unlock();
        return true;
      }
    void stop()
      { 
        mutex.lock();
        if (started)
          { 
            WSACleanup();
            started = false;
          }
        mutex.unlock();
      }
  private: // Data
    kdu_mutex mutex;
    bool started;
  };
#else // !Windows
#  include <signal.h>
class kdcs_network_services {
  public: // Functions
    kdcs_network_services()
      { 
        signal(SIGPIPE,SIG_IGN);
      }
    bool start() { return true; }
    void stop() {}
  };
#endif // !KDCS_WIN_SOCKETS

static kdcs_network_services network_services;

/* ========================================================================= */
/*                          Windows APC callbacks                            */
/* ========================================================================= */

#ifdef KDCS_WIN_SOCKETS

/*****************************************************************************/
/* STATIC                    kdcs_select_timer_apc                           */
/*****************************************************************************/

VOID CALLBACK kdcs_select_timer_apc(LPVOID ptr, DWORD low_val, DWORD high_val)
{
  kdu_byte *addr = (kdu_byte *)ptr;
  int phase = _addr_to_kdu_int32(addr) & 3;
  addr -= phase;
  kdcs_select_interruptor *obj = (kdcs_select_interruptor *)addr;
  if (obj->in_select && (obj->timer_apc_phase == phase) &&
      obj->interrupt_socket.is_valid())
    { 
      obj->interrupt_socket.close();
      obj->interrupt_socket.sock = socket(AF_INET,SOCK_DGRAM,0);
      assert(obj->interrupt_socket.is_valid());
    }
}

/*****************************************************************************/
/* STATIC                  kdcs_select_interrupt_apc                         */
/*****************************************************************************/

VOID CALLBACK kdcs_select_interrupt_apc(ULONG_PTR dwParam)
{
  kdcs_select_interruptor *obj = (kdcs_select_interruptor *)dwParam;
  kdu_int32 old_val, new_val;
  do { // enter compare-and-set loop
    old_val = obj->interrupt_apc_counter.get();
    new_val = (old_val > 0)?(old_val-1):old_val;
  } while (!obj->interrupt_apc_counter.compare_and_set(old_val,new_val));
  if (new_val > 0)
    return; // There are still more APC calls on the queue
  if (obj->in_select && obj->interrupt_socket.is_valid())
    { 
      obj->interrupt_socket.close();
      obj->interrupt_socket.sock = socket(AF_INET,SOCK_DGRAM,0);
      assert(obj->interrupt_socket.is_valid());
    }
}

#endif // KDCS_WIN_SOCKETS


/* ========================================================================= */
/*                                FUNCTIONS                                  */
/* ========================================================================= */

namespace kdu_supp {
    
/*****************************************************************************/
/* EXTERN                      kdcs_microsleep                               */
/*****************************************************************************/

void kdcs_microsleep(int usecs)
{
#  ifdef KDCS_WIN_SOCKETS
  Sleep((usecs+999)/1000);
#  else // KDCS_BSD_SOCKETS
  kdu_timespec spec;
  spec.tv_sec = (usecs / 1000000);
  spec.tv_nsec = (usecs % 1000000)*1000;
  nanosleep(&spec,NULL);
#  endif // KDCS_BSD_SOCKETS
}

/*****************************************************************************/
/* EXTERN                        kdcs_chdir                                  */
/*****************************************************************************/

bool kdcs_chdir(const char *pathname)
{
#ifdef KDCS_WIN_SOCKETS
  return (_chdir(pathname) == 0);
#elif (defined KDCS_BSD_SOCKETS)
  return (chdir(pathname) == 0);
#else
  return false;
#endif
}

/*****************************************************************************/
/* EXTERN                    kdcs_start_network                              */
/*****************************************************************************/

bool kdcs_start_network()
{
  return network_services.start();
}

/*****************************************************************************/
/* EXTERN                   kdcs_cleanup_network                             */
/*****************************************************************************/

void kdcs_cleanup_network()
{
  network_services.stop();
}

} // namespace kdu_supp


/* ========================================================================= */
/*                            kdcs_message_block                             */
/* ========================================================================= */

/*****************************************************************************/
/*                  kdcs_message_block::~kdcs_message_block                  */
/*****************************************************************************/

kdcs_message_block::~kdcs_message_block()
{
  if (block != NULL) delete[] block;
  if (text != NULL) delete[] text;
}

/*****************************************************************************/
/*                       kdcs_message_block::read_line                       */
/*****************************************************************************/

const char *
  kdcs_message_block::read_line(char delim)
{
  if (text == NULL)
    {
      text_max = 100;
      text = new char[text_max+1];
    }
  int text_len = 0;
  bool skip_white = !leave_white;
  bool line_start = true;
  while (next_unread < next_unwritten)
    {
      if (text_len == text_max)
        {
          int new_text_max = text_max*2;
          char *new_text = new char[new_text_max+1];
          memcpy(new_text,text,(size_t) text_len);
          delete[] text;
          text = new_text;
          text_max = new_text_max;
        }
      char ch = *(next_unread++);
      if ((ch == '\0') || (ch == delim))
        {
          if (skip_white && !line_start)
            { // Back over last white space character
              assert(text_len > 0);
              text_len--;
            }
          text[text_len++] = ch;
          break;
        }
      else if ((ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n'))
        {
          if (!skip_white)
            {
              if (ch == '\n')
                text[text_len++] = ch;
              else
                text[text_len++] = ' ';
            }
          skip_white = !leave_white;
        }
      else
        {
          line_start = skip_white = false;
          text[text_len++] = ch;
        }
    }
  text[text_len] = '\0';
  if (text_len == 0)
    return NULL;
  return text;
}

/*****************************************************************************/
/*                    kdcs_message_block::read_paragraph                     */
/*****************************************************************************/

const char *
  kdcs_message_block::read_paragraph(char delim)
{
  if (text == NULL)
    {
      text_max = 100;
      text = new char[text_max+1];
    }
  int text_len = 0;
  bool skip_white = !leave_white;
  bool line_start = true;
  while (next_unread < next_unwritten)
    {
      if (text_len == text_max)
        {
          int new_text_max = text_max*2;
          char *new_text = new char[new_text_max+1];
          memcpy(new_text,text,(size_t) text_len);
          delete[] text;
          text = new_text;
          text_max = new_text_max;
        }
      char ch = *(next_unread++);
      if ((ch == '\0') || (ch == delim))
        {
          if (skip_white && !line_start)
            { // Back over last white space character
              assert(text_len > 0);
              text_len--;
            }
          text[text_len++] = ch;
          skip_white = !leave_white;
          line_start = true;
          if ((ch == '\0') || (text_len == 1) || (text[text_len-2] == delim))
            break;
        }
      else if ((ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n'))
        {
          if (!skip_white)
            {
              if (ch == '\n')
                text[text_len++] = ch;
              else
                text[text_len++] = ' ';
            }
          skip_white = !leave_white;
        }
      else
        {
          line_start = skip_white = false;
          text[text_len++] = ch;
        }
    }
  text[text_len] = '\0';
  return text;
}

/*****************************************************************************/
/*                       kdcs_message_block::read_raw                        */
/*****************************************************************************/

kdu_byte *
  kdcs_message_block::read_raw(int num_bytes)
{
  if (block == NULL)
    return NULL;
  kdu_byte *result = next_unread;
  next_unread += num_bytes;
  if (next_unread > next_unwritten)
    return NULL;
  return result;
}

/*****************************************************************************/
/*                      kdcs_message_block::write_raw                        */
/*****************************************************************************/

void
  kdcs_message_block::write_raw(const kdu_byte *buf, int num_bytes)
{
  if (num_bytes <= 0)
    return;
  if (block == NULL)
    {
      block_bytes = 160;
      block = new kdu_byte[block_bytes];
      next_unread = next_unwritten = block;
    }
  if ((next_unwritten - next_unread) < (next_unread - block))
    { // Reclaim wasted space before continuing.
      kdu_byte *scan = block;
      while (next_unread < next_unwritten)
        *(scan++) = *(next_unread++);
      next_unread = block;
      next_unwritten = scan;
    }
  int need_bytes = (int)(next_unwritten-block) + num_bytes;
  if (need_bytes > block_bytes)
    {
      int new_block_bytes = block_bytes + need_bytes;
      kdu_byte *new_block = new kdu_byte[new_block_bytes];
      memcpy(new_block,block,(size_t)(next_unwritten-block));
      next_unwritten += new_block-block;
      next_unread += new_block-block;
      delete[] block;
      block = new_block;
      block_bytes = new_block_bytes;
    }
  memcpy(next_unwritten,buf,(size_t) num_bytes);
  next_unwritten += num_bytes;
}

/*****************************************************************************/
/*                  kdcs_message_block::hex_hex_encode_tail                  */
/*****************************************************************************/

int kdcs_message_block::hex_hex_encode_tail(int num_chars,
                                            const char *special_chars)
{
  if (num_chars == 0)
    return 0;
  if (((int)(next_unwritten-next_unread)) < num_chars)
    { assert(0); num_chars = (int)(next_unwritten-next_unread); }
  char *src = ((char *) next_unwritten) - num_chars;
  int encoded_chars = kdu_hex_hex_encode(src,NULL,src+num_chars,special_chars);
  if (encoded_chars == num_chars)
    return num_chars; // Hex-hex encoding would do nothing
  int needed_bytes = encoded_chars + 1 + (int)(next_unwritten-block);
  if (needed_bytes > block_bytes)
    {
      int new_block_bytes = block_bytes + needed_bytes;
      kdu_byte *new_block = new kdu_byte[new_block_bytes];
      memcpy(new_block,block,(size_t)(next_unwritten-block));
      next_unwritten += new_block-block;
      next_unread += new_block-block;
      src += new_block-block;
      delete[] block;
      block = new_block;
      block_bytes = new_block_bytes;
    }
  char *dst = (char *) next_unwritten;
  kdu_hex_hex_encode(src,dst,src+num_chars,special_chars);
  for (num_chars=encoded_chars; num_chars > 0; num_chars--)
    *(src++) = *(dst++);
  next_unwritten = (kdu_byte *) src;
  return encoded_chars;
}


/*===========================================================================*/
/*                                 kdcs_timer                                */
/*===========================================================================*/

/*****************************************************************************/
/*                           kdcs_timer::kdcs_timer                          */
/*****************************************************************************/

kdcs_timer::kdcs_timer()
{
#ifdef KDCS_BSD_SOCKETS
  tspec.get_time();
  clock_resolution = 1000; // Millisecond resolution should be a safe bet
#else // KDCS_WIN_SOCKETS
  high_resolution_time = 0;
  tick_count_milliseconds = 0;
  high_resolution_freq = 0;
  high_resolution_base = 0;
  LARGE_INTEGER tmp_val;
  if (QueryPerformanceFrequency(&tmp_val))
    high_resolution_freq = tmp_val.QuadPart;
  high_resolution_factor = 0.0;
  if ((high_resolution_freq > 0) && QueryPerformanceCounter(&tmp_val))
    { // Use high resolution timer
      high_resolution_time = tmp_val.QuadPart;
      high_resolution_factor = 1000000.0 / (double) high_resolution_freq;
      clock_resolution = 1 + (int) high_resolution_factor;
    }
  else
    { // Use system clock
      high_resolution_freq = 0;
      tick_count_milliseconds = GetTickCount();
      DWORD adjustment, interval; // Measurements in 100ns intervals
      BOOL disabled;
      if (GetSystemTimeAdjustment(&adjustment,&interval,&disabled))
        clock_resolution = 1 + (interval / 10);
      else
        clock_resolution = 1000;
    }
#endif // BSD or WIN
  last_ellapsed_microseconds = 0;
}

/*****************************************************************************/
/*                  kdcs_timer::get_ellapsed_microseconds                    */
/*****************************************************************************/

kdu_long kdcs_timer::get_ellapsed_microseconds()
{
#ifdef KDCS_BSD_SOCKETS
  kdu_timespec new_tspec;
  new_tspec = tspec; // Suppress warnings from GCC
  new_tspec.get_time();
  kdu_long result = (new_tspec.tv_sec - tspec.tv_sec);
  result = (result * 1000000) + ((new_tspec.tv_nsec-tspec.tv_nsec) / 1000);
  if (result > last_ellapsed_microseconds)
    last_ellapsed_microseconds = result;
#else // KDCS_WIN_SOCKETS
  if (high_resolution_freq > 0)
    { // Have high resolution timer
      LARGE_INTEGER tmp_val;
      __int64 new_time = high_resolution_time;
      if (QueryPerformanceCounter(&tmp_val))
        new_time = tmp_val.QuadPart;
      __int64 increment = new_time - high_resolution_time;
      if (increment < -0x4000000000000000)
        increment += 0x8000000000000000; // Overflow detected
      if (increment > 0)
        { // Adjust ellapsed microseconds carefully here
          last_ellapsed_microseconds = high_resolution_base +
            (kdu_long)(increment * high_resolution_factor);
          if (increment > 0x1000000000000000)
            { // Adjust the `high_resolution_base'
              high_resolution_base = last_ellapsed_microseconds;
              high_resolution_time = new_time;
            }
        }
    }
  else
    { // Use low resolution timer
      kdu_uint32 new_tick_count = GetTickCount();
      int increment = (int)(new_tick_count - tick_count_milliseconds);
      if (increment < -0x40000000)
        increment += 0x80000000; // Overflow detected
      if (increment > 0)
        {
          last_ellapsed_microseconds += ((kdu_long) increment) * 1000;
          tick_count_milliseconds = new_tick_count;
        }
    }
#endif // BSD or WIN
  return last_ellapsed_microseconds;
}


/*===========================================================================*/
/*                           kdcs_channel_servicer                           */
/*===========================================================================*/

/*****************************************************************************/
/*               kdcs_channel_servicer::kdcs_channel_servicer                */
/*****************************************************************************/

kdcs_channel_servicer::kdcs_channel_servicer()
{
  ref_count.set(1);
  next_replacement = NULL;
}

/*****************************************************************************/
/*               kdcs_channel_servicer::~kdcs_channel_servicer               */
/*****************************************************************************/

kdcs_channel_servicer::~kdcs_channel_servicer()
{
  assert(ref_count.get() == 0);
  assert(next_replacement == NULL); // Any replacements should have been
                       // removed first by `kdcs_channel_monitor::run_once'.
}

/*****************************************************************************/
/*                        kdcs_channel_servicer::add_ref                     */
/*****************************************************************************/

void kdcs_channel_servicer::add_ref()
{
  kdu_int32 old_val = ref_count.exchange_add(1);
  if (old_val <= 0)
    assert(0);
}

/*****************************************************************************/
/*                     kdcs_channel_servicer::release_ref                    */
/*****************************************************************************/

void kdcs_channel_servicer::release_ref()
{
  kdu_int32 old_val = ref_count.exchange_add(-1);
  if (old_val == 1)
    delete this;
}

/*****************************************************************************/
/*                 kdcs_channel_servicer::append_replacement                 */
/*****************************************************************************/

void kdcs_channel_servicer::append_replacement(kdcs_channel_servicer *repl)
{
  if ((repl == this) || (repl == NULL))
    return; // Nothing to do
  assert(repl->next_replacement == NULL);
  kdcs_channel_servicer *prev=this, *scan=next_replacement;
  for (; scan != NULL; prev=scan, scan=scan->next_replacement)
    if (scan == repl)
      return;
  prev->next_replacement = repl;
}


/*===========================================================================*/
/*                          kdcs_select_interruptor                          */
/*===========================================================================*/

/*****************************************************************************/
/*             kdcs_select_interruptor::kdcs_select_interruptor              */
/*****************************************************************************/

kdcs_select_interruptor::kdcs_select_interruptor(kdcs_channel_monitor *monitor)
{
  this->owner = monitor;
#ifdef KDCS_WIN_SOCKETS
  run_thread = NULL;
  run_thread_id = 0;
  waitable_timer = NULL;
  interrupt_apc_counter.set(0);
  timer_apc_phase = 0;
#else // assume BSD sockets
  pipe_rdfd = pipe_wrfd = 0;
  pipe_valid = false;
#endif // BSD sockets
}

/*****************************************************************************/
/*                       kdcs_select_interruptor::init                       */
/*****************************************************************************/

bool kdcs_select_interruptor::init()
{
#ifdef KDCS_WIN_SOCKETS
  assert(waitable_timer == NULL);
  assert(!interrupt_socket.is_valid());
  waitable_timer = CreateWaitableTimer(NULL,TRUE,NULL);
  if (waitable_timer == NULL)
    return false;
  interrupt_socket.sock = socket(AF_INET,SOCK_DGRAM,0);
  if (!interrupt_socket.is_valid())
    return false;
#else // assume BSD sockets
  assert(!pipe_valid);
  int fd[2];
  pipe_valid = (pipe(fd) == 0);
  if (!pipe_valid)
    return false;
  pipe_rdfd = fd[0];
  pipe_wrfd = fd[1];
  int rd_flgs = ::fcntl(pipe_rdfd,F_GETFL);
  if (rd_flgs != -1)
    ::fcntl(pipe_rdfd,F_SETFL,(rd_flgs|O_NONBLOCK));
  int wr_flgs = ::fcntl(pipe_wrfd,F_GETFL);
  if (wr_flgs != -1)
    ::fcntl(pipe_rdfd,F_SETFL,(wr_flgs|O_NONBLOCK));
#endif // BSD sockets
  return true;
}

/*****************************************************************************/
/*             kdcs_select_interruptor::~kdcs_select_interruptor             */
/*****************************************************************************/

kdcs_select_interruptor::~kdcs_select_interruptor()
{
#ifdef KDCS_WIN_SOCKETS
  if (run_thread != NULL)
    { CloseHandle(run_thread); run_thread = NULL; }
  if (waitable_timer != NULL)
    { CloseHandle(waitable_timer); waitable_timer = NULL; }
  interrupt_socket.close();
#else // assume BSD sockets
  if (pipe_valid)
    { close(pipe_rdfd); close(pipe_wrfd); pipe_valid = false; }
#endif // BSD sockets
}

/*****************************************************************************/
/*               kdcs_select_interruptor::clean_thread_info                  */
/*****************************************************************************/

void kdcs_select_interruptor::clean_thread_info()
{
#ifdef KDCS_WIN_SOCKETS
  if (run_thread != NULL)
    CloseHandle(run_thread);
  run_thread = NULL;
  run_thread_id = 0;
#endif // KDCS_WIN_SOCKETS
}

/*****************************************************************************/
/*                     kdcs_select_interruptor::do_poll                      */
/*****************************************************************************/

bool kdcs_select_interruptor::do_poll(int nfds, kdcs_fd_sets *fd_sets,
                                      kdu_mutex &mutex)
{
  fd_set *read_set  = fd_sets->active_reads;
  fd_set *write_set = fd_sets->active_writes;
  fd_set *error_set = fd_sets->active_errors;
  int sel_result = 0;
  if ((read_set != NULL) || (write_set != NULL) || (error_set != NULL))
    { 
      struct timeval delay;
      delay.tv_sec = 0;
      delay.tv_usec = 0;
      mutex.unlock();
      sel_result = ::select(nfds,read_set,write_set,error_set,&delay);
      mutex.lock();
    }
  return (sel_result > 0);
}

/*****************************************************************************/
/*                    kdcs_select_interruptor::do_select                     */
/*****************************************************************************/

bool kdcs_select_interruptor::do_select(int nfds, kdcs_fd_sets *fd_sets,
                                        int delay_usecs, kdu_mutex &mutex)
{
  assert(delay_usecs > 0);
  assert(fd_sets != NULL);
#ifdef KDCS_WIN_SOCKETS
  if ((run_thread == NULL) || (run_thread_id != ::GetCurrentThreadId()))
    { // Build/rebuild the `run_thread' handle, with appropriate access
      run_thread_id = ::GetCurrentThreadId();
      ::DuplicateHandle(::GetCurrentProcess(),::GetCurrentThread(),
                        ::GetCurrentProcess(),&run_thread,
                        THREAD_SET_CONTEXT | DELETE,FALSE,0);
    }
  if ((run_thread != NULL) && (interrupt_socket.is_valid()) &&
      (waitable_timer != NULL))
    { 
      LARGE_INTEGER due_time;
      due_time.QuadPart = -delay_usecs; // -ve for relative time
      due_time.QuadPart *= 10; // because due time is in multiples of 100ns
      timer_apc_phase &= 3;
      kdu_byte *timer_apc_addr = ((kdu_byte *)this) + timer_apc_phase;
      assert((_addr_to_kdu_int32(timer_apc_addr) & 3) == timer_apc_phase);
      ::SetWaitableTimer(waitable_timer,&due_time,0,
                         ::kdcs_select_timer_apc,timer_apc_addr,TRUE);
      fd_set *read_set  = &(fd_sets->read_set);
      FD_SET(interrupt_socket.sock,read_set);
      fd_set *write_set = fd_sets->active_writes;
      fd_set *error_set = fd_sets->active_errors;
      in_select = true;
      mutex.unlock();
      int sel_result = ::select(0,read_set,write_set,error_set,NULL);
      mutex.lock();
      in_select = false;
      timer_apc_addr++; // In case timer APC call is processed later
      ::CancelWaitableTimer(waitable_timer);
      if (FD_ISSET(interrupt_socket.sock,read_set))
        { 
          FD_CLR(interrupt_socket.sock,read_set);
          return (sel_result > 1);
        }
      else
        return (sel_result > 0);
    }
#else // assume BSD sockets
  if (pipe_valid)
    { 
      struct timeval delay;
      delay.tv_sec = delay_usecs / 1000000;
      delay.tv_usec = delay_usecs % 1000000;
      fd_set *read_set  = &(fd_sets->read_set);
      FD_SET(pipe_rdfd,read_set);
      if (pipe_rdfd >= nfds)
        nfds = pipe_rdfd + 1;
      fd_set *write_set = fd_sets->active_writes;
      fd_set *error_set = fd_sets->active_errors;
      mutex.unlock();
      int sel_result = ::select(nfds,read_set,write_set,error_set,&delay);
      mutex.lock();
      if (FD_ISSET(pipe_rdfd,read_set))
        { // We received an interrupt
          FD_CLR(pipe_rdfd,read_set);
          kdu_byte byte=0;
          while (::read(pipe_rdfd,&byte,1) > 0)
            assert(byte == 0xFF);
          assert(errno == EAGAIN);
          return (sel_result > 1);
        }
      else
        return (sel_result > 0);
    }
#endif // BSD sockets
  return false;
}

/*****************************************************************************/
/*                 kdcs_select_interruptor::interrupt_select                 */
/*****************************************************************************/

void kdcs_select_interruptor::interrupt_select(kdu_mutex *mutex)
{
#ifdef KDCS_WIN_SOCKETS
  if (run_thread != NULL)
    { 
      interrupt_apc_counter.exchange_add(1);
      if (mutex != NULL)
        mutex->lock(); // Guard against any changes in the `run_thread'
      if (run_thread != NULL)
        ::QueueUserAPC(::kdcs_select_interrupt_apc,run_thread,
                       (ULONG_PTR)this);
      if (mutex != NULL)
        mutex->unlock();
    }
#else // assume BSD sockets
  if (pipe_valid)
    { 
      kdu_byte byte=0xFF;
      ::write(pipe_wrfd,&byte,1);
    }
#endif // BSD sockets
}


/*===========================================================================*/
/*                            kdcs_channel_monitor                           */
/*===========================================================================*/

/*****************************************************************************/
/*                kdcs_channel_monitor::kdcs_channel_monitor                 */
/*****************************************************************************/

kdcs_channel_monitor::kdcs_channel_monitor()
{
  network_services.start();
  num_channels = 0;
  max_channels = FD_SETSIZE-2; // Leave 2 spares for interrupting `::select'
  channel_refs = NULL;
  active_fd_sets = new kdcs_fd_sets;
  select_wake_time = -1;
  min_latest_wakeup = KDU_LONG_MAX;
  mutex.create();
  run_state.set(0);
  interruptor = new kdcs_select_interruptor(this);
  if (!interruptor->init())
    run_state.set(KDCS_MONITOR_STATE_CLOSE);
}

/*****************************************************************************/
/*               kdcs_channel_monitor::~kdcs_channel_monitor                 */
/*****************************************************************************/

kdcs_channel_monitor::~kdcs_channel_monitor()
{
  assert(channel_refs == NULL);
  mutex.destroy();
  if (active_fd_sets != NULL)
    { 
      delete active_fd_sets;
      active_fd_sets = NULL;
    }
  if (interruptor != NULL)
    delete interruptor;
}

/*****************************************************************************/
/*                   kdcs_channel_monitor::request_closure                   */
/*****************************************************************************/

void kdcs_channel_monitor::request_closure()
{
  kdu_int32 old_val, new_val;
  do { // enter compare-and-set loop
    old_val = run_state.get();
    new_val = old_val | KDCS_MONITOR_STATE_CLOSE;
    if (old_val & KDCS_MONITOR_STATE_WAITING)
      new_val |= KDCS_MONITOR_STATE_INTERRUPTED;
  } while (!run_state.compare_and_set(old_val,new_val));
  if ((old_val ^ new_val) & KDCS_MONITOR_STATE_INTERRUPTED)
    interruptor->interrupt_select(&mutex);
}

/*****************************************************************************/
/*                 kdcs_channel_monitor::synchronize_timing                  */
/*****************************************************************************/

void kdcs_channel_monitor::synchronize_timing(kdcs_timer &timer)
{
  mutex.lock();
  timer.synchronize(this->timer);
  mutex.unlock();
}

/*****************************************************************************/
/*                  kdcs_channel_monitor::get_current_time                   */
/*****************************************************************************/

kdu_long kdcs_channel_monitor::get_current_time()
{
  mutex.lock();
  kdu_long result = timer.get_ellapsed_microseconds();
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                     kdcs_channel_monitor::add_channel                     */
/*****************************************************************************/

kdcs_channel_ref *
  kdcs_channel_monitor::add_channel(kdcs_channel *channel,
                                    kdcs_channel_servicer *servicer)
{
  if (channel->socket == NULL)
    return NULL;
  mutex.lock();
  kdcs_channel_ref *scan;
  for (scan=channel_refs; scan != NULL; scan=scan->next)
    if (scan->channel == channel)
      break;
  if ((scan == NULL) && (num_channels < max_channels) &&
      !(run_state.get() & KDCS_MONITOR_STATE_CLOSE))
    { 
      scan = new kdcs_channel_ref;
      scan->prev = NULL;
      if ((scan->next = channel_refs) != NULL)
        channel_refs->prev = scan;
      channel_refs = scan;
      scan->active_conditions = 0;
      scan->queued_conditions.set(0);
      scan->in_service = false;
      scan->is_active = false;
      scan->active_next = NULL;
      scan->servicer = NULL;
      scan->channel = channel;
      scan->earliest_wakeup = scan->latest_wakeup = -1;
      num_channels++;
    }
  if (scan != NULL)
    {
      scan->socket = channel->socket;
      if (servicer != scan->servicer)
        { 
          if (servicer != NULL)
            servicer->add_ref();
          if (scan->servicer == NULL)
            scan->servicer = servicer; // Just put it in
          else if (scan->in_service)
            scan->servicer->append_replacement(servicer);
          else
            { // Do the replacement outselves
              scan->servicer->release_ref();
              scan->servicer = servicer;
            }
          scan->earliest_wakeup = scan->latest_wakeup = -1;
          scan->active_conditions = 0;
          scan->queued_conditions.set(0);
        }
    }
  mutex.unlock();
  return scan;
}

/*****************************************************************************/
/*                   kdcs_channel_monitor::remove_channel                    */
/*****************************************************************************/

void kdcs_channel_monitor::remove_channel(kdcs_channel_ref *ref)
{
  if (ref == NULL)
    return; // Should never happen
  bool let_run_once_delete_reference = false;
  mutex.lock();
  if (ref->prev == NULL)
    {
      assert(ref == channel_refs);
      channel_refs = ref->next;
    }
  else
    ref->prev->next = ref->next;
  if (ref->next != NULL)
    ref->next->prev = ref->prev;
  
  if (ref->is_active || ref->in_service)
    { // We can't manipulate the active list here (only inside `run_once') so
      // we mark the reference for `run_once' to delete.
      let_run_once_delete_reference = true;
      ref->socket = NULL; // So that `run_once' will delete it
      ref->earliest_wakeup = ref->latest_wakeup = -1;
      ref->active_conditions = 0;
      ref->queued_conditions.set(0);
    }
  num_channels--;
  mutex.unlock();
  if (!let_run_once_delete_reference)
    {
      assert(ref->socket != NULL);
      if (ref->servicer != NULL)
        ref->servicer->release_ref();
      delete ref;
    }
}

/*****************************************************************************/
/*                  kdcs_channel_monitor::queue_conditions                   */
/*****************************************************************************/

bool
  kdcs_channel_monitor::queue_conditions(kdcs_channel_ref *ref, int cond_flags)
{
  if ((ref == NULL) || (ref->channel->socket == NULL))
    return false;
  kdu_int32 old_val, new_val;
  do { // enter compare-and-set loop
    old_val = ref->queued_conditions.get();
    new_val = old_val | cond_flags;
  } while (!ref->queued_conditions.compare_and_set(old_val,new_val));
  if (old_val == new_val)
    return (run_state.get() & KDCS_MONITOR_STATE_CLOSE)?false:true;
  do { // enter compare-and-set loop
    old_val = run_state.get();
    new_val = old_val | KDCS_MONITOR_STATE_NEW_CONDITIONS;
    if ((old_val & KDCS_MONITOR_STATE_WAITING) &&
        !(old_val & (KDCS_MONITOR_STATE_INTERRUPTED |
                     KDCS_MONITOR_STATE_CLOSE)))
      new_val |= KDCS_MONITOR_STATE_INTERRUPTED;
  } while (!run_state.compare_and_set(old_val,new_val));
  if (old_val & KDCS_MONITOR_STATE_CLOSE)
    return false;
  if ((old_val ^ new_val) & KDCS_MONITOR_STATE_INTERRUPTED)
    interruptor->interrupt_select(&mutex);
  return true;
}

/*****************************************************************************/
/*                   kdcs_channel_monitor::schedule_wakeup                   */
/*****************************************************************************/

bool
  kdcs_channel_monitor::schedule_wakeup(kdcs_channel_ref *ref,
                                        kdu_long earliest, kdu_long latest)
{
  if ((ref == NULL) || (ref->channel->socket == NULL) ||
      (run_state.get() & KDCS_MONITOR_STATE_CLOSE))
    return false;
  assert(latest >= earliest);
  mutex.lock();
  ref->earliest_wakeup = earliest;
  ref->latest_wakeup = latest;
  if (latest < this->min_latest_wakeup)
    this->min_latest_wakeup = latest;
  if (latest < this->select_wake_time)
    { // Can't wait for any pending call to `::select' to wake up by itself
      kdu_int32 old_val, new_val;
      do { // enter compare-and-set loop
        old_val = new_val = run_state.get();
        if ((old_val & KDCS_MONITOR_STATE_WAITING) &&
            !(old_val & (KDCS_MONITOR_STATE_INTERRUPTED |
                         KDCS_MONITOR_STATE_CLOSE)))
          new_val |= KDCS_MONITOR_STATE_INTERRUPTED;
      } while (!run_state.compare_and_set(old_val,new_val));
      if (old_val & KDCS_MONITOR_STATE_CLOSE)
        { 
          mutex.unlock();
          return false;
        }
      if ((old_val ^ new_val) & KDCS_MONITOR_STATE_INTERRUPTED)
        interruptor->interrupt_select(NULL); // `mutex' already locked
    }
  mutex.unlock();
  return true;
}

/*****************************************************************************/
/*                    kdcs_channel_monitor::wake_from_run                    */
/*****************************************************************************/

void
  kdcs_channel_monitor::wake_from_run()
{
  kdu_int32 old_val, new_val;
  do { // enter compare-and-set loop
    old_val = run_state.get();
    new_val = old_val | KDCS_MONITOR_STATE_WAKEUP;
    if ((old_val & KDCS_MONITOR_STATE_WAITING) &&
        !(old_val & (KDCS_MONITOR_STATE_INTERRUPTED |
                     KDCS_MONITOR_STATE_CLOSE)))
      new_val |= KDCS_MONITOR_STATE_INTERRUPTED;
  } while (!run_state.compare_and_set(old_val,new_val));
  if ((old_val ^ new_val) & KDCS_MONITOR_STATE_INTERRUPTED)
    interruptor->interrupt_select(&mutex);
}

/*****************************************************************************/
/*                       kdcs_channel_monitor::run_once                      */
/*****************************************************************************/

bool kdcs_channel_monitor::run_once(int wait_microseconds,
                                    int new_condition_wait_microseconds)
{
  kdu_int32 old_state, new_state;
  do { // enter compare-and-set loop to reset the `NEW_CONDITIONS' flag
    old_state = run_state.get();
    new_state = old_state & ~(KDCS_MONITOR_STATE_NEW_CONDITIONS |
                              KDCS_MONITOR_STATE_INTERRUPTED |
                              KDCS_MONITOR_STATE_WAITING);
  } while (!run_state.compare_and_set(old_state,new_state));

  // Reset the active network condition sets
  int nfds = 0; // Used only for BSD sockets
  active_fd_sets->clear();
  mutex.lock();
  kdu_long current_time = timer.get_ellapsed_microseconds();
  kdu_long target_wake_time = KDU_LONG_MAX;
  kdcs_channel_ref *scan, *active_refs=NULL;
  int max_wait_microseconds = 0; // Evaluated after figuring out if there
                                 // is any `active_refs' list or not.
  do { // Loop until all IMMEDIATE conditions have been cleared
    
    // Scan all channels, looking for those which have network conditions
    // and performing any immediate service that is required, building a
    // list of `active_refs' that we intend to monitor using `::select'.
    // This process is iterative because new conditions may arrive that
    // require us to re-scan the channels before we enter `::select'.
    
    min_latest_wakeup = KDU_LONG_MAX; // Catch any changes while scanning here
    if (!(new_state & KDCS_MONITOR_STATE_CLOSE))
      for (scan=channel_refs; scan != NULL; scan=scan->next)
        { 
          assert(!scan->in_service);
          assert(scan->socket != NULL);
          int imm_flags = 0;
          if ((scan->earliest_wakeup >= 0) &&
              (scan->earliest_wakeup <= current_time))
            { // Need immediate `service_channel'
              scan->earliest_wakeup = scan->latest_wakeup = -1;
              imm_flags |= KDCS_CONDITION_WAKEUP;
            }
          else if ((scan->latest_wakeup >= 0) &&
                   (scan->latest_wakeup < target_wake_time))
            target_wake_time = scan->latest_wakeup;
          scan->active_conditions |= scan->queued_conditions.exchange(0);
          if (scan->active_conditions & KDCS_CONDITION_IMMEDIATE)
            imm_flags |= (scan->active_conditions &
                          (KDCS_CONDITION_IMMEDIATE |
                           KDCS_CONDITION_CONTINUOUS));
          scan->active_conditions &= ~(KDCS_CONDITION_IMMEDIATE |
                                       KDCS_CONDITION_CONTINUOUS);
          if (imm_flags != 0)
            { 
              if (scan->servicer != NULL)
                { 
                  scan->in_service = true;
                  mutex.unlock();
                  scan->servicer->service_channel(this,scan->channel,
                                                  imm_flags);
                  mutex.lock();
                  scan->in_service = false;
                  scan->active_conditions |= KDCS_CONDITION_CONTINUOUS;
                }
            }
          
          if (scan->active_conditions & (KDCS_READ_CONDITIONS |
                                         KDCS_WRITE_CONDITIONS |
                                         KDCS_ERROR_CONDITIONS))
            { // These must be network conditions to be waited upon
              if (!scan->is_active)
                { // We may have been put on the list in an earlier iteration
                  scan->active_next = active_refs;  active_refs = scan;
                  scan->is_active = true;
                }
              if (scan->active_conditions & KDCS_READ_CONDITIONS)
                active_fd_sets->add_read(scan->socket);
              if (scan->active_conditions & KDCS_WRITE_CONDITIONS)
                active_fd_sets->add_write(scan->socket);
              if (scan->active_conditions & KDCS_ERROR_CONDITIONS)
                active_fd_sets->add_error(scan->socket);
#ifdef KDCS_BSD_SOCKETS
              if (scan->socket->sock >= nfds)
                nfds = scan->socket->sock + 1;
#endif // KDCS_BSD_SOCKETS
            }
        }
    
    // Evaluate `max_wait_microseconds' to determine whether we will be polling
    if (active_refs == NULL)
      max_wait_microseconds = new_condition_wait_microseconds;
    else
      max_wait_microseconds = wait_microseconds;
    
    // Let's see if anything has changed; if not, and neither WAKEUP nor CLOSE
    // are requested, we will enter the WAITING state atomically.
    kdu_int32 wait_mask = KDCS_MONITOR_STATE_WAITING;
    if ((min_latest_wakeup < target_wake_time) || (max_wait_microseconds <= 0))
      wait_mask = 0; // Cannot enter waiting state yet, but need to reset the
                     // `NEW_CONDITIONS' and `WAKEUP' flags.
    do { // enter compare-and-set loop
      old_state = run_state.get();
      new_state = old_state & ~(KDCS_MONITOR_STATE_NEW_CONDITIONS |
                                KDCS_MONITOR_STATE_WAKEUP);
      if (!(old_state & (KDCS_MONITOR_STATE_NEW_CONDITIONS |
                         KDCS_MONITOR_STATE_WAKEUP |
                         KDCS_MONITOR_STATE_CLOSE)))
        new_state |= wait_mask;
    } while (!run_state.compare_and_set(old_state,new_state));
  } while (!((new_state & KDCS_MONITOR_STATE_CLOSE) ||
             ((new_state ^ old_state) & (KDCS_MONITOR_STATE_WAITING |
                                         KDCS_MONITOR_STATE_WAKEUP)) ||
             (max_wait_microseconds <= 0)));

  if (new_state & KDCS_MONITOR_STATE_CLOSE)
    { 
      // Put all channels on the `active_refs' list so we can safely
      // traverse the list in the face of possible intervening calls
      // to `remove_channel'
      active_refs = NULL;
      for (scan=channel_refs; scan != NULL; scan=scan->next)
        { 
          scan->active_next = active_refs;
          active_refs = scan;
          scan->is_active = true;
        }
      
      // Now traverse the list of nodes, removing them from the active list
      // one-by-one and invoking their service function.
      while ((scan=active_refs) != NULL)
        { 
          active_refs = scan->active_next;
          if ((scan->socket != NULL) && (scan->servicer != NULL))
            { 
              scan->in_service = true;
              mutex.unlock();
              scan->servicer->service_channel(this,scan->channel,
                                              KDCS_CONDITION_MONITOR_CLOSING);
              mutex.lock();
              scan->in_service = false;
            }
          kdcs_channel_servicer *new_svc, *old_svc;
          while (((old_svc = scan->servicer) != NULL) &&
                 ((new_svc = old_svc->get_next_replacement()) != NULL))
            { 
              scan->servicer = new_svc;
              mutex.unlock();
              old_svc->release_ref();
              mutex.lock();
            }
          scan->is_active = false;
          if (scan->socket == NULL)
            { 
              mutex.unlock();
              if (scan->servicer != NULL)
                scan->servicer->release_ref();
              delete scan;
              mutex.lock();
            }
        }
      mutex.unlock();
      return false;
    }

  bool found_condition = false;
  if (new_state & KDCS_MONITOR_STATE_WAITING)
    { // Wait within `::select'
      assert(max_wait_microseconds > 0);
      int delay_microseconds = 1000000; // Block for at most 1 second
      if (delay_microseconds > max_wait_microseconds)
        delay_microseconds = max_wait_microseconds;
      if (target_wake_time < (current_time + delay_microseconds))
        { 
          if (target_wake_time < (current_time+10))
            delay_microseconds = 10; // Make sure we at least wait a little bit
          else
            delay_microseconds = (int)(target_wake_time - current_time);
        }
      this->select_wake_time = current_time + delay_microseconds;
      found_condition =
        interruptor->do_select(nfds, active_fd_sets, delay_microseconds,
                               mutex);
      this->select_wake_time = -1;
      do { // enter compare-and-set loop to reset WAITING and INTERRUPTED flags
        old_state = run_state.get();
        new_state = old_state & ~(KDCS_MONITOR_STATE_WAITING |
                                  KDCS_MONITOR_STATE_INTERRUPTED |
                                  KDCS_MONITOR_STATE_WAKEUP);
      } while (!run_state.compare_and_set(old_state,new_state));
    }
  else
    { // We either found the WAKEUP flag before getting a chance to call
      // `::select' or else `max_wait_microseconds' <= 0.
      found_condition =
        interruptor->do_poll(nfds,active_fd_sets,mutex);
    }
  
  // See what network conditions have occurred, emptying the `active_refs' list
  current_time = timer.get_ellapsed_microseconds();
  fd_set *read_set = active_fd_sets->active_reads;
  fd_set *write_set = active_fd_sets->active_writes;
  fd_set *error_set = active_fd_sets->active_errors;
  while ((scan=active_refs) != NULL)
    { 
      active_refs = scan->active_next;
      scan->active_conditions &= ~KDCS_CONDITION_CONTINUOUS;
      if (found_condition && (scan->socket != NULL))
        { 
          int flags = 0;
          if ((read_set != NULL) && FD_ISSET(scan->socket->sock,read_set))
            flags |= (scan->active_conditions & KDCS_READ_CONDITIONS);
          if ((write_set != NULL) && FD_ISSET(scan->socket->sock,write_set))
            flags |= (scan->active_conditions & KDCS_WRITE_CONDITIONS);
          if ((error_set != NULL) && FD_ISSET(scan->socket->sock,error_set))
            flags |= (scan->active_conditions & KDCS_ERROR_CONDITIONS)
                  |  KDCS_CONDITION_ERROR;
          if ((scan->earliest_wakeup >= 0) &&
              (scan->earliest_wakeup <= current_time))
            {
              scan->earliest_wakeup = scan->latest_wakeup = -1;
              flags |= KDCS_CONDITION_WAKEUP;
            }
          if (flags != 0)
            { 
              scan->active_conditions &= ~flags;
              if (scan->servicer != NULL)
                { 
                  scan->in_service = true;
                  mutex.unlock();
                  scan->servicer->service_channel(this,scan->channel,flags);
                  mutex.lock();
                  scan->in_service = false;
                  scan->active_conditions |= KDCS_CONDITION_CONTINUOUS;
                }
            }
        }
      
      kdcs_channel_servicer *new_svc, *old_svc;
      while (((old_svc = scan->servicer) != NULL) &&
             ((new_svc = old_svc->get_next_replacement()) != NULL))
        { 
          scan->servicer = new_svc;
          mutex.unlock();
          old_svc->release_ref();
          mutex.lock();
        }
      scan->is_active = false;
      if (scan->socket == NULL)
        { 
          mutex.unlock();
          if (scan->servicer != NULL)
            scan->servicer->release_ref();
          delete scan;
          mutex.lock();
        }
    }

  mutex.unlock();
  return true;
}

/*****************************************************************************/
/*                      kdcs_channel_monitor::run_clean                      */
/*****************************************************************************/

void kdcs_channel_monitor::run_clean()
{
  mutex.lock();
  interruptor->clean_thread_info();
  mutex.unlock();
}


/*===========================================================================*/
/*                               kdcs_sockaddr                               */
/*===========================================================================*/

/*****************************************************************************/
/*                       kdcs_sockaddr::~kdcs_sockaddr                       */
/*****************************************************************************/

kdcs_sockaddr::~kdcs_sockaddr()
{
  reset();
  if (string_buf != NULL)
    delete[] string_buf;
}

/*****************************************************************************/
/*                            kdcs_sockaddr::reset                           */
/*****************************************************************************/

void kdcs_sockaddr::reset()
{
  num_addresses=0;
  if (addresses != NULL)
    delete[] addresses;
  if (address_lengths != NULL)
    delete[] address_lengths;
  if (address_families != NULL)
    delete[] address_families;
  if (addr_handle != NULL)
    delete[] addr_handle;
  addresses = NULL; address_lengths=NULL; address_families=NULL;
  addr_handle=NULL; active_address=-1; port_valid=false;
  max_address_length = 0;
}

/*****************************************************************************/
/*                            kdcs_sockaddr::equals                          */
/*****************************************************************************/

bool kdcs_sockaddr::equals(const kdcs_sockaddr &rhs) const
{
  if ((!is_valid()) || (num_addresses != rhs.num_addresses))
    return false;
  for (int n=0; n < num_addresses; n++)
    if ((address_families[n] != rhs.address_families[n]) ||
        (address_lengths[n] != rhs.address_lengths[n]) ||
        (memcmp(addresses[n],rhs.addresses[n],address_lengths[n]) != 0))
      return false;
  return true;
}

/*****************************************************************************/
/*                             kdcs_sockaddr::copy                           */
/*****************************************************************************/

void kdcs_sockaddr::copy(const kdcs_sockaddr &rhs)
{
  if (rhs.num_addresses == 0)
    { 
      reset();
      return;
    }
  set_num_addresses(rhs.num_addresses); // Set up the memory
  for (int n=0; n < num_addresses; n++)
    { 
      address_lengths[n] = rhs.address_lengths[n];
      address_families[n] = rhs.address_families[n];
      memcpy(addresses[n],rhs.addresses[n],address_lengths[n]);
    }
  this->port_valid = rhs.port_valid;
  this->active_address = rhs.active_address;
}

/*****************************************************************************/
/* STATIC               kdcs_sockaddr::test_ip_literal                       */
/*****************************************************************************/

bool kdcs_sockaddr::test_ip_literal(const char *name, int flags)
{
  network_services.start();
  struct addrinfo hints, *scan, *addr_list=NULL;
  memset(&hints,0,sizeof(hints));
  if (flags & KDCS_ADDR_FLAG_IPV4_ONLY)
    { 
      if (flags & KDCS_ADDR_FLAG_IPV6_ONLY)
        return false; // Mutually incompatible flags
      hints.ai_family = AF_INET;
    }
  else if (flags & KDCS_ADDR_FLAG_IPV6_ONLY)
    hints.ai_family = AF_INET6;
  else
    hints.ai_family = PF_UNSPEC;
  hints.ai_flags = AI_NUMERICHOST;
  if (::getaddrinfo(name,NULL,&hints,&addr_list) != 0)
    return false;
  for (scan=addr_list; scan != NULL; scan=scan->ai_next)
    if ((scan->ai_family==AF_INET) || (scan->ai_family==AF_INET6))
      break;
  ::freeaddrinfo(addr_list);
  return (scan != NULL);
}

/*****************************************************************************/
/*                       kdcs_sockaddr::init (resolve)                       */
/*****************************************************************************/

bool kdcs_sockaddr::init(const char *name, int flags)
{
  reset();
  char local_hostname[KDCS_HOSTNAME_MAX+2]; // 2 more just in case of unicode
  if (name == NULL)
    { 
      memset(local_hostname,0,KDCS_HOSTNAME_MAX+2);
      if (::gethostname(local_hostname,KDCS_HOSTNAME_MAX) == 0)
        name = local_hostname;
    }

  struct addrinfo hints, *scan, *addr_list=NULL;
  memset(&hints,0,sizeof(hints));
  if (flags & KDCS_ADDR_FLAG_IPV4_ONLY)
    { 
      if (flags & KDCS_ADDR_FLAG_IPV6_ONLY)
        return false; // Mutually incompatible flags
      hints.ai_family = AF_INET;
      if (name == NULL)
        name = "127.0.0.1";
    }
  else if (flags & KDCS_ADDR_FLAG_IPV6_ONLY)
    { 
      hints.ai_family = AF_INET6;
      if (name == NULL)
        name = "::1";
    }
  else
    { 
      hints.ai_family = PF_UNSPEC;
      if (name == NULL)
        name = "127.0.0.1";
    }
  if (flags & KDCS_ADDR_FLAG_LITERAL_ONLY)
    hints.ai_flags = AI_NUMERICHOST;
  if ((flags & KDCS_ADDR_FLAG_BRACKETED_LITERALS) &&
      ((name[0] == '[') && (name[strlen(name)-1] == ']')))
    { // Remove the brackets and interpret as a literal IP address
      hints.ai_flags = AI_NUMERICHOST;
      size_string_buf(strlen(name));
      strcpy(string_buf,name+1);
      string_buf[strlen(string_buf)-1] = '\0';
      name = string_buf;
    }
  else if ((flags & KDCS_ADDR_FLAG_ESCAPED_NAMES) &&
           (strchr(name,'%') != NULL))
    { // Need to hex-hex decode the name
      size_string_buf(strlen(name)+1);
      strcpy(string_buf,name);
      name = kdu_hex_hex_decode(string_buf);
    }
   
  if (::getaddrinfo(name,NULL,&hints,&addr_list) != 0)
    { // Failed, but let's just see if this is because local host has no name
      if (name != local_hostname)
        return false;
      if (flags & KDCS_ADDR_FLAG_IPV6_ONLY)
        name = "::1";
      else
        name = "127.0.0.1";
      if (::getaddrinfo(name,NULL,&hints,&addr_list) != 0)
        return false;
    }
  int n=0;
  for (scan=addr_list; scan != NULL; scan=scan->ai_next)
    if ((scan->ai_family == AF_INET) || (scan->ai_family == AF_INET6))
      n++;
  if (n > 0)
    { 
      set_num_addresses(n);
      active_address = 0;
      port_valid = ((flags & KDCS_ADDR_FLAG_NEED_PORT) == 0);
    }
  for (n=0, scan=addr_list; scan != NULL; scan=scan->ai_next)
    if ((scan->ai_family == AF_INET) || (scan->ai_family == AF_INET6))
      { 
        address_lengths[n] = (size_t) scan->ai_addrlen;
        address_families[n] = (int) scan->ai_family;
        memcpy(addresses[n],scan->ai_addr,address_lengths[n]);
        n++;
      }
  ::freeaddrinfo(addr_list);
  return (num_addresses > 0);
}

/*****************************************************************************/
/*                   kdcs_sockaddr::init (single address)                    */
/*****************************************************************************/

bool kdcs_sockaddr::init(const sockaddr *addr, size_t len, int family)
{
  set_num_addresses(1);
  if ((len == 0) || (len > max_address_length))
    { reset(); return false; }
  address_lengths[0] = len;
  address_families[0] = family;
  memcpy(addresses[0],addr,len);
  active_address = 0;
  port_valid = true;
  return true; 
}
  
/*****************************************************************************/
/*                          kdcs_sockaddr::set_port                          */
/*****************************************************************************/

bool kdcs_sockaddr::set_port(kdu_uint16 num)
{ 
  if (num_addresses == 0)
    return false;
  for (int n=0; n < num_addresses; n++)
    if (address_families[n] == (int) AF_INET)
      ((sockaddr_in *) addresses[n])->sin_port = htons(num);
    else if (address_families[n] == (int) AF_INET6)
      ((sockaddr_in6 *) addresses[n])->sin6_port = htons(num);
  port_valid = true;
  return true;
}

/*****************************************************************************/
/*                          kdcs_sockaddr::get_port                          */
/*****************************************************************************/

kdu_uint16 kdcs_sockaddr::get_port()
{ 
  if ((active_address < 0) || (active_address >= num_addresses))
    return 0;
  int family = address_families[active_address];
  sockaddr *addr = addresses[active_address];
  if (family == (int) AF_INET)
    return (kdu_uint16)  ntohs(((sockaddr_in *) addr)->sin_port);
  else if (family == (int) AF_INET6)
    return (kdu_uint16) ntohs(((sockaddr_in6 *) addr)->sin6_port);
  else
    return 0;
}

/*****************************************************************************/
/*                         kdcs_sockaddr::textualize                         */
/*****************************************************************************/

const char *kdcs_sockaddr::textualize(int my_flags)
{
  if ((active_address < 0) || (active_address >= num_addresses))
    return NULL;
  size_string_buf(NI_MAXHOST+3);
  sockaddr *addr = addresses[active_address];
  socklen_t addr_len = (socklen_t) address_lengths[active_address];
  char *host = string_buf;
  size_t hostlen = string_buf_len-1;
  int nminfo_flags = NI_NAMEREQD;
  if (my_flags & KDCS_ADDR_FLAG_LITERAL_ONLY)
    nminfo_flags = NI_NUMERICHOST;
  if (my_flags & KDCS_ADDR_FLAG_BRACKETED_LITERALS)
    { // Make sure we can always add the brackets in if required
      host++; hostlen--;
    }
  if (::getnameinfo(addr,addr_len,host,(unsigned int) hostlen,
                    NULL,0,nminfo_flags) != 0)
    { // First attempt failed, but perhaps we need to settle for an IP literal
      if (nminfo_flags & NI_NUMERICHOST)
        return NULL; // Already asked for an IP literal, but no luck.
      nminfo_flags = (nminfo_flags & ~NI_NAMEREQD) | NI_NUMERICHOST;
      if (::getnameinfo(addr,addr_len,host,(unsigned int) hostlen,
                        NULL,0,nminfo_flags) != 0)
        return NULL;
    }
  if (nminfo_flags & NI_NUMERICHOST)
    { // We must have recovered an IP literal address (IPv4 or IPv6)
      if (my_flags & KDCS_ADDR_FLAG_BRACKETED_LITERALS)
        { (--host)[0] = '['; strcat(host,"]"); }
    }
  else
    { // We must have recovered an actual hostname
      if (my_flags & KDCS_ADDR_FLAG_ESCAPED_NAMES)
        { // Perform hex-hex encoding; need a second string buffer for this
          char *old_string_buf = string_buf;
          string_buf = NULL; string_buf_len = 0;
          int enc_len = kdu_hex_hex_encode(host,NULL,NULL,"[]:");
          size_string_buf((size_t)(enc_len+1));
          kdu_hex_hex_encode(host,string_buf,NULL,"[]:");
          host = string_buf;
          delete[] old_string_buf;
        }
    }
  return host;
}
                                              
/*****************************************************************************/
/*                      kdcs_sockaddr::set_num_addresses                     */
/*****************************************************************************/

void kdcs_sockaddr::set_num_addresses(int num)
{ 
  if (num == num_addresses)
    return;
  reset();
  if (num < 1)
    return;
  num_addresses = num;
  address_families = new int[num];
  address_lengths = new size_t[num];
  addresses = new sockaddr *[num];
#ifdef KDCS_BSD_SOCKETS
  sockaddr_storage *storage = new sockaddr_storage[num];
  max_address_length = sizeof(sockaddr_storage);
#else // Windows
  SOCKADDR_STORAGE *storage = new SOCKADDR_STORAGE[num];
  max_address_length = sizeof(SOCKADDR_STORAGE);
#endif // Windows
  addr_handle = (sockaddr *) storage;
  for (int n=0; n < num; n++)
    { 
      addresses[n] = (sockaddr *)(storage + n);
      address_lengths[n] = 0;
      address_families[n] = 0;
    }
}

/*****************************************************************************/
/*                        kdcs_sockaddr::size_string_buf                     */
/*****************************************************************************/

void kdcs_sockaddr::size_string_buf(size_t min_len)
{
  if (string_buf_len < min_len)
    { 
      if (string_buf != NULL)
        delete[] string_buf;
      string_buf = NULL;
      string_buf_len = min_len;
      string_buf = new char[string_buf_len];
    }
}

/*****************************************************************************/
/*                         kdcs_sockaddr::test_ipv6                          */
/*****************************************************************************/

bool kdcs_sockaddr::test_ipv6() const
{
  return (get_addr_family() == AF_INET6);
}


/*===========================================================================*/
/*                                kdcs_channel                               */
/*===========================================================================*/

/*****************************************************************************/
/*                          kdcs_channel::kdcs_channel                       */
/*****************************************************************************/

kdcs_channel::kdcs_channel(kdcs_channel_monitor *monitor,
                           bool have_separate_monitor_thread)
{
  kdcs_start_network();
  this->monitor = monitor;
  socket = NULL; socket_connected = false;
  channel_ref = NULL; servicer = NULL; internal_servicer = NULL;
  suppress_errors = false; error_exception = KDCS_CLOSED_EXCEPTION;
  this->have_separate_monitor_thread = have_separate_monitor_thread;
  this->start_time = -1;
  this->blocking_lifespan = -1;
  this->lifespan_expired = false;        
}

/*****************************************************************************/
/*                       kdcs_channel::get_local_address                     */
/*****************************************************************************/

bool kdcs_channel::get_local_address(kdcs_sockaddr &local_addr)
{
  if (!is_active())
    return false;
#ifdef KDCS_BSD_SOCKETS
  sockaddr_storage addr_buf;
  socklen_t addr_len = sizeof(addr_buf);
#else // Must be Windows
  SOCKADDR_STORAGE addr_buf;
  int addr_len = (int) sizeof(addr_buf);
#endif // !KDCS_BSD_SOCKETS
  if (::getsockname(socket->sock,(sockaddr *) &addr_buf,&addr_len) < 0)
    return false;
  local_addr.init((sockaddr *) &addr_buf,(size_t) addr_len,
                  (int) addr_buf.ss_family);
  return true;
}

/*****************************************************************************/
/*                       kdcs_channel::get_peer_address                      */
/*****************************************************************************/

bool kdcs_channel::get_peer_address(kdcs_sockaddr &local_addr)
{
  if (!(socket_connected && is_active()))
    return false;
#ifdef KDCS_BSD_SOCKETS
  sockaddr_storage addr_buf;
  socklen_t addr_len = sizeof(addr_buf);
#else // Must be Windows
  SOCKADDR_STORAGE addr_buf;
  int addr_len = (int) sizeof(addr_buf);
#endif // !KDCS_BSD_SOCKETS
  if (::getpeername(socket->sock,(sockaddr *) &addr_buf,&addr_len) < 0)
    return false;
  local_addr.init((sockaddr *) &addr_buf,(size_t) addr_len,
                  (int) addr_buf.ss_family);
  return true;
}

/*****************************************************************************/
/*                     kdcs_channel::set_channel_servicer                    */
/*****************************************************************************/

void kdcs_channel::set_channel_servicer(kdcs_channel_servicer *servicer)
{
  assert(monitor != NULL);
  if (!is_active())
    { kdu_error e; e << "Attempting to change the channel servicer object "
      "associated with a channel which is not currently active.  You need "
      "to have an open socket to register the channel and an associated "
      "channel servicer with the `kdcs_channel_monitor' object."; }
  if (servicer != NULL)
    { 
      if (internal_servicer != NULL)
        { 
          internal_servicer->release();
          internal_servicer = NULL;
        }
    }
  else
    { 
      if (internal_servicer == NULL)
        internal_servicer =
          new kdcs_private_servicer((have_separate_monitor_thread)?
                                    NULL:monitor);
      servicer = internal_servicer;
    }
  if (servicer == this->servicer)
    return;
  channel_ref = monitor->add_channel(this,servicer);
  if (channel_ref == NULL)
    { 
      close();
      if (suppress_errors)
        throw error_exception;
      else
        { kdu_error e; e << "Too many channels being monitored at once."; }
    }
  this->servicer = servicer;
  if ((start_time >= 0) && (blocking_lifespan >= 0) &&
      (channel_ref != NULL) && (internal_servicer != NULL) &&
      !lifespan_expired)
    monitor->schedule_wakeup(channel_ref,start_time+blocking_lifespan,
                             start_time+blocking_lifespan+10000);
      // Happy to have our lifespan extended by 10ms (10000 microseconds)
}

/*****************************************************************************/
/*                   kdcs_channel::set_blocking_lifespan                     */
/*****************************************************************************/

void kdcs_channel::set_blocking_lifespan(float seconds)
{
  lifespan_expired = false;
  blocking_lifespan = (kdu_long) ceil(seconds * 1000000.0); // In microseconds
  if (start_time >= 0)
    monitor->schedule_wakeup(channel_ref,start_time+blocking_lifespan,
                             start_time+blocking_lifespan+10000);
  // Happy to have our lifespan extended by 10ms (10000 microseconds)
}

/*****************************************************************************/
/*                      kdcs_channel::schedule_wakeup                        */
/*****************************************************************************/

bool kdcs_channel::schedule_wakeup(kdu_long earliest, kdu_long latest)
{
  if ((internal_servicer == NULL) && (channel_ref != NULL))
    return monitor->schedule_wakeup(channel_ref,earliest,latest);
  return false;
}

/*****************************************************************************/
/*                           kdcs_channel::close                             */
/*****************************************************************************/

void kdcs_channel::close()
{
  if (internal_servicer != NULL)
    {
      internal_servicer->release();
      internal_servicer = NULL;
    }
  servicer = NULL;
  if (channel_ref != NULL)
    monitor->remove_channel(channel_ref);
  channel_ref = NULL;
  if (socket != NULL)
    { 
      socket->close();
      delete socket;
      socket = NULL;
    }
  socket_connected = false;
}


/*===========================================================================*/
/*                              kdcs_tcp_channel                             */
/*===========================================================================*/

/*****************************************************************************/
/*                     kdcs_tcp_channel::kdcs_tcp_channel                    */
/*****************************************************************************/

kdcs_tcp_channel::kdcs_tcp_channel(kdcs_channel_monitor *monitor,
                                   bool have_separate_monitor_thread)
                : kdcs_channel(monitor,have_separate_monitor_thread)
{
  tbuf_bytes = tbuf_used = 0;
  text = NULL;
  text_len = text_max = 0;
  raw_complete = text_complete = skip_white = false;
  raw = NULL;
  raw_len = raw_max = 0;
  block_len = 0;
  partial_bytes_sent = 0;
}

/*****************************************************************************/
/*                    kdcs_tcp_channel::~kdcs_tcp_channel                    */
/*****************************************************************************/

kdcs_tcp_channel::~kdcs_tcp_channel()
{
  close();
  if (raw != NULL)
    delete[] raw;
  if (text != NULL)
    delete[] text;
  raw = NULL;
  text = NULL;
}

/*****************************************************************************/
/*                          kdcs_tcp_channel::close                          */
/*****************************************************************************/

void kdcs_tcp_channel::close()
{
  start_time = -1; // So it can be set again in next call to `connect'/`listen'
  lifespan_expired = false;
  if (socket != NULL)
    socket->shutdown();
  kdcs_channel::close();
  connect_address.reset();
  listen_address.reset();
  connect_call_has_valid_args = false;
  tbuf_bytes = tbuf_used = 0;
  text_len = 0;
  raw_len = 0;
  text_complete = true; // So next text read will start from scratch
  raw_complete = true; // So next raw complete will start from scratch
  block_len = 0;
  partial_bytes_sent = 0;
}

/*****************************************************************************/
/*                        kdcs_tcp_channel::connect                          */
/*****************************************************************************/

bool kdcs_tcp_channel::connect(const kdcs_sockaddr &address,
                               kdcs_channel_servicer *servicer)
{
  if (!address.is_valid())
    { kdu_error e; e << "The `address' object supplied to "
      "`kdcs_tcp_channel::connect' indicates that it does not hold a valid "
      "address.  Be sure to call `kdcs_sockaddr::init' and, if required, "
      "`kdcs_sockaddr::set_port' before passing the address to this "
      "function."; }
  if ((!connect_address.is_valid()) || (connect_address != address))
    { 
      close();
      connect_address = address;
      connect_address.first();
    }
  if (socket_connected)
    return true;
  if (socket == NULL)
    socket = new kdcs_socket;
  if (start_time < 0)
    start_time = monitor->get_current_time();

  while (1)
    {
      if (!socket->is_valid())
        { // Need to create the socket itself
          socket->sock = ::socket(connect_address.get_addr_family(),
                                  SOCK_STREAM,0);
          if (!socket->is_valid())
            { // Try advancing to a new address; might have different family
              if (connect_address.next())
                continue;
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e << "Unable to create new socket; system "
                  "resource limit may have been reached, for example!"; }
            }
          if (!socket->make_nonblocking())
            { 
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e <<
                  "Cannot put socket into non-blocking mode -- weird!!"; }
            }
          socket->disable_nagel();
          set_channel_servicer(servicer); // Creates internal servicer if necessary
          if (channel_ref == NULL)
            { 
              close();
              return false; // Must be out of channel resources
            }
        }    
    
      if (::connect(socket->sock,connect_address.get_addr(),
                    (socklen_t) connect_address.get_addr_len()) == 0)
        return (socket_connected = true);
      else
        {
          int err = socket->get_last_error();
          if (socket->check_error_connected(err))
            return (socket_connected = true); // Already connected
          else if (socket->check_error_invalidargs(err) &&
                   connect_call_has_valid_args)
            { // This is a concession to Winsock2 weirdness.  If we `select'
              // for a `connect' which would block, without waiting at all,
              // it seems that the `select' call returns immediate,
              // encouraging us to retry the `connect' call which then
              // generates an EINVAL error code (invalid arguments).  This
              // makes no sense and only seems to happen on Windows platorms,
              // but goes away if we do a little sleeping.
              kdcs_microsleep(1000);
            }
          else if (socket->check_error_wouldblock(err))
            connect_call_has_valid_args = true; // Args check out as valid
          else
            { // Attempt to use this version of the address has failed
              if (connect_address.next())
                { 
                  socket->close(); // In case address family changed
                  continue;
                }
              close();
              break;
            }
        }
      if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_CONNECT))
        {
          close(); // Monitor closing
          throw KDCS_CLOSING_EXCEPTION;
        }
      if (internal_servicer == NULL)
        break;
      if (lifespan_expired || !internal_servicer->wait_for_service())
        { // Lifespan has expired
          close();
          throw KDCS_LIFESPAN_EXCEPTION;
        }
    }
  return false;
}

/*****************************************************************************/
/*                        kdcs_tcp_channel::listen                          */
/*****************************************************************************/

bool kdcs_tcp_channel::listen(const kdcs_sockaddr &address,
                              int backlog_limit,
                              kdcs_channel_servicer *servicer)
{
  if (backlog_limit < 1)
    backlog_limit = 1;
  close(); // Just in case
  if (!address.is_valid())
    { kdu_error e; e << "The `address' object supplied to "
      "`kdcs_tcp_channel::listen' indicates that it does not hold a valid "
      "address.  Be sure to call `kdcs_sockaddr_in::set_valid' before "
      "passing the address to this function."; }
  socket = new kdcs_socket;
  listen_address = address;
  listen_address.first();
  while (1) {
    socket->sock = ::socket(listen_address.get_addr_family(),SOCK_STREAM,0);
    if (!socket->is_valid())
      { // Try advancing to a new address; might have different family
        if (listen_address.next())
          continue;
        close();
        if (suppress_errors)
          throw error_exception;
        else
          { kdu_error e;
            e << "Unable to create new socket for listening.";
          }
      }
    if (!socket->make_nonblocking())
      { 
        close();
        if (suppress_errors)
          throw error_exception;
        else
          { kdu_error e; e <<
            "Cannot put socket into non-blocking mode -- weird!!"; }
      }
    socket->disable_nagel();
    socket->reuse_address();
    if ((::bind(socket->sock,listen_address.get_addr(),
                (socklen_t) listen_address.get_addr_len()) == 0) &&
        (::listen(socket->sock,backlog_limit) == 0))
      break;
    socket->close();
    if (listen_address.next())
      continue;
    close();
    return false;
  }

  start_time = monitor->get_current_time();
  set_channel_servicer(servicer);
  if (channel_ref == NULL)
    { 
      close();
      return false; // Must be out of channel resources
    }
  return true;
}

/*****************************************************************************/
/*                        kdcs_tcp_channel::accept                           */
/*****************************************************************************/

kdcs_tcp_channel *
  kdcs_tcp_channel::accept(kdcs_channel_monitor *target_monitor,
                           kdcs_channel_servicer *target_servicer,
                           bool have_separate_target_monitor_thread)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  while (1)
    { 
      if (channel_ref == NULL)
        throw KDCS_CLOSED_EXCEPTION;
#ifdef KDCS_BSD_SOCKETS
      sockaddr_storage addr;
      socklen_t addr_len = sizeof(addr);
#else // Must be Windows
      SOCKADDR_STORAGE addr;
      int addr_len = (int) sizeof(addr);
#endif // !KDCS_BSD_SOCKETS
      kdcs_socket target_socket;
      target_socket.sock =
        ::accept(socket->sock,(sockaddr *) &addr,&addr_len);
      int err_val = socket->get_last_error();
      if (target_socket.is_valid())
        { 
          if (!target_socket.make_nonblocking())
            { 
              target_socket.shutdown();
              target_socket.close();
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e << "Unable to set newly accepted connection "
                  "socket into non-blocking mode!"; }
            }
          target_socket.disable_nagel();
          target_socket.reuse_address();

          kdcs_tcp_channel *result =
            new kdcs_tcp_channel(target_monitor,
                                 have_separate_target_monitor_thread);
          result->socket = new kdcs_socket(target_socket);
          result->connect_address.init((sockaddr *) &addr,(size_t) addr_len,
                                       (int) addr.ss_family);
          result->start_time = target_monitor->get_current_time();
          result->set_channel_servicer(target_servicer);
          if (result->channel_ref == NULL)
            { // Must be out of channel resources
              result->close();
              delete result;
              return NULL;
            }
          result->socket_connected = true;
          return result;
        }
      if (!socket->check_error_wouldblock(err_val))
        {
          close();
          if (suppress_errors)
            throw error_exception;
          else
            { kdu_error e; e << "Attempt to accept incoming TCP socket "
              "connection failed unexpectedly!  Perhaps the system is low "
              "on resources."; }
        }
      if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_ACCEPT))
        { // Monitor is closing
          close();
          throw KDCS_CLOSING_EXCEPTION;
        }
      if (internal_servicer == NULL)
        break;
      if (lifespan_expired || !internal_servicer->wait_for_service())
        {
          lifespan_expired = true;
          throw KDCS_LIFESPAN_EXCEPTION;
        }
    }
  return NULL;
}

/*****************************************************************************/
/*                       kdcs_tcp_channel::read_line                         */
/*****************************************************************************/

const char *kdcs_tcp_channel::read_line(bool accumulate, char delim)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION; // Channel already closed
  assert(block_len == 0); // Otherwise didn't complete a previous block read
  if (text_complete && !accumulate)
    text_len = 0;
  text_complete = false;
  line_start = true;
  skip_white = true; // Skip over leading white space
  while (!text_complete)
    {
      // Start by using whatever data is available in `tbuf'.
      while ((tbuf_used < tbuf_bytes) && !text_complete)
        {
          if (text_len == text_max)
            { // Grow the text buffer
              int new_text_max = 2*text_max + 10;
              char *new_text = new char[new_text_max+1];
              if (text != NULL)
                {
                  memcpy(new_text,text,(size_t) text_len);
                  delete[] text;
                }
              text = new_text;
              text_max = new_text_max;
            }
          char ch = tbuf[tbuf_used++];
          if ((ch == '\0') || (ch == delim))
            {
              if (skip_white && !line_start)
                { // Back over last white space character
                  assert(text_len > 0);
                  text_len--;
                }
              text[text_len++] = ch;
              text_complete = true;
            }
          else if ((ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n'))
            {
              if (!skip_white)
                {
                  if (ch == '\n')
                    text[text_len++] = ch;
                  else
                    text[text_len++] = ' ';
                }
              skip_white = true;
            }
          else
            {
              line_start = skip_white = false;
              text[text_len++] = ch;
            }
        }
      
      if (!text_complete)
        { // Load more data from the TCP channel
          assert(tbuf_used == tbuf_bytes);
          tbuf_used = 0;
          tbuf_bytes = (int)::recv(socket->sock,(char *) tbuf,256,0);
          if (tbuf_bytes == 0)
            { // Socket has been closed gracefully from the other end
              close();
              throw KDCS_CLOSED_EXCEPTION;
            }
          if (tbuf_bytes < 0)
            { // Socket error or socket would block
              tbuf_bytes = 0;
              int err_val = socket->get_last_error();
              if (!socket->check_error_wouldblock(err_val))
                { // Socket error; maybe non-graceful disconnect from other end
                  close();
                  throw KDCS_CLOSED_EXCEPTION;
                }
              if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_READ))
                { // Monitor closing
                  close();
                  throw KDCS_CLOSING_EXCEPTION;
                }
              if (internal_servicer == NULL)
                { 
                  return NULL; // Non-blocking call; come again later
                }
              if (lifespan_expired || !internal_servicer->wait_for_service())
                {
                  lifespan_expired = true;
                  throw KDCS_LIFESPAN_EXCEPTION;
                }
            }
        }
    }
  assert(text_complete);
  text[text_len] = '\0'; // Allocation always leaves enough room for terminator
  return text;
}

/*****************************************************************************/
/*                    kdcs_tcp_channel::read_paragraph                       */
/*****************************************************************************/

const char *kdcs_tcp_channel::read_paragraph(char delim)
{
  if (text_complete)
    text_len = 0;
  text_complete = false;
  do {
    if (read_line(true,delim) == NULL)
      return NULL;
  } while ((text_len >= 2) && (text[text_len-1] != '\0') &&
           (text[text_len-2] != delim));
  return text;
}

/*****************************************************************************/
/*                       kdcs_tcp_channel::read_raw                          */
/*****************************************************************************/

kdu_byte *kdcs_tcp_channel::read_raw(int num_bytes)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  assert(block_len == 0); // Otherwise didn't complete a previous block read
  if (raw_complete)
    raw_len = 0;
  raw_complete = false;
  if ((raw_max < num_bytes) || (raw == NULL))
    {
      int new_raw_max = (num_bytes > 0)?num_bytes:1;
      kdu_byte *new_raw = new kdu_byte[new_raw_max];
      if (raw != NULL)
        delete[] raw;
      raw = new_raw;
      raw_max = new_raw_max;
    }
  
  while (raw_len < num_bytes)
    {
      // Start by using whatever data is available in `tbuf'.
      int xfer_bytes = tbuf_bytes - tbuf_used;
      if (xfer_bytes > (num_bytes-raw_len))
        xfer_bytes = num_bytes - raw_len;
      if (xfer_bytes > 0)
        {
          memcpy(raw+raw_len,tbuf+tbuf_used,(size_t) xfer_bytes);
          tbuf_used += xfer_bytes;
          raw_len += xfer_bytes;
        }      
      if (raw_len < num_bytes)
        { // Load more data from the TCP channel
          assert(tbuf_used == tbuf_bytes);
          tbuf_used = 0;
          tbuf_bytes = (int)::recv(socket->sock,(char *) tbuf,256,0);
          if (tbuf_bytes == 0)
            { // Socket has been closed gracefully from the other end
              close();
              throw KDCS_CLOSED_EXCEPTION;
            }
          if (tbuf_bytes < 0)
            { // Socket error or socket would block
              tbuf_bytes = 0;
              int err_val = socket->get_last_error();
              if (!socket->check_error_wouldblock(err_val))
                { // Socket error; maybe non-graceful disconnect from other end
                  close();
                  throw KDCS_CLOSED_EXCEPTION;
                }
              if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_READ))
                { // Monitor closing
                  close();
                  throw KDCS_CLOSING_EXCEPTION;
                }
              if (internal_servicer == NULL)
                return NULL; // Non-blocking I/O; try again later
              if (lifespan_expired || !internal_servicer->wait_for_service())
                {
                  lifespan_expired = true;
                  throw KDCS_LIFESPAN_EXCEPTION;
                }
            }
        }
    }
  raw_complete = true;
  return raw;
}
  
/*****************************************************************************/
/*                      kdcs_tcp_channel::read_block                         */
/*****************************************************************************/

bool kdcs_tcp_channel::read_block(int num_bytes, kdcs_message_block &block)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  while (block_len < num_bytes)
    {
      // Start by using whatever data is available in `tbuf'.
      int xfer_bytes = tbuf_bytes - tbuf_used;
      if (xfer_bytes > (num_bytes-block_len))
        xfer_bytes = num_bytes - block_len;
      if (xfer_bytes > 0)
        {
          block.write_raw(tbuf+tbuf_used,xfer_bytes);
          tbuf_used += xfer_bytes;
          block_len += xfer_bytes;
        }
      if (block_len < num_bytes)
        { // Load more data from the TCP channel
          assert(tbuf_used == tbuf_bytes);
          tbuf_used = 0;
          tbuf_bytes = (int)::recv(socket->sock,(char *) tbuf,256,0);
          if (tbuf_bytes == 0)
            { // Socket has been closed gracefully from the other end
              close();
              throw KDCS_CLOSED_EXCEPTION;
            }
          if (tbuf_bytes < 0)
            { // Socket error or socket would block
              tbuf_bytes = 0;
              int err_val = socket->get_last_error();
              if (!socket->check_error_wouldblock(err_val))
                { // Socket error; maybe non-graceful disconnect from other end
                  close();
                  throw KDCS_CLOSED_EXCEPTION;
                }
              if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_READ))
                { // Monitor closing
                  close();
                  throw KDCS_CLOSING_EXCEPTION;
                }
              if (internal_servicer == NULL)
                return false; // Non-blocking I/O; try again later
              if (lifespan_expired || !internal_servicer->wait_for_service())
                {
                  lifespan_expired = true;
                  throw KDCS_LIFESPAN_EXCEPTION;
                }
            }
        }
    }
  block_len = 0;
  return true;
}

/*****************************************************************************/
/*                       kdcs_tcp_channel::write_raw                         */
/*****************************************************************************/

bool kdcs_tcp_channel::write_raw(const kdu_byte *buf, int num_bytes)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  num_bytes -= partial_bytes_sent;
  buf += partial_bytes_sent;
  if (num_bytes <= 0)
    return true;
  while (num_bytes > 0)
    {
      int xfer_bytes = (int)::send(socket->sock,(char *) buf,num_bytes,0);
      if (xfer_bytes == 0)
        { // Connection closed gracefully from other end.
          close();
          throw KDCS_CLOSED_EXCEPTION;
        }
      else if (xfer_bytes < 0)
        { // Socket error or socket would block
          int err_val = socket->get_last_error();
          if (!socket->check_error_wouldblock(err_val))
            { // Socket error; maybe non-graceful disconnect from other end
              close();
              throw KDCS_CLOSED_EXCEPTION;
            }
          if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_WRITE))
            { // Monitor closing
              close();
              throw KDCS_CLOSING_EXCEPTION;
            }
          if (internal_servicer == NULL)
            return false; // Non-blocking I/O; try again later
          if (lifespan_expired || !internal_servicer->wait_for_service())
            {
              lifespan_expired = true;
              throw KDCS_LIFESPAN_EXCEPTION;
            }
        }
      else
        {
          assert(xfer_bytes > 0);
          num_bytes -= xfer_bytes;
          partial_bytes_sent += xfer_bytes;
          buf += xfer_bytes;
        }
    }
  partial_bytes_sent = 0;
  return true;
}


/*===========================================================================*/
/*                              kdcs_udp_channel                             */
/*===========================================================================*/

/*****************************************************************************/
/*                     kdcs_udp_channel::kdcs_udp_channel                    */
/*****************************************************************************/

kdcs_udp_channel::kdcs_udp_channel(kdcs_channel_monitor *monitor,
                                   bool have_separate_monitor_thread)
: kdcs_channel(monitor,have_separate_monitor_thread)
{
  buf = NULL;
  buf_size = 0;
}

/*****************************************************************************/
/*                    kdcs_udp_channel::~kdcs_udp_channel                    */
/*****************************************************************************/

kdcs_udp_channel::~kdcs_udp_channel()
{
  close();
  if (buf != NULL)
    delete[] buf;
  buf = NULL;
}

/*****************************************************************************/
/*                          kdcs_udp_channel::close                          */
/*****************************************************************************/

void kdcs_udp_channel::close()
{
  start_time = -1; // So it can be set again in next call to `connect'/`listen'
  lifespan_expired = false;
  kdcs_channel::close();
  connect_address.reset();
  bind_address.reset();
}

/*****************************************************************************/
/*                         kdcs_udp_channel::bind                            */
/*****************************************************************************/

bool kdcs_udp_channel::bind(const kdcs_sockaddr &address,
                            kdcs_channel_servicer *servicer)
{
  if (!address.is_valid())
    { kdu_error e; e << "The `local_address' object supplied to "
      "`kdcs_udp_channel::bind' indicates that it does not hold a valid "
      "address.  Be sure to call `kdcs_sockaddr::init' and, if required, "
      "`kdcs_sockaddr::set_port' before passing the address to this "
      "function."; }
  
  bind_address = address;
  bind_address.first();

  if (socket == NULL)
    socket = new kdcs_socket;
  if (start_time < 0)
    start_time = monitor->get_current_time();
  while (1)
    { 
      if (!socket->is_valid())
        { 
          socket->sock = ::socket(bind_address.get_addr_family(),SOCK_DGRAM,0);
          if (!socket->is_valid())
            { 
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e << "Unable to create new socket; system "
                  "resource limit may have been reached, for example!"; }
            }
          if (!socket->make_nonblocking())
            { 
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e <<
                  "Cannot put socket into non-blocking mode -- weird!!"; }
            }
          socket->disable_nagel();
        }
      set_channel_servicer(servicer); // Creates internal servicer if necessary
      if (channel_ref == NULL)
        { 
          close();
          return false; // Must be out of channel resources
        }
      if (::bind(socket->sock,bind_address.get_addr(),
                 (socklen_t) bind_address.get_addr_len()) == 0)
        return true;
      else
        { // Attempt to use this version of the address has failed
          if (bind_address.next() && !socket_connected)
            { 
              socket->close(); // In case address family changed
              continue;
            }
          break;
        }
    }

  bind_address.reset();
  if (!socket_connected)
    close();
  return false;
}

/*****************************************************************************/
/*                        kdcs_udp_channel::connect                          */
/*****************************************************************************/

bool kdcs_udp_channel::connect(const kdcs_sockaddr &address,
                               kdcs_channel_servicer *servicer)
{
  if (!address.is_valid())
    { kdu_error e; e << "The `peer_address' object supplied to "
      "`kdcs_udp_channel::connect' indicates that it does not hold a valid "
      "address.  Be sure to call `kdcs_sockaddr::init' and, if required, "
      "`kdcs_sockaddr::set_port' before passing the address to this "
      "function."; }
  
  connect_address = address;
  connect_address.first();
  socket_connected = false; // In case current call fails

  if (socket == NULL)
    socket = new kdcs_socket;
  if (start_time < 0)
    start_time = monitor->get_current_time();
  while (1)
    { 
      if (!socket->is_valid())
        { 
          socket->sock = ::socket(connect_address.get_addr_family(),
                                  SOCK_DGRAM,0);
          if (!socket->is_valid())
            { 
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e << "Unable to create new socket; system "
                  "resource limit may have been reached, for example!"; }
            }
          if (!socket->make_nonblocking())
            { 
              close();
              if (suppress_errors)
                throw error_exception;
              else
                { kdu_error e; e <<
                  "Cannot put socket into non-blocking mode -- weird!!"; }
            }
          socket->disable_nagel();
        }
      set_channel_servicer(servicer); // Creates internal servicer if necessary
      if (channel_ref == NULL)
        { 
          close();
          return false; // Must be out of channel resources
        }
      if (::connect(socket->sock,connect_address.get_addr(),
                    (socklen_t) connect_address.get_addr_len()) == 0)
        return (socket_connected = true);
      else
        { // Attempt to use this version of the address has failed
          if (connect_address.next() && !bind_address.is_valid())
            { 
              socket->close(); // In case address family changed
              continue;
            }
          break;
        }
    }

  connect_address.reset();
  if (!bind_address.is_valid())
    close();
  return false;
}

/*****************************************************************************/
/*                       kdcs_udp_channel::recv_msg                          */
/*****************************************************************************/

kdu_byte *kdcs_udp_channel::recv_msg(int &num_bytes, int max_bytes,
                                     kdcs_sockaddr *peer_address)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  int msg_flags=MSG_PEEK, msg_len=-1, msg_max=max_bytes;
  if (msg_max == 0)
    msg_max = 1;
  else if (msg_max < 0)
    msg_max = -msg_max;
  else
    msg_flags = 0;
  while (msg_len < 0)
    { 
      if (buf_size < msg_max)
        { // Reallocate the buffer
          buf_size += msg_max;
          if (buf != NULL)
            { delete[] buf; buf = NULL; }
          buf = new kdu_byte[buf_size];
        }
      if (socket_connected)
        { // No need to explicitly recover the sender's address
          msg_len = (int)::recvfrom(socket->sock,(char *) buf,
                                    (size_t) msg_max,msg_flags,NULL,NULL);
#ifdef KDCS_WIN_SOCKETS
          if ((msg_len < 0) && (socket->get_last_error() == WSAEMSGSIZE))
            msg_len = msg_max; // Only windows reports an error if `msg_max' is
                  // too small.  This is really a problem if we are just
                  // peeking to get the first part of the message.
#endif // KDCS_WIN_SOCKETS
          if ((msg_len > 0) && (peer_address != NULL))
            peer_address->copy(connect_address);
        }
      else
        { // Need to pick up the sender's address
#ifdef KDCS_BSD_SOCKETS
          sockaddr_storage addr_buf;
          socklen_t addr_len = sizeof(addr_buf);
#else // Must be Windows
          SOCKADDR_STORAGE addr_buf;
          int addr_len = (int) sizeof(addr_buf);
#endif // !KDCS_BSD_SOCKETS
          msg_len = (int)::recvfrom(socket->sock,(char *) buf,
                                    (size_t) msg_max,msg_flags,
                                    (sockaddr *) &addr_buf,&addr_len);
          if ((msg_len > 0) && (peer_address != NULL))
            peer_address->init((sockaddr *) &addr_buf,(size_t) addr_len,
                               (int) addr_buf.ss_family);
        }
      
      if (msg_len == 0)
        { // Should not happen with UDP
          close();
          throw KDCS_CLOSED_EXCEPTION;
        }
      
      if (msg_len < 0)
        { // Socket error or socket would block
          int err_val = socket->get_last_error();
          if (!socket->check_error_wouldblock(err_val))
            { // Socket error of some sort; perhaps already locally closed
              close();
              throw KDCS_CLOSED_EXCEPTION;
            }
          if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_READ))
            { // Monitor closing
              close();
              throw KDCS_CLOSING_EXCEPTION;
            }
          
          if (internal_servicer == NULL)
            return NULL; // Non-blocking I/O; try again later
          if (lifespan_expired || !internal_servicer->wait_for_service())
            { 
              lifespan_expired = true;
              throw KDCS_LIFESPAN_EXCEPTION;
            }
        }
    }
  num_bytes = (msg_len < msg_max)?msg_len:msg_max;
  return buf;
}

/*****************************************************************************/
/*                       kdcs_udp_channel::send_msg                          */
/*****************************************************************************/

bool kdcs_udp_channel::send_msg(const kdu_byte *msg, int msg_len,
                                kdcs_sockaddr *peer_address)
{
  if (!is_active())
    throw KDCS_CLOSED_EXCEPTION;
  
  while (1)
    { 
      int result = 0;
      if (socket_connected)
        { // We will ignore `peer_address'
          result = (int)::send(socket->sock,(char *) msg,msg_len,0);
        }
      else
        { // Need `peer_address'
          if ((peer_address == NULL) || !peer_address->is_valid())
            { kdu_error e; e << "The `peer_address' argument supplied to "
              "`kdcs_udp_channel::send_msg' is either NULL or else the object "
              "to which it refers indicates that it does not hold a valid "
              "address.  You must provide a valid peer address or else call "
              "`kdcs_udp_channel::connect' first."; }
          result = (int)::sendto(socket->sock,(char *) msg,msg_len,0,
                                 peer_address->get_addr(),
                                 (socklen_t) peer_address->get_addr_len());
        }
      if (result == msg_len)
        return true;
      if (result >= 0)
        { // Something went wrong -- maybe socket is not connected?
          close();
          throw KDCS_CLOSED_EXCEPTION;
        }
      int err_val = socket->get_last_error();
      if (!socket->check_error_wouldblock(err_val))
        { // Socket error; maybe not connected??
          close();
          throw KDCS_CLOSED_EXCEPTION;
        }
      if (!monitor->queue_conditions(channel_ref,KDCS_CONDITION_WRITE))
        { // Monitor closing
          close();
          throw KDCS_CLOSING_EXCEPTION;
        }
      if (internal_servicer == NULL)
        return false; // Non-blocking I/O; try again later
      if (lifespan_expired || !internal_servicer->wait_for_service())
        { 
          lifespan_expired = true;
          throw KDCS_LIFESPAN_EXCEPTION;
        }
    }
  return true;
}

