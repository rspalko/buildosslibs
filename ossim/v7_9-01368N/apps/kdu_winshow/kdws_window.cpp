/*****************************************************************************/
// File: kdws_window.cpp [scope = APPS/WINSHOW]
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
   Implementation of the objects declared in "kdws_window.h"
******************************************************************************/

#include "stdafx.h"
#include <Cderr.h>
#include <math.h>
#include "kdu_utils.h"
#include "kdws_manager.h"
#include "kdws_window.h"
#include "kdws_renderer.h"
#include "kdws_url_dialog.h"
#include "kdws_metadata_editor.h"
#include "menu_tips.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define KD_MAX_CATALOG_PANEL_WIDTH 280

#define KD_LEFT_TIMEFIELD_ID          (AFX_IDW_PANE_FIRST+200)
#define KD_LEFT_TIMEBUTTON_ID         (AFX_IDW_PANE_FIRST+201)
#define KD_CENTRAL_SLIDER_ID          (AFX_IDW_PANE_FIRST+202)
#define KD_RIGHT_TIMEBUTTON_ID        (AFX_IDW_PANE_FIRST+203)
#define KD_RIGHT_TIMEFIELD_ID         (AFX_IDW_PANE_FIRST+204)
#define KD_RIGHT_TIMESPIN_ID          (AFX_IDW_PANE_FIRST+205)

#define KD_FWD_PLAYBUTTON_ID          (AFX_IDW_PANE_FIRST+300)
#define KD_GO_STARTBUTTON_ID          (AFX_IDW_PANE_FIRST+301)
#define KD_STOP_PLAYBUTTON_ID         (AFX_IDW_PANE_FIRST+302)
#define KD_GO_ENDBUTTON_ID            (AFX_IDW_PANE_FIRST+303)
#define KD_REV_PLAYBUTTON_ID          (AFX_IDW_PANE_FIRST+304)
#define KD_FWD_STEPBUTTON_ID          (AFX_IDW_PANE_FIRST+305)
#define KD_REV_STEPBUTTON_ID          (AFX_IDW_PANE_FIRST+306)
#define KD_STEP_HILITEBOX_ID          (AFX_IDW_PANE_FIRST+307)
#define KD_PLAY_RATEFIELD_ID          (AFX_IDW_PANE_FIRST+310)
#define KD_PLAY_RATESPIN_ID           (AFX_IDW_PANE_FIRST+311)
#define KD_PLAY_NATIVEBUTTON_ID       (AFX_IDW_PANE_FIRST+312)
#define KD_PLAY_REPEATBUTTON_ID       (AFX_IDW_PANE_FIRST+313)

#define KD_CENTRAL_SLIDER_MAX 10000

/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* EXTERN              kdws_create_preferred_window_font                     */
/*****************************************************************************/

void kdws_create_preferred_window_font(CFont *font, double scale,
                                       bool fixed_pitch)
{
  // Start by figuring out the font height applicable to window captions
  NONCLIENTMETRICS metrics;
  metrics.cbSize = sizeof(NONCLIENTMETRICS);
  int default_font_height = 17;
  if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
                           metrics.cbSize,&metrics,0))
    {
      default_font_height = metrics.lfCaptionFont.lfHeight;
      if (default_font_height < 0)
        default_font_height = -default_font_height;
      default_font_height += (default_font_height >> 2);
    }

  CFont *sys_font;
  LOGFONT font_attributes;
  if (fixed_pitch)
    sys_font = CFont::FromHandle((HFONT) GetStockObject(ANSI_FIXED_FONT));
  else
    sys_font = CFont::FromHandle((HFONT) GetStockObject(ANSI_VAR_FONT));
  int height = (int)(0.5+default_font_height*scale);
  sys_font->GetLogFont(&font_attributes);
  font_attributes.lfWidth = 0; font_attributes.lfHeight = height;
  font_attributes.lfWeight = FW_NORMAL;
  if (fixed_pitch)
    wcscpy(font_attributes.lfFaceName,L"Courier New");
  else
    wcscpy(font_attributes.lfFaceName,L"Calibri");
  if (!font->CreateFontIndirect(&font_attributes))
    { // Try using original face name
      sys_font->GetLogFont(&font_attributes);
      font_attributes.lfWidth = 0; font_attributes.lfHeight = height;
      font_attributes.lfWeight = FW_NORMAL;
      if (!font->CreateFontIndirect(&font_attributes))
        { // Just use the ANSI_VAR_FONT as-is
          sys_font->GetLogFont(&font_attributes);
          font->CreateFontIndirect(&font_attributes);
        }
    }
}


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                       kd_time_to_hms                               */
/*****************************************************************************/

void kd_time_to_hms(double time, int &hours, int &minutes, int &seconds)
  /* Converts `time' (measured in seconds) into an hours:minutes:seconds
     representation. */
{
  time = floor(time + 0.5); // Round to nearest whole number of seconds
  hours = (int) floor(time / 3600.0);
  time -= hours*3600.0;
  minutes = (int) floor(time / 60.0);
  if (minutes >= 60)
    minutes = 59; // Just in case
  seconds = (int)(0.5+time-minutes*60.0);
  if (seconds >= 60)
    seconds = 59; // Just in case
}

/*****************************************************************************/
/* STATIC                     kd_ellipse_to_bezier                           */
/*****************************************************************************/

static void
  kd_ellipse_to_bezier(POINT bpts[], POINT centre,
                       double extent_x, double extent_y, double alpha)
  /* Approximates a skewed ellipse with four bezier spline sections, writing
     the bezier control points to the 13-element `bpts' array.  On entry,
     `centre' identifies the centre of the ellipse, `extent_x' and
     `extent_y' identify the half-width of the smallest bounding rectangle
     for the ellipse prior to skewing, and `alpha' is the skewing parameter.
     This representation is described in connection with
     `jpx_roi::init_ellipse' and related functions. */
{
  double f = (sqrt(2.0)-1.0)*4.0/3.0;
  double rx[12], ry[12]; // Non-skewed ellipse control points, centred at 0
  rx[0] = -extent_x;             ry[0] =  0.0;
  rx[1] = -extent_x;             ry[1] = -f*extent_y;
  rx[2] = -f*extent_x;           ry[2] = -extent_y;
  rx[3] =  0.0;                  ry[3] = -extent_y;
  rx[4] =  f*extent_x;           ry[4] = -extent_y;
  rx[5] =  extent_x;             ry[5] = -f*extent_y;
  rx[6] =  extent_x;             ry[6] =  0.0;
  rx[7] =  extent_x;             ry[7] =  f*extent_y;
  rx[8] =  f*extent_x;           ry[8] =  extent_y;
  rx[9] =  0.0;                  ry[9] =  extent_y;
  rx[10]= -f*extent_x;           ry[10]=  extent_y;
  rx[11]= -extent_x;             ry[11]=  f*extent_y;
  for (int n=0; n < 12; n++)
    { 
      bpts[n].x = centre.x + (int) kdu_round(rx[n] + alpha*ry[n]);
      bpts[n].y = centre.y + (int) kdu_round(ry[n]);
    }
  bpts[12] = bpts[0];
}

/* ========================================================================= */
/*                         kdws_interrogate_dlg Class                        */
/* ========================================================================= */

/*****************************************************************************/
/*               kdws_interrogate_dlg::kdws_interrogate_dlg                  */
/*****************************************************************************/

kdws_interrogate_dlg::kdws_interrogate_dlg(CWnd *pParent,
                                           const char *caption,
                                           const char *message,
                                           const char *option0,
                                           const char *option1,
                                           const char *option2,
                                           const char *option3)
      : CDialog(kdws_interrogate_dlg::IDD, pParent)
{
  caption_string = msg = opt[0] = opt[1] = opt[2] = opt[3] = NULL;
  bgnd_colour = RGB(255,255,230);
  bgnd_brush.CreateSolidBrush(bgnd_colour);
  kdws_create_preferred_window_font(&local_font,1.2,false);
  assert((caption != NULL) && (message != NULL));
  caption_string = new kdws_string(caption);
  msg = new kdws_string(message);
  if (option0 != NULL) opt[0] = new kdws_string(option0);
  if (option1 != NULL) opt[1] = new kdws_string(option1);
  if (option2 != NULL) opt[2] = new kdws_string(option2);
  if (option3 != NULL) opt[3] = new kdws_string(option3);
}

/*****************************************************************************/
/*               kdws_interrogate_dlg::~kdws_interrogate_dlg                 */
/*****************************************************************************/

kdws_interrogate_dlg::~kdws_interrogate_dlg()
{ 
  if (caption_string != NULL) delete caption_string;
  if (msg != NULL) delete msg;
  for (int n=0; n < 4; n++)
    if (opt[n] != NULL) delete opt[n];
  bgnd_brush.DeleteObject();
}

BEGIN_MESSAGE_MAP(kdws_interrogate_dlg, CDialog)
  ON_BN_CLICKED(IDC_INTERROGATE_BUTTON1, on_button1)
  ON_BN_CLICKED(IDC_INTERROGATE_BUTTON2, on_button2)
  ON_BN_CLICKED(IDC_INTERROGATE_BUTTON3, on_button3)
  ON_BN_CLICKED(IDC_INTERROGATE_BUTTON4, on_button4)
  ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

/*****************************************************************************/
/*                    kdws_interrogate_dlg::OnInitDialog                     */
/*****************************************************************************/

BOOL kdws_interrogate_dlg::OnInitDialog()
{
  CDialog::OnInitDialog();
  SetWindowText(*caption_string);
  CDC *dc = get_textfield()->GetDC();
  CFont *old_font = dc->SelectObject(&local_font);

  // See if we need to provide a system menu for the dialog
  if (opt[0] == NULL)
    ModifyStyle(0,WS_SYSMENU,0);

  // Start by working out widths for each button
  WINDOWPLACEMENT button_placement;
  get_button1()->GetWindowPlacement(&button_placement);
  int min_button_width = button_placement.rcNormalPosition.right -
    button_placement.rcNormalPosition.left;
  int n, total_button_width=0, button_widths[4]={0,0,0,0};
  for (n=0; (n < 4) && (opt[n] != NULL); n++)
    { 
      int len = (int) strlen(*opt[n]);
      SIZE text_size = dc->GetTextExtent(*opt[n],len);
      button_widths[n] = 16 + text_size.cx;
      if (button_widths[n] < min_button_width)
        button_widths[n] = min_button_width;
      total_button_width += button_widths[n];
    }
  int num_buttons = n;

  // Now work out existing placement and dimensions of controls
  WINDOWPLACEMENT dialog_placement, text_placement;
  GetWindowPlacement(&dialog_placement);
  get_textfield()->GetWindowPlacement(&text_placement);
  int dialog_width = dialog_placement.rcNormalPosition.right -
    dialog_placement.rcNormalPosition.left;
  int dialog_height = dialog_placement.rcNormalPosition.bottom -
    dialog_placement.rcNormalPosition.top;
  int text_width = text_placement.rcNormalPosition.right -
    text_placement.rcNormalPosition.left;
  int text_height = text_placement.rcNormalPosition.bottom -
    text_placement.rcNormalPosition.top;
  int left_border = text_placement.rcNormalPosition.left;
  int top_border = text_placement.rcNormalPosition.top;
  int button_top = button_placement.rcNormalPosition.top;
  int button_height = button_placement.rcNormalPosition.bottom -
    button_placement.rcNormalPosition.top;

  // Figure out dialog width and horizontal button placement
  int expand = 0;
  if (num_buttons  > 0)
    { 
      expand = total_button_width + 32*(num_buttons-1) - text_width;
      if (expand < 0)
        expand = 0;
    }
  dialog_width += expand;
  text_width += expand;
  int button_left[4]; // Left coordinates for each button
  if (num_buttons <= 1)
    button_left[0] = left_border + text_width - button_widths[0];
  else
    { 
      int total_gap = text_width - total_button_width;
      button_left[0] = left_border;
      for (n=1; n < num_buttons; n++)
        { 
          int gap = total_gap / (num_buttons-n);
          total_gap -= gap;
          button_left[n] = button_left[n-1]+button_widths[n-1]+gap;
        }
    }

  // Finally, work out dialog height and vertical placement
  int len = (int) strlen(*msg);
  RECT text_rect;
  text_rect.left = text_rect.top = 0;
  text_rect.right = text_width; text_rect.bottom = text_height;
  dc->DrawText(*msg,len,&text_rect,DT_CALCRECT|DT_WORDBREAK);
  expand = text_rect.bottom - text_rect.top - text_height; // Might be -ve
  dialog_height += expand;
  text_height += expand;
  button_top += expand;
  if (num_buttons == 0)
    { 
      dialog_height -= (button_top+button_height);
      dialog_height += text_height;
    }

  // Release device context
  dc->SelectObject(old_font);
  get_textfield()->ReleaseDC(dc);

  // Resize the dialog and place all controls
  SetWindowPos(NULL,0,0,dialog_width,dialog_height,
               SWP_NOZORDER | SWP_NOMOVE | SWP_SHOWWINDOW);
  get_textfield()->SetWindowPos(NULL,left_border,0,text_width,text_height,
                                SWP_NOZORDER | SWP_NOMOVE | SWP_SHOWWINDOW);
  if (button_widths[0] == 0)
    get_button1()->ShowWindow(SW_HIDE);
  else
    get_button1()->SetWindowPos(NULL,button_left[0],button_top,
                                button_widths[0],button_height,
                                SWP_NOZORDER | SWP_SHOWWINDOW);
  if (button_widths[1] == 0)
    get_button2()->ShowWindow(SW_HIDE);
  else
    get_button2()->SetWindowPos(NULL,button_left[1],button_top,
                                button_widths[1],button_height,
                                SWP_NOZORDER | SWP_SHOWWINDOW);
  if (button_widths[2] == 0)
    get_button3()->ShowWindow(SW_HIDE);
  else
    get_button3()->SetWindowPos(NULL,button_left[2],button_top,
                                button_widths[2],button_height,
                                SWP_NOZORDER | SWP_SHOWWINDOW);
  if (button_widths[3] == 0)
    get_button4()->ShowWindow(SW_HIDE);
  else
    get_button4()->SetWindowPos(NULL,button_left[3],button_top,
                                button_widths[3],button_height,
                                SWP_NOZORDER | SWP_SHOWWINDOW);

  // Draw text to the controls
  get_textfield()->SetFont(&local_font);
  get_textfield()->SetWindowText(*msg);
  if (opt[0] != NULL)
    {
      get_button1()->SetFont(&local_font);
      get_button1()->SetWindowText(*(opt[0]));
    }
  if (opt[1] != NULL)
    { 
      get_button2()->SetFont(&local_font);
      get_button2()->SetWindowText(*(opt[1]));
    }
  if (opt[2] != NULL)
    { 
      get_button3()->SetFont(&local_font);
      get_button3()->SetWindowText(*(opt[2]));
    }
  if (opt[3] != NULL)
    { 
      get_button4()->SetFont(&local_font);
      get_button4()->SetWindowText(*(opt[3]));
    }

  return TRUE;
}

/*****************************************************************************/
/*                    kdws_interrogate_dlg::OnCtlColor                       */
/*****************************************************************************/

HBRUSH kdws_interrogate_dlg::OnCtlColor(CDC *dc, CWnd *wnd, UINT control_type)
{
  int id = wnd->GetDlgCtrlID();
  if (id == IDC_INTERROGATE_TEXT)
    dc->SetTextColor(RGB(0,0,128));
  dc->SetBkColor(bgnd_colour);
  return (HBRUSH) bgnd_brush.GetSafeHandle();
}

/* ========================================================================= */
/*                              kdws_image_view                              */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdws_image_view::kdws_image_view                     */
/*****************************************************************************/

kdws_image_view::kdws_image_view()
{
  manager = NULL;
  window = NULL;
  renderer = NULL;
  pixel_scale = last_pixel_scale = 1;
  max_view_size = kdu_coords(10000,10000);
  max_view_size_known = false;
  sizing = false;
  last_size = kdu_coords(0,0);
  scroll_step = kdu_coords(1,1);
  scroll_page = scroll_step;
  scroll_end = kdu_coords(100000,100000);
  screen_size.x = GetSystemMetrics(SM_CXSCREEN);
  screen_size.y = GetSystemMetrics(SM_CYSCREEN);
  
  scrollers_fixed = false;
  scrollers_fixed_pending = false;
  scroller_mutex.create();

  is_focussing = is_dragging = false;
  shape_dragging = shape_scribbling = false;
  mouse_start.x = mouse_start.y = 0;
  mouse_last = mouse_start;
  target_is_focus = false;
  have_keyboard_focus = false;

  HINSTANCE hinst = AfxGetInstanceHandle();
  normal_cursor = LoadCursor(NULL,IDC_CROSS);
  drag_cursor = LoadCursor(NULL,IDC_SIZEALL);
}

/*****************************************************************************/
/*                     kdws_image_view::~kdws_image_view                     */
/*****************************************************************************/

kdws_image_view::~kdws_image_view()
{
  scroller_mutex.destroy();
}

/*****************************************************************************/
/*                     kdws_image_view::set_max_view_size                    */
/*****************************************************************************/

void
  kdws_image_view::set_max_view_size(kdu_coords size, int pscale,
                                     bool first_time)
{
  max_view_size = size;
  pixel_scale = pscale;
  max_view_size.x*=pixel_scale;
  max_view_size.y*=pixel_scale;
  max_view_size_known = true;
  if ((last_size.x > 0) && (last_size.y > 0))
    { /* Otherwise, the windows message loop is bound to send us a
         WM_SIZE message some time soon, which will call the
         following function out of the 'OnSize' member function. */
      check_and_report_size(first_time);
    }
  last_pixel_scale = pixel_scale;
}

/*****************************************************************************/
/*                   kdws_image_view::check_and_report_size                  */
/*****************************************************************************/

void
  kdws_image_view::check_and_report_size(bool first_time)
{
  if (sizing)
    return; // We are already in the process of negotiating new dimensions.
  if ((!first_time) && max_view_size_known &&
      ((last_size.x | last_size.y) == 0))
    first_time = true;

  kdu_coords inner_size, outer_size, border_size;
  RECT rect;
  GetClientRect(&rect);
  inner_size.x = rect.right-rect.left;
  inner_size.y = rect.bottom-rect.top;
  kdu_coords target_size = inner_size;
  if (pixel_scale != last_pixel_scale)
    {
      target_size.x = (target_size.x * pixel_scale) / last_pixel_scale;
      target_size.y = (target_size.y * pixel_scale) / last_pixel_scale;
      last_pixel_scale = pixel_scale;
    }
  if (target_size.x > max_view_size.x)
    target_size.x = max_view_size.x;
  if (target_size.y > max_view_size.y)
    target_size.y = max_view_size.y;
  if (first_time)
    { // Can make window larger, if desired
      target_size = max_view_size;
      if (target_size.x > screen_size.x)
        target_size.x = screen_size.x;
      if (target_size.y > screen_size.y)
        target_size.y = screen_size.y;
    }
  target_size.x -= target_size.x % pixel_scale;
  target_size.y -= target_size.y % pixel_scale;

  if (inner_size != target_size)
    { // Need to resize windows -- ugh.
      sizing = true; // Guard against unwanted recursion.
      int iteration = 0;
      do {
          iteration++;
          // First determine the difference between the image view's client
          // area and the frame window's outer boundary.  This total border
          // area may change during resizing, which is the reason for the
          // loop.
          window->GetWindowRect(&rect);
          outer_size.x = rect.right-rect.left;
          outer_size.y = rect.bottom-rect.top;
          
          border_size = outer_size - inner_size;
          if (iteration > 4)
            {
              target_size = outer_size - border_size;
              if (target_size.x < pixel_scale)
                target_size.x = pixel_scale;
              if (target_size.y < pixel_scale)
                target_size.y = pixel_scale;
            }
          outer_size = target_size + border_size; // Assuming no border change
          iteration++;
          if (outer_size.x > screen_size.x)
            outer_size.x = screen_size.x;
          if (outer_size.y > screen_size.y)
            outer_size.y = screen_size.y;
          target_size = outer_size - border_size;
          window->SetWindowPos(NULL,0,0,outer_size.x,outer_size.y,
                               SWP_NOZORDER|SWP_NOMOVE|SWP_SHOWWINDOW);
          GetClientRect(&rect);
          inner_size.x = rect.right-rect.left;
          inner_size.y = rect.bottom-rect.top;
        } while ((inner_size.x != target_size.x) ||
                 (inner_size.y != target_size.y));
      target_size = inner_size; // If we could not achieve our original target,
      sizing = false;           // the image view will have to be a bit larger.
    }

  last_size = inner_size;
  if (first_time)
    {
      sizing = true;
      manager->place_window(window,outer_size,true);
      sizing = false;
    }
  if (renderer != NULL)
    {
      kdu_coords tmp = last_size;
      tmp.x = 1 + ((tmp.x-1)/pixel_scale);
      tmp.y = 1 + ((tmp.y-1)/pixel_scale);
      renderer->set_view_size(tmp);
    }
}

/*****************************************************************************/
/*                     kdws_image_view::PreCreateWindow                      */
/*****************************************************************************/

BOOL kdws_image_view::PreCreateWindow(CREATESTRUCT& cs) 
{
  if (!CWnd::PreCreateWindow(cs))
    return FALSE;

  cs.style &= ~WS_BORDER;
  cs.lpszClass = 
    AfxRegisterWndClass(CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS, 
                        ::LoadCursor(NULL, IDC_CROSS),
                        HBRUSH(COLOR_WINDOW+1), NULL);
  return TRUE;
}

/*****************************************************************************/
/*                  kdws_image_view::configure_scrollers                     */
/*****************************************************************************/

void kdws_image_view::configure_scrollers(kdu_dims viewport, kdu_dims canvas)
{
  scroller_viewport_unfixed = viewport;
  scroller_canvas_unfixed = canvas;
  if (!scrollers_fixed)
    update_scrollers();
}

/*****************************************************************************/
/*                    kdws_image_view::update_scrollers                      */
/*****************************************************************************/

void kdws_image_view::update_scrollers()
{
  kdu_dims viewport, canvas;
  if (scrollers_fixed)
    { viewport = scroller_viewport_fixed;
      canvas = scroller_canvas_fixed; }
  else
    { viewport = scroller_viewport_unfixed;
      canvas = scroller_canvas_unfixed; }

  SCROLLINFO sc_info; sc_info.cbSize = sizeof(sc_info);
  sc_info.fMask = SIF_DISABLENOSCROLL | SIF_ALL;
  sc_info.nMin = 0;
  sc_info.nMax = canvas.size.x-1;
  sc_info.nPage = viewport.size.x;
  sc_info.nPos = viewport.pos.x - canvas.pos.x;
  SetScrollInfo(SB_HORZ,&sc_info);

  sc_info.fMask = SIF_DISABLENOSCROLL | SIF_ALL;
  sc_info.nMin = 0;
  sc_info.nMax = canvas.size.y-1;
  sc_info.nPage = viewport.size.y;
  sc_info.nPos = viewport.pos.y - canvas.pos.y;
  SetScrollInfo(SB_VERT,&sc_info);

  kdu_coords step = viewport.size;
  step.x = (step.x >> 4) + 1;
  step.y = (step.y >> 4) + 1;
  kdu_coords page = viewport.size - step;
  set_scroll_metrics(step,page,canvas.size-viewport.size);
}

/*****************************************************************************/
/*                     kdws_image_view::fix_scrollers                        */
/*****************************************************************************/

void kdws_image_view::fix_scrollers(kdu_dims view_dims, kdu_dims image_dims)
{
  bool need_post = false;
  scroller_mutex.lock();
  scrollers_fixed_pending = true;
  scroller_viewport_pending = view_dims;
  scroller_canvas_pending = image_dims;
  need_post = ((!scrollers_fixed) || (scroller_viewport_fixed != view_dims) ||
               (scroller_canvas_fixed != image_dims));
  scroller_mutex.unlock();
  if (need_post)
    PostMessage(KDWS_SCROLLER_MESSAGE,0,0);
}

/*****************************************************************************/
/*                    kdws_image_view::release_scrollers                     */
/*****************************************************************************/

void kdws_image_view::release_scrollers(bool main_thread)
{
  bool need_update = false;
  bool need_post = false;
  scroller_mutex.lock();
  scrollers_fixed_pending = false;
  if (main_thread && scrollers_fixed)
    { 
      scrollers_fixed = false;
      need_update = true;
    }
  else
    need_post = scrollers_fixed;
  scroller_mutex.unlock();
  if (need_update)
    update_scrollers();
  else if (need_post)
    PostMessage(KDWS_SCROLLER_MESSAGE,0,0);
}

/*****************************************************************************/
/*                 kdws_image_view::apply_scroller_fixing                    */
/*****************************************************************************/

LRESULT kdws_image_view::apply_scroller_fixing(WPARAM wparam, LPARAM lparam)
{
  scroller_mutex.lock();

  // Find current parameters
  kdu_dims cur_viewport, cur_canvas;
  if (scrollers_fixed)
    { cur_viewport = scroller_viewport_fixed;
      cur_canvas = scroller_canvas_fixed; }
  else
    { cur_viewport = scroller_viewport_unfixed;
      cur_canvas = scroller_canvas_unfixed; }

  // Find new parameters
  kdu_dims new_viewport, new_canvas;
  scrollers_fixed = scrollers_fixed_pending;
  if (scrollers_fixed)
    { 
      new_viewport = scroller_viewport_fixed = scroller_viewport_pending;
      new_canvas = scroller_canvas_fixed = scroller_canvas_pending;
    }
  else
    { 
      new_viewport = scroller_viewport_unfixed;
      new_canvas = scroller_canvas_unfixed;
    }
  scroller_mutex.unlock();
  if ((new_viewport != cur_viewport) || (new_canvas != cur_canvas))
    update_scrollers();
  return 0;
}

/*****************************************************************************/
/*                     kdws_image_view::set_drag_cursor                      */
/*****************************************************************************/

void kdws_image_view::set_drag_cursor(bool set_drag)
{
  if (set_drag)
    SetCursor(drag_cursor);
  else
    SetCursor(normal_cursor);
}

/*****************************************************************************/
/*                         kdws_image_view::OnPaint                          */
/*****************************************************************************/

void kdws_image_view::OnPaint() 
{
  if (renderer == NULL)
    return;
  PAINTSTRUCT paint_struct;
  CDC *dc = BeginPaint(&paint_struct);

  renderer->paint_region(paint_struct.rcPaint,dc);

  if (renderer->get_shape_editor() != NULL)
    {
      int n, flags, sel, drag;
      for (drag=0; drag < 2; drag++)
        { 
          CPen pen, *old_pen;
          LOGBRUSH lbrush;
          int penwidth;

          // Draw edges, highlighting selected edges on the correct side
          for (sel=0; sel < ((drag)?2:3); sel++)
            { 
              int mask=-1;
              if (sel == 1)
                mask = JPX_EDITOR_FLAG_SHARED;
              else if (sel == 2)
                mask = JPX_EDITOR_FLAG_SELECTED;
              lbrush.lbStyle = BS_SOLID;
              if (sel == 0)
                lbrush.lbColor = RGB(255,255,255); // white
              else if (sel == 1)
                lbrush.lbColor = RGB(128,128,128); // grey
              else
                lbrush.lbColor = RGB(255,255,0); // yellow
              penwidth = (sel==2)?3:1;
              if (drag)
                pen.CreatePen(PS_DOT|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
              else
                pen.CreatePen(PS_SOLID|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
              old_pen = dc->SelectObject(&pen);
              POINT v1, v2;
              for (n=0; (flags=renderer->get_shape_edge(v1,v2,n,(sel==2),
                                                        (drag>0))); n++)
                { 
                  if (((v1.x==v1.y) && (v2.x==v2.y)) || !(flags & mask))
                    continue;
                  if (sel == 2)
                    { 
                      double dx=(v2.y-v1.y), dy=-(v2.x-v1.x);
                      double norm = 3.0 / sqrt(dx*dx + dy*dy);
                      v1.x += (int) floor(dx*norm+0.5);
                      v2.x += (int) floor(dx*norm+0.5);
                      v1.y += (int) floor(dy*norm+0.5);
                      v2.y += (int) floor(dy*norm+0.5);
                    }
                  dc->MoveTo(v1);
                  dc->LineTo(v2);
                }
              dc->SelectObject(old_pen);
              pen.DeleteObject();
            }

          // Draw ellipses
          lbrush.lbStyle = BS_SOLID;
          lbrush.lbColor = RGB(255,255,255); // white
          bool selected = ((flags & JPX_EDITOR_FLAG_SELECTED) != 0);
          penwidth = (selected)?3:1;
          if (drag)
            pen.CreatePen(PS_DOT|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
          else
            pen.CreatePen(PS_SOLID|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
          old_pen = dc->SelectObject(&pen);
          POINT centre; double extent_x, extent_y; double alpha;
          for (n=0;
               (flags=renderer->get_shape_curve(centre,extent_x,extent_y,
                                                alpha,n,false,(drag>0)));
               n++)
            { 
              POINT bezier_pts[13];
              kd_ellipse_to_bezier(bezier_pts,centre,extent_x,extent_y,alpha);
              dc->BeginPath();
              dc->PolyBezier(bezier_pts,13);
              dc->EndPath();
              dc->StrokePath();
            }
          dc->SelectObject(old_pen);
          pen.DeleteObject();
          
          if (!drag)
            { // Draw path outline
              lbrush.lbStyle = BS_SOLID;
              lbrush.lbColor = RGB(255,0,0); // red
              pen.CreatePen(PS_DASH|PS_GEOMETRIC,3,&lbrush,0,NULL);
              old_pen = dc->SelectObject(&pen);
              POINT v1, v2;
              for (n=0; renderer->get_shape_path(v1,v2,n); n++)
                if ((v1.x!=v1.y) || (v2.x!=v2.y))
                  { 
                    dc->MoveTo(v1);
                    dc->LineTo(v2);
                  }
              dc->SelectObject(old_pen);
              pen.DeleteObject();
            }
          
          // Draw anchor points, highlighting the selected anchor point
          for (sel=0; sel < 2; sel++)
            { 
              int mask=(sel>0)?JPX_EDITOR_FLAG_SELECTED:-1;
              CPen pen;
              LOGBRUSH lbrush;
              lbrush.lbStyle = BS_SOLID;
              lbrush.lbColor = RGB(255,255,0); // yellow
              int penwidth = (sel > 0)?3:1;
              if (drag)
                pen.CreatePen(PS_DOT|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
              else
                pen.CreatePen(PS_SOLID|PS_GEOMETRIC,penwidth,&lbrush,0,NULL);
              CPen *old_pen = dc->SelectObject(&pen);
              POINT v;
              for (n=0; (flags=renderer->get_shape_anchor(v,n,(sel>0),
                                                          (drag>0))); n++)
                { 
                  if (!(flags & mask))
                    continue;
                  RECT rect;
                  v.x -= 4;  v.y -= 4;
                  rect.left = v.x;  rect.right = v.x+9;
                  rect.top = v.y;   rect.bottom = v.y+9;
                  dc->Arc(&rect,v,v);
                }
              dc->SelectObject(old_pen);
              pen.DeleteObject();
            }
        }
      
      if (renderer->get_shape_scribble_mode())
        { // Draw scribble path
          CPen pen;
          pen.CreatePen(PS_SOLID,1,RGB(200,100,0)); // Orange pen, width 1
          CPen *old_pen = dc->SelectObject(&pen);
          POINT v;
          if (renderer->get_scribble_point(v,0))
            { 
              dc->MoveTo(v);
              for (n=1; renderer->get_scribble_point(v,n); n++)
                dc->LineTo(v);
            }
          dc->SelectObject(old_pen);
          pen.DeleteObject();
        }
    }

  EndPaint(&paint_struct);
}

/*****************************************************************************/
/*                        kdws_image_view::OnHScroll                         */
/*****************************************************************************/

void kdws_image_view::OnHScroll(UINT nSBCode, UINT nPos,
                                CScrollBar* pScrollBar) 
{
  if (renderer == NULL)
    return;
  if (check_scrollers_fixed())
    {
      update_scrollers(); // Put the scroll bars back in place!
      return;
    }
  if ((nSBCode == SB_THUMBPOSITION) || (nSBCode == SB_THUMBTRACK))
    {
      SCROLLINFO sc_info;
      GetScrollInfo(SB_HORZ,&sc_info);
      renderer->set_hscroll_pos(sc_info.nTrackPos);
    }
  else if (nSBCode == SB_LINELEFT)
    renderer->set_hscroll_pos(-scroll_step.x,true);
  else if (nSBCode == SB_LINERIGHT)
    renderer->set_hscroll_pos(scroll_step.x,true);
  else if (nSBCode == SB_PAGELEFT)
    renderer->set_hscroll_pos(-scroll_page.x,true);
  else if (nSBCode == SB_PAGERIGHT)
    renderer->set_hscroll_pos(scroll_page.x,true);
  else if (nSBCode == SB_LEFT)
    renderer->set_hscroll_pos(0);
  else if (nSBCode == SB_RIGHT)
    renderer->set_hscroll_pos(scroll_end.x);
}

/*****************************************************************************/
/*                         kdws_image_view::OnVScroll                        */
/*****************************************************************************/

void kdws_image_view::OnVScroll(UINT nSBCode, UINT nPos,
                                CScrollBar* pScrollBar) 
{
  if (renderer == NULL)
    return;
  if (check_scrollers_fixed())
    { 
      update_scrollers(); // Put the scroll bars back in place!
      return;
    }
  if ((nSBCode == SB_THUMBPOSITION) || (nSBCode == SB_THUMBTRACK))
    {
      SCROLLINFO sc_info;
      GetScrollInfo(SB_VERT,&sc_info);
      renderer->set_vscroll_pos(sc_info.nTrackPos);
    }
  else if (nSBCode == SB_LINEUP)
    renderer->set_vscroll_pos(-scroll_step.y,true);
  else if (nSBCode == SB_LINEDOWN)
    renderer->set_vscroll_pos(scroll_step.y,true);
  else if (nSBCode == SB_PAGEUP)
    renderer->set_vscroll_pos(-scroll_page.y,true);
  else if (nSBCode == SB_PAGEDOWN)
    renderer->set_vscroll_pos(scroll_page.y,true);
  else if (nSBCode == SB_TOP)
    renderer->set_vscroll_pos(0);
  else if (nSBCode == SB_BOTTOM)
    renderer->set_vscroll_pos(scroll_end.y);
}

/*****************************************************************************/
/*                  kdws_image_view::get_last_mouse_point                    */
/*****************************************************************************/

kdu_coords kdws_image_view::get_last_mouse_point()
{
  kdu_coords result;
  if (renderer != NULL)
    result = renderer->convert_point_from_display_view(mouse_last);
  return result;
}

/*****************************************************************************/
/*                       kdws_image_view::start_focusbox                     */
/*****************************************************************************/

void kdws_image_view::start_focusbox(POINT point)
{
  if (renderer == NULL)
    return;
  if (is_focussing || is_dragging)
    cancel_focus_drag();
  mouse_start = mouse_last = point;
  is_focussing = true;
  RECT rect = get_rectangle_from_mouse_start_to_point(point);
  kdu_dims dims = renderer->convert_region_from_display_view(rect);
  renderer->set_focussing_rect(dims,true);
}

/*****************************************************************************/
/*                      kdws_image_view::start_viewdrag                      */
/*****************************************************************************/

void kdws_image_view::start_viewdrag(POINT point)
{
  if (renderer == NULL)
    return;
  if (is_focussing || is_dragging)
    cancel_focus_drag();
  is_dragging = true;
  mouse_start = mouse_last = point;
  kdu_dims box;
  target_is_focus =
    ((renderer->get_focus_box(box) > 0) &&
     box.contains(renderer->convert_point_from_display_view(point)));
  SetCapture();
  set_drag_cursor(true);
}

/*****************************************************************************/
/*                    kdws_image_view::cancel_focus_drag                     */
/*****************************************************************************/

void kdws_image_view::cancel_focus_drag()
{
  if (renderer == NULL)
    return;
  if (is_dragging)
    { 
      is_dragging = false;
      ReleaseCapture();
      set_drag_cursor(false);
    }
  if (is_focussing)
    { 
      is_focussing = false;
      renderer->set_focussing_rect(kdu_dims(),false);
      invalidate_mouse_box();
    }
  if (shape_dragging)
    {
      jpx_roi_editor *sed = renderer->get_shape_editor();
      if (sed == NULL)
        return;
      kdu_dims update_region = sed->cancel_drag();
      if (!update_region.is_empty())
        {
          repaint_shape_region(update_region);
          renderer->editor->update_shape_editor_controls();
        }
    }
  shape_dragging = false;
  if (shape_scribbling)
    {
      jpx_roi_editor *sed = renderer->get_shape_editor();
      if (sed == NULL)
        return;
      kdu_dims update_region = sed->clear_scribble_points();
      if (!update_region.is_empty())
        {
          repaint_shape_region(update_region);
          renderer->editor->update_shape_editor_controls();
        }      
    }
  shape_scribbling = false;
}

/*****************************************************************************/
/*                   kdws_image_view::mouse_drag_to_point                    */
/*****************************************************************************/

void kdws_image_view::mouse_drag_to_point(POINT point)
{
  if ((renderer != NULL) && is_focussing)
    { 
      RECT rect = get_rectangle_from_mouse_start_to_point(point);
      kdu_dims dims = renderer->convert_region_from_display_view(rect);
      renderer->set_focussing_rect(dims,true);
      invalidate_mouse_box();
      mouse_last = point;
      invalidate_mouse_box();
    }
  else if ((renderer != NULL) && is_dragging &&
           !check_scrollers_fixed())
    {
      kdu_coords disp = renderer->convert_point_from_display_view(point);
      disp -= renderer->convert_point_from_display_view(mouse_last);
      if (!target_is_focus)
        renderer->set_scroll_pos(disp.x,disp.y,true);
      else
        renderer->displace_focus_box(disp);
      mouse_last = point;
    }
}

/*****************************************************************************/
/*                     kdws_image_view::end_focus_drag                       */
/*****************************************************************************/

bool kdws_image_view::end_focus_drag(POINT point)
{
  if (renderer == NULL)
    return false;
  if (is_focussing)
    {
      RECT rect = get_rectangle_from_mouse_start_to_point(point);
      cancel_focus_drag();
      if (((rect.right-rect.left) >= 2) && ((rect.bottom-rect.top) > 2))
        {
          kdu_dims focus_box=renderer->convert_region_from_display_view(rect);
          if ((focus_box.size.x > 1) && (focus_box.size.y > 1))
            renderer->set_focus_box(focus_box,jpx_metanode());
          return true;
        }
    }
  else if (is_dragging)
    {
      is_dragging = false;
      target_is_focus = false;
      ReleaseCapture();
      set_drag_cursor(false);
      return true;
    }
  return false;
}

/*****************************************************************************/
/*          kdws_image_view::get_rectangle_from_mouse_start_to_point         */
/*****************************************************************************/

RECT kdws_image_view::get_rectangle_from_mouse_start_to_point(POINT point)
{
  RECT rect;
  if (mouse_start.x <= point.x)
    {
      rect.left = mouse_start.x;
      rect.right = point.x + 1;
    }
  else
    {
      rect.left = point.x;
      rect.right = mouse_start.x + 1;
    }
  
  if (mouse_start.y <= point.y)
    {
      rect.top = mouse_start.y;
      rect.bottom = point.y + 1;
    }
  else
    {
      rect.top = point.y;
      rect.bottom = mouse_start.y + 1;
    }
  return rect;
}

/*****************************************************************************/
/*                   kdws_image_view::invalidate_mouse_box                   */
/*****************************************************************************/

void kdws_image_view::invalidate_mouse_box()
{
  RECT dirty_rect = get_rectangle_from_mouse_start_to_point(mouse_last);
  dirty_rect.left -= KDWS_FOCUSSING_MARGIN;
  dirty_rect.right += KDWS_FOCUSSING_MARGIN;
  dirty_rect.top -= KDWS_FOCUSSING_MARGIN;
  dirty_rect.bottom += KDWS_FOCUSSING_MARGIN;
  this->InvalidateRect(&dirty_rect,FALSE);
}

/*****************************************************************************/
/*                   kdws_image_view::handle_single_click                    */
/*****************************************************************************/

void kdws_image_view::handle_single_click(POINT point)
{
  if (renderer == NULL)
    return;
  kdu_coords coords = renderer->convert_point_from_display_view(point);
  renderer->reveal_metadata_at_point(coords);  
}

/*****************************************************************************/
/*                   kdws_image_view::handle_double_click                    */
/*****************************************************************************/

void kdws_image_view::handle_double_click(POINT point)
{
  if (renderer == NULL)
    return;
  kdu_coords coords = renderer->convert_point_from_display_view(point);
  renderer->reveal_metadata_at_point(coords,true);
}

/*****************************************************************************/
/*                   kdws_image_view::handle_right_click                     */
/*****************************************************************************/

void kdws_image_view::handle_right_click(POINT point)
{
  if (renderer == NULL)
    return;
  kdu_coords coords = renderer->convert_point_from_display_view(point);
  renderer->edit_metadata_at_point(&coords);
}

/*****************************************************************************/
/*              kdws_image_view::handle_shape_editor_click                   */
/*****************************************************************************/

void kdws_image_view::handle_shape_editor_click(POINT point)
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_display_view(point,shape_point);
  if (sed == NULL)
    return;
  kdu_dims update_region;
  if (renderer->get_shape_scribble_mode())
    {
      update_region = sed->clear_scribble_points();
      if (!update_region.is_empty())
        renderer->editor->update_shape_editor_controls();
      shape_scribbling = true;
      update_region.augment(sed->add_scribble_point(shape_point));
    }
  else
    {
      if (!sed->find_nearest_anchor(shape_point,false))
        return;
      POINT nearest;
      renderer->map_shape_point_to_display_view(shape_point,nearest);
      double dx=nearest.x-point.x, dy=nearest.y-point.y;
      if ((dx*dx+dy*dy) < 4.5*4.5)
        { 
          shape_dragging = true;
          mouse_start = mouse_last = point;
          update_region = sed->select_anchor(shape_point,true);
        }
      else
        update_region = sed->cancel_selection();
      if (!update_region.is_empty())
        renderer->editor->update_shape_editor_controls();
    }
  if (!update_region.is_empty())
    repaint_shape_region(update_region);
}

/*****************************************************************************/
/*               kdws_image_view::handle_shape_editor_drag                   */
/*****************************************************************************/

void kdws_image_view::handle_shape_editor_drag(POINT point)
{
  kdu_coords shape_point;
  if (snap_doc_point_to_shape(point,&shape_point))
    {
      jpx_roi_editor *sed = renderer->get_shape_editor();
      kdu_dims update_region = sed->drag_selected_anchor(shape_point);
      if (!update_region.is_empty())
        repaint_shape_region(update_region);
      mouse_last = point;
    }
}

/*****************************************************************************/
/*               kdws_image_view::handle_shape_editor_move                   */
/*****************************************************************************/

void kdws_image_view::handle_shape_editor_move(POINT point)
{
  handle_shape_editor_drag(point);
  point = mouse_last; // Ensure we move to the last valid point  
  kdu_coords shape_point;
  shape_dragging = false;
  jpx_roi_editor *sed = renderer->get_shape_editor();
  if (sed == NULL)
    return;
  kdu_dims update_region;
  if (((point.x == mouse_start.x) && (point.y == mouse_start.y)) ||
      !snap_doc_point_to_shape(point,&shape_point))
    update_region = sed->cancel_drag();
  else
    { 
      update_region = sed->move_selected_anchor(shape_point);
      update_region.augment(renderer->shape_editor_adjust_path_thickness());
    }
  renderer->editor->update_shape_editor_controls();
  if (!update_region.is_empty())
    repaint_shape_region(update_region);
}

/*****************************************************************************/
/*               kdws_image_view::add_shape_scribble_point                   */
/*****************************************************************************/

void kdws_image_view::add_shape_scribble_point(POINT point)
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_display_view(point,shape_point);
  if (sed != NULL)
    {
      kdu_dims update_region =
        sed->add_scribble_point(shape_point);
      if (!update_region.is_empty())
        repaint_shape_region(update_region);
    }
}

/*****************************************************************************/
/*               kdws_image_view::snap_doc_point_to_shape                    */
/*****************************************************************************/

bool kdws_image_view::snap_doc_point_to_shape(POINT point,
                                              kdu_coords *snapped_point)
{
  kdu_coords shape_point;
  jpx_roi_editor *sed =
    renderer->map_shape_point_from_display_view(point,shape_point);
  if (sed == NULL)
    return false;
  kdu_coords test_shape_point;
  POINT test_doc_point;
  kdu_coords nearest_shape_point = shape_point;
  double min_doc_dist_sq = 20.0; // Snap only if dist^2 is less than this
  double dx, dy, dist_sq;
  
  test_shape_point = shape_point;
  if (sed->find_nearest_anchor(test_shape_point,true))
    { // Consider snapping to a non-selected anchor point
      renderer->map_shape_point_to_display_view(test_shape_point,
                                                test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { 
          nearest_shape_point = test_shape_point;
          if (sed->can_move_selected_anchor(nearest_shape_point,false))
            { *snapped_point = nearest_shape_point; return true; }
        }
    }
  
  test_shape_point = shape_point;
  if (sed->find_nearest_boundary_point(test_shape_point,true))
    { // Consider snapping to the boundary of any non-selected region
      renderer->map_shape_point_to_display_view(test_shape_point,
                                                test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { min_doc_dist_sq = dist_sq; nearest_shape_point = test_shape_point; }
    }
  
  test_shape_point = shape_point;
  if (sed->find_nearest_guide_point(test_shape_point))
    { // Consider snapping to the nearest guide line -- see definitions
      renderer->map_shape_point_to_display_view(test_shape_point,
                                                test_doc_point);
      dx = test_doc_point.x-point.x;  dy = test_doc_point.y-point.y;
      dist_sq = dx*dx + dy*dy;
      if (dist_sq < min_doc_dist_sq)
        { min_doc_dist_sq = dist_sq; nearest_shape_point = test_shape_point; }
    }
  
  if (!sed->can_move_selected_anchor(nearest_shape_point,false))
    return false;
  *snapped_point = nearest_shape_point;
  return true;
}

/*****************************************************************************/
/*                  kdws_image_view::repaint_shape_region                    */
/*****************************************************************************/

void kdws_image_view::repaint_shape_region(kdu_dims region)
{
  kdu_coords min=region.pos, max=min+region.size; min -= kdu_coords(1,1);
  POINT p1, p2;
  if ((renderer == NULL) ||
      !(renderer->map_shape_point_to_display_view(min,p1) &&
        renderer->map_shape_point_to_display_view(max,p2)))
    return;
  RECT rect;
  if (p1.x < p2.x)
    { rect.left = p1.x; rect.right = p2.x+1; }
  else
    { rect.left = p2.x; rect.right = p1.x+1; }
  if (p1.y < p2.y)
    { rect.top = p1.y; rect.bottom = p2.y+1; }
  else
    { rect.top = p2.y; rect.bottom = p1.y+1; }
  rect.left -= 7; rect.top -= 7;
  rect.right += 7; rect.bottom += 7;
  this->InvalidateRect(&rect,FALSE);
}

/*****************************************************************************/
/*                       kdws_image_view::OnNcHitTest                        */
/*****************************************************************************/

LRESULT
  kdws_image_view::OnNcHitTest(CPoint point) 
{
  if (scrollers_fixed)
    return HTNOWHERE; // Prevent highlighting of scrollbars
  return CWnd::OnNcHitTest(point);
}

/*****************************************************************************/
/*                     kdws_image_view::OnNcLButtonDown                      */
/*****************************************************************************/

void
  kdws_image_view::OnNcLButtonDown(UINT nFlags, CPoint point) 
{
  if (scrollers_fixed)
    return; // The only things in the non-client area of the window are the
            // horizontal and vertical scrollbars; we don't want the user
            // messing with them.
  CWnd::OnNcLButtonDown(nFlags, point);
}

/*****************************************************************************/
/*                       kdws_image_view::OnLButtonDown                      */
/*****************************************************************************/

void
  kdws_image_view::OnLButtonDown(UINT nFlags, CPoint point) 
{
  window->cancel_pending_show();
  if (nFlags & MK_CONTROL)
    handle_right_click(point);
  else if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    handle_shape_editor_click(point);
  else if (nFlags & MK_SHIFT)
    start_viewdrag(point);
  else
    start_focusbox(point);
  CWnd::OnLButtonDown(nFlags, point);
}

/*****************************************************************************/
/*                     kdws_image_view::OnNcRButtonDown                      */
/*****************************************************************************/

void
  kdws_image_view::OnNcRButtonDown(UINT nFlags, CPoint point) 
{
  if (scrollers_fixed)
    return; // The only things in the non-client area of the window are the
            // horizontal and vertical scrollbars; we don't want the user
            // messing with them.
  CWnd::OnNcRButtonDown(nFlags, point);
}

/*****************************************************************************/
/*                        kdws_image_view::OnLButtonUp                       */
/*****************************************************************************/

void
  kdws_image_view::OnLButtonUp(UINT nFlags, CPoint point) 
{
  window->cancel_pending_show();
  if (is_focussing && !end_focus_drag(point))
    handle_single_click(point);
  else if (is_dragging)
    end_focus_drag(point);
  if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    { 
      if (shape_dragging)
        handle_shape_editor_move(point);
      else if (shape_scribbling)
        {
          add_shape_scribble_point(point);
          renderer->editor->update_shape_editor_controls();
          shape_scribbling = false;
        }
    }
  shape_dragging = false;
  CWnd::OnLButtonUp(nFlags, point);
}

/*****************************************************************************/
/*                    kdws_image_view::OnLButtonDblClk                       */
/*****************************************************************************/

void
  kdws_image_view::OnLButtonDblClk(UINT nFlags, CPoint point) 
{
  window->cancel_pending_show();
  cancel_focus_drag();
  handle_double_click(point);
}

/*****************************************************************************/
/*                      kdws_image_view::OnRButtonDown                       */
/*****************************************************************************/

void kdws_image_view::OnRButtonDown(UINT nFlags, CPoint point) 
{
  window->cancel_pending_show();
  cancel_focus_drag();
  handle_right_click(point);
}

/*****************************************************************************/
/*                       kdws_image_view::OnMouseMove                        */
/*****************************************************************************/

void
  kdws_image_view::OnMouseMove(UINT nFlags, CPoint point) 
{
  if ((renderer != NULL) && (renderer->get_shape_editor() != NULL))
    { 
      if (shape_dragging)
        handle_shape_editor_drag(point);
      else if (shape_scribbling)
        add_shape_scribble_point(point);
    }
  else if (is_dragging || is_focussing)
    mouse_drag_to_point(point);
  mouse_last = point; // So `get_last_mouse_point' always works
}

/*****************************************************************************/
/*                     kdws_image_view::OnMouseActivate                      */
/*****************************************************************************/

int
  kdws_image_view::OnMouseActivate(CWnd* pDesktopWnd,
                                   UINT nHitTest, UINT message) 
{
  if (this != GetFocus())
    { // Mouse click is activating window; prevent further mouse messages
      CWnd::OnMouseActivate(pDesktopWnd,nHitTest,message);
      return MA_ACTIVATEANDEAT;
    }
  else
    return CWnd::OnMouseActivate(pDesktopWnd,nHitTest,message);
}

/*****************************************************************************/
/*                     kdws_image_view::OnMouseWheel                         */
/*****************************************************************************/

BOOL
  kdws_image_view::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) 
{
  if (renderer == NULL)
    return FALSE;
  if ( nFlags & MK_CONTROL )
    { // Zoom
      if ( zDelta > 0 )
        renderer->menu_ViewZoomInSlightly();
      else
        renderer->menu_ViewZoomOutSlightly();
    }
  else if (!check_scrollers_fixed())
    { // Scroll
      if (nFlags & MK_SHIFT)          // Horizontal
        renderer->set_hscroll_pos(zDelta/10,true);
      else                            // Vertical
        renderer->set_vscroll_pos(-zDelta/10,true);
    }
  return TRUE;
}

/*****************************************************************************/
/*                       kdws_image_view::OnKeyDown                          */
/*****************************************************************************/

void kdws_image_view::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
  if (renderer == NULL)
    {
      CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
      return;
    }
  if (is_dragging)
    return;
  window->cancel_pending_show();
  cancel_focus_drag();
  bool control_key_down = (GetKeyState(VK_CONTROL) < 0);
  bool shift_key_down = (GetKeyState(VK_SHIFT) < 0);
  kdu_dims shape_regn;
  int na = (control_key_down)?1:10; // Nudge amount
  if (nChar == VK_LEFT)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_hscroll_pos(-scroll_step.x,true);
          return;
        }
      else if (renderer->nudge_selected_shape_points(-na,0,shape_regn))
        { repaint_shape_region(shape_regn); return; }
      else
        renderer->menu_FocusLeft();
    }
  else if (nChar == VK_RIGHT)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_hscroll_pos(scroll_step.x,true);
          return;
        }
      else if (renderer->nudge_selected_shape_points(na,0,shape_regn))
        { repaint_shape_region(shape_regn); return; }
      else
        renderer->menu_FocusRight();
    }
  else if (nChar == VK_UP)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_vscroll_pos(-scroll_step.y,true);
          return;
        }
      else if (renderer->nudge_selected_shape_points(0,-na,shape_regn))
        { repaint_shape_region(shape_regn); return; }
      else
        renderer->menu_FocusUp();
    }
  else if (nChar == VK_DOWN)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_vscroll_pos(scroll_step.y,true);
          return;
        }
      else if (renderer->nudge_selected_shape_points(0,na,shape_regn))
        { repaint_shape_region(shape_regn); return; }
      else
        renderer->menu_FocusDown();
    }
  else if (nChar == VK_PRIOR)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_vscroll_pos(-scroll_page.y,true);
          return;
        }
    }
  else if (nChar == VK_NEXT)
    { 
      if (!shift_key_down)
        { 
          if (!check_scrollers_fixed())
            renderer->set_vscroll_pos(scroll_page.y,true);
          return;
        }
    }
  else if (nChar == VK_RETURN)
    { 
      if (!shift_key_down)
        { renderer->menu_NavImageNext(); return; }
    }
  else if (nChar == VK_BACK)
    { 
      if (!shift_key_down)
        { renderer->menu_NavImagePrev(); return; }
    }
  CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

/*****************************************************************************/
/*                       kdws_image_view::OnSetFocus                         */
/*****************************************************************************/

void kdws_image_view::OnSetFocus(CWnd *pOldWnd)
{
  CWnd::OnSetFocus(pOldWnd);
  this->have_keyboard_focus = true;
}

/*****************************************************************************/
/*                      kdws_image_view::OnKillFocus                         */
/*****************************************************************************/

void
  kdws_image_view::OnKillFocus(CWnd* pNewWnd) 
{
  CWnd ::OnKillFocus(pNewWnd);
  this->have_keyboard_focus = false;
  cancel_focus_drag();
}

/*****************************************************************************/
/*                    Message Handlers for kdws_image_view                   */
/*****************************************************************************/

BEGIN_MESSAGE_MAP(kdws_image_view,CWnd)
  ON_MESSAGE(KDWS_SCROLLER_MESSAGE, apply_scroller_fixing)
	ON_WM_PAINT()
	ON_WM_KEYDOWN()
	ON_WM_HSCROLL()
	ON_WM_VSCROLL()
	ON_WM_MOUSEMOVE()
  ON_WM_NCHITTEST()
  ON_WM_NCLBUTTONDOWN()
	ON_WM_LBUTTONDOWN()
  ON_WM_NCRBUTTONDOWN()
	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEACTIVATE()
	ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDBLCLK()
	ON_WM_KEYUP()
  ON_WM_MOUSEWHEEL()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
END_MESSAGE_MAP()


/* ========================================================================= */
/*                          kdws_animation_bar                               */
/* ========================================================================= */

/*****************************************************************************/
/*                kdws_animation_bar::kdws_animation_bar                     */
/*****************************************************************************/

kdws_animation_bar::kdws_animation_bar()
{
  line_height = 0;
  bar_width = 0;
  image_view = NULL;
  target= NULL;
  show_indices = false;
  first_frame = last_frame = false;
  track_interval = 1.0;
  cur_frame_start = cur_frame_end = 0.0;
  slider_start = 0.0;
  slider_end = 0.0;
  slider_interval = 10.0;
  slider_step = 1.0;

  play_repeat = false;
  play_fwd = play_rev = false;
  custom_rate = -1.0;
  rate_multiplier = 1.0;

  allow_auto_slide = true;
  allow_positioning = true; // We may use this in the future for special
                            // sources, for which user positioning of the
                            // animation playback point makes no sense.

  kdws_create_preferred_window_font(&fixed_font,0.8,true);
  kdws_create_preferred_window_font(&var_font,0.8,false);
  background_brush.CreateSolidBrush(RGB(240,240,240));

  fwd_play_enabled = rev_play_enabled = stop_play_enabled = true;
  fwd_step_enabled = rev_step_enabled = true; step_button_hilite = false;
}

/*****************************************************************************/
/*                kdws_animation_bar::~kdws_animation_bar                    */
/*****************************************************************************/

kdws_animation_bar::~kdws_animation_bar()
{
  fixed_font.DeleteObject();
  var_font.DeleteObject();
  background_brush.DeleteObject();
}

/*****************************************************************************/
/*                       kdws_animation_bar::reset                           */
/*****************************************************************************/

void kdws_animation_bar::reset()
{
  target = NULL;
  custom_rate = rate_multiplier = -1.0; // So `set_state' does something
  set_state(false,false,false,-1.0,1.0);
  show_indices = false;
  first_frame = last_frame = false;
  cur_frame_start = cur_frame_end = 0.0;
  track_interval = -1.0;
  slider_start = slider_end = 0.0;
  slider_interval = 10.0;
  slider_step = 1.0;
  allow_positioning = true;
  left_timefield.SetWindowText(_T("00:00:00"));
  right_timefield.SetWindowText(_T("+10sec"));
  left_timebutton.EnableWindow(TRUE);
  central_slider.EnableWindow(TRUE);
  right_timebutton.EnableWindow(TRUE);
  fwd_playbutton.EnableWindow(TRUE);
  go_startbutton.EnableWindow(TRUE);
  go_endbutton.EnableWindow(TRUE);
  rev_playbutton.EnableWindow(TRUE);
  stop_playbutton.EnableWindow(FALSE);
  fwd_stepbutton.EnableWindow(TRUE);
  rev_stepbutton.EnableWindow(TRUE);
  step_hilitebox.ShowWindow(SW_HIDE);
  fwd_play_enabled = rev_play_enabled = true;
  stop_play_enabled = false;
  fwd_step_enabled = rev_step_enabled = true;
  step_button_hilite = false;
}

/*****************************************************************************/
/*                 kdws_animation_bar::update_play_controls                  */
/*****************************************************************************/

void kdws_animation_bar::update_play_controls()
{
  bool at_end = last_frame ||
    ((track_interval > 0.0) && (cur_frame_end >= track_interval));
  bool at_start = first_frame || (cur_frame_start <= 0.0);
  bool new_fwd_enabled = !(play_fwd || (at_end && !play_repeat));
  bool new_rev_enabled = !(play_rev || (at_start && !play_repeat));
  if (new_fwd_enabled != fwd_play_enabled)
    { 
      fwd_play_enabled = new_fwd_enabled;
      fwd_playbutton.EnableWindow((fwd_play_enabled)?TRUE:FALSE);
    }
  if (new_rev_enabled != rev_play_enabled)
    { 
      rev_play_enabled = new_rev_enabled;
      rev_playbutton.EnableWindow((rev_play_enabled)?TRUE:FALSE);
    }
  bool new_stop_enabled = play_fwd || play_rev;
  if (new_stop_enabled != stop_play_enabled)
    { 
      stop_play_enabled = new_stop_enabled;
      stop_playbutton.EnableWindow((stop_play_enabled)?TRUE:FALSE);
    }
  if (step_button_hilite && (cur_frame_start < cur_frame_end))
    { 
      step_button_hilite = false;
      step_hilitebox.ShowWindow(SW_HIDE);
    }
  else if ((cur_frame_start >= cur_frame_end) && !step_button_hilite)
    { 
      step_button_hilite = true;
      step_hilitebox.ShowWindow(SW_SHOW);
    }
  bool can_step_fwd = allow_positioning && !last_frame;
  if (fwd_step_enabled != can_step_fwd)
    { 
      fwd_step_enabled = can_step_fwd;
      fwd_stepbutton.EnableWindow((fwd_step_enabled)?TRUE:FALSE);
    }
  bool can_step_bwd = allow_positioning && !first_frame;
  if (rev_step_enabled != can_step_bwd)
    { 
      rev_step_enabled = can_step_bwd;
      rev_stepbutton.EnableWindow((rev_step_enabled)?TRUE:FALSE);
    }
}

/*****************************************************************************/
/*                     kdws_animation_bar::set_state                         */
/*****************************************************************************/

void kdws_animation_bar::set_state(bool playing_forward, bool playing_reverse,
                                   bool repeat_enabled, double custom_fps,
                                   double native_rate_multiplier)
{
  if ((this->play_repeat == repeat_enabled) &&
      (this->play_fwd == playing_forward) &&
      (this->play_rev == playing_reverse) &&
      (this->custom_rate == custom_fps) &&
      (this->rate_multiplier == native_rate_multiplier))
    return; // Nothing to do

  this->play_repeat = repeat_enabled;
  this->play_fwd = playing_forward;
  this->play_rev = playing_reverse;
  this->custom_rate = custom_fps;
  this->rate_multiplier = native_rate_multiplier;

  play_repeatbutton.SetState((play_repeat)?TRUE:FALSE);
  update_play_controls();
  char text[20];
  if (custom_rate < 0.0)
    { 
      play_nativebutton.SetState(TRUE);
      if (rate_multiplier >= 100.0)
        sprintf(text,"x%1.0f",rate_multiplier);
      else if (rate_multiplier >= 10.0)
        sprintf(text,"x%#01.1f",rate_multiplier);
      else
        sprintf(text,"x%#01.2f",rate_multiplier);
    }
  else
    { 
      play_nativebutton.SetState(FALSE);
      if (custom_rate >= 100.0)
        sprintf(text,"%#1.0ffps",custom_rate);
      else if (custom_rate >= 10.0)
        sprintf(text,"%#01.1ffps",custom_rate);
      else
        sprintf(text,"%#01.2ffps",custom_rate);
    }
  play_ratefield.SetWindowText(kdws_string(text));
}

/*****************************************************************************/
/*                      kdws_animation_bar::set_frame                        */
/*****************************************************************************/

void kdws_animation_bar::set_frame(int frame_idx, int max_frame_idx,
                                   double frame_start_time,
                                   double frame_end_time,
                                   double track_end_time)
{
  if (frame_idx < 0)
    frame_idx = 0; // Just in case
  else if ((max_frame_idx >= 0) && (frame_idx > max_frame_idx))
    frame_idx = max_frame_idx; // Just in case
  if (frame_end_time < frame_start_time)
    frame_end_time = frame_start_time; // Just in case
  if ((track_end_time > 0.0) && (frame_end_time > track_end_time))
    frame_end_time = track_end_time; // Just in case
  if (frame_start_time > frame_end_time)
    frame_start_time = frame_end_time; // Just in case
      
  bool force_update = !show_indices;
  if (frame_start_time < 0.0)
    { 
      show_indices = true;
      cur_frame_start = (double) frame_idx;
      cur_frame_end = cur_frame_start + 1.0;
    }
  else
    { 
      show_indices = false;
      cur_frame_start = frame_start_time;
      cur_frame_end = frame_end_time;
      if (track_end_time < 0.0)
        max_frame_idx = -1;
    }
  bool was_first=first_frame, was_last=last_frame;
  this->first_frame = (frame_idx == 0);
  this->last_frame = (frame_idx == max_frame_idx);
  if ((was_first!=first_frame) || (was_last!=last_frame))
    force_update = true;
  if (force_update)
    slider_interval = get_natural_slider_interval(0,slider_step);
  if (max_frame_idx <= 0)
    { 
      if (track_interval > 0.0)
        force_update = true;
      track_interval = -1.0;
    }
  else
    { 
      if (track_interval != track_end_time)
        force_update = true;
      track_interval = track_end_time;
      if (show_indices)
        track_interval = (double)(max_frame_idx+1);
      double new_val, new_step;
      while ((track_interval <=
              (new_val=get_natural_slider_interval(-1,new_step))) &&
             (slider_interval != new_val))
        { slider_interval = new_val; slider_step = new_step; }
    }
  update_controls_for_track_pos(force_update);
}

/*****************************************************************************/
/*              kdws_animation_bar::get_natural_slider_interval              */
/*****************************************************************************/

double
  kdws_animation_bar::get_natural_slider_interval(int adjust, double &step)
{
  double natural_vals[4], natural_steps[4];
  if (show_indices)
    { 
      natural_vals[0] = 10.0;    natural_vals[1] = 100.0;
      natural_vals[2] = 1000.0;  natural_vals[3] = 10000.0;
      natural_steps[0] = 1.0;    natural_steps[1] = 10.0;
      natural_steps[2] = 100.0;  natural_steps[3] = 1000.0;
    }
  else
    { 
      natural_vals[0] = 2.0;     natural_vals[1] = 10.0;
      natural_vals[2] = 60.0;    natural_vals[3] = 600.0;
      natural_steps[0] = 1.0;    natural_steps[1] = 1.0;
      natural_steps[2] = 10.0;   natural_steps[3] = 60.0;      
    }
  int best_idx = 0;
  if (adjust == 0)
    { // Want closest
      for (int n=1; n < 4; n++)
        { 
          double best_delta = slider_interval - natural_vals[best_idx];
          double delta = slider_interval - natural_vals[n];
          delta = (delta < 0.0)?-delta:delta;
          best_delta = (best_delta < 0.0)?-best_delta:best_delta;
          if (delta < best_delta)
            best_idx = n;
        }
    }
  else if (adjust > 0)
    { // Want next larger interval
      for (int n=0; n < 4; n++)
        { 
          best_idx = n;
          if (slider_interval < natural_vals[n])
            break;
        }
    }
  else
    { // Want next smaller interval
      for (int n=3; n >= 0; n--)
        { 
          best_idx = n;
          if (slider_interval > natural_vals[n])
            break;
        }
    }
  step = natural_steps[best_idx];
  return natural_vals[best_idx];
}

/*****************************************************************************/
/*             kdws_animation_bar::update_controls_for_track_pos             */
/*****************************************************************************/

void kdws_animation_bar::update_controls_for_track_pos(bool force_update)
{
  // Save control state so we can avoid unnecessary updates
  double old_slider_start=slider_start;
  double old_slider_end=slider_end;

  // Adjust slider start position to be a multiple of 0.5*slider_interval so
  // that `pos' lies at least one `slider_step' from the slider ends, if
  // possible.
  double min_threshold=slider_start, max_threshold=slider_end;
  if (allow_auto_slide)
    { 
      double gap = 0.15*(slider_end-slider_start);
      if (gap > slider_step)
        gap = slider_step;
      min_threshold += gap;
      max_threshold -= gap;
    }
  if (slider_step > 0.0)
    { 
      double slider_inc=slider_step*floor(0.5+0.5*slider_interval/slider_step);
      if ((slider_end <= slider_start) ||
          (cur_frame_start < min_threshold) ||
          (cur_frame_start > max_threshold))
        { 
          slider_start=slider_inc*(floor(0.5+cur_frame_start/slider_inc)-1.0);
          if (slider_start < 0.0)
            slider_start = 0.0;
          slider_end = slider_start + slider_interval;
        }
      if (track_interval > 0.0)
        { 
          if (slider_end >= (track_interval+slider_inc))
            { slider_start -= slider_inc; slider_end -= slider_inc; }
          if (slider_start <= 0.0)
            { 
              slider_start = 0.0;
              while (slider_end >= (track_interval+slider_step))
                slider_end -= slider_step;
            }
        }
    }

  // Modify the controls
  double slider_range = slider_end - slider_start;
  if ((slider_start != old_slider_start) ||
      (slider_end != old_slider_end) || force_update)
    {
      char start_text[20], end_text[20];
      if (show_indices)
        {
          sprintf(start_text,"%d",(int)(1.5+slider_start));
          sprintf(end_text,"+%d",(int)(0.5+slider_range));
        }
      else
        {
          int hours, minutes, seconds;
          kd_time_to_hms(slider_start,hours,minutes,seconds);
          sprintf(start_text,"%02d:%02d:%02d",hours,minutes,seconds);
          kd_time_to_hms(slider_range,hours,minutes,seconds);
          minutes += 60*hours; // There should be no hours
          if (minutes == 0)
            sprintf(end_text,"+%-2dsec",seconds);
          else
            sprintf(end_text,"+%-2dmin",minutes);
        }
      draw_slider_ticks();
      left_timefield.SetWindowText(kdws_string(start_text));
      right_timefield.SetWindowText(kdws_string(end_text));
      
      left_timebutton.EnableWindow((allow_positioning &&
                                    (slider_start > 0.0))?TRUE:FALSE);
      right_timebutton.EnableWindow((allow_positioning &&
                                     ((slider_end < track_interval) ||
                                      (track_interval <= 0.0)))?TRUE:FALSE);
      go_startbutton.EnableWindow((allow_positioning &&
                                   !first_frame)?TRUE:FALSE);
      go_endbutton.EnableWindow((allow_positioning &&
                                 !last_frame)?TRUE:FALSE);
    }

  if ((slider_range == 0.0) || first_frame)
    central_slider.SetPos(0); // Should not happen
  else
    { 
      double rel_pos = (cur_frame_start-slider_start) / slider_range;
      central_slider.SetPos((int)(0.5+rel_pos*KD_CENTRAL_SLIDER_MAX));
    }

  update_play_controls();
}

/*****************************************************************************/
/*                 kdws_animation_bar::draw_slider_ticks                     */
/*****************************************************************************/

void kdws_animation_bar::draw_slider_ticks()
{
  central_slider.ClearTics();
  if (slider_step > 0.0)
    { 
      int num_ticks = (int) floor(0.5+(slider_end-slider_start)/slider_step);
      double rel_gap = 1.0 / (double) num_ticks;
      for (int n=1; n < num_ticks; n++)
        { 
          double tick_pos = n * rel_gap;
          central_slider.SetTic((int)(0.5+tick_pos*KD_CENTRAL_SLIDER_MAX));
        }
      central_slider.SetPageSize((int)(0.5 + rel_gap*KD_CENTRAL_SLIDER_MAX));
    }
  central_slider.Invalidate();
}

/*****************************************************************************/
/*                   kdws_animation_bar::PreCreateWindow                     */
/*****************************************************************************/

BOOL kdws_animation_bar::PreCreateWindow(CREATESTRUCT& cs) 
{
  if (!CWnd::PreCreateWindow(cs))
    return FALSE;
  return TRUE;
}

/*****************************************************************************/
/*                       kdws_animation_bar::OnCreate                        */
/*****************************************************************************/

int kdws_animation_bar::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
  CDC *dc = this->GetDC();
  CFont *existing_font = dc->SelectObject(&fixed_font);
  left_timefield_size = dc->GetTextExtent(_T("0:00:00"),7);
  right_timefield_size = dc->GetTextExtent(_T("+10min"),6);
  play_ratefield_size = dc->GetTextExtent(_T("1000fps"),7);
  dc->SelectObject(&var_font);
  repeat_button_size = dc->GetTextExtent(_T("repeat"),6);
  native_button_size = dc->GetTextExtent(_T("native"),6);
  dc->SelectObject(existing_font);
  this->ReleaseDC(dc);
  left_timefield_size.cx += 2;
  right_timefield_size.cx += 2;
  play_ratefield_size.cx += 2;
  repeat_button_size.cx += 2;
  native_button_size.cx += 2;

  // Top row controls
  if (!left_timefield.Create(_T("0:00:00"),WS_CHILD|WS_VISIBLE|SS_RIGHT,
                             CRect(0,0,0,0),this,KD_LEFT_TIMEFIELD_ID))
    return FALSE;
  left_timefield.SetFont(&fixed_font,FALSE);

  if (!left_timebutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                              CRect(0,0,0,0),this,KD_LEFT_TIMEBUTTON_ID))
    return FALSE;
  left_timebutton.LoadBitmaps(IDB_LEFT_TIMEBUTTON,
                              IDB_LEFT_TIMEBUTTON_DISABLED,
                              NULL,
                              IDB_LEFT_TIMEBUTTON_DISABLED);

  if (!central_slider.Create(WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_TOP,
                             CRect(0,0,0,0),this,KD_CENTRAL_SLIDER_ID))
    return FALSE;
  central_slider.SetRange(0,KD_CENTRAL_SLIDER_MAX);

  if (!right_timebutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                               CRect(0,0,0,0),this,KD_RIGHT_TIMEBUTTON_ID))
    return FALSE;
  right_timebutton.LoadBitmaps(IDB_RIGHT_TIMEBUTTON,
                               IDB_RIGHT_TIMEBUTTON_DISABLED,
                               NULL,
                               IDB_RIGHT_TIMEBUTTON_DISABLED);

  if (!right_timefield.Create(_T("+10min"),WS_CHILD|WS_VISIBLE|SS_LEFT,
                              CRect(0,0,0,0),this,KD_LEFT_TIMEFIELD_ID))
    return FALSE;
  right_timefield.SetFont(&fixed_font,FALSE);
  
  if (!right_timespin.Create(WS_CHILD|WS_VISIBLE|UDS_HORZ|UDS_ARROWKEYS,
                             CRect(0,0,0,0),this,KD_RIGHT_TIMESPIN_ID))
    return FALSE;
  right_timespin.SetRange32(-1,1);
  right_timespin.SetPos(0);
  
  // Base row controls
  if (!fwd_playbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_FWD_PLAYBUTTON_ID))
    return FALSE;
  fwd_playbutton.LoadBitmaps(IDB_FWD_PLAYBUTTON,
                             IDB_FWD_PLAYBUTTON_DISABLED,
                             NULL,
                             IDB_FWD_PLAYBUTTON_DISABLED);

  if (!go_startbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_GO_STARTBUTTON_ID))
    return FALSE;
  go_startbutton.LoadBitmaps(IDB_GO_STARTBUTTON,
                             IDB_GO_STARTBUTTON_DISABLED,
                             NULL,
                             IDB_GO_STARTBUTTON_DISABLED);

  if (!stop_playbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                              CRect(0,0,0,0),this,KD_STOP_PLAYBUTTON_ID))
    return FALSE;
  stop_playbutton.LoadBitmaps(IDB_STOP_PLAYBUTTON,
                             IDB_STOP_PLAYBUTTON_DISABLED,
                             NULL,
                             IDB_STOP_PLAYBUTTON_DISABLED);

  if (!go_endbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_GO_ENDBUTTON_ID))
    return FALSE;
  go_endbutton.LoadBitmaps(IDB_GO_ENDBUTTON,
                           IDB_GO_ENDBUTTON_DISABLED,
                           NULL,
                           IDB_GO_ENDBUTTON_DISABLED);

  if (!rev_playbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_REV_PLAYBUTTON_ID))
    return FALSE;
  rev_playbutton.LoadBitmaps(IDB_REV_PLAYBUTTON,
                             IDB_REV_PLAYBUTTON_DISABLED,
                             NULL,
                             IDB_REV_PLAYBUTTON_DISABLED);

  if (!step_hilitebox.Create(NULL,WS_CHILD|WS_VISIBLE|SS_BLACKFRAME,
                             CRect(0,0,0,0),this,KD_STEP_HILITEBOX_ID))
    return FALSE;

  if (!fwd_stepbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_FWD_STEPBUTTON_ID))
    return FALSE;
  fwd_stepbutton.LoadBitmaps(IDB_FWD_STEPBUTTON,
                             IDB_FWD_STEPBUTTON_DISABLED,
                             NULL,
                             IDB_FWD_STEPBUTTON_DISABLED);

  if (!rev_stepbutton.Create(_T(""),WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                             CRect(0,0,0,0),this,KD_REV_STEPBUTTON_ID))
    return FALSE;
  rev_stepbutton.LoadBitmaps(IDB_REV_STEPBUTTON,
                             IDB_REV_STEPBUTTON_DISABLED,
                             NULL,
                             IDB_REV_STEPBUTTON_DISABLED);

  if (!play_ratefield.Create(_T("x1.00"),WS_CHILD|WS_VISIBLE|SS_RIGHT,
                             CRect(0,0,0,0),this,KD_PLAY_RATEFIELD_ID))
    return FALSE;
  play_ratefield.SetFont(&fixed_font,FALSE);
  
  if (!play_ratespin.Create(WS_CHILD|WS_VISIBLE|UDS_HORZ|UDS_ARROWKEYS,
                            CRect(0,0,0,0),this,KD_PLAY_RATESPIN_ID))
    return FALSE;

  if (!play_nativebutton.Create(_T("native"),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                CRect(0,0,0,0),this,
                                KD_PLAY_NATIVEBUTTON_ID))
    return FALSE;
  play_nativebutton.SetFont(&var_font);

  if (!play_repeatbutton.Create(_T("repeat"),WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                                CRect(0,0,0,0),this,
                                KD_PLAY_REPEATBUTTON_ID))
    return FALSE;
  play_repeatbutton.SetFont(&var_font);

  button_size.cx = button_size.cy = 15;
  repeat_button_size.cy = native_button_size.cy = line_height-(line_height/5);
  repeat_button_size.cx += (line_height/4);
  native_button_size.cx += (line_height/4);
  spin_size.cy = line_height-(line_height/5);
  spin_size.cx = spin_size.cy*2;

  reset(); // Make sure our internal notion of the button states agrees
           // with their actual settings.

  return TRUE;
}

/*****************************************************************************/
/*                   kdws_animation_bar::regenerate_layout                   */
/*****************************************************************************/

void kdws_animation_bar::regenerate_layout() 
{
  // Start by finding the layout rectangles for all constituent window
  RECT bar_rect;
  GetClientRect(&bar_rect);
  assert((bar_rect.top == 0) && (bar_rect.left == 0));
  if (bar_rect.right == bar_width)
    return; // Nothing to do
  bar_width = bar_rect.right; // Remember this for next time

  { // Arrange top row controls
    int rubber_gap = 5; // Size of adjustable gaps
    CWnd *controls[6] = { &left_timefield, &left_timebutton,
                          &central_slider,
                          &right_timebutton, &right_timefield,
                          &right_timespin };
    int control_widths[6] = { left_timefield_size.cx, button_size.cx,
                              0, // Initial width for the slider
                              button_size.cx, right_timefield_size.cx,
                              spin_size.cx };
    int control_heights[6] = { left_timefield_size.cy, button_size.cy,
                               button_size.cy+(line_height/4), // Slider height
                               button_size.cy, right_timefield_size.cy,
                               spin_size.cy };
    int pre_control_gaps[6] = { rubber_gap, rubber_gap, rubber_gap,
                                rubber_gap, rubber_gap, 0 };
    int c, num_rubber_gaps=6, accounted_width=rubber_gap; // Gap on right
    for (c=0; c < 6; c++)
      accounted_width += pre_control_gaps[c] + control_widths[c];

    int slider_width = bar_width - accounted_width;
    if (slider_width > (bar_width / 4))
      { // Include slider
        accounted_width += slider_width;
        control_widths[2] = slider_width;
      }
    else
      { // Exclude slider
        num_rubber_gaps--;
        accounted_width -= rubber_gap;
        slider_width = 0;
      }
    int y = bar_rect.bottom - line_height - (line_height / 2); // Mid-line
    int x = 0;
    for (c=0; c < 6; c++)
      { 
        if (control_widths[c] == 0)
          { 
            controls[c]->SetWindowPos(NULL,0,0,0,0,
                                      SWP_NOZORDER|SWP_NOMOVE|SWP_HIDEWINDOW);
            continue;
          }
        x += pre_control_gaps[c];
        if (pre_control_gaps[c] == rubber_gap)
          { 
            if (accounted_width < bar_width)
              { // Distribute unaccounted width to the gaps
                int extra = (bar_width-accounted_width) / num_rubber_gaps;
                x += extra;
                accounted_width += extra;
              }
            num_rubber_gaps--;
          }
        RECT rect;
        rect.left = x;  rect.right = (x += control_widths[c]);
        rect.bottom = y + (control_heights[c]/2);
        rect.top = rect.bottom - control_heights[c];
        controls[c]->SetWindowPos(NULL,rect.left,rect.top,
                                  rect.right-rect.left,rect.bottom-rect.top,
                                  SWP_NOZORDER|SWP_SHOWWINDOW);
      }
  }

  { // Arrange base row controls
    int rubber_gap = 8; // Size of adjustable gaps
    CWnd *controls[11] = { &go_startbutton, &fwd_playbutton, &stop_playbutton,
                           &rev_playbutton, &go_endbutton,
                           &rev_stepbutton, &fwd_stepbutton,
                           &play_ratefield, &play_ratespin,
                           &play_nativebutton, &play_repeatbutton };
    int control_widths[11] = { button_size.cx, button_size.cx, button_size.cx,
                               button_size.cx, button_size.cx,
                               button_size.cx, button_size.cx,
                               play_ratefield_size.cx, spin_size.cx,
                               native_button_size.cx, repeat_button_size.cx };
    int control_heights[11] = { button_size.cy, button_size.cy, button_size.cy,
                                button_size.cy, button_size.cy,
                                button_size.cy, button_size.cy,
                                play_ratefield_size.cy, spin_size.cy,
                                native_button_size.cy, repeat_button_size.cy };
    int pre_gap = line_height/4;

    int pre_control_gaps[12] = { pre_gap, pre_gap, pre_gap, pre_gap, pre_gap,
                                 rubber_gap, pre_gap, rubber_gap,
                                 0, pre_gap, pre_gap, pre_gap };
    int c, accounted_width=pre_control_gaps[11]; // Right side gap
    for (c=0; c < 11; c++)
      accounted_width += pre_control_gaps[c] + control_widths[c];
    int y = bar_rect.bottom - (line_height / 2); // Mid-line
    int x = 0;
    int num_rubber_gaps = 2;
    RECT hilite_rect;
    for (c=0; c < 11; c++)
      { 
        x += pre_control_gaps[c];
        if ((pre_control_gaps[c] == rubber_gap) &&
            (accounted_width < bar_width))
          { // Distribute unaccounted width to the rubber gaps
            int extra = (bar_width - accounted_width) / num_rubber_gaps;
            x += extra;
            accounted_width += extra;
            num_rubber_gaps--;
          }
        RECT rect;
        rect.left = x;  rect.right = (x += control_widths[c]);
        rect.bottom = y + (control_heights[c]/2);
        rect.top = rect.bottom - control_heights[c];
        controls[c]->SetWindowPos(NULL,rect.left,rect.top,
                                  rect.right-rect.left,rect.bottom-rect.top,
                                  SWP_NOZORDER|SWP_SHOWWINDOW);
        if (controls[c] == &rev_stepbutton)
          hilite_rect = rect;
        else if (controls[c] == &fwd_stepbutton)
          hilite_rect.right = rect.right;
      }
    hilite_rect.top -= 2;    hilite_rect.left -= 2;
    hilite_rect.bottom += 2; hilite_rect.right += 2;
    step_hilitebox.SetWindowPos(NULL,hilite_rect.left,hilite_rect.top,
                                hilite_rect.right-hilite_rect.left,
                                hilite_rect.bottom-hilite_rect.top,
                                SWP_NOZORDER);
  }
}

/*****************************************************************************/
/*                      kdws_animation_bar::OnPaint                          */
/*****************************************************************************/

void kdws_animation_bar::OnPaint() 
{
  PAINTSTRUCT paint_struct;
  CDC *dc = BeginPaint(&paint_struct);
  RECT *rect = &paint_struct.rcPaint;
  dc->FillRect(rect,&background_brush);
  EndPaint(&paint_struct);
}

/*****************************************************************************/
/*                      kdws_animation_bar::OnHScroll                        */
/*****************************************************************************/

void kdws_animation_bar::OnHScroll(UINT nSBCode, UINT nPos,
                                   CScrollBar* pScrollBar)
{
  if ((pScrollBar != (CScrollBar *) &central_slider) ||
      (target==NULL) || !allow_positioning)
    return;
  double slider_range = slider_end - slider_start;
  if (slider_range <= 0.0)
    return;
  double rel_pos = ((double) central_slider.GetPos()) / KD_CENTRAL_SLIDER_MAX;
  if (rel_pos > 1.0)
    rel_pos = 1.0;
  double tgt_frame_start = (rel_pos * slider_range) + slider_start;
  bool old_auto_slide = this->allow_auto_slide;
  this->allow_auto_slide = false;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else if (tgt_frame_start <= 0.0)
    target->set_animation_point_by_idx(0);
  else if ((track_interval > 0.0) && (tgt_frame_start >= track_interval))
    target->set_animation_point_by_idx(-1);
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
  this->allow_auto_slide = old_auto_slide;
}

/*****************************************************************************/
/*                kdws_animation_bar::deltapos_right_timespin                */
/*****************************************************************************/

void
  kdws_animation_bar::deltapos_right_timespin(NMHDR* pNMHDR, LRESULT* pResult)
{
  NM_UPDOWN* pNMUpDown = (NM_UPDOWN*)pNMHDR;
  if (pNMUpDown->iDelta < 0)
    { 
      double new_val, new_step;
      new_val = get_natural_slider_interval(-1,new_step);
      if (new_val != slider_interval)
        { 
          slider_interval = new_val;
          slider_step = new_step;
          slider_start = slider_end = 0.0;
          update_controls_for_track_pos(true);
        }
    }
  else if ((pNMUpDown->iDelta > 0) &&
           ((track_interval <= 0.0) || (slider_interval < track_interval)))
    { 
      double new_val, new_step;
      new_val = get_natural_slider_interval(1,new_step);
      if (new_val != slider_interval)
        { 
          slider_interval = new_val;
          slider_step = new_step;
          slider_start = slider_end = 0.0;
          update_controls_for_track_pos(true);
        }
    }
}

/*****************************************************************************/
/*                kdws_animation_bar::deltapos_play_ratespin                 */
/*****************************************************************************/

void
  kdws_animation_bar::deltapos_play_ratespin(NMHDR* pNMHDR, LRESULT* pResult)
{
  if (target == NULL)
    return;
  NM_UPDOWN* pNMUpDown = (NM_UPDOWN*)pNMHDR;
  if (pNMUpDown->iDelta > 0)
    target->menu_PlayFrameRateDown();
  else if (pNMUpDown->iDelta < 0)
    target->menu_PlayFrameRateUp(); 
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_fwd_playbutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_fwd_playbutton()
{
  image_view->SetFocus();
  if (target != NULL)
    target->menu_PlayStartForward();
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_rev_playbutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_rev_playbutton()
{
  image_view->SetFocus();
  if (target != NULL)
    target->menu_PlayStartBackward();
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_stop_playbutton                */
/*****************************************************************************/

void kdws_animation_bar::clicked_stop_playbutton()
{
  image_view->SetFocus();
  if (target != NULL)
    target->menu_PlayStop();
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_fwd_stepbutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_fwd_stepbutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  target->menu_PlayStop();
  target->menu_NavImageNext();
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_rev_stepbutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_rev_stepbutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  target->menu_PlayStop();
  target->menu_NavImagePrev();
}

/*****************************************************************************/
/*                kdws_animation_bar::clicked_go_startbutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_go_startbutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  target->set_animation_point_by_idx(0);
}

/*****************************************************************************/
/*                 kdws_animation_bar::clicked_go_endbutton                  */
/*****************************************************************************/

void kdws_animation_bar::clicked_go_endbutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  target->set_animation_point_by_idx(-1);
}

/*****************************************************************************/
/*               kdws_animation_bar::clicked_left_timebutton                 */
/*****************************************************************************/

void kdws_animation_bar::clicked_left_timebutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  double tgt_frame_start = cur_frame_start - slider_interval;
  if (tgt_frame_start < 0.0)
    tgt_frame_start = 0.0;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
}

/*****************************************************************************/
/*               kdws_animation_bar::clicked_right_timebutton                */
/*****************************************************************************/

void kdws_animation_bar::clicked_right_timebutton()
{
  image_view->SetFocus();
  if ((target == NULL) || !allow_positioning)
    return;
  double tgt_frame_start = cur_frame_start + slider_interval;
  if (show_indices)
    target->set_animation_point_by_idx((int)(0.5+tgt_frame_start));
  else
    target->set_animation_point_by_time(tgt_frame_start,true);
}

/*****************************************************************************/
/*              kdws_animation_bar::clicked_play_repeatbutton                */
/*****************************************************************************/

void kdws_animation_bar::clicked_play_repeatbutton()
{
  image_view->SetFocus();
  if (target != NULL)
    target->menu_PlayRepeat();
}

/*****************************************************************************/
/*              kdws_animation_bar::clicked_play_nativebutton                */
/*****************************************************************************/

void kdws_animation_bar::clicked_play_nativebutton()
{
  image_view->SetFocus();
  if (target == NULL)
    return;
  if (custom_rate < 0.0)
    target->menu_PlayCustom();
  else
    target->menu_PlayNative();
}

/*****************************************************************************/
/*                 Message Handlers for kdws_animation_bar                   */
/*****************************************************************************/

BEGIN_MESSAGE_MAP(kdws_animation_bar,CWnd)
  ON_WM_CREATE()
	ON_WM_PAINT()
  ON_WM_SIZE()
  ON_WM_HSCROLL()
  ON_NOTIFY(UDN_DELTAPOS, KD_RIGHT_TIMESPIN_ID, deltapos_right_timespin)
  ON_NOTIFY(UDN_DELTAPOS, KD_PLAY_RATESPIN_ID, deltapos_play_ratespin)
  ON_BN_CLICKED(KD_FWD_PLAYBUTTON_ID, clicked_fwd_playbutton)
  ON_BN_CLICKED(KD_REV_PLAYBUTTON_ID, clicked_rev_playbutton)
  ON_BN_CLICKED(KD_STOP_PLAYBUTTON_ID, clicked_stop_playbutton)
  ON_BN_CLICKED(KD_GO_STARTBUTTON_ID, clicked_go_startbutton)
  ON_BN_CLICKED(KD_GO_ENDBUTTON_ID, clicked_go_endbutton)
  ON_BN_CLICKED(KD_LEFT_TIMEBUTTON_ID, clicked_left_timebutton)
  ON_BN_CLICKED(KD_RIGHT_TIMEBUTTON_ID, clicked_right_timebutton)
  ON_BN_CLICKED(KD_PLAY_NATIVEBUTTON_ID, clicked_play_nativebutton)
  ON_BN_CLICKED(KD_PLAY_REPEATBUTTON_ID, clicked_play_repeatbutton)
  ON_BN_CLICKED(KD_FWD_STEPBUTTON_ID, clicked_fwd_stepbutton)
  ON_BN_CLICKED(KD_REV_STEPBUTTON_ID, clicked_rev_stepbutton)
END_MESSAGE_MAP()


/* ========================================================================= */
/*                           kdws_frame_window                               */
/* ========================================================================= */

/*****************************************************************************/
/*                  kdws_frame_window::kdws_frame_window                     */
/*****************************************************************************/

kdws_frame_window::kdws_frame_window()
{
  manager = NULL;
  renderer = NULL;
  menu_tip_manager = NULL;
  added_to_manager = false;
  image_view.set_frame_window(this);
  progress_height = 0;
  status_height = 0;
  status_text_height = 0;
  animation_height = 0;
  catalog_panel_width = 0;
  for (int p=0; p < 3; p++)
    {
      status_pane_lengths[p] = 0;
      status_pane_caches[p] = NULL;
    }
  status_pane_colour = RGB(255,255,230);
  status_text_colour = RGB(100,60,0);
  status_brush.CreateSolidBrush(status_pane_colour);
  kdws_create_preferred_window_font(&status_font,1.0);
}

/*****************************************************************************/
/*                         kdws_frame_window::init                           */
/*****************************************************************************/

bool kdws_frame_window::init(kdws_manager *manager)
{
  this->manager = manager;
  try {
    if (!this->LoadFrame(IDR_MAINFRAME,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                         WS_THICKFRAME | WS_MINIMIZEBOX,
                         NULL,NULL))
      { kdu_error e; e <<
          "Failed to create frame window: perhaps the "
          "menu resource could not be found or some resource limit has "
          "been exceeded???";
      }
  }
  catch (int) {
    return false;
  };
  manager->add_window(this);
  added_to_manager = true;
  renderer = new kdws_renderer(this,&image_view,manager);
  image_view.set_manager_and_renderer(manager,renderer);
  renderer->close_file(); // Causes the window title to be updated
  return true;
}

/*****************************************************************************/
/*                  kdws_frame_window::~kdws_frame_window                    */
/*****************************************************************************/

kdws_frame_window::~kdws_frame_window()
{
  if ((manager != NULL) && added_to_manager)
    {
      manager->remove_window(this);
      manager = NULL;
    }
  image_view.set_manager_and_renderer(NULL,NULL);
  if (renderer != NULL)
    { delete renderer; renderer = NULL; }
  status_brush.DeleteObject();
  status_font.DeleteObject();
  if (menu_tip_manager != NULL)
    delete menu_tip_manager;
}

/*****************************************************************************/
/*                     kdws_frame_window::PostNcDestroy                      */
/*****************************************************************************/

void kdws_frame_window::PostNcDestroy() 
{
  if ((manager != NULL) && added_to_manager)
    {
      manager->remove_window(this);
      manager = NULL;
    }
  catalog_view.deactivate();
  CFrameWnd::PostNcDestroy(); // this should destroy the window
}

/*****************************************************************************/
/*                        kdws_frame_window::OnCmdMsg                        */
/*****************************************************************************/

BOOL kdws_frame_window::OnCmdMsg(UINT nID, int nCode, void *pExtra,
                                 AFX_CMDHANDLERINFO *pHandlerInfo)
{
  if ((renderer != NULL) &&
      ((nCode == CN_COMMAND)  || (nCode == CN_UPDATE_COMMAND_UI)) &&
      renderer->OnCmdMsg(nID,nCode,pExtra,pHandlerInfo))
    {
      kdws_frame_window *next_wnd=NULL;
      if (nCode == CN_UPDATE_COMMAND_UI)
        cancel_pending_show();
      else if ((next_wnd = manager->get_next_action_window(this)) != NULL)
        next_wnd->OnCmdMsg(nID,nCode,pExtra,pHandlerInfo);
      return TRUE;
    }
  return CFrameWnd::OnCmdMsg(nID,nCode,pExtra,pHandlerInfo);
}

/*****************************************************************************/
/*                kdws_frame_window::application_can_terminate               */
/*****************************************************************************/

bool kdws_frame_window::application_can_terminate()
{
  if (renderer == NULL)
    return true;
  if ((renderer->editor != NULL) &&
      (interrogate_user("You have an open metadata editor for one or more "
                        "windows that will be closed when this application "
                        "terminates. Are you sure you want to quit the "
                        "application?",
                        "Cancel","Quit Application") != 1))
    return false;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have unsaved edits in one or more windows that "
                        "will be closed when this application terminates.  "
                        "Are you sure you want to quit the application?",
                        "Cancel","Quit Application") != 1))
    return false;
  return true;
}

/*****************************************************************************/
/*                kdws_frame_window::application_terminating                 */
/*****************************************************************************/

void kdws_frame_window::application_terminating()
{
  catalog_view.deactivate();
  if (renderer != NULL)
    renderer->perform_essential_cleanup_steps(true);
}

/*****************************************************************************/
/*                  kdws_frame_window::cancel_pending_show                   */
/*****************************************************************************/

void kdws_frame_window::cancel_pending_show()
{
  if (renderer != NULL)
    renderer->cancel_pending_show();
}

/*****************************************************************************/
/*                    kdws_frame_window::PreCreateWindow                     */
/*****************************************************************************/

BOOL kdws_frame_window::PreCreateWindow(CREATESTRUCT &cs)
{
  if( !CFrameWnd::PreCreateWindow(cs) )
    return FALSE;
  cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
  cs.lpszClass = AfxRegisterWndClass(0);
  return TRUE;
}


/*****************************************************************************/
/*                    kdws_frame_window::OnCreateClient                      */
/*****************************************************************************/

BOOL kdws_frame_window::OnCreateClient(CREATESTRUCT *cs, CCreateContext *ctxt)
{
  CDC *dc = this->GetDC();
  CFont *existing_font = dc->SelectObject(&status_font);
  TEXTMETRIC font_metrics;
  dc->GetTextMetrics(&font_metrics);
  status_text_height = font_metrics.tmHeight + 4;
  dc->SelectObject(existing_font);
  this->ReleaseDC(dc);

  UINT wnd_id = AFX_IDW_PANE_FIRST;
  if (!image_view.Create(NULL, NULL,
                         AFX_WS_DEFAULT_VIEW | WS_HSCROLL | WS_VSCROLL,
                         CRect(0,0,0,0),this,wnd_id++,NULL))
    return FALSE;
  if(!progress_indicator.Create(AFX_WS_DEFAULT_VIEW | PBS_SMOOTH,
                          CRect(0,0,0,0),this,wnd_id++))
    return FALSE;
  for (int p=0; p < 3; p++)
    {
      DWORD style = SS_SUNKEN | SS_LEFT | SS_ENDELLIPSIS;
      if (!status_panes[p].Create(NULL,style,CRect(0,0,0,0),this,wnd_id++))
        return FALSE;
      status_panes[p].SetFont(&status_font,FALSE);
      status_panes[p].SetWindowText(_T(""));
    }

  animation_line_height = status_text_height+2;
  animation_bar.init(animation_line_height,&image_view);
  if (!animation_bar.Create(NULL,NULL,
                            AFX_WS_DEFAULT_VIEW,
                            CRect(0,0,0,0),this,wnd_id++))
    return FALSE;
  catalog_view.set_tools_font_and_height(&status_font,status_text_height-4);
  if (!catalog_view.Create(NULL,SS_SUNKEN | SS_CENTER | SS_ENDELLIPSIS |
                           SS_OWNERDRAW, // We draw a focus-dependent border
                           CRect(0,0,0,0),this,wnd_id++))
    return FALSE;

  progress_height = 0;
  status_height = status_text_height;
  animation_height = 0;
  catalog_panel_width = 0;

  assert(manager != NULL);

  kdu_coords initial_size = image_view.get_screen_size();
  initial_size.x = (initial_size.x * 3) / 10;
  initial_size.y = (initial_size.y * 3) / 10;
  this->SetWindowPos(NULL,0,0,initial_size.x,initial_size.y,
                     SWP_SHOWWINDOW | SWP_NOZORDER);
  ShowWindow(SW_SHOW);
  this->DragAcceptFiles(TRUE);
  return TRUE;
}

/*****************************************************************************/
/*                    kdws_frame_window::interrogate_user                    */
/*****************************************************************************/

int kdws_frame_window::interrogate_user(const char *message,
                                        const char *option_0,
                                        const char *option_1,
                                        const char *option_2,
                                        const char *option_3)
{
  const char *caption = "\"Kdu_show\" needs your input";
  if (option_1 == NULL)
    caption = "\"Kdu_show\" information";
  kdws_interrogate_dlg dialog(this,caption,message,
                              option_0,option_1,option_2,option_3);
  int result = (int) dialog.DoModal();
  return result;
}

/*****************************************************************************/
/*                       kdws_frame_window::open_file                        */
/*****************************************************************************/

void kdws_frame_window::open_file(const char *filename)
{
  if (renderer == NULL)
    return;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have edited the existing file but not saved "
                        "these edits ... perhaps you saved the file in a "
                        "format which cannot hold all the metadata ("
                        "use JPX).  Do you still want to close the existing "
                        "file to open a new one?",
                        "Cancel","Close without Saving") == 0))
      return;

  char full_pathname[_MAX_PATH];
  if (_fullpath(full_pathname,filename,_MAX_PATH ) == NULL)
    { kdu_error e;
      e << "\"" << filename << "\" does not appear to be a valid file name\n";
    }
  if (manager->check_open_file_replaced(full_pathname) &&
      (interrogate_user("A file you have elected to open is out of date, "
                        "meaning that another open window in the application "
                        "has saved over the file but not yet released its "
                        "access, so that the saved copy can replace the "
                        "original.  Do you still wish to open the old copy?",
                        "Cancel","Open Anyway")))
    return;
  renderer->open_file(full_pathname);
}

/*****************************************************************************/
/*                       kdws_frame_window::open_url                         */
/*****************************************************************************/

void kdws_frame_window::open_url(const char *url)
{
  if (renderer == NULL)
    return;
  if (renderer->have_unsaved_edits &&
      (interrogate_user("You have edited the existing file but not saved "
                        "these edits ... perhaps you saved the file in a "
                        "format which cannot hold all the metadata ("
                        "use JPX).  Do you still want to close the existing "
                        "file to open a new one?",
                        "Cancel","Close without Saving") == 0))
      return;
  renderer->open_file(url);
}

/*****************************************************************************/
/*              kdws_frame_window::note_jpip_client_reconnecting             */
/*****************************************************************************/

void kdws_frame_window::note_jpip_client_reconnecting(kdu_client *client)
{
 if (renderer != NULL)
    renderer->note_jpip_client_reconnecting(client);
}

/*****************************************************************************/
/*                      kdws_frame_window::do_processing                     */
/*****************************************************************************/

bool kdws_frame_window::do_processing()
{
  if (renderer == NULL)
    return false;
  else
    return renderer->do_processing();
}

/*****************************************************************************/
/*                 kdws_frame_window::display_notification                   */
/*****************************************************************************/

bool kdws_frame_window::display_notification()
{
  if (renderer == NULL)
    return false;
  renderer->update_animation_status_info();
  renderer->manage_animation_frame_queue();
  return renderer->need_processing();
}

/*****************************************************************************/
/*                  kdws_frame_window::client_notification                   */
/*****************************************************************************/

bool kdws_frame_window::client_notification()
{
  if (renderer == NULL)
    return false;
  renderer->client_notification();
  return renderer->need_processing();
}

/*****************************************************************************/
/*                         kdws_frame_window::wakeup                         */
/*****************************************************************************/

void kdws_frame_window::wakeup(double scheduled_time, double current_time)
{
  if (renderer != NULL)
    renderer->wakeup(scheduled_time,current_time);
}

/*****************************************************************************/
/*                   kdws_frame_window::adjust_playclock                     */
/*****************************************************************************/

void kdws_frame_window::adjust_playclock(double delta)
{
  if (renderer != NULL)
    renderer->adjust_playclock(delta);
}

/*****************************************************************************/
/*                  kdws_frame_window::set_status_strings                    */
/*****************************************************************************/

void kdws_frame_window::set_status_strings(const char **three_strings)
{
  int p; // Panel index
  
  // Start by finding the status panes which have changed, and allocating
  // space to cache the new status strings.
  int total_chars = 0; // Including NULL chars.
  bool pane_updated[3] = {false,false,false};
  for (p=0; p < 3; p++)
    {
      const char *string = three_strings[p];
      if (string == NULL)
        string = "";
      int len = (int) strlen(string)+1;
      pane_updated[p] = ((len != status_pane_lengths[p]) ||
                         (strcmp(string,status_pane_caches[p]) != 0));
      if ((total_chars + len) <= 256)
        { // Can cache this string
          status_pane_lengths[p] = len;
          status_pane_caches[p] = status_pane_cache_buf + total_chars;
          total_chars += len;
        }
      else
        { // Cannot cache this string
          status_pane_lengths[p] = 0;
          status_pane_caches[p] = NULL;
        }          
    }
  
  // Now update any status panes whose text has changed and also cache the
  // new text if we can, for next time.  Caching allows us to avoid updating
  // too many status panes, which can speed things up in video applications,
  // for example.
  for (p=0; p < 3; p++)
    {
      if (!pane_updated[p])
        continue;
      const char *string = three_strings[p];
      if (string == NULL)
        string = "";
      if (status_pane_caches[p] != NULL)
        strcpy(status_pane_caches[p],string);
      CString cstring(string);
      status_panes[p].SetWindowText(cstring);
    }
}

/*****************************************************************************/
/*                   kdws_frame_window::set_progress_bar                     */
/*****************************************************************************/

void kdws_frame_window::set_progress_bar(double value)
{
  if (progress_height == 0)
    {
      progress_height = status_text_height >> 1;
      status_height = status_text_height + progress_height;
      regenerate_layout();
    }
  if (value < 0.0)
    value = 0.0;
  else if (value > 100.0)
    value = 100.0;
  progress_indicator.SetPos((int)(value+0.5));
}

/*****************************************************************************/
/*                 kdws_frame_window::remove_progress_bar                    */
/*****************************************************************************/

void kdws_frame_window::remove_progress_bar()
{
  progress_height = 0;
  status_height = status_text_height;
  regenerate_layout();
}

/*****************************************************************************/
/*                  kdws_frame_window::get_animation_bar                     */
/*****************************************************************************/

kdws_animation_bar *
  kdws_frame_window::get_animation_bar(bool allow_positioning)
{
  if (animation_height == 0)
    { 
      animation_height = 2*animation_line_height;
      regenerate_layout();
    }
  animation_bar.set_target(renderer,allow_positioning);
  return &animation_bar;
}

/*****************************************************************************/
/*                kdws_frame_window::remove_animation_bar                    */
/*****************************************************************************/

void kdws_frame_window::remove_animation_bar()
{
  animation_height = 0;
  animation_bar.set_target(NULL,false);
  regenerate_layout();
}

/*****************************************************************************/
/*               kdws_frame_window::create_metadata_catalog                  */
/*****************************************************************************/

void kdws_frame_window::create_metadata_catalog()
{
  if (renderer == NULL)
    return;
  if (catalog_view.is_active())
    { // Catalog already exists
      assert(catalog_panel_width > 0);
      renderer->set_metadata_catalog_source(&catalog_view);
      return;
    }
  catalog_panel_width = KD_MAX_CATALOG_PANEL_WIDTH;
  RECT rect;  GetWindowRect(&rect);
  rect.right += catalog_panel_width;
  int screen_width = image_view.get_screen_size().y;
  if (rect.right > screen_width)
    { // At least some of the catalog will be invisible
      int width = rect.right-rect.left;
      if (width >= screen_width)
        rect.right = rect.left + screen_width;
      int delta = (rect.right-screen_width) >> 1;
      rect.left -= delta;
      rect.right -= delta;
    }
  SetWindowPos(NULL,0,0,rect.right-rect.left,rect.bottom-rect.top,
               SWP_NOZORDER | SWP_NOMOVE | SWP_SHOWWINDOW);
  catalog_view.Invalidate(TRUE);
  catalog_view.activate(renderer);
  renderer->set_metadata_catalog_source(&catalog_view);
}

/*****************************************************************************/
/*               kdws_frame_window::remove_metadata_catalog                  */
/*****************************************************************************/

void kdws_frame_window::remove_metadata_catalog()
{
  catalog_view.deactivate();
  renderer->set_metadata_catalog_source(NULL);
  catalog_panel_width = 0;
  regenerate_layout(); // May force the window to shrink
}

/*****************************************************************************/
/*                   kdws_frame_window::regenerate_layout                    */
/*****************************************************************************/

void kdws_frame_window::regenerate_layout() 
{
  // Start by finding the layout rectangles for all constituent window
  RECT frame_rect;
  GetClientRect(&frame_rect);
  assert((frame_rect.top == 0) && (frame_rect.left == 0));

  bool change_catalog_width = false;
  if (catalog_panel_width > 0)
    {
      int old_width = catalog_panel_width;
      catalog_panel_width = KD_MAX_CATALOG_PANEL_WIDTH;
      int frame_width = frame_rect.right-frame_rect.left;
      if (catalog_panel_width > (frame_width>>1))
        catalog_panel_width = frame_width>>1;
      if (catalog_panel_width < 1)
        catalog_panel_width = 1; // Make sure it doesn't go to zero.
      change_catalog_width = (catalog_panel_width != old_width);
    }

  RECT image_rect = frame_rect;
  image_rect.bottom -= (status_height + animation_height);
  if (image_rect.bottom < 8) // NB: 8 assumed to be less than scrollbar height
    image_rect.bottom = 8;
  image_rect.right -= catalog_panel_width;
  if (image_rect.right < 8) // NB: 8 assumed to be less than scrollbar width
    image_rect.right = 8;

  frame_rect.right = image_rect.right + catalog_panel_width;
  frame_rect.bottom = image_rect.bottom + status_height + animation_height;

  RECT status_rect = image_rect;
  status_rect.top = image_rect.bottom;
  status_rect.bottom = image_rect.bottom + status_height;

  RECT catalog_rect = frame_rect;
  catalog_rect.left = image_rect.right;

  image_view.SetWindowPos(NULL,image_rect.left,image_rect.top,
                          image_rect.right-image_rect.left,
                          image_rect.bottom-image_rect.top,
                          SWP_NOZORDER|SWP_SHOWWINDOW);

  if (catalog_panel_width > 0)
    catalog_view.SetWindowPos(NULL,catalog_rect.left,catalog_rect.top,
                              catalog_rect.right-catalog_rect.left,
                              catalog_rect.bottom-catalog_rect.top,
                              SWP_NOZORDER|SWP_SHOWWINDOW);
  else
    catalog_view.SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_HIDEWINDOW);

  if (progress_height > 0)
    {
      RECT prog_rect = status_rect;
      prog_rect.bottom = prog_rect.top + progress_height;
      status_rect.top = prog_rect.bottom;
      progress_indicator.SetWindowPos(NULL,prog_rect.left,prog_rect.top,
                                      prog_rect.right-prog_rect.left,
                                      prog_rect.bottom-prog_rect.top,
                                      SWP_NOZORDER|SWP_SHOWWINDOW);
    }
  else
    progress_indicator.SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_HIDEWINDOW);

  int p;
  RECT pane_rects[3];
  for (p=0; p < 3; p++)
    pane_rects[p] = status_rect;
  int width = status_rect.right-status_rect.left;
  int outer_width = (width * 3) / 10;
  int inner_width = width - 2*outer_width;
  pane_rects[0].right = pane_rects[1].left = pane_rects[0].left + outer_width;
  pane_rects[1].right = pane_rects[2].left = pane_rects[1].left + inner_width;
  for (p=0; p < 3; p++)
    status_panes[p].SetWindowPos(NULL,pane_rects[p].left,pane_rects[p].top,
                                 pane_rects[p].right-pane_rects[p].left,
                                 pane_rects[p].bottom-pane_rects[p].top,
                                 SWP_NOZORDER|SWP_SHOWWINDOW);

  if (animation_height > 0)
    { // Generate the animation bar
      RECT animation_rect = status_rect;
      animation_rect.top = status_rect.bottom;
      animation_rect.bottom = frame_rect.bottom;
      animation_bar.SetWindowPos(NULL,animation_rect.left,animation_rect.top,
                                 animation_rect.right-animation_rect.left,
                                 animation_rect.bottom-animation_rect.top,
                                 SWP_NOZORDER|SWP_SHOWWINDOW);
    }
  else
    animation_bar.SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_HIDEWINDOW);

  // Finish by invalidating all the sub-windows, so they redraw themselves
  // in their new positions
  if ((catalog_panel_width > 0) && change_catalog_width)
    catalog_view.Invalidate(TRUE);
  if (progress_height > 0)
    progress_indicator.Invalidate();
  for (p=0; p < 3; p++)
    status_panes[p].Invalidate();
  if (animation_height > 0)
    animation_bar.Invalidate();

  image_view.check_and_report_size(false);
}

/*****************************************************************************/
/*                  Message Handlers for kdws_frame_window                   */
/*****************************************************************************/

BEGIN_MESSAGE_MAP(kdws_frame_window, CFrameWnd)
  ON_WM_CREATE()
	ON_WM_SIZE()
  ON_WM_CTLCOLOR()
	ON_WM_DROPFILES()
  ON_WM_CLOSE()
  ON_WM_ACTIVATE()
  ON_WM_PARENTNOTIFY()
	ON_COMMAND(ID_FILE_OPEN, menu_FileOpen)
  ON_COMMAND(ID_FILE_OPENURL, menu_FileOpenURL)
  ON_COMMAND(ID_WINDOW_DUPLICATE, menu_WindowDuplicate)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_DUPLICATE, can_do_WindowDuplicate)
  ON_COMMAND(ID_WINDOW_NEXT, menu_WindowNext)
  ON_UPDATE_COMMAND_UI(ID_WINDOW_NEXT, can_do_WindowNext)
  ON_COMMAND(ID_WINDOW_CLOSE, OnClose)
END_MESSAGE_MAP()

/*****************************************************************************/
/*                       kdws_frame_window::OnCreate                         */
/*****************************************************************************/

int kdws_frame_window::OnCreate(CREATESTRUCT *create_struct)
{
  int result = CFrameWnd::OnCreate(create_struct);
  if (result != 0)
    return result;
  menu_tip_manager = new CMenuTipManager;
	menu_tip_manager->Install(this); // Comment out to make menu tooltips go away
	return 0;
}

/*****************************************************************************/
/*                      kdws_frame_window::OnCtlColor                        */
/*****************************************************************************/

HBRUSH kdws_frame_window::OnCtlColor(CDC *dc, CWnd *wnd, UINT control_type)
{
  // Call the base implementation first, to get a default brush
  HBRUSH brush = CFrameWnd::OnCtlColor(dc,wnd,control_type);
  for (int p=0; p < 3; p++)
    if (wnd == (status_panes+p))
      {
        dc->SetTextColor(status_text_colour);
        dc->SetBkColor(status_pane_colour);
        return status_brush;
      }
  return brush;
}

/*****************************************************************************/
/*                     kdws_frame_window::OnParentNotify                     */
/*****************************************************************************/

void kdws_frame_window::OnParentNotify(UINT message, LPARAM lParam)
{
  kdu_uint16 low_word = (kdu_uint16) message;
  if ((low_word == WM_LBUTTONDOWN) || (low_word == WM_RBUTTONDOWN) ||
      (low_word == WM_MBUTTONDOWN))
    {
      POINT point;
      point.x = (int)(lParam & 0x0000FFFF);
      point.y = (int)((lParam>>16) & 0x0000FFFF);
      this->ClientToScreen(&point); // Gets mouse location in screen coords
      RECT rect; image_view.GetWindowRect(&rect); // `rect' is in screen coords
      if ((point.x >= rect.left) && (point.x < rect.right) &&
          (point.y >= rect.top) && (point.y < rect.bottom) &&
          GetFocus() != &image_view)
        image_view.SetFocus();
    }
}

/*****************************************************************************/
/*                       kdws_frame_window::OnDropFiles                      */
/*****************************************************************************/

void kdws_frame_window::OnDropFiles(HDROP hDropInfo) 
{
  kdws_string fname(1023);
  DragQueryFile(hDropInfo,0,fname,1023);
  DragFinish(hDropInfo);
  if (renderer != NULL)
    renderer->open_file(fname);
}

/*****************************************************************************/
/*                        kdws_frame_window::OnClose                         */
/*****************************************************************************/

void kdws_frame_window::OnClose()
{
  if (renderer != NULL)
    {
      if ((renderer->editor != NULL) &&
          (interrogate_user("You have an open metadata editor, which will "
                            "be closed if you continue.  Do you still want "
                            "to close the window?",
                            "Cancel","Continue with Close") != 1))
        return;
      if (renderer->have_unsaved_edits &&
          (interrogate_user("You have edited this file but not saved these "
                            "edits ... perhaps you saved the file in a "
                            "format which cannot hold all the metadata ("
                            "use JPX).  Do you still want to close?",
                            "Cancel","Close without Saving") != 1))
        return;
    }
  catalog_view.deactivate(); // Do this before destroying windows
  if ((manager != NULL) && added_to_manager)
    { // Doing this here allows the application's main window to be changed
      // so that the application doesn't accidentally get closed down
      // prematurely by the framework.
      manager->remove_window(this);
      manager = NULL;
    }
  this->DestroyWindow();
}

/*****************************************************************************/
/*                       kdws_frame_window::OnActivate                       */
/*****************************************************************************/

void kdws_frame_window::OnActivate(UINT nState, CWnd *pWndOther,
                                   BOOL bMinimized)
{
  CFrameWnd::OnActivate(nState,pWndOther,bMinimized);
  if (renderer != NULL)
    renderer->set_key_window_status(nState != WA_INACTIVE);
  if (nState != WA_INACTIVE)
    image_view.SetFocus();
  if (this->catalog_panel_width > 0)
    catalog_view.reveal_focus();
}

/*****************************************************************************/
/*                      kdws_frame_window::menu_FileOpen                     */
/*****************************************************************************/

void kdws_frame_window::menu_FileOpen()
{
  int filename_buf_len = 4096; // Allows for a lot of files (multi-select)
  kdws_string filename_buf(filename_buf_len);
  kdws_string initial_dir(MAX_PATH);
  kdws_settings *settings = manager->access_persistent_settings();
  OPENFILENAME ofn;
  memset(&ofn,0,sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
  strcpy(initial_dir,settings->get_open_save_dir());
  if (initial_dir.is_empty())
    GetCurrentDirectory(MAX_PATH,initial_dir);

  ofn.hwndOwner = GetSafeHwnd();
  ofn.lpstrFilter =
    _T("JP2-family file ")
    _T("(*.jp2, *.jpx, *.jpf, *.mj2)\0*.jp2;*.jpx;*.jpf;*.mj2;*.kjc\0")
    _T("JP2 files (*.jp2)\0*.jp2\0")
    _T("JPX files (*.jpx, *.jpf)\0*.jpx;*.jpf\0")
    _T("MJ2 files (*.mj2)\0*.mj2\0")
    _T("Uwrapped code-streams (*.j2c, *.j2k)\0*.j2c;*.j2k\0")
    _T("Kakadu JPIP cache (*.kjc)\0*.kjc\0")
    _T("All files (*.*)\0*.*\0\0");
  ofn.nFilterIndex = settings->get_open_idx();
  if ((ofn.nFilterIndex > 5) || (ofn.nFilterIndex < 1))
    ofn.nFilterIndex = 1;
  ofn.lpstrFile = filename_buf;
  ofn.nMaxFile = filename_buf_len;
  ofn.lpstrTitle = _T("Open file (or multiple files)");
  ofn.lpstrInitialDir = initial_dir;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
  if (!GetOpenFileName(&ofn))
    {
      if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL)
        interrogate_user("Too many files selected -- exceeded allocated "
                         "buffer!","OK");
      return;
    }

  // Get the path prefix and update `settings' for next time
  int prefix_len = (int) ofn.nFileOffset;
  initial_dir.clear();
  if (prefix_len > 0)
    { // Get the initial part of the pathname
      if (prefix_len > MAX_PATH)
        {
          interrogate_user("Strange error produced by open file dialog: the "
                           "path prefix of a file to open seems to exceed the "
                           "system-wide maximum path length??","OK");
          return;
        }
      initial_dir.clear();
      memcpy((WCHAR *) initial_dir,(const WCHAR *) filename_buf,
             sizeof(WCHAR)*(size_t)prefix_len);
      prefix_len = initial_dir.strlen(); // In case nulls were inserted
      if (prefix_len > 0)
        {
          WCHAR *last_cp = initial_dir; last_cp += prefix_len-1;
          if ((*last_cp == L'/') || (*last_cp == L'\\'))
            *last_cp = L'\0';
          settings->set_open_save_dir(initial_dir);
        }
    }
  settings->set_open_idx(ofn.nFilterIndex);

  // Now finally open the file(s)
  const char *src = ((const char *) filename_buf) + ofn.nFileOffset;
  char *dst = ((char *) filename_buf) + prefix_len;
  if ((*dst == '\0') && (src > dst))
    *(dst++) = '\\';
  bool first_file = true;
  while (*src != '\0')
    {
      // Transfer filename part of the string down to `dst'
      assert(src >= dst);
      char *dp = dst;
      while (*src != '\0')
        *(dp++) = *(src++);
      *dp = '\0';
      kdws_frame_window *target_wnd = this;
      if (!first_file)
        {
          target_wnd = new kdws_frame_window();
          if (!target_wnd->init(manager))
            {
              delete target_wnd;
              break;
            }
        }
      target_wnd->open_file(filename_buf);

      first_file = false;
      if (filename_buf.is_valid_pointer(src+1))
        src++; // So we can pick up extra files, if they exist
    }
}

/*****************************************************************************/
/*                     kdws_frame_window::menu_FileOpenURL                   */
/*****************************************************************************/

void kdws_frame_window::menu_FileOpenURL()
{
  kdws_settings *settings = manager->access_persistent_settings();
  kdws_url_dialog url_dialog(settings,this);
  if (url_dialog.DoModal() == IDOK)
    this->open_url(url_dialog.get_url());
}

/*****************************************************************************/
/*                   kdws_frame_window:menu_WindowDuplicate                  */
/*****************************************************************************/

void kdws_frame_window::menu_WindowDuplicate()
{
  if ((renderer != NULL) && renderer->can_do_WindowDuplicate())
    {
      kdws_frame_window *wnd = new kdws_frame_window();
      if (!wnd->init(manager))
        {
          delete wnd;
          return;
        }
      wnd->renderer->open_as_duplicate(this->renderer);
    }
}

/*****************************************************************************/
/*                      kdws_frame_window:menu_WindowNext                    */
/*****************************************************************************/

void kdws_frame_window::menu_WindowNext()
{
  int idx = manager->get_access_idx(this);
  if (idx < 0) return;
  kdws_frame_window *target_wnd = manager->access_window(idx+1);
  if (target_wnd == NULL)
    target_wnd = manager->access_window(0);
  if (target_wnd == NULL)
    return;
  target_wnd->SetActiveWindow();
}

/*****************************************************************************/
/*                     kdws_frame_window:can_do_WindowNext                   */
/*****************************************************************************/

void kdws_frame_window::can_do_WindowNext(CCmdUI* pCmdUI)
{
  int idx = manager->get_access_idx(this);
  pCmdUI->Enable((idx > 0) ||
                 ((idx == 0) && (manager->access_window(1) != NULL)));
}
