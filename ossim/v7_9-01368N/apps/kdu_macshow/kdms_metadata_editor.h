/*****************************************************************************/
// File: kdms_metadata_editor.h [scope = APPS/MACSHOW]
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
    Defines the `kdms_metadata_editor' Objective-C class, which implements a
dialog for adding and editing simple metadata boxes, which may be associated
with regions of interest.  This class goes hand in hand with the dialog
window and associated controls, defined in the "metadata_editor.nib" file.
******************************************************************************/

#import <Cocoa/Cocoa.h>
#import "kdms_controller.h"
#include "kdms_renderer.h"

// Defined here:
struct kdms_matching_metanode;
class kdms_matching_metalist;
struct kdms_box_template;
class kdms_metanode_edit_state;
class kdms_metadata_dialog_state;
@class kdms_metadata_editor;

/*****************************************************************************/
/*                           kdms_matching_metanode                          */
/*****************************************************************************/

struct kdms_matching_metanode {
  jpx_metanode node;
  kdms_matching_metanode *next;
  kdms_matching_metanode *prev;
};

/*****************************************************************************/
/*                           kdms_matching_metalist                          */
/*****************************************************************************/

class kdms_matching_metalist {
  /* This class is used to build a list of jpx_metanodes to pass
     to the medadata editor.  From version 7.0, the list can contain
     numlist nodes explicitly.  A metalist is a set of metadata nodes
     that are not descended from other metadata nodes in the same list
     and that are relevant to some editing operation.  For example, if
     the user clicks on a location within the image, the editor might
     be launched and passed a list of all region-of-interest metadata
     nodes that contain the location that was clicked. */
public: // Member functions
  kdms_matching_metalist() { head = tail = NULL; }
  ~kdms_matching_metalist()
    { while ((tail=head) != NULL) { head=tail->next; delete tail; } }
  kdms_matching_metanode *find_container(jpx_metanode node);
    /* Searches for an existing element in the list, which is either equal
     to `node' or an acestor of `node'.  Returns NULL if none can be found. */
  kdms_matching_metanode *find_member(jpx_metanode node);
    /* Searches for an existing element in the list, which is either equal
     to `node' or a descendant of `node'.  Returns NULL if none can be
     found. */
  void delete_item(kdms_matching_metanode *entry);
    /* Deletes `entry' from the list managed by the object. */
  kdms_matching_metanode *append_node(jpx_metanode node);
    /* Creates a new `kdms_matching_metanode' item to hold `node' and
     appends it to the tail of the internal list, returning a pointer to
     the created item.  If the metalist already contains the node or
     one of its ancestors, the function adds nothing, returning a
     pointer to the existing entry. */
  kdms_matching_metanode *get_head() { return head; }
private: // Data
  kdms_matching_metanode *head, *tail;
};

/*****************************************************************************/
/*                             kdms_box_template                             */
/*****************************************************************************/

struct kdms_box_template {
  kdu_uint32 box_type;
  char box_type_string[5]; // 4-character code in printable form
  const char *file_suffix; // Always a non-empty string
  const char *initializer; // NULL for binary external file representations
public: // Convenience initializer
  void init(kdu_uint32 box_type, const char *suffix, const char *initializer)
    { 
      this->box_type=box_type; this->initializer=initializer;
      if (box_type == 0)
        { 
          strcpy(box_type_string,"root");
          this->file_suffix = "";
        }
      else
        { 
          box_type_string[0] = make_4cc_char((char)(box_type>>24));
          box_type_string[1] = make_4cc_char((char)(box_type>>16));
          box_type_string[2] = make_4cc_char((char)(box_type>>8));
          box_type_string[3] = make_4cc_char((char)(box_type>>0));
          box_type_string[4] = '\0';
          this->file_suffix = (suffix==NULL)?box_type_string:suffix;
        }
    }
private: // Convenience function
  char make_4cc_char(char ch)
    {
      if (ch == ' ')
        return '_';
      if ((ch < 0x20) || (ch & 0x80))
        return '.';
      return ch;
    }
};

// Indices into an array of templates:
#define KDMS_LABEL_TEMPLATE 0
#define KDMS_XML_TEMPLATE 1
#define KDMS_IPR_TEMPLATE 2
#define KDMS_CUSTOM_TEMPLATE 3 // Used if original box type none of above
#define KDMS_NUM_BOX_TEMPLATES (KDMS_CUSTOM_TEMPLATE+1)

/*****************************************************************************/
/*                          kdms_metanode_edit_state                         */
/*****************************************************************************/

class kdms_metanode_edit_state {
public: // Member functions
  kdms_metanode_edit_state(jpx_source *source,
                           kdms_file_services *file_server);
  bool at_root() { return cur_node==root_node; }
  void move_to_parent();
  void move_to_next();
  void move_to_prev();
  void move_to_descendants();
  void move_to_node(jpx_metanode node);
  void delete_cur_node(kdms_renderer *renderer);
    /* Deletes the `cur_node' and the JPX data source and makes all required
     adjustments in order to obtain new valid `cur_node', `root_node' and
     `edit_item' values, if possible.  If there is no relevant new node,
     the function returns with `cur_node' empty.  The `renderer' object
     is provided so that its `metadata_changed' function can be called once
     the node has been deleted. */
  void add_child_node(kdms_renderer *renderer);
    /* Adds a descendant to the current node and adjusts the `cur_node'
     member to reference the new child.  The `renderer' object is
     provided so that its `metadata_changed' function can be called once
     the child has been added.  The new descendant is set to be a label
     node, with an initial text string equal to "<new label>". */
  void add_parent_node(kdms_renderer *renderer);
    /* Similar to `add_child_node' except that a new parent of the current
     node is added, having initial label text "<new parent>". */
  void validate_metalist_and_root_node();
    /* This function is called after a node has (or may have) been deleted.
       It deals with the fact that the deletion of one node may cause other
       nodes to be deleted; not just descendants, but also nodes which link
       to the deleted node, their descendants and so forth.  The function
       removes dead nodes from the edit list and adjusts `edit_item' and
       `root_node' accordingly.  It does not touch `cur_node' since that
       will be manipulated by the caller, who is in the process of deleting
       `cur_node'. */
public: // Data
  jpx_source *source;
  jpx_meta_manager meta_manager; // Recovered from `source'
  kdms_matching_metalist *metalist;
  kdms_matching_metanode *edit_item; // Item from edit list; either this node
            // is being edited or else one of its descendants is being edited.
  jpx_metanode root_node; // Equals `edit_item->node'
  jpx_metanode cur_node; // Either `root_node' or one of its descendants
  jpx_roi_editor active_shape_editor; // For actual interactive shape editing
private: // Private data
  kdms_file_services *file_server; // Provided by the constructor
};

/*****************************************************************************/
/*                        kdms_metadata_dialog_state                         */
/*****************************************************************************/

class kdms_metadata_dialog_state {
public: // Functions
  kdms_metadata_dialog_state(int num_codestreams, int num_layers,
                             kdms_box_template *available_types,
                             kdms_file_services *file_server,
                             bool allow_edits);
  ~kdms_metadata_dialog_state();
  bool compare_region_associations(kdms_metadata_dialog_state *ref);
    /* Returns true if the ROI associations of this
       object are the same as those of the `ref' object. */
  bool compare_image_entity_associations(kdms_metadata_dialog_state *ref);
    /* Returns true if the image entity associations (codestreams, layers
       or rendered result) are the same as those of the `ref' object. */
  bool compare_contents(kdms_metadata_dialog_state *ref);
    /* Returns true if the box-type, label-string and any other node content
       attributes are the same as those of the `ref' object. */
  bool compare(kdms_metadata_dialog_state *ref)
    { return (compare_image_entity_associations(ref) &&
              compare_region_associations(ref) && compare_contents(ref)); }
  int add_selected_codestream(int idx, jpx_source *source);
    /* Augments the `selected_codestreams' list.  Returns -ve if `idx' was
       already in the list or if the caller responded to a warning message
       by opting to cancel the operation.  If `source' is non-NULL, the
       function checks for compatibility with JPX containers.  In this
       case, a warning message is generated if a non-negative
       `selected_container_id' is about to become negative as a result of
       the operation, allowing the user to abort if desired.  If `source'
       is NULL, `potential_container_id' and `selected_container_id' are both
       forced to -1.  If successful, the function returns
       the entry in the `selected_codestream' array which contains the
       new codestream index.  If `selected_container_id' is left equal to
       -1 as a result of this call, the caller should generally invoke
       `find_potential_container_id' to determine whether the collection
       of selected codestreams and compositing layers is compatible with
       any JPX container. */
  int add_selected_layer(int idx, jpx_source *source);
    /* As above, but for compositing layers. */
  void find_potential_container_id(jpx_source *source);
    /* Searches through the collection of JPX containers in `source' to
       determine if there is one that is uniquely compatible with the
       current set of selected codestreams and compositing layers, setting
       the `potential_container_id' member accordingly. */
  void select_potential_container_id(bool select, jpx_source *source);
    /* Use this function to modify `selected_container_id' rather than
       directly setting it yourself.  If `select' is true, the
       `selected_container_id' value is set equal to `potential_container_id'.
       Otherwise, `selected_container_id' is set to -1.  Along the way,
       the function configures the other `selected_container_...' members. */
  void configure_with_cur_node(kdms_metanode_edit_state *edit);
    /* Sets up the internal member values based on the information in
       `edit->cur_node'. */
  void copy(kdms_metadata_dialog_state *src);
    /* Copy contents from `src'. */
  kdms_file *get_external_file() { return external_file; }
  void set_external_file(kdms_file *file, bool new_file_just_retained=false)
    { // Manages the release/retain calls required to ensure that external
      // file resources will be properly cleaned up when not required.  The
      // `new_file_just_retained' argument should be true only if the file
      // has just been obtained using `kdms_file_services::retain...'.
      if ((this->external_file == file) && !new_file_just_retained) return;
      if (external_file != NULL) external_file->release();
      external_file = file;
      if ((file != NULL) && !new_file_just_retained)
        file->retain();
    }
  void set_external_file_replaces_contents()
    { if (external_file != NULL) external_file_replaces_contents = true; }
  bool save_to_external_file(kdms_metanode_edit_state *edit);
    /* Creates a temporary external file if necessary.  Saves the contents
     of the label string, or `edit->cur_node' to the file.  Returns false
     if nothing could be saved for some reason (e.g., the file was locked
     or there was nothing to save because a current node does not exist or
     already has an associated file. */
  bool internalize_label_node();
    /* Reads back the label text from an external file.  Returns false
     if this cannot be done or the current node does not have an associated
     external file.  If it can be done, the file is released
     and the current node is replaced with an internalized label node,
     rather than a delayed label node and the function returns true. */
  bool save_metanode(kdms_metanode_edit_state *edit_state,
                     kdms_metadata_dialog_state *initial_state,
                     kdms_renderer *renderer);
    /* Uses the information contained in the object's members to create
     a new metadata node.  The new metanode becomes the new value of
     `edit_state->cur_node' and, if appropriate, `edit_state->root_node'.
        If `edit_state->cur_node' exists on entry, that node is changed to
     the new node; this may require deletion of the existing node or moving
     it to a different parent.
        The `initial_state' object is compared against the current object
     to determine whether or not there are changes that need to be saved.
        If the numlist or ROI associations have changed, the
     `jpx_meta_manager::insert_node' function is used to create a new parent
     to hold the node which is being created (or to create the new node itself
     if it is an ROI description box node).  When this happens, the new node
     (or its parent) is appended to the edit list as a new top-level matching
     metanode and the `edit_state' members are all adjusted to reflect the
     new structure.
        The function returns false if the process could not be performed
     because some inappropriate condition was detected (and flagged to the
     user).  In this case, no changes are made.  Otherwise, the function
     returns true, even if there are no changes to save.
        The `renderer' object's `metadata_changed' function is invoked
     if any changes are made in the metadata structure.  If metadata is
     added only, the function is invoked with its `new_data_only'
     argument equal to true. */
  void set_label_string(const char *string)
    {
      if (label_string != NULL)
        { delete[] label_string; label_string = NULL; }
      if (string == NULL) return;
      while ((*string == '\n') || (*string == ' ') ||
           (*string == '\r') || (*string == '\t'))
        string++;
      label_string = new char[strlen(string)+1];
      strcpy(label_string,string);
    }
  const char *get_label_string()
    { return (label_string != NULL)?label_string:""; }
  bool is_label_string_empty()
    { return ((label_string == NULL) || (*label_string == '\0')); }
private: // Helper functions
  void augment_max_selected_codestreams(int min_capacity);
  void augment_max_selected_layers(int min_capacity);
public: // Data members describing associations for the node
  int num_codestreams; // Provided by the constructor
  int num_layers; // Provided by the constructor
  int max_selected_codestreams; // Capacity of `selected_codestreams' array
  int num_selected_codestreams;
  int *selected_codestreams; // Indices of selected codestreams
  int max_selected_layers; // Capacity of `selected_layers' array
  int num_selected_layers;
  int *selected_layers; // Indices of selected compositing layers
  int selected_container_id; // -1 or container we are associated with
  int potential_container_id; // Any container compatible with layers & streams
  int selected_container_min_base_stream; // See below
  int selected_container_min_base_layer; // See below
  int selected_container_num_base_streams; // See below
  int selected_container_num_base_layers; // See below
  bool allow_edits; // Equals `kdms_metadata_editor:allow_edits'
  bool is_link; // If the node is a link -- can't edit actual node contents
  bool is_cross_reference;
  bool can_edit_node; // Links and cross-reference boxes cannot be edited
  bool can_edit_image_entities;
  bool can_edit_region;
  bool rendered_result;
  bool whole_image; // If true, `roi_editor' state temporarily ignored
  jpx_roi_editor roi_editor;
public: // Data members describing box contents for the node
  kdms_box_template *available_types; // Array provided by the constructor
  kdms_box_template *offered_types[KDMS_NUM_BOX_TEMPLATES]; // Array of
                             // pointers into the `available_types' array
  int num_offered_types; // Number of options offered to the user
  kdms_box_template *selected_type; // Matches cur selection from offered list
private: // We keep the following member private, since we need to be sure
         // to manage the retain/release calls correctly
  char *label_string; // NULL if node holds an internally stored label.
         // Note taht this member and `external_file' can both be non-NULL if
         // the external file is just a copy of the label string; in this case,
         // `external_file_replaces_contents' will be false of course.
  kdms_file *external_file; // If this reference is non-NULL, the present
         // object has retained it.  It may be obtained from an existing
         // metanode, in which case the same referece will be found in
         // `kdms_metanode_edit_state'.  Alternatively, it may have been
         // freshly created to hold user edits.
  bool external_file_replaces_contents; // True if `external_file'
         // is anything other than a copy of the existing box
         // contents (i.e., if edited, or if file is provided by user).
  kdms_file_services *file_server; // Provided by the constructor
};
/* Notes:
      The `selected_container_min_base_stream' member is set to -1 whenever
   `selected_container_id' is -ve, or `selected_container_id' refers to a
   JPX container that does not contain REPLICATED base codestreams.  Under
   these same conditions, `selected_container_num_base_streams is set to 0.
   Otherwise, these members identify the first base codestream index and
   the number of base codestreams defined by the selected container.
      Similarly, `selected_container_min_base_layer' is set to the container's
   first base compositing layer index and `selected_container_num_base_layers'
   is set to the number of these base layers, unless there is no container,
   or the container is not REPLICATED, in which case they are set to -1
   and 0, respectively.
      These members are configured by the `select_potential_container_id'
   function; they are used to modify the way in which codestreams and
   compositing layers are displayed in their list boxes. */

/*****************************************************************************/
/*                           kdms_metadata_editor                            */
/*****************************************************************************/

@interface kdms_metadata_editor : NSObject {
  IBOutlet NSWindow *dialog_window;
  IBOutlet NSTextField *x_pos_field;
  IBOutlet NSTextField *y_pos_field;
  IBOutlet NSTextField *width_field;
  IBOutlet NSTextField *height_field;
  IBOutlet NSButton *single_rectangle_button;
  IBOutlet NSButton *single_ellipse_button;
  IBOutlet NSButton *roi_encoded_button;
  IBOutlet NSButton *whole_image_button;

  IBOutlet NSButton *roi_shape_editor_button;
  IBOutlet NSPopUpButton *roi_mode_popup;
  IBOutlet NSLevelIndicator *roi_complexity_level;
  IBOutlet NSTextField *roi_complexity_value;
  IBOutlet NSButton *roi_add_region_button;
  IBOutlet NSButton *roi_elliptical_button;
  IBOutlet NSButton *roi_delete_region_button;
  IBOutlet NSButton *roi_split_vertices_button;
  IBOutlet NSTextField *roi_path_width_field;
  IBOutlet NSButton *roi_set_path_button;
  IBOutlet NSButton *roi_fill_path_button;
  IBOutlet NSButton *roi_undo_button;
  IBOutlet NSButton *roi_redo_button;
  IBOutlet NSButton *roi_scribble_button;
  IBOutlet NSButton *roi_scribble_convert_button;
  IBOutlet NSButton *roi_scribble_replaces_button;
  IBOutlet NSSlider *roi_scribble_accuracy_slider;
  
  IBOutlet NSPopUpButton *codestream_popup;
  IBOutlet NSTextField *codestream_field;
  IBOutlet NSButton *codestream_add_button;
  IBOutlet NSButton *codestream_remove_button;
  IBOutlet NSStepper *codestream_stepper;
  IBOutlet NSPopUpButton *compositing_layer_popup;
  IBOutlet NSTextField *compositing_layer_field;
  IBOutlet NSButton *compositing_layer_add_button;
  IBOutlet NSButton *compositing_layer_remove_button;
  IBOutlet NSStepper *compositing_layer_stepper;
  IBOutlet NSButton *container_embedding_button;
  IBOutlet NSButton *rendered_result_button;
  
  IBOutlet NSButton *is_link_button;
  IBOutlet NSTextField *link_type_label;
  IBOutlet NSPopUpButton *box_type_popup;
  IBOutlet NSPopUpButton *box_editor_popup;
  IBOutlet NSTextField *label_field;
  IBOutlet NSTextField *external_file_field;
  IBOutlet NSButton *temporary_file_button;
  IBOutlet NSButton *save_to_file_button;
  IBOutlet NSButton *edit_file_button;
  IBOutlet NSButton *choose_file_button;
  IBOutlet NSButton *internalize_label_button;

  IBOutlet NSButton *copy_link_button;
  IBOutlet NSButton *paste_button;
  IBOutlet NSButton *copy_descendants_button;
  
  IBOutlet NSButton *apply_button;
  IBOutlet NSButton *apply_and_exit_button;
  IBOutlet NSButton *delete_button;
  IBOutlet NSButton *exit_button;

  IBOutlet NSButton *next_button;
  IBOutlet NSTextField *next_button_label;
  IBOutlet NSButton *prev_button;
  IBOutlet NSTextField *prev_button_label;
  IBOutlet NSButton *parent_button;
  IBOutlet NSTextField *parent_button_label;
  IBOutlet NSButton *descendants_button;
  IBOutlet NSTextField *descendants_button_label;
  IBOutlet NSButton *add_child_button;
  IBOutlet NSButton *add_parent_button;
  IBOutlet NSButton *metashow_button;
  IBOutlet NSButton *catalog_button;
  
  NSArray *top_level_nib_objects; // `loadNibNamed:...'uses this array to store
                                  // retaining references to the top-level objs

  bool allow_edits; // If editing is allowed
  bool editing_shape;
  bool hide_shape_paths;
  bool shape_scribbling_active;
  int fixed_shape_path_thickness;
  jpx_roi_editor_mode shape_editing_mode;
  kdms_box_template available_types[KDMS_NUM_BOX_TEMPLATES];
  
  int num_codestreams;
  int num_layers;
  
  jpx_source *source;
  kdms_file_services *file_server;
  kdms_renderer *renderer;
  
  kdms_matching_metalist *owned_metalist; // Passed by `configureWithEditList'
  kdms_metanode_edit_state *edit_state; // For C++ objs which need constructor
  kdms_metadata_dialog_state *state;
  kdms_metadata_dialog_state *initial_state;
       // Provision of two copies of the state allows one to keep track of
       // the original node parameters so we can see what has changed.
}

// Actions which are used to alter box associations
- (IBAction) removeCodestream:(id)sender;
- (IBAction) addCodestream:(id)sender;
- (IBAction) stepCodestream:(id)sender;
- (IBAction) showCodestream:(id)sender;
- (IBAction) removeCompositingLayer:(id)sender;
- (IBAction) addCompositingLayer:(id)sender;
- (IBAction) stepCompositingLayer:(id)sender;
- (IBAction) showCompositingLayer:(id)sender;
- (IBAction) clickedContainerEmbedding:(id)sender;
- (IBAction) clickedWholeImage:(id)sender;
- (IBAction) clearROIEncoded:(id)sender;
- (IBAction) setSingleRectangle:(id)sender;
- (IBAction) setSingleEllipse:(id)sender;

// Actions which are used in connection with the interactive ROI editor
- (IBAction) editROIShape:(id)sender; // Also stops the editor
- (IBAction) changeROIMode:(id)sender;
- (IBAction) addROIRegion:(id)sender;
- (IBAction) deleteROIRegion:(id)sender;
- (IBAction) splitROIAnchor:(id)sender;
- (IBAction) undoROIEdit:(id)sender;
- (IBAction) redoROIEdit:(id)sender;
- (IBAction) setROIPathWidth:(id)sender;
- (IBAction) fillROIPath:(id)sender;
- (IBAction) startScribble:(id)sender; // Also stops scribble mode
- (IBAction) convertScribble:(id)sender;

// Actions which are used to alter box contents
- (IBAction) changeBoxType:(id)sender; // Send when user clicks box type popup
- (IBAction) saveToFile:(id)sender;
- (IBAction) chooseFile:(id)sender;
- (IBAction) editFile:(id)sender;
- (IBAction) changeEditor:(id)sender;
- (IBAction) internalizeLabel:(id)sender;

// Actions which work with the catalog pastebar
- (IBAction) clickedCopyLink:(id)sender;
- (IBAction) clickedPaste:(id)sender;
- (IBAction) clickedCopyDescendants:(id)sender;

// Actions used to navigate and control the editing session
- (IBAction) clickedExit:(id)sender;
- (IBAction) clickedDelete:(id)sender;
- (IBAction) clickedApply:(id)sender;
- (IBAction) clickedApplyAndExit:(id)sender;
- (IBAction) clickedNext:(id)sender;
- (IBAction) clickedPrev:(id)sender;
- (IBAction) clickedParent:(id)sender;
- (IBAction) clickedDescendants:(id)sender;
- (IBAction) clickedAddChild:(id)sender;
- (IBAction) clickedAddParent:(id)sender;
- (IBAction) findInMetashow:(id)sender;
- (IBAction) findInCatalog:(id)sender;

// Generic actions
- (IBAction) revertToInitialState:(id)sender;
- (IBAction) otherAction:(id)sender;

// Delegation messages
- (void) controlTextDidChange:(NSNotification *)notification;
    // Received from the `label_field' for which we are a delegate

// Messages used to initialize, configure and run the editing dialog
- (void) initWithSource:(jpx_source *)manager
           fileServices:(kdms_file_services *)file_services
               editable:(bool)can_edit
                  owner:(kdms_renderer *)renderer
               location:(NSPoint)preferred_location
          andIdentifier:(int)identifier;
  /* Starts the dialog window, but you need to call one of the `configure...'
     functions to do useful editing.  After that you can call `configure...'
     as often as you like before closing the editor.
     [//]
     Note that editing activities may have various side-effects related to
     the `renderer' object.
     [//]
     The `identifier' is used to construct a title for the editor which
     associates it with the `owner's window.  The `location' represents
     a preferred location for the upper left hand corner of the window.  If
     the window cannot fit at that location, adjustments are of course
     made. */
- (void) close;
  /* Closes dialog, detaches it from `the_renderer' and releases self. */
- (void) dealloc;
  /* Note that only the `close' function explicitly releases the object,
     which should be immediately followed by `dealloc' unless we somehow
     got retained elsewhere for a period. */
- (void) configureWithEditList:(kdms_matching_metalist *)metalist;
  /* To use this function, you should be sure that `metalist->get_head()'
     returns a non-empty list of matching metadata nodes. The editor takes
     ownership of the `metalist' object after the function returns, so you
     should not delete it yourself. */
- (void) setActiveNode:(jpx_metanode)node;
  /* You may call this function after `configureWithEditList' in order to
     move the active node being edited to `node', but this will be ignored
     unless `node' is a descendant of one of the nodes in the `metalist'
     supplied to `configureWithEditList'.  The function has the same effect
     as user navigation to `node'.  It is a good idea to call this function
     even if you are only moving to the head of the metalist passed to
     `configureWithEditList' since this function automatically launches the
     shape editor if the active node happens to be a region of interest
     that can be edited. */
// Utility functions
- (void) preconfigure; // First step in each `configure...' call
- (void) map_state_to_controls; // Copies `state' to dialog controls
- (void) map_controls_to_state; // Copies dialog control info to `state'
- (void) update_apply_buttons; // Enables/disables the "Apply ..." buttons,
                      // based on whether or not there is anything to apply.
- (void) update_shape_editor_controls;
- (bool) query_save;
    // Called in a context where continuing with an action may require
    // editing state to be saved before proceeding.  Returns false, if the
    // caller should not take the action that required editing state to be
    // saved.  This function always stops the shape editor, so you may need
    // to restart it afterwards.
- (bool) query_save_shape_edits;
    // Saves shape edits and stops shape editor, unless the ROID limit is
    // exceeded, in which case the user is queried for what to do -- returns
    // false if shape editing should continue.
- (void) stop_shape_editor;
- (void) start_shape_editor;
- (void) auto_start_shape_editor;
    // Automatically starts the shape editor, if that is appropriate.  This
    // happens if the node currently being edited is actually an ROI
    // node, but not if it is a descendant of an ROI node, even if the
    // region is editable in the current editing context.

// Delegate functions
- (BOOL) control:(NSControl *)control isValidObject:(id)object;
         // Used by text-fields, which set this object as their delegate,
         // to reject non-integer values.

@end // kdms_metadata_editor
