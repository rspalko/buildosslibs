/*****************************************************************************/
// File: kdws_manager.cpp [scope = APPS/WINSHOW]
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
"kdu_show".  Menu processing and idle-time decompressor processing are all
controlled from here.  The "kdu_show" application demonstrates some of the
support offered by the Kakadu framework for interactive applications,
including persistence and incremental region-based decompression.
******************************************************************************/

#include "stdafx.h"
#include <math.h>
#include <MMSystem.h>
#include "kdws_manager.h"
#include "kdws_window.h"
#include "kdws_renderer.h"

#pragma comment(lib,"Winmm.lib")
#pragma comment(lib,"d3d9.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

kdws_manager theApp; // There can only be one application object.

const char *jpip_channel_types[5] =
  {"none","http-udp","http-tcp","http",NULL};


/* ========================================================================= */
/*                           Internal Functions                              */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                presentation_thread_startup                         */
/*****************************************************************************/

static UINT __cdecl presentation_thread_startup(void *param)
{
  kdws_manager *app = (kdws_manager *) param;
  app->presentation_thread_entry();
  return 0;
}

/*****************************************************************************/
/* STATIC                     register_protocols                             */
/*****************************************************************************/

static void
  register_protocols(const char *executable_path)
  /* Sets up the registry entries required to register the JPIP protocol
     variants which are currently supported.  URL's of the form
     <protocol name>:... will be passed into a newly launched instance of
     the present application when clicked within the iExplorer application. */
{
  DWORD disposition;
  HKEY root_key, icon_key, shell_key, open_key, command_key;

  kdws_string executable(executable_path);
  kdws_string description("JPIP Interactive Imaging Protocol");
  kdws_string command_string((int) strlen(executable_path) + 4);
  strcpy(command_string,executable_path);
  strcat(command_string," %1");

  LONG result;
  if ((result =
       RegCreateKeyEx(HKEY_CLASSES_ROOT,_T("jpip"),0,REG_NONE,
                      REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,
                      &root_key,&disposition)) == ERROR_SUCCESS)
    {
      RegSetValueEx(root_key,NULL,0,REG_SZ,
                    (kdu_byte *)((const _TCHAR *)description),
                    (int)(sizeof(_TCHAR)*(strlen(description)+1)));
      RegSetValueEx(root_key,_T("URL Protocol"),0,REG_SZ,
                    (kdu_byte *)(_T("")),1);
      if (RegCreateKeyEx(root_key,_T("DefaultIcon"),0,REG_NONE,
                         REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,
                         &icon_key,&disposition) == ERROR_SUCCESS)
        {
          RegSetValueEx(icon_key,NULL,0,REG_SZ,
                        (kdu_byte *)((const _TCHAR *) executable),
                        (int)(sizeof(_TCHAR)*(strlen(executable)+1)));
          RegCloseKey(icon_key);
        }
      if (RegCreateKeyEx(root_key,_T("shell"),0,REG_NONE,
                         REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,
                         &shell_key,&disposition) == ERROR_SUCCESS)
        {
          if (RegCreateKeyEx(shell_key,_T("open"),0,REG_NONE,
                             REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,
                             &open_key,&disposition) == ERROR_SUCCESS)
            {
              if (RegCreateKeyEx(open_key,_T("command"),0,REG_NONE,
                                 REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,
                                 &command_key,&disposition) == ERROR_SUCCESS)
                {
                  RegSetValueEx(command_key,NULL,0,REG_SZ,(kdu_byte *)
                                ((const _TCHAR *) command_string),
                                (int)(sizeof(_TCHAR) *
                                      (strlen(command_string)+1)));
                  RegCloseKey(command_key);
                }
              RegCloseKey(open_key);
            }
          RegCloseKey(shell_key);
        }
      RegCloseKey(root_key);
    }
}


/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN                 kdws_compare_file_pathnames                        */
/*****************************************************************************/

bool
  kdws_compare_file_pathnames(const char *fname1, const char *fname2)
{
  if ((fname1 == NULL) || (fname2 == NULL))
    return false;
  for (; (*fname1 != '\0') && (*fname2 != '\0'); fname1++, fname2++)
    {
      if (toupper(*fname1) == toupper(*fname2))
        continue;
      if (((*fname1 != '/') && (*fname1 != '\\')) ||
          ((*fname2 != '/') && (*fname2 != '\\')))
        break;
    }
  return (*fname1 == '\0') && (*fname2 == '\0');
}


/* ========================================================================= */
/*                               CAboutDlg Class                             */
/* ========================================================================= */

class CAboutDlg : public CDialog {
  public:
    CAboutDlg() : CDialog(CAboutDlg::IDD) { return; }
	enum { IDD = IDD_ABOUTBOX };
protected:
	DECLARE_MESSAGE_MAP()
  };

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()

/* ========================================================================= */
/*                    Error and Warning Message Handlers                     */
/* ========================================================================= */

/*****************************************************************************/
/* CLASS                     kd_core_message_queue                           */
/*****************************************************************************/

class kd_core_message_queue : public kdu_message_queue {
  /* This object is used to implement error and warning message services
     which are registered globally for use by all threads which may
     use `kdu_error' or `kdu_warning'.  The object is based on the
     functionality offered by the versatile Kakadu core message handling
     class `kdu_message_queue', providing the required application specific
     override for `kdu_message_queue::pop_message' so that all message display
     can be handled in the main window management thread. */
  public: // Member functions
    kd_core_message_queue(bool for_errors)
      {
        this->configure(16,true,for_errors,0);
        main_thread_id = 0; main_thread_msg = 0;
        in_modal_display = false;
      }
    void configure_posting(UINT msg, DWORD thread_id)
      { /* This function should be called when the application is started,
           supplying a `msg' number, to be posted to the message loop of the
           thread with the indicated `thread_id', in the event that an
           error/warning message should arrive on another thread.  */
        this->main_thread_msg = msg;
        this->main_thread_id = thread_id;
        main_thread.set_to_self();
        assert(main_thread_id == GetCurrentThreadId());
      }
    const char *pop_message()
      {
        kdu_thread self;  self.set_to_self();
        if (self != main_thread)
          {
            PostThreadMessage(main_thread_id,main_thread_msg,0,0);
            return NULL;
          }
        const char *text = NULL;
        if ((!in_modal_display) &&
            ((text = kdu_message_queue::pop_message()) != NULL))
          {
            const char *caption = "Kakadu system message";
            const char *sep = strchr(text,'\n');
            char caption_buf[80];
            if ((sep != NULL) && (sep[1] != '\0') && (sep < (text+80)))
              { // Convert the first line into a caption
                memcpy(caption_buf,text,(size_t)(sep-text));
                caption_buf[(int)(sep-text)] = '\0';
                caption = caption_buf; text = sep+1;
              }
            kdws_interrogate_dlg dlg(NULL,caption,text,NULL);
            in_modal_display = true;
            dlg.DoModal();
            in_modal_display = false;
          }
        return text;
      }
  private: // Data
    bool in_modal_display; // Prevent recursive message window creation
    DWORD main_thread_id; // Used to post thread messages
    UINT main_thread_msg;
    kdu_thread main_thread;
  };

static kd_core_message_queue warn_message_queue(false);
static kd_core_message_queue err_message_queue(true);
static kdu_message_formatter warn_formatter(&warn_message_queue,50);
static kdu_message_formatter err_formatter(&err_message_queue,50);
    // Still need to call `warn_message_queue.configure_display_wnd' and
    // `err_message_queue.configure_display_wnd' once a frame window is
    // available.


/* ========================================================================= */
/*                                kdws_string                                */
/* ========================================================================= */

/*****************************************************************************/
/*                    kdws_string::kdws_string (max_chars)                   */
/*****************************************************************************/

kdws_string::kdws_string(int max_chars)
{
  wide_buf_len = max_chars+1;
  utf8_buf_len = max_chars*3 + 1; // Safe upper bound
  wide_buf = NULL;  utf8_buf = NULL;
  wide_buf = new WCHAR[wide_buf_len];
  utf8_buf = new char[utf8_buf_len];
  utf8_buf_valid = wide_buf_valid = false;
}

/*****************************************************************************/
/*                       kdws_string::kdws_string (UTF8)                     */
/*****************************************************************************/

kdws_string::kdws_string(const char *cp)
{
  utf8_buf_len = (int)(::strlen(cp)+1);
  wide_buf_len = 0;
  utf8_buf = new char[utf8_buf_len];
  wide_buf = NULL;
  utf8_buf_valid = true; wide_buf_valid = false;
  memcpy(utf8_buf,cp,(size_t)utf8_buf_len);
}

/*****************************************************************************/
/*                     kdws_string::kdws_string (unicode)                    */
/*****************************************************************************/

kdws_string::kdws_string(const WCHAR *cp)
{
  wide_buf_len = (int)(wcslen(cp)+1);
  utf8_buf_len = 0;
  wide_buf = new WCHAR[wide_buf_len];
  utf8_buf = NULL;
  wide_buf_valid = true; utf8_buf_valid = false;
  memcpy(wide_buf,cp,sizeof(WCHAR)*(size_t)wide_buf_len);
}

/*****************************************************************************/
/*                          kdws_string::set_strlen                          */
/*****************************************************************************/

void kdws_string::set_strlen(int len)
{
  if (wide_buf_valid)
    {
      assert(len < wide_buf_len);
      if (len >= wide_buf_len)
        len = wide_buf_len-1;
      if (len >= 0)
        wide_buf[len] = L'\0';
    }
  if (utf8_buf_valid)
    {
      assert(len < utf8_buf_len);
      if (len >= utf8_buf_len)
        len = utf8_buf_len-1;
      if (len >= 0)
        utf8_buf[len] = '\0';
    }
}

/*****************************************************************************/
/*                            kdws_string::strlen                            */
/*****************************************************************************/

int kdws_string::strlen()
{
  int length = 0;
  if (wide_buf_valid)
    {
      WCHAR *wp = wide_buf;
      while (*(wp++) != L'\0')
        length++;
    }
  else if (utf8_buf_valid)
    {
      char tmp;
      const char *cp = utf8_buf;
      const char *lim_cp = utf8_buf + utf8_buf_len;
      while ((tmp = *cp) != '\0')
        {
          if (tmp < 128)
            cp++;
          else if (tmp < 224)
            cp += 2;
          else if (tmp < 240)
            cp += 3;
          else if (tmp < 248)
            cp += 4;
          else if (tmp < 252)
            cp += 5;
          else
            cp += 6;
          if (cp >= lim_cp)
            break;
          length++;;
        }
    }
  return length;
}

/*****************************************************************************/
/*                       kdws_string::validate_utf8_buf                      */
/*****************************************************************************/

void kdws_string::validate_utf8_buf()
{
  if (utf8_buf_valid)
    return;
  int count_chars = 0;
  if (wide_buf_valid)
    {
      count_chars = wide_buf_len-1;
      while ((count_chars > 0) && (wide_buf[count_chars-1] == L'\0'))
        count_chars--;
    }
  if (utf8_buf == NULL)
    {
      if (utf8_buf_len == 0)
        utf8_buf_len = 3*count_chars+1;
      if (utf8_buf_len < 1)
        utf8_buf_len = 1; // Just in case of overflow
      utf8_buf = new char[utf8_buf_len];
    }
  memset(utf8_buf,0,(size_t)utf8_buf_len);
  char *dp = utf8_buf;
  if (wide_buf_valid)
    {
      const WCHAR *sp=wide_buf;
      char *dp_lim = utf8_buf + utf8_buf_len - 1;
      for (; (count_chars > 0) && (dp < dp_lim); count_chars--, sp++)
        {
          WCHAR val = *sp;
          if (val < 0x80)
            *(dp++) = (char) val;
          else if (val < 0x800)
            {
              if (dp >= (dp_lim+1))
                break;
              *(dp++) = (char) (0xC0 | (val>>6));
              *(dp++) = (char) (0x80 | (val & 0x3F));
            }
          else
            {
              if (dp >= (dp_lim+2))
                break;
              *(dp++) = (char) (0xE0 | (val>>12));
              *(dp++) = (char) (0x80 | ((val>>6) & 0x3F));
              *(dp++) = (char) (0x80 | (val & 0x3F));
            }
        }
    }
  *dp = '\0';
  utf8_buf_valid = true;
}

/*****************************************************************************/
/*                       kdws_string::validate_wide_buf                      */
/*****************************************************************************/

void kdws_string::validate_wide_buf()
{
  if (wide_buf_valid)
    return;
  int count_bytes = 0;
  if (utf8_buf_valid)
    {
      count_bytes = utf8_buf_len-1;
      while ((count_bytes > 0) && (utf8_buf[count_bytes-1] == '\0'))
        count_bytes--;
    }
  if (wide_buf == NULL)
    {
      if (wide_buf_len == 0)
        wide_buf_len = count_bytes+1;
      wide_buf = new WCHAR[wide_buf_len];
    }
  memset(wide_buf,0,sizeof(WCHAR)*(size_t)wide_buf_len);
  WCHAR *dp = wide_buf;
  if (utf8_buf_valid)
    {
      const char *sp = utf8_buf;
      WCHAR *dp_lim = wide_buf + wide_buf_len - 1;
      while ((count_bytes > 0) && (dp < dp_lim))
        {
          int extra_chars = 0;
          WCHAR val, tmp=((WCHAR)(*(sp++))) & 0x00FF;
          if (tmp < 128)
            val = tmp;
          else if  (tmp < 224)
            { val = (tmp-192); extra_chars = 1; }
          else if (tmp < 240)
            { val = (tmp-224); extra_chars = 2; }
          else if (tmp < 248)
            { val = (tmp-240); extra_chars = 3; }
          else if (tmp < 252)
            { val = (tmp-240); extra_chars = 4; }
          else
            { val = (tmp-252); extra_chars = 5; }
          count_bytes -= (1+extra_chars);
          if (count_bytes < 0)
            break;
          for (; extra_chars > 0; extra_chars--)
            {
              val <<= 6;
              val |= (((kdu_uint16)(*(sp++))) & 0x00FF) - 128;
            }
          *(dp++) = val;
        }
    }
  *dp = L'\0';
  wide_buf_valid = true;
}


/* ========================================================================= */
/*                               kdws_settings                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdws_settings::kdws_settings                       */
/*****************************************************************************/

kdws_settings::kdws_settings()
{
  open_save_dir = NULL; open_idx = save_idx = 1;
  jpip_server = jpip_proxy = jpip_cache = NULL;
  jpip_request = jpip_channel = NULL;
  set_jpip_channel_type("http");
  jpip_client_mode = KDU_CLIENT_MODE_INTERACTIVE;
  should_use_cache = false;
}

/*****************************************************************************/
/*                       kdws_settings::~kdws_settings                       */
/*****************************************************************************/

kdws_settings::~kdws_settings()
{
  if (open_save_dir != NULL)
    delete[] open_save_dir;
  if (jpip_server != NULL)
    delete[] jpip_server;
  if (jpip_proxy != NULL)
    delete[] jpip_proxy;
  if (jpip_cache != NULL)
    delete[] jpip_cache;
  if (jpip_request != NULL)
    delete[] jpip_request;
  if (jpip_channel != NULL)
    delete[] jpip_channel;
}

/*****************************************************************************/
/*                      kdws_settings::save_to_registry                      */
/*****************************************************************************/

void
  kdws_settings::save_to_registry(CWinApp *app)
{
  app->WriteProfileString(_T("files"),_T("directory"),
                          kdws_string(get_open_save_dir()));
  app->WriteProfileInt(_T("files"),_T("open-index"),open_idx);
  app->WriteProfileInt(_T("files"),_T("save-index"),save_idx);
  app->WriteProfileString(_T("jpip"),_T("cache-dir"),
                          kdws_string(get_jpip_cache()));
  app->WriteProfileString(_T("jpip"),_T("server"),
                          kdws_string(get_jpip_server()));
  app->WriteProfileString(_T("jpip"),_T("request"),
                          kdws_string(get_jpip_request()));
  app->WriteProfileString(_T("jpip"),_T("channel-type"),
                          kdws_string(get_jpip_channel_type()));
  const _TCHAR *mode_string = _T("interactive");
  switch (jpip_client_mode) {
    case KDU_CLIENT_MODE_AUTO: mode_string=_T("auto");
         break;
    case KDU_CLIENT_MODE_INTERACTIVE: mode_string=_T("interactive");
         break;
    case KDU_CLIENT_MODE_NON_INTERACTIVE: mode_string=_T("non-interactive");
         break;
    default: break;
  }
  app->WriteProfileString(_T("jpip"),_T("client-mode"),mode_string);
  app->WriteProfileInt(_T("jpip"), _T("use-cache"), (should_use_cache)?1:0);
}

/*****************************************************************************/
/*                     kdws_settings::load_from_registry                     */
/*****************************************************************************/

void
  kdws_settings::load_from_registry(CWinApp *app)
{
  set_open_save_dir(
    kdws_string((const _TCHAR *)
                app->GetProfileString(_T("files"),_T("directory"))));
  open_idx = app->GetProfileInt(_T("files"),_T("open-index"),1);
  save_idx = app->GetProfileInt(_T("files"),_T("save-index"),1);
  CString missing_cache_dir = _T("***");
  CString existing_cache_dir =
    app->GetProfileString(_T("jpip"),_T("cache-dir"),missing_cache_dir);
  if (existing_cache_dir.Compare(missing_cache_dir) == 0)
    {
      kdws_string temp_path(MAX_PATH);
      int len = GetTempPath(MAX_PATH,temp_path);
      if (len > 0)
        set_jpip_cache(temp_path);
    }
  else
    set_jpip_cache(kdws_string((const _TCHAR *) existing_cache_dir));
  set_jpip_server(
    kdws_string((const _TCHAR *)
                app->GetProfileString(_T("jpip"),_T("server"))));
  set_jpip_request(
    kdws_string((const _TCHAR *)
                app->GetProfileString(_T("jpip"),_T("request"))));
  kdws_string c_string((const _TCHAR *)
                       app->GetProfileString(_T("jpip"),_T("channel-type")));
  if (c_string.is_empty())
    set_jpip_channel_type("http");
  else
    set_jpip_channel_type(c_string);

  kdws_string mode_string((const _TCHAR *)
                          app->GetProfileString(_T("jpip"),_T("client-mode")));
  const char *string = (const char *) mode_string;
  jpip_client_mode = KDU_CLIENT_MODE_INTERACTIVE;
  if (strcmp(string,"auto") == 0)
    jpip_client_mode = KDU_CLIENT_MODE_AUTO;
  else if (strcmp(string,"non-interactive") == 0)
    jpip_client_mode = KDU_CLIENT_MODE_NON_INTERACTIVE;

  should_use_cache =
    (app->GetProfileInt(_T("jpip"), _T("use-cache"), 1) != 0);
}


/*===========================================================================*/
/*                           kdws_frame_presenter                            */
/*===========================================================================*/

/*****************************************************************************/
/*                kdws_frame_presenter::kdws_frame_presenter                 */
/*****************************************************************************/

kdws_frame_presenter::kdws_frame_presenter(kdws_manager *manager,
                                           kdws_notification_manager *notifier,
                                           double event_interval,
                                           kdws_frame_window *wnd)
{
  this->manager = manager;
  this->notification_manager = notifier;
  this->window = wnd;
  drawing_mutex.create();
  target = NULL;
  swap_chain = NULL;
  surface = NULL;
  wants_swap_buffers = false;
  display_event_interval = event_interval;
  waiting_for_next_display_event_time = -1.0;
}

/*****************************************************************************/
/*                kdws_frame_presenter::~kdws_frame_presenter                */
/*****************************************************************************/

kdws_frame_presenter::~kdws_frame_presenter()
{
  if (surface != NULL)
    { 
      surface->Release();
      surface = NULL;
    }
  if (swap_chain != NULL)
    { 
      swap_chain->Release();
      swap_chain = NULL;
    }
  drawing_mutex.destroy();
}

/*****************************************************************************/
/*                      kdws_frame_presenter::enable                         */
/*****************************************************************************/

double
  kdws_frame_presenter::enable(kdws_renderer *renderer, HWND image_view_hwnd)
{
  assert((swap_chain == NULL) && (surface == NULL) && (target == NULL));
  IDirect3DSwapChain9 *new_swap_chain = NULL;
  IDirect3DSurface9 *new_surface = NULL;
  kdu_coords new_backbuffer_size;

  new_swap_chain =
    manager->create_swap_chain(image_view_hwnd,new_backbuffer_size);
  if (new_swap_chain == NULL)
    return -1.0;

  new_surface = manager->create_surface(new_backbuffer_size);
  if (new_surface == NULL)
    { // Surface creation failed
      new_swap_chain->Release();
      return -1.0;
    }

  drawing_mutex.lock();
  this->target = renderer;
  this->swap_chain = new_swap_chain;
  this->surface = new_surface;
  this->backbuffer_size = new_backbuffer_size;
  this->wants_swap_buffers = false;
  this->waiting_for_next_display_event_time = -1.0;
  drawing_mutex.unlock();
  double result = -1.0;
  double cum_sleep_time = 0.0;
  do {
    Sleep(10); cum_sleep_time += 0.01; // Just slept for 10ms.
    drawing_mutex.lock();
    result = waiting_for_next_display_event_time;
    drawing_mutex.unlock();
  } while ((result < 0.0) && (cum_sleep_time < 1.0));
  if (result < 0.0)
    disable();
  return result;
}

/*****************************************************************************/
/*                      kdws_frame_presenter::resize                         */
/*****************************************************************************/

bool kdws_frame_presenter::resize(HWND image_view_hwnd)
{
  if ((target == NULL) || (swap_chain == NULL))
    return true;

  kdu_coords tmp_backbuffer_size;
  RECT client_rect;
  if (GetClientRect(image_view_hwnd,&client_rect))
    { // See if we can avoid making any changes
      tmp_backbuffer_size.x = client_rect.right - client_rect.left;
      tmp_backbuffer_size.y = client_rect.bottom - client_rect.top;
      if (tmp_backbuffer_size == this->backbuffer_size)
        return true;
    }

  // First remove the existing DirectX resources
  IDirect3DSwapChain9 *tmp_swap_chain = NULL;
  IDirect3DSurface9 *tmp_surface = NULL;
  drawing_mutex.lock();
  if (surface != NULL)
    { tmp_surface = surface; surface = NULL; }
  if (swap_chain != NULL)
    { tmp_swap_chain = swap_chain; swap_chain = NULL; }
  backbuffer_size = kdu_coords();
  wants_swap_buffers = false;
  drawing_mutex.unlock();

  if (tmp_swap_chain != NULL)
    tmp_swap_chain->Release();
  if (tmp_surface != NULL)
    tmp_surface->Release();
  tmp_swap_chain = NULL;
  tmp_surface = NULL;

  // Now create some more DirectX resources
  tmp_swap_chain =
    manager->create_swap_chain(image_view_hwnd,tmp_backbuffer_size);
  if (tmp_swap_chain == NULL)
    return false;
  tmp_surface = manager->create_surface(tmp_backbuffer_size);
  if (tmp_surface == NULL)
    { // Surface creation failed
      tmp_swap_chain->Release();
      return false;
    }

  // Install the new resources
  drawing_mutex.lock();
  this->swap_chain = tmp_swap_chain;
  this->surface = tmp_surface;
  this->backbuffer_size = tmp_backbuffer_size;
  drawing_mutex.unlock();

  return true;
}

/*****************************************************************************/
/*                      kdws_frame_presenter::disable                        */
/*****************************************************************************/

void kdws_frame_presenter::disable()
{
  drawing_mutex.lock();
  target = NULL;
  wants_swap_buffers = false;
  assert(surface_dc.m_hDC == NULL);
  if (surface != NULL)
    { 
      surface->Release();
      surface = NULL;
    }
  if (swap_chain != NULL)
    { 
      swap_chain->Release();
      swap_chain = NULL;
    }
  this->backbuffer_size = kdu_coords();
  drawing_mutex.unlock();
}

/*****************************************************************************/
/*                 kdws_frame_presenter::draw_pending_frame                  */
/*****************************************************************************/

bool kdws_frame_presenter::draw_pending_frame(double display_event_time,
                                              double next_display_event_time)
{
  if (target == NULL)
    return false; // Saves wasting time locking the mutex
  drawing_mutex.lock();
  wants_swap_buffers = false;
  if (target != NULL)
    { 
      if (waiting_for_next_display_event_time < 0.0)
        waiting_for_next_display_event_time = next_display_event_time;
      try {
        if (target->present_queued_frame_buffer(display_event_time,
                                                next_display_event_time))
          { 
            wants_swap_buffers = true;
            notification_manager->notify_display_change();
          }
      } catch (...) {}
    }
  drawing_mutex.unlock();
  return wants_swap_buffers;
}

/*****************************************************************************/
/*                    kdws_frame_presenter::swap_buffers                     */
/*****************************************************************************/

bool kdws_frame_presenter::swap_buffers()
{
  if ((target == NULL) || !wants_swap_buffers)
    return false;
  drawing_mutex.lock();
  bool result = false;
  if (wants_swap_buffers)
    { 
      HRESULT hres;
      hres = swap_chain->Present(NULL,NULL,NULL,NULL,0);
      if (hres == D3D_OK)
        result = true;
    }
  wants_swap_buffers = false;
  drawing_mutex.unlock();
  return result;
}

/*****************************************************************************/
/*                   kdws_frame_presenter::access_surface                    */
/*****************************************************************************/

CDC *kdws_frame_presenter::access_surface(kdu_coords &buffer_size)
{
  assert(surface_dc.m_hDC == NULL);
  if (surface == NULL)
    return NULL;
  HDC hdc = NULL;
  if (surface->GetDC(&hdc) != D3D_OK)
    return NULL;
  surface_dc.Attach(hdc);
  buffer_size = backbuffer_size;
  return &surface_dc;
}

/*****************************************************************************/
/*                   kdws_frame_presenter::release_surface                   */
/*****************************************************************************/

void kdws_frame_presenter::release_surface()
{
  assert((surface_dc.m_hDC != NULL) && (surface != NULL));
  HDC hdc = surface_dc.Detach();
  surface->ReleaseDC(hdc);
  IDirect3DSurface9 *backbuffer = NULL;
  swap_chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&backbuffer);
  IDirect3DDevice9 *device = NULL;
  swap_chain->GetDevice(&device);
  if ((backbuffer != NULL) && (device != NULL))
    { // Transfer `surface' to `backbuffer'
      RECT source_rect;
      source_rect.left = source_rect.top = 0;
      source_rect.right = backbuffer_size.x;
      source_rect.bottom = backbuffer_size.y;
      POINT dest_point;
      dest_point.x = dest_point.y = 0;
      device->UpdateSurface(surface,&source_rect,backbuffer,&dest_point);
    }
}


/* ========================================================================= */
/*                         kdws_notification_manager                         */
/* ========================================================================= */

/*****************************************************************************/
/*            kdws_notification_manager::kdws_notification_manager           */
/*****************************************************************************/

kdws_notification_manager::kdws_notification_manager()
{
  window = NULL;
  main_thread_id = 0;
  state.set(0);
  jpip_client_notifier = NULL;
  next = NULL;
}

/*****************************************************************************/
/*                    kdws_notification_manager::init                        */
/*****************************************************************************/

void kdws_notification_manager::init(kdws_window_list *wnd,
                                     DWORD main_thread_identity)
{
  this->window = wnd;
  this->main_thread_id = main_thread_identity;
}

/*****************************************************************************/
/*             kdws_notification_manager::notify_display_change              */
/*****************************************************************************/

void kdws_notification_manager::notify_display_change()
{
  kdu_int32 old_state = state.exchange_or(KDWS_NOTIFICATION_DISPLAY);
  ::PostThreadMessage(main_thread_id,WM_NULL,0,0); // Reignite the message pump
}

/*****************************************************************************/
/*               kdws_notification_manager::notify_jpip_change               */
/*****************************************************************************/

void kdws_notification_manager::notify_jpip_change()
{
  kdu_int32 old_state = state.exchange_or(KDWS_NOTIFICATION_JPIP);
  ::PostThreadMessage(main_thread_id,WM_NULL,0,0); // Reignite the message pump
}

/*****************************************************************************/
/*           kdws_notification_manager::notify_processing_needed             */
/*****************************************************************************/

void kdws_notification_manager::notify_processing_needed()
{
  kdu_int32 old_state = state.exchange_or(KDWS_NOTIFICATION_PROCESSING);
  ::PostThreadMessage(main_thread_id,WM_NULL,0,0); // Reignite the message pump
}

/*****************************************************************************/
/*             kdws_notification_manager::process_notifications              */
/*****************************************************************************/

bool kdws_notification_manager::process_notifications(bool idle_context)
{
  kdu_int32 notes = state.exchange(0);
  if (idle_context)
    notes |= KDWS_NOTIFICATION_PROCESSING;
  if ((window == NULL) || (notes == 0))
    return false;
  if ((notes & KDWS_NOTIFICATION_DISPLAY) &&
      window->wnd->display_notification())
    notes |= KDWS_NOTIFICATION_PROCESSING;
  if ((notes & KDWS_NOTIFICATION_JPIP) &&
      window->wnd->client_notification())
    notes |= KDWS_NOTIFICATION_PROCESSING;
  if ((notes & KDWS_NOTIFICATION_PROCESSING) &&
      window->wnd->do_processing())
    this->notify_processing_needed();
  return true;
}


/* ========================================================================== */
/*                       kdws_manager Class Implementation                    */
/* ========================================================================== */

/******************************************************************************/
/*                         kdws_manager::kdws_manager                         */
/******************************************************************************/

kdws_manager::kdws_manager()
{
  idle_hook_handle = NULL; // Install the foreground idle hook in InitInstance
  main_thread_id = 0; // Set the correct value in InitInstance

  windows = NULL;
  next_window_identifier = 1;
  last_known_key_wnd = NULL;
  broadcast_actions_once = false;
  broadcast_actions_indefinitely = false;

  wakeup_timer_id = 0;
  next_window_to_wake = NULL;
  will_check_best_window_to_wake = false;
  
  window_list_change_mutex.create();
  open_file_list = NULL;
  
  reset_placement_engine();

  direct3d = NULL;
  device3d_hwnd = NULL;
  device3d = NULL;
  mm_timer_resolution = 0;
  presentation_thread = NULL;
  presentation_thread_exited = false;
  presentation_thread_exit_requested = false;
  presentation_timer = NULL;
  display_event_interval = next_display_event_time = 0.0;
  next_display_event_abs_usecs = display_interval_usecs = 0;
}

/******************************************************************************/
/*                        kdws_manager::~kdws_manager                         */
/******************************************************************************/

kdws_manager::~kdws_manager()
{
  if (presentation_thread != NULL)
    { 
      presentation_thread_exit_requested = true;
      WaitForSingleObject(presentation_thread->m_hThread,INFINITE);
      delete presentation_thread;
      presentation_thread = NULL;
    }
  if (presentation_timer != NULL)
    {
      CloseHandle(presentation_timer);
      presentation_timer = NULL;
    }
  while (windows != NULL)
    { 
      kdws_window_list *elt = windows;  windows = elt->next;
      delete elt;
    }
  kdws_open_file_record *frec;
  while ((frec = open_file_list) != NULL)
    {
      open_file_list = frec->next;
      delete frec;
    }
  window_list_change_mutex.destroy();
}

/******************************************************************************/
/*                  kdws_manager::presentation_thread_entry                   */
/******************************************************************************/

void kdws_manager::presentation_thread_entry()
{
  // Start by creating the DirectX device.  We do this in the presentation
  // thread, since on some platforms it can take a second or so to complete
  // and we don't want to hold up launch of the application.
  if (direct3d == NULL)
    direct3d = Direct3DCreate9(D3D_SDK_VERSION);
  if ((direct3d != NULL) && (device3d == NULL))
    { 
      if (device3d_hwnd == NULL)
        device3d_hwnd = CreateWindow(AfxRegisterWndClass(0),
                                     _T("kdu_winshow_d3di"),
                                     WS_THICKFRAME,
                                     0,0,100,100,NULL,
                                     NULL,NULL,NULL);
      if (device3d_hwnd != NULL)
        { 
          D3DPRESENT_PARAMETERS d3dpp;
          memset(&d3dpp,0,sizeof(d3dpp));
          d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
          d3dpp.hDeviceWindow = device3d_hwnd;
          d3dpp.Windowed = TRUE;
          d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
          d3dpp.BackBufferCount = 1;
          d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
          d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
          d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
          direct3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,
                                 device3d_hwnd,
                                 D3DCREATE_MIXED_VERTEXPROCESSING,
                                 &d3dpp,&device3d);
        }
    }

  while ((device3d != NULL) && !presentation_thread_exit_requested)
    { 
      // Start by finding a good time to draw pending frames for the
      // `next_display_event_time'
      kdu_long next_draw_abs_usecs = next_display_event_abs_usecs;
      next_draw_abs_usecs -= display_interval_usecs;
      next_draw_abs_usecs += display_interval_usecs >> 2;

      // Wait until this good draw point
      kdu_long cur_abs_usecs =
        presentation_absolute_time.get_ellapsed_microseconds();
      while (cur_abs_usecs > next_draw_abs_usecs)
        { 
          next_draw_abs_usecs += display_interval_usecs;
          next_display_event_abs_usecs += display_interval_usecs;
          next_display_event_time += display_event_interval;
        }
      kdu_long delay_usecs = next_draw_abs_usecs - cur_abs_usecs;
      if (delay_usecs > 1000)
        presentation_thread_wait(delay_usecs);

      // Lock the window list to prevent changes
      window_list_change_mutex.lock();

      // Draw any pending frames
      double event_time = next_display_event_time;
      double next_event_time = event_time + display_event_interval;
      bool need_swap_buffers = false;
      kdws_window_list *elt;
      for (elt=windows; elt != NULL; elt=elt->next)
        { // Give every window a chance to draw a frame
          if (elt->frame_presenter->draw_pending_frame(event_time,
                                                       next_event_time))
            need_swap_buffers = true;
        }

      bool did_swap_buffers = false;
      kdu_long diff_usecs = 0;
      if (need_swap_buffers)
        { 
          // First, wait a little while until we get close to where the
          // vertical blank is supposedly located
          cur_abs_usecs =
            presentation_absolute_time.get_ellapsed_microseconds();
          delay_usecs = next_display_event_abs_usecs - cur_abs_usecs;
          if (delay_usecs > 4000)
            presentation_thread_wait(delay_usecs);
          for (elt=windows; elt != NULL; elt = elt->next)
            if (elt->frame_presenter->swap_buffers())
              did_swap_buffers = true;
          kdu_long new_abs_usecs =
            presentation_absolute_time.get_ellapsed_microseconds();
          diff_usecs = new_abs_usecs-next_display_event_abs_usecs; // Ideally,
                                        // this difference will be close to 0.
          while (diff_usecs > (display_interval_usecs>>1))
            { // We seem to have lost one or more whole display intervals
              next_display_event_time += display_event_interval;
              next_display_event_abs_usecs += display_interval_usecs;
              diff_usecs -= display_interval_usecs;
            }
        }
      if (did_swap_buffers)
        next_display_event_abs_usecs += diff_usecs >> 4; // Make small changes
            // to the value of the absolute display event time, measured in
            // usecs, so that it gradually comes into sync with VBlank
      next_display_event_time += display_event_interval;
      next_display_event_abs_usecs += display_interval_usecs;

      // Release the window list to allow windows to be created/destroyed
      window_list_change_mutex.unlock();
  }

  // Clean up all DirectX resources
  if (device3d != NULL)
    { 
      device3d->Release();
      device3d = NULL;
    }
  if (device3d_hwnd != NULL)
    { 
      DestroyWindow(device3d_hwnd);
      device3d_hwnd = NULL;
    }
  if (direct3d != NULL)
    { 
      direct3d->Release();
      direct3d = NULL;
    }
  presentation_thread_exited = true;
}

/******************************************************************************/
/*                  kdws_manager::application_can_terminate                   */
/******************************************************************************/

bool kdws_manager::application_can_terminate()
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (!elt->wnd->application_can_terminate())
      return false;
  return true;
}

/******************************************************************************/
/*            kdws_manager::send_application_terminating_messages             */
/******************************************************************************/

void kdws_manager::send_application_terminating_messages()
{
  kdws_window_list *elt, *next;
  for (elt=windows; elt != NULL; elt=next)
    {
      next = elt->next;
      elt->wnd->application_terminating();
    }
  for (elt=windows; elt != NULL; elt=next)
    {
      next = elt->next;
      elt->wnd->DestroyWindow();
    }
}

/*****************************************************************************/
/*                   kdws_manager::reset_placement_engine                    */
/*****************************************************************************/

void kdws_manager::reset_placement_engine()
{
  cycle_origin = next_window_pos = kdu_coords(10,10);
  next_window_row = cycle_origin.y;  
}

/*****************************************************************************/
/*                        kdws_manager::place_window                         */
/*****************************************************************************/

bool kdws_manager::place_window(kdws_frame_window *wnd,
                                kdu_coords frame_size,
                                bool do_not_place_again,
                                bool placing_first_empty_window)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      break;
  if (elt != NULL)
    {
      if (elt->window_placed && do_not_place_again)
        return false;
      if (!placing_first_empty_window)
        elt->window_placed = true;
    }
  kdu_dims screen_rect;
  screen_rect.size.x = GetSystemMetrics(SM_CXSCREEN);
  screen_rect.size.y = GetSystemMetrics(SM_CYSCREEN);
  
  // Determine `frame_rect' as it sits within the `screen_rect', with the
  // origin at the top left hand corner of the screen rectangle.
  kdu_dims frame_rect;
  frame_rect.pos = next_window_pos;
  frame_rect.size = frame_size;
  int h_delta = screen_rect.size.x - frame_rect.size.x; // Hor. placement slack
  if (frame_rect.pos.x > h_delta)
    { // Window does not fit horizontally
      if (frame_rect.pos.x == cycle_origin.x)
        { // This is the first attempt to place a window on this row
          frame_rect.pos.x = h_delta; // Shift left as little as possible
        }
      else
        { // Move down to the next window placement row
          frame_rect.pos.x = cycle_origin.x;
          frame_rect.pos.y = next_window_row;
          if (frame_rect.pos.x > h_delta)
            frame_rect.pos.x = h_delta; // Shift left as little as possible
        }
    }
  int v_delta = screen_rect.size.y-frame_rect.size.y; // Vert. placement slack
  if (frame_rect.pos.y > v_delta)
    { // Window does not fit vertically
      if (frame_rect.pos.y == cycle_origin.y)
        { // This is the first row of windows for the cycle
          frame_rect.pos.y = v_delta; // Shift window up as little as possible
        }
      else
        { // Start a new cycle
          cycle_origin.x += 50;
          cycle_origin.y += 50;
          while (cycle_origin.y > (screen_rect.size.y>>2))
            cycle_origin.y -= (screen_rect.size.y>>2);
          while (cycle_origin.x > (screen_rect.size.x>>2))
            cycle_origin.x -= (screen_rect.size.x>>2);
          frame_rect.pos.y = next_window_row = cycle_origin.y;
          frame_rect.pos.x = cycle_origin.x;
          if (frame_rect.pos.y > v_delta)
            frame_rect.pos.y = v_delta;
          if (frame_rect.pos.x > h_delta)
            frame_rect.pos.x = h_delta;
        }
    }

  if (frame_rect.pos.x < 0)
    frame_rect.pos.x = 0; // Just in case
  if (frame_rect.pos.y < 0)
    frame_rect.pos.y = 0; // Just in case
  
  // Determine the next window placement location
  if (!placing_first_empty_window)
    {
      next_window_pos = frame_rect.pos;
      next_window_pos.x += frame_rect.size.x + 10; // Leave a small gap.
      if ((v_delta =
           (frame_rect.pos.y+frame_rect.size.y+10)-next_window_row) > 0)
        next_window_row += v_delta;
    }
  
  // Actually place and display the window
  wnd->SetWindowPos(NULL,frame_rect.pos.x,frame_rect.pos.y,
                    frame_rect.size.x,frame_rect.size.y,
                    SWP_NOZORDER|SWP_SHOWWINDOW);
  return true;
}

/******************************************************************************/
/*                          kdws_manager::add_window                          */
/******************************************************************************/

void kdws_manager::add_window(kdws_frame_window *wnd)
{
  kdws_window_list *elt, *tail=NULL;
  for (elt=windows; elt != NULL; tail=elt, elt=elt->next)
    if (elt->wnd == wnd)
      return;
  elt = new kdws_window_list;
  elt->wnd = wnd;
  elt->window_identifier = next_window_identifier++;
  elt->file_or_url_name = NULL; // Until we have one
  elt->wakeup_time = -1.0;
  elt->window_empty = true;
  elt->window_placed = false;
  elt->notification_manager.init(elt,main_thread_id);
  elt->frame_presenter =
    new kdws_frame_presenter(this,&(elt->notification_manager),
                             display_event_interval,wnd);
  window_list_change_mutex.lock();
  elt->next = NULL;
  if ((elt->prev = tail) == NULL)
    windows = elt;
  else
    tail->next = elt;
  window_list_change_mutex.unlock();
  if (m_pMainWnd == NULL)
    m_pMainWnd = wnd;
}

/******************************************************************************/
/*                        kdws_manager::remove_window                         */
/******************************************************************************/

void kdws_manager::remove_window(kdws_frame_window *wnd)
{
  if (wnd == last_known_key_wnd)
    last_known_key_wnd = NULL;
  
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      break;
  assert(elt != NULL);
  if (elt == NULL)
    return;
  elt->notification_manager.init(NULL,0);

  window_list_change_mutex.lock();

  if (elt->frame_presenter != NULL)
    {
      elt->frame_presenter->disable(); // Just in case
      delete elt->frame_presenter;
      elt->frame_presenter = NULL;
    }
  
  if (elt->prev == NULL)
    windows = elt->next;
  else
    elt->prev->next = elt->next;
  if (elt->next != NULL)
    elt->next->prev = elt->prev;
  window_list_change_mutex.unlock();

  if (elt->file_or_url_name != NULL)
    { 
      kdws_open_file_record *fscan;
      for (fscan=open_file_list; fscan != NULL; fscan=fscan->next)
        if (elt->file_or_url_name == fscan->open_url)
          fscan->client_notifier->release_window(&elt->notification_manager);
    }
  
  delete elt;
  if ((m_pMainWnd == wnd) && (windows != NULL))
    m_pMainWnd = windows->wnd;
      // If there are no more windows, `m_pMainWnd' will remain NULL,
      // which will cause the main run-loop to terminate the application,
      // passing through `ExitInstance'.  If we really want to keep the
      // application alive beyond this point, we have to create a new
      // empty window.
  
  if (windows == NULL)
    reset_placement_engine();
  if (elt == next_window_to_wake)
    {
      next_window_to_wake = NULL;
      install_next_scheduled_wakeup();
    }
}

/*****************************************************************************/
/*                       kdws_manager::get_access_idx                        */
/*****************************************************************************/

int kdws_manager::get_access_idx(kdws_frame_window *wnd)
{
  kdws_window_list *elt;
  int idx = 0;
  for (elt=windows; (elt != NULL) && (elt->wnd != wnd); idx++, elt=elt->next);
  return (elt==NULL)?-1:idx;
}

/*****************************************************************************/
/*                        kdws_manager::access_window                        */
/*****************************************************************************/

kdws_frame_window *kdws_manager::access_window(int idx)
{
  kdws_window_list *elt;
  for (elt=windows; (elt != NULL) && (idx > 0); idx--, elt=elt->next);
  return ((elt==NULL) || (idx != 0))?NULL:elt->wnd;
}

/*****************************************************************************/
/*                    kdws_manager::get_window_identifier                    */
/*****************************************************************************/

int kdws_manager::get_window_identifier(kdws_frame_window *wnd)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      return elt->window_identifier;
  return 0;
}

/*****************************************************************************/
/*                    kdws_manager::declare_window_empty                     */
/*****************************************************************************/

void kdws_manager::declare_window_empty(kdws_frame_window *wnd, bool empty)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      elt->window_empty = empty;
}

/*****************************************************************************/
/*                       kdws_manager::find_empty_window                     */
/*****************************************************************************/

kdws_frame_window *kdws_manager::find_empty_window()
{
  kdws_frame_window *result = NULL;
  kdws_window_list *best_elt = NULL;
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->window_empty &&
        ((best_elt == NULL) ||
         (best_elt->window_identifier < elt->window_identifier)))
      {
        best_elt = elt;
        result = elt->wnd;
      }
  return result;
}

/*****************************************************************************/
/*                   kdws_manager::get_next_action_window                    */
/*****************************************************************************/

kdws_frame_window *kdws_manager::get_next_action_window(kdws_frame_window *wnd)
{
  if (!(broadcast_actions_once || broadcast_actions_indefinitely))
    return NULL;
  if (!last_known_key_wnd)
    {
      if (wnd->is_key_window())
        last_known_key_wnd = wnd;
      else
        { // Initiating menu action from non-key window?? Safest to stop here.
          broadcast_actions_once = false;
          return NULL;
        }
    }

  kdws_window_list *elt = windows;
  for (; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      break;
  if (elt == NULL)
    { // Should not happen!  Safest to cancel any action broadcast
      last_known_key_wnd = NULL;
      broadcast_actions_once = false;
      return NULL;
    }
  elt = elt->next;
  if (elt == NULL)
    elt = windows;
  kdws_frame_window *result = elt->wnd;
  
  if (result == last_known_key_wnd)
    { // We have been right around already
      last_known_key_wnd = NULL;
      broadcast_actions_once = false;
      return NULL;
    }
  
  return result;
}

/*****************************************************************************/
/*                   kdws_manager::set_action_broadcasting                   */
/*****************************************************************************/

void kdws_manager::set_action_broadcasting(bool broadcast_once,
                                           bool broadcast_indefinitely)
{
  this->broadcast_actions_once = broadcast_once;
  this->broadcast_actions_indefinitely = broadcast_indefinitely;
  last_known_key_wnd = NULL;
}

/*****************************************************************************/
/*               kdws_manager::broadcast_playclock_adjustment                */
/*****************************************************************************/

void kdws_manager::broadcast_playclock_adjustment(double delta)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    elt->wnd->adjust_playclock(delta);
}

/*****************************************************************************/
/* STATIC              kdws_manager::idle_hook_callback                      */
/*****************************************************************************/

LRESULT CALLBACK
  kdws_manager::idle_hook_callback(int code, WPARAM wParam, LPARAM lParam)
{
  theApp.do_idle_processing();
  return CallNextHookEx(theApp.idle_hook_handle,code,wParam,lParam);
}

/*****************************************************************************/
/*                     kdws_manager::do_idle_processing                      */
/*****************************************************************************/

void kdws_manager::do_idle_processing()
{
  // Make sure we didn't lose any important messages that may have been
  // posted to a full thread queue while we were busy.
  while (err_message_queue.pop_message() != NULL);
  while (warn_message_queue.pop_message() != NULL);
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    elt->notification_manager.process_notifications(true);
}

/*****************************************************************************/
/*                        kdws_manager::schedule_wakeup                      */
/*****************************************************************************/

void
  kdws_manager::schedule_wakeup(kdws_frame_window *wnd, double wakeup_time)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      break;
  if (elt == NULL)
    return; // Should never happen
  elt->wakeup_time = wakeup_time;
  if (will_check_best_window_to_wake)
    { // We will get around to considering this wakeup when we get back into
      // the ongoing call to `install_next_scheduled_wakeup'.
      assert(next_window_to_wake == NULL);
      return;
    }
  if (wakeup_time < 0.0)
    {
      if (next_window_to_wake == elt)
        {
          next_window_to_wake = NULL;
          install_next_scheduled_wakeup();
        }
    }
  else if ((next_window_to_wake == NULL) ||
           (next_window_to_wake == elt) ||
           (wakeup_time < next_window_to_wake->wakeup_time))
    {
      next_window_to_wake = elt;
      set_wakeup_time(wakeup_time,-1.0);
    }
}

/*****************************************************************************/
/*                kdws_manager::install_next_scheduled_wakeup                */
/*****************************************************************************/

void kdws_manager::install_next_scheduled_wakeup()
{
  next_window_to_wake = NULL; // Just in case
  will_check_best_window_to_wake = true;
      // Avoids accidental installation of a new scheduled wakeup while we
      // are scanning for one.  This could happen if `schedule_wakeup' is
      // called from within a window's `wakeup' function.
  
  double current_time = -1.0; // Evaluate only if we need it
  kdws_window_list *elt, *earliest_elt = NULL;
  while (earliest_elt == NULL)
    {
      for (elt=windows; elt != NULL; elt=elt->next)
        if (elt->wakeup_time >= 0.0)
          {
            if (earliest_elt == NULL)
              {
                current_time = get_current_time();
                earliest_elt = elt;
              }
            else if (elt->wakeup_time < earliest_elt->wakeup_time)
              earliest_elt = elt;
          }
      if (earliest_elt == NULL)
        { // Nothing left to wakeup
          will_check_best_window_to_wake = false;
          if (wakeup_timer_id != 0)
            {
              KillTimer(NULL,wakeup_timer_id);
              wakeup_timer_id = 0;
            }
          return;
        }
      if (earliest_elt->wakeup_time <= current_time)
        {
          double scheduled_time = earliest_elt->wakeup_time;
          earliest_elt->wakeup_time = -1.0;
          if (scheduled_time >= 0.0)
            {
              earliest_elt->wnd->wakeup(scheduled_time,current_time);
              current_time = -1.0; // Some time may have been consumed
            }
          earliest_elt = NULL;
        }
    }

  // If we get here, we are ready to wait for `earliest_elt'
  next_window_to_wake = earliest_elt;
  set_wakeup_time(earliest_elt->wakeup_time,current_time);
  will_check_best_window_to_wake = false;
}

/*****************************************************************************/
/*                  kdws_manager::process_scheduled_wakeup                   */
/*****************************************************************************/

void kdws_manager::process_scheduled_wakeup()
{
  kdws_window_list *elt = next_window_to_wake;
  if (elt == NULL)
    return;
  double current_time = get_current_time();
  double scheduled_time = elt->wakeup_time;
  elt->wakeup_time = -1.0; // Cancel further wakeups
  next_window_to_wake = NULL;
  will_check_best_window_to_wake = true; // Prevents scheduling a new
      // timer event from within `schedule_wakeup' if it is called from within
      // the following `wakeup' call before getting a chance to choose the
      // best window to time, in the ensuing call to
      // `install_next_scheduled_wakeup'.
  if (scheduled_time >= 0.0)
    elt->wnd->wakeup(scheduled_time,current_time);
  install_next_scheduled_wakeup();
}

/*****************************************************************************/
/* STATIC              kdws_manager::wakeup_timer_callback                   */
/*****************************************************************************/

void CALLBACK
  kdws_manager::wakeup_timer_callback(HWND hwnd, UINT msg, UINT_PTR event_id,
                                      DWORD time)
{
  theApp.process_scheduled_wakeup();
}

/*****************************************************************************/
/*                       kdws_manager::set_wakeup_time                       */
/*****************************************************************************/

void kdws_manager::set_wakeup_time(double abs_time, double current_time)
{
  if (wakeup_timer_id != 0)
    { KillTimer(NULL,wakeup_timer_id); wakeup_timer_id = 0; }
  if (current_time <= 0.0)
    current_time = get_current_time();
  double wait_secs = abs_time - current_time;
  if (wait_secs <= 0.0)
    { // Directly issue the WM_TIMER message ourself!
      ::PostThreadMessage(main_thread_id,WM_TIMER,
                          0,(LPARAM)wakeup_timer_callback);
    }
  else
    {
      UINT wait_msecs = (UINT)(wait_secs * 1000.0);
      wait_msecs += 2; // Wait a bit longer (1.5 ms on average) for efficiency
      wakeup_timer_id = ::SetTimer(NULL,wakeup_timer_id,wait_msecs,
                                   wakeup_timer_callback);
    }
}

/*****************************************************************************/
/*                      kdws_manager::get_frame_presenter                    */
/*****************************************************************************/

kdws_frame_presenter *
  kdws_manager::get_frame_presenter(kdws_frame_window *wnd)
{
  kdws_window_list *elt;
  for (elt=windows; elt != NULL; elt=elt->next)
    if (elt->wnd == wnd)
      return elt->frame_presenter;
  return NULL;
}

/*****************************************************************************/
/*                      kdws_manager::create_swap_chain                      */
/*****************************************************************************/

IDirect3DSwapChain9 *
  kdws_manager::create_swap_chain(HWND hwnd, kdu_coords &backbuffer_size)
{
  if (device3d == NULL)
    { // Presentation thread may still be in the process of creating `device3d'
      double cum_sleep_time = 0.0;
      while ((device3d == NULL) && !presentation_thread_exited)
        { 
          if (cum_sleep_time > 1.0)
            return NULL; // Give up
          Sleep(10); cum_sleep_time += 0.01; // Just slept for 10ms.
        }
    }

  D3DPRESENT_PARAMETERS d3dpp;
  memset(&d3dpp,0,sizeof(d3dpp));
  d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
  d3dpp.hDeviceWindow = hwnd;
  d3dpp.Windowed = TRUE;
  d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
  d3dpp.BackBufferCount = 1;
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  d3dpp.Flags = D3DPRESENTFLAG_VIDEO;
  d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

  IDirect3DSwapChain9 *result = NULL;
  if (device3d != NULL)
    { 
      HRESULT hr = device3d->CreateAdditionalSwapChain(&d3dpp,&result);
      if (hr != D3D_OK)
        return NULL;
      backbuffer_size.x = (int) d3dpp.BackBufferWidth;
      backbuffer_size.y = (int) d3dpp.BackBufferHeight;
    }
  return result;
}

/*****************************************************************************/
/*                       kdws_manager::create_surface                        */
/*****************************************************************************/

IDirect3DSurface9 *
  kdws_manager::create_surface(kdu_coords size)
{
  size.x += (-size.x) & 3; // Round up to multiple of 4 pixels
  IDirect3DSurface9 *result = NULL;
  if (device3d != NULL)
    { 
      HRESULT hr =
        device3d->CreateOffscreenPlainSurface((UINT)size.x,(UINT)size.y,
                                              D3DFMT_X8R8G8B8,
                                              D3DPOOL_SYSTEMMEM,
                                              &result,NULL);
      if (hr != D3D_OK)
        result = NULL; // Just in case
    }
  return result;
}

/*****************************************************************************/
/*                   kdws_manager::presentation_thread_wait                  */
/*****************************************************************************/

void kdws_manager::presentation_thread_wait(kdu_long usecs)
{
  LARGE_INTEGER delay_100ns;
  delay_100ns.QuadPart = -(__int64)(usecs * 10);
  if ((presentation_timer != NULL) &&
      SetWaitableTimer(presentation_timer,&delay_100ns,0,NULL,NULL,0))
    WaitForSingleObject(presentation_timer,INFINITE);
  else if (usecs > 1000)
    ::Sleep((int)(usecs / 1000));
}

/*****************************************************************************/
/*                  kdws_manager::retain_open_file_pathname                  */
/*****************************************************************************/

const char *kdws_manager::retain_open_file_pathname(const char *path,
                                                    kdws_frame_window *wnd)
{  
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (kdws_compare_file_pathnames(scan->open_pathname,path))
      {
        scan->retain_count++;
        break;
      }
  if (scan == NULL)
    {
      scan = new kdws_open_file_record;
      scan->open_pathname = new char[strlen(path)+1];
      strcpy(scan->open_pathname,path);
      scan->next = open_file_list;
      open_file_list = scan;
      scan->retain_count = 1;
    }
  kdws_window_list *wnd_elt = windows;
  for (; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      { 
        assert(wnd_elt->file_or_url_name == NULL);
        wnd_elt->file_or_url_name = scan->open_pathname;
        break;
      }
  assert(wnd_elt != NULL);
  return scan->open_pathname;
}

/*****************************************************************************/
/*                  kdws_manager::release_open_file_pathname                 */
/*****************************************************************************/

void kdws_manager::release_open_file_pathname(const char *path,
                                              kdws_frame_window *wnd)
{
  kdws_window_list *wnd_elt = windows;
  for (; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      wnd_elt->file_or_url_name = NULL;
  
  kdws_open_file_record *scan, *prev;
  for (prev=NULL, scan=open_file_list; scan!=NULL; prev=scan, scan=scan->next)
    if (kdws_compare_file_pathnames(scan->open_pathname,path))
      break;
  if (scan == NULL)
    return; // Nothing to release
  scan->retain_count--;
  if (scan->retain_count == 0)
    {
      if (prev == NULL)
        open_file_list = scan->next;
      else
        prev->next = scan->next;
      if (scan->save_pathname != NULL)
        MoveFileEx(kdws_string(scan->save_pathname),
                   kdws_string(scan->open_pathname),
                   MOVEFILE_REPLACE_EXISTING);
      delete scan;
    }
}

/*****************************************************************************/
/*                     kdws_manager::retain_jpip_client                      */
/*****************************************************************************/

const char *
  kdws_manager::retain_jpip_client(const char *server,
                                   const char *request,
                                   const char *url,
                                   kdu_client * &client,
                                   int &request_queue_id,
                                   kdws_frame_window *wnd)
{
  client = NULL; request_queue_id = -1; // Just in case something goes wrong
  if (url != NULL)
    server = request = NULL; // To be sure we don't mix two sources of info
  kdws_window_list *wnd_elt = windows;
  for (; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      {
        wnd_elt->file_or_url_name = NULL;
        break;
      }

  kdu_client_mode mode = settings.get_jpip_client_mode();
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    {
      if (scan->jpip_client == NULL)
        continue;
      if (!scan->jpip_client->check_compatible_connection(server,request,
                                                          mode,url))
        continue;
      if (scan->jpip_client->is_one_time_request())
        request_queue_id = 0;
      else
        request_queue_id = scan->jpip_client->add_queue();
         /* If `request_queue_id' is -ve that is fine; just means that the
            connection is no longer alive, but we can still use the shared
            cache and re-establish connections later -- reconnecting will
            cause all attached windows to get new request queue-id's. */
      scan->retain_count++;
      break;
    }
  if (scan == NULL)
    { 
      const char *proxy = settings.get_jpip_proxy();
      const char *transport = settings.get_jpip_channel_type();
      const char *cache_dir = settings.get_jpip_cache();
      if (((cache_dir != NULL) && (cache_dir[0] == '\0')) ||
          !settings.get_jpip_use_cache())
        cache_dir = NULL;
      scan = new kdws_open_file_record;
      scan->jpip_client = new kdu_client;
      scan->client_auto_save_mode = (cache_dir != NULL);
      scan->client_original_auto_save_mode = scan->client_auto_save_mode;
      try {
        if (url != NULL)
          request_queue_id =
            scan->jpip_client->connect(NULL,proxy,NULL,transport,
                                       cache_dir,mode,url);
        else
          request_queue_id =
            scan->jpip_client->connect(server,proxy,request,transport,
                                       cache_dir,mode,NULL);
        scan->jpip_client->set_preferred_memory_limit(KDWS_CACHE_MEMORY_LIMIT);
        scan->open_url = scan->jpip_client->construct_jpip_url();
        if (scan->open_url == NULL)
          { // This should not happen
            scan->open_url = new char[strlen("<jpip://inactive>")+1];
            strcpy(scan->open_url,"<jpip://inactive>");
          }
      }
      catch (kdu_exception exc) {
        delete scan;       
        throw exc;
      }
      scan->client_notifier = new kdws_client_notifier;
      scan->jpip_client->install_notifier(scan->client_notifier);
      scan->next = open_file_list;
      open_file_list = scan;
      scan->retain_count = 1;
    }

  if (wnd_elt != NULL)
    { 
      scan->client_notifier->retain_window(&wnd_elt->notification_manager);
      wnd_elt->file_or_url_name = scan->open_url;
    }
  client = scan->jpip_client;
  return scan->open_url;
}

/*****************************************************************************/
/*                     kdws_manager::retain_jpip_cache                       */
/*****************************************************************************/

const char *
  kdws_manager::retain_jpip_cache(const char *path,
                                  const char *cache_identifier,
                                  kdu_client * &client,
                                  int &request_queue_id,
                                  kdws_frame_window *wnd)
{
  client = NULL; request_queue_id = -1; // In case something goes wrong
  kdws_window_list *wnd_elt = windows;
  for (; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      { 
        assert(wnd_elt->file_or_url_name == NULL);
        break;
      }
  
  // Arrange for `path' to be expanded to a fully qualified path name
  char real_path_buf[2*MAX_PATH+2];
  real_path_buf[0] = '\0';
  GetFullPathNameA(path,2*MAX_PATH+1,real_path_buf,NULL);
  if (real_path_buf[0] != '\0')
    path = real_path_buf;
  
  // Now look for compatible existing clients
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    { 
      if (scan->jpip_client == NULL)
        continue;
      const char *existing_id = scan->jpip_client->get_cache_identifier();
      if ((existing_id == NULL) || (cache_identifier == NULL) ||
          (strcmp(existing_id,cache_identifier) != 0) ||
          !scan->jpip_client->augment_with_cache_file(path))
        continue;
      if (scan->jpip_client->is_one_time_request())
        request_queue_id = 0;
      else
        request_queue_id = scan->jpip_client->add_queue();
         /* If `request_queue_id' is -ve that is fine; just means that the
            connection is no longer alive, but we can still use the shared
            cache and re-establish connections later -- reconnecting will
            cause all attached windows to get new request queue-id's. */
      scan->retain_count++;
      break;
    }
  
  if (scan == NULL)
    { 
      const char *cache_dir = settings.get_jpip_cache();
      if (((cache_dir != NULL) && (cache_dir[0] == '\0')) ||
          !settings.get_jpip_use_cache())
        cache_dir = NULL;
      scan = new kdws_open_file_record;
      scan->jpip_client = new kdu_client;
      scan->client_auto_save_mode = (cache_dir != NULL);
      scan->client_original_auto_save_mode = scan->client_auto_save_mode;
      try { 
        request_queue_id =
        scan->jpip_client->open_with_cache_file(path,cache_dir);
        scan->open_url = scan->jpip_client->construct_jpip_url();
        if (scan->open_url == NULL)
          { // This should not happen
            scan->open_url = new char[strlen("<jpip://inactive>")+1];
            strcpy(scan->open_url,"<jpip://inactive>");
          }
      } catch (kdu_exception exc) {
        delete scan;
        throw exc;
      }
      scan->client_notifier = new kdws_client_notifier;
      scan->jpip_client->install_notifier(scan->client_notifier);
      scan->next = open_file_list;
      open_file_list = scan;
      scan->retain_count = 1;
    }
  if (wnd_elt != NULL)
    { 
      scan->client_notifier->retain_window(&wnd_elt->notification_manager);
      wnd_elt->file_or_url_name = scan->open_url;
    }
  client = scan->jpip_client;
  return scan->open_url;
}

/*****************************************************************************/
/*                         kdws_manager::count_peers                         */
/*****************************************************************************/

int kdws_manager::count_peers(kdu_client *client, kdws_frame_window *wnd)
{
  kdws_window_list *wnd_elt;
  for (wnd_elt=windows; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      break;
  if ((wnd_elt == NULL) || (wnd_elt->file_or_url_name == NULL))
    return 0;
  int count = 0;
  if (client == NULL)
    { 
      const char *file_or_url_name = wnd_elt->file_or_url_name;
      for (wnd_elt=windows; wnd_elt != NULL; wnd_elt=wnd_elt->next)
        if ((wnd_elt->file_or_url_name == file_or_url_name) &&
            (wnd_elt->wnd != wnd))
          count++;
    }
  else
    { 
      kdws_open_file_record *scan;
      for (scan=open_file_list; scan != NULL; scan=scan->next)
        if (scan->jpip_client == client)
          break;
      if ((scan != NULL) && (scan->retain_count > 0) &&
          (wnd_elt->file_or_url_name == scan->open_url))
        count = scan->retain_count - 1;
    }
  return count;
}

/*****************************************************************************/
/*                   kdws_manager::reconnect_jpip_client                     */
/*****************************************************************************/

int kdws_manager::reconnect_jpip_client(kdu_client *client,
                                        kdws_frame_window *wnd)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (scan->jpip_client == client)
      break;
  if (scan == NULL)
    { // Should not be possible
      assert(0);
      return -1;
    }
  
  if (scan->reconnecting_client || client->is_alive())
    return client->add_queue();
  
  const char *transport = settings.get_jpip_channel_type();
  const char *proxy = settings.get_jpip_proxy();
  int new_queue_id = client->reconnect(transport,proxy);
  if (new_queue_id >= 0)
    { 
      client->set_preferred_memory_limit(KDWS_CACHE_MEMORY_LIMIT);
      scan->reconnecting_client = true; // Prevents unbounded recursion
      kdws_window_list *wnd_elt;
      for (wnd_elt=windows; wnd_elt != NULL; wnd_elt=wnd_elt->next)
        if (wnd_elt->wnd != wnd)
          wnd->note_jpip_client_reconnecting(client);
      scan->reconnecting_client = false;
    }
  return new_queue_id;
}

/*****************************************************************************/
/*                    kdws_manager::restart_jpip_client                      */
/*****************************************************************************/

int
  kdws_manager::restart_jpip_client(kdu_client *client,
                                    kdws_frame_window *wnd)
{
  kdws_window_list *wnd_elt;
  for (wnd_elt=windows; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      break;
  if (wnd_elt == NULL)
    return 0;
  
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (scan->jpip_client == client)
      break;
  if (scan == NULL)
    { // Should not be possible
      assert(0);
      return -1;
    }
  if ((scan->retain_count != 1) ||
      (wnd_elt->file_or_url_name != scan->open_url))
    return -1; // We are not the sole user of `client'
  
  const char *transport = settings.get_jpip_channel_type();
  const char *proxy = settings.get_jpip_proxy();
  int new_queue_id = client->reconnect(transport,proxy,true);
  return new_queue_id;
}

/*****************************************************************************/
/*                    kdms_manager::set_cache_save_mode                      */
/*****************************************************************************/

bool kdws_manager::set_cache_save_mode(kdu_client *client, bool desired_mode)
{ 
  kdws_open_file_record *scan=NULL;
  if (client != NULL)
    for (scan=open_file_list; scan != NULL; scan=scan->next)
      if (scan->jpip_client == client)
        break;
  if (scan != NULL)
    { 
      if (scan->client_auto_save_mode == desired_mode)
        return true; // Mode is already correctly configured
      if (!scan->client_original_auto_save_mode)
        return false; // Cannot make any change at this point
      scan->client_auto_save_mode = !scan->client_auto_save_mode;
      int new_handling = 0;
      if (scan->client_auto_save_mode)
        new_handling = KDU_CLIENT_FILE_SAVE;
      else
        new_handling = KDU_CLIENT_FILE_DELETE;
      scan->jpip_client->set_cache_file_handling(new_handling);
      settings.set_jpip_use_cache(scan->client_auto_save_mode);
    }
  else
    { 
      const char *cache_dir = settings.get_jpip_cache();
      if ((cache_dir != NULL) && (cache_dir[0] == '\0'))
        cache_dir = NULL;
      bool can_auto_save = (cache_dir != NULL);
      bool old_mode = can_auto_save && settings.get_jpip_use_cache();
      if (old_mode == desired_mode)
        return true; // Mode is already correctly configured
      if (!can_auto_save)
        return false;
      settings.set_jpip_use_cache(desired_mode);
    }
  return true;
}

/*****************************************************************************/
/*                   kdws_manager::check_cache_save_mode                     */
/*****************************************************************************/

bool kdws_manager::check_cache_save_mode(kdu_client *client, bool &can_change)
{
  kdws_open_file_record *scan=NULL;
  if (client != NULL)
    for (scan=open_file_list; scan != NULL; scan=scan->next)
      if (scan->jpip_client == client)
        break;
  can_change = false;
  if (scan != NULL)
    { 
      if (scan->client_original_auto_save_mode)
        can_change = true;
      return scan->client_auto_save_mode;
    }
  else
    { 
      const char *cache_dir = settings.get_jpip_cache();
      if ((cache_dir != NULL) && (cache_dir[0] == '\0'))
        cache_dir = NULL;
      bool mode = settings.get_jpip_use_cache();
      if (cache_dir != NULL)
        can_change = true;
      return (can_change && mode);
    }
}

/*****************************************************************************/
/*             kdws_manager::use_jpx_translator_with_jpip_client             */
/*****************************************************************************/

void kdws_manager::use_jpx_translator_with_jpip_client(kdu_client *client)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (scan->jpip_client == client)
      {
        if (scan->jpx_client_translator == NULL)
          {
            scan->jpx_client_translator = new kdu_clientx;
            client->install_context_translator(scan->jpx_client_translator);
          }
        break;
      }
}

/*****************************************************************************/
/*                      kdws_manager::release_jpip_client                    */
/*****************************************************************************/

void kdws_manager::release_jpip_client(kdu_client *client,
                                       kdws_frame_window *wnd)
{
  kdws_window_list *wnd_elt = windows;
  for (; wnd_elt != NULL; wnd_elt=wnd_elt->next)
    if (wnd_elt->wnd == wnd)
      break;
  
  kdws_open_file_record *scan, *prev;
  for (prev=NULL, scan=open_file_list; scan!=NULL; prev=scan, scan=scan->next)
    if (scan->jpip_client == client)
      break;
  if (scan == NULL)
    { // Nothing to release
      assert((wnd_elt == NULL) || (wnd_elt->file_or_url_name == NULL));
      return;
    }

  if ((wnd_elt != NULL) && (wnd_elt->file_or_url_name != NULL))
    { 
      assert(wnd_elt->file_or_url_name == scan->open_url);
      scan->client_notifier->release_window(&wnd_elt->notification_manager);
      wnd_elt->file_or_url_name = NULL;
    }
  scan->retain_count--;

  if (scan->retain_count == 0)
    {
      if (prev == NULL)
        open_file_list = scan->next;
      else
        prev->next = scan->next;
      assert(scan->save_pathname == NULL);
      scan->jpip_client->disconnect(false,2000,-1,true);
         // Forcibly disconnect everything after a 2 second timeout.
      scan->jpip_client->close();
      delete scan;
    }
}

/*****************************************************************************/
/*              kdws_manager::open_url_in_preferred_application              */
/*****************************************************************************/

void
  kdws_manager::open_url_in_preferred_application(const char *url,
                                                  const char *base)
{
  assert((url != NULL) && (base != NULL));
  
  // Start by figuring out what portion of the base file or URL should be
  // used to expand relative URL's.  At the same time we figure out whether
  // our base resource is local or being accessed by JPIP.
  bool base_uses_jpip = false;
  const char *jpip_host=NULL, *jpip_resource=NULL, *jpip_query=NULL;
  jpip_host = kdu_client::check_compatible_url(base,true,NULL,&jpip_resource,
                                               &jpip_query);
  const char *base_lim = base + strlen(base);
  if (jpip_host != NULL)
    { 
      base_uses_jpip = true;
      base = jpip_host;
      if (jpip_query != NULL)
        base_lim = jpip_query;
    }
  while ((base_lim > base) &&
         (base_lim[-1] != '/') && (base_lim[-1] != '\\') &&
         (base_lim[-1] != ':'))
      base_lim--;

  // Now examine `url' for compatibility and concatenation with `base'
  bool is_relative=false;
  bool use_jpip=false;
  bool use_this_app=false;
  const char *sep = strrchr(url,'.');
  if ((sep != NULL) && 
      (((tolower(sep[1]) == 'j') && (tolower(sep[2]) == 'p') &&
        (tolower(sep[3]) == 'x') && (sep[4] == '\0')) ||
       ((tolower(sep[1]) == 'j') && (tolower(sep[2]) == 'p') &&
        (tolower(sep[3]) == 'f') && (sep[4] == '\0')) ||
       ((tolower(sep[1]) == 'j') && (tolower(sep[2]) == 'p') &&
        (tolower(sep[3]) == '2') && (sep[4] == '\0')) ||
       ((tolower(sep[1]) == 'j') && (tolower(sep[2]) == '2') &&
        (tolower(sep[3]) == 'c') && (sep[4] == '\0')) ||
       ((tolower(sep[1]) == 'j') && (tolower(sep[2]) == '2') &&
        (tolower(sep[3]) == 'k') && (sep[4] == '\0'))))
    use_this_app = true;
  
  if ((url[0] == '.') && (url[1] == '/'))
    { 
      use_jpip = base_uses_jpip;
      is_relative = true;
    }
  else if ((url[0] == '.') && (url[1] == '.') && (url[2] == '/'))
    { 
      is_relative = true;
      use_jpip = base_uses_jpip;
    }
  else if ((tolower(url[0]) == 'j') && (tolower(url[1]) == 'p') &&
           (tolower(url[2]) == 'i') && (tolower(url[3]) == 'p') &&
           (tolower(url[4]) == ':') && (tolower(url[5]) == '/') &&
           (tolower(url[6]) == '/'))
    { 
      use_jpip = use_this_app = true;
      url += 7;
    }
  else if ((tolower(url[0]) == 'h') && (tolower(url[1]) == 't') &&
           (tolower(url[2]) == 't') && (tolower(url[3]) == 'p') &&
           (tolower(url[4]) == ':') && (tolower(url[5]) == '/') &&
           (tolower(url[6]) == '/'))
    { 
      if (use_this_app)
        { 
          use_jpip = true;
          url += 7;
        }
    }
  else
    return;
  
  // Now we are ready to form the absolute path/URL string
  int path_len = (int) strlen(url);
  if (is_relative)
    path_len += (int)(base_lim-base);
  if (use_jpip)
    path_len += 7;
  char *path = new char[path_len+1];
  try { 
    char *ptr = path;
    if (use_jpip)
      { 
        strcpy(ptr,"jpip://");
        ptr += strlen(ptr);
      }
    if (is_relative)
      { 
        strncpy(ptr,base,base_lim-base);
        ptr += base_lim-base;
      }
    strcpy(ptr,url);
    ptr += strlen(ptr);
    assert(((int)(ptr-path)) <= path_len);
    *ptr = '\0';
    
    if (use_this_app)
      { 
        if (use_jpip)
          this->open_url_in_free_window(path);
        else
          this->open_file_in_free_window(path);
      }
    else
      { // Get the OS to launch an application
        assert(!use_jpip);
        kdws_string the_path(path);
        kdws_string the_verb("open");
        ShellExecute(NULL,the_verb,the_path,NULL,NULL,SW_SHOWNORMAL);
      }
  }
  catch (...) {
    delete[] path;
    throw;
  }
  delete[] path;
}

/*****************************************************************************/
/*                   kdws_manager::get_save_file_pathname                    */
/*****************************************************************************/

const char *kdws_manager::get_save_file_pathname(const char *path)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan!=NULL; scan=scan->next)
    if (kdws_compare_file_pathnames(scan->open_pathname,path))
      {
        if (scan->save_pathname == NULL)
          {
            scan->save_pathname = new char[strlen(path)+2];
            strcpy(scan->save_pathname,path);
            strcat(scan->save_pathname,"~");
          }
        return scan->save_pathname;
      }
  return path;
}

/*****************************************************************************/
/*                 kdws_manager::declare_save_file_invalid                   */
/*****************************************************************************/

void kdws_manager::declare_save_file_invalid(const char *path)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (kdws_compare_file_pathnames(scan->open_pathname,path))
      return; // Safety measure to prevent deleting open file; shouldn't happen
    else if ((scan->save_pathname != NULL) &&
             kdws_compare_file_pathnames(scan->save_pathname,path))
      {
        delete[] scan->save_pathname;
        scan->save_pathname = NULL;
      }
  remove(path);
}

/*****************************************************************************/
/*                  kdws_manager::get_open_file_retain_count                 */
/*****************************************************************************/

int kdws_manager::get_open_file_retain_count(const char *path)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if (kdws_compare_file_pathnames(scan->open_pathname,path))
      return scan->retain_count;
  return 0;
}
  
/*****************************************************************************/
/*                   kdws_manager::check_open_file_replaced                  */
/*****************************************************************************/

bool kdws_manager::check_open_file_replaced(const char *path)
{
  kdws_open_file_record *scan;
  for (scan=open_file_list; scan != NULL; scan=scan->next)
    if ((scan->save_pathname != NULL) &&
        kdws_compare_file_pathnames(scan->open_pathname,path))
      return true;
  return false;
}

/*****************************************************************************/
/*                   kdws_manager::open_file_in_free_window                  */
/*****************************************************************************/

void kdws_manager::open_file_in_free_window(const char *fname)
{
  kdws_frame_window *target_wnd = this->find_empty_window();
  if (target_wnd == NULL)
    { 
      target_wnd = new kdws_frame_window();
      if (!target_wnd->init(this))
        { 
          delete target_wnd;
          return;
        }
    }
  target_wnd->open_file(fname);
}

/*****************************************************************************/
/*                    kdws_manager::open_url_in_free_window                  */
/*****************************************************************************/

void kdws_manager::open_url_in_free_window(const char *url)
{
  kdws_frame_window *target_wnd = this->find_empty_window();
  if (target_wnd == NULL)
    { 
      target_wnd = new kdws_frame_window();
      if (!target_wnd->init(this))
        { 
          delete target_wnd;
          return;
        }
    }
  target_wnd->open_url(url);
}

/*****************************************************************************/
/*                        kdws_manager::InitInstance                         */
/*****************************************************************************/

BOOL kdws_manager::InitInstance()
{
  //_CrtSetBreakAlloc(612);
  
  //AllocConsole();
  //freopen("CONOUT$","wb",stdout);

  // Configure the application and load settings
  AfxEnableControlContainer();
#if _MFC_VER < 0x0500
#  ifdef _AFXDLL
     Enable3dControls(); // Call this when using MFC in a shared DLL
#  else
     Enable3dControlsStatic(); // Call this when linking to MFC statically
#  endif
#endif // _MFC_VER
  SetRegistryKey(_T("Kakadu"));
  LoadStdProfileSettings(4);
  settings.load_from_registry(this);
  kdws_string executable_path(_MAX_PATH);
  GetModuleFileName(m_hInstance,executable_path,_MAX_PATH);
  if (!executable_path.is_empty())
    register_protocols(executable_path);
  m_pMainWnd = NULL; // The `add_window' function will set the main window
  main_thread_id = GetCurrentThreadId();
  
  idle_hook_handle =
    ::SetWindowsHookEx(WH_FOREGROUNDIDLE,idle_hook_callback,
                       NULL,main_thread_id);

  // Find screen refresh parameters
  DEVMODE dev_mode;
  memset(&dev_mode,0,sizeof(dev_mode));
  dev_mode.dmSize = sizeof(dev_mode);
  EnumDisplaySettings(NULL,ENUM_CURRENT_SETTINGS,&dev_mode);
  int refresh_rate = (int) dev_mode.dmDisplayFrequency;
  if (refresh_rate <= 0)
    refresh_rate = 60; // Can't find true value; assume standard LCD rate
  if (refresh_rate > 240)
    refresh_rate = 240; // Just in case

  // Configure presentation timer
  mm_timer_resolution = 2;
  while (timeBeginPeriod(mm_timer_resolution) != TIMERR_NOERROR)
    { 
      if (mm_timer_resolution < 8)
        mm_timer_resolution += 2;
      else
        { mm_timer_resolution = 0; break; }
    }
  presentation_timer = CreateWaitableTimer(NULL,TRUE,NULL);

  // Configure presentation thread
  display_event_interval = 1.0 / refresh_rate;
  display_interval_usecs = (kdu_long)(0.5+1000000*display_event_interval);
  next_display_event_time =
    0.000001*presentation_absolute_time.get_ellapsed_microseconds();
  next_display_event_time = display_event_interval *
    (10.0 + floor(next_display_event_time/display_event_interval));
  next_display_event_abs_usecs = (kdu_long)(1000000*next_display_event_time);
  presentation_thread = AfxBeginThread(presentation_thread_startup,this,
                                       THREAD_PRIORITY_TIME_CRITICAL,0,
                                       CREATE_SUSPENDED,NULL);
  presentation_thread->m_bAutoDelete = FALSE;
  presentation_thread->ResumeThread();

  // Configure thread-safe and GUI-safe Kakadu error/warning handlers
  warn_message_queue.configure_posting(KDWS_CORE_MESSAGE,main_thread_id);
  err_message_queue.configure_posting(KDWS_CORE_MESSAGE,main_thread_id);
  kdu_customize_errors(&err_formatter);
  kdu_customize_warnings(&warn_formatter);

  // Create an initial window to get us started
  kdws_frame_window *window = new kdws_frame_window();
  if (!window->init(this))
    {
      delete window;
      return FALSE;
    }

  // Configure DirectX
  screen_size.x = GetSystemMetrics(SM_CXSCREEN);
  screen_size.y = GetSystemMetrics(SM_CYSCREEN);

  // See if there is an initial file to open
  kdws_string filename(m_lpCmdLine);
  char *fname = (char *) filename;
  while ((*fname != '\0') && ((*fname == ' ') || (*fname == '\t')))
    fname++;
  if (*fname != '\0')
    {
      if (*fname == '\"')
        {
          char *ch = strrchr(fname+1,'\"');
          if (ch != NULL)
            {
              fname++;
              *ch = '\0';
            }
        }
      if (((toupper(fname[0])=='J') && (toupper(fname[1])=='P') &&
           (toupper(fname[2])=='I') && (toupper(fname[3])=='P')) ||
          ((toupper(fname[0])=='H') && (toupper(fname[1])=='T') &&
           (toupper(fname[2])=='T') && (toupper(fname[3])=='P')) &&
          (fname[4]==':') && (fname[5]=='/') && (fname[6]=='/'))
          window->open_url(fname);
      else
        window->open_file(fname);
    }
  return TRUE;
}

/*****************************************************************************/
/*                       kdws_manager::SaveAllModified                       */
/*****************************************************************************/

BOOL kdws_manager::SaveAllModified()
{
  if (!application_can_terminate())
    return FALSE;
  return TRUE;
}

/*****************************************************************************/
/*                        kdws_manager::ExitInstance                         */
/*****************************************************************************/

int kdws_manager::ExitInstance()
{
  send_application_terminating_messages();
  if (presentation_thread != NULL)
    { 
      presentation_thread_exit_requested = true;
      WaitForSingleObject(presentation_thread->m_hThread,INFINITE);
      delete presentation_thread;
      presentation_thread = NULL;
    }
  if (presentation_timer != NULL)
    {
      CloseHandle(presentation_timer);
      presentation_timer = NULL;
    }
  if (mm_timer_resolution != 0)
    {
      timeEndPeriod(mm_timer_resolution);
      mm_timer_resolution = 0;
    }
  assert((direct3d == NULL) && (device3d == NULL) && (device3d_hwnd == NULL));
  if (wakeup_timer_id != 0)
    { KillTimer(NULL,wakeup_timer_id); wakeup_timer_id = 0; }
  next_window_to_wake = NULL;
  last_known_key_wnd = NULL;
  if (idle_hook_handle != NULL)
    ::UnhookWindowsHookEx(idle_hook_handle);

  settings.save_to_registry(this);
  //FreeConsole();
  return 0;
}

/*****************************************************************************/
/*                     kdws_manager::PreTranslateMessage                     */
/*****************************************************************************/

BOOL kdws_manager::PreTranslateMessage(MSG *pMsg)
{
  if (pMsg->hwnd == NULL)
    { // We provide special processing only for thread messages
      if (pMsg->message == KDWS_CORE_MESSAGE)
        { // Message delivered by a call to `kdu_error' or `kdu_warning' in a
          // different thread, via `kd_core_message_queue::pop_message'.
          while (err_message_queue.pop_message() != NULL);
          while (warn_message_queue.pop_message() != NULL);
          return TRUE;
        }
    }
  return CWinApp::PreTranslateMessage(pMsg);
}

/*****************************************************************************/
/*                           kdws_manager::OnIdle                            */
/*****************************************************************************/

BOOL kdws_manager::OnIdle(LONG lCount)
{
  do_idle_processing();
  return CWinApp::OnIdle(lCount); // Give base a chance to do something
}

/******************************************************************************/
/*                        MESSAGE MAP for kdws_manager                        */
/******************************************************************************/

BEGIN_MESSAGE_MAP(kdws_manager, CWinApp)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
  ON_COMMAND(ID_APP_EXIT, menu_AppExit)
  ON_COMMAND(ID_WINDOW_NEW, menu_WindowNew)
  ON_COMMAND(ID_WINDOW_ARRANGE, menu_WindowArrange)
  ON_COMMAND(ID_WINDOW_BROADCAST, menu_WindowBroadcast)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_BROADCAST, can_do_WindowBroadcast)
END_MESSAGE_MAP()
  
/*****************************************************************************/
/*                         kdws_manager::OnAppAbout                          */
/*****************************************************************************/

void kdws_manager::OnAppAbout()
{
  CAboutDlg aboutDlg;
  aboutDlg.DoModal();
}

/*****************************************************************************/
/*                        kdws_manager::menu_AppExit                         */
/*****************************************************************************/

void kdws_manager::menu_AppExit()
{
  if (application_can_terminate())
    send_application_terminating_messages();
}

/*****************************************************************************/
/*                      kdws_manager::menu_WindowNew                         */
/*****************************************************************************/

void kdws_manager::menu_WindowNew()
{
  kdws_frame_window *wnd = new kdws_frame_window;
  try {
    if (!wnd->init(this))
      { kdu_error e; e << "Internal error creating new window."; }
  }
  catch (int) {
    delete this;
  }
  CRect wnd_frame;  wnd->GetWindowRect(&wnd_frame);
  kdu_coords frame_size;
  frame_size.x = wnd_frame.Width();
  frame_size.y = wnd_frame.Height();
  this->place_window(wnd,frame_size);
}

/*****************************************************************************/
/*                     kdws_manager::menu_WindowArrange                      */
/*****************************************************************************/

void kdws_manager::menu_WindowArrange()
{
  reset_placement_engine();
  kdws_frame_window *wnd;
  for (int w=0; (wnd = this->access_window(w)) != NULL; w++)
    {
      CRect wnd_frame;  wnd->GetWindowRect(&wnd_frame);
      kdu_coords frame_size;
      frame_size.x = wnd_frame.Width();
      frame_size.y = wnd_frame.Height();
      this->place_window(wnd,frame_size);
    }
}

/*****************************************************************************/
/*                     kdws_manager::menu_WindowBroadcast                    */
/*****************************************************************************/

void kdws_manager::menu_WindowBroadcast()
{
  bool indefinite = this->broadcast_actions_indefinitely;
  this->set_action_broadcasting(false,!indefinite);
}

/*****************************************************************************/
/*                    kdws_manager::can_do_WindowBroadcast                   */
/*****************************************************************************/

void kdws_manager::can_do_WindowBroadcast(CCmdUI *pCmdUI)
{
  pCmdUI->Enable(TRUE);
  pCmdUI->SetCheck(this->broadcast_actions_indefinitely);
}
