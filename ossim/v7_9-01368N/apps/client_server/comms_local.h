/*****************************************************************************/
// File: comms_local.h [scope = APPS/CLIENT-SERVER]
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
   This header file is designed to hide platform-specific definitions from
 the more portable definitions in "kdcs_comms.h".
******************************************************************************/

#ifndef COMMS_LOCAL_H
#define COMMS_LOCAL_H

#define FD_SETSIZE 1024

#if (defined WIN32) || (defined _WIN32) || (defined _WIN64)
#  include <winsock2.h>
#  include <direct.h>
#  include <ws2tcpip.h>
#  define KDCS_WIN_SOCKETS
#  define KDCS_HOSTNAME_MAX 256
#else // not Windows
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#  include <netdb.h>
#  include <time.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  define KDCS_BSD_SOCKETS
#  ifdef _POSIX_HOST_NAME_MAX
#    define KDCS_HOSTNAME_MAX _POSIX_HOST_NAME_MAX
#  else
#    define KDCS_HOSTNAME_MAX 1024
#  endif // no _POSIX_HOST_NAME_MAX
#endif // not Windows

#include "kdcs_comms.h"

// Defined here:
namespace kdu_supp {
  struct kdcs_channel_ref; // Facilitate global pointer declarations
}
namespace kd_supp_local {
  struct kdcs_socket;
  struct kdcs_fd_sets;
  struct kdcs_select_interruptor;
}

namespace kd_supp_local {
  using namespace kdu_supp;

/*****************************************************************************/
/*                                kdcs_socket                                */
/*****************************************************************************/

#ifdef KDCS_WIN_SOCKETS
struct kdcs_socket {
  public: // Member functions
    kdcs_socket() { sock = INVALID_SOCKET; }
    kdcs_socket(kdcs_socket &xfer_src)
      { // Transfers the actual `sock' member from `xfer_src' leaving it invalid
        sock = xfer_src.sock; xfer_src.sock = INVALID_SOCKET;
      }
    ~kdcs_socket() { close(); }
    bool is_valid() { return (sock != INVALID_SOCKET); }
    void shutdown()
      { if (is_valid()) ::shutdown(sock,SD_BOTH); }
    void close()
      { if (is_valid()) { ::closesocket(sock); sock = INVALID_SOCKET; } }
    bool make_nonblocking()
      { unsigned long upar=1; return (ioctlsocket(sock,FIONBIO,&upar) == 0); }
    void disable_nagel() {}
    void reuse_address() {}
  public: // Static functions for testing errors
    static int get_last_error() { return (int) WSAGetLastError(); }
    static bool check_error_connected(int err) { return (err==WSAEISCONN); }
    static bool check_error_wouldblock(int err)
      { return ((err==WSAEWOULDBLOCK) || (err==WSAEALREADY) ||
                (err==WSAEINPROGRESS)); }
    static bool check_error_invalidargs(int err) { return (err==WSAEINVAL); };
  public: // Data
    SOCKET sock;
  };
#else // BSD sockets
struct kdcs_socket {
  public: // Member functions
    kdcs_socket() { sock = -1; }
    kdcs_socket(kdcs_socket &xfer_src)
      { // Transfers actual `sock' member from `xfer_src' leaving it invalid
        sock = xfer_src.sock;  xfer_src.sock = -1;
      }
    ~kdcs_socket() { close(); }
    bool is_valid() { return (sock >= 0); }
    void shutdown()
      { if (is_valid()) ::shutdown(sock,SHUT_RDWR); }
    void close()
      { if (is_valid()) { ::close(sock); sock = -1; } }
    bool make_nonblocking()
      { int tmp = fcntl(sock,F_GETFL);
        return ((tmp != -1) && (fcntl(sock,F_SETFL,(tmp|O_NONBLOCK)) != -1)); }
    void disable_nagel()
      { int tval=1;
        setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char *)&tval,sizeof(tval)); }
    void reuse_address()
      { int tval=1;
        setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char *)&tval,sizeof(tval)); }
  public: // Static functions for testing errors
    static int get_last_error() { return (int) errno; }
    static bool check_error_connected(int err) { return (err==EISCONN); }
    static bool check_error_wouldblock(int err)
      { return ((err==EWOULDBLOCK) || (err==EAGAIN) || (err==EALREADY) ||
                (err==EINPROGRESS)); }
    static bool check_error_invalidargs(int err) { return (err==EINVAL); }
  public: // Data
    int sock;
  };
#endif // BSD sockets

/*****************************************************************************/
/*                               kdcs_fd_sets                                */
/*****************************************************************************/

struct kdcs_fd_sets {
  public: // Functions
    kdcs_fd_sets() { clear(); }
    void clear()
      { FD_ZERO(&read_set); FD_ZERO(&write_set); FD_ZERO(&error_set);
        active_reads = active_writes = active_errors = NULL; }
    void add_read(kdcs_socket *sock)
      { active_reads=&read_set; FD_SET(sock->sock,active_reads); }
    void add_write(kdcs_socket *sock)
      { active_writes=&write_set; FD_SET(sock->sock,active_writes); }
    void add_error(kdcs_socket *sock)
      { active_errors=&error_set; FD_SET(sock->sock,active_errors); }
  public: // Data
    fd_set read_set;
    fd_set write_set;
    fd_set error_set;
    fd_set *active_reads;  // Non-NULL if anything added to the corresponding
    fd_set *active_writes; // set since the last call to `clear', via one of
    fd_set *active_errors; // the `add_...' functions.
  };

/*****************************************************************************/
/*                           kdcs_select_interruptor                         */
/*****************************************************************************/

struct kdcs_select_interruptor {
  public: // Member functions    
    kdcs_select_interruptor(kdcs_channel_monitor *monitor);
    ~kdcs_select_interruptor();
    bool init();
      /* Creates the machinery required to allow interruption of `::select'
         calls, returning false if something went wrong in this process.  If
         the function returns false, the `kdcs_channel_monitor' object should
         put itself into the closed state. */
    void clean_thread_info();
      /* Called by `kdcs_channel_monitor::run_clean', this function currently
         does nothing except on Windows operating systems, where it deletes
         the duplicated thread handle created by calls to `do_select'. */
    bool do_poll(int nfds, kdcs_fd_sets *fd_sets, kdu_mutex &mutex);
      /* Convenience function that polls the file descriptor conditions in
         `fd_sets' rather than issuing a blocking call.  Polling is, of
         course, non-interruptable.  The function unlocks `mutex' immediately
         before the poll and re-locks it immediately afterwards.
            Returns true if one or more conditions were satisfied.
      */
    bool do_select(int nfds, kdcs_fd_sets *fd_sets, int delay_microseconds,
                   kdu_mutex &mutex);
      /* This function augments the sets of file-descriptors as appropriate
         in order to include one that allows interruption of the `::select'
         call.  It takes any other required preparatory steps prior to
         actually invoking `::select' and then, upon return from `::select',
         takes any steps required to remove the interruption descriptor from
         the file-descriptor sets.
            The function unlocks `mutex' immediately before calling `::select'
         and re-locks it immediately afterwards, thereby allowing any internal
         bookkeeping to be performed while the lock is held.
            Returns true if one or more of the conditions originally supplied
         via `fd_sets' may be satisfied.
            Returns false if the `::select' call was interrupted, timed out or
         failed (e.g. due to one of the sockets in `fd_sets' having been
         closed, without any conditions being satisfied).  The function also
         returns false if `init' was never called or it returned false.  In
         any event, after a false return, the caller should not test the active
         sets in `fd_sets' to determine conditions that might have occurred.
            Note that we expect `delay_microseconds' to be strictly greater
         than zero in this call.  If you want to poll network conditions,
         call `::do_poll' instead.  We deliberately provide no version of this
         function that blocks indefinitely without a timer based wakeup of any
         form.
            The `fd_sets' argument must be non-NULL, but it is allowed to
         contain no active sets.
      */
    void interrupt_select(kdu_mutex *mutex);
      /* Does whatever is required to interrupt a call to `::select' that
         is assumed to be in-progress.  Note that this call generally arrives
         on a different thread of execution to `do_select'.  If the
         `kdcs_channel_monitor's mutex is already locked when this function
         is called, the `mutex' argument should be NULL; otherwise, the
         function may lock `mutex' temorarily, if thread-safe manipulation
         of internal state information is required. */
  public: // Data
    kdcs_channel_monitor *owner;
#ifdef KDCS_WIN_SOCKETS
    HANDLE run_thread; // Thread that is issuing calls to `run_once'
    DWORD run_thread_id; // Identifier for `run_thread'
    HANDLE waitable_timer;
    kdu_interlocked_int32 interrupt_apc_counter; // See below
    kdcs_socket interrupt_socket; // Unbound DGRAM socket
    int timer_apc_phase; // Used to eliminate APC calls from old timer configs
    bool in_select; // If a call to ::select is in progress; read/written only
                    // from within the thread that calls `do_select'.
#else // assume BSD sockets
    bool pipe_valid; // If `pipe()' call succeeded in creating the fd's below
    int pipe_rdfd; // File descriptor for waiting for the pipe within `select'
    int pipe_wrfd; // File descriptor for waking `select' with write to pipe
#endif // BSD sockets
  };
  /* Notes for Windows OS:
       In this case calls to `::select' always include `wake_socket' in the
       read-set.  An interrupt is achieved by closing this unbound datagram
       socket and immediately recreating it.  Both operations are performed
       within an APC call that is queued onto the `run_thread's asynchronous
       procedure queue.  `::select' is always invoked with a NULL timeout
       argument to be certain that it waits in an alertable state.  Timeouts
       are achieved by setting up the `waitable_timer' to issue an APC call
       that will wake the blocking call to `::select'.  There are thus two
       separate APC functions: one delivered by the timer; the other delivered
       by explicit calls to `interrupt'.  Each time an APC call of the second
       type is queued we first atomically increment `interrupt_apc_counter';
       each time such a call is executed, we atomically decrement the
       `interrupt_apc_counter', performing the wakeup only if the counter has
       reached zero.  Since calls to `QueueUserAPC' require a suitable thread
       handle, we keep a true handle to the thread that last invoked `run_once'
       in the `run_thread' member, along with the corresponding thread-id in
       `run_thread_id'.  Each call to `do_select' compares `run_thread_id' with
       the value returned by `GetCurrentThreadId' to determine whether the
       `run_thread' handle needs to be updated -- a rare, but potentially
       expensive operation.
     Notes for BSD/Linux OS:
       In this case calls to `::select' always include `pipe_rdfd' in the
       read-set.  A wakeup is effected by writing one byte to `pipe_wrfd'.
       Timed waits are achieved by passing a timeout to `::select'.       
  */
  
} // namespace kd_supp_local

/*****************************************************************************/
/*                              kdcs_channel_ref                             */
/*****************************************************************************/

namespace kdu_supp { // Add the following to the kd_supp namespac

struct kdcs_channel_ref {
  public: // Members accessed only under the channel monitor's mutex lock
    kd_supp_local::kdcs_channel *channel;
    kd_supp_local::kdcs_socket *socket; // NULL if channel reference is marked
                                        // for deletion
    kd_supp_local::kdcs_channel_servicer *servicer;
    int active_conditions; // Passed to `::select' at least once so far
    kdcs_channel_ref *next, *prev; // Used to build `channel_refs' list
    bool in_service; // True if `servicer->service_channel' may be in progress
    bool is_active; // True if part of an `active_refs' list
    kdcs_channel_ref *active_next; // Used to build an `active_refs' list
    kdu_long earliest_wakeup; // Earliest time (usecs) to wakeup; -ve if none
    kdu_long latest_wakeup; // Latest time to schedule the wakeup; -ve if none
  public: // Interlocked members that do not require mutex locking
    kdu_interlocked_int32 queued_conditions; // Arrived during `::select' call
};
  
} // namespace kdu_supp
  

#endif // COMMS_LOCAL_H
