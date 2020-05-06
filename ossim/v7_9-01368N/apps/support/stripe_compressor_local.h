/*****************************************************************************/
// File: stripe_compressor_local.h [scope = APPS/SUPPORT]
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
   Local definitions used in the implementation of the
`kdu_stripe_compressor' object.
******************************************************************************/

#ifndef STRIPE_COMPRESSOR_LOCAL_H
#define STRIPE_COMPRESSOR_LOCAL_H

#include "kdu_stripe_compressor.h"

// Objects declared here:
namespace kd_supp_local {
  struct kdsc_component_state;
  struct kdsc_component;
  struct kdsc_tile;
  struct kdsc_queue;
}

#define KDSC_BUF8      0
#define KDSC_BUF16     1
#define KDSC_BUF32     2
#define KDSC_BUF_FLOAT 6

// Configure processor-specific compilation options
#if (defined KDU_PENTIUM_MSVC)
#  undef KDU_PENTIUM_MSVC
#  ifndef KDU_X86_INTRINSICS
#    define KDU_X86_INTRINSICS // Use portable intrinsics instead
#  endif
#endif // KDU_PENTIUM_MSVC

#if defined KDU_X86_INTRINSICS
#  include "x86_stripe_transfer_local.h"
#  define KDU_SIMD_OPTIMIZATIONS
#elif defined KDU_NEON_INTRINSICS
#  include "neon_stripe_transfer_local.h"
#  define KDU_SIMD_OPTIMIZATIONS
#endif

namespace kd_supp_local {
  using namespace kdu_supp;

#ifdef KDU_SIMD_OPTIMIZATIONS
  using namespace kd_supp_simd;
#endif

  
/* ========================================================================= */
/*                      ACCELERATION FUNCTION POINTERS                       */
/* ========================================================================= */

typedef void
  (*kdsc_simd_transfer_func)(void **dst, void *src, int width,
                             int src_precision, int tgt_precision,
                             bool is_absolute, bool src_signed);

/*****************************************************************************/
/*                            kdsc_component_state                           */
/*****************************************************************************/

struct kdsc_component_state {
  public: // Member functions
    void update(kdu_coords next_tile_idx, kdu_codestream codestream);
      /* Called immediately after processing stripe data for a row of tiles.
         Adjusts the values of `remaining_tile_height', `next_tile_height'
         and `stripe_height' accordingly, while also updating the `buf8',
         `buf16', `buf32' or `buf_float' pointers to address the start of
         the next row of tiles.
            If `remaining_tile_height' is reduced to 0, the function
         copies `next_tile_height' into `remaining_tile_height', decrements
         `remaining_tile_rows' and then uses the codestream interface together
         with `next_tile_idx' to determine a new value for
         `next_tile_height'. */
  public: // Data members that hold global information
    int comp_idx;
    int pos_x; // x-coord of left-most sample in the component
    int width; // Full width of the component
    int original_precision; // Precision recorded in SIZ marker segment
    kdu_coords sub_sampling; // Component sub-sampling factors from codestream
  public: // Data members that hold stripe-specific state information
    int row_gap, sample_gap, precision; // Values supplied by `push_stripe'  
    bool is_signed; // Value supplied by `push_stripe'; always false for `buf8'  
    int buf_type; // One of `KDSC_BUFxxx'; note, 2 lsbs hold log2(bytes/sample)
    union {
      kdu_byte *buf_ptr; // Emphasizes the presence of an anonymous union
      kdu_byte *buf8;   // if `buf_type'=`KDSC_BUF8'=0
      kdu_int16 *buf16; // if `buf_type'=`KDSC_BUF16'=1
      kdu_int32 *buf32; // if `buf_type'=`KDSC_BUF32'=2
      float *buf_float; // if `buf_type'=`KDSC_BUF_FLOAT'=6
    };
    int stripe_height; // Remaining height in the current stripe
    int remaining_tile_height; // See below
    int next_tile_height; // See below
    int max_tile_height;
    int max_recommended_stripe_height;
    int remaining_tile_rows; // See below
  };
  /* Notes:
       `stripe_height' holds the total number of rows in the current stripe
     which have not yet been fully processed.  This value is updated at the
     end of each row of tiles, by subtracting the smaller of `stripe_height'
     and `remaining_tile_height'.
       `remaining_tile_height' holds the number of rows in the current
     row of tiles, which have not yet been fully processed.  This value
     is updated at the end of each row of tiles, by subtracting the smaller
     of `stripe_height' and `remaining_tile_height'.
       `remaining_tile_rows' holds the number of tile rows that have not
     yet been fully processed.  It is decremented each time
     `remaining_tile_height' goes to 0.  The `update' function uses this
     to figure out how to set `next_tile_height'.
       `next_tile_height' holds the value that will be moved into
     `remaining_tile_height' when we advance to the next row of tiles.  It
     reflects the total height of the tile that is immediately below the
     current one -- 0 if there is no such tile.
       `max_tile_height' is the maximum height of any tile in the image.
     In practice, this is the maximum of the heights of the first and
     second vertical tiles, if there are multiple tiles.
       `max_recommended_stripe_height' remembers the value returned by the
     first call to `kdu_stripe_compressor::get_recommended_stripe_heights'. */

/*****************************************************************************/
/*                              kdsc_component                               */
/*****************************************************************************/

struct kdsc_component {
    // Manages processing of a single tile-component.
  public: // Data configured by `kdsc_tile::init' for a new tile
    kdu_coords size; // Tile width, by tile rows left in tile
    bool using_shorts; // If `kdu_line_buf's hold 16-bit samples
    bool is_absolute; // If `kdu_line_buf's hold absolute integers
    int horizontal_offset; // From left edge of image component
    int ratio_counter; // See below
  public: // Data configured by `kdsc_tile::init' for a new or existing tile
    int stripe_rows_left; // Counts down from `stripe_rows' during processing
    int sample_gap; // For current stripe being processed by `push_stripe'
    int row_gap; // For current stripe being processed by `push_stripe'
    int precision; // For current stripe being processed by `push_stripe'
    bool is_signed; // For current stripe being processed by `push_stripe'
    int buf_type; // One of `KDSC_BUFxxx'; note, 2 lsbs hold log2(bytes/sample)
    union { 
      kdu_byte *buf_ptr; // Emphasizes the presence of an anonymous union
      kdu_byte *buf8;   // if `buf_type'=`KDSC_BUF8'=0
      kdu_int16 *buf16; // if `buf_type'=`KDSC_BUF16'=1
      kdu_int32 *buf32; // if `buf_type'=`KDSC_BUF32'=2
      float *buf_float; // if `buf_type'=`KDSC_BUF_FLOAT'=6
    };
    kdu_line_buf *line; // From last `kdu_multi_analysis::exchange_line' call
  public: // Data configured by `kdu_stripe_compressor::get_new_tile'
    int original_precision; // Original sample precision
    int vert_subsampling; // Vertical sub-sampling factor of this component
    int count_delta; // See below
  public: // Acceleration functions
#ifdef KDU_SIMD_OPTIMIZATIONS
  kdsc_simd_transfer_func  simd_transfer; // See below
  kdsc_component *simd_grp; // See below
  int simd_ilv; // See below
  void *simd_dst[4]; // See below
#endif
  };
  /* Notes:
        The `stripe_rows_left' member holds the number of rows in the current
     stripe, which are still to be processed by this tile-component.
        Only one of the `buf8', `buf16', `buf32' or `buf_float' members will
     be non-NULL, depending on whether the stripe data is available as bytes
     or words.  The relevant member points to the start of the first
     unprocessed row of the current tile-component.
        The `ratio_counter' is initialized to 0 at the start of each stripe
     and decremented by `count_delta' each time we consider processing this
     component within the tile.  Once the counter becomes negative, a new row
     from the stripe is processed and the ratio counter is subsequently
     incremented by `vert_subsampling'.  The value of the `count_delta'
     member is the minimum of the vertical sub-sampling factors for all
     components.  This policy ensures that we process the components in a
     proportional way.
           The `simd_transfer' function pointer, along with the other
     `simd_xxx' members, are configured by `kdsc_tile::init' based on
     functions that might be available to handle the particular conversion
     configuration for a tile-component (or collection of tile-components) in
     a vectorized manner.
        `simd_grp', `simd_ilv', `simd_dst' and `simd_lines' work together
     to support efficient transfer from interleaved component buffers, but
     the interpretation works also for non-interleaved buffers.  They
     have the following meanings:
     -- `simd_grp' points to the last component in an interleaved group; if
        NULL there is no SID implementation; the `simd_transfer' function
        is not called until we reach that component.
     -- `simd_ilv' holds the location that the current image component
        occupies within the `simd_grp' object's `simd_dst' array -- the
        address of the current `kdu_line_buf' object's internal array should
        be written to that entry prior to any call to the `simd_transfer'
        function.
     -- `simd_lines' keeps track of 
  */

/*****************************************************************************/
/*                                kdsc_tile                                  */
/*****************************************************************************/

struct kdsc_tile {
  public: // Member functions
    kdsc_tile() { num_components=0; components=NULL; next=NULL; queue=NULL; }
    ~kdsc_tile()
      {
        if (components != NULL) delete[] components;
        if (engine.exists()) engine.destroy();
      }
    void configure(int num_comps, const kdsc_component_state *comp_states);
      /* This function must be called after construction or whenever the
         number of components or their individual attributes (original
         precision or sub-sampling) may have changed.  In practice, the
         function is always invoked when a tile is first constructed or
         retrieved from the `kdu_stripe_compressor::free_tiles' list. */
    void init(kdu_coords idx, kdu_codestream codestream,
              kdsc_component_state *comp_states, bool force_precise,
              bool want_fastest, kdu_thread_env *env,
              int env_dbuf_height, kdsc_queue *env_queue,
              const kdu_push_pull_params *pp_params, int tiles_wide);
      /* Initializes a new or partially completed tile, with a new set of
         stripe buffers, and associated parameters. If the `tile' interface
         is empty, a new code-stream tile is opened, and the various members
         of this structure and its constituent components are initialized.
         Otherwise, the tile already exists and we are supplying additional
         stripe samples.  The object must have been `configure'd before
         this function is called.
            The pointers supplied by the `buf8' or `buf16' member of each
         element in the `comp_states' array, whichever is non-NULL,
         correspond to the first sample in what remains of the current
         stripe, within each component.  This first sample is aligned at the
         left edge of the image.  The function determines the amount by which
         the buffer should be advanced to find the first sample in the current
         tile, by comparing the horizontal location of each tile-component,
         as returned by `kdu_tile_comp::get_dims', with the values in the
         `pos_x' members of the `kdsc_component_state' entries in the
         `comp_states' array.
            In a multi-threaded setting (i.e., where `env' is non-NULL), you
         are required to supply a non-NULL `env_queue' argument, representing
         the queue to which the tile engine is supposed to belong.  If the 
         function is creating the tile processing engine for the first time,
         it should find the `queue' member to be NULL, so it changes
         `queue' to point to `env_queue' and makes sure that the `env_queue'
         identifies us as one of its members (demarcated by the
         `kdsc_queue::first_tile' and `kdsc_queue::last_tile' members).
         Otherwise, the function only verifies that `queue' is identical to
         `env_queue'.
            The `tiles_wide' argument is used by the internal algorithm to
         determine whether a negative `env_dbuf_height' argument should be
         passed straight through to `kdu_multi_analysis::start' to choose
         its own default double buffering strategy or instead set to a
         value which is large enough to accommodate complete buffering of
         the entire tile's data.  The latter is appropriate if the
         codestream has multiple horizontally adjacent tiles and the
         current stripe being pushed into the `kdu_stripe_compressor::push'
         function spans the entire tile.  In this special case we would like
         to make sure that the `push' call does not get blocked waiting for
         a first tile to absorb all of its data before we get to the following
         tile -- we normally keep multiple concurrent tile processing engines
         active even if we do not keep enough tile engines to span an entire
         row of tiles. */
    bool process(kdu_thread_env *env);
      /* Processes all the stripe rows available to tile-components in the
         current tile, returning true if the tile is completed and false
         otherwise.  Always call this function right after `init'.
         When the function returns true, the tile should be passed to
         `kdu_stripe_compressor::release_tile', which invokes its `cleanup'
         function.  However, for tiles that belong to a thread queue (`queue'
         non-NULL, equivalently `env' non-NULL), the entire queue should
         be passed to `kdu_stripe_compressor::release_queue' but not until
         the tile's last processing engine is finished (i.e., this function
         has returned true for all tiles in the queue).
            Typically, calls to `kdu_stripe_compressor::release_queue' should
         be deferred until the processing resources really need to be
         reclaimed -- that maximizes the degree to which jobs from different
         tile processing engines can be overlapped, which in turn minimizes
         the likelihood that worker threads go idle. */
    void cleanup()
      { // NB: `kdu_thread_env::cs_terminate' must have been called already!
        assert(queue == NULL);
        if (tile.exists())
          tile.close(); // Find, since `cs_terminate' called already, if req'd
        engine.destroy();
      }
  public: // Data
    kdu_tile tile;
    kdu_multi_analysis engine;
    kdu_sample_allocator sample_allocator; // Retains buffers used by `engine'
    kdsc_tile *next; // Next free tile, or next partially completed tile.
    kdsc_queue *queue; // Non-NULL only for multi-threaded processing
  public: // Data configured by `kdu_stripe_compressor::get_new_tile'.
    int num_components;
    kdsc_component *components; // Array of tile-components
  };

/*****************************************************************************/
/*                                kdsc_queue                                 */
/*****************************************************************************/

struct kdsc_queue {
  public: // Member functions
    kdsc_queue()
      { first_tile = last_tile = NULL; next = NULL; num_tiles=0; }
    ~kdsc_queue()
      { assert(!thread_queue.is_attached()); }
  public: // Data
    kdu_thread_queue thread_queue;
    kdsc_tile *first_tile;
    kdsc_tile *last_tile;
    int num_tiles; // Number of tiles associated with this queue
    kdsc_queue *next;
  };
  /* Notes:
       This object is required only for multi-threaded processing.  It
       serves to associate a collection of tile processing engines with a
       single queue that can be waited upon (joined).  This is particularly
       interesting for multi-tile images.
          If sample data is pushed into tiles sequentially (all of one tile,
       then all of the next, and so forth), each tile gets its own queue and
       we generally delay waiting upon the queue and cleaning up the
       corresponding tile processing engine at least until we have finished
       pushing data into the next tile.
          If sample data is pushed into a whole row of tiles together, in
       interleaved fashion (one or more lines of one tile, then the next, etc.,
       then back to push more lines into the first tile on the row, and so
       forth), the entire row of tile processing engines is assigned to a
       single queue.  In this case, we generally delay waiting upon the queue
       and cleaning up the row of tile processing engines until we have
       finished pushing data into the next row of tile processing engines.
          In this way, we reduce the chance that joining on queues blocks the
       caller, which in turn maximizes the likelihood that worker threads have
       useful work to do all the time.
  */

} // namespace kd_supp_local

#endif // STRIPE_COMPRESSOR_LOCAL_H
