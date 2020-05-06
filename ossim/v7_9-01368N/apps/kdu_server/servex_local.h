/*****************************************************************************/
// File: servex_local.h [scope = APPS/SERVER]
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
   Private header file used by "kdu_servex.cpp".
******************************************************************************/

#ifndef SERVEX_LOCAL_H
#define SERVEX_LOCAL_H

#include "kdu_compressed.h"
#include "kdu_servex.h"
#include "kdu_file_io.h"

// Defined here:
namespace kd_serve_local {
  struct kdsx_open_file;
  struct kdsx_entity_container;
  class kdsx_image_entities;
  struct kdsx_metagroup;
  class kdsx_stream;
  class kdsx_context_mappings;
  struct kdsx_stream_mapping;
  struct kdsx_layer_member;
  struct kdsx_layer_mapping;
  struct kdsx_comp_instruction;
}

namespace kd_serve_local {
  using namespace kdu_supp;

/*****************************************************************************/
/*                              kdsx_open_file                               */
/*****************************************************************************/

struct kdsx_open_file {
  public: // Construct/destruct functions
    kdsx_open_file(kdu_servex *owner)
      { 
        this->owner = owner;
        users = NULL; num_locks = 0; open_filename = NULL; fp = NULL;
        open_pos = 0; next = prev = NULL;
      }
    ~kdsx_open_file()
      { /* Removes any users and closes any open file pointer before
           deleting the object's resources. */
        assert(num_locks == 0);
        while (users != NULL)
          remove_user(users);
        if (fp != NULL)
          { 
            fclose(fp);
            fp = NULL;
          }
        if (open_filename != NULL)
          { delete[] open_filename; open_filename = NULL; }
      }
  public: // Member functions that are only invoked with the `owner' object's
          // `codestream_mutex' locked, except when creating initial cache
          // contents inside a first ever call to `kdu_servex::open'.
    void add_user(kdsx_stream *user);
      /* Adds `user' to the list of users for this object; if there were
         previously no users, the object is moved from the `owner' object's
         list of unused files to its list of used files.  This function
         leaves the `user->open_file' member equal to `this'. */
    void remove_user(kdsx_stream *user);
      /* Removes `user' from the list of users for this object; if this
         leaves no users, the object is moved from the `owner' object's
         list of used files to its list of unused files.  This function
         leaves the `user->open_file' member equal to NULL. */
    void add_lock(kdsx_stream *user);
      /* Increments the `num_locks' counter; if this was previous 0, the
         object is moved from the `owner' object's list of used files to
         its list of locked files.  Also increments `owner->num_locked_streams'
         and sets`user->locked_for_access'. */
    void remove_lock(kdsx_stream *user);
      /* Decrements the `num_locks' counter; if this becomes 0, the
         object is moved from the `owner' object's list of locked files to
         its list of used files.  Also decrements `owner->num_locked_streams'
         and resets `user->locked_for_access'. */
  public: // Data
    kdu_servex *owner;
    kdsx_stream *users; // Points to linked list of streams that reference us
    int num_locks; // Number of our users that are locked for access
    char *open_filename;
    FILE *fp; // Open file pointer
    kdu_long open_pos; // Last point at which the file was positioned
    kdsx_open_file *next;
    kdsx_open_file *prev;
  };

/*****************************************************************************/
/*                          kdsx_entity_container                            */
/*****************************************************************************/

struct kdsx_entity_container : public kds_entity_container {
  public: // Member functions
    kdsx_entity_container(kdsx_context_mappings *top_mappings);
      /* `top_mappings' is the top-level context-mappings object for the
         file. */
    ~kdsx_entity_container();
    void init(int num_top_codestreams, int num_top_layers,
              int first_base_codestream, int first_base_layer);
    void parse_info_box(jp2_input_box *box);
      /* Finishes the initialization process commenced by `init', setting
         `num_base_codestreams', `max_codestream', `num_base_layers' and
         `max_layer' members based on the information found within
         the supplied Compositing Layer Extensions Info box.  Leaves `box'
         open. */
    void finish_parsing();
      /* Called from `kdu_metagroup::create' once a Compositing Layer
         Extensions box has been fully parsed.  Creates the
         `committed_entity_refs' array with pointers to each of
         the elements added to the `committed_entities_list' while parsing
         boxes.  Also invokes `context_mappings->finish_parsing'. */
    void serialize(FILE *fp);
    void deserialize(FILE *fp);
      /* `ref_id' is set by the caller before deserialization. */
  public: // Data
    int ref_id; // Starts from 1 -- container 0 interpreted as "top-level"
    kdsx_entity_container *next; // Used to build a linked list
  public: // Image enities and context mappings for this container
    kdsx_image_entities *committed_entities_list;
    int num_committed_entities; // Becomes number of elements in below array
    kdsx_image_entities **committed_entity_refs; // Ptrs into committed list
    kdsx_context_mappings *context_mappings; // Holds info for translating
                         // codestream contexts relating to this container
  };

/*****************************************************************************/
/*                           kdsx_image_entities                             */
/*****************************************************************************/

// Extends `kds_image_entities' to include private book-keeping information.
class kdsx_image_entities : public kds_image_entities {
  public: // Member functions
    kdsx_image_entities()
      { num_entities=max_entities=0; universal_flags=0;
        entities=NULL; container=NULL; next=prev=NULL; ref_id=-1; }
    ~kdsx_image_entities() { if (entities != NULL) delete[] entities; }
    void set_container(kds_entity_container *cont)
      { this->container = cont; }
      /* If the object is to represent entity descriptions that are found
         within a JPX container, this function must be called with the
         relevant container.  Subsequent calls to `add_universal' and
         `add_entity' check for container consistency, while calls to
         `copy_from' copy the container information. */
    int get_container_ref_id()
      { /* Used to uniquely identify the context in which entities are found
           during serialization and deserialization; returns 0 if `container'
           is NULL; else returns the positive container reference id. */
        if (container == NULL) return 0;
        int ref_id = ((kdsx_entity_container *) container)->ref_id;
        assert(ref_id > 0);
        return ref_id;
      }
    void add_entity(int idx, kds_entity_container *container);
      /* Adds a new entity, keeping the sorting order.  It does
         no harm to add existing entities over again.  The `container'
         argument must be consistent with the `container' member. */
    void add_universal(int flags, kds_entity_container *container);
      /* `flags' is either or both of 0x01000000 (add all codestreams) or
         0x02000000 (add all compositing layers).  The `container' argument
         must be consistent with the `container' member. */
    void copy_from(kdsx_image_entities *src)
      { 
        if ((num_entities == 0) && (universal_flags == 0) && (container==NULL))
          container = src->container;
        else
          assert(container == src->container);
        add_universal(src->universal_flags,src->container);
        for (int n=0; n < src->num_entities; n++)
          add_entity(src->entities[n],src->container);
      }
      /* Adds elements from `src' to the current list of image entities,
         avoiding duplicates.  Both objects must have the same `container'
         if either has a non-NULL `container' member. */
    kdsx_image_entities *find_match(kdsx_image_entities *head,
                                    kdsx_image_entities * &goes_after_this);
      /* Looks on the list headed by `head' for an object identical to
         this one, returning a pointer to the object it finds.  If none
         is found, the function returns NULL, but `goes_after_this' points
         to the object immediately after which a copy of the present
         object should be added (NULL, if it should be added at the head). */
    void validate(kds_entity_container *cont);
      /* Generates an error if any of the `cont' differs from the `container'
         member or if any of the entities is incompatible with a non-NULL
         `container's parameters. */
    void reset()
      { num_entities = 0; universal_flags = 0; container=NULL; }
    void serialize(FILE *fp);
    void deserialize(FILE *fp, kdsx_entity_container *container);
  public: // Data
    int max_entities; // Size of the base object's `entities' array.
    int ref_id; // Used for serialization and deserialization
    kdsx_image_entities *next; // Used to build doubly linked lists
    kdsx_image_entities *prev;
  };

/*****************************************************************************/
/*                              kdsx_metagroup                               */
/*****************************************************************************/

// Extends `kds_metagroup' to include private book-keeping information.
struct kdsx_metagroup : public kds_metagroup {
  public: // Member functions
    kdsx_metagroup(kdu_servex *owner);
      /* Sets all member variables to 0 or NULL. */
    virtual ~kdsx_metagroup();
    void create(kdsx_metagroup *parent, kdsx_entity_container *container,
                kdsx_image_entities *parent_entities, jp2_input_box *box,
                int max_size, kdu_long &last_used_bin_id,
                int *num_codestreams, int *num_jpch, int *num_jplh,
                kdu_long fpos_lim, bool is_first_subbox_of_asoc);
      /* Initializes a newly constructed object to represent the supplied
         open box.  Each group created by this function holds a single
         top-level box or else a placeholder to a list of box groups
         representing its sub-boxes.  The `max_size' argument is used to
         decide whether or not a box should be replaced by a placeholder.
         If so, the `last_used_bin_id' argument is incremented and used for
         the new meta data-bin identifier.
            The `container' argument is non-NULL if `box' is found within
         a JPX container.
            The `num_codestreams' argument is used to keep track of the
         number of contiguous codestream boxes or fragment table boxes
         which have been seen so far.  This member will be NULL if
         `container' is non-NULL.
            Similarly, `num_jpch' and `num_jplh' keep track of the number
         of codestream headers and compositing layer header boxes which have
         been seen so far.  These members point to global counters if
         `container' is NULL; otherwise, they point to local counters
         that keep track of the number of JPCH and JPLH boxes that have
         been seen within the container.
            Whenever a contiguous codestream or fragment table is encountered,
         a new `kdsx_stream' object is created using `kdu_server::add_stream'.
            The `fpos_lim' argument provides an exclusive upper bound to
         file positions which can be considered part of the target.  The
         limit is applied when filling in group lengths and stream lengths. */
    void inherit_child_scope(kdsx_image_entities *entities,
                             kds_metagroup *child);
      /* This function is called after parsing descendants of a given
         metagroup to augment the scope of the current group so as to
         represent a union of all its children.  Normally, this is done
         after all sub-groups have been created so that the sub-groups
         inherit from their parents only new scope which is induced by
         information in the parent box itself.  However, for association
         boxes, the first sub-box's scope is added to the association
         box immediately before passing that scope into all the other
         sub-boxes.   Augmenting the current scope means growing regions
         of interest so as to include that of the child (if any), augmenting
         the list of associated image entities, and including all flags
         from the child except KDS_METAGROUP_LEAF and
         KDS_METAGROUP_INCLUDE_NEXT_SIBLING.  The image entities which will
         eventually form part of the current object's scope are recorded
         in the temporary `entities' object, rather than in the
         `scope->entities' member which will eventually be formed by
         invoking `kdu_servex::commit_image_entities' on the temporary
         `entities' object. */
    void serialize(FILE *fp);
      /* For saving the meta-data structure to a file.  Recursively serializes
         placeholders. */
    bool deserialize(kdsx_metagroup *parent, FILE *fp);
      /* Recursively deserializes placeholders.  Returns true if there are
         still more meta-data groups to be deserialized. Otherwise returns
         false.  Generates an error if parsing fails. */
  private: // Helper functions
    int synthesize_placeholder(kdu_long original_header_length,
                               kdu_long original_box_length,
                               int *stream_id);
      /* Allocates the `data' array and writes a placeholder box into it,
         returning the total length of the placeholder box.  The placeholder
         is to represent a box whose type code is in the `last_box_type'
         member and whose original header length and total length are as
         indicated.  If the original box type is a code-stream, an
         incremental code-stream placeholder box is written, using
         *`stream_id' for the code-stream identifier -- in other cases,
         `stream_id' is permitted to be NULL.  For non-codestream
         placeholders, the data-bin ID is found in the `phld_bin_id'
         member. */
  public: // Data
    kdu_servex *owner;
    kdsx_metagroup *parent;
    kds_metascope scope_data; // Give each group its own scope record
    kdu_byte *data; // Non-NULL only if boxes do not exist in file.
  };

/*****************************************************************************/
/*                            kdsx_stream_suminfo                            */
/*****************************************************************************/

struct kdsx_stream_suminfo {
  public: // Functions
    kdsx_stream_suminfo()
      { 
        max_discard_levels=max_quality_layers=0;
        num_components=num_output_components=0;
        component_subs = output_component_subs = NULL;
      }
    ~kdsx_stream_suminfo()
      { 
        if (component_subs != NULL)
          delete[] component_subs;
        if (output_component_subs != NULL)
          delete[] output_component_subs;
      }
    void create(kdu_codestream cs);
    bool equals(const kdsx_stream_suminfo *src);
    void serialize(FILE *fp, kdu_servex *owner);
    void deserialize(FILE *fp, kdu_servex *owner);
  public: // Data
    kdu_dims image_dims;
    kdu_dims tile_partition;
    kdu_dims tile_indices;
    int max_discard_levels;
    int max_quality_layers;
    int num_components;
    int num_output_components;
    kdu_coords *component_subs;
    kdu_coords *output_component_subs;
  };
  /* Notes: this structure stores summary information concerning a
     single codestream that is likely to be replicated across all
     codestreams in a video stream. */

/*****************************************************************************/
/*                                kdsx_stream                                */
/*****************************************************************************/

class kdsx_stream : public kdu_compressed_source {
  public: // Member functions
    kdsx_stream()
      { 
        stream_id = -1;
        suminfo = local_suminfo=NULL; layer_stats_handle = NULL;
        num_layer_stats = 0; layer_log_slopes = NULL; layer_lengths = NULL;
        url_idx = 0; target_filename = NULL; expanded_filename = NULL;
        open_filename = NULL; start_pos = length = 0;
        num_attachments = 0; locked_for_access = false;
        open_file = NULL; next_open_file_user = prev_open_file_user = NULL;
        rel_pos = 0; next = NULL; per_client_cache = 0;
      }
    ~kdsx_stream()
      { 
        close();
        if (expanded_filename != NULL)
          delete[] expanded_filename;
        if (local_suminfo != NULL)
          delete local_suminfo;
        suminfo = local_suminfo = NULL;
        if (layer_stats_handle != NULL)
          { delete[] layer_stats_handle; layer_stats_handle = NULL; }
        layer_log_slopes = NULL; layer_lengths = NULL;
      }
    virtual bool close()
      { // Called when `num_attachments' goes to 0
        assert(!locked_for_access);
        if (codestream.exists())
          codestream.destroy();
        if (open_file != NULL)
          open_file->remove_user(this);
        assert(open_file == NULL);
        this->open_filename = NULL;
        return true;
      }
    void open(const char *parent_filename, kdu_servex *owner);
      /* Call this on first attachment.  Leaves `open_file' non-NULL and
         leaves the current object as a user (but not a locked user) of
         the `open_file' object. */
    virtual int get_capabilities()
      { return KDU_SOURCE_CAP_SEQUENTIAL | KDU_SOURCE_CAP_SEEKABLE; }
    virtual bool seek(kdu_long offset)
      { 
        assert(locked_for_access);
        if (offset > length) offset = length;
        if (offset < 0) offset = 0;
        rel_pos = offset;
        return true;
      }
    virtual kdu_long get_pos()
      { return rel_pos; }
    virtual int read(kdu_byte *buf, int num_bytes)
      { 
        assert(locked_for_access);
        if ((rel_pos+num_bytes) > length)
          num_bytes = (int)(length-rel_pos);
        if (num_bytes <= 0) return 0;
        kdu_long abs_pos = start_pos + rel_pos;
        if (abs_pos != open_file->open_pos)
          { kdu_fseek(open_file->fp,abs_pos); open_file->open_pos=abs_pos; }
        num_bytes = (int) fread(buf,1,(size_t) num_bytes,open_file->fp);
        rel_pos += num_bytes;
        open_file->open_pos += num_bytes;
        return num_bytes;
      }
    void serialize(FILE *fp, kdu_servex *owner);
      /* For saving the code-stream structure to a file. */
    void deserialize(FILE *fp, kdu_servex *owner);
      /* For recovering the saved code-stream structure from a file. */
  public: // Data members used to summarize the codestream's properties
    int stream_id;
    const kdsx_stream_suminfo *suminfo; // local_suminfo or global default
    kdsx_stream_suminfo *local_suminfo; // May be NULL
    int num_layer_stats; // Length of two arrays below
    int *layer_log_slopes;   // Pointers into `layer_stats_handle'
    kdu_long *layer_lengths;
    int *layer_stats_handle;
  public: // Data members used to access the codestream itself
    int url_idx; // 0 if main file, else index into data references box
    const char *target_filename; // Pointer to name of target file
    char *expanded_filename; // Non-NULL if `target_filename' is relative
    const char *open_filename; // Set by first and only call to `open'
    kdu_long start_pos; // Absolute location of the stream within the file
    kdu_long length; // Length of stream
    int num_attachments; // May close file and `codestream' when this goes to 0    
    bool locked_for_access;
    kdsx_open_file *open_file; // May be NULL, unless `locked_for_access' true
    kdsx_stream *next_open_file_user; // Builds `users' list within `open_file'
    kdsx_stream *prev_open_file_user; // `users' list is doubly-linked
    kdu_long rel_pos; // Position last read from file, relative to `start_pos'
    int per_client_cache;
    kdu_codestream codestream; // Exists only if `fp' non-NULL
    kdsx_stream *next; // To build a linked list of streams in the file
  };

/*****************************************************************************/
/*                           kdsx_stream_mapping                             */
/*****************************************************************************/

struct kdsx_stream_mapping {
  /* This object represents information which might be recovered from a
     codestream header box, which is required for mapping codestream
     contexts to codestreams and components. */
  public: // Member functions
    kdsx_stream_mapping()
      { num_channels = num_components = 0; component_indices = NULL; }
    ~kdsx_stream_mapping()
      { if (component_indices != NULL) delete[] component_indices; }
    void parse_ihdr_box(jp2_input_box *box);
      /* Parses to the end of the image header box leaving it open, using
         the information to set the `size' and `num_components' members. */
    void parse_cmap_box(jp2_input_box *box);
      /* Parses to the end of the component mapping box leaving it open,
         using the information to set the `num_channels' and
         `component_indices' members. */
    void finish_parsing(kdsx_stream_mapping *defaults);
      /* Fills in any information not already parsed, by using defaults
         (parsed from within the JP2 header box) as appropriate. */
    void serialize(FILE *fp);
    void deserialize(FILE *fp);
      /* The above functions save and reload only the `size' and
         `num_components' values.  The channel indices are required only
         during parsing, to compute the components which belong to each
         compositing layer. */
  public: // Data
    kdu_coords size;
    int num_components;
    int num_channels;
    int *component_indices; // Lists component indices of each channel
  };

/*****************************************************************************/
/*                           kdsx_layer_member                               */
/*****************************************************************************/

struct kdsx_layer_member {
  public: // Member functions
    kdsx_layer_member()
      { codestream_idx = num_components = 0; component_indices = NULL; }
    ~kdsx_layer_member()
      { if (component_indices != NULL) delete[] component_indices; }
  public: // Data
    int codestream_idx; // See below
    kdu_coords reg_subsampling;
    kdu_coords reg_offset;
    int num_components;
    int *component_indices;
  };
  /* If this object is embedded within the top-level `kdsx_context_mappings'
     object, `codestream_idx' is an absolute codestream index; otherwise,
     `codestream_idx' is equal to C + T-B, where C is the absolute codestream
     index, T is the number of top-level codestreams in the file (reported
     via `kdsx_entity_container::num_top_codestreams') and B is the absolute
     index of the first base codestream index in the associated JPX container
     (reported via `kdsx_entity_container::first_base_codestream').  This
     is exactly the way codestream indices found within codestream
     registration boxes are to be interpreted when they are embedded within
     a JPX container (Compositing Layer Extensions box). */

/*****************************************************************************/
/*                           kdsx_layer_mapping                              */
/*****************************************************************************/

struct kdsx_layer_mapping {
  public: // Member functions
    kdsx_layer_mapping()
      { 
        rel_layer_idx=-1;
        num_members = num_channels = num_colours = 0;
        have_opct_box = have_opacity_channel = false;
        members=NULL; channel_indices=NULL;
      }
    kdsx_layer_mapping(int rel_layer_idx)
      { /* Note: `rel_layer_idx' values start from 0 within the containing
           `kdsx_context_mappings' object.  For the top-level mappings object,
           `rel_layer_idx' values correspond to absolute compositing layer
           indices; otherwise, `rel_layer_idx' measures the ordinal position
           of this layer within the collection of base compositing layers for
           a JPX container. */
        this->rel_layer_idx = rel_layer_idx;
        num_members = num_channels = num_colours = 0;
        have_opct_box = have_opacity_channel = false;
        members=NULL; channel_indices=NULL;
      }
    ~kdsx_layer_mapping()
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
    void parse_opct_box(jp2_input_box *box);
      /* Parses the opacity box, leaving it open.  Sets `have_opct_box' to
         true.  Also sets `have_opacity_channel' to true if the box has
         an OTyp field equal to 0 or 1.  In this case, there will be no
         channel definition box, but `num_colours' must be augmented by 1
         to get the number of channels used. */
    void parse_colr_box(jp2_input_box *box);
      /* Parses to the end of the colour description box leaving it open,
         using the information to set the `num_colours' member if possible.
         Once any colour description box is encountered, the value of
         `num_colours' is set to a non-zero value -- either the actual
         number of colours (if the box can be parsed) or -1 (if the box
         cannot be parsed). */
    void finish_parsing(kdsx_layer_mapping *defaults,
                        kdsx_context_mappings *owner);
      /* Fills in uninitialized members with appropriate defaults and
         determines the `kdsx_layer_member::num_components' values and
         `kdsx_layer_member::component_indices' arrays, based on the
         available information. */
    void serialize(FILE *fp);
    void deserialize(FILE *fp, kdsx_context_mappings *owner);
      /* The above functions save and reload everything except for the
         channel indices, since these are used only while parsing, to
         determine the set of image components which are involved from
         each codestream which is used by each compositing layer. */
  public: // Data
    int rel_layer_idx;
    kdu_coords layer_size;
    kdu_coords reg_precision;
    int num_members;
    kdsx_layer_member *members;
    bool have_opct_box; // If opacity box was found in the layer header
    bool have_opacity_channel; // If true, `num_channels'=1+`num_colours'
    int num_colours; // Used to set `num_channels' if no `cdef' box exists
    int num_channels;
    int *channel_indices; // Sorted into increasing order
  };

/*****************************************************************************/
/*                          kdsx_comp_instruction                            */
/*****************************************************************************/

struct kdsx_comp_instruction {
  public: // Member functions
    kdsx_comp_instruction() { transpose=vflip=hflip=false; }
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
/*                          kdsx_context_mappings                            */
/*****************************************************************************/

class kdsx_context_mappings : public kdu_wincontext_mappings {
  public: // Member functions
    kdsx_context_mappings(kdsx_entity_container *container,
                          kdsx_context_mappings *top_mappings)
      { 
        this->container = container;  this->top_mappings = top_mappings;
        num_codestreams = max_codestreams = 0;
        num_compositing_layers = max_compositing_layers = 0;
        stream_refs = NULL;  layer_refs = NULL;
        finished_codestreams = finished_layers = false;
        num_comp_sets = max_comp_sets = 0;
        comp_set_starts = NULL;
        num_comp_instructions = max_comp_instructions = 0;
        comp_instructions = NULL;
      }
    ~kdsx_context_mappings()
      { 
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
    kdsx_stream_mapping *add_stream(int idx, bool container_relative);
      /* If the indicated codestream does not already have an allocated
         `kdsx_stream_mapping' object, one is created here.  In any event,
         the relevant object is returned.
            The `container_relative' argument has no impact unless this object
         belongs to a `kdsx_entity_container'.  In that case, if
         `container_relative' is true, `idx'=0 corresponds to the first
         base codestream defined by the JPX container; on the other hand,
         if `container_relative' is false, the first base codestream defined
         by the container corresponds to the value `idx' =
         `kdsx_entity_container::num_top_codestreams' and this is the smallest
         value allowed. */
    kdsx_layer_mapping *add_layer(int idx, bool is_relative);
      /* If the indicated compositing layer does not already have an allocated
         `kdsx_layer_mapping' object, one is created here.  In any event,
         the relevant object is returned.
            The `container_relative' argument has no impact unless this object
         belongs to a `kdsx_entity_container'.  In that case, if
         `container_relative' is true, `idx'=0 corresponds to the first
         base compositing layer defined by the JPX container; on the other
         hand, if `container_relative' is false, the first base layer defined
         by the container corresponds to the value `idx' =
         `kdsx_entity_container::num_top_layers' and this is the smallest
         value allowed. */
    kdsx_stream_mapping *get_stream_defaults() { return &stream_defaults; }
    kdsx_layer_mapping *get_layer_defaults() { return &layer_defaults; }
    void parse_copt_box(jp2_input_box *box);
      /* Parses to the end of the composition options box, leaving it open,
         and using the contents to set the `composited_size' member. */
    void parse_iset_box(jp2_input_box *box);
      /* Parses to the end of the composition instruction box, leaving it open,
         and using the contents to add a new composition instruction set to
         the `comp_set_starts' array and to parse its instructions into the
         `comp_instructions' array, adjusting the relevant counters. */
    void finish_parsing(int num_top_codestreams, int num_top_layers);
      /* The `num_top_codestreams' and `num_top_layers' arguments correspond
         to the number of top-level codestreams and compositing layers in the
         file.  Note that it is safe to call this function multiple times. */
    void serialize(FILE *fp);
    void deserialize(FILE *fp);
  private: // Helper functions
    friend struct kdsx_layer_mapping;
    friend class kdu_supp::kdu_servex;
    kdsx_stream_mapping *stream_lookup(int idx)
      { 
        assert(finished_codestreams);
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
    kdsx_entity_container *container;
    kdsx_context_mappings *top_mappings;
    kdsx_stream_mapping stream_defaults;
    kdsx_layer_mapping layer_defaults;
    int num_codestreams;
    int max_codestreams; // Size of following array
    kdsx_stream_mapping **stream_refs;
    int num_compositing_layers;
    int max_compositing_layers; // Size of following array
    kdsx_layer_mapping **layer_refs;
    bool finished_codestreams; // True if we have finished parsing codestreams
    bool finished_layers; // True if finished parsing compositing layers
    kdu_coords composited_size; // Valid only if `num_comp_sets' > 0
    int num_comp_sets; // Num valid elts in `comp_set_starts' array
    int max_comp_sets; // Max elts in `comp_set_starts' array
    int *comp_set_starts; // Index of first instruction in each comp set
    int num_comp_instructions; //  Num valid elts in `comp_instructions' array
    int max_comp_instructions; // Max elts in `comp_instructions' array
    kdsx_comp_instruction *comp_instructions; // Array of all instructions
  };
  
} // namespace kd_serve_local

#endif // SERVEX_LOCAL_H
