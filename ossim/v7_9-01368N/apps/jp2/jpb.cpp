/*****************************************************************************/
// File: jpb.cpp [scope = APPS/JP2]
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
/*****************************************************************************
Description:
   Implements the internal machinery whose external interfaces are defined
in the compressed-io header file, "jpb.h".
******************************************************************************/

#include <assert.h>
#include "kdu_messaging.h"
#include "jpb.h"
using namespace kdu_supp;

/* Note Carefully:
      If you want to be able to use the "kdu_text_extractor" tool to
   extract text from calls to `kdu_error' and `kdu_warning' so that it
   can be separately registered (possibly in a variety of different
   languages), you should carefully preserve the form of the definitions
   below, starting from #ifdef KDU_CUSTOM_TEXT and extending to the
   definitions of KDU_WARNING_DEV and KDU_ERROR_DEV.  All of these
   definitions are expected by the current, reasonably inflexible
   implementation of "kdu_text_extractor".
      The only things you should change when these definitions are ported to
   different source files are the strings found inside the `kdu_error'
   and `kdu_warning' constructors.  These strings may be arbitrarily
   defined, as far as "kdu_text_extractor" is concerned, except that they
   must not occupy more than one line of text.
*/
#ifdef KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("E(jpb.cpp)",_id);
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("W(jpb.cpp)",_id);
#  define KDU_TXT(_string) "<#>" // Special replacement pattern
#else // !KDU_CUSTOM_TEXT
#  define KDU_ERROR(_name,_id) \
     kdu_error _name("Error in Kakadu File Format Support:\n");
#  define KDU_WARNING(_name,_id) \
     kdu_warning _name("Warning in Kakadu File Format Support:\n");
#  define KDU_TXT(_string) _string
#endif // !KDU_CUSTOM_TEXT

#define KDU_ERROR_DEV(_name,_id) KDU_ERROR(_name,_id)
 // Use the above version for errors which are of interest only to developers
#define KDU_WARNING_DEV(_name,_id) KDU_WARNING(_name,_id)
 // Use the above version for warnings which are of interest only to developers

/* ========================================================================= */
/*                            External Functions                             */
/* ========================================================================= */

static bool jb_textualizer_frat(jp2_input_box *, kdu_message &, bool, int);
static bool jb_textualizer_brat(jp2_input_box *, kdu_message &, bool, int);
static bool jb_textualizer_fiel(jp2_input_box *, kdu_message &, bool, int);
static bool jb_textualizer_tcod(jp2_input_box *, kdu_message &, bool, int);
static bool jb_textualizer_bcol(jp2_input_box *, kdu_message &, bool, int);
static bool jb_textualizer_dmon(jp2_input_box *, kdu_message &, bool, int);

/*****************************************************************************/
/* EXTERN                   jpb_add_box_descriptions                         */
/*****************************************************************************/
namespace kdu_supp {

void
  jpb_add_box_descriptions(jp2_box_textualizer &textualizer)
{
  textualizer.add_box_type(jpb_frame_rate_4cc,NULL,jb_textualizer_frat);
  textualizer.add_box_type(jpb_max_bitrate_4cc,NULL,jb_textualizer_brat);
  textualizer.add_box_type(jpb_field_coding_4cc,NULL,jb_textualizer_fiel);
  textualizer.add_box_type(jpb_time_code_4cc,NULL,jb_textualizer_tcod);
  textualizer.add_box_type(jpb_colour_4cc,NULL,jb_textualizer_bcol);
  textualizer.add_box_type(jpb_mastering_display_4cc,NULL,jb_textualizer_dmon);
}

} // namespace kdu_supp


/* ========================================================================= */
/*                            Internal Functions                             */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                    remove_common_factors                           */
/*****************************************************************************/

static void
  remove_common_factors(kdu_uint16 *val1, kdu_uint16 *val2)
{
  kdu_uint16 v1=*val1, v2=*val2, divisor;
  if ((v1 == 0) || (v2 == 0))
    return;
  if ((v2 % v1) == 0)
    { v2 /= v1;  v1 = 1; }
  if ((v1 % v2) == 0)
    { v1 /= v2;  v2 = 1; }
  for (divisor=2; divisor < v1; divisor++)
    { 
      while (((v1 % divisor) == 0) && ((v2 % divisor) == 0))
        { v1 /= divisor;  v2 /= divisor; }
    }
  *val1 = v1;  *val2 = v2;
}

/*****************************************************************************/
/* STATIC                          from_bcd                                  */
/*****************************************************************************/

static bool
from_bcd(kdu_uint32 timecode, int &hh, int &mm, int &ss, int &ff)
/* Extracts the four BCD fields from `timecode'. */
{
  int h0, h1, m0, m1, s0, s1, f0, f1;
  h0 = (int)((timecode >> 24) & 0x0F);
  h1 = (int)((timecode >> 28) & 0x0F);
  m0 = (int)((timecode >> 16) & 0x0F);
  m1 = (int)((timecode >> 20) & 0x0F);
  s0 = (int)((timecode >> 8 ) & 0x0F);
  s1 = (int)((timecode >> 12) & 0x0F);
  f0 = (int)((timecode >> 0) & 0x0F);
  f1 = (int)((timecode >> 4) & 0x0F);
  hh = h0 + 10*h1;
  mm = m0 + 10*m1;
  ss = s0 + 10*s1;
  ff = f0 + 10*f1;
  return ((h0 < 10) && (h1 < 10) && (m0 < 10) && (m1 < 10) &&
          (s0 < 10) && (s1 < 10) && (f0 < 10) && (f1 < 10));
}

/*****************************************************************************/
/* STATIC                           to_bcd                                   */
/*****************************************************************************/

static bool
to_bcd(kdu_uint32 &timecode, int hh, int mm, int ss, int ff)
/* Extracts the four BCD fields from `timecode'. */
{
  bool valid = ((hh >= 0) && (hh < 100) && (mm >= 0) && (mm < 100) &&
                (ss >= 0) && (ss < 100) && (ff >= 0) && (ff < 100));
  hh = (hh % 10) + ((hh / 10) << 4);
  mm = (mm % 10) + ((mm / 10) << 4);
  ss = (ss % 10) + ((ss / 10) << 4);
  ff = (ff % 10) + ((ff / 10) << 4);
  timecode = hh;
  timecode = (timecode << 8) + mm;
  timecode = (timecode << 8) + ss;
  timecode = (timecode << 8) + ff;
  return valid;
}


/* ========================================================================= */
/*                       Internal Textualizer Functions                      */
/* ========================================================================= */

/*****************************************************************************/
/* STATIC                     jb_textualizer_frat                            */
/*****************************************************************************/

static bool
  jb_textualizer_frat(jp2_input_box *box, kdu_message &msg,
                       bool xml_embedded, int max_len)
{
  kdu_uint16 frat_duration=0, frat_timescale=0;
  if (!(box->read(frat_duration) && box->read(frat_timescale)))
    return false;
  msg << "<frames_per_second> " << ((int) frat_timescale)
      << "/" << ((int) frat_duration) << " </frames_per_second>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jb_textualizer_brat                            */
/*****************************************************************************/

static bool
  jb_textualizer_brat(jp2_input_box *box, kdu_message &msg,
                       bool xml_embedded, int max_len)
{
  kdu_uint32 brat_maxbr=0, brat_size0=0, brat_size1=0;
  if (!(box->read(brat_maxbr) && box->read(brat_size0) &&
        box->read(brat_size1)))
    return false;
  msg << "<max_bits_per_second> " << brat_maxbr << " </max_bits_per_second>\n";
  msg << "<field_bytes> " << brat_size0 << " "
      << brat_size1 << " </field_bytes>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jb_textualizer_fiel                            */
/*****************************************************************************/

static bool
  jb_textualizer_fiel(jp2_input_box *box, kdu_message &msg,
                       bool xml_embedded, int max_len)
{ 
  kdu_byte fiel_num=0, fiel_order=0;
  if (!(box->read(fiel_num) && box->read(fiel_order)))
    return false;
  const char *order_string = "unrecognized";
  if (fiel_order == 0)
    order_string = "UNKNOWN";
  else if (fiel_order == 1)
    order_string = "TOP-FIRST";
  else if (fiel_order == 6)
    order_string = "TOP-SECOND";
  msg << "<fields_per_frame> " << ((int) fiel_num) << " </fields_per_frame>\n";
  msg << "<field_order> \"" << order_string << "\" "
      << ((int) fiel_order) << " </field_order>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jb_textualizer_tcod                            */
/*****************************************************************************/

static bool
  jb_textualizer_tcod(jp2_input_box *box, kdu_message &msg,
                       bool xml_embedded, int max_len)
{
  kdu_uint32 timecode;
  if (!box->read(timecode))
    return false;
  char string[10];
  sprintf(string,"%08X",timecode);
  msg << "<timecode> " << string << " </timecode>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jb_textualizer_bcol                            */
/*****************************************************************************/

static bool
  jb_textualizer_bcol(jp2_input_box *box, kdu_message &msg,
                       bool xml_embedded, int max_len)
{
  kdu_byte colour;
  if (!box->read(colour))
    return false;
  const char *string;
  switch (colour) {
    case JPB_UNKNOWN_SPACE: string = "UNKNOWN"; break;
    case JPB_SRGB_SPACE: string = "sRGB"; break;
    case JPB_601_SPACE: string = "CCIR-601"; break;
    case JPB_709_SPACE: string = "CCIR-709"; break;
    case JPB_LUV_SPACE: string = "LUV"; break;
    case JPB_XYZ_SPACE: string = "XYZ"; break;
    case JPB_BT2020_SPACE: string = "BT.2020"; break;
    case JPB_ST2084_SPACE: string = "SMPTE-2084_BT.2020white"; break;
    default: string = "unrecognized"; break;
  }
  msg << "<colour> \"" << string << "\" " << ((int) colour) << " </colour>\n";
  return true;
}

/*****************************************************************************/
/* STATIC                     jb_textualizer_dmon                            */
/*****************************************************************************/

static bool
  jb_textualizer_dmon(jp2_input_box *box, kdu_message &msg,
                      bool xml_embedded, int max_len)
{
  float chroma[8]={0,0,0,0,0,0,0,0};
  double lum[2]={0,0};
  int n;
  for (n=0; n < 8; n++)
    { 
      kdu_uint16 val16;
      if (!box->read(val16))
        return false;
      chroma[n] = 0.00002f * (float) val16;
    }
  for (n=0; n < 2; n++)
    { 
      kdu_uint32 val32;
      if (!box->read(val32))
        return false;
      lum[n] = 0.0001 * (double) val32;
    }
  msg << "<mastering-display>\n";
  msg << "    <X0> " << chroma[0] << " </X0> <Y0> " << chroma[1] << " </Y0>\n";
  msg << "    <X1> " << chroma[2] << " </X1> <Y1> " << chroma[3] << " </Y1>\n";
  msg << "    <X2> " << chroma[4] << " </X2> <Y2> " << chroma[5] << " </Y2>\n";
  msg << "    <Xw> " << chroma[6] << " </Xw> <Yw> " << chroma[7] << " </Yw>\n";
  msg << "    <Lmin> " <<lum[0]<< " </Lmin> <Lmax> " <<lum[1]<< " </Lmax>\n";
  msg << "</mastering-display>\n";
  return true;
}


/* ========================================================================= */
/*                             Local Definitions                             */
/* ========================================================================= */
namespace kd_supp_local {
  
/*****************************************************************************/
/*                                 jb_target                                 */
/*****************************************************************************/

struct jb_target {
  public: // Member functions
    jb_target()
      { memset(this,0,sizeof(*this)); rewrite_pos=-1; last_tc=0xFFFFFFFF; }
    ~jb_target()
      { if (fields[0] != NULL) delete[] fields[0];
        if (fields[1] != NULL) delete[] fields[1]; }
    kdu_uint32 get_timecode()
      { 
        int ss = tc_seconds;  int hh = ss / 3600;  ss -= hh*3600;
        int mm = ss / 60;    ss -= mm*60;
        int ff = tc_frame_counter / tc_group_frames;
        kdu_uint32 result;
        if (!to_bcd(result,hh,mm,ss,ff))
          assert(0); // Should never happen
        return result;
      }
  public: // Constant parameters
    jp2_family_tgt *tgt;
    int timescale;
    int frame_duration;
    kdu_field_order field_order;
    int num_fields; // Number of fields per frame
    kdu_byte colour_space;
    int max_bitrate;
    kdu_long max_codestream_length; // Max bytes in any field's codestream
  public: // Mastering display parameters
    bool write_dmon; // True if parameters to write are known
    kdu_uint16 dmon_chromaticities[8];
    kdu_uint32 dmon_luminances[2];
  public: // Timecode related parameters
    int tc_seconds; // Non-FF part of timecode for frame being generated
    int tc_group_frames; // Num consecutive frames with same timecode
    int tc_frame_counter; // FF part of timecode = frame_counter/group_frames
    int tc_remainder; // See below
    int nominal_fps; // Frames for each timecode second, ignoring drop frames
    int drop_interval; // Interval T, in seconds, between possible drop frames
    int drop_count; // Number of frame counts to drop at once
    kdu_uint32 last_tc; // Timecode of last written frame, or 0xFFFFFFFF
    bool tc_adjusted; // Becomes true after the first adjustment warning
  public: // Field storage
    kdu_byte *fields[2]; // Two arrays of length `max_codestream_length' each
    int field_lengths[2]; // Actual codestream lengths
    int field_idx; // For current open image or next open image is none open
    int rewrite_pos; // -ve if not in rewrite
    bool image_open; // True if an image is open
  };
  /* Notes:
       When generating time-codes, the `tc_remainder' member holds the
     value S - tc_seconds*timescale, where S is the start-time for the
     frame being generated, measured in `timescale' ticks.  Each time
     a frame is generated, S is increased by `frame_duration' and
     `tc_frame_counter' is incremented.  When `tc_frame_counter' reaches
     `nominal_fps', `tc_frame_counter' is reset to 0, `tc_seconds' is
     incremented, and S is decreased by `timescale'.  When `tc_seconds'
     becomes a multiple of `drop_interval', if `tc_remainder' > 0, the
     value of `tc_frame_counter' is set to `drop_count' (was previously 0).
     Whenever `tc_seconds' becomes a multiple of 3600 (one hour), the
     `tc_frame_counter' is reset to 0.
  */

/*****************************************************************************/
/*                              jb_frame_block                               */
/*****************************************************************************/

#define JB_BLOCK_FRAMES 1024
struct jb_frame_block {
    jb_frame_block() { memset(this,0,sizeof(*this)); }
    int first_frame_idx; // Index of first frame in block
    int block_frames; // Number of frames in block
    kdu_long locations[JB_BLOCK_FRAMES];
    kdu_long instants[JB_BLOCK_FRAMES]; // Frame start instants
    jb_frame_block *next;
  };

/*****************************************************************************/
/*                                 jb_source                                 */
/*****************************************************************************/

struct jb_source {
  public: // Member functions
    jb_source()
      { 
        src=NULL; jp2c_capabilities=0;
        field_mode=2; num_frames=-1;
        
        last_frame_idx=-1; frame_blocks = NULL; last_block = NULL;
        last_elsm_pos=-1; next_elsm_pos=0; last_timescale=0;
        last_period=0; last_instant=0; backtrack_block=NULL;
        
        open_frame_idx=-1; open_timescale=0; open_period=0; open_instant=0;
        open_timecode=0; open_field_order=KDU_FIELDS_NONE; open_num_fields=0;
        open_colour_space=0; have_dmon=false;
        first_field_contents_length=0; open_end_pos=0;
        
        field_idx=0;
      }
    ~jb_source()
      { 
        while ((last_block=frame_blocks) != NULL)
          { frame_blocks=last_block->next; delete last_block; }
      }
    kdu_long parse_frame(kdu_long pos, int &timescale, int &duration,
                         kdu_byte &colour, kdu_field_order &fields,
                         kdu_uint32 &timecode, kdu_long &field_box_location,
                         kdu_long &field_location, kdu_long &field_length);
      /* This work-horse function parses an ELSM box at location `pos' and
         returns the important metadata for the frame via the various
         function arguments, along with the location and length of the first
         contiguous codestream box and its contents.  If the function
         fails to open an ELSM box, it returns 0; however, if an error
         is encountered in the contents of the ELSM box, an error message
         is generated through `kdu_error' and the function does not return.
         If successful, the function returns the location of the start of the
         next ELSM box, if there is one. */
    bool open_frame(int idx);
      /* This function attempts to open the frame whose frame index is supplied
         as the `idx' argument.  This may require many frames to be parsed.
         Alternatively, it may happen that the required frame is already
         open, in which case the function returns true immediately.  If the
         frame cannot be opened, it must lie beyond the end of the video
         sequence, in which case the function returns false. */
  public: // Global information
    jp2_family_src *src;
    int jp2c_capabilities; // Obtained from first codestream box opened
    int field_mode;
    int num_frames; // -ve until we try to go past the last frame
  private: // State information used for keeping track of parsed frames
    int last_frame_idx; // Index of latest frame to be discovered
    jb_frame_block *frame_blocks; // List records info for all parsed frames
    jb_frame_block *last_block; // Block containing last frame to be discovered
    kdu_long last_elsm_pos; // Location of ELSM box for last frame discovered
    kdu_long next_elsm_pos; // Location of next ELSM box to open
    int last_timescale;    // These members keep track of the timescale,
    int last_period;       // frame period and starting instant of the last
    kdu_long last_instant; // parsed frame with a non-zero timescale.
    jb_frame_block *backtrack_block; // Keeps track of blocks visited by
                     // `open_frame' when backtracking -- save list scanning.
  public: // State information for currently open frame; note that there
          // should usually be an open frame at all times.  When the last
          // image of a frame is closed, the next frame is opened automatically
          // so that its metadata is available to be queried.
    int open_frame_idx; // -ve if no frame is open and once last frame closed
    int open_timescale;
    int open_period; // 0 if no frame-rate information is available
    kdu_long open_instant;
    kdu_uint32 open_timecode;
    kdu_field_order open_field_order;
    int open_num_fields;
    kdu_byte open_colour_space;
    bool have_dmon; // True if a dmon box was parsed
    kdu_uint16 dmon_chromaticities[8];
    kdu_uint32 dmon_luminances[2];
    jp2_locator first_field_box_location;
    jp2_locator first_field_contents_location;
    kdu_long first_field_contents_length;
    kdu_long open_end_pos; // Points to location immediately beyond ELSM box
  public: // State information for currently open field
    int field_idx; // For current open image or next open image if none open
    jp2_input_box image_box; // Used for fields opened with `open_image'.
  };
  /* Note:
       For frames with two fields, the location of the second field is not
     explicitly stored here.  Instead, to locate the second field one must
     open the box located at `first_field_contents_location' +
     `first_field_contents_length'; if this box is not a contiguous codestream
     box, one must skip over boxes until one comes to the contiguous
     codestream box -- it is possible that custom boxes have been interleaved,
     although this would be rather silly.  The reason for storing the
     information this way is to avoid unnecessary file seeking, in the common
     case where frames are opened and read sequentially. */

} // namespace kd_supp_local

using namespace kd_supp_local;
  
/* ========================================================================= */
/*                                 jpb_target                                */
/* ========================================================================= */

/*****************************************************************************/
/*                              jpb_target::open                             */
/*****************************************************************************/

void
  jpb_target::open(jp2_family_tgt *tgt, kdu_uint16 timescale,
                   kdu_uint16 frame_duration, kdu_field_order field_order,
                   kdu_byte frame_space, kdu_uint32 max_bitrate,
                   kdu_uint32 initial_timecode, int timecode_flags)
{
  if (state != NULL)
    close();
  remove_common_factors(&timescale,&frame_duration);
  if ((max_bitrate <= 0) || (timescale == 0) || (frame_duration == 0))
    { KDU_ERROR_DEV(e,1); e <<
      KDU_TXT("`jpb_target::open' requires strictly positive values for the "
              "`timescale', `frame_duration' and `max_bitrate' arguments.");
    }
  if (frame_space > 5)
    { KDU_ERROR_DEV(e,3); e <<
      KDU_TXT("Unrecognized frame colour space passed to `jpb_target'.  "
              "Valid colour space identifiers for elementary broadcast "
              "streams must be 1-byte integers in the range 0 to 5.");
    }
  state = new jb_target;
  state->tgt = tgt;
  state->timescale = timescale;
  state->frame_duration = frame_duration;
  state->field_order = field_order;
  state->num_fields = (field_order == KDU_FIELDS_NONE)?1:2;
  state->colour_space = frame_space;
  state->max_bitrate = max_bitrate;
  state->max_codestream_length =
    ((((kdu_long) max_bitrate) * state->frame_duration) /
     (state->timescale * state->num_fields * 8));
  
  // Configure mastering display parameters
  state->write_dmon = false;
  
  // Configure timecode generation parameters
  state->tc_seconds = 0;
  state->tc_frame_counter = 0;
  state->tc_remainder = 0;
  state->tc_group_frames = 1;
  if (timecode_flags & JPB_TIMEFLAG_FRAME_PAIRS)
    state->tc_group_frames = 2;
  state->drop_count = 1;
  if (timecode_flags & JPB_TIMEFLAG_NDF)
    state->drop_count = 0;
  else if (timecode_flags & JPB_TIMEFLAG_DF4)
    state->drop_count = 4;
  else if (timecode_flags & JPB_TIMEFLAG_DF2)
    state->drop_count = 2;
  state->drop_count *= state->tc_group_frames;

  state->nominal_fps =
    (state->timescale+state->frame_duration-1) / state->frame_duration;
  state->nominal_fps = state->tc_group_frames *
    (1 + ((state->nominal_fps-1)/state->tc_group_frames));
  if (state->nominal_fps > (100*state->tc_group_frames))
    { KDU_ERROR(e,4); e <<
      KDU_TXT("Attempting to initialize `jpb_target' with parameters "
              "that require more than 100 distinct FF values to be used "
              "when generating timecodes.  Legal timecodes are a "
              "required feature of any elementary broadcast stream.");
    }
  
  if (state->drop_count > state->nominal_fps)
    state->drop_count = state->nominal_fps;
  state->drop_interval = 0; // If frame rate is equal to `nominal_fps'
  if ((state->nominal_fps * state->frame_duration) == state->nominal_fps)
    state->drop_count = 0; // No frame dropping
  if (state->drop_count > 0)
    { // Need to initialize frame dropping machinery
      state->drop_interval = 1;
      kdu_long overshoot = ((state->nominal_fps*10*state->frame_duration) -
                            (state->timescale * 10));
      assert(overshoot >= 0);
      if (overshoot <= (state->frame_duration*state->drop_count))
        { // We can limit frame dropping to 10 second intervals at least
          state->drop_interval = 10;
          overshoot = ((state->nominal_fps*60*state->frame_duration) -
                       (state->timescale * 60));
          assert(overshoot >= 0);
          if (overshoot <= (state->frame_duration*state->drop_count))
            { // We can limit frame dropping to 1 minute intervals at least
              state->drop_interval = 60;
              overshoot = ((state->nominal_fps*600*state->frame_duration) -
                           (state->timescale * 600));
              assert(overshoot >= 0);
              if (overshoot <= (state->frame_duration*state->drop_count))
                state->drop_interval = 600; // Frame drop interval is 10 mins
            }
        }
    }
  
  // Before finishing up, convert the initial timecode into an absolute
  // frame start time, measured in ticks of the timescale, verifying that
  // `initial_timecode' is actually valid.
  int hh=0, mm=0, ss=0, ff=0;
  if ((!from_bcd(initial_timecode,hh,mm,ss,ff)) ||
      (hh >= 24) || (mm >= 60) || (ss >= 60) ||
      ((ff*state->tc_group_frames) >= state->nominal_fps))
    { KDU_ERROR_DEV(e,5); e <<
      KDU_TXT("Illegal initial timecode passed to `jpb_target'.  Each "
              "byte of a valid timecode must hold 2 decimal digits in BCD "
              "format, with the fields satisfying HH<24, MM<60 and SS<60 "
              "and FF < ceil(frame-rate).");
    }
  state->tc_adjusted = false;
  this->set_next_timecode(initial_timecode);

  // Create storage for the field codestreams
  for (int f=0; f < state->num_fields; f++)
    state->fields[f] = new kdu_byte[(int) state->max_codestream_length];
}

/*****************************************************************************/
/*                      jpb_target::set_next_timecode                        */
/*****************************************************************************/

void jpb_target::set_next_timecode(kdu_uint32 timecode)
{
  int hh=0, mm=0, ss=0, ff=0;
  if ((!from_bcd(timecode,hh,mm,ss,ff)) ||
      (hh >= 24) || (mm >= 60) || (ss >= 60) ||
      ((ff*state->tc_group_frames) >= state->nominal_fps))
    { KDU_ERROR_DEV(e,0x04051201); e <<
      KDU_TXT("Illegal timecode passed to `jpb_target::set_timecode'.  Each "
              "byte of a valid timecode must hold 2 decimal digits in BCD "
              "format, with the fields satisfying HH<24, MM<60, SS<60 and "
              "FF < ceil(frame-rate).");
    }
  ff *= state->tc_group_frames;
  
  state->tc_seconds = mm*60 + ss; // Add in the hours afterwards 
  state->tc_remainder = 0;
  int initial_ff = 0; // FF value for first frame at time HH:MM:SS
  if (state->drop_interval != 0)
    { // True frame-rate is not equal to nominal_fps and dropframe timecode
      // generation is in use.  Need to figure out the `tc_remainder' value
      // corresponding to `tc_seconds'.
      int num_drop_intervals = (state->tc_seconds-1) / state->drop_interval;
        // Counts the number of drop intervals occurring prior to the start
        // of the point at which all `state->tc_seconds' seconds ellapsed.
      int drop_span = num_drop_intervals * state->drop_interval; // in seconds
      kdu_long per_second_remainder = // Nominal growth in remainder w/o drops
        state->nominal_fps * ((kdu_long) state->frame_duration) -
        state->timescale;
      kdu_long drop_remainder = per_second_remainder * drop_span;
        // This is the remainder that would be observed after `drop_span'
        // seconds in the absence of any drop frames.
      kdu_long drop_impact = state->drop_count*state->frame_duration;
        // This is the impact of a drop on the remainder that is observed
        // at the end of the second in which the drop occurs.
      int ref_seconds = drop_span + 1;
      int num_drops = (int)((drop_remainder+drop_impact-1) / drop_impact);
        // This number of drops occurs within in the first `ref_seconds'.
      kdu_long ref_remainder = // The remainder we get after `ref_seconds'
      drop_remainder+per_second_remainder - num_drops * drop_impact;
      state->tc_remainder = (int)
        (ref_remainder + per_second_remainder*(state->tc_seconds-ref_seconds));
        // This is the remainder after the completion of `state->tc_seconds'
      if (((state->tc_seconds % state->drop_interval) == 0) &&
          (state->tc_remainder > 0))
        { // The current second begins with a drop
          initial_ff = state->drop_count;
          if (ff < initial_ff)
            { 
              if (!state->tc_adjusted)
                { KDU_WARNING_DEV(w,30); w <<
                  KDU_TXT("Application-supplied timecode required adjustment "
                          "to conform to the drop-frame timecode generation "
                          "rules set up by `jpb_target::open'.  To avoid "
                          "this, specify the `JPB_TIMEFLAG_NDF' flag to "
                          "disable the drop-frame timecode generation "
                          "algorithm.");
                }
              ff = initial_ff;
              state->tc_adjusted = true;
            }
        }
    }

  state->tc_frame_counter = ff;
  state->tc_remainder += (ff-initial_ff)*state->frame_duration;
  state->tc_seconds += hh*3600;
}

/*****************************************************************************/
/*                 jpb_target::set_mastering_display_info                    */
/*****************************************************************************/

void
  jpb_target::set_mastering_display_info(float x0, float y0,
                                         float x1, float y1,
                                         float x2, float y2,
                                         float xw, float yw,
                                         double Lmin, double Lmax)
{
  if ((x0 < 0.0f) || (y0 < 0.0f) || (x1 < 0.0f) || (y1 < 0.0f) ||
      (x2 < 0.0f) || (y2 < 0.0f) || (xw < 0.0f) || (y1 < 0.0f) ||
      (Lmin <= 0.0f) || (Lmax <= 0.0f))
    state->write_dmon = false;
  else
    { 
      state->write_dmon = true;
      x0 = (x0 > 1.0f)?1.0f:x0;  y0 = (y0 > 1.0f)?1.0f:y0;
      x1 = (x1 > 1.0f)?1.0f:x1;  y1 = (y1 > 1.0f)?1.0f:y1;
      x2 = (x2 > 1.0f)?1.0f:x2;  y2 = (y2 > 1.0f)?1.0f:y2;
      xw = (xw > 1.0f)?1.0f:xw;  yw = (yw > 1.0f)?1.0f:yw;
      state->dmon_chromaticities[0] = (kdu_uint16)(0.5f + x0*(1.0f/0.00002f));
      state->dmon_chromaticities[1] = (kdu_uint16)(0.5f + y0*(1.0f/0.00002f));
      state->dmon_chromaticities[2] = (kdu_uint16)(0.5f + x1*(1.0f/0.00002f));
      state->dmon_chromaticities[3] = (kdu_uint16)(0.5f + y1*(1.0f/0.00002f));
      state->dmon_chromaticities[4] = (kdu_uint16)(0.5f + x2*(1.0f/0.00002f));
      state->dmon_chromaticities[5] = (kdu_uint16)(0.5f + y2*(1.0f/0.00002f));
      state->dmon_chromaticities[6] = (kdu_uint16)(0.5f + xw*(1.0f/0.00002f));
      state->dmon_chromaticities[7] = (kdu_uint16)(0.5f + yw*(1.0f/0.00002f));
      Lmin *= 1.0/0.0001;  Lmax *= 1.0/0.0001;
      if (Lmin < 1.0) Lmin = 1.0;
      if (Lmax < 1.0) Lmax = 1.0;
      if (Lmin > (double)0xFFFFFFFF) Lmin = (double)0xFFFFFFFF;
      if (Lmax > (double)0xFFFFFFFF) Lmax = (double)0xFFFFFFFF;
      state->dmon_luminances[0] = (kdu_uint32)(0.5 + Lmin);
      state->dmon_luminances[1] = (kdu_uint32)(0.5 + Lmax);
      if (state->dmon_luminances[0] == 0)
        state->dmon_luminances[0] = 0xFFFFFFFF; // Wrap-around
      if (state->dmon_luminances[1] == 0)
        state->dmon_luminances[1] = 0xFFFFFFFF; // Wrap-around
      if (state->dmon_luminances[1] < state->dmon_luminances[0])
        state->dmon_luminances[1] = state->dmon_luminances[0];
    }
}

/*****************************************************************************/
/*                            jpb_target::close                              */
/*****************************************************************************/

bool jpb_target::close()
{
  if (state == NULL)
    return false;
  bool result = !state->image_open;
  delete state;
  state = NULL;
  return result;
}

/*****************************************************************************/
/*                          jpb_target::open_image                           */
/*****************************************************************************/

void jpb_target::open_image()
{
  if ((state == NULL) || state->image_open)
    { KDU_ERROR_DEV(e,6); e <<
      KDU_TXT("Attempting to invoke `jpb_target:open_image' without "
              "first closing an existing open image.");
    }
  state->image_open = true;
  state->field_lengths[state->field_idx] = 0;
  state->rewrite_pos = -1;
}

/*****************************************************************************/
/*                          jpb_target::close_image                           */
/*****************************************************************************/

void jpb_target::close_image(kdu_codestream codestream)
{
  if ((state == NULL) || (!state->image_open) ||
      (state->field_lengths[state->field_idx] <= 0))
    { KDU_ERROR_DEV(e,7); e <<
      KDU_TXT("Attempting to invoke `jpb_target::close_image' without "
              "first writing some content to an open image.");
    }
  if (codestream.exists())
    { // Check profile consistency
      kdu_params *siz = codestream.access_siz();
      int profile = 0;
      int max_bitrate = 0;
      siz->get(Sprofile,0,0,profile);
      if ((profile == Sprofile_CINEMA2K) || (profile == Sprofile_CINEMA4K))
        { 
          max_bitrate = 244000000;
        }
      else if (profile == Sprofile_BROADCAST)
        { 
          int level = 0;
          siz->get(Sbroadcast,0,0,level);
          if ((level >= 1) && (level <= 3))
            max_bitrate = 200000000;
          else if (level == 4)
            max_bitrate = 400000000;
          else if (level == 5)
            max_bitrate = 800000000;
          else if (level == 6)
            max_bitrate = 1600000000;
        }
      else
        { KDU_ERROR(e,8); e <<
          KDU_TXT("Codestreams embedded within an elementary broadcast "
                  "stream must have a BROADCAST or DIGITAL CINEMA profile.");
        }
      if (state->max_bitrate > max_bitrate)
        { KDU_ERROR(e,9); e <<
          KDU_TXT("Profile of codestream being embedded in an elementary "
                  "broadcast stream is not compatible with the maximum "
                  "bit-rate declared in the stream's `BRAT' box.  Suspect "
                  "you are encoding to a bit-rate higher than that supported "
                  "by the profile.  The maximum bit-rate for the codestream's "
                  "profile is ") << max_bitrate / 1000000 << "Mbit/s.";
        }
    }
  state->field_idx++;
  state->image_open = false;
  if (state->field_idx < state->num_fields)
    return; // Still need more fields to complete the frame
  
  // If we get here, we need to write out the elementary broadcast stream
  // super-box together with all relevant sub-boxes.
  int f; // Field index
  kdu_long elsm_length = 0; // Figure out size of ELSM box contents first
  elsm_length += 12; // Account for FRAT box
  elsm_length += 20; // Account for BRAT box
  if (state->num_fields > 1)
    elsm_length += 10; // Account for the FIEL box
  elsm_length += 12; // Account for TCOD box
  elsm_length += 9; // Account for BCOL box
  if (state->write_dmon)
    elsm_length += 32; // Account for the DMON box
  for (f=0; f < state->num_fields; f++)
    elsm_length += 8 + state->field_lengths[f]; // Contiguous codestream box
  
  jp2_output_box elsm;
  elsm.open(state->tgt,jpb_elementary_stream_4cc);
  elsm.set_target_size(elsm_length);
  
  jp2_output_box frat;
  frat.open(&elsm,jpb_frame_rate_4cc);
  frat.write((kdu_uint16)(state->frame_duration));
  frat.write((kdu_uint16)(state->timescale));
  frat.close();
  assert(frat.get_box_length() == 12);
  
  jp2_output_box brat;
  brat.open(&elsm,jpb_max_bitrate_4cc);
  brat.write((kdu_uint32)(state->max_bitrate));
  brat.write((kdu_uint32)(state->field_lengths[0]));
  if (state->num_fields == 1)
    brat.write((kdu_uint32) 0);
  else
    brat.write((kdu_uint32)(state->field_lengths[1]));
  brat.close();
  assert(brat.get_box_length() == 20);
  
  if (state->num_fields > 1)
    { 
      jp2_output_box fiel;
      fiel.open(&elsm,jpb_field_coding_4cc);
      fiel.write((kdu_byte) state->num_fields);
      kdu_byte code = (state->field_order == KDU_FIELDS_TOP_FIRST)?1:6;
      fiel.write(code);
      fiel.close();
      assert(fiel.get_box_length() == 10);
    }
  
  kdu_uint32 timecode = state->get_timecode();
  jp2_output_box tcod;
  tcod.open(&elsm,jpb_time_code_4cc);
  tcod.write(timecode);
  tcod.close();
  assert(tcod.get_box_length() == 12);
  state->last_tc = timecode;
  
  jp2_output_box bcol;
  bcol.open(&elsm,jpb_colour_4cc);
  bcol.write(state->colour_space);
  bcol.close();
  assert(bcol.get_box_length() == 9);
  
  if (state->write_dmon)
    { 
      jp2_output_box dmon;
      dmon.open(&elsm,jpb_mastering_display_4cc);
      for (int c=0; c < 8; c++)
        dmon.write(state->dmon_chromaticities[c]);
      for (int l=0; l < 2; l++)
        dmon.write(state->dmon_luminances[l]);
      dmon.close();
      assert(dmon.get_box_length() == 32);
    }
  
  for (f=0; f < state->num_fields; f++)
    { // Write contiguous codestream box
      elsm.write((kdu_uint32)(8+state->field_lengths[f]));
      elsm.write(jp2_codestream_4cc);
      elsm.write(state->fields[f],state->field_lengths[f]);
    }
  
  elsm.close();
  
  // Advance to the next frame
  state->field_idx = 0;
  state->tc_remainder += state->frame_duration;
  state->tc_frame_counter++;
  while (state->tc_frame_counter == state->nominal_fps)
    { // Advance the second count; might also need to do timecode frame drops
      state->tc_frame_counter = 0;
      state->tc_seconds++;
      if (state->drop_interval > 0)
        { // May need to drop frames from the frame count
          if ((state->tc_seconds % 3600) == 0)
            { // Have to reset drop-frame generation machinery each hour for
              // conformance with NTSC drop-frame procedure
              state->tc_remainder = 0;
              state->tc_frame_counter = 0;
            }
          else
            { 
              state->tc_remainder -= state->timescale;
              if ((state->tc_remainder > 0) &&
                  ((state->tc_seconds % state->drop_interval) == 0))
                state->tc_frame_counter = state->drop_count;
            }
        }
      else if ((state->nominal_fps == 1) &&
               (state->tc_remainder >= state->timescale))
        { // Frame-rate must be so low that we need to skip multiple
          // seconds
          int skip_seconds = state->tc_remainder / state->timescale;
          state->tc_remainder -= skip_seconds * state->timescale;
          state->tc_seconds += skip_seconds;
        }
    }
}

/*****************************************************************************/
/*                       jpb_target::get_next_timecode                       */
/*****************************************************************************/

kdu_uint32 jpb_target::get_next_timecode() const
{ 
  return (state == NULL)?0:state->get_timecode();
}

/*****************************************************************************/
/*                       jpb_target::get_last_timecode                       */
/*****************************************************************************/

kdu_uint32 jpb_target::get_last_timecode() const
{ 
  if ((state == NULL) || (state->last_tc == 0xFFFFFFFF))
    return get_next_timecode();
  return state->last_tc;
}

/*****************************************************************************/
/*                         jpb_target::start_rewrite                         */
/*****************************************************************************/

bool jpb_target::start_rewrite(kdu_long backtrack)
{
  if ((state->rewrite_pos >= 0) || (!state->image_open) ||
      (backtrack < 0) ||
      (backtrack > (kdu_long)(state->field_lengths[state->field_idx])))
    return false;
  state->rewrite_pos = state->field_lengths[state->field_idx]-(int)backtrack;
  return true;
}

/*****************************************************************************/
/*                          jpb_target::end_rewrite                          */
/*****************************************************************************/

bool jpb_target::end_rewrite()
{
  if (state->rewrite_pos < 0)
    return false;
  state->rewrite_pos = -1;
  return true;
}

/*****************************************************************************/
/*                             jpb_target::write                             */
/*****************************************************************************/

bool jpb_target::write(const kdu_byte *buf, int num_bytes)
{
  if ((state == NULL) || !state->image_open)
    { 
      assert(0);
      return false;
    }
  assert(state->field_idx < state->num_fields);
  int pos = state->field_lengths[state->field_idx];
  bool result = true;
  if (state->rewrite_pos < 0)
    { 
      if ((pos + num_bytes) > state->max_codestream_length)
        { KDU_ERROR(e,10); e <<
          KDU_TXT("Attempting to write a codestream that exceeds the "
                  "maximum allowable size for any field/frame for the open "
                  "elementary broadcast stream.  Perhaps you have included "
                  "informational marker segments (for random access) in the "
                  "codestream but have failed to allow for their size in the "
                  "specified rate limit supplied to `kdu_codestream::flush'.  "
                  "Rate limitations are applied to all codestream content and "
                  "all non-optional headers, but PLT marker segments are not "
                  "automatically accounted for ahead of time, partly because "
                  "some codestream generation orders may not allow precise "
                  "determination of the size of these headers.");
        }
      state->field_lengths[state->field_idx] = pos + num_bytes;
    }
  else
    { 
      if ((state->rewrite_pos+num_bytes) > pos)
        { 
          result = false;
          num_bytes = pos - state->rewrite_pos;
        }
      pos = state->rewrite_pos;
      state->rewrite_pos += num_bytes;
    }
  if (num_bytes > 0)
    memcpy(state->fields[state->field_idx]+pos,buf,(size_t) num_bytes);
  return result;
}


/* ========================================================================= */
/*                                jb_source                                  */
/* ========================================================================= */

/*****************************************************************************/
/*                          jb_source::parse_frame                           */
/*****************************************************************************/

kdu_long
  jb_source::parse_frame(kdu_long pos, int &timescale, int &duration,
                         kdu_byte &colour, kdu_field_order &fields,
                         kdu_uint32 &timecode, kdu_long &field_box_location,
                         kdu_long &field_location, kdu_long &field_length)
{
  jp2_locator loc; loc.set_file_pos(pos);
  jp2_input_box elsm;
  elsm.open(src,loc);
  if (elsm.get_box_type() != jpb_elementary_stream_4cc)
    return 0;
  kdu_long next_elsm_pos = pos + elsm.get_box_bytes();
  
  bool have_frat=false;
  kdu_uint16 frat_duration=0, frat_timescale=0;
  bool have_brat=false;
  kdu_uint32 brat_maxbr=0, brat_size0=0, brat_size1=0;
  bool have_fiel=false; // Field coding box is optional
  kdu_byte fiel_num=1, fiel_order=0;
  bool have_tcod=false;
  bool have_bcol=false;
  bool have_jp2c=false;
  this->have_dmon = false;
  jp2_input_box sub;
  while (sub.open(&elsm) && !have_jp2c)
    { 
      if (sub.get_box_type() == jpb_frame_rate_4cc)
        { 
          if (have_frat)
            { KDU_ERROR(e,11); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `FRAT' (frame-rate) sub-boxes.");
            }
          have_frat = true;
          if (!(sub.read(frat_duration) && sub.read(frat_timescale)))
            { KDU_ERROR(e,12); e <<
              KDU_TXT("Invalid `FRAT' (frame-rate) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jpb_max_bitrate_4cc)
        { 
          if (have_brat)
            { KDU_ERROR(e,13); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `BRAT' (max bit-rate) sub-boxes.");
            }
          have_brat = true;
          if (!(sub.read(brat_maxbr) && sub.read(brat_size0) &&
                sub.read(brat_size1)))
            { KDU_ERROR(e,14); e <<
              KDU_TXT("Invalid `BRAT' (max bit-rate) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jpb_field_coding_4cc)
        { 
          if (have_fiel)
            { KDU_ERROR(e,15); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `FIEL' (field coding) sub-boxes.");
            }
          have_fiel = true;
          if (!(sub.read(fiel_num) && sub.read(fiel_order) &&
                ((fiel_num == 1) ||
                 ((fiel_num == 2) && ((fiel_order == 0) || (fiel_order == 1) ||
                                      (fiel_order == 6))))))
            { KDU_ERROR(e,16); e <<
              KDU_TXT("Invalid `BRAT' (max bit-rate) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jpb_time_code_4cc)
        { 
          if (have_tcod)
            { KDU_ERROR(e,17); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `TCOD' (timecode) sub-boxes.");
            }
          have_tcod = true;
          if (!sub.read(timecode))
            { KDU_ERROR(e,18); e <<
              KDU_TXT("Invalid `TCOD' (timecode) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jpb_colour_4cc)
        { 
          if (have_bcol)
            { KDU_ERROR(e,19); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `BCOL' (broadcast colour) sub-boxes.");
            }
          have_bcol = true;
          if (!sub.read(colour))
            { KDU_ERROR(e,20); e <<
              KDU_TXT("Invalid `BCOL' (broadcast colour) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jpb_mastering_display_4cc)
        { 
          if (this->have_dmon)
            { KDU_ERROR(e,0x10071501); e <<
              KDU_TXT("Elemetary broadcast stream super-box contains "
                      "multiple `DMON' (mastering display) sub-boxes.");
            }
          this->have_dmon = true;
          if (!(sub.read(dmon_chromaticities[0]) &&
                sub.read(dmon_chromaticities[1]) &&
                sub.read(dmon_chromaticities[2]) &&
                sub.read(dmon_chromaticities[3]) &&
                sub.read(dmon_chromaticities[4]) &&
                sub.read(dmon_chromaticities[5]) &&
                sub.read(dmon_chromaticities[6]) &&
                sub.read(dmon_chromaticities[7]) &&
                sub.read(dmon_luminances[0]) &&
                sub.read(dmon_luminances[1])))
            { KDU_ERROR(e,0x10071502); e <<
              KDU_TXT("Invalid `DMON' (mastering display) sub-box encountered "
                      "while parsing elementary broadcast stream.");
            }
        }
      else if (sub.get_box_type() == jp2_codestream_4cc)
        { 
          have_jp2c = true;
          field_box_location = sub.get_locator().get_file_pos();
          int hdr_len = sub.get_box_header_length();
          field_location = field_box_location + hdr_len;
          field_length = sub.get_remaining_bytes();
          if (jp2c_capabilities == 0)
            jp2c_capabilities = sub.get_capabilities();
        }
      sub.close();
    }
  
  // Now check to see that we really do have all required frame metadata
  if ((!(have_bcol && have_tcod && have_frat && have_brat && have_jp2c)) ||
      ((fiel_num > 1) && ((field_location+field_length) >= next_elsm_pos)))
    { KDU_ERROR(e,21); e <<
      KDU_TXT("Encountered invalid `ELSM' (elementary broadcast stream) "
              "super-box in source file; one or more required sub-boxes "
              "are missing.");
    }
  
  // Return with the information recovered
  if ((frat_duration == 0) || (frat_timescale == 0))
    frat_timescale = frat_duration = 0;
  timescale = frat_timescale;
  duration = frat_duration;
  if (fiel_num < 2)
    fields = KDU_FIELDS_NONE;
  else if (fiel_order == 0)
    fields = KDU_FIELDS_UNKNOWN;
  else if (fiel_order == 1)
    fields = KDU_FIELDS_TOP_FIRST;
  else
    fields = KDU_FIELDS_TOP_SECOND;
  return next_elsm_pos;
}

/*****************************************************************************/
/*                          jb_source::open_frame                            */
/*****************************************************************************/

bool jb_source::open_frame(int idx)
{
  if ((idx < 0) || ((num_frames >= 0) && (idx >= num_frames)))
    return false;
  int timescale, duration;
  kdu_byte colour;
  kdu_field_order fields;
  kdu_uint32 timecode;
  kdu_long field_box_location, field_location, field_length;
  while (idx > last_frame_idx)
    { // Open frames sequentially until the desired one is encountered
      kdu_long next_pos =
        parse_frame(next_elsm_pos,timescale,duration,colour,fields,
                    timecode,field_box_location,field_location,field_length);
      if (next_pos == 0)
        { // Reached the end of the file
          num_frames = last_frame_idx+1;
          return false;
        }
      open_frame_idx = last_frame_idx+1;
      if (timescale == 0)
        { 
          open_timescale = last_timescale;
          open_period = 0;
          open_instant = last_instant;
        }
      else
        { 
          open_timescale = timescale;
          open_period = duration;
          open_instant = last_instant + last_period;
          if ((open_instant != 0) && (timescale != last_timescale))
            { 
              assert(last_timescale != 0);
              open_instant =
                (open_instant*timescale+(last_timescale>>1)) / last_timescale;
              kdu_long min_instant =
                (last_instant*timescale+last_timescale-1) / last_timescale;
              if (open_instant < min_instant)
                open_instant = min_instant;
            }
        }
      open_timecode = timecode;
      open_field_order = fields;
      open_num_fields = (fields == KDU_FIELDS_NONE)?1:2;
      open_colour_space = colour;
      first_field_box_location.set_file_pos(field_box_location);
      first_field_contents_location.set_file_pos(field_location);
      first_field_contents_length = field_length;
      open_end_pos = next_pos;
      
      // Update the `last_...' members
      last_frame_idx = open_frame_idx;
      last_elsm_pos = next_elsm_pos;
      next_elsm_pos = next_pos;
      last_timescale = open_timescale;
      last_period = open_period;
      last_instant = open_instant;
      
      // Finish by recording the new frame's information
      if (last_block == NULL)
        last_block = frame_blocks = new jb_frame_block;
      int rel_idx = last_frame_idx - last_block->first_frame_idx;
      assert((rel_idx >= 0) && (rel_idx <= JB_BLOCK_FRAMES));
      if (rel_idx == JB_BLOCK_FRAMES)
        { 
          last_block = last_block->next = new jb_frame_block;
          last_block->first_frame_idx = last_frame_idx;
          rel_idx = 0;
        }
      assert(rel_idx == last_block->block_frames);
      last_block->block_frames++;
      last_block->locations[rel_idx] = last_elsm_pos;
      last_block->instants[rel_idx] = open_instant;
    }
  if (idx == open_frame_idx)
    return true;
  
  // If we get here, `idx' must refer to a frame that has already been
  // parsed.  We can get its location and starting frame instant from the
  // `frame_blocks' list.
  jb_frame_block *scan_block = backtrack_block;
  if ((scan_block == NULL) || (idx < scan_block->first_frame_idx) ||
      (idx >= last_block->first_frame_idx))
    scan_block = last_block; // Try this one on for size
  if (idx < scan_block->first_frame_idx)
    scan_block = frame_blocks;
  int rel_idx = idx - scan_block->first_frame_idx;
  while (rel_idx >= JB_BLOCK_FRAMES)
    { 
      assert(scan_block->block_frames == JB_BLOCK_FRAMES);
      scan_block = scan_block->next;
      rel_idx -= JB_BLOCK_FRAMES;
    }
  assert((rel_idx >= 0) && (rel_idx < JB_BLOCK_FRAMES));
  backtrack_block = scan_block; // So we can use the same block next time
  kdu_long pos = scan_block->locations[rel_idx];
  kdu_long next_pos =
    parse_frame(pos,timescale,duration,colour,fields,timecode,
                field_box_location,field_location,field_length);
  if (next_pos == 0)
    { KDU_ERROR_DEV(e,22); e <<
      KDU_TXT("Unable to seek to previously parsed frame within elementary "
              "broadcast stream.  Most likely cause is a lack of support "
              "for seeking within the file source.");
    }
  
  open_frame_idx = idx;
  open_timecode = timecode;
  open_field_order = fields;
  open_num_fields = (fields == KDU_FIELDS_NONE)?1:2;
  open_colour_space = colour;
  first_field_box_location.set_file_pos(field_box_location);
  first_field_contents_location.set_file_pos(field_location);
  first_field_contents_length = field_length;
  open_end_pos = next_pos;
  return true;
}



/* ========================================================================= */
/*                                jpb_source                                 */
/* ========================================================================= */

/*****************************************************************************/
/*                             jpb_source::open                              */
/*****************************************************************************/

int jpb_source::open(jp2_family_src *src, bool return_if_incompatible)
{
  if (state != NULL)
    close();
  state = new jb_source;
  state->src = src;
  if (state->open_frame(0))
    return 1;
  if (!return_if_incompatible)
    { KDU_ERROR(e,23); e <<
      KDU_TXT("Source file does not appear to contain an elementary "
              "broadcast stream.  First box was not an `ELSM' super-box.");
    }
  return -1;
}

/*****************************************************************************/
/*                             jpb_source::close                             */
/*****************************************************************************/

bool jpb_source::close()
{
  if (state == NULL)
    return false;
  delete state;
  return true;
}

/*****************************************************************************/
/*                        jpb_source::get_ultimate_src                       */
/*****************************************************************************/

jp2_family_src *jpb_source::get_ultimate_src()
{
  if (state == NULL)
    return NULL;
  return state->src;
}

/*****************************************************************************/
/*                         jpb_source::get_frame_space                       */
/*****************************************************************************/

kdu_byte jpb_source::get_frame_space() const
{
  if (state == NULL)
    return JPB_UNKNOWN_SPACE;
  else
    return state->open_colour_space;
}

/*****************************************************************************/
/*                   jpb_source::get_mastering_display_info                  */
/*****************************************************************************/

bool
  jpb_source::get_mastering_display_info(float &x0, float &y0,
                                         float &x1, float &y1,
                                         float &x2, float &y2,
                                         float &xw, float &yw,
                                         double &Lmin, double &Lmax)
{
  const kdu_uint16 default_chromaticities[8] =
    { 0x3a98, 0x7530, 0x1d4c, 0x0bb8, 0x0c80, 0x4074, 0x3d13, 0x4042};
  const kdu_uint32 default_luminances[2] = { 0x00000367, 0x000f4240};
  bool result = state->have_dmon;
  const kdu_uint16 *chroma = default_chromaticities;
  const kdu_uint32 *luma = default_luminances;
  if (result)
    { 
      chroma = state->dmon_chromaticities;
      luma = state->dmon_luminances;
    }
  x0 = 0.00002f * chroma[0];  y0 = 0.00002f * chroma[1];
  x1 = 0.00002f * chroma[2];  y1 = 0.00002f * chroma[3];
  x2 = 0.00002f * chroma[4];  y2 = 0.00002f * chroma[5];
  xw = 0.00002f * chroma[6];  yw = 0.00002f * chroma[7];
  Lmin = 0.0001 * luma[0];    Lmax = 0.0001 * luma[1];
  return result;
}

/*****************************************************************************/
/*                       jpb_source::get_frame_instant                       */
/*****************************************************************************/

kdu_long jpb_source::get_frame_instant()
{
  if (state == NULL)
    return 0;
  return state->open_instant;
}

/*****************************************************************************/
/*                        jpb_source::get_frame_period                       */
/*****************************************************************************/

kdu_long jpb_source::get_frame_period()
{
  if ((state == NULL) || (state->open_frame_idx < 0))
    return 0;
  return state->open_period;
}

/*****************************************************************************/
/*                       jpb_source::get_frame_timecode                      */
/*****************************************************************************/

kdu_uint32 jpb_source::get_frame_timecode()
{
  if ((state == NULL) || (state->open_frame_idx < 0))
    return 0xFFFFFFFF;
  return state->open_timecode;
}

/*****************************************************************************/
/*                          jpb_source::open_stream                          */
/*****************************************************************************/

int jpb_source::open_stream(int field_idx, jp2_input_box *input_box)
{
  if (state == NULL)
    return -1;
  if ((state->open_frame_idx < 0) ||
      (field_idx < 0) || (field_idx >= state->open_num_fields))
    return -1;
  if (field_idx == 0)
    { 
      if (!input_box->open_as(jp2_codestream_4cc,state->src,
                              state->first_field_box_location,
                              state->first_field_contents_location,
                              state->first_field_contents_length))
        return -1;
    }
  else
    { 
      jp2_locator loc = state->first_field_contents_location;
      loc.set_file_pos(loc.get_file_pos()+state->first_field_contents_length);
      if (!input_box->open(state->src,loc))
        return -1;
      while (input_box->get_box_type() != jp2_codestream_4cc)
        { 
          if (input_box->get_locator().get_file_pos() >= state->open_end_pos)
            return -1;
          input_box->close();
          if (!input_box->open_next())
            return -1;
        }
    }
  return state->open_frame_idx;
}

/*****************************************************************************/
/*                          jpb_source::get_timescale                        */
/*****************************************************************************/

kdu_uint32 jpb_source::get_timescale()
{
  if (state == NULL)
    return 0;
  return (kdu_uint32) state->open_timescale;
}

/*****************************************************************************/
/*                         jpb_source::get_field_order                       */
/*****************************************************************************/

kdu_field_order jpb_source::get_field_order()
{
  if (state == NULL)
    return KDU_FIELDS_NONE;
  return state->open_field_order;
}

/*****************************************************************************/
/*                          jpb_source::set_field_mode                       */
/*****************************************************************************/

void jpb_source::set_field_mode(int which)
{
  if (which < 0)
    which = 0;
  else if (which > 2)
    which = 2;
  if (state != NULL)
    state->field_mode = which;
}

/*****************************************************************************/
/*                          jpb_source::seek_to_frame                        */
/*****************************************************************************/

bool jpb_source::seek_to_frame(int frame_idx)
{
  if (state == NULL)
    return false;
  if (state->image_box.exists())
    { KDU_ERROR_DEV(e,24); e <<
      KDU_TXT("The `jpb_source::seek_to_frame' function may not be called "
              "until any image opened with `open_image' has been closed.");
    }
  if (!state->open_frame(frame_idx))
    return false;
  state->field_idx = 0;
  return true;
}

/*****************************************************************************/
/*                            jpb_source::open_image                         */
/*****************************************************************************/

int jpb_source::open_image()
{
  if (state == NULL)
    return -1;
  if (state->image_box.exists())
    { KDU_ERROR_DEV(e,25); e <<
      KDU_TXT("Attempting to invoke `jpb_source::open_image' without first "
              "closing a previously opened image.");
    }
  if (state->open_frame_idx >= 0)
    { 
      if ((state->field_mode == 1) && (state->field_idx == 0))
        state->field_idx = 1;
      else if ((state->field_mode == 0) && (state->field_idx == 1))
        state->field_idx = 0;
      if (state->field_idx == 0)
        { 
          if (!state->image_box.open_as(jp2_codestream_4cc,state->src,
                                        state->first_field_box_location,
                                        state->first_field_contents_location,
                                        state->first_field_contents_length))
            return -1;
        }
      else
        { // Opening second field is a bit more tricky
          assert(state->open_num_fields == 2);
          jp2_locator loc = state->first_field_contents_location;
          loc.set_file_pos(loc.get_file_pos() +
                           state->first_field_contents_length);
          if (!state->image_box.open(state->src,loc))
            return -1;
          while (state->image_box.get_box_type() != jp2_codestream_4cc)
            { 
              if (state->image_box.get_locator().get_file_pos() >=
                  state->open_end_pos)
                return -1;
              state->image_box.close();
              if (!state->image_box.open_next())
                return -1;
            }
        }
    }
  return state->open_frame_idx;
}

/*****************************************************************************/
/*                           jpb_source::close_image                         */
/*****************************************************************************/

void jpb_source::close_image()
{
  if ((state == NULL) || !state->image_box)
    return;
  state->image_box.close();
  assert(state->open_frame_idx >= 0);
  state->field_idx++;
  if ((state->field_idx >= state->open_num_fields) ||
      ((state->field_mode != 2) &&
       ((state->field_mode ^ state->field_idx) & 1)))
    { // Jump to next frame
      state->field_idx = 0;
      state->have_dmon = false;
      state->open_frame(state->open_frame_idx+1); // Might not succeed
    }
}

/*****************************************************************************/
/*                        jpb_source::get_capabilities                       */
/*****************************************************************************/

int jpb_source::get_capabilities()
{
  if (state == NULL)
    return 0;
  if (!state->image_box)
    return state->jp2c_capabilities;
  else
    return state->image_box.get_capabilities();
}

/*****************************************************************************/
/*                              jpb_source::read                             */
/*****************************************************************************/

int jpb_source::read(kdu_byte *buf, int num_bytes)
{
  if ((state == NULL) || !state->image_box)
    return 0;
  return state->image_box.read(buf,num_bytes);
}

/*****************************************************************************/
/*                              jpb_source::seek                             */
/*****************************************************************************/

bool jpb_source::seek(kdu_long offset)
{
  if ((state == NULL) || !state->image_box)
    return false;
  return state->image_box.seek(offset);
}

/*****************************************************************************/
/*                             jpb_source::get_pos                           */
/*****************************************************************************/

kdu_long jpb_source::get_pos()
{
  if ((state == NULL) || !state->image_box)
    return -1;
  else
    return state->image_box.get_pos();
}
