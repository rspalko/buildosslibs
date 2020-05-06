/*****************************************************************************/
// File: kdws_manager.h [scope = APPS/WINSHOW]
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
   Defines the main application object for the interactive JPEG2000 viewer,
"kdu_show".
******************************************************************************/
#ifndef KDWS_MANAGER_H
#define KDWS_MANAGER_H

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#if !(defined _MSC_VER && (_MSC_VER >= 1300)) // If not .NET
#  define DWORD_PTR DWORD
#  define UINT_PTR kdu_uint32
#  define INT_PTR kdu_int32
#endif // Not .NET

#include "resource.h"       // main symbols
#include "kdu_messaging.h"
#include "kdu_compressed.h"
#include "kdcs_comms.h" // Gives us access to a nice timer
#include "kdws_window.h"
#include "kdws_renderer.h"
#include "kdu_client.h"

#include <d3d9.h> // Limit ourselves to Direct3D9 for compatibility with XP on

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_winshow" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace

// Declared here:
class kdws_string;
class kdws_settings;
class kdws_notification_manager;
class kdws_frame_presenter;
class kdws_client_notifier;
struct kdws_window_list;
struct kdws_open_file_record;
class kdws_manager;

// Custom Message Codes
#define KDWS_CORE_MESSAGE           WM_APP

/*****************************************************************************/
/*                   Macros Representing Resource Limits                     */
/*****************************************************************************/

#define KDWS_CACHE_MEMORY_LIMIT 512000000 /* 512MB reasonable for desk/laptop*/

/*****************************************************************************/
/*                      EXTERNAL FUNCTIONS and ARRAYS                        */
/*****************************************************************************/

extern const char *jpip_channel_types[];
  /* NULL terminated array of JPIP channel transport names. */

extern bool kdws_compare_file_pathnames(const char *name1, const char *name2);
  /* Returns true if `name1' and `name2' refer to the same file.  If a simple
   string comparison returns false, the function converts both names to
   file system references, if possible, and performs the comparison on
   the references.  This helps minimize the risk of overwriting an existing
   file which the application is using. */

/*****************************************************************************/
/*                                kdws_string                                */
/*****************************************************************************/

class kdws_string {
  /* The sole purpose of this object is to act as a broker between unicode
     and UTF8 (or ASCII) representations of strings so that we can easily
     compile the "kdu_winshow" application with UNICODE enabled, while
     Kakadu natively uses ASCII and UTF8. */
  public: // Member functions
    kdws_string(int max_chars);
      /* Initializes with an empty buffer of the indicated length, with all
         characters cleared.  To get data into the buffer, you need to invoke
         one of the (char *) or (WCHAR *) cast operators and write to the
         buffer you received -- being careful to write no more than the
         maximum number of characters -- does not include the
         null-terminator. */
    kdws_string(const char *src);
      /* Constructs from null-terminated UTF8. After using this constructor,
         it is illegal to retrieve the buffer directly -- you can only use
         the (const char *) and (const WCHAR *) type cast operators. */
    kdws_string(const WCHAR *src);
      /* Constructs from null-terminated unicode. After using this constructor,
         it is illegal to retrieve the buffer directly -- you can only use
         the (const char *) and (const WCHAR *) type cast operators. */
    ~kdws_string()
      {
        if (utf8_buf != NULL) delete[] utf8_buf;
        if (wide_buf != NULL) delete[] wide_buf;
      }
    operator char *()
      { validate_utf8_buf(); wide_buf_valid=false; return utf8_buf; }
      /* Retrieves the internal UTF8 string buffer for the purpose of writing
         to it.  If the last written representation was unicode, the function
         first converts the unicode to UTF8 so that both representations start
         out being consistent; it then invalidates the unicode representation,
         so that any subsequent attempt to access the string as unicode will
         cause the UTF8 version (presumably modified here) to be converted.
         [//]
         Note: when converting from unicode to UTF8, the function determines
         the number of characters to convert by scanning from the end of the
         buffer until the first non-zero entry is encountered.  This ensures
         that multi-string text (with multiple internal null-terminators)
         will be fully converted. */
    operator WCHAR *()
      { validate_wide_buf(); utf8_buf_valid=false; return wide_buf; }
      /* Retrieve the internal unicode string buffer for the purpose of writing
         to it.  If the last written representation was UTF8, the function
         first converts the UTF8 to unicode so that both representations start
         out being consistent; it then invalidates the UTF8 representation,
         so that any subsequent attempt to access the string as UTF8 will
         cause the unicode version (presumably modified here) to be
         converted.
         [//]
         Note: when converting from UTF8 to unicode, the function determines
         the number of characters to convert by scanning from the end of the
         buffer until the first non-zero entry is encountered.  This ensures
         that multi-string text (with multiple internal null-terminators)
         will be fully converted. */
    operator const char *()
      { validate_utf8_buf(); return utf8_buf; }
      /* Returns the UTF8 version of the internal string.  If the last written
         representation was unicode, it is converted to UTF8 here. */
    operator const WCHAR *()
      { validate_wide_buf(); return wide_buf; }
      /* Returns the unicode version of the internal string.  If the last
         written representation was UTF8, it is converted to unicode here. */
    bool is_valid_pointer(const char *cp)
      { return ((cp >= utf8_buf) && (cp < (utf8_buf+utf8_buf_len))); }
      /* Returns true if `cp' points into the internal UTF8 buffer (returned
         via the (char *) or (const char *) type cast operators. */
    bool is_valid_pointer(const WCHAR *cp)
      { return ((cp >= wide_buf) && (cp < (wide_buf+wide_buf_len))); }
      /* Returns true if `cp' points into the internal unicode buffer (returned
         via the (WCHAR *) or (const WCHAR *) type cast operators. */
    void clear()
      { utf8_buf_valid = wide_buf_valid = false;
        if (utf8_buf_len > 0) memset(utf8_buf,0,(size_t) utf8_buf_len);
        if (wide_buf_len > 0) memset(wide_buf,0,(size_t)(2*wide_buf_len)); }
      /* Ensures that both represetations of the string are empty -- single
         null terminator.  This function clears all characters in the internal
         buffer, thereby ensuring that subsequent writes to the buffer need
         not explicitly write their null-terminators.  The same thing is done
         to either the UT8 (resp. unicode) representation prior to converting
         from unicode (resp. UTF8). */
    void set_strlen(int len);
      /* Inserts a null-terminator into the relevant valid buffer(s) at the
         end of `len' characters -- raises an assertion error if `len' goes
         beyond the length of the actual buffer. */
    bool is_empty() { return (this->strlen() == 0); }
      /* Returns true if the string is empty (null-terminator only) in whatever
         representation was most recently written. */
    int strlen();
      /* Returns the number of characters preceding the first null-terminator,
         in whatever representation was most recently written. */
  private: // Helper functions
    void validate_utf8_buf();
    void validate_wide_buf();
  private: // Data
    int utf8_buf_len; // Size of `utf8_buf' -- includes null terminator
    int wide_buf_len; // Size of `wide_buf' -- includes null terminator
    bool utf8_buf_valid; // If no conversion required.
    bool wide_buf_valid; // If no conversion required.
    char *utf8_buf;
    WCHAR *wide_buf;
  };

/*****************************************************************************/
/*                               kdws_settings                               */
/*****************************************************************************/

class kdws_settings {
  public: // Member functions
    kdws_settings();
    ~kdws_settings();
    void save_to_registry(CWinApp *app);
    void load_from_registry(CWinApp *app);
    const char *get_open_save_dir()
      { return ((open_save_dir==NULL)?"":open_save_dir); }
    int get_open_idx() { return open_idx; }
    int get_save_idx() { return save_idx; }
    const char *get_jpip_server()
      { return ((jpip_server==NULL)?"":jpip_server); }
    const char *get_jpip_proxy()
      { return ((jpip_proxy==NULL)?"":jpip_proxy); }
    const char *get_jpip_cache()
      { return ((jpip_cache==NULL)?"":jpip_cache); }
    bool get_jpip_use_cache()
      { return should_use_cache;  }
    const char *get_jpip_request()
      { return ((jpip_request==NULL)?"":jpip_request); }
    const char *get_jpip_channel_type()
      { return ((jpip_channel==NULL)?"":jpip_channel); }
    kdu_client_mode get_jpip_client_mode()
      { return jpip_client_mode; }
    const char *strip_leading_whitespace(const char *str)
      {
        while ((*str=='\r') | (*str=='\n') || (*str=='\t') || (*str==' '))
          str++;
        return str;
      }
    void set_open_save_dir(const char *string)
      {
        if (open_save_dir != NULL) delete[] open_save_dir;
        open_save_dir = new char[strlen(string)+1];
        strcpy(open_save_dir,string);
      }
    void set_open_idx(int idx) {open_idx = idx; }
    void set_save_idx(int idx) {save_idx = idx; }
    void set_jpip_server(const char *string)
      {
        if (jpip_server != NULL) delete[] jpip_server;
        string = strip_leading_whitespace(string);
        jpip_server = new char[strlen(string)+1];
        strcpy(jpip_server,string);
      }
    void set_jpip_proxy(const char *string)
      {
        if (jpip_proxy != NULL) delete[] jpip_proxy;
        string = strip_leading_whitespace(string);
        jpip_proxy = new char[strlen(string)+1];
        strcpy(jpip_proxy,string);
      }
    void set_jpip_cache(const char *string)
      {
        if (jpip_cache != NULL) delete[] jpip_cache;
        string = strip_leading_whitespace(string);
        jpip_cache = new char[strlen(string)+1];
        strcpy(jpip_cache,string);
      }
    void set_jpip_use_cache(bool use_it)
      { should_use_cache = use_it; }
    void set_jpip_request(const char *string)
      {
        if (jpip_request != NULL) delete[] jpip_request;
        string = strip_leading_whitespace(string);
        jpip_request = new char[strlen(string)+1];
        strcpy(jpip_request,string);
      }
    void set_jpip_channel_type(const char *string)
      {
        if (jpip_channel != NULL) delete[] jpip_channel;
        string = strip_leading_whitespace(string);
        jpip_channel = new char[strlen(string)+1];
        strcpy(jpip_channel,string);
      }
    void set_jpip_client_mode(kdu_client_mode mode)
      { jpip_client_mode = mode; }
  private: // Data
    char *open_save_dir;
    int open_idx, save_idx;
    char *jpip_server;
    char *jpip_proxy;
    char *jpip_cache;
    char *jpip_request;
    char *jpip_channel;
    bool should_use_cache;
    kdu_client_mode jpip_client_mode;
  };

/*****************************************************************************/
/* CLASS                    kdws_notification_manager                        */
/*****************************************************************************/

#define KDWS_NOTIFICATION_JPIP        ((kdu_int32) 1)
#define KDWS_NOTIFICATION_DISPLAY     ((kdu_int32) 2)
#define KDWS_NOTIFICATION_PROCESSING  ((kdu_int32) 4)

class kdws_notification_manager {
      /* There is a unique notification manager for each window managed by
         the `kdws_manager' object.  Its purpose is to keep track of
         notification events which arrive on theads other than the main
         thread, so that these notification events can be passed on to the
         window at discrete epochs.
            Notifications are processed within the main thread's run-loop
         when the run-loop is about to become idle. */
public: // Member functions
  kdws_notification_manager();
  void init(kdws_window_list *wnd, DWORD main_thread_id);
  void notify_display_change(); // Called if a new frame has been displayed
  void notify_jpip_change(); // Called if render processing may be needed
  void notify_processing_needed(); // Called if render processing may be needed
  bool process_notifications(bool idle_context);
    /* This function is where we actually invoke the relevant
       `kdws_frame_window' member functions that are the subject of
       notifications supplied previously.  The function atomically
       reads and clears the `state' variable and invokes whatever functions
       are required.  If `idle_context' is true, the function is being
       called from a context in which the main thread's message queue was
       empty, so a call to `window->wnd->do_processing' is always issued,
       regardless of whether it was requested or not.  The function returns
       true if any notifications were processed. */
private: // Data
  kdws_window_list *window;
  DWORD main_thread_id;
  kdu_interlocked_int32 state; // Logical OR of `KDWS_NOTIFICATION_xxx' flags
public: // Links
  kdws_client_notifier *jpip_client_notifier; // Non-NULL if on client list
  kdws_notification_manager *next; // Used to build list for JPIP client
};

/*****************************************************************************/
/*                               kdws_frame_presenter                        */
/*****************************************************************************/

class kdws_frame_presenter {
      /* There is a unique frame presenter for each window managed by the
         application. */
public: // Member functions invoked from within `kdws_manager'
  kdws_frame_presenter(kdws_manager *manager,
                       kdws_notification_manager *notifier,
                       double display_event_interval,
                       kdws_frame_window *wnd);
  ~kdws_frame_presenter();
  bool draw_pending_frame(double display_event_time,
                          double next_display_event_time);
    /* Called from the presentation thread's run-loop at regular intervals.
       If something is actually drawn, this function returns true, meaning
       that the `swap_buffers' function should later be called.  When there
       are multiple windows involved in an animation, we draw each of them
       and then invoke `swap_buffers' on them all one after the other,
       thereby maximizing the chance that they can all window buffers can
       become current during the same vertical blanking interval.
          This function locks the `drawing_mutex' then proceeds to invoke
       `kdws_renderer::present_queued_frame_buffer', if the object
       is enabled (see `enable').  That function both draws the most relevant
       queued frame, if any, and pops any frames that have already expired.
       If anything was presented, that function returns true, causing the
       present function to call `notification_manager->notify_display_change'
       and then wake the application thread, if necessary.  This ultimately
       ensures that the `kdws_renderer::update_animation_status_info' and
       `kdws_renderer::manage_animation_frame_queue' functions will be called.
          The `display_event_time' and `next_display_event_time' arguments
       determine which frame buffer should be displayed and also provide
       the renderer information about when this function is likely to be
       called next.  The separation between these two times is the monitor
       refresh rate and calls to this function are expected to be separated
       by this amount (or something very close to it) in real system time.
       However, the display event times are ideally derived from a monitor
       time base that may not run at exactly the same rate as the system 
       clock.
          In practice, `display_event_time' is interpreted as the end of the
       VBlank interval during which we expect `swap_buffers' calls to succeed.
       The call to this function arrives earlier than the nominal
       `display_event_time' by somewhat less than one monitor refresh period,
       so as to maximize the chance that all drawing can be compete before
       the VBlank arrives. */
  bool swap_buffers();
    /* Does nothing, returning false, unless a previous call to
       `draw_pending_frame' left the object with something to present to
       the display.  Returns true if something is presented.  This call is
       expected to return during the next VBlank period, if anything was
       drawn. */
public: // Member functions invoked by `kdws_renderer' from the main thread
  double enable(kdws_renderer *renderer, HWND image_view_hwnd);
    /* This function is invoked by `renderer' to activate the frame presenter,
       passing in the image view's window handle.  Each time the window
       dimensions changes, the `resize' function must be called.
          The function returns the next display event time that it expects to
       pass as the current display event time in the next call to
       `renderer->present_queued_frame_buffer'.  In order to determine this
       value, it is possible that the function has to wait for a display
       event cycle to pass -- if one does not occur within a reasonable
       time, for some reason, the function assumes that the presentation
       thread is broken and returns a -ve argument.  The caller should
       recognize a -ve argument as an indication that animation will not
       be possible. */
  bool resize(HWND image_view_hwnd);
    /* This function is invoked from within `kdws_renderer::view_dims_changed'
       to resize the `surface' and `swap_chain' Direct3D objects.  If
       something goes wrong, the function returns false, which means that
       animation should be stopped and `disable' should be invoked.
         If the object is not currently `enable'd the function does nothing,
       but it does return true (success), since nothing needed to be done. */
  void disable();
    /* Once disabled, the frame presenter is guaranteed not to invoke
       `kdws_renderer::present_queued_frame_buffer'.  The object starts out
       in the disabled state.  Typically, the application enables frame
       presentation prior to starting an animation and then disables it
       again once the animation is complete. */
  double get_display_event_interval() { return display_event_interval; }
    /* Display event times are expected to be separated by this interval. */
public: // Member functions invoked by `kdws_renderer' from presentation thread
  CDC *access_surface(kdu_coords &buffer_size);
    /* Returns a device context that can be used to draw to the internal
       off-screen plain surface.  Returns NULL if there is no off-screen
       plain surface to use.  This function must be followed by a call to
       `release_surface' which unlocks the internal off-screen plain Direct3D
       surface and transfers its contents to the swap-chain's backbuffer.
       The `buffer_size' argument is filled with the current size of
       the backbuffer, which is the entire region that can be drawn. */
  void release_surface();
    /* Always follow a call to `access_surface' with a call to
       `release_surface' before returning from the
       `kdws_renderer::present_queued_frame_buffer' function. */
private: // Data
  kdws_manager *manager; // We can use this to get a DirectX swapchain
  kdws_notification_manager *notification_manager;
  kdws_frame_window *window;
  kdu_mutex drawing_mutex; // Locked while the frame is being drawn
  kdws_renderer *target; // non-NULL only when enabled
  IDirect3DSwapChain9 *swap_chain; // Created only when enabled
  kdu_coords backbuffer_size; // Current dimensions of swap-chain backbuffer
  IDirect3DSurface9 *surface; // Off-screen plain surface for rendering
  CDC surface_dc; // Surface device-context when attached
  bool wants_swap_buffers; // If the next `swap_buffers' call can do something
  double display_event_interval;
  double waiting_for_next_display_event_time; // Used to communicate between
          // `enable' and `draw_pending_frame'.
};

/*****************************************************************************/
/*                             kdws_client_notifier                          */
/*****************************************************************************/

class kdws_client_notifier : public kdu_client_notifier {
  public: // Member functions
    kdws_client_notifier()
      {
        mutex.create();
        window_notifiers = NULL;
      }
    ~kdws_client_notifier()
      { 
        assert(window_notifiers == NULL);
        mutex.destroy();
      }
    void retain_window(kdws_notification_manager *mgr)
      { 
        assert(mgr->jpip_client_notifier == NULL);
        mutex.lock();
        mgr->next = window_notifiers;
        window_notifiers = mgr;
        mgr->jpip_client_notifier = this;
        mutex.unlock();
      }
    void release_window(kdws_notification_manager *mgr)
      { 
        assert(mgr->jpip_client_notifier == this);
        mutex.lock();
        kdws_notification_manager *prev=NULL, *scan=window_notifiers;
        for (; scan != NULL; prev=scan, scan=scan->next)
          if (scan == mgr)
            { 
              if (prev == NULL)
                window_notifiers = mgr->next;
              else
                prev->next = mgr->next;
              break;
            }
        assert(scan != NULL);
        mgr->next = NULL;
        mgr->jpip_client_notifier = NULL;
        mutex.unlock();
      }
    void notify()
      { 
        mutex.lock();
        kdws_notification_manager *scan;
        for (scan=window_notifiers; scan != NULL; scan=scan->next)
          scan->notify_jpip_change();
        mutex.unlock();
      }
  private: // Data
    kdu_mutex mutex; // Protects the list of notification managers
    kdws_notification_manager *window_notifiers;
};

/*****************************************************************************/
/* STRUCT                         kdws_window_list                           */
/*****************************************************************************/

struct kdws_window_list {
  kdws_frame_window *wnd;
  int window_identifier; // See `kdws_manager::get_window_identifier'
  const char *file_or_url_name; // Used as an identifier or title
  double wakeup_time; // -ve if no wakeup is scheduled
  kdws_notification_manager notification_manager;
  kdws_frame_presenter *frame_presenter;
  bool window_empty;
  bool window_placed;
  kdws_window_list *next;
  kdws_window_list *prev;
};

/*****************************************************************************/
/* STRUCT                     kdws_open_file_record                          */
/*****************************************************************************/

struct kdws_open_file_record {
  kdws_open_file_record()
    { 
      open_pathname = open_url = save_pathname = NULL; retain_count = 0;
      jpip_client = NULL; jpx_client_translator = NULL; client_notifier=NULL;
      reconnecting_client = false; client_auto_save_mode = false;
      client_original_auto_save_mode = false; next = NULL;
    }
  ~kdws_open_file_record()
    {
      if (open_pathname != NULL) delete[] open_pathname;
      if (save_pathname != NULL) delete[] save_pathname;
      if (open_url != NULL) delete[] open_url;
      if (jpip_client != NULL)
        {
          jpip_client->close(); // So we can remove the context translator
          jpip_client->install_context_translator(NULL);
        }
      if (jpx_client_translator != NULL) delete jpx_client_translator;
      if (jpip_client != NULL) delete jpip_client;
      if (client_notifier != NULL) delete client_notifier;
    }
  int retain_count;
  char *open_pathname; // Non-NULL if this record represents a local file
  char *open_url; // Non-NULL if this record represents a URL served via JPIP
  char *save_pathname; // Non-NULL if there is a valid saved file which needs
                       // to replace the existing file before closing.
  kdu_client *jpip_client; // Non-NULL if and only if `open_url' is non-NULL
  kdu_clientx *jpx_client_translator;
  kdws_client_notifier *client_notifier;
  bool client_auto_save_mode; // If client is configured to save a cache file
  bool client_original_auto_save_mode; // Mode when client was activated
  bool reconnecting_client; // Used only to prevent unbounded recursive calls
                            // to `kdws_window::reconnect_jpip_client'.
  kdws_open_file_record *next;
};

/*****************************************************************************/
/*                                 kdws_manager                              */
/*****************************************************************************/

class kdws_manager : public CWinApp {
public: // Member functions
  kdws_manager();
  ~kdws_manager();
  void presentation_thread_entry();
    /* Entry-point for the presentation thread. */
  bool application_can_terminate();
    /* Returns true if the application's windows are all happy to
       terminate -- interrogates the user if there is any unsaved data. */
  void send_application_terminating_messages();
    /* Sends a terminating message to each window in the application -- this
       will perform any essential cleanup.  This invokes the `DestroyWindow'
       function on each window. */
// ---------------------------------------------------------------------------
public: // Access to state managed on behalf of the image windows
  kdws_settings *access_persistent_settings() { return &settings; }
    /* Accesses the common `kdws_settings' object, which manages state
       information that is saved between invocations of the application. */
// ---------------------------------------------------------------------------
public: // Window list manipulation functions
  void add_window(kdws_frame_window *wnd);
  void remove_window(kdws_frame_window *wnd);
    /* The above functions add and remove a window from the internal list
       of managed windows.  They do not create `kdws_frame_window' objects;
       nor do they destroy them.  The functions are called from within
       `kdws_frame_window' itself. */
  int get_access_idx(kdws_frame_window *wnd);
    /* Returns the position of the supplied window within the list of
       all active windows -- if this index is passed to `access_window',
       the same window will be returned.  Returns -1 if, for some reason,
       the window turns out not to be in the list -- may have been removed
       with `remove_window' or never added by `add_window'. */
  kdws_frame_window *access_window(int idx);
    /* Retrieve the idx'th window in the list, starting from idx=0. */
  int get_window_identifier(kdws_frame_window *wnd);
    /* Retrieves the integer identifier which is associated with the
       indicated window (0 if the window cannot be found).  The identifier
       is currently set equal to the number of `add_window' calls which
       occurred prior to and including the one which added this window. */
  void reset_placement_engine();
    /* Resets the placement engine so that new window placement operations
       will start again from the top-left corner of the screen. */
  void declare_window_empty(kdws_frame_window *wnd, bool is_empty);
    /* Called with `is_empty'=false when the window's
       `kdws_renderer::open_file' function is used to open a new file/URL.
       Called with `is_empty'=true when the window's
       `kdws_renderer::close_file' function is used to close a file/URL.
       Windows which are empty can be re-used by controller-wide
       operations which would otherwise create a new window. */
  kdws_frame_window *find_empty_window();
    /* Returns NULL if there are no empty windows. */
  bool place_window(kdws_frame_window *wnd, kdu_coords frame_size,
                    bool do_not_place_again=false,
                    bool placing_first_empty_window=false);
    /* Place the window at a good location.  If `do_not_place_again' is true
       and the window has been placed before, the function returns false,
       doing nothing.  Otherwise, the function always returns true.  If
       `placing_first_empty_window' is true, the function places the window
       but does not update any internal state, so the window can be placed
       again when something is actually opened; this is sure to leave the
       window in the same position where possible, which is the most
       desirable scenario. */
// ---------------------------------------------------------------------------
public: // Menu action broadcasting functions
  kdws_frame_window *get_next_action_window(kdws_frame_window *caller);
    /* Called from within window-specific menu action handlers to determine
       the next window, if any, to which the menu action should be passed.
       The function returns nil if there is none (the normal situation).  The
       function may be called recursively.  It knows how to prevent indefinite
       recursion by identifying the key window (the one which should have
       received the menu action call in the first place.  If there is no
       key window when the function is called and the caller is not the
       key window, the function always returns nil for safety. */
  void set_action_broadcasting(bool broadcast_once,
                               bool broadcast_indefinitely);
    /* This function is used to configure the behavior of calls to
       `get_next_action_window'.  If both arguments are false, the latter
       function will always return nil.  If `broadcast_once' is false, the
       `get_next_action_window' function will return each window in turn for
       one single cycle.  If `broadcast_indefinitely' is true, the function
       will work to broadcast all menu actions to all windows. */
// ---------------------------------------------------------------------------
public: // Timing and scheduling functions
  double get_current_time()
    {
      kdu_long usecs = absolute_time.get_ellapsed_microseconds();
      return ((double) usecs) * 0.000001;
    }
  void schedule_wakeup(kdws_frame_window *window, double time);
    /* Schedules a wakeup call for the supplied window at the indicated
       time.  `kdws_frame_window::wakeup'  will be invoked on `window'
       at this time (or shortly after) passing the scheduled `time', together
       with the time at which the wakeup message is actually sent.  At most
       one wakeup time may be maintained for each window, so this function
       may change any previously installed wakeup time.  All wakeup times are
       managed internally to this object by a single timer object, so as to
       minimize overhead and encourage synchronization of frame playout times
       where there are multiple windows.
          If the `time' has already passed, this function will not invoke
       `window->wakeup' immediately.  This is a safety measure to prevent
       unbounded recursion in case `schedule_wakeup' is invoked from within
       the `wakeup' function itself (a common occurrence).  Instead, the
       `wakeup' call will be made once the underlying run-loop gets control
       back again and processes the relevant timer message.
          If the `time' argument is -ve, this function simply cancels
       any pending wakeup call for the window. */
  void broadcast_playclock_adjustment(double delta);
    /* Called from any window in playback mode, which is getting behind its
       desired playback rate.  This function makes adjustments to all window's
       play clocks so that they can remain roughly in sync. */
// ---------------------------------------------------------------------------
public: // Frame presenter management functions
  kdws_frame_presenter *get_frame_presenter(kdws_frame_window *window);
    /* Returns the frame presenter object associated with the window, for
       use in presenting live video frames efficiently, in the background
       presentation thread. */
  IDirect3DSwapChain9 *create_swap_chain(HWND wnd, kdu_coords &wnd_size);
    /* Creates a new Direct3D swap chain to manage the window identified
       by `wnd'.  Upon successful return, the `wnd_size' argument is set
       to the dimensions of the new swap-chain's back buffer.  The
       function returns NULL if not successful. */
  IDirect3DSurface9 *create_surface(kdu_coords size);
    /* Creates a new Direct3D off-screen plain surface with the indicated
       size.  The surface width is automatically rounded up
       to a multiple of 4 pixels (16 bytes) to facilitate direct data
       transfers.  Returs NULL if not successful. */
  void presentation_thread_wait(kdu_long usecs);
    /* This function is called only from the presentation thread; it
       uses the `presentation_timer' to wait for the indicated number of
       microseconds, that is guaranteed to be positive. */
// ---------------------------------------------------------------------------
public: // Management of files, URL's and JPIP clients
  const char *retain_open_file_pathname(const char *file_pathname,
                                        kdws_frame_window *wnd);
    /* This function declares that a window (identified by `wnd') is about
       to open a file whose name is supplied as `file_pathname'.  If the file
       is already opened by another window, its retain count is incremented.
       Otherwise, a new internal record of the file pathname is made.  In any
       case, the returned pointer corresponds to the internal file pathname
       buffer managed by this object, which saves the caller from having to
       copy the file to its own persistent storage. */
  void release_open_file_pathname(const char *file_pathname,
                                  kdws_frame_window *wnd);
    /* Releases a file previously retained via `retain_open_file_pathname'.
       If a temporary file has previously been used to save over an existing
       open file, and the retain count reaches 0, this function deletes the
       original file and replaces it with the temporary file.  The `wnd'
       argument identifies the window which is releasing the file. */
  const char *get_save_file_pathname(const char *file_pathname);
    /* This function is used to avoid overwriting open files when trying
       to save to an existing file.  The pathname of the file you want to
       save to is supplied as the argument.  The function either returns
       that same pathname (without copying it to an internal buffer) or else
       it returns a temporary pathname that should be use instead, remembering
       to move the temporary file into the original file once its retain
       count reaches zero, as described above in connection with the
       `release_open_file_pathname' function. */
  void declare_save_file_invalid(const char *file_pathname);
    /* This function is called if an attempt to save failed.  You supply
       the same pathname supplied originally by `get_save_file_pathname' (even
       if that was just the pathname you passed into the function).  The file
       is deleted and, if necessary, any internal reminder to copy that file
       over the original once the retain count reaches zero is removed. */
  int get_open_file_retain_count(const char *file_pathname);
    /* Returns the file's retain count. */
  bool check_open_file_replaced(const char *file_pathname);
    /* Returns false if the supplied file pathname already has an alternate
       save pathname, which will be used to replace the file once its retain
       count reaches zero, as explained in connection with the
       `release_open_file_pathname' function. */
  const char *retain_jpip_client(const char *server, const char *request,
                                 const char *url, kdu_client * &client,
                                 int &request_queue_id,
                                 kdws_frame_window *wnd);
    /* This function is used when a window (identified by `wnd') needs to
       open a JPIP connection to an image on a remote server.  The first thing
       to understand is that when multiple windows want to access the same
       remote image, it is much more efficient for them to share a single cache
       and a single `kdu_client' object, opening multiple request queues
       within the client.  Where possible, this translates into the client
       opening multiple parallel JPIP channels to the server.  To facilitate
       this, the `kdu_client' object is not created within the window (or its
       associated `kdws_renderer' object), but within the present function.
       If a client already exists which has an alive connection to the server
       for the same resource, the function returns a reference to the existing
       `client' to which it has opened a new request queue, whose id is
       returned via `request_queue_id' -- this is the value supplied in calls
       to `kdu_client::post_window' and `kdu_client::disconnect' amongst other
       member functions.
       [//]
       In the special case of a one-time request, the function allows any
       number of windows to associate themselves with the client, returning
       a `request_queue_id' value of 0 in every case, as if each of them
       were the one which originally called `kdu_client::connect'.  This
       is fine because none of them are allowed to alter the window of
       interest for clients opened with a one-time request.
       [//]
       The remote image can be identified either through a non-NULL
       `url' string, or through non-NULL `server' and `request' strings.
       In the former case, the server name and request component's of the
       URL are separated by the function.  In either case, the function
       returns a reference to an internally created URL string, which could
       be used as the `url' in a future call to this function to retain the
       same client again, opening another request queue on it.  The returned
       string is usually also employed as the window's title.  This string
       remains valid until the retain count for the JPIP client reaches zero
       (through matching calls to `release_jpip_client'), at which point the
       client is closed and deleted.
       [//]
       The present function also arranges for a `kdu_client_notifier'
       object to be created and associated with the client, which in turn
       arranges for a `client_notification' message to be sent to `wnd'
       whenever the client has something to notify the application about.
       If multiple windows are sharing the same client, they will all receive
       notification messages.  The `client_notification' messages are
       delivered (asynchonously) within the application's main thread, even
       through they are generated from within the client's separate
       network management thread.
       [//]
       Note that this function may indirectly generate an error through
       `kdu_error' if there is something wrong with the `server',
       `request' or `url' arguments, so the caller needs to be prepared to
       catch the resulting integer exception.
    */
  const char *retain_jpip_cache(const char *path, const char *cache_identifier,
                                kdu_client * &client, int &request_queue_id,
                                kdws_frame_window *wnd);
    /* Similar to `retain_jpip_client', except that this function opens
       a JPIP cache file that might have been produced by a previous browsing
       session (possibly a file that is already in the application's JPIP cache
       directory, but perhaps a file from some other source, such as a
       web download or email attachment).  The cache file is at the location
       supplied by `path', which the current function expands (if necessary)
       to ensure that a fully qualified path name is available.
       As with `retain_jpip_cache', the function either succeeds, returning
       the name to be used for the opened target, or it generates an error
       through `kdu_error'.  Upon success, `client' is set to point to the
       relevant `kdu_client' object whose `target_started' function should
       immediately return true.
          Before attempting to construct a new `kdu_client' and invoke
       `kdu_client::open_cache_file', the function checks to see if there
       is an existing `kdu_client' whose `kdu_client::get_cache_identifier'
       function reports the same string as the supplied `cache_identifier'
       argument -- the `cache_identifier' should have been obtained from a
       call to the static `kdu_client::check_cache_file' function before
       calling here.  If there is such a compatible client, the function
       calls its `kdu_client::augment_with_cache' function, adds the `wnd'
       window as one of the client's retainers, and uses
       `kdu_client::add_queue' to obtain a new `request_queue_id' for the
       caller, if the client has a live server connection.  If there is no
       current live server connection, `request_queue_id' will be 0 upon
       return.
          If there is no existing compatible client, the function creates
       a new one and invokes its `open_from_cache' function to set things
       in motion, returning with `request_queue_id' equal to 0.
       Even though the returned `client' may not be connected to a
       JPIP server (i.e., `is_alive' returns false) so that the
       `request_queue_id' for this and perhaps other attached windows shows
       as 0, the `reconnect_jpip_client' may later be called to establish a
       server connection and assign all windows that are using the `client'
       new `request_queue_id' values for posting window-of-interest
       requests. */
  int count_peers(kdu_client *client, kdws_frame_window *wnd);
    /* Counts the number of other windows that are sharing the same
       file or JPIP client as `wnd'.  If `client' is NULL, the function
       counts peers sharing the same file.   This information can help in
       determining whether or not it is safe to make some change that
       requires no other users of a resource to be active -- the return
       value is 0 in this case. */
  int reconnect_jpip_client(kdu_client *client, kdws_frame_window *wnd);
    /* Note, this function may be called in response to a call to
       `kdws_window:note_jpip_client_reconnecting' which originates from
       another call to this function.  In that case, the caller should
       set its `client_request_queue_id' to -1 to reinforce the fact that any
       queue-id it previously had is no longer valid, after which it calls
       this function to obtain a new queue-id.  Otherwise, the function
       should only be called if the renderer's current
       `client_request_queue_id' is no longer associated with an alive JPIP
       queue, as returned by `client->is_alive'.
       If the client is still connected to the server, or this is a
       recursive call from `kdws_window:note_jpip_client_reconnecting', the
       function only attempts to assign a new queue to the caller, returning
       the queue-id, which will be -ve if unsuccessful.
       If the client is not conencted, the function invokes
       `kdu_client::reconnect' returning the base queue-id (always 0) if
       successful and then invoking every other window's
       `kdws_window:note_jpip_client_reconnecting' function so that all
       renderer's can get updated queue-ids.  If the `kdu_client::reconnect'
       call fails immediately, this function returns the failure code, which
       is always negative. */
  int restart_jpip_client(kdu_client *client, kdws_frame_window *wnd);
    /* Similar to `reconnect_jpip_client', except that this function calls
       `client->reconnect' with the `clear_cache' argument equal to true,
       meaning that all existing cache contents are destroyed in the call
       and the service is started again (if possible).  This function
       will fail, returning a -ve value, if the connection cannot be
       established, or `wnd' is not currently associated with `client', or
       there are any other windows associated with `client'; in that event,
       the caller will still need to call `release_jpip_client' with its
       original queue-id at some point.  If successful, the function returns
       the new queue-id to be used for the window's interactions with
       `client' in the future -- this will inevitably be 0.  Upon successful
       return from this function, the caller will generally need to
       attach an auxiliary `kdu_cache' to the `client' and and file interfaces,
       such as a `jp2_family_src' and associated `jpx_source', from scratch,
       then re-enter `kdrd_renderer::open_file' with a NULL argument to
       progress the reconnection process.  These steps are necessary because
       the call obliterates all cache contents and hence renders invalid
       any metadata that may have been used in the opening of a file or
       codestream interface based on the cache contents. */
  bool set_cache_save_mode(kdu_client *client, bool mode);
    /* This call can be issued by any window, even if it does not have a JPIP
       client (`client' is NULL in this case).
       If there is a `client' and it was originally configured in an
       auto-save mode, such that the cache contents would eventually be saved
       to a cache file in a supplied cache directory, the auto-save mode can
       be changed (i.e., this function can succeed whether `mode' is true or
       false).  Flipping of the auto-save mode is achieved by calling
       `client->set_cache_handling' with a value of 1 (`mode'=true) or
       -1 (`mode'=false).
          If an existing `client's cache saving mode is flipped, the function
       also adjusts the default "use cache-directory" mode that determines the
       behaviour of future calls to `retain_jpip_client' and
       `retain_jpip_cache'.
          If `client' is NULL and there is a default cache directory defined
       for possible use from the last presentation of the URL dialog, this
       function change the default "use cache-directory" mode that determines
       the behaviour of future calls to `retain_jpip_client' and
       `retain_jpip_cache' in order to match `mode'.
          In either case the function returns true if the cache auto-save
       `mode' was already in place or a change was made, but false if
       the requested change could not be made.  The following function allows
       you to test ahead of time whether a change can be made, as well as
       discovering the current mode. */
  bool check_cache_save_mode(kdu_client *client, bool &can_change);
    /* If `client' is NULL, this function always sets `can_change' to true
       if and only if a default cache directory is available, returning the
       current default state of the cache-file-use mode that determines the
       future behaviour of calls to `retain_jpip_client' and
       `retain_jpip_cache'.
          If `client' is non-NULL, the function returns the current state
       of the client's auto-save mode, which affects whether or not cache
       contents will be saved in a cache directory at some appropriate
       point, setting `can_change' to true only if the auto-save mode was
       enabled when the client was activated via `retain_jpip_client' or
       `retain_jpip_cache'. */
  void use_jpx_translator_with_jpip_client(kdu_client *client);
    /* This function is called if you discover that the resource being
       fetched using this client represents a JPX image resource.  The
       function installs a `kdu_clientx' client-translator for the client
       if one is not already installed. */
  void release_jpip_client(kdu_client *client, kdws_frame_window *wnd);
    /* Releases access to a JPIP client (`kdu_client' object) obtained by
       a previous call to `retain_jpip_client'.  The caller should have
       already invoked `kdu_client::disconnect' (usually with a long timeout
       and without waiting).  The present function invokes the
       `kdu_client::disconnect' function again if and only if the number
       of windows using the client drops to 0; in this case a much smaller
       timeout is used to forcibly disconnect everything if the server is
       too slow; the function also waits for disconnection (or timeout)
       to complete. */
  void open_url_in_preferred_application(const char *url,
                                         const char *base_path);
    /* This function provides a service that can be accessed from
       `kdws_renderer' or any of the related objects to open a URL that
       might be encountered within the metadata of some existing file or
       resource.  The `url' can be relative or absolute and it can also
       be a file.  The `base_path' argument provides the path name or JPIP
       URL of the object within which the `url' is found -- this is used to
       resolve relative `url's.  The `base_path' should be one of the
       strings returned by this object's `retain_jpip_client' or
       `retain_open_file_pathname' functions.  This function does not
       perform any hex-hex encoding of non-URI legal characters that
       might be found in the `url' string.
          The function first checks to see if the `url' can be understood
       as a JPIP reference (either because it contains a "jpip://" protocol
       prefix or because the referenced resource ends in ".jpx", ".jp2",
       ".j2k" or ".j2c" (caseless comparison).  If so, the function directs
       the current application to open the resource using JPIP.
          Otherwise, the function directs the Operating System to open the
       URL or file, using whatever application is most appropriate. */
// ---------------------------------------------------------------------------
public: // MFC overrides
	virtual BOOL InitInstance();
  virtual int ExitInstance();
	virtual BOOL OnIdle(LONG lCount);
  virtual BOOL PreTranslateMessage(MSG *pMsg);
  virtual BOOL SaveAllModified();
// ---------------------------------------------------------------------------
private: // Helper functions
  static LRESULT CALLBACK
    idle_hook_callback(int code, WPARAM wParam, LPARAM lParam);
    /* This hook is registered inside `InitInstance' and unregistered inside
       `ExitInstance', to be called when the application's main thread
       becomes idle in the foreground.  After a lot of soul searching and
       failed attempts with the annoying windows message model, we finally
       found this as a reasonable mechanism for catching idle events within
       the silly modal processing loops used by scrollbars and interactive
       window resizing.
          Unfortunately, Windows does not have the nice queue interleaving
       properties of OSX that allow the application to interleave processing
       messages with user input messages so that everyone gets a fair share.
       Instead, the Windows message processing system has two extremely
       annoying and almost unavoidable properties: a) user input gets
       indefinitely deferred behind messages that are posted to the queue
       and this cannot be circumvented in any way; and b) modal processing
       loops are entered at points that cannot be anticipated within the
       message processing loop, taking control away from the application's
       logic and providing no sensible means to get it back.  These two
       things conspire together to make it impossible to synthesize nice
       interleaving of posted messages.  For example, if we try to clear
       and process messages in our own modal loop inside the handler for
       a "processing-needed" notification, we are liable to unwittingly
       enter a new level of modal processing within the scrollbar
       machinery -- attempts to do further modal processing if our
       "processing needed" messages happen to be called from that context
       fails for completely inexplicable reasons and Microsoft provides
       absolutely no clues at to what they are doing in the modal processing
       loops of their controls.
          After wasting a lot of time on this silly message queueing scheme
       (and it seems plenty of others have too), it turns out that the
       most sensible approach is to install a `WH_FOREGROUNDIDLE' hook.
       Notification processing is now done both within this hook function
       and within the `CWinApp::OnIdle' override and this ensures that
       we always get to see idle events so we can keep the processing going.
       Since Windows always defers user input indefinitely behind posted
       messages, the only sensible way to keep everything alive and active
       even when processing workload becomes excessive, is to put all of our
       processing inside these idle points.  There is multi-threaded
       rendering that is generally going on in the background, but in this
       application we prefer not to rely upon performing all processing
       calls to `kdu_region_compositor' within a separate thread -- we may
       do that in the future. */
  void do_idle_processing();
    /* Called from `idle_hook_callback' as well as `OnIdle', this function
       makes sure we catch all error/warning popup messages and then passes
       through the `kdws_notification_manager::process_notifications'
       functions -- these functions may trigger further notification calls
       which will result in the issuing of WM_NULL messages to keep the
       message pump alive. */
  void set_wakeup_time(double abs_time, double current_time);
    /* Sets up the wakeup timer, as required, to ensure that we get woken
       up when `abs_time' arrives.  If `abs_time' has already arrived, we
       post a fake WM_TIMER message to the message queue.  If `current_time'
       is > 0.0, the current time has recently been evaluated and need not
       be evaluated again. */
  void install_next_scheduled_wakeup();
    /* Scans the window list to find the next window which requires a wakeup
       call.  If the time has already passed, executes its wakeup function
       immediately and continue to scan; otherwise, sets the timer for future
       wakeup.  This function attempts to execute any pending wakeup calls
       in order. */
  void process_scheduled_wakeup();
    /* Called if a WM_TIMER message is processed on the main application
       thread. */
  static void CALLBACK
    wakeup_timer_callback(HWND hwnd, UINT msg, UINT_PTR event_id,
                          DWORD time);
// ---------------------------------------------------------------------------
private: // Functions used to open files
  void open_file_in_free_window(const char *fname);
  void open_url_in_free_window(const char *url);
    /* These functions attempt to open the file/URL in an existing empty
       window; if there is none, they create a new window. */
// ----------------------------------------------------------------------------
private: // Generic state
  HHOOK idle_hook_handle; // See `idle_hook_callback'.
  DWORD main_thread_id;
  kdcs_timer absolute_time;
  kdws_settings settings; // Manages load/save and JPIP settings.
private: // State related to window list management
  kdws_window_list *windows; // Window identifiers strictly increasing in list
  int next_window_identifier;
  kdws_frame_window *last_known_key_wnd; // NULL if none is known to be key
  bool broadcast_actions_once;
  bool broadcast_actions_indefinitely;
private: // Auto-placement information; these quantities are expressed in
         // screen coordinates.
  kdu_coords next_window_pos; // For next window to be placed on current row
  int next_window_row;
  kdu_coords cycle_origin; // Origin of current placement cycle
private: // Information for timed wakeups
  UINT_PTR wakeup_timer_id; // 0 means no timer is installed
  kdws_window_list *next_window_to_wake;
  bool will_check_best_window_to_wake; // This flag is set while in (or about
         // to call `install_next_scheduled_wakeup'.  In this case, a call
         // to `schedule_wakeup' should not try to determine the next window
         // to wake up by itself.
private: // Data required for DirectX
  IDirect3D9 *direct3d;
  HWND device3d_hwnd; // Dummy window created for device implicit swapchain
  IDirect3DDevice9 *device3d;
  kdu_coords screen_size; // Can help in interpreting raster status
private: // Data required to manage the presentation thread
  int mm_timer_resolution; // Value passed to `timeBeginPeriod', or 0
  CWinThread *presentation_thread;
  bool presentation_thread_exited;
  bool presentation_thread_exit_requested;
  HANDLE presentation_timer; // A waitable timer
  kdcs_timer presentation_absolute_time; // Used only by presentation thread
  double display_event_interval;
  double next_display_event_time;
  kdu_long next_display_event_abs_usecs; // Expected value of abs timer
  kdu_long display_interval_usecs; // Approx. display interval in microseconds
  kdu_mutex window_list_change_mutex; // Locked by the main thread before
         // changing the window list.  Locked by the presentation thread
         // before scanning the window list for windows whose frame presenter
         // needs to be serviced.
private: // Data required to safely manage open files in the face of saving
  kdws_open_file_record *open_file_list;
// ---------------------------------------------------------------------------
public: // Command Handlers
	afx_msg void OnAppAbout();
  afx_msg void menu_AppExit();
  afx_msg void menu_WindowNew();
  afx_msg void menu_WindowArrange();
  afx_msg void menu_WindowBroadcast();
  afx_msg void kdws_manager::can_do_WindowBroadcast(CCmdUI *pCmdUI);
	DECLARE_MESSAGE_MAP()
};

#endif // KDWS_MANAGER_H
