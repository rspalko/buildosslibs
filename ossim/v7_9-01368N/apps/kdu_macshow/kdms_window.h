/*****************************************************************************/
// File: kdms_window.h [scope = APPS/MACSHOW]
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
   Defines the `kdms_window' and `kdms_document_view' Objective-C classes,
which manage most of the Cocoa interaction for an single image/video source.
The rendering aspects are handled by the `kdms_renderer' object.  There is
always a single `kdms_renderer' object for each `kdms_window'.
******************************************************************************/

#import <Cocoa/Cocoa.h>
#import "kdms_renderer.h"
#import "kdms_controller.h"

#define KDMS_FOCUS_MARGIN 2.0F // Added to boundaries of focus region, in
  // screen space, to determine a region which safely covers the
  // focus box and any bounding outline.
#define KDMS_FOCUSSING_MARGIN 4.0F // Same but for the focussing rectangle
  // that is currently being dragged to define a new focus box -- lines are
  // thicker.

/*****************************************************************************/
/*                               kdms_scroller                               */
/*****************************************************************************/

@interface kdms_scroller : NSScroller {
  double fixed_pos;
  double fixed_size;
  id old_target; // non-nil if scroller settings are fixed
  SEL old_action; // Saved when scroller settings are fixed
  double unfixed_pos; // Scroller position to restore when unfixed
  double unfixed_size; // Scroller size to restore when unfixed
  NSControlTint unfixed_tint; // Scroller tint to restore when unfixed
}
+(BOOL)isCompatibleWithOverlayScrollers;
   /* Answer is NO. */
-(id)initWithFrame:(NSRect)frame_rect;
   /* The `frame_rect' is important only for determining whether the scroller
      is a vertical or horizontal scroll bar -- based on the relative
      dimensions. */
-(void)mouseDown:(NSEvent *)theEvent;
   /* This function overrides the base member to disable user interaction if
      the scroller has been fixed. */
-(void)setKnobProportion:(CGFloat)proportion;
-(void)setDoubleValue:(double)aDouble;
-(void)setControlTint:(NSControlTint)tint;
   /* These three functions override the base members to catch attempts by the
      NSScrollView owner to change the state of the scroll bar.  If the
      scroller is fixed, we record the desired change internally but do not
      pass it on to the base `NSScroller' class until the scroller is
      `unfix'ed. */
-(void)fixPos:(double)pos andSize:(double)size;
-(void)unfix;

@end // kdms_scroller

/*****************************************************************************/
/*                             kdms_document_view                            */
/*****************************************************************************/

@interface kdms_document_view : NSView {
  kdms_renderer *renderer;
  kdms_window *wnd;
  BOOL paint_region_allowed; // If YES, `renderer->paint_region' can be called
  bool is_first_responder; // Otherwise, the catalog panel may be 1st responder
  bool is_focussing; // If a new focus box is being delineated
  bool is_dragging; // While panning under mouse control
  bool shape_dragging; // If mouse down while editing ROI shape
  bool shape_scribbling; // If mouse down while scribbling an ROI path/boundary
  NSPoint mouse_start; // Point at which focus/editing commenced
  NSPoint mouse_last; // Point last reached while focussing/dragging
  NSPoint last_mouse_shift; // Last value passed to `shift_mouse'
  bool in_gesture; // Between beginGestureWithEvent and endGestureWithEvent
  bool in_gesture_drag; // When dragging view/focus around via gestures
  bool in_gesture_zoom; // When interactively zooming the view via gestures
  bool target_is_focus; // If dragging/zooming targets the focus box
  NSPoint gesture_stationary_point; // Location unaffected by gesture zooming
  NSSize focus_residual_zoom; // Amount by which the focus box should ideally
         // be zoomed, were it not for the fact that the box must have
         // integer-valued dimensions, no larger than those of the image
         // itself; used only if `target_is_focus' and `in_gesture_zoom'.
}
- (id)initWithFrame:(NSRect)frame;
- (void)set_renderer:(kdms_renderer *)renderer;
- (void)set_owner:(kdms_window *)owner;
- (void)enablePaintRegion:(BOOL)enable;
      /* The above function is used to temporarily prevent calls to
         `kdms_renderer::paint_region' from within `drawRect'.  This is done
         only during animation and then only temporarily while performing a
         Window-related operation that might trigger painting operations. */
- (BOOL)isOpaque;
- (void)drawRect:(NSRect)rect;
- (bool)is_first_responder;
      /* Allows the `renderer' object to discover whether or not the document
         view is the first responder within its window -- if not, and the
         catalog panel is active, it may be the first responder. */
- (void)start_focusbox_at:(NSPoint)point;
      /* Called when the left mouse button is pressed without shift.
         This initiates the definition of a new focus box. */
- (void)start_viewdrag_at:(NSPoint)point;
      /* Called when the left mouse button is pressed with the shift key.
         This initiates a panning operation which is driven by mouse drag
         events. */
- (void)cancel_focus_drag;
      /* Called whenever a key is pressed.  This cancels any focus box
         delineation or mouse drag based panning operations. */
- (bool)end_focus_drag_at:(NSPoint)point;
      /* Called when the left mouse button is released.  This ends any
         focus box delineation or mouse drag based panning operations.  Returns
         true if a focus or dragging operation was ended by the mouse release
         operation; otherwise, returns false, meaning that the user has
         depressed and released the mouse button without any intervening drag
         operation. */
- (void)mouse_drag_to:(NSPoint)point;
      /* Called when a mouse drag event occurs with the left mouse button
         down and we are not in the `is_panning' condition.  This affects
         any current focus box delineation initiated by the `start_focusbox_at'
         or `start_viewdrag_at' functions, if they have not been cancelled. */
- (void)mouse_drag_by:(NSPoint)displacement;
      /* Called instead of `mouse_drag_to' when we are panning, because during
         these events, the mouse is disassociated from the cursor position.
         This function registers changes in the mouse position and
         (only if `target_is_focus' is true) directly moves the mouse cursor
         to the new location.  The function is also called in response to
         a drag gesture. */
- (void)shift_mouse:(NSPoint)displacement;
      /* Convenience function to displace the location of the mouse cursor
         by the indicated amount (in screen coordinates) without generating
         mouse events. */
- (void)cancel_gesture_handling;
      /* Called from `endGestureWithEvent' if `in_gesture' is true.  Also
         called if the mouse button is pressed or the mouse wheel used during
         a gesture. */
- (NSRect)rectangleFromMouseStartToPoint:(NSPoint)point;
      /* Returns the rectangle formed between `mouse_start' and `point'. */
- (void)invalidate_mouse_box;
      /* Utility function called from within the various focus/drag functions
         above, in order o invalidate the rectangle defined by `mouse_start'
         through `mouse_last', so that it will be redrawn. */
- (void)handle_single_click:(NSPoint)point;
      /* Called when the left mouse button is released, without having dragged
         the mouse and without holding the shift key down. */
- (void)handle_double_click:(NSPoint)point;
      /* Called when the left mouse button is double-clicked. */
- (void)handle_right_click:(NSPoint)point;
      /* Called when a right-click or ctrl-left-click occurs inside the window.
         As with the other functions above, `point' is a location expressed
         relative to the present document view. */
- (void)handle_shape_editor_click:(NSPoint)point;
- (void)handle_shape_editor_drag:(NSPoint)point;
- (void)handle_shape_editor_move:(NSPoint)point;
      /* Perform selection/deselection/drag/move of a vertex in the shape
         editor, based on whether the mouse `point' (expressed relative to the
         current document view) lies within the nearest vertex painted by the
         shape editor. */
- (void)add_shape_scribble_point:(NSPoint)point;
      /* Add the current mouse point to the scribble points associated with
         the shape editor and redraw, as required. */
- (bool)snap_doc_point:(NSPoint)point to_shape:(kdu_coords *)shape_point;
      /* This function maps `point' from the document view to the
         integer coordinate system of the shape editor, if there is one -- if
         there is no current shape editor, the function immediately returns
         true.  The function additionally implements a policy for "snapping"
         the supplied `point' to nearby anchor points, region boundaries and
         editing guide lines in the shape editor, so as to facilitate
         interactive editing. */
- (void)repaint_shape_region:(kdu_dims)shape_region;
      /* Performs repainting of the region of the document view which is
         affected by `shape_region', as expressed on the codestream
         coordinate system used by an installed `jpx_roi_editor' shape
         editor. */
//-----------------------------------------------------------------------------
// User Event Override Functions
- (BOOL)acceptsFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)resignFirstResponder;
- (void)rightMouseDown:(NSEvent *)mouse_event;
- (void)mouseDown:(NSEvent *)mouse_event;
- (void)mouseDragged:(NSEvent *)mouse_event;
- (void)mouseUp:(NSEvent *)mouse_event;
- (void)beginGestureWithEvent:(NSEvent *)event;
- (void)endGestureWithEvent:(NSEvent *)event;
- (void)magnifyWithEvent:(NSEvent *)event;
- (void)scrollWheel:(NSEvent *)wheel_event;
- (void)keyDown:(NSEvent *)key_event;

@end // kdms_document_view

/*****************************************************************************/
/*                               kdms_glview                                 */
/*****************************************************************************/

@interface kdms_glview : NSOpenGLView {
  NSScrollView *scroll_view; // We aim to cover the contents of this view
  kdms_document_view *doc_view; // Forward gestures to the underlying doc_view
  kdms_renderer *renderer;
  bool allow_main_thread_draw;
}
// Own functions
-(kdms_glview *)initWithFrame:(NSRect)frame
                   scrollView:(NSScrollView *)scroll_view
                      docView:(kdms_document_view *)doc_view
                  andRenderer:(kdms_renderer *)renderer;
-(void)dealloc; // Destroys the `mutex'
-(void)prepareOpenGL; // Configures the OpenGL rendering state after `init...'
-(void)lock_context;    // Invokes `CGLLockContext' -- recursive so always safe
-(void)unlock_context;  // Invokes `CGLUnlockContext'

// Overrides
-(void)reshape; // Called whenever the scroll view may have moved/resized
-(void)update; // Sets `allow_main_thread_draw' to true
-(void)drawRect:(NSRect)rect; // Does nothing unless `allow_main_thread_draw'
-(BOOL)isOpaque;
-(BOOL)acceptsFirstResponder; // We accept first responder status and pass
-(BOOL)becomeFirstResponder;  // it down to the `kdms_document_view' object,
    // which is incapable of receiving the first responder status all by
    // itself, since it is largely covered by the `kdms_glview'.

// Messages forwarded to `doc_view'
-(void)beginGestureWithEvent:(NSEvent *)event;
-(void)endGestureWithEvent:(NSEvent *)event;
-(void)magnifyWithEvent:(NSEvent *)event;

@end // kdms_glview

/*****************************************************************************/
/*                           kdms_animation_bar                              */
/*****************************************************************************/

@interface kdms_animation_bar : NSBox {
  kdms_window *window;
  kdms_document_view *doc_view;
  kdms_renderer *target;
  float line_height; // Controls positioned across two lines
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
  // --------------------------------------------------------------------------
  bool fwd_play_enabled;
  bool rev_play_enabled;
  bool stop_play_enabled;
  bool fwd_step_enabled;
  bool rev_step_enabled;
  bool step_button_hilite;
  bool step_controls_hidden; // So we don't unhide a box that does not fit
  // --------------------------------------------------------------------------
  NSTextField *left_timefield;
  NSButton *left_timebutton;
  NSSlider *central_slider;
  NSButton *right_timebutton;
  NSTextField *right_timefield;
  NSStepper *right_timespin; 
  // --------------------------------------------------------------------------
  NSButton *fwd_playbutton;
  NSButton *go_startbutton;
  NSButton *stop_playbutton;
  NSButton *go_endbutton;
  NSButton *rev_playbutton;
  NSButton *fwd_stepbutton;
  NSButton *rev_stepbutton;
  NSBox *step_hilitebox;
  NSTextField *play_ratefield;
  NSStepper *play_ratespin;
  NSButton *play_nativebutton;
  NSButton *play_repeatbutton;    
}
-(kdms_animation_bar *)initWithFrame:(NSRect)frame_rect
                              window:(kdms_window *)wnd
                            doc_view:(kdms_document_view *)view
                       andLineHeight:(float)height;
-(void)dealloc;
-(void)set_target:(kdms_renderer *)renderer can_position:(bool)allow;
  /* Used by `kdms_frame_window::set_animation_bar'. */
-(void)reset;
  /* This function resets internal state information that cannot
     be directly manipulated by `set_state'.  It resets any rendering
     `target' to NULL, resets the slider interval to a starting
     default value and enables all controls. */
-(void)set_play_fwd:(bool)playing_forward
                rev:(bool)playing_reverse
             repeat:(bool)repeat_enabled
                fps:(double)custom_frame_rate
         multiplier:(double)native_frame_rate_multiplier;
  /* This function configures the state information displayed in the
     animation bar.  Much, if not all of this state information can
     also be changed from within the animation bar, but this happens
     by sending messages to the `kdws_renderer' object, which responds
     eventually by invoking this function.  If `custom_frame_rate' < 0,
     native source timing is used with a scaling factor of
     `native_frame_rate_multiplier'.  Otherwise, a custom frame rate
     is being used and `native_frame_rate_multiplier' is ignored. */
-(void)set_frame:(int)frame_idx
             max:(int)max_frame_idx
           start:(double)frame_start_time
             end:(double)frame_end_time
  track_duration:(double)track_end_time;
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
// Helper functions
- (double)get_natural_slider_interval:(int) adjust and_step:(double &)step;
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
- (void)update_controls_for_track_pos:(bool)force_update;
  /* Adjusts `slider_start' and `slider_end' to contain `track_pos'.  If
     any changes are made, the first row of controls is modified, as
     required.  The `force_update' flag forces updating of all relevant
     controls, even if their values do not seem to have changed.  This
     is useful if, for example, the `show_indices' flag has changed
     state. */
- (void)update_play_controls;
  /* Modifies (if necessary) the state of the `fwd_playbutton',
     `rev_playbutton' and `stop_playbutton' controls. */
- (void)draw_slider_ticks;
  /* Places ticks at meaningful intervals and locations on the slider. */
// ----------------------------------------------------------------------------
// Notification Functions
- (void)contentBoundsDidChange:(NSNotification *)notification;
// ----------------------------------------------------------------------------
// Actions
-(IBAction)moved_sliderthumb:(NSSlider *)sender;
-(IBAction)deltapos_right_timespin:(NSStepper *)sender;
-(IBAction)deltapos_play_ratespin:(NSStepper *)sender;
-(IBAction)clicked_fwd_playbutton:(NSButton *)sender;
-(IBAction)clicked_rev_playbutton:(NSButton *)sender;
-(IBAction)clicked_stop_playbutton:(NSButton *)sender;
-(IBAction)clicked_go_startbutton:(NSButton *)sender;
-(IBAction)clicked_go_endbutton:(NSButton *)sender;
-(IBAction)clicked_fwd_stepbutton:(NSButton *)sender;
-(IBAction)clicked_rev_stepbutton:(NSButton *)sender;
-(IBAction)clicked_left_timebutton:(NSButton *)sender;
-(IBAction)clicked_right_timebutton:(NSButton *)sender;
-(IBAction)clicked_play_repeatbutton:(NSButton *)sender;
-(IBAction)clicked_play_nativebutton:(NSButton *)sender;

@end // kdms_animation_bar

/*****************************************************************************/
/*                               kdms_window                                 */
/*****************************************************************************/

@interface kdms_window : NSWindow {
  kdms_window_manager *window_manager;
  kdms_renderer *renderer;
  kdms_document_view *doc_view;
  NSCursor **manager_cursors;
  NSCursor *doc_cursor_normal; // Obtained from the controller
  NSCursor *doc_cursor_dragging; // Obtained from the controller
  kdms_scroller *hor_scroller;
  kdms_scroller *vert_scroller;
  NSScrollView *scroll_view;
  kdms_glview *glview; // Used only for animations
  //---------------------------------------------------------------------------
  kdu_mutex scroller_mutex;        // Manipulated by `releaseScrollers',
  bool scrollers_fixed;            // `fixScrollerPos:andWdith', and
  double hor_scroller_fixed_pos;   // `applyScrollerFixing' methods.
  double hor_scroller_fixed_size;  // The first two functions set up the state
  double vert_scroller_fixed_pos;  // values then schedule the 3rd function
  double vert_scroller_fixed_size; // to be performed on the main thread
  //---------------------------------------------------------------------------
  NSView *status_bar; // Contains status panes, progress bar and animation bar
  NSFont *status_font; // Used for status panes, catalog paste bar & buttons
  NSTextField *status_panes[3]; // At bottom of status bar
  float status_height; // Includes status panes + any progress bar
  float status_pane_height; // Just the status panes -- fixed
  int status_pane_lengths[3]; // Num chars currently in each status pane + 1
  char *status_pane_caches[3]; // Caches current string contents of each pane
  char status_pane_cache_buf[256]; // Provides storage for the above array
  //---------------------------------------------------------------------------
  NSProgressIndicator *progress_indicator; // At top of status bar, if present
  float progress_height; // 0 unless progress indicator is being displayed
  //---------------------------------------------------------------------------
  kdms_animation_bar *animation_bar;
  float animation_line_height; // Animation bar needs at least two lines
  float animation_height; // Height of animation bar, if any, in pixels
  //---------------------------------------------------------------------------
  NSScrollView *catalog_panel; // Created on demand for metadata catalog
  NSBox *catalog_tools; // Created on demand for metadata catalog
  NSButton *catalog_paste_bar; // Button that displays pasted text
  NSButton *catalog_paste_clear; // Button that clears the paste bar
  NSButton *catalog_buttons[3]; // History "back"/"fwd" and "peer" buttons
  float catalog_tools_height; // Positioned above the catalog panel
  float catalog_panel_width; // Positioned on the right of main scroll view
}

//-----------------------------------------------------------------------------
// Initialization/Termination Functions
- (kdms_window *)initWithManager:(kdms_window_manager *)manager
                      andCursors:(NSCursor **)cursors;
    /* Initializes a newly allocated window.  The `cursors' array holds
       pointers to two cursors (normal and dragging), which are shared
       by all windows. */
- (void)dealloc; // Removes us from the `controller's list
- (void)open_file:(NSString *)filename;
- (void)open_url:(NSString *)url;
- (void)note_jpip_client_reconnecting:(kdu_client *)client;
  /* Called from `kdms_window_manager::reconnect_jpip_client' if the
     client is found to be disconnected, after `kdu_client::reconnect' has
     been invoked.  The function is invoked on every window except the one
     that originated the call.  Each `renderer' that is using `client'
     should call back into `kdms_window_manager::reconnect_jpip_client' to
     get assigned a new queue-id. */
- (bool)application_can_terminate;
     // Used by the window manager to see if the application should be allowed
     // to quit at this point in time -- typically, if there are unsaved
     // edits, the implementation will interrogate the user in order to
     // decide.
- (void)application_terminating;
- (void)close;

//-----------------------------------------------------------------------------
// Functions used to manage scroller fixing
- (bool)check_scrollers_fixed;
  /* Returns true if the scrollers currently have fixed locations -- only
     happens during metadata-driven animations.  If this is the case,
     view dragging and key-based manipulation operations should be skipped.
     This is all handled from within the `kdms_document_view' object, where
     such user interaction events are captured and handled. */
- (void)fixScrollerPos:(NSPoint)pos andSize:(NSSize)size;
- (void)releaseScrollers:(BOOL)main_thread;
- (void)applyScrollerFixing;
  /* These three functions are used together to temporarily fix the horizontal
     and vertical scroll bar locations and dimensions and to subsequently
     release them.  When the scrollers are fixed, no action messages are
     sent to the `scroll_view' and user interaction via the mouse is disabled;
     however, attempts by the `scroll_view' to modify the scrollers are
     recorded internally within the `kdms_scroller' objects themselves so
     that these values can be restored when the scrollers are released back
     to user and scroll_view control.
        The `fixScrollerPos:andSize' method is invoked only from the
     presentation thread during an active animation.  It temporarily locks
     the `scroller_mutex' and sets up the relevant parameters, after which
     `applyScrollerFixing' is passed to `performSelectorOnMainThread' and the
     `scroller_mutex' is unlocked -- these operations are all skipped if the
     call would have no effect.
        The `releaseScrollers' function is usually invoked by the presentation
     thread also, when an animation frame is about to be displayed that does
     not require fixing of the scroll bar states.  The function is called
     directly from the main thread (with `main_thread'=true) only after
     animation has been stopped (see `kdms_renderer::stop_animation').  The
     function does nothing if `scrollers_fixed' is found to be false on entry.
     Otherwise, if called from the presentation thread, the function locks
     the `scroller_mutex', resets the `scrollers_fixed' member, and then
     passes `applyScrollerFixing' to `performSelectorOnMainThread'.  If
     called from the main thread, this function updates the scrollers
     directly, rather than via `applyScrollerFixing'; moreover, in this case,
     there is no need to lock the mutex. */
//-----------------------------------------------------------------------------
// Functions used to route information to the renderer
- (bool)do_processing;
- (bool)display_notification;
- (bool)client_notification;
  /* The above three functions are all invoked, as needed, from the
     `kdms_controller' object when the run-loop is about to become idle.
     They may also be invoked from other points in the run-loop message
     queue if a call to `kdms_notification_manager::process_notifications'
     was scheduled in response to a display update notification, a JPIP
     cache update notification or a "processing-needed" notification.
        Each function returns true if and only if a subsequent call to
     `do_processing' may be needed. */
- (void)adjust_playclock:(double)delta;
  /* Calls `renderer->adjust_playclock'. */
- (void)wakeupScheduledFor:(CFAbsoluteTime)scheduled_wakeup_time
        occurredAt:(CFAbsoluteTime)current_time;
  /* Invokes `kdms_renderer::wakeup'. */
- (void)cancel_pending_show;
  /* See `kdms_renderer::cancel_pending_show'. */
//-----------------------------------------------------------------------------
// Functions used to adjust for animation
- (kdms_glview *) create_glview; // Called when animation starts
- (void) release_glview; // Called when animation completes
//-----------------------------------------------------------------------------
// Utility Functions for managed objects
- (void)set_min_max_size_for_scroll_view:(NSSize)max_scroll_view_size;
    // Used whenever the fixed/maximum size of any of the views in the
    // window changes to modify the minSize and maxSize attributes of
    // the window's frame.  The function must be called when adding/removing
    // tool views around the document view and also when the maximum
    // size for the document view changes.
- (void)set_progress_bar:(double)value;
    // If a progress bar does not already exist, one is created, which may
    // cause some resizing of the document view.  In any event, the new
    // or existing progress bar's progress value is set to `value', which
    // is expected to lie in the range 0 to 100.
- (void)remove_progress_bar;
    // Make sure there is no progress bar being displayed.
- (kdms_animation_bar *)get_animation_bar:(bool)allow_positioning;
    // If an animation bar is not already present, one is created, which
    // may cause some resizing of the document view and (if present)
    // the metadata catalog.  Activity within the animation bar
    // may invoke functions within the `renderer'.
- (void)remove_animation_bar;
    // Make sure there is no animation bar being displayed.
- (void)reset_animation_bar;
    // This function invokes `kdms_animation_bar::reset' to reset
    // internal state information associated with the animation
    // bar that is normally preserved between calls to
    // `remove_animation_bar' and `get_animation_bar'.  We provide
    // this function so that `kdms_renderer::close_file' can
    // remove settings that might cause confusion if they
    // persist when a new data source is opened within the window.
    // The function should be called after removing any open
    // animation bar; even if the animation bar is not currently
    // open, this function should be called because otherwise
    // persistent state information set up when the animation bar
    // was last open may survive.
- (void)create_metadata_catalog;
    // Called from the main menu or from within `kdms_renderer' to create
    // the `catalog_panel' and associated objects to manage a metadata
    // catalog.  Also invokes `renderer->set_metadata_catalog_source'.
- (void)remove_metadata_catalog;
    // Called from the main menu or from within `kdms_renderer' to remove
    // the `catalog_panel' and associated objects.  Also invokes
    // `renderer->set_metadata_catalog_source' with a NULL argument.
- (void)set_status_strings:(const char **)three_strings;
    // Takes an array of three character strings, whose contents
    // are written to the left, centre and right status bar panels.
    // Use NULL or an empty string for a panel you want to leave empty
- (float)get_bottom_margin_height; // Return height of status+animation bar
- (float)get_right_margin_width; // Return width of any active catalog panel.
- (void) set_drag_cursor:(BOOL)is_dragging;
    // If yes, a cursor is shown to indicate dragging of the view around.

//-----------------------------------------------------------------------------
// Internal helper functions
- (void) changed_bottom_margin_height:(float)old_height;
- (void) changed_right_margin_width:(float)old_width;
  // These functions are called when tools are added/removed from the
  // lower (1st function) and right (second function) boundaries of the
  // document view.  Before calling the functions, auto-resizing should
  // be disabled; upon return it should be re-enabled.

//-----------------------------------------------------------------------------
// Menu Functions
- (IBAction) menuFileOpen:(NSMenuItem *)sender;
- (IBAction) menuFileOpenUrl:(NSMenuItem *)sender;
- (IBAction) menuFileDisconnectURL:(NSMenuItem *)sender;
- (IBAction) menuFileReconnectURL:(NSMenuItem *)sender;
- (IBAction) menuFileEraseAndReconnectURL:(NSMenuItem *)sender;
- (IBAction) menuFileJPIPSaveMode:(NSMenuItem *)sender;
- (IBAction) menuFileJPIPUnsavedMode:(NSMenuItem *)sender;
- (IBAction) menuFileSave:(NSMenuItem *)sender;
- (IBAction) menuFileSaveAs:(NSMenuItem *)sender;
- (IBAction) menuFileSaveAsLinked:(NSMenuItem *)sender;
- (IBAction) menuFileSaveAsEmbedded:(NSMenuItem *)sender;
- (IBAction) menuFileSaveReload:(NSMenuItem *)sender;
- (IBAction) menuFileProperties:(NSMenuItem *)sender;

- (IBAction) menuViewHflip:(NSMenuItem *)sender;
- (IBAction) menuViewVflip:(NSMenuItem *)sender;
- (IBAction) menuViewRotate:(NSMenuItem *)sender;
- (IBAction) menuViewCounterRotate:(NSMenuItem *)sender;
- (IBAction) menuViewZoomIn:(NSMenuItem *)sender;
- (IBAction) menuViewZoomOut:(NSMenuItem *)sender;
- (IBAction) menuViewZoomInSlightly:(NSMenuItem *)sender;
- (IBAction) menuViewZoomOutSlightly:(NSMenuItem *)sender;
- (IBAction) menuViewWiden:(NSMenuItem *)sender;
- (IBAction) menuViewShrink:(NSMenuItem *)sender;
- (IBAction) menuViewRestore:(NSMenuItem *)sender;
- (IBAction) menuViewRefresh:(NSMenuItem *)sender;
- (IBAction) menuViewLayersLess:(NSMenuItem *)sender;
- (IBAction) menuViewLayersMore:(NSMenuItem *)sender;
- (IBAction) menuViewOptimizeScale:(NSMenuItem *)sender;
- (IBAction) menuViewPixelScaleX1:(NSMenuItem *)sender;
- (IBAction) menuViewPixelScaleX2:(NSMenuItem *)sender;

- (IBAction) menuFocusOff:(NSMenuItem *)sender;
- (IBAction) menuFocusHighlighting:(NSMenuItem *)sender;
- (IBAction) menuFocusWiden:(NSMenuItem *)sender;
- (IBAction) menuFocusShrink:(NSMenuItem *)sender;
- (IBAction) menuFocusLeft:(NSMenuItem *)sender;
- (IBAction) menuFocusRight:(NSMenuItem *)sender;
- (IBAction) menuFocusUp:(NSMenuItem *)sender;
- (IBAction) menuFocusDown:(NSMenuItem *)sender;

- (IBAction) menuModeFast:(NSMenuItem *)sender;
- (IBAction) menuModeFussy:(NSMenuItem *)sender;
- (IBAction) menuModeResilient:(NSMenuItem *)sender;
- (IBAction) menuModeResilientSOP:(NSMenuItem *)sender;
- (IBAction) menuModeSingleThreaded:(NSMenuItem *)sender;
- (IBAction) menuModeMultiThreaded:(NSMenuItem *)sender;
- (IBAction) menuModeMoreThreads:(NSMenuItem *)sender;
- (IBAction) menuModeLessThreads:(NSMenuItem *)sender;
- (IBAction) menuModeRecommendedThreads:(NSMenuItem *)sender;

- (IBAction) menuNavComponent1:(NSMenuItem *)sender;
- (IBAction) menuNavMultiComponent:(NSMenuItem *)sender;
- (IBAction) menuNavComponentNext:(NSMenuItem *)sender;
- (IBAction) menuNavComponentPrev:(NSMenuItem *)sender;
- (IBAction) menuNavImageNext:(NSMenuItem *)sender;
- (IBAction) menuNavImagePrev:(NSMenuItem *)sender;
- (IBAction) menuNavCompositingLayer:(NSMenuItem *)sender;
- (IBAction) menuNavTrackNext:(NSMenuItem *)sender;

- (IBAction) menuMetadataCatalog:(NSMenuItem *)sender;
- (IBAction) menuMetadataSwapFocus:(NSMenuItem *)sender;
- (IBAction) menuMetadataShow:(NSMenuItem *)sender;
- (IBAction) menuMetadataAdd:(NSMenuItem *)sender;
- (IBAction) menuMetadataEdit:(NSMenuItem *)sender;
- (IBAction) menuMetadataDelete:(NSMenuItem *)sender;
- (IBAction) menuMetadataCopyLabel:(NSMenuItem *)sender;
- (IBAction) menuMetadataCopyLink:(NSMenuItem *)sender;
- (IBAction) menuMetadataCut:(NSMenuItem *)sender;
- (IBAction) menuMetadataPasteNew:(NSMenuItem *)sender;
- (IBAction) menuMetadataDuplicate:(NSMenuItem *)sender;
- (IBAction) menuMetadataUndo:(NSMenuItem *)sender;
- (IBAction) menuMetadataRedo:(NSMenuItem *)sender;
- (IBAction) menuOverlayEnable:(NSMenuItem *)sender;
- (IBAction) menuOverlayFlashing:(NSMenuItem *)sender;
- (IBAction) menuOverlayAutoFlash:(NSMenuItem *)sender;
- (IBAction) menuOverlayToggle:(NSMenuItem *)sender;
- (IBAction) menuOverlayRestrict:(NSMenuItem *)sender;
- (IBAction) menuOverlayLighter:(NSMenuItem *)sender;
- (IBAction) menuOverlayHeavier:(NSMenuItem *)sender;
- (IBAction) menuOverlayDoubleSize:(NSMenuItem *)sender;
- (IBAction) menuOverlayHalveSize:(NSMenuItem *)sender;

- (IBAction) menuPlayStartForward:(NSMenuItem *)sender;
- (IBAction) menuPlayStartBackward:(NSMenuItem *)sender;
- (IBAction) menuPlayStop:(NSMenuItem *)sender;
- (IBAction) menuPlayRenderAll:(NSMenuItem *)sender;
- (IBAction) menuPlayRepeat:(NSMenuItem *)sender;
- (IBAction) menuPlayNative:(NSMenuItem *)sender;
- (IBAction) menuPlayCustom:(NSMenuItem *)sender;
- (IBAction) menuPlayFrameRateUp:(NSMenuItem *)sender;
- (IBAction) menuPlayFrameRateDown:(NSMenuItem *)sender;

- (IBAction) menuStatusToggle:(NSMenuItem *)sender;

- (IBAction) menuWindowDuplicate:(NSMenuItem *)sender;
- (IBAction) menuWindowNext:(NSMenuItem *)sender;
- (bool) can_do_WindowNext;

- (BOOL) validateMenuItem:(NSMenuItem *)menuitem;

//-----------------------------------------------------------------------------
// Overrides
- (void) resignKeyWindow;
- (void) becomeKeyWindow;

//-----------------------------------------------------------------------------
// Notification Functions
- (void) scroll_view_BoundsDidChange:(NSNotification *)notification;
    /* Set up to be called if either bounds or frame rectangles changed. */

@end // kdms_window

/*****************************************************************************/
/*                            External Functions                             */
/*****************************************************************************/

extern int interrogate_user(const char *message, const char *option_0,
                            const char *option_1="", const char *option_2="",
                            const char *option_3="");
  /* Presents a warning message to the user with up to three options, returning
   the index of the selected option.  If `option_1' is an empty string, only
   the first option is presented to the user.  If `option_2' is emty, at most
   two options are presented to the user; and so on. */
