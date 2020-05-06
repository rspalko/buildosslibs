/*****************************************************************************/
// File: kdu_cache.h [scope = APPS/COMPRESSED_IO]
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
  Describes a platform independent caching compressed data source.  A
complete implementation for the client in an interactive client-server
application can be derived from this class and requires relatively little
additional effort.  The complete client must incorporate networking elements.
******************************************************************************/

#ifndef KDU_CACHE_H
#define KDU_CACHE_H

#include <assert.h>
#include <string.h>
#include "kdu_elementary.h"
#include "kdu_compressed.h"

// Defined here
namespace kdu_supp {
  class kdu_cache;
}

// Defined elsewhere
namespace kd_supp_local {
  struct kd_cache;
}

namespace kdu_supp {
  using namespace kdu_core;

/*****************************************************************************/
/* ENUM                    Data-bin Class Identifiers                        */
/*****************************************************************************/

#define KDU_PRECINCT_DATABIN    0 // Used for precinct-oriented streams
#define KDU_TILE_HEADER_DATABIN 1 // One for each tile in the code-stream
#define KDU_TILE_DATABIN        2 // Used for tile-part oriented streams
#define KDU_MAIN_HEADER_DATABIN 3 // Code-stream main header; only ID=0 allowed
#define KDU_META_DATABIN        4 // Used for meta-data and file structure info
#define KDU_UNDEFINED_DATABIN   5

#define KDU_NUM_DATABIN_CLASSES KDU_UNDEFINED_DATABIN

/*****************************************************************************/
/* ENUM                      Data-bin Marking Flags                          */
/*****************************************************************************/

#define KDU_CACHE_BIN_DELETED   ((kdu_int32) 1)
#define KDU_CACHE_BIN_AUGMENTED ((kdu_int32) 2)
#define KDU_CACHE_BIN_MARKED    ((kdu_int32) 4)

/*****************************************************************************/
/* ENUM                      Data-bin Scanning Flags                         */
/*****************************************************************************/

#define KDU_CACHE_SCAN_START          ((kdu_int32) 0x01)
#define KDU_CACHE_SCAN_PRESERVED_ONLY ((kdu_int32) 0x02)
#define KDU_CACHE_SCAN_PRESERVED_SKIP ((kdu_int32) 0x04)
#define KDU_CACHE_SCAN_NO_ADVANCE     ((kdu_int32) 0x08)
#define KDU_CACHE_SCAN_FIX_CODESTREAM ((kdu_int32) 0x10)
#define KDU_CACHE_SCAN_FIX_CLASS      ((kdu_int32) 0x20)
#define KDU_CACHE_SCAN_MARKED_ONLY    ((kdu_int32) 0x40)
  
/*****************************************************************************/
/*                                 kdu_cache                                 */
/*****************************************************************************/

class kdu_cache : public kdu_compressed_source {
  /* [BIND: reference]
     [SYNOPSIS]
       Implements a caching compressed data source, i.e., one which offers
       the `KDU_SOURCE_CAP_CACHED' capability explained in connection with
       `kdu_compressed_source::get_capabilities'.
       [//]
       The object has two types of interfaces: those used to transfer new
       data into the cache (e.g., data received incrementally over a
       network connection); and those used to retrieve the cached data.
       These two sets of functions may be safely invoked from different
       threads in a multi-threading environment, allowing cache updates to
       occur asynchronously with cached data access.
       [//]
       Where multiple threads are involved, however, it is preferable to
       access/read content via a `kdu_cache' object that is attached to the
       master cache via the `attach_to' function, attaching a separate
       such access portal for each reading thread that may require
       asynchronous access to the content.
       [//]
       When a caching data source is supplied to `kdu_codestream::create'
       the image quality obtained when rendering the code-stream data
       generally improves as more and more compressed data is transferred
       into the cache using the `add_to_databin' function.  Note carefully
       that the object must not be passed into `kdu_codestream::create'
       at least until the main header data-bin has been completed.  This
       may be verified by calling `get_databin_length', with a
       data-bin class of `KDU_MAIN_HEADER_DATABIN'.  Before passing the
       object to `kdu_codestream::create' you must call `set_read_scope',
       passing in these same data-bin class and in-class identifiers
       (`KDU_MAIN_HEADER_DATABIN' and 0, respectively).
       [//]
       Even more functionality may be achieved if you read from a caching
       data source indirectly via a `jp2_source' object.  To do this, you
       must first create and open a `jp2_family_src' object, passing the
       `kdu_cache' object in as the `jp2_family_src' object's data source.
       You can then open JP2 boxes by passing the `jp2_family_src' object
       in the call to `jp2_input_box::open' or to `jp2_source::open'.  In
       fact, you may open multiple boxes simultaneously on the same
       `jp2_family_src' object (even from multiple threads, if you are
       careful to implement the synchronization objects offered by the
       `jp2_family_src' class.  Using the `kdu_cache' object as a source
       for JP2 box and JP2 source reading, provides you with additional
       functionality specifically designed for interacting with the meta-data
       and with multiple code-streams via the JPIP protocol and allows high
       level applications to be largely oblivious as to whether the ultimate
       source of information is local or remote.
       [//]
       Some effort is invested in the implementation of this object to
       ensure that truly massive compressed images (many Gigabytes in size)
       may be cached efficiently.  In particular, both header data and
       compressed precinct data are managed using a special sparse cache
       structure, which supports both efficient random access and dynamic
       growth.
       [//]
       From KDU-7.6, this object no longer provides explicit functions for
       accessing or manipulating internal LRU/MRU (least/most recently used)
       lists.  In particular, `get_next_lru_databin', `get_next_mru_databin',
       `promote_databin' and `demote_databin' have all been removed.
       [//]
       Also, from KDU-7.6 some other functions you might possibly have used
       are now missing.  These are all functions for which thread-safety was
       ambiguous so they might have been used in ways that would either
       necessitate an inefficient internal implementation or an internal
       implementation that might break if thread safety was assumed.  In
       particular, we have removed `get_databin_prefix' -- you should use
       `set_read_scope' instead, possibly via a separate attached `kdu_cache'
       interface, which is very efficient.  Also, `get_next_codestream' has
       been removed, since sequentially walking through anything in a
       dynamically evolving cache is questionable.
       [//]
       On the other hand, KDU-7.6 introduces mechanisms for deleting
       data-bins (`delete_databin' and `delete_stream_class'), for discovering
       and clearing deletion marks (through augmentation of `mark_databin'),
       and for trimming the cache to respect desired size constraints
       (`set_preferred_memory_limit') -- the latter mechanism is based on
       data-bin usage statistics that are maintained internally, so that
       deleted content is less likely to be required in the near future.
       [//]
       KDU-7.6 also introduces a new way to scan through the elements of a
       cache to replace `get_next_codestream', `get_next_lru_databin' and
       `get_next_mru_databin'.  This new mechanism is embodied by the
       `scan_databins' function.
  */
  public: // Lifecycle member functions
    KDU_AUX_EXPORT kdu_cache();
    KDU_AUX_EXPORT virtual ~kdu_cache();
      /* [SYNOPSIS]
           Implicitly calls the `close' function.
      */
    KDU_AUX_EXPORT void attach_to(kdu_cache *existing);
      /* [SYNOPSIS]
           This function may be called to attach the cache object to another
           existing cache object.  Once attached, all attempts to access
           the contents of the cache will actually access the contents of
           the `existing' cache.  The same is true for functions which return
           statistics, such as `get_max_codestream_id', `get_peak_cache_memory'
           or `get_transferred_bytes'.
           [//]
           While attached, the `mark_databin' function will do nothing, since
           this is likely to erase marking flags that might be needed
           by another thread is directly interacting with the primary cache.
           You are allowed to invoke `add_to_databin' and `delete_databin'
           from an attached (i.e., non-primary) cache, but this would be
           rather unusual and may break things if you do not specify that
           the addition/deletion operations are to be recorded by passing
           `mark_if_augmented' to `add_to_databin' and `mark_if_non_empty' to
           `delete_databin'.
           [//]
           Closing the primary `kdu_cache' automatically causes all attached
           caches to be closed.  Note, however, that you must be sure that
           no other thread is accessing an attached `kdu_cache' at the point
           when the primary cache is closed.  Invoking `close' on the
           present object also causes it to be detached from any primary
           `kdu_cache' to which it is attached, but this has no impact on
           the primary cache of course.  These are the only two mechanisms for
           detaching an attached `kdu_cache', noting that the destructor
           implicitly invokes `close'.
           [//]
           This function automatically closes the present cache object if it
           has been used prior to calling this function.  Also, if `existing'
           is itself attached to another `kdu_cache', this function ensures
           that attachment is to the ultimate (primary) cache rather than
           `existing'.
      */
    KDU_AUX_EXPORT virtual bool close();
      /* [SYNOPSIS]
           It is always safe to call this function.  It discards all cached
           data-bins and any associated structures, after which you can start
           using the object again with a clean slate.  Immediately after
           this call, the `get_databin_length' function will return 0 in
           response to all queries.
         [RETURNS]
           Always returns true.
      */
  public: // Functions used to manage cached data
    KDU_AUX_EXPORT virtual bool
      add_to_databin(int databin_class, kdu_long codestream_id,
                     kdu_long databin_id, const kdu_byte data[],
                     int offset, int num_bytes, bool is_final,
                     bool add_as_most_recent=true,
                     bool mark_if_augmented=false);
      /* [SYNOPSIS]
           Augments the cached representation of the indicated data-bin.  The
           data-bin is identified by a class code, `databin_class', a
           code-stream ID (normally 0, unless the cache is used to manage
           multiple code-streams simultaneously) and an in-class identifier,
           `databin_id'.  Currently, the classes defined by this
           object are `KDU_META_DATABIN', `KDU_MAIN_HEADER_DATABIN',
           `KDU_TILE_HEADER_DATABIN', `KDU_PRECINCT_DATABIN' and
           `KDU_TILE_DATABIN'.
           [>>] The `KDU_META_DATABIN' class is used with JP2-family file
                formats to communicate metadata and file structure
                information.  Amongst other things, this metadata provides
                the framework within which the content of code-stream
                specific data-bins are to be interpreted.  For this data-bin
                class, the value of the `codestream_id' is ignored.  Raw
                code-streams have exactly one metadata-bin (the one with
                `databin_id'=0) which must be empty -- it must be signalled
                as finalized after transferring 0 bytes of content data.
           [>>] For the `KDU_MAIN_HEADER_DATABIN' class, the in-class
                identifier must be equal to 0, while the code-stream
                identifier indicates the code-stream whose main header is
                being augmented.
           [>>] For the `KDU_TILE_HEADER_DATABIN' class, the in-class
                identifier (`databin_id') holds the tile number, starting
                from 0 for the tile in the upper left hand corner of the
                image.
           [>>] For the `KDU_PRECINCT_DATABIN' class, the in-class identifier
                has the interpretation described in connection with
                `kdu_compressed_source::set_precinct_scope'.
           [>>] For the `KDU_TILE_DATABIN' class, the in-class identifier has
                the same interpretation as for `KDU_TILE_HEADER_DATABIN'.
                Generally, an interactive communication involves either the
                `KDU_TILE_HEADER_DATABIN' and `KDU_PRECINCT_DATABIN' classes,
                or the `KDU_TILE_DATABIN' class, but not both.
           [//]
           New data may be added to the cache in any desired order, which is
           why the absolute location (`offset') and length (`num_bytes') of
           the new data are explicitly specified in the call.  If the new
           data range overlaps with data which already exists in the cache,
           the present function may either overwrite or leave the overlapping
           data bytes as they were.  Both interpretations are assumed to
           produce the same result, since the nominal contents of any
           given data-bin are not permitted to change over time.
           [//]
           This function is completely thread-safe; multiple threads may
           attempt to add data concurrently, since access is serialized
           internally.  However, this is not recommended, since internal
           state information keeps track of recently visited data-bins in a
           manner that is intended to make access to this function efficient
           if consecutive calls commonly reference nearby data-bins.
         [RETURNS]
           False only if the function was unable to allocate sufficient
           memory or if the `databin_class', `stream_id' or `bin_id' appear
           to be invalid.
         [ARG: is_final]
           If true, the cache is being informed that the supplied bytes
           represent the terminal portion of the data-bin.  If there are no
           holes, the cache representation is complete.  If there are holes,
           the cache representation of the data-bin will be complete once
           these holes have been filled in.  At that time, the
           `get_databin_length' function will set an `is_complete' argument
           to true.
         [ARG: add_as_most_recent]
           Ignored by the current implementation of this object, since the
           concept of "most-recently used" and "least-recently used" not longer
           exists in the form that it did prior to KDU-7.6.  In practice,
           the implementation keeps track of several paths through the cache
           hierarchy, corresponding to added data-bins, open data-bins, and
           so forth, and these paths retain access locks.  Nothing will be
           automatically removed from the cache until those locks are removed,
           and the order in which they are removed depends on the order in
           which the locks disappeared, not the order in which they appeared.
           [//]
           For backward compatibility at least, we do retain this argument;
           you may pass either true or false.
         [ARG: mark_if_augmented]
           If true, and the present call augments the cached contents of the
           indicated data-bin, the data-bin is assigned a special flag that
           allows the application, or higher level API's such as `kdu_client',
           to determine whether a remote JPIP server needs to have its cache
           model updated.  The marked state can be discovered and manipulated
           by calling `mark_databin'.  In particular, if this argument is
           true and the data-bin is augmented by the call, the
           `KDU_CACHE_BIN_MARKED' flag is set; moreover if the data-bin was
           previously unmarked and was non-empty, the `KDU_CACHE_BIN_AUGMENTED'
           flag is also set.  These flags are explained in detail with the
           `mark_databin' function.
      */
    KDU_AUX_EXPORT virtual bool
      delete_databin(int databin_class, kdu_long codestream_id,
                     kdu_long databin_id, bool mark_if_nonempty=true);
      /* [SYNOPSIS]
           Deletes all storage associated with the indicated data-bin from
           the cache.  If `mark_if_nonempty' is false, no indication that the
           data-bin had ever existed will be retained.  On the other hand,
           if `mark_if_nonempty' is true, the function leaves behind sufficient
           state to record the deletion event, unless it can be determined that
           this is not necessary.  In particular:
           [>>] If the data-bin was previously non-empty and not marked with
                any of the flags returned by `mark_databin', the data-bin
                is marked with the `KDU_CACHE_BIN_DELETED' and
                `KDU_CACHE_BIN_MARKED' flags.
           [>>] If the data-bin was already marked with `KDU_CACHE_BIN_DELETED'
                this mark is preserved, along with the `KDU_CACHE_BIN_MARKED'
                flag itself.
           [>>] If the data-bin was marked with `KDU_CACHE_BIN_AUGMENTED',
                this mark is cleared but replaced by `KDU_CACHE_BIN_DELETED'.
           [>>] If none of the above is true, all trace of the data-bin is
                erased, as if `mark_if_nonempty' had been false.
           [//]
           The `KDU_CACHE_BIN_DELETED' mark, if applied, is retained until such
           time as the `mark_databin' function is called referencing this
           data-bin, or until `clear_all_marks' or `set_all_marks' is called.
           [//]
           Also note that data-bins that are both empty and complete (i.e.,
           data-bins that have no bytes at all, but are nonetheless a perfect
           representation of the original source data-bins), may be left
           intact by this function; only the cache trimming machinery (if used)
           can purge these, since they do not occupy any storage buffers of
           their own.
         [RETURNS]
           True if the data-bin was found and was not already in the deleted
           state.
      */
    KDU_AUX_EXPORT virtual int
      delete_stream_class(int databin_class, kdu_long codestream_id,
                          bool mark_if_nonempty=true);
      /* [SYNOPSIS]
           Similar to `delete_databin', except that this function deletes all
           data-bins within a specific data-bin class that belong ot a
           particular codestream.
           [//]
           Currently we do not recommend deleting anything other than
           precinct data-bins (i.e., using `KDU_PRECINCT_DATABIN' for the
           `databin_class') since some parts of the Kakadu machinery (most
           notably `jpx_source') assume that once a codestream header has
           been discovered, it will continue to exist.  We will fix this in
           the future to allow full deletion of codestreams robustly,
           removing this comment at that time.
         [RETURNS]
           Total number of matching data-bins encountered, which were not
           already deleted.
         [ARG: databin_class]
           This value should either be one of the valid data-bin classes, in
           the range 0 to `KDU_NUM_DATABIN_CLASSES'-1.  Note that values less
           than 0 are not currently recognized as wildcards.
         [ARG: codestream_id]
           This value should be non-negative.  Again, values less than 0 are
           not recognized as wildcards and will result in the function
           returning 0.
      */
    KDU_AUX_EXPORT void
      set_preferred_memory_limit(kdu_long preferred_byte_limit);
      /* [SYNOPSIS]
           This function allows the total amount of memory consumed by the
           cache to be bounded.  In practice, calling this function will not
           result in any reduction in the current memory consumption, but
           it is likely to prevent the amount of cached data growing beyond
           the supplied limit in the future.  All memory that has already
           been allocated will continue to be used.  As a result, it is
           normally advisable to call this function before you add any
           content to the cache, after creation or a call to `close'.  Any
           limit configured here is removed by a call to `close'.  Also,
           this call will do nothing if invoked on a `kdu_cache' interface
           that has been attached to another (primary) cache via the
           `attach_to' function -- that is, cache trimming must be
           configured directly via the primary cache interface, since
           all memory is managed by the primary `kdu_cache'.
           [//]
           At the end of each call to `add_to_databin', the amount of
           actively used data-bin storage is tested against the
           `preferred_byte_limit'.  If the limit is exceeded, the function
           removes a convenient amount of storage from active use, but at
           least more than was added in the `add_to_databin' call.  This
           approach avoids the need to hold critical section locks for very
           long and should eventually lead to the amount of active storage
           reducing to the specified limit.  Note, however, that certain
           sets of data-bins cannot be trimmed using this function.  These
           include those marked for preservation (see `preserve_databin' and
           `preserve_class_stream') and those that belong to one of a number
           of internally locked segments that may exist, if they have been
           recently accessed for a variety of purposes.
           [//]
           It can be useful to reduce the amount of active data-bin storage
           immediately, to around the `preferred_byte_limit' specified by
           this function.  In particular, this may be useful prior to
           scanning the cache contents with `scan_databins' and storing them
           to an external file.  Rather than waiting for storage to be
           incrementally reduced via calls to `add_to_databin' that find
           the preferred_byte_limit' to be violated, you can call
           `trim_to_preferred_memory_limit', but that function may take some
           time to execute.
         [ARG: preferred_byte_limit]
           If less than or equal to 0, cache trimming is disabled.
       */
    KDU_AUX_EXPORT void trim_to_preferred_memory_limit();
      /* [SYNOPSIS]
           This function is most likely to be of interest only if you have
           changed the preferred byte limit recently, via a call to
           `set_preferred_memory_limit', revising any pre-existing byte
           limit downwards, and you want this byte limit to limit the amount of
           data discovered by a subsequence series of calls to
           `scan_databins'.  Normally, the `add_to_databin' function tries
           to work gradually towards any preferred byte limit that has been
           exceeded, but this function acquires an internal critical section
           and trims all relevant content as soon as possible, an activity
           that might potentially take some time.
      */
    KDU_AUX_EXPORT void
      preserve_databin(int databin_class, kdu_long codestream_id,
                       kdu_long databin_id);
      /* [SYNOPSIS]
           This function flags the indicated data-bin for preservation against
           cache trimming operations that might otherwise delete content to
           satisfy memory size constraints.  A data-bin can be successfully
           flagged for preservation even if it does not yet have an existence
           in the cache, although this might require some very small amount
           of memory.  Data-bins marked for preservation can be explicitly
           deleted using the `delete_databin' or `delete_stream_class'
           functions, but the preservation flags will nonetheless persist.
           [//]
           One way to use this function within a JPIP client is to set up a
           pre-defined JPIP request that might or might not actually be sent
           to a server, but whose implied data-bins are all to be preserved.
           The window-of-interest for such a request might typically cover the
           first codestream or compositing layer of a source, up to a certain
           resolution, sufficient at least to regenerate a thumbnail of the
           first image in the source.  This mechanism is implemented by
           `kdu_client', which knows exactly how to translate JPIP requests
           into data-bin ID's (a requirement for efficiently driving the
           `mark_databin' function below to update server chache models).
           [//]
           It is also possible to mark entire classes of data-bins for
           preservation, potentially limited to particular codestreams, via
           the `preserve_class_stream' function, which is much more efficient
           when you know that everything in a codestream/class should be
           preserved no matter what.
           [//]
           Like `add_to_databin' and `delete_databin' and all other member
           functions that do not clearly configure internal state that will
           be needed to interpret subsequent calls, this function is completely
           thread-safe.
      */
    KDU_AUX_EXPORT void
      preserve_class_stream(int databin_class, kdu_long codestream_id);
      /* [SYNOPSIS]
           This function allows you to flag an entire data-bin class for
           preservation, optionally limited to a particular codestream, as
           a simple and efficient alternative to issuing explicit calls to
           `preserve_databin'.  Note, however, that for each data-bin class
           that is flagged in this way, the internal machinery records only
           one `codestream_id' (possibly -ve, meaning all codestreams).
           Unlike `preserve_databin', this function has no direct memory
           implications whatsoever.  What happens is that each call to
           `add_to_databin' or `mark_databin' flags the data-bin for
           preservation if it happens to match the specifications provided
           by prior calls to this function.
           [//]
           There are two special cases worth mentioning here:
           [>>] If `databin_class' is `KDU_META_DATABIN' and `codestream_id'
                is 0 or a -ve value, all meta-databins that are added after
                this call will be flagged for preservation.  This is the
                single most valuable application of the function, since there
                is no simply way to determine what meta-databins are best
                to preserve to achieve a given rendering objective in the
                future.
           [>>] If `databin_class' is -ve, all data-bins added to any
                codestream matching the `codestream_id' value in the future
                will be flagged for presentation, with the exception of
                meta-databins; the `KDU_META_DATABIN' class is not covered by
                a wildcard -ve value for `databin_class'.
         [ARG: databin_class]
           This value should either be one of the valid data-bin classes, in
           the range 0 to `KDU_NUM_DATABIN_CLASSES'-1, or else the "wildcard"
           value -1.  The value -1 includes all data-bin classes, except for
           `KDU_META_DATABIN' class.
         [ARG: codestream_id]
           If -ve, this argument is interpreted as a "wildcard" specification
           that will match all data-bins belonging to the relevant class,
           within any codestream.
      */
    KDU_AUX_EXPORT void
      touch_databin(int databin_class, kdu_long codestream_id,
                    kdu_long databin_id);
      /* [SYNOPSIS]
           This function provides a reasonably efficient mechanism to
           manipulate the impact of automatic cache trimming operations which
           might occur as data is added to the cache with `add_to_databin',
           where there is a memory constraint in force, as introduced by
           `set_preferred_memory_limit'.  When cache trimming operations need
           to reclaim memory, they do so by reclaiming the elements that have
           been touched least recently.
           [//]
           Databins are implicitly touched by calls to `add_to_databin',
           `mark_databin', `set_read_scope' and related functions.  Touching a
           databin also generally touches quite a few other close relatives.
           Also, databins contine to be touched, in this manner until the
           relevant activity (adding, reading, marking, etc.) moves on to work
           with a different databin; until this happens, the databin that it is
           touching cannot be removed by cache trimming operations.
           [//]
           This function provides another means of touching databins, which
           serves no other purpose than to make sure that they get recognized
           as recently touched, and hence least likely to be trimmed away
           in order to meet a memory constraint.  It is more efficient than
           the other mechanisms, since it does not attempt to create slots in
           the cache to record anything and it rarely needs to lock internal
           critical sections or even perform any atomic state manipulation.
           [//]
           Note that this function is not completely thread safe, in the
           sense that two threads may not concurrently execute this function
           on the same `kdu_cache' interface.  However, two threads may
           concurrently invoke the function on different `kdu_cache' interfaces
           that share the same underlying primary cache.  This is the same
           situation that applies to reading from the cache which involves
           invocation of `set_read_scope', either directly or indirectly.
         [RETURNS]
           True if the identified databin exists.  If not, this function does
           not create any slot in the cache to hold it.
         [ARG: databin_class]
           See `add_to_databin'.
         [ARG: codestream_id]
           See `add_to_databin'.
         [ARG: databin_id]
           See `add_to_databin'.
      */
    KDU_AUX_EXPORT virtual kdu_int32
      mark_databin(int databin_class, kdu_long codestream_id,
                   kdu_long databin_id, bool mark_state,
                   int &length, bool &is_complete);
      /* [SYNOPSIS]
           Note that this function's interface has been augmented in KDU-7.6
           in a way that is designed to force compilation errors in any
           pre-existing applications that directly invoked it (probably not
           many).  The reason is that the function now plays a very much
           expanded role, since data-bins can be explicitly and implicitly
           deleted.  Importantly, the return value is now a union of flags,
           rather than a boolean, and the function always returns the current
           length of the data-bin, so as to ensure that the flags and length
           are consistent in the presence of asynchronous updates.
           [//]
           The function provides a convenient service which may be used
           by client applications or derived objects for marking data-bins
           as novel with respect to a server's cache model and for discovering
           how a data-bin's contents might differ from a server's cache model.
           [//]
           In practice, data-bins are usually marked via calls to
           `add_to_databin' (see that function's `mark_if_augmented' argument)
           or `delete_databin' (whether explicit or implicit) and the marks
           are normally discovered and simultaneously cleared by calls to this
           function (i.e., the `mark_state' argument to this function is
           usually false).
           [//]
           Regardless of the `mark_state' argument, if the data-bin identified
           by this function does not exist, it will not be created here.
           [//]
           Internally, the `kdu_cache' object maintains more than just a
           single 1-bit flag to characterize data-bins as marked or unmarked.
           Insteaad, it keeps 3 separate flags whose meaning is explained
           below, in connection with the function's return value.
           [//]
           The `length' and `is_complete' arguments return additional
           information about the current state of the data-bin, at the time
           when the marking state was sampled and modified in accordance with
           `mark_state'.  This information provides a JPIP client with
           everything it needs to generate cache model manipulation commands
           in its communication with a server. 
           [//]
           Note that this function does nothing, returning 0, if invoked
           on a `kdu_cache' object that has been attached to another (primary)
           cache via `attach_to'.
         [RETURNS]
           The function's return value is the logical OR of zero or more of
           the following flags:
           [>>] `KDU_CACHE_BIN_DELETED'.  Set if the data-bin has been deleted
                by a call to `delete_databin', since the last call to this
                function, except if `delete_databin' found the bin to be
                already marked with the `KDU_CACHE_BIN_MARKED' flag and without
                the `KDU_CACHE_BIN_AUGMENTED' flag -- see below for an
                explanation of this.  This flag is returned even if the
                data-bin has since been augmented via a call to
                `add_to_databin'.  However, the deleted flag is
                cleared by this function, as well as `clear_all_marks' and
                `set_all_marks'.
           [>>] `KDU_CACHE_BIN_AUGMENTED' is set if the data-bin was unmarked
                and non-empty at the point when it was marked via a call to
                `add_to_databin'.  This flag is retained through calls to
                `add_to_databin', but cleared by `delete_databin'.  The flag is
                always cleared by this function, as well as by
                `clear_all_marks' and `set_all_marks'.  It is not
                possible for the flag to co-exist with `KDU_CACHE_BIN_DELETED'.
           [>>] `KDU_CACHE_BIN_MARKED' -- is always present if the data-bin
                has been marked in any way, whether by an `add_to_databin' call
                that specified `mark_if_augmented'=true, by a call to
                `delete_databin', by a call to this function with
                `mark_state'=true, or by a call to `set_all_marks'.  The flag
                is cleared by this function, if `mark_state'=false and by
                `clear_all_marks'.
           [//]
           Note that the `KDU_CACHE_BIN_AUGMENTED' flag plays a significant
           role in the implementation of `delete_databin'.  In particular,
           that function is normally invoked in such a way as to leave behind
           a vestige of the data-bin, capable of preserving the
           `KDU_CACHE_BIN_DELETED' flag itself, only so long as the
           data-bin was either non-empty and not marked or marked with the
           `KDU_CACHE_BIN_AUGMENTED' flag.  The idea is that a remote server
           does not need to be informed of deletions that occur to
           data-bins that have never had any content of which the server's
           cache model could be aware.
           [//]
           If the data-bin does not exist, or has not been marked in any
           way, this function returns 0.  The function does exactly the same
           thing if the data-bin exists but has no data, even if
           `add_to_databin' has identified it as complete.  Note that deleted
           data-bins do formally exist until their deletion has been noted by
           a call to this function that returns the `KDU_CACHE_BIN_DELETED'
           flag.  As it turns out, data-bins that have no data do not get
           explicitly deleted, even if they are identified as complete, since
           they occupy no storage of their own (see `delete_databin'), so the
           function does indeed always return 0 for data-bins that had no data.
         [ARG: databin_class]
           See `add_to_databin' for an explanation of data-bin classes.
         [ARG: codestream_id]
           See `add_to_databin' for an explanation of codestream identifiers.
         [ARG: databin_id]
           See `add_to_databin' for an explanation of in-class data-bin id's.
         [ARG: mark_state]
           If true, and is non-empty, the function causes the data-bin to be
           marked, leaving it with only the `KDU_CACHE_BIN_MARKED' flag.  If
           false, the function causes all marking flags to be cleared from the
           data-bin.
         [ARG: length]
           Used to return the current length of the data-bin, as would be
           returned by `get_databin_length', without any intervening calls to
           `add_to_databin' or `delete_databin'.
         [ARG: is_complete]
           Used to return the completion-state of the data-bin, as would be
           returned by `get_databin_length' is separately invoked, without
           any intervening calls to `add_to_databin' or `delete_databin'.
      */
    KDU_AUX_EXPORT bool
      stream_class_marked(int databin_class, kdu_long codestream_id);
      /* [SYNOPSIS]
           This function can greatly increase the efficiency associated with
           the discovery of data-bin marks via the `mark_databins' function.
           In most cases, `mark_databins' is used to simultaneously recover
           any existing marks and clear all marks associated with a
           specific data-bin.  However, if it can be determined that an entire
           class of data-bins have no current marks, there is no need to
           scan through them all, invoking that function.  That is what this
           function is for.
           [//]
           There are three special cases worth mentioning here:
           [>>] If `databin_class' is `KDU_META_DATABIN' and `codestream_id'=0,
                the function determines whether there are any marks associated
                with meta-data.
           [>>] If `databin_class' is -ve, the function determines whether any
                data-bin class associated with the codestream identified by
                `codestream_id' has any marks, excluding only the
                `KDU_META_DATABIN' class.
           [>>] If `databin_class' is either `KDU_MAIN_HEADER_DATABIN' or
                `KDU_TILE_HEADER_DATABIN', the function returns true if
                either or both of these classes hold marked data-bins.  This
                is because the internal implementation actually fuses the
                main and tile header data-bins into one class (the fusion is
                usually transparent to the application, but not in this case).
                However, you should not rely upon this.
           [//]
           For the purpose of this function, a data-bin class within a
           codestream is considered to be marked if the `mark_databin' function
           would return non-zero for any data-bin belonging to that
           codestream-class.
           [//]
           Note that the function may return true even if a codestream-class
           has no current representation in the cache, if that class or
           codestream might belong to a larger collection of classes or
           codestreams that have been deleted from the cache.  Once an entire
           codestream has been truly deleted from the cache, this function
           will continue to return true when invoked with that codestream-id,
           until the `clear_all_marks' or `set_all_marks' function is called,
           both of which eliminate deletion marks.  This behaviour is
           important, since even after explicitly clearing marks for specific
           data-bins within a codestream that has been completely deleted,
           there is no simple way to determine whether other data-bins might
           have been eliminated from the cache for which deletion marks might
           be appropriate.  This is one reason why the JPIP protocol defines
           the "mset" request field, which allows it to explicitly clear the
           server's cache model for entire codestreams -- after doing this, it
           would be appropriate to invoke `clear_all_marks' on that
           codestream.
           [//]
           This function is usually invoked either with `KDU_META_DATABIN'
           as the class and a `codestream_id' of 0 to discover whether there
           are marked meta-databins, or with -1 for the class and a specific
           `codestream_id', to determine whether the codestream needs to be
           carefully modelled to determine which data-bins should be the
           subject of calls to `mark_databin'.
           [//]
           As with `mark_databin', this function does nothing (returning false)
           if invoked on a `kdu_cache' object that has been attached to another
           (primary) cache via `attach_to'.
         [RETURNS]
           True if the `mark_databin' function might return non-zero for any
           data-bin belonging to the indicated `databin_class' (or any of the
           classes associated with the wildcard value of -1) within the
           codestream indicated by `codestream_id'.  This includes
           data-bins for which internal marks (including deletion marks) are
           explicitly recorded, but also data-bins which belong to larger
           groups (perhaps whole classes or codestreams) that have been
           deleted from the cache while deletion marks existed.
         [ARG: databin_class]
           This value should either be one of the valid data-bin classes, in
           the range 0 to `KDU_NUM_DATABIN_CLASSES'-1, or else the "wildcard"
           value -1.  The value -1 includes all data-bin classes for the
           codestream identified by `codestream_id', excepting only the
           `KDU_META_DATABIN' class.
           [//]
           Also, note that the `KDU_MAIN_HEADER_DATABIN' and
           `KDU_TILE_HEADER_DATABIN' classes may be internally fused for
           efficiency reasons so that the function returns true if either of
           these classes is used as a query and either of them contains
           data-bins for which `mark_databin' would return non-zero.
         [ARG: codestream_id]
           Note that this function does not recognize negative
           `codestream_id' values as wild-cards; if `codestream_id' is -ve,
           the function will inevitably return false.
      */
    KDU_AUX_EXPORT virtual void clear_all_marks();
      /* [SYNOPSIS]
           Returns all data-bins to the unmarked state, so that `mark_databin'
           will return 0.  Note that this function does nothing if invoked
           on a `kdu_cache' object that has been attached to another (primary)
           cache via `attach_to'.
      */
    KDU_AUX_EXPORT virtual void set_all_marks();
      /* [SYNOPSIS]
           Causes all data-bins for which the cache contains some data to
           be marked, so that the next call to `mark_databin' will return
           `KDU_CACHE_BIN_MARKED'.  As explained with the `mark_databin'
           function there are actually different types of marking flags, but
           this function clears the other ones: `KDU_CACHE_BIN_DELETED' and
           `KDU_CACHE_BIN_AUGMENTED'.
           [//]
           Typically, a JPIP client calls this function when it reconnects
           to a JPIP server, since the new connection starts off with an
           empty cache model on the server side, so there is no need to signal
           deleted data-bins, but there is a need to inform the server of
           any non-empty data-bins that were available prior to the
           session connection.
           [//]
           Note that this function does nothing if invoked on a `kdu_cache'
           object that has been attached to another (primary) cache via
           `attach_to'.
      */
  public: // Cache query functions
    KDU_AUX_EXPORT virtual int
      get_databin_length(int databin_class, kdu_long codestream_id,
                         kdu_long databin_id, bool *is_complete=NULL);
      /* [SYNOPSIS]
           Returns the total number of initial bytes which have been cached
           for a particular data-bin.  See `add_to_databin' for an explanation
           of data-bin classes and identifiers.
           [//]
           This function is completely thread safe; however, it is likely to be
           less efficient than calling `set_read_scope'.  The latter function
           has obvious implications for multi-threaded access, since the
           read-scope is necessarily an internal state that cannot meaningfully
           be shared by asynchronous threads using the same `kdu_cache'
           interface.  To ensure thread safe acess to this function, a mutual
           exclusion lock must be acquired internally, whereas this can often
           be avoided by `set_read_scope'.
           [//]
           If you do need to discover data-bin information in a context
           where other threads may asynchronously be trying to read or query
           cache content via this same `kdu_cache' interface, you can safely
           call this function.  Alternatively, you can attach a separate
           `kdu_cache' interface to the primary cache and use its
           `set_read_scope' function.
         [RETURNS]
           If the data-bin has multiple contiguous ranges of bytes, with
           intervening holes, the function returns the length only of the
           contiguous range of bytes which commences at byte 0.
         [ARG: is_complete]
           If non-NULL, this argument is used to return a value indicating
           whether or not the cached representation of the indicated data-bin
           is complete.  For this, the representation must have non holes
           and the `is_final' flag must have been asserted in a previous call
           to `add_to_databin'.
      */
    KDU_AUX_EXPORT virtual bool
      scan_databins(kdu_int32 scan_flags, int &databin_class,
                    kdu_long &codestream_id, kdu_long &databin_id,
                    int &bin_length, bool &bin_complete,
                    kdu_byte *buf=NULL, int buf_len=0);
      /* [SYNOPSIS]
           Use this function to scan through the entire cache hierarchy,
           usually with a view to saving the cache contents to an external
           file.  The `scan_flags' arguments control the behaviour, as follows:
           [>>] If the `KDU_CACHE_SCAN_START' flag is present in `flags',
                the scan starts from scratch, returning the first matching
                data-bin.  If the function has never been called before and
                this flag is missing, the function will find nothing, returning
                false.
           [>>] If the `KDU_CACHE_SCAN_PRESERVED_ONLY' flag is present, the
                function visits only those data-bins that have been flagged for
                preservation, with the aid of `preserve_class_stream' or
                `preserve_databin'.  Usually, the set of preserved data-bins
                corresponds to a special limited sub-set that is useful for
                generating a summary (e.g., a thumbnail) of the source.
                Scanning just these data-bins allows file writers to store
                these high priority elements in the first part of the file.
           [>>] If the `KDU_CACHE_SCAN_PRESERVED_SKIP' flag is present, the
                function skips over those data-bins that are flagged for
                preservation.  If `KDU_CACHE_SCAN_PRESERVED_ONLY' is also
                present, the scan is terminated, returning false; subsequent
                calls return false until a call with `KDU_CACHE_SCAN_START'
                is issued.
           [>>] If the `KDU_CACHE_SCAN_ONLY_MARKED' flag is present, the
                function visits only those data-bins that are marked with
                one of the conditions returned via `mark_databin' -- i.e.,
                that function would return at least the `KDU_CACHE_BIN_MARKED'
                flag.  This type of scan can be used to examine all marked
                data-bins for the purpose of updating a server's cache model.
           [>>] If the `KDU_CACHE_SCAN_NO_ADVANCE' flag is present, the
                function returns information for the same data-bin as the
                last call, unless that data-bin has ceased to exist or no
                longer matches the conditions associated with the other flags.
                This behaviour is useful when retrieving data-bin contents
                via a non-NULL `buf' argument; if the `buf_len' is smaller
                than the data-bin's length, a subsequent call with a larger
                buffer can be used to retrieve everything.
           [>>] If the `KDU_CACHE_SCAN_FIX_CODESTREAM' flag is present, the
                function only scans data-bins whose codestream index matches
                the value found in the `codestream_id' argument on entry. If
                this flag is missing, the entry value of `codestream_id' is
                irrelevant.  If the `KDU_CACHE_SCAN_START' flag is not present,
                the function immediately returns NULL unless the last returned
                data-bin also belongs to the fixed codestream.
           [>>] If the `KDU_CACHE_SCAN_FIX_CLASS' flag is present, the
                function only scans data-bins whose class-id matches the
                value found in the `databin_class' argument on entry.  If
                the `KDU_CACHE_SCAN_START' flag is not present, the function
                immediately returns NULL unless the last returned data-bin
                belongs to the fixed class.
           [//]
           Evidently, this function involves internal state information that
           is preserved between calls.  You can have multiple `kdu_cache'
           interfaces, each of which is attached to a single primary cache
           via the `attach_to' function, in which case each such interface
           keeps its own independent scanning state that can be accessed
           asynchronously by different threads.  This function is robust to
           asynchronous cache updates that might occur via `add_to_databin',
           `delete_databin' and the like, although such calls may affect the
           returned information of course.
           [//]
           The first 5 arguments are all used to return results from the call,
           being valid so long as the function returns true.  The `buf' and
           `buf_len' arguments allow some or all of the data-bin's contents to
           be retrieved.  In particular, if `buf' is non-NULL, up to `buf_len'
           initial bytes from the data-bin are copied to `buf'.  If `buf_len'
           is smaller than the value returned via `bin_length', the function
           can be called again with a larger buffer and passing the
           `KDU_CACHE_SCAN_NO_ADVANCE' flag.
         [RETURNS]
           False if the scan has ended (or perhaps was never started), in
           which case none of the arguments are modified.
         [ARG: scan_flags]
           Explained above.  Start the scan with `KDU_CACHE_SCAN_START'.
         [ARG: databin_class]
           Used to return the class of the accessed data-bin; not modified if
           the function returns false.
         [ARG: codestream_id]
           Used to return the codestream-id of the accessed data-bin; not
           modified if the function returns false.
         [ARG: databin_id]
           Used to return the databin-id of the accessed data-bin; not
           modified if the function returns false.
         [ARG: bin_length]
           Used to return the length of the data-bin prefix that is available
           in the case; there may be additional information available for
           the data-bin, but only after intervening holes.  This information
           cannot be explicitly retrieved.  This argument is not modified if
           the function returns false.
         [ARG: bin_complete]
           Used to return an indication of whether or not the `bin_length'
           bytes represent the entire contents of the data-bin, as found
           in the original source.
         [ARG: buf]
           If non-NULL, up to `buf_len' bytes from the data-bin's `bin_length'
           byte prefix are copied to `buf'.  The actual number of copied bytes
           is the smaller of `buf_len' and `bin_length', upon return.  Nothing
           is copied if the function returns false or `bin_length' is 0.
         [ARG: buf_len]
           Max bytes that can be copied to `buf'.  Ignored if `buf' is NULL.
      */
  public: // Functions used to read from the cache
    KDU_AUX_EXPORT virtual int
      set_read_scope(int databin_class, kdu_long codestream_id,
                     kdu_long databin_id, bool *is_complete=NULL);
      /* [SYNOPSIS]
           This function must be called at least once after the object is
           created or closed, before calling `read'.  It identifies the
           particular data-bin from which reading will proceed.  Reading
           always starts from the beginning of the relevant data-bin.  Note,
           however, that the `set_tileheader_scope' and `set_precinct_scope'
           functions may both alter the read scope (they actually do so
           by calling this function).  See `add_to_databin' for an explanation
           of data-bin classes.
           [//]
           It is not safe to call this function while any calls to the
           codestream management machinery accessed via `kdu_codestream'
           are in progress, since they may try to read from the caching data
           source themselves.  For this reason, you will usually want to
           invoke this function from the same thread of execution as that
           used to access the code-stream and perform decompression tasks.
           Use of this function from within a `jp2_input_box' or `jp2_source'
           object is safe, even in multi-threaded applications (so long as
           the synchronization members of `kdu_family_src' are implemented
           by the application).
           [//]
           If the present object has been attached (see `attach_to') to
           another `kdu_cache' object, the present function copies the current
           cache header information for the relevant databin into the present
           object; this header information keeps track of the number of
           available bytes and their location within the cache.  This low
           cost copy operation, allows subsequent reads to proceed
           asynchronously with cache updates, without taking out any mutual
           exclusion locks.  In fact, you can have many `kdu_cache' objects
           attached to a single source cache, each of which has a different
           read scope and can be read independently without any performance
           penalties.  While this is a very valuable attribute, there may
           be synchronization implications, as follows:
           [>>] Calls to `get_databin_length' and `get_databin_prefix' access
                the original source `kdu_cache' object, rather than the
                information copied by a call to `set_read_scope'.  As a
                result, if a databin has been augmented since it
                was last set as the target of an attached `kdu_cache'
                object's `set_read_scope' call, the update will be reflected
                in calls to `get_databin_length' and `get_databin_prefix',
                but not in calls to `read'.  So, for example, the
                `get_databin_length' call might suggest that the databin is
                complete, even though its entire contents cannot be read
                by `read'.
           [>>] To avoid problems that might be caused by the above situation,
                for databins that you access via `set_read_scope' and `read'
                you will do best to use the databin length returned by this
                function and the completeness information that can be
                obtained by passing a non-NULL `is_complete' argument to
                this function.
           [>>] To update the bin length and completeness information
                obtained as above, you can always invoke `set_read_scope'
                again, even if the scope has not actually changed, in order
                to copy the most up-to-date version of a databin's cached
                status to to an attached `kdu_cache' object.
           [//]
           Before passing a `kdu_cache' object directly to
           `kdu_codestream::create', you should first use the present function
           to set the read-scope to `databin_class'=`KDU_MAIN_HEADER_DATABIN'
           and `databin_id'=0.
         [RETURNS]
           The length of the databin -- same value as that returned by
           `get_databin_length'.  Returns 0 if the function's arguments do
           not identify an available databin, but may also return 0 if the
           data-bin is available but known to be empty in its original form.
         [ARG: is_complete]
           If non-NULL, this argument allows you to recover the completeness
           of the databin -- same value returned by the argument of the same
           name in `get_databin_length'.  A data-bin can be both complete and
           empty, so that the function also returns 0.
      */
    KDU_AUX_EXPORT virtual bool set_tileheader_scope(int tnum, int num_tiles);
      /* [SYNOPSIS]
           See `kdu_compressed_source::set_tileheader_scope' for an
           explanation.  Note that this function essentially just calls
           `set_read_scope', passing in `databin_class'=`KDU_HEADER_DATABIN'
           and `databin_id'=`tnum', while using the same code-stream
           identifier which was involved in the most recent call to
           `set_read_scope'.  The function always returns true.
      */
    KDU_AUX_EXPORT virtual bool set_precinct_scope(kdu_long unique_id);
      /* [SYNOPSIS]
           See `kdu_compressed_source::set_precinct_scope' for an
           explanation.  Note that this function essentially just calls
           `set_read_scope', passing in `databin_class'=`KDU_PRECINCT_DATABIN'
           and `databin_id'=`unique_id', while using the same code-stream
           identifier which was involved in the most recent call to
           `set_read_scope'.  The present function always returns true.
      */
    KDU_AUX_EXPORT virtual int read(kdu_byte *buf, int num_bytes);
      /* [SYNOPSIS]
           This function implements the functionality required of
           `kdu_compressed_source::read'.  It reads data sequentially from
           the data-bin indicated in the most recent call to
           `set_read_scope' -- note that `set_read_scope' may have been
           invoked indirectly from `set_tileheader_scope' or
           `set_precinct_sope'.  If `set_read_scope' has not yet been called,
           since the object was created or since the last call to `close',
           this function returns 0 immediately.
         [RETURNS]
           The number of bytes actually read and written into the supplied
           `buf' buffer.  This value will never exceed `num_bytes', but may
           be less than `num_bytes' if the end of the data-bin is encountered.
           After the end of the data-bin has been reached, subsequent calls
           to `read' will invariably return 0.
      */
  public: // Other base function overrides
    virtual int get_capabilities()
      { return KDU_SOURCE_CAP_CACHED | KDU_SOURCE_CAP_SEEKABLE; }
      /* [SYNOPSIS]
           This object offers the `KDU_SOURCE_CAP_CACHED' and
           `KDU_SOURCE_CAP_SEEKABLE' capabilities.  It does not support
           sequential reading of the code-stream.  See
           `kdu_compressed_source::get_capabilities' for definitions of
           the various capabilities which compressed data sources may
           advertise.
      */
    KDU_AUX_EXPORT virtual bool seek(kdu_long offset);
      /* [SYNOPSIS]
           Implements `kdu_compressed_source::seek'.  As explained there,
           seeking within a cached source moves the read pointer relative
           to the context of the current data-bin only.  An `offset' of 0
           refers to the start of the current data-bin, which may be the
           code-stream main header, a tile header, or a precinct.
      */
    KDU_AUX_EXPORT virtual kdu_long get_pos();
      /* [SYNOPSIS]
           Implements `kdu_compressed_source::get_pos', returning the location
           of the read pointer, relative to the start of the current context
           (current data-bin).  See `seek' for more information.
      */
  public: // Statistics reporting functions
    KDU_AUX_EXPORT kdu_long get_max_codestream_id();
      /* [SYNOPSIS]
           Returns the maximum codesteam-id value associated with any
           data-bin written to the cache (or a cache to which we have been
           attached via the `attach_to' function).  If no data-bin content
           has yet been written to the cache (or the one to which we are
           attached), the function returns -1.
      */
    KDU_AUX_EXPORT kdu_long get_peak_cache_memory();
      /* [SYNOPSIS]
           Returns the total amount of memory used to cache compressed data.
           This includes the cached bytes, as well as the additional state
           information required to maintain the state of the cache.  It
           includes the storage used to cache JPEG2000 packet data as well as
           headers.  If we are attached to a different `kdu_cache' via
           `attach_to', this function returns information for the cache to
           which we are attached.
      */
    KDU_AUX_EXPORT kdu_int64
      get_reclaimed_memory(kdu_int64 &peak_allocation,
                           kdu_int64 &preferred_limit);
      /* [SYNOPSIS]
           This function is provided to help track and manage the consequences
           of automatic cache trimming operations that may occur in response
           to a preferred size limit supplied via `set_preferred_memory_limit'.
           The function's two arguments return the maximum amount of memory
           allocated to date (same as `get_peak_cache_memory') and the value of
           any preferred byte limit (0 if there is no limit in force), while
           its return value represents the total amount of data reclaimed
           from the cache so far, in response to any preferred limit (including
           any limit that might have been overridden by a more recent call to
           `set_preferred_memory_limit').
           [//]
           One application for this function is that it allows a JPIP client
           to estimate whether it is likely that a request it is about to
           issue is vulnerable to having some of the existing data that is
           relevant to the request trimmed away while new data arrives.  This
           type of vulnerability can be avoided (at some cost) by determining
           which data-bins are relevant to the request and explicitly
           "touching" each of them, so that they are least likely to be
           trimmed away.  For example, a client might deduce that this
           vulnerability exists if `preferred_limit' is positive and
           the `peak_allocation' is getting close to the `preferred_limit'.
           Data-bins are "touched" when they are the subject of any of
           the data-bin specific functions provided by this object, such as
           `set_read_scope', `add_to_databin', `mark_databin' or just
           `touch_databin'.
         [RETURNS]
           Total number of internal cache buffers reclaimed to meet a
           preferred byte limit, multiplied by the size of these buffers.
           This value is generally larger than the amount of valid data
           deleted from the cache through the automatic cache trimming
           procedure, since the internal buffers might only be partially
           occupied at the point when they are deleted.
         [ARG: peak_allocation]
           Returns the same value as `get_peak_cache_memory', but using a
           type that is guaranteed to always be large enough to record the
           true value, rather than potentially clipping it.
         [ARG: preferred_limit]
           Returns the value last passed to `set_preferred_memory_limit', or 0
           if no limit has ever been set.  0 means that there is no current
           limit in force, so cache trimming is disabled at present.
      */
    KDU_AUX_EXPORT kdu_int64
      get_transferred_bytes(int databin_class);
      /* [SYNOPSIS]
           This function returns the total number of bytes which have been
           transferred into the object within the indicated data-bin class.
           See `add_to_databin' for an explanation of data-bin classes.
           If we are attached to a different `kdu_cache' via
           `attach_to', this function returns information for the cache to
           which we are attached.
      */
  private: // Data
    kd_supp_local::kd_cache *state; // Hides the object's real storage.
  };
  
} // namespace kdu_supp

#endif // KDU_CACHE_H
