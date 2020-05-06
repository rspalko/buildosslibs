/*****************************************************************************/
// File: kdws_window.h [scope = APPS/WINSHOW]
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
   Class definitions for the window-related objects used by the "kdu_show"
application.
******************************************************************************/
#ifndef KDWS_WINDOW_H
#define KDWS_WINDOW_H

#include "kdu_compressed.h"
#include "kdws_renderer.h"
#include "kdws_catalog.h"

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_winshow" demo app.
using namespace kdu_core;

// Defined here:
class kdws_interrogate_dlg;
class kdws_splitter_window;
class kdws_image_view;
class kdws_animation_bar;
class kdws_frame_window;

// Defined elsewhere:
class kdws_renderer;
class kdws_catalog;
class kdws_manager;
class CMenuTipManager;

#define KDWS_FOCUS_MARGIN 2 // Added to boundaries of focus region, in
  // screen space, to determine a region which safely covers the
  // focus box and any bounding outline.
#define KDWS_FOCUSSING_MARGIN 4 // Same but for the focussing rectangle
  // that is currently being dragged to define a new focus box -- lines are
  // thicker.

#define KDWS_SCROLLER_MESSAGE (WM_USER + 0x10)


/*****************************************************************************/
/*                      EXTERNAL FUNCTIONS and ARRAYS                        */
/*****************************************************************************/

extern void
  kdws_create_preferred_window_font(CFont *logfont, double scale,
                                    bool fixed_pitch=false);
  /* Invokes `logfont->CreateIndirect' with the preferred logical font
     attributes for use within the "kdu_show" application's various
     windows.  If `scale'=1.0, the relatively small font used for status and
     metadata catalog text is used.  If `fixed_pitch' is true, a preferred
     fixed-pitch font is created; otherwise though, the preferred font has
     variable pitch. */

/*****************************************************************************/
/*                          kdws_interrogate_dlg                             */
/*****************************************************************************/

class kdws_interrogate_dlg : public CDialog {
  /* This class is used to implement user interrogation services.  To do this,
     you just construct an instance of the object, passing in the query
     `message' and text to display in 0, 1, 2, 3 or 4 option buttons, then
     invoke `CDialog::DoModal' -- the return value is the index of the
     option selected, being 0, 1, 2 or 3.   If `option0' passed to the
     constructor is NULL, no option buttons will be displayed, but the
     dialog will be given a system menu for the user to kill it, and
     the `CDialog::DoModal' function will always return 0. */
  public:
    kdws_interrogate_dlg(CWnd *pParent,
                         const char *caption, const char *message,
                         const char *option0, const char *option1=NULL,
                         const char *option2=NULL, const char *option3=NULL);
    ~kdws_interrogate_dlg();
    enum { IDD = IDD_INTERROGATE_USER };
  protected:
    virtual BOOL OnInitDialog();
    DECLARE_MESSAGE_MAP()
  private:
    CStatic *get_textfield()
      { return (CStatic *)GetDlgItem(IDC_INTERROGATE_TEXT); }
    CButton *get_button1()
      { return (CButton *)GetDlgItem(IDC_INTERROGATE_BUTTON1); }
    CButton *get_button2()
      { return (CButton *)GetDlgItem(IDC_INTERROGATE_BUTTON2); }
    CButton *get_button3()
      { return (CButton *)GetDlgItem(IDC_INTERROGATE_BUTTON3); }
    CButton *get_button4()
      { return (CButton *)GetDlgItem(IDC_INTERROGATE_BUTTON4); }
    afx_msg void on_button1() { EndDialog(0); }
    afx_msg void on_button2() { EndDialog(1); } 
    afx_msg void on_button3() { EndDialog(2); }
    afx_msg void on_button4() { EndDialog(3); }
    afx_msg HBRUSH OnCtlColor(CDC *dc, CWnd *wnd, UINT control_type);
  private:
    CFont local_font;
    kdws_string *caption_string;
    kdws_string *msg;
    kdws_string *opt[4];
    COLORREF bgnd_colour;
    CBrush bgnd_brush;
};

/*****************************************************************************/
/*                             kdws_image_view                               */
/*****************************************************************************/

class kdws_image_view : public CWnd {
public: // Access methods for use by the frame window
  kdws_image_view();
  ~kdws_image_view();
  void set_manager_and_renderer(kdws_manager *manager, kdws_renderer *renderer)
    { this->manager = manager; this->renderer = renderer; }
  void set_frame_window(kdws_frame_window *frame_window)
    { this->window=frame_window; }
  kdu_coords get_screen_size() { return screen_size; }
  void set_max_view_size(kdu_coords size, int pixel_scale,
                         bool first_time=false);
    /* Called by the manager object whenever the image dimensions
       change for one reason or another.  This is used to prevent the
       window from growing larger than the dimensions of the image (unless
       the image is too tiny).  During periods when no image is loaded, the
       maximum view size should be set sufficiently large that it does not
       restrict window resizing.  The `size' value refers to the number
       of image pixels which are available for display.  Each of these
       occupies `pixel_scale' by `pixel_scale' display pixels.
          If `first_time' is true, the image dimensions have only just been
       determined for the first time.  In this case, the function is allowed
       to grow the current size of the window and it also invokes
       `kdws_manager::place_window'. */
  void check_and_report_size(bool first_time);
    /* Called when the window size may have changed or may need to be
       adjusted to conform to maximum dimensions established by the
       image -- e.g., called from inside the frame's OnSize message handler,
       or when the image dimensions may have changed. */
// ----------------------------------------------------------------------------
public: // Member functions used for scrollbar state management
  void set_scroll_metrics(kdu_coords step, kdu_coords page, kdu_coords end)
    { /* This function is called by the manager to set or adjust the
         interpretation of incremental scrolling commands.  In particular,
         these parameters are adjusted whenever the window size changes.
            The `step' coordinates identify the amount to scroll by when an
         arrow key is used, or the scrolling arrows are clicked.
            The `page' coordinates identify the amount to scroll by when the
         page up/down keys are used or the empty scroll bar region is clicked.
            The `end' coordinates identify the maximum scrolling position. */
      scroll_step = step;  scroll_page = page;  scroll_end = end;
    }
  void configure_scrollers(kdu_dims view_dims, kdu_dims image_dims);
    /* Utility function, called from `kdws_renderer::set_view_size'
       to configure the appearance of scroll bars in the image view.  Always
       invoked within the main application thread. */
  void update_scrollers();
    /* Updates the scrollbar positions and sizes after (possible) adjustments
       to the `scrollers_fixed', `scroller_viewport_fixed',
       `scroller_canvas_fixed', `scroller_viewport_unfixed' and
       `scroller_canvas_unfixed' state variables.  Always invoked within
       the main application thread. */
  bool check_scrollers_fixed() { return scrollers_fixed; }
    /* Returns true if the scrollers currently have fixed locations -- only
       happens during metadata-driven animations.  If this is the case,
       view dragging and key-based manipulation operations should be
       skipped. */
  void fix_scrollers(kdu_dims view_dims, kdu_dims image_dims);
  void release_scrollers(bool main_thread);
    /* These functions are used, together with the messandling function
       `apply_scroller_fixing', definted below, to temporarily fix the
       horizontal and vertical scroll bar locations and dimensions and to
       subsequently release them.  When the scrollers are fixed, `WM_HSCROLL'
       and `WM_VSCROLL' messages ignored, while the values passed in calls to
       `configure_image_scrollers' are remembered internally but not reflected
       to the scroll bars themselves -- they are used to reconfigure the
       scroll bars when `release_scrollers' is called.
          The `fix_scrollers' method is invoked only from the presentation
       thread during an active animation.  It temporarily locks the
       `scroller_mutex' and sets up the relevant parameters, after which
       the custom `KDWS_SCROLLER_MESSAGE' message is posted to the window,
       to be dispatched on the main application thread and the
       `scroller_mutex' is unlocked -- these operations are all skipped if
       the call would have no effect.
          The `release_scrollers' function is usually invoked bu the
       presentation thread also, when an animation frame is about to be
       displayed that does not require fixing of the scroll bar states.  The
       function is called directly from the main thread (with
       `main_thread'=true) only after animation has been stopped (see
       `kdws_renderer::stop_animation').  The function does nothing if
       `scrollers_fixed' is found to be false on entry.  Otherwise, if called
       from the presentation thread, the function locks the `scroller_mutex',
       resets the `scrollers_fixed' member, and then again posts the
       `KDWS_SCROLLER_MESSAGE' message.  If called from the main thread, this
       function updates the scrollers directly, rather than posting a
       message for `apply_scroller_fixing' to do it. */
// ----------------------------------------------------------------------------
public: // Member functions to assist with user interaction
  bool is_first_responder() { return this->have_keyboard_focus; }
    /* Returns true if the image view window currently has the keyboard
       focus.  This may affect the interpretation of various commands. */
  kdu_coords get_last_mouse_point();
    /* Returns the coordinates of the last detected mouse location,
       expressed in the compositor's coordinate system. */
  void start_focusbox(POINT point);
     /* Called when the left mouse button is pressed without shift.
      This initiates the definition of a new focus box. */
  void start_viewdrag(POINT point);
     /* Called when the left mouse button is pressed with the shift key.
      This initiates a panning operation which is driven by mouse drag
      events. */
  void cancel_focus_drag();
     /* Called whenever a key is pressed.  This cancels any focus box
      delineation or mouse drag based panning operations. */
  bool end_focus_drag(POINT point);
     /* Called when the left mouse button is released.  This ends any
      focus box delineation or mouse drag based panning operations.  Returns
      true if a focus or dragging operation was ended by the mouse release
      operation; otherwise, returns false, meaning that the user has
      depressed and released the mouse button without any intervening drag
      operation. */
  void mouse_drag_to_point(POINT point);
     /* Called when a mouse drag event occurs with the left mouse button
      down.  This affects any current focus box delineation or view
      panning operations initiated by the `start_focusbox_at' or
      `start_viewdrag_at' functions, if they have not been cancelled. */
  RECT get_rectangle_from_mouse_start_to_point(POINT point);
      /* Returns the rectangle formed between `mouse_start' and `point'. */
  void invalidate_mouse_box();
      /* Utility function called from within the various focus/drag functions
       above, in order o invalidate the rectangle defined by `mouse_start'
       through `mouse_last', so that it will be redrawn. */
  void handle_single_click(POINT point);
      /* Called when the left mouse button is released, without having dragged
       the mouse and without holding the shift key down. */
  void handle_double_click(POINT point);
      /* Called when the left mouse button is double-clicked. */
  void handle_right_click(POINT point);
      /* Called when a right-click or ctrl-left-click occurs inside the window.
         As with the other functions above, `point' is a location expressed
         relative to the present document view. */
// ----------------------------------------------------------------------------
public: // Member functions used for shape editing
  void handle_shape_editor_click(POINT point);
  void handle_shape_editor_drag(POINT point);
  void handle_shape_editor_move(POINT point);
      /* Perform selection/deselection/drag/move of a vertex in the shape
         editor, based on whether the mouse `point' (expressed relative to the
         current document view) lies within the nearest vertex painted by the
         shape editor. */
  void add_shape_scribble_point(POINT point);
      /* Add the current mouse point to the scribble points associated with
         the shape editor and redraw, as required. */
  bool snap_doc_point_to_shape(POINT point, kdu_coords *shape_point);
      /* This function maps `point' from the document view to the
         integer coordinate system of the shape editor, if there is one -- if
         there is no current shape editor, the function immediately returns
         true.  The function additionally implements a policy for "snapping"
         the supplied `point' to nearby anchor points, region boundaries and
         editing guide lines in the shape editor, so as to facilitate
         interactive editing. */
  void repaint_shape_region(kdu_dims shape_region);
      /* Performs repainting of the region of the document view which is
         affected by `shape_region', as expressed on the codestream
         coordinate system used by an installed `jpx_roi_editor' shape
         editor. */
// ----------------------------------------------------------------------------
public: // Cursor manipulation
  void set_drag_cursor(bool set_to_drag);
    /* If `set_to_drag' is true, the drag cursor is installed; otherwise, the
       regular cursor is restored. */
// ----------------------------------------------------------------------------
private: // Data
  kdws_manager *manager;
  kdws_renderer *renderer;
  kdws_frame_window *window;
  int pixel_scale; // Display pixels per image pixel
  int last_pixel_scale; // So we can resize display in `check_and_report_size'
  kdu_coords screen_size; // Dimensions of the display
  kdu_coords max_view_size; // Max size for client area (in display pixels)
  bool max_view_size_known; // If the `renderer' has called `set_max_view_size'
  kdu_coords last_size;
  bool sizing; // Flag to prevent recursive calls to resizing functions.

private: // Scrollbar management state information
  kdu_coords scroll_step, scroll_page, scroll_end;
  kdu_mutex scroller_mutex;  // Protects the following members as required
  bool scrollers_fixed;
  kdu_dims scroller_viewport_fixed;
  kdu_dims scroller_canvas_fixed;
  kdu_dims scroller_viewport_unfixed;
  kdu_dims scroller_canvas_unfixed;
  bool scrollers_fixed_pending;       // values of `scrollers_fixed' and the
  kdu_dims scroller_viewport_pending; // relevant scroller dimensions that are
  kdu_dims scroller_canvas_pending;   // pending a `apply_scroller_fixing' call

private: // User interaction management state
  bool have_keyboard_focus;
  bool is_focussing; // If a new focus box is being delineated
  bool is_dragging; // While panning under mouse control
  bool shape_dragging; // If mouse down while editing ROI shape
  bool shape_scribbling; // If mouse down while scribbling an ROI path/boundary
  POINT mouse_start; // Point at which focus/editing commenced
  POINT mouse_last; // Point last reached dragging mouse during focus/editing
  bool target_is_focus; // If draggin/zooming targets the focus box
  HCURSOR normal_cursor;
  HCURSOR drag_cursor;
// ----------------------------------------------------------------------------
protected: // MFC overrides
	void OnPaint();
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
// ----------------------------------------------------------------------------
protected: // Message map functions
  afx_msg LRESULT apply_scroller_fixing(WPARAM wparam, LPARAM lparam);
    /* See `fix_scrollers' and `release_scrollers'. */
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnNcHitTest(CPoint point);
  afx_msg void OnNcLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnNcRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  afx_msg void OnKillFocus(CWnd *pNewWnd);
  afx_msg void OnSetFocus(CWnd *pOldWnd);
	DECLARE_MESSAGE_MAP()
};

/*****************************************************************************/
/*                           kdws_animation_bar                              */
/*****************************************************************************/

class kdws_animation_bar : public CWnd {
public: // Member functions
  kdws_animation_bar();
  ~kdws_animation_bar();
  void init(int line_height, kdws_image_view *iview)
    { this->line_height = line_height; this->image_view = iview; }
    /* Called before the window is created -- the animation bar is layed out
       in two lines whose height is derived from the height of the frame
       window's status bar. */
  void set_target(kdws_renderer *renderer, bool can_position)
    { this->target = renderer;  this->allow_positioning = can_position; }
    /* Used by `kdws_frame_window::set_animation_bar'. */
  void reset();
    /* This function resets internal state information that cannot
       be directly manipulated by `set_state'.  It resets any rendering
       `target' to NULL, resets the slider interval to a starting
       default value and enables all controls. */
  void set_state(bool playing_forward, bool playing_reverse,
                 bool repeat_enabled, double custom_frame_rate,
                 double native_frame_rate_multiplier);
    /* This function configures the state information displayed in the
       animation bar.  Much, if not all of this state information can
       also be changed from within the animation bar, but this happens
       by sending messages to the `kdws_renderer' object, which responds
       eventually by invoking this function.  If `custom_frame_rate' < 0,
       native source timing is used with a scaling factor of
       `native_frame_rate_multiplier'.  Otherwise, a custom frame rate
       is being used and `native_frame_rate_multiplier' is ignored. */
  void set_frame(int frame_idx, int max_frame_idx, double frame_start_time,
                 double frame_end_time, double track_end_time);
  /* The application uses this functions to set the current frame position
     identified by the animation bar, along with associated attributes.
        If the data source contains timing information, the `frame_start_time'
     and `frame_end_time' values should be non-negative, with
     `frame_end_time' >= `frame_start_time'.  In this case, the animation
     bar displays frame times, rather than frame indices.  If the data
     source does not provide timing information, the `frame_start_time'
     values should be -ve and `frame_end_time' and `track_end_time' are
     both ignored.
        If the number of frames in the animation is not known, both
     `max_frame_idx' and `track_end_time' should be -ve.  Otherwise,
     `max_frame_idx' should hold the index of the last frame in the
     animation source and `track_end_time' should hold its `frame_end_time'
     unless `frame_start_time' is -ve (in that case we only have frame index
     information).
        All times are measured in seconds using the original data source's
     timebase, where `frame_start_time' should be exactly 0.0 for the
     first frame of the track. */
// ----------------------------------------------------------------------------
private: // Helper functions
  double get_natural_slider_interval(int adjust, double &step);
    /* If `adjust' is 0, this function returns the closest natural value
       for `slider_interval' to the value that it currently holds, depending
       on the `show_indices' flag.  If `adjust' is +ve, the function returns
       the closest natural value for `slider_interval' that is larger than
       the value it currently holds; similarly, if `adjust' is -ve, the
       function returns the closest natural value for `slider_interval' that
       is smaller than the value it currently holds.  The function also
     returns natural step sizes for the slider -- e.g., seconds, tens of
     seconds, minutes, etc.; the`slider_start' and `slider_end' must
     be multiples of this step size and the ticks places on the slider
     are separated also by this step size. */
  void update_controls_for_track_pos(bool force_update);
    /* Adjusts `slider_start' and `slider_end' to contain `track_pos'.  If
       any changes are made, the first row of controls is modified, as
       required.  The `force_update' flag forces updating of all relevant
       controls, even if their values do not seem to have changed.  This
       is useful if, for example, the `show_indices' flag has changed
       state. */
  void update_play_controls();
    /* Modifies (if necessary) the state of the `fwd_playbutton',
       `rev_playbutton' and `stop_playbutton' controls. */
  void draw_slider_ticks();
    /* Places ticks at meaningful intervals and locations on the slider. */
  void regenerate_layout();
    /* Call this when the animation bar's dimensions changes. */
// ----------------------------------------------------------------------------
private: // State information
  int line_height; // Controls positioned across two lines
  int bar_width; // When this changes, `regenerate_layout' must be called
  kdws_image_view *image_view; // Lets us set focus to that view
  kdws_renderer *target;
  bool show_indices; // If frame indices are shown rather than time
  bool first_frame; // If `track_pos' corresponds to the first frame
  bool last_frame; // If `track_pos' corresponds to the last frame
  double cur_frame_start; // Nominal start time of current frame in track
  double cur_frame_end; // Nominal end time of current frame in track
  double track_interval; // Nominal duration of track -- -ve if not known
  double slider_start; // Nominal time at left edge of slider
  double slider_end; // Nominal time at right edge of slider
  double slider_interval; // Preferred nominal time range covered by slider
  double slider_step; // Slider end-points must be multiples of the step
  bool play_repeat; // True if repeat mode is activated
  bool play_fwd; // True if currently playing forward
  bool play_rev; // True if currently playing backwards
  bool allow_positioning; // If the user can manually position the animation
  bool allow_auto_slide; // If slider shifts along when we get near either end
  double custom_rate;
  double rate_multiplier; // Valid if `custom_rate' is -ve
// ----------------------------------------------------------------------------
private: // Current state of controls -- so we don't needlessly set it
  bool fwd_play_enabled;
  bool rev_play_enabled;
  bool stop_play_enabled;
  bool fwd_step_enabled;
  bool rev_step_enabled;
  bool step_button_hilite;
private: // Resources and information for controls
  CFont fixed_font; // Small, fixed-pitch font for time stamps, etc.
  CFont var_font; // Small, variable-pitch font, used rarely
  CBrush background_brush;
  CSize left_timefield_size;
  CSize right_timefield_size;
  CSize play_ratefield_size;
  CSize spin_size; // Size of a spin control
  CSize button_size; // Size of a bitmap button
  CSize native_button_size; // This button has no bitmap
  CSize repeat_button_size; // This button has no bitmap
// ----------------------------------------------------------------------------
private: // Controls, in order of appearance within upper line of the bar
  CStatic left_timefield;
  CBitmapButton left_timebutton;
  CSliderCtrl central_slider;
  CBitmapButton right_timebutton;
  CStatic right_timefield;
  CSpinButtonCtrl right_timespin;
// ----------------------------------------------------------------------------
private: // Controls, in order of appearance within base line of the bar
  CBitmapButton fwd_playbutton;
  CBitmapButton go_startbutton;
  CBitmapButton stop_playbutton;
  CBitmapButton go_endbutton;
  CBitmapButton rev_playbutton;
  CBitmapButton fwd_stepbutton;
  CBitmapButton rev_stepbutton;
  CStatic step_hilitebox;
  CStatic play_ratefield;
  CSpinButtonCtrl play_ratespin;
  CButton play_nativebutton;
  CButton play_repeatbutton;
// ----------------------------------------------------------------------------
protected: // MFC overrides
	void OnPaint();
  virtual BOOL PreCreateWindow(CREATESTRUCT &cs);
  void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
// ----------------------------------------------------------------------------
protected: // Message map functions
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnSize(UINT nType, int cx, int cy)
    { if ((cx > 0) && (cy > 0)) regenerate_layout(); }
  afx_msg void deltapos_right_timespin(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void deltapos_play_ratespin(NMHDR* pNMHDR, LRESULT* pResult);
  afx_msg void clicked_play_repeatbutton();
  afx_msg void clicked_play_nativebutton();
  afx_msg void clicked_fwd_playbutton();
  afx_msg void clicked_rev_playbutton();
  afx_msg void clicked_stop_playbutton();
  afx_msg void clicked_go_startbutton();
  afx_msg void clicked_go_endbutton();
  afx_msg void clicked_left_timebutton();
  afx_msg void clicked_right_timebutton();
  afx_msg void clicked_fwd_stepbutton();
  afx_msg void clicked_rev_stepbutton();
  DECLARE_MESSAGE_MAP()
};

/*****************************************************************************/
/*                            kdws_frame_window                              */
/*****************************************************************************/

class kdws_frame_window : public CFrameWnd {
// ----------------------------------------------------------------------------
public: // Member functions
	kdws_frame_window();
  virtual ~kdws_frame_window();
  bool init(kdws_manager *app);
    /* Call this right after the constructor, to load the frame window and
       its menu. */
  int interrogate_user(const char *message, const char *option_0,
                       const char *option_1=NULL, const char *option_2=NULL,
                       const char *option_3=NULL);
    /* Presents an informative message to the user with up to four options,
       returning the index of the selected option.  If `option_1' is an empty
       string, only the first option is presented to the user.  If
       `option_2' is emty, at most two options are presented to the user; and
       so on. */
  void open_file(const char *filename);
    /* Invokes `renderer->open_file' after first performing some validation
       checks. */
  void open_url(const char *url);
    /* Invokes `renderer->open_file' after first performing some validation
       checks. */
  void note_jpip_client_reconnecting(kdu_client *client);
    /* Called from `kdws_manager::reconnect_jpip_client' if the
       client is found to be disconnected, after `kdu_client::reconnect' has
       been invoked.  The function is invoked on every window except the one
       that originated the call.  Each `renderer' that is using `client'
       should call back into `kdws_manager::reconnect_jpip_client' to
       get assigned a new queue-id. */
  bool do_processing();
  bool display_notification();
  bool client_notification();
    /* The above three functions are all invoked, as needed, from the
       window manager when the run-loop is about to become idle.
       They may also be invoked from other points in the run-loop message
       queue if a call to `kdws_notification_manager::process_notifications'
       was scheduled in response to a display update notification, a JPIP
       cache update notification or a "processing-needed" notification.
       Each function returns true if and only if a subsequent call to
       `do_processing' may be beneficial. */
  void wakeup(double scheduled_time, double current_time);
    /* Passes on scheduled wakeup events generated by the manager. */
  void adjust_playclock(double delta);
    /* Passes on adjustments delivered to all windows via the manager. */
  bool application_can_terminate();
    /* Used by the application to see if it should be allowed to quit at
       this point in time -- typically, if there are unsaved edits, the
       implementation will interrogate the user in order to decide. */
  void application_terminating();
    /* Sent by the application before closing all windows. */
  void cancel_pending_show();
    /* Simply invokes `renderer->cancel_pending_show', if non-NULL. */
  bool is_key_window() { return (this == GetActiveWindow()); }
    /* Returns true if this frame window has key status.  By that we mean
       that the frame window is the active window on the display. */
  void set_status_strings(const char **three_strings);
    /* Takes an array of three character strings, whose contents
       are written to the left, centre and right status bar panels.
       Use NULL or an empty string for a panel you want to leave empty. */
  void set_progress_bar(double value);
    /* If a progress bar does not already exist, one is created, which may
       cause some resizing of the document view.  In any event, the new
       or existing progress bar's progress value is set to `value', which
       is expected to lie in the range 0 to 100. */
  void remove_progress_bar();
    /* Make sure there is no progress bar being displayed.  This function
       also removes the metadata catalog if there is one, for simplicity.
       This should cause no problems, since the progress bar is only removed
       from within `kdws_renderer::close_file' which also removes any
       existing metadata catalog. */
  kdws_animation_bar *get_animation_bar(bool allow_positioning=true);
    /* If an animation bar is not already present, one is created, which
       may cause some resizing of the document view and (if present)
       the metadata catalog.  Activity within the animation bar
       may invoke functions within the `renderer'.  If `allow_positioning'
       is false, interactive controls for repositioning the animation
       are disabled -- this mode is currently used for cases where the
       indices displayed in the animation bar only have temporary meaning,
       induced by a metadata-driven animation. */
  void remove_animation_bar();
    /* Make sure there is no animation bar being displayed. */
  void reset_animation_bar() { animation_bar.reset(); }
    /* This function invokes `kdws_animation_bar::reset' to reset
       internal state information associated with the animation
       bar that is normally preserved between calls to
       `remove_animation_bar' and `get_animation_bar'.  We provide
       this function so that `kdws_renderer::close_file' can
       remove settings that might cause confusion if they
       persist when a new data source is opened within the window.
       The function should be called after removing any open
       animation bar; even if the animation bar is not currently
       open, this function should be called because otherwise
       persistent state information set up when the animation bar
       was last open may survive. */
  void create_metadata_catalog();
    /* Called from the main menu or from within `kdws_renderer' to create
       the `catalog_panel' and associated objects to manage a metadata
       catalog.  Also invokes `renderer->set_metadata_catalog_source'. */
  void remove_metadata_catalog();
    /* Called from the main menu or from within `kdws_renderer' to remove
       the `catalog_panel' and associated objects.  Also invokes
       `renderer->set_metadata_catalog_source' with a NULL argument. */
// ----------------------------------------------------------------------------
protected: // MFC overrides
  virtual BOOL PreCreateWindow(CREATESTRUCT &cs);
  virtual BOOL OnCreateClient(CREATESTRUCT *cs, CCreateContext *ctxt); 
  virtual void PostNcDestroy();
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void *pExtra,
                        AFX_CMDHANDLERINFO *pHandlerInfo);
    // Used to route commands to the renderer
// ----------------------------------------------------------------------------
private: // Internal helper functions
  void regenerate_layout();
    /* Call this whenever anything might have changed the layout of views
       and controls within the frame window. */
// ----------------------------------------------------------------------------
private: // Client windows
  kdws_image_view image_view;
  CProgressCtrl progress_indicator; // At top of status bar, if present
  CStatic status_panes[3]; // At bottom of status bar
  CMenuTipManager *menu_tip_manager;
  kdws_catalog catalog_view;
  kdws_animation_bar animation_bar;
private: // Fonts, colours & brushes
  COLORREF status_pane_colour;
  COLORREF status_text_colour;
  COLORREF catalog_panel_colour;
  CBrush status_brush;
  CFont status_font;
private: // Other state information
  kdws_manager *manager;
  kdws_renderer *renderer;
  bool added_to_manager; // True once `manager->add_window' called
  int progress_height; // 0 if there is no progress bar
  int status_height; // Height of status bar (panes + progress), in pixels
  int status_text_height; // Height of the font + 4
  int animation_line_height; // Animation bar needs at least two lines
  int animation_height; // Height of animation bar, if any, in pixels
  int catalog_panel_width; // 0 if no catalog
  int status_pane_lengths[3]; // Num chars currently in each status pane + 1
  char *status_pane_caches[3]; // Caches current string contents of each pane
  char status_pane_cache_buf[256]; // Provides storage for the above array
// ----------------------------------------------------------------------------
public: // Diagnostic functions
#ifdef _DEBUG
  virtual void AssertValid() const
  {
    CFrameWnd::AssertValid();
  }
  virtual void Dump(CDumpContext& dc) const
  {
    CFrameWnd::Dump(dc);
  }
#endif
// ----------------------------------------------------------------------------
protected: // Message map functions
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnClose();
  afx_msg void OnActivate(UINT nState, CWnd *pWndOther, BOOL bMinimized);
  afx_msg void OnParentNotify(UINT message, LPARAM lParam);
	afx_msg void OnSize(UINT nType, int cx, int cy)
    { if ((cx > 0) && (cy > 0)) regenerate_layout(); }
	afx_msg void OnDropFiles(HDROP hDropInfo);
  afx_msg HBRUSH OnCtlColor(CDC *dc, CWnd *wnd, UINT control_type);
  afx_msg void menu_FileOpen();
  afx_msg void menu_FileOpenURL();
  afx_msg void menu_WindowDuplicate();
  afx_msg void can_do_WindowDuplicate(CCmdUI* pCmdUI)
    { pCmdUI->Enable((renderer != NULL) &&
                     renderer->can_do_WindowDuplicate()); }
  afx_msg void menu_WindowNext();
  afx_msg void can_do_WindowNext(CCmdUI* pCmdUI);
  DECLARE_MESSAGE_MAP()
};

#endif // KDWS_WINDOW_H
