/*****************************************************************************/
// File: kdws_metadata_editor.cpp [scope = APPS/WINSHOW]
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
   Implementation of the metadata display dialog in the "kdu_show" application.
******************************************************************************/
#include "stdafx.h"
#include <math.h>
#include "kdws_renderer.h"
#include "kdws_metadata_editor.h"
#include "kdws_window.h"
#include "kdws_metashow.h"
#include "kdws_catalog.h"
#include "kdws_manager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/*===========================================================================*/
/*                           INTERNAL FUNCTIONS                              */
/*===========================================================================*/

/*****************************************************************************/
/* STATIC                     get_nonneg_integer                             */
/*****************************************************************************/

static void get_nonneg_integer(CEdit *field, int &val)
{
  kdws_string string(80);
  int label_length = field->LineLength();
  if (label_length > 80)
    label_length = 80;
  field->GetLine(0,string,label_length);
  const char *str = string;
  val = 0;
  for (; *str != '\0'; str++)
    if (!isdigit(*str))
      break;
    else
      val = (val*10) + (int)(*str - '0');
}

/*****************************************************************************/
/* STATIC                   check_integer_in_range                           */
/*****************************************************************************/

static bool check_integer_in_range(kdws_metadata_editor *dlg,
                                   CEdit *field, int min_val,
                                   int max_val)
{
  kdws_string string(80);
  int label_length = field->LineLength();
  if (label_length > 80)
    label_length = 80;
  field->GetLine(0,string,label_length);
  const char *str = string;
  int val = 0;
  for (; *str != '\0'; str++)
    if (!isdigit(*str))
      break;
    else
      val = (val*10) + (int)(*str - '0');
  if ((*str == '\0') && (val >= min_val) && (val <= max_val))
    return true;
  sprintf(string,"Text field requires integer in range %d to %d",
          min_val,max_val);
  dlg->interrogate_user(string,"OK");
  dlg->GotoDlgCtrl(field);
  return false;
}

/*****************************************************************************/
/* STATIC                         copy_file                                  */
/*****************************************************************************/

static void
  copy_file(kdws_file *existing_file, kdws_file *new_file)
{
  if (existing_file == new_file)
    return;
  kdu_byte buf[512];
  size_t xfer_bytes;
  FILE *src = fopen(existing_file->get_pathname(),"rb");
  if (src == NULL)
    return;
  FILE *dst = fopen(new_file->get_pathname(),"wb");
  if (dst != NULL)
    {
      while ((xfer_bytes = fread(buf,1,512,src)) > 0)
        fwrite(buf,1,xfer_bytes,dst);
      fclose(dst);
    }
  fclose(src);
}

/*****************************************************************************/
/* STATIC                      choose_editor                                 */
/*****************************************************************************/

static kdws_file_editor *
  choose_editor(kdws_file_services *file_server, const char *doc_suffix)
{
 kdws_string pathname(MAX_PATH);
  OPENFILENAME ofn;
  memset(&ofn,0,sizeof(ofn));  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL;
  ofn.lpstrFilter = _T("Executable file\0*.exe\0\0");
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = pathname;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = _T("Choose editor application");
  ofn.lpstrInitialDir = _T("c:\\Program Files");
  ofn.Flags = OFN_FILEMUSTEXIST;
  if (!GetOpenFileName(&ofn))
    return NULL;
  return file_server->add_editor_for_doc_type(doc_suffix,pathname);
}

/*****************************************************************************/
/* STATIC                 choose_open_file_pathname                          */
/*****************************************************************************/

static bool
  choose_open_file_pathname(const char *suffix, kdws_file *existing_file,
                            char pathname[], int max_pathname_chars)
  /* Allows the user to select an existing file, with the indicated suffix
     (extension) in the finder.  If `existing_file' is non-NULL, the function
     uses the existing file's pathname to decide which directory to start the
     finder in.  If the user selects a file, the function returns true,
     writing the file pathname into `pathname'. */
{
  OPENFILENAME ofn;
  memset(&ofn,0,sizeof(ofn));  ofn.lStructSize = sizeof(ofn);
  char initial_dir_buf[MAX_PATH+1];
  const char *initial_dir = NULL;
  if (existing_file != NULL)
    {
      initial_dir = initial_dir_buf;
      strncpy(initial_dir_buf,existing_file->get_pathname(),MAX_PATH);
      initial_dir_buf[MAX_PATH] = '\0';
      char *cp = initial_dir_buf + strlen(initial_dir) - 1;
      for (; (cp > initial_dir_buf) && (*cp != '/') && (*cp != '\\') &&
             (cp[-1] != ':'); cp--);
      if (cp > initial_dir_buf)
        *cp = '\0';
    }
  ofn.hwndOwner = NULL;
  ofn.lpstrFilter = _T("*.*\0*.*\0\0");
  kdws_string suffix_ext(20);
  if (strlen(suffix) < 8)
    {
      strcpy(suffix_ext,suffix);
      ofn.lpstrFilter = suffix_ext;
    }
  kdws_string pathname_string(max_pathname_chars);
  kdws_string initial_dir_string(initial_dir);
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = pathname_string;
  ofn.nMaxFile = max_pathname_chars;
  ofn.lpstrTitle = _T("Choose file for box contents");
  ofn.lpstrInitialDir = initial_dir_string;
  ofn.Flags = OFN_FILEMUSTEXIST;
  if (!GetOpenFileName(&ofn))
    return false;
  strcpy(pathname,pathname_string);
  return true;
}

/*****************************************************************************/
/* STATIC                 choose_save_file_pathname                          */
/*****************************************************************************/

static bool
  choose_save_file_pathname(const char *suffix, kdws_file *existing_file,
                            char pathname[], int max_pathname_chars)
  /* Same as `choose_open_file_pathname' except that you are choosing a
     file to save to, rather than an existing file to use. */
{
  OPENFILENAME ofn;
  memset(&ofn,0,sizeof(ofn));  ofn.lStructSize = sizeof(ofn);
  char initial_dir_buf[MAX_PATH+1];
  const char *initial_dir = NULL;
  if (existing_file != NULL)
    {
      initial_dir = initial_dir_buf;
      strncpy(initial_dir_buf,existing_file->get_pathname(),MAX_PATH);
      initial_dir_buf[MAX_PATH] = '\0';
      char *cp = initial_dir_buf + strlen(initial_dir) - 1;
      for (; (cp > initial_dir_buf) && (*cp != '/') && (*cp != '\\') &&
             (cp[-1] != ':'); cp--);
      if (cp > initial_dir_buf)
        *cp = '\0';
    }
  ofn.hwndOwner = NULL;
  kdws_string suffix_ext(20);
  ofn.lpstrFilter = _T("*.*\0*.*\0\0");
  if (strlen(suffix) < 8)
    {
      strcpy(suffix_ext,suffix);
      ofn.lpstrFilter = suffix_ext;
    }
  kdws_string pathname_string(max_pathname_chars);
  kdws_string initial_dir_string(initial_dir);
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = pathname_string;
  ofn.nMaxFile = max_pathname_chars;
  ofn.lpstrTitle = _T("Choose file to receive box contents");
  ofn.lpstrInitialDir = initial_dir_string;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
  if (!GetSaveFileName(&ofn))
    return false;
  strcpy(pathname,pathname_string);
  return true;
}

/*****************************************************************************/
/* STATIC                 get_file_retained_by_node                          */
/*****************************************************************************/

static kdws_file *
  get_file_retained_by_node(jpx_metanode node, kdws_file_services *file_server)
{
  int i_param;
  void *addr_param;
  kdws_file *result = NULL;
  if (node.get_delayed(i_param,addr_param) &&
      (addr_param == (void *)file_server))
    result = file_server->find_file(i_param);
  return result;
}

/*****************************************************************************/
/* STATIC             copy_descendants_with_file_retain                      */
/*****************************************************************************/

static void
  copy_descendants_with_file_retain(jpx_metanode src, jpx_metanode dst,
                                    kdws_file_services *file_server,
                                    bool copy_linkers)
{
  jpx_metanode src_child;
  while ((src_child = src.get_next_descendant(src_child)).exists())
    {
      jpx_metanode dst_child = dst.add_copy(src_child,false,true);
      if (!dst_child)
        continue;
      kdws_file *file = get_file_retained_by_node(dst_child,file_server);
      if (file != NULL)
        file->retain(); // Retain a second time on behalf of the copy
      copy_descendants_with_file_retain(src_child,dst_child,file_server,
                                        false);
    }
  
  jpx_metanode src_linker;
  while (copy_linkers && (src_linker = src.enum_linkers(src_linker)).exists())
    {
      jpx_metanode_link_type link_type;
      jpx_metanode link_target = src_linker.get_link(link_type);
      assert(link_target == src);
      jpx_metanode parent = src_linker.get_parent();
      if (!parent)
        continue; // Should not be possible
      jpx_metanode dst_linker = parent.add_link(dst,link_type);
      copy_descendants_with_file_retain(src_linker,dst_linker,file_server,
                                        false);
    }
}


/*===========================================================================*/
/*                           kdws_matching_metalist                          */
/*===========================================================================*/

/*****************************************************************************/
/*                  kdws_matching_metalist::find_container                   */
/*****************************************************************************/

kdws_matching_metanode *
  kdws_matching_metalist::find_container(jpx_metanode node)
{
  for (; node.exists(); node=node.get_parent())
    for (kdws_matching_metanode *scan=head; scan != NULL; scan=scan->next)
      if (scan->node == node)
        return scan;
  return NULL;
}

/*****************************************************************************/
/*                    kdws_matching_metalist::find_member                    */
/*****************************************************************************/

kdws_matching_metanode *
  kdws_matching_metalist::find_member(jpx_metanode node)
{
  for (kdws_matching_metanode *scan=head; scan != NULL; scan=scan->next)
    {
      jpx_metanode scan_node = scan->node;
      for (; scan_node.exists(); scan_node=scan_node.get_parent())
        if (scan_node == node)
          return scan;
    }
  return NULL;
}

/*****************************************************************************/
/*                   kdws_matching_metalist::delete_item                     */
/*****************************************************************************/

void kdws_matching_metalist::delete_item(kdws_matching_metanode *item)
{
  if (item->prev == NULL)
    {
      assert(item == head);
      head = item->next;
    }
  else
    item->prev->next = item->next;
  if (item->next == NULL)
    {
      assert(item == tail);
      tail = item->prev;
    }
  else
    item->next->prev = item->prev;
  delete item;
}

/*****************************************************************************/
/*                    kdws_matching_metalist::append_node                    */
/*****************************************************************************/

kdws_matching_metanode *kdws_matching_metalist::append_node(jpx_metanode node)
{
  kdws_matching_metanode *elt = new kdws_matching_metanode;
  elt->node = node;
  elt->next = NULL;
  if ((elt->prev = tail) == NULL)
    head = tail = elt;
  else
    tail = tail->next = elt;
  return elt;
}


/*===========================================================================*/
/*                          kdws_metanode_edit_state                         */
/*===========================================================================*/

/*****************************************************************************/
/*           kdws_metanode_edit_state::kdws_metanode_edit_state              */
/*****************************************************************************/

kdws_metanode_edit_state::kdws_metanode_edit_state(jpx_source *source,
                                              kdws_file_services *file_server)
{
  this->source = source;
  this->meta_manager = source->access_meta_manager();
  metalist = NULL;
  edit_item = NULL;
  this->file_server = file_server;
}

/*****************************************************************************/
/*                 kdws_metanode_edit_state::move_to_parent                  */
/*****************************************************************************/

void kdws_metanode_edit_state::move_to_parent()
{
  if (at_root() || !cur_node.exists())
    return;
  cur_node = cur_node.get_parent();
}

/*****************************************************************************/
/*                  kdws_metanode_edit_state::move_to_next                   */
/*****************************************************************************/

void kdws_metanode_edit_state::move_to_next()
{
  if (!cur_node.exists())
    return;
  if (at_root())
    {
      if (edit_item->next == NULL)
        return;
      edit_item = edit_item->next;
      root_node = cur_node = edit_item->node;
    }
  else
    { 
      jpx_metanode parent = cur_node.get_parent();
      jpx_metanode sibling = parent.get_next_descendant(cur_node);
      if (sibling.exists())
        cur_node = sibling;
    }
}

/*****************************************************************************/
/*                  kdws_metanode_edit_state::move_to_prev                   */
/*****************************************************************************/

void kdws_metanode_edit_state::move_to_prev()
{
  if (!cur_node.exists())
    return;
  if (at_root())
    {
      if (edit_item->prev == NULL)
        return;
      edit_item = edit_item->prev;
      root_node = cur_node = edit_item->node;
    }
  else
    { 
      jpx_metanode parent = cur_node.get_parent();
      jpx_metanode sibling = parent.get_prev_descendant(cur_node);
      if (sibling.exists())
        cur_node = sibling;
    }
}

/*****************************************************************************/
/*              kdws_metanode_edit_state::move_to_descendants                */
/*****************************************************************************/

void kdws_metanode_edit_state::move_to_descendants()
{
  if (!cur_node.exists())
    return;
  jpx_metanode child = cur_node.get_descendant(0);
  if (child.exists())
    cur_node = child;
}

/*****************************************************************************/
/*                 kdws_metanode_edit_state::move_to_node                    */
/*****************************************************************************/

void kdws_metanode_edit_state::move_to_node(jpx_metanode node)
{
  if (metalist == NULL)
    return; // Should not happen
  kdws_matching_metanode *container = metalist->find_container(node);
  if (container == NULL)
    return; // Do nothing
  edit_item = container;
  root_node = edit_item->node;
  cur_node = node;
}

/*****************************************************************************/
/*                kdws_metanode_edit_state::delete_cur_node                  */
/*****************************************************************************/

void
  kdws_metanode_edit_state::delete_cur_node(kdws_renderer *renderer)
{
  jpx_metanode node_to_delete = cur_node;
  jpx_metanode next_cur_node;
  if (!at_root())
    { // Deleting a descendant of a top-level node.
      jpx_metanode parent = cur_node.get_parent();
      next_cur_node = parent.get_prev_descendant(cur_node);
      if (!next_cur_node)
        next_cur_node = parent.get_next_descendant(cur_node);
      if (!next_cur_node)
        next_cur_node = parent;
    }
  
  cur_node = jpx_metanode(); // Temporarily get rid of it
  
  int i_param;
  void *addr_param;
  if (node_to_delete.get_delayed(i_param,addr_param) &&
      (addr_param == (void *)file_server))
    {
      kdws_file *file = file_server->find_file(i_param);
      if (file != NULL)
        file->release();
    }
  node_to_delete.delete_node();
  validate_metalist_and_root_node();
  if ((!next_cur_node) || next_cur_node.is_deleted())
    cur_node = root_node;
  else
    cur_node = next_cur_node;  
  renderer->metadata_changed(false);
}

/*****************************************************************************/
/*        kdws_metanode_edit_state::validate_metalist_and_root_node          */
/*****************************************************************************/

void kdws_metanode_edit_state::validate_metalist_and_root_node()
{
  if (metalist == NULL)
    return;
  kdws_matching_metanode *item, *next_item;
  for (item=metalist->get_head(); item != NULL; item=next_item)
    {
      next_item = item->next;
      if (!item->node.is_deleted())
        continue;
      if (item == edit_item)
        {
          edit_item = (edit_item->prev!=NULL)?edit_item->prev:edit_item->next;
          root_node = (edit_item!=NULL)?edit_item->node:jpx_metanode();
        }
      metalist->delete_item(item);
    }
}

/*****************************************************************************/
/*                 kdws_metanode_edit_state::add_child_node                  */
/*****************************************************************************/

void
  kdws_metanode_edit_state::add_child_node(kdws_renderer *renderer)
{
  assert(cur_node.exists());
  jpx_metanode new_node;
  try {
    new_node = cur_node.add_label("<new label>");
  }
  catch (kdu_exception) {
    return;
  }
  if (new_node.exists())
    {
      cur_node = new_node;
      renderer->metadata_changed(true);
    }
}

/*****************************************************************************/
/*                kdws_metanode_edit_state::add_parent_node                  */
/*****************************************************************************/

void
  kdws_metanode_edit_state::add_parent_node(kdws_renderer *renderer)
{
  assert(cur_node.exists());
  jpx_metanode parent = cur_node.get_parent();
  if (!parent.exists())
    return; // Try to insert a new parent above the root node
  jpx_metanode new_parent;
  try {
    int nstr, nlyr; bool rres;
    if (cur_node.get_container_id() >= 0)
      { // Need to be careful to ensure that the new parent will have the
        // necessary container embedding
        if (parent.get_parent().exists())
          assert(parent.get_container_id() == cur_node.get_container_id());
        else if (cur_node.get_numlist_info(nstr,nlyr,rres) &&
                 (nstr==0) && (nlyr==0) && !rres)
          return; // Makes no sense to add a parent node above a number list
                  // that exists only to mark container embedding
        else
          { // `parent' is the root of the metadata hierarchy
            parent = parent.add_numlist(0,NULL,0,NULL,false,
                                        cur_node.get_container_id());
          }
      }
    
    if ((cur_node.get_num_regions() > 0) &&
        parent.get_numlist_info(nstr,nlyr,rres))
      { // Special numlist/roi combo -- in this case, we may need to leave
        // the existing numlist where it is and create a new one under the
        // new parent.
        jpx_metanode combo_parent = parent.get_parent();
        new_parent = combo_parent.add_label("<new parent>");
        int nd=0;
        if (parent.count_descendants(nd) && (nd == 1))
          { // We are the only descendant of the numlist; move the combo
            parent.change_parent(new_parent);
          }
        else
          { // We need to leave the numlist in place
            parent =
              meta_manager.insert_node(nstr,parent.get_numlist_codestreams(),
                                       nlyr,parent.get_numlist_layers(),rres,
                                       NULL,0,new_parent,
                                       cur_node.get_container_id());
            cur_node.change_parent(parent);
          }
      }
    else
      { 
        new_parent = parent.add_label("<new parent>");
        cur_node.change_parent(new_parent);
      }
  }
  catch (kdu_exception) {
    renderer->metadata_changed(true); // Just in case one op succeeded above
    return;
  }
  if (at_root())
    {
      root_node = new_parent;
      if (edit_item != NULL)
        edit_item->node = new_parent;
    }
  cur_node = new_parent;
  renderer->metadata_changed(true);
}


/*===========================================================================*/
/*                        kdws_metadata_dialog_state                         */
/*===========================================================================*/

/*****************************************************************************/
/*         kdws_metadata_dialog_state::kdws_metadata_dialog_state            */
/*****************************************************************************/

kdws_metadata_dialog_state::kdws_metadata_dialog_state(
                                         kdws_metadata_editor *dlg,
                                         int num_codestreams,
                                         int num_layers,
                                         kdws_box_template *available_types,
                                         kdws_file_services *file_server,
                                         bool allow_edits)
{
  this->num_codestreams = num_codestreams;
  this->num_layers = num_layers;
  num_selected_codestreams = num_selected_layers = 0;
  max_selected_codestreams = max_selected_layers = 1;
  selected_codestreams = new int[max_selected_codestreams];
  selected_layers = new int[max_selected_layers];
  selected_container_id = potential_container_id = -1;
  selected_container_min_base_layer = -1;
  selected_container_min_base_stream = -1;
  selected_container_num_base_layers = 0;
  selected_container_num_base_streams = 0;
  this->allow_edits = allow_edits;
  is_link = is_cross_reference = false;
  can_edit_node = allow_edits;
  can_edit_image_entities = can_edit_region = false;
  rendered_result = false;
  whole_image = true;
  this->available_types = available_types;
  memset(offered_types,0,sizeof(int *) * KDWS_NUM_BOX_TEMPLATES);
  num_offered_types = 1;
  offered_types[0] = available_types + KDWS_LABEL_TEMPLATE;
  selected_type = offered_types[0];
  label_string = NULL;
  this->file_server = file_server;
  external_file = NULL;
  external_file_replaces_contents = false;
}

/*****************************************************************************/
/*         kdws_metadata_dialog_state::~kdws_metadata_dialog_state           */
/*****************************************************************************/

kdws_metadata_dialog_state::~kdws_metadata_dialog_state()
{
  if (selected_codestreams != NULL)
    delete[] selected_codestreams;
  if (selected_layers != NULL)
    delete[] selected_layers;
  if (external_file != NULL)
    external_file->release();
  if (label_string != NULL)
    delete[] label_string;
}

/*****************************************************************************/
/*        kdws_metadata_dialog_state::compare_image_entity_associations      */
/*****************************************************************************/

bool kdws_metadata_dialog_state::compare_image_entity_associations(
                                          kdws_metadata_dialog_state *ref)
{
  int n;
  if (num_selected_codestreams != ref->num_selected_codestreams)
    return false;
  for (n=0; n < num_selected_codestreams; n++)
    if (selected_codestreams[n] != ref->selected_codestreams[n])
      return false;
  if (num_selected_layers != ref->num_selected_layers)
    return false;
  for (n=0; n < num_selected_layers; n++)
    if (selected_layers[n] != ref->selected_layers[n])
      return false;
  return ((rendered_result == ref->rendered_result) &&
          (selected_container_id == ref->selected_container_id));
}

/*****************************************************************************/
/*           kdws_metadata_dialog_state::compare_region_associations         */
/*****************************************************************************/

bool kdws_metadata_dialog_state::compare_region_associations(
                                          kdws_metadata_dialog_state *ref)
{
  if (whole_image != ref->whole_image)
    return false;
  if (whole_image)
    return true;
  else
    return (roi_editor == ref->roi_editor);
}

/*****************************************************************************/
/*                kdws_metadata_dialog_state::compare_contents               */
/*****************************************************************************/

bool kdws_metadata_dialog_state::compare_contents(
                                          kdws_metadata_dialog_state *ref)
{
  if (selected_type != ref->selected_type)
    return false;
  if (is_link != ref->is_link)
    return false;
  if (external_file_replaces_contents || ref->external_file_replaces_contents)
    return false;
  if (((label_string != NULL) || (ref->label_string != NULL)) &&
      ((label_string == NULL) || (ref->label_string == NULL) ||
       (strcmp(label_string,ref->label_string) != 0)))
    return false;
  return true;
}

/*****************************************************************************/
/*        kdws_metadata_dialog_state::eliminate_duplicate_codestreams        */
/*****************************************************************************/

void kdws_metadata_dialog_state::eliminate_duplicate_codestreams()
{
  int n, m;
  for (n=0; n < num_selected_codestreams; n++)
    {
      for (m=0; m < n; m++)
        if (selected_codestreams[m] == selected_codestreams[n])
          break;
      if (m < n)
        { // entry n is a duplicate codestream
          num_selected_codestreams--;
          for (m=n; m < num_selected_codestreams; m++)
            selected_codestreams[m] = selected_codestreams[m+1];
          n--; // So main loop visits all locations
        }
    }
}

/*****************************************************************************/
/*           kdws_metadata_dialog_state::eliminate_duplicate_layers          */
/*****************************************************************************/

void kdws_metadata_dialog_state::eliminate_duplicate_layers()
{
  int n, m;
  for (n=0; n < num_selected_layers; n++)
    {
      for (m=0; m < n; m++)
        if (selected_layers[m] == selected_layers[n])
          break;
      if (m < n)
        { // entry n is a duplicate codestream
          num_selected_layers--;
          for (m=n; m < num_selected_layers; m++)
            selected_layers[m] = selected_layers[m+1];
          n--; // So main loop visits all locations
        }
    }
}

/*****************************************************************************/
/*            kdws_metadata_dialog_state::add_selected_codestream            */
/*****************************************************************************/

int
  kdws_metadata_dialog_state::add_selected_codestream(int idx,
                                                      jpx_source *source)
{
  if (idx >= num_codestreams)
    return -1;
  int m, n;
  for (n=0; n < num_selected_codestreams; n++)
    if (selected_codestreams[n] == idx)
      return n; // Nothing to do; already exists in the list
  jpx_container_source container;
  if (source == NULL)
    { 
      potential_container_id = -1;
      select_potential_container_id(false,NULL);
    }
  else if (selected_container_id >= 0)
    container = source->access_container(selected_container_id);
  if (idx < 0)
    { // Choose a value for `idx' automatically
      if (container.exists())
        { 
          int num_top, first_base, num_base;
          num_top = container.get_num_top_codestreams();
          first_base = container.get_base_codestreams(num_base);
          for (m=0; (idx < 0) && (m < num_base); m++)
            for (idx=first_base+m, n=0; n < num_selected_codestreams; n++)
              if ((selected_codestreams[n] >= first_base) &&
                  (((selected_codestreams[n]-idx) % num_base) == 0))
                { idx=-1; break; }
          for (m=0; (idx < 0) && (m < num_top); m++)
            for (idx=m, n=0; n < num_selected_codestreams; n++)
              if (selected_codestreams[n] == idx)
                { idx=-1; break; }
        }
      else
        { 
          for (m=0; (idx < 0) && (m < num_codestreams); m++)
            for (idx=m, n=0; n < num_selected_codestreams; n++)
              if (selected_codestreams[n] == idx)
                { idx=-1; break; } 
        }
      if (idx < 0)
        return -1;
    }
  else if (container.exists() && !container.check_compatibility(1,&idx,0,NULL))
    return -1;
  augment_max_selected_codestreams(num_selected_codestreams+1);
  n = num_selected_codestreams++;
  selected_codestreams[n] = idx;
  return n;
}

/*****************************************************************************/
/*              kdws_metadata_dialog_state::add_selected_layer               */
/*****************************************************************************/

int kdws_metadata_dialog_state::add_selected_layer(int idx,
                                                   jpx_source *source)
{
  if (idx >= num_layers)
    return -1;
  int m, n;
  for (n=0; n < num_selected_layers; n++)
    if (selected_layers[n] == idx)
      return n; // Nothing to do; already exists in the list
  jpx_container_source container;
  if (source == NULL)
    { 
      potential_container_id = -1;
      select_potential_container_id(false,NULL);
    }
  else if (selected_container_id >= 0)
    container = source->access_container(selected_container_id);
  if (idx < 0)
    { // Choose a value for `idx' automatically
      if (container.exists())
        { 
          int num_top, first_base, num_base;
          num_top = container.get_num_top_layers();
          first_base = container.get_base_layers(num_base);
          for (m=0; (idx < 0) && (m < num_base); m++)
            for (idx=first_base+m, n=0; n < num_selected_layers; n++)
              if ((selected_layers[n] >= first_base) &&
                  (((selected_layers[n]-idx) % num_base) == 0))
                { idx=-1; break; }
          for (m=0; (idx < 0) && (m < num_top); m++)
            for (idx=m, n=0; n < num_selected_layers; n++)
              if (selected_layers[n] == idx)
                { idx=-1; break; }
        }
      else
        { 
          for (m=0; (idx < 0) && (m < num_layers); m++)
            for (idx=m, n=0; n < num_selected_layers; n++)
              if (selected_layers[n] == idx)
                { idx=-1; break; } 
        }
      if (idx < 0)
        return -1;
    }
  else if (container.exists() && !container.check_compatibility(1,&idx,0,NULL))
    return -1;
  augment_max_selected_layers(num_selected_layers+1);
  n = num_selected_layers++;
  selected_layers[n] = idx;
  return n;
}

/*****************************************************************************/
/*          kdws_metadata_dialog_state::find_potential_container_id          */
/*****************************************************************************/

void
  kdws_metadata_dialog_state::find_potential_container_id(jpx_source *source)
{ 
  jpx_container_source container =
    source->find_unique_compatible_container(num_selected_codestreams,
                                             selected_codestreams,
                                             num_selected_layers,
                                             selected_layers);
  potential_container_id=(container.exists())?container.get_container_id():-1;
}

/*****************************************************************************/
/*         kdws_metadata_dialog_state::select_potential_container_id         */
/*****************************************************************************/

void
  kdws_metadata_dialog_state::select_potential_container_id(bool select,
                                                            jpx_source *source)
{
  selected_container_min_base_layer = -1;
  selected_container_min_base_stream = -1;
  selected_container_num_base_layers = 0;
  selected_container_num_base_streams = 0;
  if ((potential_container_id < 0) || !select)
    selected_container_id = -1;
  else
    { 
      selected_container_id = potential_container_id;
      jpx_container_source container =
        source->access_container(selected_container_id);
      assert(container.exists());
      int num_reps=0; container.count_repetitions(num_reps);
      if (num_reps > 1)
        { 
          int base, span=0;
          base = container.get_base_codestreams(span);
          selected_container_num_base_streams = span;
          if (span > 0)
            selected_container_min_base_stream = base;
          base = container.get_base_layers(span);
          selected_container_num_base_layers = span;
          if (span > 0)
            selected_container_min_base_layer = base;
        }
    }
}

/*****************************************************************************/
/*            kdws_metadata_dialog_state::make_compatible_stream             */
/*****************************************************************************/

int
  kdws_metadata_dialog_state::make_compatible_stream(int old_idx, int delta,
                                                     jpx_source *source)
{
  if ((old_idx+delta) >= num_codestreams)
    delta = 0;
  int new_idx = old_idx + delta;
  if (selected_container_id >= 0)
    { 
      jpx_container_source container =
        source->access_container(selected_container_id);
      int base, span=0, num_top=container.get_num_top_codestreams();
      base = container.get_base_codestreams(span);
      if ((old_idx >= base) && (new_idx < base))
        new_idx = num_top-1;
      else if ((old_idx < num_top) && (new_idx >= num_top))
        new_idx = (span == 0)?old_idx:base;
      else if ((old_idx < (base+span)) && (new_idx >= (base+span)))
        new_idx = old_idx;
    }
  if (new_idx < 0)
    new_idx = 0;
  return new_idx;
}

/*****************************************************************************/
/*            kdws_metadata_dialog_state::make_compatible_layer              */
/*****************************************************************************/

int
  kdws_metadata_dialog_state::make_compatible_layer(int old_idx, int delta,
                                                    jpx_source *source)
{
  if ((old_idx+delta) >= num_layers)
    delta = 0;
  int new_idx = old_idx + delta;
  if (selected_container_id >= 0)
    { 
      jpx_container_source container =
        source->access_container(selected_container_id);
      int base, span=0, num_top=container.get_num_top_layers();
      base = container.get_base_layers(span);
      if ((old_idx >= base) && (new_idx < base))
        new_idx = num_top-1;
      else if ((old_idx < num_top) && (new_idx >= num_top))
        new_idx = (span == 0)?old_idx:base;
      else if ((old_idx < (base+span)) && (new_idx >= (base+span)))
        new_idx = old_idx;
    }
  if (new_idx < 0)
    new_idx = 0;
  return new_idx;
}

/*****************************************************************************/
/*            kdws_metadata_dialog_state::configure_with_cur_node            */
/*****************************************************************************/

void kdws_metadata_dialog_state::configure_with_cur_node(
                                            kdws_metanode_edit_state *edit)
{
  jpx_metanode node = edit->cur_node;
  kdu_uint32 box_type = node.get_box_type();
  can_edit_node = allow_edits;
  if (box_type == jp2_cross_reference_4cc)
    can_edit_node = false;
  is_link = is_cross_reference = false;
  jpx_fragment_list frag_list;
  if (node.get_cross_reference(frag_list) != 0)
    is_cross_reference = true;
  jpx_metanode_link_type link_type = JPX_METANODE_LINK_NONE;
  jpx_metanode link_target = node.get_link(link_type);
  if (link_target.exists())
    {
      is_link = true;
      can_edit_node = false;
      node = link_target;
      box_type = node.get_box_type();
    }
  
  if (box_type == jp2_label_4cc)
    {
      num_offered_types = 3;
      offered_types[0] = available_types + KDWS_LABEL_TEMPLATE;
      offered_types[1] = available_types + KDWS_XML_TEMPLATE;
      offered_types[2] = available_types + KDWS_IPR_TEMPLATE;
    }
  else if (box_type == jp2_xml_4cc)
    {
      num_offered_types = 3;
      offered_types[1] = available_types + KDWS_LABEL_TEMPLATE;
      offered_types[0] = available_types + KDWS_XML_TEMPLATE;
      offered_types[2] = available_types + KDWS_IPR_TEMPLATE;
    }
  else if (box_type == jp2_iprights_4cc)
    {
      num_offered_types = 3;
      offered_types[2] = available_types + KDWS_LABEL_TEMPLATE;
      offered_types[1] = available_types + KDWS_XML_TEMPLATE;
      offered_types[0] = available_types + KDWS_IPR_TEMPLATE;
    }
  else
    {
      num_offered_types = 1;
      offered_types[0] = available_types + KDWS_CUSTOM_TEMPLATE;
      available_types[KDWS_CUSTOM_TEMPLATE].init(box_type,NULL,NULL);
    }
  if (!can_edit_node)
    num_offered_types = 1; // Do not offer other box-types
  selected_type = offered_types[0];
  set_label_string(node.get_label());
  external_file_replaces_contents = false;
  set_external_file(NULL);
  {
    int i_param;
    void *addr_param;
    if (node.get_delayed(i_param,addr_param) &&
        (addr_param == (void *)file_server))
      set_external_file(file_server->find_file(i_param));
  }
  
  whole_image = true;
  roi_editor.reset();
  rendered_result = false;
  num_selected_codestreams = num_selected_layers = 0;
  
  jpx_metanode scan;
  for (scan=node; scan.exists(); scan=scan.get_parent())
    {
      bool rres;
      int n, num_cs, num_l;
      if (scan.get_numlist_info(num_cs,num_l,rres))
        { 
          const int *streams = scan.get_numlist_codestreams();
          for (n=0; n < num_cs; n++)
            add_selected_codestream(streams[n],NULL);
          const int *layers = scan.get_numlist_layers();
          for (n=0; n < num_l; n++)
            add_selected_layer(layers[n],NULL);
          if (rres)
            rendered_result = true;
          break; // Don't look any higher up for associations
        }
      else if (whole_image && (scan.get_num_regions() > 0))
        { 
          whole_image = false;
          roi_editor.init(scan.get_regions(),scan.get_num_regions());
        }
    }
  potential_container_id = node.get_container_id();
  select_potential_container_id(true,edit->source);
  if (potential_container_id < 0)
    find_potential_container_id(edit->source);
  
  can_edit_image_entities = can_edit_node &&
    ((!scan.exists()) || (scan == node) || (scan == node.get_parent()) ||
     ((scan == node.get_parent().get_parent()) &&
      (node.get_parent().get_num_regions() > 0) &&
      (node == node.get_parent().get_descendant(0)) &&
      (node.get_parent().get_descendant(1) == 0)));
    // A node's image entity associations can be edited if:
    // a) it is not already associated with any image entities; or
    // b) the node itself is a numlist node; or
    // c) the node's parent is a numlist node; or
    // d) the node is the only immediate descendant of an ROI node, whose
    //    parent is a numlist node.
    // In case (a), a new numlist parent may be inserted after editing.
    // In case (b), the numlist is edited directly.
    // In case (c), the node may be moved to a new numlist parent, which may
    // cause the original numlist to be cleaned up, if the system detects that
    // it no longer plays any role (top-level numlist without descendants
    // or links).  Case (d) is the same as case (c), except that the ROI
    // parent node may need to be moved to a new numlist parent.
  
  can_edit_region = can_edit_node &&
    ((whole_image && (num_selected_codestreams > 0)) ||
     (node.get_num_regions() > 0) ||
     (node.get_parent().exists() &&
      (node == node.get_parent().get_descendant(0)) &&
      !node.get_parent().get_descendant(1)));
    // A node's region associations can be edited if:
    //   a) it is not already associated with a region of interest; or
    //   b) the node itself is an ROI node; or
    //   c) the node is the only immediate descendant of an ROI node.
    // In case (a), a new ROI parent may be inserted after editing.
    // In cases (b) and (c), the ROI node can be safely edited without
    // affecting the associations of siblings.
}

/*****************************************************************************/
/*                     kdws_metadata_dialog_state::copy                      */
/*****************************************************************************/

void kdws_metadata_dialog_state::copy(kdws_metadata_dialog_state *src)
{
  assert((num_codestreams == src->num_codestreams) &&
         (num_layers == src->num_layers));
  augment_max_selected_codestreams(src->num_selected_codestreams);
  augment_max_selected_layers(src->num_selected_layers);
  num_selected_codestreams = src->num_selected_codestreams;
  num_selected_layers = src->num_selected_layers;
  memcpy(selected_codestreams,src->selected_codestreams,
         (size_t)(sizeof(int)*num_selected_codestreams));
  memcpy(selected_layers,src->selected_layers,
         (size_t)(sizeof(int)*num_selected_layers));
  selected_container_id = src->selected_container_id;
  potential_container_id = src->potential_container_id;
  selected_container_min_base_layer = src->selected_container_min_base_layer;
  selected_container_min_base_stream = src->selected_container_min_base_stream;
  selected_container_num_base_layers=src->selected_container_num_base_layers;
  selected_container_num_base_streams=src->selected_container_num_base_streams;
  rendered_result = src->rendered_result;
  allow_edits = src->allow_edits;
  can_edit_node = src->can_edit_node;
  is_link = src->is_link;
  is_cross_reference = src->is_cross_reference;
  whole_image = src->whole_image;
  roi_editor.copy_from(src->roi_editor);
  num_offered_types = src->num_offered_types;
  selected_type = src->selected_type;
  external_file_replaces_contents = src->external_file_replaces_contents;
  this->set_external_file(src->external_file);
  set_label_string(src->label_string);
  for (int n=0; n < num_offered_types; n++)
    offered_types[n] = src->offered_types[n];
}

/*****************************************************************************/
/*            kdws_metadata_dialog_state::save_to_external_file              */
/*****************************************************************************/

bool kdws_metadata_dialog_state::save_to_external_file(
                                               kdws_metanode_edit_state *edit)
{
  if (external_file == NULL)
    set_external_file(file_server->retain_tmp_file(selected_type->file_suffix),
                      true);
  if ((selected_type->box_type == jp2_label_4cc) && (label_string != NULL))
    { // Save the label string
      FILE *file = fopen(external_file->get_pathname(),"w");
      if (file != NULL)
        {
          fwrite(label_string,1,strlen(label_string),file);
          fclose(file);
          return true;
        }
    }
  else if (edit->cur_node.exists())
    {
      jp2_input_box box;
      if (edit->cur_node.open_existing(box))
        {
          const char *fopen_mode = "w";
          if (selected_type->initializer == NULL)
            fopen_mode = "wb"; // Write unrecognized boxes as binary
          FILE *file = fopen(external_file->get_pathname(),fopen_mode);
          if (file != NULL)
            {
              kdu_byte buf[512];
              int xfer_bytes;
              while ((xfer_bytes = box.read(buf,512)) > 0)
                fwrite(buf,1,(size_t)xfer_bytes,file);
              fclose(file);
              return true;
            }
        }
    }
  return false;
}

/*****************************************************************************/
/*           kdws_metadata_dialog_state::internalize_label_node              */
/*****************************************************************************/

bool kdws_metadata_dialog_state::internalize_label_node()
{
  if ((external_file == NULL) ||
      (selected_type->box_type != jp2_label_4cc))
    return false;
  
  const char *pathname = external_file->get_pathname();
  FILE *fp = fopen(pathname,"r");
  if (fp == NULL)
    return false;
  
  char *tmp_label = new char[4097];
  size_t num_chars = fread(tmp_label,1,4097,fp);
  fclose(fp);
  if (num_chars > 4096)
    {
      dlg->interrogate_user(
                       "External file contains more than 4096 characters!  "
                       "Managing internal labels of this size can become "
                       "impractical -- you should consider using an XML "
                       "box for large amounts of text.","OK");
      delete[] tmp_label;
      return false;
    }
  tmp_label[num_chars] = '\0';
  set_label_string(tmp_label);
  delete[] tmp_label;
  set_external_file(NULL);
  external_file_replaces_contents = false;
  return true;
}

/*****************************************************************************/
/*                 kdws_metadata_dialog_state::save_metanode                 */
/*****************************************************************************/

bool
  kdws_metadata_dialog_state::save_metanode(kdws_metanode_edit_state *edit,
                                            kdws_metadata_dialog_state *ref,
                                            kdws_renderer *renderer)
{
  if (!can_edit_node)
    return false;

  assert(edit->cur_node.exists());
  jpx_metanode node = edit->cur_node;
  jpx_meta_manager manager = edit->meta_manager;
  
  bool changed_region = !compare_region_associations(ref);
  bool changed_image_entities = !compare_image_entity_associations(ref);
  const jpx_roi *roi_regions = NULL;
  int num_roi_regions=0;
  if (!whole_image)
    roi_regions = roi_editor.get_regions(num_roi_regions);
  kdws_file *existing_file = get_file_retained_by_node(node,file_server);
  if ((num_selected_codestreams == 0) &&
      (changed_image_entities || changed_region) &&
      ((roi_regions != NULL) || node.has_dependent_roi_nodes()))
    { 
      dlg->interrogate_user("Regions of interest should be associated with at "
                       "least one codestream index in order to have meaning.  "
                       "You have either introduced a region of interest or "
                       "removed a codestream association that creates "
                       "a conflict in this node or in one of its "
                       "descendants.","OK");
      return false;
    }
  
  jpx_metanode top_non_numlist;
  int num_cs, num_l; bool rres; // Dummy variables
  if (selected_container_id != ref->selected_container_id)
    { // See if this change is going to affect multiple number lists
      jpx_metanode scan;
      bool have_numlist = false;
      for (scan=node; scan.get_parent().exists(); scan=scan.get_parent())
        if (scan.get_numlist_info(num_cs,num_l,rres))
          have_numlist = true;
        else if ((scan != node) && (scan.get_num_regions() == 0))
          top_non_numlist = scan;
      if (top_non_numlist.exists())
        { 
          int num_affected = 0;
          top_non_numlist.count_numlist_descendants(num_affected);
          int other_affected = num_affected - ((have_numlist)?1:0);
          assert(other_affected >= 0);
          if ((other_affected > 0) &&
              !dlg->interrogate_user("You are about to change JPX container "
                                "embedding/association properties.  This "
                                "change will affect multiple number list "
                                "nodes that are currently descended from a "
                                "common metadata node -- the change is "
                                "unavoidable, because container embedding "
                                "can be expressed only at the top level of "
                                "the file.  If you are unsure, you may wish "
                                "to cancel this operation and click the "
                                "\"revert\" button.","Cancel","Proceed"))
            return false;
        }
    }
  
  bool roi_parent_removed = false;
  if (changed_image_entities)
    { // We have changed, added or removed image entity and/or container
      // associations.  May need to modify or create a suitable numlist node.
      if (selected_container_id != ref->selected_container_id)
        { 
          if ((selected_container_id < 0) && top_non_numlist.exists())
            { // Move the `top_non_numlist' node out from the shadow of its
              // container embedding.
              top_non_numlist.change_parent(manager.access_root());
            }
          if ((selected_container_id >= 0) && top_non_numlist.exists())
            { // Move `top_non_numlist' node to make it a descendant of a new
              // special embedding numlist
              jpx_metanode tmp =
                manager.insert_node(0,NULL,0,NULL,false,NULL,0,
                                    jpx_metanode(),selected_container_id);
              top_non_numlist.change_parent(tmp);
            }
        }
      assert(can_edit_image_entities);
      bool have_image_entities = ((num_selected_codestreams > 0) ||
                                  (num_selected_layers > 0) ||
                                  (selected_container_id >= 0) ||
                                  rendered_result);
      
      // One of the following must apply (see `configure_with_cur_node'):
      // a) `node' is not currently associated with any image entities;
      // b) `node' is a numlist node;
      // c) `node's parent is a numlist node; or
      // d) `node' is the only immediate descendant of an ROI node whose
      //    parent is a numlist node.
      jpx_metanode parent = node.get_parent();
      if (node.get_numlist_info(num_cs,num_l,rres))
        { // Case (b) -- changing `node' itself
          if (!have_image_entities)
            { // Remove the numlist altogether, moving its children up the
              // hierarchy and making them the new top-level metalist nodes
              // if necessary.
              bool at_root = edit->at_root();
              if (at_root)
                { 
                  edit->metalist->delete_item(edit->edit_item);
                  edit->edit_item = NULL;
                  edit->root_node = jpx_metanode();
                }
              edit->cur_node = jpx_metanode();
              jpx_metanode child;
              while ((child = node.get_descendant(0)).exists())
                { 
                  child.change_parent(parent);
                  if (at_root)
                    edit->metalist->append_node(child);
                  if (!edit->cur_node)
                    edit->cur_node = child;
                }
              node.delete_node();
              if (!edit->cur_node)
                edit->cur_node = manager.access_root();
              node = edit->cur_node;
            }
          else
            { // Insert a new numlist under the current parent
              node=manager.insert_node(num_selected_codestreams,
                                       selected_codestreams,
                                       num_selected_layers,selected_layers,
                                       rendered_result,NULL,0,parent,
                                       selected_container_id);
              if (node != edit->cur_node)
                { 
                  jpx_metanode child;
                  while ((child=edit->cur_node.get_descendant(0)).exists())
                    child.change_parent(node);
                  edit->cur_node.delete_node();
                  edit->cur_node = node;
                }
            }
        }
      else if (parent.get_numlist_info(num_cs,num_l,rres))
        { // Case (c)
          jpx_metanode ancestor = parent.get_parent();
          if ((selected_container_id != ref->selected_container_id) &&
              !top_non_numlist.exists())
            ancestor = manager.access_root(); // Build new numlist under root
          if (have_image_entities)
            ancestor =
              manager.insert_node(num_selected_codestreams,
                                  selected_codestreams,
                                  num_selected_layers,selected_layers,
                                  rendered_result,NULL,0,ancestor,
                                  selected_container_id);
          node.change_parent(ancestor);
        }
      else if (parent.get_num_regions() > 0)
        { // Case (d)
          jpx_metanode ancestor = parent.get_parent(); // Up to the numlist
          if (ancestor.get_parent().exists()) // Just in case
            ancestor = ancestor.get_parent(); // Up to the numlist's parent
          if ((selected_container_id != ref->selected_container_id) &&
              !top_non_numlist.exists())
            ancestor = manager.access_root(); // Build new numlist under root
          if (have_image_entities)
            ancestor =
              manager.insert_node(num_selected_codestreams,
                                  selected_codestreams,
                                  num_selected_layers,selected_layers,
                                  rendered_result,NULL,0,ancestor,
                                  selected_container_id);
          if (roi_regions == NULL)
            { 
              node.change_parent(ancestor);
              parent.delete_node();
              roi_parent_removed = true;
            }
          else
            parent.change_parent(ancestor);
        }
      else if (have_image_entities)
        { // Case (a)
          jpx_metanode new_parent =
            manager.insert_node(num_selected_codestreams,
                                selected_codestreams,
                                num_selected_layers,selected_layers,
                                rendered_result,NULL,0,parent,
                                selected_container_id);
          node.change_parent(new_parent);
        }
    }
  
  // At this point `node' has the right numlist associations, if any
  if (changed_region && !roi_parent_removed)
    { // We have changed, added or removed region of interest associations
      assert(can_edit_region);
      // One of the following must apply (see `configure_with_cur_node'):
      // a) `node' is not currently associated with any region of interest;
      // b) `node' is itself an ROI node; or
      // c) `node' is the only immediate descendant of an ROI node.
      jpx_metanode parent = node.get_parent();
      if (node.get_num_regions() > 0)
        { // Case (b)
          if (roi_regions == NULL)
            { // Delete the ROI node, after moving its children up and making
              // them the new metalist root nodes, if appropriate.
              bool at_root = edit->at_root();
              if (at_root)
                { 
                  edit->metalist->delete_item(edit->edit_item);
                  edit->edit_item = NULL;
                  edit->root_node = jpx_metanode();
                }
              edit->cur_node = jpx_metanode();
              jpx_metanode child;
              while ((child = node.get_descendant(0)).exists())
                { 
                  child.change_parent(parent);
                  if (at_root)
                    edit->metalist->append_node(child);
                  if (!edit->cur_node)
                    edit->cur_node = child;
                }
              node.delete_node();
              if (!edit->cur_node)
                edit->cur_node = manager.access_root();
              node = edit->cur_node;
            }
          else
            { // Create a new ROI node and move the original node's children
              // into it, deleting the original one.
              node = parent.add_regions(num_roi_regions,roi_regions);
              jpx_metanode child;
              while ((child=edit->cur_node.get_descendant(0)).exists())
                child.change_parent(node);
              edit->cur_node.delete_node();
              edit->cur_node = node;
            }
        }
      else if (parent.get_num_regions() > 0)
        { // Case (c)
          jpx_metanode container = parent.get_parent();
          if (roi_regions != NULL)
            container = container.add_regions(num_roi_regions,roi_regions);
          node.change_parent(container);
          parent.delete_node();
        }
      else if (roi_regions != NULL)
        { // Assume case (a)
          if (node.get_numlist_info(num_cs,num_l,rres))
            { // Place region of interest underneath `node' and move all
              // the other descendants of `node' unde the region of interest,
              // changing `node' to the new region of interest.
              if (node == edit->root_node)
                { // Make the new ROI the root of the list
                  edit->metalist->delete_item(edit->edit_item);
                  edit->edit_item = NULL;
                  edit->root_node = jpx_metanode();
                }
              jpx_metanode roi_node, child;
              roi_node = node.add_regions(num_roi_regions,roi_regions);
              while ((child = node.get_descendant(0)).exists() &&
                     (child != roi_node))
                child.change_parent(roi_node);
              edit->cur_node = node = roi_node;
            }
          else
            { 
              jpx_metanode container =
                parent.add_regions(num_roi_regions,roi_regions);
              node.change_parent(container);
            }
        }
      else
        assert(0); // How else could the region associations have changed??
    }
  
  if (external_file_replaces_contents)
    {
      node.change_to_delayed(selected_type->box_type,
                             external_file->get_id(),file_server);
      external_file->retain(); // Retain on behalf of the node.
      if (existing_file != NULL)
        existing_file->release();
      external_file_replaces_contents = false;
    }
  else if (label_string != NULL)
    { 
      assert(selected_type->box_type == jp2_label_4cc);
      node.change_to_label(label_string);
      if (existing_file != NULL)
        existing_file->release();
    }

  assert(node == edit->cur_node);
  edit->validate_metalist_and_root_node(); // In case something deleted
  kdws_matching_metanode *item = edit->metalist->find_container(node);
  if (item == NULL)
    item = edit->metalist->append_node(node);
  edit->edit_item = item;
  edit->root_node = item->node;
  edit->cur_node = node; // Just in case
  renderer->metadata_changed(false);
  configure_with_cur_node(edit);
  return true;
}

/*****************************************************************************/
/*        kdws_metadata_dialog_state::augment_max_selected_codestreams       */
/*****************************************************************************/

void kdws_metadata_dialog_state::augment_max_selected_codestreams(int capacity)
{
  if (capacity <= max_selected_codestreams)
    return;
  int n, new_max = max_selected_codestreams + capacity;
  int *new_blk = new int[new_max];
  for (n=0; n < num_selected_codestreams; n++)
    new_blk[n] = selected_codestreams[n];
  delete[] selected_codestreams;
  selected_codestreams = new_blk;
  max_selected_codestreams = new_max;
}

/*****************************************************************************/
/*          kdws_metadata_dialog_state::augment_max_selected_layers          */
/*****************************************************************************/

void kdws_metadata_dialog_state::augment_max_selected_layers(int capacity)
{
  if (capacity <= max_selected_layers)
    return;
  int n, new_max = max_selected_layers + capacity;
  int *new_blk = new int[new_max];
  for (n=0; n < num_selected_layers; n++)
    new_blk[n] = selected_layers[n];
  delete[] selected_layers;
  selected_layers = new_blk;
  max_selected_layers = new_max;
}


/* ========================================================================= */
/*                          kdws_metadata_editor                             */
/* ========================================================================= */

/*****************************************************************************/
/*                kdws_metadata_editor::kdws_metadata_editor                 */
/*****************************************************************************/

kdws_metadata_editor::kdws_metadata_editor(jpx_source *source,
                                           kdws_file_services *file_services,
                                           bool can_edit,
                                           kdws_renderer *renderer,
                                           POINT preferred_location,
                                           int window_identifier,
                                           CWnd* pParent)
{
  this->dialog_created = false;
  this->mapping_state_to_controls = false;

  this->source = source;
  this->editing_shape = false;
  this->shape_editing_mode = JPX_EDITOR_VERTEX_MODE;
  this->allow_edits = can_edit;
  this->file_server = file_services;
  this->renderer = renderer;

  num_codestreams = 1;
  num_layers = 1;
  edit_state = NULL;
  state = NULL;
  initial_state = NULL;
  owned_metalist = NULL;
  
  available_types[KDWS_LABEL_TEMPLATE].init(jp2_label_4cc,"txt",
                                            "<new label>");
  available_types[KDWS_XML_TEMPLATE].init(jp2_xml_4cc,"xml",
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r");
  available_types[KDWS_IPR_TEMPLATE].init(jp2_iprights_4cc,"xml",
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r");

  if (!this->Create(kdws_metadata_editor::IDD,pParent))
    { kdu_warning w;
      w << "Unable to load/create the metdata editor dialog.";
      return;
    }
  dialog_created = true;

  RECT frame_rect; GetWindowRect(&frame_rect);
  int screen_width = GetSystemMetrics(SM_CXSCREEN);
  int screen_height = GetSystemMetrics(SM_CYSCREEN);
  int x_off = preferred_location.x - frame_rect.left;
  int y_off = preferred_location.y - frame_rect.top;
  if ((frame_rect.right+x_off) > screen_width)
    x_off = screen_width - frame_rect.right;
  if ((frame_rect.bottom+y_off) > screen_height)
    y_off = screen_height - frame_rect.bottom;
  if ((frame_rect.left+x_off) < 0)
    x_off = -frame_rect.left;
  if ((frame_rect.top+y_off) < 0)
    y_off = -frame_rect.top;
  frame_rect.left += x_off;  frame_rect.right  += x_off;
  frame_rect.top  += y_off;  frame_rect.bottom += y_off;
  SetWindowPos(NULL,frame_rect.left,frame_rect.top,0,0,
               SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);

  char title[40];
  sprintf(title,"Metadata editor %d",window_identifier);
  SetWindowText(kdws_string(title));
  EnableToolTips(TRUE);
  roi_mode_popup()->SetCurSel(0);
  roi_elliptical_button()->SetCheck(BST_CHECKED);
}

/*****************************************************************************/
/*              kdws_metadata_editor::~kdws_metadata_editor                  */
/*****************************************************************************/

kdws_metadata_editor::~kdws_metadata_editor()
{
  if (edit_state != NULL)
    delete edit_state;
  if (state != NULL)
    delete state;
  if (initial_state != NULL)
    delete initial_state;
  if (owned_metalist != NULL)
    delete owned_metalist;
}

/*****************************************************************************/
/*                    kdws_metadata_editor:DestroyWindow                     */
/*****************************************************************************/

BOOL kdws_metadata_editor::DestroyWindow()
{
  if ((renderer != NULL) && (renderer->editor == this))
    renderer->editor = NULL;
  renderer = NULL;
  BOOL result = CDialog::DestroyWindow();
  delete this;
  return result;
}

/*****************************************************************************/
/*                     kdws_metadata_editor:preconfigure                     */
/*****************************************************************************/

void kdws_metadata_editor::preconfigure()
{
  assert(!editing_shape);
  source->count_codestreams(num_codestreams);
  if (num_codestreams < 1)
    num_codestreams = 1;
  source->count_compositing_layers(num_layers);
  if (num_layers < 1)
    num_layers = 1;

  if (edit_state != NULL)
    delete edit_state;
  edit_state = new
    kdws_metanode_edit_state(source,file_server);
  if (state != NULL)
    delete state;
  state = new
    kdws_metadata_dialog_state(this,num_codestreams,num_layers,
                               available_types,file_server,allow_edits);  
  if (initial_state != NULL)
    delete initial_state;
  initial_state = new
    kdws_metadata_dialog_state(this,num_codestreams,num_layers,
                               available_types,file_server,allow_edits);
  
  if (owned_metalist != NULL)
    delete owned_metalist;
  owned_metalist = NULL;

  roi_mode_popup()->ResetContent();
  roi_mode_popup()->AddString(_T("Vertex mode"));
  roi_mode_popup()->AddString(_T("Skeleton mode"));
  roi_mode_popup()->AddString(_T("Path mode"));
  roi_mode_popup()->SetCurSel(0);

  roi_elliptical_button()->SetCheck(BST_UNCHECKED);
  roi_path_width_field()->SetWindowText(_T("1"));
  roi_complexity_level()->SetRange(0,1000);
  roi_complexity_level()->SetPos(0);

  roi_scribble_accuracy_slider()->SetRange(0,1000);
  roi_scribble_accuracy_slider()->SetPos(500);

  map_state_to_controls();
}

/*****************************************************************************/
/*              kdws_metadata_editor::configure_with_edit_list               */
/*****************************************************************************/

void
  kdws_metadata_editor::configure_with_edit_list(kdws_matching_metalist *
                                                 metalist)
{
  if (!dialog_created)
    return;
  SetWindowPos(&wndTop,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
  if (!query_save())
    { 
      delete metalist;
      return;
    }
  stop_shape_editor();

  this->preconfigure();
  owned_metalist = metalist;
  
  edit_state->metalist = metalist;
  edit_state->edit_item = metalist->get_head();
  if (edit_state->edit_item == NULL)
    {
      assert(0);
      return;
    }
  edit_state->cur_node = edit_state->root_node = edit_state->edit_item->node;
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  map_state_to_controls();
}

/*****************************************************************************/
/*                   kdws_metadata_editor:set_active_node                    */
/*****************************************************************************/

void kdws_metadata_editor::set_active_node(jpx_metanode node)
{
  stop_shape_editor(); // Just in case
  if (owned_metalist == NULL)
    return;
  edit_state->move_to_node(node);
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();
}

/*****************************************************************************/
/*                 kdws_metadata_editor::interrogate_user                    */
/*****************************************************************************/

int kdws_metadata_editor::interrogate_user(const char *message,
                                           const char *option_0,
                                           const char *option_1,
                                           const char *option_2,
                                           const char *option_3)
{
  const char *caption = "\"Kdu_show\" metadata editor needs input";
  if (option_1 == NULL)
    caption = "\"Kdu_show\" metadata editor information";
  kdws_interrogate_dlg dialog(this,caption,message,
                              option_0,option_1,option_2,option_3);
  int result = (int) dialog.DoModal();
  return result;
}

/*****************************************************************************/
/*               kdws_metadata_editor::map_state_to_controls                 */
/*****************************************************************************/

void kdws_metadata_editor::map_state_to_controls()
{
  kdws_string string(80);
  int c, sel;

  mapping_state_to_controls = true;

  // Set data values
  sel = list_codestreams()->GetCurSel();
  list_codestreams()->ResetContent();
  for (c=0; c < state->num_selected_codestreams; c++)
    { 
      int delta, val=state->selected_codestreams[c];
      if ((state->selected_container_num_base_streams > 0) &&
          ((delta = val - state->selected_container_min_base_stream) >= 0) &&
          (delta < state->selected_container_num_base_streams))
        sprintf(string,"Stream %d+",val);
      else
        sprintf(string,"Stream %d",val);
      list_codestreams()->AddString(string);
    }
  if ((sel != LB_ERR) && (sel < c))
    list_codestreams()->SetCurSel(sel);
  
  sel = list_layers()->GetCurSel();
  list_layers()->ResetContent();
  for (c=0; c < state->num_selected_layers; c++)
    { 
      int delta, val=state->selected_layers[c];
      if ((state->selected_container_num_base_layers > 0) &&
          ((delta = val - state->selected_container_min_base_layer) >= 0) &&
          (delta < state->selected_container_num_base_layers))
        sprintf(string,"Layer %d+",val);
      else
        sprintf(string,"Layer %d",val);
      list_layers()->AddString(string);
    }
  if ((sel != LB_ERR) && (sel < c))
    list_layers()->SetCurSel(sel);

  container_embedding_button()->SetCheck(
    (state->selected_container_id >= 0)?BST_CHECKED:BST_UNCHECKED);
  rendered_result_button()->SetCheck(
    (state->rendered_result)?BST_CHECKED:BST_UNCHECKED);

  whole_image_button()->SetCheck(
    (state->whole_image)?BST_CHECKED:BST_UNCHECKED);
  kdu_dims bb;
  if (state->roi_editor.get_bounding_box(bb))
    {
      sprintf(string,"%d",bb.pos.x);   x_pos_field()->SetWindowText(string);
      sprintf(string,"%d",bb.pos.y);   y_pos_field()->SetWindowText(string);
      sprintf(string,"%d",bb.size.x);  width_field()->SetWindowText(string);
      sprintf(string,"%d",bb.size.y);  height_field()->SetWindowText(string);
    }
  else
    {
      x_pos_field()->SetWindowText(_T(""));
      y_pos_field()->SetWindowText(_T(""));
      width_field()->SetWindowText(_T(""));
      height_field()->SetWindowText(_T(""));
    }
  if (state->roi_editor.is_simple())
    { 
      int nregns;
      const jpx_roi *roi = state->roi_editor.get_regions(nregns);
      single_rectangle_button()->SetCheck(
        (roi->is_elliptical)?BST_UNCHECKED:BST_CHECKED);
      single_ellipse_button()->SetCheck(
        (roi->is_elliptical)?BST_CHECKED:BST_UNCHECKED);
    }
  else
    {
      single_rectangle_button()->SetCheck(BST_UNCHECKED);
      single_ellipse_button()->SetCheck(BST_UNCHECKED);
    }
  roi_encoded_button()->SetCheck(
    (state->roi_editor.contains_encoded_regions())?BST_CHECKED:BST_UNCHECKED);

  if (editing_shape)
    roi_shape_editor_button()->SetWindowText(_T("Cancel"));
  else if (!state->compare(initial_state))
    roi_shape_editor_button()->SetWindowText(_T("Apply & edit"));
  else
    roi_shape_editor_button()->SetWindowText(_T("Edit"));
  jpx_metanode_link_type link_type = JPX_METANODE_LINK_NONE;
  if (state->is_link)
    {
      is_link_button()->SetCheck(BST_CHECKED);
      if (edit_state->cur_node.exists())
        edit_state->cur_node.get_link(link_type);
    }
  else
    is_link_button()->SetCheck(BST_UNCHECKED);
  if (link_type == JPX_GROUPING_LINK)
    is_link_button()->SetWindowText(_T("Link node (grouping)"));
  else if (link_type == JPX_ALTERNATE_CHILD_LINK)
    is_link_button()->SetWindowText(_T("Link node (alt child)"));
  else if (link_type == JPX_ALTERNATE_PARENT_LINK)
    is_link_button()->SetWindowText(_T("Link node (alt parent)"));
  else
    is_link_button()->SetWindowText(_T("Not a link"));

  int t;
  box_type_popup()->ResetContent();
  for (t=0; t < state->num_offered_types; t++)
    box_type_popup()->AddString(
      kdws_string(state->offered_types[t]->box_type_string));
  for (t=0; t < state->num_offered_types; t++)
    if (state->offered_types[t] == state->selected_type)
      box_type_popup()->SetCurSel(t);
  
  box_editor_popup()->ResetContent();
  box_editor_popup()->AddString(_T("Choose in Finder"));
  kdws_file_editor *editor;
  for (t=0; (editor=file_server->get_editor_for_doc_type(
                           state->selected_type->file_suffix,t)) != NULL; t++)
    box_editor_popup()->AddString(kdws_string(editor->app_name));
  if (t > 0)
    box_editor_popup()->SetCurSel(1);
  
  kdws_file *external_file = state->get_external_file();
  if (external_file != NULL)
    external_file_field()->SetWindowText(
      kdws_string(external_file->get_pathname()));
  else
    external_file_field()->SetWindowText(_T(""));
  temporary_file_button()->SetCheck(
   ((external_file!=NULL) &&
    external_file->get_temporary())?BST_CHECKED:BST_UNCHECKED);

  label_field()->SetWindowText(kdws_string(state->get_label_string()));

  // Determine which controls should be enabled
  bool can_edit = state->can_edit_node;
  bool have_encoded_regions =
    state->roi_editor.contains_encoded_regions() && !state->whole_image;
  bool can_edit_region = can_edit && state->can_edit_region;
  bool can_edit_image_entities = can_edit && state->can_edit_image_entities;
  bool can_edit_codestreams = can_edit_image_entities && !have_encoded_regions;
  bool can_do_shape_editing = can_edit_region && (!state->whole_image) &&
    !state->roi_editor.contains_encoded_regions();

  add_stream_button()->EnableWindow(can_edit_codestreams?TRUE:FALSE);
  remove_stream_button()->EnableWindow(
    (can_edit_codestreams && (state->num_selected_codestreams>0))?TRUE:FALSE);
  codestream_stepper()->EnableWindow(can_edit_codestreams?TRUE:FALSE);

  add_layer_button()->EnableWindow(can_edit_image_entities?TRUE:FALSE);
  remove_layer_button()->EnableWindow(
    (can_edit_image_entities && (state->num_selected_layers>0))?TRUE:FALSE);
  compositing_layer_stepper()->EnableWindow(
    can_edit_image_entities?TRUE:FALSE);

  container_embedding_button()->EnableWindow(
    (can_edit_image_entities &&
     ((state->selected_container_id >= 0) ||
      (state->potential_container_id >= 0)))?TRUE:FALSE);
  rendered_result_button()->EnableWindow(can_edit_image_entities?TRUE:FALSE);

  bool roi_enable = can_edit_region &&
    !(state->whole_image || have_encoded_regions);
  x_pos_field()->SetReadOnly(TRUE);
  y_pos_field()->SetReadOnly(TRUE);
  width_field()->SetReadOnly(TRUE);
  height_field()->SetReadOnly(TRUE);
  single_rectangle_button()->EnableWindow((roi_enable)?TRUE:FALSE);
  single_ellipse_button()->EnableWindow((roi_enable)?TRUE:FALSE);
  whole_image_button()->EnableWindow(
    (can_edit_region &&
     ((!state->whole_image) ||
      (state->num_selected_codestreams != 0)))?TRUE:FALSE);
  roi_encoded_button()->EnableWindow(
    (can_edit_region && have_encoded_regions)?TRUE:FALSE);
  roi_mode_popup()->EnableWindow((roi_enable)?TRUE:FALSE);
  roi_shape_editor_button()->EnableWindow((can_do_shape_editing)?TRUE:FALSE);

  jp2_family_src *existing_src;
  bool can_save_to_file =
    ((state->get_external_file() != NULL) ||
     ((state->selected_type->box_type == jp2_label_4cc) &&
      (!state->is_label_string_empty()) &&
      !(state->is_link || state->is_cross_reference ||
        (state->selected_type->box_type == jp2_roi_description_4cc) ||
        (state->selected_type->box_type == jp2_number_list_4cc))) ||
     (edit_state->cur_node.exists() &&
      !edit_state->cur_node.get_existing(existing_src).is_null()));

  is_link_button()->EnableWindow(FALSE);
  box_type_popup()->EnableWindow(
    (can_edit && (state->num_offered_types > 1))?TRUE:FALSE);
  box_editor_popup()->EnableWindow((can_save_to_file)?TRUE:FALSE);
  save_to_file_button()->EnableWindow((can_save_to_file)?TRUE:FALSE);
  external_file_field()->SetReadOnly(TRUE);
  choose_file_button()->EnableWindow((can_save_to_file)?TRUE:FALSE);
  edit_file_button()->EnableWindow((can_edit && can_save_to_file)?TRUE:FALSE);

  internalize_label_button()->EnableWindow(
    ((external_file != NULL) && can_edit &&
     (state->selected_type->box_type==jp2_label_4cc))?TRUE:FALSE);

  copy_link_button()->EnableWindow(
   (edit_state->cur_node.exists() && allow_edits &&
    (state->is_link || !state->is_cross_reference))?TRUE:FALSE);
  paste_button()->EnableWindow(
    (allow_edits && renderer->catalog_source)?TRUE:FALSE);
  copy_descendants_button()->EnableWindow(
   (edit_state->cur_node.exists() && allow_edits &&
    (renderer->catalog_source != NULL))?TRUE:FALSE);

  label_field()->SetReadOnly(
    (state->can_edit_node &&
     (state->selected_type->box_type==jp2_label_4cc) &&
     (external_file == NULL))?FALSE:TRUE);

  apply_button()->EnableWindow((allow_edits)?TRUE:FALSE);
  delete_button()->EnableWindow((allow_edits)?TRUE:FALSE);
  if (state->is_link)
    delete_button()->SetWindowText(_T("Delete Link"));
  else
    delete_button()->SetWindowText(_T("Delete"));
  exit_button()->EnableWindow(TRUE);

  if (edit_state->at_root())
    {
      next_button()->EnableWindow(
       ((edit_state->edit_item != NULL) &&
        (edit_state->edit_item->next != NULL))?TRUE:FALSE);
      prev_button()->EnableWindow(
       ((edit_state->edit_item != NULL) &&
        (edit_state->edit_item->prev != NULL))?TRUE:FALSE);
      parent_button()->EnableWindow(FALSE);
    }
  else
    { 
      jpx_metanode parent = edit_state->cur_node.get_parent();
      bool have_next=parent.get_next_descendant(edit_state->cur_node).exists();
      bool have_prev=parent.get_prev_descendant(edit_state->cur_node).exists();
      next_button()->EnableWindow((have_next)?TRUE:FALSE);
      prev_button()->EnableWindow((have_prev)?TRUE:FALSE);
      parent_button()->EnableWindow(TRUE);
    }

  bool is_root = (state->selected_type->box_type == 0);
  bool have_children = edit_state->cur_node.exists() &&
    edit_state->cur_node.get_descendant(0).exists();
  descendants_button()->EnableWindow((have_children)?TRUE:FALSE);
  add_child_button()->EnableWindow((allow_edits)?TRUE:FALSE);
  add_parent_button()->EnableWindow((allow_edits && !is_root)?TRUE:FALSE);
  metashow_button()->EnableWindow((edit_state->cur_node.exists() &&
                                   !state->is_link)?TRUE:FALSE);
  catalog_button()->EnableWindow(
    (edit_state->cur_node.exists() &&
     (edit_state->cur_node.get_state_ref() != NULL))?TRUE:FALSE);

  update_apply_buttons();
  update_shape_editor_controls();

  mapping_state_to_controls = false;
}

/*****************************************************************************/
/*              kdws_metadata_editor::map_controls_to_state                  */
/*****************************************************************************/

void kdws_metadata_editor::map_controls_to_state()
{
  /* The following members have already been set in response to IBAction
   messages: `selected_codestreams', `selected_layers', `whole_image',
   `external_file' and `selected_type'.
   Here we find the remaining member values by querying the current state
   of the dialog controls. */
  if (mapping_state_to_controls)
    return;

  state->rendered_result = (rendered_result_button()->GetCheck()==BST_CHECKED);
  if (container_embedding_button()->GetCheck() == BST_CHECKED)
     state->select_potential_container_id(true,source);
  else
    state->select_potential_container_id(false,source);
  if ((state->selected_type->box_type == jp2_label_4cc) &&
      (state->get_external_file() == NULL))
    {
      int label_length = label_field()->LineLength();
      kdws_string str(label_length+8);
      label_field()->GetLine(0,str,label_length);
      state->set_label_string(str);
    }
  else
    state->set_label_string(NULL);
}

/*****************************************************************************/
/*               kdws_metadata_editor::update_apply_buttons                  */
/*****************************************************************************/

void kdws_metadata_editor::update_apply_buttons()
{
  bool something_to_apply = !state->compare(initial_state);
  if ((!something_to_apply) && editing_shape &&
      (apply_button()->IsWindowEnabled() ||
       (edit_state->active_shape_editor != state->roi_editor)))
    something_to_apply = true;
       // Note: in the above, the test for shape editor changes is skipped if
       // we already know that the Apply button is enabled -- this saves a
       // lot of work.  However, we do need to make sure that we update apply
       // buttons immediately before turning on the shape editor.  This is
       // done in `start_shape_editor'.
  apply_button()->EnableWindow((something_to_apply)?TRUE:FALSE);
  apply_and_exit_button()->EnableWindow((something_to_apply)?TRUE:FALSE);
  apply_button()->SetWindowText(_T("Apply"));
  apply_and_exit_button()->SetWindowText(_T("Apply + Exit"));
}

/*****************************************************************************/
/*           kdws_metadata_editor::update_shape_editor_controls              */
/*****************************************************************************/

void kdws_metadata_editor::update_shape_editor_controls()
{
  kdu_coords point;
  int num_point_instances = 0;
  bool have_selection =
    (edit_state->active_shape_editor.get_selection(point,
                                                   num_point_instances) >= 0);
  int num_regions=0;
  edit_state->active_shape_editor.get_regions(num_regions);
  int max_undo=0; bool can_redo=false;
  edit_state->active_shape_editor.get_history_info(max_undo,can_redo);
  
  roi_complexity_level()->EnableWindow((editing_shape)?TRUE:FALSE);
  if (editing_shape)
    {
      double complexity = 100.0 *
        edit_state->active_shape_editor.measure_complexity();
      roi_complexity_level()->SetPos((int)floor(10.0*complexity+0.5));
      kdws_string string(40);
      sprintf(string,"%4.1f%%",complexity);
      roi_complexity_value()->SetWindowText(string);
      roi_complexity_level()->SetBarColor(RGB(50,50,200));
    }
  else
    { 
      roi_complexity_level()->SetPos(0);
      roi_complexity_level()->SetWindowText(_T(""));
      roi_complexity_level()->SetBarColor(RGB(100,100,100));
    }

  roi_add_region_button()->EnableWindow(
    (editing_shape && have_selection)?TRUE:FALSE);
  roi_elliptical_button()->EnableWindow((editing_shape)?TRUE:FALSE);
  if (shape_editing_mode == JPX_EDITOR_PATH_MODE)
    roi_elliptical_button()->SetWindowText(_T("+junctions"));
  else
    roi_elliptical_button()->SetWindowText(_T("Ellipses"));
  roi_delete_region_button()->EnableWindow(
   (editing_shape && have_selection && (num_regions > 1))?TRUE:FALSE);
  roi_split_vertices_button()->EnableWindow(
   (editing_shape && have_selection && (num_point_instances > 1))?TRUE:FALSE); 
  bool path_mode = (shape_editing_mode == JPX_EDITOR_PATH_MODE);
  roi_path_width_field()->EnableWindow(
   (editing_shape && path_mode && (fixed_shape_path_thickness==0))?TRUE:FALSE);
  roi_set_path_width_button()->EnableWindow(
    (editing_shape && path_mode)?TRUE:FALSE);
  bool have_closed_path_to_fill = false;
  if (editing_shape && path_mode)
    {
      kdu_uint32 path_flags[8]={0,0,0,0,0,0,0,0}; kdu_byte members[256];
      kdu_coords start_pt, end_pt;
      while (edit_state->active_shape_editor.enum_paths(path_flags,members,
                                                        start_pt,end_pt) > 0)
        if (start_pt == end_pt)
          { have_closed_path_to_fill = true; break; }
    }
  roi_fill_path_button()->EnableWindow((have_closed_path_to_fill)?TRUE:FALSE);
  if (fixed_shape_path_thickness == 0)
    roi_set_path_width_button()->SetWindowText(_T("Fix path width"));
  else
    roi_set_path_width_button()->SetWindowText(_T("Release path width"));
  roi_undo_button()->EnableWindow((editing_shape && (max_undo>0))?TRUE:FALSE);
  roi_redo_button()->EnableWindow((editing_shape && can_redo)?TRUE:FALSE);

  int num_scribble_points = 0;
  if (shape_scribbling_active)
    edit_state->active_shape_editor.get_scribble_points(num_scribble_points);
  roi_scribble_button()->EnableWindow((editing_shape)?TRUE:FALSE);
  roi_scribble_replaces_button()->EnableWindow((editing_shape)?TRUE:FALSE);
  roi_scribble_convert_button()->EnableWindow(
   (editing_shape && (num_scribble_points > 0))?TRUE:FALSE);
  roi_scribble_accuracy_slider()->EnableWindow(
   (editing_shape && (num_scribble_points > 0))?TRUE:FALSE);
  roi_scribble_button()->SetWindowText(
   (shape_scribbling_active)?_T("Stop scribble"):_T("Start scribble"));
  roi_scribble_convert_button()->SetWindowText(
   (path_mode)?_T("Scribble -> path"):_T("Scribble -> fill"));

  update_apply_buttons();
}

/*****************************************************************************/
/*                kdws_metadata_editor::start_shape_editor                   */
/*****************************************************************************/

void kdws_metadata_editor::start_shape_editor()
{
  if (editing_shape || (!state->can_edit_region) ||
      state->roi_editor.contains_encoded_regions())
    return;
  update_apply_buttons(); // See `update_apply_buttons' for why we
                          // need to do this as a first step.
  map_controls_to_state();
  kdu_istream_ref istream_ref;
  renderer->stop_animation();
  if (!state->compare(initial_state))
    { // Must apply changes first
      try {
        if (!state->save_metanode(edit_state,initial_state,renderer))
          { 
            update_apply_buttons();
            return;
          }
      }
      catch (kdu_exception) {
        close();
        return;
      }
      state->configure_with_cur_node(edit_state);
      initial_state->copy(state);
    }      
  edit_state->active_shape_editor.copy_from(state->roi_editor);
  edit_state->active_shape_editor.set_max_undo_history(4096);
  int idx = roi_mode_popup()->GetCurSel();
  if (idx == 0)
    shape_editing_mode = JPX_EDITOR_VERTEX_MODE;
  else if (idx == 1)
    shape_editing_mode = JPX_EDITOR_SKELETON_MODE;
  else if (idx == 2)
    shape_editing_mode = JPX_EDITOR_PATH_MODE;
  hide_shape_paths = (shape_editing_mode != JPX_EDITOR_PATH_MODE);
  shape_scribbling_active = false;
  fixed_shape_path_thickness = 0;
  edit_state->active_shape_editor.set_mode(shape_editing_mode);
  if (renderer->show_imagery_for_metanode(edit_state->cur_node,
                                          &istream_ref) &&
      istream_ref.exists() &&
      renderer->set_shape_editor(&(edit_state->active_shape_editor),
                                 istream_ref,hide_shape_paths,
                                 shape_scribbling_active))
    editing_shape = true;
  else if (!istream_ref)
    interrogate_user("Cannot find (and display) the imagery for "
                     "this ROI description node, which is an essential "
                     "preliminary for launching the shape editor.  "
                     "Perhaps the codestream associations are invalid??",
                     "OK");
  else
    interrogate_user("Cannot start the shape editor for some reason.  "
                     "Most likely this ROI description node is too "
                     "complex for the current implementation of the "
                     "shape editor.","OK");
  map_state_to_controls();
}

/*****************************************************************************/
/*                 kdws_metadata_editor::stop_shape_editor                   */
/*****************************************************************************/

void kdws_metadata_editor::stop_shape_editor()
{
  shape_scribbling_active = false;
  if (!editing_shape)
    return;
  if (renderer != NULL)
    {
      renderer->set_shape_editor(NULL);
      renderer->set_focus_box(kdu_dims(),jpx_metanode());
    }
  editing_shape = false;
}

/*****************************************************************************/
/*              kdws_metadata_editor::auto_start_shape_editor                */
/*****************************************************************************/

void kdws_metadata_editor::auto_start_shape_editor()
{
  if ((!editing_shape) && state->can_edit_region &&
      (state->selected_type->box_type == jp2_roi_description_4cc) &&
      !state->roi_editor.contains_encoded_regions())
    this->start_shape_editor();
}

/*****************************************************************************/
/*                     kdws_metadata_editor::query_save                      */
/*****************************************************************************/

bool kdws_metadata_editor::query_save()
{
  if (state == NULL)
    return true;
  bool request_save = false;
  bool request_discard = false;
  if (editing_shape &&
      (edit_state->active_shape_editor != state->roi_editor))
    { 
      int response =
        interrogate_user("You appear to be in the middle of editing a "
                         "region's shape.  The action you have requested "
                         "requires these changes to be saved or discarded.",
                         "Ignore action","Save edits","Discard edits");
      if (response == 0)
        return false;
      if (response == 1)
        { 
          request_save = true;
          if (!query_save_shape_edits())
            return false;
        }
      else
        request_discard = true;
    }
  stop_shape_editor();
  
  if ((!(request_save || request_discard)) &&
      (initial_state != NULL) && !state->compare(initial_state))
    { 
      int response =
        interrogate_user("You have made some changes; the action you have "
                         "requested requires the current editing state to "
                         "be saved before proceeding.",
                         "Ignore action","Save edits","Discard edits");
      if (response == 0)
        return false;
      else if (response == 1)
        request_save = true;
      else
        request_discard = true;
    }
  if (request_discard)
    initial_state->copy(state);
  else if (request_save)
    { 
      map_controls_to_state();
      try {
        if (!state->save_metanode(edit_state,initial_state,renderer))
          { 
            update_apply_buttons();
            return false;
          }
      }
      catch (kdu_exception)
      { 
        close();
        return false;
      }
      initial_state->copy(state);
    }
  map_state_to_controls();
  return true;
}

/*****************************************************************************/
/*               kdws_metadata_editor::query_save_shape_edits                */
/*****************************************************************************/

bool kdws_metadata_editor::query_save_shape_edits()
{
  if (!editing_shape)
    true;
  if (edit_state->active_shape_editor.measure_complexity() > 1.0)
    {
      if (!interrogate_user("The edited shape configuration exceeds the "
                            "number of region elements which can be saved "
                            "in a JPX ROI Description box.  The region "
                            "will not be rendered completely.  You should "
                            "delete some quadrilateral/elliptical regions "
                            "or simplify them to horizontally/vertically "
                            "aligned rectangles or ellipses (these require "
                            "fewer elements to represent).",
                            "Keep editing","Discard edits"))
        return false;
    }
  else
    state->roi_editor.copy_from(edit_state->active_shape_editor);
  stop_shape_editor();
  return true;
}

/*****************************************************************************/
/*                  MESSAGE MAP for kdws_metadata_editor                     */
/*****************************************************************************/

BEGIN_MESSAGE_MAP(kdws_metadata_editor, CDialog)
  ON_WM_CTLCOLOR()
  ON_NOTIFY(TTN_GETDISPINFO, 0, get_tooltip_dispinfo)

	ON_NOTIFY(UDN_DELTAPOS, IDC_META_SPIN_STREAM, OnDeltaposMetaSpinStream)
	ON_NOTIFY(UDN_DELTAPOS, IDC_META_SPIN_LAYER, OnDeltaposMetaSpinLayer)
	ON_BN_CLICKED(IDC_META_REMOVE_STREAM, OnMetaRemoveStream)
	ON_BN_CLICKED(IDC_META_ADD_STREAM, OnMetaAddStream)
	ON_BN_CLICKED(IDC_META_REMOVE_LAYER, OnMetaRemoveLayer)
	ON_BN_CLICKED(IDC_META_ADD_LAYER, OnMetaAddLayer)
	ON_BN_CLICKED(IDC_META_WHOLE_IMAGE, clickedWholeImage)
	ON_BN_CLICKED(IDC_META_ENCODED, clearROIEncoded)
	ON_BN_CLICKED(IDC_META_RECTANGULAR, setSingleRectangle)
	ON_BN_CLICKED(IDC_META_ELLIPTICAL, setSingleEllipse)

	ON_BN_CLICKED(IDC_META_ROI_EDIT_SHAPE, editROIShape)
	ON_CBN_SELCHANGE(IDC_META_ROI_MODE, changeROIMode)
	ON_BN_CLICKED(IDC_META_ROI_ADD_REGION, addROIRegion)
	ON_BN_CLICKED(IDC_META_ROI_DELETE_REGION, deleteROIRegion)
	ON_BN_CLICKED(IDC_META_ROI_SPLIT_VERTICES, splitROIAnchor)
	ON_BN_CLICKED(IDC_META_ROI_UNDO, undoROIEdit)
	ON_BN_CLICKED(IDC_META_ROI_REDO, redoROIEdit)
	ON_BN_CLICKED(IDC_META_ROI_SET_PATH_WIDTH, setROIPathWidth)
	ON_BN_CLICKED(IDC_META_ROI_FILL_PATH, fillROIPath)
	ON_BN_CLICKED(IDC_META_ROI_SCRIBBLE, startScribble)
	ON_BN_CLICKED(IDC_META_ROI_SCRIBBLE_CONVERT, convertScribble)

	ON_CBN_SELCHANGE(IDC_META_BOX_TYPE, changeBoxType)
	ON_BN_CLICKED(IDC_META_SAVE_TO_FILE, saveToFile)
	ON_BN_CLICKED(IDC_META_CHOOSE_FILE, chooseFile)
	ON_BN_CLICKED(IDC_META_EDIT_FILE, editFile)
	ON_CBN_SELCHANGE(IDC_META_BOX_EDITOR, changeEditor)
	ON_BN_CLICKED(IDC_META_INTERNALIZE_LABEL, internalizeLabel)

	ON_BN_CLICKED(IDC_META_COPY_LINK, clickedCopyLink)
	ON_BN_CLICKED(IDC_META_PASTE, clickedPaste)
	ON_BN_CLICKED(IDC_META_COPY_DESCENDANTS, clickedCopyDescendants)

	ON_BN_CLICKED(IDC_META_CANCEL, clickedExit)
	ON_BN_CLICKED(IDC_META_DELETE, clickedDelete)
	ON_BN_CLICKED(IDC_META_APPLY, clickedApply)
	ON_BN_CLICKED(IDC_META_APPLY_AND_EXIT, clickedApplyAndExit)
	ON_BN_CLICKED(IDC_META_NEXT, clickedNext)
	ON_BN_CLICKED(IDC_META_PREV, clickedPrev)
	ON_BN_CLICKED(IDC_META_PARENT, clickedParent)
	ON_BN_CLICKED(IDC_META_DESCENDANTS, clickedDescendants)
	ON_BN_CLICKED(IDC_META_ADD_CHILD, clickedAddChild)
	ON_BN_CLICKED(IDC_META_ADD_PARENT, clickedAddParent)

	ON_BN_CLICKED(IDC_META_METASHOW, findInMetashow)
	ON_BN_CLICKED(IDC_META_CATALOG, findInCatalog)

  ON_BN_CLICKED(IDC_META_REVERT, clickedRevert)
	ON_EN_CHANGE(IDC_META_XPOS, otherAction)
	ON_EN_CHANGE(IDC_META_WIDTH, otherAction)
	ON_EN_CHANGE(IDC_META_YPOS, otherAction)
	ON_EN_CHANGE(IDC_META_HEIGHT, otherAction)
	ON_EN_CHANGE(IDC_META_LABEL, otherAction)
  ON_BN_CLICKED(IDC_META_RENDERED, otherAction)
  ON_BN_CLICKED(IDC_META_CONTAINER_EMBEDDING, clickedContainerEmbedding)
END_MESSAGE_MAP()

/*****************************************************************************/
/*                     kdws_metadata_editor::OnCtlColor                      */
/*****************************************************************************/

HBRUSH kdws_metadata_editor::OnCtlColor(CDC *dc, CWnd *wnd, UINT control_type)
{
  // Call the base implementation first, to get a default brush
  HBRUSH brush = CDialog::OnCtlColor(dc,wnd,control_type);
  int id = wnd->GetDlgCtrlID();
  if ((id == IDC_META_XPOS) || (id == IDC_META_YPOS) ||
      (id == IDC_META_WIDTH) || (id == IDC_META_HEIGHT) ||
      (id == IDC_META_LABEL) || (id == IDC_META_IS_LINK))
    {
      COLORREF link_status_text_colour = RGB(0,0,0);
      if ((state != NULL) && state->is_link)
        link_status_text_colour = RGB(0,0,255);
      dc->SetTextColor(link_status_text_colour);
    }
  else if ((id == IDC_META_LINK_EXPLANATION) ||
           (id == IDC_META_COPY_CHILDREN_EXPLANATION))
    { 
      COLORREF red_text = RGB(128,0,0);
      dc->SetTextColor(red_text);
    }
  return brush;
}

/*****************************************************************************/
/*                kdws_metadata_editor::get_tooltip_dispinfo                 */
/*****************************************************************************/

void kdws_metadata_editor::get_tooltip_dispinfo(NMHDR* pNMHDR,
                                                LRESULT *pResult)
{
  NMTTDISPINFO *pTTT = (NMTTDISPINFO *) pNMHDR;
  UINT_PTR nID =pNMHDR->idFrom;
  if (pTTT->uFlags & TTF_IDISHWND)
    {
        // idFrom is actually the HWND of the tool
        nID = ::GetDlgCtrlID((HWND)nID);
        if(nID)
          {
            int num_chars =
              ::LoadString(AfxGetResourceHandle(),
                           (UINT) nID,this->tooltip_buffer,
                           KD_METAEDIT_MAX_TOOLTIP_CHARS);
            if (num_chars > 0)
              {
                pTTT->lpszText = this->tooltip_buffer;
                pTTT->hinst = NULL;
              }
            ::SendMessage(pNMHDR->hwndFrom,TTM_SETMAXTIPWIDTH,0,300);
          }
    }
}

/*****************************************************************************/
/*         kdws_metadata_editor:[ACTIONS for Association Editing]            */
/*****************************************************************************/

void
  kdws_metadata_editor::OnMetaRemoveStream() 
{
  if (state->roi_editor.contains_encoded_regions() ||
      (state->num_selected_codestreams == 0) ||
      !state->can_edit_image_entities)
    return;
  int n, idx = list_codestreams()->GetCurSel();
  if ((idx == LB_ERR) || (idx >= state->num_selected_codestreams))
    idx = state->num_selected_codestreams-1;
  map_controls_to_state();
  for (n=idx+1; n < state->num_selected_codestreams; n++)
    state->selected_codestreams[n-1] = state->selected_codestreams[n];
  state->num_selected_codestreams--;
  if (state->selected_container_id < 0)
    state->find_potential_container_id(source);
  map_state_to_controls();
}

void
  kdws_metadata_editor::OnMetaAddStream() 
{
  if (state->roi_editor.contains_encoded_regions() ||
      !state->can_edit_image_entities)
    return;
  map_controls_to_state();
  int idx = state->add_selected_codestream(-1,source);
  if (idx < 0)
    return; // Nothing more can be added
  map_state_to_controls();
  if (state->selected_container_id < 0)
    state->find_potential_container_id(source);
  list_codestreams()->SetCurSel(idx);
}

void
  kdws_metadata_editor::OnDeltaposMetaSpinStream(NMHDR* pNMHDR,
                                                 LRESULT* pResult) 
{
  if (state->roi_editor.contains_encoded_regions() ||
      !state->can_edit_image_entities)
    return;
  NM_UPDOWN* pNMUpDown = (NM_UPDOWN*)pNMHDR;
  if (state->num_selected_codestreams == 0)
    {
      OnMetaAddStream();
      return;
    }
  int idx = list_codestreams()->GetCurSel();
  if ((idx == LB_ERR) || (idx >= state->num_selected_codestreams))
    idx = state->num_selected_codestreams-1;
  map_controls_to_state();
  int old_val = state->selected_codestreams[idx];
  int new_val = state->make_compatible_stream(old_val,-pNMUpDown->iDelta,
                                              source);
  state->selected_codestreams[idx] = new_val;
  map_state_to_controls();
  list_codestreams()->SetCurSel(idx);
  *pResult = 0;
}

void
  kdws_metadata_editor::OnMetaRemoveLayer() 
{
  if ((state->num_selected_layers == 0) || !state->can_edit_image_entities)
    return;
  int n, idx = list_layers()->GetCurSel();
  if ((idx == LB_ERR) || (idx >= state->num_selected_layers))
    idx = state->num_selected_layers-1;
  map_controls_to_state();
  for (n=idx+1; n < state->num_selected_layers; n++)
    state->selected_layers[n-1] = state->selected_layers[n];
  state->num_selected_layers--;
  if (state->selected_container_id < 0)
    state->find_potential_container_id(source);
  map_state_to_controls();
}

void
  kdws_metadata_editor::OnMetaAddLayer() 
{
  if (!state->can_edit_image_entities)
    return;
  map_controls_to_state();
  int idx = state->add_selected_layer(-1,source);
  if (idx < 0)
    return; // Nothing more can be added
  map_state_to_controls();
  if (state->selected_container_id < 0)
    state->find_potential_container_id(source);
  list_layers()->SetCurSel(idx);
}

void
  kdws_metadata_editor::OnDeltaposMetaSpinLayer(NMHDR* pNMHDR,
                                                LRESULT* pResult) 
{
  if (!state->can_edit_image_entities)
    return;
  NM_UPDOWN* pNMUpDown = (NM_UPDOWN*)pNMHDR;
  if (state->num_selected_layers == 0)
    {
      OnMetaAddLayer();
      return;
    }
  int idx = list_layers()->GetCurSel();
  if ((idx == LB_ERR) || (idx >= state->num_selected_layers))
    idx = state->num_selected_layers-1;
  map_controls_to_state();
  int old_val = state->selected_layers[idx];
  int new_val = state->make_compatible_layer(old_val,-pNMUpDown->iDelta,
                                             source);
  state->selected_layers[idx] = new_val;
  map_state_to_controls();
  list_layers()->SetCurSel(idx);
  *pResult = 0;
}

void
  kdws_metadata_editor::clickedContainerEmbedding()
{
  if (!state->can_edit_image_entities)
    return;
  map_controls_to_state();
  map_state_to_controls();
}

void kdws_metadata_editor::clickedWholeImage() 
{
  if ((!state->can_edit_region) ||
      (state->whole_image && (state->num_selected_codestreams == 0)))
    return;
  state->whole_image = (whole_image_button()->GetCheck() == BST_CHECKED);
  if (editing_shape && state->whole_image)
    stop_shape_editor();
  else if (!(state->whole_image || editing_shape))
    { 
      if (state->roi_editor.is_empty())
        { // Initialize with a centred rectangle that is half the size of
          // the whole image.
          assert(state->num_selected_codestreams > 0);
          kdu_dims rect;
          if (!renderer->get_codestream_size(rect.size,
                                             state->selected_codestreams[0]))
            { 
              MessageBeep(MB_ICONEXCLAMATION);
              state->whole_image = true;
              return;
            }
          rect.pos.x = rect.size.x >> 2;
          rect.pos.y = rect.size.y >> 2;
          rect.size.x >>= 1;
          rect.size.y >>= 1;
          jpx_roi regn; regn.init_rectangle(rect);
          state->roi_editor.init(&regn,1);
        }
      auto_start_shape_editor();
    }
  map_controls_to_state();
  map_state_to_controls();
}

void kdws_metadata_editor::clearROIEncoded() 
{
  if (!(state->can_edit_region &&
        state->roi_editor.contains_encoded_regions()))
    return;
  if (interrogate_user("The node you are editing is associated with regions "
                        "of interest that are explicitly encoded with higher "
                        "priority in the codestream.  Removing the encoded "
                        "status will allow you to edit the region shape and "
                        "codestream associations but lose the connection "
                        "with codestream encoding properties.  Are you sure "
                        "you want to proceed?","Cancel","Proceed"))
    return;
  stop_shape_editor();
  int n, num_regions=0;
  const jpx_roi *regions = state->roi_editor.get_regions(num_regions);
  for (n=0; n < num_regions; n++)
    if (regions[n].is_encoded)
      {
        jpx_roi mod_region = regions[n];
        mod_region.is_encoded = false;
        state->roi_editor.modify_region(n,mod_region);
      }
  auto_start_shape_editor();
  map_controls_to_state();
  map_state_to_controls();	
}

void kdws_metadata_editor::setSingleRectangle() 
{
  map_controls_to_state();
  if (state->roi_editor.contains_encoded_regions() || !state->can_edit_region)
    return;
  stop_shape_editor();
  int nregns=0;
  const jpx_roi *roi = state->roi_editor.get_regions(nregns);
  if (roi != NULL)
    { 
      jpx_roi regn = *roi;
      regn.is_elliptical = false; regn.flags &= ~JPX_QUADRILATERAL_ROI;
      state->roi_editor.init(&regn,1);
    }
  auto_start_shape_editor();
  map_state_to_controls();
}

void kdws_metadata_editor::setSingleEllipse() 
{
  map_controls_to_state();
  if (state->roi_editor.contains_encoded_regions() || !state->can_edit_region)
    return;
  stop_shape_editor();
  int nregns=0;
  const jpx_roi *roi = state->roi_editor.get_regions(nregns);
  if (roi != NULL)
    { 
      jpx_roi regn = *roi;
      regn.is_elliptical = true; regn.flags &= ~JPX_QUADRILATERAL_ROI;
      state->roi_editor.init(&regn,1);
    }
  auto_start_shape_editor();
  map_state_to_controls();
}

/*****************************************************************************/
/*          kdws_metadata_editor:[ACTIONS for ROI shape editing]             */
/*****************************************************************************/

void kdws_metadata_editor::editROIShape()
{
  if (!state->can_edit_region)
    return;
  map_controls_to_state();
  kdu_istream_ref istream_ref;
  if (editing_shape)
    { 
      stop_shape_editor();
      map_state_to_controls();
    }
  else
    start_shape_editor();
}

void kdws_metadata_editor::changeROIMode()
{
  if (!editing_shape)
    return;
  int idx = roi_mode_popup()->GetCurSel();
  jpx_roi_editor_mode mode = shape_editing_mode;
  if (idx == 0)
    mode = JPX_EDITOR_VERTEX_MODE;
  else if (idx == 1)
    mode = JPX_EDITOR_SKELETON_MODE;
  else if (idx == 2)
    mode = JPX_EDITOR_PATH_MODE;
  if (mode == shape_editing_mode)
    return;
  
  hide_shape_paths = (mode != JPX_EDITOR_PATH_MODE);
  fixed_shape_path_thickness = 0;
  shape_editing_mode = mode;
  kdu_dims update_dims =
    edit_state->active_shape_editor.set_mode(shape_editing_mode);
  if (!update_dims.is_empty())
    renderer->update_shape_editor(update_dims,hide_shape_paths,
                                  fixed_shape_path_thickness,
                                  shape_scribbling_active);
  update_shape_editor_controls();
}

void kdws_metadata_editor::addROIRegion()
{
  if (!editing_shape)
    return;
  bool ellipses = (roi_elliptical_button()->GetCheck() == BST_CHECKED);
  kdu_dims visible_frame = renderer->get_visible_shape_region();
  kdu_dims update_dims =
    edit_state->active_shape_editor.add_region(ellipses,visible_frame);
  if (!update_dims.is_empty())
    renderer->update_shape_editor(update_dims,hide_shape_paths,
                                  fixed_shape_path_thickness,
                                  shape_scribbling_active);
  update_shape_editor_controls();
}

void kdws_metadata_editor::deleteROIRegion()
{
  if (!editing_shape)
    return;
  kdu_dims update_dims =
    edit_state->active_shape_editor.delete_selected_region();
  if (!update_dims.is_empty())
    { 
      update_dims.augment(renderer->shape_editor_adjust_path_thickness());
      renderer->update_shape_editor(update_dims,hide_shape_paths,
                                    fixed_shape_path_thickness,
                                    shape_scribbling_active);
    }
  update_shape_editor_controls();
}

void kdws_metadata_editor::splitROIAnchor()
{
  if (!editing_shape)
    return;
  kdu_dims update_dims =
    edit_state->active_shape_editor.split_selected_anchor();
  if (!update_dims.is_empty())
    { 
      update_dims.augment(renderer->shape_editor_adjust_path_thickness());
      renderer->update_shape_editor(update_dims,hide_shape_paths,
                                    fixed_shape_path_thickness,
                                    shape_scribbling_active);
    }
  update_shape_editor_controls();
}

void kdws_metadata_editor::setROIPathWidth()
{
  if (!editing_shape)
    return;
  if (fixed_shape_path_thickness > 0)
    { // Must be releasing the path thickness
      fixed_shape_path_thickness = 0;
      renderer->update_shape_editor(kdu_dims(),hide_shape_paths,0,
                                    shape_scribbling_active);
      update_shape_editor_controls();
      return;
    }
  if (!check_integer_in_range(this,roi_path_width_field(),1,255))
    return;
  int thickness = 0;
  get_nonneg_integer(roi_path_width_field(),thickness);
  if (thickness < 1) thickness = 1;
  if (thickness > 255) thickness = 255;
  bool success = false;
  kdu_dims update_dims =
    edit_state->active_shape_editor.set_path_thickness(thickness,success);
  fixed_shape_path_thickness = (success)?thickness:0;
  if (!update_dims.is_empty())
    renderer->update_shape_editor(update_dims,hide_shape_paths,
                                  fixed_shape_path_thickness,
                                  shape_scribbling_active);
  if (!success)
    MessageBeep(MB_ICONEXCLAMATION);
  update_shape_editor_controls();
}

void kdws_metadata_editor::fillROIPath()
{
  if (!editing_shape)
    return;
  bool success = false;
  kdu_dims update_dims =
    edit_state->active_shape_editor.fill_closed_paths(success);
  if (!update_dims.is_empty())
    { 
      shape_editing_mode = JPX_EDITOR_VERTEX_MODE;
      hide_shape_paths = true;
      fixed_shape_path_thickness = 0;
      update_dims.augment(
          edit_state->active_shape_editor.set_mode(shape_editing_mode));
      renderer->update_shape_editor(update_dims,hide_shape_paths,
                                    fixed_shape_path_thickness,
                                    shape_scribbling_active);
      roi_mode_popup()->SetCurSel(0);
    }
  if (!success)
    MessageBeep(MB_ICONEXCLAMATION);
  update_shape_editor_controls();
}

void kdws_metadata_editor::undoROIEdit()
{
  if (!editing_shape)
    return;
  kdu_dims update_dims = edit_state->active_shape_editor.undo();
  if (shape_scribbling_active)
    { 
      int num_points=0;
      edit_state->active_shape_editor.get_scribble_points(num_points);
      if (num_points == 0)
        shape_scribbling_active = false;
    }
  if (!update_dims.is_empty())
    renderer->update_shape_editor(update_dims,hide_shape_paths,
                                  fixed_shape_path_thickness,
                                  shape_scribbling_active);
  update_shape_editor_controls();
}

void kdws_metadata_editor::redoROIEdit()
{
  if (!editing_shape)
    return;
  kdu_dims update_dims = edit_state->active_shape_editor.redo();
  if (shape_scribbling_active)
    { 
      int num_points=0;
      edit_state->active_shape_editor.get_scribble_points(num_points);
      if (num_points == 0)
        shape_scribbling_active = false;
    }
  if (!update_dims.is_empty())
    renderer->update_shape_editor(update_dims,hide_shape_paths,
                                  fixed_shape_path_thickness,
                                  shape_scribbling_active);
  update_shape_editor_controls();
}

void kdws_metadata_editor::startScribble()
{
  if (!editing_shape)
    return;
  kdu_dims update_dims;
  if (shape_scribbling_active)
    { 
      update_dims = edit_state->active_shape_editor.clear_scribble_points();
      shape_scribbling_active = false;
    }
  else
    shape_scribbling_active = true;
  renderer->update_shape_editor(update_dims,hide_shape_paths,
                                fixed_shape_path_thickness,
                                shape_scribbling_active);
  update_shape_editor_controls();
}

void kdws_metadata_editor::convertScribble()
{
  if (!(editing_shape && shape_scribbling_active))
    return;
  int num_scribble_points;
  edit_state->active_shape_editor.get_scribble_points(num_scribble_points);
  if (num_scribble_points < 1)
    return;
  bool replace = (roi_scribble_replaces_button()->GetCheck() == BST_CHECKED);
  int flags = JPX_EDITOR_FLAG_FILL | JPX_EDITOR_FLAG_ELLIPSES;
  if (shape_editing_mode == JPX_EDITOR_PATH_MODE)
    { 
      bool ellipses = (roi_elliptical_button()->GetCheck() == BST_CHECKED);
      flags = (ellipses)?JPX_EDITOR_FLAG_ELLIPSES:0;
    }

  double acc = 0.001 * roi_scribble_accuracy_slider()->GetPos();
  if (acc < 0.0)
    acc = 0.0;
  else if (acc > 1.0)
    acc = 1.0;
  kdu_dims update_dims =
    edit_state->active_shape_editor.convert_scribble_path(replace,flags,acc);
  if (update_dims.is_empty())
    interrogate_user("Converted scribble path's complexity will be too high.  "
                     "Adjust the conversion accuracy to a lower value using "
                     "the slider provided.","OK");
  else
    { 
      update_dims.augment(renderer->shape_editor_adjust_path_thickness());
      renderer->update_shape_editor(update_dims,hide_shape_paths,
                                    fixed_shape_path_thickness,
                                    shape_scribbling_active);
    }
  update_shape_editor_controls();
}

/*****************************************************************************/
/*           kdws_metadata_editor:[ACTIONS for Content Editing]              */
/*****************************************************************************/

void kdws_metadata_editor::changeBoxType() 
{
  int idx = box_type_popup()->GetCurSel();
  if ((idx < 0) || (idx >= state->num_offered_types) ||
      (state->selected_type == state->offered_types[idx]) ||
      editing_shape || !state->can_edit_node)
    return;
  map_controls_to_state();
  state->selected_type = state->offered_types[idx];
  state->set_external_file(NULL);
  if (state->selected_type->box_type == jp2_label_4cc)
    state->set_label_string("<new label>");
  else
    {
      state->set_label_string(NULL);
      kdws_file *file =
        file_server->retain_tmp_file(state->selected_type->file_suffix);
      file->create_if_necessary(state->selected_type->initializer);
      state->set_external_file(file,true);
    }
  state->set_external_file_replaces_contents();
      // Does nothing if no external file
  map_state_to_controls();
}

void kdws_metadata_editor::saveToFile() 
{
  if (state->is_link)
    return;
  kdws_file *existing_file = state->get_external_file();
  char filename[MAX_PATH+1];
  if (!choose_save_file_pathname(state->selected_type->file_suffix,
                                 existing_file,filename,MAX_PATH))
    return;
  kdws_file *new_file = file_server->retain_known_file(filename);
  map_controls_to_state();
  if (existing_file != NULL)
    copy_file(existing_file,new_file);
  state->set_external_file(new_file);
  if ((existing_file == NULL) && !state->save_to_external_file(edit_state))
    new_file->create_if_necessary(state->selected_type->initializer);
  map_state_to_controls();
}

void kdws_metadata_editor::chooseFile() 
{
  if (!state->can_edit_node)
    return;
  map_controls_to_state();
  char filename[MAX_PATH+1];
  if (choose_open_file_pathname(state->selected_type->file_suffix,
                                state->get_external_file(),
                                filename,MAX_PATH))
    {
      kdws_file *file = file_server->retain_known_file(filename);
      file->create_if_necessary(state->selected_type->initializer);
      state->set_external_file(file,true);
      state->set_external_file_replaces_contents();
      state->set_label_string(NULL);
    }
  map_state_to_controls();
}

void kdws_metadata_editor::editFile() 
{
  if (editing_shape || (!state->can_edit_node) ||
      (state->selected_type == NULL))
    return;
  map_controls_to_state();
  kdws_file *external_file = state->get_external_file();
  if (external_file == NULL)
    {
      state->save_to_external_file(edit_state);
      external_file = state->get_external_file();
    }
  if (external_file == NULL)
    { // Should not happen
      MessageBeep(MB_ICONEXCLAMATION); return;
    }
  external_file->create_if_necessary(state->selected_type->initializer);
  
  const char *file_suffix = state->selected_type->file_suffix;
  kdws_file_editor *editor=NULL;
  int idx = box_editor_popup()->GetCurSel();
  if (idx > 0)
    editor = file_server->get_editor_for_doc_type(file_suffix,idx-1);
  if (editor != NULL) // Put it at the head of the list of known editors
    file_server->add_editor_for_doc_type(file_suffix,editor->app_pathname);
  else
    editor = choose_editor(file_server,file_suffix);
  if (editor == NULL)
    MessageBeep(MB_ICONEXCLAMATION);
  else
    {
      kdws_string command_line((int)(strlen(editor->app_pathname) + 6 +
                                     strlen(external_file->get_pathname())));
      if (editor->app_pathname[0] == '\"')
        sprintf(command_line,"%s \"%s\"",editor->app_pathname,
                external_file->get_pathname());
      else
        sprintf(command_line,"\"%s\" \"%s\"",editor->app_pathname,
                external_file->get_pathname());
      STARTUPINFO startup_info;
      memset(&startup_info,0,sizeof(startup_info));
      startup_info.cb = sizeof(startup_info);
      PROCESS_INFORMATION process_info;
      memset(&process_info,0,sizeof(process_info));
      if (CreateProcess(NULL,command_line,NULL,NULL,FALSE,
                        NORMAL_PRIORITY_CLASS,NULL,NULL,&startup_info,
                        &process_info))
        state->set_external_file_replaces_contents();
      else
        MessageBeep(MB_ICONEXCLAMATION);
    }
  map_state_to_controls();
}

void kdws_metadata_editor::changeEditor() 
{
  if ((!state->can_edit_node) || (state->selected_type == NULL))
    return;
  map_controls_to_state();
  kdws_file_editor *editor = NULL;
  const char *file_suffix = state->selected_type->file_suffix;
  int idx = box_editor_popup()->GetCurSel();
  if (idx > 0)
    editor = file_server->get_editor_for_doc_type(file_suffix,idx-1);
  if (editor == NULL)
    editor = choose_editor(file_server,file_suffix);
  if (editor != NULL)
    file_server->add_editor_for_doc_type(file_suffix,editor->app_pathname);
  map_state_to_controls();
}

void kdws_metadata_editor::internalizeLabel()
{
  map_controls_to_state();
  if (!state->internalize_label_node())
    {
      MessageBeep(MB_ICONEXCLAMATION);
      return;
    }
  map_state_to_controls();
}

/*****************************************************************************/
/*       kdws_metadata_editor:[ACTIONS which work with the Pastebar]         */
/*****************************************************************************/

void kdws_metadata_editor::clickedCopyLink()
{
  if ((!edit_state->cur_node.exists()) || (!state->can_edit_node) ||
      (state->is_cross_reference && !state->is_link))
    return;
  jpx_metanode link_target;
  jpx_metanode_link_type link_type;
  if (state->is_link)
    link_target = edit_state->cur_node.get_link(link_type);
  else
    link_target = edit_state->cur_node;
  if (renderer->catalog_source == NULL)
    renderer->window->create_metadata_catalog();
  if (link_target.exists() && (renderer->catalog_source != NULL))
    renderer->catalog_source->paste_link(link_target);
}

void kdws_metadata_editor::clickedPaste()
{
  if ((renderer->catalog_source == NULL) || !allow_edits)
    return;
  stop_shape_editor();
  const char *pasted_label=renderer->catalog_source->get_pasted_label();
  jpx_metanode pasted_link=renderer->catalog_source->get_pasted_link();
  jpx_metanode node_to_cut=renderer->catalog_source->get_pasted_node_to_cut();
  if ((pasted_label == NULL) && (!pasted_link) && (!node_to_cut))
    {
      interrogate_user("You need to copy/link/cut something to the catalog "
                       "paste bar before you can paste over the current node "
                       "in the editor.","OK");
      return;
    }
  jpx_metanode_link_type link_type = JPX_METANODE_LINK_NONE;
  if (pasted_link.exists())
    {
      int user_response =
        interrogate_user("What type of link would you like to add?\n"
                         "  \"Grouping links\" can have their own "
                         "descendants, all of which can be understood as "
                         "members of the same group.\n"
                         "  \"Alternate child\" is the most natural type of "
                         "link -- it suggests that the target of the link "
                         "and all its descendants could be considered "
                         "descendants of the node within which the link is "
                         "found.\n"
                         "  \"Alternate parent\" suggests that the target "
                         "of the link could be considered a parent of the "
                         "node within which the link is found.\n",
                         "Grouping","Alternate child","Alternate parent",
                         "Cancel");
      switch (user_response) {
        case 0: link_type = JPX_GROUPING_LINK; break;
        case 1: link_type = JPX_ALTERNATE_CHILD_LINK; break;
        case 2: link_type = JPX_ALTERNATE_PARENT_LINK; break;
        default: return;
      }
    }

  map_controls_to_state();
  if (!edit_state->cur_node)
    { // Save the node first, so we can restrict our attention to replacing it
      try {
        if (!state->save_metanode(edit_state,initial_state,renderer))
          {
            MessageBeep(MB_ICONEXCLAMATION);
            update_apply_buttons();
            return;
          }
      }
      catch (kdu_exception)
      {
        close();
        return;
      }
    }

  kdws_file *file=get_file_retained_by_node(edit_state->cur_node,file_server);
  if (file != NULL)
    file->release();
  try {
    if (pasted_label != NULL)
      edit_state->cur_node.change_to_label(pasted_label);
    else if (pasted_link.exists())
      edit_state->cur_node.change_to_link(pasted_link,link_type);
    else if (node_to_cut.exists())
      {
        if (!node_to_cut.change_parent(edit_state->cur_node.get_parent()))
          { kdu_error e; e << "Cut and paste operation seems to be trying "
            "to move a node (along with its descendants) into its own "
            "descendant hierarchy!"; }
        if (edit_state->at_root())
          {
            edit_state->root_node = node_to_cut;
            if (edit_state->edit_item != NULL)
              edit_state->edit_item->node = node_to_cut;
          }
        edit_state->cur_node.delete_node();
        edit_state->cur_node = node_to_cut;
        edit_state->validate_metalist_and_root_node();
      }
  }
  catch (kdu_exception)
  {
    renderer->metadata_changed(true);
    map_state_to_controls();
    return;
  }
  renderer->metadata_changed(true);
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();
}

void kdws_metadata_editor::clickedCopyDescendants()
{
  jpx_metanode pasted_link = renderer->catalog_source->get_pasted_link();
  if (!pasted_link)
    {
     interrogate_user("You need to copy a link to the catalog pastebar "
                       "before you can copy the present node's descendants "
                       "across as descendants of the target of that link.",
                       "OK");
      return;
    }
  int user_choice =
    interrogate_user("You are about to copy all descendants of the current "
                     "node across as descendants of the node targeted by "
                     "the link in the catalog pastebar.  As an added feature, "
                     "you can opt to also make copies of all link nodes (and "
                     "their descendants) which currently point to the current "
                     "node -- that is, you can treat links to the current "
                     "node as if they were descendants.  Doing this now "
                     "will help to ensure that all copied links point "
                     "correctly to the relevant copies of their original "
                     "targets.",
                     "Copy all","Descendants only","Cancel");
  if (user_choice == 2)
    return;
  bool copy_linkers = (user_choice == 0);
  stop_shape_editor(); // Minimize risk of visual confusion
  jpx_metanode root_metanode = edit_state->meta_manager.access_root();
  edit_state->meta_manager.reset_copy_locators(root_metanode,true);
  copy_descendants_with_file_retain(edit_state->cur_node,pasted_link,
                                    file_server,copy_linkers);
  edit_state->meta_manager.reset_copy_locators(root_metanode,true,true);
  renderer->metadata_changed(true);
}


/*****************************************************************************/
/*        kdws_metadata_editor:[ACTIONS for Navigation and Control]          */
/*****************************************************************************/

void kdws_metadata_editor::clickedExit() 
{
  map_controls_to_state();
  if (!query_save())
    return;
  close();
}

void kdws_metadata_editor::clickedDelete() 
{
  if (interrogate_user("Are you sure you want to delete this node and "
                       "all its descendants?","Delete","Cancel"))
    {
      update_apply_buttons();
      return;
    }
  try {
    edit_state->delete_cur_node(renderer);
  }
  catch (kdu_exception) {
    close();
    return;
  }

  if (!edit_state->cur_node.exists())
    {
      close();
      return;
    }
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();      
}

void kdws_metadata_editor::clickedApply() 
{
  map_controls_to_state();
  if (editing_shape && !query_save_shape_edits())
    return;
  try {
    if (!state->save_metanode(edit_state,initial_state,renderer))
      {
        update_apply_buttons();
        return;
      }
  }
  catch (kdu_exception) {
    close();
    return;
  }
  initial_state->copy(state); // Now everthing starts again from scratch
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls(); // May alter the navigation options
}

void kdws_metadata_editor::clickedApplyAndExit() 
{
  map_controls_to_state();
  if (editing_shape && !query_save_shape_edits())
    return;
  try {
    if (!state->save_metanode(edit_state,initial_state,renderer))
      {
        update_apply_buttons();
        return;
      }
  }
  catch (kdu_exception) { }
  close();
}

void kdws_metadata_editor::clickedNext() 
{
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->move_to_next();
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();
}

void kdws_metadata_editor::clickedPrev() 
{
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->move_to_prev();
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();  
}

void kdws_metadata_editor::clickedParent() 
{
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->move_to_parent();
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();  
}

void kdws_metadata_editor::clickedDescendants() 
{
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->move_to_descendants();
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();  
}

void kdws_metadata_editor::clickedAddChild() 
{
  if (!allow_edits)
    return;
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->add_child_node(renderer);
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();  
}

void kdws_metadata_editor::clickedAddParent()
{
  if (!allow_edits)
    return;
  map_controls_to_state();
  if (!query_save())
    return;
  edit_state->add_parent_node(renderer);
  state->configure_with_cur_node(edit_state);
  initial_state->copy(state);
  if (state->can_edit_region)
    auto_start_shape_editor();
  map_state_to_controls();
}

void kdws_metadata_editor::findInMetashow() 
{
  if (!edit_state->cur_node)
    return;
  if (renderer->metashow == NULL)
    renderer->menu_MetadataShow();
  if (renderer->metashow != NULL)
    renderer->metashow->select_matching_metanode(edit_state->cur_node); 
}

void kdws_metadata_editor::findInCatalog()
{
  if (!edit_state->cur_node)
    return;
  if (renderer->catalog_source == NULL)
    renderer->window->create_metadata_catalog();
  jpx_metanode node=edit_state->cur_node;
  if (renderer->catalog_source)
    renderer->catalog_source->select_matching_metanode(node);
}

/*****************************************************************************/
/*                 kdws_metadata_editor:[Generic ACTIONS]                    */
/*****************************************************************************/

void kdws_metadata_editor::clickedRevert() 
{
  stop_shape_editor();
  state->copy(initial_state);
  map_state_to_controls();
}

void kdws_metadata_editor::otherAction() 
{
  map_controls_to_state();
  update_apply_buttons();
}
