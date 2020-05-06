/*****************************************************************************/
// File: vcom_compress_jpx.cpp [scope = APPS/VCOM_FAST]
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
   Implements the `vcom_initialize_jpx_target' function that allows
"kdu_vcom_fast" to write video directly to a JPX file.  By and large, doing
this is trivial, except for the process of creating the initial file structure
and setting up a JPX container to describe the content, along with associated
auxiliary metadata.  To avoid distracting clutter in the main
"kdu_vcom_fast.cpp" file, we put these steps into this separate source file.
   Note that the contents of this file and the definitions of the associated
objects, `kdv_jpx_labels' and `kdv_jpx_target', are direct copies of the
corresponding file and definitions found with the "kdu_v_compress" demo app,
at least for the moment.
******************************************************************************/

#include "kdu_vcom.h"
using namespace kdu_supp_vcom;

/* ========================================================================= */
/*                             Internal Functions                            */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                      copy_jpx_stream                               */
/*****************************************************************************/

static void
  copy_jpx_stream(jpx_codestream_source src, jpx_codestream_target tgt)
{
  jpx_input_box box_in;
  src.open_stream(&box_in);
  jp2_output_box *box_out = NULL;
  box_out = tgt.open_stream();
  
  kdu_byte *buffer = NULL;
  try { // Ensure that `buffer' gets deleted in case of failure
    int xfer_bytes;
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
  } catch (...) {
    if (buffer != NULL)
      delete[] buffer;
    throw;
  }  
  box_in.close();
}

/*****************************************************************************/
/* STATIC                   make_layer_descriptions                          */
/*****************************************************************************/

static vcom_jpx_layer *
  make_layer_descriptions(kdu_args &args, int num_components, bool is_ycc)
  /* Builds a linked list of JPX compositing layer descriptions from the
     `-jpx_layers' argument, if it exists; otherwise, constructs just
     one layer description from the `num_components' and `is_ycc' info. */
{
  vcom_jpx_layer *head, *tail;
  head = tail = NULL;
  if (args.find("-jpx_layers") != NULL)
    { 
      char*cp, *string = args.advance();
      for (; (string != NULL) && (*string != '-') &&
             ((cp=strchr(string,',')) != NULL);
           string=args.advance())
        {       
          if (tail == NULL)
            head = tail = new vcom_jpx_layer;
          else
            tail = tail->next = new vcom_jpx_layer;
          *cp = '\0';
          if (strcmp(string,"bilevel1") == 0)
            tail->space = JP2_bilevel1_SPACE;
          else if (strcmp(string,"bilevel2") == 0)
            tail->space = JP2_bilevel2_SPACE;
          else if (strcmp(string,"YCbCr1") == 0)
            tail->space = JP2_YCbCr1_SPACE;
          else if (strcmp(string,"YCbCr2") == 0)
            tail->space = JP2_YCbCr2_SPACE;
          else if (strcmp(string,"YCbCr3") == 0)
            tail->space = JP2_YCbCr3_SPACE;
          else if (strcmp(string,"PhotoYCC") == 0)
            tail->space = JP2_PhotoYCC_SPACE;
          else if (strcmp(string,"CMY") == 0)
            tail->space = JP2_CMY_SPACE;
          else if (strcmp(string,"CMYK") == 0)
            tail->space = JP2_CMYK_SPACE;
          else if (strcmp(string,"YCCK") == 0)
            tail->space = JP2_YCCK_SPACE;
          else if (strcmp(string,"CIELab") == 0)
            tail->space = JP2_CIELab_SPACE;
          else if (strcmp(string,"CIEJab") == 0)
            tail->space = JP2_CIEJab_SPACE;
          else if (strcmp(string,"sLUM") == 0)
            tail->space = JP2_sLUM_SPACE;
          else if (strcmp(string,"sRGB") == 0)
            tail->space = JP2_sRGB_SPACE;
          else if (strcmp(string,"sYCC") == 0)
            tail->space = JP2_sYCC_SPACE;
          else if (strcmp(string,"esRGB") == 0)
            tail->space = JP2_esRGB_SPACE;
          else if (strcmp(string,"esYCC") == 0)
            tail->space = JP2_esYCC_SPACE;
          else if (strcmp(string,"ROMMRGB") == 0)
            tail->space = JP2_ROMMRGB_SPACE;
          else if (strcmp(string,"YPbPr60_SPACE") == 0)
            tail->space = JP2_YPbPr60_SPACE;
          else if (strcmp(string,"YPbPr50_SPACE") == 0)
            tail->space = JP2_YPbPr50_SPACE;
          else
            { kdu_error e; e << "Unrecognized colour space type, \""
              << string << "\", in `-jpx_layers' argument."; }
          string = cp+1;
          tail->num_colours = 0;
          while (*string != '\0')
            { 
              if (*string == ',')
                { string++; continue; }
              int idx = (int)strtol(string,&cp,10);
              if ((idx < 0) || (idx >= num_components))
                { kdu_error e; e << "Invalid codestream component "
                  "specification found in `-jpx_layers' argument.  Component "
                  "indices must lie in the range 0 to " << num_components-1 <<
                  " for this source."; }
              if (tail->num_colours >= 4)
                { kdu_error e; e << "Invalid codestream component "
                  "specification found in `-jpx_layers' argument.  There "
                  "can be at most 4 colour channels for any colour space; "
                  "you have supplied a parameter string with more than 4 "
                  "colour channel component indices."; }
              tail->components[tail->num_colours++] = idx;
              string = cp;
              if ((*string != '\0') && (*string != ','))
                { kdu_error e; e << "Invalid layer specification supplied "
                  "with `-jpx_layers' argument; expected colour space, "
                  "followed by a comma-separated list of component indices; "
                  "invalid suffix is: \"" << string << "\"."; }
            }
        }
      if (head == NULL)
        { kdu_error e; e << "`-jpx_layers' requires at least one parameter "
          "string."; }
    }
  else
    { 
      head = tail = new vcom_jpx_layer;
      if (num_components < 3)
        { 
          tail->num_colours = 1;
          tail->space = JP2_sLUM_SPACE;
          tail->components[0] = 0;
        }
      else
        { 
          tail->num_colours = 3;
          tail->space = (is_ycc)?JP2_sYCC_SPACE:JP2_sRGB_SPACE;
          tail->components[0] = 0;
          tail->components[1] = 1;
          tail->components[2] = 2;
        }
    }
  return head;
}


/* ========================================================================= */
/*                             External Functions                            */
/* ========================================================================= */

namespace kdu_supp_vcom {
  
/*****************************************************************************/
/* EXTERN                  vcom_initialize_jpx_target                        */
/*****************************************************************************/

jpx_container_target
  vcom_initialize_jpx_target(jpx_target &jpx_out, jpx_source &jpx_in,
                             int num_components, bool is_ycc,
                             kdu_field_order field_order, kdu_args &args)
{
  // First copy the prefix file, `jpx_in' across to `jpx_out'
  int n, w, num_top_codestreams, num_top_compositing_layers;
  jpx_container_source csrc = jpx_in.access_container(0);
  if (csrc.exists())
    { 
      num_top_codestreams = csrc.get_num_top_codestreams();
      num_top_compositing_layers = csrc.get_num_top_layers();
    }
  else
    { 
      jpx_in.count_codestreams(num_top_codestreams);
      jpx_in.count_compositing_layers(num_top_compositing_layers);
    }

  // Transfer prefix file attributes
  jpx_out.access_compatibility().copy(jpx_in.access_compatibility());
  jpx_composition top_comp = jpx_in.access_composition();
  if (!(top_comp.exists() && top_comp.access_frame(0,0,true).exists()))
    { kdu_error e; e << "The `-jpx_prefix' file must contain at least one "
      "top-level composited frame, described by a Composition box.";
    }
  jpx_out.access_composition().copy(top_comp);
  for (n=0; n < num_top_compositing_layers; n++)
    jpx_out.add_layer().copy_attributes(jpx_in.access_layer(n));
  jpx_codestream_target *out_top_streams =
    new jpx_codestream_target[num_top_codestreams];
  for (n=0; n < num_top_codestreams; n++)
    { 
      out_top_streams[n] = jpx_out.add_codestream();
      out_top_streams[n].copy_attributes(jpx_in.access_codestream(n));
    }
  for (w=0; ((csrc=jpx_in.access_container(w)).exists() &&
             csrc.access_layer(0,0,true).exists()); w++)
    { 
      int num_layers, num_streams;
      csrc.get_base_layers(num_layers);
      csrc.get_base_codestreams(num_streams);
      int num_reps=0; csrc.count_repetitions(num_reps);
      assert(num_reps > 0);      
      jpx_container_target ctgt = jpx_out.add_container(num_streams,num_layers,
                                                        num_reps);
      kdu_uint32 t, num_tracks = csrc.get_num_tracks();
      for (t=1; t <= num_tracks; t++)
        { 
          jpx_composition comp_src, comp_tgt;
          if (!(comp_src = csrc.access_presentation_track(t)))
            break; // Also should not happen, but might as well play safe
          int trk_layers; csrc.get_track_base_layers(t,trk_layers);
          ctgt.add_presentation_track(trk_layers).copy(comp_src);
        }
      for (n=0; n < num_layers; n++)
        ctgt.access_layer(n).copy_attributes(csrc.access_layer(n,0));
      for (n=0; n < num_streams; n++)
        ctgt.access_codestream(n).copy_attributes(csrc.access_codestream(n,0));
    }
  
  // Write top-level headers and codestreams, after first reserving the
  // right to append JPX containers
  jpx_out.expect_containers();
  for (n=0; n < num_top_codestreams; n++)
    { 
      jpx_out.write_headers(NULL,NULL,n);
      jpx_codestream_source in_stream = jpx_in.access_codestream(n);
      assert(in_stream.exists());
      copy_jpx_stream(in_stream,out_top_streams[n]);
    }
  delete[] out_top_streams;
  out_top_streams = NULL;
  
  // Copy JPX metadata
  jpx_meta_manager meta_in = jpx_in.access_meta_manager();
  jpx_meta_manager meta_out = jpx_out.access_meta_manager();
  meta_out.copy(meta_in);
  
  // Write all copied headers and metadata
  while (jpx_out.write_headers() != NULL);
  while (jpx_out.write_metadata() != NULL);
  
  // Write all relevant container codestreams
  for (w=0; ((csrc=jpx_in.access_container(w)).exists() &&
             csrc.access_layer(0,0,true).exists()); w++)
    { 
      jpx_container_target ctgt = jpx_out.access_container(w);
      assert(csrc.exists() && ctgt.exists());
      int num_streams;  csrc.get_base_codestreams(num_streams);
      int num_reps=0; csrc.count_repetitions(num_reps);
      assert(num_reps > 0);
      for (int r=0; r < num_reps; r++)
        for (n=0; n < num_streams; n++)
          { 
            jpx_codestream_source in_stream = csrc.access_codestream(n,r,true);
            assert(in_stream.exists());
            jpx_codestream_target out_stream = ctgt.access_codestream(n);
            copy_jpx_stream(in_stream,out_stream);
          }
    }
  
  // Next, determine the number of compositing layers to add for each
  // codestream, along with the associated bindings.
  vcom_jpx_layer *desc, *layer_descriptions =
    make_layer_descriptions(args,num_components,is_ycc);
  assert(layer_descriptions != NULL);
  
  // Finally, we are in a position to set up the new JPX container
  int num_streams=1;
  if (field_order != KDU_FIELDS_NONE)
    num_streams = 2;
  int num_layers = 0;
  for (desc=layer_descriptions; desc != NULL; desc=desc->next)
    num_layers += num_streams;
  jpx_container_target result=jpx_out.add_container(num_streams,num_layers,0);
  for (w=0, desc=layer_descriptions; desc != NULL; desc=desc->next, w++)
    { 
      char lbl[40];
      sprintf(lbl,"Presentation %d",w+1);
      jpx_metanode desc_node =
        meta_out.insert_node(0,NULL,0,NULL,false,0,NULL,
                             jpx_metanode(),result.get_container_id());
      desc_node = desc_node.add_label(lbl);
      for (n=0; n < num_streams; n++)
        { 
          int stream_idx = result.access_codestream(n).get_codestream_id();
          jpx_layer_target layer = result.access_layer(w*num_streams+n);
          int layer_idx = layer.get_layer_id();
          jp2_colour colour = layer.add_colour();
          colour.init(desc->space);
          if (colour.get_num_colours() != desc->num_colours)
            { kdu_error e; e << "Incorrect number of codestream component "
              "indices supplied for colour description in `-jpx_layers' "
              "argument."; }
          jp2_channels channels = layer.access_channels();
          channels.init(desc->num_colours);
          for (int c=0; c < desc->num_colours; c++)
            channels.set_colour_mapping(c,desc->components[c],-1,stream_idx);
          jpx_metanode layer_node =
            meta_out.insert_node(0,NULL,1,&layer_idx,false,0,NULL,
                                 desc_node,result.get_container_id());
          if (field_order == KDU_FIELDS_TOP_FIRST)
            sprintf(lbl,"Fields-%d (top first)",n);
          else if (field_order != KDU_FIELDS_NONE)
            sprintf(lbl,"Fields-%d (top second)",n);
          else
            sprintf(lbl,"Frames");
          layer_node.add_label(lbl);
        }
    }
  
  while ((desc=layer_descriptions) != NULL)
    { 
      layer_descriptions = desc->next;
      delete desc;
    }
  
  return result;
}

} // namespace kdu_supp_vcom
