/*****************************************************************************/
// File: kdms_renderer.mm [scope = APPS/MACSHOW]
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
   Implements the main image management and rendering object, `kdms_renderer',
an instance of which is associated with each `kdms_window' object.
******************************************************************************/
#import "kdms_renderer.h"
#import "kdms_window.h"
#import "kdms_properties.h"
#import "kdms_metadata_editor.h"
#import "kdms_catalog.h"
#include "kdu_arch.h"

#ifdef __SSE__
#  ifndef KDU_X86_INTRINSICS
#    define KDU_X86_INTRINSICS
#  endif
#endif

#ifdef KDU_X86_INTRINSICS
#  include <tmmintrin.h>
#endif // KDU_X86_INTRINSICS

#include <OpenGL/glu.h>

#define KDMS_OVERLAY_DEFAULT_INTENSITY 0.7F
#define KDMS_OVERLAY_FLASHING_STATES 3 // Num distinct flashing states
static float kdms_overlay_flash_factor[KDMS_OVERLAY_FLASHING_STATES] =
  {1.2F,0.6F,-0.6F};
static float kdms_overlay_flash_dwell[KDMS_OVERLAY_FLASHING_STATES] =
  {0.5F,0.5F,1.0F};
  /* Notes: when metadata overlay painting is enabled, they can either be
     displayed statically, using the nominal intensity (starts out with
     the default value given above, but can be adjusted up and down by the
     user), or dynamically (flashing).  In the dynamic case, the current
     value of the nominal intensity (as mentioned, this can be adjusted up
     or down by the user) is additionally scaled by a value which depends
     upon the current flashing state.  The flashing state machine moves
     cyclically through `KDMS_OVERLAY_FLASHING_STATES' states.  Each
     state is characterized by a scaling factor (mentioned above) and a
     dwelling period (measured in seconds); these are stored in the
     `kdms_overlay_flash_factor' and `kdms_overlay_flash_dwell' arrays. */
#define KDMS_OVERLAY_HIGHLIGHT_STATES 5
static float kdms_overlay_highlight_factor[KDMS_OVERLAY_HIGHLIGHT_STATES] =
  {2.0F,-2.0F,2.0F,-2.0F,2.0F};
static float kdms_overlay_highlight_dwell[KDMS_OVERLAY_HIGHLIGHT_STATES] =
  {0.125F,0.125F,0.125F,0.125F,2.0F};
  /* Notes: the above arrays play a similar role to `kdms_overlay_flash_factor'
     and `kdms_overlay_flash_dwell', but they apply to the temporary flashing
     of overlay information for a specific metadata node, in response to
     calls to `kdms_renderer::highlight_imagery_for_metanode'. */

/*===========================================================================*/
/*                             EXTERNAL FUNCTIONS                            */
/*===========================================================================*/

/*****************************************************************************/
/* EXTERN                     kdms_utf8_to_unicode                           */
/*****************************************************************************/

int kdms_utf8_to_unicode(const char *utf8, kdu_uint16 unicode[], int max_chars)
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


/*===========================================================================*/
/*                             INTERNAL FUNCTIONS                            */
/*===========================================================================*/

/*****************************************************************************/
/* STATIC               kdms_dist_to_common_ancestor                         */
/*****************************************************************************/

static int
  kdms_dist_to_common_ancestor(jpx_metanode ref, jpx_metanode test)
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
/* STATIC INLINE                   to_CGRect                               */
/*****************************************************************************/

static inline CGRect to_CGRect(const NSRect *src)
{
  CGRect result;
  result.origin.x = src->origin.x;
  result.origin.y = src->origin.y;
  result.size.width = src->size.width;
  result.size.height = src->size.height;
  return result;
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
  /* Used by `kdms_renderer::save_as_jpx'.  The `force_empty_code_blocks'
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
  write_metadata_box_from_file(jp2_output_box *container, kdms_file *file)
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
                        kdms_file_services *file_server)
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
  kdms_file *file;
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
/*                                 kdms_file                                 */
/*===========================================================================*/

/*****************************************************************************/
/*                           kdms_file::kdms_file                            */
/*****************************************************************************/

kdms_file::kdms_file(kdms_file_services *owner)
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
/*                           kdms_file::~kdms_file                           */
/*****************************************************************************/

kdms_file::~kdms_file()
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
/*                             kdms_file::release                            */
/*****************************************************************************/

void kdms_file::release()
{
  retain_count--;
  if (retain_count == 0)
    delete this;
}

/*****************************************************************************/
/*                       kdms_file::create_if_necessary                      */
/*****************************************************************************/

bool kdms_file::create_if_necessary(const char *initializer)
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
/*                             kdms_file_services                            */
/*===========================================================================*/

/*****************************************************************************/
/*                   kdms_file_services::kdms_file_services                  */
/*****************************************************************************/

kdms_file_services::kdms_file_services(const char *source_pathname)
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
/*                   kdms_file_services::~kdms_file_services                 */
/*****************************************************************************/

kdms_file_services::~kdms_file_services()
{
  while (head != NULL)
    delete head;
  if (base_pathname != NULL)
    delete[] base_pathname;
  kdms_file_editor *editor;
  while ((editor=editors) != NULL)
    {
      editors = editor->next;
      delete editor;
    }
}

/*****************************************************************************/
/*                 kdms_file_services::find_new_base_pathname                */
/*****************************************************************************/

bool kdms_file_services::find_new_base_pathname()
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
/*                 kdms_file_services::confirm_base_pathname                 */
/*****************************************************************************/

void kdms_file_services::confirm_base_pathname()
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
/*                    kdms_file_services::retain_known_file                  */
/*****************************************************************************/

kdms_file *kdms_file_services::retain_known_file(const char *pathname)
{
  kdms_file *file;
  for (file=head; file != NULL; file=file->next)
    if (strcmp(file->pathname,pathname) == 0)
      break;
  if (file == NULL)
    {
      file = new kdms_file(this);
      file->pathname = new char[strlen(pathname)+1];
      strcpy(file->pathname,pathname);
    }
  file->is_temporary = false;
  file->retain();
  return file;
}

/*****************************************************************************/
/*                     kdms_file_services::retain_tmp_file                   */
/*****************************************************************************/

kdms_file *kdms_file_services::retain_tmp_file(const char *suffix)
{
  if (!base_pathname_confirmed)
    confirm_base_pathname();
  kdms_file *file = new kdms_file(this);
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
/*                        kdms_file_services::find_file                      */
/*****************************************************************************/

kdms_file *kdms_file_services::find_file(int identifier)
{
  kdms_file *scan;
  for (scan=head; scan != NULL; scan=scan->next)
    if (scan->unique_id == identifier)
      return scan;
  return NULL;
}

/*****************************************************************************/
/*               kdms_file_services::get_editor_for_doc_type                 */
/*****************************************************************************/

kdms_file_editor *
  kdms_file_services::get_editor_for_doc_type(const char *doc_suffix,
                                              int which)
{
  if (doc_suffix[0] == '\0')
    return NULL;
  
  kdms_file_editor *scan;
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
  // we can add one.
  NSString *ext_string = [NSString stringWithUTF8String:doc_suffix];
  NSURL *app_url=nil;
  OSStatus os_status =
    LSGetApplicationForInfo(kLSUnknownType,kLSUnknownCreator,
                            (CFStringRef)ext_string,
                            kLSRolesAll,NULL,(CFURLRef *)&app_url);
  if (os_status == 0)
    {
      NSString *path_string = [app_url path];
      if (path_string != nil)
        add_editor_for_doc_type(doc_suffix,[path_string UTF8String]);
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
/*               kdms_file_services::add_editor_for_doc_type                 */
/*****************************************************************************/

kdms_file_editor *
  kdms_file_services::add_editor_for_doc_type(const char *doc_suffix,
                                              const char *app_pathname)
{
  kdms_file_editor *scan, *prev;
  for (scan=editors, prev=NULL; scan != NULL; prev=scan, scan=scan->next)
    if ((strcmp(scan->app_pathname,app_pathname) == 0) &&
        (strcmp(scan->doc_suffix,doc_suffix) == 0))
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
  
  NSURL *app_url =
    [NSURL fileURLWithPath:[NSString stringWithUTF8String:app_pathname]];
  if (app_url != nil)
    { 
      scan = new kdms_file_editor;
      scan->doc_suffix = new char[strlen(doc_suffix)+1];
      strcpy(scan->doc_suffix,doc_suffix);
      scan->app_pathname = new char[strlen(app_pathname)+1];
      strcpy(scan->app_pathname,app_pathname);
      if ((scan->app_name = strrchr(scan->app_pathname,'/')) != NULL)
        scan->app_name++;
      else
        scan->app_name = scan->app_pathname;
      scan->app_url = [app_url retain];
      scan->next = editors;
      editors = scan;
      return scan;
    }
  return NULL;
}


/*===========================================================================*/
/*                           kdms_region_compositor                          */
/*===========================================================================*/

/*****************************************************************************/
/*                  kdms_region_compositor::record_viewport                  */
/*****************************************************************************/

void kdms_region_compositor::record_viewport(kdms_orientation orientation,
                                             double pixel_scale,
                                             kdu_dims view_dims)
{
  kdu_dims image_dims;
  bool configured = this->get_total_composition_dims(image_dims);
  mutex.lock();
  kdms_display_geometry new_geometry;
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
/*                   kdms_region_compositor::set_focus_box                   */
/*****************************************************************************/

bool kdms_region_compositor::set_focus_box(kdu_dims &box, int focus_state)
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
/*                  kdms_region_compositor::allocate_buffer                  */
/*****************************************************************************/

kdu_compositor_buf *
  kdms_region_compositor::allocate_buffer(kdu_coords min_size,
                                          kdu_coords &actual_size,
                                          bool read_access_required)
{
  if (min_size.x < 1) min_size.x = 1;
  if (min_size.y < 1) min_size.y = 1;
  actual_size = min_size;
  
  kdms_compositor_buf *prev=NULL, *elt=NULL;
  
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
      elt = new kdms_compositor_buf;
      need_init = true;
    }
  elt->next = active_list;
  active_list = elt;
  elt->set_read_accessibility(read_access_required);
  elt->reset_state();
  if (need_init)
    {
      elt->size = actual_size;
      elt->row_gap = actual_size.x + ((-actual_size.x) & 3); // 16-byte align
      size_t buffer_pels = elt->size.y*elt->row_gap + 3; // Allow for realign
      elt->handle = new(std::nothrow) kdu_uint32[buffer_pels];
      if (elt->handle == NULL)
        { // See if we can free up some memory by culling unused layers
          cull_inactive_ilayers(0);
          elt->handle = new kdu_uint32[buffer_pels];
        }
      int align_offset = ((-_addr_to_kdu_int32(elt->handle)) & 0x0F) >> 2;
      elt->buf = elt->handle + align_offset;
    }
  
  // Verify that the buffer is not on the free list.
  for (prev=free_list; prev != NULL; prev=prev->next)
    assert(prev != elt);
  
  return elt;
}

/*****************************************************************************/
/*                    kdms_region_compositor::delete_buffer                  */
/*****************************************************************************/

void
  kdms_region_compositor::delete_buffer(kdu_compositor_buf *_buffer)
{  
  kdms_compositor_buf *buffer = (kdms_compositor_buf *) _buffer;
  kdms_compositor_buf *scan, *prev;
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
/*           kdms_region_compositor::lock_new_composition_queue_head         */
/*****************************************************************************/

bool
  kdms_region_compositor::lock_new_composition_queue_head(
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
    if (((kdms_compositor_buf *)buf)->get_display_time() <= display_threshold)
      pop_count = idx;
  for (; pop_count > 0; pop_count--)
    { 
      pop_composition_buffer();
      queue_head_presented = false;
    }
  
  if (((buf = inspect_composition_queue(0)) != NULL))
    { 
      kdms_compositor_buf *my_buf = (kdms_compositor_buf *) buf;
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
/*      kdms_region_compositor::unlock_and_mark_composition_queue_head       */
/*****************************************************************************/

void kdms_region_compositor::unlock_and_mark_composition_queue_head()
{
  mutex.lock();
  int frame_idx=0;
  kdu_compositor_buf *buf;
  if ((buf = inspect_composition_queue(0,NULL,&frame_idx)) != NULL)
    { 
      kdms_compositor_buf *my_buf = (kdms_compositor_buf *) buf;
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
/*         kdms_region_compositor::get_presented_queue_head_info             */
/*****************************************************************************/

bool
  kdms_region_compositor::get_presented_queue_head_info(int &frame_idx,
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
          kdms_compositor_buf *my_buf;
          while ((!(frame_change_found && node_change_found &&
                    progress_change_found)) &&
                 ((my_buf = (kdms_compositor_buf *)
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
/*       kdms_region_compositor::conditionally_push_composition_buffer       */
/*****************************************************************************/

bool
  kdms_region_compositor::conditionally_push_composition_buffer(double d_time,
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
      kdms_compositor_buf *head_buf=NULL;
      while (((head_buf = (kdms_compositor_buf *)
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
/*                               kdms_rel_dims                               */
/* ========================================================================= */

/*****************************************************************************/
/*                             kdms_rel_dims::set                            */
/*****************************************************************************/

bool kdms_rel_dims::set(kdu_dims region, kdu_dims container)
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
/*                     kdms_rel_dims::apply_to_container                     */
/*****************************************************************************/

kdu_dims kdms_rel_dims::apply_to_container(kdu_dims container)
{
  kdu_dims result;
  result.pos.x = (int)(0.01 + (centre_x-0.5*size_x)*container.size.x);
  result.pos.y = (int)(0.01 + (centre_y-0.5*size_y)*container.size.y);
  result.size.x = (int)(0.99 + (centre_x+0.5*size_x)*container.size.x);
  result.size.y = (int)(0.99 + (centre_y+0.5*size_y)*container.size.y);
  result.size -= result.pos;
  result.pos += container.pos;
  return result;
}


/*===========================================================================*/
/*                               kdms_renderer                               */
/*===========================================================================*/

/*****************************************************************************/
/*                        kdms_renderer::kdms_renderer                       */
/*****************************************************************************/

kdms_renderer::kdms_renderer(kdms_window *owner, 
                             kdms_document_view *doc_view,
                             NSScrollView *scroll_view,
                             kdms_window_manager *window_manager)
{
  this->window = owner;
  this->doc_view = doc_view;
  this->scroll_view = scroll_view;
  this->gl_view = nil;
  this->window_manager = window_manager;
  this->metashow = nil;
  this->window_identifier = window_manager->get_window_identifier(owner);
  this->catalog_source = nil;
  this->catalog_closed = false;
  this->editor = nil;
  
  [[scroll_view contentView] setCopiesOnScroll:NO];
    // We get much better responsiveness from scrolling by disabling this,
    // since scrolling the view content behind the scenes causes the system
    // to chew up a lot of processing resources so that the rendering of
    // new content gets delayed unnecessarily.  At least this is our
    // experience.  Everything works beautifully the supposedly
    // "accelerated" scroll feature disabled.
  
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
  set_compositor_process_increment();
  defer_process_regions_deadline = -1.0;
  earliest_incremental_paint_time = 0.0;
  
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

  animation_bar = nil;
  metamation_bar = nil;
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
  
  animation_max_queued_frames = 4;
  animation_metanode_shows_box = false;
  animation_advancing = false;
  animation_frame_started = false;
  animation_frame_complete = false;
  animation_frame_needs_push = false;
  animation_border_x = animation_border_y = 0.0F;
  animation_last_sys_time = -1.0;
  frame_presenter = window_manager->get_frame_presenter(owner);
  
  earliest_render_refresh_time= -1.0;
  last_metadata_refresh_time = -1.0;
  next_render_refresh_time = -1.0;
  next_metadata_refresh_time = -1.0;
  next_overlay_change_time = -1.0;
  next_frame_advance_time = -1.0;
  next_scheduled_wakeup_time = -1.0;
  
  izoom = 1.0;
  pixel_scale = 1;
  quality_limiting_mode = 1;
  image_dims.pos = image_dims.size = kdu_coords(0,0);
  working_buffer_dims = view_dims = image_dims;
  view_centre_x = view_centre_y = 0.0F;
  view_centre_known = false;
  skip_view_dims_changed = false;
  view_dims_change_deferred = false;
  
  highlight_focus = true;
  
  overlays_enabled = false;
  overlays_auto_flash = true;
  overlay_flashing_state = 1;
  overlay_highlight_state = 0;
  overlay_nominal_intensity = KDMS_OVERLAY_DEFAULT_INTENSITY;
  overlay_size_threshold = 1;
  if (KDU_IS_LITTLENDIAN())
    { // See the comments appearing immediately prior to the call to
      // `kdu_region_compositor::set_colour_order' to understand why
      // little-endian architectures should use the ABGR order on OSX.
      kdu_uint32 params[KDMS_NUM_OVERLAY_PARAMS] = KDMS_OVERLAY_PARAMS_ABGR;
      for (int p=0; p < KDMS_NUM_OVERLAY_PARAMS; p++)
        overlay_params[p] = params[p];      
    }
  else
    { 
      kdu_uint32 params[KDMS_NUM_OVERLAY_PARAMS] = KDMS_OVERLAY_PARAMS_ARGB;
      for (int p=0; p < KDMS_NUM_OVERLAY_PARAMS; p++)
        overlay_params[p] = params[p];
    }
  working_buffer = NULL;
  generic_rgb_space = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
  
  stripmap_buffer = NULL;
  for (int i=0; i < 256; i++)
    { 
      foreground_lut[i] = (kdu_byte)(40 + ((i*216) >> 8));
      background_lut[i] = (kdu_byte)((i*216) >> 8);
    }

  set_key_window_status([window isKeyWindow]);
}

/*****************************************************************************/
/*                        kdms_renderer::~kdms_renderer                      */
/*****************************************************************************/

kdms_renderer::~kdms_renderer()
{
  frame_presenter = NULL; // Avoid access to potentially non-existent object
  perform_essential_cleanup_steps(false);
  
  if (file_server != NULL)
    { delete file_server; file_server = NULL; }
  if (compositor != NULL)
    { delete compositor; compositor = NULL; }
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
      window_manager->release_jpip_client(jpip_client,window);      
      jpip_client = NULL;
    }
  if (last_save_pathname != NULL)
    delete[] last_save_pathname;
  CGColorSpaceRelease(generic_rgb_space);
  if (stripmap_buffer != NULL)
    delete[] stripmap_buffer;
}

/*****************************************************************************/
/*             kdms_renderer::perform_essential_cleanup_steps                */
/*****************************************************************************/

void kdms_renderer::perform_essential_cleanup_steps(bool app_terminating)
{
  if (frame_presenter != NULL)
    frame_presenter->disable(); // Make sure there is no asynchronous painting
  if (window_manager != NULL)
    window_manager->schedule_wakeup(window,-1.0); // Cancel pending wakeups
  if (file_server != NULL)
    delete file_server;
  file_server = NULL;
  if (animator != NULL)
    delete animator;
  animator = NULL;
  if ((window_manager != NULL) && (open_file_pathname != NULL))
    {
      if (compositor != NULL)
        { delete compositor; compositor = NULL; }
      file_in.close(); // Unlocks any files
      jp2_family_in.close(); // Unlocks any files
      window_manager->release_open_file_pathname(open_file_pathname,window);
      open_file_pathname = NULL;
      open_filename = NULL;
    }
  if (app_terminating && (jpip_client != NULL))
    { // App is about to terminate; disconnect ASAP and release the client
      jpip_client->disconnect(false,1,client_request_queue_id,false);
      window_manager->release_jpip_client(jpip_client,window);      
      jpip_client = NULL;
    }
}

/*****************************************************************************/
/*                        kdms_renderer::deactivate                          */
/*****************************************************************************/

void kdms_renderer::deactivate()
{
  if (catalog_source != nil)
    [catalog_source deactivate];
  if (metashow != nil)
    [metashow deactivate];
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
  window_manager->schedule_wakeup(window,-1.0);
  
  // Close down all information rendering machinery
  if (compositor != NULL)
    { delete compositor; compositor = NULL; }
  if (thread_env.exists())
    thread_env.destroy();
  if (editor != nil)
    { 
      [editor close];
      assert(editor == nil);
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
  if ((animation_bar != nil) || (metamation_bar != nil))
    { 
      [window remove_animation_bar];
      animation_bar = metamation_bar = nil;
    }
  [window reset_animation_bar];
  
  izoom = 1.0;
  izoom_centre.x = izoom_centre.y = 0.0F;
  image_dims.pos = image_dims.size = kdu_coords(0,0);
  working_buffer_dims = view_dims = image_dims;
  view_centre_known = false;
  skip_view_dims_changed = false;
  view_dims_change_deferred = false;
  
  rel_focus.reset();
  focus_metanode = jpx_metanode();
  
  overlays_enabled = false;
  overlay_flashing_state = 1;
  overlay_highlight_state = 0;
  highlight_metanode = jpx_metanode();
  overlay_nominal_intensity = KDMS_OVERLAY_DEFAULT_INTENSITY;
  overlay_restriction = last_selected_overlay_restriction = jpx_metanode();
  
  working_buffer = NULL;

  if ((catalog_source != nil) && (window != nil))
    [window remove_metadata_catalog];
  else if (catalog_source)
    set_metadata_catalog_source(NULL); // Just in case the window went away
  [window remove_progress_bar];
  [doc_view setNeedsDisplay:YES];
  catalog_closed = false;
}

/*****************************************************************************/
/*                        kdms_renderer::close_file                          */
/*****************************************************************************/

void kdms_renderer::close_file()
{
  if (catalog_source != nil)
    [catalog_source deactivate];
  if (metashow != nil)
    [metashow deactivate];
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
      window_manager->release_jpip_client(jpip_client,window);      
      jpip_client = NULL;
    }
  client_request_queue_id = -1;
  client_prefs.init(); // All defaults
   
  // Update GUI elements to reflect "closed" status
  [window setTitle:
   [NSString stringWithFormat:@"kdu_show %d: <no data>",window_identifier]];
  window_manager->declare_window_empty(window,true);
}

/*****************************************************************************/
/*                   kdms_renderer::close_after_exception                    */
/*****************************************************************************/

void kdms_renderer::close_after_exception(kdu_exception exc)
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
/*                   kdms_renderer::set_key_window_status                    */
/*****************************************************************************/

void kdms_renderer::set_key_window_status(bool is_key_window)
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
/*                     kdms_renderer::open_as_duplicate                      */
/*****************************************************************************/

void kdms_renderer::open_as_duplicate(kdms_renderer *src)
{
  this->duplication_src = src;
  if (src->open_file_pathname != NULL)
    [window open_file:[NSString stringWithUTF8String:src->open_file_pathname]];
       // We could just call `open_file' directly, but the above statement
       // interrogates the user if the file has been modified.
  else if (src->open_file_urlname != NULL)
    open_file(src->open_file_urlname);
  this->duplication_src = NULL; // Because the `src' might disappear
}

/*****************************************************************************/
/*                         kdms_renderer::open_file                          */
/*****************************************************************************/

void kdms_renderer::open_file(const char *filename_or_url)
{
  if (processing_call_to_open_file)
    return; // Prevent recursive entry to this function from event handlers
  if (filename_or_url != NULL)
    {
      close_file();
      window_manager->declare_window_empty(window,false);
    }
  if ((num_threads > 0) && !thread_env.exists())
    { 
      thread_env.create();
      for (int k=1; k < num_threads; k++)
        if (!thread_env.add_thread())
          num_threads = k;
      set_compositor_process_increment();
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
            window_manager->retain_jpip_client(NULL,NULL,filename_or_url,
                                               jpip_client,
                                               client_request_queue_id,
                                               window);
          }
        else if (kdu_client::check_cache_file(filename_or_url,&kjc_info))
          { 
            const char *cache_identifier = kjc_info.get_cache_identifier();
            open_file_urlname =
            window_manager->retain_jpip_cache(filename_or_url,
                                              cache_identifier,jpip_client,
                                              client_request_queue_id,
                                              window);
          }
        else
          { // Open as local file
            status_id = KDS_STATUS_LAYER_RES;
            jp2_family_in.open(filename_or_url,allow_seeking);
            compositor = new kdms_region_compositor(&thread_env);
            if (jpx_in.open(&jp2_family_in,true) >= 0)
              { // We have a JP2/JPX-compatible file.
                compositor->create(&jpx_in);
              }
            else if (mj2_in.open(&jp2_family_in,true) >= 0)
              { 
                compositor->create(&mj2_in);
              }
            else
              { // Incompatible with JP2/JPX or MJ2. Try opening raw stream
                jp2_family_in.close();
                file_in.open(filename_or_url,allow_seeking);
                compositor->create(&file_in);
              }
            
            // Set up the correct colour channel order for the 32-bit pixel
            // buffers we will be using.  We will be painting the display
            // based on the order Red, Green, Blue, Alpha (byte ordering).
            // If the machine is big-endian, this corresponds to the
            // compositor's ARGB order; otherwise it corresponds to ABGR.
            // This is because the compositor always puts the Alpha channel
            // in the 8 MSB's of each 32-bit pixel in the surface buffer
            // to which it renders.
            if (KDU_IS_LITTLENDIAN())
              compositor->set_colour_order(KDU_ABGR_ORDER);
            else
              compositor->set_colour_order(KDU_ARGB_ORDER);
            
            compositor->set_error_level(error_level);
            compositor->set_process_aggregation_threshold(0.5F);
              // Display updates are expensive, so encourage aggregtion in
              // calls to `process'.
            open_file_pathname =
              window_manager->retain_open_file_pathname(filename_or_url,
                                                        window);
            open_filename = strrchr(open_file_pathname,'/');
            if (open_filename == NULL)
              open_filename = open_file_pathname;
            else
              open_filename++;
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
            [window set_progress_bar:0.0];
          }
        
        if ((metashow != nil) && jp2_family_in.exists())
          { 
            [metashow activateWithSource:&jp2_family_in
                             andFileName:open_filename];
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
            if (metashow != nil)
              [metashow update_metadata];
            if (jpx_in.open(&jp2_family_in,false))
              { 
                compositor = new kdms_region_compositor(&thread_env);
                compositor->create(&jpx_in);
              }
            window_manager->use_jpx_translator_with_jpip_client(jpip_client);
            kdu_sampled_range rg; rg.from = rg.to = 0;
            rg.context_type = KDU_JPIP_CONTEXT_JPXL;
            preserve_wnd.contexts.add(rg); // Preserve compositing layer 0
          }
        else if (bin0_complete)
          { // Open as a raw file
            assert(!jpx_in.exists());
            if (metashow != nil)
              [metashow deactivate];
            bool hdr_complete=false;
            cache_in.get_databin_length(KDU_MAIN_HEADER_DATABIN,0,0,
                                        &hdr_complete);
            if (hdr_complete)
              { 
                cache_in.set_read_scope(KDU_MAIN_HEADER_DATABIN,0,0);
                compositor = new kdms_region_compositor(&thread_env);
                compositor->create(&cache_in);
              }
            preserve_wnd.codestreams.add(0); // Preserve codestream 0
          }
        if (compositor != NULL)
          { 
            if (KDU_IS_LITTLENDIAN())
              compositor->set_colour_order(KDU_ABGR_ORDER);
            else
              compositor->set_colour_order(KDU_ARGB_ORDER);
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
          { kdu_error e; e << "Unable to recover sufficient information "
            "from remote server (or a local cache) to open the image."; }
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
      overlay_nominal_intensity = KDMS_OVERLAY_DEFAULT_INTENSITY;
      configure_overlays(-1.0,false);

      invalidate_configuration();
      fit_scale_to_window = true;
      image_dims = working_buffer_dims = view_dims = kdu_dims();
      working_buffer = NULL;
      if (duplication_src != NULL)
        { 
          kdms_renderer *src = duplication_src;
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
                 sizeof(kdu_uint32)*KDMS_NUM_OVERLAY_PARAMS);
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
      [window setTitle:[NSString stringWithUTF8String:title]];
      delete[] title;
    }
  
  if ((duplication_src == NULL) && jpx_in.exists() &&
      kdms_catalog_new_data_available(jpx_in.access_meta_manager()))
    { 
      [window create_metadata_catalog];
      refresh_metadata(get_current_time());
    }
  processing_call_to_open_file = false;
}

/*****************************************************************************/
/*                   kdms_renderer::invalidate_configuration                 */
/*****************************************************************************/

void kdms_renderer::invalidate_configuration()
{
  configuration_complete = false;
  max_components = -1; // Changed config may have different num image comps
  working_buffer = NULL;
  working_buffer_dims = kdu_dims();
  if (compositor != NULL)
    compositor->remove_ilayer(kdu_ilayer_ref(),false);
}

/*****************************************************************************/
/*                      kdms_renderer::initialize_regions                    */
/*****************************************************************************/

void kdms_renderer::initialize_regions(bool perform_full_window_init)
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
        min_rendering_scale=-1.0F; // Need to discover these from
        max_rendering_scale=-1.0F; // scratch each time.
      }
      catch (kdu_exception exc) { // Try downgrading to more primitive mode
        stop_animation();
        if ((single_component_idx >= 0) ||
            !(jpx_in.exists() || mj2_in.exists()))
          { // Already in single component mode; nothing more primitive exists
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
                    e << "Cannot display the image.  Inexplicable error "
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
                jpip_client->get_window_in_progress(&tmp_roi,0,NULL,
                                                    NULL,true) &&
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
            while (((min_rendering_scale < 0.0F) ||
                    (new_rendering_scale >= min_rendering_scale)) &&
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
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Insufficient memory -- you could improve "
     "upon the current implementation to invoke "
     "`kdu_region_compositor::cull_inactive_layers' "
     "or take other memory-saving action in response "
     "to this condition."];
    [alert addButtonWithTitle:@"OK"];
    [alert setAlertStyle:NSCriticalAlertStyle];
    [alert runModal];
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
      metamation_bar = nil;
      if (animation_bar == nil)
        animation_bar = [window get_animation_bar:true];
      update_animation_bar_state(); 
    }
  else if (animation_bar != nil)
    { 
      [window remove_animation_bar];
      animation_bar = nil;
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
      size_window_for_image_dims(can_enlarge_window);
      /* Don't need this any more, because `do_processing' now has provision
         to call `setNeedsDisplay' in such a way that the entire display is
         updated, even if not yet fully processed, if processing is taking
         more than a little while -- avoids unnecessary flashing between
         blank (white) background and rendered content now that processing
         is so fast.
         if ((compositor != NULL) && (animator==NULL) && !view_dims.is_empty())
           [doc_view setNeedsDisplay:YES];
      */
    }
  
  if (!respect_initial_jpip_request)
    update_client_window_of_interest();
  if (animator == NULL)
    { 
      if (overlays_enabled)
        { 
          jpx_metanode node;
          if (catalog_source &&
              (node=[catalog_source get_selected_metanode]).exists())
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
/*                 kdms_renderer::configure_quality_limiting                 */
/*****************************************************************************/

void kdms_renderer::configure_quality_limiting()
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
/*                     kdms_renderer::configure_overlays                     */
/*****************************************************************************/

void kdms_renderer::configure_overlays(double cur_time, bool in_wakeup)
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
          if (overlay_highlight_state > KDMS_OVERLAY_HIGHLIGHT_STATES)
            overlay_highlight_state = 0;
        }
      else if (overlay_flashing_state)
        { 
          overlay_flashing_state++;
          if (overlay_flashing_state > KDMS_OVERLAY_FLASHING_STATES)
            overlay_flashing_state = 1;
        }
      if (overlay_highlight_state)
        next_overlay_change_time +=
          kdms_overlay_highlight_dwell[overlay_highlight_state-1];
      else if (overlay_flashing_state)
        next_overlay_change_time +=
          kdms_overlay_flash_dwell[overlay_flashing_state-1];
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
      intensity *= kdms_overlay_highlight_factor[overlay_highlight_state-1];
      restriction = highlight_metanode;
      if (next_overlay_change_time < 0.0)
        { 
          if (cur_time < 0.0)
            cur_time = get_current_time();
          next_overlay_change_time = cur_time +
            kdms_overlay_highlight_dwell[overlay_highlight_state-1];
        }
    }
  else if (overlay_flashing_state)
    { 
      intensity *= kdms_overlay_flash_factor[overlay_flashing_state-1];
      if (next_overlay_change_time < 0.0)
        { 
          if (cur_time < 0.0)
            cur_time = get_current_time();
          next_overlay_change_time = cur_time +
            kdms_overlay_flash_dwell[overlay_flashing_state-1];
        }
    }

  if ((!in_wakeup) || compositor->is_processing_complete())
    { // Don't change the overlay configuration while we are still processing
      // something.  Wait until finished -- by then, the overlay state may
      // have changed to yet another configuration if processing load is
      // heavy.  Make sure we call the present function again when processing
      // actually does complete.
      compositor->configure_overlays(overlays_enabled,overlay_size_threshold,
                                     intensity,KDMS_OVERLAY_BORDER,
                                     restriction,0,overlay_params,
                                     KDMS_NUM_OVERLAY_PARAMS);
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
/*                    kdms_renderer::place_duplicate_window                  */
/*****************************************************************************/

void kdms_renderer::place_duplicate_window()
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
  NSSize doc_size; // Size on the `doc_view' frame
  doc_size.width = ((float) view_size.x) * pixel_scale;
  doc_size.height = ((float) view_size.y) * pixel_scale;
  NSRect scroll_rect, content_rect, window_rect;
  scroll_rect.origin.x = scroll_rect.origin.y = 0.0F;
  scroll_rect.size =
    [NSScrollView frameSizeForContentSize:doc_size
                  horizontalScrollerClass:[kdms_scroller class]
                    verticalScrollerClass:[kdms_scroller class]
                               borderType:NSNoBorder
                              controlSize:NSRegularControlSize
                            scrollerStyle:NSScrollerStyleLegacy];
  content_rect = scroll_rect;
  content_rect.size.height += [window get_bottom_margin_height];
  content_rect.size.width += [window get_right_margin_width];
  window_rect = [window frameRectForContentRect:content_rect];
  window_manager->place_window(window,window_rect.size);
}

/*****************************************************************************/
/*                kdms_renderer::size_window_for_image_dims                  */
/*****************************************************************************/

void kdms_renderer::size_window_for_image_dims(bool first_time)
{
  // Find the size of the `doc_view' window and see if it can fit
  // completely inside the `scroll_view', given the window dimensions.
  NSSize doc_size; // Size of the `doc_view' frame
  doc_size.width = ((float) image_dims.size.x) * pixel_scale;
  doc_size.height = ((float) image_dims.size.y) * pixel_scale;
  NSRect scroll_rect;
  scroll_rect.origin.x = scroll_rect.origin.y = 0.0F;
  scroll_rect.size =
    [NSScrollView frameSizeForContentSize:doc_size
                  horizontalScrollerClass:[kdms_scroller class]
                    verticalScrollerClass:[kdms_scroller class]
                               borderType:NSNoBorder
                              controlSize:NSRegularControlSize
                            scrollerStyle:NSScrollerStyleLegacy];
  [window set_min_max_size_for_scroll_view:scroll_rect.size];
  
  NSRect max_window_rect = [[NSScreen mainScreen] visibleFrame];
  NSRect existing_window_rect = [window frame];
  if (!first_time)
    max_window_rect = existing_window_rect;
  
  NSRect window_rect;
  window_rect.origin = existing_window_rect.origin;
  window_rect.size = [window maxSize];
  if (window_rect.size.width > max_window_rect.size.width)
    window_rect.size.width = max_window_rect.size.width;
  if (window_rect.size.height > max_window_rect.size.height)
    window_rect.size.height = max_window_rect.size.height;
  
  bool resize_window = false;
  if (window_rect.size.width != existing_window_rect.size.width)
    { // Need to adjust window width and position.
      resize_window = true;
      float left = existing_window_rect.origin.x;
      float right = left + window_rect.size.width;
      float max_left = max_window_rect.origin.x;
      float max_right = max_left + max_window_rect.size.width;
      float shift;
      if ((shift = (right - max_right)) > 0.0F)
        { right -= shift; left -= shift; }
      if ((shift = (max_left-left)) > 0.0F)
        { right += shift; left += shift; }
      window_rect.origin.x = left;
    }
  if (window_rect.size.height != existing_window_rect.size.height)
    { // Need to adjust window width and position.
      resize_window = true;
      float top =
      existing_window_rect.origin.y + existing_window_rect.size.height;
      float bottom = top - window_rect.size.height;
      float max_bottom = max_window_rect.origin.y;
      float max_top = max_bottom + max_window_rect.size.height;
      float shift;
      if ((shift = (max_bottom-bottom)) > 0.0F)
        { bottom += shift; top += shift; }
      if ((shift = (top - max_top)) > 0.0F)
        { bottom -= shift; top -= shift; }
      window_rect.origin.y = top - window_rect.size.height;
    }
  if (first_time || resize_window)
    { 
      NSSize min_size = [window minSize];
      if (window_rect.size.height < min_size.height)
        window_rect.size.height = min_size.height;
      if (window_rect.size.width < min_size.width)
        window_rect.size.width = min_size.width;
      if (!(first_time &&
            window_manager->place_window(window,window_rect.size,true)))
        { 
          kdms_rel_focus rel_focus_save = rel_focus; // Otherwise the focus box
            // may be pushed around by the change in frame dimensions before we
            // get a chance to scroll back it back into view
          [window setFrame:window_rect display:NO];
          this->rel_focus = rel_focus_save;
        }
    }
  
  // Finally set the `doc_view'
  skip_view_dims_changed = true;
  [doc_view setFrameSize:doc_size];
  if (view_centre_known)
    scroll_to_view_anchors();
  else if (first_time)
    { // Adjust `scroll_view' position to the top of the image.
      NSRect view_rect = [scroll_view documentVisibleRect];
      NSPoint new_view_origin;
      new_view_origin.x = 0.0F;
      new_view_origin.y = doc_size.height - view_rect.size.height;
      [doc_view scrollPoint:new_view_origin];
    }
  skip_view_dims_changed = false;
  view_dims_changed();
}

/*****************************************************************************/
/*                 kdms_renderer::size_buffer_for_target_view                */
/*****************************************************************************/

kdu_dims
  kdms_renderer::size_buffer_for_target_view(kdu_dims min_buffer_dims,
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
  set_compositor_process_increment(assume_scroll_pan);

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
/*                     kdms_renderer::view_dims_changed                      */
/*****************************************************************************/

void kdms_renderer::view_dims_changed()
{
  kdu_dims new_view_dims, new_buffer_dims;
  kdu_region_animator_roi animator_roi;
  bool animator_refreshed=false;
  while (true)
    { // We repeat at most once if the animator gets refreshed
      if ((compositor == NULL) || skip_view_dims_changed)
        return;
      
      // Start by finding `view_dims' as the smallest rectangle (in the same
      // coordinate system as `image_dims') which spans the contents of
      // the `scroll_view'.
      NSRect ns_rect = [scroll_view documentVisibleRect];
      NSRect view_rect;
      view_rect.origin.x = (float) floor(ns_rect.origin.x);
      view_rect.origin.y = (float) floor(ns_rect.origin.y);
      view_rect.size.width = (float)
      ceil(ns_rect.size.width+ns_rect.origin.x) - view_rect.origin.x;
      view_rect.size.height = (float)
      ceil(ns_rect.size.height+ns_rect.origin.y) - view_rect.origin.y;
      
      // Figure out `new_view_dims' and `new_buffer_dims'
      new_view_dims = convert_region_from_doc_view(view_rect);
      new_view_dims &= image_dims;
      if ((new_view_dims == view_dims) &&
          working_buffer_dims.contains(view_dims) &&
          !view_dims_change_deferred)
        { // No change
          update_focus_box();
          return;
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
}

/*****************************************************************************/
/*                kdms_renderer::convert_region_from_doc_view                */
/*****************************************************************************/

kdu_dims
  kdms_renderer::convert_region_from_doc_view(NSRect rect, double scale,
                                              kdu_dims ref_image_dims)
{
  scale = 1.0 / scale;
  kdu_coords region_min, region_lim;
  region_min.x = (int) floor(rect.origin.x * scale);
  region_lim.x = (int) ceil((rect.origin.x+rect.size.width) * scale);
  region_lim.y = ref_image_dims.size.y - (int)
    floor(rect.origin.y * scale);
  region_min.y = ref_image_dims.size.y - (int)
    ceil((rect.origin.y+rect.size.height) * scale);
  kdu_dims region;
  region.pos = region_min + ref_image_dims.pos;
  region.size = region_lim - region_min;
  return region;
}

/*****************************************************************************/
/*                  kdms_renderer::convert_region_to_doc_view                */
/*****************************************************************************/

NSRect
  kdms_renderer::convert_region_to_doc_view(kdu_dims region, double scale,
                                            kdu_dims ref_image_dims)
{
  kdu_coords rel_pos = region.pos - ref_image_dims.pos;
  NSRect doc_rect;
  doc_rect.origin.x = rel_pos.x * scale;
  doc_rect.size.width = region.size.x * scale;
  doc_rect.origin.y = (ref_image_dims.size.y-rel_pos.y-region.size.y)*scale;
  doc_rect.size.height = region.size.y * scale;
  return doc_rect;
}

/*****************************************************************************/
/*                kdms_renderer::convert_point_from_doc_view                 */
/*****************************************************************************/

kdu_coords kdms_renderer::convert_point_from_doc_view(NSPoint point)
{
  float scale_factor = 1.0F / (float) pixel_scale;
  kdu_coords pos;
  pos.x = (int) round(point.x * scale_factor);
  pos.y = image_dims.size.y - 1 - (int) round(point.y * scale_factor);
  return pos + image_dims.pos;
}

/*****************************************************************************/
/*                  kdms_renderer::convert_point_to_doc_view                 */
/*****************************************************************************/

NSPoint kdms_renderer::convert_point_to_doc_view(kdu_coords point)
{
  float scale_factor = (float) pixel_scale;
  kdu_coords rel_pos = point - image_dims.pos;
  NSPoint doc_point;
  doc_point.x = rel_pos.x * scale_factor;
  doc_point.y = (image_dims.size.y-1-rel_pos.y)*scale_factor;
  return doc_point;
}

/*****************************************************************************/
/*                  kdms_renderer::scale_vector_to_doc_view                  */
/*****************************************************************************/

NSPoint kdms_renderer::scale_vector_to_doc_view(kdu_coords vec)
{
  float scale_factor = (float) pixel_scale;
  NSPoint result;
  result.x = vec.x * scale_factor;
  result.y = -vec.y * scale_factor;
  return result;
}

/*****************************************************************************/
/*                 kdms_renderer::scale_vector_from_doc_view                 */
/*****************************************************************************/

kdu_coords kdms_renderer::scale_vector_from_doc_view(NSPoint vec)
{
  float scale_factor = 1.0F / (float) pixel_scale;
  kdu_coords result;
  result.x = (int) round(vec.x * scale_factor);
  result.y = (int)(-round(vec.y * scale_factor));
  return result;
}

/*****************************************************************************/
/*                      kdms_renderer::resize_stripmap                       */
/*****************************************************************************/

void
  kdms_renderer::resize_stripmap(int min_width)
{
  if (min_width <= strip_extent.x)
    return;
  if (stripmap_buffer != NULL)
    { delete[] stripmap_buffer; stripmap_buffer = NULL; }
  strip_extent.x = min_width + 128;
  strip_extent.y = 512;
  stripmap_buffer = new kdu_uint32[strip_extent.x*strip_extent.y];
}

/*****************************************************************************/
/*                        kdms_renderer::paint_region                        */
/*****************************************************************************/

void kdms_renderer::paint_region(NSRect region,
                                 CGContextRef graphics_context)
{
  if (gl_view)
    return; // We are covered by the `gl_view', so painting here should have
            // no effect; leave it to the `paint_gl' function.
  
  kdms_orientation buf_orientation;
  kdu_dims buf_dims, buf_image_dims;
  kdms_compositor_buf *buffer = NULL;
  if (compositor != NULL)
    buffer = compositor->get_composition_buffer_ex(buf_dims,false);
  if ((buffer == NULL) || buf_dims.is_empty() ||
      (!buffer->get_image_dims(buf_image_dims,buf_orientation)) ||
      buf_image_dims.is_empty())
    { // Paint blank region and return
      CGContextSetRGBFillColor(graphics_context,1.0F,1.0F,1.0F,1.0F);
      CGContextFillRect(graphics_context,to_CGRect(&region));
      return;
    }

  // Obtain information required for all painting operations below
  kdms_display_geometry display_geometry;
  compositor->get_display_geometry(display_geometry);
  if (display_geometry.orientation != buf_orientation)
    { // Incompatible scaling factors; major view changes occurred; don't
      // paint at this point, situation is transient.
      return;
    }
  
  // Work out coordinate transformations to take into account both interactive
  // zoom and any differences between the image dimensions for which the
  // buffer was rendered and those that currently prevail, so as to map
  // points in `doc_view' coords to points in `buf_dims' coordinates.  The
  // interpretation will be as follows:
  //    For a point (sx,sy) in `doc_view' coordinates, the corresponding point
  // (nx,ny) in `buf_dims' coordinates is given by
  //      (nx - buf_image_dims.pos.x) = buf_displacement.x + buf_scale * sx
  //      (ny - buf_image_dims.pos.y) = buf_displacement.y - buf_scale * sy
  // or in the reverse direction:
  //      sx = doc_displacement.x + doc_scale * (nx - buf_image_dims.pos.x)
  //      sy = doc_displacement.y - doc_scale * (ny - buf_image_dims.pos.y)
  NSPoint buf_displacement, doc_displacement;
  double buf_scale, doc_scale;
  doc_scale = display_geometry.pixel_scale;
  buf_displacement.x = 0.0;
  buf_displacement.y = display_geometry.image_dims.size.y;
  double geo_scale=1.0;     // Corrects for differences between rendered size
  double inv_geo_scale=1.0; // and `display_geometry' size
  if (display_geometry.image_dims.size != buf_image_dims.size)
    { 
      geo_scale = sqrt(((double) display_geometry.image_dims.area()) /
                         ((double) buf_image_dims.area()));
      inv_geo_scale = 1.0 / geo_scale;
      buf_displacement.y *= inv_geo_scale;
      doc_scale *= geo_scale;
    }
  buf_scale = 1.0 / doc_scale;
  if (izoom != 1.0)
    { 
      double inv_izoom = 1.0 / izoom;
      buf_displacement.x += buf_scale * (1.0 - inv_izoom) * izoom_centre.x;
      buf_displacement.y -= buf_scale * (1.0 - inv_izoom) * izoom_centre.y;
      buf_scale *= inv_izoom;
      doc_scale *= izoom;
    }
  doc_displacement.x = -buf_displacement.x * doc_scale;
  doc_displacement.y = buf_displacement.y * doc_scale;

  // Next work out the region that we can paint
  kdu_dims buf_region; // Will be region to paint, expressed in buf coords
  NSRect doc_region; // Will be region to paint, expressed in `doc_view' coords'

  kdu_coords buf_min, buf_lim;
  NSPoint doc_min=region.origin, doc_lim=region.origin;
  doc_lim.x += region.size.width;  doc_lim.y += region.size.height;
  buf_min.x = (int) floor(buf_displacement.x + buf_scale*doc_min.x);
  buf_lim.x = (int) ceil( buf_displacement.y + buf_scale*doc_lim.x);
  buf_min.y = (int) floor(buf_displacement.y - buf_scale*doc_lim.y);
  buf_lim.y = (int) ceil( buf_displacement.y - buf_scale*doc_min.y);
  buf_region.pos = buf_min + buf_image_dims.pos;
  buf_region.size = buf_lim - buf_min;  
  buf_region &= buf_dims; // Might not have data to paint everything
  
  // Now create `src_region' from `region', modified so as to have nice
  // alignment properties.
  kdu_dims src_region = buf_region;
  kdu_coords rel_pos;
  if (buf_region.is_empty())
    doc_region.size.width = doc_region.size.height = 0.0F;
  else
    { // We have some buffer samples to paint
      rel_pos = src_region.pos - buf_dims.pos;
      if (rel_pos.x & 3)
        { // Align start of rendering region on 16-byte boundary
          int offset = rel_pos.x & 3;
          src_region.pos.x -= offset;
          rel_pos.x -= offset;
          src_region.size.x += offset;
        }
      if (src_region.size.x & 3)
        { // Align end of region on 16-byte boundary
          src_region.size.x += (4-src_region.size.x) & 3;
        }
  
      // Now map `src_region' to the `target_rect' that will be painted
      NSRect target_rect;
      buf_min = src_region.pos - buf_image_dims.pos;
      buf_lim = buf_min + src_region.size;
      doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
      target_rect.origin = doc_min;
      target_rect.size.width=doc_lim.x-doc_min.x;
      target_rect.size.height=doc_lim.y-doc_min.y;
    
      // Also map `buf_region' to the `doc_region' over which painting can
      // be considered reliable
      buf_min = buf_region.pos - buf_image_dims.pos;
      buf_lim = buf_min + buf_region.size;
      doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
      doc_region.origin = doc_min;
      doc_region.size.width=doc_lim.x-doc_min.x;
      doc_region.size.height=doc_lim.y-doc_min.y;

      // Figure out where any focus box sits relative to the `src_region'
      bool display_with_focus =
        (display_geometry.focus_state != 0) && highlight_focus;
      kdu_dims focus_region = display_geometry.focus_box;
      if (shape_istream.exists())
        { // Force whole image to be painted darker
          display_with_focus = true;
          display_geometry.focus_state = 0;
          focus_region=kdu_dims();
        }

      int alpha_order = 0; // Default is for alpha to come last
      if (!KDU_IS_LITTLENDIAN())
        alpha_order = NSAlphaFirstBitmapFormat;

      int buf_row_gap;
      kdu_uint32 *buf_data = buffer->get_buf(buf_row_gap,false);
      buf_data += rel_pos.y*buf_row_gap + rel_pos.x;

      if (display_with_focus)
        { // Paint with focus-box adjustments as we go
          focus_region &= src_region;
          if (!focus_region)
            focus_region.pos = src_region.pos;
          int focus_above = focus_region.pos.y - src_region.pos.y;
          int focus_height = focus_region.size.y;
          int focus_left = focus_region.pos.x - src_region.pos.x;
          int focus_width = focus_region.size.x;
          int focus_right = src_region.size.x - focus_left - focus_width;
          assert((focus_above >= 0) && (focus_height >= 0) &&
                 (focus_left >= 0) && (focus_width >= 0) &&
                 (focus_right >= 0));
          if (strip_extent.x < src_region.size.x)
            resize_stripmap(src_region.size.x);
          target_rect.origin.y += target_rect.size.height;
          while (src_region.size.y > 0)
            { 
              int i, j, xfer_height = src_region.size.y;
              if (xfer_height > strip_extent.y)
                { 
                  xfer_height = strip_extent.y;
                  if ((src_region.size.y-xfer_height) <= (strip_extent.y>>2))
                    xfer_height -= strip_extent.y>>2; // So no strip too small
                }
              kdu_uint32 *sp, *dp, *dpp=stripmap_buffer;
              for (j=xfer_height; j > 0; j--,
                   buf_data+=buf_row_gap, dpp+=strip_extent.x,
                   focus_above--)
                { 
                  sp = buf_data; dp = dpp;
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
              CGFloat tgt_height = doc_scale * xfer_height;
              target_rect.origin.y -= tgt_height;
              target_rect.size.height = tgt_height;
              NSBitmapImageRep *irep = [NSBitmapImageRep alloc];
              irep =
                [irep initWithBitmapDataPlanes:(kdu_byte **)(&stripmap_buffer)
                                    pixelsWide:src_region.size.x
                                    pixelsHigh:xfer_height
                                 bitsPerSample:8
                               samplesPerPixel:4
                                      hasAlpha:YES
                                      isPlanar:NO
                                colorSpaceName:NSDeviceRGBColorSpace
                                  bitmapFormat:alpha_order
                                   bytesPerRow:(strip_extent.x<<2)
                                  bitsPerPixel:32];
              [irep drawInRect:target_rect];
              [irep release];
              src_region.pos.y += xfer_height;
              src_region.size.y -= xfer_height;
          }
        }
      else
        { // Paint the region
          NSBitmapImageRep *irep = [NSBitmapImageRep alloc];
          irep = [irep initWithBitmapDataPlanes:(kdu_byte **)(&buf_data)
                                     pixelsWide:src_region.size.x
                                     pixelsHigh:src_region.size.y
                                  bitsPerSample:8
                                samplesPerPixel:4
                                       hasAlpha:YES
                                       isPlanar:NO
                                 colorSpaceName:NSDeviceRGBColorSpace
                                   bitmapFormat:alpha_order
                                    bytesPerRow:(buf_row_gap<<2)
                                   bitsPerPixel:32];
          [irep drawInRect:target_rect];
          [irep release];
        }
    }
  
  // See if we need to fill in any missing part of the frame
  if ((doc_region.origin.x > region.origin.x) ||
      (doc_region.origin.y > region.origin.y) ||
      ((doc_region.origin.x+doc_region.size.width) <
       (region.origin.x+region.size.width)) ||
      ((doc_region.origin.y+doc_region.size.height) <
       (region.origin.y+region.size.height)))
    { 
      CGContextSetRGBFillColor(graphics_context,1.0F,1.0F,1.0F,1.0F);
      for (int rgn=0; rgn < 4; rgn++)
        { // May need to fill as many as 4 regions, depending on what
          // is not covered by `doc_region'.
          CGRect cg_rect = to_CGRect(&region);
          if ((doc_region.size.width <= 0.0F) ||
              (doc_region.size.height <= 0.0F))
            { // Fill the whole thing and get out of the loop
              CGContextFillRect(graphics_context,cg_rect);
              break;
            }
          if (rgn < 2)
            { // Fill any left over region above or below `drawn_rect'
              if (rgn == 0) // Fill below
                cg_rect.size.height = doc_region.origin.y - region.origin.y;
              else
                { // Fill above `doc_region'
                  double y = doc_region.origin.y + doc_region.size.height;
                  cg_rect.origin.y = y;
                  cg_rect.size.height = region.origin.y+region.size.height-y;
                }
            }
          else
            { // Fill any left over region to the left or right of
              // `drawn_rect'
              if (rgn == 2) // Fill to the left
                cg_rect.size.width = doc_region.origin.x - region.origin.x;
              else
                { // Fill to the right of `doc_region'
                  double x = doc_region.origin.x + doc_region.size.width;
                  cg_rect.origin.x = x;
                  cg_rect.size.width = region.origin.x + region.size.width - x;
                }
            }
          if ((cg_rect.size.width > 0.0) && (cg_rect.size.height > 0.0))
            { 
              double adj;
              adj = cg_rect.origin.x - floor(cg_rect.origin.x);
              cg_rect.origin.x -= adj; cg_rect.size.width += adj;
              cg_rect.size.width = ceil(cg_rect.size.width);
              adj = cg_rect.origin.y - floor(cg_rect.origin.y);
              cg_rect.origin.y -= adj; cg_rect.size.height += adj;
              cg_rect.size.height = ceil(cg_rect.size.height);
              CGContextFillRect(graphics_context,cg_rect);
            }
        }
    }
  
  // See if we need to draw a focus box outline
  if (display_geometry.focus_state != 0)
    { 
      assert(!shape_istream.exists());
      NSRect target_rect;
      buf_min = display_geometry.focus_box.pos-display_geometry.image_dims.pos;
      buf_lim = buf_min + display_geometry.focus_box.size;
      double tmp_scale = doc_scale * inv_geo_scale; // Remove geo correction
      doc_min.x = doc_displacement.x + tmp_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + tmp_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - tmp_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - tmp_scale*buf_min.y;
      target_rect.origin = doc_min;
      target_rect.size.width=doc_lim.x-doc_min.x;
      target_rect.size.height=doc_lim.y-doc_min.y;

      CGContextSetLineWidth(graphics_context,1.0F);
      CGContextSetLineJoin(graphics_context,kCGLineJoinRound);
      
      // Draw inner rectangle
      target_rect.origin.x += 0.5F;    target_rect.origin.y += 0.5F;
      target_rect.size.width -= 1.0F;  target_rect.size.height -= 1.0F;
      if (display_geometry.focus_state > 0) // Draw brown line
        CGContextSetRGBStrokeColor(graphics_context,1.0F,1.0F,0.0F,1.0F);
      else // Change colour to indicate a metadata-derived focus box
        CGContextSetRGBStrokeColor(graphics_context,1.0F,1.0F,0.0F,0.8F);
      CGContextStrokeRect(graphics_context,to_CGRect(&target_rect));
      
      // Draw outer rectangle
      target_rect.origin.x -= 1.0F;    target_rect.origin.y -= 1.0F;
      target_rect.size.width += 2.0F;  target_rect.size.height += 2.0F;
      if (display_geometry.focus_state > 0) // Draw yellow line
        CGContextSetRGBStrokeColor(graphics_context,0.6F,0.4F,0.2F,1.0F);
      else // Change colour to indicate a metadata-derived focus box
        CGContextSetRGBStrokeColor(graphics_context,0.0F,0.0F,1.0F,0.8F);
      CGContextStrokeRect(graphics_context,to_CGRect(&target_rect));
    }

  // See if we need to draw a "focussing" outline
  kdu_dims focussing_rect;
  if (compositor->get_focussing_rect(focussing_rect))
    { // Focussing outlines are drawn on the immediate surface managed by
      // `display_geometry'.
      NSRect target_rect;
      buf_min = focussing_rect.pos-display_geometry.image_dims.pos;
      buf_lim = buf_min + focussing_rect.size;
      double tmp_scale = doc_scale*inv_geo_scale; // Remove geo correction
      doc_min.x = doc_displacement.x + tmp_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + tmp_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - tmp_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - tmp_scale*buf_min.y;
      target_rect.origin = doc_min;
      target_rect.size.width=doc_lim.x-doc_min.x;
      target_rect.size.height=doc_lim.y-doc_min.y;

      CGContextSetLineWidth(graphics_context,3.0F);
      CGContextSetLineJoin(graphics_context,kCGLineJoinRound);
      CGFloat dash_pattern[2]={5.0F,5.0F};
    
      // Draw brown inner rectangle
      CGContextSetLineDash(graphics_context,0.0F,dash_pattern,2);
      target_rect.origin.x += 1.0F;    target_rect.origin.y += 1.0F;
      target_rect.size.width -= 2.0F;  target_rect.size.height -= 2.0F;
      CGContextSetRGBStrokeColor(graphics_context,0.6F,0.4F,0.2F,1.0F);
      CGContextStrokeRect(graphics_context,to_CGRect(&target_rect));
      
      // Draw yellow outer rectangle
      CGContextSetLineDash(graphics_context,5.0F,dash_pattern,2);
      target_rect.origin.x -= 2.0F;    target_rect.origin.y -= 2.0F;
      target_rect.size.width += 4.0F;  target_rect.size.height += 4.0F;
      CGContextSetRGBStrokeColor(graphics_context,1.0F,1.0F,0.0F,1.0F);
      CGContextStrokeRect(graphics_context,to_CGRect(&target_rect));    
    }
  
  // See if we need to draw frame bounds during interactive zoom
  buf_min = kdu_coords(0,0);  buf_lim = buf_image_dims.size;
  doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
  doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
  doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
  doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
  if ((doc_min.x > (region.origin.x+0.5)) ||
      (doc_min.y > (region.origin.y+0.5)) ||
      (doc_lim.x < (region.origin.x+region.size.width-0.5)) ||
      (doc_lim.y < (region.origin.y+region.size.height-0.5)))
    { 
      NSRect target_rect;
      target_rect.origin = doc_min;
      target_rect.size.width=doc_lim.x-doc_min.x;
      target_rect.size.height=doc_lim.y-doc_min.y;
      CGFloat dash_pattern[2]={5.0F,5.0F};
      CGContextSetLineWidth(graphics_context,1.0F);
      CGContextSetLineJoin(graphics_context,kCGLineJoinMiter);
      CGContextSetLineDash(graphics_context,0.0F,dash_pattern,2);
      CGContextSetRGBStrokeColor(graphics_context,0.0F,0.0F,0.0F,1.0F);
      CGContextStrokeRect(graphics_context,to_CGRect(&target_rect));
    }
}

/*****************************************************************************/
/*                          kdms_renderer::paint_gl                          */
/*****************************************************************************/

bool kdms_renderer::paint_gl(bool in_presenter)
{
  if (!gl_view)
    return false;
  
  kdms_orientation buf_orientation;
  kdu_dims buf_dims, buf_image_dims, buf_roi;
  kdms_compositor_buf *buffer = NULL;
  if (compositor != NULL)
    buffer = compositor->get_composition_buffer_ex(buf_dims,false);
      /* Note: the above function always retrieves the head of the composition
         buffer queue, if there is one; otherwise it retrieves the current
         working buffer.  If this function is invoked from the main application
         thread, as a result of user interaction, it is possible that the
         frame presentation thread will actually remove the head of the
         composition buffer queue while we are using it here.  However, this
         is of no concern, since popping buffers from the queue does not
         delete or even recycle their storage until a subsequent call to
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
      glClear(GL_COLOR_BUFFER_BIT);
      return true;
    }
  buffer->get_roi(buf_roi);
  
  // Obtain information required for all painting operations below
  kdms_display_geometry display_geometry;
  compositor->get_display_geometry(display_geometry);
  if (display_geometry.orientation != buf_orientation)
    { // Incompatible scaling factors; major view changes occurred; don't
      // paint anything or flip buffers at this point situation is transient.
      return false; // So the `flushBuffer' function is not invoked
    }

  // Start by finding a `gl_viewport' that represents what we believe to be
  // the region occupied by the `gl_view', expressed in the `doc_view'
  // coordinate system -- this is useful, because `izoom_point' is expressed
  // in `doc_view' coordinates.  For this purpose, the only relevant scaling
  // factor is `pixel_scale' and the buffer coordinates are taken to be
  // those given by `display_geometry'.
  NSRect gl_viewport;
  double doc_scale = pixel_scale;
  kdu_coords buf_min, buf_lim;
  buf_min = display_geometry.viewport_dims.pos-display_geometry.image_dims.pos;
  buf_lim = buf_min + display_geometry.viewport_dims.size;
  NSPoint doc_min, doc_lim;
  doc_min.x = buf_min.x*doc_scale;
  doc_lim.x = buf_lim.x*doc_scale;
  doc_min.y = (display_geometry.image_dims.size.y-buf_lim.y)*doc_scale;
  doc_lim.y = (display_geometry.image_dims.size.y-buf_min.y)*doc_scale;
  gl_viewport.origin = doc_min;
  gl_viewport.size.width = doc_lim.x - doc_min.x;
  gl_viewport.size.height = doc_lim.y - doc_min.y;
  
  // Now work out the impact of any differences between the image dimensions
  // for which the buffer was rendered and those recorded in `display_geometry'
  NSSize buf_viewport_size; // In buffer coordinates, but real-valued
  NSPoint buf_viewport_rel_pos; // Relative to `buf_image_dims.pos'
  buf_viewport_size.width = display_geometry.viewport_dims.size.x;
  buf_viewport_size.height = display_geometry.viewport_dims.size.y;
  buf_viewport_rel_pos.x = (display_geometry.viewport_dims.pos.x -
                            display_geometry.image_dims.pos.x);
  buf_viewport_rel_pos.y = (display_geometry.viewport_dims.pos.y -
                            display_geometry.image_dims.pos.y);
  double geo_scale = 1.0;
  double inv_geo_scale=1.0;
  if (display_geometry.image_dims.size != buf_image_dims.size)
    { 
      geo_scale = sqrt(((double) display_geometry.image_dims.area()) /
                       ((double) buf_image_dims.area()));
      inv_geo_scale = 1.0 / geo_scale;
      doc_scale *= geo_scale;
      buf_viewport_size.width *= inv_geo_scale;
      buf_viewport_size.height *= inv_geo_scale;
      buf_viewport_rel_pos.x *= inv_geo_scale;
      buf_viewport_rel_pos.y *= inv_geo_scale;
    }
  
  // Next, determine any displacement that needs to be applied in order to
  // keep the view centered about a animator-driven ROI -- i.e., `buf_roi'.  
  if (!buf_roi.is_empty())
    { 
      NSPoint rel_centre; // Centre of `buf_roi' relative to `buf_image_dims'
      rel_centre.x = (buf_roi.pos.x-buf_image_dims.pos.x) + 0.5*buf_roi.size.x;
      rel_centre.y = (buf_roi.pos.y-buf_image_dims.pos.y) + 0.5*buf_roi.size.y;
      NSPoint viewport_centre; // Centre of buf-viewport w.r.t `buf_image_dims'
      viewport_centre.x = buf_viewport_rel_pos.x+0.5*buf_viewport_size.width;
      viewport_centre.y = buf_viewport_rel_pos.y+0.5*buf_viewport_size.height;
      NSPoint shift; // Amount we need to shift the viewport right and down
      shift.x = rel_centre.x - viewport_centre.x;
      shift.y = rel_centre.y - viewport_centre.y;
      
      // Shift the `buf_viewport' but be careful not to let it move outside
      // the confines of `buf_image_dims'.
      NSSize buf_image_size;
      buf_image_size.width = buf_image_dims.size.x;
      buf_image_size.height = buf_image_dims.size.y;
      
      buf_viewport_rel_pos.x += shift.x;
      buf_viewport_rel_pos.y += shift.y;
      if (buf_viewport_rel_pos.x < 0.0)
        buf_viewport_rel_pos.x = 0.0;
      if (buf_viewport_rel_pos.y < 0.0)
        buf_viewport_rel_pos.y = 0.0;
      double tmp;
      if ((tmp = buf_viewport_rel_pos.x + buf_viewport_size.width -
           buf_image_size.width) > 0.0)
        buf_viewport_rel_pos.x -= tmp;
      if ((tmp = buf_viewport_rel_pos.y + buf_viewport_size.height -
           buf_image_size.height) > 0.0)
        buf_viewport_rel_pos.y -= tmp;
      
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
      assert((buf_image_dims.size.x > 0) && (buf_image_dims.size.y > 0));
      NSPoint scroller_pos;
      NSSize scroller_size;
      scroller_size.width = buf_viewport_size.width / buf_image_size.width;
      scroller_size.height = buf_viewport_size.height / buf_image_size.height;
      if (buf_image_size.width <= buf_viewport_size.width)
        scroller_pos.x = 0.0;
      else
        scroller_pos.x = (buf_viewport_rel_pos.x /
                          (buf_image_size.width - buf_viewport_size.width));
      if (buf_image_size.height <= buf_viewport_size.height)
        scroller_pos.y = 0.0;
      else
        scroller_pos.y = (buf_viewport_rel_pos.y /
                          (buf_image_size.height - buf_viewport_size.height));
      
      // Note: the following call sends a message to be processed in the
      // main application thread.
      [window fixScrollerPos:scroller_pos andSize:scroller_size];
    }
  else
    [window releaseScrollers:NO];
  
  // Now that we have taken care of dynamic ROI's we can calculate the
  // coordinate transformation that maps coordinates expressed within the
  // `gl_viewport' to coordinates that are expressed on the rendered
  // buffer surface, taking into account the impact of `izoom' and
  // `izoom_centre' parameters that perform local magnification/contraction
  // around the `izoom_centre' location within the same coordinate system
  // as `gl_viewport'.  The interpretation will be as follows:
  //    For a poitn (sx,sy) in `doc_view' coordinates, the corresponding point
  // (nx',ny') in `buf_dims' coordinates, expressed relative to
  // `buf_image_dims.pos', is given by
  //      nx' = buf_displacement.x + buf_scale * sx
  //      ny' = buf_displacement.y - buf_scale * sy
  // or in the reverse direction:
  //      sx = doc_displacement.x + doc_scale * nx'
  //      sy = doc_displacement.y - doc_scale * ny'
  double buf_scale = 1.0 / doc_scale;
  NSPoint buf_displacement, doc_displacement;
  buf_displacement.x = buf_viewport_rel_pos.x - buf_scale*gl_viewport.origin.x;
  buf_displacement.y = (buf_viewport_rel_pos.y + buf_viewport_size.height +
                        buf_scale*gl_viewport.origin.y);
  if (izoom != 1.0)
    { 
      double inv_izoom = 1.0 / izoom;
      buf_displacement.x += buf_scale*(1.0-inv_izoom)*izoom_centre.x;
      buf_displacement.y -= buf_scale*(1.0-inv_izoom)*izoom_centre.y;
      doc_scale *= izoom;
      buf_scale *= inv_izoom;
    }
  doc_displacement.x = -buf_displacement.x * doc_scale;
  doc_displacement.y = buf_displacement.y * doc_scale;

  // Prepare the gl_view's surface
  glClear(GL_COLOR_BUFFER_BIT);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glViewport(0,0, // Note that `gl_viewport' actually does have integer valued
             (int) round(gl_viewport.size.width),   // coords, but they are
             (int) round(gl_viewport.size.height)); // expressed as real values
  gluOrtho2D(0,gl_viewport.size.width,0,gl_viewport.size.height);

  kdu_dims buf_region; // Will be region to paint, expressed in buf coords
  buf_min.x = (int) floor(buf_viewport_rel_pos.x);
  buf_min.y = (int) floor(buf_viewport_rel_pos.y);
  buf_lim.x = (int) ceil(buf_viewport_rel_pos.x + buf_viewport_size.width);
  buf_lim.y = (int) ceil(buf_viewport_rel_pos.y + buf_viewport_size.height);
  buf_region.pos = buf_min + buf_image_dims.pos;
  buf_region.size = buf_lim - buf_min;
  buf_region &= buf_dims; // We only have data to paint this region
    
  if (!buf_region.is_empty())
    { // We have some buffer samples to paint
      NSPoint target_min, target_lim; // Expressed in `gl_view' coords
      buf_min = buf_region.pos - buf_image_dims.pos;
      buf_lim = buf_min + buf_region.size;
      doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
      target_min.x = doc_min.x - gl_viewport.origin.x;
      target_min.y = doc_min.y - gl_viewport.origin.y;
      target_lim.x = doc_lim.x - gl_viewport.origin.x;
      target_lim.y = doc_lim.y - gl_viewport.origin.y;

      // Paint the imagery
      int pixel_format = GL_RGBA;
      if (!KDU_IS_LITTLENDIAN())
        pixel_format = GL_BGRA;

      int buf_row_gap;
      kdu_uint32 *buf_data = buffer->get_buf(buf_row_gap,false);
      kdu_coords rel_pos = buf_region.pos - buf_dims.pos;
      glPixelStorei(GL_UNPACK_ALIGNMENT,4);
      glPixelStorei(GL_UNPACK_ROW_LENGTH,buf_row_gap);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,buf_region.size.x,
                   buf_region.size.y,0,pixel_format,
                   GL_UNSIGNED_INT_8_8_8_8_REV,
                   buf_data + rel_pos.x + rel_pos.y*buf_row_gap);

      glEnable(GL_TEXTURE_2D);
      glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 1.0f);   glVertex2f(target_min.x, target_min.y);
      glTexCoord2f(1.0f, 1.0f);   glVertex2f(target_lim.x, target_min.y);
      glTexCoord2f(1.0f, 0.0f);   glVertex2f(target_lim.x, target_lim.y);
      glTexCoord2f(0.0f, 0.0f);   glVertex2f(target_min.x, target_lim.y);
      glEnd();
      glDisable(GL_TEXTURE_2D);
      
      // Do focus highlighting, if necessary
      if ((display_geometry.focus_state != 0) && highlight_focus)
        { // For simplicity here, we ignore any disconnect between
          // `display_geometry.image_dims' and `buf_image_dims', although
          // we could take this into account in a similar fashion to
          // `paint_region' -- note, however, that we also ignored any
          // disconnect when replacing `display_geometry.focus_box' with
          // `buf_roi' in the case where there is an animator-driven region
          // of interest.  In reality, disconnects between `buf_image_dims'
          // and `display_geometry.image_dims' are at most transient
          // conditions.
          kdu_dims focus_region = display_geometry.focus_box;
          focus_region &= buf_region;
          if (!focus_region)
            focus_region.pos = buf_region.pos;
          
          NSPoint focus_min, focus_lim; // expressed in `gl_view' coords
          buf_min = focus_region.pos - buf_image_dims.pos;
          buf_lim = buf_min + focus_region.size;
          doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
          doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
          doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
          doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
          focus_min.x = doc_min.x - gl_viewport.origin.x;
          focus_min.y = doc_min.y - gl_viewport.origin.y;
          focus_lim.x = doc_lim.x - gl_viewport.origin.x;
          focus_lim.y = doc_lim.y - gl_viewport.origin.y;
          
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
          NSPoint coords[4] = {target_min,focus_min,focus_lim,target_lim};
          for (int v=0; v < 3; v++)
            { 
              GLfloat min_y=coords[v].y, lim_y=coords[v+1].y;
              if (lim_y <= min_y)
                continue;
              for (int h=0; h < 3; h++)
                { 
                  GLfloat min_x=coords[h].x, lim_x=coords[h+1].x;
                  if (lim_x <= min_x)
                    continue;
                  if ((h == 1) && (v == 1))
                    { // Central "focus" region needs a boost
                      glColor4f(1.0f,1.0f,1.0f,0.156f);
                    }
                  else
                    { // Outer "non-focus" region needs darkening
                      glColor4f(0.0f,0.0f,0.0f,0.156f);
                    }
                  glBegin(GL_QUADS);
                  glVertex2f(min_x,min_y);
                  glVertex2f(lim_x,min_y);
                  glVertex2f(lim_x,lim_y);
                  glVertex2f(min_x,lim_y);
                  glEnd();
                }
            }
          glColor4f(1,1,1,1); // Restore default colour         
          glDisable(GL_BLEND);
        }
    }
  
  // See if we need to draw a fous box outline
  if (display_geometry.focus_state != 0)
    { 
      assert(!shape_istream.exists());
      NSPoint focus_min, focus_lim; // expressed in `gl_view' coords
      buf_min = display_geometry.focus_box.pos - buf_image_dims.pos;
      buf_lim = buf_min + display_geometry.focus_box.size;
      doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
      focus_min.x = doc_min.x - gl_viewport.origin.x;
      focus_min.y = doc_min.y - gl_viewport.origin.y;
      focus_lim.x = doc_lim.x - gl_viewport.origin.x;
      focus_lim.y = doc_lim.y - gl_viewport.origin.y;

      glEnable(GL_BLEND); // The lines may have non-trivial alpha
      glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_LINE_SMOOTH);
      glLineWidth(1.0F);
      glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);

      // Draw inner rectangle
      focus_min.x += 0.5F;  focus_min.y += 0.5F;
      focus_lim.x -= 0.5F;  focus_lim.y -= 0.5F;
      if ((focus_lim.x > focus_min.x) && (focus_lim.y > focus_min.y))
        { 
          if (display_geometry.focus_state > 0) // Draw brown line
            glColor4f(1.0F,1.0F,0.0F,1.0F);
          else // Change colour to indicate a metadata-derived focus box
            glColor4f(1.0F,1.0F,0.0F,0.8F);
          glBegin(GL_QUADS);
          glVertex2f(focus_min.x,focus_min.y);
          glVertex2f(focus_lim.x,focus_min.y);
          glVertex2f(focus_lim.x,focus_lim.y);
          glVertex2f(focus_min.x,focus_lim.y);
          glEnd();
        }
            
      // Draw outer rectangle
      focus_min.x -= 1.0F;  focus_min.y -= 1.0F;
      focus_lim.x += 1.0F;  focus_lim.y += 1.0F;
      if (display_geometry.focus_state > 0) // Draw yellow line
        glColor4f(0.6F,0.4F,0.2F,1.0F);
      else // Change colour to indicate a metadata-derived focus box
        glColor4f(0.0F,0.0F,1.0F,0.8F);
      glBegin(GL_QUADS);
      glVertex2f(focus_min.x,focus_min.y);
      glVertex2f(focus_lim.x,focus_min.y);
      glVertex2f(focus_lim.x,focus_lim.y);
      glVertex2f(focus_min.x,focus_lim.y);
      glEnd();
      glColor4f(1,1,1,1); // Restore default colour
      glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); // Restore default fill mode
      glDisable(GL_BLEND); // Restore default no-blending option
    }

  // See if we need to draw a "focussing" outline
  kdu_dims focussing_rect;
  if (compositor->get_focussing_rect(focussing_rect))
    { // Focussing outlines are drawn on the immediate surface managed by
      // `display_geometry'.  Again, we will not bother converting between
      // the `display_geometry' and the rendering buffer geometry, since they
      // are identical except under brief transient conditions and these
      // conditions (interactive zoom) are unlikely to occur while a user
      // is drawing a focussing outline on the display.
      NSPoint focus_min, focus_lim; // expressed in `gl_view' coords
      buf_min = focussing_rect.pos - buf_image_dims.pos;
      buf_lim = buf_min + focussing_rect.size;
      doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
      doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
      doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
      doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
      focus_min.x = doc_min.x - gl_viewport.origin.x;
      focus_min.y = doc_min.y - gl_viewport.origin.y;
      focus_lim.x = doc_lim.x - gl_viewport.origin.x;
      focus_lim.y = doc_lim.y - gl_viewport.origin.y;

      glEnable(GL_LINE_SMOOTH);
      glEnable(GL_LINE_STIPPLE);
      glLineWidth(2.0F);
      glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
      
      // Draw brown inner rectangle
      focus_min.x += 1.0F;  focus_min.y += 1.0F;
      focus_lim.x -= 1.0F;  focus_lim.y -= 1.0F;
      if ((focus_lim.x > focus_min.x) && (focus_lim.y > focus_min.y))
        { 
          glLineStipple(6,0xAAAA);
          glColor4f(0.6F,0.4F,0.2F,1.0F);
          glBegin(GL_QUADS);
          glVertex2f(focus_min.x,focus_min.y);
          glVertex2f(focus_lim.x,focus_min.y);
          glVertex2f(focus_lim.x,focus_lim.y);
          glVertex2f(focus_min.x,focus_lim.y);
          glEnd();
        }

      // Draw yellow outer rectangle
      focus_min.x -= 2.0F;  focus_min.y -= 2.0F;
      focus_lim.x += 2.0F;  focus_lim.y += 2.0F;
      glLineStipple(6,0x5555);
      glColor4f(1.0F,1.0F,0.0F,1.0F);
      glBegin(GL_QUADS);
      glVertex2f(focus_min.x,focus_min.y);
      glVertex2f(focus_lim.x,focus_min.y);
      glVertex2f(focus_lim.x,focus_lim.y);
      glVertex2f(focus_min.x,focus_lim.y);
      glEnd();
      glColor4f(1,1,1,1); // Restore default colour
      glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); // Restore default fill mode
      glDisable(GL_LINE_STIPPLE); // Restore default no-stippling option
    }
  
  // See if we need to draw frame bounds during interacive zoom
  buf_min = kdu_coords(0,0);  buf_lim = buf_image_dims.size;
  doc_min.x = doc_displacement.x + doc_scale*buf_min.x;
  doc_lim.x = doc_displacement.x + doc_scale*buf_lim.x;
  doc_min.y = doc_displacement.y - doc_scale*buf_lim.y;
  doc_lim.y = doc_displacement.y - doc_scale*buf_min.y;
  NSPoint frame_min, frame_lim;
  frame_min.x = doc_min.x - gl_viewport.origin.x;
  frame_min.y = doc_min.y - gl_viewport.origin.y;
  frame_lim.x = doc_lim.x - gl_viewport.origin.x;
  frame_lim.y = doc_lim.y - gl_viewport.origin.y;
  if ((frame_min.x > 0.5) ||
      (frame_min.y > 0.5) ||
      (frame_lim.x < (gl_viewport.size.width-0.5)) ||
      (frame_lim.y < (gl_viewport.size.height-0.5)))
    { 
      glEnable(GL_LINE_SMOOTH);
      glEnable(GL_LINE_STIPPLE);
      glLineWidth(1.0F);
      glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
      glLineStipple(6,0x5555);
      glColor4f(0.0F,0.0F,0.0F,1.0F);
      glBegin(GL_QUADS);
      glVertex2f(frame_min.x,frame_min.y);
      glVertex2f(frame_lim.x,frame_min.y);
      glVertex2f(frame_lim.x,frame_lim.y);
      glVertex2f(frame_min.x,frame_lim.y);
      glEnd();
      glColor4f(1,1,1,1); // Restore default colour
      glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); // Restore default fill mode
      glDisable(GL_LINE_STIPPLE); // Restore default no-stippling option
    }
  return true;
}

/*****************************************************************************/
/*                kdms_renderer::present_queued_frame_buffer                 */
/*****************************************************************************/

bool
  kdms_renderer::present_queued_frame_buffer(double display_event_time,
                                             double next_display_event_time,
                                             NSGraphicsContext *gc)
{
  if ((compositor == NULL) || (!view_dims) || !gl_view)
    return false;
  if (!compositor->lock_new_composition_queue_head(display_event_time,
                                                   next_display_event_time))
    return false;
  [gl_view lock_context];
  NSOpenGLContext *gl_context = [gl_view openGLContext];
  [gl_context makeCurrentContext];
  if (paint_gl(true)) // Otherwise nothing was draw or even cleared
    [gl_context flushBuffer]; // Perform synchronized display buffer flip
                              // without blocking the caller.
  [NSOpenGLContext clearCurrentContext]; // Prevent accidental use of context
  [gl_view unlock_context];
  compositor->unlock_and_mark_composition_queue_head();
  return true;
}

/*****************************************************************************/
/*                      kdms_renderer::display_status                        */
/*****************************************************************************/

void kdms_renderer::display_status()
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
          component_scale_x *= (float) izoom;
          component_scale_y *= (float) izoom;
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
      float res_percent = 100.0F*rendering_scale*(float)izoom;
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
          [window set_progress_bar:jpip_progress];
        }
    }
  [window set_status_strings:((const char **)panel_text)];
}

/*****************************************************************************/
/*                        kdms_renderer::do_processing                       */
/*****************************************************************************/

bool kdms_renderer::do_processing()
{
  double cur_time = get_current_time();
  
  // Start by managing the processing of imagery
  kdu_dims new_region;
  bool processed_something = false;
  bool processing_complete = true;
  bool processing_newly_complete = false;
  int first_rendered_row=0, num_rendered_rows=0;
  if (configuration_complete && (working_buffer != NULL) &&
      !animation_frame_complete)
    { 
      if (animator != NULL)
        animation_frame_started = true;
      try {
        int defer_flag = KDU_COMPOSIT_DEFER_REGION;
        if ((animator == NULL) && (defer_process_regions_deadline > 0.0) &&
            (cur_time > defer_process_regions_deadline))
          { 
            defer_flag=0;
            defer_process_regions_deadline = cur_time + 0.05;
          }
        processed_something=compositor->process(compositor_process_increment,
                                                new_region,defer_flag);
        if (!(processed_something ||
              compositor->get_total_composition_dims(image_dims)))
          { // Must have invalid scale
            working_buffer = NULL;
            defer_process_regions_deadline = -1.0;
            initialize_regions(false); // Call will find a satisfactory scale
            return false;
          }
        if ((!working_buffer->get_rendering_status(num_rendered_rows,
                                                   first_rendered_row,
                                                   false)) &&
            (earliest_incremental_paint_time <= 0.0))
          { // We will have to paint the entire buffer at once; don't do this
            // too hastily -- best to render completely first
            earliest_incremental_paint_time = cur_time + 0.1;
          }
        cur_time = get_current_time();
        if (processed_something && (animator == NULL))
          { 
            if (defer_process_regions_deadline <= 0.0)
              defer_process_regions_deadline = cur_time + 0.05;
            else if ((defer_flag == 0) &&
                     (num_rendered_rows > first_rendered_row))
              defer_process_regions_deadline += 0.05;
          }
      }
      catch (kdu_exception exc) { // Only safe thing is close the file.
        close_after_exception(exc);
        return false;
      }
      catch (std::bad_alloc &) {
        close_after_exception(KDU_MEMORY_EXCEPTION);
        NSAlert *alert = [[NSAlert alloc] init];
        [alert
         setMessageText:@"Insufficient memory -- you could improve "
                         "upon the current implementation to invoke "
                         "`kdu_region_compositor::cull_inactive_layers' "
                         "or take other memory-saving action in response "
                         "to this condition."];
        [alert addButtonWithTitle:@"OK"];
        [alert setAlertStyle:NSCriticalAlertStyle];
        [alert runModal];
        return false;
      }

      processing_complete = compositor->is_processing_complete();
      processing_newly_complete = (processed_something && processing_complete);
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
      
      // Paint any newly decompressed region right away (perhaps).
      if ((num_rendered_rows > first_rendered_row) &&
          (processing_complete ||
           (cur_time >= earliest_incremental_paint_time)))
        { 
          bool prior_rendered_content_valid =
            working_buffer->get_rendering_status(num_rendered_rows,
                                                 first_rendered_row,true);
          if (!prior_rendered_content_valid)
            [doc_view setNeedsDisplay:YES]; // Update everything
          else
            { 
              new_region = working_buffer_dims;
              if (prior_rendered_content_valid)
                { 
                  new_region.pos.y += first_rendered_row;
                  new_region.size.y = num_rendered_rows;
                  if (first_rendered_row > 0)
                    { // Create a little overlap to avoid possibility of cracks
                      // being introduced if interpolation is involved.
                      new_region.pos.y--;
                      new_region.size.y++;
                    }
                }
              NSRect dirty_rect = convert_region_to_doc_view(new_region);
              if (izoom != 1.0)
                { 
                  dirty_rect.origin.x *= izoom;
                  dirty_rect.origin.x += (1.0-izoom) * izoom_centre.x;
                  dirty_rect.origin.y *= izoom;
                  dirty_rect.origin.y *= (1.0-izoom) * izoom_centre.y;
                }
              [doc_view setNeedsDisplayInRect:dirty_rect];
                earliest_incremental_paint_time = cur_time + 0.05;
            }
        }
      if (processing_complete)
        earliest_incremental_paint_time = -1.0;
      if ((jpip_client == NULL) || processing_newly_complete)
        display_status(); // All processing done, keep status up to date
    }

  return ((compositor != NULL) && !processing_complete);
}

/*****************************************************************************/
/*                 kdms_renderer::update_animation_status_info               */
/*****************************************************************************/

void kdms_renderer::update_animation_status_info()
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
      bool have_updates = false;
      if (animation_status_invalid)
        { 
          need_display_status_update = have_updates = true;
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
              need_display_status_update = have_updates = true;
              if (animation_bar != nil)
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
              if (catalog_source != nil)
                need_catalog_node_select = have_updates = true;
              if (metamation_bar != nil)
                need_metamation_bar_update = have_updates = true;
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
              need_display_status_update = have_updates = true;
              double delta = (presented_display_time -
                              animation_jpip_status_update_time);
              delta = 0.25 * (1.0+floor(delta*4.0)); // Advance at least 0.25s
              animation_jpip_status_update_time += delta;
            }
        }
      if (need_display_status_update)
        display_status();
      if (need_catalog_node_select)
        [catalog_source select_matching_metanode:presented_metanode];
      if (need_animation_bar_update)
        { 
          if (presented_frame_start < 0.0)
            [animation_bar set_frame:presented_idx
                                 max:max_compositing_layer_idx
                               start:-1.0 end:-1.0 track_duration:-1.0];
          else
            { 
              double track_end=-1.0;
              if (!get_track_duration(track_end))
                track_end = -1.0;
              [animation_bar set_frame:presented_idx
                                   max:(num_frames-1)
                                 start:presented_frame_start
                                   end:presented_frame_end
                        track_duration:track_end];          
            }
        }
      if (need_metamation_bar_update)
        { 
          int num_meta_frames;
          if (!animator->count_metadata_driven_frames(num_meta_frames))
            num_meta_frames = 0;
          [metamation_bar set_frame:presented_metamation_id
                                max:(num_meta_frames-1)
                              start:-1.0 end:-1.0 track_duration:-1.0];
        }
      if (pushed_last_frame && compositor->composition_queue_tail_presented())
        { 
          have_updates = true;
          stop_animation();
        }
    }
}

/*****************************************************************************/
/*                 kdms_renderer::manage_animation_frame_queue               */
/*****************************************************************************/

void kdms_renderer::manage_animation_frame_queue(double cur_system_time)
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
        { // Running behind by more than 100ms
          double disp_interval =
          next_display_event_time - last_display_event_time;
          int n_intervals = (int)(0.5*overdue / disp_interval);
          if (n_intervals > 0)
            window_manager->broadcast_playclock_adjustment(n_intervals *
                                                           disp_interval);
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
          // `kdms_window::display_notification' function will be called.
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
/*                          kdms_renderer::wakeup                            */
/*****************************************************************************/

void kdms_renderer::wakeup(double scheduled_time, double current_time)
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
/*                   kdms_renderer::client_notification                      */
/*****************************************************************************/

void kdms_renderer::client_notification()
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
      if (metadata_changed || /*bytes_changed ||*/ status_text_changed)
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
  
  // See if the receipt of data by the JPIP client warrants the
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
/*                      kdms_renderer::render_refresh                        */
/*****************************************************************************/

bool kdms_renderer::render_refresh(bool new_imagery_only)
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
/*              kdms_renderer::perform_or_schedule_render_refresh            */
/*****************************************************************************/

void kdms_renderer::perform_or_schedule_render_refresh(double cur_time)
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
/*                      kdms_renderer::refresh_metadata                      */
/*****************************************************************************/

void kdms_renderer::refresh_metadata(double cur_time)
{
  if ((compositor != NULL) && (metashow == nil))
    compositor->load_metadata_matches();
  else if (jpx_in.exists())
    jpx_in.access_meta_manager().load_matches(-1,NULL,-1,NULL);
    
  if (metashow != nil)
    [metashow update_metadata];
  
  if (compositor != NULL)
    { 
      if ((catalog_source == nil) && jpx_in.exists() && (!catalog_closed) &&
          kdms_catalog_new_data_available(jpx_in.access_meta_manager()))
        [window create_metadata_catalog];
  
      if (catalog_source != nil)
        [catalog_source update_metadata];
    }

  next_metadata_refresh_time = -1.0;
  last_metadata_refresh_time = cur_time;
  
  if (animator == NULL)
    update_client_window_of_interest(cur_time);
}

/*****************************************************************************/
/*               kdms_renderer::install_next_scheduled_wakeup                */
/*****************************************************************************/

void kdms_renderer::install_next_scheduled_wakeup()
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
        window_manager->schedule_wakeup(window,-1.0); // Cancel existing wakeup
      next_scheduled_wakeup_time = -100.0; // Make sure it cannot pass any
        // of the tests in the above functions, which coerce a loose wakeup
        // time to equal the next scheduled wakeup time if it is already close.
    }
  else if (min_time != next_scheduled_wakeup_time)
    {
      next_scheduled_wakeup_time = min_time;
      window_manager->schedule_wakeup(window,min_time);
    }
}

/*****************************************************************************/
/*                       kdms_renderer::increase_scale                       */
/*****************************************************************************/

float kdms_renderer::increase_scale(float from_scale, bool slightly)
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
/*                       kdms_renderer::decrease_scale                       */
/*****************************************************************************/

float kdms_renderer::decrease_scale(float from_scale, bool slightly)
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
/*                      kdms_renderer::change_frame_idx                      */
/*****************************************************************************/

bool kdms_renderer::change_frame_idx(int new_frame_idx)
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
/*                        kdms_renderer::change_frame                        */
/*****************************************************************************/

void kdms_renderer::change_frame(jpx_frame new_frame)
{
  if ((!composition_rules) || (new_frame == frame) || !new_frame.exists())
    return;
  bool last_in_context=false;
  kdu_uint32 new_track_idx = new_frame.get_track_idx(last_in_context);
  if ((track_idx < new_track_idx) ||
      ((track_idx > new_track_idx) && !last_in_context))
    { // Current one not compatible with `new_frame'
      track_idx = new_track_idx;
      num_frames_known = false;
    }
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
/*                     kdms_renderer::get_track_duration                     */
/*****************************************************************************/

bool
  kdms_renderer::get_track_duration(double &duration)
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
/*                   kdms_renderer::find_compatible_frame                    */
/*****************************************************************************/

jpx_frame
  kdms_renderer::find_compatible_frame(jpx_metanode numlist, int num_regions,
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
/*                        kdms_renderer::set_rel_focus                       */
/*****************************************************************************/

void kdms_renderer::set_rel_focus(kdu_dims box)
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
/*                      kdms_renderer::adjust_rel_focus                      */
/*****************************************************************************/

bool kdms_renderer::adjust_rel_focus()
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
    { // In JPX frame composition or MJ2 frame display mode
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
/*                  kdms_renderer::calculate_rel_view_params                 */
/*****************************************************************************/

void kdms_renderer::calculate_rel_view_params()
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
/*                  kdms_renderer::scroll_to_view_anchors                    */
/*****************************************************************************/

void kdms_renderer::scroll_to_view_anchors()
{
  if (!view_centre_known)
    return;
  NSSize doc_size = [doc_view frame].size;
  NSRect view_rect = [scroll_view documentVisibleRect];
  NSPoint new_view_origin;
  new_view_origin.x = (float)
    floor(0.5 + doc_size.width * view_centre_x - 0.5F*view_rect.size.width);
  new_view_origin.y = (float)
    floor(0.5+doc_size.height*(1.0F-view_centre_y)-0.5F*view_rect.size.height);
  view_centre_known = false;
  if (animation_advancing)
    { // The animator is in the process of changing animation frames and
      // must have called `initialize_regions' to do so, which has in turn
      // called this function.  We don't want any imagery to be rendered as
      // a result of the above scrolling call, because imagery will be
      // rendered at the correct display time once the animation frame
      // has been generated and pushed onto the queue.  However, to ensure
      // that everything works as expected we still want the call to
      // `scrollPoint'.  Since we cannot prevent `scrollPoint' from
      // generating its own calls to `setNeedsDisplay', the best thing we
      // can do here is to force immediate flushing of the content that
      // is waiting for display update.  This results in an immediate call to
      // `paint_region', rather than waiting until the regular display update
      // cycle when the run loop is next idle.  This, in turn, allows the
      // `paint_region' function to detect the `animation_advancing' condition
      // and skip the painting process.
      [doc_view enablePaintRegion:NO];
      [doc_view scrollPoint:new_view_origin];
      [doc_view displayIfNeeded];
      [doc_view enablePaintRegion:YES];
    }
  else
    [doc_view scrollPoint:new_view_origin];
}

/*****************************************************************************/
/*                 kdms_renderer::scroll_relative_to_view                    */
/*****************************************************************************/

void kdms_renderer::scroll_relative_to_view(float h_scroll, float v_scroll)
{
  NSSize doc_size = [doc_view frame].size;
  NSRect view_rect = [scroll_view documentVisibleRect];
  NSPoint new_view_origin = view_rect.origin;
  
  h_scroll *= view_rect.size.width;
  v_scroll *= view_rect.size.height;
  if (h_scroll > 0.0F)
    h_scroll = ((float) ceil(h_scroll / pixel_scale)) * pixel_scale;
  else if (h_scroll < 0.0F)
    h_scroll = ((float) floor(h_scroll / pixel_scale)) * pixel_scale;
  if (v_scroll > 0.0F)
    v_scroll = ((float) ceil(v_scroll / pixel_scale)) * pixel_scale;
  else if (v_scroll < 0.0F)
    v_scroll = ((float) floor(v_scroll / pixel_scale)) * pixel_scale;
  
  new_view_origin.x += h_scroll;
  new_view_origin.y -= v_scroll;
  if (new_view_origin.x < 0.0F)
    new_view_origin.x = 0.0F;
  if ((new_view_origin.x + view_rect.size.width) > doc_size.width)
    new_view_origin.x = doc_size.width - view_rect.size.width;
  if ((new_view_origin.y + view_rect.size.height) > doc_size.height)
    new_view_origin.y = doc_size.height - view_rect.size.height;
  [doc_view scrollPoint:new_view_origin];
}

/*****************************************************************************/
/*                     kdms_renderer::scroll_absolute                        */
/*****************************************************************************/

void kdms_renderer::scroll_absolute(float h_scroll, float v_scroll,
                                    bool use_doc_coords)
{
  if (!use_doc_coords)
    {
      h_scroll *= pixel_scale;
      v_scroll *= pixel_scale;
      v_scroll = -v_scroll;
    }
  
  NSSize doc_size = [doc_view frame].size;
  NSRect view_rect = [scroll_view documentVisibleRect];
  NSPoint new_view_origin = view_rect.origin;
  new_view_origin.x += h_scroll;
  new_view_origin.y += v_scroll;
  if (new_view_origin.x < 0.0F)
    new_view_origin.x = 0.0F;
  if ((new_view_origin.x + view_rect.size.width) > doc_size.width)
    new_view_origin.x = doc_size.width - view_rect.size.width;
  if ((new_view_origin.y + view_rect.size.height) > doc_size.height)
    new_view_origin.y = doc_size.height - view_rect.size.height;
  [doc_view scrollPoint:new_view_origin];
}

/*****************************************************************************/
/*                      kdms_renderer::zoom_interactive                      */
/*****************************************************************************/

void kdms_renderer::zoom_interactive(double factor, NSPoint stationary_point,
                                     bool consolidate)
{
  izoom *= factor;
  izoom_centre = stationary_point;
  if (consolidate)
    { 
      if ((working_buffer != NULL) && (compositor != NULL))
        { 
          float scale_anchor = (float) izoom;
          float min_scale = scale_anchor * 0.9F;
          float max_scale = scale_anchor * 1.1F;
          if ((izoom > 1.0) && (min_scale < 1.0F))
            min_scale = 1.0F;
          else if ((izoom < 1.0) && (max_scale > 1.0F))
            max_scale = 1.0;
          min_scale *= rendering_scale;
          max_scale *= rendering_scale;
          scale_anchor *= rendering_scale;
          if (max_rendering_scale > 0.0F)
            { 
              if (min_scale > max_rendering_scale)
                min_scale = max_rendering_scale;
              if (max_scale > max_rendering_scale)
                max_scale = max_rendering_scale;
              if (scale_anchor > max_rendering_scale)
                scale_anchor = max_rendering_scale;
            }
          kdu_dims region_basis = find_focus_box();
          float new_scale =
            compositor->find_optimal_scale(region_basis,scale_anchor,
                                           min_scale,max_scale);
          if (new_scale < min_scale)
            max_rendering_scale = new_scale; // This is as large as we can go
          
          if (!image_dims.is_empty())
            { // Before changing the rendering scale, work out where the new
              // viewport should be centred.
              kdu_coords rel_stationary_point = // Relative to image origin
              convert_point_from_doc_view(izoom_centre) - image_dims.pos;
              float rpos_x = rel_stationary_point.x / (float)image_dims.size.x;
              float rpos_y = rel_stationary_point.y / (float)image_dims.size.y;
              calculate_rel_view_params(); // Find centre of current view
              float alpha = new_scale / rendering_scale;
              view_centre_x = (view_centre_x - (1.0F-alpha)*rpos_x) / alpha;
              view_centre_y = (view_centre_y - (1.0F-alpha)*rpos_y) / alpha;
            }
          rendering_scale = new_scale;
          initialize_regions(true);
        }
      izoom = 1.0;
    }
  if (compositor != NULL)
    { 
      if (animator == NULL)
        { 
          [doc_view setNeedsDisplay:YES];
          if (compositor->is_processing_complete())
            display_status();
        }
      else
        animation_status_invalid = true;
      compositor->force_display_redraw();
    }
}

/*****************************************************************************/
/*                       kdms_renderer::find_focus_box                       */
/*****************************************************************************/

kdu_dims kdms_renderer::find_focus_box(bool allow_box_shift)
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
/*                      kdms_renderer::set_focus_box                         */
/*****************************************************************************/

void kdms_renderer::set_focus_box(kdu_dims new_box, jpx_metanode node)
{
  if (rel_focus.animator_driven)
    return; // Adjustments not allowed until this condition is removed
  if ((new_box.size.x < 0) || (new_box.size.y < 0) ||
      ((new_box.area() < (kdu_long) 100) && !node))
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
    { // Something has happened
      [doc_view setNeedsDisplay:YES];
    }
  update_client_window_of_interest();
  display_status();
}

/*****************************************************************************/
/*                     kdms_renderer::displace_focus_box                     */
/*****************************************************************************/

kdu_coords kdms_renderer::displace_focus_box(kdu_coords shift)
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
        scroll_absolute(scroll.x,scroll.y,false);
      shift -= scroll;
    }
  rel_focus.reset();
  set_focus_box(new_box,jpx_metanode());
  return shift;
}

/*****************************************************************************/
/*                       kdms_renderer::scale_focus_box                      */
/*****************************************************************************/

NSSize kdms_renderer::scale_focus_box(NSSize factors)
{
  if (shape_istream.exists() || focus_metanode.exists())
    return factors;
  kdu_dims old_box;
  if (rel_focus.is_valid())
    { 
      old_box.pos.x = (int)
        floor(0.4 + (rel_focus.region.centre_x -
                     0.5*rel_focus.region.size_x) * image_dims.size.x);
      old_box.pos.y = (int)
        floor(0.4 + (rel_focus.region.centre_y -
                     0.5*rel_focus.region.size_y)*image_dims.size.y);
      old_box.size.x = (int)
        floor(0.6 + (rel_focus.region.centre_x +
                     0.5*rel_focus.region.size_x)*image_dims.size.x);
      old_box.size.y = (int)
        floor(0.6 + (rel_focus.region.centre_y +
                     0.5*rel_focus.region.size_y)*image_dims.size.y);
      old_box.size -= old_box.pos;
      old_box.pos += image_dims.pos;
    }
  else
    { 
      if ((factors.width >= 1.0) && (factors.height >= 1.0))
        return factors; // Unable to apply any of them
      else
        old_box = image_dims;
    }

  kdu_coords half_size_inc;
  half_size_inc.x = (int) round(old_box.size.x * 0.5 * (factors.width-1.0));
  half_size_inc.y = (int) round(old_box.size.y * 0.5 * (factors.height-1.0));
  if ((half_size_inc.x == 0) && (half_size_inc.y == 0))
    return factors; // No change
  kdu_dims new_box = old_box;
  new_box.pos -= half_size_inc;
  new_box.size += half_size_inc + half_size_inc;
  if (new_box.size.x >= image_dims.size.x)
    new_box.size.x = image_dims.size.x;
  if (new_box.size.y >= image_dims.size.y)
    new_box.size.y = image_dims.size.y;
  int delta;
  if ((delta = (new_box.pos.x - image_dims.pos.x)) < 0)
    new_box.pos.x -= delta;
  if ((delta = (new_box.pos.x + new_box.size.x -
                image_dims.pos.x - image_dims.size.x)) > 0)
    new_box.pos.x -= delta;
  if ((delta = (new_box.pos.y - image_dims.pos.y)) < 0)
    new_box.pos.y -= delta;
  if ((delta = (new_box.pos.y + new_box.size.y -
                image_dims.pos.y - image_dims.size.y)) > 0)
    new_box.pos.y -= delta;
  if ((new_box.size.x <= 0) || (new_box.size.y <= 0))
    { 
      factors.width = factors.height = 0.0F; // We cannot grow this back again
      new_box = kdu_dims();
    }
  else
    { 
      factors.width = (old_box.size.x*factors.width) / new_box.size.x;
      factors.height = (old_box.size.y*factors.height) / new_box.size.y;
      if (new_box.size == image_dims.size)
        new_box = kdu_dims();
    }
  set_focus_box(new_box,jpx_metanode());
  return factors;
}

/*****************************************************************************/
/*                      kdms_renderer::update_focus_box                      */
/*****************************************************************************/

void kdms_renderer::update_focus_box(bool view_may_be_scrolling)
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
      [doc_view setNeedsDisplay:YES];
    }
  update_client_window_of_interest();
}

/*****************************************************************************/
/*            kdms_renderer::check_initial_request_compatibility             */
/*****************************************************************************/

bool kdms_renderer::check_initial_request_compatibility()
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
/*              kdms_renderer::update_client_window_of_interest              */
/*****************************************************************************/

void
  kdms_renderer::update_client_window_of_interest(double cur_time,
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
        if ([catalog_source generate_client_requests:&tmp_roi] > 0)
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
        else if (!(image_dims.is_empty() || roi_region.is_empty()))
          { 
            woi_dims = roi_region;
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
                                      source_dims,target_dims,
                                      orientation);
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
/*                    kdms_renderer::change_client_focus                     */
/*****************************************************************************/

void kdms_renderer::change_client_focus(kdu_coords actual_resolution,
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
/*                      kdms_renderer::set_codestream                        */
/*****************************************************************************/

void kdms_renderer::set_codestream(int codestream_idx)
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                  kdms_renderer::set_compositing_layer                     */
/*****************************************************************************/

void kdms_renderer::set_compositing_layer(int layer_idx)
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                  kdms_renderer::set_compositing_layer                     */
/*****************************************************************************/

void kdms_renderer::set_compositing_layer(kdu_coords point)
{
  if (compositor == NULL)
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
  point += view_dims.pos;
  kdu_ilayer_ref ilyr = compositor->find_point(point);
  int layer_idx, cs_idx; bool is_opaque;
  if ((compositor->get_ilayer_info(ilyr,layer_idx,cs_idx,is_opaque) > 0) &&
      (layer_idx >= 0))
    set_compositing_layer(layer_idx);
}

/*****************************************************************************/
/*                        kdms_renderer::set_frame                           */
/*****************************************************************************/

bool kdms_renderer::set_frame(int new_frame_idx, double cur_time)
{
  if ((compositor == NULL) ||
      (single_component_idx >= 0) ||
      (single_layer_idx >= 0))
    return false;
  if (shape_istream.exists())
    { NSBeep(); return false; }
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
          { // May need to adjust buffer surface based on animation ROI
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
                (node=[catalog_source get_selected_metanode]).exists())
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
  catch (std::bad_alloc) {
    close_file();
    return false;
  }
  return true;
}

/*****************************************************************************/
/*                        kdms_renderer::set_frame                           */
/*****************************************************************************/

bool kdms_renderer::set_frame(jpx_frame new_frame, double cur_time)
{
  if ((compositor == NULL) || (single_component_idx >= 0) ||
      (single_layer_idx >= 0))
    return false;
  if (shape_istream.exists())
    { NSBeep(); return false; }
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
              (node=[catalog_source get_selected_metanode]).exists())
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
/*                kdms_renderer::set_animation_point_by_idx                  */
/*****************************************************************************/

void kdms_renderer::set_animation_point_by_idx(int idx)
{
  if (animator != NULL)
    stop_animation();
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*               kdms_renderer::set_animation_point_by_time                  */
/*****************************************************************************/

void kdms_renderer::set_animation_point_by_time(double time,
                                                bool nearest_start)
{
  if (animator != NULL)
    stop_animation();
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                      kdms_renderer::stop_animation                        */
/*****************************************************************************/

void kdms_renderer::stop_animation()
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
  [window release_glview];
  gl_view = nil;
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
      view_dims_changed(); // Make sure we install the right buffer surface
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
  if (animation_bar != nil)
    { 
      update_animation_bar_state();
      update_animation_bar_pos();
    }
  else if (metamation_bar != nil)
    { 
      [window remove_animation_bar];
      metamation_bar = nil;
    }

  [window releaseScrollers:YES];
  
  animation_advancing = false; // Just in case
  update_client_window_of_interest();
}

/*****************************************************************************/
/*                     kdms_renderer::start_animation                        */
/*****************************************************************************/

kdu_region_animator *kdms_renderer::start_animation(bool reverse,
                                                    jpx_metanode metanode)
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
  animation_frame_started = false;
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

  // Create OpenGL view
  const char *panel_text[3]={"","Configuring OpenGL ...",""};
  [window set_status_strings:panel_text];
  [window displayIfNeeded];
  gl_view = [window create_glview];
  
  // Start the frame presenter
  if (num_frames_known && (single_layer_idx < 0))
    animator->set_max_frames(num_frames);
  
  if (metanode.exists())
    overlay_restriction = jpx_metanode();
  double next_display_event_time = frame_presenter->enable(this);
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
  if (animation_bar != nil)
    { 
      update_animation_bar_state();
      update_animation_bar_pos();
    }
  else if (metanode.exists())
    { 
      metamation_bar = [window get_animation_bar:false];
      update_animation_bar_state();
      int num_meta_frames=0, max_id=0;
      if (animator->count_metadata_driven_frames(num_meta_frames))
        max_id = num_meta_frames-1;
      [metamation_bar set_frame:((reverse)?(num_meta_frames-1):0) max:max_id
                            start:-1.0 end:-1.0 track_duration:-1.0];    
    }
  return animator;
}

/*****************************************************************************/
/*                 kdms_renderer::update_animation_bar_pos                   */
/*****************************************************************************/

void kdms_renderer::update_animation_bar_pos()
{
  if (animation_bar)
    { 
      if (jpx_in.exists() && (single_layer_idx >= 0))
        [animation_bar set_frame:single_layer_idx
                             max:max_compositing_layer_idx
                           start:-1.0 end:-1.0 track_duration:-1.0];
      else
        { 
          double track_end = -1.0;
          if (!get_track_duration(track_end))
            track_end = -1.0;
          [animation_bar set_frame:frame_idx
                               max:(num_frames-1)
                             start:frame_start
                               end:frame_end
                    track_duration:track_end];
        }
    }
}

/*****************************************************************************/
/*                kdms_renderer::update_animation_bar_state                  */
/*****************************************************************************/

void kdms_renderer::update_animation_bar_state()
{
  kdms_animation_bar *bar = animation_bar;
  if ((bar == nil) && ((bar = metamation_bar) == nil))
    return;
  double custom_rate = animation_custom_rate;
  if (animation_uses_native_timing)
    custom_rate = -1.0;
  if (animator == NULL)
    [bar set_play_fwd:false rev:false repeat:animation_repeat
                  fps:custom_rate
           multiplier:animation_native_rate_multiplier];
  else
    { 
      bool repeat=animator->get_repeat(), reverse=animator->get_reverse();
      [bar set_play_fwd:!reverse rev:reverse repeat:repeat
                    fps:custom_rate
             multiplier:animation_native_rate_multiplier];
    }
}

/*****************************************************************************/
/*                     kdms_renderer::advance_animation                      */
/*****************************************************************************/
  
bool kdms_renderer::advance_animation(double cur_system_time,
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
          if ((jpip_client == NULL) ||
              !(jpip_client->is_alive(client_request_queue_id)))
            stop_animation();
          else
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
            { // Wait until the frame is presented before closing the animation
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
      view_dims_changed(); // Update view dimensions. We have to reset
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
/*                kdms_renderer::set_metadata_catalog_source                 */
/*****************************************************************************/

void kdms_renderer::set_metadata_catalog_source(kdms_catalog *source)
{
  if ((source == nil) && (this->catalog_source != nil))
    catalog_closed = true;
  if (this->catalog_source != source)
    {
      if (this->catalog_source != nil)
        {
          [this->catalog_source deactivate]; // Essential to do this first
          [this->catalog_source release];
        }
      if (source != nil)
        [source retain];
    }
  this->catalog_source = source;
  if ((source != nil) && jpx_in.exists())
    {
      catalog_closed = false;
      refresh_metadata(get_current_time());
    }
}

/*****************************************************************************/
/*                 kdms_renderer::reveal_metadata_at_point                   */
/*****************************************************************************/

void kdms_renderer::reveal_metadata_at_point(kdu_coords point, bool dbl_click)
{
  if ((compositor == NULL) || (!jpx_in) ||
      ((catalog_source == nil) && (metashow == nil) && !dbl_click))
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
      if (catalog_source != nil)
        { 
          cur_sel = [catalog_source get_selected_metanode];
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
                    dist_to_cur_sel=kdms_dist_to_common_ancestor(cur_sel,scan);
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
      if (metashow != nil)
        [metashow select_matching_metanode:node];
      if (catalog_source != nil)
        [catalog_source select_matching_metanode:node];
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
/*                  kdms_renderer::edit_metadata_at_point                    */
/*****************************************************************************/

void kdms_renderer::edit_metadata_at_point(kdu_coords *point_ref)
{
  if ((compositor == NULL) || !jpx_in)
    return;
  
  kdu_coords point; // Will hold mouse location in `view_dims' coordinates
  if (point_ref != NULL)
    point = *point_ref;
  else
    {
      NSRect ns_rect;
      ns_rect.origin = [window mouseLocationOutsideOfEventStream];
      ns_rect.origin = [doc_view convertPoint:ns_rect.origin fromView:nil];
      ns_rect.size.width = ns_rect.size.height = 0.0F;
      point = convert_region_from_doc_view(ns_rect).pos;
    }
  
  kdu_ilayer_ref ilyr;
  kdu_istream_ref istr;
  jpx_meta_manager manager = jpx_in.access_meta_manager();
  jpx_metanode launch_node;
  if (overlays_enabled)
    launch_node = compositor->search_overlays(point,istr,0.1F);
  kdms_matching_metalist *metalist = new kdms_matching_metalist;
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
    file_server = new kdms_file_services(open_file_pathname);
  if (editor == nil)
    {
      editor = [kdms_metadata_editor alloc];
      bool can_edit = (jpip_client == NULL) || !jpip_client->is_alive(-1);
      NSRect wnd_frame = [window frame];
      NSPoint preferred_location = wnd_frame.origin;
      preferred_location.x += wnd_frame.size.width + 10.0F;
      preferred_location.y += wnd_frame.size.height;
      [editor initWithSource:(&jpx_in)
                fileServices:file_server
                    editable:((can_edit)?YES:NO)
                    owner:this
                    location:preferred_location
               andIdentifier:window_identifier];
    }
  [editor configureWithEditList:metalist];
  [editor setActiveNode:launch_node];
}

/*****************************************************************************/
/*                      kdms_renderer::edit_metanode                         */
/*****************************************************************************/

void kdms_renderer::edit_metanode(jpx_metanode node,
                                  bool include_roi_or_numlist)
{
  if ((compositor == NULL) || (!jpx_in) || (!node) || node.is_deleted())
    return;
  kdms_matching_metalist *metalist = new kdms_matching_metalist;
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
          metalist = new kdms_matching_metalist;
          metalist->append_node(scan);
        }
    }
  
  if (file_server == NULL)
    file_server = new kdms_file_services(open_file_pathname);
  if (editor == nil)
    {
      editor = [kdms_metadata_editor alloc];
      bool can_edit = (jpip_client == NULL) || !jpip_client->is_alive(-1);
      NSRect wnd_frame = [window frame];
      NSPoint preferred_location = wnd_frame.origin;
      preferred_location.x += wnd_frame.size.width + 10.0F;
      preferred_location.y += wnd_frame.size.height;
      [editor initWithSource:(&jpx_in)
                fileServices:file_server
                    editable:((can_edit)?YES:NO)
                    owner:this
                    location:preferred_location
               andIdentifier:window_identifier];
    }
  [editor configureWithEditList:metalist];
  [editor setActiveNode:editor_startup_node];
}

/*****************************************************************************/
/*              kdms_renderer::highlight_imagery_for_metanode                */
/*****************************************************************************/

bool
  kdms_renderer::highlight_imagery_for_metanode(jpx_metanode node)
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
                                     1.0F,KDMS_OVERLAY_BORDER,node,0,
                                     overlay_params,KDMS_NUM_OVERLAY_PARAMS);
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
/*                kdms_renderer::show_imagery_for_metanode                   */
/*****************************************************************************/

bool
  kdms_renderer::show_imagery_for_metanode(jpx_metanode node,
                                           kdu_istream_ref *istream_ref)
{
  if (animator != NULL)
    return false; // Too many forces competing for control of current frame/ROI
  if (shape_istream.exists())
    { pending_show_metanode = jpx_metanode(); NSBeep(); return false; }
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
/*                    kdms_renderer::cancel_pending_show                     */
/*****************************************************************************/

void kdms_renderer::cancel_pending_show()
{
  pending_show_metanode = jpx_metanode();
}

/*****************************************************************************/
/*                     kdms_renderer::metadata_changed                       */
/*****************************************************************************/

void kdms_renderer::metadata_changed(bool new_data_only)
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
/*                   kdms_renderer::get_codestream_size                      */
/*****************************************************************************/

bool kdms_renderer::get_codestream_size(kdu_coords &size, int stream_idx)
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
/*                    kdms_renderer::set_shape_editor                        */
/*****************************************************************************/

bool
  kdms_renderer::set_shape_editor(jpx_roi_editor *ed,
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
    [doc_view setNeedsDisplay:YES];
  return (shape_editor != NULL);
}

/*****************************************************************************/
/*                   kdms_renderer::update_shape_editor                      */
/*****************************************************************************/

void
  kdms_renderer::update_shape_editor(kdu_dims region, bool hide_path_segments,
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
  [doc_view repaint_shape_region:region];
}

/*****************************************************************************/
/*               kdms_renderer::nudge_selected_shape_points                  */
/*****************************************************************************/

bool
  kdms_renderer::nudge_selected_shape_points(int delta_x, int delta_y,
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
/*                    kdms_renderer::get_shape_anchor                        */
/*****************************************************************************/

int kdms_renderer::get_shape_anchor(NSPoint &point, int which,
                                    bool sel_only, bool dragged)
{
  int result;
  kdu_coords pt;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !((result=shape_editor->get_anchor(pt,which,sel_only,dragged)) &&
        map_shape_point_to_doc_view(pt,point)))
    result = 0;
  return result;
}

/*****************************************************************************/
/*                     kdms_renderer::get_shape_edge                         */
/*****************************************************************************/

int kdms_renderer::get_shape_edge(NSPoint &from, NSPoint &to, int which,
                                  bool sel_only, bool dragged)
{
  int result;
  kdu_coords v1, v2;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !(result=shape_editor->get_edge(v1,v2,which,sel_only,dragged,true)))
    return 0;
  if (!(map_shape_point_to_doc_view(v1,from) &&
        map_shape_point_to_doc_view(v2,to)))
    return 0;
  int cs_idx; bool tran=false, vf=false, hf=false;
  compositor->get_istream_info(shape_istream,cs_idx,NULL,NULL,4,NULL,
                               NULL,NULL,&tran,&vf,&hf);
  if (orientation.trans ^ tran ^ orientation.vf ^ vf ^
      orientation.hf ^ hf)
    { NSPoint tmp=from; from=to; to=tmp; }
  return result;
}

/*****************************************************************************/
/*                    kdms_renderer::get_shape_curve                         */
/*****************************************************************************/

int kdms_renderer::get_shape_curve(NSPoint &centre, NSSize &extent,
                                   double &alpha, int which,
                                   bool sel_only, bool dragged)
{
  int result;
  kdu_coords ctr, ext, skew;
  if ((shape_editor == NULL) || (compositor == NULL) ||
      !(result=shape_editor->get_curve(ctr,ext,skew,which,sel_only,dragged)))
    return 0;
  kdu_coords tl=ctr-ext, br=ctr+ext;
  skew += ctr;
  NSPoint mapped_tl, mapped_br, mapped_skew;
  if (!(map_shape_point_to_doc_view(ctr,centre) &&
        map_shape_point_to_doc_view(tl,mapped_tl) &&
        map_shape_point_to_doc_view(br,mapped_br) &&
        map_shape_point_to_doc_view(skew,mapped_skew)))
    return 0;
  extent.width = 0.5F*(mapped_br.x - mapped_tl.x);
  extent.height = 0.5F*(mapped_br.y - mapped_tl.y);
  if (extent.width < 0.0)
    extent.width = -extent.width;
  bool vflip = true;
  if (extent.height < 0.0)
    {
      extent.height = -extent.height;
      vflip = false;
    }
  mapped_skew.x -= centre.x;
  mapped_skew.y -= centre.y;
  alpha = mapped_skew.x / extent.height;
  double mapped_gamma_sq = (mapped_skew.y/extent.width)*alpha;
  if (mapped_gamma_sq < 0.0)
    mapped_gamma_sq = -mapped_gamma_sq;
  if (mapped_gamma_sq >= 1.0)
    extent.width = 0.1;
  else if ((extent.width *= sqrt(1.0 - mapped_gamma_sq)) < 0.1)
    extent.width = 0.1;
  if (vflip)
    alpha = -alpha;
  return result;
}

/*****************************************************************************/
/*                     kdms_renderer::get_shape_path                         */
/*****************************************************************************/

int kdms_renderer::get_shape_path(NSPoint &from, NSPoint &to, int which)
{
  int result; kdu_coords v1, v2;
  if ((shape_editor == NULL) || hide_shape_path || (compositor == NULL) ||
      !(result=shape_editor->get_path_segment(v1,v2,which)))
    return 0;
  if (!(map_shape_point_to_doc_view(v1,from) &&
        map_shape_point_to_doc_view(v2,to)))
    return 0;
  return result;      
}

/*****************************************************************************/
/*                        kdms_renderer::save_over                           */
/*****************************************************************************/

bool kdms_renderer::save_over()
{
  const char *save_pathname =
    window_manager->get_save_file_pathname(open_file_pathname);
  const char *suffix = strrchr(open_filename,'.');
  
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
  } catch (kdu_exception) {
    window_manager->declare_save_file_invalid(save_pathname);
    return false;
  }
  return true;
}

/*****************************************************************************/
/*                       kdms_renderer::save_as_jp2                          */
/*****************************************************************************/

void kdms_renderer::save_as_jp2(const char *filename)
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
          assert(track_idx > 0);
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
/*                       kdms_renderer::save_as_jpx                          */
/*****************************************************************************/

void kdms_renderer::save_as_jpx(const char *filename,
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
        kdms_file *file;
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
/*                       kdms_renderer::save_as_raw                          */
/*****************************************************************************/

void kdms_renderer::save_as_raw(const char *filename)
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
/*                       File Menu Command Handlers                          */
/* ========================================================================= */

/*****************************************************************************/
/*                   kdms_renderer::menu_FileDisconnectURL                   */
/*****************************************************************************/

void kdms_renderer::menu_FileDisconnectURL()
{
  if ((jpip_client != NULL) && jpip_client->is_alive(client_request_queue_id))
    jpip_client->disconnect(false,30000,client_request_queue_id,false);
             // Give it a long timeout so as to avoid accidentally breaking
             // an HTTP request channel which might be shared with other
             // request queues -- but let's not wait around here.
}

/*****************************************************************************/
/*                   kdms_renderer::menu_FileReconnectURL                    */
/*****************************************************************************/

void kdms_renderer::menu_FileReconnectURL()
{
  if ((jpip_client == NULL) ||
      jpip_client->is_alive(client_request_queue_id))
    return;
  client_request_queue_id = window_manager->reconnect_jpip_client(jpip_client,
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
/*               kdms_renderer::note_jpip_client_reconnecting                */
/*****************************************************************************/

void kdms_renderer::note_jpip_client_reconnecting(kdu_client *client)
{ 
  if (client != this->jpip_client)
    return; // Does not apply to us
  client_request_queue_id = window_manager->reconnect_jpip_client(jpip_client,
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
/*                kdms_renderer::menu_FileEraseAndReconnectURL               */
/*****************************************************************************/

void kdms_renderer::menu_FileEraseAndReconnectURL()
{
  if ((jpip_client == NULL) || jpip_client->is_alive(-1) ||
      processing_call_to_open_file)
    return;
  if (window_manager->count_peers(jpip_client,window) > 0)
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
  int new_queue_id = window_manager->restart_jpip_client(jpip_client,window);
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
      [window set_progress_bar:0.0];
      if (metashow != nil)
        { 
          [metashow activateWithSource:&jp2_family_in
                           andFileName:open_filename];
          refresh_metadata(get_current_time());
        }
      open_file(NULL); // Continues the opening process
    }
}

/*****************************************************************************/
/*                   kdms_renderer::menu_FileJPIPSaveMode                    */
/*****************************************************************************/

void kdms_renderer::menu_FileJPIPSaveMode()
{ 
  bool can_change=false;
  bool will_save =
    window_manager->check_cache_save_mode(jpip_client,can_change);
  if (will_save || !can_change)
    return; // Nothing to do
  window_manager->set_cache_save_mode(jpip_client,true);
}

/*****************************************************************************/
/*                  kdms_renderer::can_do_FileJPIPSaveMode                   */
/*****************************************************************************/

bool kdms_renderer::can_do_FileJPIPSaveMode(NSMenuItem *item)
{ 
  bool can_change=false;
  bool will_save =
    window_manager->check_cache_save_mode(jpip_client,can_change);
  [item setState:(will_save)?NSOnState:NSOffState];
  return can_change;
}

/*****************************************************************************/
/*                  kdms_renderer::menu_FileJPIPUnsavedMode                  */
/*****************************************************************************/

void kdms_renderer::menu_FileJPIPUnsavedMode()
{
  bool can_change=false;
  bool will_save =
    window_manager->check_cache_save_mode(jpip_client,can_change);
  if (!(will_save && can_change))
    return; // Nothing to do
  if (jpip_client != NULL)
    { 
      int response =
        interrogate_user("You are about to convert your JPIP client to "
                         "PRIVATE mode, deleting any saved cache file, "
                         "even if it was pre-existing; are you sure?",
                         "Cancel","Go Private");
      if (!response)
        return;
    }
  window_manager->set_cache_save_mode(jpip_client,false);
}

/*****************************************************************************/
/*                kdms_renderer::can_do_FileJPIPUnsavedMode                  */
/*****************************************************************************/

bool kdms_renderer::can_do_FileJPIPUnsavedMode(NSMenuItem *item)
{ 
  bool can_change=false;
  bool will_save =
    window_manager->check_cache_save_mode(jpip_client,can_change);
  [item setState:(!will_save)?NSOnState:NSOffState];
  return can_change;
}

/*****************************************************************************/
/*                      kdms_renderer::menu_FileSave                         */
/*****************************************************************************/

void kdms_renderer::menu_FileSave()
{
  if ((compositor == NULL) || (animator != NULL) || mj2_in.exists() ||
      (!have_unsaved_edits) || (open_file_pathname == NULL))
    return;
  if (!save_without_asking)
    {
      int response =
        interrogate_user("You are about to save to the file which you are "
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
  save_over();
}

/*****************************************************************************/
/*                   kdms_renderer::menu_FileSaveReload                      */
/*****************************************************************************/

void kdms_renderer::menu_FileSaveReload()
{
  if (editor != nil)
    {
      if (interrogate_user("You have an open metadata editor, which will "
                           "be closed if you continue.  Do you still want "
                           "to save and reload the file?",
                           "Cancel","Continue") == 0)
        return;
      [editor close];
    }
  
  if (window_manager->get_open_file_retain_count(open_file_pathname) != 1)
    {
      interrogate_user("Cannot perform this operation at present.  There "
                       "are other open windows in the application which "
                       "are also using this same file -- you may not "
                       "overwrite and reload it until they are closed.",
                       "OK");
      return;
    }
  if (!save_over())
    return;
  NSString *filename = [NSString stringWithUTF8String:open_file_pathname];
  bool save_without_asking = this->save_without_asking;
  close_file();
  open_file([filename UTF8String]);
  this->save_without_asking = save_without_asking;
}

/*****************************************************************************/
/*                     kdms_renderer::menu_FileSaveAs                        */
/*****************************************************************************/

void kdms_renderer::menu_FileSaveAs(bool preserve_codestream_links,
                                    bool force_codestream_links)
{
  if ((compositor == NULL) || (animator != NULL))
    return;

  // Get the file name
  NSString *suggested_file = nil;
  const char *suffix = NULL;
  if (last_save_pathname != NULL)
    {
      suggested_file = [NSString stringWithUTF8String:last_save_pathname];
      suffix = strrchr(last_save_pathname,'.');
      suggested_file = [suggested_file lastPathComponent];
    }
  else
    {
      suggested_file = [NSString stringWithUTF8String:open_filename];
      suffix = strrchr(open_filename,'.');
      suggested_file =
        [suggested_file stringByRemovingPercentEncoding];
    }
  if (suggested_file == nil)
    suggested_file = @"";
  
  int s;
  char suggested_file_suffix[5] = {'\0','\0','\0','\0','\0'};
  if ((suffix != NULL) && (strlen(suffix) <= 4)) 
    for (s=0; (s < 4) && (suffix[s+1] != '\0'); s++)
      suggested_file_suffix[s] = (char) tolower(suffix[s+1]);
  
  NSArray *file_types = nil;
  if (jpx_in.exists())
    {
      if (force_codestream_links)
        file_types = [NSArray arrayWithObjects:@"jpx",@"jpf",nil];
      else
        file_types = [NSArray arrayWithObjects:
                      @"jp2",@"jpx",@"jpf",@"j2c",@"j2k",nil];
      if (have_unsaved_edits &&
          (strcmp(suggested_file_suffix,"jpx") != 0) &&
          (strcmp(suggested_file_suffix,"jpf") != 0))
        suggested_file = [[suggested_file stringByDeletingPathExtension]
                          stringByAppendingPathExtension:@"jpx"];
    }
  else if (mj2_in.exists())
    {
      file_types = [NSArray arrayWithObjects:@"jp2",@"j2c",@"j2k",nil];
      if (strcmp(suggested_file_suffix,"mj2") != 0)
        suggested_file = [[suggested_file stringByDeletingPathExtension]
                          stringByAppendingPathExtension:@"jp2"];
    }
  else
    {
      file_types = [NSArray arrayWithObjects:@"j2c",@"j2k",nil]; 
      if ((strcmp(suggested_file_suffix,"j2c") != 0) &&
          (strcmp(suggested_file_suffix,"j2k") != 0))
        suggested_file = [[suggested_file stringByDeletingPathExtension]
                          stringByAppendingPathExtension:@"j2c"];
    }
  
  NSSavePanel *panel = [NSSavePanel savePanel];
  [panel setTitle:@"Select filename for saving"];
  [panel setCanCreateDirectories:YES];
  [panel setCanSelectHiddenExtension:YES];
  [panel setExtensionHidden:NO];
  [panel setAllowedFileTypes:file_types];
  [panel setAllowsOtherFileTypes:NO];
  NSString *directory = nil;
  if (last_save_pathname != NULL)
    directory = [NSString stringWithUTF8String:last_save_pathname];
  else if (open_file_pathname != NULL)
    directory = [NSString stringWithUTF8String:open_file_pathname];
  if (directory != NULL)
    { 
      NSURL *directory_url = [NSURL fileURLWithPath:directory];
      directory_url = [directory_url URLByDeletingLastPathComponent];
      [panel setDirectoryURL:directory_url];
    }
  if (suggested_file != nil)
    [panel setNameFieldStringValue:suggested_file];
  
  if (([panel runModal] != NSModalResponseOK) ||
      ([panel URL] == nil))
    return;
  
  // Remember the last saved file name
  const char *chosen_pathname = [[[panel URL] path] UTF8String];
  suffix = strrchr(chosen_pathname,'.');
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
  try { // Protect the entire file writing process against exceptions
    bool may_overwrite_link_source = false;
    if (force_codestream_links && jpx_suffix && (open_file_pathname != NULL))
      if (kdms_compare_file_pathnames(chosen_pathname,open_file_pathname))
        may_overwrite_link_source = true;
    if (jpx_in.exists() && !jp2_family_in.uses_cache())
      {
        jp2_data_references data_refs;
        if (jpx_in.exists())
          data_refs = jpx_in.access_data_references();
        if (data_refs.exists())
          {
            int u;
            const char *url_pathname;
            for (u=0; (url_pathname = data_refs.get_file_url(u)) != NULL; u++)
              if (kdms_compare_file_pathnames(chosen_pathname,url_pathname))
                may_overwrite_link_source = true;
          }
      }
    if (may_overwrite_link_source)
      { kdu_error e; e << "You are about to overwrite an existing file "
        "which is being linked by (or potentially about to be linked by) "
        "the file you are saving.  This operation is too dangerous to "
        "attempt."; }
    save_pathname = window_manager->get_save_file_pathname(chosen_pathname);
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
      window_manager->declare_save_file_invalid(save_pathname);
  }
}

/*****************************************************************************/
/*                   kdms_renderer::menu_FileProperties                      */
/*****************************************************************************/

void kdms_renderer::menu_FileProperties() 
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
      interrogate_user("Codestream not yet available in JPIP cache.","OK");
      return;
    }
  compositor->get_istream_info(istr,c_idx);
  
  NSRect my_rect = [window frame];
  NSPoint top_left_point = my_rect.origin;
  top_left_point.x += my_rect.size.width * 0.1F;
  top_left_point.y += my_rect.size.height * 0.9F;
  
  kdms_properties *properties_dialog = [kdms_properties alloc];
  if (![properties_dialog initWithTopLeftPoint:top_left_point
                                the_codestream:codestream
                              codestream_index:c_idx])
    close_file();
  else
    [properties_dialog run_modal];
  [properties_dialog release];
}

/* ========================================================================= */
/*             View Menu Command Handlers (orientation and zoom)             */
/* ========================================================================= */

/*****************************************************************************/
/*                      kdms_renderer::menu_ViewVflip                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewVflip() 
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
/*                      kdms_renderer::menu_ViewHflip                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewHflip() 
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
/*                     kdms_renderer::menu_ViewRotate                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewRotate() 
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
/*                 kdms_renderer::menu_ViewCounterRotate                     */
/*****************************************************************************/

void kdms_renderer::menu_ViewCounterRotate() 
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
/*                     kdms_renderer::menu_ViewZoomIn                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewZoomIn() 
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
/*                     kdms_renderer::menu_ViewZoomOut                       */
/*****************************************************************************/

void kdms_renderer::menu_ViewZoomOut()
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == min_rendering_scale)
    return;
  calculate_rel_view_params();
  rendering_scale = decrease_scale(rendering_scale,false);
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                  kdms_renderer::menu_ViewZoomInSlightly                   */
/*****************************************************************************/

void kdms_renderer::menu_ViewZoomInSlightly() 
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
/*                  kdms_renderer::menu_ViewZoomOutSlightly                  */
/*****************************************************************************/

void kdms_renderer::menu_ViewZoomOutSlightly()
{
  if (working_buffer == NULL)
    return;
  if (rendering_scale == min_rendering_scale)
    return;
  calculate_rel_view_params();
  rendering_scale = decrease_scale(rendering_scale,true);
  initialize_regions(true);
  animation_status_invalid = true; // No harm if we are not doing animation
}

/*****************************************************************************/
/*                  kdms_renderer::menu_ViewOptimizeScale                    */
/*****************************************************************************/

void kdms_renderer::menu_ViewOptimizeScale()
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
/*                      kdms_renderer::menu_ViewWiden                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewWiden() 
{
  NSSize view_size = [[scroll_view contentView] frame].size;
  NSSize doc_size = [doc_view frame].size;
  NSSize expand; // This is how much we want to grow window size
  expand.width = doc_size.width - view_size.width;
  if (expand.width > view_size.width)
    expand.width = view_size.width;
  expand.height = doc_size.height - view_size.height;
  if (expand.height > view_size.height)
    expand.height = view_size.height;
  
  NSRect frame_rect = [window frame];
  NSRect screen_rect = [[NSScreen mainScreen] visibleFrame];
  
  calculate_rel_view_params();
  
  float min_x = screen_rect.origin.x;
  if (min_x > frame_rect.origin.x)
    min_x = frame_rect.origin.x;
  float lim_x = screen_rect.origin.x+screen_rect.size.width;
  if (lim_x < (frame_rect.origin.x+frame_rect.size.width))
    lim_x = frame_rect.origin.x+frame_rect.size.width;
  
  float min_y = screen_rect.origin.y;
  if (min_y > frame_rect.origin.y)
    min_y = frame_rect.origin.y;
  float lim_y = screen_rect.origin.y+screen_rect.size.height;
  if (lim_y < (frame_rect.origin.y+frame_rect.size.height))
    lim_y = frame_rect.origin.y+frame_rect.size.height;
  
  float add_to_width = screen_rect.size.width - frame_rect.size.width;
  if (add_to_width < 0.0F)
    add_to_width = 0.0F;
  else if (add_to_width > expand.width)
    add_to_width = expand.width;
  float add_to_height = screen_rect.size.height - frame_rect.size.height;
  if (add_to_height < 0.0F)
    add_to_height = 0.0F;
  else if (add_to_height > expand.height)
    add_to_height = expand.height;
  if ((add_to_width == 0.0F) && (add_to_height == 0.0))
    return;
  frame_rect.size.width += add_to_width;
  frame_rect.size.height += add_to_height;
  frame_rect.origin.x -= (float) floor(add_to_width*0.5);
  frame_rect.origin.y -= (float) floor(add_to_height*0.5);
  
  if (frame_rect.origin.x < min_x)
    frame_rect.origin.x = min_x;
  if (frame_rect.origin.y < min_y)
    frame_rect.origin.y = min_y;
  if ((frame_rect.origin.x+frame_rect.size.width) > lim_x)
    frame_rect.origin.x = lim_x - frame_rect.size.width;
  if ((frame_rect.origin.y+frame_rect.size.height) > lim_y)
    frame_rect.origin.y = lim_y - frame_rect.size.height;
  
  [window setFrame:frame_rect display:YES];
  scroll_to_view_anchors();
  view_centre_known = false;
}

/*****************************************************************************/
/*                     kdms_renderer::menu_ViewShrink                        */
/*****************************************************************************/

void kdms_renderer::menu_ViewShrink() 
{
  NSSize view_size = [[scroll_view contentView] frame].size;
  NSSize new_view_size = view_size;
  new_view_size.width *= 0.5F;
  new_view_size.height *= 0.5F;
  if (new_view_size.width < 50.0F)
    new_view_size.width = 50.0F;
  if (new_view_size.height < 30.0F)
    new_view_size.height = 30.0F;

  NSSize half_reduce;
  half_reduce.width = floor((view_size.width - new_view_size.width) * 0.5F);
  half_reduce.height = floor((view_size.height - new_view_size.height) * 0.5F);
  if (half_reduce.width < 0.0F)
    half_reduce.width = 0.0F;
  if (half_reduce.height < 0.0F)
    half_reduce.height = 0.0F;
  
  calculate_rel_view_params();
  rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
  NSRect frame_rect = [window frame];
  frame_rect.size.width -= 2.0F*half_reduce.width;
  frame_rect.size.height -= 2.0F*half_reduce.height;  
  frame_rect.origin.x += half_reduce.width;
  frame_rect.origin.y += half_reduce.height;
  
  NSSize min_size = [window minSize];
  if (frame_rect.size.height < min_size.height)
    frame_rect.size.height = min_size.height;
  if (frame_rect.size.width < min_size.width)
    frame_rect.size.width = min_size.width;
  
  kdms_rel_focus rel_focus_save = rel_focus; // Otherwise the focus box
    // may be pushed around by the change in frame dimensions before we
    // get a chance to scroll back it back into view
  [window setFrame:frame_rect display:YES];
  rel_focus = rel_focus_save;
  scroll_to_view_anchors();
  view_centre_known = false;
}

/*****************************************************************************/
/*                     kdms_renderer::menu_ViewRestore                       */
/*****************************************************************************/

void kdms_renderer::menu_ViewRestore() 
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
/*                     kdms_renderer::menu_ViewRefresh                       */
/*****************************************************************************/

void kdms_renderer::menu_ViewRefresh()
{
  refresh_display();
}

/*****************************************************************************/
/*                    kdms_renderer::menu_ViewLayersLess                     */
/*****************************************************************************/

void kdms_renderer::menu_ViewLayersLess()
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
/*                    kdms_renderer::menu_ViewLayersMore                     */
/*****************************************************************************/

void kdms_renderer::menu_ViewLayersMore()
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

/*****************************************************************************/
/*                   kdms_renderer::menu_ViewPixelScaleX1                    */
/*****************************************************************************/

void kdms_renderer::menu_ViewPixelScaleX1()
{
  if (pixel_scale != 1)
    { 
      pixel_scale = 1;
      if (quality_limiting_mode)
        configure_quality_limiting();
      size_window_for_image_dims(false);
      invalidate_configuration();
      initialize_regions(false);
      animation_status_invalid = true; // No harm if we are not doing animation
    }
}

/*****************************************************************************/
/*                   kdms_renderer::menu_ViewPixelScaleX2                    */
/*****************************************************************************/

void kdms_renderer::menu_ViewPixelScaleX2()
{
  if (pixel_scale != 2)
    { 
      pixel_scale = 2;
      if (quality_limiting_mode)
        configure_quality_limiting();
      calculate_rel_view_params();
      rel_focus.get_centre_if_valid(view_centre_x,view_centre_y);
      size_window_for_image_dims(true);
      invalidate_configuration();
      initialize_regions(false);
      animation_status_invalid = true; // No harm if we are not doing animation
    }
}

/*****************************************************************************/
/*                       kdms_renderer::menu_FocusOff                        */
/*****************************************************************************/

void kdms_renderer::menu_FocusOff()
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
/*                  kdms_renderer::menu_FocusHighlighting                    */
/*****************************************************************************/

void kdms_renderer::menu_FocusHighlighting()
{
  highlight_focus = !highlight_focus;
  if (rel_focus.is_valid() && (compositor != NULL))
    { 
      if (animator == NULL)
        [doc_view setNeedsDisplay:YES];
      compositor->force_display_redraw();
    }
}

/*****************************************************************************/
/*                      kdms_renderer::menu_FocusWiden                       */
/*****************************************************************************/

void kdms_renderer::menu_FocusWiden()
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
/*                     kdms_renderer::menu_FocusShrink                       */
/*****************************************************************************/

void kdms_renderer::menu_FocusShrink()
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
/*                      kdms_renderer::menu_FocusLeft                        */
/*****************************************************************************/

void kdms_renderer::menu_FocusLeft()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { NSBeep(); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.x -= 20;
  if (new_box.pos.x < view_dims.pos.x)
    {
      if (new_box.pos.x < image_dims.pos.x)
        new_box.pos.x = image_dims.pos.x;
      if (new_box.pos.x < view_dims.pos.x)
        scroll_absolute(new_box.pos.x-view_dims.pos.x,0,false);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                      kdms_renderer::menu_FocusRight                       */
/*****************************************************************************/

void kdms_renderer::menu_FocusRight()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { NSBeep(); return; }
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
        scroll_absolute(new_lim-view_lim,0,false);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                       kdms_renderer::menu_FocusUp                         */
/*****************************************************************************/

void kdms_renderer::menu_FocusUp()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { NSBeep(); return; }
  kdu_dims new_box = find_focus_box();
  rel_focus.reset();
  new_box.pos.y -= 20;
  if (new_box.pos.y < view_dims.pos.y)
    {
      if (new_box.pos.y < image_dims.pos.y)
        new_box.pos.y = image_dims.pos.y;
      if (new_box.pos.y < view_dims.pos.y)
        scroll_absolute(0,new_box.pos.y-view_dims.pos.y,false);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                      kdms_renderer::menu_FocusDown                        */
/*****************************************************************************/

void kdms_renderer::menu_FocusDown()
{
  if (shape_istream.exists() || !rel_focus.is_valid())
    return;
  if (rel_focus.animator_driven)
    { NSBeep(); return; }
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
        scroll_absolute(0,new_lim-view_lim,false);
    }
  set_focus_box(new_box,jpx_metanode());  
}

/*****************************************************************************/
/*                       kdms_renderer::menu_ModeFast                        */
/*****************************************************************************/

void kdms_renderer::menu_ModeFast()
{
  error_level = 0;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                       kdms_renderer::menu_ModeFussy                       */
/*****************************************************************************/

void kdms_renderer::menu_ModeFussy()
{
  error_level = 1;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                    kdms_renderer::menu_ModeResilient                      */
/*****************************************************************************/

void kdms_renderer::menu_ModeResilient()
{
  error_level = 2;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                   kdms_renderer::menu_ModeResilientSOP                    */
/*****************************************************************************/

void kdms_renderer::menu_ModeResilientSOP()
{
  error_level = 3;
  if (compositor != NULL)
    compositor->set_error_level(error_level);
}

/*****************************************************************************/
/*                  kdms_renderer::menu_ModeSingleThreaded                   */
/*****************************************************************************/

void kdms_renderer::menu_ModeSingleThreaded()
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
  set_compositor_process_increment();
}

/*****************************************************************************/
/*                  kdms_renderer::menu_ModeMultiThreaded                    */
/*****************************************************************************/

void kdms_renderer::menu_ModeMultiThreaded()
{
  if (thread_env.exists())
    return;
  num_threads = 1;
  set_compositor_process_increment();
  thread_env.create();
  if (compositor != NULL)
    {
      compositor->halt_processing();
      compositor->set_thread_env(&thread_env,NULL);
    }
}

/*****************************************************************************/
/*                 kdms_renderer::can_do_ModeMultiThreaded                   */
/*****************************************************************************/

bool kdms_renderer::can_do_ModeMultiThreaded(NSMenuItem *item)
{
  const char *menu_string = "Multi-Threaded";
  char menu_string_buf[80];
  if (num_threads > 0)
    {
      sprintf(menu_string_buf,"Multi-Threaded (%d threads)",num_threads);
      menu_string = menu_string_buf;
    }
  NSString *ns_string = [NSString stringWithUTF8String:menu_string];
  [item setTitle:ns_string];
  [item setState:((num_threads > 0)?NSOnState:NSOffState)];
  return true;
}

/*****************************************************************************/
/*                   kdms_renderer::menu_ModeMoreThreads                     */
/*****************************************************************************/

void kdms_renderer::menu_ModeMoreThreads()
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
  set_compositor_process_increment();
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);
}

/*****************************************************************************/
/*                   kdms_renderer::menu_ModeLessThreads                     */
/*****************************************************************************/

void kdms_renderer::menu_ModeLessThreads()
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
  set_compositor_process_increment();
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);  
}

/*****************************************************************************/
/*                kdms_renderer::menu_ModeRecommendedThreads                 */
/*****************************************************************************/

void kdms_renderer::menu_ModeRecommendedThreads()
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
  set_compositor_process_increment();
  if (compositor != NULL)
    compositor->set_thread_env(&thread_env,NULL);
}
  
/*****************************************************************************/
/*              kdms_renderer::can_do_ModeRecommendedThreaded                */
/*****************************************************************************/

bool kdms_renderer::can_do_ModeRecommendedThreads(NSMenuItem *item)
{
  const char *menu_string = "Recommended: single threaded";
  char menu_string_buf[80];
  if (num_recommended_threads > 0)
    {
      sprintf(menu_string_buf,"Recommended: %d threads",
              num_recommended_threads);
      menu_string = menu_string_buf;
    }
  NSString *ns_string = [NSString stringWithUTF8String:menu_string];
  [item setTitle:ns_string];
  [item setState:((num_threads > 0)?NSOnState:NSOffState)];
  return true;
}

/*****************************************************************************/
/*                    kdms_renderer::menu_NavComponent1                      */
/*****************************************************************************/

void kdms_renderer::menu_NavComponent1() 
{
  if ((compositor == NULL) || (single_component_idx == 0) ||
      (animator != NULL))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                  kdms_renderer::menu_NavComponentNext                     */
/*****************************************************************************/

void kdms_renderer::menu_NavComponentNext() 
{
  if ((compositor == NULL) || (animator != NULL) ||
      ((max_components >= 0) && (single_component_idx >= (max_components-1))))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                  kdms_renderer::menu_NavComponentPrev                     */
/*****************************************************************************/

void kdms_renderer::menu_NavComponentPrev() 
{
  if ((compositor == NULL) || (single_component_idx == 0))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                 kds_renderer::menu_NavCompositingLayer                    */
/*****************************************************************************/

void kdms_renderer::menu_NavCompositingLayer() 
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
/*                  kdms_renderer::menu_NavMultiComponent                    */
/*****************************************************************************/

void kdms_renderer::menu_NavMultiComponent()
{
  if ((compositor == NULL) || (animator != NULL) ||
      ((single_component_idx < 0) &&
       ((single_layer_idx < 0) || ((num_frames == 0) && num_frames_known))))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
  if (num_frames_known && (num_frames == 0))
    menu_NavCompositingLayer();
  else
    {
      calculate_rel_view_params();
      single_component_idx = -1;
      single_layer_idx = -1;
      frame_idx = 0;
      frame_start = frame_end = 0.0;
      frame = jpx_frame(); // Let `initialize_regions' try to find it.
    }
  invalidate_configuration();
  initialize_regions(false);  
}
  
/*****************************************************************************/
/*                    kdms_renderer::menu_NavImageNext                       */
/*****************************************************************************/

void kdms_renderer::menu_NavImageNext() 
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                   kdms_renderer::can_do_NavImageNext                      */
/*****************************************************************************/

bool kdms_renderer::can_do_NavImageNext(NSMenuItem *item)
{
  if (animator != NULL)
    return false;
  else if (single_component_idx >= 0)
    return (compositor != NULL) &&
           ((max_codestreams < 0) ||
            (single_codestream_idx < (max_codestreams-1)));
  else if (single_layer_idx >= 0)
    return (compositor != NULL) &&
           ((max_compositing_layer_idx < 0) ||
            (single_layer_idx < max_compositing_layer_idx));
  else
    return ((compositor != NULL) &&
            ((!num_frames_known) || (frame_idx < (num_frames-1))) &&
            ((!jpx_in.exists()) || frame.exists()));
}

/*****************************************************************************/
/*                    kdms_renderer::menu_NavImagePrev                       */
/*****************************************************************************/

void kdms_renderer::menu_NavImagePrev() 
{
  if ((compositor == NULL) || (animator != NULL))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                   kdms_renderer::can_do_NavImagePrev                      */
/*****************************************************************************/

bool kdms_renderer::can_do_NavImagePrev(NSMenuItem *item)
{
  if (animator != NULL)
    return false;
  else if (single_component_idx >= 0)
    return (compositor != NULL) && (single_codestream_idx > 0);
  else if (single_layer_idx >= 0)
    return (compositor != NULL) && (single_layer_idx > 0);
  else
    return (compositor != NULL) && (frame_idx > 0);
}

/*****************************************************************************/
/*                    kdms_renderer::menu_NavTrackNext                       */
/*****************************************************************************/

void kdms_renderer::menu_NavTrackNext()
{
  if ((compositor == NULL) || ((max_track_idx <= 1) && num_tracks_known))
    return;
  if (shape_istream.exists())
    { NSBeep(); return; }
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

/*****************************************************************************/
/*                   kdms_renderer::menu_MetadataCatalog                     */
/*****************************************************************************/

void kdms_renderer::menu_MetadataCatalog()
{
  if ((!catalog_source) && jpx_in.exists())
    [window create_metadata_catalog];
  else if (catalog_source)
    [window remove_metadata_catalog];
  if (catalog_source != nil)
    { 
      [window makeFirstResponder:catalog_source];
      refresh_metadata(get_current_time());
    }
}

/*****************************************************************************/
/*                  kdms_renderer::menu_MetadataSwapFocus                    */
/*****************************************************************************/

void kdms_renderer::menu_MetadataSwapFocus()
{
  if ((!catalog_source) && jpx_in.exists())
    { 
      [window create_metadata_catalog];
      refresh_metadata(get_current_time());
    }
  else if (catalog_source)
    {
      if ([doc_view is_first_responder])
        {
          [window makeFirstResponder:catalog_source];
          refresh_metadata(get_current_time());
        }
      else
        [window makeFirstResponder:doc_view];
    }
}

/*****************************************************************************/
/*                     kdms_renderer::menu_MetadataShow                      */
/*****************************************************************************/

void kdms_renderer::menu_MetadataShow()
{
  if (metashow != nil)
    [metashow makeKeyAndOrderFront:window];
  else
    {
      NSRect wnd_frame = [window frame];
      NSPoint preferred_location = wnd_frame.origin;
      preferred_location.y -= 10.0F;
      metashow = [kdms_metashow alloc];
      [metashow initWithOwner:this
                     location:preferred_location
                andIdentifier:window_identifier];
      if (jp2_family_in.exists())
        { 
          [metashow activateWithSource:&jp2_family_in
                           andFileName:open_filename];
          refresh_metadata(get_current_time());
        }
    }
}

/*****************************************************************************/
/*                    kdms_renderer::menu_MetadataAdd                        */
/*****************************************************************************/

void kdms_renderer::menu_MetadataAdd()
{
  if ((compositor == NULL) || (!jpx_in) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  if ((editor != nil) && (shape_editor != NULL))
    { 
      [editor addROIRegion:nil];
      return;
    }
  if (focus_metanode.exists())
    set_focus_box(kdu_dims(),jpx_metanode()); // Don't let user get confused by
            // a focus box that is derived from existing metadata, as opposed
            // to an interactively specified region of interest.
  
  if (file_server == NULL)
    file_server = new kdms_file_services(open_file_pathname);
  
  jpx_meta_manager meta_manager = jpx_in.access_meta_manager();
  jpx_metanode parent;
  if (catalog_source != nil)
    parent = [catalog_source get_selected_metanode];

  kdu_dims initial_region = find_focus_box();
  kdu_istream_ref istr; // Initialized if there is a focus box
  if (rel_focus.is_valid() &&
      (initial_region != view_dims) && !initial_region.is_empty())
    istr = compositor->map_region(initial_region,kdu_istream_ref());
  else
    initial_region = kdu_dims();

  kdu_ilayer_ref ilyr; // Initialized based on mouse pos if no focus box
  if (!istr.exists())
    { 
      NSRect ns_rect;
      ns_rect.origin = [window mouseLocationOutsideOfEventStream];
      ns_rect.origin = [doc_view convertPoint:ns_rect.origin fromView:nil];
      ns_rect.size.width = ns_rect.size.height = 0.0F;
      kdu_coords point = convert_region_from_doc_view(ns_rect).pos;
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
        user_response =
          interrogate_user("You are about to add new metadata to describe "
                           "a region of interest (based initially on the "
                           "focus box).  Would you like to add the "
                           "description as a descendant of the item that is "
                           "currently selected in the metadata side-bar, or "
                           "as a top-level region of interest?",
                           "Top-level","Child of selected item","Cancel");
      else
        user_response =
          interrogate_user("You are about to add new metadata to reference "
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
  kdms_matching_metalist *metalist = new kdms_matching_metalist;
  metalist->append_node(new_node);
  if (editor == nil)
    { 
      NSRect wnd_frame = [window frame];
      NSPoint preferred_location = wnd_frame.origin;
      preferred_location.x += wnd_frame.size.width + 10.0F;
      preferred_location.y += wnd_frame.size.height;
      editor = [kdms_metadata_editor alloc];
      [editor initWithSource:(&jpx_in)
                fileServices:file_server
                    editable:YES
                       owner:this
                    location:preferred_location
               andIdentifier:window_identifier];
    }
  [editor configureWithEditList:metalist];
  [editor setActiveNode:new_node];
}

/*****************************************************************************/
/*                    kdms_renderer::menu_MetadataEdit                       */
/*****************************************************************************/

void kdms_renderer::menu_MetadataEdit()
{
  if ((compositor == NULL) || !jpx_in.exists())
    return;
  if ((catalog_source != nil) && ![doc_view is_first_responder])
    {
      jpx_metanode metanode = [catalog_source get_unique_selected_metanode];
      if (metanode.exists())
        edit_metanode(metanode,true);
      else
        NSBeep();
    }
  else
    edit_metadata_at_point(NULL);
}

/*****************************************************************************/
/*                    kdms_renderer::menu_MetadataDelete                     */
/*****************************************************************************/

void kdms_renderer::menu_MetadataDelete()
{
  if ((shape_editor != NULL) && (editor != nil))
    {
      [editor deleteROIRegion:nil];
      return;
    }
  if ((catalog_source == nil) || (editor != nil) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)) ||
      [doc_view is_first_responder])
    return;
  jpx_metanode metanode = [catalog_source get_unique_selected_metanode];
  if (!metanode)
    {
      NSBeep();
      return;
    }
  if (interrogate_user("Are you sure you want to delete this label/link from "
                       "the file's metadata?  This operation cannot be "
                       "undone.","Delete","Cancel") == 1)
    return;
  jpx_metanode parent = metanode.get_parent();
  if (parent.exists() && (parent.get_num_regions() > 0))
    {
      int num_siblings=0;
      parent.count_descendants(num_siblings);
      if ((num_siblings == 1) &&
          (interrogate_user("The node you are deleting belongs to a region "
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
    [catalog_source select_matching_metanode:parent];
}

/*****************************************************************************/
/*                   kdms_renderer::can_do_MetadataDelete                    */
/*****************************************************************************/

bool kdms_renderer::can_do_MetadataDelete(NSMenuItem *item)
{
  if (shape_editor != NULL)
    return true;
  else
    return ((catalog_source != nil) && (editor == nil) &&
            ((jpip_client == NULL) || !jpip_client->is_alive(-1)) &&
            [catalog_source get_unique_selected_metanode].exists() &&
            ![doc_view is_first_responder]);
}

/*****************************************************************************/
/*                  kdms_renderer::menu_MetadataCopyLabel                    */
/*****************************************************************************/

void kdms_renderer::menu_MetadataCopyLabel()
{
  if ((catalog_source == nil) || [doc_view is_first_responder])
    return;
  jpx_metanode node = [catalog_source get_selected_metanode];
  const char *label = NULL;
  jpx_metanode_link_type link_type;
  if (node.exists() &&
      ((label = node.get_label()) == NULL) &&
      (node = node.get_link(link_type)).exists())
    label = node.get_label();
  if (label == NULL)
    NSBeep();
  else
    [catalog_source paste_label:label];
}

/*****************************************************************************/
/*                 kdms_renderer::can_do_MetadataCopyLabel                   */
/*****************************************************************************/

bool kdms_renderer::can_do_MetadataCopyLabel(NSMenuItem *item)
{
  return ((catalog_source != nil) && ![doc_view is_first_responder]);
}

/*****************************************************************************/
/*                  kdms_renderer::menu_MetadataCopyLink                     */
/*****************************************************************************/

void kdms_renderer::menu_MetadataCopyLink()
{
  if ((compositor == NULL) || !jpx_in)
    return;
  if ((catalog_source != nil) && ![doc_view is_first_responder])
    { // Link to something selected within the catalog
      jpx_metanode node = [catalog_source get_selected_metanode];
      if (node.exists())
        [catalog_source paste_link:node];
    }
  else
    { // Link to an image feature.
      NSRect ns_rect;
      ns_rect.origin = [window mouseLocationOutsideOfEventStream];
      ns_rect.origin = [doc_view convertPoint:ns_rect.origin fromView:nil];
      ns_rect.size.width = ns_rect.size.height = 0.0F;
      kdu_coords point = convert_region_from_doc_view(ns_rect).pos;
      kdu_istream_ref istr;
      jpx_metanode roi_node =
        compositor->search_overlays(point,istr,0.1F);
      if (roi_node.exists())
        [catalog_source paste_link:roi_node];
    }
}

/*****************************************************************************/
/*                     kdms_renderer::menu_MetadataCut                       */
/*****************************************************************************/

void kdms_renderer::menu_MetadataCut()
{
  if ((compositor == NULL) || !jpx_in)
    return;
  if ((catalog_source != nil) && ![doc_view is_first_responder])
    { // Cut something selected within the catalog
      jpx_metanode node = [catalog_source get_unique_selected_metanode];
      if (node.exists())
        [catalog_source paste_node_to_cut:node];
    }
  else
    { // Cut something identified within the image view
      NSRect ns_rect;
      ns_rect.origin = [window mouseLocationOutsideOfEventStream];
      ns_rect.origin = [doc_view convertPoint:ns_rect.origin fromView:nil];
      ns_rect.size.width = ns_rect.size.height = 0.0F;
      kdu_coords point = convert_region_from_doc_view(ns_rect).pos;
      kdu_istream_ref istr;
      jpx_metanode roi_node =
      compositor->search_overlays(point,istr,0.1F);
      if (roi_node.exists())
        [catalog_source paste_node_to_cut:roi_node];
    }
}

/*****************************************************************************/
/*                  kdms_renderer::menu_MetadataPasteNew                     */
/*****************************************************************************/

void kdms_renderer::menu_MetadataPasteNew()
{
  if ((compositor == NULL) || (catalog_source == nil) || (editor != nil) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  jpx_metanode parent = [catalog_source get_unique_selected_metanode];
  if (!parent.exists())
    {
      NSBeep();
      return;
    }
  const char *label = [catalog_source get_pasted_label];
  jpx_metanode link = [catalog_source get_pasted_link];
  jpx_metanode node_to_cut = [catalog_source get_pasted_node_to_cut];
  
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
              NSBeep();
              return;
            }
          link_type = JPX_ALTERNATE_CHILD_LINK;
        }
      else
        { 
          user_response =
            interrogate_user("What type of link would you like to add?\n"
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
          user_response =
            interrogate_user("Would you like to create an \"Alternate "
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
          user_response =
            interrogate_user("Would you like to create an \"Alternate "
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
          user_response =
            interrogate_user("Would you like to create an \"Alternate "
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
        [catalog_source select_matching_metanode:new_node];
      }
  }
  catch (kdu_exception)
  {}
}

/*****************************************************************************/
/*                 kdms_renderer::can_do_MetadataPasteNew                    */
/*****************************************************************************/

bool kdms_renderer::can_do_MetadataPasteNew(NSMenuItem *item)
{
  return ((catalog_source != nil) && (editor == nil) &&
          ((jpip_client == NULL) || !jpip_client->is_alive(-1)) &&
          (([catalog_source get_pasted_label] != NULL) ||
           [catalog_source get_pasted_link].exists() ||
           [catalog_source get_pasted_node_to_cut].exists()));
}

/*****************************************************************************/
/*                  kdms_renderer::menu_MetadataDuplicate                    */
/*****************************************************************************/

void kdms_renderer::menu_MetadataDuplicate()
{
  if ((compositor == NULL) || (catalog_source == nil) || (editor != nil) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return;
  const char *src_label = NULL;
  jpx_metanode parent, src = [catalog_source get_unique_selected_metanode];
  if ((!(src.exists() && (parent=src.get_parent()).exists())) ||
      ((src_label=src.get_label()) == NULL))
    { 
      NSBeep();
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
      [catalog_source select_matching_metanode:new_node];
    }
}

/*****************************************************************************/
/*                 kdms_renderer::can_do_MetadataDuplicate                   */
/*****************************************************************************/

bool kdms_renderer::can_do_MetadataDuplicate(NSMenuItem *item)
{
  if ((catalog_source == nil) || (editor != nil) ||
      ((jpip_client != NULL) && jpip_client->is_alive(-1)))
    return false;
  jpx_metanode node = [catalog_source get_unique_selected_metanode];
  return (node.exists() && (node.get_label() != NULL));
}

/*****************************************************************************/
/*                    kdms_renderer::menu_MetadataUndo                       */
/*****************************************************************************/

void kdms_renderer::menu_MetadataUndo()
{
  if (editor != nil)
    [editor undoROIEdit:nil];
}

/*****************************************************************************/
/*                    kdms_renderer::menu_MetadataRedo                       */
/*****************************************************************************/

void kdms_renderer::menu_MetadataRedo()
{
  if (editor != nil)
    [editor redoROIEdit:nil];
}

/*****************************************************************************/
/*                   kdms_renderer::menu_OverlayEnable                       */
/*****************************************************************************/

void kdms_renderer::menu_OverlayEnable() 
{
  if ((compositor == NULL) || !jpx_in)
    return;
  overlays_enabled = !overlays_enabled;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kdms_renderer::menu_OverlayFlashing                     */
/*****************************************************************************/

void kdms_renderer::menu_OverlayFlashing()
{
  if ((compositor == NULL) || (!jpx_in) || !overlays_enabled)
    return;
  overlay_flashing_state = (overlay_flashing_state)?0:1;
  overlays_auto_flash = false;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kdms_renderer::menu_OverlayAutoFlash                    */
/*****************************************************************************/

void kdms_renderer::menu_OverlayAutoFlash()
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
/*                   kdms_renderer::menu_OverlayToggle                       */
/*****************************************************************************/

void kdms_renderer::menu_OverlayToggle()
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
/*                  kdms_renderer::menu_OverlayRestrict                      */
/*****************************************************************************/

void kdms_renderer::menu_OverlayRestrict()
{
  if (animation_metanode.exists())
    return; // Have to let the `animator' drive overlay restrictions
      
  jpx_metanode new_restriction;
  if ((catalog_source != nil) && ![doc_view is_first_responder])
    {
      jpx_metanode metanode = [catalog_source get_selected_metanode];
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
/*                 kdms_renderer::can_do_OverlayRestrict                     */
/*****************************************************************************/

bool kdms_renderer::can_do_OverlayRestrict(NSMenuItem *item)
{
  [item setState:(overlay_restriction.exists())?NSOnState:NSOffState];
  return (((animator == NULL) || !animation_metanode) &&
          (overlay_restriction.exists() ||
           ((catalog_source != nil) && ![doc_view is_first_responder])));
}

/*****************************************************************************/
/*                  kdms_renderer::menu_OverlayLighter                       */
/*****************************************************************************/

void kdms_renderer::menu_OverlayLighter()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled))
    return;
  overlay_nominal_intensity *= 1.0F/1.5F;
  if (overlay_nominal_intensity < 0.25F)
    overlay_nominal_intensity = 0.25F;  
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                   kdms_renderer::menu_OverlayHeavier                      */
/*****************************************************************************/

void kdms_renderer::menu_OverlayHeavier()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled))
    return;
  overlay_nominal_intensity *= 1.5F;
  if (overlay_nominal_intensity > 4.0F)
    overlay_nominal_intensity = 4.0F;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                 kdms_renderer::menu_OverlayDoubleSize                     */
/*****************************************************************************/

void kdms_renderer::menu_OverlayDoubleSize()
{
  if ((compositor == NULL) || (!jpx_in) || (!overlays_enabled) ||
      (overlay_size_threshold & 0x40000000))
    return;
  overlay_size_threshold <<= 1;
  configure_overlays(-1.0,false);
}

/*****************************************************************************/
/*                  kdms_renderer::menu_OverlayHalveSize                     */
/*****************************************************************************/

void kdms_renderer::menu_OverlayHalveSize()
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
/*                     kdms_renderer::menu_PlayNative                        */
/*****************************************************************************/

void kdms_renderer::menu_PlayNative()
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                    kdms_renderer::can_do_PlayNative                       */
/*****************************************************************************/

bool kdms_renderer::can_do_PlayNative(NSMenuItem *item)
{
  if (!source_supports_animation())
    return false;
  const char *menu_string = "Native play rate";
  char menu_string_buf[80];
  if ((animation_native_rate_multiplier > 1.01F) ||
      (animation_native_rate_multiplier < 0.99F))
    {
      sprintf(menu_string_buf,"Native playback rate x %5.2g",
              animation_native_rate_multiplier);
      menu_string = menu_string_buf;
    }
  NSString *ns_string = [NSString stringWithUTF8String:menu_string];
  [item setTitle:ns_string];
  [item setState:((animation_uses_native_timing)?NSOnState:NSOffState)];
  return true;
}

/*****************************************************************************/
/*                    kdms_renderer::menu_PlayCustom                         */
/*****************************************************************************/

void kdms_renderer::menu_PlayCustom()
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                    kdms_renderer::can_do_PlayCustom                       */
/*****************************************************************************/

bool kdms_renderer::can_do_PlayCustom(NSMenuItem *item)
{
  if (!source_supports_animation())
    return false;
  char menu_string_buf[80];
  sprintf(menu_string_buf,"Custom frame rate = %5.2g fps",
          animation_custom_rate);
  NSString *ns_string = [NSString stringWithUTF8String:menu_string_buf];
  [item setTitle:ns_string];
  [item setState:((animation_uses_native_timing)?NSOffState:NSOnState)];
  return true;
}

/*****************************************************************************/
/*                   kdms_renderer::menu_PlayFrameRateUp                     */
/*****************************************************************************/

void kdms_renderer::menu_PlayFrameRateUp()
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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
/*                kdms_renderer::menu_PlayFrameRateDown                      */
/*****************************************************************************/

void kdms_renderer::menu_PlayFrameRateDown()
{
  if (shape_istream.exists())
    { NSBeep(); return; }
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

/*****************************************************************************/
/*                   kdms_renderer::menu_StatusToggle                        */
/*****************************************************************************/

void kdms_renderer::menu_StatusToggle() 
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
