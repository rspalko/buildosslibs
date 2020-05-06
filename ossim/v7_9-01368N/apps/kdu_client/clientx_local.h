/*****************************************************************************/
// File: clientx_local.h [scope = APPS/KDU_CLIENT]
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
  Private definitions used in the implementation of the "kdu_clientx" class.
******************************************************************************/

#ifndef CLIENTX_LOCAL_H
#define CLIENTX_LOCAL_H

#include "kdu_clientx.h"

// Defined here:
namespace kd_supp_local {
  struct kdcx_entity_container;
  class kdcx_context_mappings;
  struct kdcx_stream_mapping;
  struct kdcx_layer_member;
  struct kdcx_layer_mapping;
  struct kdcx_iset_list;
  struct kdcx_comp_instruction;
}

namespace kd_supp_local {
  using namespace kdu_supp;

/*****************************************************************************/
/*                          kdcx_entity_container                            */
/*****************************************************************************/

struct kdcx_entity_container {
  public: // Member functions
    kdcx_entity_container(kdcx_context_mappings *top_mappings);
      /* `top_mappings' is the top-level context-mappings object for the
         file. */
    ~kdcx_entity_container();
    bool init(kdcx_entity_container *last_initialized);
      /* Attempts to parse the INFO sub-box of the `jclx_box', returning false
         if this is not possible.  If successful, the function initializes
         all member variables (or finishes initializing them), using
         `last_initialized' to figure out the absolute indices of the
         first base codestream and first base compositing layer, after which
         `info_complete' is set to true and the function returns true.
         It is an error to call this function if `info_complete' is already
         true, or if `jclx_box' does not exist. */
    bool finish();
      /* Called if `info_complete' is true, but `all_complete' is false.
         This function attempts to parse sub-boxes from the `jclx_box' if it
         is still open, passing them along to the `context_mappings' object.
         It also invokes `context_mappings->finish_layers_and_streams'.  The
         `all_complete' member is set to true if `jclx_box' is no longer
         open and all embedded codestreams and compositing layers have been
         successfully finished.  The function returns true if the set of
         known information is augmented by the call, which may happen even
         if `all_complete' remains false. */
  public: // Data
    kdcx_entity_container *next; // Used to build a linked list
    bool info_complete; // If the "info" box has been fully parsed
    bool all_complete; // If there is no future need to call `finish'
    int num_jpch; // Number of JPCH boxes found so far
    int num_jplh; // Number of JPLH boxes found so far
    jp2_input_box jclx_box;
    jp2_input_box jclx_sub;
  public: // Members affecting the interpretation of codestream entities
    int num_top_codestreams;
    int first_base_codestream;
    int num_base_codestreams;
    int max_codestream;
  public: // Members affecting the interpretation of compositing layer entities
    int num_top_layers;
    int first_base_layer;
    int num_base_layers;
    int max_layer;  
  public: // Context mappings for this container
    kdcx_context_mappings *context_mappings; // Holds info for translating
                         // codestream contexts relating to this container
  };

/*****************************************************************************/
/*                           kdcx_stream_mapping                             */
/*****************************************************************************/

struct kdcx_stream_mapping {
  /* This object represents information which might be recovered from a
     codestream header box, which is required for mapping codestream
     contexts to codestreams and components. */
  public: // Member functions
    kdcx_stream_mapping()
      {
        is_complete = jpch_closed = false;
        num_channels = num_components = 0;
        component_indices = NULL;
      }
    ~kdcx_stream_mapping()
      { if (component_indices != NULL) delete[] component_indices; }
    void parse_ihdr_box(jp2_input_box *box);
      /* Parses to the end of the image header box leaving it open, using
         the information to set the `size' and `num_components' members. */
    void parse_cmap_box(jp2_input_box *box);
      /* Parses to the end of the component mapping box leaving it open,
         using the information to set the `num_channels' and
         `component_indices' members. */
    bool finish(kdcx_stream_mapping *defaults);
      /* This function attempts to complete the parsing and configuration of
         the object.  You should not call this function until you have
         encountered the JPCH box for this codestream, or else you can
         be sure that no JPCH box will be found.  If `is_complete' is
         found to be true on entry, the function returns false immediately,
         meaning that there has been no change.
            The function attempts to finish parsing any open `jpch_box',
         returning false if this task cannot be completed.  Once there is no
         open `jpch_box', the function attempts to import any missing
         information from the `defaults' object, returning prematurely if
         there is missing information and `defaults->is_complete' is false.
            If all these operations succeed, the function sets the
         `is_complete' member from false to true, returning true to indicate
         that information has become newly available. */
  public: // Data
    bool is_complete; // True once all boxes parsed and `finish' called
    bool jpch_closed; // If a `jpch_box' existed and has been fully parsed
    jp2_input_box jpch_box; // Used to keep track of an incomplete jpch box
    jp2_input_box jpch_sub; // Used to keep track of an incomplete sub-box
    kdu_coords size;
    int num_components;
    int num_channels;
    int *component_indices; // Lists component indices for each channel
  };

/*****************************************************************************/
/*                           kdcx_layer_member                               */
/*****************************************************************************/

struct kdcx_layer_member {
  public: // Member functions
    kdcx_layer_member()
      { codestream_idx = num_components = 0; component_indices = NULL; }
    ~kdcx_layer_member()
      { if (component_indices != NULL) delete[] component_indices; }
  public: // Data
    int codestream_idx; // See below
    kdu_coords reg_subsampling;
    kdu_coords reg_offset;
    int num_components;
    int *component_indices;
  };
  /* If this object is embedded within the top-level `kdcx_context_mappings'
     object, `codestream_idx' is an absolute codestream index; otherwise,
     `codestream_idx' is equal to C + T-B, where C is the absolute codestream
     index, T is the number of top-level codestreams in the file (reported
     via `kdcx_entity_container::num_top_codestreams') and B is the absolute
     index of the first base codestream index in the associated JPX container
     (reported via `kdcx_entity_container::first_base_codestream').  This
     is exactly the way codestream indices found within codestream
     registration boxes are to be interpreted when they are embedded within
     a JPX container (Compositing Layer Extensions box). */

/*****************************************************************************/
/*                           kdcx_layer_mapping                              */
/*****************************************************************************/

struct kdcx_layer_mapping {
  public: // Member functions
    kdcx_layer_mapping()
      { 
        is_complete = jplh_closed = false; rel_layer_idx=-1;
        num_members = num_channels = num_colours = 0;
        have_opct_box = have_opacity_channel = false;
        members=NULL;  channel_indices=NULL;
      }
    kdcx_layer_mapping(int rel_layer_idx)
      { /* Note: `rel_layer_idx' values start from 0 within the containing
           `kdcx_context_mappings' object.  For a top-level mappings object,
           `rel_layer_idx' values correspond to absolute compositing layer
           indices; otherwise, `rel_layer_idx' measures the ordinal position
           of this layer within the collection of base compositing layers for
           a JPX container. */
        is_complete = false;  this->rel_layer_idx = rel_layer_idx;
        num_members = num_channels = num_colours = 0;
        have_opct_box = have_opacity_channel = false;
        members=NULL; channel_indices=NULL;
      }
    ~kdcx_layer_mapping()
      {
        if (members != NULL) delete[] members;
        if (channel_indices != NULL) delete[] channel_indices;
      }
    void parse_cdef_box(jp2_input_box *box);
      /* Parses to the end of the channel definition box leaving it open, using
         the information to set the `num_channels' and `channel_indices'
         members. */
    void parse_creg_box(jp2_input_box *box);
      /* Parses to the end of the codestream registration box, leaving it open,
         using the information to set the `num_members' and `members'
         members. */
    void parse_colr_box(jp2_input_box *box);
      /* Parses to the end of the colour description box leaving it open,
         using the information to set the `num_colours' member if possible.
         Once any colour description box is encountered, the value of
         `num_colours' is set to a non-zero value -- either the actual
         number of colours (if the box can be parsed) or -1 (if the box
         cannot be parsed). */
    void parse_opct_box(jp2_input_box *box);
      /* Parses the opacity box, leaving it open.  Sets `have_opct_box' to
         true.  Also sets `have_opacity_channel' to true if the box has
         an OTyp field equal to 0 or 1.  In this case, there will be no
         channel definition box, but `num_colours' must be augmented by 1
         to get the number of channels used. */
    bool finish(kdcx_layer_mapping *defaults, kdcx_context_mappings *owner);
      /* This function attempts to complete the parsing and configuration of
         the object.  You should not call this function until you have
         encountered the JPLH box for this compositing layer, or else you can
         be sure that no JPLH box will be found.  If `is_complete' is
         found to be true on entry, the function returns false immediately,
         meaning that there has been no change.
            The function attempts to finish parsing any open `jplh_box',
         returning false if this task cannot be completed.  Once there is no
         open `jplh_box', the function attempts to finish parsing any
         open `cgrp_box', returning false if this task cannot be completed.
         Once there is no open `jplh_box' or `cgrp_box', the function attempts
         to import any missing information from the `defaults' object,
         returning prematurely if there is missing information and
         `defaults->is_complete' is false.  The function also returns false
         prematurely if any required codestream is missing or incomplete.
            If all these operations succeed, the function sets the
         `is_complete' member from false to true, returning true to indicate
         that information has become newly available. */
  public: // Data
    bool is_complete; // True once all boxes parsed and `finish' called
    bool jplh_closed; // If a `jplh_box' existed and has been fully parsed
    jp2_input_box jplh_box; // Used to keep track of an incomplete jplh box
    jp2_input_box jplh_sub; // Used to keep track of an incomplete sub-box
    jp2_input_box cgrp_box; // Used to keep track of an incomplete cgrp box
    jp2_input_box cgrp_sub; // Used to keep track of an incomplete colr box
    int rel_layer_idx;
    kdu_coords layer_size;
    kdu_coords reg_precision;
    int num_members;
    kdcx_layer_member *members;
    bool have_opct_box; // If opacity box was found in the layer header
    bool have_opacity_channel; // If true, `num_channels'=1+`num_colours'
    int num_colours; // Used when `num_channels' is not known.
    int num_channels;
    int *channel_indices; // Sorted into increasing order
  };

/*****************************************************************************/
/*                              kdcx_iset_list                               */
/*****************************************************************************/

struct kdcx_iset_list {
  jp2_input_box iset_box;
  kdcx_iset_list *next;
  };

/*****************************************************************************/
/*                          kdcx_comp_instruction                            */
/*****************************************************************************/

struct kdcx_comp_instruction {
  public: // Member functions
    kdcx_comp_instruction() { transpose=vflip=hflip=false; }
  public: // Data
    kdu_dims source_dims; // After cropping
    bool transpose, vflip, hflip; // Geometric manipulations
    kdu_dims target_dims; // On final rendering grid
  };
  /* Notes:
       If `source_dims' has zero area, the source JPX compositing layer is to
     be used without any cropping.  Otherwise, it is cropped by
     `source_dims.pos' from the upper left hand corner, and to a size of
     `source_dims.size' on the compositing layer registration grid.
       If `transpose' is true, the cropped source image is to be transposed
     before scaling and translating to match `target_dims'.  If `vflip' and/or
     `hflip' is true, the cropped (possibly transposed) source image is to
     be flipped before scaling and translating to match `target_dims'.
       If `target_dims' has zero area, the compositing layer is to be
     placed on the rendering canvas without scaling.  The size is derived
     either from `source_dims.size' or, if `source_dims' has zero area, from
     the original compositing layer dimensions, after possible
     transposition. */   

/*****************************************************************************/
/*                          kdcx_context_mappings                            */
/*****************************************************************************/

class kdcx_context_mappings : public kdu_wincontext_mappings {
  public: // Member functions
    kdcx_context_mappings(kdcx_entity_container *container,
                          kdcx_context_mappings *top_mappings)
      { /* NULL is passed for both arguments when constructing the top-level
           `kdcx_context_mappings' object. */
        this->container = container;  this->top_mappings = top_mappings;
        num_codestreams = max_codestreams = 0;
        num_compositing_layers = max_compositing_layers = 0;
        stream_refs = NULL;  layer_refs = NULL;
        
        comp_info_complete = false;
        donated_isets = last_donated_iset = NULL;
        first_unfinished_stream = first_unfinished_layer = 0;
        
        num_comp_sets = max_comp_sets = 0;
        comp_set_starts = NULL;
        num_comp_instructions = max_comp_instructions = 0;
        comp_instructions = NULL;
      }
    ~kdcx_context_mappings()
      { 
        while ((last_donated_iset = donated_isets) != NULL)
          { 
            donated_isets = last_donated_iset->next;
            delete last_donated_iset;
          }
        int n;
        for (n=0; n < num_codestreams; n++)
          delete stream_refs[n];
        if (stream_refs != NULL) delete[] stream_refs;
        for (n=0; n < num_compositing_layers; n++)
          delete layer_refs[n];
        if (layer_refs != NULL) delete[] layer_refs;
        if (comp_set_starts != NULL) delete[] comp_set_starts;
        if (comp_instructions != NULL) delete[] comp_instructions;
      }
  public: // Implementation of pure virtual functions from base class
    virtual int get_num_members(int base_context_idx, int rep_idx,
                                const int remapping_ids[]);
    virtual int get_codestream(int base_context_idx, int rep_idx,
                               const int remapping_ids[], int member_idx);
    virtual const int *get_components(int base_context_idx, int rep_idx,
                                      const int remapping_ids[],
                                      int member_idx, int &num_components);
    virtual bool perform_remapping(int base_context_idx, int rep_idx,
                                   const int remapping_ids[], int member_idx,
                                   kdu_coords &resolution, kdu_dims &region);
  public: // Functions specific to this derived class
    kdcx_stream_mapping *add_stream(int idx, bool container_relative);
      /* If the indicated codestream does not already have an allocated
         `kdcx_stream_mapping' object, one is created here.  In any event,
         the relevant object is returned.
         The `container_relative' argument has no impact unless this object
         belongs to a `kdcx_entity_container'.  In that case, if
         `container_relative' is true, `idx'=0 corresponds to the first
         base codestream defined by the JPX container; on the other hand,
         if `container_relative' is false, the first base codestream defined
         by the container corresponds to the value `idx' =
         `kdcx_entity_container::num_top_codestreams' and this is the smallest
         value allowed. */
    kdcx_layer_mapping *add_layer(int idx, bool is_relative);
      /* If the indicated compositing layer does not already have an allocated
         `kdcx_layer_mapping' object, one is created here.  In any event,
         the relevant object is returned.
         The `container_relative' argument has no impact unless this object
         belongs to a `kdcx_entity_container'.  In that case, if
         `container_relative' is true, `idx'=0 corresponds to the first
         base compositing layer defined by the JPX container; on the other
         hand, if `container_relative' is false, the first base layer defined
         by the container corresponds to the value `idx' =
         `kdcx_entity_container::num_top_layers' and this is the smallest
         value allowed. */
    void donate_iset(jp2_input_box &iset_box)
      { // This function allows instruction set boxes that might not yet be
        // complete to be stored internally until they can be parse later by
        // `parse_all_donated_isets'.
        kdcx_iset_list *elt = new kdcx_iset_list; elt->next = NULL;
        if (last_donated_iset == NULL)
          donated_isets = last_donated_iset = elt;
        else
          last_donated_iset = last_donated_iset->next = elt;
        elt->iset_box.transplant(iset_box);
      }
    bool parse_all_donated_isets();
      /* Attempts to parse iset boxes donated by `donate_iset', in order,
         returning true only if it manages to completely parse all donated
         iset boxes. */
    kdcx_stream_mapping *get_stream_defaults() { return &stream_defaults; }
    kdcx_layer_mapping *get_layer_defaults() { return &layer_defaults; }
    void parse_copt_box(jp2_input_box *box);
      /* Parses to the end of the composition options box, leaving it open,
         and using the contents to set the `composited_size' member. */
    void parse_iset_box(jp2_input_box *box);
      /* Parses to the end of the composition instruction box, leaving it open,
         and using the contents to add a new composition instruction set to
         the `comp_set_starts' array and to parse its instructions into the
         `comp_instructions' array, adjusting the relevant counters. */
    bool finish_layers_and_streams(bool context_complete);
      /* Tries to advance `first_unfinished_stream' to `num_codestreams'
         and `first_unfinished_layer' to `num_layers', returning true if
         layer or stream description becomes finished.  If `context_complete'
         is true, there will be no more compositing layer header boxes or
         codestream header boxes discovered for this context, which means
         that any entities for which headers have not yet been discovered
         should use the defaults. */
  private: // Helper functions
    friend struct kdcx_entity_container;
    friend struct kdcx_layer_mapping;
    friend class kdu_supp::kdu_clientx;
    kdcx_stream_mapping *stream_lookup(int idx)
      { 
        if (container != NULL)
          { 
            if (idx < container->num_top_codestreams)
              return top_mappings->stream_lookup(idx);
            idx -= container->num_top_codestreams; // Make relative
          }
        if ((idx < 0) || (idx >= num_codestreams)) return NULL;
        return stream_refs[idx];
      }
      /* This function is used to find a codestream specification that is
         referenced by a compositing layer defined within this object.  The
         `idx' member is related to the absolute codestream index of interest
         in the following way:
         1. If this is the top-level context-mapping object, `idx' is
            the absolute index of the codestream.
         2. Otherwise, if `idx' is less than `container->num_top_codestreams',
            `idx' is still the absolute index of the codestream.
         3. Otherwise, `idx'-`container->num_top_codestreams' is the relative
            index of one of the base codestreams defined by the container.
         The function returns NULL if it determines that `idx' is not valid.
      */
  private: // Data
    kdcx_entity_container *container;
    kdcx_context_mappings *top_mappings;
    kdcx_stream_mapping stream_defaults;
    kdcx_layer_mapping layer_defaults;

    bool comp_info_complete;
    kdcx_iset_list *donated_isets;
    kdcx_iset_list *last_donated_iset;
    int first_unfinished_stream;
    int first_unfinished_layer;
  
    int num_codestreams;
    int max_codestreams; // Size of following array
    kdcx_stream_mapping **stream_refs;
    int num_compositing_layers;
    int max_compositing_layers; // Size of following array
    kdcx_layer_mapping **layer_refs;
    
    kdu_coords composited_size; // Valid only if `num_comp_sets' > 0
    int num_comp_sets; // Num valid elts in `comp_set_starts' array
    int max_comp_sets; // Max elts in `comp_set_starts' array
    int *comp_set_starts; // Index of first instruction in each comp set
    int num_comp_instructions; //  Num valid elts in `comp_instructions' array
    int max_comp_instructions; // Max elts in `comp_instructions' array
    kdcx_comp_instruction *comp_instructions; // Array of all instructions
  };
  
} // namespace kd_supp_local

#endif // CLIENTX_LOCAL_H

