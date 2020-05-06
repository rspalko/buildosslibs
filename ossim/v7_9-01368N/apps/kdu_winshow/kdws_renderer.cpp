/*****************************************************************************/
// File: kdu_renderer.cpp [scope = APPS/WINSHOW]
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
   Implements the image management and rendering object, `kdws_renderer'.
This object lies the heart of the "kdu_show" application's functionality,
implementing menu commands, image processing, painting, navigation and JPIP
remote image browsing functionality.
******************************************************************************/

#include "stdafx.h"
#include "kdu_arch.h"
#include "kdu_utils.h"
#include "kdws_manager.h"
#include "kdws_renderer.h"
#include "kdws_window.h"
#include "kdws_properties.h"
#include "kdws_metashow.h"
#include "kdws_metadata_editor.h"
#include "kdws_catalog.h"
#include <math.h>

#if defined KDU_X86_INTRINSICS
#  include <emmintrin.h>
#endif // KDU_X86_INTRINSICS

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define KDWS_OVERLAY_DEFAULT_INTENSITY 0.55F
#define KDWS_OVERLAY_FLASHING_STATES 3 // Num distinct flashing states
static float kdws_overlay_flash_factor[KDWS_OVERLAY_FLASHING_STATES] =
  {1.2F,0.6F,-0.6F};
static float kdws_overlay_flash_dwell[KDWS_OVERLAY_FLASHING_STATES] =
  {0.5F,0.5F,1.0F};
  /* Notes: when metadata overlay painting is enabled, they can either be
     displayed statically, using the nominal intensity (starts out with
     the default value given above, but can be adjusted up and down by the
     user), or dynamically (flashing).  In the dynamic case, the current
     value of the nominal intensity (as mentioned, this can be adjusted up
     or down by the user) is additionally scaled by a value which depends
     upon the current flashing state.  The flashing state machine moves
     cyclically through `KDWS_OVERLAY_FLASHING_STATES' states.  Each
     state is characterized by a scaling factor (mentioned above) and a
     dwelling period (measured in seconds); these are stored in the
     `kdws_overlay_flash_factor' and `kdws_overlay_flash_dwell' arrays. */
#define KDWS_OVERLAY_HIGHLIGHT_STATES 5
static float kdws_overlay_highlight_factor[KDWS_OVERLAY_HIGHLIGHT_STATES] =
  {2.0F,-2.0F,2.0F,-2.0F,2.0F};
static float kdws_overlay_highlight_dwell[KDWS_OVERLAY_HIGHLIGHT_STATES] =
  {0.125F,0.125F,0.125F,0.125F,2.0F};
  /* Notes: the above arrays play a similar role to `kdws_overlay_flash_factor'
     and `kdws_overlay_flash_dwell', but they apply to the temporary flashing
     of overlay information for a specific metadata node, in response to
     calls to `kdws_renderer::flash_imagery_for_metanode'. */


/*===========================================================================*/
/*                             EXTERNAL FUNCTIONS                            */
/*===========================================================================*/

/*****************************************************************************/
/* EXTERN                     kdws_utf8_to_unicode                           */
/*****************************************************************************/

int kdws_utf8_to_unicode(const char *utf8, kdu_uint16 unicode[], int max_chars)
{
  int length = 0;
  while ((length < max_chars) && (*utf8 != '\0'))
    {
      int extra_chars = 0;
      kdu_uint16 val, tmp=((kdu_uint16)(*(utf8++))) & 0x00FF;
      if (tmp < 128)
        val = tmp;
      else if (tmp < 224)
        { val = (tmp-192); extra_chars = 1; }
      else if (tmp < 240)
        { val = (tmp-224); extra_chars = 2; }
      else if (tmp < 248)
        { val = (tmp-240); extra_chars = 3; }
      else if (tmp < 252)
        { val = (tmp-240); extra_chars = 4; }
      else
        { val = (tmp-252); extra_chars = 5; }
      for (; extra_chars > 0; extra_chars--)
        {
          val <<= 6;
          if (*utf8 != '\0')
            val |= (((kdu_uint16)(*(utf8++))) & 0x00FF) - 128;
        }
      unicode[length++] = val;
    }
  unicode[length] = 0;
  return length;
}


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC               kdws_dist_to_common_ancestor                         */
/*****************************************************************************/

static int
  kdws_dist_to_common_ancestor(jpx_metanode ref, jpx_metanode test)
  /* This function is used to help determine the best metadata item to select
     when a user taps within the image viewport.  The function is used only
     where there is a currently selected metadata item (`ref') and that
     item does not itself match the tapped content, but `test' does.  This
     implies that we do not expect `test' to be contained within `ref',
     although that could possibly happen if we have numlists embedded within
     other numlists in a non-compatible manner.
        The function tests each successive ancestor of `ref' to determine
     whether or not it contains `test' as a descendant.  The function then
     returns the sum of the number of traversals from `ref' and from
     `test' to this closest common ancestor. */
{
  int ref_traversals=1;
  ref = ref.get_parent();
  while (ref.exists())
    { 
      jpx_metanode scan=test;
      int scan_traversals=0;
      while (scan.exists() && (scan != ref))
        { scan_traversals++; scan = scan.get_parent(); }
      if (scan == ref)
        return scan_traversals + ref_traversals;
      ref_traversals++;
      ref = ref.get_parent();
    }
  return ref_traversals; // We should not get here unless one node was root!
}

/*****************************************************************************/
/* STATIC                         copy_tile                                  */
/*****************************************************************************/

static void
  copy_tile(kdu_tile tile_in, kdu_tile tile_out, bool force_empty)
  /* Walks through all code-blocks of the tile in raster scan order, copying
     them from the input to the output tile.  If `force_empty' is true,
     the code-blocks are written as having no coding passes at all.  This
     can be useful when we do not actually have a copy of the codestream
     that really needs to be copied, so we are copying a different one
     instead (as a template). */
{
  int c, num_components = tile_out.get_num_components();
  for (c=0; c < num_components; c++)
    {
      kdu_tile_comp comp_in;  comp_in  = tile_in.access_component(c);
      kdu_tile_comp comp_out; comp_out = tile_out.access_component(c);
      int r, num_resolutions = comp_out.get_num_resolutions();
      for (r=0; r < num_resolutions; r++)
        {
          kdu_resolution res_in;  res_in  = comp_in.access_resolution(r);
          kdu_resolution res_out; res_out = comp_out.access_resolution(r);
          int b, min_band;
          int num_bands = res_in.get_valid_band_indices(min_band);
          for (b=min_band; num_bands > 0; num_bands--, b++)
            {
              kdu_subband band_in;  band_in = res_in.access_subband(b);
              kdu_subband band_out; band_out = res_out.access_subband(b);
              kdu_dims blocks_in;  band_in.get_valid_blocks(blocks_in);
              kdu_dims blocks_out; band_out.get_valid_blocks(blocks_out);
              if ((blocks_in.size.x != blocks_out.size.x) ||
                  (blocks_in.size.y != blocks_out.size.y))
                { kdu_error e; e << "Transcoding operation cannot proceed: "
                  "Code-block partitions for the input and output "
                  "code-streams do not agree."; }
              kdu_coords idx;
              for (idx.y=0; idx.y < blocks_out.size.y; idx.y++)
                for (idx.x=0; idx.x < blocks_out.size.x; idx.x++)
                  { 
                    kdu_block *out = band_out.open_block(idx+blocks_out.pos);
                    if (force_empty)
                      { 
                        out->missing_msbs = out->K_max_prime;
                        out->num_passes = 0;
                      }
                    else
                      { 
                        kdu_block *in  = band_in.open_block(idx+blocks_in.pos);
                        if (in->K_max_prime != out->K_max_prime)
                          { kdu_error e;
                            e << "Cannot copy blocks belonging to subbands "
                            "with different quantization parameters."; }
                        if ((in->size.x != out->size.x) ||
                            (in->size.y != out->size.y))  
                          { kdu_error e; e << "Cannot copy code-blocks with "
                            "different dimensions."; }
                        out->missing_msbs = in->missing_msbs;
                        if (out->max_passes < (in->num_passes+2))
                          out->set_max_passes(in->num_passes+2,false);
                        out->num_passes = in->num_passes;
                        int num_bytes = 0;
                        for (int z=0; z < in->num_passes; z++)
                          { 
                            num_bytes +=
                            (out->pass_lengths[z] = in->pass_lengths[z]);
                            out->pass_slopes[z] = in->pass_slopes[z];
                          }
                        if (out->max_bytes < num_bytes)
                          out->set_max_bytes(num_bytes,false);
                        memcpy(out->byte_buffer,in->byte_buffer,
                               (size_t)num_bytes);
                        band_in.close_block(in);
                      }
                    band_out.close_block(out);
                  }
            }
        }
    }
}

/*****************************************************************************/
/* STATIC                       copy_codestream                              */
/*****************************************************************************/

static void
  copy_codestream(kdu_compressed_target *tgt, kdu_codestream src,
                  bool force_empty_code_blocks)
  /* If `force_empty_code_blocks', the `tgt' codestream is written with empty
     code-blocks -- all zero.  This can be useful when we do not actually
     have a copy of the codestream that really needs to be copied, so we
     are copying a different one instead (as a template). */
{
  src.apply_input_restrictions(0,0,0,0,NULL,KDU_WANT_OUTPUT_COMPONENTS);
  siz_params *siz_in = src.access_siz();
  siz_params init_siz; init_siz.copy_from(siz_in,-1,-1);
  kdu_codestream dst; dst.create(&init_siz,tgt);
  try { // Protect the `dst' codestream
    siz_params *siz_out = dst.access_siz();
    siz_out->copy_from(siz_in,-1,-1);
    siz_out->parse_string("ORGgen_plt=yes");
    siz_out->finalize_all(-1);
    
    // Now ready to perform the transfer of compressed data between streams
    kdu_dims tile_indices; src.get_valid_tiles(tile_indices);
    kdu_coords idx;
    for (idx.y=0; idx.y < tile_indices.size.y; idx.y++)
      for (idx.x=0; idx.x < tile_indices.size.x; idx.x++)
        { 
          kdu_tile tile_in = src.open_tile(idx+tile_indices.pos);
          int tnum = tile_in.get_tnum();
          siz_out->copy_from(siz_in,tnum,tnum);
          siz_out->finalize_all(tnum);
          kdu_tile tile_out = dst.open_tile(idx+tile_indices.pos);
          assert(tnum == tile_out.get_tnum());
          copy_tile(tile_in,tile_out,force_empty_code_blocks);
          tile_in.close();
          tile_out.close();
        }
    dst.trans_out();
  }
  catch (kdu_exception exc)
  { 
    if (dst.exists())
      dst.destroy();
    throw exc;
  }
  dst.destroy();
}

/*****************************************************************************/
/* STATIC                      copy_jpx_stream                               */
/*****************************************************************************/

static void
  copy_jpx_stream(jpx_codestream_source src, jpx_codestream_target tgt,
                  const char *src_pathname, const char *tgt_pathname,
                  jp2_data_references src_data_refs,
                  bool src_uses_cache, bool preserve_codestream_links,
                  bool force_codestream_links, bool force_empty_code_blocks)
  /* Used by `kdws_renderer::save_as_jpx'.  The `force_empty_code_blocks'
     argument is set to true if we are actually copying the wrong
     codestream (as a default template) to ensure structural integrity
     when the original codestream is not available from the source (can
     happen when the ultimate source of data is a dynamic cache during
     JPIP browsing). */
{
  jpx_input_box box_in;
  jpx_fragment_list frags_in;
  if (preserve_codestream_links)
    frags_in = src.access_fragment_list();
  if ((!frags_in.exists()) || frags_in.any_local_fragments() ||
      (!src_data_refs.exists()))
    src.open_stream(&box_in);
  jp2_output_box *box_out = NULL;
  if ((!force_codestream_links) && box_in.exists())
    box_out = tgt.open_stream();
  
  kdu_codestream cs_in;  
  kdu_byte *buffer = NULL;
  try { // Ensure tht `cs_in' and `buffer' get deleted in case of failure
    int xfer_bytes;
    if (src_uses_cache)
      { // Need to generate output codestreams from scratch -- transcoding
        assert(box_in.exists() && (box_out != NULL));
        cs_in.create(&box_in);
        copy_codestream(box_out,cs_in,force_empty_code_blocks);
        cs_in.destroy();
        box_out->close();
      }
    else if (box_out != NULL)
      { // Simply copy the box contents directly
        buffer = new kdu_byte[1<<20];
        if (box_in.get_remaining_bytes() > 0)
          box_out->set_target_size(box_in.get_remaining_bytes());
        else
          box_out->write_header_last();
        while ((xfer_bytes=box_in.read(buffer,1<<20)) > 0)
          box_out->write(buffer,xfer_bytes);
        delete[] buffer;
        buffer = NULL;
        box_out->close();
      }
    else
      { // Writing a fragment list
        kdu_long offset, length;            
        if (box_in.exists())
          { // Generate fragment table from embedded source codestream
            assert(force_codestream_links);
            jp2_locator loc = box_in.get_locator();
            offset = loc.get_file_pos() + box_in.get_box_header_length();
            length = box_in.get_remaining_bytes();
            if (length < 0)
              { 
                buffer = new kdu_byte[1<<20];
                for (length=0; (xfer_bytes=box_in.read(buffer,1<<20)) > 0; )
                  length += xfer_bytes;
                delete[] buffer;
                buffer = NULL;
              }
            const char *sep1 = strrchr(tgt_pathname,'/');
            const char *sep2 = strrchr(src_pathname,'/');
            const char *source_path = src_pathname;
            if ((sep1 != NULL) && (sep2 != NULL) &&
                ((sep1-tgt_pathname) == (sep2-src_pathname)) &&
                (strncmp(tgt_pathname,src_pathname,sep1-tgt_pathname)==0))
              source_path = sep2+1;
            tgt.add_fragment(source_path,offset,length,true);
          }
        else
          { // Copy fragment table from linked source codestream
            if (!(frags_in.exists() && src_data_refs.exists()))
              { kdu_error e;
                e << "Source appears to be corrupt in some way."; }
            int frag_idx, num_frags = frags_in.get_num_fragments();
            for (frag_idx=0; frag_idx < num_frags; frag_idx++)
              { 
                int url_idx;
                const char *url_path;
                if (frags_in.get_fragment(frag_idx,url_idx,
                                          offset,length) &&
                    ((url_path=src_data_refs.get_url(url_idx)) != NULL))
                  tgt.add_fragment(url_path,offset,length);
              }
          }
        tgt.write_fragment_table();
      }
  } catch (...) {
    if (buffer != NULL)
      delete[] buffer;
    if (cs_in.exists())
      cs_in.destroy();
    throw;
  }
    
  box_in.close();
}              

/*****************************************************************************/
/* STATIC                write_metadata_box_from_file                        */
/*****************************************************************************/

static void
  write_metadata_box_from_file(jp2_output_box *container, kdws_file *file)
{
  const char *filename = file->get_pathname();
  FILE *fp = fopen(filename,"rb");
  if (fp != NULL)
    {
      kdu_byte buf[512];
      size_t xfer_bytes;
      while ((xfer_bytes = fread(buf,1,512,fp)) > 0)
        container->write(buf,(int)xfer_bytes);
    }
  fclose(fp);
}

/*****************************************************************************/
/* STATIC                    write_metanode_to_jp2                           */
/*****************************************************************************/

static void
  write_metanode_to_jp2(jpx_metanode node, jp2_output_box &tgt,
                        kdws_file_services *file_server)
/* Writes the JP2 box represented by `node' or (if `node' is a numlist) from
   its descendants, to the `tgt' object.  Does not recursively visit
   descendants since this would require the writing of asoc boxes, which are
   not part of the JP2 file format.  Writes only boxes which are JP2
   compatible.  Uses the `file_server' object to resolve boxes whose contents
   have been defined by the metadata editor. */
{
  bool rres;
  int num_cs, num_l;
  int c, num_children = 0;
  node.count_descendants(num_children);
  if (node.get_numlist_info(num_cs,num_l,rres))
    {
      jpx_metanode scan;
      for (c=0; c < num_children; c++)
        if ((scan=node.get_descendant(c)).exists())
          write_metanode_to_jp2(scan,tgt,file_server);
      return;
    }
  if (num_children > 0)
    return; // Don't write nodes with descendants
  
  kdu_uint32 box_type = node.get_box_type();
  if ((box_type != jp2_label_4cc) && (box_type != jp2_xml_4cc) &&
      (box_type != jp2_iprights_4cc) && (box_type != jp2_uuid_4cc) &&
      (box_type != jp2_uuid_info_4cc))
    return; // Not a box we should write to a JP2 file
  
  int num_bytes;
  const char *label = node.get_label();
  if (label != NULL)
    {
      tgt.open_next(jp2_label_4cc);
      tgt.set_target_size((kdu_long)(num_bytes=(int)strlen(label)));
      tgt.write((kdu_byte *)label,num_bytes);
      tgt.close();
      return;
    }
  
  jp2_input_box in_box;
  if (node.open_existing(in_box))
    {
      tgt.open_next(box_type);
      if (in_box.get_remaining_bytes() > 0)
        tgt.set_target_size((kdu_long)(in_box.get_remaining_bytes()));
      kdu_byte buf[512];
      while ((num_bytes = in_box.read(buf,512)) > 0)
        tgt.write(buf,num_bytes);
      tgt.close();
      return;
    }
  
  int i_param;
  void *addr_param;
  kdws_file *file;
  if (node.get_delayed(i_param,addr_param) &&
      (addr_param == (void *)file_server) &&
      ((file = file_server->find_file(i_param)) != NULL))
    {
      tgt.open_next(box_type);
      write_metadata_box_from_file(&tgt,file);
      tgt.close();
    }
}


/*===========================================================================*/
/*                                 kdws_file                                 */
/*===========================================================================*/

/*****************************************************************************/
/*                           kdws_file::kdws_file                            */
/*****************************************************************************/

kdws_file::kdws_file(kdws_file_services *owner)
{
  this->owner = owner;
  unique_id = owner->next_unique_id++;
  retain_count = 0; // Caller needs to explicitly retain it
  pathname = NULL;
  is_temporary = false;
  next = NULL;
  if ((prev = owner->tail) == NULL)
    owner->head = this;
  else
    prev->next = this;
  owner->tail = this;
}

/*****************************************************************************/
/*                           kdws_file::~kdws_file                           */
/*****************************************************************************/

kdws_file::~kdws_file()
{
  if (owner != NULL)
    { // Unlink from owner.
      if (prev == NULL)
        {
          assert(this == owner->head);
          owner->head = next;
        }
      else
        prev->next = next;
      if (next == NULL)
        {
          assert(this == owner->tail);
          owner->tail = prev;
        }
      else
        next->prev = prev;
      prev = next = NULL;
      owner = NULL;
    }
  if (is_temporary && (pathname != NULL))
    remove(pathname);
  if (pathname != NULL)
    delete[] pathname;
}

/*****************************************************************************/
/*                             kdws_file::release                            */
/*****************************************************************************/

void kdws_file::release()
{
  retain_count--;
  if (retain_count == 0)
    delete this;
}

/*****************************************************************************/
/*                       kdws_file::create_if_necessary                      */
/*****************************************************************************/

bool kdws_file::create_if_necessary(const char *initializer)
{
  if ((pathname == NULL) || !is_temporary)
    return false;
  const char *mode = (initializer == NULL)?"rb":"r";
  FILE *file = fopen(pathname,mode);
  if (file != NULL)
    { // File already exists
      fclose(file);
      return false;
    }
  mode = (initializer == NULL)?"wb":"w";
  file = fopen(pathname,mode);
  if (file == NULL)
    return false; // Cannot write to file
  if (initializer != NULL)
    fwrite(initializer,1,strlen(initializer),file);
  fclose(file);
  return true;
}


/*===========================================================================*/
/*                             kdws_file_services                            */
/*===========================================================================*/

/*****************************************************************************/
/*                   kdws_file_services::kdws_file_services                  */
/*****************************************************************************/

kdws_file_services::kdws_file_services(const char *source_pathname)
{
  head = tail = NULL;
  next_unique_id = 1;
  editors = NULL;
  base_pathname_confirmed = false;
  base_pathname = NULL;
  if (source_pathname != NULL)
    {
      base_pathname = new char[strlen(source_pathname)+8];
      strcpy(base_pathname,source_pathname);
      const char *cp = strrchr(base_pathname,'.');
      if (cp == NULL)
        strcat(base_pathname,"_meta_");
      else
        strcpy(base_pathname + (cp-base_pathname),"_meta_");
    }
}

/*****************************************************************************/
/*                   kdws_file_services::~kdws_file_services                 */
/*****************************************************************************/

kdws_file_services::~kdws_file_services()
{
  while (head != NULL)
    delete head;
  if (base_pathname != NULL)
    delete[] base_pathname;
  kdws_file_editor *editor;
  while ((editor=editors) != NULL)
    {
      editors = editor->next;
      delete editor;
    }
}

/*****************************************************************************/
/*                 kdws_file_services::find_new_base_pathname                */
/*****************************************************************************/

bool kdws_file_services::find_new_base_pathname()
{
  if (base_pathname != NULL)
    delete[] base_pathname;
  base_pathname_confirmed = false;
  base_pathname = new char[L_tmpnam+8];
  tmpnam(base_pathname);
  FILE *fp = fopen(base_pathname,"wb");
  if (fp == NULL)
    return false;
  fclose(fp);
  remove(base_pathname);
  base_pathname_confirmed = true;
  strcat(base_pathname,"_meta_");
  return true;
}

/*****************************************************************************/
/*                 kdws_file_services::confirm_base_pathname                 */
/*****************************************************************************/

void kdws_file_services::confirm_base_pathname()
{
  if (base_pathname_confirmed)
    return;
  if (base_pathname != NULL)
    {
      FILE *fp = fopen(base_pathname,"rb");
      if (fp != NULL)
        {
          fclose(fp); // Existing base pathname already exists; can't test it
          // easily for writability; safest to make new base path
          find_new_base_pathname();
        }
      else
        {
          fp = fopen(base_pathname,"wb");
          if (fp != NULL)
            { // All good; can write to this volume
              fclose(fp);
              remove(base_pathname);
              base_pathname_confirmed = true;
            }
          else
            find_new_base_pathname();
        }
    }
  if (base_pathname == NULL)
    find_new_base_pathname();
}

/*****************************************************************************/
/*                    kdws_file_services::retain_known_file                  */
/*****************************************************************************/

kdws_file *kdws_file_services::retain_known_file(const char *pathname)
{
  kdws_file *file;
  for (file=head; file != NULL; file=file->next)
    if (strcmp(file->pathname,pathname) == 0)
      break;
  if (file == NULL)
    {
      file = new kdws_file(this);
      file->pathname = new char[strlen(pathname)+1];
      strcpy(file->pathname,pathname);
    }
  file->is_temporary = false;
  file->retain();
  return file;
}

/*****************************************************************************/
/*                     kdws_file_services::retain_tmp_file                   */
/*****************************************************************************/

kdws_file *kdws_file_services::retain_tmp_file(const char *suffix)
{
  if (!base_pathname_confirmed)
    confirm_base_pathname();
  kdws_file *file = new kdws_file(this);
  file->pathname = new char[strlen(base_pathname)+81];
  strcpy(file->pathname,base_pathname);
  char *cp = file->pathname + strlen(file->pathname);
  int extra_id = 0;
  bool found_new_filename = false;
  while (!found_new_filename)
    {
      if (extra_id == 0)
        sprintf(cp,"_tmp_%d.%s",file->unique_id,suffix);
      else
        sprintf(cp,"_tmp%d_%d.%s",extra_id,file->unique_id,suffix);
      FILE *fp = fopen(file->pathname,"rb");
      if (fp != NULL)
        {
          fclose(fp); // File already exists
          extra_id++;
        }
      else
        found_new_filename = true;
    }
  file->is_temporary = true;
  file->retain();
  return file;
}

/*****************************************************************************/
/*                        kdws_file_services::find_file                      */
/*****************************************************************************/

kdws_file *kdws_file_services::find_file(int identifier)
{
  kdws_file *scan;
  for (scan=head; scan != NULL; scan=scan->next)
    if (scan->unique_id == identifier)
      return scan;
  return NULL;
}

/*****************************************************************************/
/*               kdws_file_services::get_editor_for_doc_type                 */
/*****************************************************************************/

kdws_file_editor *
  kdws_file_services::get_editor_for_doc_type(const char *doc_suffix,
                                              int which)
{
  kdws_file_editor *scan;
  int n;
  
  for (n=0, scan=editors; scan != NULL; scan=scan->next)
    if (strcmp(scan->doc_suffix,doc_suffix) == 0)
      {
        if (n == which)
          return scan;
        n++;
      }
  
  if (n > 0)
    return NULL;
  
  // If we get here, we didn't find any matching editors at all.  Let's see if
  // we can add some.
  kdws_string extension(79);
  char *ext_string = extension;
  ext_string[0] = '.';
  strncpy(ext_string+1,doc_suffix,78);
  DWORD val_type, prog_id_len;
  kdws_string prog_id(255); // Only collect prog-id's with 255 chars or less
  HKEY ext_key;
  if (RegOpenKeyEx(HKEY_CLASSES_ROOT,extension,0,KEY_READ,
                   &ext_key) == ERROR_SUCCESS)
    {
      HKEY openwith_key;
      if (RegOpenKeyEx(ext_key,_T("OpenWithProgids"),0,KEY_READ,
                       &openwith_key) == ERROR_SUCCESS)
        {
          for (n=0; true; n++)
            {
              prog_id_len = 255;
              prog_id.clear();
              if (RegEnumValue(openwith_key,n,prog_id,&prog_id_len,
                               NULL,NULL,NULL,NULL) != ERROR_SUCCESS)
                break;
              add_editor_progid_for_doc_type(doc_suffix,prog_id);
            }
          RegCloseKey(openwith_key);
        }
      kdws_string valname(79);
      for (n=0; true; n++)
        {
          DWORD name_len=79;
          prog_id_len = 255;
          prog_id.clear();
          valname.clear();
          if (RegEnumValue(ext_key,n,valname,&name_len,NULL,
                           &val_type,(LPBYTE)((_TCHAR *) prog_id),
                           &prog_id_len) != ERROR_SUCCESS)
            break;
          if ((prog_id_len > 0) && (val_type == REG_SZ) &&
              valname.is_empty())
            add_editor_progid_for_doc_type(doc_suffix,prog_id);
        }
      RegCloseKey(ext_key);
    }

  // Now go back and try to answer the request again
  for (n=0, scan=editors; scan != NULL; scan=scan->next)
    if (strcmp(scan->doc_suffix,doc_suffix) == 0)
      {
        if (n == which)
          return scan;
        n++;
      }
  
  return NULL;
}

/*****************************************************************************/
/*           kdws_file_services::add_editor_progid_for_doc_type              */
/*****************************************************************************/

void
  kdws_file_services::add_editor_progid_for_doc_type(const char *doc_suffix,
                                                     kdws_string &prog_id)
{
  HKEY prog_key;
  if (RegOpenKeyEx(HKEY_CLASSES_ROOT,prog_id,0,KEY_READ,
                   &prog_key) == ERROR_SUCCESS)
    {
      HKEY shell_key;
      if (RegOpenKeyEx(prog_key,_T("shell"),0,KEY_READ,
                       &shell_key) == ERROR_SUCCESS)
        {
          HKEY action_key;
          for (int action=0; action < 2; action++)
            {
              const _TCHAR *action_verb = (action==1)?_T("edit"):_T("open");
              if (RegOpenKeyEx(shell_key,action_verb,0,KEY_READ,
                               &action_key) != ERROR_SUCCESS)
                continue;
              HKEY command_key;
              if (RegOpenKeyEx(action_key,_T("command"),0,KEY_READ,
                               &command_key) == ERROR_SUCCESS)
                {
                  kdws_string name_string(80);
                  kdws_string command_string(MAX_PATH+80);
                  for (int n=0; true; n++)
                    {
                      DWORD val_type;
                      DWORD name_chars = 80;
                      DWORD command_chars = MAX_PATH+80;
                      if (RegEnumValue(command_key,n,name_string,&name_chars,
                                       NULL,&val_type,
                                       (LPBYTE)((_TCHAR *) command_string),
                                       &command_chars) != ERROR_SUCCESS)
                        break;
                      if (!name_string.is_empty())
                        continue;

                      // Now we have the default value for the "command" key
                      if (val_type == REG_EXPAND_SZ)
                        { // Need to expand environment variables.
                          kdws_string expand_string(MAX_PATH+79);
                          DWORD expand_len =
                            ExpandEnvironmentStrings(command_string,
                                                     expand_string,
                                                     MAX_PATH+79);
                          if ((expand_len == 0) ||
                              (expand_len > (MAX_PATH+79)))
                            break; // Expand not successful
                          command_string.clear();
                          strcpy(command_string,expand_string);
                        }
                      else if (val_type != REG_SZ)
                        break;
                      char *sep, *app_path = command_string;
                      while ((*app_path == ' ') || (*app_path == '\t'))
                        app_path++;
                      bool have_arg = false;
                      for (sep=app_path; *sep != '\0'; sep++)
                        if ((sep[0] == '%') && (sep[1] == '1'))
                        { have_arg = true; break; }
                      if ((sep > app_path) && have_arg && (sep[-1]=='\"'))
                        sep--;
                      while ((sep > command_string) &&
                             ((sep[-1] == ' ') || (sep[-1] == '\t')))
                        sep--;
                      *sep = '\0';
                      if (!have_arg)
                        { // Still OK if "/dde" is found
                          if (((sep-app_path) < 6) ||
                              (sep[-4] != '/') || (sep[-3] != 'd') ||
                              (sep[-2] != 'd') || (sep[-1] != 'e'))
                            break;
                        }
                      if (*app_path != '\0')
                        add_editor_for_doc_type(doc_suffix,app_path);
                      break;
                    }
                  RegCloseKey(command_key);
                }
              RegCloseKey(action_key);
            }
          RegCloseKey(shell_key);
        }
      RegCloseKey(prog_key);
    }
}

/*****************************************************************************/
/*               kdws_file_services::add_editor_for_doc_type                 */
/*****************************************************************************/

kdws_file_editor *
  kdws_file_services::add_editor_for_doc_type(const char *doc_suffix,
                                              const char *app_pathname)
{
  kdws_file_editor *scan, *prev;
  for (scan=editors, prev=NULL; scan != NULL; prev=scan, scan=scan->next)
    if (strcmp(scan->app_pathname,app_pathname) == 0)
      break;
  
  if (scan != NULL)
    { // Found an existing editor
      if (prev == NULL)
        editors = scan->next;
      else
        prev->next = scan->next;
      scan->next = editors;
      editors = scan;
      return scan;
    }
  
  scan = new kdws_file_editor;
  scan->doc_suffix = new char[strlen(doc_suffix)+1];
  strcpy(scan->doc_suffix,doc_suffix);
  scan->app_pathname = new char[strlen(app_pathname)+1];
  strcpy(scan->app_pathname,app_pathname);
  
  const char *cp, *name_start = scan->app_pathname;
  for (cp=name_start; *cp != '\0'; cp++)
    if ((*cp == '\"') && (scan->app_pathname[0] == '\"') && (cp > name_start))
      break;
    else if ((cp[0] == '.') && (toupper(cp[1]) == (int)'E') &&
             (toupper(cp[2]) == (int)'X') && (toupper(cp[3]) == (int)'E'))
      break;
    else
      if (((*cp == '/') || (*cp == '\\') || (*cp == ':')) && (cp[1] != '\0'))
        name_start = cp+1;
  scan->app_name = new char[(cp-name_start)+1];
  memcpy(scan->app_name,name_start,(cp-name_start));
  scan->app_name[cp-name_start] = '\0';

  kdws_file_editor *dup;
  for (dup=editors; dup != NULL; dup=dup->next)
    if (strcmp(dup->app_name,scan->app_name) == 0)
      {
        delete scan;
        return dup;
      }
  scan->next = editors;
  editors = scan;
  return scan;
}


/* ========================================================================= */
/*                           kdws_compositor_buf                             */
/* ========================================================================= */

/*****************************************************************************/
/*                    kdws_compositor_buf::create_bitmap                     */
/*****************************************************************************/

void
  kdws_compositor_buf::create_bitmap(kdu_coords size)
{
  if (bitmap != NULL)
    {
      assert(this->size == size);
      return;
    }

  memset(&bitmap_info,0,sizeof(bitmap_info));
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = size.x;
  bitmap_info.bmiHeader.biHeight = -size.y;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  void *buf_addr;
  bitmap = CreateDIBSection(NULL,&bitmap_info,DIB_RGB_COLORS,&buf_addr,NULL,0);
  if (bitmap == NULL)
    { kdu_error e; e << "Unable to allocate sufficient bitmap surfaces "
      "to service `kdu_region_compositor'."; }
  this->buf = (kdu_uint32 *) buf_addr;
  this->row_gap = size.x;
  this->size = size;
}

/*****************************************************************************/
/*                   kdws_compositor_buf::flush_from_cache                   */
/*****************************************************************************/

void
  kdws_compositor_buf::flush_from_cache()
{
#if defined KDU_X86_INTRINSICS
  if (kdu_mmx_level < 2)
    return;
  kdu_uint32 *sp=this->buf, *ep=sp+size.x*size.y-1;
  for (ep-=128; sp < ep; sp+=128)
    {
      _mm_clflush(sp+ 0); _mm_clflush(sp+16);
      _mm_clflush(sp+32); _mm_clflush(sp+48);
      _mm_clflush(sp+64); _mm_clflush(sp+80);
      _mm_clflush(sp+96); _mm_clflush(sp+112);
    }
  for (ep+=128; sp < ep; sp+=16)
    _mm_clflush(sp);
  _mm_clflush(ep);
  _mm_mfence();
#endif // KDU_X86_INTRINSICS
}


/* ========================================================================= */
/*                          kdws_region_compositor                           */
/* ========================================================================= */

/*****************************************************************************/
/*                  kdws_region_compositor::record_viewport                  */
/*****************************************************************************/

void kdws_region_compositor::record_viewport(kdws_orientation orientation,
                                             double pixel_scale,
                                             kdu_dims view_dims)
{
  kdu_dims image_dims;
  bool configured = this->get_total_composition_dims(image_dims);
  mutex.lock();
  kdws_display_geometry new_geometry;
  if (configured)
    { 
      new_geometry.orientation = orientation;
      new_geometry.pixel_scale = pixel_scale;
      new_geometry.image_dims = image_dims;
      new_geometry.viewport_dims = (view_dims & image_dims);
      if (display_geometry.focus_state)
        { 
          new_geometry.focus_box = display_geometry.focus_box;
          new_geometry.focus_box &= display_geometry.viewport_dims;
          if (!new_geometry.focus_box.is_empty())
            new_geometry.focus_state = display_geometry.focus_state;
        }
      if (display_geometry != new_geometry)
        { 
          need_display_redraw = true;
          display_geometry = new_geometry;
        }
    }
  mutex.unlock();
}

/*****************************************************************************/
/*                   kdws_region_compositor::set_focus_box                   */
/*****************************************************************************/

bool kdws_region_compositor::set_focus_box(kdu_dims &box, int focus_state)
{
  bool return_val = false;
  kdu_dims return_box;
  mutex.lock();
  if ((focus_state != display_geometry.focus_state) ||
      (focus_state && (box != display_geometry.focus_box)))
    need_display_redraw = return_val = true;
  if (display_geometry.focus_state)
    return_box = display_geometry.focus_box;
  display_geometry.focus_box = box;
  display_geometry.focus_state = focus_state;
  mutex.unlock();
  box = return_box;
  return return_val;
}

/*****************************************************************************/
/*                   kdws_region_compositor::allocate_buffer                 */
/*****************************************************************************/

kdu_compositor_buf *
  kdws_region_compositor::allocate_buffer(kdu_coords min_size,
                                          kdu_coords &actual_size,
                                          bool read_access_required)
{
  if (min_size.x < 1) min_size.x = 1;
  if (min_size.y < 1) min_size.y = 1;
  actual_size = min_size;
  int row_gap = actual_size.x;

  kdws_compositor_buf *prev=NULL, *elt=NULL;

  // Start by trying to find a compatible buffer on the free list.
  for (elt=free_list, prev=NULL; elt != NULL; prev=elt, elt=elt->next)
    if (elt->size == actual_size)
      break;

  bool need_init = false;
  if (elt != NULL)
    { // Remove from the free list
      if (prev == NULL)
        free_list = elt->next;
      else
        prev->next = elt->next;
      elt->next = NULL;
    }
  else
    {
      // Delete the entire free list: one way to avoid accumulating buffers
      // whose sizes are no longer helpful
      while ((elt=free_list) != NULL)
        { free_list = elt->next;  delete elt; }

      // Allocate a new element
      elt = new kdws_compositor_buf;
      need_init = true;
    }
  elt->next = active_list;
  active_list = elt;
  elt->set_read_accessibility(read_access_required);
  if (need_init)
    elt->create_bitmap(actual_size);
  elt->reset_state();
  return elt;
}

/*****************************************************************************/
/*                  kdws_region_compositor::delete_buffer                    */
/*****************************************************************************/

void
  kdws_region_compositor::delete_buffer(kdu_compositor_buf *_buffer)
{
  kdws_compositor_buf *buffer = (kdws_compositor_buf *) _buffer;
  kdws_compositor_buf *scan, *prev;
  for (prev=NULL, scan=active_list; scan != NULL; prev=scan, scan=scan->next)
    if (scan == buffer)
      break;
  assert(scan != NULL);
  if (prev == NULL)
    active_list = scan->next;
  else
    prev->next = scan->next;

  scan->next = free_list;
  free_list = scan;
}

/*****************************************************************************/
/*           kdws_region_compositor::lock_new_composition_queue_head         */
/*****************************************************************************/

bool
  kdws_region_compositor::lock_new_composition_queue_head(
                                               double cur_display_time,
                                               double next_display_time)
{
  bool result = false;
  mutex.lock();

  // First, store the updated display times for later access from the
  // main application thread.
  this->last_display_event_time = cur_display_time;
  this->next_display_event_time = next_display_time;
  double epsilon = 0.1*(next_display_time - cur_display_time);
  double display_threshold = cur_display_time + epsilon;
  this->next_display_time_threshold = next_display_time + epsilon;
  
  // Start by finding out how many buffers we can pop from the queue
  int idx, pop_count=0;
  kdu_compositor_buf *buf;
  for (idx=1; (buf=inspect_composition_queue(idx)) != NULL; idx++)
    if (((kdws_compositor_buf *)buf)->get_display_time() <= display_threshold)
      pop_count = idx;
  for (; pop_count > 0; pop_count--)
    { 
      pop_composition_buffer();
      queue_head_presented = false;
    }
  
  if (((buf = inspect_composition_queue(0)) != NULL))
    { 
      kdws_compositor_buf *my_buf = (kdws_compositor_buf *) buf;
      double disp_time = my_buf->get_display_time();
      if ((disp_time <= display_threshold) &&
          (need_display_redraw || !queue_head_presented))
        { 
          result = queue_head_locked = true;
          my_buf->advance_animator_roi(cur_display_time-disp_time);
        }
    }
  need_display_redraw = false;
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*      kdws_region_compositor::unlock_and_mark_composition_queue_head       */
/*****************************************************************************/

void kdws_region_compositor::unlock_and_mark_composition_queue_head()
{
  mutex.lock();
  int frame_idx=0;
  kdu_compositor_buf *buf;
  if ((buf = inspect_composition_queue(0,NULL,&frame_idx)) != NULL)
    { 
      kdws_compositor_buf *my_buf = (kdws_compositor_buf *) buf;
      presented_queue_head_frame_idx = frame_idx;
      my_buf->get_orig_frame_times(presented_queue_head_frame_start,
                                   presented_queue_head_frame_end);
      presented_queue_head_metanode =
        my_buf->get_metanode(presented_queue_head_metamation_frame_id);
      presented_queue_head_jpip_progress = my_buf->jpip_progress;
      presented_queue_head_display_time = my_buf->display_time;
      have_presented_queue_head_info = true;
      if (!my_buf->advance_animator_roi(1.0 / 60.0))
        { 
          queue_head_presented = true;
          queue_tail_presented = (inspect_composition_queue(1) == NULL);
        }
    }
  queue_head_locked = false;
  mutex.unlock();
}

/*****************************************************************************/
/*         kdws_region_compositor::get_presented_queue_head_info             */
/*****************************************************************************/

bool
  kdws_region_compositor::get_presented_queue_head_info(int &frame_idx,
                              double &frame_start, double &frame_end,
                              jpx_metanode &node, int &metamation_id,
                              double &jpip_progress, double &display_time,
                              double &frame_change_threshold,
                              double &node_change_threshold,
                              double &progress_change_threshold)
{
  bool result;
  mutex.lock();
  result = have_presented_queue_head_info;
  if (result)
    { 
      frame_idx = presented_queue_head_frame_idx;
      frame_start = presented_queue_head_frame_start;
      frame_end = presented_queue_head_frame_end;
      node = presented_queue_head_metanode;
      metamation_id = presented_queue_head_metamation_frame_id;
      jpip_progress = presented_queue_head_jpip_progress;
      display_time = presented_queue_head_display_time;
      bool frame_change_found = (frame_change_threshold < 0.0);
      bool node_change_found = (node_change_threshold < 0.0);
      bool progress_change_found = (progress_change_threshold < 0.0);
      if (!queue_tail_presented)
        { // Need to see if there are later elements of the queue whose
          // display time may still be within the threshold
          int new_frm_idx=frame_idx;
          int qidx = (queue_head_presented)?1:0;
          kdws_compositor_buf *my_buf;
          while ((!(frame_change_found && node_change_found &&
                    progress_change_found)) &&
                 ((my_buf = (kdws_compositor_buf *)
                   inspect_composition_queue(qidx++,NULL,
                                             &new_frm_idx)) != NULL))
            { 
              if ((!frame_change_found) &&
                  (my_buf->display_time <= frame_change_threshold) &&
                  (new_frm_idx != frame_idx))
                { 
                  frame_change_found = true;
                  frame_change_threshold = my_buf->display_time;
                }
              if ((!node_change_found) &&
                  (my_buf->display_time <= node_change_threshold) &&
                  (my_buf->metanode != node))
                { 
                  node_change_found = true;
                  node_change_threshold = my_buf->display_time;
                }
              if ((!progress_change_found) &&
                  (my_buf->display_time <= progress_change_threshold) &&
                  (my_buf->jpip_progress != jpip_progress))
                { 
                  progress_change_found = true;
                  progress_change_threshold = my_buf->display_time;
                }
            }
        }
      if (!frame_change_found)
        frame_change_threshold = -1.0;
      if (!node_change_found)
        node_change_threshold = -1.0;
      if (!progress_change_found)
        progress_change_threshold = -1.0;
    }
  mutex.unlock();
  return result;
}

/*****************************************************************************/
/*       kdws_region_compositor::conditionally_push_composition_buffer       */
/*****************************************************************************/

bool
  kdws_region_compositor::conditionally_push_composition_buffer(double d_time,
                                                                int frame_idx,
                                                                int max_bufs)
{
  bool result = false;
  mutex.lock();
  if (!queue_head_locked)
    { // See if we can pop some stuff
      if (queue_head_presented)
        { 
          pop_composition_buffer();
          queue_head_presented = queue_tail_presented = false;
        }
      kdws_compositor_buf *head_buf=NULL;
      while (((head_buf = (kdws_compositor_buf *)
               inspect_composition_queue(0)) != NULL) &&
             (head_buf->get_display_time() >= d_time))
        { // This buffer will be discarded by `lock_new_composition_queue_head'
          // so we might as well get rid of it immediately.
          pop_composition_buffer();
        }
    }

  if (inspect_composition_queue(max_bufs-1) == NULL)
    { // We can just augment the buffer queue
      result = push_composition_buffer(0,frame_idx);
      queue_tail_presented = false;
    }
  else if ((d_time < next_display_time_threshold) &&
           ((max_bufs > 1) || !queue_head_locked))
    { // We can strip the last buffer from the composition queue
      result = replace_composition_queue_tail(0,frame_idx);
      queue_tail_presented = false;
      if (max_bufs == 1)
        queue_head_presented = false;
    }
  mutex.unlock();
  return result;
}


/* ========================================================================= */
/*                               kdws_rel_dims                               */
/* ========================================================================= */

/*****************************************************************************/
/*                             kdws_rel_dims::set                            */
/*****************************************************************************/

bool kdws_rel_dims::set(kdu_dims region, kdu_dims container)
{
  kdu_dims content = region & container;
  centre_x = centre_y = size_x = size_y = 0.0;
  if (!container)
    return false;
  if (!content.is_empty())
    {
      double fact_x = 1.0 / container.size.x;
      double fact_y = 1.0 / container.size.y;
      size_x = fact_x * content.size.x;
      size_y = fact_y * content.size.y;
      centre_x = fact_x * (content.pos.x-container.pos.x) + 0.5*size_x;
      centre_y = fact_y * (content.pos.y-container.pos.y) + 0.5*size_y;
    }
  return (content == region);
}

/*****************************************************************************/
/*                     kdws_rel_dims::apply_to_container                     */
/*****************************************************************************/

kdu_dims kdws_rel_dims::apply_to_container(kdu_dims container)
{
  kdu_dims result;
  result.size.x = (int)(0.5 + size_x * container.size.x);
  result.size.y = (int)(0.5 + size_y * container.size.y);
  result.pos.x = (int)(0.5 + (centre_x-0.5*size_x) * container.size.x);
  result.pos.y = (int)(0.5 + (centre_y-0.5*size_y) * container.size.y);
  result.pos += container.pos;
  return result;
}


/* ========================================================================= */
/*                               kdws_renderer                               */
/* ========================================================================= */

/*****************************************************************************/
/*                        kdws_renderer::kdws_renderer                       */
/*****************************************************************************/

kdws_renderer::kdws_renderer(kdws_frame_window *owner,
                             kdws_image_view *image_view,
                             kdws_manager *manager)
{
  this->window = owner;
  this->image_view = image_view;
  this->manager = manager;
  this->metashow = NULL;
  this->window_identifier = manager->get_window_identifier(owner);
  this->catalog_source = NULL;
  this->catalog_closed = false;
  this->editor = NULL;

  processing_call_to_open_file = false;
  open_file_pathname = NULL;
  open_file_urlname = NULL;
  open_filename = NULL;
  last_save_pathname = NULL;
  save_without_asking = false;
  duplication_src = NULL;

  compositor = NULL;
  num_recommended_threads = kdu_get_num_processors();
  if (num_recommended_threads < 2)
    num_recommended_threads = 0;
  else if (num_recommended_threads > 8)
    num_recommended_threads = 8;
  num_threads = num_recommended_threads;
  in_idle = false;
  defer_process_regions_deadline = -1.0;

  file_server = NULL;
  have_unsaved_edits = false;

  jpip_client = NULL;
  client_request_queue_id = -1;
  one_time_jpip_request = false;
  respect_initial_jpip_request = false;
  enable_region_posting = false;
  animator_has_oob_metareqs = false;
  catalog_has_oob_metareqs = false;
  have_novel_anchor_metareqs = false;
  jpip_client_received_queue_bytes = 0;
  jpip_client_received_total_bytes = 0;
  jpip_cache_metadata_bytes = 0;
  jpip_wants_render_refresh = false;
  jpip_refresh_interval = 0.5;
  jpip_client_status = "<no JPIP>";
  jpip_interval_start_bytes = 0;
  jpip_interval_start_time = 0.0;
  jpip_client_data_rate = -1.0;
  jpip_progress = 0.0;
  jpip_last_displayed_progress = 0.0;

  allow_seeking = true;
  error_level = 0;
  max_display_layers = 1<<16;
  min_rendering_scale = -1.0F;
  max_rendering_scale = -1.0F;
  rendering_scale = 1.0F;
  max_components = max_codestreams = max_compositing_layer_idx = -1;
  single_component_idx = single_codestream_idx = single_layer_idx = 0;
  frame_idx = 0; track_idx = 0;
  num_frames = 0; max_track_idx = 0;
  num_frames_known = num_tracks_known = false;
  frame_start = frame_end = 0.0;
  frame_expander.reset();
  fit_scale_to_window = false;
  configuration_complete = false;
  status_id = KDS_STATUS_LAYER_RES;
  pending_show_metanode = jpx_metanode();

  animation_bar = NULL;
  metamation_bar = NULL;
  animator = NULL;
  animation_repeat = false;
  animation_uses_native_timing = true;
  animation_skip_undisplayables = true;
  animation_custom_rate = 1.0;
  animation_native_rate_multiplier = 1.0;
  pushed_last_frame = false;
  animation_frame_status_update_time = 0.0;
  animation_metanode_status_update_time = 0.0;
  animation_jpip_status_update_time = 0.0;
  animation_display_idx = -1;
  animation_display_jpip_progress = -1.0;
  animation_status_invalid = false;

  animation_max_queued_frames = 3;
  animation_metanode_shows_box = false;
  animation_advancing = false;

  animation_frame_started = false;
  animation_frame_complete = false;
  animation_frame_needs_push = false;
  animation_border_x = animation_border_y = 0.0F;
  animation_last_sys_time = -1.0;
  frame_presenter = manager->get_frame_presenter(owner);

  earliest_render_refresh_time= -1.0;
  last_metadata_refresh_time = -1.0;
  next_render_refresh_time = -1.0;
  next_metadata_refresh_time = -1.0;
  next_overlay_change_time = -1.0;
  next_frame_advance_time = -1.0;
  next_scheduled_wakeup_time = -1.0;

  pixel_scale = 1;
  quality_limiting_mode = 1;
  image_dims.pos = image_dims.size = kdu_coords(0,0);
  working_buffer_dims = view_dims = image_dims;
  view_centre_x = view_centre_y = 0.0F;
  view_centre_known = false;
  view_dims_change_deferred = false;

  highlight_focus = true;
  manual_focus_pens[0].CreatePen(PS_SOLID,1,RGB(153,102,51));
  manual_focus_pens[1].CreatePen(PS_SOLID,1,RGB(255,255,0));
  metadata_focus_pens[0].CreatePen(PS_SOLID,1,RGB(0,0,255));
  metadata_focus_pens[1].CreatePen(PS_SOLID,1,RGB(255,255,0));
  focussing_pens[0].CreatePen(PS_SOLID,1,RGB(153,102,51));
  focussing_pens[1].CreatePen(PS_DASH,1,RGB(255,255,0));
  focussing_pens[2].CreatePen(PS_SOLID,1,RGB(153,102,51));

  overlays_enabled = false;
  overlays_auto_flash = true;
  overlay_flashing_state = 1;
  overlay_highlight_state = 0;
  overlay_nominal_intensity = KDWS_OVERLAY_DEFAULT_INTENSITY;
  overlay_size_threshold = 1;
  kdu_uint32 params[KDWS_NUM_OVERLAY_PARAMS] = KDWS_OVERLAY_PARAMS;
  for (int p=0; p < KDWS_NUM_OVERLAY_PARAMS; p++)
    overlay_params[p] = params[p];
  
  working_buffer = NULL;
  compatible_dc.CreateCompatibleDC(NULL);
  strip_extent = kdu_coords(0,0);
  stripmap = NULL;
  stripmap_buffer = NULL;
  for (int i=0; i < 256; i++)
    {
      foreground_lut[i] = (kdu_byte)(40 + ((i*216) >> 8));
      background_lut[i] = (kdu_byte)((i*216) >> 8);
    }

  set_key_window_status(window->is_key_window());
}

/*****************************************************************************/
/*                        kdws_renderer::~kdws_renderer                      */
/*****************************************************************************/

kdws_renderer::~kdws_renderer()
{
  frame_presenter = NULL; // Avoid access to potentially non-existent object
  perform_essential_cleanup_steps(false);

  if (file_server != NULL)
    { delete file_server; file_server = NULL; }
  if (compositor != NULL)
    { delete compositor; compositor = NULL; }
  if (metashow != NULL)
    metashow->close();
  if (editor != NULL)
    {
      editor->close();
      editor = NULL;
    }
  if (thread_env.exists())
    thread_env.destroy();
  file_in.close();
  jpx_in.close();
  mj2_in.close();
  jp2_family_in.close();
  cache_in.close();
  if (jpip_client != NULL)
    {
      jpip_client->disconnect(false,20000,client_request_queue_id,false);
      // Give it a long timeout so as to avoid accidentally breaking any
      // other request queues by forcing a timeout, but we won't wait for
      // closure here.  Forced closure with a shorter timeout can be
      // performed in the `release_jpip_client' call below, if there
      // are no windows left using the client.
      manager->release_jpip_client(jpip_client,window);
      jpip_client = NULL;
    }
  if (stripmap != NULL)
    { DeleteObject(stripmap); stripmap = NULL; stripmap_buffer = NULL; }
  if (last_save_pathname != NULL)
    delete[] last_save_pathname;
  manual_focus_pens[0].DeleteObject();
  manual_focus_pens[1].DeleteObject();
  metadata_focus_pens[0].DeleteObject();
  metadata_focus_pens[1].DeleteObject();
  focussing_pens[0].DeleteObject();
  focussing_pens[1].DeleteObject();
  focussing_pens[2].DeleteObject();
}

/*****************************************************************************/
/*             kdws_renderer::perform_essential_cleanup_steps                */
/*****************************************************************************/

void kdws_renderer::perform_essential_cleanup_steps(bool app_terminating)
{
  if (frame_presenter != NULL)
    frame_presenter->disable(); // Make sure there is no asynchronous painting
  if (manager != NULL)
    manager->schedule_wakeup(window,-1.0);
  if (file_server != NULL)
    delete file_server;
  file_server = NULL;
  if (animator != NULL)
    delete animator;
  animator = NULL;
  if ((manager != NULL) && (open_file_pathname != NULL))
    {
      if (compositor != NULL)
        { delete compositor; compositor = NULL; }
      file_in.close(); // Unlocks any files
      jp2_family_in.close(); // Unlocks any files
      manager->release_open_file_pathname(open_file_pathname,window);
      open_file_pathname = NULL;
      open_filename = NULL;
    }
  if (app_terminating && (jpip_client != NULL))
    { // App is about to terminate; disconnect ASAP and release the client
      jpip_client->disconnect(false,1,client_request_queue_id,false);
      manager->release_jpip_client(jpip_client,window);      
      jpip_client = NULL;
    }
}

/*****************************************************************************/
/*                        kdws_renderer::deactivate                          */
/*****************************************************************************/

void kdws_renderer::deactivate()
{
  if (catalog_source != NULL)
    catalog_source->deactivate();
  if (metashow != NULL)
    metashow->deactivate();
  shape_editor = NULL;  shape_istream = kdu_istream_ref();
  stop_animation();

  // Cancel any pending wakeups
  earliest_render_refresh_time = -1.0;
  last_metadata_refresh_time = -1.0;
  next_render_refresh_time = -1.0;
  next_metadata_refresh_time = -1.0;
  next_overlay_change_time = -1.0;
  next_frame_advance_time = -1.0;
  next_scheduled_wakeup_time = -1.0;
  manager->schedule_wakeup(window,-1.0);
  
  // Close down all information rendering machinery
  if (compositor != NULL)
    { delete compositor; compositor = NULL; }
  if (thread_env.exists())
    thread_env.destroy();
  if (editor != NULL)
    { 
      editor->close();
      assert(editor == NULL);
    }
  file_in.close();
  cache_in.close();
  jpx_in.close();
  mj2_in.close();
  jp2_family_in.close();
  
  // Reset state variables
  client_roi.init();
  oob_metareqs.init();
  one_time_jpip_request = false;
  respect_initial_jpip_request = false;
  enable_region_posting = false;
  animator_has_oob_metareqs = false;
  catalog_has_oob_metareqs = false;
  have_novel_anchor_metareqs = false;
  jpip_client_received_queue_bytes = 0;
  jpip_client_received_total_bytes = 0;
  jpip_cache_metadata_bytes = 0;
  jpip_wants_render_refresh = false;
  jpip_client_status = "<no JPIP>";
  jpip_interval_start_bytes = 0;
  jpip_interval_start_time = 0.0;
  jpip_client_data_rate = -1.0;
  jpip_progress = 0.0;
  jpip_last_displayed_progress = 0.0;
  
  composition_rules = jpx_composition(NULL);
  defer_process_regions_deadline = -1.0;
  fit_scale_to_window = false;
  frame_idx = 0; track_idx = 0;
  num_frames = 0; max_track_idx = 0;
  num_frames_known = num_tracks_known = false;
  frame_start = frame_end = 0.0;
  frame = jpx_frame();
  frame_expander.reset();
  configuration_complete = false;
  orientation.reset();
  pending_show_metanode = jpx_metanode();
  last_show_metanode = jpx_metanode();
  
  animation_uses_native_timing = true;
  animation_skip_undisplayables = true;
  animation_custom_rate = 1.0;
  animation_native_rate_multiplier = 1.0;
  animation_border_x = animation_border_y = 0.0F;
  animation_last_sys_time = -1.0;
  if ((animation_bar != NULL) || (metamation_bar != NULL))
    {
    window->remove_animation_bar();
    animation_bar = metamation_bar = NULL;
    }
  window->reset_animation_bar();
  
  image_dims.pos = image_dims.size = kdu_coords(0,0);
  working_buffer_dims = view_dims = image_dims;
  view_centre_known = false;
  view_dims_change_deferred = false;
  
  rel_focus.reset();
  focus_metanode = jpx_metanode();
  
  overlays_enabled = false;
  overlay_flashing_state = 1;
  overlay_highlight_state = 0;
  highlight_metanode = jpx_metanode();
  overlay_nominal_intensity = KDWS_OVERLAY_DEFAULT_INTENSITY;
  overlay_restriction = last_selected_overlay_restriction = jpx_metanode();
  
  working_buffer = NULL;
  
  if ((catalog_source != NULL) && (window != NULL))
    window->remove_metadata_catalog();
  else if (catalog_source != NULL)
    set_metadata_catalog_source(NULL); // Just in case the window went away
  catalog_closed = false;
}

/*****************************************************************************/
/*                          kdws_renderer::close_file                        */
/*****************************************************************************/

void
  kdws_renderer::close_file()
{
  if (catalog_source != NULL)
    catalog_source->deactivate();
  if (metashow != NULL)
    metashow->deactivate();
  shape_editor = NULL;  shape_istream = kdu_istream_ref();
  stop_animation();

  perform_essential_cleanup_steps(false);
  deactivate();

  // Reset all file path names and related attributes
  processing_call_to_open_file = false;
  open_file_pathname = NULL;
  open_file_urlname = NULL;
  open_filename = NULL;
  if (last_save_pathname != NULL)
    { delete[] last_save_pathname; last_save_pathname = NULL; }
  save_without_asking = false;

  if (file_server != NULL)
    { delete file_server; file_server = NULL; }
  have_unsaved_edits = false;

  // Shut down JPIP client
  if (jpip_client != NULL)
    {
      jpip_client->disconnect(false,20000,client_request_queue_id,false);
          // Give it a long timeout so as to avoid accidentally breaking any
          // other request queues by forcing a timeout, but we won't wait for
          // closure here.  Forced closure with a shorter timeout can be
          // performed in the `release_jpip_client' call below, if there
          // are no windows left using the client.
      manager->release_jpip_client(jpip_client,window);
      jpip_client = NULL;
    }
  client_request_queue_id = -1;
  client_prefs.init(); // All defaults

  // Update GUI elements to reflect "closed" status
  char title[80];
  sprintf(title,"kdu_show %d: <no data>",window_identifier);
  window->SetWindowText(kdws_string(title));
  window->remove_progress_bar();
  if (image_view != NULL)
    {
      image_view->set_max_view_size(kdu_coords(20000,20000),pixel_scale);
      if (image_view->GetSafeHwnd() != NULL)
        image_view->Invalidate();
    }
  manager->declare_window_empty(window,true);
}

/*****************************************************************************/
/*                   kdws_renderer::close_after_exception                    */
/*****************************************************************************/

void kdws_renderer::close_after_exception(kdu_exception exc)
{
  if (thread_env.exists())
    { 
      thread_env.handle_exception(exc);
      thread_env.destroy();
      if (compositor != NULL)
        compositor->set_thread_env(NULL,NULL);
    }
  close_file();
}

/*****************************************************************************/
/*                   kdws_renderer::set_key_window_status                    */
/*****************************************************************************/

void kdws_renderer::set_key_window_status(bool is_key_window)
{
  jpip_refresh_interval = (is_key_window)?0.5:2.0;
  jpip_metadata_refresh_interval = (is_key_window)?0.25:1.0;
  if (!is_key_window)
    return;
  bool jpip_bytes_changed = false;
  if (jpip_client != NULL)
    { // Perhaps there are more bytes available anyway -- because of activity
      // in a different window which shares the same cache; better check here,
      // especially because this might augment the metadata catalog
      // significantly, which is very useful for a key window.
      kdu_long new_bytes = jpip_client->get_received_bytes(-1);
      if (new_bytes > jpip_client_received_total_bytes)
        { 
          jpip_bytes_changed = true;
          if (animator == NULL)
            jpip_wants_render_refresh = true;
          jpip_client_received_total_bytes = new_bytes;
        }
    }
  
  double cur_time = get_current_time();
  if (jpip_bytes_changed && (animator == NULL) &&
      (earliest_render_refresh_time >= 0.0))
    { // Arrange for a new refresh to happen quite soon
      double latest_time = cur_time+0.5*jpip_refresh_interval;
      if (latest_time < earliest_render_refresh_time)
        earliest_render_refresh_time = latest_time;
      perform_or_schedule_render_refresh(cur_time);
    }
  
  if (jpip_bytes_changed)
    { // May need to refresh metadata or schedule/reschedule a refresh
      double refresh_interval = jpip_metadata_refresh_interval;
      if (catalog_has_oob_metareqs)
        refresh_interval *= 0.2;
      if (cur_time > (last_metadata_refresh_time + 0.9*refresh_interval))
        refresh_metadata(cur_time);
      else
        schedule_metadata_refresh_wakeup(last_metadata_refresh_time +
                                         refresh_interval);
    }
}

/*****************************************************************************/
/*                     kdws_renderer::open_as_duplicate                      */
/*****************************************************************************/

void kdws_renderer::open_as_duplicate(kdws_renderer *src)
{
  this->duplication_src = src;
  if (src->open_file_pathname != NULL)
    window->open_file(src->open_file_pathname);
       // We could just call `open_file' directly, but the above statement
       // interrogates the user if the file has been modified.
  else if (src->open_file_urlname != NULL)
    open_file(src->open_file_urlname);
  this->duplication_src = NULL; // Because the `src' might disappear
}

/*****************************************************************************/
/*                          kdws_renderer::open_file                         */
/*****************************************************************************/

void
  kdws_renderer::open_file(const char *filename_or_url)
{
  if (processing_call_to_open_file)
    return; // Prevent recursive entry to this function from event handlers
  if (filename_or_url != NULL)
    {
      close_file();
      manager->declare_window_empty(window,false);
    }
  if ((num_threads > 0) && !thread_env.exists())
    {
      thread_env.create();
      for (int k=1; k < num_threads; k++)
        if (!thread_env.add_thread())
          num_threads = k;
    }
  processing_call_to_open_file = true;
  assert(compositor == NULL);
  client_roi.init();
  enable_region_posting = false;
  try {
      kdu_cache_file_info kjc_info;
      if (filename_or_url != NULL)
        {
          const char *url_host=NULL, *url_request=NULL;
          if ((url_host =
               kdu_client::check_compatible_url(filename_or_url,false,NULL,
                                                &url_request)) != NULL)
            { // Open as an interactive client-server application
              if (url_request == NULL)
                { kdu_error e;
                  e << "Illegal JPIP request, \"" << filename_or_url
                    << "\".  General form is \"<prot>://<hostname>[:<port>]/"
                       "<resource>[?<query>]\"\nwhere <prot> is \"jpip\" or "
                       "\"http\" and forward slashes may be substituted "
                       "for back slashes if desired.";
                }
              open_file_urlname =
                manager->retain_jpip_client(NULL,NULL,filename_or_url,
                                                jpip_client,
                                                client_request_queue_id,
                                                window);
            }
          else if (kdu_client::check_cache_file(filename_or_url,&kjc_info))
            { 
              const char *cache_identifier = kjc_info.get_cache_identifier();
              open_file_urlname =
                manager->retain_jpip_cache(filename_or_url,
                                           cache_identifier,jpip_client,
                                           client_request_queue_id,
                                           window);
            }
          else
            { // Open as a local file
              status_id = KDS_STATUS_LAYER_RES;
              jp2_family_in.open(filename_or_url,allow_seeking);
              compositor = new kdws_region_compositor(&thread_env);
              if (jpx_in.open(&jp2_family_in,true) >= 0)
                { // We have a JP2/JPX-compatible file.
                  compositor->create(&jpx_in);
                }
              else if (mj2_in.open(&jp2_family_in,true) >= 0)
                { 
                  compositor->create(&mj2_in);
                }
              else
                { // Incompatible with JP2/JPX or MJ2. Try opening as raw stream
                  jp2_family_in.close();
                  file_in.open(filename_or_url,allow_seeking);
                  compositor->create(&file_in);
                }
              compositor->set_error_level(error_level);
              compositor->set_process_aggregation_threshold(0.5F);
                // Display updates are expensive, so encourage aggregtion in
                // calls to `process'.
              open_file_pathname =
                manager->retain_open_file_pathname(filename_or_url,
                                                   window);
              const char *cp = open_filename = open_file_pathname;
              for (; *cp != '\0'; cp=cp++)
                if ((*cp == '/') || (*cp == '\\'))
                  open_filename = cp+1;
            }
        
          if (jpip_client != NULL)
            { // Complete initialization for the case where we have opened a
              // JPIP cache file or a connection to a server
              one_time_jpip_request = jpip_client->is_one_time_request();
              respect_initial_jpip_request = one_time_jpip_request;
              if ((client_request_queue_id == 0) && (!one_time_jpip_request) &&
                  jpip_client->connect_request_has_non_empty_window())
                respect_initial_jpip_request = true;
              open_filename = jpip_client->get_target_name();
              jpip_client_status =
                jpip_client->get_status(client_request_queue_id);
              status_id = KDS_STATUS_CACHE;
              cache_in.attach_to(jpip_client);
              jp2_family_in.open(&cache_in);
              window->set_progress_bar(0.0);
            }
          if ((metashow != NULL) && jp2_family_in.exists())
            { 
              metashow->activate(&jp2_family_in,open_filename);
              refresh_metadata(get_current_time());
            }
        }

      if ((jpip_client != NULL) && jpip_client->target_started())
        { // See if the client is ready yet, creating a preserve window to
          // pass to the client if it is.
          kdu_window preserve_wnd;
          preserve_wnd.resolution = kdu_coords(500,500);
          preserve_wnd.round_direction = 0; // nearest
          preserve_wnd.region.pos = kdu_coords(0,0);
          preserve_wnd.region.size = preserve_wnd.resolution;
          assert((compositor == NULL) && jp2_family_in.exists());
          bool bin0_complete = false;
          if (cache_in.get_databin_length(KDU_META_DATABIN,0,0,
                                          &bin0_complete) > 0)
            { // Open as a JP2/JPX family file
              if (metashow != NULL)
                metashow->update_metadata();
              if (jpx_in.open(&jp2_family_in,false))
                {
                  compositor = new kdws_region_compositor(&thread_env);
                  compositor->create(&jpx_in);
                }
              manager->use_jpx_translator_with_jpip_client(jpip_client);
              kdu_sampled_range rg; rg.from = rg.to = 0;
              rg.context_type = KDU_JPIP_CONTEXT_JPXL;
              preserve_wnd.contexts.add(rg); // Preserve compositing layer 0
            }
          else if (bin0_complete)
            { // Open as a raw file
              assert(!jpx_in.exists());
              if (metashow != NULL)
                metashow->update_metadata();
              bool hdr_complete;
              cache_in.get_databin_length(KDU_MAIN_HEADER_DATABIN,0,0,
                                          &hdr_complete);
              if (hdr_complete)
                {
                  cache_in.set_read_scope(KDU_MAIN_HEADER_DATABIN,0,0);
                  compositor = new kdws_region_compositor(&thread_env);
                  compositor->create(&cache_in);
                }
              preserve_wnd.codestreams.add(0); // Preserve codestream 0
            }
          if (compositor != NULL)
            { 
              compositor->set_error_level(error_level);
              compositor->set_process_aggregation_threshold(0.5F);
                // Display updates are expensive, so encourage aggregtion in
                // calls to `process'.
              client_roi.init();
              enable_region_posting = true;
              jpip_client->set_preserve_window(&preserve_wnd); // May get done
                // multiple times if there are other windows using this client,
                // but not worth bothering to avoid this redundancy.
            }
          else if (!jpip_client->is_alive(client_request_queue_id))
            {
              kdu_error e; e << "Unable to recover sufficient information "
              "from remote server (or a local cache) to open the image.";
            }
        }
    } catch (kdu_exception exc) {
      close_after_exception(exc);
      processing_call_to_open_file = false;
      return;
    }

  if (compositor != NULL)
    { 
      max_display_layers = 1<<16;
      orientation.reset();
      min_rendering_scale = -1.0F;
      max_rendering_scale = -1.0F;
      rendering_scale = 1.0F;
      single_component_idx = -1;
      single_codestream_idx = 0;
      max_components = -1;
      frame_idx = 0; track_idx = 0;
      num_frames = 0; max_track_idx = 0;
      num_frames_known = num_tracks_known = false;
      frame = jpx_frame();
      frame_expander.reset();
      composition_rules = jpx_composition(NULL);
      if (jpx_in.exists())
        {
          max_codestreams = -1;  // Unknown as yet
          max_compositing_layer_idx = -1; // Unknown as yet
          single_layer_idx = -1; // Try starting in composite frame mode
        }
      else if (mj2_in.exists())
        {
          single_layer_idx = -1; // Try starting in composed track mode
          track_idx = 1;
        }
      else
        {
          max_codestreams = 1;
          max_compositing_layer_idx = 0;
          num_frames = 0;
          single_layer_idx = 0; // Start in composed single layer mode
        }

      overlays_enabled = true; // Default starting position
      overlay_flashing_state = 1;
      overlay_highlight_state = 0;
      overlay_nominal_intensity = KDWS_OVERLAY_DEFAULT_INTENSITY;
      configure_overlays(-1.0,false);

      invalidate_configuration();
      fit_scale_to_window = true;
      image_dims = working_buffer_dims = view_dims = kdu_dims();
      working_buffer = NULL;
      if (duplication_src != NULL)
        { 
          kdws_renderer *src = duplication_src;
          if (src->last_save_pathname != NULL)
            {
              last_save_pathname = new char[strlen(src->last_save_pathname)+1];
              strcpy(last_save_pathname,src->last_save_pathname);
            }
          save_without_asking = src->save_without_asking;
          allow_seeking = src->allow_seeking;
          error_level = src->error_level;
          max_display_layers = src->max_display_layers;
          orientation = src->orientation;
          min_rendering_scale = src->min_rendering_scale;
          max_rendering_scale = src->max_rendering_scale;
          rendering_scale = src->rendering_scale;
          single_component_idx = src->single_component_idx;
          max_components = src->max_components;
          single_codestream_idx = src->single_component_idx;
          max_codestreams = src->max_codestreams;
          single_layer_idx = src->single_layer_idx;
          max_compositing_layer_idx = src->max_compositing_layer_idx;
          track_idx = src->track_idx;
          max_track_idx = src->max_track_idx;
          num_tracks_known = src->num_tracks_known;
          frame_idx = src->frame_idx;
          num_frames = src->num_frames;
          num_frames_known = src->num_frames_known;
          frame_start = src->frame_start;
          frame_end = src->frame_end;
          frame = src->frame;
          status_id = src->status_id;
          pixel_scale = src->pixel_scale;
          quality_limiting_mode = src->quality_limiting_mode;
          src->calculate_rel_view_params();
          view_centre_x = src->view_centre_x;
          view_centre_y = src->view_centre_y;
          view_centre_known = src->view_centre_known;
          focus_metanode = src->focus_metanode;
          rel_focus = src->rel_focus;
          src->view_centre_known = false;
          highlight_focus = src->highlight_focus;
          overlays_enabled = src->overlays_enabled;
          overlays_auto_flash = src->overlays_auto_flash;
          overlay_flashing_state = src->overlay_flashing_state;
          overlay_nominal_intensity = src->overlay_nominal_intensity;
          overlay_size_threshold = src->overlay_size_threshold;
          memcpy(overlay_params,src->overlay_params,
                 sizeof(kdu_uint32)*KDWS_NUM_OVERLAY_PARAMS);
          catalog_closed = true; // Prevents auto opening of duplicate catalog
          if (view_centre_known)
            {
              place_duplicate_window();          
              fit_scale_to_window = false;
            }
        }
      configure_quality_limiting();
      initialize_regions(true);
    }
  if ((filename_or_url != NULL) && (open_filename != NULL))
    { // Being called for the first time; set the title here
      char *title = new char[strlen(open_filename)+40];
      sprintf(title,"kdu_show %d: %s",window_identifier,open_filename);
      window->SetWindowText(kdws_string(title));
      delete[] title;
    }

  if ((duplication_src == NULL) && jpx_in.exists() &&
      kdws_catalog_new_data_available(jpx_in.access_meta_manager()))
    {
      window->create_metadata_catalog();
      refresh_metadata(get_current_time());
    }
  processing_call_to_open_file = false;
}

/*****************************************************************************/
/*                   kdws_renderer::invalidate_configuration                 */
/*****************************************************************************/

void
  kdws_renderer::invalidate_configuration()
{
  configuration_complete = false;
  max_components = -1; // Changed config may have different num image components
  working_buffer = NULL;
  if (compositor != NULL)
    compositor->remove_ilayer(kdu_ilayer_ref(),false);
}

/*****************************************************************************/
/*                      kdws_renderer::initialize_regions                    */
/*****************************************************************************/

void
  kdws_renderer::initialize_regions(bool perform_full_window_init)
{
  if (compositor == NULL)
    return;
  
  bool can_enlarge_window = fit_scale_to_window;

  // Reset the buffer and view dimensions to zero size.
  working_buffer = NULL;
  if (perform_full_window_init)
    view_dims = kdu_dims();
  
  while (!configuration_complete)
    { // Install configuration
      try {
        if (single_component_idx >= 0)
          { // Check for valid codestream index before opening
            if (jpx_in.exists())
              {
                int count=max_codestreams;
                if ((count < 0) && !jpx_in.count_codestreams(count))
                  { // Actual number not yet known, but have at least `count'
                    if (single_codestream_idx >= count)
                      { // Must have a dynamic cache
                        if ((jpip_client != NULL) && jpip_client->is_alive())
                          return; // Come back later once more data is in cache
                        else
                          { // JPIP client is down and cache is insufficient;
                            // need to go back to something that is available.
                            if (single_codestream_idx == 0)
                              return; // Cannot downgrade any further
                            single_codestream_idx--; // Walk backwards
                            continue; // Try again
                          }
                      }
                  }
                else
                  { // Number of codestreams is now known
                    max_codestreams = count;
                    if (single_codestream_idx >= max_codestreams)
                      single_codestream_idx = max_codestreams-1;
                  }
              }
            else if (mj2_in.exists())
              {
                kdu_uint32 trk;
                int frm, fld;
                if (!mj2_in.find_stream(single_codestream_idx,trk,frm,fld))
                  { // Must have a dynamic cache
                    if ((jpip_client != NULL) && jpip_client->is_alive())
                      return; // Come back later once more data is in cache
                    else
                      { // JPIP client is down and cache is insufficient;
                        // need to go back to something that is available
                        if (single_codestream_idx == 0)
                          return; // Cannot downgrade any further
                        single_codestream_idx--; // Walk backwards
                        continue; // Try again
                      }
                  }
                if (trk == 0)
                  { 
                    int count;
                    if (mj2_in.count_codestreams(count))
                      max_codestreams = count;
                    if (count == 0)
                      return; // Nothing more to try right now
                    single_codestream_idx = count-1;
                  }
              }
            else
              { // Raw codestream
                single_codestream_idx = 0;
              }
            if (!compositor->add_primitive_ilayer(single_codestream_idx,
                                              single_component_idx,
                                              KDU_WANT_CODESTREAM_COMPONENTS,
                                              kdu_dims(),kdu_dims()))
              { // Cannot open codestream yet; waiting for cache
                if (!respect_initial_jpip_request)
                  update_client_window_of_interest();
                if ((jpip_client != NULL) && jpip_client->is_alive())
                  return; // Wait for more data
                if (single_codestream_idx == 0)
                  return; // Cannot downgrade any further
                single_codestream_idx--; // Walk backwards
                continue; // Have another go with this one
              }
            
            kdu_istream_ref str;
            str = compositor->get_next_istream(str);
            kdu_codestream codestream = compositor->access_codestream(str);
            codestream.apply_input_restrictions(0,0,0,0,NULL);
            max_components = codestream.get_num_components();
            if (single_component_idx >= max_components)
              single_component_idx = max_components-1;
          }
        else if (single_layer_idx >= 0)
          { // Check for valid compositing layer index before opening
            if (jpx_in.exists())
              { 
                frame = jpx_frame(); // Just in case it was not reset
                int count=max_compositing_layer_idx+1;
                if ((count <= 0) && !jpx_in.count_compositing_layers(count))
                  { // Actual number not yet known, but have at least `count'
                    if (single_layer_idx >= count)
                      { // Must have a dynamic cache
                        if ((jpip_client != NULL) && jpip_client->is_alive())
                          return; // Come back later once more data is in cache
                        else
                          { // JPIP client is down and cache is insufficient;
                            // need to go back to something that is available
                            if (single_layer_idx == 0)
                              throw KDU_NULL_EXCEPTION; // Will downgrade mode
                            single_layer_idx--; // Walk backwards
                            continue; // Try again
                          }
                      }
                  }
                else
                  { // Number of compositing layers is now known
                    max_compositing_layer_idx = count-1;
                    if (single_layer_idx > max_compositing_layer_idx)
                      single_layer_idx = max_compositing_layer_idx;
                  }
              }
            else if (mj2_in.exists())
              { // MJ2 tracks are not identified by `single_layer_idx' 
                throw KDU_NULL_EXCEPTION; // Will downgrade mode
              }
            else
              { // Raw codestream
                single_layer_idx = 0;
              }
            if (!compositor->add_ilayer(single_layer_idx,
                                        kdu_dims(),kdu_dims()))
              { // Cannot open compositing layer yet; waiting for cache
                if (respect_initial_jpip_request)
                  { // See if request is compatible with opening a layer
                    if (!check_initial_request_compatibility())         
                      continue; // Mode changed to single component
                  }
                else
                  update_client_window_of_interest();
                if ((jpip_client != NULL) && jpip_client->is_alive())
                  return; // Wait for more data
                if (single_layer_idx == 0)
                  throw KDU_NULL_EXCEPTION; // Will downgrade mode
                single_layer_idx--; // Walk backwards
                continue; // Have another go with this one
              }
          }
        else
          { // Attempt to open frame/track
            if (jpx_in.exists())
              { 
                if (!composition_rules)
                  composition_rules = jpx_in.access_composition();
                if (!composition_rules)
                  { // Waiting on dynamic cache
                    if ((jpip_client != NULL) && jpip_client->is_alive())
                      return; // Come back later once cache has content
                    else
                      throw KDU_NULL_EXCEPTION; // Will downgrade mode
                  }
                if (!num_tracks_known)
                  num_tracks_known =
                  composition_rules.count_tracks(max_track_idx);
                if (max_track_idx == 0)
                  throw KDU_NULL_EXCEPTION; // Force downgrade
                if (track_idx > max_track_idx)
                  { 
                    track_idx = max_track_idx; num_frames = 0;
                    num_frames_known = false; frame = jpx_frame();
                  }
                else if (track_idx == 0)
                  track_idx = 1; // In case we are just starting out
                if ((!num_frames_known) && (num_frames == 0))
                  num_frames_known =
                  composition_rules.count_track_frames(track_idx,
                                                       num_frames);
                if ((num_frames == 0) && num_frames_known)
                  throw KDU_NULL_EXCEPTION; // Force downgrade
                if (!(change_frame_idx(frame_idx) &&
                      frame_expander.construct(&jpx_in,frame)))
                  { 
                    if (respect_initial_jpip_request)
                      { // See if request is compatible with opening a frame
                        if (!check_initial_request_compatibility())         
                          continue; // Mode changed to single layer/component
                      }
                    else
                      update_client_window_of_interest();
                    if ((jpip_client != NULL) && jpip_client->is_alive())
                      return; // Wait for more data
                    if (frame_idx == 0)
                      throw KDU_NULL_EXCEPTION; // Will downgrade mode
                    frame_idx--; // Walk backwards
                    frame = jpx_frame();
                    continue; // Have another go with this one
                  }
            
                if (frame_expander.get_num_members() == 0)
                  { // Requested frame does not exist
                    if (frame_idx == 0)
                      { kdu_error e; e << "Cannot render even the first "
                        "composited frame described in the JPX composition "
                        "box due to unavailability of the required "
                        "compositing layers in the original file.  Viewer "
                        "will fall back to single layer rendering mode."; }
                    frame_idx--;
                    frame = jpx_frame();
                    continue; // Loop around to try again
                  }
            
                compositor->set_frame(&frame_expander);
              }
            else if (mj2_in.exists())
              { 
                int track_type;
                bool loop_check = false;
                mj2_video_source *track = NULL;
                while (track == NULL)
                  {
                    track_type = mj2_in.get_track_type(track_idx);
                    if (track_type == MJ2_TRACK_IS_VIDEO)
                      { 
                        track = mj2_in.access_video_track(track_idx);
                        if (track_idx > max_track_idx)
                          max_track_idx = track_idx;
                        if (track == NULL)
                          { 
                            if ((jpip_client != NULL)&&jpip_client->is_alive())
                              return; // Wait for more data
                            else
                              throw KDU_NULL_EXCEPTION; // Will downgrade mode
                          }
                        num_frames = track->get_num_frames();
                        if (num_frames == 0)
                          { // Skip over track with no frames
                            track_idx=mj2_in.get_next_track(track_idx);
                            continue; // If we have looped, we will have 0 in
                                      // the `track_idx' which will trigger the
                                      // loop checking code below.
                          }
                        num_frames_known = true;
                      }
                    else if (track_type == MJ2_TRACK_NON_EXISTENT)
                      { // Go back to the first track, but we can also check
                        // whether this means that we now know how many
                        // tracks there are.
                        if (loop_check)
                          { kdu_error e; e << "Motion JPEG2000 source "
                          "has no video tracks with any frames!"; }
                        if (track_idx == (max_track_idx+1))
                          num_tracks_known = true;
                        loop_check = true;
                        track_idx = mj2_in.get_next_track(0);
                      }
                    else if (track_type == MJ2_TRACK_MAY_EXIST)
                      { 
                        if ((jpip_client != NULL)&&jpip_client->is_alive())
                          return; // Wait for more data
                        track_idx = mj2_in.get_next_track(track_idx);
                        continue; // May eventually trigger the loop checking
                                  // code above
                      }
                    else
                      { // Go to the next track
                        track_idx = mj2_in.get_next_track(track_idx);
                        continue; // May eventually trigger the loop checking
                                  // code above
                      }
                  }
                if (frame_idx >= num_frames)
                  frame_idx = num_frames-1;
                change_frame_idx(frame_idx);
                if (!compositor->add_ilayer((int)(track_idx-1),
                                            kdu_dims(),kdu_dims(),
                                            false,false,false,frame_idx,0))
                  { // Cannot open the frame yet
                    if ((jpip_client != NULL) && jpip_client->is_alive())
                      return; // Wait for more data
                    if (frame_idx == 0)
                      throw KDU_NULL_EXCEPTION; // Will downgrade mode
                    frame_idx--; // Walk backwards
                    continue; // Have another go with this one
                  }
              }
            else
              { // Raw codestream
                single_layer_idx = 0;
              }
          }

        if (fit_scale_to_window && respect_initial_jpip_request &&
            !check_initial_request_compatibility())
          { // Check we are opening the image in the intended manner
            compositor->remove_ilayer(kdu_ilayer_ref(),false);
            continue; // Open again in different mode
          }
        compositor->set_max_quality_layers(max_display_layers);
        compositor->cull_inactive_ilayers(3); // Really an arbitrary amount of
                                              // culling in this demo app.
        configuration_complete = true;
        min_rendering_scale=-1.0F; // Need to discover these
        max_rendering_scale=-1.0F; // from scratch each time
      }
      catch (kdu_exception exc) { // Try downgrading to a more primitive mode
          if ((single_component_idx >= 0) ||
              !(jpx_in.exists() || mj2_in.exists()))
            { // Already in single comp mode; nothing more primitive exists
              close_after_exception(exc);
              return;
            }
          else if (single_layer_idx >= 0)
            { // Downgrade from single layer mode to single component mode
              single_component_idx = 0;
              single_codestream_idx = 0;
              single_layer_idx = -1;
              update_client_window_of_interest();
            }
          else
            { // Downgrade from frame animation mode to single layer mode
              num_frames = 0;   num_frames_known = false;
              frame_idx = 0;    track_idx = 0;
              single_layer_idx = 0;
              frame = jpx_frame();
              frame_expander.reset();
              update_client_window_of_interest();
            }
        }
    }

  // Get size at current scale, possibly adjusting the scale if this is the
  // first time through
  float new_rendering_scale = rendering_scale;
  kdu_dims new_image_dims = image_dims;
  try {
      bool found_valid_scale=false;
      while (fit_scale_to_window || !found_valid_scale)
        {
          if ((min_rendering_scale > 0.0F) &&
              (new_rendering_scale < min_rendering_scale))
            new_rendering_scale = min_rendering_scale;
          if ((max_rendering_scale > 0.0F) &&
              (new_rendering_scale > max_rendering_scale))
            new_rendering_scale = max_rendering_scale;          
          compositor->set_scale(orientation.trans,orientation.vf,
                                orientation.hf,new_rendering_scale);
          found_valid_scale = 
            compositor->get_total_composition_dims(new_image_dims);
          if (!found_valid_scale)
            { // Increase scale systematically before trying again
              int invalid_scale_code = compositor->check_invalid_scale_code();
              if ((invalid_scale_code & KDU_COMPOSITOR_CANNOT_FLIP) &&
                  (orientation.hf || orientation.vf))
                {
                  kdu_warning w; w << "This image cannot be decompressed "
                    "with the requested geometry (horizontally or vertically "
                    "flipped), since it employs a packet wavelet transform "
                    "in which horizontally (resp. vertically) high-pass "
                    "subbands are further decomposed in the horizontal "
                    "(resp. vertical) direction.  Only transposed decoding "
                    "will be permitted.";
                  orientation.vf = orientation.hf = false;
                }
              else if ((invalid_scale_code & KDU_COMPOSITOR_SCALE_TOO_SMALL) &&
                       !(invalid_scale_code & KDU_COMPOSITOR_SCALE_TOO_LARGE))
                {
                  new_rendering_scale =
                    decrease_scale(new_rendering_scale,false);
                    // The above call will not actually decrease the scale; it
                    // will just find the smallest possible scale that can be
                    // supported.
                  assert((min_rendering_scale > 0.0F) &&
                         (new_rendering_scale == min_rendering_scale));
                  if (new_rendering_scale > 1000.0F)
                    {
                      if (single_component_idx >= 0)
                        { kdu_error e;
                          e << "Cannot display the image.  Seems to "
                          "require some non-trivial scaling.";
                        }
                      else
                        {
                          { kdu_warning w; w << "Cannot display the image.  "
                            "Seems to require some non-trivial scaling.  "
                            "Downgrading to single component mode.";
                          }
                          single_component_idx = 0;
                          invalidate_configuration();
                          if (!compositor->add_primitive_ilayer(0,
                                                single_component_idx,
                                                KDU_WANT_CODESTREAM_COMPONENTS,
                                                kdu_dims(),kdu_dims()))
                            return;
                          configuration_complete = true;
                        }
                    }
                }
              else if ((invalid_scale_code & KDU_COMPOSITOR_SCALE_TOO_LARGE) &&
                       !(invalid_scale_code & KDU_COMPOSITOR_SCALE_TOO_SMALL))
                {
                  new_rendering_scale =
                    increase_scale(new_rendering_scale,false);
                    // The above call will not actually increase the scale; it
                    // will just find the maximum possible scale that can be
                    // supported.
                  assert(new_rendering_scale == max_rendering_scale);
                }
              else if (!(invalid_scale_code & KDU_COMPOSITOR_TRY_SCALE_AGAIN))
                {
                  if (single_component_idx >= 0)
                    { kdu_error e;
                      e << "Cannot display the image.  Unexplicable error "
                        "code encountered in call to "
                        "`kdu_region_compositor::get_total_composition_dims'.";
                    }
                  else
                    {
                      { kdu_warning w; w << "Cannot display the image.  "
                        "Seems to require some incompatible set of geometric "
                        "manipulations for the various composed codestreams.";
                      }
                      single_component_idx = 0;
                      invalidate_configuration();
                      if (!compositor->add_primitive_ilayer(0,
                                                single_component_idx,
                                                KDU_WANT_CODESTREAM_COMPONENTS,
                                                kdu_dims(),kdu_dims()))
                        return;
                      configuration_complete = true;
                    }
                }
            }
          else if (fit_scale_to_window)
            {
              kdu_coords max_tgt_size =
                kdu_coords(1000/pixel_scale,1000/pixel_scale);
              if (respect_initial_jpip_request &&
                  jpip_client->get_window_in_progress(&tmp_roi,0,
                                                      NULL,NULL,true) &&
                  (tmp_roi.resolution.x > 0) && (tmp_roi.resolution.y > 0))
                { // Roughly fit scale to match the resolution
                  max_tgt_size = tmp_roi.resolution;
                  max_tgt_size.x += (3*max_tgt_size.x)/4;
                  max_tgt_size.y += (3*max_tgt_size.y)/4;
                  if (!tmp_roi.region.is_empty())
                    { // Configure view and focus box based on one-time request
                      rel_focus.reset();
                      rel_focus.region.set(tmp_roi.region,tmp_roi.resolution);
                      if (orientation.trans)
                        rel_focus.region.transpose();
                      if (orientation.hf)
                        rel_focus.region.hflip();
                      if (orientation.vf)
                        rel_focus.region.vflip();
                      if (single_component_idx >= 0)
                        { rel_focus.codestream_idx = single_codestream_idx;
                          rel_focus.codestream_region = rel_focus.region; }
                      else if (jpx_in.exists() && (single_layer_idx >= 0))
                        { rel_focus.layer_idx = single_layer_idx;
                          rel_focus.layer_region = rel_focus.region; }
                      else
                        { rel_focus.frame_idx = frame_idx;
                          rel_focus.frame = frame; }
                      rel_focus.get_centre_if_valid(view_centre_x,
                                                    view_centre_y);
                      view_centre_known = true;
                      if (!rel_focus.region.is_partial())
                        rel_focus.reset();
                    }
                }
              float max_x = new_rendering_scale *
                ((float) max_tgt_size.x) / ((float) new_image_dims.size.x);
              float max_y = new_rendering_scale *
                ((float) max_tgt_size.y) / ((float) new_image_dims.size.y);
              while (((min_rendering_scale < 0.0) ||
                      (new_rendering_scale > min_rendering_scale)) &&
                     ((new_rendering_scale > max_x) ||
                      (new_rendering_scale > max_y)))
                {
                  new_rendering_scale =
                    decrease_scale(new_rendering_scale,false);
                  found_valid_scale = false;
                }
              kdu_dims region_basis = find_focus_box();
              new_rendering_scale =
                compositor->find_optimal_scale(region_basis,
                                               new_rendering_scale,
                                               new_rendering_scale,
                                               new_rendering_scale);              
              fit_scale_to_window = false;
            }
        }
    }
  catch (kdu_exception exc) { // Some error occurred while parsing code-streams
      close_after_exception(exc);
      return;
    }
  catch (std::bad_alloc &) {
    close_after_exception(KDU_MEMORY_EXCEPTION);
    kdws_string text_string("Insufficient memory -- you could improve "
                            "upon the current implementation to invoke "
                            "`kdu_region_compositor::cull_inactive_layers' "
                            "or take other memory-saving action in response "
                            "to this condition.");
    kdws_string caption("kdu_show -- memory error");
    window->MessageBox(text_string,caption,MB_OK);
    return;
  }

  if (animator != NULL)
    { // See if we need to disable the animator
      bool want_single_layer_mode=false, have_single_layer_mode=false;
      if (jpx_in.exists())
        { 
          want_single_layer_mode = (single_layer_idx >= 0);
          have_single_layer_mode = animator->get_single_layer_mode();
        }
      if ((single_component_idx >= 0) ||
          (want_single_layer_mode && !have_single_layer_mode) ||
          (have_single_layer_mode && !want_single_layer_mode))
        stop_animation();
      animation_frame_started = false;
      animation_frame_complete = false; // Prevent stalling of the animation
          // machinery in case `advance_animation' was not invoked for refresh.
    }

  // Configure animation bar, as appropriate
  if ((single_component_idx < 0) &&
      ((mj2_in.exists() && ((num_frames != 1) || !num_frames_known)) ||
       (jpx_in.exists() &&
        (((single_layer_idx<0) && ((num_frames != 1) || !num_frames_known)) ||
         ((single_layer_idx>=0) && (max_compositing_layer_idx != 0))))))
    { // Put up the animation bar
      metamation_bar = NULL;
      if (animation_bar == NULL)
        animation_bar = window->get_animation_bar();
      update_animation_bar_state(); 
    }
  else if (animation_bar != NULL)
    { 
      window->remove_animation_bar();
      animation_bar = NULL;
    }

  // Install the dimensioning parameters we just found, checking to see
  // if the window needs to be resized.
  rendering_scale = new_rendering_scale;
  if ((image_dims == new_image_dims) && (!view_dims.is_empty()) &&
      (!working_buffer_dims.is_empty()))
    { // No need to resize the window
      kdu_region_animator_roi animator_roi;
      if (animator != NULL)
        { 
          kdu_dims vport = view_dims;
          animator->get_roi_and_mod_viewport(compositor,animator_roi,vport);
          vport = size_buffer_for_target_view(vport,false,false);
          compositor->set_buffer_surface(vport);
        }
      working_buffer =
        compositor->get_composition_buffer_ex(working_buffer_dims,true);
      if (working_buffer != NULL)
        { 
          working_buffer->set_image_dims(image_dims,orientation);
          working_buffer->set_roi_info(animator_roi);
        }
      bool focus_needs_update = adjust_rel_focus();
      if (focus_needs_update &&
          rel_focus.get_centre_if_valid(view_centre_x,view_centre_y) &&
          (animator == NULL))
        view_centre_known = true;
      if (view_centre_known)
        scroll_to_view_anchors();
      if (focus_needs_update)
        update_focus_box();
      compositor->record_viewport(orientation,pixel_scale,view_dims);
    }
  else
    { // Need to size the window, possibly for the first time.
      view_dims = working_buffer_dims = kdu_dims();
      image_dims = new_image_dims;
      if ((duplication_src == NULL) && adjust_rel_focus() &&
          (animator == NULL) &&
          rel_focus.get_centre_if_valid(view_centre_x,view_centre_y))
        view_centre_known = true;
      image_view->set_max_view_size(image_dims.size,pixel_scale,
                                    can_enlarge_window);
      if ((compositor != NULL) && !view_dims.is_empty())
        compositor->invalidate_rect(view_dims);
    }

  if (animator == NULL)
    {
      if (!respect_initial_jpip_request)
        update_client_window_of_interest();
      if (overlays_enabled)
        { 
          jpx_metanode node;
          if ((catalog_source != NULL) &&
              (node=catalog_source->get_selected_metanode()).exists())
            highlight_imagery_for_metanode(node);
          else
            overlay_highlight_state = 0;
          if (overlay_flashing_state && !overlay_highlight_state)
            { 
              overlay_flashing_state = 1;
              configure_overlays(-1.0,false);
            }
        }
      display_status();
      update_animation_bar_pos();
    }
  view_centre_known = false;
  perform_any_outstanding_render_refresh();
}

/*****************************************************************************/
/*                 kdws_renderer::configure_quality_limiting                 */
/*****************************************************************************/

void kdws_renderer::configure_quality_limiting()
{ 
  if (compositor == NULL) return;
  if (quality_limiting_mode == 0)
    compositor->set_quality_limiting(NULL,-1.0f,-1.0f);
  else
    { 
      kdu_quality_limiter limiter(1.0f/256.0f,(quality_limiting_mode<2));
      float assumed_ppi = 200.0f / pixel_scale;
      compositor->set_quality_limiting(&limiter,assumed_ppi,assumed_ppi);
    }
}

/*****************************************************************************/
/*                     kdws_renderer::configure_overlays                     */
/*****************************************************************************/

void kdws_renderer::configure_overlays(double cur_time, bool in_wakeup)
{
  if (compositor == NULL)
    { 
      next_overlay_change_time = -1.0; // Just in case
      return;
    }
  if (animator != NULL)
    { 
      assert(!in_wakeup); // Should not get called from `wakeup'
      if ((cur_time = animator->get_display_time()) < 0.0)
        return; // No animation frame activated yet
    }

  if (!overlays_enabled)
    { // disable scheduling state machinery -- just in case
      overlay_flashing_state = overlay_highlight_state = 0;
      next_overlay_change_time = -1.0;
    }
  else if (cur_time < 0.0)
    next_overlay_change_time = -1.0;

  while ((next_overlay_change_time >= 0) &&
         (next_overlay_change_time <= cur_time))
    { 
      if (overlay_highlight_state)
        { 
          overlay_highlight_state++;
          if (overlay_highlight_state > KDWS_OVERLAY_HIGHLIGHT_STATES)
            overlay_highlight_state = 0;
        }
      else if (overlay_flashing_state)
        { 
          overlay_flashing_state++;
          if (overlay_flashing_state > KDWS_OVERLAY_FLASHING_STATES)
            overlay_flashing_state = 1;
        }
      if (overlay_highlight_state)
        next_overlay_change_time +=
          kdws_overlay_highlight_dwell[overlay_highlight_state-1];
      else if (overlay_flashing_state)
        next_overlay_change_time +=
          kdws_overlay_flash_dwell[overlay_flashing_state-1];
      else
        next_overlay_change_time = -1.0;
    }
  
  float intensity = overlay_nominal_intensity;
  jpx_metanode restriction = overlay_restriction;
  if (animator != NULL)
    { 
      restriction = animation_metanode;
      jpx_metanode_link_type link_type;
      jpx_metanode link_target = restriction.get_link(link_type);
      if (link_target.exists())
        restriction = link_target;
    }      
  if (overlay_highlight_state)
    { 
      assert(animator == NULL);
      intensity *= kdws_overlay_highlight_factor[overlay_highlight_state-1];
      restriction = highlight_metanode;
      if (next_overlay_change_time < 0.0)
        { 
          if (cur_time < 0.0)
            cur_time = get_current_time();
          next_overlay_change_time = cur_time +
            kdws_overlay_highlight_dwell[overlay_highlight_state-1];
        }
    }
  else if (overlay_flashing_state)
    { 
      intensity *= kdws_overlay_flash_factor[overlay_flashing_state-1];
      if (next_overlay_change_time < 0.0)
        { 
          if (cur_time < 0.0)
            cur_time = get_current_time();
          next_overlay_change_time = cur_time +
            kdws_overlay_flash_dwell[overlay_flashing_state-1];
        }
    }

  if ((!in_wakeup) || compositor->is_processing_complete())
    { // Don't change the overlay configuration while we are still processing
      // something.  Wait until finished -- by then, the overlay state may
      // have changed to yet another configuration if processing load is
      // heavy.  Make sure we call the present function again when processing
      // actually does complete.
      compositor->configure_overlays(overlays_enabled,overlay_size_threshold,
                                     intensity,KDWS_OVERLAY_BORDER,
                                     restriction,0,overlay_params,
                                     KDWS_NUM_OVERLAY_PARAMS);
      perform_any_outstanding_render_refresh(cur_time);
    }
  if (next_overlay_change_time >= 0.0)
    { // See if we really do need to modulate overlays or not -- may be able
      // to save some computation if it turns out that there is no overlay
      // metadata to render right now.
      int total_roi_nodes=0, hidden_roi_nodes=0;
      compositor->get_overlay_info(total_roi_nodes,hidden_roi_nodes);
      if (total_roi_nodes == 0)
        next_overlay_change_time = -1.0;
    }
  
  if ((next_overlay_change_time >= 0.0) && !in_wakeup)
    {  // If invoked from `wakeup' the following steps happen there
      if (animator == NULL)
        install_next_scheduled_wakeup();
      else
        { 
          double guard_seconds = animator->calculate_cpu_allowance();
          animator->insert_conditional_frame_step(next_overlay_change_time,
                                                  guard_seconds);
        }
    }
}

/*****************************************************************************/
/*                    kdws_renderer::place_duplicate_window                  */
/*****************************************************************************/

void kdws_renderer::place_duplicate_window()
{
  assert(view_centre_known);
  kdu_coords view_size = duplication_src->view_dims.size;
  if (rel_focus.get_centre_if_valid(view_centre_x,view_centre_y))
    {
      kdu_coords max_view_size = duplication_src->find_focus_box().size;
      max_view_size.x += max_view_size.x / 3;
      max_view_size.y += max_view_size.y / 3;
      if (view_size.x > max_view_size.x)
        view_size.x = max_view_size.x;
      if (view_size.y > max_view_size.y)
        view_size.y = max_view_size.y;
    }

  // Now we need to estimate the frame margins so that we can size and
  // place the window appropriately.  Easiest way to do this is to use
  // the margins of the window being duplicated, being careful not
  // to count the contribution of any catalog.  Unfortunately, this method
  // cannot account for the possible impact of extra menu rows which might
  // be added if the window is too narrow.  To fix the problem, we'll adjust
  // the size in two phases: horizontal first and then vertical.
  CRect window_frame; duplication_src->window->GetWindowRect(&window_frame);
  CRect window_client; duplication_src->window->GetClientRect(&window_client);
  CRect view_frame; duplication_src->image_view->GetWindowRect(&view_frame);
  CRect view_client; duplication_src->image_view->GetClientRect(&view_client);
  kdu_coords margins;
  margins.x = window_frame.Width() - window_client.Width();
  margins.y = window_frame.Height() - view_frame.Height();
  margins.x += view_frame.Width() - view_client.Width();
  margins.y += view_frame.Height() - view_client.Height();

  kdu_coords target_frame_size;
  target_frame_size.x = view_size.x * pixel_scale + margins.x;
  target_frame_size.y = window_frame.Height();
  window->SetWindowPos(NULL,0,0,target_frame_size.x,target_frame_size.y,
                       SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOMOVE);
  window->GetWindowRect(&window_frame);
  window->GetClientRect(&window_client);
  image_view->GetWindowRect(&view_frame);
  image_view->GetClientRect(&view_client);
  margins.x = window_frame.Width() - window_client.Width();
  margins.y = window_frame.Height() - view_frame.Height();
  margins.x += view_frame.Width() - view_client.Width();
  margins.y += view_frame.Height() - view_client.Height();
  target_frame_size.x = view_size.x * pixel_scale + margins.x;
  target_frame_size.y = view_size.y * pixel_scale + margins.y;
  manager->place_window(window,target_frame_size);
}

/*****************************************************************************/
/*                 kdws_renderer::size_buffer_for_target_view                */
/*****************************************************************************/

kdu_dims
  kdws_renderer::size_buffer_for_target_view(kdu_dims min_buffer_dims,
                                             bool maximize_consistency,
                                             bool assume_scroll_pan)
{
  min_buffer_dims &= image_dims;
  if (min_buffer_dims.is_empty())
    return min_buffer_dims;
  
  // Create a border, so as to minimize impact of scrolling 
  kdu_coords static_border; // Border for static images, as opposed to video
  static_border.x = (min_buffer_dims.size.x >> 5) + 50;
  static_border.y = (min_buffer_dims.size.y >> 5) + 50;
  kdu_coords border = static_border;
  if (animator != NULL)
    { 
      if (assume_scroll_pan)
        { // Update animation borders to something a little over what the
          // static border would suggest so that the border will remain clipped
          // at the static borders for some time even after the animation
          // borders start to decay away with time.
          animation_border_x = 1.5F*static_border.x;
          animation_border_y = 1.5F*static_border.y;
        }
      else
        { 
          if (animation_border_x > (1.5F*static_border.x))
            animation_border_x = 1.5F*static_border.x;
          if (animation_border_y > (1.5F*static_border.y))
            animation_border_y = 1.5F*static_border.y;
          border.x = (int) animation_border_x;
          border.y = (int) animation_border_y;
          if (border.x > static_border.x) border.x = static_border.x;
          if (border.y > static_border.y) border.y = static_border.y;
        }
    }

  kdu_dims result_dims;
  result_dims.pos = min_buffer_dims.pos - border;
  result_dims.size = min_buffer_dims.size + border + border;
  if (result_dims.size.x > image_dims.size.x)
    result_dims.size.x = image_dims.size.x;
  if (result_dims.size.y > image_dims.size.y)
    result_dims.size.y = image_dims.size.y;

  if (maximize_consistency && !working_buffer_dims.is_empty())
    { 
      kdu_dims alt_result_dims = result_dims;
      alt_result_dims.pos = working_buffer_dims.pos;
  
      // Make sure we still fall within the boundaries of the image
      kdu_coords buffer_lim = alt_result_dims.pos + alt_result_dims.size;
      kdu_coords image_lim = image_dims.pos + image_dims.size;
      if (buffer_lim.x > image_lim.x)
        alt_result_dims.pos.x -= buffer_lim.x-image_lim.x;
      if (buffer_lim.y > image_lim.y)
        alt_result_dims.pos.y -= buffer_lim.y-image_lim.y;
      if (alt_result_dims.pos.x < image_dims.pos.x)
        alt_result_dims.pos.x = image_dims.pos.x;
      if (alt_result_dims.pos.y < image_dims.pos.y)
        alt_result_dims.pos.y = image_dims.pos.y;
      if (alt_result_dims.contains(min_buffer_dims) &&
          working_buffer_dims.contains(alt_result_dims))
        return alt_result_dims; // Otherwise, we need to generate some new data
                                // so we might as well optimize the dimensions
    }

  if (result_dims.pos.x < image_dims.pos.x)
    result_dims.pos.x = image_dims.pos.x;
  if (result_dims.pos.y < image_dims.pos.y)
    result_dims.pos.y = image_dims.pos.y;
  kdu_coords buffer_lim = result_dims.pos + result_dims.size;
  kdu_coords image_lim = image_dims.pos + image_dims.size;
  if (buffer_lim.x > image_lim.x)
    result_dims.pos.x -= buffer_lim.x - image_lim.x;
  if (buffer_lim.y > image_lim.y)
    result_dims.pos.y -= buffer_lim.y - image_lim.y;
  assert(min_buffer_dims == (min_buffer_dims & result_dims));
  assert(result_dims == (image_dims & result_dims));
  return result_dims;
}

/*****************************************************************************/
/*                     kdws_renderer::view_dims_changed                      */
/*****************************************************************************/

bool kdws_renderer::view_dims_changed(kdu_dims new_view_dims)
{
  kdu_dims new_buffer_dims;
  kdu_region_animator_roi animator_roi;
  bool animator_refreshed=false;
  while (true)
    { // We repeat at most once if the animator gets refreshed
      if (compositor == NULL)
        return false;
      
      new_view_dims &= image_dims;
      if ((new_view_dims == view_dims) &&
          working_buffer_dims.contains(view_dims) &&
          !view_dims_change_deferred)
        { // No change
          update_focus_box();
          return false;
        }
      
      kdu_dims min_buffer_dims = new_view_dims;
      bool assume_scroll_pan =
        (view_dims != new_view_dims) && !view_dims.is_empty();
      // Expect we are doing resize/pan, because other operations like
      // rotate/zoom all zero out `view_dims' in their call to
      // `initialize_regions'.
      if (animator != NULL)
        animator->get_roi_and_mod_viewport(compositor,animator_roi,
                                           min_buffer_dims);
      new_buffer_dims =
        size_buffer_for_target_view(min_buffer_dims,(animator==NULL),
                                    assume_scroll_pan);
      
      if ((animator != NULL) &&
          !(animator_refreshed || animation_advancing))
        {           
          if (working_buffer_dims.contains(min_buffer_dims))
            { // The current working buffer is suitable, if not optimal, so
              // there is no need for a costly animation refresh call.
              if (animation_frame_started)
                { // Defer all changes until we have finished rendering
                  // this animation frame.
                  view_dims_change_deferred = true;
                  new_buffer_dims = working_buffer_dims;
                }
            }
          else
            { // Backtrack to refresh surface, then go around again.
              advance_animation(-1.0,-1.0,-1.0);
              animator_refreshed = true;
              continue; // Go around one more time
            }
        }
      break; // Make sure we do not loop
    }
  
  bool need_redraw = (new_view_dims != view_dims);
  view_dims = new_view_dims;
  if (new_buffer_dims != working_buffer_dims)
    { // Set surface and get new buffer
      working_buffer = NULL;
      compositor->set_buffer_surface(new_buffer_dims);
      working_buffer =
        compositor->get_composition_buffer_ex(working_buffer_dims,true);
      if (working_buffer != NULL)
        { 
          working_buffer->set_image_dims(image_dims,orientation);
          working_buffer->set_roi_info(animator_roi);
        }
    }
  compositor->record_viewport(orientation,pixel_scale,view_dims);
  update_focus_box(true);
  if (need_redraw && (animator != NULL))
    compositor->force_display_redraw();
  return true;
}

/*****************************************************************************/
/*                        kdws_renderer::set_view_size                       */
/*****************************************************************************/

void
  kdws_renderer::set_view_size(kdu_coords size)
{
  if ((compositor == NULL) || !configuration_complete)
    return;
  if (image_view != NULL)
    image_view->cancel_focus_drag();

  // Set view region to the largest subset of the image region consistent with
  // the size of the new viewing region.
  kdu_dims new_view_dims = view_dims;
  new_view_dims.size = size;
  if (view_centre_known)
    { // Note: we leave `view_centre_known' true until about to
      // return, because we may have to come back here during
      // an animation refresh.
      new_view_dims.pos.x = image_dims.pos.x - (size.x / 2) +
        (int) floor(0.5 + image_dims.size.x*view_centre_x);
      new_view_dims.pos.y = image_dims.pos.y - (size.y / 2) +
        (int) floor(0.5 + image_dims.size.y*view_centre_y);
    }
  if (new_view_dims.pos.x < image_dims.pos.x)
    new_view_dims.pos.x = image_dims.pos.x;
  if (new_view_dims.pos.y < image_dims.pos.y)
    new_view_dims.pos.y = image_dims.pos.y;
  kdu_coords view_lim = new_view_dims.pos + new_view_dims.size;
  kdu_coords image_lim = image_dims.pos + image_dims.size;
  if (view_lim.x > image_lim.x)
    new_view_dims.pos.x -= view_lim.x-image_lim.x;
  if (view_lim.y > image_lim.y)
    new_view_dims.pos.y -= view_lim.y-image_lim.y;
  new_view_dims &= image_dims;

  if (!frame_presenter->resize(image_view->m_hWnd))
    stop_animation();
  
  if (view_dims_changed(new_view_dims))
    { 
      image_view->configure_scrollers(view_dims,image_dims);
      if (animator == NULL)        // Redraw for animation is handled in the
        image_view->Invalidate();  // call to `view_dims_changed' above.
    }
  view_centre_known = false;
}

/*****************************************************************************/
/*                       kdws_renderer::set_hscroll_pos                      */
/*****************************************************************************/

void
  kdws_renderer::set_hscroll_pos(int pos, bool relative_to_last)
{
  if ((compositor == NULL) || (working_buffer == NULL))
    return;
  if (image_view != NULL)
    image_view->cancel_focus_drag();

  view_centre_known = false;
  if (relative_to_last)
    pos += view_dims.pos.x;
  else
    pos += image_dims.pos.x;
  if (pos < image_dims.pos.x)
    pos = image_dims.pos.x;
  if ((pos+view_dims.size.x) > (image_dims.pos.x+image_dims.size.x))
    pos = image_dims.pos.x+image_dims.size.x-view_dims.size.x;
  if (pos == view_dims.pos.x)
    return;

  RECT update;
  if (animator == NULL)
    image_view->ScrollWindowEx((view_dims.pos.x-pos)*pixel_scale,0,
                               NULL,NULL,NULL,&update,0);
  kdu_dims new_view_dims = view_dims;
  new_view_dims.pos.x = pos;
  if (view_dims_changed(new_view_dims))
    image_view->configure_scrollers(view_dims,image_dims);
  view_dims_changed(new_view_dims);
  image_view->configure_scrollers(view_dims,image_dims);
  if (animator == NULL)
    { // Update whatever region has been exposed
      if (shape_editor == NULL)
        paint_region(update,NULL); // Paint directly
      else // Need window machinery to draw editor outlines
        image_view->InvalidateRect(&update);
    }
}

/*****************************************************************************/
/*                       kdws_renderer::set_vscroll_pos                      */
/*****************************************************************************/

void
  kdws_renderer::set_vscroll_pos(int pos, bool relative_to_last)
{
  if ((compositor == NULL) || (working_buffer == NULL))
    return;
  if (image_view != NULL)
    image_view->cancel_focus_drag();

  view_centre_known = false;
  if (relative_to_last)
    pos += view_dims.pos.y;
  else
    pos += image_dims.pos.y;
  if (pos < image_dims.pos.y)
    pos = image_dims.pos.y;
  if ((pos+view_dims.size.y) > (image_dims.pos.y+image_dims.size.y))
    pos = image_dims.pos.y+image_dims.size.y-view_dims.size.y;
  if (pos == view_dims.pos.y)
    return;
  
  RECT update;
  if (animator == NULL)
    image_view->ScrollWindowEx(0,(view_dims.pos.y-pos)*pixel_scale,
                               NULL,NULL,NULL,&update,0);
  kdu_dims new_view_dims = view_dims;
  new_view_dims.pos.y = pos;
  view_dims_changed(new_view_dims);
  image_view->configure_scrollers(view_dims,image_dims);
  if (animator == NULL)
    { // Update whatever region has been exposed
      if (shape_editor == NULL)
        paint_region(update,NULL); // Paint directly
      else // Need window machinery to draw editor outlines
        image_view->InvalidateRect(&update);
    }
}

/*****************************************************************************/
/*                       kdws_renderer::set_scroll_pos                       */
/*****************************************************************************/

void
  kdws_renderer::set_scroll_pos(int pos_x, int pos_y, bool relative_to_last)
{
  if ((compositor == NULL) || (working_buffer == NULL))
    return;
  view_centre_known = false;
  if (relative_to_last)
    {
      pos_y += view_dims.pos.y;
      pos_x += view_dims.pos.x;
    }
  else
    {
      pos_y += image_dims.pos.y;
      pos_x += image_dims.pos.x;
    }
  if (pos_y < image_dims.pos.y)
    pos_y = image_dims.pos.y;
  if ((pos_y+view_dims.size.y) > (image_dims.pos.y+image_dims.size.y))
    pos_y = image_dims.pos.y+image_dims.size.y-view_dims.size.y;
  if (pos_x < image_dims.pos.x)
    pos_x = image_dims.pos.x;
  if ((pos_x+view_dims.size.x) > (image_dims.pos.x+image_dims.size.x))
    pos_x = image_dims.pos.x+image_dims.size.x-view_dims.size.x;
  if ((pos_x == view_dims.pos.x) && (pos_y == view_dims.pos.y))
    return;

  char string[30]; sprintf(string,"Scrolling to %d,%d\n",pos_x,pos_y);
  OutputDebugStringA(string);

  RECT update;
  if (animator == NULL)
    image_view->ScrollWindowEx((view_dims.pos.x-pos_x)*pixel_scale,
                               (view_dims.pos.y-pos_y)*pixel_scale,
                               NULL,NULL,NULL,&update,0);
  kdu_dims new_view_dims = view_dims;
  new_view_dims.pos.x = pos_x;
  new_view_dims.pos.y = pos_y;
  view_dims_changed(new_view_dims);
  image_view->configure_scrollers(view_dims,image_dims);
  if (animator == NULL)
    { 
      if (shape_editor == NULL)
        paint_region(update,NULL); // Paint directly
      else // Need window machinery to draw editor outlines
        image_view->InvalidateRect(&update);
    }
}

/*****************************************************************************/
/*             kdws_renderer::convert_region_from_display_view               */
/*****************************************************************************/

kdu_dims
  kdws_renderer::convert_region_from_display_view(RECT rect, double scale,
                                                  kdu_dims ref_viewport)
{
  scale = 1.0 / scale;
  kdu_coords region_min, region_lim;
  region_min.x = (int) kdu_round(rect.left * scale - 0.4);
  region_lim.x = (int) kdu_round(rect.right * scale + 0.4);
  region_min.y = (int) kdu_round(rect.top * scale - 0.4);
  region_lim.y = (int) kdu_round(rect.bottom * scale + 0.4);
  kdu_dims region;
  region.pos = region_min + ref_viewport.pos;
  region.size = region_lim - region_min;
  return region;
}

/*****************************************************************************/
/*              kdws_renderer::convert_region_to_display_view                */
/*****************************************************************************/

RECT
  kdws_renderer::convert_region_to_display_view(kdu_dims region, double scale,
                                                kdu_dims ref_viewport)
{
  kdu_coords rel_pos = region.pos - ref_viewport.pos;
  RECT rect;
  rect.left = (int) kdu_round(rel_pos.x * scale);
  rect.right = (int) kdu_round((rel_pos.x+region.size.x) * scale);
  rect.top = (int) kdu_round(rel_pos.y * scale);
  rect.bottom = (int) kdu_round((rel_pos.y+region.size.y) * scale);
  return rect;
}

/*****************************************************************************/
/*              kdws_renderer::convert_point_from_display_view               */
/*****************************************************************************/

kdu_coords
  kdws_renderer::convert_point_from_display_view(POINT point)
{
  double scale = 1.0 / pixel_scale;
  kdu_coords pos;
  pos.x = (int) kdu_round(point.x * scale);
  pos.y = (int) kdu_round(point.y * scale);
  return pos + view_dims.pos;
}

/*****************************************************************************/
/*               kdws_renderer::convert_point_to_display_view                */
/*****************************************************************************/

POINT
  kdws_renderer::convert_point_to_display_view(kdu_coords point)
{
  POINT pt;
  pt.x = (point.x - view_dims.pos.x) * pixel_scale;
  pt.y = (point.y - view_dims.pos.y) * pixel_scale;
  return pt;
}

/*****************************************************************************/
/*                      kdws_renderer::resize_stripmap                       */
/*****************************************************************************/

void
kdws_renderer::resize_stripmap(int min_width)
{
  if (min_width <= strip_extent.x)
    return;
  strip_extent.x = min_width + 128;
  strip_extent.y = 512;
  memset(&stripmap_info,0,sizeof(stripmap_info));
  stripmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  stripmap_info.bmiHeader.biWidth = strip_extent.x;
  stripmap_info.bmiHeader.biHeight = -strip_extent.y;
  stripmap_info.bmiHeader.biPlanes = 1;
  stripmap_info.bmiHeader.biBitCount = 32;
  stripmap_info.bmiHeader.biCompression = BI_RGB;
  if (stripmap != NULL)
    DeleteObject(stripmap);
  stripmap = CreateDIBSection(NULL,&stripmap_info,DIB_RGB_COLORS,
                              (void **) &stripmap_buffer,NULL,0);
}

/*****************************************************************************/
/*                        kdws_renderer::paint_region                        */
/*****************************************************************************/

void
  kdws_renderer::paint_region(RECT region, CDC *supplied_dc)
{
  CDC *dc = supplied_dc;
  if (dc == NULL)
    dc = image_view->GetDC();
  int save_dc_state = 0;
  if (supplied_dc != NULL)
    save_dc_state = supplied_dc->SaveDC();

  kdws_orientation buf_orientation;
  kdu_dims buf_dims, buf_image_dims;
  kdws_compositor_buf *buffer = NULL;
  if (compositor != NULL)
    buffer = compositor->get_composition_buffer_ex(buf_dims,false);
    /* Note: the above function always retrieves the head of the composition
       buffer queue, if there is one; otherwise it retrieves the current
       working buffer.  If this function is invoked from the main application
       thread, as a result of user interaction, it is possible that the
       frame presentation thread will actually remove the head of the
       composition buffer queue while we are using it here.  However, this
       is of no concern, since popping a buffer from the queue does not
       delete or even recycle its storage until a subsequent call to
       `push_composition_buffer', which can only arrive from within the
       main application thread.  Moreover, if the above situation does occur,
       the frame presentation thread will be waiting to lock focus on the
       window, which will happen after the user interaction-inspired painting
       completes, at which time a more up-to-date version of the view will
       be painted in full. */
  if ((buffer == NULL) || buf_dims.is_empty() ||
      (!buffer->get_image_dims(buf_image_dims,buf_orientation)) ||
      buf_image_dims.is_empty())
    {
      dc->BitBlt(region.left,region.top,
                 region.right-region.left,region.bottom-region.top,
                 NULL,0,0,WHITENESS);
      if (supplied_dc == NULL)
        image_view->ReleaseDC(dc);
      else if (save_dc_state != 0)
        supplied_dc->RestoreDC(save_dc_state);
      return;
    }

  // Obtain information required for all painting operations below
  kdws_display_geometry display_geometry;
  compositor->get_display_geometry(display_geometry);
  if (display_geometry.orientation != buf_orientation)
    { // Incompatible scaling factors; major view changes occurred; don't
      // paint at this point, situation is transient.
      if (supplied_dc == NULL)
        image_view->ReleaseDC(dc);
      else if (save_dc_state != 0)
        supplied_dc->RestoreDC(save_dc_state);
      return;
    }

  kdu_dims buf_viewport; // Will be visible region, expressed in buf coords
  buf_viewport = display_geometry.viewport_dims;
  buf_viewport.pos -= display_geometry.image_dims.pos; // Make coords relative
  double scale=1.0;
  if (display_geometry.image_dims.size != buf_image_dims.size)
    { 
      scale = sqrt(((double) display_geometry.image_dims.area()) /
                   ((double) buf_image_dims.area()));
      double inv_scale = 1.0 / scale;
      kdu_coords lim = buf_viewport.pos + buf_viewport.size;
      buf_viewport.pos.x = (int) kdu_round(buf_viewport.pos.x*inv_scale-0.4);
      buf_viewport.pos.y = (int) kdu_round(buf_viewport.pos.y*inv_scale-0.4);
      lim.x = (int) kdu_round(lim.x * inv_scale + 0.4);
      lim.y = (int) kdu_round(lim.y * inv_scale + 0.4);
      buf_viewport.size = lim - buf_viewport.pos;
    }
  buf_viewport.pos += buf_image_dims.pos; // Make coords absolute
  buf_viewport &= buf_image_dims;

  kdu_dims buf_region; // Will be region to paint, expressed in buf coords
  RECT display_region; // Will be region to paint, expressed in window coords
  double pscale = scale * display_geometry.pixel_scale;

  // The region to paint, expressed in window coords, has been given.  We
  // can convert this region back to buf coordinates by using `pscale'
  // and recognizing that the client window region corresponds to
  // `buf_viewport' in buf coordinates.
  display_region = region;
  buf_region = convert_region_from_display_view(display_region,pscale,
                                                buf_viewport);

  // Now create `src_region' from `region', modified so as to have nice
  // alignment properties.
  buf_region &= buf_dims;
  if (buf_region.is_empty())
    { 
      display_region.right = display_region.left;
      display_region.bottom = display_region.top;
    }
  else
    { // We have some buffer samples to paint

      // Figure out where any focus box sits relative to the `buf_region'
      bool display_with_focus =
        (display_geometry.focus_state != 0) && highlight_focus;
      kdu_dims focus_region = display_geometry.focus_box;
      if (shape_istream.exists())
        { // Force whole image to be painted darker
          display_with_focus = true;
          display_geometry.focus_state = 0;
          focus_region = kdu_dims();
        }

      int bitmap_row_gap;
      kdu_uint32 *bitmap_buffer;
      HBITMAP bitmap = buffer->access_bitmap(bitmap_buffer,bitmap_row_gap);
   
      if (display_with_focus)
        { // Paint with focus-box adjustments
          assert(bitmap != NULL);
          // Map `buf_region' back to the `display_region' that we are actually
          // able to paint.
          display_region = convert_region_to_display_view(buf_region,pscale,
                                                          buf_viewport);
          focus_region &= buf_region;
          if (!focus_region)
            focus_region.pos = buf_region.pos;
          int focus_above = focus_region.pos.y - buf_region.pos.y;
          int focus_height = focus_region.size.y;
          int focus_left = focus_region.pos.x - buf_region.pos.x;
          int focus_width = focus_region.size.x;
          int focus_right = buf_region.size.x - focus_left - focus_width;
          assert((focus_above >= 0) && (focus_height >= 0) &&
                 (focus_left >= 0) && (focus_width >= 0) &&
                 (focus_right >= 0));
          if (strip_extent.x < buf_region.size.x)
            resize_stripmap(buf_region.size.x);
          RECT tgt_rect = display_region;

          kdu_dims src_region = buf_region;
          src_region.pos -= buf_dims.pos;
          while (src_region.size.y > 0)
            { 
              int i, j, xfer_height = src_region.size.y;
              if (xfer_height > strip_extent.y)
                { 
                  xfer_height = strip_extent.y;
                  if ((src_region.size.y-xfer_height) <= (strip_extent.y>>2))
                    xfer_height -= strip_extent.y>>2; // So no strip too small
                }
              int tgt_height = (int) kdu_round(xfer_height*pscale);
              if ((tgt_rect.top+tgt_height) > tgt_rect.bottom)
                tgt_height = tgt_rect.bottom - tgt_rect.top;
              kdu_uint32 *sp, *spp = bitmap_buffer +
                (src_region.pos.x + src_region.pos.y*bitmap_row_gap);
              kdu_uint32 *dp, *dpp = stripmap_buffer;
              for (j=xfer_height; j > 0; j--,
                   spp+=bitmap_row_gap, dpp+=strip_extent.x,
                   focus_above--)
                { 
                  sp = spp; dp = dpp;
                  kdu_uint32 val;
                  kdu_byte *lut;
                  if ((focus_above <= 0) && (focus_height > 0))
                    { 
                      focus_height--;
                      for (lut=background_lut, i=focus_left; i > 0; i--)
                        { 
                          val = *(sp++);
                          *(dp++) = 0xFF000000 +
                            (((kdu_uint32) lut[(val>>16)&0xFF])<<16) +
                            (((kdu_uint32) lut[(val>> 8)&0xFF])<< 8) +
                            (((kdu_uint32) lut[(val    )&0xFF])    );
                        }
                      for (lut=foreground_lut, i=focus_width; i > 0; i--)
                        { 
                          val = *(sp++);
                          *(dp++) = 0xFF000000 +
                            (((kdu_uint32) lut[(val>>16)&0xFF])<<16) +
                            (((kdu_uint32) lut[(val>> 8)&0xFF])<< 8) +
                            (((kdu_uint32) lut[(val    )&0xFF])    );
                      }
                      i = focus_right;
                    }
                  else
                    i = src_region.size.x;
                  for (lut=background_lut; i > 0; i--)
                    { 
                      val = *(sp++);
                      *(dp++) = 0xFF000000 +
                        (((kdu_uint32) lut[(val>>16)&0xFF])<<16) +
                        (((kdu_uint32) lut[(val>> 8)&0xFF])<< 8) +
                        (((kdu_uint32) lut[(val    )&0xFF])    );
                    }
                }
              HBITMAP old_bitmap = (HBITMAP)
                SelectObject(compatible_dc.m_hDC,stripmap);
              if (pscale == 1.0)
                dc->BitBlt(tgt_rect.left,tgt_rect.top,src_region.size.x,
                           xfer_height,&compatible_dc,0,0,SRCCOPY);
              else
                dc->StretchBlt(tgt_rect.left,tgt_rect.top,
                               tgt_rect.right-tgt_rect.left,tgt_height,
                               &compatible_dc,0,0,src_region.size.x,
                               xfer_height,SRCCOPY);
              SelectObject(compatible_dc.m_hDC,old_bitmap);
              src_region.pos.y += xfer_height;
              src_region.size.y -= xfer_height;
              tgt_rect.top += tgt_height;
            }
        }
      else
        { // Paint bitmap directly
          HBITMAP old_bitmap = (HBITMAP)
            SelectObject(compatible_dc.m_hDC,bitmap);
          // Map `buf_region' back to the `display_region' that we are
          // actually able to paint.
          display_region = convert_region_to_display_view(buf_region,pscale,
                                                          buf_viewport);
          if (pscale == 1.0)
            dc->BitBlt(display_region.left,display_region.top,
                       buf_region.size.x,buf_region.size.y,&compatible_dc,
                       buf_region.pos.x-buf_dims.pos.x,
                       buf_region.pos.y-buf_dims.pos.y,SRCCOPY);
          else
            dc->StretchBlt(display_region.left,display_region.top,
                           display_region.right-display_region.left,
                           display_region.bottom-display_region.top,
                           &compatible_dc,buf_region.pos.x-buf_dims.pos.x,
                           buf_region.pos.y-buf_dims.pos.y,
                           buf_region.size.x,buf_region.size.y,SRCCOPY);
          SelectObject(compatible_dc.m_hDC,old_bitmap);
        }
    }

  // See if we need to fill in any missing part of the frame
  if (region.top < display_region.top)
    dc->BitBlt(region.left,region.top,
               region.right-region.left,display_region.top-region.top,
               NULL,0,0,WHITENESS);
  if (region.bottom > display_region.bottom)
    dc->BitBlt(region.left,display_region.bottom,
               region.right-region.left,region.bottom-display_region.bottom,
               NULL,0,0,WHITENESS);
  if (region.left < display_region.left)
    dc->BitBlt(region.left,display_region.top,
               display_region.left-region.left,
               display_region.bottom-display_region.top,
               NULL,0,0,WHITENESS);
  if (region.right > display_region.right)
    dc->BitBlt(display_region.right,display_region.top,
               region.right-display_region.right,
               display_region.bottom-display_region.top,
               NULL,0,0,WHITENESS);
  display_region = region;

  // See if we need to draw the focus box outline
  if (display_geometry.focus_state != 0)
    { 
      assert(!shape_istream.exists());
      RECT box_rect =
        convert_region_to_display_view(display_geometry.focus_box,pscale,
                                       buf_viewport);
      if ((box_rect.right >= (display_region.left-3)) &&
          (box_rect.left <= (display_region.right+3)) &&
          (box_rect.bottom >= (display_region.top-3)) &&
          (box_rect.top <= (display_region.bottom+3)))
        { // Otherwise, the box does not show up at all inside `display_region'
          POINT pt;
          for (int p=0; p < 2; p++)
            { // Draw 2 lines of different colours around the focus box
              if (display_geometry.focus_state > 0)
                dc->SelectObject(manual_focus_pens[1-p]);
              else // Use different colour to indicate metadata-derived box
                dc->SelectObject(metadata_focus_pens[1-p]);
              pt.x = box_rect.left-p; pt.y = box_rect.top-p;
              dc->MoveTo(pt);
              pt.y = box_rect.bottom+p;
              dc->LineTo(pt);
              pt.x = box_rect.right+p;
              dc->LineTo(pt);
              pt.y = box_rect.top-p;
              dc->LineTo(pt);
              pt.x = box_rect.left-p;
              dc->LineTo(pt);
            }
        }
    }

  // See if we need to draw a "focussing" outline
  kdu_dims focussing_dims;
  if (compositor->get_focussing_rect(focussing_dims))
    { // Focussing outlines are drawn on the immediate surface managed by
      // `display_geometry'.
      RECT box_rect =
        convert_region_to_display_view(focussing_dims,pscale,
                                       display_geometry.viewport_dims);
      POINT pt;
      for (int p=0; p < 3; p++)
        { // Draw 3 lines, the inner one dashed
          dc->SelectObject(focussing_pens[p]);
          pt.x = box_rect.left-p; pt.y = box_rect.top-p;
          dc->MoveTo(pt);
          pt.y = box_rect.bottom+p;
          dc->LineTo(pt);
          pt.x = box_rect.right+p;
          dc->LineTo(pt);
          pt.y = box_rect.top-p;
          dc->LineTo(pt);
          pt.x = box_rect.left-p;
          dc->LineTo(pt);
        }
    }

  if (supplied_dc == NULL)
    image_view->ReleaseDC(dc);
  else
    supplied_dc->RestoreDC(save_dc_state);
}

/*****************************************************************************/
/*                          kdws_renderer::paint_dx                          */
/*****************************************************************************/

bool
  kdws_renderer::paint_dx(CDC *dc, kdu_coords surface_size)
{
  int save_dc_state = dc->SaveDC();

  kdws_orientation buf_orientation;
  kdu_dims buf_dims, buf_image_dims, buf_roi;
  kdws_compositor_buf *buffer = NULL;
  if (compositor != NULL)
    buffer = compositor->get_composition_buffer_ex(buf_dims,false);
    /* Note: the above function always retrieves the head of the composition
       buffer queue, if there is one; otherwise it retrieves the current
       working buffer.  If this function is invoked from the main application
       thread, as a result of user interaction, it is possible that the
       frame presentation thread will actually remove the head of the
       composition buffer queue while we are using it here.  However, this
       is of no concern, since popping a buffer from the queue does not
       delete or even recycle its storage until a subsequent call to
       `push_composition_buffer', which can only arrive from within the
       main application thread.  Moreover, if the above situation does occur,
       the frame presentation thread will be waiting to lock focus on the
       window, which will happen after the user interaction-inspired painting
       completes, at which time a more up-to-date version of the view will
       be painted in full. */
  if ((buffer == NULL) || buf_dims.is_empty() ||
      (!buffer->get_image_dims(buf_image_dims,buf_orientation)) ||
      buf_image_dims.is_empty())
    { // Paint blank region and return
      dc->BitBlt(0,0,surface_size.x,surface_size.y,NULL,0,0,WHITENESS);
      dc->RestoreDC(save_dc_state);
      return true;
    }
  buffer->get_roi(buf_roi);

  // Obtain information required for all painting operations below
  kdws_display_geometry display_geometry;
  compositor->get_display_geometry(display_geometry);
  if (display_geometry.orientation != buf_orientation)
    { // Incompatible scaling factors; major view changes occurred; don't
      // paint at this point, situation is transient.
      dc->RestoreDC(save_dc_state);
      return false;
    }

  kdu_dims buf_viewport; // Will be visible region, expressed in buf coords
  buf_viewport = display_geometry.viewport_dims;
  buf_viewport.pos -= display_geometry.image_dims.pos; // Make coords relative
  double scale=1.0;
  if (display_geometry.image_dims.size != buf_image_dims.size)
    { 
      scale = sqrt(((double) display_geometry.image_dims.area()) /
                   ((double) buf_image_dims.area()));
      double inv_scale = 1.0 / scale;
      kdu_coords lim = buf_viewport.pos + buf_viewport.size;
      buf_viewport.pos.x = (int) kdu_round(buf_viewport.pos.x*inv_scale-0.4);
      buf_viewport.pos.y = (int) kdu_round(buf_viewport.pos.y*inv_scale-0.4);
      lim.x = (int) kdu_round(lim.x * inv_scale + 0.4);
      lim.y = (int) kdu_round(lim.y * inv_scale + 0.4);
      buf_viewport.size = lim - buf_viewport.pos;
    }
  buf_viewport.pos += buf_image_dims.pos; // Make coords absolute
  buf_viewport &= buf_image_dims;

  kdu_coords buf_viewport_shift;
  if (!buf_roi.is_empty())
    { // The animator may be trying to move the viewport around in this case.
      // What this means is that we should ideally be painting from a region
      // on the buffer whose size is equal to `buf_viewport.size' but whose
      // location is centred over `buf_roi'.  In each case, the painted
      // region is to be transferred to `doc_region' on the display.  We
      // need to be careful to ensure that the adjusted `buf_viewport' is
      // contained within the available buffered data, being given by
      // `buf_dims'.
      kdu_coords view_pos; // Location we want to place the `buf_viewport'
      view_pos.x = buf_roi.pos.x +
        ((buf_roi.size.x - buf_viewport.size.x) >> 1);
      view_pos.y = buf_roi.pos.y +
        ((buf_roi.size.y - buf_viewport.size.y) >> 1);
      
      // Now adjust `view_pos' so as to make sure that the viewport is as
      // compatible as possible with `buf_dims'.
      int delta, slack1, slack2;
      slack1 = view_pos.x - buf_dims.pos.x;
      slack2 = buf_dims.size.x - buf_viewport.size.x - slack1;
      if ((slack1 < 0) && (slack2 > 0))
        { // Use up `slack2' as we need it until it becomes 0
          delta = ((-slack1) < slack2)?(-slack1):slack2;
          slack1+=delta; slack2-=delta; view_pos.x+=delta;
        }
      else if ((slack2 < 0) && (slack1 > 0))
        { // Use up `slack1' as we need it until it becomes 0
          delta = ((-slack2) < slack1)?(-slack2):slack1;
          slack2+=delta; slack1-=delta; view_pos.x-=delta;
        }
      if ((slack1 < 0) || (slack2 < 0))
        view_pos.x += (slack2-slack1)>>1;
      
      slack1 = view_pos.y - buf_dims.pos.y;
      slack2 = buf_dims.size.y - buf_viewport.size.y - slack1;
      if ((slack1 < 0) && (slack2 > 0))
        { // Use up `slack2' as we need it until it becomes 0
          delta = ((-slack1) < slack2)?(-slack1):slack2;
          slack1+=delta; slack2-=delta; view_pos.y+=delta;
        }
      else if ((slack2 < 0) && (slack1 > 0))
        { // Use up `slack1' as we need it until it becomes 0
          delta = ((-slack2) < slack1)?(-slack2):slack1;
          slack2+=delta; slack1-=delta; view_pos.y-=delta;
        }
      if ((slack1 < 0) || (slack2 < 0))
        view_pos.y += (slack2-slack1)>>1;
  
      // Adjust to make sure the viewport lies within `buf_image_dims'
      if ((delta = view_pos.x - buf_image_dims.pos.x) < 0)
        { view_pos.x -= delta; delta = 0; }
      if ((delta += buf_viewport.size.x - buf_image_dims.size.x) > 0)
        view_pos.x -= delta;
      if ((delta = view_pos.y - buf_image_dims.pos.y) < 0)
        { view_pos.y -= delta; delta = 0; }
      if ((delta += buf_viewport.size.y - buf_image_dims.size.y) > 0)
        view_pos.y -= delta;
      
      // Determine the `viewport_shift' and adjust regions
      buf_viewport_shift = view_pos - buf_viewport.pos;
      buf_viewport.pos += buf_viewport_shift;
      buf_roi &= buf_viewport;
      if (animation_metanode_shows_box)
        { 
          display_geometry.focus_state = -1;
          display_geometry.focus_box = buf_roi;
        }
      else
        display_geometry.focus_state = 0; // Don't confuse things with manual
                                          // focus boxes during animation.

      // Fix scrollers, if appropriate, so as to reveal the animation driven
      // viewport location and size -- we do this only from the presentation
      // thread, since any rendering from the main thread serves at most as
      // a filler, prior to the next attempt by the presentation thread to
      // display a correctly rendered frame.
      image_view->fix_scrollers(buf_viewport,buf_image_dims);
    }
  else
    image_view->release_scrollers(false);

  // Prepare for painting
  double pscale = scale * display_geometry.pixel_scale;
  kdu_dims buf_region; // Will be region to paint, expressed in buf coords
  buf_region = buf_viewport;
  buf_region &= buf_dims;

  if (buf_region.is_empty())
    { // Paint blank region and return
      dc->BitBlt(0,0,surface_size.x,surface_size.y,NULL,0,0,WHITENESS);
    }
  else
    { 
      // Paint the bitmap
      int bitmap_row_gap;
      kdu_uint32 *bitmap_buffer;
      HBITMAP bitmap = buffer->access_bitmap(bitmap_buffer,bitmap_row_gap);
      HBITMAP old_bitmap = (HBITMAP) SelectObject(compatible_dc.m_hDC,bitmap);

      RECT display_region; // Will be painted region, in image view coords
      display_region = convert_region_to_display_view(buf_region,pscale,
                                                      buf_viewport);
      if (pscale == 1.0)
        dc->BitBlt(display_region.left,display_region.top,buf_region.size.x,
                   buf_region.size.y,&compatible_dc,
                   buf_region.pos.x-buf_dims.pos.x,
                   buf_region.pos.y-buf_dims.pos.y,SRCCOPY);
      else
        dc->StretchBlt(display_region.left,display_region.top,
                       display_region.right-display_region.left,
                       display_region.bottom-display_region.top,
                       &compatible_dc,buf_region.pos.x-buf_dims.pos.x,
                       buf_region.pos.y-buf_dims.pos.y,
                       buf_region.size.x,buf_region.size.y,SRCCOPY);
      SelectObject(compatible_dc.m_hDC,old_bitmap);

      // Do focus highlighting, if necessary
      if ((display_geometry.focus_state != 0) && highlight_focus)
        { 
          if (strip_extent.x < 2)
            resize_stripmap(2);
          BLENDFUNCTION blendop;
          blendop.BlendOp = AC_SRC_OVER;
          blendop.BlendFlags = 0;
          blendop.SourceConstantAlpha = 0xFF;
          blendop.AlphaFormat = AC_SRC_ALPHA;

          kdu_dims focus_region = display_geometry.focus_box;
          focus_region &= buf_region;
          if (!focus_region)
            focus_region.pos = buf_region.pos;
          RECT focus_rect =
            convert_region_to_display_view(focus_region,pscale,buf_viewport);
          int x_coords[4] = { display_region.left, focus_rect.left,
                              focus_rect.right, display_region.right };
          int y_coords[4] = { display_region.top, focus_rect.top,
                              focus_rect.bottom, display_region.bottom };
          for (int v=0; v < 3; v++)
            { 
              int min_y=y_coords[v], lim_y=y_coords[v+1];
              if (lim_y <= min_y)
                continue;
              for (int h=0; h < 3; h++)
                { 
                  int min_x=x_coords[h], lim_x=x_coords[h+1];
                  if (lim_x <= min_x)
                    continue;

                  // Create a 1x1 image in the `stripmap' object whose
                  // colour (ARGB) can be blended with the region to
                  // produce the focus highlighting effect.
                  kdu_uint32 *dp = stripmap_buffer;
                  if ((h == 1) && (v == 1))
                    { // Central "focus" region needs a boost
                      *dp = (40<<24) + (40<<16) + (40<<8) + 40;
                    }
                  else
                    { // Outer "non-focus" region needs darkening
                      *dp = (40<<24);
                    }
                  HBITMAP old_bitmap = (HBITMAP)
                    SelectObject(compatible_dc.m_hDC,stripmap);
                  dc->AlphaBlend(min_x,min_y,lim_x-min_x,lim_y-min_y,
                                 &compatible_dc,0,0,1,1,blendop);
                  SelectObject(compatible_dc.m_hDC,old_bitmap);
                }
            }
        }

      // See if we need to fill in any missing part of the frame
      if (0 < display_region.top)
        dc->BitBlt(0,0,surface_size.x,display_region.top,NULL,0,0,WHITENESS);
      if (surface_size.y > display_region.bottom)
        dc->BitBlt(0,display_region.bottom,surface_size.x,
                   surface_size.y-display_region.bottom,NULL,0,0,WHITENESS);
      if (0 < display_region.left)
        dc->BitBlt(0,display_region.top,display_region.left,
                   display_region.bottom-display_region.top,
                   NULL,0,0,WHITENESS);
      if (surface_size.x > display_region.right)
        dc->BitBlt(display_region.right,display_region.top,
                   surface_size.x-display_region.right,
                   display_region.bottom-display_region.top,
                   NULL,0,0,WHITENESS);
    }

  // See if we need to draw the focus box outline
  if (display_geometry.focus_state != 0)
    { 
      assert(!shape_istream.exists());
      RECT box_rect =
        convert_region_to_display_view(display_geometry.focus_box,pscale,
                                       buf_viewport);
      if ((box_rect.right >= -3) && (box_rect.left <= (surface_size.x+3)) &&
          (box_rect.bottom >= -3) && (box_rect.top <= (surface_size.y+3)))
        { // Otherwise, the box does not show up at all on the surface
          POINT pt;
          for (int p=0; p < 2; p++)
            { // Draw 2 lines of different colours around the focus box
              if (display_geometry.focus_state > 0)
                dc->SelectObject(manual_focus_pens[1-p]);
              else // Use different colour to indicate metadata-derived box
                dc->SelectObject(metadata_focus_pens[1-p]);
              pt.x = box_rect.left-p; pt.y = box_rect.top-p;
              dc->MoveTo(pt);
              pt.y = box_rect.bottom+p;
              dc->LineTo(pt);
              pt.x = box_rect.right+p;
              dc->LineTo(pt);
              pt.y = box_rect.top-p;
              dc->LineTo(pt);
              pt.x = box_rect.left-p;
              dc->LineTo(pt);
            }
        }
    }

  // See if we need to draw a "focussing" outline
  kdu_dims focussing_dims;
  if (compositor->get_focussing_rect(focussing_dims))
    { // Focussing outlines are drawn on the immediate surface managed by
      // `display_geometry'.
      RECT box_rect =
        convert_region_to_display_view(focussing_dims,pscale,
                                       display_geometry.viewport_dims);
      POINT pt;
      for (int p=0; p < 3; p++)
        { // Draw 3 lines, the inner one dashed
          dc->SelectObject(focussing_pens[p]);
          pt.x = box_rect.left-p; pt.y = box_rect.top-p;
          dc->MoveTo(pt);
          pt.y = box_rect.bottom+p;
          dc->LineTo(pt);
          pt.x = box_rect.right+p;
          dc->LineTo(pt);
          pt.y = box_rect.top-p;
          dc->LineTo(pt);
          pt.x = box_rect.left-p;
          dc->LineTo(pt);
        }
    }

  dc->RestoreDC(save_dc_state);
  return true;
}

/*****************************************************************************/
/*                kdws_renderer::present_queued_frame_buffer                 */
/*****************************************************************************/

bool
  kdws_renderer::present_queued_frame_buffer(double display_event_time,
                                             double next_display_event_time)
{
  if ((compositor == NULL) || !view_dims)
    return false;
  if (!compositor->lock_new_composition_queue_head(display_event_time,
                                                   next_display_event_time))
    return false;

  bool result = false;
  kdu_coords surface_size;
  CDC *dc = frame_presenter->access_surface(surface_size);
  if (dc != NULL)
    { 
      try {
        paint_dx(dc,surface_size);
      } catch (...) {}
      frame_presenter->release_surface();
      result = true;
    }

  compositor->unlock_and_mark_composition_queue_head();  
  return result;
}

/*****************************************************************************/
/*                      kdws_renderer::display_status                        */
/*****************************************************************************/

void kdws_renderer::display_status()
{
  int p;
  char char_buf[300];
  char *panel_text[3] = {char_buf,char_buf+100,char_buf+200};
  for (p=0; p < 3; p++)
    *(panel_text[p]) = '\0'; // Reset all panels to empty strings.

  // Find resolution information
  kdu_dims basis_region = find_focus_box();
  float component_scale_x=-1.0F, component_scale_y=-1.0F;
  if ((compositor != NULL) && configuration_complete)
    {
      kdu_ilayer_ref layer =
        compositor->get_next_visible_ilayer(kdu_ilayer_ref(),basis_region);
      if (layer.exists() &&
          !compositor->get_next_visible_ilayer(layer,basis_region))
        {
          int cs_idx;
          kdu_istream_ref stream = compositor->get_ilayer_stream(layer,0);
          compositor->get_istream_info(stream,cs_idx,NULL,NULL,4,NULL,
                                       &component_scale_x,
                                       &component_scale_y);
          component_scale_x *= rendering_scale;
          component_scale_y *= rendering_scale;
          if (orientation.trans)
            {
              float tmp=component_scale_x;
              component_scale_x=component_scale_y;
              component_scale_y=tmp;
            }
        }
    }

  if ((status_id == KDS_STATUS_CACHE) && (jpip_client == NULL))
    status_id = KDS_STATUS_LAYER_RES;

  // Fill in status pane 0 with resolution information
  if (status_id == KDS_STATUS_CACHE)
    {
      assert(jpip_client_status != NULL);
      if (strlen(jpip_client_status) < 80)
        strcpy(panel_text[0],jpip_client_status);
      else
        {
          strncpy(panel_text[0],jpip_client_status,80);
          strcpy(panel_text[0]+80,"...");
        }
    }
  else if (compositor != NULL)
    {
      float res_percent = 100.0F*rendering_scale;
      float x_percent = 100.0F*component_scale_x;
      float y_percent = 100.0F*component_scale_y;
      if ((x_percent > (0.99F*res_percent)) &&
          (x_percent < (1.01F*res_percent)) &&
          (y_percent > (0.99F*res_percent)) &&
          (y_percent < (1.01F*res_percent)))
        x_percent = y_percent = -1.0F;
      if (x_percent < 0.0F)
        sprintf(panel_text[0],"Res=%1.1f%% x%d",res_percent,pixel_scale);
      else if ((x_percent > (0.99F*y_percent)) &&
               (x_percent < (1.01F*y_percent)))
        sprintf(panel_text[0],"Res=%1.1f%% (%1.1f%%) x%d",
                res_percent,x_percent,pixel_scale);
      else
        sprintf(panel_text[0],"Res=%1.1f%% (x:%1.1f%%,y:%1.1f%%) x%d",
                res_percent,x_percent,y_percent,pixel_scale);
    }
  
  // Fill in status pane 1 with image/component/frame information
  if (compositor != NULL)
    {
      char *sp = panel_text[1];
      if (single_component_idx >= 0)
        {
          sprintf(sp,"Comp=%d/",single_component_idx+1);
          sp += strlen(sp);
          if (max_components <= 0)
            *(sp++) = '?';
          else
            { sprintf(sp,"%d",max_components); sp += strlen(sp); }
          sprintf(sp,", Stream=%d/",single_codestream_idx+1);
          sp += strlen(sp);
          if (max_codestreams <= 0)
            strcpy(sp,"?");
          else
            sprintf(sp,"%d",max_codestreams);
        }
      else if (single_layer_idx >= 0)
        { // Report compositing layer index
          if (animation_display_idx < 0)
            sprintf(sp,"C.Layer=%d/",single_layer_idx+1);
          else
            sprintf(sp,"PLAY C.Layer=%d/",animation_display_idx+1);
          sp += strlen(sp);
          if (max_compositing_layer_idx < 0)
            strcpy(sp,"?");
          else
            sprintf(sp,"%d",max_compositing_layer_idx+1);
        }
      else
        { // Report track and frame indices
          if (animation_display_idx < 0)
            sprintf(sp,"Trk:Frm=%d:%d/",(int)track_idx,frame_idx+1);
          else
            sprintf(sp,"PLAY Trk:Frm=%d:%d/",
                    (int)track_idx,animation_display_idx+1);
          sp += strlen(sp);
          if (!num_frames_known)
            strcpy(sp,"?");
          else
            sprintf(sp,"%d",num_frames);
        }   
    }
  
  // Fill in status pane 2
  if ((compositor != NULL) && (status_id == KDS_STATUS_LAYER_RES))
    {
      if ((animator == NULL) &&
          !compositor->is_codestream_processing_complete())
        strcpy(panel_text[2],"WORKING");
      else
        { 
          int max_layers = compositor->get_max_available_quality_layers();
          if (max_display_layers >= max_layers)
            sprintf(panel_text[2],"Q.lyrs=all, ");
          else
            sprintf(panel_text[2],"Q.lyrs=%d/%d, ",
                    max_display_layers,max_layers);
          if (quality_limiting_mode == 0)
            strcat(panel_text[2],"full Q");
          else if (quality_limiting_mode == 1)
            strcat(panel_text[2],"trimmed");
          else
            strcat(panel_text[2],"lossy");
        }
    }
  else if ((compositor != NULL) && (status_id == KDS_STATUS_DIMS))
    {
      if ((animator == NULL) &&
          !compositor->is_codestream_processing_complete())
        strcpy(panel_text[2],"WORKING");
      else
        sprintf(panel_text[2],"HxW=%dx%d ",
                image_dims.size.y,image_dims.size.x);
    }
  else if ((compositor != NULL) && (status_id == KDS_STATUS_MEM))
    {
      if ((animator == NULL) &&
          !compositor->is_codestream_processing_complete())
        strcpy(panel_text[2],"WORKING");
      else
        {
          kdu_long bytes = 0;
          kdu_istream_ref str;
          while ((str=compositor->get_next_istream(str,false,true)).exists())
            {
              kdu_codestream ifc = compositor->access_codestream(str);
              bytes += ifc.get_compressed_data_memory()
                     + ifc.get_compressed_state_memory();
            }
          if ((jpip_client != NULL) && jpip_client->is_active())
            bytes += jpip_client->get_peak_cache_memory();
          sprintf(panel_text[2],"%5g MB compressed data memory",1.0E-6*bytes);
        }
    }
  else if ((compositor != NULL) && (status_id == KDS_STATUS_PLAYSTATS))
    { 
      double avg_frame_rate = 0.0;
      if (animator != NULL)
        avg_frame_rate = animator->get_avg_frame_rate();
      if (avg_frame_rate > 0.0)
        {
          const char *format = "%4.2f fps";
          if (avg_frame_rate >= 10.0)
            format = "%4.1f fps";
          if (avg_frame_rate >= 100.0)
            format = "%4.0f fps";
          if (avg_frame_rate >= 1000.0)
            format = "%5g fps";
          sprintf(panel_text[2],format,avg_frame_rate);
        }
    }
  else if ((jpip_client != NULL) && (status_id == KDS_STATUS_CACHE))
    { 
      double active_secs = 0.0;
      kdu_long global_bytes = jpip_client->get_received_bytes(-1);
      kdu_long queue_bytes =
        jpip_client->get_received_bytes(client_request_queue_id,&active_secs);
      double interval_seconds = active_secs - jpip_interval_start_time;
      if (queue_bytes <= jpip_interval_start_bytes)
        { // Use last known statistics
          queue_bytes = jpip_interval_start_bytes;
        }
      else if ((interval_seconds > 0.5) && (active_secs > 2.0))
        { // Update statistics for this queue so long as some active reception
          // time has ellapsed since last update and we have been active for
          // a total of at least 2 seconds.
          kdu_long interval_bytes = queue_bytes - jpip_interval_start_bytes;
          jpip_interval_start_bytes = queue_bytes;
          jpip_interval_start_time = active_secs;
          double inst_rate = ((double) interval_bytes) / interval_seconds;
          double rho = 1.0 - pow(0.85,interval_seconds);
          if (jpip_client_data_rate <= 0.0)
            rho = 1.0;
          jpip_client_data_rate += rho * (inst_rate - jpip_client_data_rate);
        }
      bool use_megabytes = (global_bytes >= 1000000);
      if (use_megabytes)
        sprintf(panel_text[2],"%5.2f MB",(0.000001 * (double) queue_bytes));
      else
        sprintf(panel_text[2],"%5.1f kB",(0.001 * (double) queue_bytes));
      if (jpip_client_data_rate > 0.0)
        { 
          char *cp = panel_text[2] + strlen(panel_text[2]);
          if (jpip_client_data_rate >= 1000000.0)
            sprintf(cp,"; %5.2f MB/s",0.000001 * jpip_client_data_rate);
          else
            sprintf(cp,"; %5.1f kB/s",0.001 * jpip_client_data_rate);
        }
      if (global_bytes > queue_bytes)
        { 
          char *cp = panel_text[2] + strlen(panel_text[2]);
          if (use_megabytes)
            sprintf(cp,"(%5.2f MB)",(0.000001 * (double) global_bytes));
          else
            sprintf(cp," (%5.1f kB)",(0.001 * (double) global_bytes));
        }
    }

  if (jpip_client != NULL)
    { 
      double progress = animation_display_jpip_progress;
      if (progress < 0.0)
        progress = jpip_progress;
      if ((jpip_progress != jpip_last_displayed_progress))
        { 
          jpip_last_displayed_progress = jpip_progress;
          window->set_progress_bar(jpip_progress);
        }
    }
  window->set_status_strings((const char **)panel_text);
}

/*****************************************************************************/
/*                        kdws_renderer::do_processing                       */
/*****************************************************************************/

bool kdws_renderer::do_processing()
{
  double cur_time = get_current_time();
  
  // Start by managing the processing of imagery
  kdu_dims new_region;
  bool processed_something = false;
  bool processing_complete = true;
  bool processing_newly_complete = false;
  if (configuration_complete && (working_buffer != NULL) &&
      !animation_frame_complete)
    { 
      if (animator != NULL)
        animation_frame_started = true;
      try {
        int defer_flag = KDU_COMPOSIT_DEFER_REGION;
        if ((animator == NULL) && (defer_process_regions_deadline > 0.0) &&
            (cur_time > defer_process_regions_deadline))
          { defer_flag=0; defer_process_regions_deadline = cur_time + 0.05; }
        processed_something=compositor->process(128000,new_region,defer_flag);
        cur_time = get_current_time();
        if (processed_something && (animator == NULL) &&
            (defer_process_regions_deadline <= 0.0))
          defer_process_regions_deadline = cur_time + 0.05;
        if (!(processed_something ||
              compositor->get_total_composition_dims(image_dims)))
          { // Must have invalid scale
            working_buffer = NULL;
            initialize_regions(false); // Call will find a satisfactory scale
            return false;
          }
      }
      catch (kdu_exception exc) { // Only safe thing is close the file.
        close_after_exception(exc);
        return false;
      }
      catch (std::bad_alloc &) {
        close_after_exception(KDU_MEMORY_EXCEPTION);
        kdws_string
          text_string("Insufficient memory -- you could improve "
                      "upon the current implementation to invoke "
                      "`kdu_region_compositor::cull_inactive_layers' "
                      "or take other memory-saving action in response "
                      "to this condition.");
        kdws_string caption("kdu_show -- memory error");
        window->MessageBox(text_string,caption,MB_OK);
        return false;
      }

      processing_complete = compositor->is_processing_complete();
      processing_newly_complete = processed_something && processing_complete;
      if (processing_newly_complete)
        defer_process_regions_deadline = -1.0;
  
      if ((jpip_client != NULL) && processing_newly_complete)
        { // Update JPIP progress estimate
          kdu_long samples=0, packet_samples=0, max_packet_samples=0, s, p, m;
          kdu_ilayer_ref lyr;
          kdu_dims roi = find_focus_box();
          while ((compositor != NULL) &&
                 ((lyr=compositor->get_next_visible_ilayer(lyr,roi)).exists()))
            { 
              kdu_istream_ref str;
              int w=0;
              while ((str=compositor->get_ilayer_stream(lyr,w++)).exists())
                { 
                  compositor->get_codestream_packets(str,roi,s,p,m,
                                                     max_display_layers);
                  samples += s;  packet_samples += p;  max_packet_samples += m;
                }
            }
          jpip_progress = 0.0;
          if (packet_samples > 0)
            jpip_progress = 2.0 + // Make sure the user can see some progress
            98.0 * (((double) packet_samples) / ((double) max_packet_samples));
        }
    }
  
  if (animator != NULL)
    { 
      if (processing_complete &&
          (animator->next_frame_has_changed() || !animation_frame_complete))
        { 
          manage_animation_frame_queue(cur_time);
          if (!animation_frame_complete)
            processing_complete = false; // `advance_animation' succeeded
        }
    }
  else if (processed_something)
    { 
      if (processing_complete && (earliest_render_refresh_time < 0.0))
        { 
          earliest_render_refresh_time = cur_time + jpip_refresh_interval;
          if (jpip_wants_render_refresh)
            perform_or_schedule_render_refresh(cur_time);
        }
      if (processing_newly_complete)
        configure_overlays(cur_time,false);
      
      // Paint any newly decompressed region right away.
      new_region &= view_dims; // No point in painting invisible regions.
      if (!new_region.is_empty())
        { 
          if (pixel_scale > 1)
            { // Adjust the invalidated region so interpolated painting
              // produces no cracks.
              new_region.pos.x--; new_region.pos.y--;
              new_region.size.x += 2; new_region.size.y += 2;
            }
          RECT dirty_rect = convert_region_to_display_view(new_region);
          if (shape_editor == NULL)
            paint_region(dirty_rect,NULL); // Paint directly
          else // Need window machinery to draw editor outlines
            image_view->InvalidateRect(&dirty_rect);
        }
      if ((jpip_client == NULL) || processing_newly_complete)
        display_status(); // All processing done, keep status up to date
    }

  return ((compositor != NULL) && !processing_complete);
}

/*****************************************************************************/
/*                 kdws_renderer::update_animation_status_info               */
/*****************************************************************************/

void kdws_renderer::update_animation_status_info()
{
  if (compositor == NULL)
    return;
  int presented_idx=animation_display_idx;
  double presented_frame_start = 0.0;
  double presented_frame_end = 0.0;
  int presented_metamation_id = -1;
  jpx_metanode presented_metanode=animation_display_metanode;
  double presented_jpip_progress=0.0;
  double presented_display_time=-1.0;
  double next_frame_change_time=animation_frame_status_update_time;
  double next_node_change_time=animation_metanode_status_update_time;
  double next_progress_change_time=animation_jpip_status_update_time;
  if (compositor->get_presented_queue_head_info(presented_idx,
                                                presented_frame_start,
                                                presented_frame_end,
                                                presented_metanode,
                                                presented_metamation_id,
                                                presented_jpip_progress,
                                                presented_display_time,
                                                next_frame_change_time,
                                                next_node_change_time,
                                                next_progress_change_time))
    { 
      bool need_display_status_update=false;
      bool need_catalog_node_select=false;
      bool need_animation_bar_update=false;
      bool need_metamation_bar_update=false;
      if (animation_status_invalid)
        { 
          need_display_status_update = true;
          animation_status_invalid = false;
        }
      if ((animator != NULL) && (working_buffer != NULL) &&
          !(pushed_last_frame ||
            (animation_frame_complete && !animation_frame_needs_push)))
        { // See if the frame we are currently working on cancels updates
          double latest_display_time = animator->get_display_time();
          if ((next_frame_change_time < 0.0) &&
              (latest_display_time <= animation_frame_status_update_time) &&
              (presented_idx !=
               ((animator->get_single_layer_mode())?single_layer_idx:
                                                    frame_idx)))
            next_frame_change_time = latest_display_time;
          if ((next_node_change_time < 0.0) &&
              (latest_display_time <= animation_metanode_status_update_time) &&
              animation_metanode.exists() &&
              (presented_metanode != animation_metanode))
            next_node_change_time = latest_display_time;
        }
      if (presented_idx != animation_display_idx)
        { 
          animation_display_idx = presented_idx;
          if (next_frame_change_time <= 0.0)
            { 
              need_display_status_update = true;
              if (animation_bar != NULL)
                need_animation_bar_update = true;
              double delta = (presented_display_time -
                              animation_frame_status_update_time);
              delta = 0.25 * (1.0+floor(delta*4.0)); // Advance at least 0.25s
              animation_frame_status_update_time += delta;
            }
        }
      if (presented_metanode != animation_display_metanode)
        { 
          animation_display_metanode = presented_metanode;
          if (next_node_change_time <= 0.0)
            { 
              if (catalog_source != NULL)
                need_catalog_node_select = true;
              if (metamation_bar != NULL)
                need_metamation_bar_update = true;
              double delta = (presented_display_time -
                              animation_metanode_status_update_time);
              delta = 0.25 * (1.0+floor(delta*4.0)); // Advance at least 0.25s
              animation_metanode_status_update_time += delta;      
            }
        }
      if (presented_jpip_progress != animation_display_jpip_progress)
        { 
          animation_display_jpip_progress = presented_jpip_progress;
          if (next_progress_change_time <= 0.0)
            { 
              need_display_status_update = true;
              double delta = (presented_display_time -
                              animation_jpip_status_update_time);
              delta = 0.25 * (1.0+floor(delta*4.0)); // Advance at least 0.25s
              animation_jpip_status_update_time += delta;
            }
        }
      if (need_display_status_update)
        display_status();
      if (need_catalog_node_select)
        catalog_source->select_matching_metanode(presented_metanode);
      if (need_animation_bar_update)
        { 
          if (presented_frame_start < 0.0)
            animation_bar->set_frame(presented_idx,max_compositing_layer_idx,
                                     -1.0,-1.0,-1.0);
          else
            { 
              double track_end=-1.0;
              if (!get_track_duration(track_end))
                track_end = -1.0;
              animation_bar->set_frame(presented_idx,(num_frames-1),
                                       presented_frame_start,
                                       presented_frame_end,
                                       track_end);
            }
        }
      if (need_metamation_bar_update)
        { 
          int num_meta_frames;
          if (!animator->count_metadata_driven_frames(num_meta_frames))
            num_meta_frames = 0;
          metamation_bar->set_frame(presented_idx,num_meta_frames-1,
                                    -1.0,-1.0,-1.0);
        }
      if (pushed_last_frame && compositor->composition_queue_tail_presented())
        stop_animation();
    }
}

/*****************************************************************************/
/*                 kdws_renderer::manage_animation_frame_queue               */
/*****************************************************************************/

void kdws_renderer::manage_animation_frame_queue(double cur_system_time)
{
  if (animator == NULL)
    return; // We should not be here
  
  // Collect timing information required by this function
  if (cur_system_time < 0.0)
    cur_system_time = get_current_time();
  double next_display_event_time=-1.0;
  double last_display_event_time =
    compositor->get_display_event_times(next_display_event_time);

  // See if we have just finished processing a frame
  if ((working_buffer != NULL) && !animation_frame_complete)
    { 
      if (!compositor->is_processing_complete())
        return; // Still working
      animator->note_frame_generated(cur_system_time,
                                     next_display_event_time);
      double display_time = animator->get_display_time();
      if ((single_layer_idx < 0) && !animation_metanode)
        working_buffer->set_orig_frame_times(frame_start,frame_end);
      working_buffer->set_display_time(display_time);
      working_buffer->set_metanode(animation_metanode,
                                   animator->get_metadata_driven_frame_id());
      working_buffer->set_jpip_progress(jpip_progress);
      animation_frame_complete = true;
      animation_frame_needs_push = true;
    
      double overdue = last_display_event_time - display_time;
      if ((overdue > 0.1) && !working_buffer->is_incomplete_dynamic_roi())
        { // Running behind by more than 100ms.
          double display_interval =
            next_display_event_time - last_display_event_time;
          int n_intervals = (int)(0.5*overdue / display_interval);
          if (n_intervals > 0)
            manager->broadcast_playclock_adjustment(n_intervals *
                                                    display_interval);
            // Broadcasting the adjustment to all windows allows multiple
            // video playback windows to keep in sync.
        }
    }

  // See if we are still waiting to push a processed frame onto the
  // composited frame queue.
  if (animation_frame_needs_push)
    { 
      if (!compositor->is_processing_complete())
        { // Something must have happened since we asserted
          // `animation_frame_complete'; we will not be able to push this
          // buffer to the queue, so don't risk popping anything off yet.
          animation_frame_complete = animation_frame_needs_push = false;
          return; // Need to do some more work
        }
      int max_queue = animation_max_queued_frames;
      int display_idx = ((animator->get_single_layer_mode())?
                         single_layer_idx:frame_idx);
      double display_time = animator->get_display_time();
      compositor->set_surface_initialization_mode(false);
      if (!compositor->conditionally_push_composition_buffer(display_time,
                                                             display_idx,
                                                             max_queue))
        { // No space on the queue, but we will come back here when the
          // frame presenter presents something, after which the
          // `kdws_window::display_notification' function will be called.
          return;
        }
      animation_frame_needs_push = false;
    }

  // See if we should try to advance to the next frame or schedule an
  // attempt to do so in the future.
  double advance_delay =
    animator->get_suggested_advance_delay(next_display_event_time);
  if (advance_delay > 0.1)
    { 
      schedule_frame_advance(cur_system_time+advance_delay);
      return;
    }  
  
  // Set up the next frame to render
  advance_animation(cur_system_time,
                    last_display_event_time,next_display_event_time);
}

/*****************************************************************************/
/*                          kdws_renderer::wakeup                            */
/*****************************************************************************/

void kdws_renderer::wakeup(double scheduled_time, double current_time)
{
  if (current_time < scheduled_time)
    current_time = scheduled_time; // Just in case; should not happen
  next_scheduled_wakeup_time = -1.0; // Nothing scheduled right now
  
  // Perform all relevant activities which were scheduled
  if (animator != NULL)
    { 
      if ((next_frame_advance_time >= 0.0) &&
          (next_frame_advance_time <= current_time))
        { 
          next_frame_advance_time = -1.0;
          manage_animation_frame_queue(current_time);
        }
    }
  else
    { // No animation
      if ((next_overlay_change_time >= 0.0) &&
          (next_overlay_change_time <= current_time))
        configure_overlays(current_time,true);  
      if ((next_render_refresh_time >= 0.0) &&
          (next_render_refresh_time <= current_time))
        { 
          next_render_refresh_time = -1.0;
          perform_any_outstanding_render_refresh(current_time);
        }
    }
  
  if ((next_metadata_refresh_time >= 0) &&
      (next_metadata_refresh_time <= current_time))
    refresh_metadata(current_time);
  
  install_next_scheduled_wakeup();
}

/*****************************************************************************/
/*                   kdws_renderer::client_notification                      */
/*****************************************************************************/

void kdws_renderer::client_notification()
{
  if (jpip_client == NULL)
    return;
  jp2_family_in.synch_with_cache();
  bool need_to_update_request = false;
  if (compositor == NULL)
    open_file(NULL); // Try to complete the client source opening operation
  if ((compositor != NULL) && !configuration_complete)
    initialize_regions(false);
  if ((compositor != NULL) && (animator == NULL) &&
      compositor->waiting_for_stream_headers())
    { // See if we can now find the stream headers
      if (render_refresh(true))
        need_to_update_request = true;
    }
  
  if (respect_initial_jpip_request && configuration_complete &&
      !one_time_jpip_request)
    respect_initial_jpip_request = false;

  const char *new_status_text = "<NO JPIP>";
  kdu_long new_bytes = jpip_client_received_queue_bytes;
  kdu_long new_metadata_bytes = jpip_cache_metadata_bytes;
  if (jpip_client != NULL)
    { 
      new_status_text = jpip_client->get_status(client_request_queue_id);
      new_bytes = jpip_client->get_received_bytes(client_request_queue_id);
      new_metadata_bytes =
        jpip_client->get_transferred_bytes(KDU_META_DATABIN);
      if ((animator == NULL) && configuration_complete &&
          jpip_client->get_window_in_progress(&tmp_roi,
                                              client_request_queue_id))
        { 
          if ((client_roi.region != tmp_roi.region) ||
              (client_roi.resolution != tmp_roi.resolution))
            change_client_focus(tmp_roi.resolution,tmp_roi.region);
          client_roi.region = tmp_roi.region;         // So we don't have to
          client_roi.resolution = tmp_roi.resolution; // repeat this step
        }
    }

  bool status_text_changed = (new_status_text != jpip_client_status);
  bool bytes_changed = (new_bytes != jpip_client_received_queue_bytes);
  bool metadata_changed = (new_metadata_bytes != jpip_cache_metadata_bytes);
  jpip_client_status = new_status_text;
  jpip_client_received_queue_bytes = new_bytes;
  jpip_cache_metadata_bytes = new_metadata_bytes;
  double cur_time = -1.0; // Initialize if and when we need it
  double next_display_event_time = -1.0; // Initialize if and when we need it
  if (bytes_changed)
    { 
      jpip_client_received_total_bytes = jpip_client->get_received_bytes(-1);
            // By updating the total client bytes only when the queue bytes
            // changes we can be sure that the value reflects any bytes
            // received on the current queue.  This allows us to separately
            // test for changes in the total client bytes at important
            // junctures (such as in `set_key_window_status') and be sure
            // that we only detect bytes received above and beyond those
            // available at the time when the bytes for the current queue
            // last changed.
      if (animator == NULL)
        { 
          bool new_wants_refresh = !jpip_wants_render_refresh;
          jpip_wants_render_refresh = true;
          if (new_wants_refresh && (earliest_render_refresh_time > 0.0))
            perform_or_schedule_render_refresh(cur_time);
        }
      else if (compositor != NULL)
        { 
          compositor->get_display_event_times(next_display_event_time);
          animator->notify_client_progress(jpip_client,client_request_queue_id,
                                           next_display_event_time);
        }
    }

  if (animator == NULL)
    { 
      if (bytes_changed && pending_show_metanode.exists())
        show_imagery_for_metanode(pending_show_metanode);
      if ( metadata_changed || /*bytes_changed ||*/ status_text_changed)
        display_status();
    }

  // See if the metadata display tools need to be refreshed.  Currently, we
  // do this in the same way regardless of whether there is any animation
  // in progress, but it might make sense to skip metadata display updates
  // during animation if they become too costly in terms of CPU time.
  if (metadata_changed && (next_metadata_refresh_time < 0.0))
    { 
      cur_time = (cur_time >= 0.0)?cur_time:get_current_time();
      double refresh_interval = jpip_metadata_refresh_interval;
      if (catalog_has_oob_metareqs)
        refresh_interval *= 0.2;
      if (cur_time > (last_metadata_refresh_time + 0.9*refresh_interval))
        refresh_metadata(cur_time);
      else
        schedule_metadata_refresh_wakeup(last_metadata_refresh_time +
                                         refresh_interval);
    }
  
  // Finally, see if the receipt of data by the JPIP client warrants the
  // generation of any new client requests
  if (jpip_client == NULL)
    return;
  if ((animator != NULL) || need_to_update_request)
    { 
      update_client_window_of_interest(cur_time,next_display_event_time);
    }
  else if (have_novel_anchor_metareqs &&
           jpip_client->is_idle(client_request_queue_id))
    { // Server has finished responding to all requests, but there may now
      // be enough data to make a more precise request for the
      // metadata we actually need.  Any call to
      // `jpx_metanode::generate_metareq' that returns non-zero, may need
      // to be re-issued in the light of newly arrived data.
      // `have_novel_anchor_metareqs' indicates whether any such calls were
      // generated while looking for or performing top-level expansion of
      // anchor nodes (relevant numlists, ROI nodes or semantically top-level
      // global metadata) during the last call to
      // `update_client_window_of_interest'.
      update_client_window_of_interest(cur_time);
    }
  else if (catalog_has_oob_metareqs || animator_has_oob_metareqs)
    { // Essentially the same as above, but we are looking for metadata
      // requests generated by the metadata catalog side-bar -- these have
      // very high priority so are sent over the special OOB (out-of-band)
      // mechanism offered by `kdu_client'.  `catalog_has_oob_metareqs'
      // records whether calls to `jpx_metanode::generate_metareq' were
      // generated by the catalog side-bar within the last call to
      // `update_client_window_of_interest'.  `animator_has_oob_metareqs' is
      // the same, but applies if the calls were generated by an active
      // `kdu_region_animator' object requesting urgent metadata.
      int status_flags = 0;
      if (jpip_client->get_oob_window_in_progress(NULL,client_request_queue_id,
                                                  &status_flags) &&
          (status_flags & KDU_CLIENT_WINDOW_RESPONSE_TERMINATED))
        { // Note: unlike regular queues, the `kdu_client' offers no
          // `is_idle' call that can be used to test if the internal OOB
          // queue is idle, mainly because this may be shared by multiple
          // caller-id's.
          update_client_window_of_interest(cur_time);
        }
    }
}

/*****************************************************************************/
/*                      kdws_renderer::render_refresh                        */
/*****************************************************************************/

bool kdws_renderer::render_refresh(bool new_imagery_only)
{
  if (compositor == NULL)
    return false;
  
  bool something_refreshed = configuration_complete;
  bool *check_new_imagery_only = NULL;
  if (new_imagery_only)
    check_new_imagery_only = &something_refreshed;
  if (configuration_complete && !compositor->refresh(check_new_imagery_only))
    { // Can no longer trust buffer surfaces
      if (check_new_imagery_only != NULL)
        assert(something_refreshed);
      kdu_dims new_image_dims;
      kdu_region_animator_roi animator_roi;
      bool have_valid_scale = 
        compositor->get_total_composition_dims(new_image_dims);
      if ((!have_valid_scale) || (new_image_dims != image_dims) ||
          (animator != NULL))
        initialize_regions(false);
      else
        { 
          working_buffer =
            compositor->get_composition_buffer_ex(working_buffer_dims,true);
          if (working_buffer != NULL)
            { 
              working_buffer->set_image_dims(image_dims,orientation);
              working_buffer->set_roi_info(animator_roi);
            }
        }
    }

  if (something_refreshed)
    { 
      next_render_refresh_time = -1.0;
      earliest_render_refresh_time = -1.0;
      animation_frame_started = false;
      animation_frame_complete = false;
      animation_frame_needs_push = false;
      jpip_wants_render_refresh = false;
    }
  return something_refreshed;
}

/*****************************************************************************/
/*              kdws_renderer::perform_or_schedule_render_refresh            */
/*****************************************************************************/

void kdws_renderer::perform_or_schedule_render_refresh(double cur_time)
{
  assert(animator == NULL);
  
  if ((compositor == NULL) || (earliest_render_refresh_time < 0.0) ||
      !jpip_wants_render_refresh)
    return; // Nothing to do yet
  if (next_overlay_change_time >= 0.0)
    return; // Let render refresh occur when we next change the
            // overlay configuration, to avoid rendundant rendering steps.
  cur_time = (cur_time >= 0.0)?cur_time:get_current_time();
  if (cur_time >= earliest_render_refresh_time)
    render_refresh();
  else
    schedule_render_refresh_wakeup(earliest_render_refresh_time);
}

/*****************************************************************************/
/*                      kdws_renderer::refresh_metadata                      */
/*****************************************************************************/

void kdws_renderer::refresh_metadata(double cur_time)
{
  if ((compositor != NULL) && (metashow == NULL))
    compositor->load_metadata_matches();
  else if (jpx_in.exists())
    jpx_in.access_meta_manager().load_matches(-1,NULL,-1,NULL);
  
  if (metashow != NULL)
    metashow->update_metadata();
  
  if (compositor != NULL)
    { 
      if ((catalog_source == NULL) && jpx_in.exists() && (!catalog_closed) &&
          kdws_catalog_new_data_available(jpx_in.access_meta_manager()))
        window->create_metadata_catalog();
  
      if (catalog_source != NULL)
        catalog_source->update_metadata();
    }

  next_metadata_refresh_time = -1.0;
  last_metadata_refresh_time = cur_time;
  
  if (animator == NULL)
    update_client_window_of_interest(cur_time);
}

/*****************************************************************************/
/*               kdws_renderer::install_next_scheduled_wakeup                */
/*****************************************************************************/

void kdws_renderer::install_next_scheduled_wakeup()
{
  double tmp, min_time = next_metadata_refresh_time;
  if (animator == NULL)
    { 
      if (((tmp=next_render_refresh_time) >= 0.0) &&
          ((min_time < 0.0) || (min_time > tmp)))
        min_time = tmp;
      if (((tmp=next_overlay_change_time) >= 0.0) &&
          ((min_time < 0.0) || (min_time > tmp)))
        min_time = tmp;
    }
  else
    { 
      if (((tmp=next_frame_advance_time) >= 0.0) &&
          ((min_time < 0.0) || (min_time > tmp)))
        min_time = tmp;
    }

  if (min_time < 0.0)
    {
      if (next_scheduled_wakeup_time >= 0.0)
        manager->schedule_wakeup(window,-1.0); // Cancel existing wakeup
      next_scheduled_wakeup_time = -100.0; // Make sure it cannot pass any
        // of the tests in the above functions, which coerce a loose wakeup
        // time to equal the next scheduled wakeup time if it is already close.
    }
  else if (min_time != next_scheduled_wakeup_time)
    {
      next_scheduled_wakeup_time = min_time;
      manager->schedule_wakeup(window,min_time);
    }
}

/*****************************************************************************/
/* INLINE               kdws_renderer::get_current_time                      */
/*****************************************************************************/

double inline kdws_renderer::get_current_time()
{
  return manager->get_current_time();
}

/*****************************************************************************/
/*                       kdws_renderer::increase_scale                       */
/*****************************************************************************/

float kdws_renderer::increase_scale(float from_scale, bool slightly)
{
  float min_scale = from_scale*1.5F;
  float max_scale = from_scale*3.0F;
  float scale_anchor = from_scale*2.0F;
  if (slightly)
    {
      min_scale = from_scale*1.1F;
      max_scale = from_scale*1.3F;
      scale_anchor = from_scale*1.2F;
    }
  if (max_rendering_scale > 0.0F)
    {
      if (min_scale > max_rendering_scale)
        min_scale = max_rendering_scale;
      if (max_scale > max_rendering_scale)
        max_scale = max_rendering_scale;
    }
  if (compositor == NULL)
    return min_scale;
  kdu_dims region_basis = find_focus_box();
  float new_scale =
    compositor->find_optimal_scale(region_basis,scale_anchor,
                                   min_scale,max_scale);
  if (new_scale < min_scale)
    max_rendering_scale = new_scale; // This is as large as we can go
  return new_scale;
}

/*****************************************************************************/
/*                       kdws_renderer::decrease_scale                       */
/*****************************************************************************/

float kdws_renderer::decrease_scale(float from_scale, bool slightly)
{
  float max_scale = from_scale/1.5F;
  float min_scale = from_scale/3.0F;
  float scale_anchor = from_scale/2.0F;
  if (slightly)
    {
      max_scale = from_scale/1.1F;
      min_scale = from_scale/1.3F;
      scale_anchor = from_scale/1.2F;      
    }
  if (min_rendering_scale > 0.0F)
    {
      if (min_scale < min_rendering_scale)
        min_scale = min_rendering_scale;
      if (max_scale < min_rendering_scale)
        max_scale = min_rendering_scale;
    }
  if (compositor == NULL)
    return max_scale;
  kdu_dims region_basis = find_focus_box();
  float new_scale =
    compositor->find_optimal_scale(region_basis,scale_anchor,
                                   min_scale,max_scale);
  if (new_scale > max_scale)
    min_rendering_scale = new_scale; // This is as small as we can go
  return new_scale;
}

/*****************************************************************************/
/*                      kdws_renderer::change_frame_idx                      */
/*****************************************************************************/

bool kdws_renderer::change_frame_idx(int new_frame_idx)
  /* Note carefully that, on entry to this function, only the `frame_start'
   time can be relied upon.  The function sets `frame_end' from scratch,
   rather than basing the newly calculated value on a previous one. */
{
  if (new_frame_idx < 0)
    new_frame_idx = 0;  
  if (num_frames_known && (new_frame_idx >= num_frames))
    new_frame_idx = num_frames-1;
  if (composition_rules.exists())
    { // Looking for JPX frame
      if (frame.exists())
        this->frame_idx = frame.get_frame_idx(); // Avoid counting errors
      if ((new_frame_idx == frame_idx) && frame.exists())
        return true; // Nothing to do
      if (frame.exists() && ((new_frame_idx-frame_idx) == 1))
        frame = frame.access_next(track_idx,false);
      else if (frame.exists() && ((new_frame_idx-frame_idx) == -1))
        frame = frame.access_prev(track_idx,false);
      else
        frame = composition_rules.access_frame(track_idx,new_frame_idx,false);
      if ((!frame.exists()) && !num_frames_known)
        { // Reassess number of frames and perhaps the track index also
          num_frames_known =
            composition_rules.count_track_frames(track_idx,num_frames);
          if (num_frames_known && (new_frame_idx >= num_frames))
            new_frame_idx = num_frames-1;
          frame = composition_rules.access_frame(track_idx,
                                                 new_frame_idx,false);
          if (!frame)
            return false; // Need more data from JPIP server
        }
      kdu_long start_time, duration;
      frame_idx = new_frame_idx;
      frame.get_info(start_time,duration);
      double inv_timescale = 1.0 / (double) composition_rules.get_timescale();
      frame_start = inv_timescale * (double) start_time;
      frame_end = inv_timescale * (double)(start_time+duration);
    }
  else if (mj2_in.exists() && (track_idx != 0))
    {
      mj2_video_source *track = mj2_in.access_video_track(track_idx);
      if (track == NULL)
        return false;
      frame_idx = new_frame_idx;
      track->seek_to_frame(new_frame_idx);
      frame_start = ((double) track->get_frame_instant()) /
        ((double) track->get_timescale());
      frame_end = frame_start + ((double) track->get_frame_period()) /
        ((double) track->get_timescale());
    }
  return true;
}

/*****************************************************************************/
/*                        kdws_renderer::change_frame                        */
/*****************************************************************************/

void kdws_renderer::change_frame(jpx_frame new_frame)
{
  if ((!composition_rules) || (new_frame == frame) || !new_frame.exists())
    return;
  bool last_in_context=false;
  kdu_uint32 new_track_idx = new_frame.get_track_idx(last_in_context);
  new_track_idx = (new_track_idx==0)?1:new_track_idx;
  if ((track_idx < new_track_idx) ||
      ((track_idx > new_track_idx) && !last_in_context))
    { // Current one not compatible with `new_frame'
      track_idx = new_track_idx;
      num_frames_known = false;
    }
  assert(track_idx > 0);
  if (!num_frames_known)
    num_frames_known =
      composition_rules.count_track_frames(track_idx,num_frames);
  frame = new_frame;
  kdu_long start_time, duration;
  frame_idx = frame.get_frame_idx();
  frame.get_info(start_time,duration);
  double inv_timescale = 1.0 / (double) composition_rules.get_timescale();
  frame_start = inv_timescale * (double) start_time;
  frame_end = inv_timescale * (double)(start_time+duration);
}

/*****************************************************************************/
/*                     kdws_renderer::get_track_duration                     */
/*****************************************************************************/

bool
  kdws_renderer::get_track_duration(double &duration)
{
  duration = 0.0;
  if (composition_rules.exists())
    { 
      assert(track_idx > 0);
      if (!num_frames_known)
        num_frames_known =
          composition_rules.count_track_frames(track_idx,num_frames);
      kdu_long track_time=0;
      bool known=composition_rules.count_track_time(track_idx,track_time);
      double inv_timescale = 1.0 / (double) composition_rules.get_timescale();
      duration = inv_timescale * (double) track_time;
      return known;
    }
  else if (mj2_in.exists())
    { 
      mj2_video_source *track = mj2_in.access_video_track(track_idx);
      if (track == NULL)
        return false;
      duration = (((double) track->get_duration()) /
                  ((double) track->get_timescale()));
      return true;
    }
  return false;
}

/*****************************************************************************/
/*                   kdws_renderer::find_compatible_frame                    */
/*****************************************************************************/

jpx_frame
  kdws_renderer::find_compatible_frame(jpx_metanode numlist, int num_regions,
                                           const jpx_roi *regions,
                                           int &compatible_codestream_idx,
                                           int &compatible_layer_idx,
                                           bool match_all_layers,
                                           bool advance_to_next)
{
  if (!composition_rules)
    composition_rules = jpx_in.access_composition();
  if (!composition_rules)
    return jpx_frame(); // No frames can be accessed.
  
  int num_layers=0, num_streams=0; bool rres;
  if (!numlist.get_numlist_info(num_streams,num_layers,rres))
    return jpx_frame();
  if ((num_layers < 1) && (num_streams < 1))
    return jpx_frame(); // For the moment, we only match compositing layers

  if ((num_regions > 0) || (num_streams > 0) || (num_layers < 2))
    match_all_layers = false;
  
  // Find starting frame from which to scan and set looping limits
  kdu_uint32 scan_track_idx = (track_idx==0)?1:track_idx; // Just in case
  jpx_frame scan_frame = frame;
  if (!scan_frame)
    scan_frame = composition_rules.access_frame(scan_track_idx,frame_idx,
                                                true,false);
  bool can_match_later_tracks = true;
  if (scan_frame.exists())
    { // See if `scan_frame' is the last track in its context; if not, we
      // must freeze the track that we are looking at until we loop back to
      // start from the beginning again.
      scan_frame.get_track_idx(can_match_later_tracks);
      if (advance_to_next)
        scan_frame = scan_frame.access_next(scan_track_idx,true);
    }
  jpx_frame first_matching_frame = jpx_frame();
  while (true)
    { 
      kdu_dims src_dims, tgt_dims; // Dummy variables
      jpx_composited_orientation orient; // Dummy variable
      if (!scan_frame.exists())
        { // Bump the track along
          scan_track_idx++;
          if (!num_tracks_known)
            num_tracks_known = composition_rules.count_tracks(max_track_idx);
          assert(max_track_idx > 0);
          if (scan_track_idx > max_track_idx)
            scan_track_idx = 1;
          can_match_later_tracks = true;
        }
      int find_flags, find_result, matching_inst_idx=0;
      find_flags = (can_match_later_tracks)?JPX_FRAME_MATCH_LATER_TRACKS:0;
      if (match_all_layers)
        find_flags |= JPX_FRAME_MATCH_ALL_LAYERS;
      find_result =
        composition_rules.find_numlist_match(scan_frame,matching_inst_idx,
                                             scan_track_idx,numlist,1,
                                             false,find_flags);
      if ((find_result <= 0) || !scan_frame.exists())
        { 
          if (!scan_frame.exists())
            { 
              assert(can_match_later_tracks);
              if (scan_track_idx <= 1)
                break; // Search was effectively unconstrained yet no match!
              scan_track_idx = 0; // Will cause us to wrap back to track 1
            }
          scan_frame = jpx_frame(); // Forces the track index to advance
          continue;
        }
      if ((scan_frame == this->frame) && advance_to_next)
        { // We've gone around and come back to the same frame we started
          // with, but perhaps with a different track index; in this case,
          // we should proceed to see if we can get a next match with this
          // new track index.
          if (scan_track_idx != this->track_idx)
            { 
              scan_frame = scan_frame.access_next(scan_track_idx,false);
              continue;
            }
        }
      if (!first_matching_frame)
        first_matching_frame = scan_frame;
      else if (first_matching_frame == scan_frame)
        break; // We've been right around once, without finding a frame
               // that matches the other criteria below.
      
      int matching_layer_idx=0;
      if (!scan_frame.get_instruction(matching_inst_idx,matching_layer_idx,
                                      src_dims,tgt_dims,orient))
        { assert(0); break; }
      int matching_rep_idx =
        numlist.get_container_layer_rep(matching_layer_idx);
      
      if (num_streams > 0)
        { // Have to match codestream
          int n, r, cs_idx, l_idx;
          kdu_dims composition_region;
          for (n=0; n < num_streams; n++)
            { 
              cs_idx = numlist.get_numlist_codestream(n,matching_rep_idx);
              if (num_regions > 0)
                { // Have to find visible region
                  for (r=0; r < num_regions; r++)
                    { // Test the `r'th region
                      composition_region = kdu_dims();
                      if ((l_idx =
                           frame_expander.test_codestream_visibility(
                                          &jpx_in,scan_frame,cs_idx,numlist,
                                          NULL,0,composition_region,
                                          regions[r].region)) >= 0)
                        { // We appear to have a match, although it may be that
                          // the above function could not yet access one or
                          // more layers that are required to evaluate the
                          // actual visibility.
                          compatible_codestream_idx = cs_idx;
                          compatible_layer_idx = l_idx;
                          return scan_frame;
                        }
                    }
                }
              else
                { // Any visible portion of the codestream will do
                  composition_region = kdu_dims();
                  if ((l_idx =
                       frame_expander.test_codestream_visibility(
                                      &jpx_in,scan_frame,cs_idx,numlist,
                                      NULL,0,composition_region)) >= 0)
                    { // We appear to have a match
                      compatible_codestream_idx = cs_idx;
                      compatible_layer_idx = l_idx;
                      return scan_frame;
                    }
                }
            }
        }
      else
        { // We already matched all required layers and we have one
          // of the layers in hand as `matching_layer_idx'.
          compatible_layer_idx = matching_layer_idx;
          return scan_frame;
        }

      scan_frame = scan_frame.access_next(scan_track_idx,false);
    }
  
  return jpx_frame(); // Didn't find any match          
}

/*****************************************************************************/
/*                        kdws_renderer::set_rel_focus                       */
/*****************************************************************************/

void kdws_renderer::set_rel_focus(kdu_dims box)
{
  rel_focus.reset(); // Just in case
  if (box.is_empty() || image_dims.is_empty())
    return;
  rel_focus.region.set(box,image_dims);
  kdu_coords focus_point = box.pos;
  focus_point.x += (box.size.x >> 1);
  focus_point.y += (box.size.y >> 1);
  jpx_metanode numlist;
  int numlist_layers=0, numlist_streams=0;
  if (focus_metanode.exists())
    { 
      bool rr; // Dummy variable
      numlist = focus_metanode.get_numlist_container();
      if (numlist.exists())
        numlist.get_numlist_info(numlist_streams,numlist_layers,rr);
    }
  if (single_component_idx >= 0)
    { 
      rel_focus.codestream_idx = single_codestream_idx;
      rel_focus.codestream_region = rel_focus.region;
    }
  else if (jpx_in.exists() && (single_layer_idx >= 0))
    { 
      rel_focus.layer_idx = single_layer_idx;
      rel_focus.layer_region = rel_focus.region;
      jpx_metanode numlist;
      if (focus_metanode.exists())
        numlist = focus_metanode.get_numlist_container();
      kdu_ilayer_ref ilyr = compositor->find_point(focus_point);
      if (ilyr.exists())
        { 
          kdu_istream_ref istr;
          int which=0;
          while ((istr = compositor->get_ilayer_stream(ilyr,which++)).exists())
            { 
              int codestream_idx = -1;
              compositor->get_istream_info(istr,codestream_idx);              
              if ((numlist_streams > 0) &&
                  !numlist.test_numlist_stream(codestream_idx))
                continue; // Look for another one
              kdu_dims cs_dims = compositor->find_istream_region(istr,false);
              if (rel_focus.codestream_region.set(box,cs_dims))
                rel_focus.codestream_idx = codestream_idx;
              break;
            }
        }
    }
  else
    { 
      rel_focus.frame_idx = frame_idx;  rel_focus.frame = frame;
      int ilyr_enum=0;
      kdu_ilayer_ref ilyr;
      while ((ilyr = compositor->find_point(focus_point,ilyr_enum++)).exists())
        { 
          int layer_idx=-1, cs=-1; bool opq=false;
          compositor->get_ilayer_info(ilyr,layer_idx,cs,opq);
          if ((numlist_layers > 0) && !numlist.test_numlist_layer(layer_idx))
            continue; // Look for a compatible layer

          int which=0;
          kdu_istream_ref istr;
          while ((istr = compositor->get_ilayer_stream(ilyr,which++)).exists())
            { 
              int codestream_idx = -1;
              compositor->get_istream_info(istr,codestream_idx);              
              if ((numlist_streams > 0) &&
                  !numlist.test_numlist_stream(codestream_idx))
                continue; // Look for a compatible codestream
              kdu_dims cs_dims=compositor->find_istream_region(istr,false);
              if (rel_focus.codestream_region.set(box,cs_dims))
                rel_focus.codestream_idx = codestream_idx;
              break;
            }
          if (istr.exists())
            { 
              kdu_dims l_dims=compositor->find_ilayer_region(ilyr,false);
              if (rel_focus.layer_region.set(box,l_dims))
                rel_focus.layer_idx = layer_idx;
              break;
            }
        }
    }
}

/*****************************************************************************/
/*                      kdws_renderer::adjust_rel_focus                      */
/*****************************************************************************/

bool kdws_renderer::adjust_rel_focus()
{
  if (rel_focus.animator_driven)
    return false; // Adjustments not allowed until this condition is removed.
  if (image_dims.is_empty() ||
      !(rel_focus.is_valid() && configuration_complete))
    return false;

  kdu_istream_ref istr;
  kdu_ilayer_ref ilyr;
  
  if (single_component_idx >= 0)
    {
      if (rel_focus.codestream_idx == single_codestream_idx)
        { // We have a focus region expressed directly for this codestream
          if ((rel_focus.frame_idx < 0) && (rel_focus.layer_idx < 0))
            return false; // No change; focus was already on single codestream
          rel_focus.frame_idx = rel_focus.layer_idx = -1;
          rel_focus.frame = jpx_frame();
          rel_focus.region = rel_focus.codestream_region;
          return true; // May require remapping of the actual focus box
        }
    }
  else if (jpx_in.exists() && (single_layer_idx >= 0))
    {
      kdu_dims region, rel_dims;
      if (rel_focus.layer_idx == single_layer_idx)
        { // We have a focus region expressed directly for this layer
          if (rel_focus.frame_idx < 0)
            return false; // No change; focus was already on single layer
          rel_focus.frame_idx = -1;  rel_focus.frame = jpx_frame();
          rel_focus.region = rel_focus.layer_region;
          return true;
        }
      else if ((rel_focus.codestream_idx >= 0) &&
               ((istr=compositor->get_next_istream(kdu_istream_ref(),
                          true,false,rel_focus.codestream_idx)).exists()) &&
               !(rel_dims=compositor->find_istream_region(istr,
                                                          false)).is_empty())
        { 
          rel_focus.frame_idx = -1;  rel_focus.frame = jpx_frame();
          region=rel_focus.codestream_region.apply_to_container(rel_dims);
          rel_focus.region.set(region,image_dims);
          return true;
        }
    }
  else
    { // In JPX frame composition (or MJ2 frame display) mode
      kdu_dims region, rel_dims;
      
      if ((rel_focus.frame_idx == frame_idx) && (rel_focus.frame == frame))
        return false; // Nothing to change
      
      int old_layer_idx = rel_focus.layer_idx;
      if ((rel_focus.layer_idx >= 0) &&
          ((ilyr=compositor->get_next_ilayer(kdu_ilayer_ref(),
                                             rel_focus.layer_idx)).exists()) &&
          !(rel_dims =
            compositor->find_ilayer_region(ilyr,false)).is_empty())
        {
          region = rel_focus.layer_region.apply_to_container(rel_dims);
          rel_focus.frame_idx = frame_idx;  rel_focus.frame = frame;
          rel_focus.region.set(region,image_dims);
        }
      else
        rel_focus.layer_idx = -1;

      int old_codestream_idx = rel_focus.codestream_idx;
      if ((rel_focus.codestream_idx >= 0) &&
          ((istr =
            compositor->get_next_istream(kdu_istream_ref(),true,false,
                                         rel_focus.codestream_idx)).exists())&&
          !(rel_dims=compositor->find_istream_region(istr,
                                                     false)).is_empty())
        { 
          region = rel_focus.codestream_region.apply_to_container(rel_dims);
          rel_focus.frame_idx = frame_idx;  rel_focus.frame = frame;
          rel_focus.region.set(region,image_dims);
        }
      else
        rel_focus.codestream_idx = -1;
      
      if ((!focus_metanode) ||
          (((old_layer_idx >= 0) || (old_codestream_idx >= 0)) &&
           (rel_focus.layer_idx == old_layer_idx) &&
           (rel_focus.codestream_idx == old_codestream_idx)))
        return true; // We can still keep the focus box
    }
  
  // If we get here, we can't transform the focus box into anything compatible
  rel_focus.reset();
  focus_metanode = jpx_metanode();
  return true;
}

/*****************************************************************************/
/*                  kdws_renderer::calculate_rel_view_params                 */
/*****************************************************************************/

void kdws_renderer::calculate_rel_view_params()
{
  if ((working_buffer == NULL) || !image_dims)
    return;
  view_centre_known = true;
  view_centre_x =
    (float)(view_dims.pos.x + view_dims.size.x/2 - image_dims.pos.x) /
    ((float) image_dims.size.x);
  view_centre_y =
    (float)(view_dims.pos.y + view_dims.size.y/2 - image_dims.pos.y) /
    ((float) image_dims.size.y);
}

/*****************************************************************************/
/*                  kdws_renderer::scroll_to_view_anchors                    */
/*****************************************************************************/

void kdws_renderer::scroll_to_view_anchors()
{
  if (!view_centre_known)
    return;
  kdu_coords new_view_origin;
  new_view_origin.x = (int) floor(image_dims.size.x*view_centre_x -
                                  0.5*view_dims.size.x);
  new_view_origin.y = (int) floor(image_dims.size.y*view_centre_y -
                                  0.5*view_dims.size.y);
  if (new_view_origin.x < 0)
    new_view_origin.x = 0;
  if (new_view_origin.y < 0)
    new_view_origin.y = 0;
  view_centre_known = false;
  set_scroll_pos(new_view_origin.x,new_view_origin.y);
}

/*****************************************************************************/
/*                       kdws_renderer::find_focus_box                       */
/*****************************************************************************/

kdu_dims kdws_renderer::find_focus_box(bool allow_box_shift)
{
  if (image_dims.is_empty() || view_dims.is_empty())
    return kdu_dims();
  if (!rel_focus.is_valid())
    return view_dims;
  kdu_dims focus_box = rel_focus.region.apply_to_container(image_dims);
  if (!rel_focus.animator_driven)
    { 
      if (allow_box_shift)
        {   // Adjust box to fit into view port.
          if (focus_box.pos.x < view_dims.pos.x)
            focus_box.pos.x = view_dims.pos.x;
          if ((focus_box.pos.x + focus_box.size.x) >
              (view_dims.pos.x + view_dims.size.x))
            focus_box.pos.x=view_dims.pos.x+view_dims.size.x-focus_box.size.x;
          if (focus_box.pos.y < view_dims.pos.y)
            focus_box.pos.y = view_dims.pos.y;
          if ((focus_box.pos.y + focus_box.size.y) >
              (view_dims.pos.y + view_dims.size.y))
            focus_box.pos.y=view_dims.pos.y+view_dims.size.y-focus_box.size.y;
        }
      focus_box &= view_dims;
    }
  return focus_box;
}

/*****************************************************************************/
/*                      kdws_renderer::set_focus_box                         */
/*****************************************************************************/

void kdws_renderer::set_focus_box(kdu_dims new_box, jpx_metanode node)
{
  if (rel_focus.animator_driven)
    return; // Adjustments not allowed until this condition is removed
  if ((new_box.area() < (kdu_long) 100) && !node)
    { new_box.size.x = new_box.size.y = 0; node =jpx_metanode(); }
  this->focus_metanode = node;
  rel_focus.reset();
  if ((new_box != view_dims) && !new_box.is_empty())
    set_rel_focus(new_box);
  if ((compositor == NULL) || (working_buffer == NULL))
    return;
  
  int focus_state = 0;
  kdu_dims old_box;
  if (rel_focus.is_valid())
    { 
      focus_state = (focus_metanode.exists())?-1:1;
      old_box = new_box = find_focus_box();
    }
  else
    new_box = old_box; // Send an empty box if no focus enabled
  if (compositor->set_focus_box(old_box,focus_state) && (animator == NULL))
    { 
      if ((focus_state == 0) || old_box.is_empty())
        compositor->invalidate_rect(view_dims);
      else
        { 
          for (int w=0; w < 2; w++)
            { 
              kdu_dims box = (w==0)?old_box:new_box;
              if (box.is_empty())
                continue;
              box.pos.x  -= KDWS_FOCUS_MARGIN;
              box.pos.y  -= KDWS_FOCUS_MARGIN;
              box.size.x += 2*KDWS_FOCUS_MARGIN;
              box.size.y += 2*KDWS_FOCUS_MARGIN;
              compositor->invalidate_rect(box);
            }
        }
    }
  update_client_window_of_interest();
  display_status();
}

/*****************************************************************************/
/*                     kdws_renderer::displace_focus_box                     */
/*****************************************************************************/

kdu_coords kdws_renderer::displace_focus_box(kdu_coords shift)
{
  if (view_dims.is_empty() || image_dims.is_empty() ||
      focus_metanode.exists() || !rel_focus.is_valid())
    return kdu_coords();
  kdu_dims old_box = find_focus_box();
  old_box &= image_dims; // Just for good measure
  kdu_coords new_min = old_box.pos + shift;
  kdu_coords new_lim = new_min + old_box.size;
  kdu_coords image_lim = image_dims.pos + image_dims.size;
  int tmp;
  if ((tmp = new_min.x) < 0)
    { new_min.x -= tmp;  new_lim.x -= tmp;  shift.x -= tmp; }
  if ((tmp = new_lim.x - image_lim.x) > 0)
    { new_min.x -= tmp;  new_lim.x -= tmp;  shift.x -= tmp; }
  if ((tmp = new_min.y) < 0)
    { new_min.y -= tmp;  new_lim.y -= tmp;  shift.y -= tmp; }
  if ((tmp = new_lim.y - image_lim.y) > 0)
    { new_min.y -= tmp;  new_lim.y -= tmp;  shift.y -= tmp; }
  kdu_dims new_box;
  new_box.pos = new_min;
  new_box.size = new_lim - new_min;
  if (new_box == old_box)
    return kdu_coords();
  if (!(view_dims.contains(new_box)))
    { // Scroll the view by as much as `shift' -- less if this will allow
      // the box to be fully contained in the view.
      kdu_coords scroll;
      kdu_coords view_min = view_dims.pos;
      kdu_coords view_lim = view_min + view_dims.size;
      if ((shift.x < 0) && ((tmp = new_min.x-view_min.x) < 0))
        scroll.x = (tmp < shift.x)?shift.x:tmp;
      else if ((shift.x > 0) && ((tmp = new_lim.x-view_lim.x) > 0))
        scroll.x = (tmp > shift.x)?shift.x:tmp;
      if ((shift.y < 0) && ((tmp = new_min.y-view_min.y) < 0))
        scroll.y = (tmp < shift.y)?shift.y:tmp;
      else if ((shift.y > 0) && ((tmp = new_lim.y-view_lim.y) > 0))
        scroll.y = (tmp > shift.y)?shift.y:tmp;
      if (scroll.x || scroll.y)
        set_scroll_pos(scroll.x,scroll.y,true);
      shift -= scroll;
    }
  rel_focus.reset();
  set_focus_box(new_box,jpx_metanode());
  return shift;
}

/*****************************************************************************/
/*                      kdws_renderer::update_focus_box                      */
/*****************************************************************************/

void kdws_renderer::update_focus_box(bool view_may_be_scrolling)
{
  if (rel_focus.animator_driven)
    return; // Adjustments not allowed until this condition is removed
  int focus_state = 0;
  kdu_dims focus_box;
  if (rel_focus.is_valid())
    { 
      focus_box = find_focus_box();
      if (focus_metanode.exists())
        { // Don't allow focus box to shift
          focus_state = -1;
          if (focus_box.is_empty())
            { 
              focus_state = 0;
              rel_focus.reset();
              focus_metanode = jpx_metanode();
            }
        }
      else
        { // Allow the focus box to shift around
          focus_state = 1;
          kdu_dims adj_focus_box = find_focus_box(true);
          if (adj_focus_box != focus_box)
            { // Shift the focus box
              rel_focus.reset();
              if ((adj_focus_box != view_dims) || !adj_focus_box.is_empty())
                { 
                  set_rel_focus(adj_focus_box);
                  focus_box = find_focus_box();
                }
              else
                focus_state = 0;
            }
        }
      if (focus_state == 0)
        focus_box = kdu_dims();
      else if (focus_box == view_dims)
        { 
          focus_state = 0;
          focus_box = kdu_dims();
        }
    }
  kdu_dims old_box = focus_box; // Because function below overwrites `old_box'
  if ((compositor != NULL) &&
      compositor->set_focus_box(old_box,focus_state) && (animator == NULL))
    { // Something has changed
      if ((focus_state == 0) || old_box.is_empty())
        compositor->invalidate_rect(view_dims);
      else
        { 
          for (int w=0; w < 2; w++)
            { 
              kdu_dims box = (w==0)?old_box:focus_box;
              if (box.is_empty())
                continue;
              box.pos.x  -= KDWS_FOCUS_MARGIN;
              box.pos.y  -= KDWS_FOCUS_MARGIN;
              box.size.x += 2*KDWS_FOCUS_MARGIN;
              box.size.y += 2*KDWS_FOCUS_MARGIN;
              compositor->invalidate_rect(box);
            }
        }
    }
  update_client_window_of_interest();
}

/*****************************************************************************/
/*            kdws_renderer::check_initial_request_compatibility             */
/*****************************************************************************/

bool kdws_renderer::check_initial_request_compatibility()
{
  assert((jpip_client != NULL) && respect_initial_jpip_request);
  if ((!jpx_in.exists()) || (single_component_idx >= 0))
    return true;
  if (!jpip_client->get_window_in_progress(&tmp_roi,client_request_queue_id))
    return true; // Window information not available yet
  
  int c;
  kdu_sampled_range *rg;
  bool compatible = true;
  if ((single_component_idx < 0) && (single_layer_idx < 0) &&
      (frame_idx == 0) && frame.exists())
    { // Check compatibility of default animation frame
      int m, num_members = frame_expander.get_num_members();
      for (m=0; m < num_members; m++)
        { 
          int instruction_idx, layer_idx;
          bool covers_composition;
          kdu_dims source_dims, target_dims;
          jpx_composited_orientation orientation;
          int remapping_ids[2];
          instruction_idx =
            frame_expander.get_member(m,layer_idx,covers_composition,
                                      source_dims,target_dims,orientation);
          frame.get_original_iset(instruction_idx,
                                  remapping_ids[0],remapping_ids[1]);
          for (c=0; (rg=tmp_roi.contexts.access_range(c)) != NULL; c++)
            if ((rg->from <= layer_idx) && (rg->to >= layer_idx) &&
                (((layer_idx-rg->from) % rg->step) == 0))
              {
                if (covers_composition &&
                    (rg->remapping_ids[0] < 0) && (rg->remapping_ids[1] < 0))
                  break; // Found compatible layer
                if ((rg->remapping_ids[0] == remapping_ids[0]) &&
                    (rg->remapping_ids[1] == remapping_ids[1]))
                  break; // Found compatible layer
              }
          if (rg == NULL)
            { // No appropriate context request for layer; see if it needs one
              if (covers_composition && (layer_idx == 0) &&
                  jpx_in.access_compatibility().is_jp2_compatible())
                {
                  for (c=0;
                       (rg = tmp_roi.codestreams.access_range(c)) != NULL;
                       c++)
                    if (rg->from == 0)
                      break;
                }
            }
          if (rg == NULL)
            { // No response for this frame layer; must downgrade
              compatible = false;
              frame_start = frame_end = 0.0;
              frame = jpx_frame();
              single_layer_idx = 0; // Try this one
              break;
            }
        }
    }
  
  if (compatible)
    return true;
  
  bool have_codestream0 = false;
  for (c=0; (rg=tmp_roi.codestreams.access_range(c)) != NULL; c++)
    {
      if (rg->from == 0)
        have_codestream0 = true;
      if (rg->context_type == KDU_JPIP_CONTEXT_TRANSLATED)
        { // Use the compositing layer translated here
          int range_idx = rg->remapping_ids[0];
          int member_idx = rg->remapping_ids[1];
          rg = tmp_roi.contexts.access_range(range_idx);
          if (rg == NULL)
            continue;
          single_layer_idx = rg->from + rg->step*member_idx;
          if (single_layer_idx > rg->to)
            continue;
          return false; // Found a suitable compositing layer
        }
    }
  
  if (have_codestream0 && jpx_in.access_compatibility().is_jp2_compatible())
    {
      single_layer_idx = 0;
      return false; // Compositing layer 0 is suitable
    }
  
  // If we get here, we could not find a compatible layer
  single_layer_idx = -1;
  single_component_idx = 0;
  if ((rg=tmp_roi.codestreams.access_range(0)) != NULL)
    single_codestream_idx = rg->from;
  else
    single_codestream_idx = 0;

  return compatible;
}

/*****************************************************************************/
/*              kdws_renderer::update_client_window_of_interest              */
/*****************************************************************************/

void
  kdws_renderer::update_client_window_of_interest(double cur_time,
                                                  double next_disp_event_time)
{
  if ((compositor == NULL) || (jpip_client == NULL) ||
      (!jpip_client->is_alive(client_request_queue_id)) ||
      respect_initial_jpip_request || !enable_region_posting)
    return;
  
  if ((animator != NULL) && (next_disp_event_time < 0.0))
    compositor->get_display_event_times(next_disp_event_time);
  try {
    // Start by introduce requests for required metadata
    tmp_roi.init();
    tmp_roi.set_metadata_only(true); // The `animator' might change this for us
    animator_has_oob_metareqs = false;
    catalog_has_oob_metareqs = false;
    have_novel_anchor_metareqs = false;
    if (animator != NULL)
      { // Ask for metadata which may be required for the animator to figure
        // out its future (or imminent) course of action.  This includes
        // metadata required to expand missing elements from a current
        // metadata-driven animation, as well as basic JPX header metadata
        // for imminent frames.
        cur_time = (cur_time < 0.0)?get_current_time():cur_time;
        double request_rtt=0.5;
        jpip_client->get_timing_info(client_request_queue_id,&request_rtt);
        if (request_rtt < 0.5) request_rtt = 0.5;
        double time_limit = next_disp_event_time + 1.0 + 2.0*request_rtt;
        if (animator->generate_metadata_requests(&tmp_roi,time_limit,
                                                 next_disp_event_time) > 0)
          { // See if the animator has issued any metadata requests which could
            // involve the delivery of new data
            if (!oob_metareqs.metareq_contains(tmp_roi))
              animator_has_oob_metareqs = true;
            else
              { 
                int status_flags = 0;
                if (!(jpip_client->get_oob_window_in_progress(NULL,
                                   client_request_queue_id,&status_flags) &&
                      (status_flags & KDU_CLIENT_WINDOW_IS_COMPLETE)))
                  animator_has_oob_metareqs = true;
              }
          }
      }
    else if (catalog_source != NULL)
      { // Ask for metadata which may have no connection to the currently
        // requested image window, using KDU_MRQ_ALL.
        if (catalog_source->generate_client_requests(&tmp_roi) > 0)
          { // See if the catalog has issued any metadata requests which could
            // involve the delivery of new data
            if (!oob_metareqs.metareq_contains(tmp_roi))
              catalog_has_oob_metareqs = true;
            else
              { 
                int status_flags = 0;
                if (!(jpip_client->get_oob_window_in_progress(NULL,
                                   client_request_queue_id,&status_flags) &&
                      (status_flags & KDU_CLIENT_WINDOW_IS_COMPLETE)))
                  catalog_has_oob_metareqs = true;
              }
          }
      }
    if (catalog_has_oob_metareqs || animator_has_oob_metareqs)
      { // Decide how to send these
        oob_metareqs.copy_from(tmp_roi);
        jpip_client->post_oob_window(&tmp_roi,
                                     client_request_queue_id,true);
          // Use regular request queue-id as the caller-id for OOB requests
      }
    else
      oob_metareqs.init();

    // Figure out the region (on the compositor's buffer surface) that is
    // currently of interest to us.
    kdu_dims roi_region;
    roi_region = find_focus_box();
    
    // Now form the main imagery request(s)
    if (animator != NULL)
      { 
        kdu_dims viewport = view_dims;
        roi_region.from_apparent(orientation.trans,
                                 orientation.vf,orientation.hf);
        viewport.from_apparent(orientation.trans,
                               orientation.vf,orientation.hf);
        if (rel_focus.animator_driven)
          roi_region = kdu_dims(); // No user-defined focus box
        animator->generate_imagery_requests(jpip_client,
                                            client_request_queue_id,
                                            next_disp_event_time,viewport,
                                            roi_region,rendering_scale,
                                            max_display_layers);
        return;
      }
    
    if (roi_region.is_empty())
      roi_region = view_dims;
    tmp_roi.init();

    // The situation here can be rather complex due to the fact that we
    // might not yet have enough information about headers that are
    // required to determine for sure how the image window of interest
    // should be best expressed.  Fortunately, we don't generally need to
    // worry about such things during animation because the animator asks
    // for the necessary metadata ahead of time.
      
    // Begin by trying to find the location, size and resolution of the
    // view window.  We might have to guess at these parameters here if we
    // do not currently have an open image.
    int round_direction=0;
    kdu_coords woi_size; // Full-size used to express the WOI
    kdu_dims woi_dims; // Expresses the location and dimensions of the WOI
  
    if (!compositor->find_compatible_jpip_window(woi_size,woi_dims,
                                                 round_direction,
                                                 roi_region))
      { // Need to guess an appropriate resolution for the request
        if (single_component_idx >= 0)
          { // Use size of the codestream, if we can determine it
            if (!jpx_in.exists())
              { 
                kdu_istream_ref istr;
                istr = compositor->get_next_istream(istr);
                if (istr.exists())
                  { 
                    kdu_dims dims;
                    compositor->access_codestream(istr).get_dims(-1,dims);
                    woi_size = dims.size;
                  }
              }
            else
              { 
                jpx_codestream_source stream =
                jpx_in.access_codestream(single_codestream_idx,false);
                if (stream.exists())
                  woi_size = stream.access_dimensions().get_size();
              }
          }
        else if (single_layer_idx >= 0)
          { // Use size of compositing layer, if we can evaluate it
            if (!jpx_in.exists())
              {  
                kdu_istream_ref istr;
                istr = compositor->get_next_istream(istr);
                if (istr.exists())
                  { 
                    kdu_dims dims;
                    compositor->access_codestream(istr).get_dims(-1,dims);
                    woi_size = dims.size;
                  }
              }
            else
              { 
                jpx_layer_source layer =
                  jpx_in.access_layer(single_layer_idx);
                if (layer.exists())
                  woi_size = layer.get_layer_size();
              }
          }
        else
          { // Use size of composition surface, if we can find it
            if (composition_rules.exists())
              composition_rules.get_global_info(woi_size);
          }
        woi_size.x = (int) ceil(woi_size.x * rendering_scale);
        woi_size.y = (int) ceil(woi_size.y * rendering_scale);
          
        if (rel_focus.is_valid())
          { 
            kdu_dims res_dims; res_dims.size = woi_size;
            res_dims.to_apparent(orientation.trans,
                                 orientation.vf,orientation.hf);
            woi_dims = rel_focus.region.apply_to_container(res_dims);
            woi_dims.from_apparent(orientation.trans,
                                   orientation.vf,orientation.hf);
          }
        else
          woi_dims.size = woi_size;
      }

    if (woi_dims.is_empty())
      { 
        woi_dims.pos = kdu_coords(0,0);
        woi_dims.size = woi_size;
      }
    else
      { 
        if (woi_dims.size.x > woi_size.x)
          woi_dims.size.x = woi_size.x;
        if (woi_dims.size.y > woi_size.y)
          woi_dims.size.y = woi_size.y;
      }
        
    // Now, we have as much information as we can guess concerning the
    // intended window of interest.  Let's use it to create a window
    // request.
    tmp_roi.resolution = woi_size;
    tmp_roi.region = woi_dims;
    tmp_roi.round_direction = round_direction;
        
    if (max_display_layers < (1<<16))
      { 
        int max_layers = compositor->get_max_available_quality_layers();
        if ((max_layers <= 1) || (max_display_layers < max_layers))
          tmp_roi.max_layers = max_display_layers;
      }
        
    if (single_component_idx >= 0)
      { 
        tmp_roi.components.add(single_component_idx);
        tmp_roi.codestreams.add(single_codestream_idx);
      }
    else if (single_layer_idx >= 0)
      { // single compositing layer
        if ((single_layer_idx == 0) &&
            ((!jpx_in) ||
             jpx_in.access_compatibility().is_jp2_compatible()))
          tmp_roi.codestreams.add(0); // Maximize chance of success even if
                                      // server cannot translate contexts
        kdu_sampled_range rg;
        rg.from = rg.to = single_layer_idx;
        rg.context_type = KDU_JPIP_CONTEXT_JPXL;
        tmp_roi.contexts.add(rg);
      }
    else if (frame.exists())
      { // Request whole animation frame
        kdu_dims scaled_woi_dims;
        if (configuration_complete && (!pending_show_metanode) &&
            !woi_dims.is_empty())
          { 
            kdu_coords comp_size;
            composition_rules.get_global_info(comp_size);
            double scale_x=((double) comp_size.x)/((double) woi_size.x);
            double scale_y=((double) comp_size.y)/((double) woi_size.y);
            kdu_coords min = woi_dims.pos;
            kdu_coords lim = min + woi_dims.size;
            min.x = (int) floor(scale_x * min.x);
            lim.x = (int) ceil(scale_x * lim.x);
            min.y = (int) floor(scale_y * min.y);
            lim.y = (int) ceil(scale_y * lim.y);
            scaled_woi_dims.pos = min;
            scaled_woi_dims.size = lim - min;
          }
        kdu_sampled_range rg;
        frame_expander.construct(&jpx_in,frame,scaled_woi_dims);
        int m, num_members = frame_expander.get_num_members();
        for (m=0; m < num_members; m++)
          { 
            bool covers_composition;
            kdu_dims source_dims, target_dims;
            jpx_composited_orientation orientation;
            int layer_idx, instruction_idx =
            frame_expander.get_member(m,layer_idx,covers_composition,
                                      source_dims,target_dims,orientation);
            rg.from = rg.to = layer_idx;
            rg.context_type = KDU_JPIP_CONTEXT_JPXL;
            if (covers_composition)
              { 
                if ((layer_idx == 0) && (num_members == 1) &&
                    jpx_in.access_compatibility().is_jp2_compatible())
                  tmp_roi.codestreams.add(0); // Maximize chance server will
                                              // respond correctly, even if it
                                              // can't translate contexts
                rg.remapping_ids[0] = rg.remapping_ids[1] = -1;
              }
            else
              frame.get_original_iset(instruction_idx,rg.remapping_ids[0],
                                      rg.remapping_ids[1]);
            tmp_roi.contexts.add(rg);
          }
      }
    jpx_in.generate_metareq(&tmp_roi);
    
    // Finish up by adding any image window dependent metadata requests.
    jpx_meta_manager meta_manager;
    if (jpx_in.exists() &&
        (meta_manager=jpx_in.access_meta_manager()).exists())
      { 
        tmp_roi.add_metareq(jp2_roi_description_4cc,KDU_MRQ_WINDOW);
        tmp_roi.add_metareq(jp2_number_list_4cc,KDU_MRQ_STREAM,true);
        jpx_metanode root_node = meta_manager.access_root();
        kdu_uint32 box_types[4]={ jp2_label_4cc, jp2_cross_reference_4cc,
                                  jp2_xml_4cc, jp2_uuid_4cc };
        if (root_node.generate_metareq(&tmp_roi,4,box_types,0,NULL,true,
                                       0,KDU_MRQ_GLOBAL) > 0)
          have_novel_anchor_metareqs = true;
        if (compositor->generate_metareq(&tmp_roi,
                                         KDU_MRQ_STREAM|KDU_MRQ_WINDOW,
                                         roi_region,4,box_types,0,NULL,
                                         true) > 0)
          have_novel_anchor_metareqs = true;
          // Note: the above calls look for:
          // 1) All semantic top-level labels and links;
          // 2) All labels and links that are directly descended from numlists
          //    relevant to the current ROI on the composited image; and
          // 3) All labels and links that are directly descended from regions
          //    relevant to the current ROI on the composited image.
          // These are all the things that are used to build the top level of
          // the metadata catalog.  The catalog itself may generate its
          // own metadata requests based on user navigation and interests and
          // these metadata requests will also include descendent numlists and
          // ROI nodes as being of interest, even if they are not related to
          // the current composition or view window.  This allows the user to
          // recover references to other imagery via metadata and to use this
          // to drive navigation within the image.
      
        if (have_novel_anchor_metareqs &&
            client_roi.metareq_contains(tmp_roi) &&
            jpip_client->is_idle(client_request_queue_id))
          have_novel_anchor_metareqs = false;
      }

    // Post the window
    if ((animator != NULL) || (roi_region == view_dims))
      client_prefs.set_pref(KDU_WINDOW_PREF_FULL);
    else
      client_prefs.set_pref(KDU_WINDOW_PREF_PROGRESSIVE);
    if (jpip_client->post_window(&tmp_roi,client_request_queue_id,true,
                                 &client_prefs))
      client_roi.copy_from(tmp_roi);
  }
  catch (kdu_exception) {
    return;
  }
  catch (std::bad_alloc) {
    close_file();
  }
}

/*****************************************************************************/
/*                     kdws_renderer::change_client_focus                    */
/*****************************************************************************/

void
  kdws_renderer::change_client_focus(kdu_coords actual_resolution,
                                     kdu_dims actual_region)
{
  if (rel_focus.animator_driven)
    return; // Adjustments not allowed until this condition is removed
  
  // Find the relative location and dimensions of the new focus box
  rel_focus.reset();
  focus_metanode = jpx_metanode();
  if ((actual_resolution.x > 0) && (actual_resolution.y > 0) &&
      !actual_region.is_empty())
    { 
      rel_focus.region.set(actual_region,actual_resolution);
      if (orientation.trans)
        rel_focus.region.transpose();
      if (orientation.hf)
        rel_focus.region.hflip();
      if (orientation.vf)
        rel_focus.region.vflip();
      if (single_component_idx >= 0)
        { rel_focus.codestream_idx = single_codestream_idx;
          rel_focus.codestream_region = rel_focus.region; }
      else if (jpx_in.exists() && (single_layer_idx >= 0))
        { rel_focus.layer_idx = single_layer_idx;
          rel_focus.layer_region = rel_focus.region; }
      else
        { rel_focus.frame_idx = frame_idx;
          rel_focus.frame = frame; }
    }
  
  // Draw the focus box from its relative coordinates.
  bool posting_state = enable_region_posting;
  enable_region_posting = false;
  update_focus_box();
  enable_region_posting = posting_state;
}

/*****************************************************************************/
/*                        kdws_renderer::set_codestream                      */
/*****************************************************************************/

void
  kdws_renderer::set_codestream(int codestream_idx)
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  calculate_rel_view_params();
  single_component_idx = 0;
  single_codestream_idx = codestream_idx;
  single_layer_idx = -1;
  frame = jpx_frame();
  adjust_rel_focus();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  invalidate_configuration();
  initialize_regions(false);
}

/*****************************************************************************/
/*                    kdws_renderer::set_compositing_layer                   */
/*****************************************************************************/

void
  kdws_renderer::set_compositing_layer(int layer_idx)
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (animator == NULL)
    calculate_rel_view_params();
  single_component_idx = -1;
  single_layer_idx = layer_idx;
  frame = jpx_frame();
  adjust_rel_focus();
  if (animator == NULL)
    { // Centre the view around any focus box of interest -- however, if
      // we are animating through the compositing layers, this will seriously
      // mess with any interactive panning attempts by the user.
      rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
    }
  invalidate_configuration();
  initialize_regions(false);
}

/*****************************************************************************/
/*                    kdws_renderer::set_compositing_layer                   */
/*****************************************************************************/

void
  kdws_renderer::set_compositing_layer(kdu_coords point)
{
  if (compositor == NULL)
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  point += view_dims.pos;
  kdu_ilayer_ref ilyr = compositor->find_point(point);
  int layer_idx, cs_idx; bool is_opaque;
  if ((compositor->get_ilayer_info(ilyr,layer_idx,cs_idx,is_opaque) > 0) &&
      (layer_idx >= 0))
    set_compositing_layer(layer_idx);
}

/*****************************************************************************/
/*                         kdws_renderer::set_frame                          */
/*****************************************************************************/

bool kdws_renderer::set_frame(int new_frame_idx, double cur_time)
{
  if ((compositor == NULL) ||
      (single_component_idx >= 0) ||
      ((single_layer_idx >= 0) && jpx_in.exists()))
    return false;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return false; }
  int old_frame_idx = this->frame_idx;
  jpx_frame old_frame = this->frame;
  change_frame_idx(new_frame_idx);
  if ((frame_idx == old_frame_idx) && (frame == old_frame))
    return false;
  
  try {
    if ((working_buffer != NULL) && mj2_in.exists())
      { 
        kdu_ilayer_ref ilyr=compositor->get_next_ilayer(kdu_ilayer_ref());
        compositor->change_ilayer_frame(ilyr,frame_idx);
        kdu_dims vport = size_buffer_for_target_view(view_dims,false,false);
        compositor->set_buffer_surface(vport);
        working_buffer =
          compositor->get_composition_buffer_ex(working_buffer_dims,true);
        if (working_buffer != NULL)
          working_buffer->set_image_dims(image_dims,orientation);
        if (animator == NULL)
          update_animation_bar_pos();
      }
    else if ((working_buffer != NULL) && frame.exists() &&
             frame_expander.construct(&jpx_in,frame) &&
             (frame_expander.get_num_members() > 0))
      { // Composit a new JPX frame
        compositor->set_frame(&frame_expander);
        kdu_dims tmp_image_dims;
        if ((!compositor->get_total_composition_dims(tmp_image_dims)) ||
            (tmp_image_dims != image_dims))
          { // Weird condition; better call `initialize_regions'
            if (rel_focus.is_valid())
              { 
                rel_focus.frame_idx = old_frame_idx;
                rel_focus.frame = old_frame;
              }
            invalidate_configuration();
            initialize_regions(false);
            return true;
          }
        int inactive_layers_to_cache = (animator != NULL)?1:3;
        compositor->cull_inactive_ilayers(inactive_layers_to_cache);
        kdu_region_animator_roi animator_roi;
        if (animator != NULL)
          { 
            kdu_dims vport = view_dims;
            animator->get_roi_and_mod_viewport(compositor,animator_roi,vport);
            vport = size_buffer_for_target_view(vport,false,false);
            compositor->set_buffer_surface(vport);
          }
        working_buffer =
          compositor->get_composition_buffer_ex(working_buffer_dims,true);
        if (working_buffer != NULL)
          { 
            working_buffer->set_image_dims(image_dims,orientation);
            working_buffer->set_roi_info(animator_roi);
          }
        if (rel_focus.is_valid())
          { 
            if (adjust_rel_focus())
              { // Need to update focus box at least
                if ((animator == NULL) && rel_focus.is_valid() &&
                    rel_focus.get_centre_if_valid(view_centre_x,view_centre_y))
                  { 
                    view_centre_known = true;
                    scroll_to_view_anchors();
                    view_centre_known = false;
                  }
                update_focus_box(false);
              }
          }
        if (animator == NULL)
          update_client_window_of_interest(cur_time);
        if (overlays_enabled && (animator == NULL))
          { 
            jpx_metanode node;
            if (catalog_source &&
                (node=catalog_source->get_selected_metanode()).exists())
              highlight_imagery_for_metanode(node);
            else if (overlay_highlight_state || overlay_flashing_state)
              { 
                overlay_highlight_state = 0;
                overlay_flashing_state = 1;
                configure_overlays(-1.0,false);
              }
          }
        perform_any_outstanding_render_refresh(cur_time);
        if (animator == NULL)
          update_animation_bar_pos();
      }
    else
      { // Have to initialize configuration from scratch
        if (rel_focus.is_valid())
          { 
            rel_focus.frame_idx = old_frame_idx;
            rel_focus.frame = old_frame;
          }
        invalidate_configuration();
        initialize_regions(false);
      }
  }
  catch (kdu_exception) {
    return false;
  }
  catch(std::bad_alloc) {
    close_file();
    return false;
  }
  return true;
}

/*****************************************************************************/
/*                        kdws_renderer::set_frame                           */
/*****************************************************************************/

bool kdws_renderer::set_frame(jpx_frame new_frame, double cur_time)
{
  if ((compositor == NULL) || (single_component_idx >= 0) ||
      (single_layer_idx >= 0))
    return false;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return false; }
  int old_frame_idx = this->frame_idx;
  jpx_frame old_frame = this->frame;
  change_frame(new_frame);
  if ((frame_idx == old_frame_idx) && (frame == old_frame))
    return false;
  if ((working_buffer != NULL) && frame_expander.construct(&jpx_in,frame) &&
      (frame_expander.get_num_members() > 0))
    { // Composit a new JPX frame
      compositor->set_frame(&frame_expander);
      kdu_dims tmp_image_dims;
      if ((!compositor->get_total_composition_dims(tmp_image_dims)) ||
          (tmp_image_dims != image_dims))
        { // Weird condition; better call `initialize_regions'
          if (rel_focus.is_valid())
            { 
              rel_focus.frame_idx = old_frame_idx;
              rel_focus.frame = old_frame;
            }
          invalidate_configuration();
          initialize_regions(false);
          return true;
        }
      int inactive_layers_to_cache = (animator != NULL)?1:3;
      compositor->cull_inactive_ilayers(inactive_layers_to_cache);
      kdu_region_animator_roi animator_roi;
      if (animator != NULL)
        { 
          kdu_dims vport = view_dims;
          animator->get_roi_and_mod_viewport(compositor,animator_roi,vport);
          vport = size_buffer_for_target_view(vport,false,false);
          compositor->set_buffer_surface(vport);
        }
      working_buffer =
        compositor->get_composition_buffer_ex(working_buffer_dims,true);
      if (working_buffer != NULL)
        { 
          working_buffer->set_image_dims(image_dims,orientation);
          working_buffer->set_roi_info(animator_roi);
        }
      if (rel_focus.is_valid())
        { 
          if (adjust_rel_focus())
            { // Need to update focus box at least
              if ((animator == NULL) && rel_focus.is_valid() &&
                  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y))
                { 
                  view_centre_known = true;
                  scroll_to_view_anchors();
                  view_centre_known = false;
                }
              update_focus_box(false);
            }
        }
      if (animator == NULL)
        update_client_window_of_interest(cur_time);
      if (overlays_enabled && (animator == NULL))
        { 
          jpx_metanode node;
          if (catalog_source &&
              (node=catalog_source->get_selected_metanode()).exists())
            highlight_imagery_for_metanode(node);
          else if (overlay_highlight_state || overlay_flashing_state)
            { 
              overlay_highlight_state = 0;
              overlay_flashing_state = 1;
              configure_overlays(-1.0,false);
            }
        }
      perform_any_outstanding_render_refresh(cur_time);
      if (animator == NULL)
        update_animation_bar_pos();
    }
  else
    { // Have to initialize configuration from scratch
      if (rel_focus.is_valid())
        { 
          rel_focus.frame_idx = old_frame_idx;
          rel_focus.frame = old_frame;
        }
      invalidate_configuration();
      initialize_regions(false);
    }
  return true;  
}

/*****************************************************************************/
/*                kdws_renderer::set_animation_point_by_idx                  */
/*****************************************************************************/

void kdws_renderer::set_animation_point_by_idx(int idx)
{
  if (animator != NULL)
    stop_animation();
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx >= 0)
    return; // We should not be here
  if (jpx_in.exists() && (single_layer_idx >= 0))
    { // Interpret `idx' as a compositing layer index
      if (max_compositing_layer_idx >= 0)
        { 
          if ((idx < 0) || (idx >= max_compositing_layer_idx))
            idx = max_compositing_layer_idx;
        }
      else
        idx = 1000000; // Just set a really huge value
      single_layer_idx = idx;
      invalidate_configuration();
      initialize_regions(false);
    }
  else
    { // Interpret `idx' as a frame index
      if (num_frames_known)
        { 
          if ((idx < 0) || (idx >= (num_frames-1)))
            idx = num_frames-1;
        }
      else if (idx < 0)
        idx = 1000000; // Just set a really huge value
      set_frame(idx);
    }
}

/*****************************************************************************/
/*               kdws_renderer::set_animation_point_by_time                  */
/*****************************************************************************/

void kdws_renderer::set_animation_point_by_time(double time,
                                                bool nearest_start)
{
  if (animator != NULL)
    stop_animation();
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if ((single_component_idx >= 0) || (single_layer_idx >= 0))
    return; // We should not be here

    if (time <= 0.0)
    { 
      set_frame(0);
      return;
    }

  if (jpx_in.exists() && composition_rules.exists())
    { 
      int tgt_idx;
      kdu_long tgt_time = (kdu_long)(time*composition_rules.get_timescale());
      if (!composition_rules.count_track_frames_before_time(track_idx,tgt_time,
                                                            tgt_idx))
        tgt_idx++; // The frame we are looking for lies at least one beyond
                   // `tgt_idx' -- if it turns out later not to exist, the
                   // frame index will be adjusted downwards.
      else if (nearest_start)
        { // See if we want the next frame
          jpx_frame frm =
            composition_rules.access_frame(track_idx,tgt_idx,false);
          if (frm.exists())
            { 
              kdu_long start_time, duration;
              frm.get_info(start_time,duration);
              if (duration < 2*(tgt_time-start_time))
                tgt_idx++;
            }
        }
      set_frame(tgt_idx);
    }
  else if (mj2_in.exists())
    { // MJ2 tracks provide all the timing information we need
      mj2_video_source *track = mj2_in.access_video_track(track_idx);
      if (track == NULL)
        return; // Should never happen
      kdu_long ticks_per_second = track->get_timescale();
      kdu_long tgt_instant = (kdu_long)(0.5+time*ticks_per_second);
      int tgt_idx = track->time_to_frame(tgt_instant);
      track->seek_to_frame(tgt_idx);
      if (nearest_start)
        { // While `tgt_idx' is the index of a frame whose period includes
          // `time', the next frame might have a start time that is closer
          // to `time'.
          kdu_long delta = tgt_instant - track->get_frame_instant();
          kdu_long period = track->get_frame_period();
          if (delta > (period>>1))
            tgt_idx++;
        }
      set_frame(tgt_idx);
    }
}

/*****************************************************************************/
/*                      kdws_renderer::stop_animation                        */
/*****************************************************************************/

void kdws_renderer::stop_animation()
{
  if (animator == NULL)
    return;
  if (status_id == KDS_STATUS_PLAYSTATS)
    {
      if (jpip_client == NULL)
        status_id = KDS_STATUS_LAYER_RES;
      else
        status_id = KDS_STATUS_CACHE;
    }
  next_frame_advance_time = -1.0;
  frame_presenter->disable(); // Blocking call prior to flush queue
  pushed_last_frame = false;
  delete animator;
  animator = NULL;
  int last_animation_display_idx = animation_display_idx;
  jpx_metanode last_animation_display_node = animation_display_metanode;
  animation_display_idx = -1;
  animation_display_metanode = jpx_metanode();
  animation_display_jpip_progress = -1.0;
  animation_metanode = jpx_metanode();
  animation_metanode_shows_box = true;
  animation_advancing = false; // Just in case
  animation_frame_started = false;
  animation_frame_complete = false;
  animation_frame_needs_push = false;
  view_dims_change_deferred = false;
  next_overlay_change_time = -1.0;
  highlight_metanode = jpx_metanode();
  if (rel_focus.animator_driven)
    rel_focus.reset(); // Remove the animation driven focus box
  if (overlays_auto_flash)
    overlay_flashing_state = 1; // Re-enable overlay flashing after animation

  // Set up non-animated rendering for the last frame that was displayed
  if (compositor != NULL)
    { 
      compositor->flush_composition_queue();
      compositor->set_surface_initialization_mode(true);
      working_buffer_dims = kdu_dims();
      working_buffer = NULL;
      if (compositor->get_total_composition_dims(image_dims))
        { // Make sure we install the right buffer surface
          image_view->set_max_view_size(image_dims.size,pixel_scale,false);
        }
      if (last_animation_display_idx >= 0)
        { 
          if ((single_layer_idx >= 0) && jpx_in.exists())
            set_compositing_layer(last_animation_display_idx);
          else
            set_frame(last_animation_display_idx);
        }
      if (last_animation_display_node.exists())
        { 
          kdu_dims roi_dims, save_view_dims = view_dims;
          roi_dims =
            kdu_region_animator::adjust_viewport_for_roi(compositor,view_dims,
                                                 last_animation_display_node);
          calculate_rel_view_params();
          view_dims = save_view_dims;
          if (view_centre_known && !roi_dims.is_empty())
            { 
              scroll_to_view_anchors();
              view_centre_known = false;
            }
          if (!roi_dims.is_empty())
            set_focus_box(roi_dims,last_animation_display_node);
        }
      render_refresh();
    }
  display_status();
  if (animation_bar != NULL)
    { 
      update_animation_bar_state();
      update_animation_bar_pos();
    }
  else if (metamation_bar != NULL)
    { 
      window->remove_animation_bar();
      metamation_bar = NULL;
    }

  // Re-enable scroll bars if necessary
  image_view->release_scrollers(true);
  
  animation_advancing = false; // Just in case
  update_client_window_of_interest();
}

/*****************************************************************************/
/*                     kdws_renderer::start_animation                        */
/*****************************************************************************/

kdu_region_animator *
  kdws_renderer::start_animation(bool reverse, jpx_metanode metanode)
{
  if (animator != NULL)
    stop_animation();
  
  if (!source_supports_animation())
    return NULL;
  if (single_component_idx >= 0)
    return NULL;
  
  frame_presenter->disable(); // Blocking call prior to flush queue
  compositor->flush_composition_queue(); // Just in case
  animation_display_idx = -1; // Becomes >= 0 with 1st frame presentation
  animation_display_metanode=jpx_metanode(); // Non-empty when metadata driven
  animation_display_jpip_progress = -1.0;
  animation_metanode=jpx_metanode();
  animation_metanode_shows_box = true;
  animation_frame_complete = false;
  animation_frame_needs_push = false;
  animation_border_x = animation_border_y = 0.0F;
  animation_last_sys_time = -1.0;
  defer_process_regions_deadline = -1.0; // Not used during animation
  earliest_render_refresh_time = -1.0;
  next_overlay_change_time = -1.0;
  next_render_refresh_time = -1.0;
  overlay_highlight_state = 0;
  highlight_metanode = jpx_metanode();
  last_show_metanode = jpx_metanode();
  if (overlays_auto_flash)
    overlay_flashing_state = 0; // Disable overlay flashing during animation
  animator = new kdu_region_animator;
  int num_added_metanodes = 0;
  if (metanode.exists())
    { 
      if (!animator->start(-1,-1,-1,
                           (jpx_in.exists()?&jpx_in:NULL),NULL,
                           0,(single_layer_idx >= 0)))
        { 
          stop_animation();
          return NULL;
        }
      num_added_metanodes = animator->add_metanode(metanode);
      if (num_added_metanodes == 0)
        { // Don't start a metadata-driven animation
          stop_animation();
          return NULL;
        }
    }
  else if (single_layer_idx < 0)
    { 
      int final_frame_idx = (num_frames_known)?(num_frames-1):INT_MAX;
      if (!animator->start(0,final_frame_idx,frame_idx,
                           (jpx_in.exists()?&jpx_in:NULL),
                           (mj2_in.exists()?&mj2_in:NULL),
                           track_idx,false))
        { 
          stop_animation();
          return NULL;
        }
    }
  else
    { 
      if (!animator->start(0,INT_MAX,single_layer_idx,
                           (jpx_in.exists()?&jpx_in:NULL),NULL,0,true))
        { 
          stop_animation();
          return NULL;
        }
    }

  animator->set_reverse(reverse);
  animator->set_repeat(animation_repeat);
  if (animation_uses_native_timing)
    animator->set_timing(-1.0,animation_native_rate_multiplier,
                         animation_native_rate_multiplier);
  else
    animator->set_timing(animation_custom_rate,1.0,animation_custom_rate);

  // Start the frame presenter
  if (num_frames_known && (single_layer_idx < 0))
    animator->set_max_frames(num_frames);

  if (metanode.exists())
    overlay_restriction = jpx_metanode();
  double next_display_event_time =
    frame_presenter->enable(this,image_view->m_hWnd);
  double last_display_event_time = next_display_event_time -
    frame_presenter->get_display_event_interval();
  if (next_display_event_time < 0.0)
    { // It seems that the presentation thread is not working!
      try {
        kdu_error e; e << "Sorry! The presentation thread does not appear "
          "to be running, so I won't be able to do any animation for you "
          ":(.  This should definitely not be happening.";
      }
      catch (kdu_exception) { }
      stop_animation();
    }
  compositor->init_display_event_times(last_display_event_time,
                                       next_display_event_time);

  // Perform time/status related initialization
  animation_frame_status_update_time = next_display_event_time;
  animation_metanode_status_update_time = next_display_event_time;
  animation_jpip_status_update_time = next_display_event_time;
  animation_display_idx = -1;
  animation_display_metanode = jpx_metanode();
  animation_display_jpip_progress = -1.0;
  animation_status_invalid = false;
  pushed_last_frame = false;
  if (status_id != KDS_STATUS_CACHE)
    status_id = KDS_STATUS_PLAYSTATS;

  // Obtain first frame from the `animator' (if possible)
  advance_animation(get_current_time(),last_display_event_time,
                    next_display_event_time);
  if (animator == NULL)
    return NULL;  
  if (animation_bar != NULL)
    { 
      update_animation_bar_state();
      update_animation_bar_pos();
    }
  else if (metanode.exists())
    { 
      metamation_bar = window->get_animation_bar(false);
      update_animation_bar_state();
      int num_meta_frames=0, max_id=0;
      if (animator->count_metadata_driven_frames(num_meta_frames))
        max_id = num_meta_frames-1;
      metamation_bar->set_frame((reverse)?(num_meta_frames-1):0,max_id,
                                -1.0,-1.0,-1.0);
    }
  return animator;
}

/*****************************************************************************/
/*                 kdws_renderer::update_animation_bar_pos                   */
/*****************************************************************************/

void kdws_renderer::update_animation_bar_pos()
{
  if (animation_bar != NULL)
    {
      if (jpx_in.exists() && (single_layer_idx >= 0))
        animation_bar->set_frame(single_layer_idx,max_compositing_layer_idx,
                                 -1.0,-1.0,-1.0);
      else
        { 
          double track_end = -1.0;
          if (!get_track_duration(track_end))
            track_end = -1.0;
          animation_bar->set_frame(frame_idx,(num_frames-1),
                                   frame_start,frame_end,track_end);
        }
    }
}

/*****************************************************************************/
/*                kdws_renderer::update_animation_bar_state                  */
/*****************************************************************************/

void kdws_renderer::update_animation_bar_state()
{
  kdws_animation_bar *bar = animation_bar;
  if ((bar == NULL) && ((bar = metamation_bar) == NULL))
    return;
  double custom_rate = animation_custom_rate;
  if (animation_uses_native_timing)
    custom_rate = -1.0;
  if (animator == NULL)
    bar->set_state(false,false,animation_repeat,custom_rate,
                   animation_native_rate_multiplier);
  else
    { 
      bool repeat=animator->get_repeat(), reverse=animator->get_reverse();
      bar->set_state(!reverse,reverse,repeat,custom_rate,
                     animation_native_rate_multiplier);
    }
}

/*****************************************************************************/
/*                     kdws_renderer::advance_animation                      */
/*****************************************************************************/
  
bool kdws_renderer::advance_animation(double cur_system_time,
                                      double last_display_event_time,
                                      double next_display_event_time)
{
  if ((animator == NULL) || pushed_last_frame)
    return false;
  bool want_refresh = false;
  if (cur_system_time < 0.0)
    { 
      want_refresh = true;
      cur_system_time = get_current_time();
    }
  if ((last_display_event_time < 0.0) || (next_display_event_time < 0.0))
    last_display_event_time =
      compositor->get_display_event_times(next_display_event_time);

  int next_idx =
    animator->advance_animation(cur_system_time,last_display_event_time,
                                next_display_event_time,want_refresh,
                                animation_skip_undisplayables);
  if (next_idx < 0)
    { 
      if (next_idx == KDU_ANIMATION_PENDING)
        { // The animator is waiting for more data
          update_client_window_of_interest(cur_system_time);
          return false;
        }
      else
        { // We have reached the end of the animation
          assert((next_idx == KDU_ANIMATION_DONE) ||
                 (next_idx == KDU_ANIMATION_PAUSE));
          if ((compositor->inspect_composition_queue(0) == NULL) &&
              !want_refresh)
            { // We never pushed a single buffer onto the queue and we
              // must be in `start_animation' or `manage_animation_...'.
              stop_animation();
              return false;
            }
          else
            {
              pushed_last_frame = true;
              if (compositor->composition_queue_tail_presented())
                stop_animation(); // Otherwise, stopped in the next call to
                                  // `update_animation_status_info'.
              return false;
            }
        }
    }
  
  // Update `animation_border'
  if ((animation_last_sys_time > 0.0) &&
      (animation_last_sys_time < cur_system_time))
    { // Progressively attenuate the animation frame rendering border -- it
      // bumps back up again whenever interactive panning/resizing occurs
      float delta_t = (float)(cur_system_time - animation_last_sys_time);
      float alpha = 1.0F - delta_t; // Time constant for decay is 1 second
      alpha = (alpha < 0.0F)?0.0F:alpha;
      animation_border_x *= alpha;
      animation_border_y *= alpha;
      if (animation_border_x < 0.0F)
        animation_border_x = 0.0F;
      if (animation_border_y < 00.0F)
        animation_border_y = 10.0F;
    }
  animation_last_sys_time = cur_system_time;
  
  this->animation_advancing = true;
  bool animating_layer_idx = false;
  int cur_idx = frame_idx;
  jpx_frame next_frm, cur_frm = frame;
  mj2_video_source *next_mj2_trk=NULL;
  animator->get_current_frame(next_frm,next_mj2_trk);
  animation_frame_started = false;
  animation_frame_complete = false;
  
  if (view_dims_change_deferred)
    { 
      view_dims_changed(view_dims); // Update view dimensions. We have to reset
      view_dims_change_deferred = false; // the deferred flag after the call;
                           // this prevents the call returning when it sees
                           // that view dimensions themselves have not changed.
    }
  if ((single_layer_idx >= 0) && jpx_in.exists())
    { 
      animating_layer_idx = true;
      cur_idx = single_layer_idx;
    }
  if ((next_idx == cur_idx) && (next_frm == cur_frm))
    { // No change in frame:track/layer index, but the buffer region may have
      // changed
      kdu_region_animator_roi animator_roi;
      kdu_dims vport = view_dims;
      animator->get_roi_and_mod_viewport(compositor,animator_roi,vport);
      vport = size_buffer_for_target_view(vport,false,false);
      compositor->set_buffer_surface(vport);
      working_buffer =
        compositor->get_composition_buffer_ex(working_buffer_dims,true);
      if (working_buffer != NULL)
        { 
          working_buffer->set_image_dims(image_dims,orientation);
          working_buffer->set_roi_info(animator_roi);
        }
    }
  else if (animating_layer_idx)
    { // Change compositing layer
      set_compositing_layer(next_idx);
    }
 else if (!next_frm.exists())
    { // Change frame by index
      if (!set_frame(next_idx))
        { // Should not happen
          animation_advancing = false;
          stop_animation();
          return false;
        }
    }
  else
    { // Change frame using the more comprehensive `jpx_frame' interface
      if (!set_frame(next_frm))
        { // Should not happen
          animation_advancing = false;
          stop_animation();
          return false;
        }
    }

  schedule_frame_advance(-1.0); // Cancel any scheduled advance attempt
  if (animator->get_suggested_refresh_action())
    render_refresh(false);
  
  bool had_animation_metanode = animation_metanode.exists();
  animation_metanode = animator->get_current_metanode();
  if (animation_metanode.exists() && !had_animation_metanode)
    { // See if we need to adjust the scale
      kdu_dims buf_roi;
      bool decreasing_scale=false, increasing_scale=false;
      while ((working_buffer != NULL) && working_buffer->get_roi(buf_roi))
        { 
          float last_scale = rendering_scale;
          if (((buf_roi.size.x > (view_dims.size.x+10)) ||
               (buf_roi.size.y > (view_dims.size.y+10))) &&
              !increasing_scale)
            { 
              if (rendering_scale == min_rendering_scale)
                break;
              decreasing_scale = true;
              rendering_scale = decrease_scale(rendering_scale,false);
            }
          else if ((buf_roi.size.x < (view_dims.size.x>>1)) &&
                   (buf_roi.size.y < (view_dims.size.y>>1)) &&
                   (buf_roi.area() < 1600) && !decreasing_scale)
            { 
              if (rendering_scale == max_rendering_scale)
                break;
              increasing_scale = true;
              rendering_scale = increase_scale(rendering_scale,false);
            }
          else
            break;
          calculate_rel_view_params();
          initialize_regions(true);
          if (last_scale == rendering_scale)
            break; // We have hit a limit on the scale; avoid looping forever
        }
    }

  // See if we need to record a focus box -- this is primarily to allow
  // JPIP progress to be computed correctly, in the case where we have a
  // JPIP client.
  if (rel_focus.animator_driven)
    rel_focus.reset(); // Remove the animation-driven focus box
  kdu_dims cur_roi, cur_dims;
  if (animator->get_current_geometry(cur_dims.size,cur_roi) &&
      !cur_roi.is_empty())
    { // Create an `animator_driven' focus definition
      kdu_dims empty_roi;
      compositor->set_focus_box(empty_roi,0); // Remove non-animation-driven
        // focus boxes identified within the `compositor' object -- such a
        // thing might have been deposited by a previous call to the
        // `set_focus' function which set up `rel_focus', but we are now
        // modifying `rel_focus' independently, so we don't want disconnected
        // focus box records floating around inside the compositor that
        // might get erroneously painted when the animation finishes.
      if (!view_dims.is_empty())
        { // Restrict `cur_roi' to a co-centred viewport-sized region
          kdu_dims viewport = view_dims;
          if (orientation.trans)
            viewport.transpose();
          viewport.size.x -= 2; viewport.size.y -= 2; // Help avoid basing the
          if (viewport.size.x < 1) viewport.size.x=1; // progress bar on larger
          if (viewport.size.y < 1) viewport.size.y=1; // region than requested
          viewport.pos = cur_roi.pos;
          viewport.pos.x += (cur_roi.size.x-viewport.size.x)>>1;
          viewport.pos.y += (cur_roi.size.y-viewport.size.y)>>1;
          cur_roi &= viewport;
        }
      rel_focus.region.set(cur_roi,cur_dims);
      if (orientation.trans) rel_focus.region.transpose();
      if (orientation.hf) rel_focus.region.hflip();
      if (orientation.vf) rel_focus.region.hflip();
      if (single_component_idx >= 0)
        { rel_focus.codestream_idx = single_codestream_idx;
          rel_focus.codestream_region = rel_focus.region; }
      else if (jpx_in.exists() && (single_layer_idx >= 0))
        { rel_focus.layer_idx = single_layer_idx;
          rel_focus.layer_region = rel_focus.region; }
      else
        rel_focus.frame_idx = frame_idx;
      rel_focus.animator_driven = true;
    }

  // Prepare for frame generation
  if (overlays_enabled)
    configure_overlays(-1.0,false);

  animation_advancing = false;
  update_client_window_of_interest(cur_system_time);
  return true;
}

/*****************************************************************************/
/*                kdws_renderer::set_metadata_catalog_source                 */
/*****************************************************************************/

void kdws_renderer::set_metadata_catalog_source(kdws_catalog *source)
{
  if ((source == NULL) && (this->catalog_source != NULL))
    catalog_closed = true;
  if (this->catalog_source != source)
    {
      if (this->catalog_source != NULL)
        this->catalog_source->deactivate(); // Best to do this first
    }
  this->catalog_source = source;
  if ((source != NULL) && jpx_in.exists())
    {
      catalog_closed = false;
      refresh_metadata(get_current_time());
    }
}

/*****************************************************************************/
/*                 kdws_renderer::reveal_metadata_at_point                   */
/*****************************************************************************/

void kdws_renderer::reveal_metadata_at_point(kdu_coords point, bool dbl_click)
{
  if ((compositor == NULL) || (!jpx_in) ||
      ((catalog_source == NULL) && (metashow == NULL) && !dbl_click))
    return;
  kdu_ilayer_ref ilyr;
  kdu_istream_ref istr;
  jpx_metanode node;
  if (overlays_enabled)
    node = compositor->search_overlays(point,istr,0.1F);
  bool revealing_roi = node.exists();
  if (!revealing_roi)
    { // Look for a relevant numlist to select.  This is not so hard, but there
      // may be many.  To address this issue, we first reduce the set of
      // considered numlists to those that are associated with a relevant
      // compositing layer and as few other compositing layers as possible.
      // Even then, there may be many such numlists.  To deal with this, we
      // consider the current selection, if any, in the metadata catalog.  If
      // one of the potential numlists is already selected, we jump to the
      // next potential numlist.
      jpx_metanode cur_sel;
      if (catalog_source != NULL)
        { 
          cur_sel = catalog_source->get_selected_metanode();
          if (cur_sel.exists())
            cur_sel = cur_sel.get_numlist_container();
        }
      jpx_meta_manager meta_manager = jpx_in.access_meta_manager();
      int enumerator=0;
      bool node_follows_cur_sel_in_layer = false;
      bool best_node_follows_cur_sel = false;
      while ((ilyr=compositor->find_point(point,enumerator,0.1F)).exists())
        { 
          int layer_idx, dcs_idx; bool opq;
          if ((compositor->get_ilayer_info(ilyr,layer_idx,dcs_idx,opq) > 0) &&
              (layer_idx >= 0))
            { 
              bool cur_sel_compatible =
                cur_sel.exists() && cur_sel.test_numlist_layer(layer_idx);
              bool best_is_cur_sel = false;
              jpx_metanode scan, best_node;
              int min_layers=INT_MAX;
              int min_dist_to_cur_sel=INT_MAX;
              while ((scan = meta_manager.enumerate_matches(scan,-1,layer_idx,
                                                            false,kdu_dims(),
                                                            0,true)).exists())
                { 
                  int num_cs, num_layers; bool rres;
                  if (!scan.get_numlist_info(num_cs,num_layers,rres))
                    continue; // Should not happen
                  bool is_cur_sel = (scan.compare_numlists(cur_sel) == 0);
                  int dist_to_cur_sel=min_dist_to_cur_sel;
                  if (!cur_sel_compatible)
                    dist_to_cur_sel=kdws_dist_to_common_ancestor(cur_sel,scan);
                  if ((!best_node) || (num_layers < min_layers) ||
                      ((num_layers == min_layers) &&
                       (dist_to_cur_sel < min_dist_to_cur_sel)))
                    { // This is a better selection than whatever we have
                      best_node = scan;
                      min_layers = num_layers;
                      min_dist_to_cur_sel = dist_to_cur_sel;
                      best_is_cur_sel = is_cur_sel;
                      best_node_follows_cur_sel = !is_cur_sel;
                    }
                  else if ((num_layers == min_layers) &&
                           (dist_to_cur_sel == min_dist_to_cur_sel))
                    { // This is just as good as whatever we have already
                      if (is_cur_sel)
                        best_node_follows_cur_sel = false;
                      else if (!best_node_follows_cur_sel)
                        { 
                          best_node = scan;
                          best_is_cur_sel = false;
                          best_node_follows_cur_sel = true;
                        }
                    }
                }
              if (best_node.exists() && (!best_is_cur_sel) &&
                  ((!node) || !node_follows_cur_sel_in_layer))
                { 
                  node = best_node;
                  node_follows_cur_sel_in_layer = best_node_follows_cur_sel;
                }
            }
          enumerator++;
        }
    }
  if (node.exists())
    {
      if (metashow != NULL)
        metashow->select_matching_metanode(node);
      if (catalog_source != NULL)
        catalog_source->select_matching_metanode(node);
      if (dbl_click)
        {
          if (!revealing_roi)
            last_show_metanode = node; // So we immediately advance to next
                                       // associated image, if any.
          show_imagery_for_metanode(node);
        }
    }
}

/*****************************************************************************/
/*                  kdws_renderer::edit_metadata_at_point                    */
/*****************************************************************************/

void kdws_renderer::edit_metadata_at_point(kdu_coords *point_ref)
{
  if ((compositor == NULL) || !jpx_in)
    return;
  
  kdu_coords point; // Will hold mouse location in `view_dims' coordinates
  if (point_ref != NULL)
    point = *point_ref;
  else
    point = image_view->get_last_mouse_point();

  kdu_ilayer_ref ilyr;
  kdu_istream_ref istr;
  jpx_meta_manager manager = jpx_in.access_meta_manager();
  jpx_metanode launch_node;
  if (overlays_enabled)
    launch_node = compositor->search_overlays(point,istr,0.1F);
  kdws_matching_metalist *metalist = new kdws_matching_metalist;
  if (launch_node.exists())
    metalist->append_node(launch_node);
  else
    {  
      jpx_metanode node;
      int enumerator=0;
      while ((ilyr=compositor->find_point(point,enumerator,0.1F)).exists())
        { 
          int layer_idx=-1, stream_idx; bool opq;
          compositor->get_ilayer_info(ilyr,layer_idx,stream_idx,opq);
          if ((istr=compositor->get_ilayer_stream(ilyr,0)).exists())
            compositor->get_istream_info(istr,stream_idx);
          manager.load_matches((stream_idx >= 0)?1:0,&stream_idx,
                               (layer_idx >= 0)?1:0,&layer_idx);
          if (layer_idx >= 0)
            while ((node=manager.enumerate_matches(node,-1,layer_idx,
                                                   false,kdu_dims(),0,true,
                                                   false,false)).exists())
              { 
                if (!launch_node)
                  launch_node = node;
                metalist->append_node(node);
              }
          if (stream_idx >= 0)
            while ((node=manager.enumerate_matches(node,stream_idx,layer_idx,
                                                   false,kdu_dims(),0,true,
                                                   true,false)).exists())
              { 
                if (!launch_node)
                  launch_node = node;
                metalist->append_node(node);
              }
          if (metalist->get_head() != NULL)
            break;
          enumerator++;
        }
      while ((node=manager.enumerate_matches(node,-1,-1,true,kdu_dims(),0,
                                             false,false)).exists())
        { 
          if (!launch_node)
            launch_node = node;
          metalist->append_node(node);
        }
    }
  
  if (metalist->get_head() == NULL)
    metalist->append_node(manager.access_root());

  if (file_server == NULL)
    file_server = new kdws_file_services(open_file_pathname);
  if (editor == NULL)
    {
      RECT wnd_frame;  window->GetWindowRect(&wnd_frame);
      POINT preferred_location;
      preferred_location.x = wnd_frame.right + 10;
      preferred_location.y = wnd_frame.top;
      editor = new kdws_metadata_editor(&jpx_in,file_server,true,
                                        this,preferred_location,
                                        window_identifier,NULL);
  }
  editor->configure_with_edit_list(metalist);
  editor->set_active_node(launch_node);
}

/*****************************************************************************/
/*                      kdws_renderer::edit_metanode                         */
/*****************************************************************************/

void kdws_renderer::edit_metanode(jpx_metanode node,
                                  bool include_roi_or_numlist)
{
  if ((compositor == NULL) || (!jpx_in) || (!node) || node.is_deleted())
    return;
  kdws_matching_metalist *metalist = new kdws_matching_metalist;
  metalist->append_node(node);
  if (metalist->get_head() == NULL)
    { // Nothing to edit; user needs to add metadata
      delete metalist;
      return;
    }
  jpx_metanode editor_startup_node = metalist->get_head()->node;
  if (include_roi_or_numlist && (editor_startup_node == node))
    { // See if we want to build the metalist with a different root
      jpx_metanode scan;
      for (scan=node.get_parent(); scan.exists(); scan=scan.get_parent())
        {
          bool rres;
          int num_cs, num_l;
          if (scan.get_numlist_info(num_cs,num_l,rres) ||
              (scan.get_num_regions() > 0))
            break;
        }
      if (scan.exists() && (scan != node))
        {
          delete metalist;
          metalist = new kdws_matching_metalist;
          metalist->append_node(scan);
        }
    }
  
  if (file_server == NULL)
    file_server = new kdws_file_services(open_file_pathname);
  if (editor == NULL)
    {
      RECT wnd_frame;  window->GetWindowRect(&wnd_frame);
      POINT preferred_location;
      preferred_location.x = wnd_frame.right + 10;
      preferred_location.y = wnd_frame.top;
      editor = new kdws_metadata_editor(&jpx_in,file_server,true,
                                        this,preferred_location,
                                        window_identifier,NULL);
  }
  editor->configure_with_edit_list(metalist);
  editor->set_active_node(editor_startup_node);
}

/*****************************************************************************/
/*              kdws_renderer::highlight_imagery_for_metanode                */
/*****************************************************************************/

bool
  kdws_renderer::highlight_imagery_for_metanode(jpx_metanode node)
{
  if ((animator != NULL) || !overlays_enabled)
    return false;

  int save_highlight_state = overlay_highlight_state;
  if (node.exists())
    { // Tentatively configure overlays inside the `compositor' to see if
      // there is anything that we can show; if not, we need to stick with
      // the current overlay state machinery
      overlay_highlight_state = 0;         // Assume nothing to highlight for
      highlight_metanode = jpx_metanode(); // the moment.
      compositor->configure_overlays(true,overlay_size_threshold,
                                     1.0F,KDWS_OVERLAY_BORDER,node,0,
                                     overlay_params,KDWS_NUM_OVERLAY_PARAMS);
      int total_roi_nodes=0, hidden_roi_nodes=0;
      compositor->get_overlay_info(total_roi_nodes,hidden_roi_nodes);
      if (total_roi_nodes > hidden_roi_nodes)
        { // Something associated with `node' is visible
          jpx_metanode roi=node;
          while (roi.exists() && (roi.get_regions() == NULL))
            roi = roi.get_parent();
          if (roi.exists())
            overlay_highlight_state = 1; // Highlighting a single ROI node
          if (hidden_roi_nodes > 0)
            overlay_highlight_state = 1; // At least something is hidden
        }
    }
  if (overlay_highlight_state)
    highlight_metanode = node;
  if (save_highlight_state != overlay_highlight_state)
    configure_overlays(-1,false); // Start scheduler from scratch
  else
    configure_overlays(next_overlay_change_time-0.001,false);
       // Simple way to ensure that we call `compositor->configure_overlays'
       // without actually adjusting any state variables.
  return true;
}

/*****************************************************************************/
/*                kdws_renderer::show_imagery_for_metanode                   */
/*****************************************************************************/

bool kdws_renderer::show_imagery_for_metanode(jpx_metanode node,
                                              kdu_istream_ref *istream_ref)
{
  if (animator != NULL)
    return false; // Too many forces competing for control of current frame/ROI
  if (shape_istream.exists())
    { 
      pending_show_metanode = jpx_metanode();
      MessageBeep(MB_ICONEXCLAMATION);
      return false;
  }
  if (istream_ref != NULL)
    *istream_ref = kdu_istream_ref();
  bool advance_to_next = ((node==last_show_metanode) &&
                          !pending_show_metanode.exists());
  last_show_metanode = node; // Assume success until proven otherwise
  pending_show_metanode = node;
  if ((compositor == NULL) || (!jpx_in) || (!node))
    {
      cancel_pending_show();
      return false;
    }
  
  bool rendered_result=false;
  int num_streams=0, num_layers=0, num_regions=0;
  jpx_metanode numlist, roi;
  for (; node.exists() && !(numlist.exists() && roi.exists());
       node=node.get_parent())
    if ((!numlist) &&
        node.get_numlist_info(num_streams,num_layers,rendered_result))
      numlist = node;
    else if ((!roi) && ((num_regions = node.get_num_regions()) > 0))
      roi = node;

  if (!(numlist.exists() || roi.exists()))
    {
      cancel_pending_show();
      return false; // Nothing to do
    }
  bool can_cancel_pending_show = true;
  
  bool have_unassociated_roi = roi.exists() && (num_streams == 0);
  if (have_unassociated_roi)
    { // An ROI must be associated with one or more codestreams, or else it
      // must be associated with nothing other than the rendered result (an
      // unassociated ROI).  This last case is probably rare, but we
      // accommodate it here anyway, by enforcing the lack of association
      // with anything other than the rendered result.
      num_layers = num_streams = 0;
      numlist = jpx_metanode();
    }
  
  if (!composition_rules)
    composition_rules = jpx_in.access_composition();
  bool match_any_frame = false;
  bool match_any_layer = false;
  if (rendered_result || have_unassociated_roi)
    {
      if (!composition_rules)
        { can_cancel_pending_show = false; match_any_layer = true; }
      else if (composition_rules.get_next_frame(NULL) == NULL)
        match_any_layer = true;
      else
        match_any_frame = true;
    }

  bool have_compatible_layer = false;
  int compatible_codestream=-1; // This is the codestream against which
                                // we will assess the `roi' if there is one
  int compatible_codestream_layer=-1; // This is the compositing layer against
                                // which we should assess the `roi' if any.
  
  int m, n, c;
  bool in_frame_mode = (single_layer_idx < 0) && (single_component_idx < 0);
  if (num_streams == 0)
    { // Compatible imagery consists either of a rendered result or a
      // single compositing layer only.  This may require us to display
      // a more complex object than is currently being rendered.  Also,
      // because `num_streams' is 0, we only need to match image entitites,
      // not ROI's.
      if ((num_layers == 0) && match_any_frame)
        { // Only a composited frame will be compatible with the metadata
          if (!composition_rules)
            { // Waiting for more data from the cache before navigating
              return false;
            }
          if ((!in_frame_mode) || (!advance_to_next) ||
              !set_frame(frame_idx+1))
            set_frame(0);
          if (!configuration_complete)
            can_cancel_pending_show = false;
          if (!have_unassociated_roi)
            {
              if (can_cancel_pending_show)
                cancel_pending_show();
              return can_cancel_pending_show;
            }
        }
      else if ((single_component_idx >= 0) && match_any_layer)
        {
          set_compositing_layer(0);
          if (istream_ref != NULL)
            *istream_ref = compositor->get_next_istream(kdu_istream_ref());
          if (!configuration_complete)
            can_cancel_pending_show = false;
          if (!have_unassociated_roi)
            {
              if (can_cancel_pending_show)
                cancel_pending_show();
              return can_cancel_pending_show;
            }
        }
      else if ((single_component_idx >= 0) && (num_layers > 0))
        { // Need to display a compositing layer.  If possible, display one
          // which contains the currently displayed codestream.
          int compatible_layer = -1;
          jpx_codestream_source cs =
            jpx_in.access_codestream(single_codestream_idx,false);
          for (n=-1; cs.exists() && (n=cs.enum_layer_ids(n)) >= 0; )
            if (numlist.test_numlist_layer(n))
              { compatible_layer = n; break; }
          if (compatible_layer < 0)
            compatible_layer = numlist.get_numlist_layer(0);
          set_compositing_layer(compatible_layer);
          if (istream_ref != NULL)
            *istream_ref = compositor->get_next_istream(kdu_istream_ref());
          if (!configuration_complete)
            can_cancel_pending_show = false;
          if (can_cancel_pending_show)
            cancel_pending_show();
          return can_cancel_pending_show;
        }
    }
  
  if (have_unassociated_roi)
    have_compatible_layer = true; // We have already bumped things up as req'd.
  const jpx_roi *regions = NULL;
  if (roi.exists())
    regions = roi.get_regions();
  
  // Now see what changes are required to match the image entities
  if (in_frame_mode && !have_compatible_layer)
    { // We are displaying a frame.  Let's try to keep doing that.
      if (match_any_frame && !roi)
        { // Any frame will do, but we may need to advance
          if (advance_to_next && !set_frame(frame_idx+1))
            set_frame(0);
          if (!configuration_complete)
            can_cancel_pending_show = false;
          if (can_cancel_pending_show)
            cancel_pending_show();
          return can_cancel_pending_show;
        }
      
      // Look for the most appropriate matching frame
      jpx_frame compatible_frame;
      if ((num_regions == 0) && (num_streams == 0) && (num_layers > 1) &&
          (num_layers <= 1024))
        compatible_frame = find_compatible_frame(numlist,num_regions,regions,
                                                 compatible_codestream,
                                                 compatible_codestream_layer,
                                                 true, advance_to_next);
      if (!compatible_frame.exists())
        compatible_frame = find_compatible_frame(numlist,num_regions,regions,
                                                 compatible_codestream,
                                                 compatible_codestream_layer,
                                                 false,advance_to_next);
      if (compatible_frame.exists())
        { 
          have_compatible_layer = true;
          set_frame(compatible_frame);
          if (!configuration_complete)
            can_cancel_pending_show = false;
        }
    }

  if ((single_layer_idx >= 0) && (!have_compatible_layer) && !advance_to_next)
    { // We are already displaying a single compositing layer; try to keep
      // doing this.
      have_compatible_layer = (match_any_layer ||
                               numlist.test_numlist_layer(single_layer_idx));
      if (have_compatible_layer || (num_layers == 0))
        for (n=0; ((c = jpx_in.get_layer_codestream_id(single_layer_idx,
                                                       n)) >= 0); n++)
          if (numlist.test_numlist_stream(c))
            { compatible_codestream=c; break; }
      if (roi.exists() && (compatible_codestream < 0))
        have_compatible_layer = false;
    }
  
  int count_layers=1;
  bool all_layers_available =
    jpx_in.count_compositing_layers(count_layers);

  if ((single_layer_idx >= 0) && (!have_compatible_layer) && match_any_layer)
    { // Advance to next available compositing layer
      assert(advance_to_next);
      int new_layer_idx = single_layer_idx+1;
      if (new_layer_idx >= count_layers)
        new_layer_idx = 0;
      if (new_layer_idx != single_layer_idx)
        set_compositing_layer(new_layer_idx);
      have_compatible_layer = true;
      if (!configuration_complete)
        can_cancel_pending_show = false;
    }
  
  if ((single_component_idx < 0) && !have_compatible_layer)
    { // See if we can change to any compositing layer which is compatible
      int l_idx, rep_idx, layer_threshold, compatible_layer=-1;
      layer_threshold = single_layer_idx + (advance_to_next?1:0);
      rep_idx = numlist.get_container_layer_rep(single_layer_idx);
      int initial_rep_idx = rep_idx;
      bool done=(num_layers==0);
      while (!done)
        { // May need to try two values for `rep_idx'
          for (m=0; m < num_layers; m++)
            { 
              l_idx = numlist.get_numlist_layer(m,rep_idx);
              c = -1;
              if (num_streams > 0)
                for (n=0; ((c=jpx_in.get_layer_codestream_id(l_idx,n))>=0);n++)
                  if (numlist.test_numlist_stream(c))
                    break;
              if ((c < 0) && roi.exists())
                continue;
              have_compatible_layer = true;
              bool best_so_far;
              if (compatible_layer < layer_threshold)
                best_so_far = (compatible_layer < 0) ||
                  (l_idx < compatible_layer) || (l_idx >= layer_threshold);
              else
                best_so_far = (l_idx >= layer_threshold) &&
                  (l_idx < compatible_layer);
              if (best_so_far)
                { compatible_layer = l_idx; compatible_codestream = c; }
            }
          done = true;
          if ((compatible_layer < layer_threshold) &&
              (numlist.get_container_id() >= 0) &&
              (rep_idx == initial_rep_idx))
            { // Try the next repetition of the container
              rep_idx++; done = false;
            }
        }

      if (num_layers == 0)
        { // Look for any compositing layer that uses one of the referenced
          // codestreams.
          done = (num_streams==0);
          while (!done)
            { // May need to try two values for `rep_idx'
              for (n=0; n < num_streams; n++)
                { 
                  c = numlist.get_numlist_codestream(n,rep_idx);
                  jpx_codestream_source cs=jpx_in.access_codestream(c,false);
                  if (!cs.exists())
                    continue;
                  for (l_idx=-1; (l_idx=cs.enum_layer_ids(l_idx)) >= 0; )
                    { 
                      have_compatible_layer = true;
                      bool best_so_far;
                      if (compatible_layer < layer_threshold)
                        best_so_far = (compatible_layer < 0) ||
                          (l_idx < compatible_layer) ||
                          (l_idx >= layer_threshold);
                      else
                        best_so_far = (l_idx >= layer_threshold) &&
                          (l_idx < compatible_layer);
                      if (best_so_far)
                        { compatible_layer=l_idx; compatible_codestream=c; }
                    }
                }

              done = true;
              if ((compatible_layer < layer_threshold) &&
                  (numlist.get_container_id() >= 0) &&
                  (rep_idx == initial_rep_idx))
                { // Try the next repetition of the container
                  rep_idx++; done = false;
                }
            }
        }

      if (have_compatible_layer)
        set_compositing_layer(compatible_layer);
      else if (!all_layers_available)
        { // Come back later once we have more information in the cache,
          // rather than downgrading now to a lower form of rendered object,
          // which is what happens if we continue.
          return false;
        }
    }
  
  if (!have_compatible_layer)
    { // See if we can display a single codestream which is compatible; it
      // is possible that we are already displaying a single compatible
      // codestream.
      if ((!advance_to_next) &&
          numlist.test_numlist_stream(single_codestream_idx))
        compatible_codestream = single_codestream_idx;
      if ((compatible_codestream < 0) && (num_streams > 0))
        { 
          int rep_idx, stream_threshold;
          stream_threshold = single_codestream_idx + (advance_to_next?1:0);
          rep_idx=numlist.get_container_codestream_rep(single_codestream_idx);
          int initial_rep_idx = rep_idx;
          bool done=(num_streams==0);
          while (!done)
            { // May need to try two values for `rep_idx'
              for (n=0; n < num_streams; n++)
                { 
                  c = numlist.get_numlist_codestream(n,rep_idx);
                  bool best_so_far;
                  if (compatible_codestream < stream_threshold)
                    best_so_far = (compatible_codestream < 0) ||
                      (c < compatible_codestream) || (c >= stream_threshold);
                  else
                    best_so_far = (c >= stream_threshold) &&
                      (c < compatible_codestream);
                  if (best_so_far)
                    compatible_codestream = c;
                }
              done = true;
              if ((compatible_codestream < stream_threshold) &&
                  (numlist.get_container_id() >= 0) &&
                  (rep_idx == initial_rep_idx))
                { // Try the next repetition of the container
                  rep_idx++; done = false;
                }
            }
        }
      if (compatible_codestream >= 0)
        set_codestream(compatible_codestream);
    }

  if (!configuration_complete)
    can_cancel_pending_show = false;

  // Find compatible ilayer and istream references on the current composition
  // as appropriate.
  kdu_ilayer_ref compatible_ilayer;
  if (compatible_codestream_layer >= 0)
    { // This branch will always be followed if `frame' was non-NULL, except
      // if `have_unassociated_roi' was true.
      compatible_ilayer =
        compositor->get_next_ilayer(compatible_ilayer,
                                    compatible_codestream_layer);
    }
  else if (single_layer_idx >= 0)
    compatible_ilayer =
      compositor->get_next_ilayer(compatible_ilayer,single_layer_idx);
  kdu_istream_ref compatible_istream;
  if (compatible_ilayer.exists() && !have_unassociated_roi)
    {
      if (compatible_codestream >= 0)
        compatible_istream =
          compositor->get_ilayer_stream(compatible_ilayer,-1,
                                        compatible_codestream);
      else
        compatible_istream=compositor->get_ilayer_stream(compatible_ilayer,0);
    }
  else if ((single_codestream_idx >= 0) && !have_unassociated_roi)
    compatible_istream = compositor->get_next_istream(compatible_istream,
                                                      true,false,
                                                      single_codestream_idx);
  if (istream_ref != NULL)
    *istream_ref = compatible_istream;
  
  kdu_dims roi_region;
  kdu_dims mapped_roi_region;
  if (have_compatible_layer && configuration_complete && (num_layers == 1) &&
      in_frame_mode && (!roi) && compatible_ilayer.exists() &&
      (compositor->get_num_ilayers() > 1))
    { // See if we should create a fake region of interest, to identify
      // a compositing layer within its frame.
      mapped_roi_region =
        compositor->find_ilayer_region(compatible_ilayer,true);
      mapped_roi_region &= image_dims;
      if (mapped_roi_region.area() == image_dims.area())
        mapped_roi_region = kdu_dims(); // No need to highlight the layer
      else if (rel_focus.is_valid())
        { // See if the current focus box is entire contained in
          // `mapped_roi_region'; if so, we should keep it.
          kdu_dims focus_box = find_focus_box();
          if ((focus_box.area() >= 100) &&
              (focus_box == (mapped_roi_region & focus_box)))
            mapped_roi_region = kdu_dims();
        }
    }
  if ((!mapped_roi_region) &&
      ((!roi) || (!configuration_complete) ||
       ((compatible_codestream < 0) && !have_unassociated_roi)))
    { // Nothing we can do or perhaps need to do.
      if (can_cancel_pending_show)
        cancel_pending_show();
      return can_cancel_pending_show;
    }
  
  // Now we need to find the region of interest on the compatible codestream
  // (or the rendered result if we have an unassociated ROI).
  kdu_istream_ref roi_istr;
  if (roi.exists())
    {
      roi_region = roi.get_bounding_box();
      roi_istr = compatible_istream;
      mapped_roi_region = compositor->inverse_map_region(roi_region,roi_istr);
    }
  mapped_roi_region &= image_dims;
  if (!mapped_roi_region)
    { // Region does not appear to intersect with the image surface.
      if (can_cancel_pending_show)
        cancel_pending_show();
      return can_cancel_pending_show;
    }
  
  rel_focus.reset(); // We will be changing it
  while (roi_istr.exists() &&
         ((mapped_roi_region.size.x > view_dims.size.x) ||
          (mapped_roi_region.size.y > view_dims.size.y)))
    {
      if (rendering_scale == min_rendering_scale)
        break;
      rendering_scale = decrease_scale(rendering_scale,false);
      calculate_rel_view_params();
      initialize_regions(true);
      mapped_roi_region = compositor->inverse_map_region(roi_region,roi_istr);
    }
  while (roi_istr.exists() &&
         (mapped_roi_region.size.x < (view_dims.size.x >> 1)) &&
         (mapped_roi_region.size.y < (view_dims.size.y >> 1)) &&
         (mapped_roi_region.area() < 400))
    {
      if (rendering_scale == max_rendering_scale)
        break;
      rendering_scale = increase_scale(rendering_scale,false);
      calculate_rel_view_params();
      initialize_regions(true);
      mapped_roi_region = compositor->inverse_map_region(roi_region,roi_istr);
    }
  if ((mapped_roi_region & view_dims) != mapped_roi_region)
    { // The ROI region is not completely visible; centre the view over the
      // region then
      rel_focus.region.set(mapped_roi_region,image_dims);
      view_centre_x = rel_focus.region.centre_x;
      view_centre_y = rel_focus.region.centre_y;
      view_centre_known = true;
      rel_focus.reset();
      scroll_to_view_anchors();
    }
  set_focus_box(mapped_roi_region,last_show_metanode);
  
  if (can_cancel_pending_show)
    cancel_pending_show();
  return can_cancel_pending_show;
}

/*****************************************************************************/
/*                    kdws_renderer::cancel_pending_show                     */
/*****************************************************************************/

void kdws_renderer::cancel_pending_show()
{
  pending_show_metanode = jpx_metanode();
}

/*****************************************************************************/
/*                     kdws_renderer::metadata_changed                       */
/*****************************************************************************/

void kdws_renderer::metadata_changed(bool new_data_only)
{
  if (compositor == NULL)
    return;
  have_unsaved_edits = true;
  if (overlay_restriction.exists() && overlay_restriction.is_deleted())
    {
      overlay_restriction = last_selected_overlay_restriction = jpx_metanode();
      configure_overlays(-1.0,false);
    }
  compositor->update_overlays(!new_data_only);
  refresh_metadata(get_current_time());
}

/*****************************************************************************/
/*                   kdws_renderer::get_codestream_size                      */
/*****************************************************************************/

bool kdws_renderer::get_codestream_size(kdu_coords &size, int stream_idx)
{
  if (!jpx_in.exists())
    return false;
  jpx_codestream_source cs = jpx_in.access_codestream(stream_idx,false);
  if (!cs.exists())
    return false;
  jp2_dimensions dims = cs.access_dimensions();
  size = dims.get_size();
  return true;
}

/*****************************************************************************/
/*                    kdws_renderer::set_shape_editor                        */
/*****************************************************************************/

bool
  kdws_renderer::set_shape_editor(jpx_roi_editor *ed,
                                  kdu_istream_ref ed_istream,
                                  bool hide_path_segments,
                                  int fixed_path_thickness,
                                  bool scribble_mode)
{
  if (animator != NULL)
    stop_animation(); // Just in case
  if (ed == NULL)
    { shape_editor = NULL; shape_istream = kdu_istream_ref(); }
  else
    { 
      this->shape_editor = ed;
      this->shape_istream = ed_istream;
      assert(ed_istream.exists());
      this->hide_shape_path = hide_path_segments;
    }
  this->fixed_shape_path_thickness = fixed_path_thickness;
  this->shape_scribbling_mode = scribble_mode;
  if (compositor != NULL)
    compositor->invalidate_rect(view_dims);
  return (shape_editor != NULL);
}

/*****************************************************************************/
/*                   kdws_renderer::update_shape_editor                      */
/*****************************************************************************/

void
  kdws_renderer::update_shape_editor(kdu_dims region, bool hide_path_segments,
                                     int fixed_path_thickness,
                                     bool scribble_mode)
{
  if (shape_editor == NULL)
    return;
  if (region.is_empty() || (this->hide_shape_path != hide_path_segments))
    { 
      shape_editor->get_bounding_box(region);
      this->hide_shape_path = hide_path_segments;
    }
  this->fixed_shape_path_thickness = fixed_path_thickness;
  this->shape_scribbling_mode = scribble_mode;
  image_view->repaint_shape_region(region);
}

/*****************************************************************************/
/*               kdws_renderer::nudge_selected_shape_points                  */
/*****************************************************************************/

bool
  kdws_renderer::nudge_selected_shape_points(int delta_x, int delta_y,
                                             kdu_dims &update_region)
{
  kdu_dims regn;  regn.size.x=regn.size.y=1;
  int ninst; // not used
  if ((shape_editor == NULL) || (compositor == NULL) ||
      (shape_editor->get_selection(regn.pos,ninst) < 0) ||
      (regn=compositor->inverse_map_region(regn,shape_istream)).is_empty())
    return false;
  regn.pos.x += delta_x*regn.size.x;
  regn.pos.y += delta_y*regn.size.y;
  regn.size.x=regn.size.y=1;
  if (!compositor->map_region(regn,shape_istream))
    return false;
  update_region = shape_editor->move_selected_anchor(regn.pos);
  return true;
}

/*****************************************************************************/
/*                    kdws_renderer::get_shape_anchor                        */
/*****************************************************************************/

int kdws_renderer::get_shape_anchor(POINT &point, int which,
                                    bool sel_only, bool dragged)
{
  int result;
  kdu_coords pt;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !((result=shape_editor->get_anchor(pt,which,sel_only,dragged)) &&
        map_shape_point_to_display_view(pt,point)))
    result = 0;
  return result;
}

/*****************************************************************************/
/*                     kdws_renderer::get_shape_edge                         */
/*****************************************************************************/

int kdws_renderer::get_shape_edge(POINT &from, POINT &to, int which,
                                  bool sel_only, bool dragged)
{
  int result;
  kdu_coords v1, v2;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !(result=shape_editor->get_edge(v1,v2,which,sel_only,dragged,true)))
    return 0;
  if (!(map_shape_point_to_display_view(v1,from) &&
        map_shape_point_to_display_view(v2,to)))
    return 0;
  int cs_idx; bool tran=false, vf=false, hf=false;
  compositor->get_istream_info(shape_istream,cs_idx,NULL,NULL,4,NULL,
                               NULL,NULL,&tran,&vf,&hf);
  if (orientation.trans ^ tran ^ orientation.vf ^ vf ^
      orientation.hf ^ hf)
    { POINT tmp=from; from=to; to=tmp; }
  return result;
}

/*****************************************************************************/
/*                    kdws_renderer::get_shape_curve                         */
/*****************************************************************************/

int kdws_renderer::get_shape_curve(POINT &centre, double &extent_x,
                                   double &extent_y, double &alpha,
                                   int which, bool sel_only, bool dragged)
{
  int result;
  kdu_coords ctr, ext, skew;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !(result=shape_editor->get_curve(ctr,ext,skew,which,sel_only,dragged)))
    return 0;
  kdu_coords tl=ctr-ext, br=ctr+ext;
  skew += ctr;
  POINT mapped_tl, mapped_br, mapped_skew;
  if (!(map_shape_point_to_display_view(ctr,centre) &&
        map_shape_point_to_display_view(tl,mapped_tl) &&
        map_shape_point_to_display_view(br,mapped_br) &&
        map_shape_point_to_display_view(skew,mapped_skew)))
    return 0;
  extent_x = 0.5F*(mapped_br.x - mapped_tl.x);
  extent_y = 0.5F*(mapped_br.y - mapped_tl.y);
  if (extent_x < 0.0)
    extent_x = -extent_x;
  bool vflip = true;
  if (extent_y < 0.0)
    {
      extent_y = -extent_y;
      vflip = false;
    }
  mapped_skew.x -= centre.x;
  mapped_skew.y -= centre.y;
  alpha = mapped_skew.x / extent_y;
  double mapped_gamma_sq = (mapped_skew.y/extent_x)*alpha;
  if (mapped_gamma_sq < 0.0)
    mapped_gamma_sq = -mapped_gamma_sq;
  if (mapped_gamma_sq >= 1.0)
    extent_x = 0.1;
  else if ((extent_x *= sqrt(1.0 - mapped_gamma_sq)) < 0.1)
    extent_x = 0.1;
  if (vflip)
    alpha = -alpha;
  return result;
}

/*****************************************************************************/
/*                     kdws_renderer::get_shape_path                         */
/*****************************************************************************/

int kdws_renderer::get_shape_path(POINT &from, POINT &to, int which)
{
  int result; kdu_coords v1, v2;
  if ((shape_editor == NULL) || hide_shape_path || (compositor == NULL) ||
      !(result=shape_editor->get_path_segment(v1,v2,which)))
    return 0;
  if (!(map_shape_point_to_display_view(v1,from) &&
        map_shape_point_to_display_view(v2,to)))
    return 0;
  return result;      
}

/*****************************************************************************/
/*                         kdws_renderer::save_over                          */
/*****************************************************************************/

bool kdws_renderer::save_over()
{
  const char *save_pathname =
    manager->get_save_file_pathname(open_file_pathname);
  const char *suffix = strrchr(open_filename,'.');
  bool result = true;
  BeginWaitCursor();
  try { // Protect the entire file writing process against exceptions
    if ((suffix != NULL) &&
        (toupper(suffix[1]) == 'J') && (toupper(suffix[2]) == 'P') &&
        ((toupper(suffix[3]) == 'X') || (toupper(suffix[3]) == 'F') ||
         (suffix[3] == '2')))
      { // Generate a JP2 or JPX file.
        if (!(jpx_in.exists() || mj2_in.exists()))
          { kdu_error e; e << "Cannot write JP2-family file if input data "
            "source is a raw codestream.  Too much information must "
            "be invented.  Probably the file you opened had a \".jp2\", "
            "\".jpx\" or \".jpf\" suffix but was actually a raw codestream "
            "not embedded within any JP2 family file structure.  Use "
            "the \"Save As\" option to save it as a codestream again if "
            "you like."; }
        if ((suffix[3] == '2') || !jpx_in)
          save_as_jp2(save_pathname);
        else
          {
            save_as_jpx(save_pathname,true,false);
            have_unsaved_edits = false;
          }
      }
    else
      save_as_raw(save_pathname);
    
    if (last_save_pathname != NULL)
      delete[] last_save_pathname;
    last_save_pathname = new char[strlen(open_file_pathname)+1];
    strcpy(last_save_pathname,open_file_pathname);
  }
  catch (kdu_exception)
  {
    manager->declare_save_file_invalid(save_pathname);
    result = false;
  }
  EndWaitCursor();
  return result;
}

/*****************************************************************************/
/*                         kdws_renderer::save_as                            */
/*****************************************************************************/

void
  kdws_renderer::save_as(bool preserve_codestream_links,
                        bool force_codestream_links)
{
  if ((compositor == NULL) || (animator != NULL))
    return;

  // Get the file name
  OPENFILENAME ofn;
  memset(&ofn,0,sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
  kdws_settings *settings = manager->access_persistent_settings();
  kdws_string initial_dir(MAX_PATH);
  strcpy(initial_dir,settings->get_open_save_dir());
  if (initial_dir.is_empty())
    GetCurrentDirectory(MAX_PATH,initial_dir);

  kdws_string filename(MAX_PATH+4);
  if ((last_save_pathname != NULL) &&
      (strlen(last_save_pathname) < MAX_PATH))
    strcpy(filename,last_save_pathname);
  else if ((open_file_pathname != NULL) &&
           (strlen(open_file_pathname) < MAX_PATH))
    strcpy(filename,open_file_pathname);
  if (mj2_in.exists())
    {
      char *suffix = strrchr((char *) filename,'.');
      if (suffix == NULL)
        filename.clear();
      else
        strcpy(suffix,".jp2");
    }
  
  int num_choices = 1;

  ofn.hwndOwner = window->GetSafeHwnd();
  if (jpx_in.exists())
    {
      ofn.lpstrFilter =
        _T("JP2 compatible file (*.jp2, *.jpx, *.jpf)\0*.jp2;*.jpx;*.jpf\0")
        _T("JPX file (*.jpx, *.jpf)\0*.jpx;*.jpf\0")
        _T("JPEG2000 unwrapped code-stream (*.j2c, *.j2k)\0*.j2c;*.j2k\0\0");
      ofn.lpstrDefExt = _T("jpf");
      num_choices = 3;
    }
  else if (mj2_in.exists())
    {
      ofn.lpstrFilter =
        _T("JP2 file (*.jp2)\0*.jp2\0")
        _T("JPEG2000 unwrapped code-stream (*.j2c, *.j2k)\0*.j2c;*.j2k\0\0");
      ofn.lpstrDefExt = _T("jp2");
      num_choices = 2;
    }
  else
    {
      ofn.lpstrFilter =
        _T("JPEG2000 unwrapped code-stream (*.j2c, *.j2k)\0*.j2c;*.j2k\0\0");
      ofn.lpstrDefExt = _T("j2c");
      num_choices = 1;
    }
  ofn.nFilterIndex = settings->get_save_idx();
  if ((ofn.nFilterIndex < 1) || ((int) ofn.nFilterIndex > num_choices))
    ofn.nFilterIndex = 1;
  ofn.lpstrFile = filename;
  ofn.nMaxFile = MAX_PATH;
  
  ofn.lpstrInitialDir = initial_dir;
  ofn.lpstrTitle = _T("Save as JPEG2000 File");
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
  if (!GetSaveFileName(&ofn))
    return;

  // Adjust `settings' based on the outcome before we do anything else
  char *fname_string = filename;
  if ((ofn.nFileOffset > 0) &&
      (fname_string[ofn.nFileOffset-1] == '/') ||
      (fname_string[ofn.nFileOffset-1] == '\\'))
    ofn.nFileOffset--;
  char sep = fname_string[ofn.nFileOffset];
  fname_string[ofn.nFileOffset] = '\0';
  settings->set_open_save_dir(fname_string);
  settings->set_save_idx(ofn.nFilterIndex);
  fname_string[ofn.nFileOffset] = sep;
  
  // Remember the last saved file name
  const char *chosen_pathname = filename;
  const char *suffix = strrchr(chosen_pathname,'.');
  bool jp2_suffix = ((suffix != NULL) &&
                     (toupper(suffix[1]) == 'J') &&
                     (toupper(suffix[2]) == 'P') &&
                     (suffix[3] == '2'));
  bool jpx_suffix = ((suffix != NULL) &&
                     (toupper(suffix[1]) == 'J') &&
                     (toupper(suffix[2]) == 'P') &&
                     ((toupper(suffix[3]) == 'X') ||
                      (toupper(suffix[3]) == 'F')));
  
  // Now save the file
  const char *save_pathname = NULL;
  BeginWaitCursor();
  try { // Protect the entire file writing process against exceptions
    bool may_overwrite_link_source = false;
    if (force_codestream_links && jpx_suffix && (open_file_pathname != NULL))
      if (kdws_compare_file_pathnames(chosen_pathname,open_file_pathname))
        may_overwrite_link_source = true;
    if (jpx_in.exists() && !jp2_family_in.uses_cache())
      {
        jp2_data_references data_refs;
        if (jpx_in.exists())
          data_refs = jpx_in.access_data_references();
        if (data_refs.exists())
          {
            int u;
            const char *url_name;
            for (u=0; (url_name = data_refs.get_url(u)) != NULL; u++)
              if (kdws_compare_file_pathnames(chosen_pathname,url_name))
                may_overwrite_link_source = true;
          }
      }
    if (may_overwrite_link_source)
      { kdu_error e; e << "You are about to overwrite an existing file "
        "which is being linked by (or potentially about to be linked by) "
        "the file you are saving.  This operation is too dangerous to "
        "attempt."; }
    save_pathname = manager->get_save_file_pathname(chosen_pathname);
    if (jpx_suffix || jp2_suffix)
      { // Generate a JP2 or JPX file.
        if (!(jpx_in.exists() || mj2_in.exists()))
          { kdu_error e; e << "Cannot write JP2-family file if input data "
            "source is a raw codestream.  Too much information must "
            "be invented."; }
        if (jp2_suffix || !jpx_in)
          save_as_jp2(save_pathname);
        else
          {
            save_as_jpx(save_pathname,preserve_codestream_links,
                        force_codestream_links);
            have_unsaved_edits = false;
          }
      }
    else
      save_as_raw(save_pathname);
    
    if (last_save_pathname != NULL)
      delete[] last_save_pathname;
    last_save_pathname = new char[strlen(chosen_pathname)+1];
    strcpy(last_save_pathname,chosen_pathname);
  }
  catch (kdu_exception)
  {
    if (save_pathname != NULL)
      manager->declare_save_file_invalid(save_pathname);
  }
  EndWaitCursor();
}

/*****************************************************************************/
/*                        kdws_renderer::save_as_jp2                         */
/*****************************************************************************/

void
  kdws_renderer::save_as_jp2(const char *filename)
{
  jp2_family_tgt tgt; 
  jp2_target jp2_out;
  jpx_input_box stream_box;
  int jpx_layer_idx=-1, jpx_stream_idx=-1;
  
  if (jpx_in.exists())
    { 
      jpx_layer_idx = 0;
      if (single_layer_idx >= 0)
        jpx_layer_idx = single_layer_idx;
      
      jpx_layer_source layer_in = jpx_in.access_layer(jpx_layer_idx);
      if (!(layer_in.exists() && (layer_in.get_num_codestreams() == 1)))
        { kdu_error e;
          e << "Cannot write JP2 file based on the currently active "
          "compositing layer.  The layer either is not yet available, or "
          "else it uses multiple codestreams.  Try saving as a JPX file.";
        }
      jpx_stream_idx = layer_in.get_codestream_id(0);
      jpx_codestream_source stream_in=jpx_in.access_codestream(jpx_stream_idx);
      
      tgt.open(filename);
      jp2_out.open(&tgt);
      jp2_out.access_dimensions().copy(stream_in.access_dimensions(true));
      jp2_out.access_colour().copy(layer_in.access_colour(0));
      jp2_out.access_palette().copy(stream_in.access_palette());
      jp2_out.access_resolution().copy(layer_in.access_resolution());
      
      jp2_channels channels_in = layer_in.access_channels();
      jp2_channels channels_out = jp2_out.access_channels();
      int n, num_colours = channels_in.get_num_colours();
      channels_out.init(num_colours);
      for (n=0; n < num_colours; n++)
        { // Copy channel information, converting only the codestream ID
          int comp_idx, lut_idx, stream_idx, format, fmt_params[3]={0,0,0};
          channels_in.get_colour_mapping(n,comp_idx,lut_idx,stream_idx,
                                         format,fmt_params);
          channels_out.set_colour_mapping(n,comp_idx,lut_idx,0,
                                          format,fmt_params);
          if (channels_in.get_opacity_mapping(n,comp_idx,lut_idx,stream_idx,
                                              format,fmt_params))
            channels_out.set_opacity_mapping(n,comp_idx,lut_idx,0,
                                             format,fmt_params);
          if (channels_in.get_premult_mapping(n,comp_idx,lut_idx,stream_idx,
                                              format,fmt_params))
            channels_out.set_premult_mapping(n,comp_idx,lut_idx,0,
                                             format,fmt_params);
        }
      
      stream_in.open_stream(&stream_box);
    }
  else
    {
      assert(mj2_in.exists());
      int frm, fld;
      mj2_video_source *track = NULL;
      if (single_component_idx >= 0)
        { // Find layer based on the track to which single component belongs
          kdu_uint32 trk;
          if (mj2_in.find_stream(single_codestream_idx,trk,frm,fld) &&
              (trk > 0))
            track = mj2_in.access_video_track(trk);
        }
      else
        {
          assert(track_idx >= 0);
          track = mj2_in.access_video_track(track_idx);
          frm = frame_idx;
          fld = 0;
        }
      if (track == NULL)
        { kdu_error e; e << "Insufficient information available to open "
          "current video track.  Perhaps insufficient information has been "
          "received so far during a JPIP browsing session.";
        }
      
      tgt.open(filename);
      jp2_out.open(&tgt);
      jp2_out.access_dimensions().copy(track->access_dimensions());
      jp2_out.access_colour().copy(track->access_colour());
      jp2_out.access_palette().copy(track->access_palette());
      jp2_out.access_resolution().copy(track->access_resolution());
      jp2_out.access_channels().copy(track->access_channels());
      
      track->seek_to_frame(frm);
      track->open_stream(fld,&stream_box);
      if (!stream_box)
        { kdu_error e; e << "Insufficient information available to open "
          "relevant video frame.  Perhaps insufficient information has been "
          "received so far during a JPIP browsing session.";
        }
    }
  
  jp2_out.write_header();
  
  if (jpx_in.exists())
    { // See if we can copy some metadata across
      jpx_meta_manager meta_manager = jpx_in.access_meta_manager();
      jpx_metanode scan;
      while ((scan=meta_manager.enumerate_matches(scan,-1,jpx_layer_idx,false,
                                                  kdu_dims(),0,true)).exists())
        write_metanode_to_jp2(scan,jp2_out,file_server);
      while ((scan=meta_manager.enumerate_matches(scan,jpx_stream_idx,-1,false,
                                                  kdu_dims(),0,true)).exists())
        write_metanode_to_jp2(scan,jp2_out,file_server);
      if (!jpx_in.access_composition())
        { // In this case, rendered result same as composition layer
          while ((scan=meta_manager.enumerate_matches(scan,-1,-1,true,
                                                      kdu_dims(),0,
                                                      true)).exists())
            write_metanode_to_jp2(scan,jp2_out,file_server);
        }
      while ((scan=meta_manager.enumerate_matches(scan,-1,-1,false,
                                                  kdu_dims(),0,true)).exists())
        write_metanode_to_jp2(scan,jp2_out,file_server);
    }
  
  jp2_out.open_codestream();
  
  if (jp2_family_in.uses_cache())
    { // Need to generate output codestreams from scratch -- transcoding
      kdu_codestream cs_in;
      try { // Protect the `cs_in' object, so we can destroy it
        cs_in.create(&stream_box);
        copy_codestream(&jp2_out,cs_in,false);
        cs_in.destroy();
      }
      catch (kdu_exception exc)
      {
        if (cs_in.exists())
          cs_in.destroy();
        throw exc;
      }
    }
  else
    { // Simply copy the box contents directly
      kdu_byte *buffer = new kdu_byte[1<<20];
      try {
        int xfer_bytes;
        while ((xfer_bytes=stream_box.read(buffer,(1<<20))) > 0)
          jp2_out.write(buffer,xfer_bytes);
      }
      catch (kdu_exception exc)
      {
        delete[] buffer;
        throw exc;
      }
      delete[] buffer;
    }
  stream_box.close();
  jp2_out.close();
  tgt.close();
}

/*****************************************************************************/
/*                       kdws_renderer::save_as_jpx                          */
/*****************************************************************************/

void kdws_renderer::save_as_jpx(const char *filename,
                                bool preserve_codestream_links,
                                bool force_codestream_links)
{
  assert(jpx_in.exists());
  if ((open_file_pathname == NULL) || jp2_family_in.uses_cache())
    force_codestream_links = preserve_codestream_links = false;
  if (force_codestream_links)
    preserve_codestream_links = true;
  
  // Start by finding the number of top-level codestreams and compositing
  // layers, as well as the number of container base codestreams.  We need
  // these so that we can allocate resources and so we can generate top-level
  // image headers first.
  int n, num_top_codestreams, num_top_compositing_layers;
  int w, num_containers=0, total_base_codestreams=0;
  jpx_in.count_containers(num_containers);
  for (w=0; w < num_containers; w++)
    { 
      jpx_container_source container = jpx_in.access_container(w);
      if (!container)
        { num_containers = w; break; }
      if (w == 0)
        { 
          num_top_codestreams = container.get_num_top_codestreams();
          num_top_compositing_layers = container.get_num_top_layers();
        }
      int num_base_streams=0;
      container.get_base_codestreams(num_base_streams);
      total_base_codestreams += num_base_streams;
    }
  if (w == 0)
    { 
      jpx_in.count_codestreams(num_top_codestreams);
      jpx_in.count_compositing_layers(num_top_compositing_layers);
    }
  
  // Now see if we can come up with default compositing layers and/or
  // codestreams to substitute for ones that might be missing.
  jpx_layer_source default_layer; // To substitute for missing ones
  for (n=0; (n < num_top_compositing_layers) && !default_layer; n++)
    default_layer = jpx_in.access_layer(n);
  if (!default_layer)
    { kdu_error e; e << "Cannot write JPX file.  Not even one of the source "
      "file's compositing layers is available yet."; }
  jpx_codestream_source default_stream = // To substitute for missing ones
    jpx_in.access_codestream(default_layer.get_codestream_id(0));
  
  // Create the output file
  jp2_family_tgt tgt; tgt.open(filename);
  jpx_target jpx_out; jpx_out.open(&tgt);
  
  // Allocate local arrays to store:
  // 1. Interfaces to each added top-level `jpx_codestream_target' interface
  // 2. Interfaces to each `jpx_codestream_source' interface that is being
  //    used as a temporary replacement for a codestream that does not at
  //    least have its main header available in the source file.
  jpx_codestream_target *out_top_streams =
    new jpx_codestream_target[num_top_codestreams];
  jpx_codestream_source *in_default_streams =
    new jpx_codestream_source[num_top_codestreams+total_base_codestreams];
  
  try { // Protect local arrays
    
    // Copy composition instructions and compatibility info
    jpx_out.access_compatibility().copy(jpx_in.access_compatibility());
    jpx_out.access_composition().copy(jpx_in.access_composition());

    // Next, copy top-level compositing layer info
    for (n=0; n < num_top_compositing_layers; n++)
      {
        jpx_layer_source layer_in = jpx_in.access_layer(n);
        if (!layer_in)
          layer_in = default_layer;
        jpx_out.add_layer().copy_attributes(layer_in);
      }
    
    // Copy codestream headers
    for (n=0; n < num_top_codestreams; n++)
      { 
        jpx_codestream_source in_stream = jpx_in.access_codestream(n);
        if (!in_stream)
          in_default_streams[n] = in_stream = default_stream;
        out_top_streams[n] = jpx_out.add_codestream();
        out_top_streams[n].copy_attributes(in_stream);
      }

    // Copy JPX containers
    jpx_codestream_source *default_base_streams =
    in_default_streams + num_top_codestreams;
    for (w=0; w < num_containers; w++)
      { 
        jpx_container_source container_src = jpx_in.access_container(w);
        assert(container_src.exists());
        int first_layer_idx, num_layers, first_stream_idx, num_streams;
        first_layer_idx = container_src.get_base_layers(num_layers);
        first_stream_idx = container_src.get_base_codestreams(num_streams);
        int num_reps=0; container_src.count_repetitions(num_reps);
        if (num_reps < 1)
          { // Container is not sufficiently available for copying
            num_containers = w; break; 
          }
        
        jpx_container_target container_tgt =
          jpx_out.add_container(num_streams,num_layers,num_reps);
        kdu_uint32 t, num_tracks = container_src.get_num_tracks();
        for (t=1; t <= num_tracks; t++)
          { 
            jpx_composition comp_src, comp_tgt;
            if (!(comp_src = container_src.access_presentation_track(t)))
              break; // Cannot safely skip tracks
            int trk_layers; container_src.get_track_base_layers(t,trk_layers);
            comp_tgt = container_tgt.add_presentation_track(trk_layers);
            comp_tgt.copy(comp_src);
          }
        
        for (n=0; n < num_layers; n++)
          { 
            jpx_layer_source layer_in =
            container_src.access_layer(n,0,true,true);
            if (!layer_in)
              layer_in = default_layer;
            container_tgt.access_layer(n).copy_attributes(layer_in);
          }
        for (n=0; n < num_streams; n++)
          { 
            jpx_codestream_source stream_in = default_base_streams[n] =
            container_src.access_codestream(n,0,true,true);
            // Note: above call looks for any instance of the container
            // codestream with base index n that has a main header
            // available; only if there is none do we fall back to the
            // default top-level codestream.
            if (!stream_in)
              stream_in = default_base_streams[n] = default_stream;
            container_tgt.access_codestream(n).copy_attributes(stream_in);
          }
        default_base_streams += num_streams;
      }
    assert(default_base_streams <=
           (in_default_streams + num_top_codestreams+total_base_codestreams));
    
    // Write top-level headers and codestreams
    jp2_data_references data_refs_in;
    if (preserve_codestream_links)
      data_refs_in = jpx_in.access_data_references();
    for (n=0; n < num_top_codestreams; n++)
      { 
        jpx_out.write_headers(NULL,NULL,n);
        jpx_codestream_source in_stream = in_default_streams[n];
        bool force_empty_code_blocks = in_stream.exists();
        if (!force_empty_code_blocks)
          in_stream = jpx_in.access_codestream(n);
        assert(in_stream.exists());
        copy_jpx_stream(in_stream,out_top_streams[n],
                        open_file_pathname,filename,data_refs_in,
                        jp2_family_in.uses_cache(),
                        preserve_codestream_links,force_codestream_links,
                        force_empty_code_blocks);
      }
        
    // Copy JPX metadata
    jpx_meta_manager meta_in = jpx_in.access_meta_manager();
    jpx_meta_manager meta_out = jpx_out.access_meta_manager();
    meta_out.copy(meta_in);
    
    // Write all remaining headers and metadata
    int i_param=0;
    void *addr_param=NULL;
    jp2_output_box *box=NULL;
    box = jpx_out.write_headers(&i_param,&addr_param);
    assert(box == NULL);
    // We have not called `jpx_layer_target::set_breakpoint' or
    // `jpx_codestream_target::set_breakpoint' so `write_headers' must
    // return NULL here; otherwise we would need to go around in a loop
    while ((box = jpx_out.write_metadata(&i_param,&addr_param)) != NULL)
      { 
        kdws_file *file;
        if ((file_server != NULL) && (addr_param == (void *)file_server) &&
            ((file = file_server->find_file(i_param)) != NULL))
          write_metadata_box_from_file(box,file);
      }
    
    // Finally write all relevant container codestreams
    default_base_streams = in_default_streams + num_top_codestreams;
    for (w=0; w < num_containers; w++)
      { 
        jpx_container_source container_src = jpx_in.access_container(w);
        jpx_container_target container_tgt = jpx_out.access_container(w);
        assert(container_src.exists() && container_tgt.exists());
        int first_stream_idx, num_streams;
        first_stream_idx = container_src.get_base_codestreams(num_streams);
        int num_reps=0; container_src.count_repetitions(num_reps);
        assert(num_reps > 0);
        for (int r=0; r < num_reps; r++)
          for (n=0; n < num_streams; n++)
            { 
              jpx_codestream_source in_stream =
                container_src.access_codestream(n,r,true);
              bool force_empty_code_blocks = !in_stream.exists();
              if (force_empty_code_blocks)
                in_stream = default_base_streams[n];
              assert(in_stream.exists());
              jpx_codestream_target out_stream =
                container_tgt.access_codestream(n);
              copy_jpx_stream(in_stream,out_stream,
                              open_file_pathname,filename,data_refs_in,
                              jp2_family_in.uses_cache(),
                              preserve_codestream_links,
                              force_codestream_links,
                              force_empty_code_blocks);
            }
        default_base_streams += num_streams;
      }
    assert(default_base_streams <=
           (in_default_streams + num_top_codestreams+total_base_codestreams));
  }
  catch (kdu_exception exc)
  {
    delete[] in_default_streams;
    delete[] out_top_streams;
    throw exc;
  }
  
  // Clean up
  delete[] in_default_streams;
  delete[] out_top_streams;
  jpx_out.close();
  tgt.close();
}

/*****************************************************************************/
/*                        kdws_renderer::save_as_raw                         */
/*****************************************************************************/

void
  kdws_renderer::save_as_raw(const char *filename)
{
  compositor->halt_processing();
  kdu_ilayer_ref ilyr = compositor->get_next_ilayer(kdu_ilayer_ref());
  kdu_istream_ref istr = compositor->get_ilayer_stream(ilyr,0);
  if (!istr)
    { kdu_error e; e << "No active codestream is available for writing the "
      "requested output file."; }
  kdu_codestream cs_in = compositor->access_codestream(istr);
  kdu_simple_file_target file_out;
  file_out.open(filename);
  copy_codestream(&file_out,cs_in,false);
  file_out.close();
}


/* ========================================================================= */
/*                    COMMAND HANDLERS for kdws_renderer                     */
/* ========================================================================= */

BEGIN_MESSAGE_MAP(kdws_renderer, CCmdTarget)
// File Menu Handlers
	ON_COMMAND(ID_FILE_DISCONNECT, menu_FileDisconnect)
	ON_UPDATE_COMMAND_UI(ID_FILE_DISCONNECT, can_do_FileDisconnect)
  ON_COMMAND(ID_FILE_RECONNECT, menu_FileReconnect)
  ON_UPDATE_COMMAND_UI(ID_FILE_RECONNECT, can_do_FileReconnect)
  ON_COMMAND(ID_FILE_ERASEANDRECONNECT, menu_FileEraseAndReconnect)
  ON_UPDATE_COMMAND_UI(ID_FILE_ERASEANDRECONNECT, can_do_FileEraseAndReconnect)
  ON_COMMAND(ID_FILE_JPIPSAVEMODE, menu_FileJPIPSaveMode)
  ON_UPDATE_COMMAND_UI(ID_FILE_JPIPSAVEMODE, can_do_FileJPIPSaveMode)
  ON_COMMAND(ID_FILE_JPIPUNSAVEDMODE, menu_FileJPIPUnsavedMode)
  ON_UPDATE_COMMAND_UI(ID_FILE_JPIPUNSAVEDMODE, can_do_FileJPIPUnsavedMode)
	ON_COMMAND(ID_FILE_SAVE_AS, menu_FileSaveAs)
	ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_AS, can_do_FileSaveAs)
  ON_COMMAND(ID_FILE_SAVEASLINKED, menu_FileSaveAsLinked)
  ON_UPDATE_COMMAND_UI(ID_FILE_SAVEASLINKED, can_do_FileSaveAsLinked)
  ON_COMMAND(ID_FILE_SAVEASEMBEDDED, menu_FileSaveAsEmbedded)
  ON_UPDATE_COMMAND_UI(ID_FILE_SAVEASEMBEDDED, can_do_FileSaveAsEmbedded)
  ON_COMMAND(ID_FILE_SAVEANDRELOAD, menu_FileSaveReload)
  ON_UPDATE_COMMAND_UI(ID_FILE_SAVEANDRELOAD, can_do_FileSaveReload)
	ON_COMMAND(ID_FILE_PROPERTIES, menu_FileProperties)
	ON_UPDATE_COMMAND_UI(ID_FILE_PROPERTIES, can_do_FileProperties)
// View Menu Handlers (orientation and zoom)
	ON_COMMAND(ID_VIEW_HFLIP, menu_ViewHflip)
	ON_UPDATE_COMMAND_UI(ID_VIEW_HFLIP, can_do_ViewHflip)
	ON_COMMAND(ID_VIEW_VFLIP, menu_ViewVflip)
	ON_UPDATE_COMMAND_UI(ID_VIEW_VFLIP, can_do_ViewVflip)
	ON_COMMAND(ID_VIEW_ROTATE, menu_ViewRotate)
	ON_UPDATE_COMMAND_UI(ID_VIEW_ROTATE, can_do_ViewRotate)
	ON_COMMAND(ID_VIEW_COUNTER_ROTATE, menu_ViewCounterRotate)
	ON_UPDATE_COMMAND_UI(ID_VIEW_COUNTER_ROTATE, can_do_ViewCounterRotate)
	ON_COMMAND(ID_VIEW_ZOOM_OUT, menu_ViewZoomOut)
	ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_OUT, can_do_ViewZoomOut)
	ON_COMMAND(ID_VIEW_ZOOM_IN, menu_ViewZoomIn)
	ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_IN, can_do_ViewZoomIn)
	ON_COMMAND(ID_VIEW_ZOOM_OUT_SLIGHTLY, menu_ViewZoomOutSlightly)
	ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_OUT_SLIGHTLY, can_do_ViewZoomOut)
	ON_COMMAND(ID_VIEW_ZOOM_IN_SLIGHTLY, menu_ViewZoomInSlightly)
	ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_IN_SLIGHTLY, can_do_ViewZoomIn)
  ON_COMMAND(ID_VIEW_OPTIMIZESCALE, menu_ViewOptimizescale)
  ON_UPDATE_COMMAND_UI(ID_VIEW_OPTIMIZESCALE, can_do_ViewOptimizescale)
	ON_COMMAND(ID_VIEW_RESTORE, menu_ViewRestore)
  ON_UPDATE_COMMAND_UI(ID_VIEW_RESTORE, can_do_ViewRestore)
	ON_COMMAND(ID_VIEW_REFRESH, menu_ViewRefresh)
	ON_UPDATE_COMMAND_UI(ID_VIEW_REFRESH, can_do_ViewRefresh)
// View Menu Handlers (quality layers)
	ON_COMMAND(ID_LAYERS_LESS, menu_LayersLess)
	ON_UPDATE_COMMAND_UI(ID_LAYERS_LESS, can_do_LayersLess)
	ON_COMMAND(ID_LAYERS_MORE, menu_LayersMore)
	ON_UPDATE_COMMAND_UI(ID_LAYERS_MORE, can_do_LayersMore)
// View Menu Handlers (status display)
	ON_COMMAND(ID_STATUS_TOGGLE, menu_StatusToggle)
	ON_UPDATE_COMMAND_UI(ID_STATUS_TOGGLE, can_do_StatusToggle)
// View Menu Handlers (image window)
	ON_COMMAND(ID_VIEW_WIDEN, menu_ViewWiden)
	ON_UPDATE_COMMAND_UI(ID_VIEW_WIDEN, can_do_ViewWiden)
	ON_COMMAND(ID_VIEW_SHRINK, menu_ViewShrink)
	ON_UPDATE_COMMAND_UI(ID_VIEW_SHRINK, can_do_ViewShrink)
  ON_COMMAND(ID_VIEW_SCALE_X2, menu_ViewScaleX2)
  ON_COMMAND(ID_VIEW_SCALE_X1, menu_ViewScaleX1)
// View Menu Handlers (navigation)
	ON_COMMAND(ID_COMPONENT1, menu_NavComponent1)
	ON_UPDATE_COMMAND_UI(ID_COMPONENT1, can_do_NavComponent1)
	ON_COMMAND(ID_COMPONENT_NEXT, menu_NavComponentNext)
	ON_UPDATE_COMMAND_UI(ID_COMPONENT_NEXT, can_do_NavComponentNext)
	ON_COMMAND(ID_COMPONENT_PREV, menu_NavComponentPrev)
	ON_UPDATE_COMMAND_UI(ID_COMPONENT_PREV, can_do_NavComponentPrev)
	ON_COMMAND(ID_COMPOSITING_LAYER, menu_NavCompositingLayer)
	ON_UPDATE_COMMAND_UI(ID_COMPOSITING_LAYER, can_do_NavCompositingLayer)
  ON_COMMAND(ID_MULTI_COMPONENT, menu_NavMultiComponent)
	ON_UPDATE_COMMAND_UI(ID_MULTI_COMPONENT, can_do_NavMultiComponent)
  ON_COMMAND(ID_TRACK_NEXT, menu_NavTrackNext)
  ON_UPDATE_COMMAND_UI(ID_TRACK_NEXT, can_do_NavTrackNext)
	ON_COMMAND(ID_IMAGE_NEXT, menu_NavImageNext)
	ON_UPDATE_COMMAND_UI(ID_IMAGE_NEXT, can_do_NavImageNext)
	ON_COMMAND(ID_IMAGE_PREV, menu_NavImagePrev)
	ON_UPDATE_COMMAND_UI(ID_IMAGE_PREV, can_do_NavImagePrev)
// Focus Menu Handlers
	ON_COMMAND(ID_FOCUS_OFF, menu_FocusOff)
	ON_UPDATE_COMMAND_UI(ID_FOCUS_OFF, can_do_FocusOff)
	ON_COMMAND(ID_FOCUS_HIGHLIGHTING, menu_FocusHighlighting)
	ON_UPDATE_COMMAND_UI(ID_FOCUS_HIGHLIGHTING, can_do_FocusHighlighting)
	ON_COMMAND(ID_FOCUS_WIDEN, menu_FocusWiden)
	ON_COMMAND(ID_FOCUS_SHRINK, menu_FocusShrink)
	ON_COMMAND(ID_FOCUS_LEFT, menu_FocusLeft)
	ON_COMMAND(ID_FOCUS_RIGHT, menu_FocusRight)
	ON_COMMAND(ID_FOCUS_UP, menu_FocusUp)
	ON_COMMAND(ID_FOCUS_DOWN, menu_FocusDown)
// Mode Menu Handlers
  ON_COMMAND(ID_MODE_FAST, menu_ModeFast)
	ON_UPDATE_COMMAND_UI(ID_MODE_FAST, can_do_ModeFast)
	ON_COMMAND(ID_MODE_FUSSY, menu_ModeFussy)
	ON_UPDATE_COMMAND_UI(ID_MODE_FUSSY, can_do_ModeFussy)
	ON_COMMAND(ID_MODE_RESILIENT, menu_ModeResilient)
	ON_UPDATE_COMMAND_UI(ID_MODE_RESILIENT, can_do_ModeResilient)
	ON_COMMAND(ID_MODE_RESILIENT_SOP, menu_ModeResilientSop)
	ON_UPDATE_COMMAND_UI(ID_MODE_RESILIENT_SOP, can_do_ModeResilientSop)
	ON_COMMAND(ID_MODE_SEEKABLE, menu_ModeSeekable)
	ON_UPDATE_COMMAND_UI(ID_MODE_SEEKABLE, can_do_ModeSeekable)
  ON_COMMAND(ID_MODE_SINGLETHREADED, menu_ModeSinglethreaded)
  ON_UPDATE_COMMAND_UI(ID_MODE_SINGLETHREADED, can_do_ModeSinglethreaded)
  ON_COMMAND(ID_MODE_MULTITHREADED, menu_ModeMultithreaded)
  ON_UPDATE_COMMAND_UI(ID_MODE_MULTITHREADED, can_do_ModeMultithreaded)
  ON_COMMAND(ID_MODE_MORETHREADS, menu_ModeMorethreads)
  ON_UPDATE_COMMAND_UI(ID_MODE_MORETHREADS, can_do_ModeMorethreads)
  ON_COMMAND(ID_MODE_LESSTHREADS, menu_ModeLessthreads)
  ON_UPDATE_COMMAND_UI(ID_MODE_LESSTHREADS, can_do_ModeLessthreads)
  ON_COMMAND(ID_MODE_RECOMMENDED_THREADS, menu_ModeRecommendedThreads)
  ON_UPDATE_COMMAND_UI(ID_MODE_RECOMMENDED_THREADS, can_do_ModeRecommendedThreads)
// Metadata Menu Handlers (metadata)
	ON_COMMAND(ID_METADATA_CATALOG, menu_MetadataCatalog)
	ON_UPDATE_COMMAND_UI(ID_METADATA_CATALOG, can_do_MetadataCatalog)
	ON_COMMAND(ID_METADATA_SWAPFOCUS, menu_MetadataSwapFocus)
	ON_UPDATE_COMMAND_UI(ID_METADATA_SWAPFOCUS, can_do_MetadataSwapFocus)
	ON_COMMAND(ID_METADATA_SHOW, menu_MetadataShow)
	ON_UPDATE_COMMAND_UI(ID_METADATA_SHOW, can_do_MetadataShow)
	ON_COMMAND(ID_METADATA_ADD, menu_MetadataAdd)
	ON_UPDATE_COMMAND_UI(ID_METADATA_ADD, can_do_MetadataAdd)
	ON_COMMAND(ID_METADATA_EDIT, menu_MetadataEdit)
	ON_UPDATE_COMMAND_UI(ID_METADATA_EDIT, can_do_MetadataEdit)
	ON_COMMAND(ID_METADATA_DELETE, menu_MetadataDelete)
	ON_UPDATE_COMMAND_UI(ID_METADATA_DELETE, can_do_MetadataDelete)
	ON_COMMAND(ID_METADATA_COPYLABEL, menu_MetadataCopyLabel)
	ON_UPDATE_COMMAND_UI(ID_METADATA_COPYLABEL, can_do_MetadataCopyLabel)
	ON_COMMAND(ID_METADATA_COPYLINK, menu_MetadataCopyLink)
	ON_UPDATE_COMMAND_UI(ID_METADATA_COPYLINK, can_do_MetadataCopyLink)
	ON_COMMAND(ID_METADATA_CUT, menu_MetadataCut)
	ON_UPDATE_COMMAND_UI(ID_METADATA_CUT, can_do_MetadataCut)
	ON_COMMAND(ID_METADATA_PASTENEW, menu_MetadataPasteNew)
	ON_UPDATE_COMMAND_UI(ID_METADATA_PASTENEW, can_do_MetadataPasteNew)
  ON_COMMAND(ID_METADATA_DUPLICATE, menu_MetadataDuplicate)
  ON_UPDATE_COMMAND_UI(ID_METADATA_DUPLICATE, can_do_MetadataDuplicate)
  ON_COMMAND(ID_METADATA_UNDO, menu_MetadataUndo)
  ON_UPDATE_COMMAND_UI(ID_METADATA_UNDO, can_do_MetadataUndo)
  ON_COMMAND(ID_METADATA_REDO, menu_MetadataRedo)
  ON_UPDATE_COMMAND_UI(ID_METADATA_REDO, can_do_MetadataRedo)
// Metadata Menu Handlers (overlays)
	ON_COMMAND(ID_OVERLAY_ENABLED, menu_OverlayEnable)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_ENABLED, can_do_OverlayEnable)
	ON_COMMAND(ID_OVERLAY_FLASHING, menu_OverlayFlashing)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_FLASHING, can_do_OverlayFlashing)
	ON_COMMAND(ID_OVERLAY_AUTOFLASH, menu_OverlayAutoFlash)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_AUTOFLASH, can_do_OverlayAutoFlash)
	ON_COMMAND(ID_OVERLAY_TOGGLE, menu_OverlayToggle)
  ON_UPDATE_COMMAND_UI(ID_OVERLAY_TOGGLE, can_do_OverlayToggle)
	ON_COMMAND(ID_OVERLAY_RESTRICT, menu_OverlayRestrict)
  ON_UPDATE_COMMAND_UI(ID_OVERLAY_RESTRICT, can_do_OverlayRestrict)
	ON_COMMAND(ID_OVERLAY_HEAVIER, menu_OverlayHeavier)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_HEAVIER, can_do_OverlayHeavier)
	ON_COMMAND(ID_OVERLAY_LIGHTER, menu_OverlayLighter)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_LIGHTER, can_do_OverlayLighter)
  ON_COMMAND(ID_OVERLAY_DOUBLESIZE, menu_OverlayDoubleSize)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_DOUBLESIZE, can_do_OverlayDoubleSize)
	ON_COMMAND(ID_OVERLAY_HALVESIZE, menu_OverlayHalveSize)
	ON_UPDATE_COMMAND_UI(ID_OVERLAY_HALVESIZE, can_do_OverlayHalveSize)
// Play Menu Handlers
	ON_COMMAND(ID_PLAY_STARTFORWARD, menu_PlayStartForward)
	ON_UPDATE_COMMAND_UI(ID_PLAY_STARTFORWARD, can_do_PlayStartForward)
	ON_COMMAND(ID_PLAY_STARTBACKWARD, menu_PlayStartBackward)
	ON_UPDATE_COMMAND_UI(ID_PLAY_STARTBACKWARD, can_do_PlayStartBackward)
	ON_COMMAND(ID_PLAY_STOP, menu_PlayStop)
	ON_UPDATE_COMMAND_UI(ID_PLAY_STOP, can_do_PlayStop)
	ON_COMMAND(ID_PLAY_RENDERALL, menu_PlayRenderAll)
	ON_UPDATE_COMMAND_UI(ID_PLAY_RENDERALL, can_do_PlayRenderAll)
	ON_COMMAND(ID_PLAY_REPEAT, menu_PlayRepeat)
	ON_UPDATE_COMMAND_UI(ID_PLAY_REPEAT, can_do_PlayRepeat)
	ON_COMMAND(ID_PLAY_NATIVE, menu_PlayNative)
	ON_UPDATE_COMMAND_UI(ID_PLAY_NATIVE, can_do_PlayNative)
	ON_COMMAND(ID_PLAY_CUSTOM, menu_PlayCustom)
	ON_UPDATE_COMMAND_UI(ID_PLAY_CUSTOM, can_do_PlayCustom)
	ON_COMMAND(ID_PLAY_FRAMERATEUP, menu_PlayFrameRateUp)
	ON_UPDATE_COMMAND_UI(ID_PLAY_FRAMERATEUP, can_do_PlayFrameRateUp)
	ON_COMMAND(ID_PLAY_FRAMERATEDOWN, menu_PlayFrameRateDown)
	ON_UPDATE_COMMAND_UI(ID_PLAY_FRAMERATEDOWN, can_do_PlayFrameRateDown)

  END_MESSAGE_MAP()

/* ========================================================================= */
/*                       File Menu Command Handlers                          */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdws_renderer::menu_FileDisconnect                      */
/*****************************************************************************/

void kdws_renderer::menu_FileDisconnect() 
{
  if ((jpip_client != NULL) && jpip_client->is_alive(client_request_queue_id))
    jpip_client->disconnect(false,30000,client_request_queue_id,false);
             // Give it a long timeout so as to avoid accidentally breaking
             // an HTTP request channel which might be shared with other
             // request queues -- but let's not wait around here.
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FileReconnect                      */
/*****************************************************************************/

void kdws_renderer::menu_FileReconnect()
{
  if ((jpip_client == NULL) ||
      jpip_client->is_alive(client_request_queue_id))
    return;
  client_request_queue_id = manager->reconnect_jpip_client(jpip_client,
                                                           window);
  if (client_request_queue_id >= 0)
    { 
      jpip_client_received_queue_bytes = 0;
      jpip_client_received_total_bytes = 0;
      jpip_interval_start_bytes = 0;
      jpip_interval_start_time = 0.0;
      jpip_client_status = jpip_client->get_status(client_request_queue_id);
      display_status();
    }  
}

/*****************************************************************************/
/*               kdws_renderer::note_jpip_client_reconnecting                */
/*****************************************************************************/

void kdws_renderer::note_jpip_client_reconnecting(kdu_client *client)
{ 
  if (client != this->jpip_client)
    return; // Does not apply to us
  client_request_queue_id = manager->reconnect_jpip_client(jpip_client,
                                                           window);
  if (client_request_queue_id >= 0)
    { 
      jpip_client_received_queue_bytes = 0;
      jpip_client_received_total_bytes = 0;
      jpip_interval_start_bytes = 0;
      jpip_interval_start_time = 0.0;
      jpip_client_status = jpip_client->get_status(client_request_queue_id);
      display_status();
    }
}

/*****************************************************************************/
/*                 kdws_renderer::menu_FileEraseAndReconnect                 */
/*****************************************************************************/

void kdws_renderer::menu_FileEraseAndReconnect()
{
  if ((jpip_client == NULL) || jpip_client->is_alive(-1) ||
      processing_call_to_open_file)
    return;
  if (manager->count_peers(jpip_client,window) > 0)
    { kdu_warning w; w << "There are other open windows that are using the "
      "same disconnected JPIP client cache.  Cannot erase and reconnect "
      "unless these peer windows are first closed.";
      return;
    }
  
  // Shut down almost everything, but leave the file/URL names in place and
  // leave the `jpip_client' alone for the moment.
  deactivate();
  file_in.close();
  cache_in.close();
  jpx_in.close();
  mj2_in.close();
  jp2_family_in.close();
  
  // Now restart the JPIP service
  int new_queue_id = manager->restart_jpip_client(jpip_client,window);
  if (new_queue_id < 0)
    { 
      close_file();
    }
  else
    { 
      client_request_queue_id = new_queue_id;
      one_time_jpip_request = jpip_client->is_one_time_request();
      respect_initial_jpip_request = one_time_jpip_request;
      if ((client_request_queue_id == 0) && (!one_time_jpip_request) &&
          jpip_client->connect_request_has_non_empty_window())
        respect_initial_jpip_request = true;
      open_filename = jpip_client->get_target_name();
      jpip_client_status = jpip_client->get_status(client_request_queue_id);
      cache_in.attach_to(jpip_client);
      status_id = KDS_STATUS_CACHE;
      jp2_family_in.open(&cache_in);
      window->set_progress_bar(0.0);
      if (metashow != NULL)
        { 
          metashow->activate(&jp2_family_in,open_filename);
          refresh_metadata(get_current_time());
        }
      open_file(NULL); // Continues the opening process
    }
}

/*****************************************************************************/
/*                   kdws_renderer::menu_FileJPIPSaveMode                    */
/*****************************************************************************/

void kdws_renderer::menu_FileJPIPSaveMode()
{ 
  bool can_change=false;
  bool will_save =
  manager->check_cache_save_mode(jpip_client,can_change);
  if (will_save || !can_change)
    return; // Nothing to do
  manager->set_cache_save_mode(jpip_client,true);
}

/*****************************************************************************/
/*                  kdws_renderer::can_do_FileJPIPSaveMode                   */
/*****************************************************************************/

void kdws_renderer::can_do_FileJPIPSaveMode(CCmdUI* pCmdUI)
{ 
  bool can_change=false;
  bool will_save =
    manager->check_cache_save_mode(jpip_client,can_change);
  pCmdUI->Enable(can_change);
  pCmdUI->SetCheck(will_save);
}

/*****************************************************************************/
/*                  kdws_renderer::menu_FileJPIPUnsavedMode                  */
/*****************************************************************************/

void kdws_renderer::menu_FileJPIPUnsavedMode()
{
  bool can_change=false;
  bool will_save =
  manager->check_cache_save_mode(jpip_client,can_change);
  if (!(will_save && can_change))
    return; // Nothing to do
  if (jpip_client != NULL)
    { 
      int response =
        window->interrogate_user(
                       "You are about to convert your JPIP client to "
                       "PRIVATE mode, deleting any saved cache file, "
                       "even if it was pre-existing; are you sure?",
                       "Cancel","Go Private");
      if (!response)
        return;
    }
  manager->set_cache_save_mode(jpip_client,false);
}

/*****************************************************************************/
/*                kdws_renderer::can_do_FileJPIPUnsavedMode                  */
/*****************************************************************************/

void kdws_renderer::can_do_FileJPIPUnsavedMode(CCmdUI* pCmdUI)
{ 
  bool can_change=false;
  bool will_save =
    manager->check_cache_save_mode(jpip_client,can_change);
  pCmdUI->Enable(can_change);
  pCmdUI->SetCheck(!will_save);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_FileSaveAs                          */
/*****************************************************************************/

void
  kdws_renderer::menu_FileSaveAs()
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  save_as(true,false);
}

/*****************************************************************************/
/*                 kdws_renderer::menu_FileSaveAsLinked                      */
/*****************************************************************************/

void kdws_renderer::menu_FileSaveAsLinked()
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  save_as(true,true);
}

/*****************************************************************************/
/*                 kdws_renderer::menu_FileSaveAsEmbedded                    */
/*****************************************************************************/

void kdws_renderer::menu_FileSaveAsEmbedded()
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  save_as(false,false);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_FileSaveReload                      */
/*****************************************************************************/

void kdws_renderer::menu_FileSaveReload()
{
  if ((compositor == NULL) || (animator != NULL) || mj2_in.exists() ||
      (!have_unsaved_edits) || (open_file_pathname == NULL))
    return;

  if (editor != NULL)
    {
      if (window->interrogate_user(
                           "You have an open metadata editor, which will "
                           "be closed if you continue.  Do you still want "
                           "to save and reload the file?",
                           "Cancel","Continue") == 0)
        return;
      editor->close();
      editor = NULL;
    }

  if (!save_without_asking)
    {
      int response = window->interrogate_user(
                         "You are about to save to the file which you are "
                         "currently viewing.  In practice, the saved data "
                         "will be written to a temporary file which will "
                         "not replace the open file until after you close "
                         "it the window or load another file into it.  "
                         "Nevertheless, this operation will eventually "
                         "overwrite the existing image file.  In the process, "
                         "there is a chance that you might lose some "
                         "metadata which could not be internally "
                         "represented.  Are you sure you wish to do this?",
                         "Cancel","Proceed","Don't ask me again");
      if (response == 0)
        return;
      if (response == 2)
        save_without_asking = true;
    }
  if (manager->get_open_file_retain_count(open_file_pathname) != 1)
    {
      window->interrogate_user(
                         "Cannot perform this operation at present.  There "
                         "are other open windows in the application which "
                         "are also using this same file -- you may not "
                         "overwrite and reload it until they are closed.",
                         "OK");
      return;
    }

  if (!save_over())
    return;
  char *filename = new char[strlen(open_file_pathname)+1];
  strcpy(filename,open_file_pathname);
  bool save_without_asking = this->save_without_asking;
  open_file(filename);
  this->save_without_asking = save_without_asking;
  delete[] filename;
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FileProperties                     */
/*****************************************************************************/

void kdws_renderer::menu_FileProperties() 
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  compositor->halt_processing(); // Stops any current processing
  kdu_dims region = find_focus_box();
  kdu_ilayer_ref ilyr =
    compositor->get_next_visible_ilayer(kdu_ilayer_ref(),region);
  kdu_istream_ref istr = compositor->get_ilayer_stream(ilyr,0);
  if (!istr)
    return;
  int c_idx;
  kdu_codestream codestream = compositor->access_codestream(istr);
  if (!codestream.exists())
    { 
      window->interrogate_user("Codestream not yet available in JPIP cache.","OK");
      return;
    }
  compositor->get_istream_info(istr,c_idx);

  kdu_message_queue collector;
  collector.start_message();
  collector << "Properties for code-stream " << c_idx << "\n\n";

  // Textualize any comments
  kdu_codestream_comment com;
  while ((com = codestream.get_comment(com)).exists())
    {
      int length;
      const char *comtext = com.get_text();
      if (*comtext != '\0')
        collector << ">>>>> Code-stream comment:\n" << comtext << "\n";
      else if ((length = com.get_data(NULL,0,INT_MAX)) > 0)
        collector << ">>>>> Binary code-stream comment ("
                  << length << " bytes)\n";
    }

  // Textualize the main code-stream header
  kdu_params *root = codestream.access_siz();
  collector << "<<<<< Main Header >>>>>\n";
  root->textualize_attributes(collector,-1,-1);

  // Textualize the tile headers
  codestream.apply_input_restrictions(0,0,0,0,NULL);
  codestream.change_appearance(false,false,false);
  kdu_dims valid_tiles; codestream.get_valid_tiles(valid_tiles);
  kdu_coords idx;
  for (idx.y=0; idx.y < valid_tiles.size.y; idx.y++)
    for (idx.x=0; idx.x < valid_tiles.size.x; idx.x++)
      {
        kdu_dims tile_dims;
        codestream.get_tile_dims(valid_tiles.pos+idx,-1,tile_dims);
        int tnum = idx.x + idx.y*valid_tiles.size.x;
        collector << "<<<<< Tile " << tnum << " >>>>>"
                  << " Canvas coordinates: "
                  << "y = " << tile_dims.pos.y
                  << "; x = " << tile_dims.pos.x
                  << "; height = " << tile_dims.size.y
                  << "; width = " << tile_dims.size.x << "\n";

        // Open and close each tile in case the code-stream has not yet been
        // fully parsed
        try {
            kdu_tile tile = codestream.open_tile(valid_tiles.pos+idx);
            tile.close();
          }
        catch (int) { // Error occurred during tile open
            close_file();
            return;
          }

        root->textualize_attributes(collector,tnum,tnum);
      }

  // Display the properties.
  collector.flush(true);
  kdws_properties properties(codestream,collector.pop_message());
  properties.DoModal();
}

/* ========================================================================= */
/*             View Menu Command Handlers (orientation and zoom)             */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdws_renderer::menu_ViewVflip                        */
/*****************************************************************************/

void kdws_renderer::menu_ViewVflip() 
{
  if (working_buffer == NULL)
    return;
  orientation.vflip();
  calculate_rel_view_params();
  view_centre_y = 1.0 - view_centre_y;
  rel_focus.vflip();
  initialize_regions(true);
}

/*****************************************************************************/
/*                      kdws_renderer::menu_ViewHflip                        */
/*****************************************************************************/

void kdws_renderer::menu_ViewHflip()
{
  if (working_buffer == NULL)
    return;
  orientation.hflip();
  calculate_rel_view_params();
  view_centre_x = 1.0 - view_centre_x;
  rel_focus.hflip();
  initialize_regions(true);
}

/*****************************************************************************/
/*                       kdws_renderer::menu_ViewRotate                      */
/*****************************************************************************/

void kdws_renderer::menu_ViewRotate() 
{
  if (working_buffer == NULL)
    return;
  orientation.transpose();
  orientation.hflip();
  calculate_rel_view_params();
  double f_tmp = view_centre_y;
  view_centre_y = view_centre_x;
  view_centre_x = 1.0-f_tmp;
  rel_focus.transpose();
  rel_focus.hflip();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  initialize_regions(true);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_ViewCounterRotate                   */
/*****************************************************************************/

void kdws_renderer::menu_ViewCounterRotate() 
{
  if (working_buffer == NULL)
    return;
  orientation.transpose();
  orientation.vflip();
  calculate_rel_view_params();
  double f_tmp = view_centre_x;
  view_centre_x = view_centre_y;
  view_centre_y = 1.0-f_tmp;
  rel_focus.transpose();
  rel_focus.vflip();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  initialize_regions(true);
}

/*****************************************************************************/
/*                     kdws_renderer::menu_ViewZoomIn                        */
/*****************************************************************************/

void kdws_renderer::menu_ViewZoomIn() 
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == max_rendering_scale)
    return;
  rendering_scale = increase_scale(rendering_scale,false);
  calculate_rel_view_params();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                      kdws_renderer::menu_ViewZoomOut                      */
/*****************************************************************************/

void kdws_renderer::menu_ViewZoomOut() 
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == min_rendering_scale)
    return;
  rendering_scale = decrease_scale(rendering_scale,false);
  calculate_rel_view_params();
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                 kdws_renderer::menu_ViewZoomInSlightly                    */
/*****************************************************************************/

void kdws_renderer::menu_ViewZoomInSlightly() 
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == max_rendering_scale)
    return;
  rendering_scale = increase_scale(rendering_scale,true);
  calculate_rel_view_params();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                 kdws_renderer::menu_ViewZoomOutSlightly                   */
/*****************************************************************************/

void kdws_renderer::menu_ViewZoomOutSlightly() 
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == min_rendering_scale)
    return;
  rendering_scale = decrease_scale(rendering_scale,true);
  calculate_rel_view_params();
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                   kdws_renderer::menu_ViewOptimizescale                   */
/*****************************************************************************/

void kdws_renderer::menu_ViewOptimizescale()
{
  if ((compositor == NULL) || !configuration_complete)
    return;
  float min_scale = rendering_scale * 0.5F;
  float max_scale = rendering_scale * 2.0F;
  if (min_rendering_scale > 0.0F)
    {
      if (min_scale < min_rendering_scale)
        min_scale = min_rendering_scale;
      if (max_scale < min_rendering_scale)
        max_scale = min_rendering_scale;
    }
  if (max_rendering_scale > 0.0F)
    {
      if (min_scale > max_rendering_scale)
        min_scale = max_rendering_scale;
      if (max_scale > max_rendering_scale)
        max_scale = max_rendering_scale;
    }
  kdu_dims region_basis = find_focus_box();
  float new_scale =
    compositor->find_optimal_scale(region_basis,rendering_scale,
                                   min_scale,max_scale);
  if (new_scale == rendering_scale)
    return;
  calculate_rel_view_params();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  rendering_scale = new_scale;
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                     kdws_renderer::menu_ViewRestore                      */
/*****************************************************************************/

void kdws_renderer::menu_ViewRestore() 
{
  if (working_buffer == NULL)
    return;
  calculate_rel_view_params();
  if (orientation.vf)
    {
      view_centre_y = 1.0-view_centre_y;
      rel_focus.vflip();
    }
  if (orientation.hf)
    {
      view_centre_x = 1.0-view_centre_x;
      rel_focus.hflip();
    }
  if (orientation.trans)
    {
      double tmp = view_centre_y;
      view_centre_y = view_centre_x;
      view_centre_x = tmp;
      rel_focus.transpose();
    }
  orientation.reset();
  initialize_regions(true);
}

/*****************************************************************************/
/*                     kdws_renderer::menu_ViewRefresh                       */
/*****************************************************************************/

void kdws_renderer::menu_ViewRefresh() 
{
  refresh_display();
}


/* ========================================================================= */
/*                View Menu Command Handlers (quality layers)                */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdws_renderer::menu_LayersLess                       */
/*****************************************************************************/

void kdws_renderer::menu_LayersLess()
{
  if (compositor == NULL)
    return;
  int max_layers = compositor->get_max_available_quality_layers();
  bool need_update = false;
  if (quality_limiting_mode < 2)
    { // Decrease quality limiting mode first
      quality_limiting_mode++;
      need_update = true;
      configure_quality_limiting();
    }
  else
    { 
      if (max_display_layers > max_layers)
        max_display_layers = max_layers;
      if (max_display_layers <= 1)
        max_display_layers = 1;
      else
        { 
          max_display_layers--;
          need_update = true;
          compositor->set_max_quality_layers(max_display_layers);
          if (enable_region_posting)
            update_client_window_of_interest();
        }
    }
  if (need_update)
    { 
      refresh_display();
      if (!enable_region_posting)
        status_id = KDS_STATUS_LAYER_RES;
      display_status();
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_LayersMore                         */
/*****************************************************************************/

void kdws_renderer::menu_LayersMore() 
{
  if (compositor == NULL)
    return;
  int max_layers = compositor->get_max_available_quality_layers();
  bool need_update = false;
  if (max_display_layers < max_layers)
    { 
      max_display_layers++;
      if (max_display_layers >= max_layers)
        max_display_layers = 1<<16; // Make sure there are no layer constraints
      need_update = true;
      compositor->set_max_quality_layers(max_display_layers);
      if (enable_region_posting)
        update_client_window_of_interest();
    }
  else if (quality_limiting_mode > 0)
    { 
      quality_limiting_mode--;
      need_update = true;
      configure_quality_limiting();
    }
  if (need_update)
    { 
      refresh_display();
      if (!enable_region_posting)
        status_id = KDS_STATUS_LAYER_RES;
      display_status();
    }
}


/* ========================================================================= */
/*                View Menu Command Handlers (status display)                */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdws_renderer::menu_StatusToggle                        */
/*****************************************************************************/

void kdws_renderer::menu_StatusToggle() 
{
  if (status_id == KDS_STATUS_LAYER_RES)
    status_id = KDS_STATUS_DIMS;
  else if (status_id == KDS_STATUS_DIMS)
    status_id = KDS_STATUS_MEM;
  else if (status_id == KDS_STATUS_MEM)
    status_id = KDS_STATUS_CACHE;
  else if (status_id == KDS_STATUS_CACHE)
    status_id = KDS_STATUS_PLAYSTATS;
  else if (status_id == KDS_STATUS_PLAYSTATS)
    status_id = KDS_STATUS_LAYER_RES;
  if ((status_id == KDS_STATUS_CACHE) && (jpip_client == NULL))
    status_id = KDS_STATUS_PLAYSTATS;
  if ((status_id == KDS_STATUS_PLAYSTATS) && (animator == NULL))
    status_id = KDS_STATUS_LAYER_RES;
  display_status();
}


/* ========================================================================= */
/*                 View Menu Command Handlers (image window)                 */
/* ========================================================================= */

/*****************************************************************************/
/*                     kdws_renderer::menu_ViewWiden                         */
/*****************************************************************************/

void kdws_renderer::menu_ViewWiden() 
{
  RECT rect;
  kdu_dims target_dims;
  kdu_coords limits;

  // Find screen dimensions
  CDC *dc = image_view->GetDC();
  limits.x = dc->GetDeviceCaps(HORZRES);
  limits.y = dc->GetDeviceCaps(VERTRES);
  image_view->ReleaseDC(dc);

  // Find current window location and dimensions
  window->GetWindowRect(&rect);
  target_dims.pos.x = rect.left;
  target_dims.pos.y = rect.top;
  target_dims.size.x = rect.right-rect.left;
  target_dims.size.y = rect.bottom-rect.top;

  // Calculate new dimensions
  kdu_coords new_size = target_dims.size;
  new_size.x += new_size.x / 2;
  new_size.y += new_size.y / 2;
  if (new_size.x > limits.x)
    new_size.x = limits.x;
  if (new_size.y > limits.y)
    new_size.y = limits.y;
  if (target_dims.size == new_size)
    return;

  target_dims.pos.x -= (new_size.x - target_dims.size.x) / 2;
  target_dims.pos.y -= (new_size.y - target_dims.size.y) / 2;
  target_dims.size = new_size;

  // Make sure new location fits inside window
  if (target_dims.pos.x < 0)
    target_dims.pos.x = 0;
  if (target_dims.pos.y < 0)
    target_dims.pos.y = 0;
  limits -= target_dims.pos + target_dims.size;
  if (limits.x < 0)
    target_dims.pos.x += limits.x;
  if (limits.y < 0)
    target_dims.pos.y += limits.y;

  // Reposition window
  calculate_rel_view_params();
  window->SetWindowPos(NULL,target_dims.pos.x,target_dims.pos.y,
                       target_dims.size.x,target_dims.size.y,
                       SWP_SHOWWINDOW | SWP_NOZORDER);
  scroll_to_view_anchors();
  view_centre_known = false;
}

/*****************************************************************************/
/*                    kdws_renderer::menu_ViewShrink                         */
/*****************************************************************************/

void kdws_renderer::menu_ViewShrink() 
{
  RECT rect;
  kdu_coords target_size;

  calculate_rel_view_params();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  window->GetWindowRect(&rect);
  target_size.x = rect.right-rect.left;
  target_size.y = rect.bottom-rect.top;
  target_size.x -= target_size.x / 3;
  target_size.y -= target_size.y / 3;
  if (target_size.x < 100)
    target_size.x = rect.right-rect.left;
  if (target_size.y < 100)
    target_size.y = rect.bottom-rect.top;
  window->SetWindowPos(NULL,0,0,target_size.x,target_size.y,
                       SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOZORDER);
  scroll_to_view_anchors();
  view_centre_known = false;
}

/*****************************************************************************/
/*                   kdws_renderer::menu_ViewScaleX2                         */
/*****************************************************************************/

void kdws_renderer::menu_ViewScaleX2()
{
  if (pixel_scale != 2)
    { 
      pixel_scale = 2;
      if (quality_limiting_mode)
        configure_quality_limiting();
      calculate_rel_view_params();
      rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
      image_view->set_max_view_size(image_dims.size,pixel_scale);
      invalidate_configuration();
      initialize_regions(false);
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_ViewScaleX1                        */
/*****************************************************************************/

void kdws_renderer::menu_ViewScaleX1()
{
  if (pixel_scale != 1)
    { 
      pixel_scale = 1;
      if (quality_limiting_mode)
        configure_quality_limiting();
      image_view->set_max_view_size(image_dims.size,pixel_scale);
      invalidate_configuration();
      initialize_regions(false);
    }
}


/* ========================================================================= */
/*                 View Menu Command Handlers (navigation)                   */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdws_renderer::menu_NavComponent1                       */
/*****************************************************************************/

void kdws_renderer::menu_NavComponent1() 
{
  if ((compositor == NULL) || (single_component_idx == 0) ||
      (animator != NULL))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx < 0)
    { // See if we can find a good codestream to go to
      kdu_coords point;
      kdu_dims focus_box;
      if (rel_focus.is_valid() &&
          !(focus_box = find_focus_box()).is_empty())
        {
          point.x = focus_box.pos.x + (focus_box.size.x>>1);
          point.y = focus_box.pos.y + (focus_box.size.y>>1);
        }
      else if (!view_dims.is_empty())
        {
          point.x = view_dims.pos.x + (view_dims.size.x>>1);
          point.y = view_dims.pos.y + (view_dims.size.y>>1);
        }
      int stream_idx;
      kdu_ilayer_ref ilyr;
      kdu_istream_ref istr;
      if ((ilyr = compositor->find_point(point)).exists() &&
          (istr = compositor->get_ilayer_stream(ilyr,0)).exists())
        compositor->get_istream_info(istr,stream_idx);
      else
        stream_idx = 0;
      set_codestream(stream_idx);
    }
  else
    {
      single_component_idx = 0;
      frame = jpx_frame();
      calculate_rel_view_params();
      invalidate_configuration();
      initialize_regions(false);
    }
}

/*****************************************************************************/
/*                  kdws_renderer::menu_NavComponentNext                     */
/*****************************************************************************/

void kdws_renderer::menu_NavComponentNext()
{
  if ((compositor == NULL) || (animator != NULL) ||
      ((max_components >= 0) && (single_component_idx >= (max_components-1))))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx < 0)
    { // See if we can find a good codestream to go to
      kdu_coords point;
      kdu_dims focus_box;
      if (rel_focus.is_valid() &&
          !(focus_box = find_focus_box()).is_empty())
        { 
          point.x = focus_box.pos.x + (focus_box.size.x>>1);
          point.y = focus_box.pos.y + (focus_box.size.y>>1);
        }
      else if (!view_dims.is_empty())
        {
          point.x = view_dims.pos.x + (view_dims.size.x>>1);
          point.y = view_dims.pos.y + (view_dims.size.y>>1);
        }
      int stream_idx;
      kdu_ilayer_ref ilyr;
      kdu_istream_ref istr;
      if ((ilyr = compositor->find_point(point)).exists() &&
          (istr = compositor->get_ilayer_stream(ilyr,0)).exists())
        compositor->get_istream_info(istr,stream_idx);
      else
        stream_idx = 0;
      set_codestream(stream_idx);
    }
  else
    {
      single_component_idx++;
      frame = jpx_frame();
      calculate_rel_view_params();
      invalidate_configuration();
      initialize_regions(false);
    }
}

/*****************************************************************************/
/*                  kdws_renderer::menu_NavComponentPrev                     */
/*****************************************************************************/

void kdws_renderer::menu_NavComponentPrev() 
{
  if ((compositor == NULL) || (single_component_idx == 0))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx < 0)
    { // See if we can find a good codestream to go to
      kdu_coords point;
      kdu_dims focus_box;
      if (rel_focus.is_valid() &&
          !(focus_box = find_focus_box()).is_empty())
        { 
          point.x = focus_box.pos.x + (focus_box.size.x>>1);
          point.y = focus_box.pos.y + (focus_box.size.y>>1);
        }
      else if (!view_dims.is_empty())
        {
          point.x = view_dims.pos.x + (view_dims.size.x>>1);
          point.y = view_dims.pos.y + (view_dims.size.y>>1);
        }
      int stream_idx;
      kdu_ilayer_ref ilyr;
      kdu_istream_ref istr;
      if ((ilyr = compositor->find_point(point)).exists() &&
          (istr = compositor->get_ilayer_stream(ilyr,0)).exists())
        compositor->get_istream_info(istr,stream_idx);
      else
        stream_idx = 0;
      set_codestream(stream_idx);
    }
  else
    {
      single_component_idx--;
      frame = jpx_frame();
      calculate_rel_view_params();
      invalidate_configuration();
      initialize_regions(false);
    }
}

/*****************************************************************************/
/*               kdws_renderer::menu_NavCompositingLayer                     */
/*****************************************************************************/

void
 kdws_renderer::menu_NavCompositingLayer() 
{
  if ((compositor == NULL) || (single_layer_idx >= 0) || (animator != NULL))
    return;
  if (mj2_in.exists())
    { 
      if (single_component_idx < 0)
        return; // Nothing to do -- single-layer mode meaningless for MJ2
      kdu_uint32 trk;
      int frm, fld;
      if (mj2_in.find_stream(single_codestream_idx,trk,frm,fld) && (trk != 0))
        track_idx = trk;
      calculate_rel_view_params();
      single_component_idx = -1;
      adjust_rel_focus();
      rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
      invalidate_configuration();
      initialize_regions(false);
    }
  else
    { 
      int layer_idx = 0;
      if (single_component_idx < 0)
        { // See if we can find a good layer to go to
          kdu_coords point;
          kdu_dims focus_box;
          if (rel_focus.is_valid() &&
              !(focus_box = find_focus_box()).is_empty())
            { 
              point.x = focus_box.pos.x + (focus_box.size.x>>1);
              point.y = focus_box.pos.y + (focus_box.size.y>>1);
            }
          else if (!view_dims.is_empty())
            { 
              point.x = view_dims.pos.x + (view_dims.size.x>>1);
              point.y = view_dims.pos.y + (view_dims.size.y>>1);
            }
          int dcs_idx; bool opq; // Not actually used
          kdu_ilayer_ref ilyr = compositor->find_point(point);
          if (!(compositor->get_ilayer_info(ilyr,layer_idx,dcs_idx,opq) &&
                (layer_idx >= 0)))
            layer_idx = 0;
        }
      set_compositing_layer(layer_idx);
    }
}

/*****************************************************************************/
/*                 kdws_renderer::menu_NavMultiComponent                     */
/*****************************************************************************/

void kdws_renderer::menu_NavMultiComponent() 
{
 if ((compositor == NULL) || (animator != NULL) ||
      ((single_component_idx < 0) &&
       ((single_layer_idx < 0) || ((num_frames == 0) && num_frames_known))))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (num_frames_known && (num_frames == 0))
    menu_NavCompositingLayer();
  else
    {
      calculate_rel_view_params();
      single_component_idx = -1;
      single_layer_idx = -1;
      frame_idx = 0;
      frame_start = frame_end = 0.0;
      frame = jpx_frame();
    }
  invalidate_configuration();
  initialize_regions(false);  
}

/*****************************************************************************/
/*                kdws_renderer::can_do_NavMultiComponent                    */
/*****************************************************************************/

void kdws_renderer::can_do_NavMultiComponent(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(!((compositor == NULL) || (animator != NULL) ||
                   ((single_component_idx < 0) &&
                    ((single_layer_idx < 0) ||
                     ((num_frames == 0) && num_frames_known)))));
  pCmdUI->SetCheck((compositor != NULL) && (animator == NULL) &&
                   (single_component_idx < 0) && (single_layer_idx < 0));
}

/*****************************************************************************/
/*                   kdws_renderer::menu_NavImageNext                        */
/*****************************************************************************/

void
  kdws_renderer::menu_NavImageNext() 
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx >= 0)
    { // Advance to next code-stream
      if ((max_codestreams > 0) &&
          (single_codestream_idx >= (max_codestreams-1)))
        return;
      calculate_rel_view_params();
      single_codestream_idx++;
      single_component_idx = 0;
    }
  else if (single_layer_idx >= 0)
    { // Advance to next compositing layer
      if ((max_compositing_layer_idx >= 0) &&
          (single_layer_idx >= max_compositing_layer_idx))
        return;
      calculate_rel_view_params();
      single_layer_idx++;
    }
  else
    { // Advance to next frame in track
      if (num_frames_known && (frame_idx == (num_frames-1)))
        return;
      if (jpx_in.exists() && !frame.exists())
        return; // Still waiting for composition instructions
      calculate_rel_view_params();
      change_frame_idx(frame_idx+1);
    }
  invalidate_configuration();
  initialize_regions(false);  
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_NavImageNext                        */
/*****************************************************************************/

void
  kdws_renderer::can_do_NavImageNext(CCmdUI* pCmdUI) 
{
  if (animator != NULL)
    pCmdUI->Enable(FALSE);
  else if (single_component_idx >= 0)
    pCmdUI->Enable((compositor != NULL) &&
                   ((max_codestreams < 0) ||
                    (single_codestream_idx < (max_codestreams-1))));
  else if (single_layer_idx >= 0)
    pCmdUI->Enable((compositor != NULL) &&
                   ((max_compositing_layer_idx < 0) ||
                    (single_layer_idx < max_compositing_layer_idx)));
  else
    pCmdUI->Enable((compositor != NULL) &&
                   ((!num_frames_known) || (frame_idx < (num_frames-1))) &&
                   ((!jpx_in.exists()) || frame.exists()));
}

/*****************************************************************************/
/*                  kdws_renderer::menu_NavImagePrev                         */
/*****************************************************************************/

void
  kdws_renderer::menu_NavImagePrev() 
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_component_idx >= 0)
    { // Go to previous code-stream
      if (single_codestream_idx == 0)
        return;
      calculate_rel_view_params();
      single_codestream_idx--;
      single_component_idx = 0;
    }
  else if (single_layer_idx >= 0)
    { // Go to previous compositing layer
      if (single_layer_idx == 0)
        return;
      calculate_rel_view_params();
      single_layer_idx--;
    }
  else
    { // Go to previous frame in track
      if (frame_idx == 0)
        return;
      calculate_rel_view_params();
      change_frame_idx(frame_idx-1);
    }
  invalidate_configuration();
  initialize_regions(false);  
}

/*****************************************************************************/
/*                   kdws_renderer::can_do_NavImagePrev                      */
/*****************************************************************************/

void
  kdws_renderer::can_do_NavImagePrev(CCmdUI* pCmdUI) 
{
  if (animator != NULL)
    pCmdUI->Enable(FALSE);
  else if (single_component_idx >= 0)
    pCmdUI->Enable((compositor != NULL) && (single_codestream_idx > 0));
  else if (single_layer_idx >= 0)
    pCmdUI->Enable((compositor != NULL) && (single_layer_idx > 0));
  else
    pCmdUI->Enable((compositor != NULL) && (frame_idx > 0));
}

/*****************************************************************************/
/*                   kdws_renderer::menu_NavTrackNext                        */
/*****************************************************************************/

void kdws_renderer::menu_NavTrackNext()
{
  if ((compositor == NULL) || ((max_track_idx <= 1) && num_tracks_known))
    return;
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (single_layer_idx >= 0)
    menu_NavMultiComponent();
  else if (animator == NULL)
    { 
      track_idx++;
      if (num_tracks_known && (track_idx > max_track_idx))
        track_idx = 1;
      frame = jpx_frame();
      invalidate_configuration();
      initialize_regions(false);
    }
  else
    { 
      bool reverse_play = animator->get_reverse();
      stop_animation();
      track_idx++;
      if (num_tracks_known && (track_idx > max_track_idx))
        track_idx = 1;
      frame = jpx_frame();
      invalidate_configuration();
      initialize_regions(false);
      start_animation(reverse_play);
    }
}


/* ========================================================================= */
/*                        Focus Menu Command Handlers                        */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdws_renderer::menu_FocusOff                        */
/*****************************************************************************/

void kdws_renderer::menu_FocusOff() 
{
  if (rel_focus.is_valid())
    set_focus_box(kdu_dims(),jpx_metanode());
  if (animation_metanode_shows_box)
    { 
      animation_metanode_shows_box = false;
      if (compositor != NULL)
        compositor->force_display_redraw();
    }
}

/*****************************************************************************/
/*                 kdws_renderer::menu_FocusHighlighting                     */
/*****************************************************************************/

void kdws_renderer::menu_FocusHighlighting()
{
  highlight_focus = !highlight_focus;
  if (rel_focus.is_valid() && (compositor != NULL))
    { 
      if (animator == NULL)
        compositor->invalidate_rect(view_dims);
      compositor->force_display_redraw();
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FocusWiden                         */
/*****************************************************************************/

void kdws_renderer::menu_FocusWiden()
{
  if (shape_istream.exists() || rel_focus.animator_driven ||
      !rel_focus.is_valid())
    return;
  kdu_dims new_box = find_focus_box();
  new_box.pos.x -= new_box.size.x >> 2;
  new_box.pos.y -= new_box.size.y >> 2;
  new_box.size.x += new_box.size.x >> 1;
  new_box.size.y += new_box.size.y >> 1;
  new_box &= view_dims;
  set_focus_box(new_box,jpx_metanode());
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FocusShrink                        */
/*****************************************************************************/

void kdws_renderer::menu_FocusShrink()
{
  if (shape_istream.exists() || rel_focus.animator_driven)
    return;
  kdu_dims new_box = find_focus_box();
  if (new_box.is_empty())
    new_box = view_dims;
  new_box.pos.x += new_box.size.x >> 3;
  new_box.pos.y += new_box.size.y >> 3;
  new_box.size.x -= new_box.size.x >> 2;
  new_box.size.y -= new_box.size.y >> 2;
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                     kdws_renderer::menu_FocusLeft                         */
/*****************************************************************************/

void kdws_renderer::menu_FocusLeft()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.x -= 20;
  if (new_box.pos.x < view_dims.pos.x)
    {
      if (new_box.pos.x < image_dims.pos.x)
        new_box.pos.x = image_dims.pos.x;
      if (new_box.pos.x < view_dims.pos.x)
        set_hscroll_pos(new_box.pos.x-view_dims.pos.x,true);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FocusRight                         */
/*****************************************************************************/

void kdws_renderer::menu_FocusRight()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.x += 20;
  int new_lim = new_box.pos.x + new_box.size.x;
  int view_lim = view_dims.pos.x + view_dims.size.x;
  if (new_lim > view_lim)
    {
      int image_lim = image_dims.pos.x + image_dims.size.x;
      if (new_lim > image_lim)
        {
          new_box.pos.x -= (new_lim-image_lim);
          new_lim = image_lim;
        }
      if (new_lim > view_lim)
        set_hscroll_pos(new_lim-view_lim,true);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                       kdws_renderer::menu_FocusUp                         */
/*****************************************************************************/

void kdws_renderer::menu_FocusUp()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.y -= 20;
  if (new_box.pos.y < view_dims.pos.y)
    {
      if (new_box.pos.y < image_dims.pos.y)
        new_box.pos.y = image_dims.pos.y;
      if (new_box.pos.y < view_dims.pos.y)
        set_vscroll_pos(new_box.pos.y-view_dims.pos.y,true);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                    kdws_renderer::menu_FocusDown                          */
/*****************************************************************************/

void kdws_renderer::menu_FocusDown()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.y += 20;
  int new_lim = new_box.pos.y + new_box.size.y;
  int view_lim = view_dims.pos.y + view_dims.size.y;
  if (new_lim > view_lim)
    {
      int image_lim = image_dims.pos.y + image_dims.size.y;
      if (new_lim > image_lim)
        {
          new_box.pos.y -= (new_lim-image_lim);
          new_lim = image_lim;
        }
      if (new_lim > view_lim)
        set_vscroll_pos(new_lim-view_lim,true);
    }
  set_focus_box(new_box,jpx_metanode());  
}


/* ========================================================================= */
/*                        Mode Menu Command Handlers                         */
/* ========================================================================= */

/*****************************************************************************/
/*                       kdws_renderer::menu_ModeFast                        */
/*****************************************************************************/

void kdws_renderer::menu_ModeFast() 
{
  error_level = 0;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                     kdws_renderer::can_do_ModeFast                        */
/*****************************************************************************/

void kdws_renderer::can_do_ModeFast(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(TRUE);
  pCmdUI->SetCheck(error_level==0);
}

/*****************************************************************************/
/*                     kdws_renderer::menu_ModeFussy                         */
/*****************************************************************************/

void kdws_renderer::menu_ModeFussy() 
{
  error_level = 1;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                    kdws_renderer::can_do_ModeFussy                        */
/*****************************************************************************/

void kdws_renderer::can_do_ModeFussy(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(TRUE);
  pCmdUI->SetCheck(error_level==1);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_ModeResilient                       */
/*****************************************************************************/

void kdws_renderer::menu_ModeResilient() 
{
  error_level = 2;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_ModeResilient                       */
/*****************************************************************************/

void kdws_renderer::can_do_ModeResilient(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(TRUE);
  pCmdUI->SetCheck(error_level==2);
}

/*****************************************************************************/
/*                 kdws_renderer::menu_ModeResilientSop                      */
/*****************************************************************************/

void kdws_renderer::menu_ModeResilientSop() 
{
  error_level = 3;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                kdws_renderer::can_do_ModeResilientSop                     */
/*****************************************************************************/

void kdws_renderer::can_do_ModeResilientSop(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(TRUE);
  pCmdUI->SetCheck(error_level==3);
}

/*****************************************************************************/
/*                    kdws_renderer::menu_ModeSeekable                       */
/*****************************************************************************/

void kdws_renderer::menu_ModeSeekable() 
{
  allow_seeking = !allow_seeking;
}

/*****************************************************************************/
/*                   kdws_renderer::can_do_ModeSeekable                      */
/*****************************************************************************/

void kdws_renderer::can_do_ModeSeekable(CCmdUI* pCmdUI) 
{
  pCmdUI->Enable(compositor == NULL); // No changes allowed when loaded
  pCmdUI->SetCheck(allow_seeking);
}

/*****************************************************************************/
/*                 kdws_renderer::menu_ModeSinglethreaded                    */
/*****************************************************************************/

void kdws_renderer::menu_ModeSinglethreaded()
{
  if (thread_env.exists())
    {
      if (compositor != NULL)
        compositor->halt_processing();
      thread_env.destroy();
      if (compositor != NULL)
        compositor->set_thread_env(NULL,NULL);
    }
  num_threads = 0;
}

/*****************************************************************************/
/*                kdws_renderer::can_do_ModeSinglethreaded                   */
/*****************************************************************************/

void kdws_renderer::can_do_ModeSinglethreaded(CCmdUI *pCmdUI)
{
  pCmdUI->SetCheck(num_threads==0);
}

/*****************************************************************************/
/*                  kdws_renderer::menu_ModeMultithreaded                    */
/*****************************************************************************/

void kdws_renderer::menu_ModeMultithreaded()
{
  if (thread_env.exists())
    return;
  num_threads = 1;
  thread_env.create();
  if (compositor != NULL)
    {
      compositor->halt_processing();
      compositor->set_thread_env(&thread_env,NULL);
    }
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_ModeMultithreaded                   */
/*****************************************************************************/

void kdws_renderer::can_do_ModeMultithreaded(CCmdUI *pCmdUI)
{
  if (pCmdUI->m_pMenu != NULL)
    {
      kdws_string menu_string(80);
      if (num_threads > 0)
        sprintf(menu_string,"Multi-Threaded (%d threads)",num_threads);
      else
        strcpy(menu_string,"Multi-Threaded");
      pCmdUI->m_pMenu->ModifyMenu(pCmdUI->m_nID,MF_BYCOMMAND,pCmdUI->m_nID,
                                  menu_string);
    }
  pCmdUI->SetCheck(num_threads > 0);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_ModeMorethreads                     */
/*****************************************************************************/

void kdws_renderer::menu_ModeMorethreads()
{
  if (compositor != NULL)
    compositor->halt_processing();
  num_threads++;
  if (thread_env.exists())
    thread_env.destroy();
  thread_env.create();
  for (int k=1; k < num_threads; k++)
    if (!thread_env.add_thread())
      num_threads = k;
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);
}

/*****************************************************************************/
/*                  kdws_renderer::can_do_ModeMorethreads                    */
/*****************************************************************************/

void kdws_renderer::can_do_ModeMorethreads(CCmdUI *pCmdUI)
{
  pCmdUI->Enable(TRUE);
}

/*****************************************************************************/
/*                    kdws_renderer::menu_ModeLessthreads                    */
/*****************************************************************************/

void kdws_renderer::menu_ModeLessthreads()
{
  if (num_threads == 0)
    return;
  if (compositor != NULL)
    compositor->halt_processing();
  num_threads--;
  if (thread_env.exists())
    thread_env.destroy();
  if (num_threads > 0)
    {
      thread_env.create();
      for (int k=1; k < num_threads; k++)
        if (!thread_env.add_thread())
          num_threads = k;
    }
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);
}

/*****************************************************************************/
/*                  kdws_renderer::can_do_ModeLessthreads                    */
/*****************************************************************************/

void kdws_renderer::can_do_ModeLessthreads(CCmdUI *pCmdUI)
{
  pCmdUI->Enable(num_threads > 0);
}

/*****************************************************************************/
/*                 kdws_renderer::menu_ModeRecommendedThreads                */
/*****************************************************************************/

void kdws_renderer::menu_ModeRecommendedThreads()
{
  if (num_threads == num_recommended_threads)
    return;
  if (compositor != NULL)
    compositor->halt_processing();
  num_threads = num_recommended_threads;
  if (thread_env.exists())
    thread_env.destroy();
  if (num_threads > 0)
    {
      thread_env.create();
      for (int k=1; k < num_threads; k++)
        if (!thread_env.add_thread())
          num_threads = k;
    }
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);
}

/*****************************************************************************/
/*               kdws_renderer::can_do_ModeRecommendedThreads                */
/*****************************************************************************/

void kdws_renderer::can_do_ModeRecommendedThreads(CCmdUI *pCmdUI)
{
  if (pCmdUI->m_pMenu != NULL)
    {
      kdws_string menu_string(79);
      if (num_recommended_threads > 0)
        sprintf(menu_string,"Recommended: %d threads",num_recommended_threads);
      else
        strcpy(menu_string,"Recommended: single threaded");
      pCmdUI->m_pMenu->ModifyMenu(pCmdUI->m_nID,MF_BYCOMMAND,pCmdUI->m_nID,
                                  menu_string);
    }
}


/* ========================================================================= */
/*               Metadata Menu Command Handlers (metadata)                   */
/* ========================================================================= */

/*****************************************************************************/
/*                 kdws_renderer::menu_MetadataCatalog                       */
/*****************************************************************************/

void kdws_renderer::menu_MetadataCatalog()
{
  if ((catalog_source == NULL) && jpx_in.exists())
    window->create_metadata_catalog();
  else if (catalog_source != NULL)
    window->remove_metadata_catalog();
  if (catalog_source != NULL)
    {
      catalog_source->tree_ctrl.SetFocus();
      refresh_metadata(get_current_time());
    }
}

/*****************************************************************************/
/*                kdws_renderer::menu_MetadataSwapFocus                      */
/*****************************************************************************/

void kdws_renderer::menu_MetadataSwapFocus()
{
  if ((catalog_source == NULL) && jpx_in.exists())
    {
      window->create_metadata_catalog();
      refresh_metadata(get_current_time());
    }
  else if (catalog_source != NULL)
    {
      if (image_view->is_first_responder())
        {
          catalog_source->tree_ctrl.SetFocus();
          refresh_metadata(get_current_time());
        }
      else
        image_view->SetFocus();
    }
}

/*****************************************************************************/
/*                   kdws_renderer::menu_MetadataShow                        */
/*****************************************************************************/

void kdws_renderer::menu_MetadataShow() 
{
  if (metashow != NULL)
    return;
  RECT wnd_frame;  window->GetWindowRect(&wnd_frame);
  POINT preferred_location;
  preferred_location.x = wnd_frame.left;
  preferred_location.y = wnd_frame.bottom + 10;
  metashow = new kdws_metashow(this,preferred_location,window_identifier);
  if (jp2_family_in.exists())
    {
      metashow->activate(&jp2_family_in,open_filename);
      refresh_metadata(get_current_time());
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_MetadataAdd                        */
/*****************************************************************************/

void
  kdws_renderer::menu_MetadataAdd() 
{
  if ((compositor == NULL) || (!jpx_in) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  if ((editor != NULL) && (shape_editor != NULL))
    { 
      editor->addROIRegion();
      return;
    }
  if (focus_metanode.exists())
    set_focus_box(kdu_dims(),jpx_metanode()); // Don't let user get confused by
            // a focus box that is derived from existing metadata, as opposed
            // to an interactively specified region of interest.

  if (file_server == NULL)
    file_server = new kdws_file_services(open_file_pathname);

  jpx_meta_manager meta_manager = jpx_in.access_meta_manager();
  jpx_metanode parent;
  if (catalog_source != NULL)
    parent = catalog_source->get_selected_metanode();
  
  kdu_dims initial_region = find_focus_box();
  kdu_istream_ref istr;
  if (rel_focus.is_valid() &&
      (initial_region != view_dims) && !initial_region.is_empty())
    istr = compositor->map_region(initial_region,kdu_istream_ref());
  else
    initial_region = kdu_dims();

  kdu_ilayer_ref ilyr; // Initialized based on mouse pos if no focus box
  if (!istr.exists())
    { 
      kdu_coords point = image_view->get_last_mouse_point();      
      ilyr = compositor->find_point(point,0,0.1F);
      if (!ilyr.exists())
        { 
          point = working_buffer_dims.pos;
          point.x += working_buffer_dims.size.x >> 1;
          point.y += working_buffer_dims.size.y >> 1;
          ilyr = compositor->find_point(point,0,0.1F);
        }
    }
  
  if (parent.exists())
    { 
      int user_response;
      if (istr.exists())
        user_response = window->interrogate_user(
                           "You are about to add new metadata to describe "
                           "a region of interest (based initially on the "
                           "focus box).  Would you like to add the "
                           "description as a descendant of the item that is "
                           "currently selected in the metadata side-bar, or "
                           "as a top-level region of interest?",
                           "Top-level","Child of selected item","Cancel");
      else
        user_response = window->interrogate_user(
                           "You are about to add new metadata to reference "
                           "the imagery you are viewing.  Would you like to "
                           "add this as a descendant of the item that is "
                           "currently selected in the metadata side-bar, or "
                           "as a top-level region of interest?",
                           "Top-level","Child of selected item","Cancel");
      if (user_response == 2)
        return;
      else if (user_response == 0)
        parent = jpx_metanode();
    }
  
  jpx_metanode new_node;
  if (istr.exists())
    { 
      int initial_codestream_idx=0, initial_layer_idx=-1, dcs_idx; bool opq;
      compositor->get_istream_info(istr,initial_codestream_idx,&ilyr);
      compositor->get_ilayer_info(ilyr,initial_layer_idx,dcs_idx,opq);
      jpx_roi region;
      region.init_rectangle(initial_region);
      new_node = meta_manager.insert_node(1,&initial_codestream_idx,
                                          (initial_layer_idx >= 0)?1:0,
                                          &initial_layer_idx,false,1,&region,
                                          parent);
    }
  else if (single_component_idx >= 0)
    new_node = meta_manager.insert_node(1,&single_codestream_idx,0,NULL,
                                        false,0,NULL,parent);
  else if (single_layer_idx >= 0)
    new_node = meta_manager.insert_node(0,NULL,1,&single_layer_idx,
                                        false,0,NULL,parent);
  else
    { // Look for a compositing layer at the mouse location
      int layer_idx=-1, stream_idx; bool opq;
      if (ilyr.exists())
        compositor->get_ilayer_info(ilyr,layer_idx,stream_idx,opq);
      if (layer_idx >= 0)
        new_node = meta_manager.insert_node(0,NULL,1,&layer_idx,
                                            false,0,NULL,parent);
      else
        new_node = meta_manager.insert_node(0,NULL,0,NULL,true,0,NULL,parent);
    }

  metadata_changed(true);
  kdws_matching_metalist *metalist = new kdws_matching_metalist;
  metalist->append_node(new_node);

  if (editor == NULL)
    {
      RECT wnd_frame;  window->GetWindowRect(&wnd_frame);
      POINT preferred_location;
      preferred_location.x = wnd_frame.right + 10;
      preferred_location.y = wnd_frame.top;
      editor = new kdws_metadata_editor(&jpx_in,file_server,true,
                                        this,preferred_location,
                                        window_identifier,NULL);
    }
  editor->configure_with_edit_list(metalist);
  editor->set_active_node(new_node);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_MetadataEdit                        */
/*****************************************************************************/

void kdws_renderer::menu_MetadataEdit()
{
  if ((compositor == NULL) || !jpx_in.exists())
    return;
  if ((catalog_source != NULL) && !image_view->is_first_responder())
    {
      jpx_metanode metanode = catalog_source->get_unique_selected_metanode();
      if (metanode.exists())
        edit_metanode(metanode,true);
      else
        MessageBeep(MB_ICONEXCLAMATION);
    }
  else
    edit_metadata_at_point(NULL);
}

/*****************************************************************************/
/*                  kdws_renderer::menu_MetadataDelete                       */
/*****************************************************************************/

void kdws_renderer::menu_MetadataDelete()
{
  if ((shape_editor != NULL) && (editor != NULL))
    {
      editor->deleteROIRegion();
      return;
    }
  if ((catalog_source == NULL) || (editor != NULL) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)) ||
      image_view->is_first_responder())
    return;
  jpx_metanode metanode = catalog_source->get_unique_selected_metanode();
  if (!metanode)
    {
      MessageBeep(MB_ICONEXCLAMATION);
      return;
    }
  if (window->interrogate_user(
                       "Are you sure you want to delete this label/link from "
                       "the file's metadata?  This operation cannot be "
                       "undone.","Delete","Cancel") == 1)
    return;
  jpx_metanode parent = metanode.get_parent();
  if (parent.exists() && (parent.get_num_regions() > 0))
    {
      int num_siblings=0;
      parent.count_descendants(num_siblings);
      if ((num_siblings == 1) &&
          (window->interrogate_user(
                            "The node you are deleting belongs to a region "
                            "of interest (ROI) with no other descendants.  "
                            "Would you like to delete the region as well?",
                            "Leave ROI","Delete ROI") == 1))
        {
          metanode = parent;
          parent = jpx_metanode();
        }
    }
      
  metanode.delete_node();
  metadata_changed(false);
  if (parent.exists())
    catalog_source->select_matching_metanode(parent);
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_MetadataDelete                      */
/*****************************************************************************/

void kdws_renderer::can_do_MetadataDelete(CCmdUI* pCmdUI)
{
  if (shape_editor != NULL)
    pCmdUI->Enable(TRUE);
  else
    pCmdUI->Enable((catalog_source != NULL) && (editor == NULL) &&
                   ((jpip_client == NULL) || !jpip_client->is_alive(-1)) &&
                   catalog_source->get_unique_selected_metanode().exists() &&
                   !image_view->is_first_responder());
}

/*****************************************************************************/
/*                 kdws_renderer::menu_MetadataCopyLabel                     */
/*****************************************************************************/

void kdws_renderer::menu_MetadataCopyLabel()
{
  if ((catalog_source == NULL) || image_view->is_first_responder())
    return;
  jpx_metanode node = catalog_source->get_selected_metanode();
  const char *label = NULL;
  jpx_metanode_link_type link_type;
  if (node.exists() &&
      ((label = node.get_label()) == NULL) &&
      (node = node.get_link(link_type)).exists())
    label = node.get_label();
  if (label == NULL)
    MessageBeep(MB_ICONEXCLAMATION);
  else
    catalog_source->paste_label(label);
}

/*****************************************************************************/
/*                kdws_renderer::can_do_MetadataCopyLabel                    */
/*****************************************************************************/

void kdws_renderer::can_do_MetadataCopyLabel(CCmdUI* pCmdUI)
{
  pCmdUI->Enable((catalog_source != NULL) &&
                 !image_view->is_first_responder());
}

/*****************************************************************************/
/*                 kdws_renderer::menu_MetadataCopyLink                      */
/*****************************************************************************/

void kdws_renderer::menu_MetadataCopyLink()
{
  if ((compositor == NULL) || !jpx_in)
    return;
  if ((catalog_source != NULL) && !image_view->is_first_responder())
    { // Link to something selected within the catalog
      jpx_metanode node = catalog_source->get_selected_metanode();
      if (node.exists())
        catalog_source->paste_link(node);
    }
  else
    { // Link to an image feature.
      kdu_coords point = image_view->get_last_mouse_point();
      kdu_istream_ref istr;
      jpx_metanode roi_node =
        compositor->search_overlays(point,istr,0.1F);
      if (roi_node.exists())
        catalog_source->paste_link(roi_node);
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_MetadataCut                        */
/*****************************************************************************/

void kdws_renderer::menu_MetadataCut()
{
  if ((compositor == NULL) || !jpx_in)
    return;
  if ((catalog_source != NULL) && !image_view->is_first_responder())
    { // Cut something selected within the catalog
      jpx_metanode node = catalog_source->get_unique_selected_metanode();
      if (node.exists())
        catalog_source->paste_node_to_cut(node);
    }
  else
    { // Cut something identified within the image view
      kdu_coords point = image_view->get_last_mouse_point();
      kdu_istream_ref istr;
      jpx_metanode roi_node =
        compositor->search_overlays(point,istr,0.1F);
      if (roi_node.exists())
        catalog_source->paste_node_to_cut(roi_node);
    }  
}

/*****************************************************************************/
/*                 kdws_renderer::menu_MetadataPasteNew                      */
/*****************************************************************************/

void kdws_renderer::menu_MetadataPasteNew()
{
  if ((compositor == NULL) || (catalog_source == NULL) || (editor != NULL) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  jpx_metanode parent = catalog_source->get_unique_selected_metanode();
  if (!parent.exists())
    {
      MessageBeep(MB_ICONEXCLAMATION);
      return;
    }
  const char *label = catalog_source->get_pasted_label();
  jpx_metanode link = catalog_source->get_pasted_link();
  jpx_metanode node_to_cut = catalog_source->get_pasted_node_to_cut();

  jpx_metanode_link_type link_type = JPX_METANODE_LINK_NONE;
  jpx_metanode_link_type reverse_link_type = JPX_METANODE_LINK_NONE;
  jpx_metanode_link_type linked_link_type = JPX_METANODE_LINK_NONE;
  jpx_metanode linked_link;
  if (link.exists())
    { 
      int user_response;
      if ((linked_link = link.get_link(linked_link_type)).exists())
        { // We are about to add a link to an existing link metanode
          if (linked_link_type != JPX_GROUPING_LINK)
            { // Can only create links to non-link and grouping link nodes
              MessageBeep(MB_ICONEXCLAMATION);
              return;
            }
          link_type = JPX_ALTERNATE_CHILD_LINK;
        }
      else
        { 
          user_response = window->interrogate_user(
                             "What type of link would you like to add?\n"
                             "  \"Grouping links\" can have their own "
                             "descendants, all of which can be understood as "
                             "members of the same group.  Groups can be "
                             "chained into bigger groups by linking to each "
                             "other.\n"
                             "  \"Alternate child\" is the most natural type "
                             "of link -- it suggests that the target of the "
                             "link and all its descendants could be "
                             "considered descendants of the node within which "
                             "the link is found.\n"
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

      if (link_type == JPX_GROUPING_LINK)
        { 
          user_response = window->interrogate_user(
                             "Would you like to create an \"Alternate "
                             "child\" link within the link target to "
                             "provide bi-directional linking for this "
                             "\"Grouping\" link?",
                             "One-way","Two-way","Cancel");
          if (user_response == 2)
            return;
          if (user_response == 1)
            reverse_link_type = JPX_ALTERNATE_CHILD_LINK;
        }
      else if ((link_type == JPX_ALTERNATE_CHILD_LINK) && !linked_link)
        {
          int user_response =
            window->interrogate_user(
                             "Would you like to create an \"Alternate "
                             "parent\" link within the link target to "
                             "provide bi-directional linking for this "
                             "\"Alternate child\" link?",
                             "One-way","Two-way","Cancel");
          if (user_response == 2)
            return;
          if (user_response == 1)
            reverse_link_type = JPX_ALTERNATE_PARENT_LINK;
        }
      else if (link_type == JPX_ALTERNATE_PARENT_LINK)
        {
          int user_response =
            window->interrogate_user(
                             "Would you like to create an \"Alternate "
                             "child\" link within the link target to "
                             "provide bi-directional linking for this "
                             "\"Alternate parent\" link?",
                             "One-way","Two-way","Cancel");
          if (user_response == 2)
            return;
          if (user_response == 1)
            reverse_link_type = JPX_ALTERNATE_CHILD_LINK;
        }
    }
  
  try {
    jpx_metanode new_node;
    if (label != NULL)
      new_node = parent.add_label(label);
    else if (node_to_cut.exists())
      {
        jpx_metanode old_parent = node_to_cut.get_parent();
        int nstr, nlyr; bool rres;
        if ((node_to_cut.get_num_regions() > 0) &&
            (!parent.get_numlist_info(nstr,nlyr,rres)) &&
            old_parent.get_numlist_info(nstr,nlyr,rres) && (nstr > 0))
          { // Special numlist/roi combo -- in this case, if the numlist has
            // no other descendants, we should move the entire combo;
            // otherwise, we can reproduce the numlist part of the combo
            // under the new parent and move the ROI node into it.
            int nd=0;
            if (old_parent.count_descendants(nd) && (nd == 1))
              { // We are the only descendant of the numlist; move the combo
                if (old_parent.change_parent(parent))
                  new_node = node_to_cut;
              }
            else
              { // We need to leave the numlist in place
                jpx_meta_manager mgr = jpx_in.access_meta_manager();
                jpx_metanode new_numlist =
                mgr.insert_node(nstr,old_parent.get_numlist_codestreams(),
                                nlyr,old_parent.get_numlist_layers(),rres,
                                NULL,0,parent);
                if (node_to_cut.change_parent(new_numlist))
                  new_node = node_to_cut;
                else
                  new_numlist.delete_node();
              }
          }
        else
          { 
            if ((node_to_cut.get_num_regions() > 0) ||
                node_to_cut.has_dependent_roi_nodes())
              { // Make sure the new `parent' provides codestream info
                jpx_metanode container = parent.get_numlist_container();
                if (container.get_numlist_codestream(0) < 0)
                  { kdu_error e; e << "You cannot move this ROI node to the "
                    "proposed location, because it would no longer have any "
                    "association with a codestream through its ancestry.  "
                    "Consider first creating an image-entity association node "
                    "that identifies at least one codestream, and then moving "
                    "the ROI node under it."; }
              }
            if (node_to_cut.change_parent(parent))
              new_node = node_to_cut;
          }
      }
    else if (link.exists())
      new_node = parent.add_link(link,link_type);
    if (new_node.exists())
      {
        if (reverse_link_type != JPX_METANODE_LINK_NONE)
          { 
            if (link_type == JPX_GROUPING_LINK)
              link.add_link(new_node,reverse_link_type);
            else
              link.add_link(parent,reverse_link_type);
          }
        metadata_changed(true);
        catalog_source->select_matching_metanode(new_node);
      }
  }
  catch (kdu_exception)
  {}
}

/*****************************************************************************/
/*                kdws_renderer::can_do_MetadataPasteNew                     */
/*****************************************************************************/

void kdws_renderer::can_do_MetadataPasteNew(CCmdUI* pCmdUI)
{
  pCmdUI->Enable((catalog_source != NULL) && (editor == NULL) &&
                 ((jpip_client == NULL) || !jpip_client->is_alive(-1)) &&
                 ((catalog_source->get_pasted_label() != NULL) ||
                  catalog_source->get_pasted_link().exists() ||
                  catalog_source->get_pasted_node_to_cut().exists()));
}

/*****************************************************************************/
/*                  kdws_renderer::menu_MetadataDuplicate                    */
/*****************************************************************************/

void kdws_renderer::menu_MetadataDuplicate()
{
  if ((compositor == NULL) || (catalog_source == NULL) || (editor != NULL) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  const char *src_label = NULL;
  jpx_metanode parent, src = catalog_source->get_unique_selected_metanode();
  if ((!(src.exists() && (parent=src.get_parent()).exists())) ||
      ((src_label=src.get_label()) == NULL))
    { 
      MessageBeep(MB_ICONEXCLAMATION);
      return;
    }
  char *new_label = new char[strlen(src_label)+15];
  jpx_metanode new_node, child;
  try {
    sprintf(new_label,"(copy) %s",src_label);
    new_node = parent.add_label(new_label);
    delete[] new_label;
    new_label = NULL;
    for (int d=0; (child=src.get_descendant(d)).exists(); d++)
      new_node.add_copy(child,true);
  }
  catch (kdu_exception) { 
    if (new_label != NULL)
      delete[] new_label;
  }
  if (new_node.exists())
    { 
      metadata_changed(true);
      catalog_source->select_matching_metanode(new_node);
    }
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_MetadataDuplicate                   */
/*****************************************************************************/

void kdws_renderer::can_do_MetadataDuplicate(CCmdUI* pCmdUI)
{
  if ((catalog_source == NULL) || (editor != NULL) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    pCmdUI->Enable(FALSE);
  else
    { 
      jpx_metanode node = catalog_source->get_unique_selected_metanode();
      pCmdUI->Enable(node.exists() && (node.get_label() != NULL));
    }
}

/*****************************************************************************/
/*                    kdws_renderer::menu_MetadataUndo                       */
/*****************************************************************************/

void kdws_renderer::menu_MetadataUndo()
{
  if (editor != NULL)
    editor->undoROIEdit();
}

/*****************************************************************************/
/*                    kdws_renderer::menu_MetadataRedo                       */
/*****************************************************************************/

void kdws_renderer::menu_MetadataRedo()
{
  if (editor != NULL)
    editor->redoROIEdit();
}


/* ========================================================================= */
/*                 Metadata Menu Command Handlers (overlays)                 */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdws_renderer::menu_OverlayEnable                       */
/*****************************************************************************/

void kdws_renderer::menu_OverlayEnable() 
{
  if ((compositor == NULL) || !jpx_in)
    return;
  overlays_enabled = !overlays_enabled;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kds_renderer::menu_OverlayFlashing                      */
/*****************************************************************************/

void kdws_renderer::menu_OverlayFlashing()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled))
    return;
  overlay_flashing_state = (overlay_flashing_state)?0:1;
  overlays_auto_flash = false;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kdws_renderer::menu_OverlayAutoFlash                    */
/*****************************************************************************/

void kdws_renderer::menu_OverlayAutoFlash()
{
  if ((compositor == NULL) || (!jpx_in) || !overlays_enabled)
    return;
  overlays_auto_flash = true;
  if (animator != NULL)
    overlay_flashing_state = 0;
  else
    overlay_flashing_state = 1;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                    kds_renderer::menu_OverlayToggle                       */
/*****************************************************************************/

void kdws_renderer::menu_OverlayToggle()
{
  if ((compositor == NULL) || !jpx_in)
    return;
  overlays_auto_flash = false;
  if (!overlays_enabled)
    {
      overlays_enabled = true;
      overlay_flashing_state = 1;
    }
  else if (overlay_flashing_state)
    overlay_flashing_state = 0;
  else
    overlays_enabled = false;
  overlay_highlight_state = 0;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kds_renderer::menu_OverlayRestrict                      */
/*****************************************************************************/

void kdws_renderer::menu_OverlayRestrict()
{
  if (animation_metanode.exists())
    return; // Have to let the `animator' drive overlay restrictions
      
  jpx_metanode new_restriction;
  if ((catalog_source != NULL) && !image_view->is_first_responder())
    {
      jpx_metanode metanode = catalog_source->get_selected_metanode();
      if (metanode.exists())
        {
          jpx_metanode_link_type link_type;
          jpx_metanode link_target = metanode.get_link(link_type);
          if (link_target.exists())
            new_restriction = link_target;
          else
            new_restriction = metanode;
        }
      if (!new_restriction)
        new_restriction = overlay_restriction;
      else
        {
          if (new_restriction == last_selected_overlay_restriction)
            new_restriction = jpx_metanode(); // Cancel restrictions
          last_selected_overlay_restriction = new_restriction;
        }
    }
  if ((new_restriction != overlay_restriction) || !overlays_enabled)
    {
      overlay_restriction = new_restriction;
      overlays_enabled = true;
      configure_overlays(-1.0,false);
    }
}

/*****************************************************************************/
/*                 kdws_renderer::can_do_OverlayRestrict                     */
/*****************************************************************************/

void kdws_renderer::can_do_OverlayRestrict(CCmdUI* pCmdUI)
{
  pCmdUI->SetCheck(overlay_restriction.exists());
  pCmdUI->Enable(((animator == NULL) || !animation_metanode) &&
                 (overlay_restriction.exists() ||
                  ((catalog_source != NULL) &&
                   !image_view->is_first_responder())));
}

/*****************************************************************************/
/*                    kds_renderer::menu_OverlayLighter                      */
/*****************************************************************************/

void kdws_renderer::menu_OverlayLighter()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled))
    return;
  overlay_nominal_intensity *= 1.0F/1.5F;
  if (overlay_nominal_intensity < 0.25F)
    overlay_nominal_intensity = 0.25F;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                    kds_renderer::menu_OverlayHeavier                      */
/*****************************************************************************/

void kdws_renderer::menu_OverlayHeavier()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled))
    return;
  overlay_nominal_intensity *= 1.5F;
  if (overlay_nominal_intensity > 4.0F)
    overlay_nominal_intensity = 4.0F;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                  kds_renderer::menu_OverlayDoubleSize                     */
/*****************************************************************************/

void kdws_renderer::menu_OverlayDoubleSize()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled) ||
      (overlay_size_threshold & 0x40000000))
    return;
  overlay_size_threshold <<= 1;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kds_renderer::menu_OverlayHalveSize                     */
/*****************************************************************************/

void kdws_renderer::menu_OverlayHalveSize()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled) ||
      (overlay_size_threshold == 1))
    return;
  overlay_size_threshold >>= 1;
  configure_overlays(-1.0,false);
}

/* ========================================================================= */
/*                        Play Menu Command Handlers                         */
/* ========================================================================= */

/*****************************************************************************/
/*                      kds_renderer::menu_PlayNative                        */
/*****************************************************************************/

void kdws_renderer::menu_PlayNative()
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (!source_supports_animation())
    return;
  if (animation_uses_native_timing)
    return;
  animation_uses_native_timing = true;
  if (animator != NULL)
    animator->set_timing(-1.0,animation_native_rate_multiplier,
                         animation_native_rate_multiplier);
  update_animation_bar_state();
  advance_animation(-1.0,-1.0,-1.0); // Introduces the new timing immediately
}

/*****************************************************************************/
/*                     kds_renderer::can_do_PlayNative                       */
/*****************************************************************************/

void kdws_renderer::can_do_PlayNative(CCmdUI* pCmdUI)
{
  if (!source_supports_animation())
    pCmdUI->Enable(false);
  else
    {
      kdws_string menu_string(79);
      if ((animation_native_rate_multiplier > 1.01F) ||
          (animation_native_rate_multiplier < 0.99F))
        sprintf(menu_string,"Native playback rate x %5.2g",
                animation_native_rate_multiplier);
      else
        strcpy(menu_string,"Native play rate");
      pCmdUI->SetText(menu_string);
      pCmdUI->SetCheck(animation_uses_native_timing);
      pCmdUI->Enable(true);
    }
}

/*****************************************************************************/
/*                     kds_renderer::menu_PlayCustom                         */
/*****************************************************************************/

void kdws_renderer::menu_PlayCustom()
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (!source_supports_animation())
    return;
  if (!animation_uses_native_timing)
    return;
  animation_uses_native_timing = false;
  if (animator != NULL)
    animator->set_timing(animation_custom_rate,1.0,animation_custom_rate);
  update_animation_bar_state();
  advance_animation(-1.0,-1.0,-1.0); // Introduces the new timing immediately
}

/*****************************************************************************/
/*                     kds_renderer::can_do_PlayCustom                       */
/*****************************************************************************/

void kdws_renderer::can_do_PlayCustom(CCmdUI* pCmdUI)
{
  if (!source_supports_animation())
    pCmdUI->Enable(false);
  else
    {
      kdws_string menu_string(79);
      sprintf(menu_string,"Custom frame rate = %5.2g fps",
              animation_custom_rate);
      pCmdUI->SetText(menu_string);
      pCmdUI->SetCheck(!animation_uses_native_timing);
      pCmdUI->Enable(true);
    }
}
  
/*****************************************************************************/
/*                  kds_renderer::menu_PlayFrameRateUp                       */
/*****************************************************************************/

void kdws_renderer::menu_PlayFrameRateUp()
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (!source_supports_animation())
    return;
  if (animation_uses_native_timing)
    { 
      animation_native_rate_multiplier *= sqrt(2.0);
      if (animator != NULL)
        animator->set_timing(-1.0,animation_native_rate_multiplier,
                             animation_native_rate_multiplier);
    }
  else
    { 
      if (animation_custom_rate > 4.5)
        animation_custom_rate += 5.0;
      else if (animation_custom_rate > 0.9)
        { 
          if ((animation_custom_rate += 1.0F) > 5.0)
            animation_custom_rate = 5.0;
        }
      else if ((animation_custom_rate *= sqrt(2.0)) > 1.0)
        animation_custom_rate = 1.0;
      if (animator != NULL)
        animator->set_timing(animation_custom_rate,1.0,animation_custom_rate);
    }
  update_animation_bar_state();
  advance_animation(-1.0,-1.0,-1.0); // Introduces the new timing immediately
}

/*****************************************************************************/
/*                 kds_renderer::menu_PlayFrameRateDown                      */
/*****************************************************************************/

void kdws_renderer::menu_PlayFrameRateDown()
{
  if (shape_istream.exists())
    { MessageBeep(MB_ICONEXCLAMATION); return; }
  if (!source_supports_animation())
    return;
  if (animation_uses_native_timing)
    { 
      animation_native_rate_multiplier *= 1.0 / sqrt(2.0); 
      if (animator != NULL)
        animator->set_timing(-1.0,animation_native_rate_multiplier,
                             animation_native_rate_multiplier);
    }
  else
    { 
      if (animation_custom_rate < 1.2)
        animation_custom_rate *= 1.0 / sqrt(2.0);
      else if (animation_custom_rate < 5.5)
        { 
          if ((animation_custom_rate -= 1.0) < 1.0)
            animation_custom_rate = 1.0;
        }
      else if ((animation_custom_rate -= 5.0) < 5.0)
        animation_custom_rate = 5.0;
      if (animator != NULL)
        animator->set_timing(animation_custom_rate,1.0,animation_custom_rate);
    }
  update_animation_bar_state();
  advance_animation(-1.0,-1.0,-1.0); // Introduces the new timing immediately
}
