/*****************************************************************************/
// File: jpx_local.h [scope = APPS/JP2]
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
   Defines local classes used by the internal JPX file format processing
machinery implemented in "jpx.cpp".  The internal object classes defined here
generally parallel the externally visible interface object classes defined
in the compressed-io header file, "jpx.h".  You should respect the local
nature of this header file and not include it directly from an application
or any other part of the Kakadu system (not in the APPS/JP2 scope).
******************************************************************************/
#ifndef JPX_LOCAL_H
#define JPX_LOCAL_H

#include <assert.h>
#include "jpx.h"
#include "jp2_shared.h"

// Defined here:
namespace kdu_supp {
  struct jx_frame;
}
namespace kd_supp_local {
  struct jx_instruction;
  struct jx_numlist;
  struct jx_regions;
  struct jx_numlist_library;
  struct jx_numlist_cluster;
  struct jx_region_library;
  struct jx_region_cluster;
  class jx_metagroup_writer;
  struct jx_trapezoid;
  struct jx_scribble_converter;
  struct jx_path_filler;
  struct jx_metaref;
  struct jx_crossref;
  struct jx_metaread;
  struct jx_incomplete_metanode_walker;
  struct jx_metaparse;
  class jx_metagroup_writer;
  struct jx_metawrite;
  struct jx_metapres;
  struct jx_metanode;
  struct jx_metaloc;
  struct jx_metaloc_block;
  class jx_metaloc_manager;
  struct jx_meta_target_level;
  class jx_meta_target;
  struct jx_meta_manager;
  struct jx_frag;
  
  class jx_fragment_lst;
  class jx_fragment_list;
  class jx_compatibility;
  class jx_composition;
  class jx_container_base; // Inherited by `jx_container_source/target'
  class jx_registration;

  struct jx_track_source;
  class jx_container_source;
  class jx_codestream_source;
  class jx_layer_source;
  struct jx_multistream_source;
  class jx_stream_locator;
  class jx_source;

  class jx_codestream_target;
  class jx_layer_target;
  struct jx_track_target;
  class jx_container_target;
  struct jx_multistream_target;
  class jx_target;
}

namespace kd_supp_local {
  using namespace kdu_supp;

/*****************************************************************************/
/*                                  jx_frag                                  */
/*****************************************************************************/

struct jx_frag {
  kdu_long offset;
  kdu_long length;
  kdu_uint16 url_idx;
  jx_frag *next;
};

/*****************************************************************************/
/*              jx_fragment_lst     and     jx_fragment_list                 */
/*****************************************************************************/

#define JX_FRAGLIST_URL_LIST        0xFFFF
#define JX_FRAGLIST_URL_FTBL        0xFFFE
#define JX_FRAGLIST_URL_JP2C        0xFFFD
#define JX_FRAGLIST_URL_INCREMENTAL 0xFFFC
#define JX_FRAGLIST_URL_MAX         0xFFFB

class jx_fragment_lst {
  public: // Member functions
    void reset()
      { 
        if (url == JX_FRAGLIST_URL_LIST)
          while (frag_list != NULL)
            { jx_frag *frag=frag_list; frag_list=frag->next; delete frag; }
        frag_list=NULL; length_low=0; length_high=0; url=0;
      }
    bool is_empty() const
      { return ((length_low | (kdu_uint32) length_high) == 0); }
    bool init(jp2_input_box *flst_box, bool allow_errors=true);
      /* Initializes the object from the information recorded in a fragment
         list box.  If `allow_errors' is false, the function simply returns
         false should any problems be encountered reading the fragment list
         box.  Otherwise and error is generated through `kdu_error'.  In any
         case, the function returns true if successful. */
    kdu_long get_link_region(kdu_long &len) const
      { /* If the object represent a single region within the same file as
           the fragment list box itself, the function returns the location
           of the first byte in that region and sets `len' equal to the
           number of bytes in the region.  This is a useful pre-requisite for
           detecting links within the metadata structure.  Otherwise the
           function returns -1. */
        if ((url != 0) || ((length_low | (kdu_uint32) length_high) == 0))
          return -1;
        len = (kdu_long) length_low;
#ifdef KDU_LONG64
        len += ((kdu_long) length_high)<<32;
#endif
        return frag_pos;
      }
    void set_link_region(kdu_long pos, kdu_long len)
      { /* Sets the fragment list to contain a single fragment from the
           current file, starting at `pos' and covering `len' bytes.  Any
           pre-existing contents are erased.  Only the 48 LSB's of `len'
           are recorded. */
        reset();
        this->frag_pos = pos;
        length_low = (kdu_uint32) len;
#ifdef KDU_LONG64
        length_high = (kdu_uint16)(len>>32);
#endif
      }
    kdu_long get_ftbl_loc() const
      { /* Returns the location of an unparsed Fragment Table box, or -ve
           if that is not what this object represents. */
        return ((url != JX_FRAGLIST_URL_FTBL) || is_empty())?-1:box_pos;
      }
    void set_ftbl_loc(kdu_long pos)
      { /* Used to force the object to represent the location of an
           unparsed Fragment Table box. */
        assert(pos >= 0);
        reset();  this->url = JX_FRAGLIST_URL_FTBL;
        this->box_pos = pos;  this->length_low = 1;
      }
    bool parse_ftbl(jp2_input_box &box);
      /* Attempts to parse a Fragment Table from `box' to initialize the
         contents of the current object.  This function does not close
         `box'. */
    kdu_long get_jp2c_loc() const
      { /* Returns the location of a Contiguous Codestream box, or -ve
           if that is not what this object represents. */
        return ((url != JX_FRAGLIST_URL_JP2C) || is_empty())?-1:box_pos;
      }
    void set_jp2c_loc(kdu_long pos)
      { /* Used to force the object to represent the location of a
           Contiguous Codestream box. */
        assert(pos >= 0);
        reset();  this->url = JX_FRAGLIST_URL_JP2C;
        this->box_pos = pos;  this->length_low = 1;
      }
    kdu_long get_incremental_codestream(bool &main_header_complete) const
      { /* Returns a -ve value if this object does not represent a JPIP
           incremental codestream equivalent placeholder; otherwise, the
           logical codestream index is returned and `main_header_complete'
           is set to indicate whether or not the codestream's main header
           is believed to be available in the cache. */
        if ((url != JX_FRAGLIST_URL_INCREMENTAL) || is_empty()) return -1;
        main_header_complete = ((length_high != 0) || (length_low != 1));
        return codestream_idx;
      }
    void set_incremental_codestream(kdu_long stream_idx,
                                    bool main_header_complete)
      { /* Note: the logical `stream_idx' value must not be -ve. */
        assert(stream_idx >= 0);
        reset(); this->url = JX_FRAGLIST_URL_INCREMENTAL;
        this->codestream_idx = stream_idx;
        this->length_low = (main_header_complete)?2:1;
      }
    int count_box_frags() const;
      /* Returns the total number of fragments required to represent the
         box.  Returns 0 if the box is empty or has a representation
         that cannot arise from or be saved to a JPX fragment list box. */
    int calculate_box_length() const
      { int num_frags = this->count_box_frags();
        return (num_frags==0)?0:(8+2+14*num_frags); }
      /* Returns the number of bytes required to represent this fragment
         list in a call to `save_box', including the box header. */
    void save_box(jp2_output_box *super_box, int flst_len=0);
      /* Writes the fragment list box within the supplied `super_box',
         even if it is empty.  If `flst_len' is non-zero, the generated
         fragment-list box is forced to have exactly this length, including
         the box header; in this case, `flst_len' must be a valid return
         value for the `calculate_box_length' function. */
    void finalize(jp2_data_references data_references);
      /* Uses the supplied object to verify that all data references from
         fragments in the fragment list point to valid URL's. */
  protected: // data
    friend class kdu_supp::jpx_fragment_list;
    union {
      jx_frag *frag_list;
      kdu_long frag_pos;
      kdu_long box_pos;
      kdu_long codestream_idx;
    };
    kdu_uint32 length_low;
    kdu_uint16 length_high;
    kdu_uint16 url;  // See below   
  };
  /* Notes:
       This object is designed to occupy as little space as possible when
     there is only one fragment (by far the most common case).  Moreover,
     the object is designed also to be able to identify codestreams that
     have been replaced by a JPIP incremental codestream equivalent
     placeholder.  Let LEN be the 48-bit unsigned quantity given by
     `length_low' + 2^32*`length_high'.  Then the following cases apply:
     -- If LEN=0, the object is considered empty, regardless of the
        other member values.
     -- If `url' lies in the range 0 to 0xFFFB (`JX_FRAGLIST_URL_MAX'), the
        object holds a single fragment, of length LEN, starting at location
        `frag_pos', within the file identified by `url' (0 means the main
        JPX file; other values are interpreted via the data-references box).
     -- If `url' = 0xFFFF (`JX_FRAGLIST_URL_LIST'), the `frag_list' member
        points to a list of fragments and LEN holds the total number of bytes
        covered by all fragments.
     -- If `url' = 0xFFFE (`JX_FRAGLIST_URL_FTBL'), the `box_pos' member
        holds the address of a Fragment Table Box that may still need to be
        parsed and LEN should be equal to 1.
     -- If `url' = 0xFFFD (`JX_FRAGLIST_URL_JP2C'), the `box_pos' member
        holds the address of a Contiguous Codestream Box that may still
        need to be parsed and LEN should be equal to 1.
     -- If `url' = 0xFFFC (`JX_FRAGLIST_URL_INCREMENTAL'), the object
        represents a JPIP incremental codestream equivalent placeholder, with
        logical codestream index `codestream_idx', and LEN should be equal
        to 1 or 2.  If LEN=1, the codestream's main header is not yet known
        to be complete, while if LEN > 1, the main header has been found to
        be fully available in the cache.
     Note that the `JX_FRAGLIST_URL_FTBL', `JX_FRAGLIST_URL_JP2C' and
     `JX_FRAGLIST_URL_INCREMENTAL' values cannot be set directly by an
     application and do not actually correspond to representations of
     Fragment List boxes.  Instead, these values are used in the
     implementation of `jx_source' to keep track of codestreams that have
     been "discovered" through encountering FTBL or JP2C boxes at the
     top-level of the file or within Multiple Codestream boxes.  If an
     FTBL box is encountered, it is kept in the `JX_FRAGLIST_URL_FTBL'
     form until it can be fully parsed (only relevant for JPIP services
     which send FTBL boxes directly).  If an incremental codestream equivalent
     placeholder box is encountered in any of the contexts in which an
     FTBL or JP2C box might be encountered, it is recorded using the
     `JX_FRAGLIST_URL_INCREMENTAL' code.
  */

class jx_fragment_list : public jx_fragment_lst {
  public: // Member functions
    jx_fragment_list()
      { frag_list = NULL; length_low=0; length_high=0; url=0; }
    ~jx_fragment_list() { reset(); }
  };
  /* Notes:
       This class has constructors and destructors; that is the only
     difference.  The `jx_fragment_lst' class is only used in arrays that
     zeroed out using memset and whose elements are destroyed by invoking
     `reset'. */

/*****************************************************************************/
/*                             jx_compatibility                              */
/*****************************************************************************/

class jx_compatibility {
  public: // Member functions
    jx_compatibility()
      { 
        is_jp2 = false; is_jpxb_compatible = is_jp2_compatible = true;
        have_rreq_box = true; // By default, we will write one
        no_extensions = true; no_opacity = true; no_fragments = true;
        no_scaling = true; has_pixel_format = false;
        single_stream_layers = true;
        max_standard_features=num_standard_features=0; standard_features=NULL;
        max_vendor_features=num_vendor_features=0; vendor_features=NULL;
        for (int i=0; i < 8; i++)
          fully_understand_mask[i] = decode_completely_mask[i] = 0;
      }
    ~jx_compatibility()
      {
        if (standard_features != NULL) delete[] standard_features;
        if (vendor_features != NULL) delete[] vendor_features;
      }
    void copy_from(jx_compatibility *src);
    bool init_ftyp(jp2_input_box *ftyp_box);
      /* Parses the file-type box.  Returns false, if not compatible with
         either JP2 or JPX.  Resets `have_rreq_box' to false. */
    void init_rreq(jp2_input_box *rreq_box);
      /* Parse reader requirements box, setting `have_rreq_box' to true. */
    void set_not_jp2_compatible() { is_jp2_compatible = false; }
      /* Called while checking compatibility information prior to writing a
         JPX header, if any reason is found to believe that the file will
         not be JP2 compatible.  The file will, by default, be marked as
         JP2 compatible. */
    void set_not_jpxb_compatible() { is_jpxb_compatible = false; }
      /* Called while checking compatibility information prior to writing a
         JPX header, if any reason is found to believe that the file will
         not be compatible with JPX  baseline.  The file will, by default, be
         marked as JP2 compatible. */
    void have_non_part1_codestream() { no_extensions = false; }
    void add_standard_feature(kdu_uint16 feature_id, bool add_to_both=true);
      /* Used by `jpx_target' to add all features which it discovers
         automatically.  Unless a "fully understand" sub-expression for the
         feature already exists, the function adds a new sub-expression for
         just this feature, marking it as required to fully understand the
         file.  If `add_to_both' is true, the same thing is done for
         "decode completely" expressions. */
    void save_boxes(jx_target *owner);
      /* Writes the file-type and reader requirements boxes.  The
         `owner->open_top_box' function is used to open the necessary boxes. */
  
    bool check_pixel_format_features()
      { return has_pixel_format; } 
      /* Returns true if any of the pixel-format related standard feature codes
         were parsed by `init_rreq' or if any of these codes have been set
         via `add_standard_feature'.  Calls to
         `jpx_compatibility::set_used_standard_feature' do not directly affect
         the value returned here, since callers of this function are only
         interested in whether or not a pixel format (pxfm) box might appear
         in the file, not what features the application might have
         decided to add to the pool that are not directly connected with
         written boxes. */

  private: // Internal structures
      struct jx_feature {
          jx_feature() { memset(this,0,sizeof(*this)); }
          kdu_uint16 feature_id;
          bool supported; // Feature supported by application
          kdu_uint32 fully_understand[8]; // Temporary holding area for writing
          kdu_uint32 decode_completely[8];// Temporary holding area for writing
          kdu_uint32 mask[8]; // Holds the sub-expression masks
        };
      struct jx_vendor_feature {
          jx_vendor_feature() { memset(this,0,sizeof(*this)); }
          kdu_byte uuid[16];
          bool supported; // Feature supported by application
          kdu_uint32 fully_understand[8]; // Temporary holding area for writing
          kdu_uint32 decode_completely[8];// Temporary holding area for writing
          kdu_uint32 mask[8]; // Holds the sub-expression masks
        };
  private: // Data
    friend class kdu_supp::jpx_compatibility;
    bool is_jp2, is_jp2_compatible, is_jpxb_compatible, have_rreq_box;
    bool no_extensions, no_opacity, no_fragments, no_scaling, has_pixel_format;
    bool single_stream_layers;
    int max_standard_features;
    int num_standard_features;
    jx_feature *standard_features;
    int max_vendor_features;
    int num_vendor_features;
    kdu_uint32 fully_understand_mask[8]; // Mask of used sub-expression indices
    kdu_uint32 decode_completely_mask[8];// Mask of used sub-expression indices
    jx_vendor_feature *vendor_features;
    jp2_output_box out; // Stable resource for use with `open_top_box'
  };

/*****************************************************************************/
/*                              jx_pending_box                               */
/*****************************************************************************/

struct jx_pending_box {
    jx_pending_box() { next = NULL; }
    jp2_input_box box;
    jx_pending_box *next;
  };
  /* Note:
       This structure is used to build lists of sub-boxes that have been
     encountered within some context, but which are not yet complete (must
     be due to insufficient contents in a dynamic cache).  Storing the
     boxes temporarily allows further boxes in the main context to be
     discovered and usefully exploited, rather than waiting for each
     sub-box in turn to become complete.
  */

/*****************************************************************************/
/*                              jx_instruction                               */
/*****************************************************************************/

struct jx_instruction {
  public: // Member functions
    jx_instruction()
      {
        layer_idx=increment=next_reuse=0; iset_idx = inum_idx = -1;
        visible=first_use=false; next=prev=NULL;
      }
  public: // Data
    int layer_idx; // Note: these are relative layer indices
    int increment; // Amount to add to `layer_idx' when repeating frame
    int next_reuse; // If non-zero, `increment' must be 0
    bool visible; // If false, the instruction serves only as a placeholder
    bool first_use; // Used by `save_box': marks first instruction to use layer
    int iset_idx; // Index of `inst' box from which instruction was parsed
    int inum_idx; // Index of the instruction within the `inst' box.
    kdu_dims source_dims;
    kdu_dims target_dims;
    jpx_composited_orientation orientation;
    jx_instruction *next; // Next instruction in doubly linked list
    jx_instruction *prev; // Previous instruction in doubly linked list
  };
  /* Notes:
          While reading a composition box, we first recover the `next_use' info
       and then use this to deduce `layer_idx' and `increment'.  While writing,
       we first create the `layer_idx' and `increment' fields and use this to
       deduce `next_use'.
          The `iset_idx' value represents the ordinal position of the
       Instruction Set box from which this instruction was parsed, measured
       relative to the start of the Composition box or the start of the
       JPX container (Compositing Layer Extensions box) within which the
       Instruction Set box was encountered. */

} // Close namespace kd_supp_local tempporarily
namespace kdu_supp {
  
/*****************************************************************************/
/*                                 jx_frame                                  */
/*****************************************************************************/

struct jx_frame {
  public: // Member functions
    jx_frame(kd_supp_local::jx_composition *comp)
      { 
        this->owner=comp;  start_time = 0; frame_idx = 0;
        min_layer_idx=max_initial_layer_idx=max_repd_layer_idx=-1;
        total_instructions=0; duration = 0;
        num_instructions=repeat_count=increment=0;
        pause=persistent=false; head=tail=NULL;
        last_persistent_frame=next=prev=NULL;
      }
    ~jx_frame()
      { reset(); }
    void reset()
      {
        num_instructions = 0;
        while ((tail=head) != NULL)
          { head = tail->next; delete tail; }
      }
    kd_supp_local::jx_instruction *add_instruction(bool is_visible)
      {
        num_instructions++;
        if (tail == NULL)
          head = tail = new kd_supp_local::jx_instruction;
        else
          {
            tail->next = new kd_supp_local::jx_instruction;
            tail->next->prev = tail;
            tail = tail->next;
          }
        tail->visible = is_visible;
        return tail;
      }
  public: // Data members used for reading and writing
    kd_supp_local::jx_composition *owner;
    kdu_long duration; // Milliseconds for a single repetition
    int repeat_count; // 0 if frame is played once
    int increment; // Common increment for all not-reused layers
    int num_instructions; // Number of instructions in this frame alone
    bool pause; // If last instruction has `LIFE'=0x7FFFFFFF -- see below
    bool persistent; // If the last instruction in the frame is persistent
    kd_supp_local::jx_instruction *head, *tail; // Instructions for this frame
                                                // alone
    jx_frame *last_persistent_frame; // Backward chain of persistent frames
    jx_frame *next; // Next frame in doubly linked list
    jx_frame *prev; // Previous frame in doubly linked list
  public: // Data members used only during reading
    kdu_long start_time; // Absolute start time for this frame, in milliseconds
    int frame_idx; // Absolute frame index for the first repetition
    int min_layer_idx; // Smallest `layer_idx' value in any instruction
    int max_initial_layer_idx; // Highest `layer_idx' without repeats
    int max_repd_layer_idx; // Highest `layer_idx' subject to increments
    int total_instructions; // Includes all previous persistent frames
  };
  /* Notes:
        If the last instruction in a frame has `LIFE'=0x7FFFFFFF, the
     `pause' flag is set and `duration' is forced to 0.  Otherwise, the
     `LIFE' value from the last instruction in a frame is multipled by
     the tick count to set `duration'.
        The `min_layer_idx', `max_initial_layer_idx' and `max_repd_layer_idx'
     members are used only during file reading, so as to accelerate the
     counting and locating of relevant frames.  They are initialized inside
     `summarize_and_remove_invisible_instructions'.  The first two hold
     minimum and maximum values of `layer_idx' values required for the first
     (not repeated) instance of the `jx_frame' object.  The third value
     holds -1 if the frame contains no incremented layers; otherwise it
     holds the maximum value of any `layer_idx' value that is subject to
     increments during repetition. */

} // Close namespace kdu_supp
namespace kd_supp_local { // Back to the local namespace
  
/*****************************************************************************/
/*                              jx_composition                               */
/*****************************************************************************/

class jx_composition {
  public: // Member functions
    jx_composition()
      { 
        source=NULL; container_source=NULL;
        is_complete=finish_in_progress=false;
        pending_isets=last_pending_iset=NULL;
        max_lookahead=last_frame_max_lookahead=0; loop_count=1;
        head=tail=last_persistent_frame=NULL; num_parsed_iset_boxes = 0;
        total_frames = 0;  first_frame_idx = 0;
        total_duration = 0;  start_time = 0;
        abs_layer_start=0; abs_layer_reps=1; abs_layer_rep_stride=0;
        abs_layers_per_rep=0; // Means that num layers not yet known
        last_in_track=this; max_tracks_noted=0;
        track_idx=0; track_next = next_in_track = prev_in_track = NULL;
      }
    ~jx_composition()
      { 
        while ((last_pending_iset=pending_isets) != NULL)
          { pending_isets=last_pending_iset->next; delete last_pending_iset; }
        while ((tail=head) != NULL)
          { head = tail->next; delete tail; }
      }
    void init_for_reading(jx_source *owner, jx_container_source *container,
                          int num_frames, kdu_uint32 life_start)
      { /* This function must be called before any call to `finish',
           `donate_composition_box' or `donate_instruction_box'.  If
           the object belongs to a JPX container, `container' will be
           non-NULL, and `total_frames' and `life_start' supply the
           values recorded in the container's INFO box.  Otherwise,
           the last three arguments are all zero. */
        assert((this->source==NULL) && (this->container_source == NULL));
        this->source = owner;  this->container_source = container;
        if (container != NULL)
          { this->total_frames = num_frames;
            this->start_time = (kdu_long) life_start; }
      }
    void donate_composition_box(jp2_input_box &comp_box);
      /* Donates the composition box when it is encountered at the top
         level of a JPX data source, so that the internal machinery can
         take over the parsing of this box, which might not be commpleted
         until a later point (see `finish').  This function is called
         automatically from within `owner->parse_next_top_level_box'. */
    void donate_instruction_box(jp2_input_box &iset_box, int iset_idx);
      /* This second `donate_...' function is called from within
         `jx_container_source::finish' to pass each discovered instruction
         box to the current object, where it is either parsed immediately
         or stored on an internal list of instructions that need to be
         parsed later.  Unlike `donate_composition_box', this function does
         not invoke `finish' itself. */
    bool finish();
      /* If the object has not yet been completely parsed, this function
         attempts to parse it.
            If this object represents the top-level Composition box, the
         `container_source' member will be NULL and the function uses the
         `comp_box' donated via `donate_composition_box', invoking
         `owner->parse_next_top_level_box' if necessary until a `comp_box'
         is available.
            If this object belongs to a presentation track within a JPX
         container, the `container_source' member will be non-NULL and
         the function uses the instruction set boxes donated via
         `donate_instruction_box', invoking `container_source->finish' to
         obtain more such boxes if the `abs_layer_rep_stride' member is
         still 0.
            The function returns true if the object has been completely
         parsed. */
    bool need_more_instructions(int min_frame_idx, int max_frame_idx);
      /* This function is used only by `jpx_source::generate_metareq' and is
         invoked only on the top-level `jx_composition' object managed by
         `jx_source'.  The function notionally scans through all tracks to
         determine whether any compositing instructions are missing that
         might be required to open `jpx_frame' interfaces to frames with
         indices in the indicated range (range is open ended if `max_frame_idx'
         is -ve).  If any instructions are missing, the function returns
         true. */
    void set_layer_mapping(int layer_start, int num_repetitions,
                           int layers_per_rep, int rep_stride)
      { 
        assert(rep_stride >= layers_per_rep);
        abs_layer_start=layer_start;  abs_layer_reps=num_repetitions;
        abs_layers_per_rep=layers_per_rep; abs_layer_rep_stride=rep_stride;
        if ((source != NULL) && is_complete && (total_frames == 0))
          { assert(num_repetitions == 1);
            total_frames = count_frames(layers_per_rep); }
      }
      /* This function is used to set up the parameters required to convert
         relative compositing layers to absolute compositing layers.  If you
         never call this function, `layer_start' is treated as 0,
         `num_repetitions' is treated as 1 and `layers_per_rep' is treated as
         infinite.  If the function is called with `layer_start'=0, the
         `num_repetitions' value should be 1 and `layers_per_rep' should be
         the total number of top-level compositing layers in the file.
         [//]
         If the function is called with `layers_start' > 0, it is describing
         a presentation track.  In that case, it is possible for
         `num_repetitions' to be 0 (unknown repetitions) in which case
         there may be no more containers and this function may be called
         again in the future, as repetitions of the container are
         incrementally discovered through the appearance of contiguous
         codestream boxes or fragment table boxes (this only happens during
         file reading).
         [//]
         The information supplied by this function is used by the
         `jpx_composition::map_rel_layer_idx' function.  Also,
         during file generation, this function must be invoked prior to
         `finalize'.
         [//]
         During file reading, if `container_source' is non-NULL, this function
         should be called once all Instruction Set boxes have
         been passed to `donate_instruction_box' -- calls to `finish' will
         not succeed until this happens.  If `container_source' is NULL, the
         function should be called as soon as the number of top-level
         compositing layers is known. */
    void finalize();
      /* You must call this before `save_box', `save_instructions' or
         `adjust_compatibility'.  Moreover, before calling this function,
         you must invoke `set_layer_mapping' -- even for the top-level
         composition box.  This function expands all repeating
         frames out into non-repeated frames, out to the limit of the
         available set of compositing layers -- cannot be too unreasonable.
         It then introduces invisible layers as required to ensure that the
         expanded frames can be represented using only the `next_use'
         links.  When the frames are written by `save_box', we look for
         repeated instruction sets to collapse the representation back down
         again. */
    void adjust_compatibility(jx_compatibility *compatibility);
      /* Called before writing a composition box, this function
         gives the object an opportunity to add to the list of features
         which will be included in the reader requirements box. */
    void save_box(jx_target *owner);
      /* This function writes the top-level composition box. */
    void save_instructions(jp2_output_box *super_box);
      /* This function writes all compositing instructions for the object
         to the supplied `box'.  It is used in the implementation of
         `save_box', but also to directly write compositing instructions
         for presentation tracks within their JPX container.  You should
         not invoke this function before calling `finalize'. */
    bool is_empty() { return (head == NULL); }
    int get_total_frames() { return total_frames; }
      /* Returns the total number of frames represented by the object,
         counting repetitions.  When generating a JPX file, the value
         returned by this function will be 0 until `finalize' has been
         called; it will also be zero if this object describes a presentation
         track within a final JPX container, that was created by calling
         `jpx_target::add_container' with a `repetition_factor' equal to 0. */
    kdu_long get_duration() { return total_duration; }
      /* Returns the total number of miliseconds spanned by all frames
         represented by this object.  When generating a JPX file, the
         value returned by this function will be 0 until `finalize' has
         been called; it will also be zero if this object describes a
         presentation track within a final JPX container, that was created
         by calling `jpx_target::add_container' with a `repetition_factor'
         equal to 0. */
  private: // Helper function
    void parse_iset_box(jp2_input_box &box);
      /* This function is called only if `box.is_complete' returns true. */
    void add_frame();
      /* Adds a new frame to the list, setting the `last_persistent_frame'
         pointers appropriately. */
    bool parse_instruction(bool have_target_pos, bool have_target_size,
                           bool have_life_persist, bool have_source_region,
                           bool have_orientation, kdu_uint32 tick,
                           jp2_input_box &box);
      /* Called from within `finish', this function parses a single
         instruction within the instruction set box, returning true if it
         found one and false if the instruction set box is complete.  The
         function draws its data from the `sub_in' member object, but does
         not close that box once it is complete.  The five flags reflect
         the information contained in the `ityp' field of the instruction
         set box. */
    void assign_layer_indices();
      /* This function is called from within `finish' once an initial set
         of frames has been created.  It uses the recorded `next_reuse'
         fields, walking through the sequence of frames and instructions
         to assign actual compositing layer indices to each instruction. */
    void process_instructions();
      /* Called from within `finish' after `assign_layer_indices' has been
         called, this function removes all the instructions which are marked
         as invisible; these were kept after parsing the composition box
         only so as to allow the layer indices to be properly recovered.
            The function also assigns the `start_time', `frame_idx',
         `min_layer_idx', `max_initial_layer_idx' and `max_repd_layer_idx'
         members of each `jx_frame' object.
            For each frame that has no `last_persistent_frame', that member
         is modified to the last persistent frame in the top-level
         composition box, except where this is the top-level composition
         object.
            Finally, the function fills in the `total_instructions' member for
         each frame by adding `num_instructions' to the `total_instructions'
         in any `last_persistent_frame'. */
    void propagate_frame_and_track_info();
      /* This function can be efficiently called at any time; if nothing
         needs to be done, the function will return immediately.  The function
         uses two state variables: `last_in_track' and `max_tracks_noted'.
         If `last_in_track' turns out to have a non-NULL `next_in_track'
         member, or `num_tracks_noted' turns out to be smaller than the
         value returned by `last_in_track->container_source->get_max_tracks',
         the machinery makes sure that it visits all `jx_composition' objects
         that have not previously been visited, initializing their `size'
         and `first_frame_idx' members.  The function should not be called
         unless `total_frames' is non-zero and either `container_source' is
         NULL or `first_frame_idx' is strictly positive. */
    int count_frames(int known_layers, kdu_long max_duration=KDU_LONG_HUGE);
      /* Used during reading, this function attempts to count the total
         number of frames provided by this `jx_composition' object, such
         that the maximum relative compositing layer used is strictly less
         than `known_layers' and the frame end time (relative to the start
         of this object) is less than or equal to `max_duration'.
            The function assumes that all compositing layers required to
         form the first frame declared by the object exist, even if the
         value of `known_layers' does not confirm this.  That way, every
         composition is believed to yield at least one frame -- the only
         way this function can return 0 is if there are no compositing
         instructions at all, or if `max_duration' is too small to
         accommodate even the first frame.
            If `known_layers' and `max_duration' turn out to be large enough
         to span all repetitions of all compositing instructions in the object,
         the function also sets the `total_frames' member (regardless of
         whether or not it was already set); however, the caller may also
         choose to use the result to set `total_frames', if the `known_layers'
         value is known to represent all compositing layers that will ever be
         available.
            If the `total_frames' member is already non-zero, this function
         does not count more frames than are recorded in `total_frames'.
            An important side effect of this function is that it always
         updates `total_duration' to reflect the maximum number of
         milliseconds that are known to be spanned by the current object's
         compositing instructions, being careful to never let this value
         decrease.
            The function plays an important role in establishing the number
         of frames that belong to the top-level Composition box, as well as
         the number of frames that are associated with each presentation
         track of a terminal JPX container that leaves the frame count value
         open-ended.  The function is also used to implement
         `jpx_composition::count_track_time' and
         `jpx_composition::count_track_frames_before_time'. */
    jx_frame *find_frame(int frame_idx, int known_layers, int &repeat_idx);
      /* Used with parsed content.  This function scans the frame list for
         this object alone, looking for the indicated frame (measured relative
         to the start of this `jx_composition' object's frames).  If the
         required frame does not exist or would require access to
         compositing layers with relative indices >= `known_layers', the
         function returns NULL, except that all compositing layers required
         to form the first frame of the composition are believed to exist
         (either now or in the future) so `known_layers' is disregarded
         if `frame_idx'=0.  If possible, the function returns the relevant
         `jx_frame' object and the specific repetition of this object that
         corresponds to `frame_idx'. */
    jx_frame *
      find_match(const int layers[], int num_layers,
                 int &repeat_idx, int &instruction_idx,
                 jx_frame *start_frm, int start_rept,
                 int max_container_rep, bool match_all);
      /* Used with parsed content.  This function scans the frame list for
         this object, starting from the element identified by `start_frm',
         looking for the first frame that uses any of the compositing layers
         whose absolute indices are found in the `layers' array.  If
         `start_frm' is non-NULL, the function starts ignores instances of
         `start_frm' with repeat indices smaller than `start_rept'.
            If `max_container_rep' > 0 and this object belongs to a JPX
         container, the behaviour of the function is modified slightly in
         that layer indices that correspond to container base layer indices
         can be re-interpreted as the compositing layer indices that are
         induced in any repetition of the container from 0 up to and including
         `max_container_rep'.
            If `match_all' is true, the function returns non-NULL only if it
         finds a frame whose instructions use all compositing layers in the
         `layers' array.  Otherwise, the function returns non-NULL if any
         match is found.  In either case, if the function returns a non-NULL
         `jx_frame' pointer, it also sets `repeat_idx' to the repetition
         index (if any) which generated the match, and `instruction_idx' to
         the index of the instruction (measured relative to the start of
         the frame) that produces the match. */
    jx_frame *
      reverse_match(const int layers[], int num_layers,
                    int &repeat_idx, int &instruction_idx,
                    jx_frame *start_frm, int start_rept,
                    int max_container_rep, bool match_all);
      /* Similar to `find_match', except this function walks backwards through
         the animation frames.  If `start_frm' is NULL, the function starts
         from the last frame which could be a match, which is determined from
         `max_container_rep' if non-NULL, or else from the largest compositing
         layer index in the `layers' array. */
  private: // Data
    friend class kdu_supp::jpx_composition;
    friend class kdu_supp::jpx_frame;
    jx_source *source; // NULL unless we are reading from a JPX file
    jx_container_source *container_source; // Used only when reading
    bool is_complete; // If `finish' has previously returned true.
    bool finish_in_progress; // Avoids recursive calls to `finish'
    jp2_output_box comp_out; // For use with `jx_target::open_top_box'
    jp2_input_box comp_in;
    jp2_input_box sub;
    jx_pending_box *pending_isets; // These manage donated instruction set
    jx_pending_box *last_pending_iset; // boxes until `finish' is called.
    int num_parsed_iset_boxes;
    int loop_count; // Global repeat count.
    kdu_coords size; // Size of compositing surface
    jx_frame *head, *tail; // Head and tail of frame list
    jx_frame *last_persistent_frame;
    int first_frame_idx; // 0 except if there is a `container_source'
    int total_frames; // Num frames represented by this object; 0 if unknown
    kdu_long start_time; // 0 except if there is a `container_source'
    kdu_long total_duration; // Duration of all `total_frames'; 0 if unknown
    int max_lookahead, last_frame_max_lookahead; // See below
    int abs_layer_start;       // These describe the absolute indices of the
    int abs_layer_reps;        // set of compositing layers that this object
    int abs_layers_per_rep;    // describes.  `rep_stride' is the amount
    int abs_layer_rep_stride;  // added to `abs_layer_start' between reps.
    jx_composition *last_in_track; // See below
    kdu_uint32 max_tracks_noted; // See below
  public: // Links to be filled in when creating presentation tracks
    kdu_uint32 track_idx; // Starts from 1, except for top-level object
    jx_composition *track_next; // Next track, if any, in same container
    jx_composition *next_in_track; // Corresponding track in next container
    jx_composition *prev_in_track; // Corresponding track in previous
                                   // container, or top-level composition box
  };
  /* Notes:
        `max_lookahead' and `last_frame_max_lookahead' are used only while
     parsing a `composition' box to determine when we can convert repeating
     instruction set boxes into repeating `jx_frame' objects.  It represents
     the distance between the most recently parsed instruction and the
     latest instruction to which any parsed instruction's `next_use' field
     refers.
        If `next_in_track' is NULL, there are no more containers with
     presentation tracks.  Otherwise `next_in_track' points to the same track
     (or the last one, if there are not enough) in the next container that
     has presentation tracks.  Similarly, `prev_in_track' is NULL only for
     the top-level Composition box; otherwise it points to the composition
     object associated with the same track (or the last one, if there are
     not enough) in the previous container that has presentation tracks or
     to the top-level `jx_composition' object.  These members are filled in
     during JPX container finalization.
        The `last_in_track' member points to the last known object in the
     same track to which this one belongs.  This member is updated by
     following the `next_in_track' and, if necessary, `track_next' links,
     as far as possible.  These members are only advanced when they are
     actually needed and the need is experienced primarily within the
     top-level composition object.  The `max_tracks_noted' member is used
     in conjunction with `last_in_track' to determine whether or not the
     `propagate_frame_and_track_info' function may have missed some newly
     discovered tracks.
  */

/*****************************************************************************/
/*                               jx_numlist                                  */
/*****************************************************************************/

#define JX_LIBRARY_C_NLL 0
#define JX_LIBRARY_C_NLC 1
#define JX_LIBRARY_NLL   2
#define JX_LIBRARY_NLC   3
#define JX_LIBRARY_NLR   4
#define JX_LIBRARY_NL_COUNT 5
  /* Note: it is particularly important that the C_NLL and C_NLC library
     indices appear first; otherwise, assumptions made within
     `jx_numlist_library::enumerate_matches' and
     `jpx_meta_manager::enumerate_matches' may be violated. */

struct jx_numlist {
  public: // Member functions
    jx_numlist(jx_metanode *owner, jx_container_base *cont)
      { 
        memset(this,0,sizeof(*this)); this->metanode=owner;
        this->container = cont;
        this->first_identical = this; // Make sure it is always connected
      }
    ~jx_numlist();
    bool equals(const jx_numlist *rhs) const;
      /* Returns true if the two numlists are identical. */
    void add_codestream(int idx, bool relative_to_container);
      /* This function is used both when parsing a number list box and when
         adding a new number list via `jx_metanode::add_numlist'.  It is
         responsible for ensuring that the `codestream_indices' array is
         ordered and for updating the `max_codestream_idx', `num_codestreams'
         and `non_base_codestreams' members.  If the `container' member is
         non-NULL, the behaviour of the function is modified as follows:
         -- If `relative_to_container' is true, the `container' is used to
            validate and remap `idx' values to ensure that they correspond
            either to top-level codestreams or container base codestreams.
         -- If `relatve_to_container' is false, the `container' is used to
            force all container-defined codestream references to reference
            the relevant base codestreams for the container. */
    void add_compositing_layer(int idx, bool relative_to_container);
      /* As above, but for compositing layer indices. */
    int *extract_codestreams(int &num, int *tbuf)
      { /* Removes all record of any codestream indices from the current
           object, setting `num' to the previous value of `num_codestreams',
           and returning the array of codestream indices, for which the
           caller will henceforth be responsible.  If `codestream_indices'
           previously pointed to the internal `max_codestream_idx' member,
           the value of `max_codestream_idx' is transferred to `tbuf[0]' and
           the function returns `tbuf'.  In any event, the caller is
           responsible for deleting the returned array unless it is NULL or
           points to `tbuf'.  The purpose of the function is to allow the
           codestreams to be added back again with some modifications by
           passing through the `indices' array and invoking `add_codestream'
           with the modified values. */
        int *indices = NULL;
        if (((num = num_codestreams) > 0) &&
            ((indices = codestream_indices) == &max_codestream_idx))
          { *tbuf = max_codestream_idx; indices = tbuf; }
        num_codestreams = max_codestreams = non_base_codestreams = 0;
        codestream_indices = NULL; max_codestream_idx = 0;
        return indices;
      }
    int *extract_layers(int &num, int *tbuf)
      { /* As above, but for compositing layers. */
        int *indices = NULL;
        if (((num = num_compositing_layers) > 0) &&
            ((indices = layer_indices) == &max_layer_idx))
          { *tbuf = max_layer_idx; indices = tbuf; }
        num_compositing_layers = max_compositing_layers = non_base_layers = 0;
        layer_indices = NULL; max_layer_idx = 0;
        return indices;        
      }
    void write(jp2_output_box &box); // Write contents into the upon box.
    bool check_match(int stream_idx, int layer_idx,
                     int base_stream_idx, int base_layer_idx,
                     bool rendered_result, bool exclude_region_numlists,
                     bool ignore_missing_numlist_categories);
      /* Does part of the work of `jpx_meta_manager::enumerate_matches'.
         Returns true if this numlist is a match for the indicated
         parameters and flags, as defined for that function. */
    void unlink();
      /* Unlinks us from any numlist cluster or list of identical numlists
         to which we belong.  If this leaves a region library unlinked from
         numlist libraries, we delete the region library here also. */
  private:
    void invalid_index_error();
      /* Called if a compositing layer or codestream index is too large to
         be written to a legal JPX number list box.  Generates `KDU_ERROR'
         error message. */
  public: // Data
    jx_container_base *container; // Non-NULL if numlist embedded in container
    int max_codestreams; // Max entries in `codestream_indices' array
    int num_codestreams;
    int non_base_codestreams; // See below
    int max_codestream_idx;
    int *codestream_indices; // Points to `max_codestream_idx' if only one
    int max_compositing_layers; // Max entries in `compositing_layers' array
    int num_compositing_layers;
    int non_base_layers; // See below
    int max_layer_idx;
    int *layer_indices; // Points to `max_layer_idx' if only one
    bool rendered_result;
  public: // Cluster links -- see `jx_numlist_cluster' and `jx_region_cluster'
    jx_metanode *metanode; // Metanode with which object is associated
    jx_numlist *next_identical; // See below
    jx_numlist *first_identical; // See below
    jx_numlist_cluster *numlist_cluster[JX_LIBRARY_NL_COUNT];
    jx_numlist *next_in_cluster[JX_LIBRARY_NL_COUNT];
    jx_numlist *prev_in_cluster[JX_LIBRARY_NL_COUNT];
    jx_region_library *region_library; // See below
  };
  /* Notes:
        The entries in the `codestream_indices' and `layer_indices' arrays
     are always ordered from the smallest to the largest index.
        The `non_base_codestreams' and `non_base_layers' members hold the
     number of initial entries from the `codestream_indices' and
     `layer_indices' arrays, respectively, which do not correspond to base
     codestreams or layers of a JPX container.  These values are identical
     to `num_codestreams' and `num_layers', respectively, if `container' is
     NULL.
        The `next_identical' and `first_identical' members are used to build
     a list of all numlist nodes that are identical to one
     another -- their descendants, of course, may generally be different.
     The head of this list is identified via the `first_identical'
     member; this member should not be NULL in any instantiated
     `jx_numlist' object -- it should at least point to itself.
        A single number list may belong to up to `JX_LIBRARY_NL_COUNT' library
     clusters: one for individual codestreams; one for individual compositing
     layers; one for auto-replicated container base codestreams; one for
     auto-replicated container base compositing layers; and a degenerative
     one for the "rendered result".  It possible for a number list to belong
     to all 5 clusters at once if its `container' member is non-NULL and its
     `non_base_codestreams' and `non_base_layers' members are both > 0 and
     smaller than `num_codestreams' and `num_compositing_layers', respectively.
     However, this is rather unlikely.
        Only the `first_identical' numlist object is actually included in the
     doubly-linked lists managed by the `next_in_cluster' and `prev_in_cluster'
     arrays and only this object may have non-NULL entries in its
     `numlist_cluster' arrays.  Moreover, only the `first_identical' numlist
     can have a non-NULL `region_library'.
        If the head of the identical list is deleted, one of the other
     numlists in the identical list takes over its role.
  */

/*****************************************************************************/
/*                               jx_regions                                  */
/*****************************************************************************/

struct jx_regions {
  public: // Member functions
    jx_regions(jx_metanode *owner)
      { memset(this,0,sizeof(*this)); this->metanode = owner; }
    ~jx_regions()
      { 
        unlink(); // In case not done already
        if ((regions!=NULL)&&(regions!=&bounding_region)) delete[] regions;
      }
    void set_num_regions(int num); // Resizes the arrays, as necessary
    void write(jp2_output_box &box);
      /* Write contents into the supplied open box. */
    bool read(jp2_input_box &box);
      /* Read open ROI description box, returning false if an error occurs. */
    void unlink();
      /* Unlinks us from any region cluster to which we belong. */
  private:
    static bool
      promote_roi_to_general_quadrilateral(jpx_roi &roi, kdu_dims inner_rect,
                                           int A, int B, int C, int D);
        /* Converts an existing rectangular region (`roi') into a general
           quadrilateral, based upon the supplied `inner_rect' (must be
           contained within `roi.region' and the four bit-fields A, B, C and D,
           whose interpretation is defined in the standard.  Returns false if
           an error occurs. */
    static int
      encode_general_quadrilateral(jpx_roi &roi, kdu_dims &inner_rect);
        /* Finds the `inner_rect' and the Rtyp code code required to
           represent the general quadrilateral in `roi', in addition to
           the quadrilateral's bounding box in `roi.region'.  Note that
           `roi.region' is assumed to have been correctly filled out to
           hold the tightest bounding box for the vertices in
           `roi.vertices'.  Moreover, `roi.vertices' are assumed to
           have the required sequence (clockwise) and order (top-most vertex
           first).  The function returns the complete Rtyp code. */
  public: // Data
    int num_regions;
    int max_regions;
    jpx_roi bounding_region;
    jpx_roi *regions; // Points to `bounding_region' if only one
    int max_width; // Width of widest region, from `jpx_roi::measure_axes'
  public: // Cluster links -- see `jx_region_cluster'
    jx_metanode *metanode; // Metadata node with which this object is associated
    jx_region_cluster *region_cluster;
    jx_regions *next_in_cluster, *prev_in_cluster;
  };

/*****************************************************************************/
/*                          jx_numlist_library                               */
/*****************************************************************************/

struct jx_numlist_library {
  public: // Member functions
    jx_numlist_library() { memset(this,0,sizeof(*this)); }
    ~jx_numlist_library();
    void add(jx_numlist *obj);
      /* Use this function to add a numlist to each of the internal libraries
         to which it belongs, attaching all relevant links to/from `obj'.
         If an identical numlist already exists, the current one is inserted
         into the relevant list of identical numlists. */
    void remove_cluster(jx_numlist_cluster *cluster);
      /* Removes the indicated cluster from the library.  Automatically
         removes any descendant nodes or clusters, as required, and also
         recursively removes a parent cluster if it turns out to be empty.
         Note, to prevent this function from calling itself with the same
         `cluster' argument as it unlinks descendants, `cluster->level'
         is temporarily set to -1 as a flag to indicate that removal is in
         progress. */
    jx_numlist *enumerate_matches(jx_numlist *last_match,
                                  int codestream_idx,
                                  int compositing_layer_idx,
                                  int base_codestream_idx,
                                  int base_compositing_layer_idx,
                                  bool applies_to_rendered_result,
                                  bool ignore_missing_numlist_info,
                                  bool only_non_roi_numlists);
     /* Does most of the work of `jpx_meta_manager::enumerate_matches' for
        numlists.  This function never returns the same numlist
        within a single sweep through the collection.
        [//]
        The `base_codestream_idx' argument may be non-negative only if
        `codestream_idx' is non-negative and it is not a top-level codestream;
        in this case, `base_codestream_idx' is the index of the
        base codestream, defined by a JPX container, one of whose replications
        is `codestream_idx'.  Similaly, `base_compositing_layer_idx' may be
        non-negative only if `compositing_layer_idx' is non-negative and it is
        not a top-level compositing layer.
        [//]
        The `only_non_roi_numlists' argument means that we want only numlists
        which have, amongst their descendants, at least one non-trivial
        non-ROI metadata node (by trivial, we mean group/free nodes that
        have no meaning).  This function only enumerates numlists which
        are found at the head of their lists of identical numlists -- i.e.,
        numlists whose `jx_numlist::first_identical' member points to
        itself.  The `last_match' argument, if non-NULL, must also have
        this property.  The `jpx_meta_manager::enumerate_matches' function
        provides additional capabilities to enumerate the individual
        elements of each list of identical numlists. */
  private: // Helper functions
    void check_split_root_cluster(jx_numlist_cluster *root);
      /* This function is called by `add' if we find that the root cluster
         of some hierarchy has been augmented.
            If the root cluster is already at a non-zero level, we consider
         creating a new root for the hierarchy which is 3 levels higher; we do
         this if and only if the root cluster currently has more than 8
         children, since each new intermediate cluster is guaranteed to have at
         most 8 descendants (growing by 3 levels at a time means that each
         successive level of clusters partitions its parent 8 ways, except if
         the parent is the root cluster -- the new intermediate levels are
         not roots).  This may still leave the root cluster with more than 8
         children if their numlists are highly spread out, so we need to be
         prepared to apply the method recursively until some numlists get
         gathered together at a suitably high scale.
            If the root cluster is still at level 0, the splitting decision is
         a bit different.  We first need to determine whether the numlists
         within the cluster would be assigned to different level 0 clusters.
         If not, there is no point in splitting.  If they would, and there are
         more than 8 regions in the root cluster, we create a new root at
         level 3 and partition the numlists between its descendants. */
  public: // Data
    jx_numlist_cluster *clusters[JX_LIBRARY_NL_COUNT];
  };
  /* Notes:
        See `jx_numlist_cluster'.
  */

/*****************************************************************************/
/*                           jx_numlist_cluster                              */
/*****************************************************************************/

struct jx_numlist_cluster {
  public: // Member functions
    jx_numlist_cluster(jx_numlist_library *lib, int lib_idx)
      { 
        memset(this,0,sizeof(*this));
        this->library = lib; this->library_idx = lib_idx;
      }
  public: // Data
    jx_numlist_library *library;
    int library_idx; // Must be in the range 0 to 2 -- see below
    int log_range;
    int level;
    int range_min, range_lim;
    jx_numlist_cluster *parent; // Up to previous level in log_range
    jx_numlist_cluster *next; // Next in level or next log_range in library
    union {
      jx_numlist_cluster *descendants; // Down to next level
      jx_numlist *numlists; // If `level'=0
    };
  };
  /* Notes:
        This object is used to build a library of numlist nodes -- i.e.,
     `jx_metanode' objects which represent a number list (and hence have an
     embedded `jx_numlist' object).  The purpose of the library is to
     facilitate fast access to metadata which is related to a specific
     codestream, compositing layer or the rendered result.  There is an
     analogous structure, `jx_region_cluster', which is used to build
     libraries of regions of interest; moreover, numlist clusters provide
     an efficient way of finding region of interest libraries which describe
     regions for specific codestreams, compositing layers, or the rendered
     result.
        The `jx_numlist_library' object maintains an array of FIVE pointers
     to top-level cluster lists (see `jx_numlist_library::libraries').  The
     five entries in this array correspond to `library_idx' values of
     `JX_LIBRARY_NLL', `JX_LIBRARY_C_NLL', `JX_LIBRARY_NLC',
     `JX_LIBRARY_C_NLC' and `JX_LIBRARY_NLR'.  These correspond to
     individual compositing layers (NLL), container-replicated compositing
     layers (C_NLL), individual codestreams (NLC), container-replicated
     codestreams (C_NLC) and rendered result (NLR).  The NLL and C_NLL
     libraries help to identify numlist nodes by the compositing layers with
     which they are associated.  Similarly, the NLC and C_NLC libraries help
     identify numlist nodes by the codestreams with which they are associated.
     Finally, the NLR library collects numlist nodes that are associated
     with the rendered result -- this one is degenerate, since there is no
     specialization to any image entities, but it is worth keeping in its
     own library for the sake of having a uniform encapsulation method.
        It is worth explaining briefly here the difference between the
     individual and container-replicated libraries.  Container-replicated
     libraries manage numlists that are embedded in JPX containers, where
     the numlist references at least one of the container's base compositing
     layers (C_NLL) or base codestreams (C_NLC).  It is possible for such
     numlists also to reference top-level compositing layers and/or
     codestreams, in which case they may also be found within the individual
     compositing layer (NLL) or codestream (NLC) libraries.  Numlists that
     are not embedded within a JPX container may still reference compositing
     layers or codestreams that are found within a container, but these are
     individual references (as opposed to references to all replicated copies
     of the container's base layers or codestreams).
        Each numlist library (i.e., each entry in the
     `jx_numlist_libaries::libraries' array) consists of a linked list of
     `jx_numlist_cluster' objects, all having `parent'=NULL, connected via
     their `next' links.  Each cluster in this top-level list has a distinct
     value for `log_range' and collects all numlist nodes whose "range"
     satisfies 2^{`log_range'} >= "range" > 2^{`log_range'-1}, where the
     value of "range" is determined as follows for each cluster type:
     -- NLL: "range" = 1 + <max comp. layer index> - <min comp. layer index>
     -- NLC: "range" = 1 + <max codestream index> - <min codestream index>
     -- C_NLL: "range" = 1 + <max container base compositing layer index> -
                             <min container base compositing layer index>
     -- C_NLC: "range" = 1 + <max container base codestream index> -
                             <min container base codestream index>
     -- NLR: "range" = 1
        The elements in the above-mentioned lists are termed root clusters.
     There is one root cluster for each log-range value within each library.
     The `range_min' and `range_lim' members in the root cluster store lower
     (inclusive) and upper (exclusive) bounds on the "start" value, where
     -- NLL: "start" = <min compositing layer index>
     -- NLC: "start" = <min codestream index>
     -- C_NLL: "start" = <min container base compositing layer index>
     -- C_NLC: "start" = <min container base codestream index>
     -- NLR: "start" = 0, but in this case, the entire library consists of
             just one cluster without any hierarchy or multiple log ranges.
        In each root cluster, the `range_min' and `range_lim' members store
     the tightest bounds that span all contained numlists (or all numlists
     that ever were contained at some point or other -- in case some have
     been removed, the `range_min' and `range_lim' values are not adjusted).
        If necessary, the root cluster will be recursively split into a list
     of `descendants', each of which have non-NULL `parent' members.  The
     descendants are also linked via their `next' members.  A cluster with
     non-NULL `parent' contains only those numlist nodes for which the "start"
     value satisfies:
         `range_min' <= "start" < `range_min' + 2^{`log_range'+`level'}
     The `range_min' values for non-root clusters must be multiples of
     2^{`log_range'+`level'}, while `range_lim' must be equal to
     `range_min' + 2^{`log_range'+`level'}+2^{`log_range'}-1.
        At the leaves of the cluster hierarchy where `level'=0 (the root of
     the hierarchy might be the sole leaf), the `numlists' member points to a
     doubly-linked list of `jx_numlists' objects, which belong to the cluster.
     The links in this list are found within the `jx_numlist::next_in_cluster'
     and `jx_numlist::prev_in_cluster' arrays of each entry in the list,
     at the entries corresponding to the cluster's `library_idx' value.
        As explained in the comments following `jx_numlist', only one out of
     each collection of identical numlists is actually linked into the
     library (this is the head of the doubly-linked list managed by
     `jx_numlist::next_identical' and `jx_numlist::first_identical'.
  */

/*****************************************************************************/
/*                            jx_region_library                              */
/*****************************************************************************/

struct jx_region_library {
  public: // Member functions
    jx_region_library()
      { representative_numlist = NULL; num_elts=0; clusters=NULL; }
    ~jx_region_library();
    void add(jx_regions *obj, bool temporary);
      /* Use this function to add an ROI node to the libraries.  If
         `temporary' is true, a new `jx_regions' object is created as a
         copy, to be deleted when the region library is destroyed.
         Otherwise, the library is linked directly into `obj' and `obj'
         will not be deleted when the library is destroyed.  Note that
         copies do not have their own `jx_regions::regions' array -- they
         point to the original `obj->regions' array. */
    void remove_cluster(jx_region_cluster *cluster);
      /* Removes the indicated cluster from the library.  Automatically
         removes any descendant nodes or clusters, as required, and also
         recursively removes a parent cluster if it turns out to be empty.
         Note, to prevent this function from calling itself with the same
         `cluster' argument as it unlinks descendants, `cluster->level'
         is temporarily set to -1 as a flag to indicate that removal is in
         progress. */
    jx_regions *enum_elts(jx_regions *prev_elt);
      /* You can use this function to walk sequentially through all ROI nodes
         ever added using `add' or `add_temporary'.  A NULL `prev_elt'
         argument returns the first node in this sequence.  The function
         essentially flattens out the clusters that are formed internally.
         This is useful when writing JPX files. */
    jx_regions *enumerate_matches(jx_regions *last_match,
                                  kdu_dims region, int min_size);
      /* Does the work of `jpx_meta_manager::enumerate_matches' for
         region nodes.  Note that this function is only invoked on
         region libraries whose numlist (or lack thereof) matches
         the other arguments supplied to the
         `jpx_meta_manager::enumerate_matches'. */
  private: // Helper functions
    void check_split_root_cluster(jx_region_cluster *root);
      /* This function is called by `add' if we find that the root cluster
         of some hierarchy has been augmented.
            If the root cluster is already at a non-zero level, we consider
         creating a new root for the hierarchy which is 2 levels higher; we do
         this if and only if the root cluster currently has more than 16
         children, since each new intermediate cluster is guaranteed to have at
         most 16 descendants (growing by 2 levels at a time means that each
         successive level of clusters partitions its parent 2^2=4 ways
         horizontally and 4 ways vertically, except if the parent is the root
         cluster -- the new intermediate levels are not roots).  This may
         still leave the root cluster with more than 16 children if their
         regions are highly spread out, so we need to be prepared to apply
         the method recursively until some regions get gathered together at
         a suitably high scale.
            If the root cluster is still at level 0, the splitting decision is
         a bit different.  We first need to determine whether the regions
         within the cluster would be assigned to different level 0 clusters.
         If not, there is no point in splitting.  If they would, and there are
         more than 16 regions in the root cluster, we create a new root at
         level 2 and partition the regions between its descendants. */
  public: // Data
    jx_numlist *representative_numlist;
    int num_elts; // Total number of ROI nodes in the library
    jx_region_cluster *clusters;
  };
  /* Notes:
        See `jx_region_cluster'.
  */

/*****************************************************************************/
/*                            jx_region_cluster                              */
/*****************************************************************************/

struct jx_region_cluster {
  public: // Member functions
    jx_region_cluster(jx_region_library *lib)
      { memset(this,0,sizeof(*this)); this->library=lib; }
  public: // Data
    jx_region_library *library; // Points back to the library we belong to
    int log_size;
    int level;
    kdu_dims cover;
    jx_region_cluster *parent; // Up to previous level
    jx_region_cluster *next; // Across to next in level or next log_range
    union { 
      jx_region_cluster *descendants; // Down to next level
      jx_regions *regions; // If `level'=0
    };
  };
  /* Notes:
        This object plays an analogous role to `jx_numlist_cluster' but for
     regions of interest.  It is used to build a library of ROI nodes -- i.e.,
     `jx_metanode' objects which represent an ROI description box (and hence
     have an embedded `jx_regions' object).  The purpose of the library is
     to facilitate fast access to metadata which is related to specific
     regions of interest -- typically within specific codestreams and/or
     compositing layers.
        Each region library consists of a single `jx_region_library' object
     whose `jx_region_library::clusters' member points to a linked list of
     top-level clusters, connected via their `next' links.  The top level
     clusters have `log_size' values which decrease monotonically.  Under
     each such `log_size' root, all ROI nodes with the same log-size value
     are organized in hierarchical fashion, where the log-size value of
     an ROI node is the smallest integer R, such that 2^R is at least as
     large the width and the height of the ROI node's bounding box.  The root
     of a `log_size' hierarchy has the largest value for `level' and a NULL
     `parent' member; its `next' member connects it with the root of the
     next smaller `log_size' hierarchy.
        The `cover' member of any cluster identifies a region which completely
     covers all regions of interest that might be assigned to the cluster.  In
     the root cluster of each log-size hierarchy, this member stores the actual
     tightest covering region that spans all contained regions (or all regions
     that ever were contained at some point or other -- in case some have
     been removed, the cover is not adjusted).  If necessary, the root cluster
     will be recursively split into a list of `descendants', each of which have
     non-NULL `parent' members.  The descendants are also linked via their
     `next' members.  A cluster with non-NULL `parent' contains only those
     ROI nodes for which the upper left hand corner of the bounding box has
     (x,y) coordinates that lie in the range:
          `cover.pos'.x <= x < `cover.pos'.x + 2^{`log_size'+`level'} and
          `cover.pos'.y <= y < `cover.pos'.y + 2^{`log_size'+`level'}.
     The `cover.pos'.x and `cover.pos.y' members must themselves be
     multiples of 2^{`log_size'+`level'} and `cover.size.x' and `cover.size.y'
     will both be equal to 2^{`log_size'+`level'}+2^{`log_size'}-1.
        At the leaves of the cluster hierarchy where `level'=0 (the root of the
     hierarchy might be the sole leaf), the `regions' member points to a
     doubly-linked list of `jx_regions' objects, which belong to the cluster.
     The links in this list are found within the `jx_regions::next_in_cluster'
     and `jx_regions::prev_in_cluster' members of each entry in the list.
        There is one global (unassociated) region library, managed directly
     by the `jx_meta_manager' object; this is unlikely to be used, in practice,
     since ROI descriptions that are not associated with numlists are strongly
     discouraged (at least within Kakadu).  This unassociated region library
     has its `jx_region_library::representative_numlist' member equal to NULL.
        All other region libraries are associated with a specific number list.
     Rather than building a separate region library for each `jx_numlist'
     instance in the metadata hierarchy, we build one region library for
     all `jx_numlist' instances which specify exactly the same collection of
     codestreams and compositing layers (plus `jx_numlist::rendered_result'
     value).  These number lists are all connected in a list via
     their `jx_numlist::next_identical' members.  The head of this list is
     the one that points to the `region_library' for the collection.
        The `regions' list normally points to a list of `jx_regions' objects
     that are actually referenced by their respective `jx_metanode' owners.
     However, the list may instead contain copies of the original `jx_regions'
     objects -- this is useful when building temporary region libraries for
     structured writing of JPX metadata to file.  The copies are readily
     distinguishable from originals because the `jx_metanode' object
     referenced by `jx_regions::metanode' does not have its
     `jx_metanode::regions' member pointing back to the same `jx_regions'
     object.  Copies do not have their own `jx_regions::regions' array, but
     rather point to the original `jx_regions::regions' array.  Copies are
     necessarily short lived and are destroyed when the referencing
     `jx_region_cluster' is destroyed.
  */

/*****************************************************************************/
/*                              jx_trapezoid                                 */
/*****************************************************************************/

struct jx_trapezoid {
    void init(kdu_coords l1, kdu_coords l2, kdu_coords r1, kdu_coords r2)
      { // left edge l1->l2 (l1 on top); right edge r1->r2 (r1 on top)
        left1=l1; left2=l2; right1=r1; right2=r2;
        min_y = (l1.y>r1.y)?l1.y:r1.y;
        max_y = (l2.y<r2.y)?l2.y:r2.y;
      }
    int min_y, max_y;
    kdu_coords left1, left2, right1, right2;
  };

/*****************************************************************************/
/*                         jx_scribble_segment                               */
/*****************************************************************************/

struct jx_scribble_segment {
  public: // Member functions
    void set_associated_points(int seg_start, int seg_length)
      { 
        first_seg_point = seg_start % num_scribble_points;
        num_seg_points = seg_length;
      }
    const kdu_coords *get_point(int n)
      { 
        if ((n < 0) || (n >= num_seg_points)) return NULL;
        n += first_seg_point;
        while (n >= num_scribble_points) n -= num_scribble_points;
        return scribble_points + n;
      }
  public: // Data
    const kdu_coords *scribble_points; // Points back to owner's array
    int num_scribble_points; // Size of owner's scribble points array
    int first_seg_point; // Index into the `scribble_points' array
    int num_seg_points; // Number of scribble points in the segment
    bool is_line;
    bool is_ellipse;
    kdu_coords seg_start, seg_end;
    kdu_coords ellipse_centre, ellipse_size, ellipse_skew;
    jx_scribble_segment *next;
    jx_scribble_segment *prev;
  };
  /* Notes:
       This structure is used by `jx_scribble_converter' to represent an
     approximated scribble path.  Segments are connected either linearly
     or cyclically, via the `next' and `prev' members.  That is, `next' and
     `prev' form a doubly-linked list in which the first segment's `prev'
     member points to the last segment (if cyclic) or NULL, while the last
     segment' `next' member points to the first segment (if cyclic) or NULL.
        Each segment has an associated set of scribble points, which come from
     the shared array managed by the owning `jx_scribble_converter' object.
     For convenience, the location of this array is stored here as
     `scribble_points' and the number of available points is stored as
     `num_scribble_points'.  The points which are associated with the current
     segment have indices starting from `first_seg_point' and running
     consecutively for a total of `num_seg_points' points.  It is possible
     that these points have indices which wrap around back to the first entry
     in the `scribble_points' array, so the `get_point' function is provided
     for convenience.  You can be sure that each segment has a disjoint
     collection of associated points.
        A segment whose `is_line' and `is_ellipse' members are both false
     is being used to hold a range of scribble points which have not yet been
     fitted.  Otherwise, exactly one of `is_line' or `is_ellipse' is true.
        If `is_line' is true, `seg_start' and `seg_end' hold the end-points
     of the line segment which is approximating the associated scribble points.
     In the simplest case, `seg_start' and `seg_end' correspond to actual
     scribble points -- one of them will be associated with an adjacent
     segment, since scribble points are disjointly associated with segments.
     More generally, though, one or both of `seg_start' and `seg_end' may be
     modified so that the line segment is tangent to an adjacent elliptical
     segment.
        If `is_ellipse' is true, the ellipse is described by the
     `ellipse_centre', `ellipse_extent' and `ellipse_skew' parameters, which
     have the same interpretation as that used by the `jpx_roi::init_ellipse'
     function.  The `axis_extents' and `tan_theta' members provide an
     alternate interpretation of the ellipse, as returned by the second
     form of the `jpx_roi::get_ellipse' function.  Ellipses are obtained
     initially by fitting the set of associated scribble points -- an
     eigenvalue/eigenvector problem.  During this process, the set of
     associated scribble points is adjusted so as to obtain an ellipse which
     meets the various constraints.  Specifically, we grow the set of
     scribble points associated with the segment until all non-associated
     scribble points are found to be outside the ellipse.  We then assess the
     fitting constraints for the associated points and continue to extend
     the set of associated points until these fitting constraints are
     violated.  Once an ellipse is found, the set of associated points is
     trimmed back (if required) so as to ensure that the first and last
     associated points lie outside or on the ellipse boundary.  These first
     and last associated scribble points are projected to the ellipse and
     assigned to the `seg_start' and `seg_end' members.  Later, line
     segments can be connected to the ellipse at these end points.  However,
     for a smoother boundary, we can later explore further trimming away
     associated scribble points from the ellipse and projecting the first
     and last remaining scribble points to obtain new `seg_start' and
     `seg_end' values -- we can keep doing this until the attaching line
     segment becomes tangential to the ellipse (or nearly so).
        Note that an elliptical segment may not be adjacent to another
     elliptical segment, apart from itself -- this degenerate case
     corresponds to the entire scribble path being approximated by a single
     elliptical segment. */

/*****************************************************************************/
/*                        jx_scribble_converter                              */
/*****************************************************************************/

#define JXSC_MAX_VERTICES 512
struct jx_scribble_converter {
  public: // Member functions
    jx_scribble_converter()
      { num_scribble_points=num_boundary_vertices=0; segments=NULL; }
    ~jx_scribble_converter() {};
    void init(const kdu_coords *points, int num_points, bool want_fill);
      /* Initializes he `scribble_points' and `num_scribble_points' members
         and creates a single segment associated with all the points. */
    bool find_polygon_edges(kdu_long max_sq_dist);
      /* This function is invoked by `jpx_roi_editor::convert_scribble_path'
         to decompose segments of uncommitted sribble points into a piecewise
         linear approximation.  The following rules apply:
         1) The `seg_start' member of any segment must be identical to the
            `seg_last' member of a previous segment, if one exists.
         2) If there is no previous segment `seg_start' must be identical to
            the first associated scribble point.  Similarly, if there is no
            next segment, `seg_end' must be identical to the last associated
            scribble point.
         3) The squared Euclidean distance between a line segment and any
            of its associated scribble points may not exceed `max_sq_dist'.
         4) No line segment may intersect with any other segment, be it
            a line segment or an elliptical segment.
         5) No line segment may cross the original scribble path anywhere
            other than within the scrible points with which it is associated.
         The function returns false if the above conditions cannot be
         satisfied for some reason. */
    bool find_boundary_vertices();
      /* Fills out the `boundary_vertices' and `num_boundary_vertices' members
         based on the available `segments'.  This is mainly to facilitate
         access to the polygon portion of a region boundary by other objects
         so they don't have to process the segments directly.  Returns false
         if something goes wrong. */
  private: // Helper functions
    jx_scribble_segment *get_free_seg()
      { 
        jx_scribble_segment *result = free_segments;
        if (result != NULL)
          { free_segments=result->next; result->next=result->prev=NULL;
            result->is_line = result->is_ellipse = false; }
        return result;
      }
  public: // Data
    int num_scribble_points; // Used to store the original scribble points
    kdu_coords scribble_points[JX_ROI_SCRIBBLE_POINTS];
    jx_scribble_segment *segments;
    jx_scribble_segment *free_segments;
    jx_scribble_segment seg_store[512]; // Used to store the actual segments
    int num_boundary_vertices;
    kdu_coords boundary_vertices[513]; // See `find_boundary_vertices'.
  };

/*****************************************************************************/
/*                             jx_path_filler                                */
/*****************************************************************************/

#define JXPF_MAX_REGIONS 512
#define JXPF_MAX_VERTICES (4*JXPF_MAX_REGIONS)
#define JXPF_INTERNAL_EDGE JXPF_MAX_VERTICES

struct jx_path_filler {
  public: // Member functions
    jx_path_filler() { memset(this,0,sizeof(*this)); }
    bool init(jpx_roi_editor *editor, int num_path_members,
              kdu_byte *path_members, kdu_coords path_start);
      /* Initialize directly with info from `editor->enum_paths'.
         Note that only the quadrilateral regions are kept.  These are
         initially modified based on path segment information recovered via
         `editor->get_path_segment_for_region', so that all exterior edges
         are bounded by vertices.  The constructor assumes that the path
         is closed, so that it returns back to `path_start'.  Returns false
         if the path is not a closed loop, or if the path intersects itself,
         amongst other pathalogical conditions. */
    bool init(const kdu_coords *src_vertices, int num_src_vertices);
      /* This second form of the `init' function is used by the
         `jpx_roi_editor::convert_scribble_path' function to initialize
         the path filler with the vertices of a polygon.  The last vertex
         in the list must be identical to the first, or else the function
         will return false.  The function also returns false if the polygonal
         boundary intersects itself.  After this version of the `init'
         function, you would generally invoke `process' immediately.  The
         `original_path_members' array is reset to zero by this function
         and left there. */
    bool contains(const jx_path_filler *src) const;
      /* Returns true if the outer boundary of the polygon described by
         `src' is entirely contained within the inner boundary of the
         polygon described by the current object. */
    bool intersects(const jx_path_filler *src) const;
      /* Returns true if any of the path bricks associated with `src'
         intersect with the current object.  This function is usually of
         greatest interest when invoked immediately after the current
         object's `init' function. */
    void import_internal_boundary(const jx_path_filler *src);
      /* Imports all the regions from `src' into the current object,
         exchanging the roles played by external edges and unpaired internal
         edges.  The two paths should pass the `contains' test before this
         function is called. */
    void get_original_path_members(kdu_uint32 flags[]) const
      { for (int n=0; n < 8; n++) flags[n] |= original_path_members[n]; }
      /* `flags' is an 8-element array of bit-fields (256 bit-fields in all).
         The function sets bits which correspond to the original indices of
         the quadrilateral regions supplied to the constructor via the
         `path_members' argument. */
    bool process();
      /* Runs the various phases of the polygon filling algorithm. */
  private: // Helper functions
    int examine_path(const kdu_coords *src_vertices, int num_src_vertices);
      /* This function takes a list of vertices which run consecutively
         around a (supposedly) closed path, such that the last vertex is
         identical to the first vertex.  The function first checks that the
         path is indeed closed and that no edge intersects with any other
         edge.  If either of these conditions is violated, the function
         returns 0.  The function then determines whether the path is
         traversed clockwise or anti-clockwise, returning a +ve value for
         clockwise and -ve value for anti-clockwise.  Both of the `init'
         functions rely upon this function. */
    bool check_integrity();
      /* Useful debugging tool.  This function checks that all edges which
         reference other edges are referenced back by those same edges. */
    int count_internal_edges();
      /* Counts the number of internal edges which are not shared with any
         other region and have non-zero length.  The goal of the `process'
         function is to reduce this value to 0. */
    bool join();
      /* Attempts to share as many internal edges as possible -- once all
         internal edges are shared there are no holes inside the polygonal
         path being filled.  Makes only one pass through the regions to see
         what edges can be shared, returning true if anything was changed.
         If so, further sharing might still be possible. */
    bool simplify();
      /* Attempts to reduce the number of distinct quadrilateral regions
         by merging regions which are already sharing at least one edge.
         In order to do this, the common vertices may need to be adjusted,
         which is allowed so long as the edges of other quadrilaterals are
         not crossed in the process.  Makes only one pass through the regions
         to see what regions can be eliminated, returning true if anything
         was changed.  If so, further simplification might still be
         possible. */
    bool fill_interior();
      /* Attempts to reduce the number of interior edges by adding new
         regions.  Three connected interior edges might be reduced to 1 by
         adding an interior quadrilateral.  Similarly, two connected
         interior edges might be reduced to 1 by adding an interior triangle.
         The function adds at most one region, returning true if it does so,
         in which case further calls to this function might be able to
         further fill the interior.  However, after each successful call to
         this function, you should invoke `join'. */
    bool check_vertex_changes_for_edge(int edge_idx, const kdu_coords *v0,
                                       const kdu_coords *v1,
                                       int initial_edge_idx=-1) const;
      /* This recursive function evaluates whether or not the end-points of
         the edge corresponding to entry `edge_idx' in the `edges' array
         can legitimately be changed such that its end-points are moved
         to the coordinates found at `v0' and `v1'.  The supplied end-points
         are correctly ordered, within the region with index n=`edge_idx'>>2.
         That is, v0 holds the new value for vertex `edge_idx'-4*n and
         v1 holds the new value for vertex (`edge_idx-4*n+1) mod 4.  The
         function returns true immediately if v0 and v1 hold the same values
         which are currently found at these vertices.  Failing this, the
         function returns false if a vertex which must be moved is one of
         the end-points of an external edge, or if the move causes region
         n to be an invalid quadrilateral (i.e., opposite edges cross), or
         if the move causes any of the edges attached to the moved vertices
         to cross any external edge of the polygon at all.  If all the above
         tests succeed, the function recursively explores the impact on
         neighbouring regions which would be affected by the change through
         shared edges.  The function assumes that the line segment joining
         v0 to v1 already lies entirely within the polygon that is being
         filled, so this condition does not have to be checked -- saves
         redundant boundary scans.
            The `initial_edge_idx' argument should be -ve when the function
         is called from any place other than itself.  This signals that both
         sides of the edge itself may need to be checked.  Subsequent recursive
         calls from within itself, have `initial_edge_idx' set to the index
         of the first `edge_idx' value with which the function was called. */
    void apply_vertex_changes_for_edge(int edge_idx, const kdu_coords *v0,
                                       const kdu_coords *v1);
      /* This recursive function changes the end-points of the indicated edge
         to `v0' and `v1', respectively, following shared edges to other
         regions which may be sharing the vertices which are changing.  If
         the change causes any edge length to go to zero, the corresponding
         `region_edges' entry will be changed to point back to itself, so
         that triangle vertices are never treated as shared edges. */
    bool check_boundary_violation(const kdu_coords *v0,
                                  const kdu_coords *v1) const;
      /* Scans all the external edges of the polygon being filled, to check
         whether or not the line segment connecting `v0' to `v1' crosses one
         of these edges.  If so, the function returns true. */
    bool check_boundary_violation(const jpx_roi &roi) const;
      /* Scans all the external edges of the polygon being filled, to check
         whether or not any of them pass through or are contained within
         any of the edges of the quadrilateral supplied by `roi'. */
    bool is_region_triangular(int idx, int edges[]);
      /* This function returns false unless the region identified by `idx'
         represents a triangle, in which case it returns true, and sets the
         entries of the `edges' array to the indices of the three edges
         of the triangle, as found within the `region_edges' array.  These
         are drawn from 4*idx+p where p runs from 0 to 3, eliminating the
         edge whose length is zero.  The edges run clockwise around the
         triangle. */
    bool remove_degenerate_region(int idx);
      /* This function returns false without doing anything, unless the
         region identified by `idx' is degenerate, in one of the following
         senses: a) Two pairs of adjacent vertices are identical (a line);
         b) Opposite vertices (v0 and v2 or v1 and v3) are identical, meanng
         the edges fold back on themselves.  If either of these conditions
         hold, the region is removed and the function returns true.  Note
         that to do this properly, the function must reconnect edges around
         the removed degenerate region. */
    void remove_edge_references_to_region(int idx);
      /* Removes all references from other regions to any of the edges of
         region `idx'.  This allows the region to be repurposed or
         removed. */
    void remove_region(int idx);
      /* Removes the region identified by `idx', reconnecting the various
         array references around it.  It is an error to call this function
         if the region is still referenced by any other region, except
         possibly from an edge of zero length. */
    bool add_quadrilateral(int shared_edge1, int shared_edge2,
                           int shared_edge3);
      /* Tries to add a new quadrilateral region which will share existing
         internal edges identified by the two arguments.  These existing edges
         run clockwise around the outside of the new quadrilateral.  The
         function returns false if there is insufficient storage to add the
         new quadrilateral, if the quadrilateral formed by completing the
         fourth edge would be invalid (e.g., runs anti-clockwise, or has
         edges which cross), or if the quadrilateral would violate the
         boundary constraints -- see `check_boundary_violation'. */
    bool add_triangle(int shared_edge1, int shared_edge2);
      /* Tries to add a new triangular region which will share existing
         internal edges identified by the two arguments.  These existing edges
         run clockwise around the outside of the new triangle.  The
         function returns false if there is insufficient storage to add the
         new triangle, if the triangle formed by completing the third edge
         would be invalid (e.g., runs anti-clockwise, or has
         edges which cross), or if the triangle would violate the boundary
         constraints -- see `check_boundary_violation'. */
  public: // Data members used only by the first form of `init', which assumes
          // a maximum of 256 initial path bricks.
    kdu_uint32 original_path_members[8]; // 256 flag bits
    int num_tmp_path_vertices; // Used to store vertices of the path polygon
    kdu_coords tmp_path_vertices[257]; // for analysis by `examine_path'.
  public: // Data members used by both forms of `init' and the other functions
    int num_regions;
    kdu_coords region_vertices[JXPF_MAX_VERTICES]; // 4 vertices per region
    int region_edges[JXPF_MAX_VERTICES]; // See below
    jx_path_filler *container; // Used to determine holes for other paths
    jx_path_filler *next;
  };
  /* Notes:
        This object is used to implement the functionality of
     `jpx_roi_editor::fill_closed_paths', as well as
     `jpx_roi_editor::convert_scribble_path'.
        The `region_vertices' member keeps track of the 4 vertices associated
     with each region.  These appear at entries 4n through 4n+3, where n is
     the region index, in the range 0 to `num_regions'-1.
        The  `region_edges' array has one entry for each quadrilateral edge.
     Specifically edge e from region n corresponds to entry 4n+e where
     edge e runs between vertex e and vertex (e+1) mod 4.  The value of
     this entry is -ve if the edge is external to the polygon being filled.
     The value is JXPF_INTERNAL_EDGE if the edge is internal to the polygon
     being filled, but not shared with any other region.  Otherwise, the
     entry is equal to 4p+q where p and q are the index of another region
     and its edge, respectively. */

/*****************************************************************************/
/*                               jx_metaref                                  */
/*****************************************************************************/

struct jx_metaref {
  public: // Member functions
    jx_metaref()
      { src=NULL; i_param=0; addr_param=NULL;
        data32[0]=data32[1]=data32[2]=data32[3]=0; }
  public: // Data
    jp2_locator src_loc; // Used to identify location in a non-NULL `src'
    jp2_family_src *src; // Non-NULL for existing metadata in a JPX data source
    int i_param; // Integer parameter for delayed output box references
    void *addr_param; // Address parameter for delayed output box references
    union {
      kdu_byte data[16]; // Depends on box-type; UUID boxes store UUID here
      kdu_uint32 data32[4]; // Facilitates erasure at least
    };
  };

/*****************************************************************************/
/*                              jx_crossref                                  */
/*****************************************************************************/

struct jx_crossref {
  public: // Member functions
    jx_crossref(jx_metanode *owner)
      { this->owner = owner; this->box_type=0; metaloc=NULL; link=NULL;
        link_type=JPX_METANODE_LINK_NONE; next_link=NULL; }
    ~jx_crossref() { unlink(); }
    void append_to_list(jx_crossref *head)
      { /* Adds ourselves to the end of a list headed by `head'. */
        while (head->next_link != NULL) head = head->next_link;
        head->next_link = this;  this->next_link = NULL;
      }
    void link_found();
      /* Called once `metaloc' points to a valid target.  If the
         `link_type' member is already set (i.e., not `JPX_METANODE_LINK_NONE')
         it is not changed here.  Otherwise, `link_type' is discovered using
         the `JX_METANODE_FIRST_SUBBOX' flag of the owner and target nodes. */
    void unlink();
      /* Removes the structure from any list to which it belongs, headed
         (directly or indirectly) by `metaloc' or `link', and connected by
         `next_link'.  Leaves all of those members NULL.  This should have
         been done before the destructor is called. */
    void fill_write_info(jx_metapres *preserved_state);
      /* Initializes the `box_type' and address info in `frag_list' based on
         the preserved asoc/box locations and lengths stored in the
         `preserved_state' object -- typically this is found in a link
         target that has already been written. */
  public: // Data
    jx_metanode *owner;
    kdu_uint32 box_type; // Always holds a valid non-0 value
    jx_fragment_list frag_list;
    jx_metaloc *metaloc; // Non-NULL only while waiting to resolve `link'
    jx_metanode *link;
    jpx_metanode_link_type link_type;
    jx_crossref *next_link; // For building a list of links to a metanode
  };
  /* Notes:
       When reading a cross-reference box, an instance of this structure is
     created to hold the fragment list and box-type.  If the cross-reference
     box has a box-type which is of interest to the meta-manager and its
     fragment list represents a single contiguous range of bytes from the
     same file, it might represent a link.  To investigate further,
     `jx_metaloc_manager::get_locator' is invoked with the location of the
     first byte in the fragment, and the `create_if_necessary' argument set
     to true.  The `metaloc' member is set to the returned reference, which
     has one of the following forms:
     a) `metaloc' references the target of the link, in which case
        `link' is set equal to `metaloc->target' and `metaloc' is set to NULL.
     b) `metaloc' is newly allocated, in which case the `metaloc->target'
        is set to `owner'.
     c) `metaloc->target' points to the `owner' of a `jx_crossref' object
        whose `jx_crossref::link' member is NULL and whose
        `jx_crossref::metaloc' member is identical to `metaloc'.  This means
        that case (b) occurred previously, so we add ourselves to the list of
        nodes waiting for the true target to become available, building this
        list via the `next_link' member.
       Once the `link' member is non-NULL, the `metaloc' member is set to
     NULL and the `link' object's `linked_from' member will point to the
     head of a list of `jx_crossref' objects which contain links to it, the
     `next_link' member being used to build this list.  Should the `link'
     object be deleted during an editing operation, all the linking nodes
     are also deleted.
       When saving metadata to a file, only those cross-reference
     nodes which contain links can be saved; the others are skipped.  A two
     pass approach is generally required.  In the first pass, the locations
     and lengths of every box to be saved are determined and the
     `jx_metanode::linked_from' lists are traversed to record this
     information in the linking nodes' `frag_list' box as a single fragment.
     Until this happens, the `box_type' member is set to 0, since the box
     type generally depends upon the way in which the link target is written
     anyway (actual node's box-type or else `jp2_association_4cc').  In the
     second pass, all the boxes (including links) can actually be written. */

/*****************************************************************************/
/*                              jx_metaread                                  */
/*****************************************************************************/

struct jx_metaread {
  public: // Member functions
    jx_metaread()
      { codestream_source=NULL; layer_source=NULL; container_source=NULL; }
  public: // Data
    jp2_input_box asoc; // Used to manage an open asoc box.
    jp2_input_box box; // Used to manage a single box, or a sub-box of an asoc
    jx_codestream_source *codestream_source; // See below
    jx_layer_source *layer_source; // See below
    jx_container_source *container_source; // See below
  };
  /* Notes:
       The `codestream_source', `layer_source' or `container_source' member
     may be non-NULL if the associated `jx_metanode' object was created while
     reading a codestream header box, compositing layer header box or
     compositing layer extensions box; in this case, the metanode was created
     to manage metadata found within the relevant box.  So long as one of
     these members is non-NULL, the `asoc' and `box' objects will be empty,
     and `jx_codestream_source::finish', `jx_layer_source::finish' or
     `jx_container_source::finish' should be called whenever more metadata
     is requested by the application.  Once all the imagery headers have been
     recovered from within the relevant box, these pointers are reset to NULL,
     and the relevant header box is transplanted into `asoc', from which
     subsequent metadata boxes may be read.
  */
  
/*****************************************************************************/
/*                              jx_metaparse                                 */
/*****************************************************************************/

struct jx_metaparse {
  public: // Member functions
    jx_metaparse()
      { 
        read = NULL; incomplete_descendants = NULL;
        incomplete_prev = incomplete_next = NULL; incomplete_walker = NULL;
        is_generator = false; num_possible_generators = 0;
        metanode_span = asoc_databin_id = box_databin_id = -1;
        asoc_nesting_level = box_nesting_level = 0;
      }
    ~jx_metaparse()
      { 
        assert(incomplete_walker == NULL);
        if (read != NULL)
          { delete read; read = NULL; }
      }
    void add_incomplete_child(jx_metanode *child);
    void remove_incomplete_child(jx_metanode *child);
      /* The above convenience functions add and remove `child' to/from the
         list of incomplete descendants of the metanode to which this object
         is attached.  The `child' object necessarily has a non-NULL
         `parse_state' member whose `next_incomplete_sibling' and
         `prev_incomplete_sibling' members manage its membership of the list
         of incomplete descendants, that is headed by the current object's
         `incomplete_descendants' member.  Removal from a list of incomplete
         descendants happens right before `child' is destroyed, while addition
         happens immediately after `child' is created and inserted into its
         parent's list of descendants. */
  public: // Data
    jx_metaread *read; // NULL once reading has finished within this metanode
    jx_metanode *incomplete_descendants; // See below
    jx_metanode *incomplete_next; // Used to create doubly-linked list
    jx_metanode *incomplete_prev; // of members in above-mentioned list
    jx_incomplete_metanode_walker *incomplete_walker;
    bool is_generator; // See below
    int num_possible_generators; // See below
    kdu_long metanode_span; // Size of the node's box or asoc container or -1
  public: // Data used to form JPIP metadata requests
    kdu_long asoc_databin_id; // -1 unless using a dynamic cache; see below
    kdu_long box_databin_id; // -1 unless using a dynamic cache; see below
    int asoc_nesting_level; // See below
    int box_nesting_level; // See below  
  };
  /* Notes:
        This object keeps track of all the information that is required only
     while parsing the `metanode' object or any of its descendants.  The
     `read' member is NULL once the contents of the node's box have been
     read, and all of its immediate descendants (if any) have been opened.
     However, this structure must persist until the metanode and all of
     its descendants have been completely parsed, as marked by the setting of
     the `JX_METANODE_IS_COMPLETE' flag.  The object is stored in the
     `jx_metanode::parse_state' member, which is deleted and set to NULL
     from within the `jx_metanode::check_parsing_complete' function.
        The `incomplete_descendants' member points to a doubly-linked list of
     immediate descendants of the object's metanodes that are currently
     incomplete -- i.e., that have non-NULL `jx_metanode::parse_state'.
     Members of the doubly-linked list are connected via their
     `incomplete_next' and `incomplete_prev' members.  Note that the list is
     not ordered in any particular fashion.  Doing so could be costly due
     to the way new incomplete metanodes are spawned from within generator
     nodes in an out-of-order fashion (see below).  Also, ordering the list
     could at most halve the search time for functions like `load_recursive'.
        The `is_generator' flag is set to true if the containing `jx_metanode'
     object exists for structuring purposes only; as its descendants are
     encountered, they are immediately promoted to siblings of the metanode
     (being placed immediately before it in the sibling list) until all
     descendants of the generator have been encountered -- once this happens,
     the generator is removed by calling `jx_metanode::remove_empty_shell'.
     Whenever a grouping box (`jp2_group_4cc') is parsed, it immediately
     becomes a generator.  Also, if `jx_meta_manager::flatten_free_asocs' is
     true, free-asoc boxes also become generators once detected.  A free-asoc
     box is an association box (`jp2_association_4cc') whose first sub-box is
     a free box (`jp2_free_4cc').  All generators are marked with the box type
     `jp2_group_4cc' and a `jx_metanode::rep_id' value of 0, in addition to
     having the `is_generator' flag set to true.
        The `num_possible_generators' member keeps track of the number of
     descendants of the current `jx_metanode' object which are either known
     to be generators or potential generators.  A descendant which is created
     from a `jp2_association_4cc' box, whose first sub-box has not yet
     become available, might be a generator if `manager->flatten_free_asocs'
     is true, because if the sub-box turns out to be a free box, that
     descendant will be marked as a generator, according to the definition
     given above.
        The last four members are used to provide additional information about
     any metadata-bin to which the metanode's box or asoc container belong.
     The node is said to have its own asoc container if it was created from the
     first sub-box of an association box, or if the node was created to
     temporarily hold the members of a grouping box -- in the latter case,
     the `jx_metanode::rep_id' value is 0.
        The `asoc_databin_id' and `asoc_nesting_level' members relate to the
     location of the header of the asoc container (i.e., asoc/group box), if
     any.  This information is used by `jpx_metanode::generate_metareq' to
     generate data-bin-relative metadata requests.
        If there is no asoc container, `asoc_databin_id' will be -ve.  If the
     asoc container is found at the top level of its databin, it will have a
     nesting level of 0.  If it is found at the top level within the contents
     of a superbox, which is at the top level of the databin, the nesting
     level will be 1; and so forth.
        The `box_databin_id' is always non-negative if the data is sourced
     from a dynamic cache.  If `box' is (or will be) found at the top level
     of its databin, it will have a nesting level of 0; and so forth.
        The `metanode_span' member holds the total number of bytes in the
     box which defines this node.  If the metanode is represented by an asoc
     container (i.e., a grouping box or an association box whose first
     sub-box yields the type of the metanode), then this member holds the
     length of the asoc container box.  In any case, `metanode_span'
     runs from the first byte of the header to the last byte of the box
     in question.  If the box in question has rubber length (very unlikely),
     the `metanode_span' value is -1.  If the object's metanode is actually
     the root of the metadata tree, its span corresponds to the entire
     data source, so `metanode_span' is again -1.  To simplify matters,
     this member is initialized to -1 and only takes on another value once
     the node's box or asoc container is actually encountered.
  */

/*****************************************************************************/
/*                          jx_metagroup_writer                              */
/*****************************************************************************/

class jx_metagroup_writer {
  public: // Member functions
    jx_metagroup_writer()
      { active = NULL; group_threshold=8;
        box_idx=group_size=0; grp_box_type=jp2_group_4cc; }
    ~jx_metagroup_writer() { init(0,false,false); }
    void init(int num_boxes_to_expect, bool use_free_asocs,
              bool is_top_level);
      /* Prepares the object to introduce grouping boxes (if required) to
         structure the writing of `num_boxes_to_expect' boxes.  These
         boxes are written to the container retrieved by `get_container' with
         a call to `advance' after each box is written -- thereafter
         `get_container' must be called again because the containing group
         may change.  It is OK to call `init' as often as you like.  It
         is also OK to write boxes after `num_boxes_to_expect' calls to
         `advance', in which case the extra boxes will be written at
         the top level of the `super_box' passed to `get_container' (or of
         the file if `super_box' is NULL).
            If `use_free_asocs' is true, the object will use association
         boxes with an empty first sub-box of type `jp2_free_4cc', rather
         than using the `jp2_group_4cc' box, which was not introduced until
         IS15444-2/Amd-2.
            If `is_top_level' is true, the object is being used to structure
         the writing of boxes that will appear at the top level of the file
         if not grouped within grouping/free-asoc boxes.  In this case, the
         threshold at which grouping is introduced is reduced to 2 boxes,
         as opposed to the normal value which is typically 8.  This is
         because reducing the number of top-level boxes in a file is a very
         important objective for JPIP.
      */
    void advance();
      /* Call this function after opening and closing a sub-box within
         the container returned by `get_container'. */
    jp2_output_box *get_container(jp2_output_box *super_box,
                                  jx_meta_manager *manager,
                                  kdu_long &file_pos);
      /* Returns the container (super-box) into which the next box needs
         to be written as a sub-box -- or else returns NULL if the next
         box needs to be written as a top-level box of `manager->target'.
         This function returns exactly the same result each time it is
         called, at least until the next call to `advance'.  Note that
         the first call after `init' or `advance' may open one or more
         internal grouping boxes to structure the data. */
  private: // Definitions
    struct jx_metagroup_list {
      jp2_output_box grp_box;
      int level; // 0 for a top-most grouping box
      int first_box_in_grp; // Index of first box contained in this group
      int lim_box_in_grp; // 1 + index of last box contained in this group
      jx_metagroup_list *parent; // Points to our container, if any
    };
  private: // Data
    jx_metagroup_list *active; // Points to lowest level group box, if any
    int group_threshold; // Min `group_size' to structure into group boxes
    int group_size; // Total number of boxes currently being structured
    int box_idx; // Index of the current box (0 to `group_size'-1)
    kdu_uint32 grp_box_type; // `jp2_group_4cc' or `jp2_association_4cc'
  };

/*****************************************************************************/
/*                              jx_metawrite                                 */
/*****************************************************************************/

struct jx_metawrite {
  public: // Functions
    jx_metawrite()  { active_descendant=NULL; active_roi=NULL; start_pos=0; }
  public: // Data
    jp2_output_box asoc; // Used to manage an open asoc box.
    jp2_output_box box; // Used to manage a single box, or a sub-box of an asoc
    jx_metanode *active_descendant; // See description of `jx_metanode::write'
    jx_regions *active_roi; // When `active_descendant' scans `region_library'
    jx_metagroup_writer group_writer; // All writing done through this object
    jx_region_library region_library; // Allows structured writing of ROI nodes
    kdu_long start_pos; // Absolute location w.r.t. start of file, or the
                        // first header byte of the node `box' (i.e., first
                        // contents byte of any containing `asoc' box).
  };

/*****************************************************************************/
/*                               jx_metapres                                 */
/*****************************************************************************/

struct jx_metapres {
  public: // Functions
    jx_metapres()
      { asoc_contents_pos=asoc_contents_len=0; contents_pos=contents_len=0; }
  public: // Data
    kdu_long asoc_contents_pos;
    kdu_long asoc_contents_len; // -ve if no asoc container
    kdu_long contents_pos;
    kdu_long contents_len;
  };
  /* Note: this structure is used to preserve location information for
     metanodes that have been written.  It is created only if the
     `JX_METANODE_PRESERVE' flag is set at the point when the
     `JX_METANODE_WRITEN' flag is asserted and `write_state' is deleted,
     but only after the final call to `jx_metanode::write' (i.e., not during
     simulation). */
  
/*****************************************************************************/
/*                               jx_metanode                                 */
/*****************************************************************************/
// The following constants are used by the `jx_metanode::flags' member:
#define JX_METANODE_EXISTING          ((kdu_uint16) 0x0001)
  // If the node is obtained by reading an existing box
#define JX_METANODE_WRITTEN           ((kdu_uint16) 0x0002)
  // If the node has already been written (and `write_state' deleted)
#define JX_METANODE_PRESERVE          ((kdu_uint16) 0x0004)
  // Used only for nodes that can be written.  This flag is set if the
  // node might be the target of a link metanode in the future.  In this
  // case, once the node has actually been written, so that the
  // `JX_METANODE_WRITTEN' flag is set, the `jx_metawrite' object is
  // deleted and replaced by a `jx_metapres' object, referenced by
  // the `preserve_state' member, whose purpose is to keep track of the
  // location and length of the box and/or its asoc container so that
  // these may be used in the future to complete link metanodes.
#define JX_METANODE_IS_COMPLETE       ((kdu_uint16) 0x0008)
  // Set once the node and all its descendants are complete.  During reading,
  // to set this flag, we need BOX_COMPLETE, DESCENDANTS_KNOWN,
  // !UNRESOLVED_LINK and `parse_state->incomplete_descendants' equal to NULL.
  // Once this flag becomes true, `jx_metanode::parse_state' can be deleted.
#define JX_METANODE_BOX_COMPLETE      ((kdu_uint16) 0x0010)
  // If the node's box contents are completely available.
  // If the node has an ASOC container, the node's box is the first sub-box
  // of the ASOC.  If there is a GRP container, the node has no "box",
  // so BOX_COMPLETE is immediately true.  If the node is created by some
  // means other than parsing, the `JX_METANODE_BOX_COMPLETE' flag is always
  // set.
#define JX_METANODE_DESCENDANTS_KNOWN ((kdu_uint16) 0x0020)
  // If number of descendants is known (only for reading).  If there is no
  // GRP or ASOC container, this flag is immediately true.  Otherwise, the flag
  // is not set until CONTAINER_KNOWN is true and
  // `parse_state->num_possible_generators'.
#define JX_METANODE_CONTAINER_KNOWN   ((kdu_uint16) 0x0040)
  // If the number of sub-boxes in the node's ASOC or GRP container is known
  // (only for reading).  Immediately true if there is no ASOC/GRP container.
  // Note that there may be more descendants than the number of sub-boxes in
  // the container, because the contents of GRP boxes or free-ASOC sub-boxes
  // are automatically moved out of their containers to become descendants of
  // the current node.
#define JX_METANODE_UNRESOLVED_LINK   ((kdu_uint16) 0x0080)
  // If the node represents a cross-reference box that could potentially be a
  // link to an as-yet unresolved target node (only for reading).  We do not
  // allow a node to be considered complete until such links are resolved in
  // one way or another, because otherwise we have no way of reliably
  // requesting the missing link target from a JPIP server.
#define JX_METANODE_FIRST_SUBBOX      ((kdu_uint16) 0x0100)
  // If this node is the first sub-box of an ASOC box (only for reading).
#define JX_METANODE_HAS_NON_ROI_CHILD ((kdu_uint16) 0x0200)
  // If a node has immediate descendants that are not ROI, group or free nodes
#define JX_METANODE_HAS_ROI_CHILD     ((kdu_uint16) 0x0400)
  // If a node has immediate descendants that are ROI nodes
#define JX_METANODE_LOOP_DETECTION    ((kdu_uint16) 0x0800)
  // Used to detect loops within the `jx_meanode::find_path_to' function
#define JX_METANODE_DELETED           ((kdu_uint16) 0x1000)
  // If the node has been moved to the deleted list
#define JX_METANODE_CONTENTS_CHANGED  ((kdu_uint16) 0x2000)
  // If a pre-existing node's contents have been changed
#define JX_METANODE_ANCESTOR_CHANGED  ((kdu_uint16) 0x4000)
  // If a pre-existing node's parent has been changed
#define JX_METANODE_CHILD_REMOVED     ((kdu_uint16) 0x8000)
  // If any direct descendant is deleted or moved

#define JX_NULL_NODE     ((kdu_byte) 0) // `rep_id' 0 until reading complete
#define JX_REF_NODE      ((kdu_byte) 1)
#define JX_NUMLIST_NODE  ((kdu_byte) 2)
#define JX_ROI_NODE      ((kdu_byte) 3)
#define JX_LABEL_NODE    ((kdu_byte) 4)
#define JX_CROSSREF_NODE ((kdu_byte) 5)

struct jx_metanode {
  private:
    friend struct kd_supp_local::jx_meta_manager;
    ~jx_metanode(); // This function should be called only from the
                    // meta-manager's own destructor, while cleaning up
                    // the `deleted_nodes' list.  Nodes should first be
                    // moved onto this list from the metadata tree, using
                    // the `safe_delete' function.
  public: // Member functions
    jx_metanode(jx_meta_manager *manager)
      { memset(this,0,sizeof(*this)); this->manager = manager; }
    void safe_delete();
      /* This function is used to remove a node from the metadata tree,
         unlinking it from its parent, from numlist/region libraries, and
         recursively doing the same for all its children.  Rather than actually
         deleting the node, or its embedded data, however, the node is
         moved onto the meta-manager's list of deleted nodes.  This allows
         persistet references even after removal from the metadata tree. */
    void remove_empty_shell();
      /* This function should be called after `finish_reading' returns
         successfully, if `rep_id' is 0 and `parse_state' has become NULL.
         This currently happens only for "generators" (see `jx_metaparse')
         after their contents have been fully extracted.
            Note that this is a self-efacing member function that deletes
         the owning object.  That is why the function is not called
         automatically from within `finish_reading'.  The caller must
         be aware that the object ceases to exist after the function has
         been called. */
    void unlink_parent(bool from_empty_shell=false);
      /* Does most of the work of `safe_delete'.  Unlinks the current object
         from its parent, updating counters and state variables as appropriate.
         This function also may need to adjust the parent's
         `JX_METANODE_HAS_NON_ROI_CHILD' and/or `JX_METANODE_HAS_ROI_CHILD'
         flags.  If the function is invoked from within `remove_empty_shell',
         the `from_empty_shell' argument should be set to true; in this
         case, the parent from which we are being removed is not appended
         to the touch list and is not marked as having lost a child.
         The function also adjusts any reference to the node that is being
         unlinked from within `manager' or a JPX container's set of embedded
         metanodes. */
    void delete_useless_numlists();
      /* This function is invoked on a node from which a descendant has
         been removed by an interactive user that results in a call to
         `jpx_metanode::delete_node' or `jpx_metanode::change_parent'.  The
         function does nothing unless the current node is a number list that
         has no more descendants.  In this case, if the node is a top level
         number list or the descendant of another number list, the node is
         automatically deleted (by a call to `safe_delete').  Moreover, in
         the latter case, the function is invoked recursively on the parent
         node. */
    void append_to_touched_list(bool recursive);
      /* This function is used to append a node to the manager's touched list,
         being careful to first remove it from the list, if it is already
         there.  If `recursive' is true, the function recursively appends
         the node's descendants also to the touched list.  The function also
         includes the JX_METANODE_ANCESTOR_CHANGED flag to the node if its
         parent has any of the change flags set. The function does nothing
         if the node does not have the JX_METANODE_BOX_COMPLETE flag set. */
    void place_on_touched_list();
      /* This function is used in place of `append_to_touched_list' when
         either of the two auxiliary conditions described under
         `jpx_meta_manager::get_touched_nodes' occurs -- these conditions
         relate to information about the immediate descendants of the node
         becoming available (number of children or availability of a child).
         An application may need to be informed of these changes where a
         dynamic cache is growing in unexpected ways, but it does not need
         to be guaranteed that nodes added to the touched list for any of
         these reasons alone will appear before any descendants on the
         touched list.  For this reason, the present function does not move
         the node if it is already on the touched list. */
    void insert_child(jx_metanode *child, jx_metanode *insert_after,
                      jp2_locator loc=jp2_locator());
      /* Use with a newly created `child' object or one which has recently
         been unlinked from its parent.  If `child' is created by parsing a
         data source, you must make sure that `loc' is a non-empty interface.
         In this case, the following things happen:
         [>>] The `JX_METANODE_EXISTING' flag is set;
         [>>] The `child->parse_state' object is created and appropriately
              initialized;
         [>>] `loc' is used to determine a value for `child->sequence_index',
              adjusting `manager->last_sequence_index' if necessary, so as to
              ensure that it holds the largest sequence index so far.
         [//]
         Otherwise, if `loc' is an empty interface, the `child->sequence_index'
         value is set to the value obtained after incrementing the value of
         `manager->last_sequence_index' -- such sequence indices should be
         unique, unless you interleave parsing operations with the addition
         of novel metadata, created by the application.
         [//]
         If `manager->target' is non-NULL, some additional bookkeeping is
         required to make sure that the `manager' members remain correct.  If
         `child' is being inserted as a descendant of the metadata root, the
         function ensures that the `manager->first_unwritten' member is
         updated as appropriate.  The function also needs to verify that
         `child' does not contain an unresolved link within its descendants --
         if so, the `manager->note_unwritten_link_target' function must be
         invoked to ensure that `manager->last_unwritten_with_link_targets' is
         kept up to date.  Of course, this is unlikely to prove an issue in
         practice, but it is possible that an application will enable
         interactive metadata editing directly within a `jpx_target' object,
         and that this application also wants to perform incremental
         generation of content and metadata, so that the
         `manager->last_unwritten_with_link_targets' member should not lag
         behind the last top-level metadata node that actually does have
         unresolved links.
         [//]
         If you are using this function to insert a node whose box contents
         are known (i.e. `JX_METANODE_BOX_COMPLETE' is true), you should be
         sure to set the `box_type' of the new `child' node, before calling
         this function, so that the `JX_METANODE_HAS_NON_ROI_CHILD' and
         `JX_METANODE_HAS_ROI_CHILD' flags get correctly updated.  Otherwise,
         these flags are set when the `JX_METANODE_BOX_COMPLETE' flag is first
         set within the `finish_reading' function.
         [//]
         The `child' node is placed immediately after `insert_after' unless
         it is NULL, in which case it is placed at the start of the
         descendants list.
      */
    void read_and_insert_child(jp2_input_box &box, int databin_nesting_level);
      /* This function is invoked by `jx_codestream_source::finish',
         `jx_layer_source::finish' or `jx_container_source::finish', to
         insert a child node that is obtained by parsing the contents of
         the supplied `box'.  Upon return, `box' will be closed -- its
         contents may have been transplanted for further parsing internally
         when more content becomes available.  The function invokes
         `donate_input_box' and then attempts to `finish' the inserted node.
            The `databin_nesting_level' argument is passed to
         `donate_input_box'; it represents the nesting level for the contents
         of the super-box in which `box' is found, relative to its databin. */
    void check_roi_child_flags();
      /* This function is called if the `JX_METANODE_HAS_NON_ROI_CHILD' and/or
         `JX_METANODE_HAS_ROI_CHILD' flags may potentially need to be
         re-evaluated -- typically because a non-ROI child has been deteted,
         changed or moved. */
    bool check_container_compatibility(jx_container_base *new_container);
      /* Returns false if the current node or any of its descendants is a
         number list that is not compatible with embedding inside the
         `new_container'.  This function should always be called prior to
         `change_container', unless `new_container' is NULL (in which case
         this function will always return true). */
    void change_container(jx_container_base *new_container);
      /* This function is called if the JPX container (if any) within which
         the node is embedded may have changed.  This function descends
         recursively through the node's descendants, looking for number
         lists and changing their `jx_numlist::container' member.  In the
         process, the number list indices may need to be modified to ensure
         that non-top-level indices are base indices for the container;
         this may cause indices to become misordered, or redundant indices
         to arise, so the function must take care to avoid these problems.
         Incompatibility triggers an assert() failure, because you should
         always invoke `check_container_compatibility' before passing a
         non-NULL `new_container' argument to this function.
         [//]
         Note that any required changes in `jx_container_base::first_metanode'
         and `jx_container_base::last_metanode' pointers or the relative
         position of a top-level node amongst its siblings that may be
         required as part of the change of container embedding (see
         notes at the end of `jx_container_base') must be performed separately
         by the caller.  As it turns out, this function will never
         be used to embed a top level number list inside a JPX container, but
         it may be used to signal a loss of container membership that comes
         from changing a top level number list node to some other type of
         metanode.  In that case, the caller must take or have taken
         steps to ensure that the node is moved outside the set of top-level
         metanodes delimited by the container's `first_metanode' and
         `last_metanode' pointers.  This is achieved by invoking the
         `move_old_top_container_numlist' function.
      */
    bool can_write()
      { /* Returns true if the object holds a node that can be written out to
           a JPX file. This is false if the box is not complete or holds a
           cross-reference that has no resolved link. */
        if ((rep_id == 0) || (flags & JX_METANODE_WRITTEN) ||
            !(flags & JX_METANODE_BOX_COMPLETE))
          return false;
        if (rep_id == JX_CROSSREF_NODE)
          return (crossref->link != NULL);
        return true;
      }
    bool is_externally_visible()
      { /* Returns true if the object holds a non-root node that can be
           presented externally via a `jpx_metanode' interface.  */
        assert(parent != NULL);
        if ((rep_id == 0) || !(flags & JX_METANODE_BOX_COMPLETE))
          return false;
        if ((flags & JX_METANODE_EXISTING) && (rep_id == JX_NUMLIST_NODE) &&
            (head == NULL) && (parse_state != NULL) &&
            (parse_state->read != NULL) &&
            ((parse_state->read->codestream_source != NULL) ||
             (parse_state->read->layer_source != NULL) ||
             (parse_state->read->container_source != NULL)))
          return false;
        return true;
      }
    bool is_empty_numlist()
      { return ((rep_id==JX_NUMLIST_NODE) && (numlist->num_codestreams==0) &&
                (numlist->num_compositing_layers==0) &&
                !numlist->rendered_result); }
      /* Returns true if the object represents a number list that is being
         used only as an internal placeholder (typically for JPX container
         embedded metadata) that cannot be written to a JPX file. */
    bool is_top_container_numlist()
      { return ((rep_id == JX_NUMLIST_NODE) && (numlist->container != NULL) &&
                (parent->parent == NULL)); }
      /* Returns true if the object holds a top-level number list node that
         is embedded within a JPX container. */
    void move_old_top_container_numlist(jx_container_base *container);
      /* This function is called if a number list that was embedded
         within `container' is changed to one that is not -- its type may
         also be changed, so the original container is no longer accessible
         internally, but is passed in here.  The function does nothing unless
         `container' is non-NULL and the node is a top-level metanode.  If
         these conditions are met and the container is marked as not parsed
         and not written, the function moves the node to a different position
         in its sibling list, so as to ensure that it does not lie within the
         span of top level metanodes delimited by `container->first_metanode'
         and `container->last_metanode'. */
    jx_container_base *find_container();
      /* Scans up through the metanode's descendants looking for a number
         list node; the first such node's `jx_numlist::container' member is
         returned. */
    jx_metanode *find_link_target();
      /* Descends recursively into the current node's descendants, returning
         the first node with a non-NULL `linked_from' member or the
         `JX_METANODE_PRESERVE' flag. */
    jx_metanode *
      add_numlist(int num_codestreams, const int *codestream_indices,
                  int num_compositing_layers, const int *layer_indices,
                  bool applies_to_rendered_result,
                  jx_container_base *container=NULL,
                  jp2_locator loc=jp2_locator(), bool no_touch=false);
      /* Does most of the work of `jpx_metanode::add_numlist', but is
         also used to create a numlist node to hold metadata found in
         compositing layer header boxes while reading.  The function sets
         the `JX_METANODE_BOX_COMPLETE' flag.  The `loc' argument is passed
         to `insert_child', where it is used to determine the value of
         the new node's `sequence_index' and also whether or not its
         `JX_METANODE_EXISTING' flag should be set.
            The supplied codestream and compositing layers are separately
         ordered, from smallest to largest codestream index and smallest
         to largest compositing layer index when stored internally.
            The `container' argument is non-NULL if this number list is
         embedded within a JPX container; if so, the codestream indices and
         layer indices supplied here must be compatible with the `container';
         the function automatically converts any container-defined layer or
         codestream indices to the corresponding base indices.  Moreover,
         the function makes sure that any top-level number list that
         has `container' != NULL and !`container->parsed' appears in the
         correct position relative to its siblings, so that it lies within
         the span of nodes delimitted by `container->first_metanode' and
         `container->last_metanode' -- at the same time the function updates
         these delimiting pointers, if necessary, and also verifies that
         `container->written' is false.
            If `no_touch' is true, the object is not added to the metadata
         manager's touched list -- this feature is used only when creating
         synthetic number lists to capture metadata that may lie within a
         JPLH, JPCH or JCLX box, before we actually know whether or not there
         is any such data.
       */
    bool count_numlist_descendants(int &count);
      /* Implements `jpx_metanode::count_numlist_descendants'.  Assumes
         `count' is initialized to zero within that function. */
    void donate_input_box(jp2_input_box &src, int databin_nesting_level);
      /* This function may be called from `jx_source::parse_next_top_box' when
         a top-level metadata or association box is encountered in a JPX
         data source, or from `jx_metanode::finish_reading' when a sub-box
         of an asoc or grouping box is encountered.  The function is also
         invoked by `read_and_insert_child', which is called from
         `jx_codestream_source::finish', `jx_layer_source::finish' and
         `jx_container_source::finish' when a metadata box of interest or an
         association box is encountered inside a codestream header box,
         compositing layer header box, or compositing layer extensions box.
            The `src' box need not be complete.  Its state is donated to the
         internal `asoc' or `box' member of a `jx_metaread' object which is
         created and assigned to the `read' object referenced by a newly
         created `parse_state' member.
            The `databin_nesting_level' identifies the nesting level of the
         box contents within which `src' is found, relative to its databin,
         following the definition given for `jx_metaparse::box_nesting_level'.
         The value of this argument is irrelevant if this box is not sourced
         from a dynamic cache.
            After calling this function, you should always invoke
         `finish_reading' and then check to see if `remove_empty_shell'
         needs to be invoked from the parent -- see `finish_reading' for more
         on this. */
    bool finish_reading(kdu_long link_pos=-1);
      /* This function should be called immediately after `donate_input_box',
         but may generally need to be called from other contexts if the
         ultimate data source is fueled by a dynamic cache whose contents are
         not yet sufficiently complete to parse all required boxes.  The
         function continues to have an effect until both the
         `JX_METANODE_BOX_COMPLETE' and the `JX_METANODE_CONTAINER_KNOWN' flags
         are set.  In the special case where `link_pos' is non-negative,
         the function stops trying to finish parsing the node's descendants
         once it encounters one whose original file position is greater than
         or equal to `link_pos'.  The function is called in this way only from
         within `load_recursive'.
            The function returns true when all reading of boxes at the
         top-level of the node is complete.  This does not mean that all
         descendants are yet known, because some of them might be found
         within "generator" nodes (see `jx_metaparse' for more on this).
         To ensure that all immediate descendants are known, we must at least
         finish reading all generators that are found as descendants of the
         node.  If, however, the function returns true when invoked on a
         generator node, the node should be empty, because the recovered
         sub-boxes are moved outside the generator as soon as they are
         detected.  This eventuality can be detected by checking for `rep_id'=0
         as mentioned below.
            After this function returns true, you should check to see
         if a generator node has been completely emptied.  This is readily
         done by checking `rep_id'=0 and `parse_state'=NULL.  If both
         conditions hold after the function returns true, the generator
         should be removed from its list of siblings by invoking
         `remove_empty_shell'.  This is left to the caller because
         we don't want to remove a node from within one of its own member
         functions.  Apart from the above condition, it is also possible
         that the object's `safe_delete' function has been invoked, in which
         case the node will no longer have any parent, descendants or
         siblings, but `rep_id', `box_type' and `flags' are still meaningful.
            The present function may legally be invoked on the root of the
         metadata tree, in which case the behaviour is rather different -- in
         that case the function attempts to parse whatever boxes remain from
         the top-level of the file, so as to recover all top-level nodes of
         the metadata hierarchy.  When invoked on the root of the
         metadata hierarchy, the function always returns false to prevent
         inappropriate calls to `remove_empty_shell' from being accidentally
         generated by the caller. */
    void check_parsing_complete();
      /* This function is called only while reading existing boxes from a
         JP2/JPX data source, whenever something changes that may render
         a metanode complete.  For a metanode to be complete, we must have
         the `JX_METANODE_BOX_COMPLETE' and `JX_METANODE_CONTAINER_KNOWN'
         flags both set and the `JX_METANODE_UNRESOLVED_LINK' flag reset.
         We must also have `parse_state->num_possible_generators'=0 and
         `parse_state->incomplete_descendants'=NULL.  The function is called
         once `JX_METANODE_BOX_COMPLETE' becomes true and also whenever any of
         the other conditions may have become true.  The
         other conditions are all checked.  If `JX_METANODE_CONTAINER_KNOWN'
         is true and `parse_state->num_possible_generators' is 0, the
         function automatically sets the `JX_METANODE_DESCENDANTS_KNOWN' flag.
         If the conditions do all hold, the function sets the
         `JX_METANODE_IS_COMPLETE' flag, deletes the `parse_state' member,
         and updates its parent's number of completed descendants,
         propagating any completeness changes that result up through the
         node's ancestors. */
    void make_complete();
      /* This function is called from the `jpx_metanode::change_...' functions
         to force an existing node and all of its descendants to appear to
         have been completely parsed.  The function scans through all the
         immediate descendants, deleting any generator nodes or nodes for which
         `JPX_METANODE_BOX_COMPLETE' is not yet set, and recursively invoking
         itself on the others.  By the time the function returns, the current
         node's `JPX_METANODE_IS_COMPLETE' and `JPX_METANODE_DESCENDANTS_KNOWN'
         flags should be set and the `parse_state' member should be NULL. */
    bool load_recursive(kdu_long link_pos=-1);
      /* Recursively passes through the metadata tree, trying to finalize as
         many nodes as possible.  If `link_pos' is >= 0, this function skips
         over nodes that cannot contain a box whose contents start at
         the absolute file location `link_pos'.  The function is invoked in
         this way only when trying to resolve the target of a potential
         link metanode.  The function returns true if either the node is
         deleted through the call (can happen for "generator nodes") or the
         node otherwise becomes complete as a result of the call. */
    jx_metanode *find_path_to(jx_metanode *tgt, int descending_flags,
                              int ascending_flags, int num_exclusion_types,
                              const kdu_uint32 *exclusion_box_types,
                              const int *exclusion_flags, bool unify_groups);
      /* Recursive function to implement the `jpx_metanode::find_path_to'
         function for all but the simplest case in which we ignore
         links between nodes in the metadata graph. */
    bool check_path_exclusion(int num_exclusion_types,
                              const kdu_uint32 *exclusion_box_types,
                              const int *exclusion_flags);
      /* This function is used by `find_path_to' to check exclusion conditions.
         Returns true if the current node is to be excluded. */
    void clear_write_state(bool reset_links);
      /* Recursively passes through the node and all its descendants, resetting
         the `JX_METANODE_WRITTEN' flag and removing any `write_state' which
         might exist for any reason.  This is done at the start of each write
         phase by the `jx_meta_manager::write_metadata' function.  When
         executed on the metadata hierarchy's root node, the function limits
         its attention to top-level descendants which lie between
         `manager->first_unwritten' and `manager->last_to_write'.
            If `reset_links' is true and the function traverses the
         `linked_from' lists found at each visited node, resetting the
         linking nodes' target address information.  This should be done
         before any simulation phase, since the purpose of the simulation
         phase is to discover the locations at which each node is to be
         written and fill in the target address information within each
         linking node. */
    jp2_output_box *write(jp2_output_box *super_box, int *i_param,
                          void **addr_param, kdu_long &file_pos);
      /* This function recursively passes through the metadata tree, writing
         the nodes that it encounters.  The function opens internal super-boxes
         as necessary and keeps track of an `active_descendant' inside
         each node.  The function even constructs a temporary region
         library to facilitate structured writing of ROI descendants.
         The function returns prematurely if it encounters any node whose box
         contents must be written by the application.  In this case, it
         sets the *`i_param' and *`addr_param' values appropriately and returns
         with a pointer to the open box whose contents are to be written by
         the application.  The function can be called again to pick up from
         where it left off.  If `super_box' is NULL, the first box is opened
         as a top level box within the `target' object.  Otherwise, `target'
         is not used.
            The function automatically organizes descendants of a node into
         groups via the `jx_metagroup_writer' class, if there are a large
         number of them.  The function also treats ROI nodes specially;
         whenever an ROI descendant is encountered, it is immediately added
         to the `jx_metawrite::region_library'.  Once all other descendants
         have been written, the function writes ROI nodes from the library --
         if this is happening when we come back after premature return,
         `jx_metawrite::active_descendant' will point to an ROI node.
            If `super_box' is non-NULL, the function writes a sub-box of the
         supplied super box.  Otherwise, the function writes a top-level box
         to the `jx_target' object found in `manager->target'.
            The `file_pos' argument is used to keep track of the location
         to which each box is written (or would be written if we are
         simulating) within the file.  This is important for resolving
         cross references.  On entry to this function, `file_pos' refers to
         the first byte position at which any box header associated with
         this object will be written.  You should be careful to preserve
         the state of `file_pos' across multiple invocations of this function.
       */
  public: // Data
    jx_meta_manager *manager;
    kdu_uint32 box_type;
    kdu_uint16 flags; // JX_METANODE_xxx where xxx includes: IS_COMPLETE,
       // BOX_COMPLETE, CONTAINER_KNOWN, DESCENDANTS_KNOWN, UNRESOLVED_LINK,
       // HAS_NON_ROI_CHILD, HAS_ROI_CHILD, EXISTING, WRITTEN, DELETED,
       // CONTENTS_CHANGED and ANCESTOR_CHANGED
    kdu_byte rep_id; // One of the constants defined above
    union {
        jx_metaref *ref; // NULL unless box identified as delayed or existing
        jx_numlist *numlist; // NULL unless this is a Number List node
        jx_regions *regions; // NULL unless this is an ROI Description node
        char *label; // NULL unless this is a label node
        jx_crossref *crossref; // NULL unless this is a cross-reference node
      };
    union {
        jx_metaparse *parse_state; // Created for metanodes that are derived
           // by reading from a JPX data source; becomes NULL only once the
           // `JX_METANODE_IS_COMPLETE' flag is set.
        jx_metawrite *write_state; // Non-NULL only after writing started
        jx_metapres *preserve_state; // Valid if `JX_METANODE_WRITTEN' and
                                     // `JX_METANODE_PRESERVE' flags both set
      };
    kdu_long sequence_index; // Obtained when first added
    void *app_state_ref; // Used to record application-specific state reference
    jx_metanode *parent; // Null if this is the root
    jx_metanode *head; // Head of descendants list; NULL if this is a leaf node
    jx_metanode *tail; // Tail of descendants list; NULL if this is a leaf node
    jx_metanode *next_sibling; // Used to link the children of a single parent
    jx_metanode *prev_sibling; // List is doubly linked
    jx_crossref *linked_from; // List of all cross-references which link to us
    jx_metanode *next_touched; // Used to link into touched node list
    jx_metanode *prev_touched; // Also doubly linked
  };
  /* Notes:
        Some care needs to be taken with a node's descendants list during
     reading, in order to correctly handle grouping boxes, even if the
     data becomes available out of order (happens when reading from a
     dynamic cache).  Specifically, whenever a new sub-box of a grouping
     box becomes available, the new sub-box is promoted as sibling of the
     grouping box.  More generally, whenever a new immediate descendant
     of any "generator" node is encountered, that descendant is inserted as
     the immediately preceding sibling of the generator node.  This policy
     ensures that the descendants of generator nodes are flattened during
     the reading process.  Generator nodes include grouping boxes and
     free-asoc boxes (association boxes whose first sub-box is a free box)
     if `manager->flatten_free_asocs' is true.
        Since grouping boxes' descendants are promoted to the level of their
     parents, we need to keep track of the number of descendants of a node
     which could potentially generate more descendants.  These include
     grouping boxes which are not yet completely parsed, as well as asoc
     boxes whose box-type is not known (could be of type `jp2_free_4cc')
     unless `manager->flatten_free_asocs' is false.  The number of such
     boxes is maintained by `parse_state->num_possible_generators'.
  */

/*****************************************************************************/
/*                      jx_incomplete_metanode_walker                        */
/*****************************************************************************/
  
struct jx_incomplete_metanode_walker {
  public: // Functions
    jx_incomplete_metanode_walker() { node = NULL; }
    bool is_attached() const { return (node != NULL); }
    bool operator!() const { return !is_attached(); }
    void detach()
      { // Disconnects the walker from any node.
        if (node == NULL) return;
        assert((node->parse_state != NULL) &&
               (node->parse_state->incomplete_walker == this));
        node->parse_state->incomplete_walker = NULL;
        node = NULL;
      }
    void attach(jx_metanode *incomplete)
      { // The `incomplete' node must be non-NULL and have a non-NULL
        // `parse_state'.
        assert((incomplete != NULL) && (incomplete->parse_state != NULL) &&
               (incomplete->parse_state->incomplete_walker == NULL));
        detach(); // Remove any connection we already have to a node
        incomplete->parse_state->incomplete_walker = this;
        node = incomplete;
      }
    jx_metanode *advance()
      { // Advances to the next incomplete node from the one that is currently
        // referenced (if any), returning the node that was referenced prior
        // to the advance operation.
        jx_metanode *result = node;
        if (result != NULL)
          { 
            detach();
            jx_metanode *next = result->parse_state->incomplete_next;
            if (next != NULL)
              { 
                assert((next->parse_state != NULL) &&
                       (next->parse_state->incomplete_walker == NULL));
                next->parse_state->incomplete_walker = this;
                this->node = next;
              }
          }
        return result;
      }
  public: // Data
    jx_metanode *node;
  };
  /* Notes:
        This structure is used to robustly walk through a list of
     incomplete metanodes.  Lists of such nodes are linked via the
     `incomplete_prev' and `incomplete_next' links that are found within
     the `jx_metaparse' member that is found within their `parse_state'
     object.  The walker deposits a reference to itself within the
     parse_state of the next object in the list that it intends to visit.
     If any parsing operations performed in the meantime cause the object
     to which the walker refers to be completed (or even deleted), the walker
     is automatically advanced to reference the next element of the list, if
     there is one.  This avoids any risk of invalid pointer deferences
     while attempting to walk through and parse a list of incompletely
     parsed metanodes.
  */
  
/*****************************************************************************/
/*                                jx_metaloc                                 */
/*****************************************************************************/
// Constants:
#define JX_METALOC_IS_BLOCK ((jx_metanode *) _kdu_long_to_addr(2))
 
struct jx_metaloc {
  public: // Convenience functions
    jx_metaloc() { loc = 0;  target = NULL; }
    bool is_block() { return (target == JX_METALOC_IS_BLOCK); }
    kdu_long get_loc() { return loc; }
  private: // Don't allow others to change `loc' accidentally
    friend class kd_supp_local::jx_metaloc_manager;
    kdu_long loc;
  public: // Data
    jx_metanode *target; // Can carry special values (see below)
  };
  /* Notes:
       This object is used in conjuction with `jx_metaloc_block' and
     `jx_metaloc_manager' to manage location information for metanodes.
     Location information is kept for metanodes when they are obtained
     by reading from a file or a cache.  Location information is also
     kept when a metanode is created by copying from another metanode,
     which may belong to a different meta-manager.  Location information
     allows metanodes to be resolved from locations, which is particularly
     important when resolving links contained within cross-reference boxes.
       A location is identified by the `loc' member, as follows:
     -> For boxes found in a file, the `loc' member holds the location of
        the first byte of the box contents within the file.
     -> For boxes found in a cache, the `loc' member holds the location of
        the first byte of the box contents within its original file, as
        reconstructed from the relevant placeholder boxes.  This information
        can be obtained by adding the length of the box header to the value
        returned by `jp2_input_box::get_locator().get_file_pos()'.
     -> For metanodes obtained by copying other metanodes, the `loc'
        member holds the address of the relevant `jx_metanode' object,
        cast as an integer (usually a 64-bit integer, unless kdu_long is
        defined to be 32-bits, which is highly unlikely).
       For metanodes which are associated with an asoc box read from a file
     or cache, there are two separate locators: one for the first byte of
     the contents of the asoc box; and one for the first byte of the
     contents of its first sub-box (the one which gives the metanode its
     type).
       In some cases, the `target' of a locator may not be known.
     This happens when a link (represented by a cross-reference box) is
     encountered and the metanode (if copied) or box (if read) to which it
     refers is not yet available.  In this case, a `jx_metaloc' locator
     is added as the target of the link with a NULL `target' member, to be
     filled in once the associated metanode is discovered and associated
     with the locator.
       The `target' member can also take on the special, value of
     `JX_METALOC_IS_BLOCK', which is neither a valid address, nor NULL.
     In this case, the `loc' member refers to the first
     location represented by an ordered block of locators or sub-blocks.
     See `jx_metaloc_block' for an explanation of how an efficiently
     searchable and expandable tree of meta-locators is built using this
     mechanism. */
 
/*****************************************************************************/
/*                             jx_metaloc_block                              */
/*****************************************************************************/
// Constants:
#define JX_METALOC_BLOCK_DIM 16 // Must be at least 2 (at least binary tree)

struct jx_metaloc_block : jx_metaloc {
    jx_metaloc_block()
      { memset(this,0,sizeof(*this)); target=JX_METALOC_IS_BLOCK; }
    jx_metaloc_block *parent; // NULL if this is the root container
    int active_elts; // Number of elements from `elts' array which are used
    jx_metaloc *elts[JX_METALOC_BLOCK_DIM]; // Pointers to locators or blocks
  };
  /* Notes:
       This object provides the mechanism to build an efficient structure for
     searching and dynamically updating meta-data locators.  The structure
     is a tree, in which each node has up to JX_METALOC_BLOCK_DIM branches.
     The actual number of branches is recorded in the `active_elts' member
     and the addresses of the branches are stored in the `elts' array.  Each
     branch in the `elts' array may point either to a `jx_metaloc' object
     (a leaf in the tree) or another `jx_metaloc_block' object (i.e.,
     a node with further branches).  You can readily rearrange the branches
     in a block to insert new locations or blocks of locations.  The
     allocation and deallocation of `jx_metaloc' and `jx_metaloc_block'
     objects is handled by `jx_metaloc_manager' in a manner which avoids
     excessive memory fragmentation or excessive calls to new and delete.
     To determine whether an element referenced by the `elts' array can
     be cast to type `jx_metaloc_block', you have only to test its type via
     the `jx_metaloc::is_block()' convenience member function. */ 

/*****************************************************************************/
/*                           jx_metaloc_manager                              */
/*****************************************************************************/

class jx_metaloc_manager {
  public: // Member functions
    jx_metaloc_manager();
    ~jx_metaloc_manager(); // Deallocates `locator_heap' and `block_heap' lists
    jx_metaloc *get_locator(kdu_long file_pos, bool create_if_necessary);
    jx_metaloc *get_copy_locator(jx_metanode *node, bool create_if_necessary)
      { return get_locator(_addr_to_kdu_long(node),create_if_necessary); }
      /* The above functions search for an existing locator first.  If none
         is found, the function either returns NULL or creates a new locator,
         depending on the `create_if_necessary' argument.  The first function
         identifies the location of the first byte of the relevant box
         contents within its original file (even if it is actually obtained
         from a cache).  The second function identifies an existing
         `jx_metanode' object as the source of a copy operation.  The
         returned object, if non-NULL, is guaranteed to be a leaf in the
         tree (i.e., not of type `jx_metaloc_block'). */
  private: // Internal types
#   define JX_METALOC_ALLOC_DIM 64 // Num locators/blocks to allocate at once
    struct jx_metaloc_alloc {
        jx_metaloc_alloc() { free_elts = JX_METALOC_ALLOC_DIM; }
        int free_elts; // Number of locators not yet allocated
        jx_metaloc locators[JX_METALOC_ALLOC_DIM];
        jx_metaloc_alloc *next;
      };
    struct jx_metaloc_block_alloc {
        jx_metaloc_block_alloc() { free_elts = JX_METALOC_ALLOC_DIM; }
        int free_elts; // Number of locators not yet allocated
        jx_metaloc_block blocks[JX_METALOC_ALLOC_DIM];
        jx_metaloc_block_alloc *next;
      };
  private: // Functions
    jx_metaloc *allocate_locator()
      {
        jx_metaloc_alloc *heap = locator_heap;
        if ((heap == NULL) || (heap->free_elts == 0))
          {
            heap = new jx_metaloc_alloc;
            heap->next = locator_heap;  locator_heap = heap;
          }
        return (heap->locators + (--heap->free_elts));
      }
    jx_metaloc_block *allocate_block()
      {
        jx_metaloc_block_alloc *heap = block_heap;
        if ((heap == NULL) || (heap->free_elts == 0))
          {
            heap = new jx_metaloc_block_alloc;
            heap->next = block_heap;  block_heap = heap;
          }
        return (heap->blocks + (--heap->free_elts));
      }
    void insert_into_metaloc_block(jx_metaloc_block *container,
                                   jx_metaloc *elt, int idx_of_predecessor);
      /* This function inserts `elt' into the location immediately
       following `idx_of_predecessor' (may be -ve if `elt' is supposed to
       be inserted into the first position of the containing metaloc block).
       If the container is not already full, this is readily accomplished
       by shuffling the existing elements (if required).  If the container is
       full, the function invokes itself recursively to insert a new
       metaloc block into the container's parent and redistribute elements
       between the current container and the new one, so as to encourage the
       formation of balanced trees.  The recursive application of this
       function may result in the entire tree becoming deeper, with a new
       root node. */
  private: // Data
    jx_metaloc_block *root; // Top of the search tree
    jx_metaloc_alloc *locator_heap;
    jx_metaloc_block_alloc *block_heap;
  };

/*****************************************************************************/
/*                          jx_meta_target_level                             */
/*****************************************************************************/

struct jx_meta_target_level {
  public: // Member functions
    jx_meta_target_level(jx_target *tgt, jx_meta_target_level *parent)
      { // `parent' is NULL for the top level
        this->target=tgt;  this->parent=parent;  this->group=NULL;
        this->box_start_pos=0;  header_length=0; content_bytes=0;
        committed_bytes = 0;  num_committed_groups = 0;
      }
    ~jx_meta_target_level();
    bool is_committed() { return (header_length > 0); }
    kdu_long fit_collection(kdu_long collection_bytes,
                            kdu_long typical_collection_bytes,
                            kdu_long preferred_level_bytes,
                            kdu_long expected_start_pos,
                            bool commit);
      /* This is actually the single most important function in implementing
         the functionality of the owning `jx_meta_target' object.  The
         function determines whether or not a collection of size
         `collection_bytes' can be accommodated within the span of the
         current level or any of its sub-levels.  If so, the function returns
         the file address at which we expect to write the collection; if
         not, the function returns -1.
            It is important to realize that `jx_meta_target_level' objects
         are created initially in an uncommitted state.  That means that no
         box has been written to the file and so `box_start_pos',
         `content_bytes', `committed_bytes', `header_length' and
         `num_committed_groups' members will all be 0.  It may be that the
         current level has been committed -- meaning that a box has been
         written and closed again, having a fixed size -- but an internal
         `group' member has not yet been committed.  If the `commit' argument
         is true, the caller is requesting any uncommitted boxes to be written
         and closed.  When this happens, the `box_start_pos', `content_bytes'
         and `header_length' members of the newly committed object become
         non-zero and the committed box's total size shows up as
         `committed_bytes' within any containing `jx_meta_target_level' object;
         that object's `num_committed_groups' member is also augmented at
         that time.
            This function is recursive, in the sense that it descends into
         `group' members as required.  Moreover, the `commit'
         request is applied recursively.  If an existing committed `group'
         descendant cannot acommodate the `collection_bytes', the function
         detetes it, possibly creating a new one.  If there is no `group'
         descendant and the function determines that the current level
         should be arranged into groups, each with its own
         `jx_meta_target_level', the first such `group' is created, but
         left in the uncommitted state, unless `commit' is true.
            The `preferred_level_bytes' and `expected_start_pos' arguments
         are ignored if the current object has already been committed;
         otherwise, these are used to deduce the location, header length
         and contents length that would (or will) be used for this object
         when it is committed.  An object cannot be committed until its
         `parent' is already committed. */
    void write_collection(const jp2_output_box *collection);
  private: // Helper functions
    static kdu_long
      get_preferred_group_length(kdu_long collection_bytes,
                                 kdu_long typical_collection_bytes,
                                 kdu_long remaining_level_bytes,
                                 int num_committed_groups);
      /* Works out the preferred way to arrange the `remaining_level_bytes'
         within a level into groups of collections, assuming that the
         first collection to be stored has `collection_bytes' to be stored
         and the others are expected to have `typical_collection_bytes'.
         The preferred arrangement involves decomposing the remaining
         content bytes into groups of collections.  The function returns
         the expected size of the first such group which itself might be
         composed of further groups.
            The `num_committed_groups' member indicates the number of
         existing sub-levels that have already been committed at the
         leve to which the `remaining_level_bytes' belong.  Groups are
         sized with the idea that a level should hold 2 to 8 groups
         in total.  The function is guaranteed that `num_committed_groups'
         will be no larger than 7.
            As a convenience, if the function determines that the preferred
         number of elements in the next group is 1, it returns exactly
         `collection_bytes'; otherwise, if it determines that the preferred
         next group will be the last one in the level, it returns the
         `remaining_level_bytes' value. */
  public: // Data
    jx_target *target;
    jx_meta_target_level *parent;
    kdu_long box_start_pos; // Current or expected file position for `box'
    int header_length;
    kdu_long content_bytes;
    kdu_long committed_bytes;
    jp2_output_box box;
    int num_committed_groups;
    jx_meta_target_level *group;
  };

/*****************************************************************************/
/*                             jx_meta_target                                */
/*****************************************************************************/

class jx_meta_target {
  public: // Member functions
    jx_meta_target(jx_target *target);
    ~jx_meta_target();
    kdu_long open_top_box(jp2_output_box &box, kdu_uint32 box_type,
                          int simulation_phase);
      /* This function should be used in place of `jx_target::open_top_box'
         when writing metadata.  In particular, the function is invoked from
         within `jx_metanode::write' and `jx_metagroup_writer::get_container'.
         The supplied `box' is opened using the indicated `box_type' as a
         top-level box within an internal "box collector".  All such boxes
         are collected until the next call to `close_collector', which
         decides what to do with the collected content.
            The function returns the location in the file at which the first
         byte of the header of `box' is expected to be written.
            All calls to this function must have the same value for the
         `simulation_phase' argument until the `close_collector' is called,
         after which a new collection cycle is initiated.  If a collection
         cycle involves `simulation_phase' != 0, nothing is actually written
         to the file when `close_collector' is called.
      */
    bool close_collector(bool need_correct_file_positions);
      /* Closes the current collection cycle and starts a new one.  If
         there have been no calls to `open_top_box' during the collection
         cycle, the function returns true but does nothing.
         [//]
         If the collected content is too large to be written at file
         locations reported by calls to `open_top_box' and the
         `need_correct_file_positions' argument is true, the function
         returns false without writing anything.  This can happen only if
         the collector was expecting to write collected content within
         free space previously reserved in a grouping box (during a
         previous metadata writing cycle), but the space turned out to be
         insufficient.  In this case, the caller will need to start a
         new metadata writing cycle; subsequent calls to `open_top_box'
         are bound to report different locations such that this function
         will return true the next time it is called.
         [//]
         If a non-zero `simulation_phase' argument was passed to
         `open_top_box', this function does not actually write any content
         to the file, but the return value still reflects whether or not
         content would have been written in the event that `simulation_phase'
         had been zero.
         [//]
         The `jx_meta_manager::write_metadata' function performs a simulated
         write phase if the metadata that needs to be written includes any
         boxes that are the targets of links (cross-reference boxes) or that
         are the subject of `jpx_metanode::preserve_for_links' calls.
         This function is invoked with `need_correct_file_positions' under
         exactly the same circumstances.  It can happen that the simulation
         fails, in the sense that the current function returns false, in
         which case another simulation phase is performed before the final
         writing phase can occur.  The two simulation phases use different
         values for the `simulation_phase' argument passed to `open_top_box'.
      */
  private: // Data
    jx_target *target;
    int last_simulation_phase; // Value last passed to `open_top_box'; else -1
    jp2_output_box collector;
    kdu_long collector_start_pos; // Where we expect to write collected bytes
    jx_meta_target_level *group;
    kdu_long num_written_collections;
    kdu_long predicted_collection_bytes; // Updated by `close_collection'
    kdu_long typical_collection_bytes; // Updated each time collection written
    kdu_long group_start_pos; // Valid if `group' is non-NULL
    kdu_long preferred_group_collections; // For sizing new groups
  };
  /* Notes:
        This object handles auxiliary metadata that would normally be directed
     to the top level of the file.  Rather than opening a box directly at
     the top level of the file, this object collects content that could be
     written to the top-level, between calls to `open_top_box' and
     `close_collector'.  This allows it to write the content directly to
     the top level of the file or to organize it within a hierarchy of
     grouping boxes.
        The most important feature of this object is its ability to
     write (or commit) content to the file while reserving space within
     a hierarchy of grouping boxes for further content to be written from
     subsequent collection phases.  This is valuable for applications
     which write metadata incrementally (e.g., after each frame of a video
     sequence has been compressed and written), since it allows the
     metadata to be written immediately without polluting the top level
     of the file (or even lower levels) with a large number of sibling boxes.
     Flat lists of boxes at any level of the file can dramatically reduce
     the efficiency of random access, especially in remote interactive
     browsing with JPIP. */

/*****************************************************************************/
/*                             jx_meta_manager                               */
/*****************************************************************************/

struct jx_meta_manager {
  public: // Functions
    jx_meta_manager();
    ~jx_meta_manager() { tear_down(); }
    void tear_down();
      /* This function does all the work of the destructor, but can be
         called first, which allows the function to be called before
         containers are deleted within the `jx_source' or `jx_target'
         destructors, while still embedding `jx_meta_manager' directly
         as a member of these objects. */
    bool test_box_filter(kdu_uint32 box_type);
      /* Returns true if the indicated box type passes the parsing filter
         test -- as set up by the `jpx_meta_manager::set_box_filter'
         function. */
    void link_to_libraries(jx_metanode *node);
      /* This function has no effect unless `node' represents a numlist or
         ROI node, in which case the node is catalogued in the relevant
         library.  The function is called when nodes are parsed in from a
         data source or when they are newly created by the application. */
    jx_container_base *find_container(int container_id);
      /* Returns NULL if `container_id' does not correspond to the index of
         a known container.  Otherwise, the returned object's `get_id'
         function will return `container_id' and its `get_num_base_layers'
         will return non-zero -- when reading from a JPX data source, this
         means that the `jx_container_source::parse_info' function must at
         least have been called. */
    void note_unwritten_link_target(jx_metanode *node)
      { /* Called if `node' has not yet been written and one of the following
           occurs:
           1. `node' is the subject of a  `jpx_metanode::preserve_for_links'
              calls.
           2. Another node adds a link to `node'.
           3. `node->linked_from' is non-NULL or `node->flags' contains the
              `JX_METANODE_PRESERVE' flag (i.e., either of the above two
              conditions has occurred) and `node' is moved to a different
              location in the metadata hierarchy (noted inside
              `jx_metanode::insert_child').
           Calling this function gives the object an opportunity to update
           its `last_unwritten_with_link_targets' member. */
        if (target == NULL) return;
        while (node->parent != tree) node = node->parent; // Get to top level
        jx_metanode *scan = last_unwritten_with_link_targets;
        if (scan == NULL)
          last_unwritten_with_link_targets = node;
        else
          for (; scan != NULL; scan = scan->next_sibling)
            if (scan == node)
              { last_unwritten_with_link_targets = node; break; }
      }
    void note_metanode_unlinked(jx_metanode *node)
      { // Called when a top-level `node's `unlink_parent' function is called
        if (node == last_unwritten_with_link_targets)
          { 
            if (node == first_unwritten)
              last_unwritten_with_link_targets = first_unwritten = NULL;
            else
              last_unwritten_with_link_targets = node->prev_sibling;
          }
        else if (node == first_unwritten)
          first_unwritten = node->next_sibling;
      }
    jp2_output_box *write_metadata(jx_metanode *last_node,
                                   int *i_param, void **addr_param);
      /* The caller wants all metadata to be written up to and including
         the the top-level node identified by `last_node' (NULL if all
         metadata is to be written), starting from the `first_unwritten'
         member.  More metadata may need to be written if there are unresolved
         link nodes.  The function may need to be called multiple times if
         an application-installed breakpoint is encountered, in which case
         it returns non-NULL with the usual `i_param' and `addr_param'
         members set to identify the breakpoint. This function may be invoked
         directly from `jpx_target::write_metadata' or indirectly when
         trying to write a JPX container with embedded metadata from within
         `jpx_target::write_headers'. */
  public: // Data
    jx_metaloc_manager metaloc_manager; // Location services for metadata
    jp2_family_src *ultimate_src;
    jx_source *source; // Non-NULL if we belong to a JPX source object
    jx_target *target; // Non-NULL if we belong to a JPX target object
    jx_meta_target *meta_target; // Created only for writing
    jx_container_base *containers;
    jx_metanode *tree; // Always exists; destroying this one node, kills tree
    jx_metanode *last_added_node; // Used by `jpx_meta_manager::load_matches'
    kdu_long last_sequence_index; // Largest index assigned to any node so far  
    jx_numlist_library numlists;
    jx_region_library unassociated_regions;
    bool flatten_free_asocs;          // These three members record the values
    bool group_with_free_asocs;       // supplied by calls to
    int max_filter_box_types; // Num elements in the `filter_box_types' array
    int num_filter_box_types;
    kdu_uint32 *filter_box_types;
    jx_metanode *deleted_nodes; // When nodes are deleted, we unlink them from
        // any library and from their parent node, if any, then move
        // them onto this list, connected via their `next_sibling' members.
        // They are not actually destroyed until the meta-manager itself is
        // destroyed.  This increases the robustness of applications which
        // manipulate meta-data and (more importantly) allows us to resolve
        // links and file addresses even if the original node has disappeared.
    jx_metanode *touched_head, *touched_tail; // Used to manage a doubly-linked
        // list of touched nodes, as described in connection with the function
        // `jpx_meta_manager::get_touched_nodes'.
  public: // Members used to manage the writing of metadata
    jx_metanode *first_unwritten;
    jx_metanode *last_to_write;
    jx_metanode *last_unwritten_with_link_targets;
    bool write_in_progress; // If `write_metadata' returned prematurely
    int simulation_phase; // Non-zero if calls to `tree->write' are only
       // simulating the write operation, so as to discover the file locations
       // required to write links as cross-reference boxes.  There may be
       // up to two simulation phases.
    kdu_long last_file_pos; // Keeps track of `file_pos' argument to
                            // `jx_metanode::write'
  };

/*****************************************************************************/
/*                            jx_container_base                              */
/*****************************************************************************/

class jx_container_base {
  public: // Member functions
    jx_container_base()
      { 
        id = -1; // Until initialized with a valid value
        indefinite_reps = false; known_reps = 0;
        num_base_layers = num_base_codestreams = 0;
        num_top_layers = num_top_codestreams = 0;
        first_layer_idx = first_codestream_idx = 0;
        parsed = written = false;
        first_metanode = last_metanode = NULL;
        next = prev = NULL;
      }
    int get_id() { return this->id; }
    int get_num_top_layers() { return num_top_layers; }
    int get_num_top_codestreams() { return num_top_codestreams; }
    int get_first_base_layer() { return first_layer_idx; }
    int get_first_base_codestream() { return first_codestream_idx; }
    int get_num_base_layers() { return num_base_layers; }
    int get_num_base_codestreams() { return num_base_codestreams; }
    int get_known_reps() { return (known_reps < 1)?1:known_reps; }
    bool indefinitely_repeated()
      { /* Returns true if `parse_info' has previously succeeded and this
           is an indefinitely repeated container. */
        return this->indefinite_reps;
      }
    int get_last_codestream()
      { /* Returns the index of the last codestream that is compatible
           with this container -- if container defines no codestreams, the
           return value is the number of top-level codestreams minus 1. */
        
        if (num_base_codestreams == 0)
          return num_top_codestreams-1;
        else if (indefinite_reps)
          return INT_MAX;
        else
          return first_codestream_idx + known_reps*num_base_codestreams - 1;
      }
    int get_last_layer()
      { /* Returns the index of the last compositing layer that is compatible
           with this container. */
        if (indefinite_reps)
          return INT_MAX;
        else
          return first_layer_idx + known_reps*num_base_layers - 1;
      }
    int map_codestream_id(int base_id, int rep_idx, bool no_rep_error=false)
      { /* `base_id' is either a top-level codestream index or the absolute
           index of one of the base codestreams in this container.  The
           function returns the (possibly) modified codestream index that
           corresponds to the indicated repetition of the container.  If
           `rep_idx' is invalid, the function either generates an error
           message (default) or returns -1 (`no_rep_error'=true). */
        if ((rep_idx < 0) || ((rep_idx >= known_reps) && !indefinite_reps))
          { 
            if (no_rep_error) return -1;
            invalid_rep_error(rep_idx);
          }
        if (base_id >= num_top_codestreams)
          base_id += rep_idx*num_base_codestreams;
        return base_id;
      }
    int map_layer_id(int base_id, int rep_idx, bool no_rep_error=false)
      { /* `base_id' is either a top-level compositing layer index or the
           absolute index of one of the base layers in this container.  The
           function returns the (possibly) modified layer index that
           corresponds to the indicated repetition of the container.  If
           `rep_idx' is invalid, the function either generates an error
           message (default) or returns -1 (`no_rep_error'=true). */
        if ((rep_idx < 0) || ((rep_idx >= known_reps) && !indefinite_reps))
          { 
            if (no_rep_error) return -1;
            invalid_rep_error(rep_idx);
          }
        if (base_id >= num_top_layers)
          base_id += rep_idx*num_base_layers;
        return base_id;
      }
    void get_codestream_map_params(int &threshold, int &offset, int rep_idx)
      { /* Returns the parameters used by `map_codestream_id' to map
           arbitrary codestream indices, for a given repetition instance.
           Specifically, the returned `threshold' and `offset' parameters
           are used to map a base codestream index according to:
           mapped_id = (base_id<threshold)?base_id:(base_id+offset) */
        if ((rep_idx < 0) || ((rep_idx >= known_reps) && !indefinite_reps))
          invalid_rep_error(rep_idx);
        threshold = num_top_codestreams;
        offset = rep_idx * num_base_codestreams;
      }
    int convert_relative_to_base_codestream_id(int rel_id)
      { /* This function is used while parsing codestream registration boxes
           and number list boxes found within the container.  It converts
           container-relative codestream indices into absolute codestream
           indices (also called base indices) for the first repetition of
           the container.  If `rel_id' is not valid, an error is generated
           through `kdu_error'. */
        if (rel_id < num_top_codestreams)
          return rel_id;
        if ((rel_id -= num_top_codestreams) >= num_base_codestreams)
          invalid_relative_index_error(rel_id,true);
        return rel_id + first_codestream_idx;
      }
    int convert_relative_to_base_layer_id(int rel_id)
      { /* Same as above, but converts relative compositing layer indices to
           absolute base compositing layer indices. */
        if (rel_id < num_top_layers)
          return rel_id;
        if ((rel_id -= num_top_layers) >= num_base_layers)
          invalid_relative_index_error(rel_id,false);
        return rel_id + first_layer_idx;
      }
    int validate_and_normalize_codestream_id(int idx, bool no_error=false)
      { /* This function verifies that `idx' is either a top-level codestream
           index or the index of a codestream defined by this container; in
           the latter case, the function modifies `idx' to the relevant base
           layer of the container, returning the modified index.  If
           validation fails, either an error message is generated using
           `KDU_ERROR_DEV' (default) or the function returns -1
           (if `no_error' is true). */
        if (idx < num_top_codestreams) return idx;
        int delta = idx - first_codestream_idx;
        if ((delta < 0) || (num_base_codestreams < 1) ||
            (((delta = delta / num_base_codestreams) >= known_reps) &&
             !indefinite_reps))
          { 
            if (no_error) return -1;
            invalid_index_error();
          }
        return idx - delta*num_base_codestreams;
      }
    int validate_and_normalize_layer_id(int idx, bool no_error=false)
      { /* As above, but for a compositing layer index. */
        if (idx < num_top_layers) return idx;
        int delta = idx - first_layer_idx;
        if ((delta < 0) || (num_base_layers < 1) ||
            (((delta = delta / num_base_layers) >= known_reps) &&
             !indefinite_reps))
          { 
            if (no_error) return -1;
            invalid_index_error();
          }
        return idx - delta*num_base_layers;
      }
    void validate_auto_codestream_binding(int base_layer_id);
      /* This function is called only if an embedded compositing layer has
         no explicit codestream registration information, to verify that
         the implicit binding rule will produce consistent associations.
         If not, an error is generated through `KDU_ERROR'. */
    bool check_compatibility(int num_codestreams, const int codestreams[],
                             int num_layers, const int layers[],
                             bool any_rep);
      /* Returns true only if all codestream indices and compositing layers
         identified in the supplied arrays correspond either to top-level
         codestreams/layers or to codestreams/layers defined by the container.
         For indices in the latter category, if `any_rep' is false, the
         function also returns false unless container-defined codestream/layer
         indices are base indices, as opposed to repetitions of the base
         indices. */
    void note_metanode_unlinked(jx_metanode *node)
      { /* Called from `node->unlink_parent' if it is currently a top-level
           embedded number list metanode.  In this case, we must make sure
           that the unlinked node is no longer referenced by the
           `first_metanode' or `last_metanode' members. */
        if (node == first_metanode)
          { 
            if (node == last_metanode)
              first_metanode = last_metanode = NULL;
            else
              first_metanode = node->next_sibling;
          }
        else if (node == last_metanode)
          last_metanode = node->prev_sibling;
      }
  private: // Helper functions
    void invalid_relative_index_error(int rel_id, bool is_codestream);
      /* Generates error messages on behalf of the `convert_relative_...'
         functions. */
    void invalid_rep_error(int rep_idx);
      /* Generates a `KDU_ERROR_DEV' error to report the fact that an
         invalid repetition index is being used in a mapping call.  This
         almost certainly means that some external interface has been
         corrupted somewhere inside the application. */
    void invalid_index_error();
      /* Called by `validate_and_normalize_[codestream|layer]_id' if a
         codestream or compositing layer index is found not to belong to
         the container.  Generates `KDU_ERROR_DEV' error.  Usually, this
         error should be avoided by calling `check_compatibility' first. */
  protected: // Data
    int id; // -ve if constructed with NULL `box' argument
    bool indefinite_reps; // See below
    int known_reps; // See below
    int num_base_layers;
    int num_base_codestreams;
    int num_top_layers;
    int num_top_codestreams;
    int first_layer_idx;
    int first_codestream_idx;
  public: // Links to embedded metanodes -- important for saving files
    bool parsed; // true if the container has been parsed from a JPX file
    bool written; // true if the container has been written to a JPX file
    jx_metanode *first_metanode; // See below
    jx_metanode *last_metanode;   // See below
  public: // Links for including this object in a list
    jx_container_base *next; // Next container in sequence or NULL
    jx_container_base *prev; // Previous container or NULL
  };
  /* When inherited by `jx_container_source', if `indefinite_reps' is true,
     the `known_reps' member grows as codestreams are found.  When
     inherited by `jx_container_target', if `indefinite_reps' is true,
     `known_reps' is left equal to 0.
        The `first_metanode' and `last_metanode' members are used only
     to record top-level number lists that are added to the metadata
     hierarchy, having a reference to this container.  These members
     are not used if `parsed' is true.  If `parsed' is false and `written'
     is true, no new metanodes may be embedded within the container.
     Otherwise, when `parsed' and `written' are both false, it is important
     that all top-level number list metanodes that reference this container
     appear as consecutive siblings, connected via their `next_sibling'
     and `prev_sibling' members.  It is also important that all such
     number list metanodes appear after those that reference any earlier
     JPX containers.  This is because top-level metanodes are written
     consecutively and those that reference a JPX container must be written
     inside its Compositing Layer Extensions box.
  */

/*****************************************************************************/
/*                             jx_registration                               */
/*****************************************************************************/

class jx_registration {
  public: // Member functions
    jx_registration()
      { 
        max_codestreams = num_codestreams = 0;
        codestreams = NULL; container = NULL;
      }
    ~jx_registration()
      { if (codestreams != NULL) delete[] codestreams; }
    void set_container(jx_container_base *cont) { this->container = cont; }
      /* This function should be called when the owning
         `jx_layer_source'or `jx_layer_target' object is created, if it
         belongs to a JPX container. */
    bool is_initialized() { return (num_codestreams > 0); }
      /* Returns true if `init' has been called. */
    void init(jp2_input_box *creg_box);
      /* Parses a codestream registration box.  If `set_container' has been
         called, the JPX container is used both to convert and verify the
         legitimacy of codestream indices found in the CREG box, based on
         the number of top-level codestreams and the number of base
         codestreams defined by the container. */
    void finalize(int layer_idx);
      /* Use this form during reading.  If no codestream registration box was
         found, this function creates the default installation, with one
         codestream whose ID is identical to the compositing layer index;
         in this case if `set_container' has been called, the container is
         used to verify that `channel_id' and all of its container-dependent
         repetitions will correspond to codestreams that exist either at the
         top-level of the file or within corresponding repetitions of base
         codestreams defined by the container.  Note that this function does
         not set the `layer_size' member.  That member may need to be set
         later. */
    void finalize(j2_channels *channels, int layer_idx);
      /* Use this form during writing.  Uses the `j2_channels' object to
         determine which codestreams have been used.  Any which have not
         been assigned registration parameters are aligned with the
         compositing layer registration grid directly.  Note that this
         member does not set the `layer_size' member.  That member needs to
         be set by the caller, before `save_box' is invoked. */
    void save_box(jp2_output_box *super_box);
      /* Writes the codestream registration (creg) box, possibly modifying
         referenced codestream indices as it goes, based on any JPX container
         reference that was passed to `set_container'. */
  private: // Internal structure
      struct jx_layer_stream {
          jx_layer_stream()
            { codestream_id=-1; codestream_finished=false;
              next_referring_layer=NULL; }
          int codestream_id;
          kdu_coords alignment;
          kdu_coords sampling;
          bool codestream_finished; // If `jx_codestream_source::finish' done
          jx_layer_source *next_referring_layer; // Used to build linked list
                  // headed by the corresponding `jx_codestream_source'
                  // object's `referring_layers' member. The list is traversed
                  // using `jx_layer_source::get_next_referring_layer'.
        };
  private: // Data
    friend class kd_supp_local::jx_layer_source;
    friend class kd_supp_local::jx_layer_target;
    friend class kdu_supp::jpx_layer_source;
    friend class kdu_supp::jpx_layer_target;
    int max_codestreams;
    int num_codestreams;
    jx_layer_stream *codestreams;
    jx_container_base *container;
    kdu_coords denominator;
    kdu_coords final_layer_size; // (0,0) until calculated
  };

/*****************************************************************************/
/*                             jx_track_source                               */
/*****************************************************************************/

struct jx_track_source {
    jx_track_source()
      { layer_start=layer_lim=0; finished=false; }
    int layer_start; // Num base layers covered by preceding tracks
    int layer_lim; // Num base layers covered by this & all preceding tracks
    bool finished; // If `composition.finish' has been successfully called
    jx_composition composition;
  };

/*****************************************************************************/
/*                           jx_container_source                             */
/*****************************************************************************/

class jx_container_source : public jx_container_base {
  public: // Member functions
    jx_container_source(jx_source *owner, jp2_input_box *box,
                        int container_id);
      /* Note that `box' is donated (transplanted) to the current object,
         but is not immediately parsed.  For this, a call to `parse_info'
         is warranted.  If `box' is NULL, a special container
         is being constructed to have indefinite repetitions, one
         base compositing layer, one base codestream, no tracks and no
         frames.  This type of object is constructed automatically when
         the JPX source parser reaches the end of the file, having
         discovered no containers, no compositing layer headers, and
         no codestream headers, but having discovered at least one
         multiple codestream box; it ensures that each JP2C/FTBL box that
         might be subsequently discovered inside a Multiple Codestream
         box will automatically be associated with the indefinitely
         repeated `jx_codestream_source' and `jx_layer_source' objects
         here.  To complete this special container, the `parse_info'
         function still needs to be invoked, although it will do no
         actual parsing.  The special container must have a -ve
         `container_id'.  Otherwise, successive containers in the file
         must have consecutive `container_id's, starting from 0. */
    ~jx_container_source();
    static jx_container_source *parse_info(jx_container_source *start);
      /* This function is responsible for parsing the JPLI (INFO) box
         that is found as the first sub-box of each JCLX (Compositing Layer
         Extensions) box -- we call these containers.  The function walks
         through the list of containers that is headed by `start', following
         the `next' links until it encounters a container for which the
         INFO sub-box cannot yet be parsed.
         [//]
         Each time it successfully parses an INFO box, the function creates
         all relevant `jx_layer_source' and `jx_codestream_source' objects
         and invokes `jx_source::update_container_info' so that the owning
         `jx_source' object is aware of the first container for which this
         function may need to be invoked again in the future, and also
         whether or not there will be any more containers in the file.
         [//]
         The function returns NULL if it parses all outstanding INFO boxes;
         otherwise, it returns a pointer to the first container for which
         the INFO box remains to be parsed. */
    void note_total_codestreams(int num);
      /* This function is called by `jx_source::update_total_codestreams'
         when the total number of codestreams increases.  The call is
         ignored unless this object happens to have an indefinite number
         of repetitions, in which case the number of known repetitions may
         be increased, warranting new notifications being sent to
         `jx_source::update_container_info'. */
    bool finish(bool invoked_by_metanode=false);
      /* This function succeeds only if `parse_info' succeeds AND all
         top-level sub-boxes of the JCLX box can be recovered.  The
         individual `jx_layer_source' and `jx_codestream_source' objects'
         `finish' functions may, however, still need to be called to
         access their properties.  Actually, those functions may be called
         before the relevant Compositing Layer Header boxes and
         Codestream Header boxes have been parsed, in which case they
         automatically invoke this function to obtain those boxes.
         [//]
         The `invoked_by_metanode' argument is true only when this function
         is called directly from within `metanode->finish_reading' to recover
         metadata from a codestream header box.  Normally, if the codestream
         header box is completely read by this function, we automatically
         delete the `metanode->parse_state->read' object and then invoke
         `metanode->check_parsing_complete', which may delete `parse_state'
         and may even delete the metanode itself if it turns out to be empty.
         However, we don't want these things to happen if we are in the
         process of invoking the `metanode' object's `finish_reading' function.
      */
    void update_completed_tracks();
      /* This function is called whenever we may have come to the end of
         a group of Instruction Set boxes while parsing the JCLX box inside
         `finish'.  The function expects to see `tracks_completed' equal to
         `tracks_found' or `tracks_found'-1.  In the latter case, it
         increments the number of `tracks_completed', invokes the relevant
         `jx_composition::set_layer_mapping' function, and then calls
         `jx_composition::finish'. */
    bool is_special()
      { /* Returns true if this object was constructed with a NULL `box'
           argument -- see constructor for an explanation. */
        return (this->id < 0);
      }
    kdu_uint32 get_max_tracks() { return this->max_tracks; }
    static jx_codestream_source *
      find_codestream(jx_container_source *start, int stream_id, int &rep_idx);
      /* This powerful function scans containers, starting from `start',
         following the `next' links until it finds a base codestream that
         is a match for the absolute logical codesteram index given by
         `stream_id'.  If necessary, it also attempts to parse new top-level
         boxes so as to extend the list of containers.  If all this fails,
         the function returns NULL.  Otherwise, the returned object's
         `match_idx' function should return true when passed `stream_id'.
         The `rep_idx' is used to return the repetition (or instance) of the
         base codestream that matches the identified `stream_id'. */
    static jx_layer_source *
      find_layer(jx_container_source *start, int layer_id, int &rep_idx);
      /* Same as `find_codestream' but for compositing layers. */
    jx_codestream_source *match_codestream(int stream_id, int &rep_idx)
      { /* Same as `find_codestream', but does no parsing and returns NULL
           unless the current object contains the relevant match. */
        if ((num_base_codestreams == 0) ||
            (stream_id < first_codestream_idx)) return NULL;
        rep_idx = (stream_id-=first_codestream_idx) / num_base_codestreams;
        if ((rep_idx >= known_reps) && !indefinite_reps) return NULL;
        return base_codestreams[stream_id - rep_idx*num_base_codestreams];
      }
    jx_layer_source *match_layer(int layer_id, int &rep_idx)
      { /* Same as `find_layer', but does no parsing and returns NULL
           unless the current object contains the relevant match. */
        if ((num_base_layers == 0) ||
            (layer_id < first_layer_idx)) return NULL;
        rep_idx = (layer_id-=first_layer_idx) / num_base_layers;
        if ((rep_idx >= known_reps) && !indefinite_reps) return NULL;
        return base_layers[layer_id - rep_idx*num_base_layers];
      }
    int match_layers(const int layers[], int num_layers)
      { /* Does no parsing; returns -1 unless one of the compositing layers
           identified through the `layers' array belongs to this container,
           in which case the matching compositing layer index is returned. */
        for (int n=0; n < num_layers; n++)
          if ((layers[n] >= first_layer_idx) &&
              (indefinite_reps || (layers[n] < lim_layer_idx)))
            return layers[n];
        return -1;
      }
    jx_composition *match_track(int layer_id)
      { /* Does no parsing; returns the first presentation track that
           uses the `layer_id' compositing layer index. */
        int delta = (layer_id - first_layer_idx);
        int rep_idx = delta / num_base_layers;
        if ((delta < 0) || ((rep_idx >= known_reps) && !indefinite_reps))
          return NULL;
        delta -= num_base_layers*rep_idx;
        for (kdu_uint32 t=0; t < tracks_completed; t++)
          if (delta < tracks[t].layer_lim)
            return &(tracks[t].composition);
        return NULL;
      }
    jx_codestream_source *get_base_codestream(int idx)
      { // Note that `idx' is the absolute index of a base codestream within
        // the container, not its relative index.
        idx -= first_codestream_idx;
        if ((idx >= 0) && (idx < num_base_codestreams))
          return base_codestreams[idx];
        else
          return NULL;
      }
    jx_layer_source *get_base_layer(int idx)
      { // Note that `idx' is the absolute index of a base compositing layer
        // within the container, not its relative index.
        idx -= first_layer_idx;
        if ((idx >= 0) && (idx < num_base_layers))
          return base_layers[idx];
        else
          return NULL;
      }
    jx_container_source *get_next() { return (jx_container_source *) next; }
    jx_container_source *get_prev() { return (jx_container_source *) prev; }
  private: // Data
    friend class kdu_supp::jpx_container_source;
    jx_source *owner;
    jx_layer_source **base_layers;
    jx_codestream_source **base_codestreams;
    int lim_layer_idx;      // Exclusive upper bounds on layer/stream indices
    int lim_codestream_idx; // covered by this container; if !indefinite_reps
    jp2_input_box jclx;
    jp2_input_box info_box;
    int next_base_layer; // Next base layer needing JPLH box
    int next_base_codestream; // Next base codestream needing JPCH box
  private: // Members relating to animation
    kdu_uint32 max_tracks; // Max tracks found up to this container
    kdu_uint32 num_tracks;
    int num_frames_per_track; // May be 0 for indefinitely repeated container
    kdu_uint32 start_time;
    jx_track_source *tracks; // Array allocated during `parse_info'
    kdu_uint32 tracks_found; // Num groups of iset boxes encountered so far
    kdu_uint32 tracks_completed; // Num groups of iset boxes terminated so far
    int iset_boxes_found; // Num Instruction Set boxes found so far
  public: // Members used to interact with the metadata hierarchy
    int jclx_nesting_level; // See below
    jx_metanode *metanode; // Used to keep track of where we are sending any
                        // auxiliary metadata encountered in the header, until
                        // we have recovered all the image related boxes.
  };
  /* The `jclx_nesting_level' value is meaningful only when the ultimate
     source of data is a dynamic cache.  It represents the nesting level of
     the contents of the `jclx' box within the associated meta-databin.  If
     the `jclx' box is represented by a placeholder, so that its contents
     are located within their own databin, the value of this member is
     necessarily 0.  Otherwise, this member will be equal to 1, because
     the JCLX box is always found at the top level of the file.
  */

/*****************************************************************************/
/*                          jx_codestream_source                             */
/*****************************************************************************/

class jx_codestream_source {
  public: // Member functions
    jx_codestream_source(jx_source *owner, int idx, bool restrict_to_jp2,
                         jx_container_source *container=NULL)
      { /* If this codestream belongs to JPX container, `container' is
           non-NULL and `idx' is the index of the base codestream. */
        this->owner=owner; this->container=container; this->id=idx;
        this->restrict_to_jp2 = restrict_to_jp2;
        have_dimensions = have_bpcc = have_palette = have_cmap = false;
        metadata_finished = compatibility_finalized = false;
        pending_subs = NULL; last_ready_rep_idx = -1; referring_layers = NULL;
        chdr_nesting_level = 0; metanode = NULL;
      }
    ~jx_codestream_source()
      { 
        while (pending_subs != NULL)
          { jx_pending_box *tmp=pending_subs; pending_subs=tmp->next;
            delete tmp; }
      }
    int get_idx() { return this->id; }
      /* Returns the codestream index, that was passed to the constructor. */
    int match_idx(int idx)
      { /* Returns true if `idx' is the logical codestream identifier of the
           current object or one of its repetitions (if it belongs to
           a `container'. */
        if (container==NULL)
          return (idx == this->id);
        else
          { int rep_idx;
            return (container->match_codestream(idx,rep_idx)==this);
          }
      }
    void donate_chdr_box(jp2_input_box &src, int databin_nesting_level);
      /* This function is called from the master `jx_source' object if it
         encounters a codestream header (chdr) box while parsing the top-level
         of the JPX data source.  The `src' box need not be complete.  Its
         state is donated to the internal `chdr' object and `src' is closed
         so that the caller can open the next box after the codestream header
         box at the top-level of the JPX data source.  The function attempts
         to parse all relevant sub-boxes of the codestream header box, but
         some data might not be available yet.  In this case, the attempt
         to parse sub-boxes will resume each time the `finish' function is
         called.
            The `databin_nesting_level' argument identifies the nesting level
         of the box contents within which `src' is found, relative to its
         databin -- this is the databin identified identified by the
         `jp2_locator' object returned by `src.get_locator', which may be 
         different to the databin in which the `src' box's contents are
         found -- identified via `src.get_contents_locator'.  In practice, if
         `src' is a top-level codestream header box, `src.get_locator' must
         return a location that lies within meta-databin 0 and
         `databin_nesting_level' must be 0.  On the other hand, if the
         codestream header box is found within a JPX container (Compositing
         Layer Extensions box), the value of `databin_nesting_level' will be 0
         if the JPX container has a placeholder (i.e., its contents have
         their own databin) and 1 otherwise.  The value of this argument is
         irrelevant unless the ultimate source of data is a dynamic cache.
      */
    bool finish(bool invoked_by_metanode=false);
      /* This function may be called at any time, but is generally called
         whenever `jpx_source::access_codestream' is used, to see whether or
         not all the required information for a codestream has been recovered.
            If the codestream header box has not been encountered, or defaults
         may be relevant and the default JP2 header box has not yet been
         completely parsed, the function calls appropriate member functions
         of the owning `jx_source' object in order to parse the relevant
         boxes, if possible.  If this fails to produce a complete
         representation, the function returns false, meaning that further
         parsing will be required at a later point.
            Once the above conditions are satisfied, the function returns
         true.  However, this does not mean that the Contiguous Codestream
         or Fragment Table box is present yet.  To determine this, you can
         call one of `get_stream'.
         If this function determines that one or more required boxes will
         never be available from the source, an appropriate error message
         is generated through `kdu_error'.
            The `invoked_by_metanode' argument is true only when this function
         is called directly from within `metanode->finish_reading' to recover
         metadata from a codestream header box.  Normally, if the codestream
         header box is completely read by this function, we automatically
         delete the `metanode->parse_state->read' object and then invoke
         `metanode->check_parsing_complete', which may delete `parse_state'
         and may even delete the metanode itself if it turns out to be empty.
         However, we don't want these things to happen if we are in the
         process of invoking the `metanode' object's `finish_reading' function.
      */
    bool confirm_stream(int rep_idx);
      /* Returns true if the existence of the Contiguous Codestream box or
         Fragment Table box for the indicated repetition (containers only) of
         this object can be confirmed -- either by discovering it or by
         encountering a Multiple Codestream Info box that confirms its
         existence. */
    jx_fragment_lst *get_stream(int rep_idx, bool want_ready);
      /* Returns NULL if the Contiguous Codestream box or Fragment Table box
         for the indicated repetition (containers only) of this object
         has not yet been discovered, or if `want_ready' is true and the
         codestream's main header is not yet available or some of fragments
         of its fragment table are still missing.  Otherwise, the function
         returns a pointer to the `jx_fragment_lst' that can be used to
         open `jpx_input_box' or `jp2_input_box' to read the codestream.
         If this function determines that it will never succeed, either now
         or in the future, it generates an appropriate error message through
         `kdu_error' -- this is actually done by
         `jx_source::find_stream_flst'. */
    bool stream_available(int rep_idx, bool want_ready)
      { /* Returns false if the existence of the relevant JP2C/FTBL box
           cannot be confirmed or (if `want_ready' is true) if `get_stream'
           returns false.  For efficiency, the function takes advantage of
           cached information from previous calls. */
        if (rep_idx == last_ready_rep_idx)
          return true;
        else if (!want_ready)
          return confirm_stream(rep_idx);
        else
          return (get_stream(rep_idx,want_ready) != NULL);
      }
    j2_component_map *get_component_map() { return &component_map; }
      /* Used by `jx_layer_source' to finish a compositing layer. */
    // ------------------------------------------------------------------------
  private: // Helper fnctions
    void add_pending_sub(jp2_input_box &box)
      { jx_pending_box *tmp=new jx_pending_box; tmp->next = pending_subs;
        pending_subs = tmp; tmp->box.transplant(box); }
  private: // Data
    friend class kdu_supp::jpx_codestream_source;
    jx_source *owner;
    jx_container_source *container;
    int id;
    bool metadata_finished; // True if chdr box finished or non-existent
    bool restrict_to_jp2; // If true and `id'=0, ignore codestream header box
    bool compatibility_finalized;
    bool have_dimensions; // If we have found an image header sub-box
    bool have_bpcc; // If we have found a bits-per-component su-box
    bool have_palette; // If we have found a palette sub-box
    bool have_cmap; // If we have found a component-mapping sub-box
    jp2_input_box chdr; // Used to manage an open codestream header box
    jx_pending_box *pending_subs; // jp2h/bpcc/cmap/palette not yet complete
    j2_dimensions dimensions;
    j2_palette palette;
    j2_component_map component_map;
    jpx_input_box stream_box; // Resource provided for `open_stream'
    int last_ready_rep_idx; // See below
  public: // Members used to keep list of compositing layers that reference us
    jx_layer_source *referring_layers;
  public: // Members used to interact with the metadata hierarchy
    jp2_locator header_loc; // Location of `chdr' box once known
    int chdr_nesting_level; // See below
    jx_metanode *metanode; // Used to keep track of where we are sending any
                       // auxiliary metadata encountered in the header, until
                       // we have recovered all the image related boxes.
  };
  /* Notes:
       If this object is found inside a JPX container, it may be associated
     with multiple JPEG2000 codestreams (corresponding to multiple repetitions
     of the container).  However, in most cases, applications make several
     API calls that relate to a given codestream (a given `rep_idx') before
     moving on to another one.  To improve the efficiency of calls to the
     `stream_available' function, the function uses `last_ready_rep_idx' to
     keep track of the most recent `rep_idx' value for which a JPEG2000
     codestream has been located and for which the main header at least is
     known to be available.  If there is none, the value of
     `last_ready_rep_idx' is -ve.
        The `chdr_nesting_level' value is meaningful only when the ultimate
     source of data is a dynamic cache.  It represents the nesting level of
     the contents of the `chdr' box within the associated meta-databin.  If
     the `chdr' box is represented by a placeholder, so that its contents
     are located within their own databin, the value of this member is
     necessarily 0.  Otherwise, this member will be equal to 1 more than
     the `databin_nesting_level' argument passed into `donate_chdr_box'.
  */

/*****************************************************************************/
/*                            jx_layer_source                                */
/*****************************************************************************/

class jx_layer_source {
  public: // Member functions
    jx_layer_source(jx_source *owner, int idx,
                    jx_container_source *container=NULL)
      { 
        this->owner=owner; this->container=container; this->id=idx;
        finished = false; have_cgrp = have_cdef_opct = have_pxfm = false;
        have_registration = have_resolution = false;
        pending_subs = NULL; last_ready_rep=-1;
        jplh_nesting_level = 0;  metanode = NULL;
        registration.set_container(container);
      }
    ~jx_layer_source()
      { 
        while (colour.next != NULL)
          { j2_colour *tmp=colour.next;
            colour.next = tmp->next; delete tmp; }
        while (pending_subs != NULL)
          { jx_pending_box *tmp=pending_subs;
            pending_subs = tmp->next; delete tmp; }
      }
    int get_idx() { return this->id; }
      /* Returns the layer index, that was passed to the constructor. */
    int match_idx(int idx)
      { /* Returns true if `idx' is the logical layer identifier of the
           current object or one of its repetitions (if it belongs to
           a `container'. */
        if (container==NULL)
          return (idx == this->id);
        else
          { int rep_idx;
            return (container->match_layer(idx,rep_idx)==this);
          }
      }
    void donate_jplh_box(jp2_input_box &src, int databin_nesting_level);
      /* This function is called from the master `jx_source' object if it
         encounters a compositing layer header (jplh) box while parsing the
         top-level of the JPX data source.  The `src' box need not be
         complete.  Its state is donated to the internal `jplh' object and
         `src' is closed so that the caller can open the next box after the
         compositing layer header box at the top-level of the JPX data source.
         The function attempts to parse all relevant sub-boxes of the
         compositing layer header box, but some data might not be available
         yet.  In this case, the attempt to parse sub-boxes will resume each
         time the `finish' function is called.
            The `databin_nesting_level' argument identifies the nesting level
         of the box contents within which `src' is found, relative to its
         databin -- this is the databin identified identified by the
         `jp2_locator' object returned by `src.get_locator', which may be
         different to the databin in which the `src' box's contents are
         found -- identified via `src.get_contents_locator'.  In practice, if
         `src' is a top-level compositing layer header box, `src.get_locator'
         must return a location that lies within meta-databin 0 and
         `databin_nesting_level' must be 0.  On the other hand, if the
         compositing layer header box is found within a JPX container
         (Compositing Layer Extensions box), the value of
         `databin_nesting_level' will be 0 if the JPX container has
         a placeholder (i.e., its contents have their own databin) and 1
         otherwise.  The value of this argument is irrelevant unless the
         ultimate source of data is a dynamic cache.
      */
    bool finish(bool invoked_by_metanode=false);
      /* This function may be called at any time, but is generally called
         whenever `jpx_source::access_layer' is used, to see whether or
         not a `jpx_layer_source' interface can be attached.  If the
         compositing layer header box has not been encountered, or the
         default JP2 header box has not yet been completely parsed, the
         function calls appropriate member functions of the owning `jx_source'
         object in order to parse the relevant boxes, if possible.  It also
         tries to finish all codestreams on which the compositing layer
         header box depends.  If these steps fail to produce a complete
         representation, the function returns false, meaning that a
         `jpx_layer_source' interface cannot yet be offered to the
         application.  Otherwise, the function returns true.
            If this function determines that one or more required boxes will
         never be available from the source, an appropriate error message
         is generated through `kdu_error'.
            The `invoked_by_metanode' argument is true only when this function
         is called directly from within `metanode->finish_reading' to recover
         metadata from a codestream header box.  Normally, if the codestream
         header box is completely read by this function, we automatically
         delete the `metanode->parse_state->read' object and then invoke
         `metanode->check_parsing_complete', which may delete `parse_state'
         and may even delete the metanode itself if it turns out to be empty.
         However, we don't want these things to happen if we are in the
         process of invoking the `metanode' object's `finish_reading' function.
      */
    bool all_streams_available(int rep_idx, bool want_ready);
      /* Returns false if any codestream associated with this compositing
         layer has yet to be confirmed, or if `want_ready' is true and
         any codestream's JP2C/FTBL box or main header (or fragment list) is
         not yet available.  If it is determined that one or more codestreams
         will never become available, an appropriate error message is
         generated through `kdu_error' -- actually this is done within
         `jx_source::find_stream_flst'. */
    int get_num_codestreams()
      { return registration.num_codestreams; }
    int get_codestream_id(int which, int rep_idx)
      { 
        if ((which < 0) || (which >= registration.num_codestreams)) return -1;
        int cs_id = registration.codestreams[which].codestream_id;
        if (container != NULL)
          cs_id = container->map_codestream_id(cs_id,rep_idx);
        return cs_id;
      }
    jx_layer_source *get_next_referring_layer(int codestream_id)
      { 
        for (int n=0; n < registration.num_codestreams; n++)
          if (registration.codestreams[n].codestream_id == codestream_id)
            return registration.codestreams[n].next_referring_layer;
        return NULL;
      }
  // --------------------------------------------------------------------------
  private: // Helper functions
    void add_pending_sub(jp2_input_box &box)
      { jx_pending_box *tmp=new jx_pending_box; tmp->next = pending_subs;
        pending_subs = tmp; tmp->box.transplant(box); }
    bool finish_cgrp();
      /* Tries to finish parsing a colour group box in `cgrp'. */
  // --------------------------------------------------------------------------
  private: // Data
    friend class kdu_supp::jpx_layer_source;
    jx_source *owner;
    jx_container_source *container;
    int id;
    bool finished; // True if `finish' has returned true in the past
    bool have_cgrp; // If we have found a colour group sub-box
    bool have_cdef_opct; // If we have found a channel defs or opacity sub-box
    bool have_pxfm; // If we have found a pixel format box
    bool have_resolution; // If we have found a resolution sub-box
    bool have_registration; // If we have found a registration sub-box
    jp2_input_box jplh; // Used to manage an open compositing layer header box
    jp2_input_box cgrp; // Used to manage an open colour group box
    jx_pending_box *pending_subs;
    j2_resolution resolution;
    j2_channels channels;
    j2_colour colour; // First element in linked list of boxes
    jx_registration registration;
    int last_ready_rep; // Similar to namesake in `jx_codestream_source';
                        // used to accelerate `all_streams_available'.
  public: // Members used to interact with the metadata hierarchy
    jp2_locator header_loc; // Location of `jplh' box, once known
    int jplh_nesting_level; // See below
    jx_metanode *metanode; // Used to keep track of where we are sending any
                       // auxiliary metadata encountered in the header, until
                       // we have recovered all the image related boxes.
  };
  /* Notes:
       The `jplh_nesting_level' value is meaningful only when the ultimate
     source of data is a dynamic cache.  It represents the nesting level of
     the contents of the `jplh' box within the associated meta-databin.  If
     the `jplh' box is represented by a placeholder, so that its contents
     are located within their own databin, the value of this member is
     necessarily 0.  Otherwise, this member will be equal to 1 more than
     the `databin_nesting_level' argument passed into `donate_jplh_box'.
  */

/*****************************************************************************/
/*                          jx_multistream_source                            */
/*****************************************************************************/

struct jx_multistream_source {
  public: // Member functions
    jx_multistream_source(jx_source *src, jx_multistream_source *p,
                          jp2_input_box &box)
      { 
        this->owner=src;  this->parent=p;
        this->min_id=0;  this->lim_id = 0; // Not known yet
        first_subbox_offset = num_descendants = 0;  descendants = NULL;
        streams_per_subbox = bytes_per_subbox = fully_recovered_streams = 0;
        next_subbox_min_id = 0;  head=tail=prev=next=NULL;
        this->main_box.transplant(box);
      }
      /* Before you can parse codestream information from the constructed
         object, you must invoke `init'.  Until that point, the `min_id'
         member holds the invalid value 0 -- codestream 0 may not be
         found within a J2CX box. */
    ~jx_multistream_source()
      { 
        if (descendants != NULL) delete[] descendants;
        while ((tail=head) != NULL)
          { head = tail->next;  delete tail; }
      }
    void init(int first_stream_id)
      { this->min_id = this->next_subbox_min_id = first_stream_id; }
      /* Call this function as soon as you know the index of the first
         codestream represented by this box -- that generally means that
         the number of codestreams represented by the preceding J2CX box
         (if any) has become available.  After calling this function, you
         may wish to invoke `parse_info', but this is not done automatically
         so as to prevent unbounded recursion in the event that the
         function is itself invoked from another object's `parse_info'
         function. */
    bool parse_info();
      /* This function need only be called if the `lim_id' member is 0.
         The function attempts to discover the number of codestreams
         represented by the object by parsing its J2CI sub-box.  If this
         is not possible, the function returns false.  If successful,
         the function invokes `jx_source::update_multistream_info'.  For a
         top-level multistream, this advances
         `jx_source::next_multistream_min_id' and
         `jx_source::first_unparsed_multistream', while also causing any
         subsequent multistream object's `init' function to be called,
         but not its `parse_info' function.  It also causes
         `jx_source::update_total_codestreams' to be invoked. */
    bool recover_codestream(int stream_id);
      /* This is the key workhorse function; it attempts to parse the boxes
         required to recover the contiguous codestream or fragment table
         box that represents the indicated codestream (`stream_id'), adding
         an appropriate entry to the global array of `jx_codestream_source'
         references maintained by the `jx_source' owner, unless this is not
         yet possible (in that case, the function returns false).  Along
         the way, the function may recover other codestreams, adding them
         to the global array of `jx_codestream_source' references so that we
         do not need to come back here too frequently.  These decisions are
         all made internally, based on the availability (or otherwise) of
         random access information.  After any call to this function (even
         a call that returns false), it can be useful to invoke
         the `check_fully_recovered' function to determine whether or not all
         codestreams have been recovered.  If they have, the entire object
         can be removed from its parent. */
    bool check_fully_recovered()
      { return ((lim_id > min_id) &&
                (fully_recovered_streams >= (lim_id-min_id))); }
      /* Returns true if all codestreams for the present object have been
         recovered. */
    void discover_codestreams();
      /* The purpose of this function is to discover the existence of as
         many codestreams as possible and report them to the `jx_source'
         owner.  If the `lim_id' member is greater than `min_id', the
         number of available codestreams within this object has already
         been discovered and reported from within `parse_info'.  Otherwise,
         the function reads sub-boxes and passes into descendants in order
         to learn as much as it can.  This feature is used to implement
         `jpx_source::count_codestreams', but it can also be important for
         counting compositing layers, where a JPX container has an
         indefinite repetition count. */
  private: // Helper functions
    jx_multistream_source *
      add_descendant(int min_stream_id, jp2_input_box &box, int idx);
      /* Adds a new descendant to the list managed by `head' and `tail',
         inserting it into any `descendants' array at the location given
         by `idx' -- value of `idx' is irrelevant if there is no
         `descendants' array.  The descendant is initialized to have the
         indicated minimum codestream id. */
    void remove_descendant(jx_multistream_source *elt, int idx);
      /* Unlinks `elt' from the list managed by `head' and `tail' and
         removes its reference from any `descendants' array, at the location
         given by `idx'.  Also updates the current object's
         `fully_recovered_streams' member. */
  public: // Data
    jx_source *owner; // Always non-NULL
    jx_multistream_source *parent; // NULL if we come from a top-level J2CX box
    int min_id; // Index of first codestream here
    int lim_id; // 0 if unknown; `min_id' if J2CI's Ncs=0; else 1+max-id
    jp2_input_box main_box; // Main J2CX box in which everything here is found
    jp2_input_box j2ci_box; // Exists only if J2CI parsing deferred
    int first_subbox_offset; // See below
    int num_descendants; // Non-zero if and only if sub-boxes randomly accessed
    jx_multistream_source **descendants; // Facilitates random access
    int streams_per_subbox; // See below
    int bytes_per_subbox; // See below
    int fully_recovered_streams; // See below
    int next_subbox_min_id; // min_id for next sub-box of `j2cx'
    jx_multistream_source *head; // First descendant, not completely parsed
    jx_multistream_source *tail; // Last descendant, not completely parsed
    jx_multistream_source *prev; // Used to build doubly-linked list of
    jx_multistream_source *next; // siblings with the same parent.
  };
  /* Notes:
        This object keeps track of the hierarchical organization found within
     J2CX (Multiple Codestream) boxes; individual objects of this class are
     kept in linked lists from which they can be removed and deleted once
     their contents have been fully parsed out.
        The `head' and `tail' members maintain a doubly-linked list of
     descendants, which may contain holes and need not be complete, either
     because random access information has allowed us to recover the
     descendants out of order, or because one or more descendants have been
     fully parsed and subsequently deleted.  The idea is that one only needs
     to access this structure if a codestream is found to be missing from
     the global array maintained within `jx_source' -- if a descendant has
     been fully parsed and removed from the list headed by `head', its
     codestreams must al be in that array so there will be no cause for
     attempting to recover it again.
        Initially, `first_subbox_offset' is set to 0.  As soon as the J2CI
     sub-box is opened (even if its contents are not yet available), the
     value of this member is initialized either to -1 (if we are reading from
     a dynamic cache) or to the length of the J2CI box.
        The `j2ci_box' member only appears open if the J2CI box could not be
     completely parsed at the point when it was discovered.  In this case,
     we must be reading from a dynamic cache and `first_subbox_offset' must be
     -ve.  Further sub-boxes of the main J2CX box can be opened sequentially,
     because `j2ci_box' is a transplant.
        Sub-boxes may be parsed from the main J2CX container either
     sequentially or randomly, depending on available information.
     Specifically, for random access to sub-boxes, `first_subbox_offset'
     must be positive and `streams_per_subbox' and `bytes_per_subbox' must
     both be non-zero.  These last two members are recovered from the J2CI
     box's Ltbl field, if non-zero.  In this case, `num_descendants'
     is calculated as the number of sub-boxes that are expected to
     contain codestreams.  Otherwise, `num_descendants' is 0 and
     sub-boxes are opened sequentially.  When `num_descendants' and
     `streams_per_subbox' are both greater than 1, the `descendants' array
     is created to keep track of all descendants that have been opened but
     are not yet completely parsed (these are the ones with non-NULL entries).
        If sequential parsing is to be used, the `next_subbox_min_id'
     holds the min_id value that will apply to the next sub-box to be opened
     within the main J2CX box, unless the number of codestreams in the `tail'
     object cannot yet be determined, in which case `next_subbox_min_id' holds
     the same value as `tail->min_id'.
        Once a sub-box is opened (randomly or sequentially), it is either
     transplanted to a new descendant, or else it is transplanted directly
     to a `jx_codestream_source' object managed by our `owner' -- which
     action is taken depends on the box type.
        Once a sub-box is transplanted to a `jx_codestream_source' or a
     descendant becomes fully recovered and removed, the associated number of
     codestreams is added to the `fully_recovered_streams' count.  This value
     is used to implement the present object's `check_fully_recovered'
     function.
  */

/*****************************************************************************/
/*                             jx_stream_locator                             */
/*****************************************************************************/

#define JX_STREAM_LOCATOR_SHIFT 6
#define JX_STREAM_LOCATOR_SLOTS (1<<JX_STREAM_LOCATOR_SHIFT)

class jx_stream_locator {
  public: // Member functions
    jx_stream_locator(jx_source *src, jx_stream_locator *p)
      { memset(this,0,sizeof(jx_stream_locator));
        this->owner=src;  this->parent=p; }
    ~jx_stream_locator();
    void add_codestream(int stream_idx, jp2_input_box &box);
      /* Location information is recovered from `box' which is then
         closed.  Note that this function may actually insert a new object
         as parent for the current one. */
    jx_fragment_lst *get_codestream(int stream_idx);
      /* Returns NULL if the codestream location information is not
         available; otherwise, returns a fragment list that can be used
         to access the codestream or, if its `get_ftbl_loc' member returns
         non-negative, to try to parse its FTBL box. */
  private: // Data
    jx_source *owner;
    jx_stream_locator *parent;
    int min_stream_idx; // Absolute index of first codestream represented here
    int shift; // First stream index in each slot is multiple of 2^shift
    union {
      jx_fragment_lst streams[JX_STREAM_LOCATOR_SLOTS];        // If `shift'=0
      jx_stream_locator *descendants[JX_STREAM_LOCATOR_SLOTS]; // If `shift'>0
    };
  };

/*****************************************************************************/
/*                                jx_source                                  */
/*****************************************************************************/

class jx_source {
  public: // Member functions
    jx_source(jp2_family_src *src);
    ~jx_source();
    bool is_top_level_complete() { return top_level_complete; }
      /* Returns true if all top-level boxes in the JPX data source have been
         seen. */
    bool is_jp2h_complete()
      { return jp2h_box_complete || (top_level_complete && !jp2h_box_found); }
      /* Returns true if the global JP2H box has been completely parsed or
         there definitely is no JP2H box in the file. */
    bool are_top_layers_complete()
      { /* Returns true if there can be no more top-level compositing layer
           header boxes in the file.  Note that pure JP2 files have no
           compositing layer headers, so the result is always true for
           such files. */
        return (top_level_complete || (containers!=NULL) || restrict_to_jp2);
      }
    bool are_top_codestreams_complete()
      { /* Returns true if there can be no more top-level codestreams
           in the file. */
        if (num_top_chdr_encountered > 0)
          return (top_level_complete || (containers != NULL));
        else
          return top_level_complete;
      }
    bool parse_next_top_level_box(bool already_open=false);
      /* Call this function to encourage the object to parse a new top-level
         box from the JPX data source.  If no new box can be parsed, the
         function returns false.  The function is normally called from
         within `jx_codestream_source::finish', `jx_layer_source::finish' or
         `jx_metanode::finish_reading'.  The function protects itself against
         recursive calls, which can arise where a box is passed to
         `jx_codestream_source::donate_chdr' or `jx_layer_source::donate_jplh'
         which in turn call the present function to seek for further boxes.
         If the function is called while another call to it is in progress, it
         will return false to the second call.  If `already_open' is true,
         the `top_box' object must already be open.  This currently happens
         only when we overshoot the mandatory header while inside
         `jpx_source::open'.  If this function determines that all top level
         boxes that exist or ever will exist for the data source have been
         read, it sets the `top_level_complete' flag and also installs the
         `JX_METANODE_CONTAINER_KNOWN' flag in the root metanode of the
         metadata hierarchy managed by the `meta_manager' object; in this
         case, the function also invokes `jx_metanode::check_parsing_complete'
         on that root node. */
    bool finish_jp2_header_box();
      /* Call this function to parse as much as possible of the JP2 header
         box, without actually opening a new top level box.  If a JP2
         header box has not already been found, or if the thing has already
         been completely parsed, nothing will be done.  The function returns
         true if there is the JP2 header box has been completely parsed
         by the time this function returns.  Otherwise, it returns false. */
    bool get_pixel_format_allowed()
      { return pixel_format_allowed; }
    void warn_pxfm_box();
      /* Generates warning message, but only once, if a pixel format box
         is encountered in a context where it should not be found. */
    jx_codestream_source *add_top_codestream();
      /* Augments the `top_codestreams' array, if necesssary, creates a new
         `jx_codestream_source' object and inserts it into the array with
         `num_top_codestreams' as its index, then increments
         `num_top_codestreams' and also adjusts `total_codestreams' to ensure
         that it is at least as large as `num_top_codestreams'.  This
         function is used only to add top-level codestreams. */
    jx_layer_source *add_top_layer();
      /* Augments the `top_layers' array, if necesssary, creates a new
         `jx_layer_source' object and inserts it into the array with
         `num_top_layers' as its index, then increments `num_top_layers'
         and `total_layers'.  This function is used only to add top-level
         compositing layers and it must not be invoked beyond the point
         at which any JPX container has been discovered. */
    void update_total_codestreams(int num)
      { // Called whenever the number of known codestreams may have changed
        if (num <= total_codestreams) return;
        total_codestreams = num;
        if (last_container != NULL)
          last_container->note_total_codestreams(num);
      }
    void update_total_layers(int num)
      { // Called whenever the number of known compositing layers changes
        if (num > total_layers)
          total_layers = num;
      }
    void update_container_info(jx_container_source *obj,
                               int layer_count, int codestream_count,
                               bool indefinite_reps);
      /* Called from within `jx_container_source::parse_info' each
         time an INFO box is parsed within a JPLX (JPX container) box.
         The `obj' argument points to the container for which an INFO
         box has just been parsed.  The `layer_count' argument identifies
         the total number of compositing layers represented by the file, up
         to and including the last compositing layer in this container.
         Similarly, `codestream_count' identifies the total number of
         codestreams represented by the file, up to and including the last
         codestream defined by this container.  If `indefinite_reps' is true,
         this is the last container in the file and it has an indefinite
         number of repetitions.  In this case, the `layer_count' and
         `codestream_count' arguments are based on the number of
         repetitions of the container that are required to cover the current
         total number of codestreams that are known to exist, assuming that
         at least one repetition occurs no matter what. */
    void update_multistream_info(jx_multistream_source *obj);
      /* Called from within `jx_multistream_source::parse_info' each time
         an INFO box is parsed within a J2CX (Multiple Codstream) box.
         The `obj' argument points to the multistream object whose INFO
         box has just been parsed -- the `lim_id' value is necessarily
         greater than `min_id'. */
    void found_codestream(int stream_idx, jp2_input_box &box)
      { /* This function is invoked each time a new JP2C/FTBL `box' is
           discovered, either at the top-level of the file or within a
           Multiple Codestream box.  The function leaves `box' closed. */
        if (stream_locator == NULL)
          stream_locator = new jx_stream_locator(this,NULL);
        stream_locator->add_codestream(stream_idx,box);
        update_total_codestreams(stream_idx+1);
        if (stream_idx >= total_jp2c_or_ftbl_confirmed)
          total_jp2c_or_ftbl_confirmed = stream_idx+1;
      }
    void remove_fully_recovered_multistream(jx_multistream_source *obj);
      /* Called when a top-level multistream object is found to have
         been fully parsed. */
    void change_stream_locator_root(jx_stream_locator *obj)
      { /* Called from within `jx_stream_locator::add_codestream' if it
           needs to insert a new `jx_stream_locator' object as parent for
           the one whose `add_codestream' function was called. */
        this->stream_locator = obj;
      }
    jx_fragment_lst *find_stream_flst(int stream_idx, bool *is_ready);
      /* This function parses boxes as required in order to recover a
         fragment-list representation for the indicated codestream's
         JP2C/FTBL or JPIP streamed incremental codestream.  If the
         codestream cannot yet be located, the function returns NULL,
         setting *`is_ready'=false.  However, if the function determines
         that the relevant stream will never exist, because it lies beyond
         the set of streams that are available in the file as a whole, an
         error is generated through `kdu_error'.
            The function also tries to ensure that all fragments of a
         fragment table are known and that the main header data-bin of an
         incremental codestream equivalent is completely available.  If
         so, *`is_ready' is set to true; otherwise *`is_ready' is set to
         false. */
    bool confirm_stream(int stream_idx)
      { 
        if (stream_idx >= total_jp2c_or_ftbl_confirmed)
          find_all_streams();
        return (stream_idx < total_jp2c_or_ftbl_confirmed);
      }
      /* Returns true if the existence of the JP2C/FTBL box for the
         indicated codestream can be confirmed. */
    bool find_all_streams();
      /* This function parses boxes as required in an attempt to discover
         the total number of JP2C/FTBL boxes in the file -- this does not
         mean that the boxes need necessarily be visited, because
         Multiple Codestream Info boxes might provide the required counts.
         The function returns true if the number is actually known --
         otherwise further parsing will be required once more data arrives
         in a dynamic cache. */
    bool find_all_container_info()
      { /* This function parses top-level boxes and JPX container INFO boxes
           in an attempt to make the `container_info_complete' member become
           true.  Returns the value of `container_info_complete'. */
        while (!container_info_complete)
          { 
            if ((first_unparsed_container != NULL) &&
                (jx_container_source::parse_info(first_unparsed_container) !=
                 NULL))
              break;
            if (!parse_next_top_level_box())
              break;
          }
        return container_info_complete;
      }
    jx_codestream_source *get_top_codestream(int codestream_id)
      { /* Returns NULL if `codestream_id' does not correspond to a top-level
           codestream that we know about.  Note that this function does
           not attempt to parse containers.  It is invoked only from
           within `jx_layer_source::all_streams_available'. */
        if ((codestream_id < 0) || (codestream_id >= num_top_codestreams))
          return NULL;
        return top_codestreams[codestream_id];
      }
    jx_layer_source *get_top_layer(int layer_id)
      { /* Returns NULL if `layer_id' does not correspond to a top-level
           composigting layer that we know about.  Note that this function
           does not attempt to parse containers.  It is invoked only from
           within `jx_codestream_source::enum_layer_ids'. */
        if ((layer_id < 0) || (layer_id >= num_top_layers))
          return NULL;
        return top_layers[layer_id];
      }
    jx_codestream_source *
      get_codestream(int codestream_id, int &rep_idx, bool add_top=false)
      { 
        while ((codestream_id >= num_top_codestreams) && (containers==NULL))
          if (container_info_complete ||
              !(parse_next_top_level_box() || container_info_complete))
            { 
              if (!(add_top && !are_top_codestreams_complete()))
                return NULL;
              while (codestream_id >= num_top_codestreams)
                add_top_codestream();
              break;
            }
        if (codestream_id < num_top_codestreams)
          { rep_idx=0; return top_codestreams[codestream_id]; }
        else
          return jx_container_source::find_codestream(containers,
                                                      codestream_id,rep_idx);
      }
      /* Returns a pointer to the internal `jx_codestream_source' whose
         logical codestream index matches `codestream_id'.  If the
         codestream has not yet been encountered, `parse_next_top_level_box'
         is used to advance to the point where the codestream is encountered
         or the end of the data source is reached, or
         `parse_next_top_level_box' returns false for other reasons (e.g.,
         incomplete dynamic cache, or a recursive call).
            If the codestream is still not found, if `add_top' is true and
         the number of top-level codestreams is not yet known, the function
         adds top-level codestreams as necessary so that the function can
         return non-NULL.  This behaviour is induced only when
         `jx_layer_source::finish' encounters top-level codestream references
         within a compositing layer header box.
            In any event, if the codestream is not found or added, the
         function returns NULL.  Otherwise, the function returns the relevant
         `jx_codestream_source' object as well as the repetition index if
         the codestream lives within a JPX container.  The function does not
         attempt to call `jx_codestream_source::finish' itself, but the
         caller may do this. */
    jx_layer_source *get_compositing_layer(int layer_id, int &rep_idx)
      { 
        while ((layer_id >= num_top_layers) && (containers==NULL))
          if (container_info_complete ||
              !(parse_next_top_level_box() || container_info_complete))
            return NULL;
        if (layer_id < num_top_layers)
          { rep_idx=0; return top_layers[layer_id]; }
        else
          return jx_container_source::find_layer(containers,layer_id,rep_idx);
      }
      /* Same as `get_codestream', but for compositing layers. */
    j2_dimensions *get_default_dimensions()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_dimensions;
        else return NULL;
      }
      /* Returns NULL if a JP2 header box has not yet been fully parsed, but
         there is a possibility that one will be encountered or fully parsed
         in the future.  Otherwise, it returns a pointer to a non-finalized
         `j2_dimensions' object; if that object's `is_initialized' function
         reports false, there will be no default dimension information for
         codestreams.  This function will not attempt to parse a new top level
         box from the data source, but it will attempt to finish parsing a
         JP2 header box if one has already been found. */
    j2_palette *get_default_palette()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_palette;
        else return NULL;
      }
      /* Returns NULL if a JP2 header box has not yet been parsed to the
         point where a default palette box has been recovered, but
         there is a possibility that one will be encountered or fully parsed
         in the future.  Otherwise, it returns a pointer to a non-finalized
         `j2_palette' object; if that object's `is_initialized' function
         reports false, there will be no default palette.  This function will
         not attempt to parse a new top level box from the data source, but
         it will attempt to finish parsing a JP2 header box if one has
         already been found. */
    j2_component_map *get_default_component_map()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_component_map;
        else return NULL;
      }
      /* Same as `get_default_palette' but for the default component mapping
         box in a JP2 header box, if any. */
    j2_channels *get_default_channels()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_channels;
        else return NULL;
      }
      /* Same as `get_default_palette' but for the default channel definitions
         box in a JP2 header box, if any. */
    j2_resolution *get_default_resolution()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_resolution;
        else return NULL;
      }
      /* Same as `get_default_palette' but for the default resolution
         box in a JP2 header box, if any. */
    j2_colour *get_default_colour()
      {
        if (jp2h_box_complete || finish_jp2_header_box())
          return &default_colour;
        else return NULL;
      }
      /* Returns NULL if a JP2 header box has not yet been fully parsed, but
         there is a possibility that one will be encountered or fully parsed
         in the future.  Otherwise, it returns a pointer to a linked list of
         `j2_colour' objects -- one for each colour description box
         encountered in the JP2 header box.  If the linked list contains
         only one element, it is possible that that element's `is_initialized'
         function will report false, in which case the JP2 header box either
         does not exist or does not contain any colour descriptions.  This
         function will not attempt to parse a new top level box from the data
         source, but it will attempt to finish parsing a JP2 header box if
         one has already been found. */
    jx_metanode *get_metatree()
      { return meta_manager.tree; }
    bool test_box_filter(kdu_uint32 box_type)
      { return meta_manager.test_box_filter(box_type); }
    jp2_data_references get_data_references()
      { return jp2_data_references(&data_references); }
    jp2_family_src *get_ultimate_src()
      { return ultimate_src; }
    jx_composition *get_composition()
      { return &composition; }
    int get_num_top_codestreams()
      { return num_top_codestreams; }
    int get_num_top_layers()
      { return num_top_layers; }
    int get_total_codestreams()
      { return total_codestreams; }
  private: // Data
    friend class kdu_supp::jpx_source;
    jp2_family_src *ultimate_src;
    int ultimate_src_id; // Used to validate identity of ultimate source
    bool have_signature; // If we have seen the signature box
    bool have_file_type; // If we have seen the file-type box
    bool have_reader_requirements; // If we have seen reader requirements box
    bool have_composition_box; // If a top-level composition box is found
    bool is_completely_open; // If `jpx_source::open' has returned true
    bool restrict_to_jp2; // See below
    bool pixel_format_allowed; // If any of the pxfm-related feature flags set
    bool unexpected_pxfm_box; // If `warn_pxfm_box' called already
    bool in_parse_next_top_level_box; // If the function is in progress.
    int num_top_codestreams; // Number of top-level streams found so far
    int num_top_layers; // Number of top-level comp. layers found so far
    int total_codestreams; // Number of codestreams found so far
    int total_layers; // Number of compositing layers found so far
    jp2_input_box top_box; // Used to walk through top level boxes
    bool top_level_complete; // If we have seen all top level boxes
    bool container_info_complete; // See `find_all_container_info'
    jp2_input_box jp2h_box; // Used to parse JP2 header box
    bool jp2h_box_found; // Once we have found the JP2 header box
    jp2_input_box sub_box; // Used to parse JP2 header sub-boxes
    bool jp2h_box_complete; // Once we have finished parsing the JP2 header box
    j2_dimensions default_dimensions;
    bool have_default_bpcc; // If we have seen a bpcc box in the JP2 header
    j2_data_references data_references;
    bool have_dtbl_box; // If `dtbl' box has been encountered
    jp2_input_box dtbl_box; // Holds open `dtbl_box' until fully parsed
    j2_palette default_palette;
    j2_component_map default_component_map;
    j2_channels default_channels;
    j2_colour default_colour; // Head of colour box list
    j2_resolution default_resolution;
    int max_top_codestreams; // Size of the allocated `top_codestreams' array
    jx_codestream_source **top_codestreams; // An array of pointers
    int max_top_layers; // Size of the allocated `top_layers' array
    jx_layer_source **top_layers; // An array of pointers
    int num_top_jplh_encountered; // Top level compositing layer headers found
    int num_top_chdr_encountered; // Top level codestream headers found
    int num_top_jp2c_or_ftbl_encountered; // Top level JP2C/FTBL boxes found
    int total_jp2c_or_ftbl_confirmed; // See below
    int next_multistream_min_id; // 0 until first Multiple Codestream box found
    int num_containers;
    jx_compatibility compatibility;
    jx_composition composition;
    jx_container_source *containers;
    jx_container_source *first_unparsed_container; // Needs `parse_info'
    jx_container_source *last_container;
    jx_multistream_source *multistreams;
    jx_multistream_source *first_unparsed_multistream; // Needs `parse_info'
    jx_multistream_source *last_multistream;
    jx_stream_locator *stream_locator;
  private: // Data related to the metadata hierarchy
    jx_meta_manager meta_manager;
  };
  /* Notes:
       `restrict_to_jp2' is true if the data source is JP2, not just
     JP2 compatible.  In this case, compositing layer header boxes will be
     ignored, and the first codestream header box, if any, will also be
     ignored.
       `next_multistream_min_id' is 0 until a Multiple Codestream box is
     encountered; at that point the value becomes equal to
     `num_top_codestreams' which must be strictly greater than 0 or the
     file is malformed.  Each time a `jx_multistream_source::lim_id' member
     becomes known, `next_multistream_min_id' takes on that value to that
     the next `jx_multistream_source' object can be initialized.
       `total_jp2c_or_ftbl_confirmed' holds the total number of contiguous
     codestream or fragment table boxes that we know must exist in the file,
     even if we do not have direct access to them yet -- can happen if the
     ultimate source of data is a dynamic cache.  The number of top-level
     JP2C/FTBL box headers that we have seen is identical to the number
     whose existence can be confirmed.  However, when multiple codestream
     boxes are encountered, we can confirm that all JP2C/FTBL boxes
     identified by their leading sub-box (the j2ci box) exist in the file
     even though we might not yet be able to access the relevant box headers.
  */

/*****************************************************************************/
/*                          jx_codestream_target                             */
/*****************************************************************************/

class jx_codestream_target {
  public: // Member functions
    jx_codestream_target(jx_target *owner, int idx,
                         jx_container_target *cont)
      { // Note: `cont' should be NULL for top-level codestreams.
        this->owner = owner; this->container = cont; this->id = idx;
        finalized = chdr_written = have_breakpoint = false;
        last_simulation_phase = 0; codestream_count = 0;
        i_param = 0; addr_param = NULL; next = NULL;
      }
    void finalize();
      /* This function is called from the owning `jx_target' object inside
         `write_header'.  You must call this function prior to
         `jx_layer_target::finalize' and prior to `write_chdr'. */
    bool is_chdr_complete()
      { return (chdr_written && (last_simulation_phase == 0)); }
      /* Returns true if `write_chdr' has completed successfully already. */
    bool is_complete()
      { return (chdr_written && (last_simulation_phase == 0) &&
                (codestream_count > 0) && !jp2c.exists()); }
      /* Returns true only once the contiguous codestream has been completely
         written. */
    j2_component_map *get_component_map()
      { assert(finalized); return &component_map; }
      /* Used by `jx_layer_target' to finish a compositing layer.  This is
         generally done prior to the point at which `write_chdr' or
         `copy_defaults' is called. */
    jp2_output_box *write_chdr(jp2_output_box *super_box,
                               int *i_param, void **addr_param,
                               int simulation_phase=0);
      /* Writes a codestream header box, either at the top-level of the file
         (if `super_box' is NULL) or else as a sub-box of `super_box'.
         Before writing each of the sub-boxes, the function uses the
         `jx_target' object to see if any defaults are available which
         exactly match the sub-box which is about to be written.  If so, the
         sub-box need not be written.  In any event, though, a codestream
         header box is always written, even if it turns out to be empty.
            If a breakpoint was installed, this function returns prematurely
         with a pointer to the codestream box itself, setting *`i_param'
         and *`addr_param' (if non-NULL) equal to the breakpoint's integer
         and address parameters, respectively.  In this case, the function
         will need to be called again.
           If this object is found within a JPX container, it may be
         necessary for the write operation to be simulated.  In this case,
         the function will be called first with `simulation_phase' non-zero
         until it returns NULL, and then later with `simulation_phase=0'
         equal to false until it returns NULL.  In fact, there can be
         up to two simulation phases; each time `simulation_phase' changes,
         the writing process begins again.
      */
    void copy_defaults(j2_dimensions &default_dims, j2_palette &default_plt,
                       j2_component_map &default_map);
      /* Copies any initialized `dimensions', `palette' and `component_map'
         objects into the file-wide defaults supplied via this function's
         arguments. */
    void adjust_compatibility(jx_compatibility *compatibility);
      /* Called before writing a reader requirements box, this function
         gives the object an opportunity to add to the list of features
         which will be included in the reader requirements box and to
         signal that the file will not be JP2 compatible if that is found
         to be the case. */
  private: // Data
    friend class kdu_supp::jpx_codestream_target;
    jx_target *owner;
    jx_container_target *container; // Non-NULL if inside container
    int id;
    int codestream_count; // # `open_stream' of `write_frament_table' calls
    bool finalized; // If `finalize' has been called
    bool chdr_written; // If codestream header box has been written
    bool have_breakpoint; // If `jpx_codestream_target::set_breakpoint' used
    int last_simulation_phase; // Value of `simulation_phase' passed in last
                               // call to `write_chdr'
    int i_param; // Set by `jpx_codestream_target::set_breakpoint'
    void *addr_param; // Set by `jpx_codestream_target::set_breakpoint'
    jx_fragment_list fragment_list;
    j2_dimensions dimensions;
    j2_palette palette;
    j2_component_map component_map;
    jp2_output_box chdr; // Maintain state between calls to `write_chdr'
    jp2_output_box jp2c; // Used to write the contiguous codestream box.
  public: // Links for including this object in a list
    jx_codestream_target *next; // Next compositing layer in sequence
  };

/*****************************************************************************/
/*                          jx_track_target                            */
/*****************************************************************************/

struct jx_track_target {
    jx_track_target()
      { layer_offset=num_track_layers=0; next=NULL; }
    int layer_offset; // Sum of base layers in all previous tracks
    int num_track_layers; // Number of base layers associated with track
    jx_composition composition;
    jx_track_target *next;
  };

/*****************************************************************************/
/*                           jx_container_target                             */
/*****************************************************************************/

class jx_container_target : public jx_container_base {
  public: // Member functions
    jx_container_target(jx_target *owner, int container_id,
                        int num_top_layers, int num_top_codestreams,
                        int num_repetitions, int num_base_layers,
                        int num_base_codestreams, int first_layer,
                        int first_codestream);
       /* Note: if the number of repetitions is to be indefinite,
          `num_repetitions' is passed as 0. */
    ~jx_container_target();
    void finalize(int &cumulative_frame_count,
                  kdu_long &cumulative_duration);
      /* This function is called from the owning `jx_target' object inside
         its `finalize_all_containers' function.  On input,
         the function's two arguments should hold the number of frames
         and the associated duration (milliseconds) of all composition
         instructions found in the top-level composition box and all
         preceding containers.  On exit, these values are updated to
         reflect the new total number of frames (same for all
         presentation tracks) and the new total duration. */
    jp2_output_box *write_jclx(int *i_param, void **addr_param,
                               int simulation_phase, kdu_long *file_pos,
                               jp2_output_box **access_jclx);
      /* This function serves a range of purposes.  Its basic intent is to
         open a JCLX box and fully write the box's contents, except for
         any embedded metadata.
            The function may be invoked directly from
         `jpx_target::write_headers', in which case the `access_jclx'
         argument will be NULL.  It may also be invoked from within
         `jx_target::write_or_simulate_earlier_containers', in which case
         `access_jclx' will also be NULL.  In both of these cases, the
         base `first_metanode' and `last_metanode' members must be NULL --
         that is, these forms of invocation are only for writing the JCLX
         box of a container that has no embedded metanodes.  Also in these
         cases, the JCLX box is closed if the function succeeds (NULL return).
         As with all interruptable content generation functions, the
         function may return prematurely if it encounters an application
         installed breakpoint, in which case the breakpoint details will be
         written to `i_param' and `addr_param', and the function returns a
         pointer to an open `jp2_output_box' into which the application may
         write any extra sub-boxes of interest.
            The function may also be invoked from within `jx_metanode::write'
         when container-embedded metadata is encountered.  In this case,
         `access_jclx' will be non-NULL.  The function again attempts to
         write all relevant content, except for the embedded metadata,
         returning prematurely with a non-NULL `jp2_output_box' pointer if
         an application-defined breakpoint is encountered.  In this case,
         however, the function leaves the JCLX box open and returns a pointer
         to it via the `access_jclx' argument so that it can be used to write
         embedded metadata.  Once the function returns NULL, such metadata
         writing can occur and the function can be called over and over again,
         each time returning the open JCLX box via `access_jclx', until the
         caller eventually finishes writing all metadata and closes the box
         itself.  In this specific case, the function always invokes
         `jx_target::write_or_simulate_earlier_containers' first, so as to
         ensure that any JPX containers without embedded metadata that fall
         between two containers that do have embedded metadata (both needing
         to be simultated and then written to fill in link metanode target
         addresses) will get incorporated into the metadata-driven simulation
         and writing process.
            Before this function is called, it is important that `finalize'
         has already been called (this is done through
         `jx_targt::finalize_all_containers', that is invoked as necessary
         from `jpx_target::write_headers' and `jpx_target::write_metadata'
         before they reach a point where the present function might be called.
            The `simulation_phase' argument may be non-zero if this function
         is called from `jx_metanode::write' or from
         `jx_target::write_or_simulate_earlier_containers'.  In this case,
         whenever a JCLX box is opened, the `simulation_phase' value is
         passed to `jx_target::open_top_box'.  The object remembers internally
         whether or not it is in the midst of a simulated write so that it
         can reset relevant state variables when the `simulation_phase'
         changes.
            If `file_pos' is non-NULL, it is used to store the location
         in the file at which data is to be written.  It is assumed that
         call calls from `jx_metanode::write' to this function provide a
         reference to exactly the same `file_pos' variable, so that the
         function can accumulate information in that variable. When the
         JCLX box is first opened its location is stored in `file_pos';
         later, when the function first completes the generation of all
         relevant boxes (not including any embedded metanodes), it invokes
         `jx_target::note_jclx_written_or_simulated' and augments the value at
         `file_pos' by the total number of bytes that have been written,
         so that if the caller adds new sub-boxes, it knows the location
         at which they are being written.
      */
    bool start_codestream(int rep_idx);
      /* This function is called immediately before generating the
         contiguous codestream box or the fragment table inside one of the
         `base_codestreams'.  The `rep_idx' argument holds the repetition
         index for the relevant codestream, starting from 0.  The function
         returns false if `rep_idx' >= `num_repetitions', unless
         `num_repetitions' is 0.  The function also verifies that there are
         `num_base_codestreams' calls to this function with `rep_idx'=0
         before any calls with `rep_idx'=1 and so forth.  This function
         manipulates state variables `next_generated_rep' and
         `rep_generated_streams'. */
    bool is_complete()
      { /* Returns true if all content for this container has been generated;
           note, however, that we can generally only know if the generation
           of codestreams has started, but not finished. */
        return (jclx_written && (!write_in_progress) &&
                (last_simulation_phase == 0) && (!jclx.exists()) &&
                ((num_base_codestreams == 0) ||
                 ((rep_generated_streams == 0) && (next_generated_rep > 0) &&
                  (indefinite_reps || (next_generated_rep == known_reps)))));
      }
    void adjust_compatibility(jx_compatibility *compatibility);
      /* Called before writing a reader requirements box, this function
         gives the object an opportunity to add to the list of features
         which will be included in the reader requirements box and to
         signal that the file will not be JP2 compatible if that is found
         to be the case. */
    jx_codestream_target *get_base_codestream(int idx)
      { // Note that `idx' is the absolute index of a base codestream within
        // the container, not its relative index.
        idx -= first_codestream_idx;
        if ((idx >= 0) && (idx < num_base_codestreams))
          return base_codestreams[idx];
        else
          return NULL;
      }
    jx_container_target *get_next() { return (jx_container_target *) next; }
    jx_container_target *get_prev() { return (jx_container_target *) prev; }
  private: // Data
    friend class kdu_supp::jpx_container_target;
    jx_target *owner;
    jx_layer_target **base_layers; // Array of pointers to base layers
    jx_codestream_target **base_codestreams; // Array of ptrs to base streams
    kdu_uint32 num_tracks;
    jx_track_target *tracks;
    jx_track_target *last_track; // Tail of above list
  private: // Data found or modified only during or after finalization
    int first_frame_idx;
    int num_frames_per_track;
    kdu_long first_frame_time; // Same for all presentation tracks
    kdu_long track_duration;
    bool finalized;
    bool write_in_progress; // If a call to `write_jclx' returned prematurely
    bool jclx_written; // Once all content written/simulated except metanodes
    int last_simulation_phase; // Value of `simulation_phase' last passed to
                               // `write_jclx'
    int num_chdr_written;      // These get reset if `simulation_phase' value
    int num_jplh_written;      // passed to `write_jclx' differs from
    jx_track_target *first_unwritten_track; // `last_simulation_phase'.
    jp2_output_box jclx; // Maintain state between calls to `write_jclx'
  private: // Data used to keep track of generated codestreams
    int next_generated_rep; // Index of repetition with any stream started
    int rep_generated_streams; // Num streams started in this repetition
  };

/*****************************************************************************/
/*                             jx_layer_target                               */
/*****************************************************************************/

class jx_layer_target {
  public: // Member functions
    jx_layer_target(jx_target *owner, int idx, jx_container_target *cont)
      { 
        this->owner = owner; this->id = idx, this->container = cont;
        finalized = creg_written = jplh_written = false;
        have_breakpoint = need_creg = false;  last_simulation_phase = 0;
        i_param = 0; addr_param = NULL; last_colour = NULL; next = NULL;
        registration.set_container(container);
      }
    ~jx_layer_target()
      { j2_colour *scan;
        while ((scan=colour.next) != NULL)
          { colour.next = scan->next; delete scan; }
      }
    bool finalize();
      /* This function is called from the owning `jx_target' object inside
         `write_header'.  You must call this function prior to
         `write_jplh'.  The function returns true if a component registration
         box is required for this compositing layer.  It is important to
         know this, since if any compositing layer header box contains a
         component registration box, they all must. */
    bool check_jplh_complete(bool need_creg_boxes)
      { 
        if ((last_simulation_phase != 0) || !jplh_written) return false;
        assert(creg_written == need_creg_boxes);
        return true;
      }
      /* Returns true if `write_jplh' has completed successfully already. */
    jp2_output_box *write_jplh(jp2_output_box *super_box, bool write_creg_box,
                               int *i_param, void **addr_param,
                               int simulation_phase=0);
      /* Writes a compositing layer header box to the top-level of the
         file (if `super_box' is NULL) or else as a sub-box of `super_box'.
         Before writing each of the sub-boxes, the function uses the
         `jx_target' object to see if any defaults are available which
         exactly match the sub-box which is about to be written.  If so, the
         sub-box need not be written.  In any event, though, a compositing
         layer header box is always written, even if it turns out to be empty.
            Codestream registration boxes are written if and only if the
         `write_creg_box' argument is true.  If any layer header box contains
         a codestream registration box, they all must.
            If a breakpoint was installed, this function returns prematurely
         with a pointer to the codestream box itself, setting *`i_param'
         and *`addr_param' (if non-NULL) equal to the breakpoint's integer
         and address parameters, respectively.  In this case, the function
         will need to be called again.
            If this object is found within a JPX container, it may be
         necessary for the write operation to be simulated.  In this case,
         the function will be called multiple times with different values of
         `simulation_phase'; for each such value it is called until it returns
         NULL; the final set of calls, in which data is actually written
         to the file, has `simulation_phase'=0.
      */
    void copy_defaults(j2_resolution &default_res,
                       j2_channels &default_channels,
                       j2_colour &default_colour);
      /* Copies any initialized `resolution', `channels' and `colour'
         objects into the file-wide defaults supplied via this function's
         arguments. */
    void adjust_compatibility(jx_compatibility *compatibility);
      /* Called before writing a reader requirements box, this function
         gives the object an opportunity to add to the list of features
         which will be included in the reader requirements box and to
         signal that the file will not be JP2 compatible if that is found
         to be the case. */
    bool uses_codestream(int idx);
      /* Returns true if a codestream with the indicated index is used by
         the current compositing layer.  This is used to implement the
         functionality expected of `jpx_target::write_headers'. */
  private: // Data
    friend class kdu_supp::jpx_layer_target;
    jx_target *owner;
    jx_container_target *container; // NULL if this is a top-level layer
    int id;
    bool finalized; // If `finalize' has been called
    bool need_creg; // If this object's `registration' member is non-trivial
    bool jplh_written; // If the compositing layer header box has been written
    bool creg_written; // If CREG box was written with the JPLH box
    bool have_breakpoint; // If `jpx_layer_target::set_breakpoint' used
    int last_simulation_phase; // Value of `simulation_phase' last passed to
                               // `write_jplh'
    int i_param; // Set by `jpx_layer_target::set_breakpoint'
    void *addr_param; // Set by `jpx_layer_target::set_breakpoint'
    j2_resolution resolution;
    j2_channels channels;
    j2_colour colour; // First element in linked list of boxes
    j2_colour *last_colour; // Last element added to linked list (or NULL)
    jx_registration registration;
    jp2_output_box jplh; // Maintain state between calls to `write_jplh'
  public: // Links for including this object in a list
    jx_layer_target *next; // Next compositing layer in sequence
  };

/*****************************************************************************/
/*                           jx_multistream_target                           */
/*****************************************************************************/

#define JX_MIN_FTBL_LEN (8+8+2+12+2)

struct jx_multistream_target {
  public: // Member functions
    jx_multistream_target(int ftbl_len=JX_MIN_FTBL_LEN)
      { 
        next_subbox_offset = 0;  out_box_len = 0;
        streams_per_subbox = 0;  bytes_per_subbox = 0;
        expected_ftbl_len = ftbl_len;   multi_sub = NULL;
        max_codestreams = num_codestreams = 0;
      }
    ~jx_multistream_target()
      { 
        finish(); // Just in case
        if (multi_sub != NULL)
          delete multi_sub;
      }
    void init(int max_cs);
      /* `max_cs' identifies the maximum number of codestreams that can be
         written within this level in the aggregation tree.  This function
         may be called again, but only after a call to `finish'. */
    void finish();
      /* This function causes all open boxes managed by this object to be
         closed and finished, in the sense that any required free boxes
         are written and all J2CI info boxes are written/rewritten as
         necessary to ensure that the aggregated codestream representation
         is complete and legal.  Thereafter, any call to `write_stream_ftbl'
         will return false.  If you have not already called `close_boxes'
         by the time you need to finish the aggregated representation, it
         is best not to do so, because `close_boxes' allocates space for
         codestreams that you may not ever intend to write. */
    void close_boxes();
      /* Closes all open boxes managed by this object, if there are any.
         The function does nothing unless the `out_box' member is open.
         This function needs to be called as soon as another top-level
         box must be opened within the file, but this does not prevent
         further calls to `write_stream_ftbl' from succeeding.  This is
         because the `close_boxes' call leaves unused space within the
         relevant boxes reserved via `free' sub-boxes.  This unused space can
         subsequently be overwritten by future calls to `write_stream_ftbl'
         until no more remains, at which point that function returns false.
         Note that this function must not be called until after the first
         call to `write_stream_ftbl' -- thereafter, it can be called at
         any time. */
    void fix_boxes(kdu_long total_bytes, jp2_output_box *super_box);
      /* This function plays a similar role to `close_boxes', except that
         it is called before anything is written to the box, immediately
         after `init' and only when constructing an aggregation node within
         another existing aggregation node, whose output box has been opened,
         closed and is being rewritten.  In this case, it can happen that
         the size of the output box for the present object needs to be set
         to exactly the number of bytes that are left in the containing
         `super_box', regardless of how many codestreams nominally need to
         be written.  Any space left at the end (if this is too many bytes)
         will be occupied by a free box.  This function leaves the present
         object's `out_box' object closed, but with a positive `out_box_len'
         value. */
    bool write_stream_ftbl(jx_fragment_list &fragment_list, jx_target *tgt,
                           jp2_output_box *super_box);
      /* Returns false if the current object cannot accommodate the
         fragment table required to record the fragments in `fragment_list'.
         This happens if either the maximum number of codestreams is
         encountered, or there is not enough space in a pre-allocated
         top-level box to accommodate the fragment table.  These two
         conditions occur together unless the number of fragments required
         to describe a codestream exceeds the expected value.
            If this is the first call to this function since `init', exactly
         one of `tgt' or `super_box' must be non-NULL, allowing the `out_box'
         object to be opened for the first time.  Otherwise, both arguments
         may be NULL. */
  private: // Helper functions
    void write_info_box(bool rewrite);
      /* Writes/rewrites the J2CI info box.  If `rewrite' is false, we are
         writing the box for the first time, so we actually write 0's for
         both the Ncs and Ltbl fields.  Otherwise, the info box is being
         rewritten from within a call to `finish', so we write the actual
         number of codestreams for the Ncs field, and we also write a
         non-zero value for the Ltbl field if this can legitimately
         be done. */
    static int
      size_container(int num_streams, int &subbox_streams, int ftbl_len);
      /* This static function is used to compute dimensions so as to
         determine box layout, nesting depth and the amount of space that
         needs to be allocated when pre-writing boxes inside the `close_boxes'
         function.  On entry, `num_streams' is the number of codestreams
         that need to be represented by a container, which is either a
         single FTBL box (only if `num_streams' is 1) or a J2CX box
         (if `num_streams' > 1).  The expected length of an FTBL box is
         supplied as the `ftbl_len' argument.  The function returns the
         number of bytes required by the container, along with the
         `subbox_streams' value, which identifies the number of codestreams
         to be represented in each sub-box of the container, except possibly
         the last one; this value will be set to 0 only if `num_streams' <= 1.
            The function returns a -ve value if the size of the container
         would equal or exceed 2^26 bytes (i.e., 64 Mbytes).  This length
         constraint means that all boxes can have short (8 byte) headers and
         that the length of a container can always be recorded within the
         lower 26 bits of the Ltbl field of a Multiple Codestream Info
         (j2ci) box.  If a negative return value is detected, the
         `num_streams' value should be reduced (typically dividing by 2 at a
         time) and the function should be tried again. */
  public: // Data members
    jp2_output_box out_box; // Rewritable
    jp2_output_box j2ci_box; // Rewritten as needed
    kdu_long next_subbox_offset; // See below
    kdu_long out_box_len; // See below
    int streams_per_subbox; // Set by `init'; reset to 0 by `finish'
    kdu_long bytes_per_subbox; // 0 from `init'; -ve if variable; see below
    int expected_ftbl_len; // Expected length of a single fragment table
    jx_multistream_target *multi_sub; // Created as required; see below
    int max_codestreams; // Value of `max_cs' passed to `init'
    int num_codestreams; // Total streams recorded within this aggregation node
  };
  /* Notes:
        This class is used to implement the functionality described in
     connection with `jpx_target::configure_codestream_aggregation'.  Each
     instance of this class represents a single node within an aggregation
     tree that is used to aggregate codestreams into JCLX boxes.
        As this object is used, the `expected_ftbl_len' member keeps track
     of the number of bytes that we expect to need to record a fragment table,
     based on the maximum number of fragments required to accommodate each
     call to `write_stream_ftbl' so far.  The `init' function sets an
     initial value for `expected_ftbl_len' unless it was previously non-zero,
     in which case `init' uses the pre-existing value.
        If `multi_sub' is already non-NULL when a call to `write_stream_ftbl'
     arrives, the call is passed recursively to that object.  Otherwise, the
     function evaluates whether or not a subordinate `multi_sub' object
     should be created.
        The `next_subbox_offset' and `out_box_len' values are 0 from the point
     at which `init' is called until `close_boxes' or `finish' is called.
     Thereafter, `out_box_len' is guaranteed to hold the total body length of
     `out_box' (i.e., not including its header) and `next_subbox_offset' holds
     the offset from the start of the body of `out_box' to the start of the
     free sub-box (if any) that occupies pre-allocated yet unwritten bytes
     within the `out_box'.  These bytes will be overwritten by calls to
     `write_stream_ftbl' that arrive after `close_boxes' is called.
        The `streams_per_subbox' value is set by `init' and then remains
     fixed, no matter what.  This is the number of codestreams that we will
     attempt to include in any sub-box written after the J2CI sub-box,
     except possibly the last one.  If this value is 1, all sub-boxes are
     FTBL boxes.  Otherwise, all sub-boxes (except possibly the last one)
     are J2CX boxes for which an `multi_sub' object is required.
        The `bytes_per_subbox' value is initialized to 0.  It becomes non-zero
     when the first sub-box is completed.  Subsequently if any sub-box takes
     a different number of bytes (other than the last one) or a different
     number of streams, due to changes in the number of bytes required to
     represent an FTBL, the value is set to -1 to mark this condition. */

/*****************************************************************************/
/*                                jx_target                                  */
/*****************************************************************************/

class jx_target {
  public: // Member functions
    jx_target(jp2_family_tgt *tgt);
    ~jx_target();
    kdu_long open_top_box(jp2_output_box *box, kdu_uint32 box_type,
                          int simulation_phase=0);
      /* Opens a new top-level output box, but keeps an internal record of
         the open box to make sure that two top-level boxes are not open
         at the same time.  If an open top level box already exists, the
         present function will generate an error.  All boxes supplied as
         arguments must be persistent for the life of the `jx_target' object
         so that it can always check whether or not they are still open.
         To escape from this condition, you may call the function with a NULL
         `box' argument when the top-level box is closed; in this case, you
         are free to destroy the box which was previously opened.
           If `simulation_phase' is non-zero, the box will be opened in such
         a way that written data disappears, rather than contributing to the
         target file.  This is used to discover the locations and lengths of
         the targets (or potential targets) of link (cross-reference) metadata
         nodes.  Each time `simulation_phase' changes, the state is reset to
         the point at which the last non-simulated write completed.
           The function returns the location of the first byte of the box to
         be written (or simulated).  This is useful during simulation, since
         it can be used to compute the locations of all constituent boxes,
         once they are closed. */
    void write_collected_boxes(const jp2_output_box *src);
      /* Dumps the contents of the `src' box directly to the top level of
         the file, using `src->write_box' to access this information.
         The `src' box must hold a sequence of sub-boxes -- that is,
         `src' should be a super-box.  The header of the `src' box is
         irrelevant and will not be written; however, `src' should have
         been opened as a grouping box (`jp2_group_4cc').  This function
         is currently used only to realize the functionality of
         `jx_meta_target'.  It is an error to invoke this function unless
         the last call to `open_top_box' passed `simulation_phase'=0 and
         there are no open top-level boxes.  With this in mind, you should
         always call `open_top_box' first with `box'=NULL, `box_type'=0
         and `simulation_phase'=0 -- does not write anything, but
         ensures that the target is in a state where top-level content
         is ready to be written. */
    void write_stream_ftbl(jx_fragment_list &fragment_list);
      /* `jpx_codestream_target::write_fragment_table' uses this function
         to write its fragment table. */
    void open_stream(jp2_output_box *stream_box);
      /* `jpx_codestream_target::open_stream' uses this function to open the
         supplied `stream_box' as a contiguous codestream box at an
         appropriate location in the file.  The `stream_box' is remembered
         internally and must be closed before this function is called
         again, or `open_top_box' is called, or the `close' function is
         called. */
    void close_any_open_stream();
      /* This function invoked automatically when a new top-level box needs
         to be opened, when a new call to `open_stream' or `write_stream_ftbl'
         arrives, or when the file is closed.  The name is perhaps slightly
         misleading, because the function will generate an error if it
         finds that any box referenced by `last_opened_jp2c' is still open --
         i.e., the function does not close such boxes.  However, the function
         does make `last_opened_jp2c' NULL and also plays a very important
         role in updating `multistream_root' with a reference to the
         now closed codestream, in the event that the codestream was not
         being written to the top level of the file. */
    jp2_output_box *
      write_top_level_headers(int *i_param, void **addr_param,
                              int codestream_threshold=-1);
      /* This function does a lot of the work of `jpx_target::write_headers',
         except that it writes only top-level composition, compositing
         layer and codestream header boxes.  The function does nothing if
         the `top_headers_complete' member is true.
           As with all content generation functions, application installed
         breakpoints may be encountered, in which case the function leaves
         behind sufficient state information to resume where it left off
         and returns a pointer to an open `jp2_output_box' into which the
         caller may write auxiliary sub-boxes, using `i_param' and
         `addr_param' to return information about the breakpoint. */
    void finalize_all_containers();
      /* This function can be called at any time after the `composition'
         object has been finalized.  If `first_unfinalized_container' is
         non-NULL, it finalizes that container and all subsequent containers,
         leaving `first_unfinalized_container' equal to NULL.  The function
         must be called before we enter a context in which the
         `jx_container_target::write_jclx' function might potentially be
         invoked. */
    jp2_output_box *
      write_or_simulate_earlier_containers(jx_container_target *caller,
                                           int *i_param, void **addr_param,
                                           int simulation_phase);
      /* This function is invoked only from within `caller->write_jclx', when
         that function is invoked from within `jx_metanode::write'.  The
         purpose of the present function is to ensure firstly that all
         JPX containers up to and including `caller' have been finalized, and
         secondly that all JPX containers up to, but not including `caller'
         have been written or simulated (depending on the `simulation_phase'
         value).  It is essential that this function not be called until all
         top level codestreams, compositing layers and the composition box
         have been written.  This means that these things should be done,
         if necessary, from within `jx_meta_manager::write_metadata'.
           As with all content generation functions, application installed
         breakpoints may be encountered, in which case the function leaves
         behind sufficient state information to resume where it left off
         and returns a pointer to an open `jp2_output_box' into which the
         caller may write auxiliary sub-boxes, using `i_param' and
         `addr_param' to return information about the breakpoint. */
    void note_jclx_written_or_simulated(jx_container_target *caller,
                                        int simulation_phase);
      /* This function is called from within `jx_container_target::write_jclx'
         once it has finished writing or simulating a JPX container.  This
         bumps along the `first_unwritten_container' or
         `first_unsimulated_container' member, as appropriate. */
    jx_codestream_target *get_codestream(int codestream_id)
      {
        jx_codestream_target *sp=codestreams;
        for (; (codestream_id>0) && (sp!=NULL); codestream_id--, sp=sp->next);
        return sp;
      }
      /* Returns a pointer to the indicated codestream, if one exists, or else
         NULL.  Used by `jx_layer_target' to build associations between
         compositing layers and their codestreams. */
    int get_num_top_codestreams()
      { return num_top_codestreams; }
    bool can_write_codestreams()
      { return main_header_complete && !headers_in_progress; }
    j2_dimensions *get_default_dimensions()
      { return &default_dimensions; }
    j2_palette *get_default_palette()
      { return &default_palette; }
    j2_component_map *get_default_component_map()
      { return &default_component_map; }
    j2_channels *get_default_channels()
      { return &default_channels; }
    j2_colour *get_default_colour()
      { return &default_colour; }
    j2_resolution *get_default_resolution()
      { return &default_resolution; }
    jp2_data_references get_data_references()
      { return jp2_data_references(&data_references); }
    jx_composition *get_composition()
      { return &composition; }
    bool check_header_or_metadata_in_progress()
      { return headers_in_progress || metadata_in_progress; }
  public: // The following are called when a new instance of an indefinitely
           // repeated JPX container is discovered dynamically.
    void add_new_container_layers(int num)
      { total_layers += num; }
    void add_new_container_codestreams(int num)
      { total_codestreams += num; }
  private: // Data
    friend class kdu_supp::jpx_target;
    jp2_family_tgt *ultimate_tgt;
    jp2_family_tgt simulated_tgt; // Opened/closed as needed by `open_top_box'
    jp2_output_box *last_opened_top_box;
    jp2_output_box box; // Stable resource to use with `open_top_box'
    int last_simulation_phase; // Value of `simulation_phase' last passed to
                               // `open_top_box'.
    bool need_creg_boxes; // If any layer needs creg, they all do
    int num_top_codestreams; // Number added so far
    int num_top_layers; // Number added so far
    int total_codestreams; // Including those found in containers
    int total_layers; // Including those found in containers
    int num_containers; // Number of JPX containers added so far
    j2_data_references data_references;
    j2_dimensions default_dimensions;
    j2_palette default_palette;
    j2_component_map default_component_map;
    j2_channels default_channels;
    j2_colour default_colour; // Head of colour box list
    j2_resolution default_resolution;
    jx_codestream_target *codestreams; // List of top-level codestreams
    jx_codestream_target *last_codestream; // Tail of above list
    jx_layer_target *layers; // List of top-level compositing layers
    jx_layer_target *last_layer; // Tail of above list
    jx_container_target *containers; // List of JPX containers
    jx_container_target *last_container; // Tail of above list
    jx_compatibility compatibility;
    jx_composition composition;
    jx_meta_manager meta_manager;
  private: // Members used to manage header/metadata writing
    int last_codestream_threshold; // From last call to `write_headers'
    bool file_has_containers; // If container added | `expect_containers' called
    bool headers_in_progress;  // Last call to write_headers returned non-NULL
    bool metadata_in_progress; // Last call to write_metadata returned non-NULL
    bool main_header_complete; // After mandatory headers written
    bool top_headers_complete; // Once we get around to starting on containers
  private: // Members used to manage JPX container finalization and writing
    int cumulative_frame_count; // Up until `first_unfinalized_container'
    kdu_long cumulative_duration; // Up until `first_unfinalized_container'
    jx_container_target *first_unfinalized_container;
    jx_container_target *first_unwritten_container;
    jx_container_target *first_unsimulated_container;
  private: // Members used for codestream aggregation
    int min_j2cx_streams; // These values are configured by
    int max_j2cx_streams; // `configure_codestream_aggregation'
    int next_stream_idx; // Counts calls to `write_stream_ftbl' & `open_stream'
    jx_multistream_target multistream_root; // For codestream aggregation
    jp2_output_box mdat_box; // Currently open mdat box for embedding streams
    jp2_output_box *last_opened_jp2c; // Must be NULL before opening another
    jx_fragment_list tmp_frag_list;
  };

} // namespace kd_supp_local

#endif // JPX_LOCAL_H
