/*****************************************************************************/
// File: kdu_merge.h [scope = APPS/MERGE]
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
   Header file for "kdu_merge.cpp"
******************************************************************************/
#ifndef KDU_MERGE_H
#define KDU_MERGE_H

#include "jpx.h"
#include "mj2.h"

// OK to have "using" statement in this header because it is intended to
// be private to the "kdu_merge" demo app.
using namespace kdu_supp; // Also includes the `kdu_core' namespace


// Defined here:
struct mg_source_spec;
struct mg_palette_spec;
struct mg_channel_spec;
struct mg_layer_spec;
struct mg_codestream_spec;
struct mg_container_spec;
struct mg_track_seg;
struct mg_track_spec;

/*****************************************************************************/
/*                               mg_source_spec                              */
/*****************************************************************************/

struct mg_source_spec {
  public: // Member functions
    mg_source_spec()
      { 
        filename = NULL;
        metadata_source = this; // Import metadata from ourself by default
        num_codestreams = num_layers = num_frames = num_fields = 0;
        field_order = KDU_FIELDS_NONE;  mjc_flags = 0; mjc_num_components = 0;
        first_frame_idx = 0; codestream_specs = NULL;
        next=NULL; video_source = NULL;
      }
    ~mg_source_spec()
      { 
        if (filename != NULL)
          delete[] filename;
        if (codestream_specs != NULL)
          delete[] codestream_specs;
        mjc_src.close();
        jpx_src.close();
        mj2_src.close();
        family_src.close();
      }
  public: // Data
    char *filename;
    kdu_simple_file_source raw_src; // Only opened on demand
    kdu_simple_video_source mjc_src; // We leave this open for convenience
    kdu_coords mjc_codestream_size; // These are for MJC (resp. raw) files, as
    kdu_coords raw_codestream_size; // would be returned by `jp2_dimensions'.
    jp2_family_src family_src; // We leave this open for convenience
    jpx_source jpx_src; // We leave this open for convenience
    mj2_source mj2_src; // We leave this open for convenience
    mj2_video_source *video_source; // For MJ2 tracks
    mg_source_spec *metadata_source; // Object from which to import metadata
    int num_codestreams;
    mg_codestream_spec **codestream_specs; // See below
    int num_layers; // One layer per codestream for MJ2 & raw sources
    int num_frames; // One frame per layer for JPX sources
    int num_fields; // Number of video fields which can be made from the source
    kdu_field_order field_order; // For MJ2 sources
    kdu_uint32 mjc_flags; // For MJC sources
    int mjc_num_components; // Read from first codestream; all should be same
    int first_frame_idx; // Seeking offset for first used frame in MJ2 track
    mg_source_spec *next;
  };
  /* Notes:
        The `codestream_specs' array has `num_codestreams' entries that are
     initialized to NULL.  If a codestream in the source file is found to
     be required for the output file, its entry in this array is initialized
     to point to the relevant `mg_codestream_spec' object. */

/*****************************************************************************/
/*                              mg_palette_spec                              */
/*****************************************************************************/

struct mg_palette_spec {
    mg_palette_spec(int n_luts, int n_bits)
      { // `n_bits' should be -ve if the LUT's hold signed data.
        num_luts = n_luts; bit_depth = n_bits;
        max_entries = num_entries = 0; data = NULL;
      }
    ~mg_palette_spec()
      { if (data != NULL) delete[] data; }
    void write(jp2_palette plt)
      { 
        plt.init(num_luts,num_entries);
        for (int n=0; n < num_luts; n++)
          plt.set_lut(n,data+n*max_entries,(bit_depth<0)?-bit_depth:bit_depth,
                      (bit_depth<0)?true:false);
      }
    void add_entry(int *lut_vals)
      { /* Supplied array must have `num_luts' elements. */
        if (num_entries == max_entries)
          { 
            int i, j, old_max_entries = max_entries;
            max_entries+=max_entries+1;
            int *new_data = new int[max_entries*num_luts];
            for(i=0; i < num_luts; i++)
              for (j=0; j < num_entries; j++)
                new_data[i*max_entries+j] = data[i*old_max_entries+j];
            if (data != NULL) delete[] data;
            data = new_data;
          }
        int i, j=num_entries++;
        for (i=0; i < num_luts; i++)
          data[i*max_entries+j] = lut_vals[i];
      }
  public: // Data
    int num_luts;
    int max_entries; // Lets us dynamically grow the `data' array
    int num_entries;
    int bit_depth; // Same for all lut's here; -ve if signed data
    int *data; // Organized one LUT at a time
  };

/*****************************************************************************/
/*                             mg_codestream_spec                            */
/*****************************************************************************/

struct mg_codestream_spec {
  mg_codestream_spec()
    { this->source=NULL; this->next=NULL; }
  public: // Data
    int out_codestream_idx;
    mg_source_spec *source;
    int source_codestream_idx;
    jpx_codestream_target tgt;
    mg_codestream_spec *next;
  };

/*****************************************************************************/
/*                               mg_channel_spec                             */
/*****************************************************************************/

struct mg_channel_spec { // For JPX output files
  public: // Member functions
    mg_channel_spec()
      { file=NULL; codestream_idx=component_idx=lut_idx=-1; next=NULL; }
  public: // Data
    mg_source_spec *file;
    int codestream_idx; // Index of codestream within `file'
    int component_idx;
    int lut_idx;
    mg_channel_spec *next;
  };

/*****************************************************************************/
/*                               mg_layer_spec                               */
/*****************************************************************************/

struct mg_layer_spec { // For JPX output files
  public: // Member functions
    mg_layer_spec(int idx)
      { file=NULL; channels=NULL; used_codestreams=NULL; next=NULL;
        album_page_idx=-1; source_layer_idx=0; out_layer_idx=idx;
        num_used_codestreams=0; num_colour_channels=0; num_alpha_channels=0; }
    ~mg_layer_spec()
      { mg_channel_spec *cp;
        while ((cp=channels) != NULL)
          { channels=cp->next; delete cp; }
        if (used_codestreams != NULL)
          delete[] used_codestreams;
      }
  public: // Members for existing layes
    mg_source_spec *file; // NULL if we are building a layer from scratch
    int source_layer_idx;
  public: // Members for building a layer from scratch
    jp2_colour_space space;
    int num_colour_channels;
    int num_alpha_channels; // At most 1
    mg_channel_spec *channels; // Linked list
  public: // Members used to keep track of codestreams used by this layer
    int num_used_codestreams;
    int *used_codestreams; // Set of output codestream indices used by layer
  public: // Common members
    int out_layer_idx;
    mg_layer_spec *next;
    kdu_coords size; // Size of the first code-stream used by the layer; filled
                     // in when writing layers to output file.
    int album_page_idx; // Index of of the frame which contains the album index
       // page on which this layer appears -- -ve if not part of an album.
  };

/*****************************************************************************/
/*                            mg_container_spec                              */
/*****************************************************************************/

struct mg_container_spec {
  public: // Member functions
    mg_container_spec()
      { base_layers=NULL; num_tracks = 0; next=NULL; }
    ~mg_container_spec()
      { if (base_layers != NULL) delete[] base_layers; }
  public: // Data
    int num_repetitions; // Can be 0 if indefinite
    int num_base_layers;
    mg_layer_spec **base_layers;
    int num_base_codestreams;
    int first_base_codestream_idx;
    int num_tracks; // Number of tracks added so far
    jpx_container_target tgt;
    mg_container_spec *next;
  };

/*****************************************************************************/
/*                               mg_track_seg                                */
/*****************************************************************************/

struct mg_track_seg { // For MJ2 output files
  public: // Member functions
    mg_track_seg()
      { from=to=0;  fps=0.0F;  next = NULL; }
  public: // Data
    int from, to;
    float fps;
    mg_track_seg *next;
  };

/*****************************************************************************/
/*                               mg_track_spec                               */
/*****************************************************************************/

struct mg_track_spec { // For MJ2 output files
  public: // Member functions
    mg_track_spec()
      { field_order = KDU_FIELDS_NONE;  segs = NULL;  next = NULL; }
    ~mg_track_spec()
      { mg_track_seg *tmp;
        while ((tmp=segs) != NULL)
          { segs = tmp->next; delete tmp; }
      }
  public: // Data
    kdu_field_order field_order;
    mg_track_seg *segs;
    mg_track_spec *next;
  };

#endif // KDU_MERGE_H
