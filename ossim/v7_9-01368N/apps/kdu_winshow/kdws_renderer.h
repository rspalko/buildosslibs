/*****************************************************************************/
// File: kdws_renderer.h [scope = APPS/WINSHOW]
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
  Defines the main image management and rendering object associated with
the image window.  The `kdws_renderer' object is created by (and conceptually
owned by) `kdws_frame_window'.  Menu commands and other user events received
by those objects are mostly forwarded to the renderer.
******************************************************************************/
#ifndef KDWS_RENDERER_H
#define KDWS_RENDERER_H

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
#include "kdu_file_io.h"
#include "jpx.h"
#include "kdu_client.h"
#include "kdu_clientx.h"
#include "kdu_region_compositor.h"
#include "kdu_region_animator.h"

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_winshow" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace

// Declared here:
class kdws_file;
struct kdws_file_editor;
class kdws_file_services;
struct kdws_orientation;
struct kdws_display_geometry;
class kdws_compositor_buf;
class kdws_region_compositor;
struct kdws_rel_dims;
struct kdws_rel_focus;
class kdws_renderer;

// Declared elsewhere
class kdws_string;
class kdws_manager;
class kdws_frame_presenter;
class kdws_frame_window;
class kdws_image_view;
class kdws_animation_bar;
class kdws_properties;
class kdws_url_dialog;
class kdws_metashow;
class kdws_metadata_editor;
class kdws_catalog;

#define KDWS_OVERLAY_BORDER 4
#define KDWS_NUM_OVERLAY_PARAMS 18 // Size of the array initializer below
#define KDWS_OVERLAY_PARAMS \
   {4, 0x20FFFF00, 0x600000FF, 0xA00000FF, 0xA0FFFF00, 0x60FFFF00, 10, \
    2, 0x200000FF, 0x600000FF, 0x80FFFF00, 6, \
    1, 0x300000FF, 0x60FFFF00, 3, \
    0, 0x400000FF }
  // Above macro defines initializers for the `overlay_params' array,
  // passed to `kdu_region_compositor::configure_overlays'.  The meaning of
  // the parameters is provided along with the definition of this function.
  // Briefly, though, each row above defines a border size B, an interior
  // ARGB colour, B ARGB border colours running from the interior to exterior
  // edge of the border, and a minimum "width" for the overall region defined
  // by the ROI description box which is being painted.  If the minimum
  // width is not met, the overlay painting logic falls through to the next
  // row of parameters, which generally (but not necessarily) involves a
  // smaller border size. The last line corresponds to the case in which no
  // border is painted at all, so there is only an interior colour.

enum kds_status_id {
    KDS_STATUS_LAYER_RES=0,
    KDS_STATUS_DIMS=1,
    KDS_STATUS_MEM=2,
    KDS_STATUS_CACHE=3,
    KDS_STATUS_PLAYSTATS=4
  };

/*****************************************************************************/
/*                             EXTERNAL FUNCTIONS                            */
/*****************************************************************************/

extern int kdws_utf8_to_unicode(const char *utf8_string,
                                kdu_uint16 unichar_buf[], int max_chars);
  /* This simple function expands a null-terminated UTF-8 string as a
     null-terminated 16-bit unicode string, storing the result in
     `unicode_buf'.  At most `max_chars' are converted, not including the
     terminating null character, so `unichar_buf' must be capable of holding
     at least `max_chars'+1 entries.  The function returns the number of
     unicode characters actually converted. Note that UTF-8 can represents
     character codes which with more than 16 bits.  Where this happens, the
     function simply discards all more significant bits.  There is probably
     a more intelligent and uniform way of handling such cases, but they
     should virtually never arise in practice. */

/*****************************************************************************/
/*                                 kdws_file                                 */
/*****************************************************************************/

class kdws_file {
private: // Private life-cycle functions
  friend class kdws_file_services;
  kdws_file(kdws_file_services *owner); // Links onto tail of `owner' list
  ~kdws_file(); // Unlinks from `owner' first.
public: // Member functions
  int get_id() { return unique_id; }
  /* Gets the unique identifier for this file, which can be used with
   `kdws_file_services::find_file'. */
  void release();
  /* Decrements the retain count.  Once the retain count reaches 0, the
   object is deleted, and the file might also be deleted if it was a
   temporary file. */
  void retain()
    { retain_count++; }
  const char *get_pathname() { return pathname; }
  bool get_temporary() { return is_temporary; }
  bool create_if_necessary(const char *initializer);
  /* Creates the file and sets some initial contents based on the supplied
   `initializer' string, if any.  Returns false if the file cannot be
   created, or it already exists.  If the `initializer' string is NULL,
   any file created here is opened as a binary file; otherwise, it is
   created to hold text. */
private: // Data
  int retain_count;
  int unique_id;
  kdws_file_services *owner;
  kdws_file *next, *prev; // Used to build a doubly-linked list
  char *pathname;
  bool is_temporary; // If true, the file is deleted once `retain_count'=0.
};

/*****************************************************************************/
/*                             kdws_file_editor                              */
/*****************************************************************************/

struct kdws_file_editor {
  kdws_file_editor()
    {
      doc_suffix = app_pathname = app_name = NULL;
      next = NULL;
    }
  ~kdws_file_editor()
    {
      if (doc_suffix != NULL) delete[] doc_suffix;
      if (app_pathname != NULL) delete[] app_pathname;
      if (app_name != NULL) delete[] app_name;
    }
  char *doc_suffix; // File extension for matching document types
  char *app_pathname; // Either the application pathname or else the first
                      // part of a command to run the application.
  char *app_name; // Extracted from `app_pathname'.
  kdws_file_editor *next;
};

/*****************************************************************************/
/*                            kdws_file_services                             */
/*****************************************************************************/

class kdws_file_services {
public: // Member functions
  kdws_file_services(const char *source_pathname);
    /* If `source_pathname' is non-NULL, all temporary files are created by
     removing its file suffix, if any, and then appending to the name to
     make it unique.  This is the way you should create the object when
     editing an existing file.  Otherwise, if `source_pathname' is NULL,
     a base pathname is created internally using the `tmpnam' function. */
  ~kdws_file_services();
  bool find_new_base_pathname();
    /* This function sets the base pathname for temporary files based upon
     the ANSI tmpnam() function.  It erases any existing base pathname and
     generates a new one which is derived from the name returned by tmpnam()
     after first testing that the file returned by tmpnam() can be created.
     If the file cannot be created, the function returns false.  The function
     is normally called only internally from within `confirm_base_pathname'. */
  kdws_file *retain_tmp_file(const char *suffix);
    /* Generates a unique filename.  Does not try to create the file at this
     point, but may do so later.  In any case, the file will be deleted once
     it is no longer needed.  The returned object has a retain count of 1.
     Once you release the object, it will be destroyed, and the temporary
     file deleted.  */
  kdws_file *retain_known_file(const char *pathname);
    /* As for the last function, but the returned object represents a
     non-temporary file (i.e., one that will not be deleted when it is no
     longer required).  Generally, this is an existing file supplied by the
     user.  You should supply the full pathname to the file.  The returned
     object has a retain count of at least 1.  If the same file is already
     retained for some other purpose, the retain count may be greater. */
  kdws_file *find_file(int identity);
    /* This function is used to recover a file whose ID was obtained
     previously using `kdws_file::get_id'.  It provides a robust mechanism
     for accessing files whose identity is stored indirectly via an integer
     and managed by the `jpx_meta_manager' interface. */
  kdws_file_editor *get_editor_for_doc_type(const char *doc_suffix, int which);
    /* This function is used to access an internal list of editors which
     can be used for files (documents) with the indicated suffix (extension).
     The function returns NULL if `which' is greater than or equal to the
     number of matching editors for such files which are known.  If the
     function has no stored editors for the file, it attempts to find some,
     using operating-system services.  You can also add editors using the
     `add_editor_for_doc_type' function. */
  kdws_file_editor *add_editor_for_doc_type(const char *doc_suffix,
                                            const char *app_pathname);
    /* Use this function if you want to add custom editors for files
     (documents) with a given suffix (extension), or if you want to move an
     existing editor to the head of the list. */
private:
  void add_editor_progid_for_doc_type(const char *doc_suffix,
                                      kdws_string &progid);
    /* Used internally to add editors based on their ProgID's, as discovered
       by scanning the registry under HKEY_CLASSES_ROOT/.<suffix>. */
  void confirm_base_pathname(); // Called when `retain_tmp_file' is first used
private: // Data
  friend class kdws_file;
  kdws_file *head, *tail; // Linked list of files
  int next_unique_id;
  char *base_pathname; // For temporary files
  bool base_pathname_confirmed; // True once we know we can write to above
  kdws_file_editor *editors; // Head of the list; new editors go to the head
};

/*****************************************************************************/
/*                            kdws_orientation                               */
/*****************************************************************************/

struct kdws_orientation {
  public: // Member functions
    kdws_orientation() { reset(); }
    void reset() { trans = vf = hf = false; }
    bool is_original() { return !(trans || vf || hf); }
    bool operator==(const kdws_orientation &rhs) const
      { return (trans==rhs.trans) && (vf==rhs.vf) && (hf==rhs.hf); }
    bool operator!=(const kdws_orientation &rhs) const
      { return (trans!=rhs.trans) || (vf!=rhs.vf) || (hf!=rhs.hf); }
    void transpose() { trans=!trans; bool tmp=vf; vf=hf; hf=tmp; }
    void vflip() { vf = !vf; }
    void hflip() { hf = !hf; }
  public: // Data
    bool trans; // Transpose
    bool vf; // Vertical flip
    bool hf; // Horizontal flip
  };
  /* Notes:
        These parameters have the same interpretation as in
     `kdu_codestream::change_appearance' and in all other object interface
     functions that accept `transpose', `vflip' and `hflip' arguments.
     If `trans' is present, it is considered that the native image
     geometry is first transposed, so that rows of the original image
     become columns of the rendered image, and then flipped as necessary. */

/*****************************************************************************/
/*                          kdws_display_geometry                            */
/*****************************************************************************/

struct kdws_display_geometry {
  public: // Member functions
    kdws_display_geometry() { focus_state = 0; pixel_scale = 1.0; }
    bool operator==(const kdws_display_geometry &rhs) const 
      { 
        return ((orientation == rhs.orientation) &&
                (image_dims == rhs.image_dims) &&
                (viewport_dims == rhs.viewport_dims) &&
                (pixel_scale == rhs.pixel_scale) &&
                (focus_state == rhs.focus_state) &&
                ((!focus_state) || (focus_box == rhs.focus_box)));
      }
    bool operator!=(const kdws_display_geometry &rhs) const
      { return !((*this) == rhs); }
  public: // Data
    kdws_orientation orientation;
    double pixel_scale;
    kdu_dims image_dims;
    kdu_dims viewport_dims;
    kdu_dims focus_box;
    int focus_state;
  };
  /* Notes:
        This structure is used to store key geometric parameters associated
     with the imagery that is supposed to be presented to the display.  The
     main purpose is to allow atomic exchange of this information between the
     main window management thread and the `kdws_renderer::paint_region'
     function, that may be invoked from a separate display thread during
     animation.  The information is recorded within the
     `kdws_region_compositor' object.
        The `image_dims' member holds the same information returned by
     `kdu_region_compositor::get_total_image_dims'.  The `viewport_dims'
     member holds the dimensions and location of a region, contained within
     `image_dims' that is supposed to be displayed within an image view
     window.
        The `orientation' member records the geometric orientation
     parameters (passed to `kdu_region_compositor::set_scale') that affect
     the interpretation of the `image_dims' and `viewport_dims' members.
     For example, if transposition is used, this is recorded in the
     `orientation.trans' member, but `image_dims' and `viewport_dims'
     also reflect the prevailing transposition.
        The `pixel_scale' member holds the scaling factor that is supposed
     to be applied during the rendering of the `viewport_dims' region to
     the display.
        The `focus_box' and `focus_state' parameters record any user-defined
     focus box that may need to be drawn over the image itself.  If
     `focus_state' is 0, there is no focus box.  Other values of `focus_state'
     might affect the way a focus box is rendered. */

/*****************************************************************************/
/*                         kdws_compositor_buf                               */
/*****************************************************************************/

class kdws_compositor_buf : public kdu_compositor_buf {
  public: // Member functions and overrides
    kdws_compositor_buf()
      { bitmap=NULL; next=NULL; reset_state(); }
    virtual ~kdws_compositor_buf()
      {
        if (bitmap != NULL) DeleteObject(bitmap);
        buf = NULL; // Remember to do this whenever overriding the base class
      }
    void reset_state()
      { 
        orig_frame_start = orig_frame_end = -1.0;
        metamation_frame_id = -1;  display_time = -1.0;
        have_image_dims=false;  metanode=jpx_metanode();
        roi_pos = -1.0;  roi_info.init();  jpip_progress = 0.0;
      }
      /* Resets all state information set by the following functions. */
    void set_image_dims(kdu_dims &im_dims, kdws_orientation &im_orientation)
      { have_image_dims=true; this->image_dims = im_dims;
        this->image_orientation = im_orientation; }
      /* Use this function to record the image dimensions (as returned by
         `kdu_region_compositor::get_total_composition_dims'), as well
         as the prevailing orientation, as soon as a working buffer is
         obtained form `kdu_region_compositor'. */
    void set_orig_frame_times(double start, double end)
      { this->orig_frame_start = start; this->orig_frame_end = end; }
      /* This function need only be called if we have meaningful information
         about a frame's nominal start/end times, measured in seconds.  If
         the original data source provides timing information, the value
         of `start' (resp. `end') should be the number of ellapsed seconds
         between the start of the relevant animation presentation or video
         track and the point at which this display of this frame should
         nominally start (resp. finish).  The `start' and `end' values are
         properties of the original data source and should not be affected
         in any way by playback speed or direction, whereas display time
         (see `set_display_time') identifies the point at which the frame
         should be displayed, measured relative to the application's
         display event clock. */
    void set_display_time(double disp_time) { this->display_time = disp_time; }
      /* This function need be called only if we are about to push a
         frame onto the composition buffer queue.
            We choose not to use the `custom_stamp' argument of the
         `kdu_region_compositor::push_composition_buffer' function for storing
         time stamps.  Instead, we record the display time explicitly
         (measured in seconds) within this override of `kdu_compositor_buf',
         right before pushing a frame onto the composition buffer queue.
         The `kdws_region_compositor' object provides special functions for
         manipulating the composition buffer that work with the display
         time recorded here.
            Note that the display time of a buffer on the composition buffer
         queue can be changed.  However, this is only done from within one
         of the `kdws_region_compositor' functions that protects the
         composition buffer queue via a mutex. */
    void set_metanode(jpx_metanode node, int frame_id)
      { this->metanode = node; this->metamation_frame_id = frame_id; }
      /* This function need be called only if we are about to push a
         frame onto the composition buffer queue and that frame was
         a metadata-driven animation frame.  The `node' argument should be the
         interface returned by `kdu_region_animator::get_current_metanode';
         the `frame_id' should be the non-negative index returned by
         `kdu_region_animator::get_metadata_driven_frame_id' -- it is not
         a source frame index, but rather an ordinal that identifies the
         animation frame within the sequence generated by the metadata that
         was passed to `kdu_region_animator::add_metanode'. */
    void set_jpip_progress(double progress)
      { this->jpip_progress = progress; }
      /* This function need be called only if we are about to push a frame
         onto the composition buffer queue and that frame was generated by
         rendering from a JPIP client cache.  In this case, the `progress'
         value is supposed to be a measure (from 0.0 to 100.0) of the
         relative quality associated with data available in the cache for
         the rendered frame region (or region of interest). */
    void set_roi_info(const kdu_region_animator_roi &info)
      { 
        if (!info) return;
        roi_info = info; roi_pos = roi_info.get_start_pos();
      }
      /* See `kdu_region_animator_roi' for a description of animated ROI's.
         If `info' is not valid, this function does nothing at all.  Otherwise,
         `info.prepare' is expected to have already been called and the
         current function simply copies `info' internally.
         If there is a dynamic animator ROI installed, you should call
         `advance_animator_roi' after displaying a frame so that you can
         move to the next version of the animated ROI, if there is one.
         This is done automatically within
         `kdws_region_compositor::unlock_and_mark_composition_queue_head'. */
    bool advance_animator_roi(double delay)
      { 
        if ((!roi_info) || (roi_pos >= roi_info.get_end_pos()))
          return false;
        if (delay <= 0.0) return true;
        double rel_time = roi_info.get_time_for_pos(roi_pos);
        roi_pos = roi_info.get_pos_for_time(rel_time+delay);
        if (roi_pos > roi_info.get_end_pos())
          { roi_pos = roi_info.get_end_pos();
            delay = roi_info.get_time_for_pos(roi_pos)-rel_time; }
        display_time += delay;
        return true;
      }
      /* This function updates the `display_time' passed to `set_display_pos'
         and the current ROI position associated with any valid
         `kdu_region_animator_roi' object passed to `set_roi_info'.  If there
         is no animator ROI installed, or we have already reached the
         `kdu_region_animator_roi::max_pos' position, the function returns
         false.  -ve delays do nothing.  If `delay' would carry us past the
         `kdu_region_animator_roi::max_pos' position, the function makes a
         smaller adjustment to the display time, as required. */
    bool get_orig_frame_times(double &start, double &end)
      { 
        start = orig_frame_start;  end = orig_frame_end;
        return (orig_frame_start >= 0);
      }
      /* Note, this function returns false and sets `start' and `end' to -ve
         values if `set_orig_frame_times' was not called. */
    double get_display_time() { return display_time; }
    bool get_image_dims(kdu_dims &dims, kdws_orientation &orientation)
      { if (!have_image_dims) return false;
        dims = image_dims; orientation=image_orientation; return true; }
      /* Sets `dims' and `orientation' to the values passed to
         `set_image_dims' and returns true, unless the `set_image_dims'
         function has not been called since this buffer was created or
         `reset_state' was called. */
    bool get_roi(kdu_dims &roi)
      { return (roi_pos >= 0.0) && roi_info.get_roi_for_pos(roi_pos,roi); }
      /* Computes the current region of interest, from the information passed
         to `set_roi_info' and the current display position.  Returns false
         if there is no region of interest information for the frame. */
    bool is_incomplete_dynamic_roi()
      { return (roi_info.valid() && (roi_info.get_end_pos() < 1.0)); }
      /* Returns true if the buffer represents a dynamic ROI but does not
         extend all the way to the end of the ROI's dynamic transition.  This
         means that an additional buffer (at least) is required to complete
         the ROI transition.  The renderer's `manage_animation_frame_queue'
         function uses this information to avoid introducing clock
         adjustments in the midst of a dynamic ROI, since this could prove
         visually annoying. */
    jpx_metanode get_metanode(int &frame_id)
      { frame_id=this->metamation_frame_id; return this->metanode; }
      /* Returns the metadata node, if any, passed to `set_metanode', along
         with the metadata-driven frame-id -- remember, this is not a
         source frame index, but a non-negative enumerator identifying
         the animation frame within the sequence generated by the
         metadata passed to the `kdu_region_animator::add_metanode'
         function. */
    double get_jpip_progress() { return jpip_progress; }
      /* Returns the value passed to `set_jpip_progress', or 0.0 if none was
         ever supplied. */
    HBITMAP access_bitmap(kdu_uint32 * &buf, int &row_gap)
      { buf=this->buf; row_gap=this->row_gap; return bitmap; }
    void flush_from_cache();
      /* This function flushes all cache-lines associated with the buffer
         to external memory in preparation for block transfer to the
         display.  This turns out to resolve some problems with the Windows
         BitBlt and StretchBlt operations during video display, in which
         occasional scanlines can contain quite different data to their
         surrounding ones.  The reason for this is not entirely clear, but
         my best guess is that when some of the imagery that needs to be
         blitted is in main memory and some resides within the CPU cache, the
         transfer to graphics memory is performed out of order; this can
         interact badly with the screen tearing associated with transferring
         new data to graphics memory in a manner which is not synchronized
         with the vertical frame blanking period.  Later, we will migrate
         the kdu_show application to use a tearing-free transfer technique
         for video rendering, but for the moment this simple function resolves
         the most annoying artefacts associated with screen tearing. */
  private: // Helper functions
    void create_bitmap(kdu_coords size);
      /* If this function finds that a bitmap buffer is already available,
         it returns without doing anything. */
  protected: // Additional data
    friend class kdws_region_compositor;
    HBITMAP bitmap; // NULL if not using a bitmap
    BITMAPINFO bitmap_info; // Description for `bitmap' if non-NULL
    kdu_coords size;
    double orig_frame_start; // -ve if no original frame times not set
    double orig_frame_end;
    double display_time; // -ve if no display time yet set
    int metamation_frame_id; // -ve if not a metadata-driven animation frame
    bool have_image_dims; // True once `set_image_dims' is called
    kdu_dims image_dims;
    kdws_orientation image_orientation;
    kdu_region_animator_roi roi_info;
    double roi_pos; // Current position for dynamic ROI
    jpx_metanode metanode;
    double jpip_progress; // Stores JPIP progress bar value; 0.0 if no JPIP
    kdws_compositor_buf *next;
  };

/*****************************************************************************/
/*                          kdws_region_compositor                           */
/*****************************************************************************/

class kdws_region_compositor : public kdu_region_compositor {
  /* This class augments the platorm independent `kdu_region_compositor'
   class with display buffer management logic which allocates system
   bitmaps.
      The class also provides synchronized functions to manage interaction
   with the composition buffer queue.  These functions both protect access
   to the composition queue with an appropriate mutex lock and provide added
   functionality to the primitive queue management functions.  In particular,
   they allow the application to manage jitter absorbtion buffers with
   as few as 2 buffers in total (one queued buffer and one working buffer)
   with minimal blocking between the presentation and main working
   threads.  You should only use the protected functions provided here,
   rather than directly invoking `push_composition_buffer',
   `pop_composition_buffer' or `inspect_composition_queue'.
      Finally, the class provides methods for managing the current display
   geometry, which simplifies the buffer painting workflow.  Not only is
   the display geometry (including focus box information) protected by a mutex
   so that it can be robustly accessed by a separate drawing thread, but the
   object also ensures that the head of the composition buffer queue (if any)
   is treated as "dirty" if any of this information changes, so that calls to
   `lock_new_composition_queue_head' will see a need to present the
   most relevant queued composition buffer, even if it is already marked
   as having been presented.
      You can take this class as a template for designing richer
   buffer management systems -- e.g. ones which might utilize display
   hardware explicitly. */
  public: // Member functions
    kdws_region_compositor(kdu_thread_env *env=NULL,
                           kdu_thread_queue *env_queue=NULL)
                           : kdu_region_compositor(env,env_queue)
      { 
        active_list = free_list = NULL;
        queue_head_presented=queue_tail_presented=queue_head_locked = false;
        have_presented_queue_head_info = false;
        presented_queue_head_frame_idx = -1;
        presented_queue_head_metamation_frame_id = -1;
        presented_queue_head_frame_start = -1.0;
        presented_queue_head_frame_end = -1.0;
        presented_queue_head_jpip_progress = -1.0;
        presented_queue_head_display_time = -1.0;
        last_display_event_time = -1.0;
        next_display_event_time = -1.0;
        next_display_time_threshold = -1.0;
        need_display_redraw = false;
        focussing_active = false;
        mutex.create();
      }
    ~kdws_region_compositor()
      {
        pre_destroy(); // Destroy all buffers before base destructor called
        assert(active_list == NULL);
        while ((active_list=free_list) != NULL)
          { free_list=active_list->next; delete active_list; }
        mutex.destroy();
      }
  //---------------------------------------------------------------------------
  public: // Buffer allocation and management
    virtual kdu_compositor_buf *
      allocate_buffer(kdu_coords min_size, kdu_coords &actual_size,
                      bool read_access_required);
    virtual void delete_buffer(kdu_compositor_buf *buffer);
  //---------------------------------------------------------------------------
  public: // Display geometry and focus box management
    void force_display_redraw() { need_display_redraw = true; }
    void record_viewport(kdws_orientation orientation,
                         double pixel_scale, kdu_dims view_dims);
      /* This function updates the internal display geometry information,
         so that a consistent record can be returned via
         `get_display_geometry'.  The update is performed atomically
         (protected by a critical section).  The
         `kdws_display_geometry::image_dims' attribute is recovered from the
         base object; the `kdws_display_geometry::viewport_dims' and
         `kdws_display_geometry::orientation' values are derived from the
         supplied arguments.  Any focus box information recorded in
         `kdws_display_geometry' is also adjusted, if necessary, to ensure
         that the focus box lies within the viewport.  If the function
         causes anything to change, it sets an internal flag to indicate that
         the head of any composition queue needs to be regarded as "dirty"
         so that the next call to `lock_new_composition_queue_head' can
         succeed. */
    bool set_focus_box(kdu_dims &focus_box, int focus_state);
      /* This function modifies the focus box information associated with the
         internal display geometry record, as returned by
         `get_display_geometry'.  The update is performed atomically
         (protected by a critical section).  If the function causes anything
         to change, the function returns true, sets an internal flag to
         indicate that the head of any composition queue needs to be regarded
         as "dirty", and also returns any previously recorded focus box via
         the `focus_box' argument.  If nothing is changed, the function
         returns false. */
    void get_display_geometry(kdws_display_geometry &geometry)
      { mutex.lock(); geometry = this->display_geometry; mutex.unlock(); }
      /* Returns all the information recorded by `record_viewport' and
         `set_focus_box'.  It is guaranteed that the entire `geometry'
         record will be internally consistent, so it can be used in
         displaying imagery to the screen, from any thread. */
    void set_focussing_rect(kdu_dims new_rect, bool active)
      { 
        mutex.lock();
        if ((active != focussing_active) ||
            (active && (new_rect != focussing_rect)))
          need_display_redraw = true;
        this->focussing_active = active;  this->focussing_rect = new_rect;
        mutex.unlock();
      }
      /* Records a focussing rectangle that is in the process of being
         defined by an interactive user.  If `active' is false, there
         is no focussing box. */
    bool get_focussing_rect(kdu_dims &rect)
      { 
        mutex.lock();
        bool active = this->focussing_active;   rect = this->focussing_rect;
        mutex.unlock();  return active;
      }
      /* Same as `get_focus_box' but for the region configured by
         `set_focussing_rect'. */
  //---------------------------------------------------------------------------
  public: // Display time clock management for animation
    void init_display_event_times(double last_event_time,
                                  double next_event_time)
      { 
        mutex.lock();
        this->last_display_event_time = last_event_time;
        this->next_display_event_time = next_event_time;
        mutex.unlock();
      }
      /* Before using this object for animation, you should call this function.
         Generally, it can be called at any time, but it should be sufficient
         to call the function immediately prior to starting an animation.  The
         call ensures that `get_display_event_times' will return meaningful
         values even before `lock_new_composition_queue_head' is first
         invoked from the presentation thread.  The function uses the
         supplied display clock event times to initialize both the
         last and next display event times, as recorded internally
         and returned via `get_display_event_times'.  It is important that
         these two times be separated by the typical display event period;
         if none is known, the two times should be close together or
         perhaps identical.
            It is expected that all valid display event times will be
         non-negative, since -ve values are used internally to identify
         quantities that are not yet known.   It is possible to call this
         function with a negative argument if you wish to cancel any
         internal record of display event times. */
    double get_display_event_times(double &assumed_next_display_event_time)
      { 
        mutex.lock();
        double result = last_display_event_time;
        assumed_next_display_event_time = this->next_display_event_time;
        mutex.unlock();
        return result;
      }
      /* The application thread uses this function to retrieve the
         most up-to-date information about the display clock.  The
         function returns the object's internal record of the last display
         event time (either the value passed to `init_display_event_times'
         or the value most recently passed as the first argument to
         `lock_new_composition_queue_head').
            The function also sets `assumed_next_display_event_time' to the
         object's internal record of the next display event time, as passed to
         `init_display_event_times' or `lock_new_composition_queue_head'.
         If the latter function has been called at all, this value will
         generally be larger than the last display event time that forms the
         function's return value.
            A negative return value should generally be interpreted as
         meaning that the display times have not yet been initialized for
         some reason. */
  //---------------------------------------------------------------------------
  public: // Composition buffer and queue management for animation
    bool lock_new_composition_queue_head(double display_event_time,
                                         double next_display_event_time);
      /* Pops buffers from the queue as necessary to find the most recent one
         whose display time is no larger than a "display time threshold".  The
         present function takes this threshold as T = `display_event_time' +
         0.1*(`next_display_event_time'-`display_event_time'), so as to
         avoid any jitter due to numerical round-off errors in the event
         that the display events are separated by exactly the same amount
         as the underlying source frame display times.
            The function does not assume that display times are monotonic
         within the queue, so it is possible for the processing thread to
         push older frames onto the queue to override existing ones.  In
         this case, the policy mentioned above, will skip through the
         initial frames and settle on the most recently pushed frame if
         it has a display time that is within the threshold.
            If no appropriate buffer is found, or the most appropriate
         buffer has already been marked as presented (see
         `unlock_and_mark_composition_queue_head') and there is no need
         to redraw (see `set_display_geometry'), the function returns false.
         Otherwise, we lock the head of the composition queue so it won't
         be popped by `conditionally_push_composition_buffer'.
            The function also calls `kdws_compositor_buf::advance_animator_roi'
         to advance any ROI transition machinery to `display_event_time';
         that function does nothing unless the working composition buffer
         has had its `kdws_compositor_buf::set_roi_info' function called.
            After this function returns true, the caller can proceed to use
         `get_composition_buffer' to retrieve the locked queue head and
         present it to the screen.  `unlock_and_mark_composition_queue_head'
         should then be called to mark the queue head as having been
         presented and also unlock it so that calls to
         `conditionally_push_composition_buffer' can reuse its storage, if
         required.
            The `display_event_time' and `next_display_event_time' values
         are recorded internally and may later be recovered from any thread
         via the `get_display_event_times' function.
      */
    void unlock_and_mark_composition_queue_head();
      /* This function removes any lock taken out by the
         `lock_new_composition_queue_head' function and also marks the head of
         the composition queue as having been presented.  This prevents
         `lock_new_composition_queue_head' from returning true again until
         either a new composition buffer needs to be displayed or the focus
         box changes (see `set_focus_box').
            If the head of the composition queue involves a dynamic region
         of interest (see `kdws_compositor_buf::set_roi_info'), this function
         invokes `kdws_comosition_buf::advance_animator_roi' to adjust
         its display time and display position before releasing the
         lock; if the latter function has any effect, the frame will be left
         unmarked, so that it can be presented again in the future. */
    bool get_presented_queue_head_info(int &frame_idx,
                                       double &frame_start, double &frame_end,
                                       jpx_metanode &animation_metanode,
                                       int &metamation_frame_id,
                                       double &jpip_progress,
                                       double &display_time,
                                       double &next_frame_change_time,
                                       double &next_metanode_change_time,
                                       double &next_progress_change_time);
      /* This function is used to return attributes that were recovered from
         the head of the composition queue and saved internally, when
         `unlock_and_mark_composition_queue_head' was last called.
         [//]
         The function also searches later elements in the composition queue
         for the first one in which the frame index, animation metanode
         or JPIP progress differ from the values returned here.  This
         last functionality deserves more explanation.  On entry,
         `next_frame_change_time', `next_metanode_change_time' and
         `next_progress_change_time' hold thresholds on the display times for
         future animation frames in which the frame index, metanode or
         JPIP progress value might differ from those returned here.  On exit,
         the relevant threshold is replaced by -1 if the function was unable
         to find any future animation frames whose display time is <= the
         threshold and for which the relevant quantity differs.  Otherwise, the
         relevant threshold is replaced by the actual display time of the
         future animation frame in which the change occurs.  The principle
         reason for recovering this information is to allow the caller to
         implement a mechanism for determining the best times to
         periodically update status or metadata display status.  If a threshold
         is -ve on entry, it remains -ve on exit.
         [//]
         The function returns false if `unlock_and_mark_composition_queue_head'
         has not been called since the last call to `flush_composition_queue',
         in which case no argument values are modified in any way. */
    bool composition_queue_tail_presented() { return queue_tail_presented; }
      /* Returns true if the final buffer on the composition queue has been
         fully presented.  Becomes false again if new buffers are pushed. */
    bool conditionally_push_composition_buffer(double display_time,
                                               int frame_idx,
                                               int max_queued_buffers);
      /* We call this function from the main processing thread, rather than
         calling `push_composition_buffer' directly.  The `frame_idx' argument
         is stored as the `custom_id_val' in the call to `push_compositor_buf'.
         The `display_time' should be identical to the value already recorded
         via `kdws_compositor_buf::set_display_time' in the current working
         buffer, but we don't explicitly check this here. The function
         provides three extra features, in addition to serializing access to
         the composition buffer queue via an internal mutex.
            The first extra feature is that the function automatically pops
         buffers from the head of the composition queue (assuming it is not
         locked), if either the buffer is marked as having been presented (see
         `unlock_and_mark_composition_queue_head') or the buffer has a later
         display time (see `kdws_compositor_buf::get_display_time') than the
         one provided here; the idea is that the processing thread may have
         been asked to regenerate frames that were previously pushed to the
         queue because conditions have changed (e.g., zoom factor, orientation,
         buffer size, etc.).
            The second extra feature is that the function automatically
         replaces the tail of the composition queue if that buffer's
         display time as well as the `display_time' argument supplied
         here both fall within the threshold T that we expect to be
         used in the next call to `lock_new_composition_queue_head' -- this
         value was estimatedby the last call to that function.
            The third extra feature is that the function will not allow the
         composition buffer queue to grow beyond `max_queued_buffers' in
         length; it returns false if this restriction prevents the working
         buffer from being pushed onto the queue.  It also returns false if
         the underlying call to `push_composition_buffer' fails -- typically
         because the working buffer has not yet been fully processed. */
    virtual void flush_composition_queue()
      { 
        mutex.lock();
        kdu_region_compositor::flush_composition_queue();
        have_presented_queue_head_info = false;
        queue_head_presented = queue_tail_presented = false;
        need_display_redraw = false;
        mutex.unlock();
      }
      /* Overrides the base member function of the same name to provide
         multi-threaded access protection via a mutex and also to reset the
         internal status so `get_presented_queue_head_info' will return false
         until a new buffer is marked as having been used. */
      virtual kdws_compositor_buf *
        get_composition_buffer_ex(kdu_dims &region, bool working_only)
        { 
          kdu_compositor_buf *res=NULL;
          if (!working_only)
            { 
              mutex.lock();
              try {
                res =
                kdu_region_compositor::get_composition_buffer(region,
                                                              working_only);
              } catch(...) {
                return NULL;
              }
              mutex.unlock();
            }
          else
            res = kdu_region_compositor::get_composition_buffer(region,true);
          return (kdws_compositor_buf *) res;
        }
        /* Use this instead of `get_composition_buffer'.  This function
           provides multi-threaded access protection to the composition
           queue and also makes available the derived `kdws_compositor_buf'
           object, whose members can be queried for additional information. */
  private: // Data
    kdws_compositor_buf *active_list;
    kdws_compositor_buf *free_list;
    kdu_mutex mutex; // Use for locking access to the composition buffer queue
    bool queue_head_locked;
    bool queue_head_presented;
    bool queue_tail_presented; // If all buffers on queue have been presented
    bool have_presented_queue_head_info;
    int presented_queue_head_frame_idx;
    double presented_queue_head_frame_start;
    double presented_queue_head_frame_end;
    int presented_queue_head_metamation_frame_id;
    jpx_metanode presented_queue_head_metanode;
    double presented_queue_head_jpip_progress;
    double presented_queue_head_display_time;
    double last_display_event_time; // These values are -ve until first call to
    double next_display_event_time; // `init_display_event_times', then updated
    double next_display_time_threshold; // by `lock_new_composition_queue_head'
    kdws_display_geometry display_geometry;
    kdu_dims focussing_rect; // Also locked via he `mutex'
    bool focussing_active; // See `set_focussing_rect'
    bool need_display_redraw; // Set if `record_display_geometry' or
          // `set_focussing_rect' changes the display geometry or changes or
          // removes a focussing rectangle at a point when the composition
          // queue is non-empty and its head has already been presented;
          // this causes the next call to `lock_new_composition_queue_head'
          // to return true so that the same buffer will be re-presented.
  };

/*****************************************************************************/
/*                              kdws_rel_dims                                */
/*****************************************************************************/

struct kdws_rel_dims {
  /* Used to represent the centre and dimensions of a rectangular region,
     relative to a containing rectangle whose identity is implied by the
     context. */
  kdws_rel_dims() { reset(); }
  void reset() { centre_x=centre_y=0.5; size_x=size_y=1.0; }
    /* Initializes the region to one which exactly covers its container. */
  bool set(kdu_dims region, kdu_dims container);
    /* Initializes the `region' relative to `container'.  Returns true if
       the `region' is a subset of or equal to `container'.  Otherwise, the
       function clips the `region' to the `container' but returns false. */
  bool set(kdu_dims region, kdu_coords container_size)
    { kdu_dims cont; cont.size=container_size; return set(region,cont); }
  kdu_dims apply_to_container(kdu_dims container);
    /* Expands the relative parameters into an absolute region, relative to
       the supplied `container'. */
  bool is_partial()
    { return ((size_x != 1.0) || (size_y != 1.0) ||
              (centre_x != 0.5) || (centre_y != 0.5)); }
    /* Returns true if the containing rectangle is only partially covered by
       the region. */
  void transpose()
    { double tmp; tmp=centre_x; centre_x=centre_y; centre_y=tmp;
      tmp=size_x; size_x=size_y; size_y=tmp; }
    /* Transpose the region. */
  void vflip() { centre_y=1.0-centre_y; }
    /* Flip the region vertically. */
  void hflip() { centre_x=1.0-centre_x; }
    /* Flip the region horizontally. */
  double centre_x, centre_y;
  double size_x, size_y;
};

/*****************************************************************************/
/*                              kdws_rel_focus                               */
/*****************************************************************************/

struct kdws_rel_focus {
public: // Member functions
  kdws_rel_focus() { reset(); }
  void reset()
    { codestream_idx=layer_idx=frame_idx=-1; frame=jpx_frame();
      animator_driven=false; region.reset(); layer_region.reset();
      codestream_region.reset(); }
  bool is_valid() { return ((codestream_idx&layer_idx&frame_idx) >= 0); }
    /* Returns true if any of the `codestream_idx', `layer_idx' or `frame_idx'
       members is non-negative. */
  bool operator!() { return !is_valid(); }
    /* Returns true if `is_valid' is false. */
  void hflip()
    { region.hflip(); layer_region.hflip(); codestream_region.hflip(); }
  void vflip()
    { region.vflip(); layer_region.vflip(); codestream_region.vflip(); }
  void transpose()
    { region.transpose(); layer_region.transpose();
      codestream_region.transpose(); }
  bool get_centre_if_valid(double &centre_x, double &centre_y)
    { if (!is_valid()) return false;
      centre_x=region.centre_x; centre_y=region.centre_y; return true; }
public: // Data
  int codestream_idx; // -ve unless focus fully contained within a codestream
  int layer_idx; // -ve unless focus fully contained in a compositing layer
  int frame_idx; // -ve unless focus defined on a JPX or MJ2 frame
  jpx_frame frame; // Ignored unless JPX source and `frame_idx' >= 0.
  bool animator_driven; // If true, focus information derived from animator
  kdws_rel_dims region; // See below
  kdws_rel_dims layer_region; // See below
  kdws_rel_dims codestream_region; // See below
};
  /* Notes:
       The `region' member holds the location and dimensions of the focus
     box relative to the full image, whatever that might be.  If `frame_idx'
     is non-negative, the full image is a composed JPX frame or an MJ2 frame.
     Otherwise, if `layer_idx' is non-negative, the full image is a
     single compositing layer.  Otherwise, if `codestream_idx' must be
     non-negative and the full image is a single component from a single
     codestream.  If none of the image entity indices is non-negative, the
     `region', `layer_region' and `codestream_region' members are all invalid.
        The `layer_region' member holds the location and dimensions of the
     focus region relative to the compositing layer identified by `layer_idx',
     if it is non-negative.
        The `codestream_region' member holds the location and dimensions of
     the focus region relative to the codestream identified by
     `codestream_idx', if it is non-negative.
        If `animator_driven' is true, the focus information was derived from
     the `kdu_region_animator' object for the animation frame currently
     being generated.  In this case, focus information must not be changed
     by user interaction and should not generally be adjusted until the
     animation frame changes. */

/*****************************************************************************/
/*                                kdws_renderer                              */
/*****************************************************************************/

class kdws_renderer: public CCmdTarget
{
public: // Public member functions for use by other objects
  kdws_renderer(kdws_frame_window *owner,
                kdws_image_view *image_view,
                kdws_manager *manager);
  ~kdws_renderer();
  void set_key_window_status(bool is_key_window);
    /* Called when the associated frame window gains or relinquishes key
       status within the application */
  void perform_essential_cleanup_steps(bool app_terminating);
    /* This function performs only those cleanup steps which are required to
       correctly manage non-memory resources (generally, temporary files).  It
       is invoked if the application is about to terminate without giving
       the destructor an opportunity to execute.  The `app_terminating'
       argument is true only if the function is being invoked from
       `kdws_window:application_terminating', in which case any connected
       `kdu_client' should be cleanly disconnected here (albeit with only
       a short timeout) here, since there will be no call to `close_file'
       or the destructor. */
  void deactivate();
    /* Does most of the work of `close_file' but does not call
       `perform_essential_cleanup_steps' and does not delete file/path
       information or release any JPIP client (or the queue-id we may have
       received for it). */
  void close_file();
  void close_after_exception(kdu_exception exc);
    /* Called within the catch statement of a try/catch clause in any context
       where the fault may potentially have involved an exception generated
       within the `kdu_thread_env' multi-threaded sub-system and the
       `close_file' function needs to be called.  Before invoking
       `close_file', the function invokes `thread_env::handle_exception' and
       `thread_env.destroy', then calls `compositor->set_thread_env(NULL,NULL)'
       if appropriate.  These are standard steps that should be performed
       after a potentially devastating error that might have been generated
       by any thread within the multi-threaded procesing sub-system. */
  void open_as_duplicate(kdws_renderer *src);
    /* Called to open the file/URL which is currently opened by `src' within
       the current window.  This function indirecly invokes `open_file'. */
  void open_file(const char *filename_or_url);
    /* This function enters with `filename_or_url'=NULL if it is being called
       to complete an overlapped opening operation.  This happens with the
       'kdu_client' compressed data source, which must first be connected
       to a server and then, after some time has passed, becomes ready
       to actually open a code-stream or JP2-family data source. */
  const char *get_open_filename_or_url()
    { return (open_file_pathname!=NULL)?open_file_pathname:open_file_urlname; }
    /* This function returns either the pathname or the URL of the currently
       open resource -- NULL if nothing is open.  This can be used as a
       base for relative URL's that might be found within the metadata of
       the resource. */
  bool view_dims_changed(kdu_dims new_view_dims);
    /* This function is invoked whenever the viewport may have changed,
       including from within `set_view_size', `set_hscroll_pos',
       `set_vscroll_pos' and `set_scroll_pos'.  The function may set the
       `view_dims_change_deferred' flag in animation mode to prevent excessive
       animation refresh events, in which case it will be called again from
       within `advance_animation' with the `view_dims_change_deferred' still
       set.  The function returns true if something has changed so that the
       scroller positions/sizes may need to be adjusted, or the view may
       need to be redrawn. */
  RECT convert_region_to_display_view(kdu_dims region)
    { return convert_region_to_display_view(region,pixel_scale,view_dims); }
  RECT convert_region_to_display_view(kdu_dims region, double scale,
                                      kdu_dims ref_viewport);
    /* Converts a `region', defined relative to the coordinate system used
       by `ref_viewport' to describe the size and location of the entire
       composited image, into a region expressed in window coordinates.
       Note that the `ref_viewport' argument identifies the
       viewport (in composited buffer coordinates) that should represent
       the portion of the image displayed in the window.  The upper left
       hand corner of `ref_viewport' (i.e., the location at
       `ref_viewport.pos') should correspond to (0,0) in window coordinates.
       The `scale' supplied here is almost always an integer.  It should
       represent the ratio between the width (or height) of the display
       window's client area and the width (or height) of `ref_viewport'.
       The `scale' factor will often be equal to 1.0.
          The first form of the function uses the objects's `pixel_scale'
       and `view_dims' members for `scale' and `ref_viewport'.  The
       more general form of the function, however, allows for conversions
       that are based on non-current rendering conditions; this is exploited
       within the implementation of `paint_region' which needs to be able to
       paint from queued buffers that might have been rendered under different
       conditions to those that currently prevail. */
  kdu_dims convert_region_from_display_view(RECT rect)
    { return convert_region_from_display_view(rect,pixel_scale,view_dims); }
  kdu_dims convert_region_from_display_view(RECT rect, double scale,
                                            kdu_dims ref_viewport);
    /* Does the reverse of `convert_region_to_display_view', except that the
       returned region is expanded as required to ensure that it covers
       the `rect' rectangle. */
  POINT convert_point_to_display_view(kdu_coords point);
    /* Converts a `point', defined in the same coordinate system as
       `view_dims', `working_buffer_dims' and `image_dims', to a loction
       within the image display window's coordinate system. */
  kdu_coords convert_point_from_display_view(POINT point);
    /* Does the reverse of `convert_point_to_doc_view'. */
  void paint_region(RECT region, CDC *dc);
    /* This function does all the actual work of drawing imagery to the
       window, except when an animation is in progress (`animator' != NULL),
       in which case he `paint_dx' function is used instead.  Since `paint_dx'
       is always used during animation, we can be sure that this function
       is always invoked from the main application thread.  In some
       contexts, it is convenient for the function to be called with `dc'
       NULL, in which case the relevant device context is discovered
       internally. */
  bool paint_dx(CDC *dc, kdu_coords surface_size);
    /* This function is used to draw imagery to the DirectX surface that is
       attached to the supplied device context, whose dimensions are given by
       `surface_size'.  Upon return, the backbuffer to which the drawing is
       rendered will be flipped (synchronized to the video blanking period).
       The function is called only from `present_queued_frame_buffer'.
          The function is complicated by the fact that the composition buffer
       that we use for drawing may either be the current working buffer or
       one which was pushed onto the `compositor's composition queue, possibly
       under different viewing conditions.  It is important to recognize that
       for buffers which are obtained from the composition queue, there are
       two modes of operation, depending on whether the animation process is
       deliberately moving the view around or the viewport is under user
       control.  The former case may hold if there is a non-empty animation
       region of interest (second extra region stored with
       `kdws_compositor_buf').
          We use `kdws_region_compositor::get_display_geometry' to
       recover the image and viewport dimensions and locations, as understood
       at the current point in time, which may differ from their values at the
       time when the composition buffer was generated.  Write
       `display_image_dims' and `display_viewport' for these quantities.
       At the same time, we also get the current orientation information for
       the display, which we write as `display_orientation'.
          Next, we use the `kdws_compositor::get_composition_buffer_ex'
       function to recover the image dimensions, buffer dimensions/location
       and prevailing orientation that were current at the time the relevant
       composition buffer was created/queued.  Call these `buf_image_dims',
       `buf_viewport' and `buf_orientation', respectively.
          By comparing `buf_image_dims.size' with `display_image_dims.size'
       and combining this information with the prevailing `pixel_scale',
       we recover the appropriate scaling factor for presenting the buffered
       data.  If `buf_orientation' and `display_orientation' disagree, the
       function draws nothing, since we can expect a new frame buffer to be
       pushed to the composition queue shortly; otherwise, we can expect
       the horizontal and vertical scaling factors to agree (at least very
       closely).
          The intention is always to repaint the entire viewport from the
       available buffer data, if possible.  If anything is drawn at all,
       the frame buffer is first cleared to white (this might be all that
       is done) and the function returns true.  Otherwise, the function
       returns false.
    */
  bool present_queued_frame_buffer(double display_event_time,
                                   double next_display_event_time);
    /* This function is used to paint directly to the image view window
       from the presentation thread.  Calls to this function arrive from the
       `kdws_frame_presenter' object on a regular basis (once it is enabled),
       regardless of whether or not there are frames to be presented.  The
       present function checks the time and the composition buffer queue to
       determine the most appropriate buffer to present, if any. The function
       automatically pops buffers from the queue until the most appropriate
       buffer is discovered.  It marks any presented frame buffer as having
       been used by calling `compositor->mark_composition_queue_head_used';
       this allows the main application thread to atomically pop the used queue
       head and use its buffer resources, if it needs to push a new
       composition buffer onto the queue.  The function returns true if and
       only if it presents a frame, marking it as having been used.  This
       action causes a flag to be set within the `kdws_frame_presenter'
       object, that causes the `window:display_notification' function to be
       called from the main thread.  This ensures that the application has
       an opportunity to push a new composition buffer onto the queue, if one
       is available.  The `display_event_time' and `next_display_event_time'
       arguments tell the function everything it needs to know about the
       current display clock -- see `kdws_frame_presenter::draw_pending_frame'
       and/or `kdu_region_animator::advance_animation' for more information
       on the concept of a display clock that is separate from the
       system clock. */
  void display_status();
    /* Displays information in the status bar.  The particular type of
       information displayed depends upon the value of the `status_id'
       member variable.  If `animation_frame_idx' is non-negative, it holds
       the index of the frame currently being presented during an animation,
       as opposed to `frame_idx' which holds the index of the frame being
       (or most recently) rendered. */
  bool do_processing();
    /* This function is called when the run-loop is about to go idle, or
       in response to a scheduled processing event.  This is where we perform
       our decompression and render processing.  The function returns true if
       a subsequent call to the function would be able to perform additional
       processing -- this may result in the function being called again
       immediately, or a future processing event being scheduled on the
       run-loop's message queue. */
  bool need_processing()
    { // Returns true if a call to `do_processing' might do some work
      return (configuration_complete && (working_buffer != NULL) &&
              (compositor != NULL) && (!animation_frame_complete) &&
              !compositor->is_processing_complete());
    }
  void refresh_display()
    { 
      if (animator == NULL)
        render_refresh();
      else
        advance_animation(-1.0,-1.0,-1.0);
    }
    /* This function is called when the processing of user interaction
       requires re-rendering of the entire image surface, without necessarily
       changing what is to be rendered.  For example, the number of requested
       quality layers may have been changed.
          If there is no animation, this function simply calls
       `render_refresh'.  During animation, however, the function invokes
       `advance_animation' with -ve arguments, which requests the animation
       machinery to backtrack to the frame that is most relevant for
       display at the current time. */
  void refresh_metadata(double cur_time);
    /* This function is called from any point where the rendering of metadata
       in non-image format may need to be updated -- typically inside
       "metashow" or the metadata catalog.  The function is invoked from
       within `metadata_changed'.  Also, the `client_notification'
       function either invokes this function itself or schedules a call to
       it at a later point through `schedule_metadata_refresh_wakeup', if
       a JPX source is involved. */
  void wakeup(double scheduled_wakeup_time, double current_time);
    /* This function is called if a wakeup was previously scheduled, once
       the scheduled time has arrived.  For convenience, the function is
       passed the original scheduled wakeup time and the current time. */
  void client_notification();
    /* This function is called if the JPIP client used by this object has
       changed the cache contents or the status message. */
  void set_codestream(int codestream_idx);
    /* Called from `kdws_metashow' when the user clicks on a code-stream box,
       this function switches to single component mode to display the
       image components of the indicated code-stream. */
  void set_compositing_layer(int layer_idx);
    /* Called from `kd_metashow' when the user clicks on a compositing layer
       header box, this function switches to single layer mode to display the
       indicated compositing layer. */
  void set_compositing_layer(kdu_coords point);
    /* This form of the function determines the compositing layer index
       based on the supplied location.  It searches for the top-most
       compositing layer which falls underneath the current mouse position
       (given by `point', relative to the upper left corner of the view
       window).  If none is found, no action is taken. */
  bool set_frame(int frame_idx, double cur_time=-1.0);
    /* Invoked by menu activity and also by `manage_animation_frame_queue',
       this function is used to change the frame within an MJ2 track or a
       JPX animation.  It invokes `change_frame_idx' but also configures the
       processing machinery to decompress the new frame.  Values which are
       currently illegal will be corrected automatically.  The function
       returns true if it produced a change in the current frame.  The
       `cur_time' argument should either be -ve or equal to the current
       time, as a convenience so that `get_current_time' need not be
       called redundantly if the caller of this function has already
       evaluated the current time -- as happens from within
       `advance_animation'. */
  bool set_frame(jpx_frame new_frame, double cur_time=-1.0);
    /* As above, but uses `change_frame' in place of `change_frame_idx'.  This
       function should be used wherever there is a possibility that the
       presentation track associated with a JPX animation might also need
       to change.  Both the frame index and the set of compatible track
       indices are obtained from `new_frame' and the track index is changed
       only if the current one is not compatible with `new_frame'. */
  void set_animation_point_by_idx(int frame_idx);
    /* Very similar to `set_frame', but this function is invoked from
       the animation bar and `frame_idx' is to be interpreted with respect
       to the way in which the animation bar has been configured.  In
       particular, since it is possible to play a sequence of compositing
       layers as an animation, if a JPX source has no explicit composition
       box, these can be considered valid frame indices.  Also, if
       `frame_idx' is -ve, the function automatically advances to the last
       frame in the animation.  This function currently cancels any
       playing animation. */
  void set_animation_point_by_time(double frame_time,
                                   bool nearest_start_time);
    /* Similar to `set_animation_point_by_idx', except that this function
       selects the animation frame to be displayed based upon its original
       frame time.  This function does nothing if there are no frame
       times available.
          If `nearest_start_time' is true, the function looks for the
       frame whose nominal start time is closest to `frame_time'; otherwise,
       it looks for a frame whose nominal start and end times straddle the
       supplied `frame_time'.
          Note that this function always skips over frames that have zero
       duration -- interpreted as "pause" frames, for pausing an animation
       and stepping through manually. */
  kdu_region_animator *get_animator() { return animator; }
    /* Returns NULL if there is no animation in progress. */
  void stop_animation();
    /* Wraps up all the functionality to terminate a video playback session
       or other form of animation. */
  kdu_region_animator *start_animation(bool reverse,
                                       jpx_metanode metanode=jpx_metanode());
    /* Wraps up all the functionality to commence video playback mode (if
       `metanode' is an empty interface) or a metadata-driven animation (if
       `metanode' is non-empty).  In the latter case, the function examines
       the descendants of `metanode'.  If they are all number lists or all
       ROI description nodes, and there are at least 2 of them, they are
       passed to `animator->add_metanode'; otherwise, the function returns
       NULL.  If the animator is started by this function, the function
       returns a pointer to it -- this is used by the metadata catalog to
       add further metadata to a metadata-driven animation, where
       appropriate. */
  void update_animation_bar_pos();
    /* Called whenever something happens that might change the frame
       position or number of frames in the track, this function updates
       the animation bar's notion of our progress through the current
       animation.  The function affects the `animation_bar', if any, but
       not the `metamation_bar'. */
  void update_animation_bar_state();
    /* Called whenever something happens that might change the repeat
       mode, playing state/direction or requested playback rate, all
       of which is displayed as state information in either the
       animation bar or the metamation bar, if there is one. */
  bool advance_animation(double cur_system_time,
                         double last_display_event_time,
                         double next_display_event_time);
    /* This function invokes `animator->advance_frame' and, if successful,
       proceeds to configure the object to generate the next animation frame.
       If invoked with `cur_system_time' < 0, the function
       calls `animator->advance_frame' with the `refresh' option.  This
       means that we are asking the animator to backtrack over previously
       generated frames, if necessary, so as to re-render them under
       (typically) new conditions.
          If `last_display_event_time' or `next_display_event_time' is -ve,
       the function uses `compositor->get_display_event_times' to discover
       the display event times for passing to `animator->advance_animation'.
          The function may fail, returning false, if: a) there are no more
       animation frames; b) the `animator' is waiting for more data to
       arrive in a dynamic cache (via JPIP) before it can determine the
       next animation frame; or c) something has gone wrong.  In case (c),
       `stop_animation' is called, so `animator' will be NULL upon return.
       In case (a), `pushed_last_frame' will be true.  In cases (a) and (b),
       the function returns without resetting `animation_frame_complete' so
       that no new image rendering will occur during calls to `do_processing'.
       If the function succeeds, returning true, it configures the
       `compositor' object to render the relevant frame.  This includes
       configuring the rendering of metadata overlays, if appropriate. */
  void adjust_playclock(double delta)
    { if (animator != NULL) animator->retard_animation(delta); }
    /* This function is called by the window manager whenever any video
       playback window finds that it is getting behind and needs to make
       adjustments to its playback clock, so that future playback will not
       try too aggressively to make up for lost time. */
// ----------------------------------------------------------------------------
public: // Public member functions which change the appearance  
  void set_view_size(kdu_coords size);
    /* This function should be called whenever the client view window's
       dimensions may have changed.  The function should be called at least
       whenever the WM_SIZE message is received by the client view window.
       The current object expects to receive this message before it can begin
       processing a newly opened image.  If `size' defines a larger region
       than the currently loaded image, the actual `view_dims' region used
       for rendering purposes will be smaller than the client view window's
       dimensions.  Like `set_hscroll_pos' and the other functions below,
       this function uses `view_dims_changed' to actually change the
       viewport and configure the working buffer geometry appropriately. */
  void set_hscroll_pos(int pos, bool relative_to_last=false);
    /* Identifies a new left hand boundary position for the current view.
       If `relative_to_last' is true, the new position is expressed relative
       to the existing left hand view boundary.  Otherwise, the new position
       is expressed relative to the image origin. */
  void set_vscroll_pos(int pos, bool relative_to_last=false);
    /* Identifies a new upper boundary position for the current view.
       If `relative_to_last' is true, the new position is expressed relative
       to the existing left hand view boundary.  Otherwise, the new position
       is expressed relative to the image origin. */
  void set_scroll_pos(int xpos, int ypos, bool relative_to_last=false);
    /* This is a combined scrolling function that allows scrolling in two
       directions simultaneously */
  void set_focus_box(kdu_dims new_box, jpx_metanode focus_metanode);
    /* Called from the `doc_view' object when the user finishes entering a
       new focus box.  Also invoked from other parts of the system when the
       need to place a box around some region of interest arises.  The focus
       box, if present, provides a centre of attention for zooming and other
       geometric manipulations.  The focus box also provides a window of
       interest for JPIP requests during remote image browsing and an
       initial rectangular region of interest to use when adding ROI metadata.
          There are two types of focus box that can be set.  If
       `focus_metanode' is non-empty, the focus box is identifying content
       that already exists -- e.g., a region of interest or a composited image
       of interest within a larger composited frame, as determined by the
       supplied metanode.  Otherwise, the focus box is identifying a
       user-defined region of interest, or a current window-of-interest
       that is being served by a JPIP server, that was not inspired by
       local analysis of any metadata.  Content-driven focus boxes cannot
       be used to create new ROI metadata and they do not generally survive
       frame changes in an animation or video.
          Note that the coordinates of `new_box' have already
       been converted to the same coordinate system as `view_dims',
       `working_buffer_dims' and `image_dims', using
       `convert_region_from_doc_view'. */
  int get_focus_box(kdu_dims &box)
    { 
      if (view_dims.is_empty() || image_dims.is_empty() ||
          !rel_focus.is_valid())
        return 0;
      box = rel_focus.region.apply_to_container(image_dims);
      return (focus_metanode.exists())?-1:1;
    }
    /* Returns 0 if there is no focus box defined.  Otherwise, `box' is
       set to represent the focus box, in the same coordinates as used by
       `set_focus_box', and the function returns either 1 or -1.  A postive
       return means that the focus box is user defined; a negative return
       means that the focus box is derived from a region of interest in
       the source file's metadata.  This function is called from the `doc_view'
       object to determine whether a drag/zoom operation should be applied
       to the focus box or to the view itself. */
  kdu_coords displace_focus_box(kdu_coords shift);
    /* This function is called from `doc_view' in response to user interaction
       that manipulates the focus box.  The behaviour is very similar to
       calling `get_focus_box', adding `shift' to the `kdu_dims::pos' member of
       the returned region and then passing it back to `set_focus_box', except
       for the following modifications:
       1. The function does nothing if there is no focus box, or the focus
          box is derived from metadata -- i.e., if `get_focus_box' would
          return non-positive.
       2. The function does not shift the focus box outside the image region
          so it will not alter the size of the box -- on the other hand,
          `set_focus_box' must clip to the image region.
       3. The view is automatically scrolled, as necessary, to keep the
          new focus box visible.
       The function returns the actual amount by which the focus box was
       displaced minus the amount by which the view was shifted; this
       information allows the caller to adjust the mouse cursor position,
       so as to improve the efficacy of the user interaction. */
  void set_focussing_rect(kdu_dims rect, bool active)
    { // Just passes the information along to `compositor'
      if (compositor != NULL)
        compositor->set_focussing_rect(rect,active);
    }
// ----------------------------------------------------------------------------
public: // Public member functions which are used for metadata
  void set_metadata_catalog_source(kdws_catalog *source);
    /* If `source' is NULL, this function releases any existing
       `catalog_source' object.  Otherwise, the function installs the new
       object as the internal `catalog_source' member and invokes the
       `source->update_metadata' function to parse JPX metadata into the
       newly created catalog. */
  void reveal_metadata_at_point(kdu_coords point, bool dbl_click=false);
    /* If the metadata catalog is open, this function attempts
       to find and select an associated label within the metadata catalog.
       `point' is expressed in compositor buffer coordinates already.
       Most of the work is done by `kdws_catalog:select_matching_metanode'.
       If the `metashow' window is open, the function also attempts to select
       the appropriate metadata within the metashow window, using the
       `kdws_metashow:select_matching_metanode' function.
       [//]
       If `dbl_click' is true, this function is being called in response to
       a mouse double-click operation, which causes the item to be
       passed to `show_imagery_for_metanode'. */
  void edit_metadata_at_point(kdu_coords *point=NULL);
    /* If `point' is NULL, the function obtains the location at which to
       edit the metadata from the current mouse position.  Otherwise, `point'
       is interpreted in compositor buffer coordinates already. */
  void edit_metanode(jpx_metanode node, bool include_roi_or_numlist=false);
    /* Opens the metadata editor to edit the supplied node and any of its
       descendants.  This function may be called from the `kdws_metashow'
       object if the user clicks the edit button.  If the node is not
       directly editable, the function searches its descendants for a
       collection of top-level editable objects.  If the `node' itself is
       directly editable and `include_roi_or_numlist' is true, the function
       searches up through the node's ancestors until it comes to an ROI
       node or a numlist node.  If it comes to an ROI node first, the ROI
       node forms the root of the edit list; if it comes to a numlist node
       first, the numlist node's descendant from which `node' descends
       forms the root of the edit list; otherwise, `node' is the root
       of the edit list.  In all cases, editing starts at `node' rather
       than any ancestor node which is set as the root of the edit list.
       However, the user may navigate up to any ancestors which may exist
       within the edit list. */
  bool highlight_imagery_for_metanode(jpx_metanode node);
    /* This function does not alter the current view (unlike
       `show_imagery_for_metanode'), but if a region of interest (or
       perhaps a specific codestream or compositing layer) identified by
       the supplied `node' is visible within the current view and does not
       occupy the entire view, it is temporarily highlighted.  This is done
       only if overlays are enabled, in which case the overlay is temporarily
       limited to `node' and the overlay brightness and dwell periods are
       controlled by a highlight state machine -- once the machine exits,
       regular overlay painting is restored. */
  bool show_imagery_for_metanode(jpx_metanode node,
                                 kdu_istream_ref *istream_ref=NULL);
    /* Changes the current view so as to best reveal the imagery associated
       with the supplied metadata node.  The function examines the node and
       its ancestors to find the first ROI (region of interest) node and the
       first numlist node it can.  If there is no numlist, but there is an ROI
       node, the ROI is associated with the first codestream.  If the numlist
       node's image entities do no intersect with what is being displayed
       or none of an ROI node's associated codestreams are being displayed, the
       display is changed to include at least one of the relevant image
       entities.  If there is an ROI node, the imagery is panned and zoomed
       in order to make the region clearly visible and the focus box is
       adjusted to fit around the ROI node's bounding box.
          During interactive JPIP browsing, it can happen that the image
       information required to complete a call to this function are not yet
       available in the cache.  When this happens, the function does as much
       as it can but leaves behind a record of the `node' argument in the
       `pending_show_metanode' member variable, so that the function can be
       automatically called again when new data arrives in the cache.  Such
       pending show events can potentially disrupt future user navigation, so
       the `cancel_pending_show' function is provided, to be called in the
       event of user interaction which might invalidate a pending attempt to
       change the display.  That function is invoked under just about any
       user interaction events which could affect the main image view.
          If `node' happens to be an empty interface, the function does
       nothing.  On the other hand, if the most recent call to this function
       supplied exactly the same `node' and if multiple frames (in frame
       composition mode) or multiple compositing layers (in single compositing
       layer rendering mode) or multiple codestreams (in single codestream
       rendering mode) can be considered to be associated with the `node',
       this function advances the view to the next associated one.  To enable
       this behaviour, the function saves the identity of the `node' supplied
       most recently to this funcion in the `last_show_metanode' variable.
          The function returns false if it was unable to find compatible
       imagery, perhaps because the relevant imagery has not yet become
       available in a dynamic cache, as explained above.  Otherwise, the
       function returns true, even if no change was made in the image
       being viewed -- so long as it is compatible.
          The `istream_ref' argument can be used to obtain the identity of
       the imagery stream (istream) associated with a successful outcome of
       the function -- the value at `istream_ref' may actually be modified
       even if the function returns false, but in that case the value
       should be ignored.  If `node' identified a compositing layer,
       the corresponding ilayer's primary istream is returned.  If `node'
       identified a specific codestream, the corresponding istream is
       returned.  If `node' identified a specific region of interest, the
       istream against which the region is registered is returned here.
       If none of the above cases hold, `istream_ref' is set to a null
       reference.  The `istream_ref' argument is particularly useful in
       view of the fact that the `node' may be associated with many
       codestreams or compositing layers and that each individual
       codestream or compositing layer may be associated with multiple
       imagery layers in the current composition, so it can be difficult
       or even impossible otherwise for the caller to figure out which
       imagery layer was the one used by a successful call to this
       function to base the displayed content. */
  void cancel_pending_show();
    /* This function cancels any internal record of a pending call to
       `show_imagery_for_metanode' which is to be re-attempted when the
       cache contents are augmented by a JPIP client.  The function is
       invoked prior to any user interaction events which might affect the
       rendering state -- actually, any menu command which might be
       routed to the window and any key or mouse events in the main image
       view.  The only operations which do not cancel pending show events
       are those which take place exclusively within the catalog panel
       and/or a metashow window. */
  jpx_meta_manager get_meta_manager()
    { return (!jpx_in)?jpx_meta_manager():jpx_in.access_meta_manager(); }
  void metadata_changed(bool new_data_only);
    /* This function is called from within the metadata editor when changes
       are made to the metadata structure, which might need to be reflected in
       the overlay display and/or an open metashow window.  If `new_data_only'
       is true, no changes have been made to existing metadata. */
  void update_client_metadata_of_interest()
    { if (jpip_client != NULL) update_client_window_of_interest(); }
    /* This function is called by the `catalog_source' if any change occurs
       which may result in the need to change the metadata currently requested
       from the client.  The function calls `update_client_window_of_interest',
       which in turn calls `kdws_catalog:generate_metadata_requests'. */
// ----------------------------------------------------------------------------
public: // Public member functions which are used for metadata editing
  void metadata_editor_closing()
    { // Called from `kdws_metadata_editor::close'.
      editor = NULL;
      refresh_metadata(get_current_time());
    }
  bool get_codestream_size(kdu_coords &size, int codestream_idx);
    /* Returns the size of the codestream on its high resolution
       grid.  If `codestream_idx' is not a known codestream or its dimensions
       cannot be worked out for some reason, the function returns false. */
  bool set_shape_editor(jpx_roi_editor *ed,
                        kdu_istream_ref ed_istream=kdu_istream_ref(),
                        bool hide_path_segments=true,
                        int fixed_path_thickness=0,
                        bool scribble_mode=false);
    /* Installs or removes a shape editor.  Used by the
       `kdws_metadata_editor' object.  Simultaneously sets up the
       configuration state parameters for shape editing -- see
       `update_shape_editor' for more on these. */
  void update_shape_editor(kdu_dims region, bool hide_path_segments,
                           int fixed_path_thickness, bool scribble_mode);
    /* Causes the `region' being edited by an active shape editor to
       be repainted where `region' is expressed using the coordinate system of
       the shape editor itself; also potentially changes the `hide_shape_path',
       `fixed_shape_path_thickness' and `shape_scribbling_mode' members.  The
       first of these state variables determines whether or not
       `get_shape_path' can return non-zero.  If this value is changed, or if
       `region' is empty, the entire shape editor region is redrawn,
       regardless of the `region' argument.  If `fixed_path_thickness' is
       non-zero, future calls to the `shape_editor_adjust_path_thickness'
       function will adjust the path thickness -- this operation occurs each
       time an editing operation changes the region of interest.  If
       `scribble_mode' is true, mouse movements with the left button down
       are caught and used to generate calls to the shape editor's
       `jpx_roi_editor::add_scribble_point' function. */
  kdu_dims shape_editor_adjust_path_thickness()
    {
      bool sc; // Success value not used
      if ((shape_editor == NULL) || (fixed_shape_path_thickness <= 0))
        return(kdu_dims());
      return shape_editor->set_path_thickness(fixed_shape_path_thickness,sc);
    }
    /* This function automatically invokes `set_path_thickness' if and only
       if a fixed shape path thickness is currently installed, as determined
       by recent calls to `set_shape_editor' or `update_shape_editor'. */
  jpx_roi_editor *get_shape_editor() { return shape_editor; }
  bool get_shape_scribble_mode() { return shape_scribbling_mode; }
  jpx_roi_editor *
    map_shape_point_from_display_view(POINT point, kdu_coords &mapped_point)
    {
      kdu_coords pt = convert_point_from_display_view(point);
      kdu_dims regn; regn.pos=pt; regn.size.x=regn.size.y=1;
      if ((shape_editor == NULL) || (compositor == NULL) ||
          !compositor->map_region(regn,shape_istream))
        return NULL;
      mapped_point = regn.pos; return shape_editor;
    }
    /* Maps a `point' in the window coordinate system to the high
       resolution codestream canvas coordinate system of the codestream
       associated with `shape_istream', adjusted so that it expresses
       the location relative to the start of the codestream's image region
       (i.e., following the convention of ROI description boxes).
       If `shape_istream' does not correspond to a valid istream reference
       into the current composition, or there is no currently active
       shape editor, the function returns NULL.  Otherwise, the function
       returns a pointer to the current shape editor, setting `mapped_point'
       to the relevant codestream location. */
  jpx_roi_editor *
    map_shape_point_to_display_view(kdu_coords point, POINT &mapped_point)
    {
      kdu_dims regn; regn.pos=point; regn.size.x=regn.size.y=1;
      if ((shape_editor == NULL) || (compositor == NULL) ||
          (regn=compositor->inverse_map_region(regn,shape_istream)).is_empty())
        return NULL;
      mapped_point = convert_point_to_display_view(regn.pos);
      return shape_editor;
    }
    /* Performs the reverse mapping to `map_shape_point_from_doc_view'. */
  kdu_dims get_visible_shape_region()
    {
      kdu_dims regn=view_dims;
      if ((shape_editor == NULL) || (compositor == NULL) ||
          !compositor->map_region(regn,shape_istream))
        regn.size.x=regn.size.y=0;
      return regn;
    }
    /* Maps the current `view_dims' region to the high resolution
       codestream canvas coordinate system of the codestream associated
       with `shape_istream', adjusted so that it expresses the location
       relative to the start of the codestream's image region (i.e., following
       the convention of ROI description boxes). */
  bool nudge_selected_shape_points(int delta_x, int delta_y,
                                   kdu_dims &update_region);
    /* This function returns false unless there is an active shape editor
       in which an anchor point is selected.  In that case, the function
       moves any currently selected anchor point `delta_x' samples to the
       right and `delta_y' samples down, where the samples in question here
       represent one pixel at the current rendering scale; it also sets
       `update_region' to the region reported by
       `jpx_roi_editor::move_selected_anchor'. */
  int get_shape_anchor(POINT &point, int which, bool sel_only, bool dragged);
    /* This function can be used to walk through the vertices which need
       to be displayed by the ROI shape editor.  The function essentially
       just invokes `shape_editor->get_anchor', converting the vertex
       location to the image display window's coordinate system.  If there
       is no active shape editor, or `which' is greater than or equal to the
       number of vertices to be painted, the function returns 0; otherwise,
       the return value is identical to the value returned by the
       corresponding call to `shape_editor->get_anchor'. */
  int get_shape_edge(POINT &from, POINT &to, int which,
                     bool sel_only, bool dragged);
    /* Same as `get_shape_anchor' but for edges. */
  int get_shape_curve(POINT &centre, double &extent_x, double &extent_y,
                      double &alpha, int which, bool sel_only, bool dragged);
    /* Similar to `get_shape_edge', but returns ellipses.  The ellipse is
       to be formed by starting with a cardinally aligned ellipse with
       centre at `centre', half-height `extent_y' and half-width
       `extent_x'.  This initial ellipse is to be skewed horizontally
       using the `alpha' parameter, meaning that a boundary point at
       location (x,y) is to be shifted to location
       (x+alpha*(y-centre.y), y). */
  int get_shape_path(POINT &from, POINT &to, int which);
    /* Same as `get_shape_edge', but returns path segments rather than
       region edges, by invoking the `shape_editor's `get_path_segment'
       function. */
  bool get_scribble_point(POINT &pt, int which)
    { kdu_coords shape_pt;
      return ((shape_editor != NULL) &&
              shape_editor->get_scribble_point(shape_pt,which) &&
              map_shape_point_to_display_view(shape_pt,pt));
    }
    /* Similar to `get_shape_edge', but returns successive scribble points
       from the shape editor.  Returns false if there is none which the index
       `which'. */
// ----------------------------------------------------------------------------
private: // Private implementation functions
  void place_duplicate_window();
    /* This function is called from `open_file' right before the call to
       `initialize_regions' if a window is being duplicated.  It sizes and
       places the current window (using `window_manager->place_window') such
       that it holds the same contents as those identified by the `view_dims'
       member of `duplication_src'.  If the `duplication_src' is using a
       focus box, the placement size and view anchors are adjusted so that
       the new window will include only a small border around the focus
       region. */
  void invalidate_configuration();
    /* Call this function if the compositing configuration parameters
       (`single_component_idx', `single_codestream_idx', `single_layer_idx')
       are changed in any way.  The function removes all compositing layers
       from the current configuration installed into the `compositor' object.
       It sets the `configuration_complete' flag to false, the `bitmap'
       member to NULL, the image dimensions to 0, and posts a new client
       request to the `kdu_client' object if required.   After calling this
       function, you should generally call `initialize_regions'. */
  void initialize_regions(bool perform_full_window_init);
    /* Called by `open_file' after first opening an image (or JPIP client
       session) and also whenever the composition is changed (e.g., a change
       in the JPX compositing layers which are to be used, or a change to
       single-component mode rendering).  If `perform_full_window_init' is
       true, the function diescards any existing information about the
       view dimensions -- this is important in many settings. */
  void configure_quality_limiting();
    /* Called if `quality_limiting_mode' changes, or if the `pixel_scale'
       changes. */
  bool render_refresh(bool new_imagery_only=false);
    /* The purpose of this function is to call `compositor->refresh' and
       then deal with the possibility that upon refreshing its understanding
       of what is being rendered, the compositor may discover that the
       current viewing conditions are not supported by the source (unlikely
       but possible if, for example, a required codestream becomes available
       for the first time from a dynamic cache and it turns out that this
       codestream does not have sufficient DWT levels to support the
       current viewing scale).
          This function is invoked from `refresh_display', but also from
       other places.  The reason for separating `refresh_display' and
       the lower level `refresh_compositor' functions is animation.  If
       there is an animation in progress, refreshing the display generally
       means that `advance_animation' needs to be called with -ve arguments
       to force the animation frame generation process to go
       back and regenerate the one that is most relevant for display at
       the current time, effectively obliterating any later frames that may
       be sitting in the display queue.
          If `new_imagery_only', the function does nothing unless a compositing
       layer or pending MJ2 frame change becomes available for the first time
       as a result of newly available codestream content.  The function can
       be called immediately with this option if any new data arrives in the
       cache from a JPIP server -- without the `new_imagery_only' argument
       being true, the function should be called at most episodically, to
       avoid wasting too much processing time.  The function returns true
       only if something was actually refreshed by `compositor->refresh'.
       The return value is of interest only to the `client_notification'
       function which calls the function with `new_imagery_only' equal to
       true if the compositor is still waiting for some codestreams and
       takes steps to ensure that JPIP requests are regenerated if something
       was refreshed.
          The `render_refresh' function plays a very important role in
       remote image and video browsing via JPIP.  In this context, the
       function interacts with state variables `jpip_wants_render_refresh'
       and `earliest_render_refresh_time'.  These deserve some explanation
       here.  Whenever new data arrives in the JPIP client's
       cache, the `jpip_wants_render_refresh' variable is set, indicating
       that whatever rendering processes are being performed or have been
       performed may need to be redone in order to fully utilize all data
       that is available.
          The `jpip_wants_render_refresh' variable is reset only in this
       function -- not in any other place, except for object construction
       and file close operations.  This is because, we cannot rely upon any
       compositor outputs being completely up to date unless
       `compositor->refresh' has been called -- even if
       the codestream, compositing layer or frame being generated by the
       compositor is changed by `initialize_regions' or `set_frame', there
       is always the possibility that previously generated surfaces are
       re-used without being fully regenerated.
          If `jpip_wants_render_refresh' is true we will eventually have to
       call the present function again some time, but when we do this is
       important.  We would at least like to wait until all rendering
       associated the the last refresh has finished before we start rendering
       again -- actually we want to wait at least `jpip_refresh_interval'
       seconds beyond that point.  The `earliest_render_refresh_time' state
       variable keeps track of the earliest time at which the present function
       should be called again; it is set to -1.0 inside this function (and
       only here) and this condition is detected within `do_processing' once
       the compositor's rendering operations are found to be complete.  At
       that point, the `do_processing' function sets the earliest time at
       which the next call to `render_refresh' should occur in response to
       `jpip_wants_render_refresh'.
    */
  void perform_any_outstanding_render_refresh(double cur_time=-1.0)
    { 
      if ((animator == NULL) && jpip_wants_render_refresh &&
          (earliest_render_refresh_time >= 0.0))
        { 
          cur_time = (cur_time >= 0.0)?cur_time:get_current_time();
          if (cur_time >= earliest_render_refresh_time)
            render_refresh();
        }
    }
    /* This function sees if a `jpip_wants_render_refresh' condition can
       be cleared by actually invoking `render_refresh'.  The function may
       be invoked from any number of possible contexts, but the idea is to
       clear outstanding render refresh calls immediately after we configure
       the compositor to do some other task, if possible -- this minimizes
       needless processing.  Thus, if `configure_overlays' reconfigures the
       compositor's overlay painting machinery, this is a good time to try
       to clear any oustanding `render_refresh'.  Similarly, the function is
       called at the end of a successful call to `initialize_regions',
       after changing frames within `set_frame' and, if `set_frame' was not
       called, after advancing the animation within `advance_animation'.
       Apart from these places, the function is called from within
       `wakeup' if the function detects that a scheduled render refresh is
       now ready to be performed.
          The `cur_time' argument is provided as a convenience, in case the
       caller already has evaluated the current time, so that
       `get_current_time' need not be called again.  However, there is
       no point in evaluating the time if you do not already know it when
       calling here, because it may well not be needed. */
  void perform_or_schedule_render_refresh(double cur_time=-1.0);
    /* This function is typically called after `jpip_wants_render_refresh'
       is set to true or `earliest_render_refresh_time' changes.  The
       function is not called if there is an animation in progress.
          If `jpip_wants_render_refresh' is false or
       `earliest_render_refresh_time' is -ve, the function does nothing.
          Otherwise, the function performs any oustanding render refresh or
       else schedules a wakeup call for the earliest point at which a render
       refresh can be issued.
          As with `perform_any_outstanding_render_refresh', the `cur_time'
       argument is provided as a convenience from contexts where the current
       time has recently been evaluated.  If -ve, the function invokes
       `get_current_time' itself if the time needs to be known. */
  void resize_stripmap(int min_width);
    /* This function is called whenever the current width of the strip map
       buffer is too small to accommodate drawing indirection for focus
       highlighting. */
  void configure_overlays(double cur_time, bool in_wakeup);
    /* This function handles all aspects of the metadata overlay drawing and
       highlighting state machinery.  One role for the function is to invoke
       `compositor->configure_overlays' with the appropriate parameters for
       the current state, which may cause overlays to be turned off, or
       modulated in intensity.  The other role for the function is to advance
       the state machines associated with any non-zero values of
       `overlay_highlight_state' or `overlay_flashing_state', based on the
       `cur_time' argument and the `next_overlay_change_time' member.
          If `cur_time' is -ve on entry, the function resets the overlay
       modulation scheduler, so that `next_overlay_change_time' is obtained by
       adding the relevant dwell time to the current system time.  Otherwise,
       dwell times are added to the current value of `next_overlay_change_time'
       and the function checks to see if multiple state changes are required
       to catch up with the current time -- this is because heavy processing
       load may have caused some state changes to be missed.  
          The function may be called from anywhere, but when called from
       within the `wakeup' function, the `in_wakeup' argument will be true.
       In this case, the function does not invoke
       `install_next_scheduled_wakeup', even if the next wakeup time is
       changed, because that function is going to be called once `wakeup'
       finishes making all required adjustments.  Also, when `in_wakeup' is
       true, the function does not invoke `compositor->configure_overlays'
       unless all processing is complete -- the function will be called again
       from within `do_processing' once processing actually does complete.
          This function is also used during animation, in which case the
       `animator' member will be non-NULL.  In this case, the function ignores
       both the arguments, advancing the overlay state machinery based upon
       the values of `next_overlay_change_time' and the current animation
       frame's display time, as returned by `animator->get_display_time'.
    */
  kdu_dims size_buffer_for_target_view(kdu_dims min_buffer_dims,
                                       bool maximize_consistency,
                                       bool assume_scroll_pan);
    /* This function is used to compute the geometry of the rendering buffer
       surface.  On entry, `min_buffer_dims' holds the viewport dimensions
       and location, expressed in the same coordinate system as `image_dims'.
       It is expected that `image_dims' is non-empty.  The intent is to
       eventually pass the returned region to `compositor->set_buffer_surface'
       and the `min_buffer_dims' region to `compositor->record_viewport';
       however, this function does not do either of these things.
          The function further expands the `min_buffer_dims' region, adding
       a small border to facilitate efficient panning, scrolling and resizing
       by an interactive user.  If `animator' is non-NULL, this border is
       based on the `animation_border_x' and `animation_border_y' members,
       clipped to the limits that would be used for non-animated image
       rendering.
          If `assume_scroll_pan' is true, the call is most likely arising from
       an interactive scroll or pan operation and the function updates the
       `animation_border_x' and `animation_border_y' values to reflect the
       anticipated need for some boundaries to minimize the need for
       animation refresh frames -- these animation borders slowly decay over
       time.
          If `maximize_consistency' is true, the function aims to make
       the returned surface region consistent with the `working_buffer_dims'
       member, by aligning one corner (where appropriate).  If there is a
       compatible region of this form that is fully contained within the
       existing `working_buffer_dims' that region is returned.  Otherwise,
       the function behaves as if `maximize_consistency' were false, returning
       a region that is centred as much as possible about the
       `min_buffer_dims' region.  It is recommended that `maximize_consistency'
       be set to true when the function is called in response to interactive
       pan/resize events within `view_dims_changed', except where `animator'
       is non-NULL, in which case entire frames need to be rendered and pushed
       to the composition buffer queue so that buffer consistency is less
       beneficial.
          Upon return, the caller may make further adjustments to the returned
       region, if desired, before calling `compositor->set_buffer_surface' and
       `compositor->get_composition_buffer_ex'. */
  float increase_scale(float from_scale, bool slightly);
    /* This function uses `kdu_region_compositor::find_optimal_scale' to
       find a suitable scale for rendering the current focus or view which
       is at least 1.5 times larger (1.1 times if `slightly' is true) than
       `from_scale' and ideally around twice (1.2 times if `slightly is true)
       `from_scale'. */
  float decrease_scale(float to_scale, bool slightly);
    /* Same as `increase_scale' but decreases the scale by at least 1.5
       times (1.1 times if `slightly' is true) `from_scale' and ideally
       around twice (1.2 times if `slightly' is true) `from_scale'. */
  bool change_frame_idx(int new_frame_idx);
    /* This function is used to manipulate `frame_idx', `frame_start' and
       `frame_end'.  For JPX sources, the `frame' member is also modified
       as necessary.  Along the way, the function may update `num_frames' and
       `num_frames_known'.  This function does useful work even if
       `new_frame_idx' is identical to the existing `frame_idx' when
       the `frame' interface is empty.  If the desired frame index turns out
       to be unachievable, the function tries to find the nearest achievable
       frame index.  The function returns false if the `new_frame_idx' value
       appears to be achievable, but the frame cannot yet be accessed (leaves
       `frame' empty).  This usually means that we are waiting for more data
       from a JPIP server. */
  void change_frame(jpx_frame new_frame);
    /* This function should be used in preference to `change_frame_idx' if
       it is possible that the presentation track for JPX data source also
       needs to change.  Both the frame index and the set of compatible
       presentation track indices are recovered from `new_frame' -- the
       current `track_idx' is changed only if it is not compatible with
       `new_frame'.  This function should always succeed, assuming that
       `new_frame' is a non-empty interface. */
  bool get_track_duration(double &duration);
    /* Measures the duration of the current track, in seconds, returning
       false if the measured value may increase in the future.  As a side
       effect, this function may increase the `num_frames' count, if more
       frames are discovered, and it may change. */
  jpx_frame find_compatible_frame(jpx_metanode numlist,
                                  int num_regions, const jpx_roi *regions,
                                  int &compatible_codestream_idx,
                                  int &compatible_layer_idx,
                                  bool match_all_layers,
                                  bool advance_to_next);
    /* This function is used when searching for a composited frame which
       matches a JPX metadata node, in the sense defined by the
       `show_imagery_for_metanode' function.
       [//]
       Starting from the current frame and track (if any), it scans through
       the frames in the JPX source, looking for one which matches at least
       one of the compositing layers in the `numlist'.  If the number list
       identifies one or more codestreams, at least one of these must be
       used (or potentially used) by the matching frame.  Moreover, if the
       `regions' array is non-empty, at least one of the regions must be
       visible, with respect to at least one of the codestreams in the
       number list node.  In any case, if a match is found and the number
       list identifies one or more codestreams, the matching codestream is
       written to the `compatible_codestream_idx' argument; otherwise, this
       argument is left untouched.  Similarly, if a match is found, the
       index of the matching compositing layer (the layer in the frame within
       which the match is found) is written to `compatible_layer_idx'.
       [//]
       If the function fails to find a match in the current track (defaults to
       0 or 1 if there is no current track), the function considers later
       tracks, eventually looping back to the first frame of the first track
       until all frames of all tracks have been considered.  Rather than
       explicitly visiting all these frames, the function takes advantage
       of the powerful `jpx_composition::find_numlist_match' API.
       [//]
       If a match is found, a non-empty `jpx_frame' interface is returned
       from which the compatible track indices may be identified using
       `jpx_track::get_track_idx'.
       [//]
       If `match_all_layers' is true, the function tries to find a frame
       which contains all compositing layers identified by the number list
       node.  If there are none, you might call the function a second
       time with `match_all_layers' false.  This option is ignored if
       the number list defines any codestreams or is embedded in a JPX
       container and references base compositing layer indices for that
       container -- see `jpx_composition::find_numlist_match' for an
       explanation of this.
       [//]
       If `advance_to_next' is true, the function tries to find the next
       frame (in cyclic sequence) beyond the current one. */
  void set_rel_focus(kdu_dims box);
    /* Configures the `rel_focus' member from scratch so that it describes
       the region identified by `box'.  This focus box region does not have
       to be confined to the view port (i.e., the region identified by
       `view_dims').  However, the functions which call this function will
       generally ensure that it is so-confined except in the case where
       `focus_metanode' is non-empty.  This function always leaves
       `rel_focus' valid unless `box' or `image_dims' is empty.
          This function is only called from within `set_focus_box' and
       `update_focus_box'.  The `box' supplied by those functions should
       generally be non-empty. */
  bool adjust_rel_focus();
    /* This function uses the various members of `rel_focus' to determine
       whether or not the relative focus information determined when
       `rel_focus' was last configured needs to be adjusted to match the
       current image which is configured within the `compositor'.  If
       adjustments do need to be made, function returns true, in which case
       the caller should call `update_focus_box'.
          The function is invoked by functions such as `set_codestream',
       `set_compositing_layer' and `set_frame'.  It is also invoked by
       `initialize_regions' once a new compositor configuration has been
       determined. */
  void calculate_rel_view_params();
    /* Call this function to determine the centre of the current view relative
       to the entire image.  This information is used by subsequent calls to
       `set_view_size' to position the view port.  The most common use of this
       function is to preserve (or modify) the key properties of the view
       port when the image is zoomed, rotated or flipped.  */
  void scroll_to_view_anchors();
    /* Adjust scrolling position so that the previously calculated view
       anchor is at the centre of the visible region, if possible. */
  kdu_dims find_focus_box(bool allow_box_shift=false);
    /* This function converts the information found in `rel_focus' into
       absolute focus box coordinates for the current rendering configuration.
       If there is no focus box (`rel_focus.is_valid()' returns false), the
       function returns the current `view_dims' -- i.e., the viewport is
       interpreted as the focus region.  If there is no currently configured
       image, the function returns an empty region.  If there is a focus
       box and it does not lie entirely within `view_dims', the behaviour
       of the function depends on the `allow_box_shift' argument: if false,
       the function returns the intersection between `view_dims' and the
       nominal focus box; otherwise, the function shifts the focus box, so
       as to preserve its size (if possible) while keeping it within
       `view_dims'.  Note that when `allow_box_shift' is false, the
       function may return an empty region, even if `view_dims' is
       non-empty. */
  void update_focus_box(bool view_may_be_scrolling=false);
    /* Called whenever a change in the focus box may be required.  This
       function uses `find_focus_box' to find the absolute coordinates of the
       focus box and passes these to `compositor->set_focus_box', repainting
       the region of the screen affected by any focus changes, as necessary.
       If there is a `focus_metanode', this function does not change
       `rel_focus', unless the associated focus box moves right off the
       edge of the view; in that case, `rel_focus' is reset and there is no
       focus box any more.  Otherwise, if `focus_metanode' is empty, the
       function allows the focus box to shift (adjusting `rel_focus'
       accordingly) so that it lies fully within the view port, if possible.
          If the function is called in response to a change in the visible
       region, it is possible that a scrolling activity is in progress.  This
       should be signalled by setting the `view_may_be_scrolling' argument to
       true, since then any change in the focus box will require the entire
       view to be repainted.  This is because the scrolling action moves
       portions of the surface and we cannot be sure in which order the focus
       box repainting and data moving will occur, since it is determined by
       the Cocoa framework. */
// ----------------------------------------------------------------------------
private: // Functions specifically provided for JPIP interactive browsing  
  bool check_initial_request_compatibility();
    /* Checks the server response from an initial JPIP request (if the
       response is available yet) to determine if it is compatible with the
       mode in which we are currently trying to open the image.  If so, the
       function returns true.  Otherwise, the function returns false after
       first downgrading the image opening mode.  If we are currently trying
       to open an animation frame, we downgrade the operation to one in which
       we are trying to open a single compositing layer.  If this is also
       incompatible, we may downgrade the opening mode to one in which we
       are trying to open only a single image component of a single
       codestream.  This function is used if the client is created for
       non-interactive communication, or if the initial request used to
       connect to the server contains a non-trivial window of interest. */
  void update_client_window_of_interest(double cur_time=-1.0,
                                        double next_display_event_time=-1.0);
    /* Called if the compressed data source is a "kdu_client"
       object, whenever the viewing region and/or resolution may
       have changed.  Also called from `update_client_metadata_of_interest'.
       This function computes and passes information about the client's
       window of interest and/or metadata of interest to the remote server.
       The `cur_time' argument provides a way for the caller to supply the
       current time, if it is known, which may save this function from
       having to re-evaluate it.  Similarly, the `next_display_event_time'
       argument is used to pass in the next animation display event time
       if it is known, so that `compositor->get_display_event_times' need
       not be re-evaluated. */  
  void change_client_focus(kdu_coords actual_resolution,
                           kdu_dims actual_region);
    /* Called if the compressed data source is a `kdu_client' object,
       whenever the server's reply to a region of interest request indicates
       that it is processing a different region to that which was requested
       by the client.  The changes are reflected by redrawing the focus
       box, as necessary, while being careful not to issue a request for a
       new region of interest from the server in the process. */
// ----------------------------------------------------------------------------
private: // Functions used to schedule events in time
  void schedule_frame_advance(double when)
    { 
      if ((next_frame_advance_time >= 0.0) &&
          (next_frame_advance_time <= when)) return;
      next_frame_advance_time = when; install_next_scheduled_wakeup();
    }
  void schedule_render_refresh_wakeup(double when)
    { 
      if ((next_render_refresh_time >= 0.0) &&
          (next_render_refresh_time <= when)) return;
      next_render_refresh_time = when; install_next_scheduled_wakeup();
    }
  void schedule_metadata_refresh_wakeup(double when)
    { 
      if ((next_metadata_refresh_time >= 0.0) &&
          (next_metadata_refresh_time <= when)) return;
      next_metadata_refresh_time = when; install_next_scheduled_wakeup();
    }
    /* Each of the above functions is used to schedule a wakeup event for
       some time-based functionality.  The new time replaces any wakeup
       time currently scheduled for the relevant event (render-refresh,
       overlay-flash, frame-end or status-update, as appropriate).
       The `install_next_schedule_wakeup' function is invoked to actually
       schedule (or reschedule) a `wakeup' call for the earliest
       outstanding wakeup time, after any adjustments made by this function.
       Apart from frame-end wakeups (used in playback mode), the other wakeup
       times do not need to be very precise.  The relevant functions adjust
       these wakeup times if possible so as to minimize unnecessary wakeup
       calls.  Note that there is no function to schedule overlay change
       wakeups here, because that is always done directly from within the
       `configure_overlays' function. */
  void install_next_scheduled_wakeup();
    /* Service function used by the above functions and in other places, to
       compare the various wakeup times, find the most imminent one, and
       schedule a new `wakeup' call if necessary.  Note that wakeups based
       on the `next_overlay_change_time' and `next_render_refresh_time'
       variables are scheduled only if `animator' is NULL.  During animation,
       the only wakeup times of interest are `next_frame_push_time',
       and `next_metadata_refresh_time'. */
  double get_current_time();
    /* Just calls `manager->get_current_time'. */
//-----------------------------------------------------------------------------
public: // Functions related to video/animation playback
  void update_animation_status_info();
    /* Called from `kdws_window::display_notification' when the run-loop
       passes along notification of the display of one or more frames by
       the presentation thread. */
  void manage_animation_frame_queue(double cur_system_time=-1.0);
    /* This function is called from:
       a) `do_processing' when processing of an animation frame has just
          completed;
       b) `wakeup' when a scheduled animation frame advance event occurs; and
       c) `kdws_window::display_notification' when the run-loop passes along
          notification of the display of one or more frames by the
          presentation thread.
       The above cover all conditions under which a new animation frame
       might be ready to push onto the composition buffer queue, or a new
       call to 'advance_animation' might be appropriate.
          The function relies upon the frame presentation thread
       asynchronously using frames that are on the queue; it pops the head
       of the queue only if it has already been used.
          The function implements a policy in which the composition buffer
       queue never contains more than `animation_max_queued_frames' frames
       and the value returned by `animator->get_suggested_advance_delay'
       is <= 0.
          If `cur_system_time' is -ve, it must be evaluated (as required) by
       calling `get_current_time' from within the function.
    */
// ----------------------------------------------------------------------------
private: // Functions related to file saving
  void save_as(bool preserve_codestream_links, bool force_codestream_links);
  bool save_over(); // Saves over the existing file; returns false if failed.
  void save_as_jpx(const char *filename, bool preserve_codestream_links,
                   bool force_codestream_links);
  void save_as_jp2(const char *filename);
  void save_as_raw(const char *filename);
// ----------------------------------------------------------------------------
public: // Public data
  kdws_frame_window *window;
  kdws_image_view *image_view; // Null until the frame is created
  kdws_manager *manager;
  kdws_metashow *metashow; // NULL if not active
  kdws_catalog *catalog_source; // NULL if metadata catalog is not visible
  kdws_metadata_editor *editor; // NULL if metadata editor is not active
  bool catalog_closed; // True if catalog was explicitly closed
  int window_identifier;
  bool have_unsaved_edits;
// ----------------------------------------------------------------------------
private: // File and URL names retained by the `window_manager'
  bool processing_call_to_open_file; // Prevents recursive entry to `open_file'
  const char *open_file_pathname; // File we are reading; NULL for a URL
  const char *open_file_urlname; // JPIP URL we are reading; NULL if not JPIP
  const char *open_filename; // Points to filename within path or URL
  char *last_save_pathname; // Last file to which user saved anything
  bool save_without_asking; // If the user does not want to be asked in future
  kdws_renderer *duplication_src; // Non-NULL inside `open_as_duplicate'
// ----------------------------------------------------------------------------
private: // Compressed data source and rendering machinery
  bool allow_seeking;
  int error_level; // 0 = fast, 1 = fussy, 2 = resilient, 3 = resilient_sop
  kdu_simple_file_source file_in;
  kdu_cache cache_in; // If open, the cache is bound to a JPIP client
  jp2_threadsafe_family_src jp2_family_in;
  jpx_source jpx_in;
  mj2_source mj2_in;
  jpx_composition composition_rules; // Composition/rendering info if available
  kdws_region_compositor *compositor;
  kdu_thread_env thread_env; // Multi-threaded environment
  int num_threads; // If 0 use single-threaded machinery with `thread_env'=NULL
  int num_recommended_threads; // If 0, recommend single-threaded machinery
  bool in_idle; // Indicates we are inside the `OnIdle' function
  double defer_process_regions_deadline; // Used in non-playback mode to
    // determine whether `compositor->process' should be called with the
    // `KDU_COMPOSIT_DEFER_REGION' flag.  This deadline is used to
    // encourage processing and region aggregation over short time scales.
// ----------------------------------------------------------------------------
private: // JPIP state information
  kdu_client *jpip_client; // Currently NULL or points to `single_client'
  kdu_window client_roi; // Last posted/in-progress client-server window.
  kdu_window oob_metareqs; // Use to keep track of metareqs delivered OOB
  kdu_window tmp_roi; // Temp work space to avoid excessive new/delete usage
  kdu_window_prefs client_prefs;
  int client_request_queue_id;
  bool one_time_jpip_request; // True if `jpip_client'!=NULL & one-time-request
  bool respect_initial_jpip_request; // True if we have a non-trivial initial
                  // request and the configuration is not yet valid
  bool enable_region_posting; // True if focus changes can be sent to server
  bool animator_has_oob_metareqs; // If the `catalog' or `animator' has pending
  bool catalog_has_oob_metareqs; // high priority metadata requests (issed OOB)
  bool have_novel_anchor_metareqs; // If metareqs generated while looking for
      // catalog anchoring metadataq in `update_client_window...'
  bool jpip_wants_render_refresh; // See `render_refresh'
  kdu_long jpip_client_received_queue_bytes; // Used to check for new data for
  kdu_long jpip_client_received_total_bytes; // request queue and whole client
  kdu_long jpip_cache_metadata_bytes; // To check for new metadata in cache
  double jpip_refresh_interval; // Measured in seconds; depends on key status
  double jpip_metadata_refresh_interval; // As above but for textual metadata
// ----------------------------------------------------------------------------
private: // JPIP state information used only for `display_status'
  const char *jpip_client_status; // Status text last displayed
  kdu_long jpip_interval_start_bytes; // Bytes at start of rate interval
  double jpip_interval_start_time; // Active seconds at start of rate interval
  double jpip_client_data_rate; // Filtered local rate (bytes/sec) for queue
  double jpip_progress; // 0 to 100.  Computed when rendering completes.
  double jpip_last_displayed_progress; // Last value passed to progress bar
// ----------------------------------------------------------------------------
private: // External file state management (for editing and saving changes)
  kdws_file_services *file_server; // Created on-demand for metadata editing
// ----------------------------------------------------------------------------
private: // Modes and status which may change while the image is open -- these
         // all relate to the working state of the `compositor' object, as
         // opposed to queued composition buffers
  int max_display_layers;
  kdws_orientation orientation; // Current geometry flags.
  float min_rendering_scale; // Negative if unknown
  float max_rendering_scale; // Negative if unknown
  float rendering_scale;
  int single_component_idx; // Negative when performing a composite rendering
  int max_components; // -ve if not known
  int single_codestream_idx; // Used with `single_component'
  int max_codestreams; // -ve if not known
  int single_layer_idx; // For rendering one layer at a time; -ve if not in use
  int max_compositing_layer_idx; // -ve if not known
  kdu_uint32 track_idx; // JPX or MJ2
  kdu_uint32 max_track_idx; // JPX or MJ2
  bool num_tracks_known; // False until we know the actual value
  int frame_idx; // Index of current frame (JPX or MJ2)
  int num_frames; // Number we actually know to exist, for the current track
  bool num_frames_known; // False until we know all frames in the track
  double frame_start; // Start time in seconds (JPX or MJ2 frames)
  double frame_end; // End time in seconds (JPX or MJ2 frames)
  jpx_frame frame; // Empty interface if not rendering composited frames
  jpx_frame_expander frame_expander; // Used to find frame membership
  bool configuration_complete; // If required compositing layers are installed
  bool fit_scale_to_window; // Used when opening file for first time
  kds_status_id status_id; // Info currently displayed in status window
  jpx_metanode pending_show_metanode; // See `show_imagery_for_metanode'
  jpx_metanode last_show_metanode; // See `show_imagery_for_metanode'
// ----------------------------------------------------------------------------
private: // State information for video/animation playback services
  kdws_animation_bar *animation_bar; // Non-NULL if animation is supported
  kdws_animation_bar *metamation_bar; // Instantiated if animation_bar is NULL
  kdu_region_animator *animator; // NULL if there is no video/animation
  bool animation_repeat; // Preserves repeat mode even if `animator' is NULL
  bool animation_uses_native_timing; // Use native frame rate
  bool animation_skip_undisplayables; // See `animator->advance_animation'
  double animation_custom_rate; // Custom frame rate for animations
  double animation_native_rate_multiplier; // Applied to native frame rate
  bool pushed_last_frame; // Last animation frame pushed to composition queue

  double animation_frame_status_update_time;    // These quantities control the
  double animation_metanode_status_update_time; // times at which status values
  double animation_jpip_status_update_time;     // can be re-displayed so that
  int animation_display_idx; // For `display_status'; -1 if no frame presented
  jpx_metanode animation_display_metanode; // Last displayed frame's metanode
  double animation_display_jpip_progress; // Last displayed frame's progress
  bool animation_status_invalid; // True if `update_animation_status_info' is
          // required to update the status bar, regardless of whether the
          // frame index, JPIP progress or JPX metanode appear to have changed.
          // This flag is set when something happens to affect the displayed
          // zoom factor, for example.

  int animation_max_queued_frames; // Max size of jitter-absorbtion queue
  jpx_metanode animation_metanode; // Working frame's metanode, if any
  bool animation_metanode_shows_box; // If draw any metanode's bounding box
  bool animation_advancing; // Prevents recursive calls to `advance_animation'
  bool animation_frame_started; // If rendering has started on current frame
  bool animation_frame_complete; // If current animation frame already rendered
  bool animation_frame_needs_push; // If waiting to push completed frame
  
  double animation_last_sys_time;
  float animation_border_x;
  float animation_border_y;
  kdws_frame_presenter *frame_presenter; // Paints frames from separate thread
// ----------------------------------------------------------------------------
private: // Image and viewport dimensions -- these all relate to the working
         // state of the `compositor' object, as opposed to queued buffers
  int pixel_scale; // Number of display pels per image pel
  int quality_limiting_mode; // 1 for irrev, 2 for rev+irrev, 0 for no limiting
  kdu_dims image_dims; // Dimensions & location of complete composed image
  kdu_dims working_buffer_dims; // Uses same coordinate system as `image_dims'
  kdu_dims view_dims; // Uses same coordinate system as `image_dims'
  double view_centre_x, view_centre_y;
  bool view_centre_known;
  bool view_dims_change_deferred; // If `view_dims_changed' needs to be called
      // again from `advance_animation' -- a prior call to the
      // `view_dims_changed' function decided not to interrupt rendering of
      // an animation frame, since that would prevent any updates from reaching
      // the screen.
// ----------------------------------------------------------------------------
private: // Focus parameters
  bool highlight_focus; // If true, focus region is highlighted.
  jpx_metanode focus_metanode; // If focus was derived from metadata
  kdws_rel_focus rel_focus; // Describes any focus box relative to the imagery
    // on which it sits, so it can be re-created (or removed) after navigation.
    // If !rel_focus.is_valid(), there is no focus box.
  CPen manual_focus_pens[2];
  CPen metadata_focus_pens[2];
  CPen focussing_pens[3];
// ----------------------------------------------------------------------------
private: // Overlay parameters
  bool overlays_enabled;
  bool overlays_auto_flash; // Flash on still frame; no flash during animation
  int overlay_flashing_state; // 0=none; 1=getting brigher; -1=getting darker
  float overlay_nominal_intensity; // Basic value we get active intensity from
  int overlay_size_threshold; // Min composited size for an overlay to be shown
  jpx_metanode overlay_restriction;
  jpx_metanode last_selected_overlay_restriction;
  kdu_uint32 overlay_params[KDWS_NUM_OVERLAY_PARAMS];
  int overlay_highlight_state; // 0 if not flashing imagery for metanode
  jpx_metanode highlight_metanode;
// ----------------------------------------------------------------------------
private: // Image buffer and painting resources
  kdws_compositor_buf *working_buffer; // Working buffer of `compositor' object
  CDC compatible_dc; // Compatible DC for use with BitBlt function
  kdu_coords strip_extent; // Dimensions of strip buffer
  HBITMAP stripmap; // Manages strip buffer for indirect painting
  BITMAPINFO stripmap_info; // Used in calls to CreateDIBSection.
  kdu_uint32 *stripmap_buffer; // Buffer managed by `stripmap' object.
  kdu_byte foreground_lut[256];
  kdu_byte background_lut[256];  
  jpx_roi_editor *shape_editor;  // If these members are non-null shape editing
  kdu_istream_ref shape_istream; // is in progress; image display is dimmed and
                                 // `kdws_window' uses `get_shape_anchor' and
                                 // `get_shape_edge' to draw the shape.
  bool hide_shape_path; // If true, `get_shape_path' always returns 0.
  bool shape_scribbling_mode;
  int fixed_shape_path_thickness; // If >0, path thickness set after each move  
// ----------------------------------------------------------------------------
private: // State information for scheduled activities
  double earliest_render_refresh_time; // see `render_refresh'
  double last_metadata_refresh_time; // Last time we scanned for new metadata
  double next_render_refresh_time; // Time for next re-rendering to occur
  double next_metadata_refresh_time; // Time for next scan for new metadata
  double next_overlay_change_time; // Time for next change in ovly (or -ve)
  double next_frame_advance_time; // Used only during animation
  double next_scheduled_wakeup_time; // -ve if no wakeup is scheduled
// ----------------------------------------------------------------------------
public: // File Menu Handlers
	afx_msg void menu_FileDisconnect();
	afx_msg void can_do_FileDisconnect(CCmdUI* pCmdUI)
    { pCmdUI->Enable((jpip_client != NULL) &&
                     jpip_client->is_alive(client_request_queue_id)); }
  afx_msg void menu_FileReconnect();
  afx_msg void can_do_FileReconnect(CCmdUI* pCmdUI)
    { pCmdUI->Enable((jpip_client != NULL) &&
                     !(jpip_client->is_alive(client_request_queue_id) ||
                       jpip_client->target_incompatible())); }
  void note_jpip_client_reconnecting(kdu_client *client);
    /* Does the work of `kdws_window:note_jpip_client_reconnecting'. */
  afx_msg void menu_FileEraseAndReconnect();
  afx_msg void can_do_FileEraseAndReconnect(CCmdUI* pCmdUI)
    { pCmdUI->Enable((jpip_client != NULL) &&
                     jpip_client->target_incompatible()); }
  afx_msg void menu_FileJPIPSaveMode();
  afx_msg void can_do_FileJPIPSaveMode(CCmdUI* pCmdUI);
  afx_msg void menu_FileJPIPUnsavedMode();
  afx_msg void can_do_FileJPIPUnsavedMode(CCmdUI* pCmdUI);
	afx_msg void menu_FileSaveAs();
	afx_msg void can_do_FileSaveAs(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (animator == NULL)); }
  afx_msg void menu_FileSaveAsLinked();
  afx_msg void can_do_FileSaveAsLinked(CCmdUI *pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (animator == NULL) &&
                     jp2_family_in.exists() && !jp2_family_in.uses_cache()); }
  afx_msg void menu_FileSaveAsEmbedded();
  afx_msg void can_do_FileSaveAsEmbedded(CCmdUI *pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (animator == NULL)); }
  afx_msg void menu_FileSaveReload();
  afx_msg void can_do_FileSaveReload(CCmdUI *pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && have_unsaved_edits &&
                 (open_file_pathname != NULL) && (!mj2_in.exists()) &&
                 (animator == NULL)); }
	afx_msg void menu_FileProperties();
	afx_msg void can_do_FileProperties(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (animator == NULL)); }
// ----------------------------------------------------------------------------
public: // View Menu Handlers (orientation and zoom)
	afx_msg void menu_ViewHflip();
	afx_msg void can_do_ViewHflip(CCmdUI* pCmdUI)
    { pCmdUI->Enable(working_buffer != NULL); }
	afx_msg void menu_ViewVflip();
	afx_msg void can_do_ViewVflip(CCmdUI* pCmdUI)
    { pCmdUI->Enable(working_buffer != NULL); }
	afx_msg void menu_ViewRotate();
	afx_msg void can_do_ViewRotate(CCmdUI* pCmdUI)
    { pCmdUI->Enable(working_buffer != NULL); }
	afx_msg void menu_ViewCounterRotate();
	afx_msg void can_do_ViewCounterRotate(CCmdUI* pCmdUI)
    { pCmdUI->Enable(working_buffer != NULL); }
	afx_msg void menu_ViewZoomOut();
	afx_msg void can_do_ViewZoomOut(CCmdUI* pCmdUI)
    { pCmdUI->Enable((working_buffer != NULL) &&
                     ((min_rendering_scale < 0.0F) ||
                     (rendering_scale > min_rendering_scale))); }
	afx_msg void menu_ViewZoomIn();
	afx_msg void can_do_ViewZoomIn(CCmdUI* pCmdUI)
    { pCmdUI->Enable(working_buffer != NULL); }
	afx_msg void menu_ViewZoomOutSlightly();
	afx_msg void menu_ViewZoomInSlightly();
  afx_msg void menu_ViewOptimizescale();
  afx_msg void can_do_ViewOptimizescale(CCmdUI *pCmdUI)
    {  pCmdUI->Enable((compositor != NULL) && configuration_complete); }
	afx_msg void menu_ViewRestore();
	afx_msg void can_do_ViewRestore(CCmdUI* pCmdUI)
    { pCmdUI->Enable((working_buffer!=NULL) && !orientation.is_original()); }
	afx_msg void menu_ViewRefresh();
	afx_msg void can_do_ViewRefresh(CCmdUI* pCmdUI)
    { pCmdUI->Enable(compositor != NULL); }
// ----------------------------------------------------------------------------
public: // View Menu Handlers (quality layers)
  afx_msg void menu_LayersLess();
	afx_msg void can_do_LayersLess(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) &&
                     ((quality_limiting_mode < 2) ||
                      (max_display_layers > 1))); }
	afx_msg void menu_LayersMore();
	afx_msg void can_do_LayersMore(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) &&
                     ((quality_limiting_mode > 0) ||
                      (max_display_layers <
                       compositor->get_max_available_quality_layers()))); }
// ----------------------------------------------------------------------------
public: // View Menu Handlers (status display)
	afx_msg void menu_StatusToggle();
	afx_msg void can_do_StatusToggle(CCmdUI* pCmdUI)
    { pCmdUI->Enable(compositor != NULL); }
// ----------------------------------------------------------------------------
public: // View Menu Handlers (image window)
	afx_msg void menu_ViewWiden();
	afx_msg void can_do_ViewWiden(CCmdUI* pCmdUI)
    { pCmdUI->Enable((working_buffer == NULL) ||
                     ((view_dims.size.x < image_dims.size.x) ||
                      (view_dims.size.y < image_dims.size.y))); }
	afx_msg void menu_ViewShrink();
	afx_msg void can_do_ViewShrink(CCmdUI* pCmdUI)
    { pCmdUI->Enable(TRUE); }
  afx_msg void menu_ViewScaleX2();
  afx_msg void menu_ViewScaleX1();
// ----------------------------------------------------------------------------
public: // View Menu Handlers (navigation)
	afx_msg void menu_NavComponent1();
	afx_msg void can_do_NavComponent1(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (single_component_idx == 0) &&
                     (animator == NULL));
      pCmdUI->SetCheck((compositor != NULL) && (single_component_idx != 0)); }
	afx_msg void menu_NavComponentNext();
	afx_msg void can_do_NavComponentNext(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (animator == NULL) &&
                     ((max_components < 0) ||
                      (single_component_idx < (max_components-1)))); }
	afx_msg void menu_NavComponentPrev();
	afx_msg void can_do_NavComponentPrev(CCmdUI* pCmdUI)
    {  pCmdUI->Enable((compositor != NULL) && (single_component_idx != 0)); }
  afx_msg void menu_NavCompositingLayer();
	afx_msg void can_do_NavCompositingLayer(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && (single_layer_idx < 0) &&
                     (animator == NULL));
      pCmdUI->SetCheck((compositor != NULL) && (single_layer_idx >= 0)); }
	afx_msg void menu_NavMultiComponent();
	afx_msg void can_do_NavMultiComponent(CCmdUI* pCmdUI);
  afx_msg void menu_NavTrackNext();
  afx_msg void can_do_NavTrackNext(CCmdUI *pCmdUI)
    { pCmdUI->Enable((compositor != NULL) &&
                     ((max_track_idx > 1) || !num_tracks_known)); }
	afx_msg void menu_NavImageNext();
	afx_msg void can_do_NavImageNext(CCmdUI* pCmdUI);
	afx_msg void menu_NavImagePrev();
	afx_msg void can_do_NavImagePrev(CCmdUI* pCmdUI);
// ----------------------------------------------------------------------------
public: // Focus Menu Handlers
	afx_msg void menu_FocusOff();
	afx_msg void can_do_FocusOff(CCmdUI* pCmdUI)
    { pCmdUI->Enable(rel_focus.is_valid() || animation_metanode_shows_box); }
	afx_msg void menu_FocusHighlighting();
	afx_msg void can_do_FocusHighlighting(CCmdUI* pCmdUI)
    {  pCmdUI->SetCheck(highlight_focus); }
	afx_msg void menu_FocusWiden();
	afx_msg void menu_FocusShrink();
	afx_msg void menu_FocusLeft();
	afx_msg void menu_FocusRight();
	afx_msg void menu_FocusUp();
	afx_msg void menu_FocusDown();
// ----------------------------------------------------------------------------
public: // Mode Menu Handlers
 	afx_msg void menu_ModeFast();
	afx_msg void can_do_ModeFast(CCmdUI* pCmdUI);
	afx_msg void menu_ModeFussy();
	afx_msg void can_do_ModeFussy(CCmdUI* pCmdUI);
	afx_msg void menu_ModeResilient();
	afx_msg void can_do_ModeResilient(CCmdUI* pCmdUI);
	afx_msg void menu_ModeResilientSop();
	afx_msg void can_do_ModeResilientSop(CCmdUI* pCmdUI);
	afx_msg void menu_ModeSeekable();
	afx_msg void can_do_ModeSeekable(CCmdUI* pCmdUI);
  afx_msg void menu_ModeSinglethreaded();
  afx_msg void can_do_ModeSinglethreaded(CCmdUI *pCmdUI);
  afx_msg void menu_ModeMultithreaded();
  afx_msg void can_do_ModeMultithreaded(CCmdUI *pCmdUI);
  afx_msg void menu_ModeMorethreads();
  afx_msg void can_do_ModeMorethreads(CCmdUI *pCmdUI);
  afx_msg void menu_ModeLessthreads();
  afx_msg void can_do_ModeLessthreads(CCmdUI *pCmdUI);
  afx_msg void menu_ModeRecommendedThreads();
  afx_msg void can_do_ModeRecommendedThreads(CCmdUI *pCmdUI);
// ----------------------------------------------------------------------------
public: // Metadata Menu Handlers
  afx_msg void menu_MetadataCatalog();
  afx_msg void can_do_MetadataCatalog(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck(catalog_source != NULL);
      pCmdUI->Enable((catalog_source != NULL) || jpx_in.exists()); }
  afx_msg void menu_MetadataSwapFocus();
  afx_msg void can_do_MetadataSwapFocus(CCmdUI *pCmdUI)
    { pCmdUI->Enable((catalog_source != NULL) || jpx_in.exists()); }
  afx_msg void menu_MetadataShow();
	afx_msg void can_do_MetadataShow(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck(metashow != NULL);
      pCmdUI->Enable(true); }
  afx_msg void menu_MetadataAdd();
  afx_msg void can_do_MetadataAdd(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     ((jpip_client == NULL) || !jpip_client->is_alive(-1))); }
  afx_msg void menu_MetadataEdit();
  afx_msg void can_do_MetadataEdit(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists()); }
  afx_msg void menu_MetadataDelete();
  afx_msg void can_do_MetadataDelete(CCmdUI* pCmdUI);
  afx_msg void menu_MetadataCopyLabel();
  afx_msg void can_do_MetadataCopyLabel(CCmdUI* pCmdUI);
  afx_msg void menu_MetadataCopyLink();
  afx_msg void can_do_MetadataCopyLink(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists()); }
  afx_msg void menu_MetadataCut();
  afx_msg void can_do_MetadataCut(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists()); }
  afx_msg void menu_MetadataPasteNew();
  afx_msg void can_do_MetadataPasteNew(CCmdUI* pCmdUI);
  afx_msg void menu_MetadataDuplicate();
  afx_msg void can_do_MetadataDuplicate(CCmdUI *pCmdUI);
  afx_msg void menu_MetadataUndo();
  afx_msg void can_do_MetadataUndo(CCmdUI * pCmdUI)
    { if (shape_editor == NULL) { pCmdUI->Enable(FALSE); return; }
      int undo_elts; bool can_redo;
      shape_editor->get_history_info(undo_elts,can_redo);
      pCmdUI->Enable(undo_elts > 0); }
  afx_msg void menu_MetadataRedo();
  afx_msg void can_do_MetadataRedo(CCmdUI * pCmdUI)
    { if (shape_editor == NULL) { pCmdUI->Enable(FALSE); return; }
      int undo_elts; bool can_redo;
      shape_editor->get_history_info(undo_elts,can_redo);
      pCmdUI->Enable(can_redo); }
  afx_msg void menu_OverlayEnable();
  afx_msg void can_do_OverlayEnable(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck((compositor != NULL) && jpx_in.exists() &&
                       overlays_enabled);
      pCmdUI->Enable((compositor != NULL) && jpx_in.exists()); } 
  afx_msg void menu_OverlayFlashing();
  afx_msg void can_do_OverlayFlashing(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck((compositor != NULL) && jpx_in.exists() &&
                       overlay_flashing_state && overlays_enabled);
      pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled); }
  afx_msg void menu_OverlayAutoFlash();
  afx_msg void can_do_OverlayAutoFlash(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck((compositor != NULL) && jpx_in.exists() &&
                       overlays_auto_flash && overlays_enabled);
      pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled); }
  afx_msg void menu_OverlayToggle();
  afx_msg void can_do_OverlayToggle(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists()); }
  afx_msg void menu_OverlayRestrict();
  afx_msg void can_do_OverlayRestrict(CCmdUI* pCmdUI);
  afx_msg void menu_OverlayLighter();
  afx_msg void can_do_OverlayLighter(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled); }
  afx_msg void menu_OverlayHeavier();
  afx_msg void can_do_OverlayHeavier(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled); }
  afx_msg void menu_OverlayDoubleSize();
  afx_msg void can_do_OverlayDoubleSize(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled); }
  afx_msg void menu_OverlayHalveSize();
  afx_msg void can_do_OverlayHalveSize(CCmdUI* pCmdUI)
    { pCmdUI->Enable((compositor != NULL) && jpx_in.exists() &&
                     overlays_enabled && (overlay_size_threshold > 1)); }
// ----------------------------------------------------------------------------
public: // Play Menu Handlers
  bool source_supports_animation()
    { return ((compositor != NULL) &&
              (mj2_in.exists() && ((num_frames != 1) || !num_frames_known)) ||
              (jpx_in.exists() &&
               (((num_frames != 1) || !num_frames_known) ||
                (metamation_bar != NULL) ||
                ((single_layer_idx >= 0) &&
                 (max_compositing_layer_idx != 0))))); }
  afx_msg void menu_PlayStartForward()
    { 
      if (animator == NULL) start_animation(false);
      if ((animator != NULL) && animator->get_reverse())
        { double nxt_event; compositor->get_display_event_times(nxt_event);
          animator->set_reverse(false,nxt_event); refresh_display();
          update_animation_bar_state(); }
    }
	afx_msg void can_do_PlayStartForward(CCmdUI* pCmdUI)
    { pCmdUI->Enable(source_supports_animation() &&
                     (single_component_idx < 0) &&
                     ((animator == NULL) || animator->get_reverse())); }
  afx_msg void menu_PlayStartBackward()
    { 
      if (animator == NULL) start_animation(true);
      if ((animator != NULL) && !animator->get_reverse())
        { double nxt_event; compositor->get_display_event_times(nxt_event);
          animator->set_reverse(true,nxt_event); refresh_display();
          update_animation_bar_state(); }
    }
	afx_msg void can_do_PlayStartBackward(CCmdUI* pCmdUI)
    { pCmdUI->Enable(source_supports_animation() &&
                     (single_component_idx < 0) &&
                     ((animator == NULL) || !animator->get_reverse())); }
  afx_msg void menu_PlayStop() { stop_animation(); }
	afx_msg void can_do_PlayStop(CCmdUI* pCmdUI)
    { pCmdUI->Enable(animator != NULL); }
  afx_msg void menu_PlayRenderAll()
    { animation_skip_undisplayables = !animation_skip_undisplayables; }
  afx_msg void can_do_PlayRenderAll(CCmdUI* pCmdUI)
    { pCmdUI->Enable(source_supports_animation() && (single_component_idx<0));
      pCmdUI->SetCheck(!animation_skip_undisplayables); }
  afx_msg void menu_PlayRepeat()
    { 
      animation_repeat = !animation_repeat;
      if ((animator != NULL) && (animation_repeat != animator->get_repeat()))
        { 
          animator->set_repeat(animation_repeat);
          refresh_display();
        }
      update_animation_bar_state();
    }
  afx_msg void can_do_PlayRepeat(CCmdUI* pCmdUI)
    { pCmdUI->SetCheck(animation_repeat);
      pCmdUI->Enable(source_supports_animation()); }
  afx_msg void menu_PlayNative();
  afx_msg void can_do_PlayNative(CCmdUI* pCmdUI);
  afx_msg void menu_PlayCustom();
  afx_msg void can_do_PlayCustom(CCmdUI* pCmdUI);  
  afx_msg void menu_PlayFrameRateUp();
  afx_msg void can_do_PlayFrameRateUp(CCmdUI* pCmdUI)
    { pCmdUI->Enable(source_supports_animation()); }
  afx_msg void menu_PlayFrameRateDown();
  afx_msg void can_do_PlayFrameRateDown(CCmdUI* pCmdUI)
    { pCmdUI->Enable(source_supports_animation()); }
// ----------------------------------------------------------------------------
public: // Window Menu Handlers -- this one not actually a message handler
	bool can_do_WindowDuplicate()
    { return (compositor != NULL); }

	DECLARE_MESSAGE_MAP()
};

#endif // KDU_SHOW_H
